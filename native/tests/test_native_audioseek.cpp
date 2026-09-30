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
#include "platform/StreamReceiver_Native.h"
#include "synth/StandardStream.h"
#include "synth/Synth.h"

#include <cstdint>
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

} // namespace
