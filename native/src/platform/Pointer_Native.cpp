// Pointer input for the game's own menus (np-pointer).  Pointer_Native.h has
// what a player can do; PointerGesture.h the rules.
//
// Main thread except Push (any thread) and StateJson (any thread; it reads a
// snapshot FramePoll writes when asked for one).

#include "Pointer_Native.h"

#include "NativeSettings.h" // Dc3EnvFlag
#include "SongBrowserHooks_Native.h"
#include "gesture/GestureMgr.h"
#include "hamobj/HamListRibbon.h"
#include "hamobj/HamNavList.h"
#include "math/Mtx.h"
#include "meta_ham/HamUI.h"
#include "meta_ham/ShellInput.h"
#include "os/Joypad.h"
#include "os/JoypadMsgs.h"
#include "os/System.h"
#include "rndobj/Anim.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "synth/Synth.h"
#include "ui/UI.h"
#include "ui/UIScreen.h"
#include "ui/UIListProvider.h"
#include "ui/UIListWidget.h"

#include "platform/Rnd_Wgpu.h"
extern WgpuRnd *gWgpuRnd;
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include "gfx/ImGuiBackend.h"
#include <GLFW/glfw3.h>
extern GLFWwindow *gNativeWindow;
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

using namespace Dc3PointerGesture;

extern bool Dc3ControllerModeForced();
extern void Dc3ControllerHoldNotePress();
extern void Dc3ControllerHoldNoteTaken();

// HamNavList's private parts, for the pointer (HamNavList.h befriends it).
struct Dc3PointerAccess {
    static bool CanAct(HamNavList *l) {
        if (!l->mEnabled || l->mRefreshPending || !l->mListState.Provider())
            return false;
        if (l->mRibbonMode == HamListRibbon::kRibbonSelect || l->IsAnimating())
            return false;
        // a screen on its way in or out: the pad's presses are not taken
        // then either (UIManager drops them during a transition)
        if (TheUI && TheUI->InTransition())
            return false;
        // HamNavList::SkipPoll's focus rule: a right-hand list that is used
        // only while focused is not this list's to act on otherwise
        if (l->mNavInputType == HamNavList::kNavInput_RightHand && l->mOnlyUseWhenFocused
            && TheUI && TheUI->FocusComponent() != l)
            return false;
        return !(TheGestureMgr && TheGestureMgr->GesturingWithVoice());
    }
    static bool Scrolling(HamNavList *l) {
        return l->mListState.ScrollPastMinDisplay() && l->mScrollBehavior.IsScrolling();
    }
    static int Selected(HamNavList *l) { return l->mListState.Selected(); }
    static bool IsActive(HamNavList *l, int showing) {
        UIListProvider *p = l->mListState.Provider();
        return p && showing >= 0 && showing < l->mListState.NumShowing() && p->IsActive(showing);
    }
    static bool Scrollable(const HamNavList *l) { return l->mListState.ScrollPastMinDisplay(); }
    static int FirstShowing(const HamNavList *l) { return l->mListState.FirstShowing(); }
    static void Highlight(HamNavList *l, int showing) { l->SetHighlight(showing); }
    static bool Select(HamNavList *l) {
        if (!l->mSelectionEnabled)
            return false;
        l->SetSelecting(true);
        return true;
    }
    // One row of scroll: > 0 later rows.  False: nothing more that way (an
    // end of the list), or not now (a scroll is still moving: try again).
    enum StepResult { kStepDone, kStepWait, kStepEnd };
    static StepResult Step(HamNavList *l, int dir) {
        if (l->mListState.ScrollPastMinDisplay()) {
            HamScrollBehavior &s = l->mScrollBehavior;
            if (s.IsScrolling())
                return kStepWait;
            if (dir > 0 ? s.AtBottom() : s.AtTop())
                return kStepEnd;
            const bool moved = dir > 0 ? s.ScrollDown(false) : s.ScrollUp(false);
            return moved ? kStepDone : kStepWait;
        }
        // a list that does not scroll: the highlight moves, as the pad's does
        int selected = l->mListState.Selected();
        do {
            selected += dir;
            if (selected < 0 || selected >= l->mListState.NumShowing())
                return kStepEnd;
        } while (!l->mListState.Provider()->IsActive(selected));
        l->SetHighlight(selected);
        return kStepDone;
    }
    static const char *Name(const HamNavList *l) { return l->Name(); }
};

namespace Dc3Pointer {
static bool PresentInfo(Rect &present, int &surfW, int &surfH);
namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point kEpoch = Clock::now();

struct Pending {
    Input in;
    float due;
    bool window;
};

std::mutex gQueueMutex;
std::vector<Pending> gQueue;

int gEnabled = -1; // -1: DC3_POINTER not read yet

struct DrawnList {
    std::vector<Row> rows;
    float pitch = 0.0f;
    unsigned frame = 0;
    bool scrollable = false;
    int firstShowing = 0;
};
std::map<HamNavList *, DrawnList> gDrawn;

Tracker gTracker;
Params gParams;
bool gParamsRead = false;

int gPendingSteps = 0;
HamNavList *gStepList = nullptr;
float gLastX = -1.0f, gLastY = -1.0f; // where the pointer was last seen
float gHoverX = -1.0f, gHoverY = -1.0f; // the browser hover last applied (one count per move)

// a press of the pad: down this frame, up the next
JoypadButton gHeldButton = kPad_NumButtons;

// what the pointer did, for StateJson
unsigned gTaps = 0, gTapSelects = 0, gTapPresses = 0, gTapHighlights = 0, gHovers = 0, gBacks = 0;
// the screen a press went down on (a press that outlives its screen is dropped)
UIScreen *gDownScreen = nullptr;
int gStepsDone = 0;
std::string gLastAction = "none";

std::mutex gStateMutex;
std::condition_variable gStateCv;
std::atomic<bool> gStateWanted{false};
std::string gStateSnapshot;
unsigned gStateSeq = 0;

float EnvFloat(const char *name, float def) {
    const char *v = std::getenv(name);
    return v && *v ? (float)std::atof(v) : def;
}

const Params &P() {
    if (!gParamsRead) {
        gParamsRead = true;
        gParams.slop = EnvFloat("DC3_POINTER_SLOP", gParams.slop);
        gParams.flickVelocity = EnvFloat("DC3_POINTER_FLICK", gParams.flickVelocity);
        gParams.friction = EnvFloat("DC3_POINTER_FRICTION", gParams.friction);
    }
    return gParams;
}

// How far a row reaches back past the point the list reports, in row
// pitches.  MEASURED on the main menu (np-pointer, mainmenu_calib.png): the
// element position is where the label starts, and DC3's ribbons run from a
// little before it to the edge of the screen on the list's side -- the right
// hand's list (x 871) is lit from ~785 to 1280, the help bar's left list
// (x 346) from 0 to ~430.  1.7 pitches is 87 px on the main menu.
float RowLeadPitches() {
    static float v = EnvFloat("DC3_POINTER_ROW_LEAD", 1.7f);
    return v;
}

JoypadAction ActionFor(JoypadButton btn, Symbol type) { return PadButtonAction(btn, type); }

void PressButton(JoypadButton btn) {
    JoypadData *pad = JoypadGetPadData(0);
    if (!pad)
        return;
    if (gHeldButton != kPad_NumButtons)
        return; // one at a time
    ButtonDownMsg msg(pad->mUser, btn, ActionFor(btn, pad->mControllerType), 0);
    JoypadPushThroughMsg(msg);
    gHeldButton = btn;
}

void ReleaseButton() {
    if (gHeldButton == kPad_NumButtons)
        return;
    JoypadData *pad = JoypadGetPadData(0);
    if (pad) {
        ButtonUpMsg msg(pad->mUser, gHeldButton, ActionFor(gHeldButton, pad->mControllerType), 0);
        JoypadPushThroughMsg(msg);
    }
    gHeldButton = kPad_NumButtons;
}

// Whether any list drawn last frame has a row to aim at.  The title's lists
// are drawn with none.
bool AnyRows() {
    for (auto &kv : gDrawn)
        if (!kv.second.rows.empty())
            return true;
    return false;
}

// A deliberate act of the pointer on a list (a row tap, a scroll): the same
// rule a press of the pad obeys (Fable review: the pointer used to force the
// menus with EnterControllerMode(true), skipping all of this).
//  - HamUI::OnMsg(ButtonDownMsg): every press restarts ShellInput's idle timer
//    and arms the press hold (PLAN H-157), so the menus stay the pointer's
//    while it is used.
//  - ShellInput::OnMsg(ButtonDownMsg), outside controller mode: not on a
//    gameplay panel (the press is the game's); otherwise EnterControllerMode
//    (false), which the helpbar may refuse (helpbar_allow_controller FALSE on
//    loading, meta_loading, about, perform_endgame) -- then the pad's press
//    plays invalid_select and does nothing, and so does the pointer's; when
//    it takes them, the notice.
// The pad's first press only takes the menus; the pointer's act also acts,
// since it was aimed at a row.  Taps that are a press of the pad (A on a
// screen with no rows, the right button's B) go through the pad's own path
// instead (PressButton) and need none of this.
// True: the act may go on.
bool PointerAct() {
    Dc3ControllerHoldNotePress();
    static ResetControllerModeTimeoutMsg resetControllerModeTimeoutMsg;
    TheHamUI.Export(resetControllerModeTimeoutMsg, true);
    if (!TheGestureMgr || TheGestureMgr->InControllerMode())
        return true;
    ShellInput *shell = TheHamUI.GetShellInput();
    if (!shell || !shell->mInputPanel)
        return true;
    static Symbol is_gameplay_panel("is_gameplay_panel");
    const DataNode *prop = shell->mInputPanel->Property(is_gameplay_panel, false);
    if (prop && prop->Int() == 1)
        return true;
    shell->EnterControllerMode(false);
    if (!TheGestureMgr->InControllerMode()) {
        if (TheSynth)
            TheSynth->RunFlow("invalid_select.flow");
        gLastAction = "the helpbar keeps the menus from the pointer here";
        return false;
    }
    Dc3ControllerHoldNoteTaken();
    return true;
}

// The list (drawn last frame) and row under (x, y).  The focused list wins
// where two overlap.
HamNavList *ListAt(float x, float y, int &row) {
    HamNavList *best = nullptr;
    row = -1;
    UIComponent *focus = TheUI ? TheUI->FocusComponent() : nullptr;
    for (auto &kv : gDrawn) {
        int r = HitRow(kv.second.rows, x, y);
        if (r < 0)
            continue;
        if (!best || (UIComponent *)kv.first == focus) {
            best = kv.first;
            row = r;
        }
    }
    return best;
}

// The list a scroll goes to: the one under the pointer, or the focused one,
// or the only one drawn.
HamNavList *ScrollTarget(float x, float y) {
    int row;
    HamNavList *under = ListAt(x, y, row);
    if (under)
        return under;
    // nearest list by its rows' vertical span and horizontal band
    UIComponent *focus = TheUI ? TheUI->FocusComponent() : nullptr;
    for (auto &kv : gDrawn)
        if ((UIComponent *)kv.first == focus)
            return kv.first;
    HamNavList *biggest = nullptr;
    size_t most = 0;
    for (auto &kv : gDrawn) {
        if (kv.second.rows.size() > most) {
            most = kv.second.rows.size();
            biggest = kv.first;
        }
    }
    return biggest;
}

void WriteState() {
    std::string j = "{\"enabled\":";
    j += Enabled() ? "true" : "false";
    char buf[640];
    snprintf(buf, sizeof buf,
             ",\"controller_mode\":%s,\"browser\":%s,\"pointer\":[%.1f,%.1f],\"pressed\":%s,"
             "\"dragging\":%s,\"flinging\":%s,\"pending_steps\":%d,\"taps\":%u,\"tap_selects\":%u,"
             "\"tap_presses\":%u,\"tap_highlights\":%u,\"hovers\":%u,\"backs\":%u,"
             "\"steps_done\":%d,\"screen\":\"%s\",\"last\":\"%s\"",
             TheGestureMgr && TheGestureMgr->InControllerMode() ? "true" : "false",
             NativeSongBrowser::HidesGameChrome() ? "true" : "false", gLastX, gLastY,
             gTracker.Pressed() ? "true" : "false", gTracker.Dragging() ? "true" : "false",
             gTracker.Flinging() ? "true" : "false", gPendingSteps, gTaps, gTapSelects,
             gTapPresses, gTapHighlights, gHovers, gBacks, gStepsDone,
             TheUI && TheUI->CurrentScreen() ? TheUI->CurrentScreen()->Name() : "",
             gLastAction.c_str());
    j += buf;
    {
        Rect pr;
        int sw = 0, sh = 0;
        PresentInfo(pr, sw, sh);
        snprintf(buf, sizeof buf,
                 ",\"present\":{\"surface_w\":%d,\"surface_h\":%d,\"x\":%.0f,\"y\":%.0f,"
                 "\"w\":%.0f,\"h\":%.0f,\"frame_w\":%d,\"frame_h\":%d}",
                 sw, sh, pr.x, pr.y, pr.w, pr.h, TheRnd.Width(), TheRnd.Height());
        j += buf;
    }
    j += ",\"lists\":[";
    bool first = true;
    UIComponent *focus = TheUI ? TheUI->FocusComponent() : nullptr;
    for (auto &kv : gDrawn) {
        if (!first)
            j += ",";
        first = false;
        snprintf(buf, sizeof buf,
                 "{\"name\":\"%s\",\"focus\":%s,\"selected\":%d,\"first_showing\":%d,"
                 "\"scrollable\":%s,\"pitch\":%.1f,\"rows\":[",
                 Dc3PointerAccess::Name(kv.first),
                 (UIComponent *)kv.first == focus ? "true" : "false",
                 Dc3PointerAccess::Selected(kv.first), kv.second.firstShowing,
                 kv.second.scrollable ? "true" : "false", kv.second.pitch);
        j += buf;
        for (size_t i = 0; i < kv.second.rows.size(); i++) {
            const Row &r = kv.second.rows[i];
            snprintf(buf, sizeof buf, "%s{\"row\":%d,\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f,\"active\":%s}",
                     i ? "," : "", r.index, r.cx, r.cy, r.halfW * 2, r.halfH * 2,
                     r.active ? "true" : "false");
            j += buf;
        }
        j += "]}";
    }
    j += "]}";
    std::lock_guard<std::mutex> lk(gStateMutex);
    gStateSnapshot = j;
    gStateSeq++;
    gStateCv.notify_all();
}

#ifndef __EMSCRIPTEN__
// GLFW: the engine's ImGui installed its callbacks first; ours chain to them.
GLFWmousebuttonfun gPrevButton = nullptr;
GLFWcursorposfun gPrevCursor = nullptr;
GLFWscrollfun gPrevScroll = nullptr;
bool gInstalled = false;

bool ImGuiHasMouse() { return ImGuiBackend::WantCaptureMouse(); }

void OnCursor(GLFWwindow *w, double x, double y) {
    if (gPrevCursor)
        gPrevCursor(w, x, y);
    Input in;
    in.kind = kMove;
    in.t = Now();
    if (!WindowToFrame(x, y, in.x, in.y) && !gTracker.Pressed())
        return;
    if (!ImGuiHasMouse())
        Push(in);
}

void OnButton(GLFWwindow *w, int button, int action, int mods) {
    if (gPrevButton)
        gPrevButton(w, button, action, mods);
    if (ImGuiHasMouse() && action == GLFW_PRESS)
        return;
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
    Input in;
    in.t = Now();
    const bool inside = WindowToFrame(x, y, in.x, in.y);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS && !inside)
            return;
        in.kind = action == GLFW_PRESS ? kDown : kUp;
        Push(in);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        in.kind = kBack;
        Push(in);
    }
}

void OnScroll(GLFWwindow *w, double dx, double dy) {
    if (gPrevScroll)
        gPrevScroll(w, dx, dy);
    if (ImGuiHasMouse())
        return;
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
    Input in;
    in.kind = kWheel;
    in.t = Now();
    WindowToFrame(x, y, in.x, in.y);
    in.wheel = (float)-dy; // a notch toward the user = later rows
    Push(in);
}

void InstallGlfw() {
    if (gInstalled || !gNativeWindow)
        return;
    gInstalled = true;
    gPrevButton = glfwSetMouseButtonCallback(gNativeWindow, OnButton);
    gPrevCursor = glfwSetCursorPosCallback(gNativeWindow, OnCursor);
    gPrevScroll = glfwSetScrollCallback(gNativeWindow, OnScroll);
}
#endif

void Act(const Output &out) {
    const bool any = out.hover || out.tap || out.steps || out.back || out.dragging;
    if (out.hover) {
        gLastX = out.x;
        gLastY = out.y;
    }
    if (out.tap) {
        gLastX = out.tapX;
        gLastY = out.tapY;
    }
    // a press that stopped the list moving ends the scroll still owed (the
    // press picks nothing: PointerGesture's Tracker::SetListMoving)
    if (out.stopped) {
        gPendingSteps = 0;
        gLastAction = "a press stopped the list";
    }
    if (!any)
        return;
    // a tap ends any scroll still owed
    if (out.tap || out.back)
        gPendingSteps = 0;
    // Only a deliberate act takes the menus from the hands; a mouse that
    // moves or rests never does, nor a drag on a frame that scrolls nothing
    // (TakesTheMenus, tested).  Each such act below goes through PointerAct
    // (or is a press of the pad).

    // the natively drawn song browser has its own rows
    if (NativeSongBrowser::HidesGameChrome()) {
        if (out.back) {
            PressButton(kPad_Circle);
            gBacks++;
            gLastAction = "back";
        }
        if (out.steps && PointerAct())
            gPendingSteps += out.steps;
        if (out.hover && !out.tap
            && HoverMayHighlight(TheGestureMgr && TheGestureMgr->InControllerMode())) {
            if (NativeSongBrowser::PointerHover(out.x, out.y) && (out.x != gHoverX || out.y != gHoverY)) {
                gHovers++;
                gLastAction = "browser: hover a song row";
            }
            gHoverX = out.x;
            gHoverY = out.y;
        }
        if (out.tap && PointerAct()) {
            gTaps++;
            // a finger's first tap on a row highlights it, a tap on the
            // highlighted row picks it (a phone's rows are ~16 CSS px tall)
            switch (NativeSongBrowser::PointerTap(out.tapX, out.tapY, out.tapTouch)) {
            case 3:
                gLastAction = "browser: pick the tapped sort tab";
                break;
            case 2:
                gTapSelects++;
                gLastAction = "browser: pick the tapped row";
                break;
            case 1:
                gTapHighlights++;
                gLastAction = "browser: highlight the tapped row";
                break;
            default:
                gLastAction = "browser: tap off the song rows";
                break;
            }
        }
        return;
    }

    if (out.back) {
        PressButton(kPad_Circle);
        gBacks++;
        gLastAction = "back";
        return;
    }

    if (out.tap) {
        gTaps++;
        int row = -1;
        HamNavList *list = ListAt(out.tapX, out.tapY, row);
        if (list) {
            if (Dc3PointerAccess::CanAct(list) && Dc3PointerAccess::IsActive(list, row)
                && PointerAct()) {
                const bool wasSelected = Dc3PointerAccess::Selected(list) == row;
                if (!wasSelected)
                    Dc3PointerAccess::Highlight(list, row);
                if (out.tapTouch && !wasSelected) {
                    // a finger: the first tap highlights, the next picks
                    gTapHighlights++;
                    gLastAction = std::string("highlight ") + Dc3PointerAccess::Name(list) + " row "
                        + std::to_string(row);
                } else if (Dc3PointerAccess::Select(list)) {
                    gTapSelects++;
                    gLastAction = std::string("select ") + Dc3PointerAccess::Name(list) + " row "
                        + std::to_string(row);
                }
            }
        } else if (!AnyRows()) {
            // nothing to aim at (the title, a movie, a screen of buttons the
            // pad drives): a tap is the pad's A
            PressButton(kPad_X);
            gTapPresses++;
            gLastAction = "press A (no list drawn)";
        } else {
            PointerAct(); // aimed, if at nothing: the menus stay the pointer's
            gLastAction = "tap between rows";
        }
        return;
    }

    if (out.hover) {
        int row = -1;
        HamNavList *list = ListAt(out.x, out.y, row);
        if (list && Dc3PointerAccess::IsActive(list, row)) {
            if (HoverMayHighlight(TheGestureMgr && TheGestureMgr->InControllerMode())
                && Dc3PointerAccess::CanAct(list)
                && Dc3PointerAccess::Selected(list) != row) {
                Dc3PointerAccess::Highlight(list, row);
                gHovers++;
                gLastAction = std::string("hover ") + Dc3PointerAccess::Name(list) + " row "
                    + std::to_string(row);
            }
        }
    }

    if (out.steps) {
        HamNavList *target = ScrollTarget(gLastX, gLastY);
        if (target && PointerAct()) {
            if (target != gStepList)
                gPendingSteps = 0;
            gStepList = target;
            gPendingSteps += out.steps;
        }
    }
}

// One row a frame at most, at the list's own pace.
void RunSteps() {
    // the browser came or went: what was owed belonged to the other list
    static bool sWasBrowser = false;
    const bool browser = NativeSongBrowser::HidesGameChrome();
    if (browser != sWasBrowser) {
        sWasBrowser = browser;
        gPendingSteps = 0;
    }
    const int cap = (int)P().maxFlingRows;
    gPendingSteps = std::max(-cap, std::min(cap, gPendingSteps));
    if (!gPendingSteps)
        return;
    const int dir = gPendingSteps > 0 ? 1 : -1;
    if (NativeSongBrowser::HidesGameChrome()) {
        if (gHeldButton == kPad_NumButtons) {
            PressButton(dir > 0 ? kPad_DDown : kPad_DUp);
            gPendingSteps -= dir;
            gStepsDone++;
        }
        return;
    }
    if (!gStepList || !gDrawn.count(gStepList)) {
        gPendingSteps = 0;
        return;
    }
    if (!Dc3PointerAccess::CanAct(gStepList))
        return;
    switch (Dc3PointerAccess::Step(gStepList, dir)) {
    case Dc3PointerAccess::kStepDone:
        gPendingSteps -= dir;
        gStepsDone++;
        gLastAction = std::string("scroll ") + Dc3PointerAccess::Name(gStepList)
            + (dir > 0 ? " +1" : " -1");
        break;
    case Dc3PointerAccess::kStepEnd:
        gPendingSteps = 0;
        break;
    case Dc3PointerAccess::kStepWait:
        break;
    }
}

} // namespace

JoypadAction PadButtonAction(JoypadButton btn, Symbol controllerType) {
    JoypadAction action = ButtonToAction(btn, controllerType);
    if (action != kAction_None)
        return action;
    switch (btn) { // milo-native-engine Joypad_Native.cpp nativeButtonToAction
    case kPad_X: return kAction_Confirm;
    case kPad_Circle: return kAction_Cancel;
    case kPad_Start: return kAction_Start;
    case kPad_Select: return kAction_Option;
    case kPad_DUp: return kAction_Up;
    case kPad_DDown: return kAction_Down;
    case kPad_DLeft: return kAction_Left;
    case kPad_DRight: return kAction_Right;
    case kPad_L1: return kAction_PageUp;
    case kPad_R1: return kAction_PageDown;
    case kPad_Square: return kAction_ViewModify;
    case kPad_Tri: return kAction_ShellOption;
    default: return kAction_None;
    }
}

bool Enabled() {
    if (gEnabled < 0)
        gEnabled = Dc3EnvFlag("DC3_POINTER", true) ? 1 : 0;
    return gEnabled == 1;
}

void SetEnabled(bool on) { gEnabled = on ? 1 : 0; }

float Now() { return std::chrono::duration<float>(Clock::now() - kEpoch).count(); }

void Push(const Input &in, float delaySeconds, bool windowSpace) {
    std::lock_guard<std::mutex> lk(gQueueMutex);
    gQueue.push_back({in, Now() + delaySeconds, windowSpace});
}

// Where the engine last put the frame on its surface, in surface pixels, and
// the surface's size: the real picture rectangle, whatever the window or the
// canvas is.  Desktop: GpuDevice::LastPresentRect, written by every present
// (the PresentScale letterbox of a fixed frame, else the whole surface).  Web:
// the engine has no present pass there -- the frame is drawn straight into
// the canvas buffer -- so the picture is the whole surface, and where that
// buffer sits on the page is the page's business (dc3_pre.js dc3PictureBox
// sends the point relative to it).
static bool PresentInfo(Rect &present, int &surfW, int &surfH) {
    if (!gWgpuRnd)
        return false;
    GpuDevice &g = gWgpuRnd->Gpu();
    surfW = g.SurfaceWidth();
    surfH = g.SurfaceHeight();
#ifdef __EMSCRIPTEN__
    // the web device keeps no surface size; the canvas's buffer is the surface
    int cw = 0, ch = 0;
    if (g.CanvasSize(cw, ch) && cw > 0 && ch > 0) {
        surfW = cw;
        surfH = ch;
    }
#endif
    present = {0.0f, 0.0f, (float)surfW, (float)surfH};
#ifndef __EMSCRIPTEN__
    const PresentRect r = g.LastPresentRect();
    if (r.w > 0 && r.h > 0)
        present = {(float)r.x, (float)r.y, (float)r.w, (float)r.h};
#endif
    return surfW > 0 && surfH > 0;
}

bool BoxToFramePresent(float bx, float by, float boxW, float boxH, float &fx, float &fy,
                       float *unitPx) {
    Rect present;
    int surfW = 0, surfH = 0;
    const float fw = TheRnd.Width() > 0 ? (float)TheRnd.Width() : 1280.0f;
    const float fh = TheRnd.Height() > 0 ? (float)TheRnd.Height() : 720.0f;
    if (!PresentInfo(present, surfW, surfH) || boxW <= 0 || boxH <= 0) {
        if (unitPx)
            *unitPx = boxW > 0 ? fw / FitFrame(fw, fh, boxW, boxH).w : 1.0f;
        return BoxToFrame(bx, by, boxW, boxH, fw, fh, true, fx, fy);
    }
    if (unitPx) // frame pixels per box pixel (a CSS pixel on the web)
        *unitPx = present.w > 0 ? (surfW / boxW) * (fw / present.w) : 1.0f;
    // the box (window points / canvas CSS pixels) -> surface pixels (HiDPI,
    // a canvas buffer that is not its CSS size)
    return PresentToFrame(bx * surfW / boxW, by * surfH / boxH, present, fw, fh, fx, fy);
}

bool WindowToFrame(double wx, double wy, float &fx, float &fy) {
#ifndef __EMSCRIPTEN__
    if (gNativeWindow) {
        int ww = 0, wh = 0;
        glfwGetWindowSize(gNativeWindow, &ww, &wh);
        return BoxToFramePresent((float)wx, (float)wy, (float)ww, (float)wh, fx, fy);
    }
#endif
    fx = (float)wx;
    fy = (float)wy;
    return true;
}

void NoteListDrawn(HamNavList *list, const UIListWidgetDrawState &state, const Transform &world,
                   const RndCam *cam) {
    if (!list || !cam || !Enabled())
        return;
    DrawnList &d = gDrawn[list];
    d.frame = TheRnd.GetFrameID();
    d.rows.clear();
    d.scrollable = Dc3PointerAccess::Scrollable(list);
    d.firstShowing = Dc3PointerAccess::FirstShowing(list);
    const float fw = (float)TheRnd.Width(), fh = (float)TheRnd.Height();
    for (const UIListElementDrawState &e : state.mElements) {
        if (!e.mActive || e.mAlpha <= 0.01f)
            continue;
        if (d.scrollable
            && (e.mShowing < d.firstShowing
                || e.mShowing >= d.firstShowing + HamListRibbon::sNumListSelectable))
            continue; // the rows gathered above and below the window are not targets
        Vector3 local(e.mPosX, e.mPosY, e.mPosZ), w;
        Multiply(local, world, w);
        Vector2 s;
        if (cam->WorldToScreen(w, s) <= 0.0f)
            continue; // behind the camera
        Row r;
        r.index = e.mShowing;
        r.cx = s.x * fw;
        r.cy = s.y * fh;
        r.active = Dc3PointerAccess::IsActive(list, e.mShowing);
        d.rows.push_back(r);
    }
    d.pitch = RowPitch(d.rows, 60.0f);
    // A row is never taller than this, whatever the gap to the next: the help
    // bar's left list puts its two rows at the top and the bottom of the
    // screen (pitch 642 on the main menu, MEASURED), and a row as tall as that
    // took a tap in the middle of the screen.
    const float rowHeight = std::min(d.pitch, 64.0f);
    for (Row &r : d.rows) {
        r.halfH = rowHeight * 0.5f;
        const float lead = rowHeight * RowLeadPitches();
        float left, right;
        if (r.cx > fw * 0.5f) { // a right-hand list: to the right edge
            left = r.cx - lead;
            right = fw;
        } else { // a left-hand list: from the left edge
            left = 0.0f;
            right = r.cx + lead;
        }
        r.cx = (left + right) * 0.5f;
        r.halfW = (right - left) * 0.5f;
    }
}

void Forget(HamNavList *list) {
    gDrawn.erase(list);
    if (gStepList == list) {
        gStepList = nullptr;
        gPendingSteps = 0;
    }
}

void FramePoll() {
#ifndef __EMSCRIPTEN__
    InstallGlfw();
#endif
    ReleaseButton();

    // lists not drawn last frame are gone from the screen
    const unsigned frame = TheRnd.GetFrameID();
    for (auto it = gDrawn.begin(); it != gDrawn.end();) {
        if (frame - it->second.frame > 2)
            it = gDrawn.erase(it);
        else
            ++it;
    }

    std::vector<Pending> due;
    const float now = Now();
    {
        std::lock_guard<std::mutex> lk(gQueueMutex);
        auto split = std::stable_partition(gQueue.begin(), gQueue.end(),
                                           [now](const Pending &p) { return p.due <= now; });
        due.assign(gQueue.begin(), split);
        gQueue.erase(gQueue.begin(), split);
    }

    if (!Enabled()) {
        gTracker.Reset();
        gPendingSteps = 0;
    } else {
        // A press that went down on another screen is not this one's: the
        // screen changed under the finger (the press took a row that moved
        // the menus on, a script), and its up must not tap here.
        UIScreen *screen = TheUI ? TheUI->CurrentScreen() : nullptr;
        auto dropPressOnScreenChange = [&](float t) {
            if (gTracker.Pressed() && screen != gDownScreen) {
                Input cancel;
                cancel.kind = kCancel;
                cancel.t = t;
                gTracker.Feed(cancel, P());
                gLastAction = "a press outlived its screen: dropped";
            }
        };
        dropPressOnScreenChange(now);
        // the list is still moving on its own: a press stops it and picks nothing
        gTracker.SetListMoving(gPendingSteps != 0
                               || (gStepList && gDrawn.count(gStepList)
                                   && Dc3PointerAccess::Scrolling(gStepList)));
        for (Pending &p : due) {
            if (p.window)
                WindowToFrame(p.in.x, p.in.y, p.in.x, p.in.y);
            if (p.in.kind != kWheel && p.in.kind != kBack) {
                gLastX = p.in.x;
                gLastY = p.in.y;
            }
            if (p.in.kind == kDown)
                gDownScreen = screen;
            gTracker.Feed(p.in, P());
        }
        // a row of the list under the pointer is what a drag of one row is
        float pitch = 60.0f;
        if (HamNavList *l = ScrollTarget(gLastX, gLastY))
            pitch = gDrawn[l].pitch;
        Act(gTracker.Tick(now, pitch, P()));
        RunSteps();
        // one line per action (the web has no HTTP server: the phone check
        // reads these from the console)
        static unsigned sReleases = 0;
        if (gTracker.Releases() != sReleases) {
            sReleases = gTracker.Releases();
            printf("[dc3-pointer] release: %.0f px/s over %d samples, %.3f s (fling at %.0f)\n",
                   gTracker.ReleaseVelocity(), gTracker.ReleaseSamples(), gTracker.ReleaseSpan(),
                   P().flickVelocity);
        }
        static std::string sLogged;
        static unsigned sLoggedCount = 0;
        const unsigned count = gTaps + gHovers + gBacks + (unsigned)gStepsDone;
        if (gLastAction != sLogged || count != sLoggedCount) {
            sLogged = gLastAction;
            sLoggedCount = count;
            printf("[dc3-pointer] %s (taps %u, selects %u, steps %d)\n", gLastAction.c_str(),
                   gTaps, gTapSelects, gStepsDone);
            fflush(stdout);
        }
    }

    if (gStateWanted.exchange(false))
        WriteState();
}

std::string StateJsonNow() {
    WriteState();
    std::lock_guard<std::mutex> lk(gStateMutex);
    return gStateSnapshot;
}

std::string StateJson() {
    std::unique_lock<std::mutex> lk(gStateMutex);
    const unsigned seq = gStateSeq;
    gStateWanted = true;
    gStateCv.wait_for(lk, std::chrono::milliseconds(2000), [seq] { return gStateSeq != seq; });
    return gStateSnapshot.empty() ? std::string("{\"enabled\":false,\"error\":\"no frame\"}")
                                  : gStateSnapshot;
}

} // namespace Dc3Pointer

#ifdef __EMSCRIPTEN__
// native/web/dc3_pre.js: a pointer event on the canvas.  (bx, by) in the
// canvas box's CSS pixels, boxW x boxH the box.  kind: Dc3PointerGesture::Kind.
// The picture is taken to be the frame's 16:9 rectangle centred in the box
// (the present scale's rule); a box that is 16:9 is the frame stretched.
// ageSeconds: how long ago the event happened (performance.now() - e.timeStamp):
// a 10 fps frame hands the page its events in a burst, and the release
// velocity needs their own times, not the burst's.
extern "C" EMSCRIPTEN_KEEPALIVE void dc3_pointer(int kind, float bx, float by, float boxW,
                                                 float boxH, float wheel, int touch,
                                                 float ageSeconds) {
    Input in;
    in.kind = (Kind)kind;
    // never earlier than the event before it (the samples stay in order)
    static float sLastT = 0.0f;
    in.t = std::max(sLastT, Dc3Pointer::Now() - std::max(0.0f, std::min(ageSeconds, 0.5f)));
    sLastT = in.t;
    in.wheel = wheel;
    in.touch = touch != 0;
    const bool inside =
        Dc3Pointer::BoxToFramePresent(bx, by, boxW, boxH, in.x, in.y, &in.unitPx);
    if (in.kind == kDown && !inside)
        return;
    Dc3Pointer::Push(in);
}
// The same JSON as GET /api/pointer/state, now (the web has no HTTP server;
// native/web/dc3_pre.js window.dc3PointerState()).  Valid until the next call.
extern "C" EMSCRIPTEN_KEEPALIVE const char *dc3_pointer_state() {
    static std::string sJson;
    sJson = Dc3Pointer::StateJsonNow();
    return sJson.c_str();
}
extern "C" EMSCRIPTEN_KEEPALIVE int dc3_pointer_enabled() { return Dc3Pointer::Enabled() ? 1 : 0; }
#endif
