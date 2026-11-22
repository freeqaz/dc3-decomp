// Menu gestures on a webcam: the native side of the measurement (PLAN P-3,
// branch np-menu-gestures).  See MenuGesture_Native.h.

#include "MenuGesture_Native.h"
#include "Pointer_Native.h"

#ifdef HX_NATIVE

#include "NativeNotice.h"
#include "NativeSettings.h" // the DC3_MENU_SELECT switch is declared there
#include "gesture/ArcDetector.h"
#include "gesture/DirectionGestureFilter.h"
#include "gesture/GestureMgr.h"
#include "gesture/HandHeightGestureFilter.h"
#include "hamobj/HamListRibbon.h"
#include "hamobj/HamNavList.h"
#include "hamobj/MoveDir.h" // MoveDir::SongSeconds: the pose fill's clock
#include "lazer/game/Game.h"
#include "lazer/game/GamePanel.h"
#include "obj/Msg.h"
#include "obj/Task.h"
#include "platform/PoseFill_Native.h"
#include "ui/UI.h"
#include "ui/UIListProvider.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <strings.h>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include <GLFW/glfw3.h>
extern GLFWwindow *gNativeWindow;
#endif

// The one name the image's classes befriend (HX_NATIVE only): it reads their
// private state, it never writes it.
struct Dc3MenuGestureAccess {
    struct List {
        const HamNavList *list = nullptr;
        std::string name;
        std::string sym;
        float lastPoll = -1000.0f; // UI seconds
        int hand = 0; // HamNavList::NavInputType
        int mode = 3; // HamListRibbon::RibbonMode
        int selected = 0, selectedData = 0, firstShowing = 0, numShowing = 0, numData = 0;
        int numItems = 0, highlightItem = 0;
        bool scrollable = false, isScrolling = false, atTop = false, atBottom = false;
        int scrollDir = 0;
        bool enabled = false, selectionEnabled = false;
        float handHeight = 0.0f;
        float swipeAmt = 0.0f, percentPulled = 0.0f;
        bool hasDirection = false, lockedIn = false;
        int nodes = -1; // -1: not a single-user filter
        int hoverMs = -1;
        // ArcDetector::mHadProgress: a sweep that got past half way and failed
        // leaves the detector refusing to start another until the forearm has
        // been seen to move back 1 cm between two frames
        bool hadProgress = false;
        // since the last reset
        float maxSwipeAmt = 0.0f;
        int maxNodes = 0;
        unsigned polls = 0, sensorUpdates = 0;
        // on request
        bool rowsValid = false;
        std::vector<std::string> rows;
        std::vector<char> rowFlags; // bit 0 header, bit 1 inactive
        // hold to select
        Dc3MenuGesture::HoldTimer hold;
        float holdShownAt = -1.0e9f; // UI seconds of the last frame the slide was the hold's
        float holdProgress = 0.0f;
    };

    static bool UpdateHold(HamNavList *l, bool handValid, List &s, float lastSelect, int *source);

    static bool HandInListPose(const HamNavList *l, const Skeleton &skeleton) {
        const DirectionGestureFilter *f = l->mDirectionGestureFilter;
        if (!f)
            return false;
        if (const DirectionGestureFilterSingleUser *single =
                dynamic_cast<const DirectionGestureFilterSingleUser *>(f))
            return single->IsValidSwipePosition(skeleton);
        return f->IsHandValid(skeleton);
    }

    static void Read(HamNavList *l, List &s, bool wantRows) {
        s.hand = (int)l->mNavInputType;
        s.mode = (int)l->mRibbonMode;
        const UIListState &st = l->mListState;
        UIListProvider *provider = st.Provider();
        s.selected = st.Selected();
        s.firstShowing = st.FirstShowing();
        s.numShowing = provider ? st.NumShowing() : 0;
        s.numData = provider ? provider->NumData() : 0;
        s.selectedData = provider ? st.SelectedData() : 0;
        s.scrollable = st.ScrollPastMinDisplay();
        s.isScrolling = l->mScrollBehavior.IsScrolling();
        s.atTop = l->mScrollBehavior.AtTop();
        s.atBottom = l->mScrollBehavior.AtBottom();
        s.scrollDir = l->mScrollBehavior.mScrollDir;
        s.enabled = l->mEnabled;
        s.selectionEnabled = l->mSelectionEnabled;
        s.numItems = provider && l->mListRibbonResource ? l->NumItems() : 0;
        s.highlightItem = provider ? l->GetHighlightItem() : 0;
        s.sym.clear();
        if (provider && s.selectedData >= 0 && s.selectedData < s.numData)
            s.sym = provider->DataSymbol(s.selectedData).Str();
        s.handHeight = l->mHandHeight;
        DirectionGestureFilter *f = l->mDirectionGestureFilter;
        s.hasDirection = f && f->HasDirection();
        s.lockedIn = f && f->IsLockedIn();
        s.percentPulled = f ? f->GetPercentPulled() : 0.0f;
        s.nodes = -1;
        s.hoverMs = -1;
        s.hadProgress = false;
        s.swipeAmt = 0.0f;
        if (DirectionGestureFilterSingleUser *single =
                dynamic_cast<DirectionGestureFilterSingleUser *>(f)) {
            s.swipeAmt = single->mArcDetector.GetSwipeAmount();
            s.nodes = single->mArcDetector.NumJointsInPath();
            s.hoverMs = single->mArcDetector.mHoverTimer;
            s.hadProgress = single->mArcDetector.mHadProgress;
        }
        if (wantRows) {
            s.rows.clear();
            s.rowFlags.clear();
            int n = s.numData < 600 ? s.numData : 600;
            for (int i = 0; i < n; i++) {
                s.rows.push_back(provider->DataSymbol(i).Str());
                s.rowFlags.push_back((provider->IsHeader(i) ? 1 : 0)
                                     | (provider->IsActive(i) ? 0 : 2));
            }
            s.rowsValid = true;
        }
    }
};

namespace Dc3MenuGesture {
namespace {

typedef Dc3MenuGestureAccess::List List;

struct SelectEvent {
    unsigned seq;
    float uiSeconds;
    std::string list;
    std::string sym;
    int hand;
    int selected;
    int firstShowing;
    int source;
};

std::vector<List> gLists;
std::deque<SelectEvent> gEvents;
unsigned gSelects = 0;
bool gWantRows = false;
int gNextSource = -1;               // the source of the select a hold is making now
float gLastSelect = -1.0e9f;        // UI seconds of the newest select of any list
const float kStaleSeconds = 0.2f;
const size_t kMaxEvents = 256;

float Now() { return TheTaskMgr.UISeconds(); }

List &Find(const HamNavList *list) {
    float now = Now();
    List *oldest = nullptr;
    for (size_t i = 0; i < gLists.size(); i++) {
        if (gLists[i].list == list)
            return gLists[i];
        // a list not polled for a while may be gone, and its address reused
        if (now - gLists[i].lastPoll > 5.0f || now < gLists[i].lastPoll)
            oldest = &gLists[i];
    }
    if (oldest) {
        *oldest = List();
        oldest->list = list;
        return *oldest;
    }
    gLists.push_back(List());
    gLists.back().list = list;
    return gLists.back();
}

void AppendEscaped(std::string &out, const std::string &s) {
    out += '"';
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    out += '"';
}

} // namespace

void NoteSelect(const HamNavList *list, int selected, int firstShowing, const char *sym,
                bool byButton) {
    SelectEvent e;
    e.seq = ++gSelects;
    e.uiSeconds = Now();
    e.list = list ? list->Name() : "";
    e.sym = sym ? sym : "";
    e.hand = 0;
    for (size_t i = 0; i < gLists.size(); i++) {
        if (gLists[i].list == list)
            e.hand = gLists[i].hand;
    }
    e.selected = selected;
    e.firstShowing = firstShowing;
    e.source = gNextSource >= 0 ? gNextSource : byButton ? kSourceButton : kSourceSwipe;
    gLastSelect = e.uiSeconds;
    gEvents.push_back(e);
    while (gEvents.size() > kMaxEvents)
        gEvents.pop_front();
}

void NoteSensorUpdate(const HamNavList *list) { Find(list).sensorUpdates++; }

void ExpireNotice(); // below, with the mode switch's banner

void NotePoll(HamNavList *list) {
    ExpireNotice();
    List &s = Find(list);
    s.name = list->Name();
    Dc3MenuGestureAccess::Read(list, s, gWantRows);
    s.lastPoll = Now();
    s.polls++;
    if (s.swipeAmt > s.maxSwipeAmt)
        s.maxSwipeAmt = s.swipeAmt;
    if (s.nodes > s.maxNodes)
        s.maxNodes = s.nodes;
}

// [arm][0 asked, 1 the image's gate refused, 2 of those started anyway], since
// the engine started (`reset` leaves them)
static unsigned gArcStart[2][3];

static int sArcStartsOnInferred = -1;

bool ArcStartsOnInferred() {
    if (sArcStartsOnInferred < 0) {
        const char *v = getenv("DC3_MENU_ARC_START");
        sArcStartsOnInferred = v && !strcasecmp(v, "inferred") ? 1 : 0;
        if (v && *v && !sArcStartsOnInferred && strcasecmp(v, "tracked"))
            fprintf(stderr, "DC3_MENU_ARC_START='%s' ignored (want tracked or inferred)\n", v);
        if (sArcStartsOnInferred)
            fprintf(stderr, "DC3 MENU ARC START: inferred (a sweep may start on a frame whose "
                            "wrist or elbow is Inferred)\n");
    }
    return sArcStartsOnInferred == 1;
}

// [slot][joint]: the camera's own confidence this sensor frame
static unsigned char gSensorConf[NUM_SKELETONS][kNumJoints];

void NoteSensorConfidence(int slot, const int *conf) {
    if (slot < 0 || slot >= NUM_SKELETONS)
        return;
    for (int j = 0; j < kNumJoints; j++)
        gSensorConf[slot][j] = conf ? (unsigned char)conf[j] : (unsigned char)kConfidenceNotTracked;
}

bool ArcStartAllowedFor(bool onInferred, bool tracked, int wristSeen, int elbowSeen) {
    if (tracked)
        return true;
    return onInferred && wristSeen >= kConfidenceInferred && elbowSeen >= kConfidenceInferred;
}

bool ArcStartAllowed(int slot, int wristJoint, int elbowJoint, bool tracked) {
    unsigned *n = gArcStart[wristJoint == kJointWristLeft ? 0 : 1];
    n[0]++;
    if (tracked)
        return true;
    n[1]++;
    if (slot < 0 || slot >= NUM_SKELETONS || wristJoint < 0 || wristJoint >= kNumJoints
        || elbowJoint < 0 || elbowJoint >= kNumJoints)
        return false;
    if (!ArcStartAllowedFor(
            ArcStartsOnInferred(), false, gSensorConf[slot][wristJoint],
            gSensorConf[slot][elbowJoint]
        ))
        return false;
    n[2]++;
    return true;
}

std::string StateJson(unsigned since, bool reset, bool rows) {
    char buf[768];
    float now = Now();
    std::string out;
    const Dc3PoseFill::Filler &fill = NativePoseFiller();
    snprintf(buf, sizeof(buf),
             "{\"ui_seconds\":%.4f,\"song_seconds\":%.4f,\"controller_mode\":%d,\"selects\":%u,"
             "\"menu_select\":\"%s\","
             "\"fill\":{\"mode\":\"%s\",\"engaged\":%d,\"fills\":%u,\"reals\":%u,"
             "\"clock_carried\":%.3f},"
             "\"arc_start\":{\"left\":[%u,%u,%u],\"right\":[%u,%u,%u]},\"lists\":[",
             now, MoveDir::SongSeconds(), TheGestureMgr && TheGestureMgr->InControllerMode() ? 1 : 0,
             gSelects, Dc3MenuSelectName(Dc3MenuSelect()), Dc3PoseFill::ModeName(fill.GetMode()),
             fill.Engaged() ? 1 : 0, fill.Fills(), fill.Reals(), NativePoseFillClockCarried(),
             gArcStart[0][0], gArcStart[0][1], gArcStart[0][2], gArcStart[1][0], gArcStart[1][1],
             gArcStart[1][2]);
    out += buf;
    bool first = true;
    for (size_t i = 0; i < gLists.size(); i++) {
        List &s = gLists[i];
        if (now - s.lastPoll > kStaleSeconds || now < s.lastPoll)
            continue;
        if (!first)
            out += ',';
        first = false;
        out += "{\"name\":";
        AppendEscaped(out, s.name);
        out += ",\"sym\":";
        AppendEscaped(out, s.sym);
        snprintf(buf, sizeof(buf),
                 ",\"hand\":\"%s\",\"mode\":%d,\"selected\":%d,\"selected_data\":%d,"
                 "\"first_showing\":%d,\"num_showing\":%d,\"num_data\":%d,\"num_items\":%d,"
                 "\"highlight_item\":%d,\"scrollable\":%d,\"is_scrolling\":%d,\"at_top\":%d,"
                 "\"at_bottom\":%d,\"scroll_dir\":%d,\"enabled\":%d,\"selection_enabled\":%d,"
                 "\"hand_height\":%.4f,\"swipe_amt\":%.4f,\"percent_pulled\":%.4f,"
                 "\"has_direction\":%d,\"locked_in\":%d,\"nodes\":%d,\"hover_ms\":%d,"
                 "\"had_progress\":%d,\"max_swipe_amt\":%.4f,\"max_nodes\":%d,\"polls\":%u,\"sensor_updates\":%u,"
                 "\"hold_progress\":%.3f,\"hold_armed\":%d",
                 s.hand == 0 ? "right" : "left", s.mode, s.selected, s.selectedData,
                 s.firstShowing, s.numShowing, s.numData, s.numItems, s.highlightItem,
                 s.scrollable ? 1 : 0, s.isScrolling ? 1 : 0, s.atTop ? 1 : 0, s.atBottom ? 1 : 0,
                 s.scrollDir, s.enabled ? 1 : 0, s.selectionEnabled ? 1 : 0, s.handHeight,
                 s.swipeAmt, s.percentPulled, s.hasDirection ? 1 : 0, s.lockedIn ? 1 : 0, s.nodes,
                 s.hoverMs, s.hadProgress ? 1 : 0, s.maxSwipeAmt, s.maxNodes, s.polls,
                 s.sensorUpdates,
                 now - s.holdShownAt < 0.1f && now >= s.holdShownAt ? s.holdProgress : 0.0f,
                 s.hold.Armed() ? 1 : 0);
        out += buf;
        if (rows && s.rowsValid) {
            out += ",\"rows\":[";
            for (size_t r = 0; r < s.rows.size(); r++) {
                if (r)
                    out += ',';
                AppendEscaped(out, s.rows[r]);
            }
            out += "],\"row_flags\":[";
            for (size_t r = 0; r < s.rowFlags.size(); r++) {
                if (r)
                    out += ',';
                out += (char)('0' + s.rowFlags[r]);
            }
            out += ']';
        }
        out += '}';
        if (reset) {
            s.maxSwipeAmt = 0.0f;
            s.maxNodes = 0;
            s.polls = 0;
            s.sensorUpdates = 0;
        }
    }
    out += "],\"events\":[";
    first = true;
    for (size_t i = 0; i < gEvents.size(); i++) {
        const SelectEvent &e = gEvents[i];
        if (e.seq <= since)
            continue;
        if (!first)
            out += ',';
        first = false;
        snprintf(buf, sizeof(buf), "{\"seq\":%u,\"ui_seconds\":%.4f,\"list\":", e.seq, e.uiSeconds);
        out += buf;
        AppendEscaped(out, e.list);
        out += ",\"sym\":";
        AppendEscaped(out, e.sym);
        snprintf(buf, sizeof(buf), ",\"hand\":\"%s\",\"selected\":%d,\"first_showing\":%d,\"source\":\"%s\"}",
                 e.hand == 0 ? "right" : "left", e.selected, e.firstShowing,
                 e.source == kSourceButton ? "button" : e.source == kSourceHold ? "hold" : "swipe");
        out += buf;
    }
    out += "]}";
    // Rows are captured by the lists' next polls and dropped again once read.
    if (!rows) {
        for (size_t i = 0; i < gLists.size(); i++) {
            gLists[i].rowsValid = false;
            gLists[i].rows.clear();
            gLists[i].rowFlags.clear();
        }
    }
    gWantRows = rows;
    return out;
}

// ---- hold to select ---------------------------------------------------------

namespace {

float EnvSeconds(const char *name, float fallback, float lo, float hi) {
    const char *v = getenv(name);
    if (!v || !*v)
        return fallback;
    char *end = nullptr;
    double ms = strtod(v, &end);
    if (end == v || *end != '\0' || !(ms >= lo * 1000.0f) || !(ms <= hi * 1000.0f)) {
        fprintf(stderr, "%s='%s' ignored (want %.0f .. %.0f ms)\n", name, v, lo * 1000.0f,
                hi * 1000.0f);
        return fallback;
    }
    return (float)(ms * 0.001);
}

// A song in play and not paused: no hold there, whatever lists it polls.
bool InGameplay() {
    return TheGamePanel && TheGamePanel->GetState() == UIPanel::kUp
        && (TheGamePanel->InIntroState() || TheGamePanel->IsPlayingState())
        && !(TheGame && TheGame->Paused());
}

} // namespace

const HoldParams &Params() {
    static HoldParams sParams;
    static bool sRead = false;
    if (!sRead) {
        sRead = true;
        HoldParams d;
        sParams.hold = EnvSeconds("DC3_MENU_HOLD_MS", d.hold, 0.2f, 10.0f);
        sParams.holdLeft = EnvSeconds("DC3_MENU_HOLD_LEFT_MS", d.holdLeft, 0.2f, 10.0f);
        sParams.settle = EnvSeconds("DC3_MENU_HOLD_SETTLE_MS", d.settle, 0.0f, 5.0f);
        sParams.rearm = EnvSeconds("DC3_MENU_HOLD_REARM_MS", d.rearm, 0.0f, 20.0f);
    }
    return sParams;
}

void HoldTimer::Disarm(float until) {
    mNeedMove = true;
    mHaveAnchor = false;
    mRearmAt = until;
    mHeld = 0.0f;
    mPaused = 0.0f;
    mProgress = 0.0f;
}

HoldTimer::Result HoldTimer::Tick(const Input &in, const HoldParams &p) {
    float dt = in.now - mLast;
    bool fresh = !mTicked || dt > p.gap || dt < 0.0f;
    mTicked = true;
    mLast = in.now;
    if (fresh) {
        // The list was away (a transition, its select animation, another
        // screen): nothing held carries over, and a hand that is already on
        // it has not chosen anything on this screen yet.
        dt = 0.0f;
        mHeld = 0.0f;
        mPaused = 0.0f;
        mProgress = 0.0f;
        mDown = 0.0f;
        mRow = -1;
        if (in.engaged)
            Disarm(in.now + p.rearm);
    }
    if (in.lastSelect > mSelectSeen) {
        mSelectSeen = in.lastSelect;
        Disarm(in.lastSelect + p.rearm);
    }
    if (!in.engaged) {
        // The arm is down (or off the list): dropping it re-arms.
        mDown += dt;
        if (mNeedMove && mDown >= p.rearmDown)
            mNeedMove = false;
        mHeld = 0.0f;
        mPaused = 0.0f;
        mProgress = 0.0f;
        mRow = -1;
        return kIdle;
    }
    mDown = 0.0f;
    if (in.row != mRow) {
        mRow = in.row;
        mHeld = 0.0f;
        mPaused = 0.0f;
    }
    if (mNeedMove) {
        if (!mHaveAnchor) {
            mAnchor = in.handHeight;
            mHaveAnchor = true;
        }
        if (fabsf(in.handHeight - mAnchor) >= p.rearmMove || in.now >= mRearmAt)
            mNeedMove = false; // the hold itself starts with the next tick
        mHeld = 0.0f;
        mPaused = 0.0f;
        mProgress = 0.0f;
        return kIdle;
    }
    if (in.blocked) {
        mHeld = 0.0f;
        mPaused = 0.0f;
        mProgress = 0.0f;
        return kIdle;
    }
    if (in.swipe) {
        // The swipe has the ribbon; the hold waits for it, a while.
        mPaused += dt;
        if (mPaused >= p.swipePause)
            mHeld = 0.0f;
        mProgress = 0.0f;
        return kIdle;
    }
    mPaused = 0.0f;
    mHeld += dt;
    if (mHeld >= p.settle + p.hold) {
        // Disarmed at once: the select this makes is seen through
        // in.lastSelect only on the next tick.
        Disarm(in.now + p.rearm);
        mProgress = 1.0f;
        return kFire;
    }
    mProgress = mHeld <= p.settle ? 0.0f : (mHeld - p.settle) / p.hold;
    return mProgress > 0.0f ? kProgress : kIdle;
}

bool HandInListPose(const HamNavList *list, const Skeleton &skeleton) {
    return list && Dc3MenuGestureAccess::HandInListPose(list, skeleton);
}

} // namespace Dc3MenuGesture

bool Dc3MenuGestureAccess::UpdateHold(HamNavList *l, bool handValid, List &s, float lastSelect,
                                      int *source) {
    using namespace Dc3MenuGesture;
    int mode = Dc3MenuSelect();
    DirectionGestureFilter *f = l->mDirectionGestureFilter;
    if (mode == kDc3MenuSelectSwipe || !f || InGameplay()) {
        s.hold.Reset();
        return false;
    }
    bool holdOnly = mode == kDc3MenuSelectHold;
    // hold: a swipe does not select (the image's arm below never sees one).
    if (holdOnly && f->HasDirection())
        f->ClearSwipe();
    const HoldParams &p = Params();
    HoldTimer::Input in;
    in.now = TheTaskMgr.UISeconds();
    in.row = l->mListState.Selected();
    in.handHeight = l->mHandHeight;
    in.engaged = handValid && l->mSelectionEnabled && in.handHeight >= -p.span
        && in.handHeight <= 1.0f + p.span;
    in.blocked = l->mScrollBehavior.IsScrolling() || l->mScrollBehavior.mScrollDir != 0;
    in.swipe = !holdOnly && (f->HasDirection() || f->GetPercentPulled() > 0.0f);
    in.lastSelect = lastSelect;
    // The left hand's list (Back) holds longer: see HoldParams::holdLeft.
    HoldParams forList = p;
    if (l->mNavInputType == HamNavList::kNavInput_LeftHand)
        forList.hold = p.holdLeft;
    HoldTimer::Result r = s.hold.Tick(in, forList);
    if (r == HoldTimer::kFire) {
        // What the image's swipe arm does (HamNavList::UpdateGestures).
        f->ClearSwipe();
        static Message forceLetterboxOff(Symbol("force_letterbox_off"));
        TheUI->Handle(forceLetterboxOff, false);
        *source = kSourceHold;
        l->SetSelecting(false);
        *source = -1;
        return true;
    }
    if (!handValid)
        return false; // the image's own arm: clear the swipe, return
    if (r == HoldTimer::kProgress) {
        l->SetSliding(s.hold.Progress());
        s.holdShownAt = in.now;
        s.holdProgress = s.hold.Progress();
        return true;
    }
    if (holdOnly) {
        // Never the swipe's slide: the list swells, its highlight follows the hand.
        l->SetSwelling();
        return true;
    }
    return false;
}

namespace Dc3MenuGesture {

bool UpdateHold(HamNavList *list, bool handValid) {
    // Under swipe (the default) a list that never held has nothing to reset.
    if (Dc3MenuSelect() == kDc3MenuSelectSwipe)
        return false;
    return Dc3MenuGestureAccess::UpdateHold(list, handValid, Find(list), gLastSelect, &gNextSource);
}

bool HoldShowing(const HamNavList *list) {
    if (Dc3MenuSelect() == kDc3MenuSelectSwipe)
        return false;
    float now = Now();
    for (size_t i = 0; i < gLists.size(); i++) {
        if (gLists[i].list == list)
            return gLists[i].holdShownAt == now;
    }
    return false;
}

namespace {

const char *kNoticeKey = "menu-select";
const float kNoticeSeconds = 4.0f;
bool gNoticeUp = false;
std::chrono::steady_clock::time_point gNoticeDown;

void PostNotice(const char *text) {
    NativeNotice::Post(kNoticeKey, text, "", kNoticeSeconds);
    // Taken down again by PollKey: a notice left posted hides older ones
    // (NativeNotice::Current shows the newest only).
    gNoticeUp = true;
    gNoticeDown = std::chrono::steady_clock::now()
        + std::chrono::milliseconds((long long)(kNoticeSeconds * 1000.0f) + 500);
}

void Announce(int mode) {
    const HoldParams &p = Params();
    char text[160];
    if (mode == kDc3MenuSelectHold)
        snprintf(text, sizeof(text),
                 "Menu select: HOLD. Keep your hand on a row for %.1f s (Back: %.1f s). F1 changes it.",
                 p.settle + p.hold, p.settle + p.holdLeft);
    else if (mode == kDc3MenuSelectBoth)
        snprintf(text, sizeof(text),
                 "Menu select: SWIPE or HOLD (%.1f s on a row). F1 changes it.", p.settle + p.hold);
    else
        snprintf(text, sizeof(text),
                 "Menu select: SWIPE. Sweep your forearm across your body. F1 changes it.");
    PostNotice(text);
}

int gMode = -1; // not read yet

} // namespace

void SetArcStartsOnInferred(bool on, bool notice) {
    sArcStartsOnInferred = on ? 1 : 0;
    if (notice)
        PostNotice(on ? "Sweep start: also on an arm the camera half sees (inferred)."
                      : "Sweep start: only on an arm the camera sees well (tracked).");
}

// From PollKey (the desktop loop) and from NotePoll (every frame a list is
// polled, on every platform: the web loop has no PollKey).
void ExpireNotice() {
    if (gNoticeUp && std::chrono::steady_clock::now() >= gNoticeDown) {
        NativeNotice::Clear(kNoticeKey);
        gNoticeUp = false;
    }
}

void PollKey() {
    ExpireNotice();
#if !defined(__EMSCRIPTEN__)
    // F1: free in the keyboard-as-pad map (JoypadKeyMap.h) and in cheats.dta.
    // Sticky keys are on (Joypad_Native.cpp): a tap reads as pressed once.
    static bool sWasDown = false;
    if (!gNativeWindow)
        return;
    bool down = glfwGetKey(gNativeWindow, GLFW_KEY_F1) == GLFW_PRESS;
    if (down && !sWasDown)
        Dc3SetMenuSelect((Dc3MenuSelect() + 1) % kDc3MenuSelectCount);
    sWasDown = down;
#endif
}

} // namespace Dc3MenuGesture

// ---- the switch (declared in NativeSettings.h) ------------------------------

const char *Dc3MenuSelectName(int mode) {
    return mode == kDc3MenuSelectHold ? "hold" : mode == kDc3MenuSelectBoth ? "both" : "swipe";
}

int Dc3MenuSelectParse(const char *text) {
    if (!text)
        return -1;
    for (int m = 0; m < kDc3MenuSelectCount; m++) {
        if (strcasecmp(text, Dc3MenuSelectName(m)) == 0)
            return m;
    }
    return -1;
}

int Dc3MenuSelect() {
    using namespace Dc3MenuGesture;
    if (gMode < 0) {
        gMode = kDc3MenuSelectSwipe;
        const char *v = getenv("DC3_MENU_SELECT");
        if (v && *v) {
            int m = Dc3MenuSelectParse(v);
            if (m < 0)
                fprintf(stderr, "DC3_MENU_SELECT='%s' ignored (want swipe, hold or both)\n", v);
            else
                gMode = m;
        }
        const HoldParams &p = Params();
        fprintf(stderr,
                "DC3 MENU SELECT: %s (hold %.0f ms, the left hand's list %.0f ms, after %.0f ms on a "
                "row, re-arm %.0f ms; F1 or {native_settings set_menu_select swipe|hold|both} "
                "changes it)\n",
                Dc3MenuSelectName(gMode), p.hold * 1000.0f, p.holdLeft * 1000.0f, p.settle * 1000.0f,
                p.rearm * 1000.0f);
        if (gMode != kDc3MenuSelectSwipe)
            Announce(gMode);
    }
    return gMode;
}

void Dc3SetMenuSelect(int mode) {
    using namespace Dc3MenuGesture;
    if (mode < 0 || mode >= kDc3MenuSelectCount)
        return;
    Dc3MenuSelect(); // read the variable (and say so) before the first change
    gMode = mode;
    fprintf(stderr, "DC3 MENU SELECT: now %s\n", Dc3MenuSelectName(mode));
    Announce(mode);
}

void Dc3MenuSelectFramePoll() {
    Dc3MenuGesture::PollKey();
    // The mouse and the finger in the menus (np-pointer): the same once-a-frame
    // point, so App.cpp needs no second hook.
    Dc3Pointer::FramePoll();
}

// ---- the sweep's start (declared in NativeSettings.h) -----------------------

const char *Dc3MenuArcStartName() {
    return Dc3MenuGesture::ArcStartsOnInferred() ? "inferred" : "tracked";
}

bool Dc3SetMenuArcStart(const char *text) {
    bool inferred = text && strcasecmp(text, "inferred") == 0;
    if (!inferred && (!text || strcasecmp(text, "tracked") != 0))
        return false;
    Dc3MenuGesture::ArcStartsOnInferred(); // read the variable (and say so) first
    Dc3MenuGesture::SetArcStartsOnInferred(inferred, true);
    fprintf(stderr, "DC3 MENU ARC START: now %s\n", Dc3MenuArcStartName());
    return true;
}

#endif // HX_NATIVE
