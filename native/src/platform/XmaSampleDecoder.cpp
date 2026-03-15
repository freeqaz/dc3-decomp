// DC3 Native Port - XMA sample decoder
// Decodes raw Xbox 360 XMA2 compressed audio to 16-bit PCM using FFmpeg.
//
// XMA2 packets are always 2048 bytes. The raw data in SampleData::mData
// is a contiguous array of these packets (sizes confirmed as multiples of 2048).
// FFmpeg's xma2 decoder (wmaprodec.c) handles the packet-level parsing.

#ifdef HX_FFMPEG

#include "platform/XmaSampleDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const int kXmaBlockAlign = 2048;

bool DecodeXMAToPCM(
    const void* xmaData, int xmaSize,
    int numSamples, int sampleRate, int numChannels,
    void** outPCM, int* outPCMSize
) {
    *outPCM = nullptr;
    *outPCMSize = 0;

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    if (!codec) {
        fprintf(stderr, "XmaDecode: XMA2 codec not found in FFmpeg\n");
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    // Suppress "Could not update timestamps for skipped samples" info messages
    av_log_set_level(AV_LOG_ERROR);

    ctx->sample_rate = sampleRate;
    av_channel_layout_default(&ctx->ch_layout, numChannels);
    ctx->block_align = kXmaBlockAlign;
    ctx->bits_per_coded_sample = 16;

    // Construct minimal XMA2 extradata (34 bytes)
    // FFmpeg's xma_decode_init reads:
    //   offset 0 (2 bytes): NumStreams
    //   offset 2 (4 bytes): ChannelMask
    // Rest can be zero for basic decoding.
    uint8_t extradata[34];
    memset(extradata, 0, sizeof(extradata));
    // NumStreams = 1 (little-endian)
    extradata[0] = 1;
    extradata[1] = 0;
    // ChannelMask: mono=0x04 (center), stereo=0x03 (front L+R)
    uint32_t chMask = (numChannels == 1) ? 0x04 : 0x03;
    memcpy(&extradata[2], &chMask, 4);

    ctx->extradata = (uint8_t*)av_malloc(sizeof(extradata) + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(ctx->extradata, extradata, sizeof(extradata));
    memset(ctx->extradata + sizeof(extradata), 0, AV_INPUT_BUFFER_PADDING_SIZE);
    ctx->extradata_size = sizeof(extradata);

    int ret = avcodec_open2(ctx, codec, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "XmaDecode: avcodec_open2 failed: %s\n", errbuf);
        avcodec_free_context(&ctx);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return false;
    }

    // Collect decoded PCM samples
    std::vector<int16_t> pcmOut;
    pcmOut.reserve(numSamples * numChannels);

    const uint8_t* data = (const uint8_t*)xmaData;
    int remaining = xmaSize;

    // Feed XMA packets (2048 bytes each) to the decoder
    while (remaining >= kXmaBlockAlign) {
        pkt->data = const_cast<uint8_t*>(data);
        pkt->size = kXmaBlockAlign;

        ret = avcodec_send_packet(ctx, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            // Skip bad packets
            data += kXmaBlockAlign;
            remaining -= kXmaBlockAlign;
            continue;
        }

        while (avcodec_receive_frame(ctx, frame) == 0) {
            int frameSamples = frame->nb_samples;
            AVSampleFormat fmt = ctx->sample_fmt;

            if (fmt == AV_SAMPLE_FMT_FLTP) {
                // Planar float → interleaved int16
                for (int s = 0; s < frameSamples; s++) {
                    for (int ch = 0; ch < numChannels; ch++) {
                        float val = ((const float*)frame->data[ch])[s] * 32767.0f;
                        if (val > 32767.0f) val = 32767.0f;
                        if (val < -32768.0f) val = -32768.0f;
                        pcmOut.push_back((int16_t)val);
                    }
                }
            } else if (fmt == AV_SAMPLE_FMT_FLT) {
                // Interleaved float → int16
                const float* src = (const float*)frame->data[0];
                for (int s = 0; s < frameSamples * numChannels; s++) {
                    float val = src[s] * 32767.0f;
                    if (val > 32767.0f) val = 32767.0f;
                    if (val < -32768.0f) val = -32768.0f;
                    pcmOut.push_back((int16_t)val);
                }
            } else if (fmt == AV_SAMPLE_FMT_S16P) {
                // Planar int16 → interleaved
                for (int s = 0; s < frameSamples; s++) {
                    for (int ch = 0; ch < numChannels; ch++) {
                        pcmOut.push_back(((const int16_t*)frame->data[ch])[s]);
                    }
                }
            } else if (fmt == AV_SAMPLE_FMT_S16) {
                // Already interleaved int16
                const int16_t* src = (const int16_t*)frame->data[0];
                for (int s = 0; s < frameSamples * numChannels; s++) {
                    pcmOut.push_back(src[s]);
                }
            }
        }

        data += kXmaBlockAlign;
        remaining -= kXmaBlockAlign;
    }

    // Flush the decoder
    avcodec_send_packet(ctx, nullptr);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        int frameSamples = frame->nb_samples;
        AVSampleFormat fmt = ctx->sample_fmt;
        if (fmt == AV_SAMPLE_FMT_FLTP) {
            for (int s = 0; s < frameSamples; s++) {
                for (int ch = 0; ch < numChannels; ch++) {
                    float val = ((const float*)frame->data[ch])[s] * 32767.0f;
                    if (val > 32767.0f) val = 32767.0f;
                    if (val < -32768.0f) val = -32768.0f;
                    pcmOut.push_back((int16_t)val);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S16) {
            const int16_t* src = (const int16_t*)frame->data[0];
            for (int s = 0; s < frameSamples * numChannels; s++) {
                pcmOut.push_back(src[s]);
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);

    if (pcmOut.empty()) {
        return false;
    }

    int pcmBytes = (int)(pcmOut.size() * sizeof(int16_t));
    *outPCM = malloc(pcmBytes);
    memcpy(*outPCM, pcmOut.data(), pcmBytes);
    *outPCMSize = pcmBytes;
    return true;
}

#endif // HX_FFMPEG
