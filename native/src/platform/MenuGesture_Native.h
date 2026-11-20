#pragma once

// Menu gestures on a webcam (PLAN P-3, branch np-menu-gestures).
//
// DC3's shell lists are HamNavLists driven by the image's own detectors
// (HandHeightGestureFilter, DirectionGestureFilterSingleUser / ArcDetector);
// natively the skeleton comes from a webcam through MediaPipe instead of a
// Kinect.  Two things live here:
//
//   1. The native side of measuring that: what every polled list is doing
//      this frame and every select with its source, for
//      scripts/menu_gesture_bench.py (GET /api/menu/state).  Read-only.
//
//   2. Hold to select (DC3_MENU_SELECT=hold|both, default swipe; the switch
//      is in NativeSettings.h): the hand kept on a row selects it, with the
//      list's own slide as the visible progress and its own select on fire.
//      Under swipe nothing here changes what a list does.

#include <string>

class HamNavList;

namespace Dc3MenuGesture {

// What made a list select.  The image has two: a swipe
// (HamNavList::UpdateGestures -> SetSelecting(false)) and a controller
// confirm (OnMsg(ButtonDownMsg) -> SetSelecting(true)).  kSourceHold is ours.
enum Source {
    kSourceSwipe = 0,
    kSourceButton = 1,
    kSourceHold = 2,
};

// HamNavList::SetSelecting, once per select that went through.
void NoteSelect(const HamNavList *list, int selected, int firstShowing, const char *sym,
                bool byButton);

// HamNavList::PostUpdate, once per skeleton frame the list's gesture filters
// were updated with (camera frames and DC3_POSE_FILL frames alike).
void NoteSensorUpdate(const HamNavList *list);

// The end of HamNavList::Poll, once per frame per polled list.
void NotePoll(HamNavList *list);

// {"ui_seconds":..,"song_seconds":..,"controller_mode":..,"selects":N,
// "menu_select":"swipe","fill":{..},"lists":[..],"events":[..]} -- the song
// clock (MoveDir::SongSeconds, what the pose fill runs on), the pose fill's
// state (DC3_POSE_FILL: its mode, whether it is engaged, how many fill and
// camera frames it has delivered while engaged, how many seconds its clock
// was carried on while the song clock stood still), the lists polled within the last
// 0.2 UI seconds and every select with seq > `since` (the last 256 are
// kept).  `reset` clears each list's running maxima and counters after
// reading them.  `rows`: also each list's data symbols, captured on its next
// poll (so ask, wait a frame, ask again).  Main thread only.
std::string StateJson(unsigned since, bool reset, bool rows);

// ---- the arc detector's first node ------------------------------------------
//
// ArcDetector::TryToStartSwipe puts the first node of a path down only on a
// frame whose wrist AND elbow are both Tracked (every later node is taken
// whatever its confidence).  On a Kinect an Inferred arm joint is a guess
// behind an occlusion.  Natively Tracked means MediaPipe's visibility score is
// 0.6 or more (Skeleton_Native.cpp), and an arm held out at a webcam is under
// that for much of the time.
//
// ArcStartAllowed: whether the path may start on this frame.  `tracked` is the
// image's answer; under DC3_MENU_ARC_START=inferred a frame whose wrist and
// elbow the CAMERA scored at least Inferred starts one too.  The camera's own
// score, not the skeleton's: the pose estimate (DC3_POSE_ESTIMATE) hands the
// detectors every joint the camera lost as Inferred at a place it made up, and
// a path must not start there (it did, on a recording: MENU_GESTURES.md).
// Counts what it was asked (GET /api/menu/state "arc_start").
bool ArcStartAllowed(int slot, int wristJoint, int elbowJoint, bool tracked);
// The rule alone (native/tests/test_menu_arc.cpp): `wristSeen` / `elbowSeen`
// are the camera's own confidences.
bool ArcStartAllowedFor(bool onInferred, bool tracked, int wristSeen, int elbowSeen);
// What the camera said of the joints in skeleton slot `slot` on this sensor
// frame, before the pose estimate relabels them (GestureMgr_Native.cpp, once
// a slot a frame).  `conf` = kNumJoints confidences, or null: nobody the
// camera sees there (an empty slot, or a body held through a dropout).
void NoteSensorConfidence(int slot, const int *conf);
// DC3_MENU_ARC_START, read at the first use.  Set also shows a notice
// ({native_settings set_menu_arc_start tracked|inferred}); the tests pass
// `notice` false.
bool ArcStartsOnInferred();
void SetArcStartsOnInferred(bool on, bool notice = false);

// ---- hold to select ---------------------------------------------------------

// Seconds, except where said.
struct HoldParams {
    // DC3_MENU_HOLD_SETTLE_MS: the highlight must have rested on the row this
    // long before the hold starts to show.
    float settle = 0.3f;
    // DC3_MENU_HOLD_MS: then this long of visible progress, then the select.
    // The price of a short one is a row picked while the list is being read
    // with the hand up.  MEASURED (the synthetic performer reading down a
    // list, 0.6-1.1 s a row, 16 five-second reads each, a 15 Hz camera with
    // the GPD's noise): at 1.2 s a row was picked in 1 read of the main menu
    // and 6 of the song list, at 1.6 s in 0 and 1, at 2.0 s in 0 and 0; a
    // pick takes 2.0, 2.3 and 2.7 s from the arm starting up (a sweep: 1.6).
    float hold = 1.2f;
    // DC3_MENU_HOLD_LEFT_MS: the same for the left hand's list (the help
    // bar: Back).  Longer, because that list has a row or two and so no
    // aiming to show the hand means it, and because of what real footage
    // does to it: two recorded playtests' 368 s outside songs, replayed in
    // front of the main menu, held the left list 6 times at 1.2 s against 2
    // and 3 at 1.8 and 2.4 s (the sweep: 0), the right one 4, 3 and 2 times
    // (the sweep: 6).
    float holdLeft = 1.8f;
    // DC3_MENU_HOLD_REARM_MS: after any select, and on a list that appears
    // under a hand already held out, a hand that has not moved cannot start a
    // hold for this long (so one hold cannot select its way through two
    // screens).  Moving the hand `rearmMove` hand heights, or dropping the
    // arm for `rearmDown`, re-arms at once.
    float rearm = 2.5f;
    float rearmMove = 0.10f;
    float rearmDown = 0.3f;
    // No tick for this long = the list was away (a transition, its own select
    // animation): what follows is a new approach.
    float gap = 0.25f;
    // both: a swipe under way makes the hold wait; one that lasts this long
    // restarts it.
    float swipePause = 0.5f;
    // The hand counts only between the list's top and bottom rows, with this
    // margin (hand heights: 0 = the top row, 1 = the bottom row).
    float span = 0.2f;
};
// The parameters in force: the defaults above with the four DC3_MENU_HOLD_*
// variables read once, at the first use.
const HoldParams &Params();

// The timing rule alone -- no list, no clock of its own -- so the unit tests
// can drive it (native/tests/test_menu_hold.cpp).  One per list.
class HoldTimer {
public:
    struct Input {
        float now = 0.0f;           // UI seconds
        int row = -1;               // the highlighted row
        float handHeight = 0.0f;    // HandHeightGestureFilter's, 0 top .. 1 bottom
        bool engaged = false;       // the hand is on this list and it may select
        bool blocked = false;       // the list is scrolling: no hold
        bool swipe = false;         // both: a swipe has progress: the hold waits
        float lastSelect = -1.0e9f; // UI seconds of the newest select of any list
    };
    enum Result {
        kIdle,     // nothing to show
        kProgress, // Progress() of the hold is done: show it
        kFire,     // select the row now
    };
    Result Tick(const Input &in, const HoldParams &p);
    float Progress() const { return mProgress; } // 0 .. 1 of `hold`
    bool Armed() const { return !mNeedMove; }
    void Reset() { *this = HoldTimer(); }

private:
    void Disarm(float until);

    bool mTicked = false;
    float mLast = 0.0f;       // the last tick
    int mRow = -1;
    float mHeld = 0.0f;       // seconds on this row
    float mPaused = 0.0f;     // seconds a swipe has kept it waiting
    float mProgress = 0.0f;
    float mDown = 0.0f;       // seconds the hand has been off the list
    bool mNeedMove = false;   // disarmed until the hand moves, drops or `rearm` passes
    bool mHaveAnchor = false; // where the hand was when it was disarmed
    float mAnchor = 0.0f;
    float mRearmAt = 0.0f;
    float mSelectSeen = -1.0e9f;
};

// HamNavList::UpdateGestures, once the list's hand height is read and before
// the image's own swipe arm.  True: the hold handled the list this frame (it
// selected the row, or it is showing its progress, or -- hold only -- it put
// the list in its swell); the caller returns.  False: the image's code runs
// as it always has.  Under DC3_MENU_SELECT=swipe it is always false and
// touches nothing; under hold a swipe no longer selects.
bool UpdateHold(HamNavList *list, bool handValid);

// HamNavList::Poll: whether the list's slide is, this frame, a hold's
// progress.  The image freezes the highlight while a list slides (a slide is
// a swipe under way); under a hold it must keep following the hand, or the
// hand could not leave the row.
bool HoldShowing(const HamNavList *list);

// Once per frame (App.cpp): F1 steps the mode (swipe -> hold -> both).
void PollKey();

} // namespace Dc3MenuGesture
