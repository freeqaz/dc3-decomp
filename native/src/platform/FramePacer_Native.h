// Native frame cap (PLAN H-17, lane np-cpu-perf).
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
