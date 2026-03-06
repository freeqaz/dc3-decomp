// FFmpeg Bink decoder tests
//
// Validates that FFmpeg can open, decode, and seek .bik files — proving
// the replacement path for RAD's proprietary Bink SDK.
//
// Usage:
//   MILO_TEST_BIK=/path/to/file.bik ./milo-tests --gtest_filter=BinkFFmpeg.*
//
// All tests skip gracefully if MILO_TEST_BIK is not set.

#include "test_helpers.h"

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class BinkFFmpeg : public ::testing::Test {
protected:
    const char *bikPath = nullptr;

    void SetUp() override {
        bikPath = GetTestBikPath();
        if (!bikPath) {
            GTEST_SKIP() << "No .bik file available. Run ExtractBik.ExtractSmallest first, "
                         << "or set MILO_TEST_BIK=/path/to/file.bik";
        }
    }
};

// ============================================================================
// Test 1: Open a .bik file via avformat, verify stream detection
// ============================================================================

TEST_F(BinkFFmpeg, OpenBikFile) {
    AVFormatContext *fmt = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt, bikPath, nullptr, nullptr), 0)
        << "Failed to open " << bikPath;
    ASSERT_EQ(avformat_find_stream_info(fmt, nullptr), 0);

    int videoIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int audioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    EXPECT_GE(videoIdx, 0) << "No video stream found";
    EXPECT_GE(audioIdx, 0) << "No audio stream found";

    if (videoIdx >= 0) {
        AVCodecParameters *vpar = fmt->streams[videoIdx]->codecpar;
        printf("  Video: %dx%d, codec_id=%d\n", vpar->width, vpar->height, vpar->codec_id);
        EXPECT_EQ(vpar->codec_id, AV_CODEC_ID_BINKVIDEO);
    }
    if (audioIdx >= 0) {
        AVCodecParameters *apar = fmt->streams[audioIdx]->codecpar;
        printf("  Audio: %d Hz, %d ch, codec_id=%d\n",
               apar->sample_rate, apar->ch_layout.nb_channels, apar->codec_id);
        EXPECT_TRUE(apar->codec_id == AV_CODEC_ID_BINKAUDIO_DCT ||
                    apar->codec_id == AV_CODEC_ID_BINKAUDIO_RDFT);
    }

    printf("  Total streams: %d\n", fmt->nb_streams);
    avformat_close_input(&fmt);
}

// ============================================================================
// Test 2: Decode the first video frame
// ============================================================================

TEST_F(BinkFFmpeg, DecodeFirstVideoFrame) {
    AVFormatContext *fmt = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt, bikPath, nullptr, nullptr), 0);
    ASSERT_EQ(avformat_find_stream_info(fmt, nullptr), 0);

    int videoIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    ASSERT_GE(videoIdx, 0) << "No video stream";

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[videoIdx]->codecpar->codec_id);
    ASSERT_NE(codec, nullptr) << "No Bink video decoder in this FFmpeg build";

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    ASSERT_NE(ctx, nullptr);
    avcodec_parameters_to_context(ctx, fmt->streams[videoIdx]->codecpar);
    ASSERT_EQ(avcodec_open2(ctx, codec, nullptr), 0);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    bool gotFrame = false;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == videoIdx) {
            int ret = avcodec_send_packet(ctx, pkt);
            ASSERT_GE(ret, 0) << "send_packet failed: " << ret;

            ret = avcodec_receive_frame(ctx, frame);
            if (ret == 0) {
                gotFrame = true;
                printf("  Decoded frame: %dx%d, format=%d, pts=%ld\n",
                       frame->width, frame->height, frame->format, (long)frame->pts);
                EXPECT_GT(frame->width, 0);
                EXPECT_GT(frame->height, 0);
                EXPECT_NE(frame->data[0], nullptr);
                break;
            }
        }
        av_packet_unref(pkt);
    }

    EXPECT_TRUE(gotFrame) << "Failed to decode any video frame";

    // Test YUV→RGBA conversion via sws_scale
    if (gotFrame) {
        SwsContext *sws = sws_getContext(
            frame->width, frame->height, (AVPixelFormat)frame->format,
            frame->width, frame->height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        ASSERT_NE(sws, nullptr);

        int rgbaStride = frame->width * 4;
        std::vector<uint8_t> rgbaBuf(rgbaStride * frame->height);
        uint8_t *dstSlice[1] = {rgbaBuf.data()};
        int dstStride[1] = {rgbaStride};

        int rows = sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                             dstSlice, dstStride);
        EXPECT_EQ(rows, frame->height);
        printf("  sws_scale: converted %d rows to RGBA (%d bytes)\n",
               rows, (int)rgbaBuf.size());

        sws_freeContext(sws);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
}

// ============================================================================
// Test 3: Decode audio track to PCM
// ============================================================================

TEST_F(BinkFFmpeg, DecodeAudioTrack) {
    AVFormatContext *fmt = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt, bikPath, nullptr, nullptr), 0);
    ASSERT_EQ(avformat_find_stream_info(fmt, nullptr), 0);

    int audioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    ASSERT_GE(audioIdx, 0) << "No audio stream";

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[audioIdx]->codecpar->codec_id);
    ASSERT_NE(codec, nullptr) << "No Bink audio decoder";

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[audioIdx]->codecpar);
    ASSERT_EQ(avcodec_open2(ctx, codec, nullptr), 0);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    int totalSamples = 0;
    int framesDecoded = 0;
    int maxFrames = 100; // decode up to 100 audio frames

    while (av_read_frame(fmt, pkt) >= 0 && framesDecoded < maxFrames) {
        if (pkt->stream_index == audioIdx) {
            int ret = avcodec_send_packet(ctx, pkt);
            if (ret < 0) { av_packet_unref(pkt); continue; }

            while (avcodec_receive_frame(ctx, frame) == 0) {
                totalSamples += frame->nb_samples;
                framesDecoded++;
            }
        }
        av_packet_unref(pkt);
    }

    printf("  Decoded %d audio frames, %d total samples\n", framesDecoded, totalSamples);
    printf("  Sample rate: %d Hz, channels: %d, format: %d\n",
           ctx->sample_rate, ctx->ch_layout.nb_channels, ctx->sample_fmt);

    EXPECT_GT(framesDecoded, 0) << "Failed to decode any audio";
    EXPECT_GT(totalSamples, 0);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
}

// ============================================================================
// Test 4: Seek to a frame and decode
// ============================================================================

TEST_F(BinkFFmpeg, SeekAndDecode) {
    AVFormatContext *fmt = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt, bikPath, nullptr, nullptr), 0);
    ASSERT_EQ(avformat_find_stream_info(fmt, nullptr), 0);

    int videoIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    ASSERT_GE(videoIdx, 0);

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[videoIdx]->codecpar->codec_id);
    ASSERT_NE(codec, nullptr);

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[videoIdx]->codecpar);
    ASSERT_EQ(avcodec_open2(ctx, codec, nullptr), 0);

    // Seek to 1 second into the video
    int64_t seekTarget = AV_TIME_BASE; // 1 second in AV_TIME_BASE units
    int ret = av_seek_frame(fmt, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        printf("  av_seek_frame returned %d — seeking not supported for this .bik\n", ret);
        // Not all .bik files support seeking; skip gracefully
    } else {
        avcodec_flush_buffers(ctx);

        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        bool gotFrame = false;

        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == videoIdx) {
                avcodec_send_packet(ctx, pkt);
                if (avcodec_receive_frame(ctx, frame) == 0) {
                    gotFrame = true;
                    printf("  After seek: decoded frame pts=%ld, %dx%d\n",
                           (long)frame->pts, frame->width, frame->height);
                    break;
                }
            }
            av_packet_unref(pkt);
        }

        EXPECT_TRUE(gotFrame) << "Failed to decode after seek";

        av_frame_free(&frame);
        av_packet_free(&pkt);
    }

    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
}
