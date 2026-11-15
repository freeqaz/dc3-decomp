// The loader's per-poll budget on native during gameplay (PLAN H-110).
//
// LoadMgr::Poll gives loading up to mPeriod ms of every frame while anything
// loads (src/system/utl/Loader.cpp). The image sets it to 10 ms (LoadMgr's
// default, PreloadPanel::FinishLoad, game_modes.dta's finish_load) and to 14 ms
// while a preload panel or the game panel loads (PreloadPanel::Load, the game
// panel's load handler), always for a 16.7 ms frame: 10 ms of a 60 Hz frame is
// 60% of it. At the native 120 fps cap the frame is 8.3 ms, so a 10 ms slice
// stretched every frame that loaded to 12-22 ms at the end of a song and in
// the run-up to results (docs section 40.3), both with gameplay on screen.
//
// The rule, in two sentences: while gameplay is on screen and the frame pacer
// has a target, the loader's slice is the image's value scaled by (target
// period / 16.7 ms), so 10 ms is 5 ms at 120 fps and stays 10 ms at 60.
// Everywhere else, or with no target, it is the image's value unchanged.
//
// "Gameplay is on screen" is the image's own test: TheUI's bottom screen is
// game_screen (MetaPanel::Poll asks the same). The win screen is pushed over
// game_screen (endgame_helpers.dta goto_win_screen), so the run-up to results
// counts; loading screens, boot and the menus do not. Scaling everywhere kept
// the duty ratio but made boot 0.1-0.3 s slower (the loader's work spread over
// twice the frames); a slice that took the frame's spare time back depended on
// how loaded the box was (docs section 41).
//
// The target is the pacer's (Dc3FramePacer on desktop, Dc3RafGate on the web).
// The image's own setter does the work (LoadMgr::SetLoaderPeriod); nothing the
// Xbox build compiles changes.
#pragma once

// The budget for one poll: the image's `imageMs` (set for a 60 Hz frame) at a
// frame target of `targetFps` (<= 0: none, the image's value).
double Dc3LoaderBudgetMs(double imageMs, double targetFps);

// What to set before a poll, given what LoadMgr holds now. A value different
// from the one this last returned is the image's (it set the budget since):
// it is the value scaled from then on. Kept apart from TheLoadMgr and TheUI so
// the rule can be tested without an engine.
class Dc3LoaderBudget {
public:
    float Step(float current, double targetFps, bool gameplayOnScreen);
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

// TheUI's bottom screen is game_screen.
bool Dc3GameplayOnScreen();

// Before the frame's SystemPoll: TheLoadMgr's budget for this frame. Logs one
// line whenever the budget, the image's value, the frame target or whether
// gameplay is on screen changes.
void Dc3LoaderBudgetApply();
