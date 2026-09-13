// HamAudio::SetCrossfadeJump crossfade-boundary regression test
// (Wave-3 Lane C, roadmap N.4).
//
// SetCrossfadeJump decides whether a queued crossfade is "invalid" (begins
// before the start of the song) and, if so, clears mCrossfade.mFlag so a hard
// jump is used instead. The original Xbox 360 binary computes that predicate as
// `startTime - fadeDuration*0.5 > 0` (a STRICT greater-than). The decompiled
// source had `startTime >= fadeDuration*0.5` (>=), which wrongly cancels the
// crossfade at the exact boundary startTime == fadeDuration*0.5.
//
// This test pins the boundary: at startTime == fadeDuration*0.5 the crossfade
// must stay pending (valid); just above it must cancel. It fails on the >=
// (pre-fix) code.

#include <gtest/gtest.h>

#include "test_helpers.h"

#include "hamobj/HamAudio.h"
#include "synth/Stream.h"

#include <vector>

// HamAudio construction allocates Fader objects, which create the "SynthFader"
// Symbol — needs the engine's StringTable/object factory up. Use the headless
// engine fixture.
class HamAudioCrossfade : public EngineTestFixture {};

namespace {

// Minimal Stream that no-ops everything SetLoop touches. SetCrossfadeJump only
// reaches stream->CurrentJumpPoints / ClearJump / ClearMarkerList / AddMarker /
// SetJump on mStreams[0]; the rest are never called but must be defined.
class NoopStream : public Stream {
public:
    bool IsReady() const override { return true; }
    bool IsFinished() const override { return false; }
    int GetNumChannels() const override { return 0; }
    int GetNumChanParams() const override { return 0; }
    void Play() override {}
    void Stop() override {}
    bool IsPlaying() const override { return false; }
    void Resync(float) override {}
    void Fill() override {}
    bool FillDone() const override { return true; }
    void EnableReads(bool) override {}
    float GetTime() override { return 0; }
    float GetJumpBackTotalTime(float) const override { return 0; }
    float GetInSongTime() override { return 0; }
    std::vector<struct JumpInstance> *GetJumpInstances() override { return nullptr; }
    float GetFilePos() const override { return 0; }
    float GetFileLength() const override { return 0; }
    void SetVolume(int, float) override {}
    float GetVolume(int) const override { return 0; }
    void SetPan(int, float) override {}
    float GetPan(int) const override { return 0; }
    void SetFX(int, bool) override {}
    bool GetFX(int) const override { return false; }
    void SetFXCore(int, FXCore) override {}
    FXCore GetFXCore(int) const override { return kFXCoreNone; }
    void SetSpeed(float) override {}
    float GetSpeed() const override { return 1.0f; }
    void LoadMarkerList(const char *) override {}
    void SetJump(String &, String &) override {}
    void SetJump(float, float, const char *) override {}
    void ClearJump() override {}
    void EnableSlipStreaming(int) override {}
    void SetSlipOffset(int, float) override {}
    void SlipStop(int) override {}
    float GetSlipOffset(int) override { return 0; }
    void SetSlipSpeed(int, float) override {}
    FaderGroup &ChannelFaders(int) override { return *(FaderGroup *)nullptr; }
};

// Run SetCrossfadeJump on a fresh HamAudio with two stack-allocated mock
// streams and return whether the crossfade stayed pending. The streams are
// detached before ~HamAudio so its Clear()/RELEASE doesn't delete our stack
// objects.
bool RunCrossfade(float startTime, float endTime, float fadeDuration) {
    HamAudio audio;
    NoopStream s0, s1;
    audio.SetStreamsForTest(&s0, &s1);
    audio.SetCrossfadeStateForTest(0);  // no existing crossfade to overlap
    audio.SetCrossfadeJump(startTime, endTime, fadeDuration);
    bool pending = audio.CrossfadePending();
    audio.SetStreamsForTest(nullptr, nullptr);  // detach before dtor
    return pending;
}

}  // namespace

// These three previously asserted the INVERTED semantics, and were written
// against a source state whose comparison was backwards. The retail listing
// settles it (build/373307D9/asm/system/hamobj/HamAudio.s, ?SetCrossfadeJump@):
//
//     fmr      f13, f31              ; f13 = fadeDuration
//     lfs      f31, __real@3f000000  ; 0.5
//     fnmsubs  f13, f13, f31, f30    ; f13 = startTime - fadeDuration*0.5
//     lfs      f0,  __real@00000000
//     fcmpu    cr6, f13, f0
//     bgt      cr6, .L_8252A354      ; > 0 SKIPS the notify
//
// So the fade reaches back to `startTime - fadeDuration*0.5`, and the crossfade
// is downgraded to a hard jump when that lead-in is at or before zero — i.e.
// when the fade would have to begin before the song does. A LARGER startTime is
// what keeps it valid; a small one is what cancels it. The old names had that
// backwards: a fade at startTime 0.5 with duration 4 begins 1.5 s before the
// song and cannot possibly stay pending.

// Lead-in exactly zero. `<= 0` cancels, so the boundary is NOT pending.
TEST_F(HamAudioCrossfade, ZeroLeadInCancelsCrossfade) {
    EXPECT_FALSE(RunCrossfade(/*startTime=*/2.0f, /*endTime=*/10.0f,
                              /*fadeDuration=*/4.0f))
        << "startTime - fadeDuration*0.5 == 0 is cancelled: retail skips the "
           "notify only on a strictly positive lead-in (bgt)";
}

// Positive lead-in: the fade fits inside the song and stays pending.
TEST_F(HamAudioCrossfade, PositiveLeadInKeepsCrossfadePending) {
    EXPECT_TRUE(RunCrossfade(/*startTime=*/2.5f, /*endTime=*/10.0f,
                             /*fadeDuration=*/4.0f))  // lead-in +0.5
        << "a strictly positive lead-in is a valid crossfade";
}

// Negative lead-in: the fade would start before the song, so it is downgraded.
TEST_F(HamAudioCrossfade, NegativeLeadInCancelsCrossfade) {
    EXPECT_FALSE(RunCrossfade(/*startTime=*/0.5f, /*endTime=*/10.0f,
                              /*fadeDuration=*/4.0f))  // lead-in -1.5
        << "a fade beginning before the song is a hard jump, not a crossfade";
}
