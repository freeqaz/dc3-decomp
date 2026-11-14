// Native frame cap (PLAN H-29, lane np-cpu-perf).
//
// The image paces its loop with the GPU's present interval: rnd `sync` is 1 on
// Xbox (system/run/config/default.dta), DxRnd::SetSync maps it to
// D3DRS_PRESENTINTERVAL 1, and the loop body (App::RunWithoutDebugging) has no
// sleep of its own -- one frame per 60 Hz TV refresh at most, the debug
// "Toggle between 30 and 60 frames per second" cheat (sync 2) being the only
// other setting. The game's clocks are wall-clock driven (song time from the
// audio clock, UI/task time from Timer), so the frame rate is not part of the
// game's behaviour.
//
// The native port presents with WebGPU Fifo (vsync) too, so on a 60 Hz panel
// it already matches; on a 144 Hz panel it draws 144 frames a second, 2.4x the
// image's ceiling, for no difference anyone can see in a 2012 dance game. This
// caps windowed runs at the image's 60, snapped to an even divisor of the
// display rate so every frame is held for the same number of refreshes (60 on
// a 144 Hz panel would alternate 2- and 3-refresh frames: judder).
//
// Headless runs are NOT capped unless asked: MILO_HEADLESS advances UI time a
// fixed 1/30 s per frame and the test gate's flow tests depend on running
// faster than real time.
//
// DC3_FPS_CAP: unset = 60 windowed / off headless; 0 (or off) = off; N = cap
// at N (windowed: snapped to the display, see Dc3FrameCapDecide; headless:
// exactly N).
#pragma once

#include <chrono>
#include <string>

struct Dc3FrameCap {
    double fps = 0.0;        // 0 = no cap
    const char *why = "";    // one-line reason, for the log
};

// The decision, separated from the environment and the display so it can be
// tested: `env` is DC3_FPS_CAP's value (nullptr when unset), `refreshHz` the
// display's refresh rate (<= 0 when unknown or headless).
Dc3FrameCap Dc3FrameCapDecide(const char *env, bool windowed, double refreshHz);

// Sleeps out the rest of each frame period. Call once per frame, after the
// frame is presented (so the sleep sits between present and the next frame's
// input poll, where it adds no input latency).
class Dc3FramePacer {
public:
    using Clock = std::chrono::steady_clock;

    void SetTarget(double fps);
    double TargetFps() const { return mPeriodS > 0.0 ? 1.0 / mPeriodS : 0.0; }
    void Pace();

    // Totals for the exit log and tests.
    long long PacedFrames() const { return mPaced; }
    double SleptSeconds() const { return mSleptS; }

private:
    double mPeriodS = 0.0;
    Clock::time_point mNext{};
    bool mStarted = false;
    long long mPaced = 0;
    double mSleptS = 0.0;
};

// The same cap for a loop the BROWSER drives (web build, PLAN H-34). There is
// no sleep there: the page runs one game frame per requestAnimationFrame, and
// rAF fires at the display's rate (120/144 Hz on a high-refresh panel). This
// gate is asked on every rAF and answers whether a game frame runs on it.
//
// The display rate is not exposed to a page, so it is estimated from the rAF
// timestamps, from the one-refresh gaps among the last kWindow rAFs: the gaps
// up to 1.5x the 25th percentile, and their median. Not the median of all
// gaps: a gap that follows a game frame holds that frame's cost (the next rAF
// waits for the first refresh after it ends), and only a gap after a SKIPPED
// rAF, or after a frame shorter than one refresh, is one clean refresh. On
// 144 Hz with a 72 fps cap and frames of 3 or 10 ms, the gaps are a mix of
// 6.9 and 13.9 ms; with 60% long frames the median read 72 Hz, the cap went
// off and stayed off, and the game ran 89 fps (RafGate test, MEASURED with
// the median put back); in Chromium on an emulated 144 Hz rAF it flipped the
// cap off and back on (frame-cap.mjs, MEASURED). Whenever a quarter of the gaps are one
// refresh, this reads the panel. A game slower than the display makes every
// gap longer and the estimate lower, which turns the cap off -- correctly,
// there is nothing to cap. The decision is Dc3FrameCapDecide(env,
// windowed=true, estimate), re-made every kWindow rAFs; a re-estimate within
// 5% of the cap in force keeps it (timestamp jitter moves it ~1-2%; a real
// display change, 144 -> 120 or 165 Hz, moves it 13% or more).
//
// A frame runs on the first rAF no earlier than half a refresh before it is
// due, so the rAF grid's own jitter never drops a frame: on 144 Hz with a
// 72 fps cap that is every second rAF exactly. The next frame is due one
// period after this one was due (not after it ran), so a frame that ran a
// refresh late is made up on the next refresh; at most one period of debt.
class Dc3RafGate {
public:
    static constexpr int kWindow = 64;

    explicit Dc3RafGate(const char *env) : mEnv(env ? env : "") {}

    // `nowMs` is the rAF timestamp (performance.now() timebase). True: run a
    // game frame on this rAF.
    bool OnRaf(double nowMs);

    double RefreshHz() const { return mRefreshHz; }
    double TargetFps() const { return mCap.fps; }
    const char *Why() const { return mCap.why; }
    long long Ran() const { return mRan; }
    long long Skipped() const { return mSkipped; }
    // Bumped whenever the decision changes (for a one-line log per change).
    int Decisions() const { return mDecisions; }

private:
    void Decide();

    std::string mEnv;
    double mGaps[kWindow] = {};
    int mGapCount = 0, mGapNext = 0, mSinceDecide = 0;
    double mLastRafMs = -1.0, mLastRunMs = -1.0, mDueMs = -1.0;
    double mRefreshHz = 0.0;
    Dc3FrameCap mCap{};
    char mWhy[160] = "";
    long long mRan = 0, mSkipped = 0;
    int mDecisions = 0;
};
