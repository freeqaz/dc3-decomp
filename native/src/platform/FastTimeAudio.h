#pragma once

// DC3_FAST_TIME and the song's audio stream (PLAN H-10).
//
// Under DC3_FAST_TIME the song clock is synthetic: LiveInput::CurrentMs adds
// 1/120 s per game frame and ignores the stream.  The stream itself still
// plays on the wall clock.  When the song clock runs SLOWER than the wall
// (a paced run: 60 fps = 0.5x, 30 fps = 0.25x; a GPU run at ~95 fps), the
// stream reaches its end while the song is still going -- ymca's 152 s of
// audio ran out at song measure 42 at 60 fps, 21 at 30 fps, 74 on GPU.  A
// finished StandardStream is no longer IsReady() (kFinished), and the image's
// MoveDir::PostUpdateFilters enqueues filter jobs only while
// TheMaster->GetAudio()->IsReady(): from then on no frame was scored, every
// move read DetectFrac 0 and rated bad, although the skeleton was tracked
// and the performer kept publishing.  On the real clock song time IS the
// stream's time, so the stream ends with the song and this cannot happen.
//
// The fix keeps the stream from running ahead of the synthetic song clock:
// LiveInput stops it (StandardStream::Stop = pause, kStopped, still IsReady)
// when it leads the song by more than kLeadMs and plays it again once the song
// has caught up.  So under FAST_TIME the music is heard in chunks that follow
// the song clock, and never ends before the song does.  Native-only; the
// image never runs on a synthetic clock.
namespace FastTimeAudio {

enum Action { kKeep, kHold, kRelease };

// How far the stream may lead the song clock before it is held.  Hysteresis
// against stopping and restarting the stream every frame.
const float kLeadMs = 100.0f;

// `playing`: the stream is playing; `held`: LiveInput stopped it (not the
// game's pause); `streamMs`/`songMs`: the stream's time and the synthetic
// song time, on the same base (what CurrentMs would read from each).
inline Action StreamAction(bool playing, bool held, float streamMs, float songMs) {
    if (playing && streamMs > songMs + kLeadMs)
        return kHold;
    if (held && !playing && streamMs <= songMs)
        return kRelease;
    return kKeep;
}

} // namespace FastTimeAudio
