// Audio system tests — AudioDevice, StreamReceiverNative, end-to-end playback
//
// Tests that don't need audio hardware run unconditionally.
// Tests that need a real audio device skip gracefully if init fails.
// Tests that need .bik fixtures use MILO_TEST_BIK env var.

#include <gtest/gtest.h>
#include "audio/AudioDevice.h"
#include "platform/StreamReceiver_Native.h"

#include <cmath>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

// ============================================================================
// AudioDevice basics
// ============================================================================

TEST(AudioDevice, Singleton) {
    AudioDevice &a = AudioDevice::GetInstance();
    AudioDevice &b = AudioDevice::GetInstance();
    EXPECT_EQ(&a, &b);
}

TEST(AudioDevice, InitAndTerminate) {
    AudioDevice &dev = AudioDevice::GetInstance();
    if (dev.IsInitialized()) {
        dev.Terminate();
    }
    bool ok = dev.Init(44100);
    if (!ok) {
        GTEST_SKIP() << "No audio device available (headless/CI)";
    }
    EXPECT_TRUE(dev.IsInitialized());
    EXPECT_GT(dev.GetSampleRate(), 0);
    dev.Terminate();
    EXPECT_FALSE(dev.IsInitialized());
}

// ============================================================================
// Simple AudioSource — sine wave generator for testing
// ============================================================================

class SineSource : public AudioSource {
public:
    SineSource(float freq, int sampleRate, int totalFrames)
        : mFreq(freq), mSampleRate(sampleRate), mTotalFrames(totalFrames),
          mFramesRendered(0), mPhase(0.0) {}

    int RenderAudio(float *output, int frameCount) override {
        int remaining = mTotalFrames - mFramesRendered;
        int toRender = std::min(frameCount, remaining);
        double inc = 2.0 * M_PI * mFreq / mSampleRate;
        for (int i = 0; i < toRender; i++) {
            float sample = (float)sin(mPhase) * 0.3f;
            output[i * 2 + 0] = sample; // left
            output[i * 2 + 1] = sample; // right
            mPhase += inc;
        }
        // Zero remaining
        for (int i = toRender; i < frameCount; i++) {
            output[i * 2 + 0] = 0.0f;
            output[i * 2 + 1] = 0.0f;
        }
        mFramesRendered += toRender;
        return frameCount;
    }

    bool IsFinished() const override { return mFramesRendered >= mTotalFrames; }
    int FramesRendered() const { return mFramesRendered; }

private:
    float mFreq;
    int mSampleRate;
    int mTotalFrames;
    int mFramesRendered;
    double mPhase;
};

TEST(AudioDevice, SineSourceRender) {
    // Test the sine source without a device — just call RenderAudio directly
    SineSource src(440.0f, 44100, 4410); // 100ms of 440Hz
    EXPECT_FALSE(src.IsFinished());

    float buf[1024]; // 256 stereo frames
    src.RenderAudio(buf, 256);
    EXPECT_EQ(src.FramesRendered(), 256);
    EXPECT_FALSE(src.IsFinished());

    // First sample should be near 0 (sin(0) = 0)
    EXPECT_NEAR(buf[0], 0.0f, 0.01f);
    // Verify non-silence somewhere in the buffer
    bool foundNonZero = false;
    for (int i = 0; i < 512; i++) {
        if (fabs(buf[i]) > 0.01f) { foundNonZero = true; break; }
    }
    EXPECT_TRUE(foundNonZero);
}

TEST(AudioDevice, SineSourceFinishes) {
    SineSource src(440.0f, 44100, 100); // 100 frames total
    float buf[400]; // 100 stereo frames
    src.RenderAudio(buf, 100);
    EXPECT_TRUE(src.IsFinished());
}

TEST(AudioDevice, MixerAddRemove) {
    AudioDevice &dev = AudioDevice::GetInstance();
    if (!dev.IsInitialized()) {
        bool ok = dev.Init(44100);
        if (!ok) GTEST_SKIP() << "No audio device";
    }

    SineSource src(440.0f, dev.GetSampleRate(), dev.GetSampleRate() / 10);
    dev.AddSource(&src);

    // Let it play briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    dev.RemoveSource(&src);
    // Should not crash
    dev.Terminate();
}

// ============================================================================
// StreamReceiverNative unit tests (no device needed)
// ============================================================================

TEST(StreamReceiverNative, CreateViaFactory) {
    StreamReceiver *rcvr = StreamReceiverNative::Create(4, 44100, false, 0);
    ASSERT_NE(rcvr, nullptr);
    EXPECT_TRUE(rcvr->Ready());
    delete rcvr;
}

TEST(StreamReceiverNative, StartSendAndRender) {
    StreamReceiverNative rcvr(4, false);

    // Create a short PCM buffer (100 samples, 16-bit mono)
    const int numSamples = 100;
    int16_t pcm[numSamples];
    for (int i = 0; i < numSamples; i++) {
        pcm[i] = (int16_t)(sin(2.0 * M_PI * 440.0 * i / 44100.0) * 16000);
    }

    // Feed data via StartSendImpl (same path as the engine)
    rcvr.StartSendImpl((unsigned char *)pcm, numSamples * 2, 0);
    EXPECT_TRUE(rcvr.SendDoneImpl());

    // Simulate play
    rcvr.PlayImpl();

    // Render some audio
    float output[200]; // 100 stereo frames
    int frames = rcvr.RenderAudio(output, 100);
    EXPECT_EQ(frames, 100);

    // Verify non-silence
    bool foundNonZero = false;
    for (int i = 0; i < 200; i++) {
        if (fabs(output[i]) > 0.001f) { foundNonZero = true; break; }
    }
    EXPECT_TRUE(foundNonZero);
}

TEST(StreamReceiverNative, PauseProducesSilence) {
    StreamReceiverNative rcvr(4, false);

    int16_t pcm[100];
    for (int i = 0; i < 100; i++) pcm[i] = 16000;
    rcvr.StartSendImpl((unsigned char *)pcm, 200, 0);
    rcvr.PlayImpl();
    rcvr.PauseImpl(true);

    float output[200];
    rcvr.RenderAudio(output, 100);

    // All silence when paused
    for (int i = 0; i < 200; i++) {
        EXPECT_FLOAT_EQ(output[i], 0.0f);
    }
}

TEST(StreamReceiverNative, VolumeScaling) {
    StreamReceiverNative rcvr(4, false);

    // Constant PCM value
    int16_t pcm[10];
    for (int i = 0; i < 10; i++) pcm[i] = 16384; // 0.5 in float
    rcvr.StartSendImpl((unsigned char *)pcm, 20, 0);
    rcvr.PlayImpl();
    rcvr.SetVolume(0.5f);

    float output[20]; // 10 stereo frames
    rcvr.RenderAudio(output, 10);

    // Expected: 16384/32768 * 0.5 = 0.25 (for center pan, both channels equal)
    float expected = (16384.0f / 32768.0f) * 0.5f;
    EXPECT_NEAR(output[0], expected, 0.01f);
    EXPECT_NEAR(output[1], expected, 0.01f);
}

TEST(StreamReceiverNative, PanLeftRight) {
    StreamReceiverNative rcvr(4, false);

    int16_t pcm[10];
    for (int i = 0; i < 10; i++) pcm[i] = 32767;
    rcvr.StartSendImpl((unsigned char *)pcm, 20, 0);
    rcvr.PlayImpl();
    rcvr.SetPan(-1.0f); // full left

    float output[20];
    rcvr.RenderAudio(output, 10);

    // Left channel should be loud, right should be silent
    EXPECT_GT(fabs(output[0]), 0.5f); // left
    EXPECT_NEAR(output[1], 0.0f, 0.01f); // right silent
}

// ============================================================================
// Bink audio decode test (uses MILO_TEST_BIK fixture)
// ============================================================================

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class BikAudioTest : public ::testing::Test {
protected:
    const char *bikPath = nullptr;
    void SetUp() override {
        bikPath = getenv("MILO_TEST_BIK");
        if (!bikPath || !bikPath[0]) {
            GTEST_SKIP() << "Set MILO_TEST_BIK to run";
        }
    }
};

TEST_F(BikAudioTest, DecodeAndFeedToReceiver) {
    // Open bik, decode audio, feed to StreamReceiverNative, verify playback
    AVFormatContext *fmt = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt, bikPath, nullptr, nullptr), 0);
    ASSERT_EQ(avformat_find_stream_info(fmt, nullptr), 0);

    int audioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) {
        avformat_close_input(&fmt);
        GTEST_SKIP() << "No audio stream in .bik";
    }

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[audioIdx]->codecpar->codec_id);
    ASSERT_NE(codec, nullptr);

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[audioIdx]->codecpar);
    ASSERT_EQ(avcodec_open2(ctx, codec, nullptr), 0);

    int sampleRate = ctx->sample_rate;
    printf("  .bik audio: %d Hz, %d channels\n", sampleRate, ctx->ch_layout.nb_channels);

    // Create receiver
    StreamReceiverNative rcvr(4, false);

    // Decode a few frames and feed to receiver
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int totalSamples = 0;
    int maxFrames = 10;

    while (av_read_frame(fmt, pkt) >= 0 && maxFrames > 0) {
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }

        avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(ctx, frame) == 0) {
            // Convert to 16-bit PCM
            std::vector<int16_t> pcm(frame->nb_samples);
            AVSampleFormat sfmt = ctx->sample_fmt;
            if (sfmt == AV_SAMPLE_FMT_FLTP || sfmt == AV_SAMPLE_FMT_FLT) {
                const float *src = (const float *)frame->data[0];
                for (int i = 0; i < frame->nb_samples; i++) {
                    float v = src[i] * 32767.0f;
                    pcm[i] = (int16_t)std::max(-32768.0f, std::min(32767.0f, v));
                }
            } else if (sfmt == AV_SAMPLE_FMT_S16 || sfmt == AV_SAMPLE_FMT_S16P) {
                memcpy(pcm.data(), frame->data[0], frame->nb_samples * 2);
            }

            rcvr.StartSendImpl((unsigned char *)pcm.data(), frame->nb_samples * 2, 0);
            totalSamples += frame->nb_samples;
            maxFrames--;
        }
    }

    printf("  Decoded %d samples, fed to StreamReceiverNative\n", totalSamples);
    EXPECT_GT(totalSamples, 0);

    // Now render from receiver and verify non-silence
    rcvr.PlayImpl();
    int renderFrames = std::min(totalSamples, 1024);
    std::vector<float> output(renderFrames * 2);
    rcvr.RenderAudio(output.data(), renderFrames);

    float maxSample = 0;
    for (auto v : output) maxSample = std::max(maxSample, fabs(v));
    printf("  Max rendered sample: %.4f\n", maxSample);
    EXPECT_GT(maxSample, 0.001f) << "Audio should not be silent";

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
}

// ============================================================================
// End-to-end: AudioDevice + StreamReceiverNative with real .bik audio
// ============================================================================

TEST_F(BikAudioTest, PlayBikAudioThroughDevice) {
    AudioDevice &dev = AudioDevice::GetInstance();
    if (!dev.IsInitialized()) {
        if (!dev.Init()) {
            GTEST_SKIP() << "No audio device available";
        }
    }

    // Open bik audio
    AVFormatContext *fmt = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt, bikPath, nullptr, nullptr), 0);
    ASSERT_EQ(avformat_find_stream_info(fmt, nullptr), 0);

    int audioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) {
        avformat_close_input(&fmt);
        GTEST_SKIP() << "No audio in .bik";
    }

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[audioIdx]->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[audioIdx]->codecpar);
    ASSERT_EQ(avcodec_open2(ctx, codec, nullptr), 0);

    // Create and register receiver
    auto *rcvr = new StreamReceiverNative(4, false);
    rcvr->PlayImpl();

    // Decode and feed first ~0.5s
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int totalSamples = 0;
    int targetSamples = ctx->sample_rate / 2; // 0.5s

    while (av_read_frame(fmt, pkt) >= 0 && totalSamples < targetSamples) {
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }
        avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(ctx, frame) == 0 && totalSamples < targetSamples) {
            std::vector<int16_t> pcm(frame->nb_samples);
            AVSampleFormat sfmt = ctx->sample_fmt;
            if (sfmt == AV_SAMPLE_FMT_FLTP || sfmt == AV_SAMPLE_FMT_FLT) {
                const float *src = (const float *)frame->data[0];
                for (int i = 0; i < frame->nb_samples; i++) {
                    float v = src[i] * 32767.0f;
                    pcm[i] = (int16_t)std::max(-32768.0f, std::min(32767.0f, v));
                }
            } else {
                memcpy(pcm.data(), frame->data[0], frame->nb_samples * 2);
            }
            rcvr->StartSendImpl((unsigned char *)pcm.data(), frame->nb_samples * 2, 0);
            totalSamples += frame->nb_samples;
        }
    }

    printf("  Playing %d samples (%.2fs) through AudioDevice\n",
           totalSamples, (float)totalSamples / ctx->sample_rate);

    // Let it play briefly (AudioDevice callback will pull from receiver)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Cleanup
    dev.RemoveSource(rcvr);
    delete rcvr;
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    dev.Terminate();
}
