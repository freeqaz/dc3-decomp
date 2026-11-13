// DC3_FAST_TIME and the song stream (PLAN H-10): FastTimeAudio::StreamAction,
// the rule LiveInput::CurrentMs applies every frame.
//
// The bug: under DC3_FAST_TIME the song clock is synthetic (1/120 s per frame)
// but the stream plays on the wall clock.  On a paced run the song clock is
// slower than the wall, the stream reached its end mid-song (ymca: 152 s of
// audio, out at song measure 42 at 60 fps, 21 at 30 fps), StandardStream went
// kFinished and stopped being IsReady(), MoveDir::PostUpdateFilters stopped
// enqueueing filter jobs, and every later move rated bad.
//
// These tests pin the rule on its own and then run it against a simulated
// stream at the measured wall/song rates, with a control that runs the same
// simulation without the rule and requires the stream to run out early.  The
// wiring in LiveInput::CurrentMs was measured end to end (a paced-30 ymca run:
// 24/0/0/0 at song 50 s, stream time 52124 ms at song 52.08 s, never
// finished; before: 17/0/0/7).

#include <gtest/gtest.h>

#include "platform/FastTimeAudio.h"

namespace {

using FastTimeAudio::StreamAction;

TEST(FastTimeAudioTest, HoldsAStreamThatLeadsTheSong) {
    const float lead = FastTimeAudio::kLeadMs;
    EXPECT_EQ(StreamAction(true, false, 1000.0f + lead + 1.0f, 1000.0f), FastTimeAudio::kHold);
    // within the hysteresis band: left playing
    EXPECT_EQ(StreamAction(true, false, 1000.0f + lead, 1000.0f), FastTimeAudio::kKeep);
    EXPECT_EQ(StreamAction(true, false, 900.0f, 1000.0f), FastTimeAudio::kKeep);
}

TEST(FastTimeAudioTest, ReleasesOnlyAStreamItHeld) {
    // held by the rule, the song caught up: play again
    EXPECT_EQ(StreamAction(false, true, 1000.0f, 1000.0f), FastTimeAudio::kRelease);
    EXPECT_EQ(StreamAction(false, true, 1000.0f, 1000.5f), FastTimeAudio::kRelease);
    // held, song not there yet: stay held
    EXPECT_EQ(StreamAction(false, true, 1000.0f, 999.0f), FastTimeAudio::kKeep);
    // stopped by the GAME (pause), not by the rule: never restarted here
    EXPECT_EQ(StreamAction(false, false, 1000.0f, 5000.0f), FastTimeAudio::kKeep);
}

struct SimResult {
    float songAtStreamEnd = -1.0f; // song ms when the stream ran out (-1: never)
    float maxLeadMs = 0.0f;        // how far the stream ever led the song
    float streamAtEndMs = 0.0f;    // where the stream is when the song ends
};

// One song: the song clock advances 1000/120 ms a frame (CurrentMs's synthetic
// step), the stream advances `wallPerSong` times that while playing (the wall
// time a paced frame takes).  `useRule` false is the code before the fix.
SimResult Simulate(float lengthMs, float wallPerSong, bool useRule) {
    SimResult r;
    const float songStep = 1000.0f / 120.0f;
    float song = 0.0f, stream = 0.0f;
    bool playing = true, held = false;
    while (song < lengthMs) {
        song += songStep;
        if (playing)
            stream += songStep * wallPerSong;
        if (stream >= lengthMs && r.songAtStreamEnd < 0.0f)
            r.songAtStreamEnd = song;
        if (useRule) {
            switch (StreamAction(playing, held, stream, song)) {
            case FastTimeAudio::kHold:
                playing = false;
                held = true;
                break;
            case FastTimeAudio::kRelease:
                playing = true;
                held = false;
                break;
            default:
                break;
            }
        }
        if (stream - song > r.maxLeadMs)
            r.maxLeadMs = stream - song;
    }
    r.streamAtEndMs = stream;
    return r;
}

TEST(FastTimeAudioTest, StreamNeverEndsBeforeTheSongAtPacedRates) {
    const float ymcaMs = 151804.0f; // ymca's song_length, the measured case
    // 60 fps paced (0.5x), 30 fps paced (0.25x), the ~95 fps GPU run
    const float rates[] = {120.0f / 60.0f, 120.0f / 30.0f, 120.0f / 95.0f};
    for (float rate : rates) {
        SimResult before = Simulate(ymcaMs, rate, false);
        // Control: the old code runs the stream out mid-song, at 1/rate of it
        // -- else this simulation could not show the bug at all.
        ASSERT_GE(before.songAtStreamEnd, 0.0f) << "rate " << rate;
        EXPECT_LT(before.songAtStreamEnd, ymcaMs * 0.9f) << "rate " << rate;

        SimResult after = Simulate(ymcaMs, rate, true);
        // The stream may reach its end only as the song does.
        if (after.songAtStreamEnd >= 0.0f)
            EXPECT_GE(after.songAtStreamEnd, ymcaMs - FastTimeAudio::kLeadMs) << "rate " << rate;
        // The stream keeps up: a hold that is never released would leave the
        // song silent from the first hold on.
        EXPECT_GE(after.streamAtEndMs, ymcaMs - 2 * 1000.0f / 120.0f * rate) << "rate " << rate;
        // ...and never leads by more than the band plus one frame of wall time.
        EXPECT_LE(after.maxLeadMs, FastTimeAudio::kLeadMs + 1000.0f / 120.0f * rate)
            << "rate " << rate;
    }
}

TEST(FastTimeAudioTest, AStreamSlowerThanTheSongIsNeverTouched) {
    // Unpaced headless runs: the song clock outruns the wall, the stream lags,
    // and the rule must leave it alone (no hold, so no audible chopping).
    SimResult r = Simulate(60000.0f, 0.25f, true);
    EXPECT_LE(r.maxLeadMs, 0.0f);
}

} // namespace
