// Audio system tests — AudioDevice, StreamReceiverNative, end-to-end playback
//
// Tests that don't need audio hardware run unconditionally.
// Tests that need a real audio device skip gracefully if init fails.
// Tests that need .bik fixtures use MILO_TEST_BIK env var.

#include "test_helpers.h"
#include <gtest/gtest.h>
#include "audio/AudioDevice.h"
#include "platform/StreamReceiver_Native.h"
#include "platform/ImagePan_Native.h"

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
    if (!getenv("DC3_AUDIO_TESTS"))
        GTEST_SKIP() << "Set DC3_AUDIO_TESTS=1 to enable (audio device contention under ctest -j)";
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
    if (!getenv("DC3_AUDIO_TESTS"))
        GTEST_SKIP() << "Set DC3_AUDIO_TESTS=1 to enable (audio device contention under ctest -j)";
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

// A stream's audio can end before its song does: DC3's Technologic has
// 145.94 s of audio and its MIDI [end] at 146.89 s. On the 360 the voice plays
// zero-filled buffers for as long as the receiver is polled, and the stream's
// clock (bytes played) runs on through them. The native mixer used to drop the
// receiver as soon as the stream was finished -- one whole 0x4000-byte buffer
// past the buffer holding the last byte -- so the count stopped there, song
// time froze at 146.2 s and the song never reached [end] (2026-11-21, on a
// real device; a headless run has no mixer and never showed it).
TEST(StreamReceiverNative, TheMixerPlaysOnThroughSilenceAfterTheStreamFinished) {
    StreamReceiverNative rcvr(4, false);
    rcvr.Play(); // into the mixer; ~StreamReceiverNative takes it out

    const int pcmBytes = 0x5000;
    std::vector<int16_t> pcm(pcmBytes / 2, 1000);
    rcvr.WriteData(pcm.data(), pcmBytes);
    rcvr.EndData();

    // The device: 512-frame callbacks, one main-thread poll after each.
    std::vector<float> mix(2 * 512);
    const unsigned long long finishAt = 0xC000; // (ceil(0x5000 / 0x4000) + 1) buffers
    for (int i = 0; i < 1000 && rcvr.GetBytesPlayed() < finishAt; i++) {
        AudioDevice::GetInstance().MixSources(mix.data(), 512);
        rcvr.Poll();
    }
    ASSERT_GE(rcvr.GetBytesPlayed(), finishAt) << "the stream never reached its finish point";
    rcvr.Poll(); // the finish is counted on the poll after the cursor passes it

    // One more second of callbacks: mono int16, two bytes a frame.
    const unsigned long long before = rcvr.GetBytesPlayed();
    const int callbacks = 86;
    for (int i = 0; i < callbacks; i++) {
        AudioDevice::GetInstance().MixSources(mix.data(), 512);
        rcvr.Poll();
    }
    EXPECT_FALSE(rcvr.IsFinished()) << "the mixer drops a source that says it is finished";
    EXPECT_EQ(rcvr.GetBytesPlayed(), before + (unsigned long long)callbacks * 512 * 2)
        << "the stream's clock stopped after it finished";
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

    // Centre pan is the image's C speaker, folded to each side 3 dB down
    // (ImagePan_Native.h): 16384/32768 * 0.5 * g(-3 dB) on both channels.
    float expected = (16384.0f / 32768.0f) * 0.5f * std::pow(10.0f, -3.0f / 20.0f);
    EXPECT_NEAR(output[0], expected, 1e-4f);
    EXPECT_NEAR(output[1], expected, 1e-4f);
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

    // Hard left is the image's FL at the voice volume: unity, not the old
    // native law's 2x (vol * (1 - pan)), which played every hard-panned stem
    // 6 dB hot. Right is silent.
    EXPECT_NEAR(output[0], 32767.0f / 32768.0f, 1e-4f);
    EXPECT_NEAR(output[1], 0.0f, 1e-6f);
}

// The image's pan law (Voice::UpdateMix), folded to stereo with the game's
// own downmix (Synth360::Poll's DML/DMR): PLAN H-115. Worked here from the
// image: a mono voice at pan p in [-1, 0] is FL*cos(a) + C*sin(a),
// a = (p + 1) * pi/2; C reaches each side at g(-3 dB). Beyond +-1 the ring
// goes to the surrounds, which the downmix brings back at g(-1.2)/g(-6.2).
TEST(StreamReceiverNative, PanFollowsTheImagesRing) {
    const float gC = std::pow(10.0f, -3.0f / 20.0f);
    const float gS = std::pow(10.0f, -1.2f / 20.0f);
    const float gR = std::pow(10.0f, -6.2f / 20.0f);
    const float hp = 1.5707964f;
    struct Case { float pan, left, right; } cases[] = {
        { -1.0f, 1.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f },
        { 0.0f, gC, gC },
        { -0.5f, std::cos(0.5f * hp) + gC * std::sin(0.5f * hp), gC * std::sin(0.5f * hp) },
        { 0.25f, gC * std::cos(0.25f * hp), std::sin(0.25f * hp) + gC * std::cos(0.25f * hp) },
        { -3.0f, gS, gR },        // SL
        { 3.0f, gR, gS },         // SR
        { 7.0f, 1.0f, 0.0f },     // folded: Voice::SetPan, Mod(7 + 4, 8) - 4 = -1
    };
    for (const Case &c : cases) {
        StreamReceiverNative rcvr(4, false);
        int16_t pcm[10];
        for (int i = 0; i < 10; i++) pcm[i] = 16384;
        rcvr.StartSendImpl((unsigned char *)pcm, 20, 0);
        rcvr.PlayImpl();
        rcvr.SetVolume(0.8f);
        rcvr.SetPan(c.pan);
        float output[20];
        rcvr.RenderAudio(output, 10);
        EXPECT_NEAR(output[0], 0.5f * 0.8f * c.left, 1e-5f) << "pan " << c.pan;
        EXPECT_NEAR(output[1], 0.5f * 0.8f * c.right, 1e-5f) << "pan " << c.pan;
    }
}

// A stereo sample keeps its channels apart and scales them by the image's
// cos/sin pair (FL = L*cos(a), FR = R*sin(a)), so at pan 0 both are 3 dB down.
TEST(ImagePanLaw, StereoVoiceIsTheCosSinPair) {
    float l, r;
    ImagePan::Stereo(1.0f, 0.0f, l, r);
    EXPECT_NEAR(l, std::pow(10.0f, -3.0f / 20.0f), 1e-3f);
    EXPECT_NEAR(r, std::pow(10.0f, -3.0f / 20.0f), 1e-3f);
    ImagePan::Stereo(0.5f, -1.0f, l, r);
    EXPECT_NEAR(l, 0.5f, 1e-6f);
    EXPECT_NEAR(r, 0.0f, 1e-6f);
}

// ============================================================================
// FxSend effect chain tests (no device needed)
// ============================================================================

#include "platform/FxSendNative.h"

TEST(FxSendNative, ProcessChainNullSafe) {
    // Should not crash with null head or buffer
    FxSendNative_ProcessChain(nullptr, nullptr, 0, 2);

    float buf[20] = {};
    FxSendNative_ProcessChain(nullptr, buf, 10, 2);
    // Buffer should be unchanged (all zeros)
    for (int i = 0; i < 20; i++) {
        EXPECT_FLOAT_EQ(buf[i], 0.0f);
    }
}

TEST(FxSendNative, EffectSlotInitDestroy) {
    // NativeEffectSlot should handle init/destroy without crashing
    NativeEffectSlot slot;
    EXPECT_EQ(slot.type, NativeEffectSlot::kNone);
    EXPECT_EQ(slot.processor, nullptr);
    slot.Destroy(); // should be safe on empty slot
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
        bikPath = GetTestBikPath();
        if (!bikPath) {
            GTEST_SKIP() << "No .bik file available. Run ExtractBik.ExtractSmallest first, "
                         << "or set MILO_TEST_BIK=/path/to/file.bik";
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
    EXPECT_GT(maxSample, 0.0001f) << "Audio should not be silent";

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
}

// ============================================================================
// End-to-end: AudioDevice + StreamReceiverNative with real .bik audio
// ============================================================================

TEST_F(BikAudioTest, PlayBikAudioThroughDevice) {
    if (!getenv("DC3_AUDIO_TESTS"))
        GTEST_SKIP() << "Set DC3_AUDIO_TESTS=1 to enable (audio device contention under ctest -j)";
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
