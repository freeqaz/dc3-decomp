// A song stream that starts (or jumps) mid-song must decode from there.
//
// Practice starts its song at the section start, and every loop, Strike a
// Pose round and HamAudio::Jump re-positions the stream with
// VorbisReader::Seek -> DoSeek -> DoRawSeek(byte). An encrypted mogg (every
// song) is AES-CTR, so DoRawSeek re-keys the cipher at the seek point by
// writing the block index byte/16 into the first word of the nonce and calling
// ctr_reinit (build/373307D9/asm/system/synth/VorbisReader.s, DoRawSeek).
// tomcrypt's CTR counter is LITTLE-endian bytewise (ctr_encrypt increments
// ctr[0] first), so the word has to land in memory as little-endian bytes. The
// image, big-endian, gets that by storing EndianSwap(byte/16); the same source
// on a little-endian host stores the BIG-endian bytes, the keystream is wrong
// from the first seeked byte, no Ogg page ever syncs, and the reader runs to
// EOF with its whole mSamplesToSkip still owed: no PCM, a frozen mCurrentSamp.
// A seek to byte 0 (every looping MoggClip wrap) is endian-neutral, which is
// why only mid-song seeks were broken.
//
// Harness: two real StandardStreams over the same in-memory song mogg, one
// from 0 and one from a mid-song start. Their receivers are sinks that record
// what WriteData() forwards and never fill, so decode is not throttled by a
// missing audio device. The mid-song stream's output must be the from-0
// stream's output at that offset.

#include "test_helpers.h"

#include "os/BufFile.h"
#include "os/File.h"
#include "audio/AudioDevice.h"
#include "platform/StreamReceiver_Native.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/Tex.h"
#include "synth/StandardStream.h"
#include "synth/Synth.h"

#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <vector>

extern File *NewFile(const char *, int);

namespace {

// Records every sample WriteData() forwards and never passes them on, so the
// ring never fills: AvailableWriteBytes() stays at its full 64 KB.
class SinkReceiver : public StreamReceiverNative {
public:
    SinkReceiver(int numBuffers, bool slip) : StreamReceiverNative(numBuffers, slip) {}
    void StartSendImpl(unsigned char *data, int size, int) override {
        const int16_t *s = reinterpret_cast<const int16_t *>(data);
        mWritten.insert(mWritten.end(), s, s + size / 2);
    }
    std::vector<int16_t> mWritten;
};

StreamReceiver *CreateSinkReceiver(int numBuffers, int, bool slip, int) {
    return new SinkReceiver(numBuffers, slip);
}

const char *kSongMogg = "songs/boyfriend/boyfriend.mogg";

class NativeAudioSeekTest : public EngineTestFixture {
protected:
    void SetUp() override {
        mSavedFactory = StreamReceiver::sFactory;
        StreamReceiver::sFactory = CreateSinkReceiver;
        // v0xE song moggs derive their key through ByteGrinder's DTA
        // functions, which SynthInit registers and the test harness skips.
        TheSynth->Grinder().Init();
        File *f = NewFile(kSongMogg, 2);
        if (!f)
            GTEST_SKIP() << kSongMogg << " not available";
        mMogg.resize(f->Size());
        f->Read(mMogg.data(), mMogg.size());
        delete f;
    }
    void TearDown() override { StreamReceiver::sFactory = mSavedFactory; }

    StandardStream *MakeStream(float startMs) {
        // The stream owns (and deletes) the BufFile; mMogg outlives it.
        return new StandardStream(
            new BufFile(mMogg.data(), mMogg.size()), startMs, 0.0f, "mogg", false, false,
            false
        );
    }

    static SinkReceiver *Sink(StandardStream *s) {
        return static_cast<SinkReceiver *>(s->GetChannel(0));
    }

    // Poll until channel 0 has recorded `samples` samples; false if the
    // stream stops producing first.
    static bool PollUntil(StandardStream *s, size_t samples) {
        size_t last = 0;
        int idle = 0;
        for (int i = 0; i < 20000; i++) {
            s->PollStream();
            if (s->Fail())
                return false;
            if (s->GetNumChannels() == 0)
                continue;
            size_t n = Sink(s)->mWritten.size();
            if (n >= samples)
                return true;
            idle = n == last ? idle + 1 : 0;
            last = n;
            if (idle > 2000)
                return false;
        }
        return false;
    }

    std::vector<unsigned char> mMogg;
    StreamReceiverFactoryFunc *mSavedFactory = nullptr;
};

TEST_F(NativeAudioSeekTest, MidSongStartDecodesTheSongFromThere) {
    const float kStartMs = 5752.352f; // practice's YMCA section start, measured
    const size_t kCompare = 4096;

    StandardStream *whole = MakeStream(0.0f);
    StandardStream *mid = MakeStream(kStartMs);

    // Headers first: the start sample needs the sample rate.
    ASSERT_TRUE(PollUntil(mid, 1) || mid->GetNumChannels() > 0) << "no header";
    const int startSamp = (int)(mid->GetSampleRate() * kStartMs / 1000.0f);
    ASSERT_GT(startSamp, 0);

    ASSERT_TRUE(PollUntil(mid, kCompare))
        << "the mid-song stream produced " << Sink(mid)->mWritten.size()
        << " samples: the seek never decoded (cur stays at "
        << mid->GetBufferAheadTime() << " ms)";
    EXPECT_GT(mid->GetBufferAheadTime(), kStartMs)
        << "the decode position never moved past the start";

    ASSERT_TRUE(PollUntil(whole, startSamp + kCompare));
    const std::vector<int16_t> &a = Sink(whole)->mWritten;
    const std::vector<int16_t> &b = Sink(mid)->mWritten;

    int nonZero = 0;
    int mismatches = 0;
    for (size_t i = 0; i < kCompare; i++) {
        if (b[i] != 0)
            nonZero++;
        if (a[startSamp + i] != b[i])
            mismatches++;
    }
    printf("  channels=%d rate=%d startSamp=%d whole=%zu mid=%zu nonZero=%d mismatches=%d\n",
           mid->GetNumChannels(), mid->GetSampleRate(), startSamp, a.size(), b.size(),
           nonZero, mismatches);
    EXPECT_GT(nonZero, (int)kCompare / 2) << "the seeked audio is silence";
    EXPECT_EQ(mismatches, 0)
        << "the mid-song stream is not the song at " << kStartMs << " ms";

    delete mid;
    delete whole;
}

// With no audio device (headless: MILO_HEADLESS / DC3_NO_AUDIO), nothing
// renders the receivers. StandardStream::UpdateTime already runs song time on
// mTimer alone in that case, but the receivers' rings were never played: once
// full (64 KB = 32768 samples, 743 ms at 44.1 kHz) ConsumeData could hand them
// nothing, so the decode position mCurrentSamp froze one ring past the start
// while song time ran on -- and IsPastStreamJumpPointOfNoReturn() ("decoded
// behind played") read true for the rest of the song: practice queued every
// loop and set none. On the image a device always plays the stream. A
// device-less stream must play its receivers at its own clock, so the decode
// position stays one ring AHEAD of song time, as it does with a device.
TEST_F(NativeAudioSeekTest, WithoutADeviceTheDecodePositionKeepsAheadOfSongTime) {
    if (AudioDevice::GetInstance().IsInitialized())
        GTEST_SKIP() << "an audio device is open; this is the device-less path";
    // Real receivers (a ring that fills), not the sinks.
    StreamReceiver::sFactory = StreamReceiverNative::Create;

    StandardStream *s = MakeStream(0.0f);
    s->Play(); // pumps the header, then pre-fills

    const float kRunMs = 1500.0f; // two rings' worth of 743 ms
    auto t0 = std::chrono::steady_clock::now();
    float elapsed = 0.0f;
    while (elapsed < kRunMs) {
        s->PollStream();
        elapsed = std::chrono::duration<float, std::milli>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
    }
    const float songMs = s->GetTime();
    const float decodedMs = s->GetBufferAheadTime();
    printf("  song time %.1f ms, decode position %.1f ms\n", songMs, decodedMs);
    EXPECT_GT(songMs, kRunMs * 0.9f) << "song time did not run on its clock";
    EXPECT_GT(decodedMs, songMs)
        << "the decode position fell behind song time: nothing played the rings";
    EXPECT_FALSE(s->IsPastStreamJumpPointOfNoReturn());
    delete s;
}

// NgRnd::Offscreen() is "the current render target is not the back buffer"
// (the image's DxRnd::Offscreen, rnddx9/Rnd.s 8260FE78: GetRenderTarget(0) !=
// BackBuffer()). A RndTexRenderer renders to a texture by selecting a camera
// with a target texture, and there the image reads true. Natively it was
// NgRnd's `return false` stub, and Character::DrawShowing gates its self-shadow
// on !Offscreen(): a character drawn into a texture prepped its self-shadow,
// whose extrude pass (Character::DrawLodOrShadow mode 4, no mShadow ->
// DrawOpaque) reached a forced CharTransDraw that draws the character itself,
// forever. Measured on the party route after Strike a Pose: a 161,000-frame
// stack overflow, RndTexRenderer::DrawToTexture -> player0 ->
// projection_trans_draw.td -> player0 -> ...
TEST(NativeAudioSeekRender, OffscreenWhileACameraRendersToATexture) {
    EnsureEngineInit();
    RndCam *prev = RndCam::Current();
    RndCam *cam = Hmx::Object::New<RndCam>();
    RndTex *tex = Hmx::Object::New<RndTex>();
    cam->Select();
    EXPECT_FALSE(TheNgRnd.Offscreen()) << "a camera with no target draws to the screen";
    cam->SetTargetTex(tex);
    cam->Select();
    EXPECT_TRUE(TheNgRnd.Offscreen()) << "a camera with a target texture draws offscreen";
    cam->SetTargetTex(nullptr);
    EXPECT_FALSE(TheNgRnd.Offscreen());
    if (prev)
        prev->Select();
    else
        RndCam::ClearCurrent();
    delete cam;
    delete tex;
}

} // namespace
