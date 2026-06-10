// Sound::SynthPoll behavioral tests — pins the ORIGINAL Xbox semantics of the
// per-frame poll loop, refuted by the Wave-2 boot diagnosis (Sound.cpp:174).
//
// Two real decomp bugs were found by reverse-engineering the Xbox asm for
// ?SynthPoll@Sound@@UAAXXZ (see docs/investigations/2026-06-10-roadmap-to-100):
//
//   1. The mSamples cleanup loop did `cur = *it; it++; ... mSamples.erase(it);`
//      which erases the NEXT element (and can erase(end()) -> UB / double-free).
//      The Xbox code saves the pre-increment iterator and erases THAT node.
//      (idx 57/78 in the target: r29 = saved node, erase(r29).)
//
//   2. The mDelayArgs loop passed `this` as the Play() event-receiver argument;
//      the Xbox code passes `cur->mEventReceiver` (idx 26: lwz r7, 0xc, r31).
//
// These tests drive the REAL Sound::SynthPoll through a test subclass and fail
// on the buggy code (wrong sample removed / wrong receiver), pass on the fix.

#include "test_helpers.h"
#include <gtest/gtest.h>

#include "synth/Sound.h"
#include "synth/PlayableSample.h"
#include "obj/Task.h"

#include <vector>

namespace {

// Minimal PlayableSample stub: DonePlaying() returns a fixed verdict, all other
// virtuals are inert. Records whether Stop() was called for completeness.
class StubSample : public PlayableSample {
public:
    explicit StubSample(int id, bool done) : mId(id), mDone(done) {}

    // PlayableSample interface
    void Play(float) override {}
    void Stop(bool) override { mStopped = true; }
    void Pause(bool) override {}
    bool DonePlaying() override { return mDone; }
    void SetVolume(float) override {}
    void SetPan(float) override {}
    void SetADSR(const ADSRImpl &) override {}
    void SetSpeed(float) override {}
    void SetReverbMixDb(float) override {}
    void SetReverbEnable(bool) override {}
    void SetSend(FxSend *) override {}
    void SetEventReceiver(Hmx::Object *) override {}
    Hmx::Object *GetEventReceiver() override { return nullptr; }
    void EndLoop() override {}
    float ElapsedTime() override { return 0.0f; }

    // SynthPollable pure virtual
    void SynthPoll() override {}

    int mId;
    bool mDone;
    bool mStopped = false;
};

// Test subclass: Sound's ctor is protected and members are protected, so a
// subclass is the sanctioned access path (CLAUDE.md: prefer friend/subclass
// over making members public). Also overrides the virtual Play() so we can
// observe the event-receiver argument the delayed-play path forwards.
class TestSound : public Sound {
public:
    TestSound() : Sound() {}

    // Inject samples directly into the protected mSamples list.
    void AddSample(PlayableSample *s) { mSamples.push_back(s); }
    const std::list<PlayableSample *> &Samples() const { return mSamples; }

    void SetIsSynthSample(bool v) { mIsSynthSample = v; }

    // Queue a delayed-play arg directly (bypasses the heap-owned new in Play()).
    void QueueDelay(DelayArgs *d) { mDelayArgs.push_back(d); }
    const std::list<DelayArgs *> &DelayArgs_() const { return mDelayArgs; }

    void ClearFadersDirty() { mFaders.ClearDirty(); }

    // Capture the (volume,pan,transpose,obj,delayMs) the poll loop fires Play with.
    void Play(float volume, float pan, float transpose, Hmx::Object *obj,
              float delayMs) override {
        mLastPlayObj = obj;
        mLastPlayVolume = volume;
        mLastPlayPan = pan;
        mLastPlayTranspose = transpose;
        ++mPlayCount;
        // Do NOT chain to Sound::Play — it would allocate a real sample.
    }

    Hmx::Object *mLastPlayObj = (Hmx::Object *)0xDEADBEEF;
    float mLastPlayVolume = -1.0f;
    float mLastPlayPan = -1.0f;
    float mLastPlayTranspose = -1.0f;
    int mPlayCount = 0;
};

class SoundSynthPollTest : public EngineTestFixture {};

// --- Bug #1: erase the CURRENT node, not the advanced iterator -------------

// First sample done, second not done. The original (buggy) code did
// `cur = *it; it++; erase(it)` -> with sample[0] done it would erase sample[1]
// (the wrong one), leaving the done sample[0] forever. The fix erases the
// saved current node.
TEST_F(SoundSynthPollTest, ErasesCurrentDoneSampleNotNext) {
    TestSound snd;
    snd.SetIsSynthSample(true);
    snd.ClearFadersDirty();

    StubSample s0(0, /*done=*/true);
    StubSample s1(1, /*done=*/false);
    snd.AddSample(&s0);
    snd.AddSample(&s1);

    snd.SynthPoll();

    // Exactly the done sample (s0) must be gone; the un-done one (s1) remains.
    const auto &remaining = snd.Samples();
    ASSERT_EQ(remaining.size(), 1u) << "expected one sample removed";
    EXPECT_EQ(remaining.front(), &s1)
        << "the un-done sample must survive; the done sample must be erased";

    // Drain so the Sound dtor doesn't touch freed stubs.
    snd.SetIsSynthSample(false);
}

// All samples done: every node must be erased without erase(end()) UB. Under
// the old code, erasing the advanced iterator on the LAST element passes
// end() to erase -> undefined behavior (often a crash/heap corruption).
TEST_F(SoundSynthPollTest, ErasesAllDoneSamplesNoEndIterator) {
    TestSound snd;
    snd.SetIsSynthSample(true);
    snd.ClearFadersDirty();

    StubSample s0(0, true), s1(1, true), s2(2, true);
    snd.AddSample(&s0);
    snd.AddSample(&s1);
    snd.AddSample(&s2);

    snd.SynthPoll();

    EXPECT_TRUE(snd.Samples().empty())
        << "all done samples must be removed without erase(end()) UB";

    snd.SetIsSynthSample(false);
}

// Middle sample done: pins that erase targets the right interior node.
TEST_F(SoundSynthPollTest, ErasesMiddleDoneSample) {
    TestSound snd;
    snd.SetIsSynthSample(true);
    snd.ClearFadersDirty();

    StubSample s0(0, false), s1(1, true), s2(2, false);
    snd.AddSample(&s0);
    snd.AddSample(&s1);
    snd.AddSample(&s2);

    snd.SynthPoll();

    const auto &r = snd.Samples();
    ASSERT_EQ(r.size(), 2u);
    auto it = r.begin();
    EXPECT_EQ(*it++, &s0) << "s0 (not done) survives";
    EXPECT_EQ(*it, &s2) << "s2 (not done) survives; only the done middle s1 erased";

    snd.SetIsSynthSample(false);
}

// --- Bug #2: delayed Play forwards the queued event-receiver, not `this` ----

TEST_F(SoundSynthPollTest, DelayedPlayUsesQueuedEventReceiver) {
    TestSound snd;
    snd.ClearFadersDirty();

    // A distinct object to stand in for the queued receiver.
    Hmx::Object receiver;
    auto *d = new Sound::DelayArgs(/*vol=*/0.25f, /*pan=*/0.5f, /*trans=*/1.5f,
                                   /*rcvr=*/&receiver, /*delay=*/0.0f);
    // Force the delay to have already elapsed so the poll fires Play this frame.
    d->mDelayMs = -1.0f;
    snd.QueueDelay(d);

    snd.SynthPoll();

    EXPECT_EQ(snd.mPlayCount, 1) << "the elapsed delayed-play must fire exactly once";
    EXPECT_EQ(snd.mLastPlayObj, &receiver)
        << "Play must receive the queued mEventReceiver, NOT `this`";
    EXPECT_NE(snd.mLastPlayObj, static_cast<Hmx::Object *>(&snd))
        << "the buggy code forwarded `this` as the event receiver";
    EXPECT_FLOAT_EQ(snd.mLastPlayVolume, 0.25f);
    EXPECT_FLOAT_EQ(snd.mLastPlayPan, 0.5f);
    EXPECT_FLOAT_EQ(snd.mLastPlayTranspose, 1.5f);

    // The fired entry must have been erased from the delay queue.
    EXPECT_TRUE(snd.DelayArgs_().empty())
        << "the elapsed delayed-play entry must be removed from mDelayArgs";
}

} // namespace
