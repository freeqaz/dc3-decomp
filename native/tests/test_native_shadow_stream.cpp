// Native-shadow regression tests for StandardStream::ConsumeData.
//
// StandardStream.cpp compiles a separate `#ifdef HX_NATIVE` ConsumeData body;
// every decomp ruler measures only the `#else` body (100% matched), so a
// semantic drift in the native body is invisible to objdiff by construction.
// Ground truth is the image: build/373307D9/asm/system/synth/StandardStream.s,
// `.fn "?ConsumeData@StandardStream@@QAAHPAPAXHH@Z"` (82770C10..82770F64).
//
// Each test was watched FAILING against the pre-fix native body before the
// fix landed (see the commit that introduced it).
//
// Harness: a real StandardStream over a NullFile. The "none" extension makes
// TheSynth->NewStreamDecoder return no reader, so nothing polls behind the
// test's back; InitInfo() then builds the receivers through
// StreamReceiver::sFactory, which the fixture points at a capturing
// StreamReceiverNative so the bytes WriteData() delivers can be read back.

#include "test_helpers.h"

#include "platform/StreamReceiver_Native.h"
#include "synth/StandardStream.h"
#include "synth/Stream.h"

#include <cstdint>
#include <vector>

namespace {

// StreamReceiverNative that records every byte WriteData() forwards (native
// WriteData -> StartSendImpl), then lets the real ring buffer advance so
// AvailableWriteBytes() stays honest.
class CapturingReceiver : public StreamReceiverNative {
public:
    CapturingReceiver(int numBuffers, bool slip) : StreamReceiverNative(numBuffers, slip) {}
    void StartSendImpl(unsigned char *data, int size, int targetIdx) override {
        const int16_t *s = reinterpret_cast<const int16_t *>(data);
        mWritten.insert(mWritten.end(), s, s + size / 2);
        mWriteCalls++;
        StreamReceiverNative::StartSendImpl(data, size, targetIdx);
    }
    std::vector<int16_t> mWritten;
    int mWriteCalls = 0;
};

StreamReceiver *CreateCapturingReceiver(int numBuffers, int, bool slip, int) {
    return new CapturingReceiver(numBuffers, slip);
}

class NativeShadowStreamTest : public EngineTestFixture {
protected:
    void SetUp() override {
        mSavedFactory = StreamReceiver::sFactory;
        StreamReceiver::sFactory = CreateCapturingReceiver;
    }
    void TearDown() override { StreamReceiver::sFactory = mSavedFactory; }

    // A stream with `realChannels` decoded channels at 44.1 kHz, no reader.
    // The stream owns (and deletes) the NullFile.
    static StandardStream *MakeStream(int realChannels, bool floatSamples, int virtualChans = 0) {
        StandardStream *s =
            new StandardStream(new NullFile(), 0.0f, 1.0f, "none", false, false, false);
        if (virtualChans)
            s->AddVirtualChannels(virtualChans);
        s->InitInfo(realChannels, 44100, floatSamples, -1);
        return s;
    }

    static CapturingReceiver *Rcvr(StandardStream *s, int i) {
        return static_cast<CapturingReceiver *>(s->GetChannel(i));
    }

    StreamReceiverFactoryFunc *mSavedFactory = nullptr;
};

// One channel of float PCM (the image's mFloatSamples=true input).
struct FloatPcm {
    explicit FloatPcm(int n, float value = 0.25f) : data(n, value) { ptr = data.data(); }
    std::vector<float> data;
    void *ptr;
};

// 44100 Hz: SetJump(ms) -> samples is ms * 44.1.
const float kFromMs = 100.0f; // 4410 samples
const float kToMs = 1000.0f; // 44100 samples
const int kFromSamp = 4410;
const int kToSamp = 44100;

// ---------------------------------------------------------------------------
// Jump cap. The image caps the consume count only when mJumpFromSamples > 0
// (82770D80 lwz r11,0x84(r30) / cmpwi / ble), and then:
//   from <  to (forward):  only while cur < to (82770D98..DA0 bge skips the
//                          cap once cur >= to); cur > from -> 0 (82770DAC);
//                          otherwise min(from - cur).
//   from == to:            no cap at all (.L_82770DB4 ble).
//   from >  to (backward): assert(cur <= from) (0x1CF at 82770DC4), cap.
// The pre-fix native body capped EVERY non-zero, non-end jump to
// from - cur. After an in-memory forward jump DoJump() sets cur = to > from
// and keeps the jump armed, so every later ConsumeData returned 0 and the
// stream stalled -- HamAudio::SetLoop(start_beat, 0.01) in practice mode.
// ---------------------------------------------------------------------------

TEST_F(NativeShadowStreamTest, ForwardJumpDoesNotStallAfterLandingOnTarget) {
    StandardStream *s = MakeStream(1, true);
    s->SetJump(kFromMs, kToMs, nullptr);
    FloatPcm pcm(256);
    // startSamp = to: the reader reports the post-DoJump position.
    int n = s->ConsumeData(&pcm.ptr, 256, kToSamp);
    EXPECT_EQ(n, 256) << "a forward jump caps only while cur < to; at cur == to "
                         "the image consumes normally (82770D9C cmpw cur,to / bge)";
    // And it keeps flowing on the next call.
    n = s->ConsumeData(&pcm.ptr, 256, -1);
    EXPECT_EQ(n, 256) << "stream stalled after a forward jump";
    EXPECT_EQ(Rcvr(s, 0)->mWritten.size(), 512u);
    delete s;
}

TEST_F(NativeShadowStreamTest, ForwardJumpCapsAtFromBeforeReachingIt) {
    StandardStream *s = MakeStream(1, true);
    s->SetJump(kFromMs, kToMs, nullptr);
    FloatPcm pcm(1024);
    int n = s->ConsumeData(&pcm.ptr, 1024, kFromSamp - 100);
    EXPECT_EQ(n, 100) << "before from, a forward jump caps to from - cur";
    delete s;
}

TEST_F(NativeShadowStreamTest, ForwardJumpInsideSkippedWindowConsumesNothing) {
    StandardStream *s = MakeStream(1, true);
    s->SetJump(kFromMs, kToMs, nullptr);
    FloatPcm pcm(256);
    int n = s->ConsumeData(&pcm.ptr, 256, kFromSamp + 10);
    EXPECT_EQ(n, 0) << "from < cur < to: the image forces 0 (82770DAC li r28,0)";
    delete s;
}

TEST_F(NativeShadowStreamTest, JumpToSelfIsNotCapped) {
    StandardStream *s = MakeStream(1, true);
    s->SetJump(kToMs, kToMs, nullptr); // from == to
    FloatPcm pcm(256);
    int n = s->ConsumeData(&pcm.ptr, 256, kToSamp);
    EXPECT_EQ(n, 256) << "from == to takes no cap (.L_82770DB4 ble skips it)";
    delete s;
}

TEST_F(NativeShadowStreamTest, BackwardJumpCapsAtFrom) {
    StandardStream *s = MakeStream(1, true);
    s->SetJump(kToMs, kFromMs, nullptr); // from = 44100 > to = 4410
    FloatPcm pcm(1024);
    int n = s->ConsumeData(&pcm.ptr, 1024, kToSamp - 100);
    EXPECT_EQ(n, 100) << "a backward jump caps to from - cur";
    delete s;
}

TEST_F(NativeShadowStreamTest, EndOfStreamJumpIsNotCapped) {
    StandardStream *s = MakeStream(1, true);
    s->SetJump(Stream::kStreamEndMs, kFromMs, nullptr); // from = kStreamEndSamples
    FloatPcm pcm(256);
    int n = s->ConsumeData(&pcm.ptr, 256, 1000);
    EXPECT_EQ(n, 256) << "from <= 0 takes no cap (82770D88 ble)";
    delete s;
}

// ---------------------------------------------------------------------------
// Sample format. The image converts float PCM only when mFloatSamples is set
// (82770EC4 lbz r11,0xe0(r30) / beq .L_82770F24); otherwise it hands the
// reader's int16 buffer to WriteData unchanged (.L_82770F24 lwzx r4 from the
// pcm[] array). The conversion is x*32767 clamped to [-32767, 32767]
// (82770EF4 fmuls by __real@46fffe00, fsel against +/-32767, fctiwz). The
// pre-fix native body always read the samples as float, so an int16 reader
// (FFmpegAudioReader, which declares floatSamples=false) was reinterpreted.
// ---------------------------------------------------------------------------

TEST_F(NativeShadowStreamTest, Int16SamplesPassThroughUnchanged) {
    StandardStream *s = MakeStream(1, false);
    const int n = 64;
    // Twice the length, zero tail: the pre-fix body reads n FLOATS (4n bytes)
    // from an int16 buffer; keep that read in bounds so it fails, not crashes.
    std::vector<int16_t> pcm(2 * n, 0);
    for (int i = 0; i < n; i++)
        pcm[i] = (int16_t)(i * 300 - 9000);
    void *ptr = pcm.data();
    ASSERT_EQ(s->ConsumeData(&ptr, n, -1), n);
    const std::vector<int16_t> &got = Rcvr(s, 0)->mWritten;
    ASSERT_EQ(got.size(), (size_t)n);
    for (int i = 0; i < n; i++)
        ASSERT_EQ(got[i], pcm[i]) << "int16 sample " << i
                                  << " was rewritten: mFloatSamples=false passes "
                                     "the reader's buffer through (.L_82770F24)";
    delete s;
}

TEST_F(NativeShadowStreamTest, FloatSamplesConvertAndClamp) {
    StandardStream *s = MakeStream(1, true);
    float in[5] = {0.25f, -0.5f, 2.0f, -2.0f, 0.0f};
    void *ptr = in;
    ASSERT_EQ(s->ConsumeData(&ptr, 5, -1), 5);
    const std::vector<int16_t> &got = Rcvr(s, 0)->mWritten;
    ASSERT_EQ(got.size(), 5u);
    EXPECT_EQ(got[0], (int16_t)(0.25f * 32767.0f));
    EXPECT_EQ(got[1], (int16_t)(-0.5f * 32767.0f));
    EXPECT_EQ(got[2], 32767);
    EXPECT_EQ(got[3], -32767) << "the image clamps to -32767, not -32768";
    EXPECT_EQ(got[4], 0);
    delete s;
}

} // namespace
