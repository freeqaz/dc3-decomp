// The loader's per-poll budget on native follows the frame period (PLAN H-107).
//
// LoadMgr::Poll gives loading up to mPeriod ms of every frame while anything
// loads (src/system/utl/Loader.cpp). The image sets it to 10 ms (LoadMgr's
// default, PreloadPanel::FinishLoad, game_modes.dta's finish_load) and to 14 ms
// while a preload panel or the game panel loads (PreloadPanel::Load, the game
// panel's load handler), always for a 16.7 ms frame: 10 ms of a 60 Hz frame is
// 60% of it. At the native 120 fps cap the frame is 8.3 ms, so a 10 ms slice
// stretched every frame that loaded to 12-22 ms (the end of a song and the
// run-up to results, docs section 40.3).
//
// Native keeps the image's duty ratio instead: before each frame's poll, the
// budget the image last asked for is scaled by (frame period / 16.7 ms), from
// the frame pacer's actual target (Dc3FramePacer on desktop, Dc3RafGate on the
// web). 10 ms is 5 ms at 120 fps and stays 10 ms at 60; 14 ms is 7 ms at 120.
// With no frame target (uncapped, a display at or under 60 Hz that vsync
// paces, or the target not known yet) the image's value is left as it is.
//
// The image's own setter does the work (LoadMgr::SetLoaderPeriod); nothing the
// Xbox build compiles changes.
#pragma once

// The budget for one poll: the image's `imageMs` (set for a 60 Hz frame) at a
// frame target of `targetFps` (<= 0: none, the image's value).
double Dc3LoaderBudgetMs(double imageMs, double targetFps);

// What to set before a poll, given what LoadMgr holds now. A value different
// from the one this last returned is the image's (it set the budget since):
// it becomes the value scaled from then on. Kept apart from TheLoadMgr so the
// rule can be tested without an engine.
class Dc3LoaderBudget {
public:
    float Step(float current, double targetFps);
    float ImageMs() const { return mImageMs; }
    float AppliedMs() const { return mAppliedMs; }

private:
    bool mHave = false;
    float mImageMs = 0.0f;
    float mAppliedMs = 0.0f;
};

// The frame pacer's target, published by whoever owns the pacer (the desktop
// main loop, the web rAF gate). 0 = none.
void Dc3LoaderBudgetSetFrameTarget(double fps);
double Dc3LoaderBudgetFrameTarget();

// Before the frame's SystemPoll: TheLoadMgr's budget for this frame. Logs one
// line whenever the budget in force changes.
void Dc3LoaderBudgetApply();
