// DC3 Native Port - System Implementation
// Replaces System_Xbox.cpp - POSIX implementations of system functions

#include "os/Archive.h"
#include "os/BlockMgr_p.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include "platform/ExecutablePath.h"
#include "NativeSettings.h"
#include "NativeNotice.h"
#include "utl/Loader.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"

#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Dc3EnvFlag — value-parsed boolean env gate for native runtime toggles.
//
//   unset / empty  -> defaultOn (the compiled default)
//   "0" "false" "off" "no" (case-insensitive) -> false
//   anything else  -> true
//
// Unlike bare getenv() truthiness (where DC3_FOO=0 still reads as "set" and so
// ENABLES a feature), this lets a script disable a default-on feature with
// DC3_FOO=0. Declared in native/src/platform/NativeSettings.h for native TUs and
// via a local `extern bool Dc3EnvFlag(const char*, bool);` from PPC units inside
// their #ifdef HX_NATIVE blocks (same idiom as Dc3RunPostPollFootPlant).
bool Dc3EnvFlag(const char *name, bool defaultOn) {
    const char *v = getenv(name);
    if (!v || !*v) return defaultOn;
    return !(strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0 ||
             strcasecmp(v, "off") == 0 || strcasecmp(v, "no") == 0);
}

// ---------------------------------------------------------------------------
// DC3_PROFILE -- the native build's defaults, in one place.
//
//   unset / empty / "dev" -> dev (the default): everything useful to a
//                            developer playing and dogfooding is on
//   "release"             -> the player-facing defaults (each variable's own
//                            compiled default: retail Options rows, no debug
//                            server, quiet console)
//   anything else         -> dev, with a warning
//
// A profile is nothing but a set of default VALUES for ordinary variables:
// Dc3ProfileApply() runs first thing in main() (native and web) and sets each
// row below that is unset or empty, so every read site -- dc3's, the
// engine's, presence tests and value parses alike -- sees the profile's value,
// and a variable already in the environment always wins (DC3_HTTP=0 under dev
// turns the server off; DC3_DEV_OPTIONS=1 under release shows the rows).  The
// table is the only place a dev default lives; `release` sets nothing, so it
// is exactly the build's behaviour without a profile.
//
// What a row may NOT be: anything that moves the game away from the original
// in a way that invalidates testing it -- scoring, timing, song content,
// skipped screens, faked players (DC3_FAST_TIME, DC3_FAST_BOOT,
// DC3_POSE_SELFTEST, DC3_POSE_PERFORM, MILO_FATAL_FAILS ...), or a diagnostic
// with a real cost (DC3_REFRING_AUDIT, DC3_POISON_FREED_OBJECTS,
// DC3_STUB_TRACE).  Those stay opt-in under every profile.  The full decision
// table, with the reasons, is in docs/debugging/native.md "Profiles".
//
// milo-tests does not call Dc3ProfileApply: an in-process test sees each
// variable's compiled default (= release) unless it sets one.  dc3-native
// subprocesses the tests launch DO apply it, so a test that asserts a
// player-facing default pins DC3_PROFILE=release.
// ---------------------------------------------------------------------------
// A label must never contain "DC3_HTTP_PORT=": harnesses find the port by
// searching the output for that text (NativeExitTest caught a label that did).
static const Dc3ProfileDefault kDc3DevDefaults[] = {
    {"DC3_DEV_OPTIONS", "1", false,
     "Options > Gameplay shows the developer rows (Camera Blend, Debug Overlay)",
     "Options > Gameplay developer rows (Camera Blend, Debug Overlay)"},
    {"DC3_DEV_MENU", "1", false,
     "the original debug build's development menu ({ui show_dev_menu}): its "
     "extra modes in Choose Mode, its extra campaign difficulty rows",
     "the original's dev menu: extra Choose Mode modes, difficulty rows"},
    // `log`, not `1` (np-notifyband): the debug build's notices (cheat_display,
    // every MILO_NOTIFY -- "creating target cache ..." across gameplay) are a
    // developer's, and the console game never drew them over the picture.
    // They still reach the log as `DC3 OVERLAY [<name>]` lines, and the dev tap
    // (DC3_TAG_MODALS) still prints each as MILO_NOTIFY:. DC3_RND_OVERLAYS=1
    // draws them in the corner again. FAILs never went through an overlay.
    {"DC3_RND_OVERLAYS", "log", false,
     "the original's RndOverlays (cheat_display notices, the framerate/stats/"
     "timer quick cheats) logged as DC3 OVERLAY lines, not drawn (1 draws them)",
     "debug notices + quick-cheat stats to the log only (=1 draws them)"},
    {"DC3_HTTP", "1", true,
     "HTTP debug server on 127.0.0.1 (DC3_HTTP_PORT unset: 9090 when free in a "
     "window, else a kernel-assigned port; printed as DC3_HTTP_PORT=<n>)",
     "HTTP debug server, 127.0.0.1 only (its port is printed below)"},
    {"DC3_TEL", "api", true,
     "gameplay telemetry sampled for GET /api/telemetry, no DC3_TEL: lines",
     "gameplay telemetry at /api/telemetry (quiet)"},
    {"DC3_TAG_MODALS", "dev", true,
     "every MILO_WARN/NOTIFY/FAIL printed as MILO_<KIND>: (rate-limited)",
     "game warnings and notices as MILO_<KIND>: lines"},
    {"MILO_INPUT_LOG", "1", false, "one Joypad: down/up line per button message",
     "a Joypad: line per button press"},
    {"DC3_MOVE_RATING_LOG", "1", false, "one DC3 MOVE_RATING line per passed move",
     "a DC3 MOVE_RATING line per scored move"},
    {"MILO_PERF", "1", false, "a [PERF] fps/frame-time line every 5 s",
     "a [PERF] frame-time line every 5 s", true},
    // Installed content, as on a 360 with packages on its drive: the original
    // lists every installed package's songs, so this is the original's
    // behaviour for the content present, not a change to it.  A box without
    // <data>/dlc is unaffected; ctest pins DC3_DLC=0 (MILO_TEST_ENV).  Web too
    // (PLAN W-23): the page fetches the packages' song lists at boot only when
    // this is on, so a server with no packages, or ?profile=release, costs
    // nothing.
    {"DC3_DLC", "1", false,
     "every DLC package under <data>/dlc mounted (orig-assets/dlc, made by "
     "scripts/dlc/import.sh; DC3_DLC_DIR names other roots)",
     "downloadable songs from orig-assets/dlc, when it exists"},
};

const Dc3ProfileDefault *Dc3ProfileDevDefaults(int *count) {
    if (count) *count = (int)(sizeof(kDc3DevDefaults) / sizeof(kDc3DevDefaults[0]));
    return kDc3DevDefaults;
}

static int sDc3Profile = -1; // kDc3ProfileDev / kDc3ProfileRelease once resolved

int Dc3ProfileResolve() {
    const char *v = getenv("DC3_PROFILE");
    if (!v || !*v || strcasecmp(v, "dev") == 0) return kDc3ProfileDev;
    if (strcasecmp(v, "release") == 0) return kDc3ProfileRelease;
    fprintf(stderr, "DC3 Native: DC3_PROFILE='%s' is not 'dev' or 'release'; using dev\n", v);
    return kDc3ProfileDev;
}

bool Dc3ProfileIsDev() {
    if (sDc3Profile < 0) sDc3Profile = Dc3ProfileResolve();
    return sDc3Profile == kDc3ProfileDev;
}

// The banner a person reads at boot: what the dev profile turned on, and the
// switch for each.  The two "profile" lines above it are for scripts.
static void Dc3ProfileBanner(const Dc3ProfileDefault *rows, int n) {
    fprintf(stderr, "DC3 Native: +-- developer profile (DC3_PROFILE=dev) --------------------\n");
    for (int i = 0; i < n; i++) {
#ifdef __EMSCRIPTEN__
        if (rows[i].desktopOnly) continue;
#endif
        const char *v = getenv(rows[i].name);
        const bool on = v && *v && Dc3EnvFlag(rows[i].name, false);
        if (on && strcmp(v, rows[i].value) == 0)
            fprintf(stderr, "DC3 Native: | on   %-66s off: %s=0\n", rows[i].label,
                    rows[i].name);
        else if (on) // the environment's own value, not the profile's
            fprintf(stderr, "DC3 Native: | on   %-66s (you set %s=%s)\n", rows[i].label,
                    rows[i].name, v);
        else if (v && *v)
            fprintf(stderr, "DC3 Native: | off  %-66s (you set %s=%s)\n", rows[i].label,
                    rows[i].name, v);
        else // a presence row the environment switched off (unset above)
            fprintf(stderr, "DC3 Native: | off  %-66s (you set %s off)\n", rows[i].label,
                    rows[i].name);
    }
    fprintf(stderr, "DC3 Native: | still opt-in: DC3_FAST_BOOT, DC3_FAST_TIME, DC3_SCORING_DEBUG"
                    " (docs/debugging/native.md \"Profiles\")\n");
    fprintf(stderr, "DC3 Native: +-- DC3_PROFILE=release turns all of it off ---------------\n");
}

void Dc3ProfileApply() {
    sDc3Profile = Dc3ProfileResolve();
    const bool explicitProfile = getenv("DC3_PROFILE") && *getenv("DC3_PROFILE");
    if (sDc3Profile == kDc3ProfileRelease) {
        fprintf(stderr, "DC3 Native: profile=release (player defaults; no developer "
                        "defaults applied)\n");
        return;
    }
    // Later readers (and anything this process starts) see the resolved name.
    setenv("DC3_PROFILE", "dev", 1);
    std::string applied, kept;
    int n = 0;
    const Dc3ProfileDefault *rows = Dc3ProfileDevDefaults(&n);
    for (int i = 0; i < n; i++) {
#ifdef __EMSCRIPTEN__
        if (rows[i].desktopOnly) continue;
#endif
        // Set and non-empty = the caller's choice; empty counts as unset (as
        // in Dc3EnvFlag), so it is overwritten.
        const char *have = getenv(rows[i].name);
        if (have && *have) {
            kept += std::string(" ") + rows[i].name + "=" + have;
            // A presence-tested reader would take "0" as on: an explicit off
            // spelling is removed, so it reads as unset = off.
            if (rows[i].presence && !Dc3EnvFlag(rows[i].name, true)) {
                unsetenv(rows[i].name);
                kept += " (off: unset for its presence test)";
            }
            continue;
        }
        setenv(rows[i].name, rows[i].value, 1);
        applied += std::string(" ") + rows[i].name + "=" + rows[i].value;
    }
    fprintf(stderr, "DC3 Native: profile=dev (%s; DC3_PROFILE=release for the player "
                    "defaults)\n", explicitProfile ? "DC3_PROFILE=dev" : "the default");
    fprintf(stderr, "DC3 Native: profile dev defaults:%s\n",
            applied.empty() ? " (none)" : applied.c_str());
    if (!kept.empty())
        fprintf(stderr, "DC3 Native: profile dev, kept from the environment:%s\n",
                kept.c_str());
    Dc3ProfileBanner(rows, n);
}

// ---------------------------------------------------------------------------
// The boot sequence (PLAN H-123).
//
// The original runs the same sequence on every launch: the ESRB and Harmonix
// splash screens, the attract movie (83 s unless skipped), a 4 s autosave
// warning with no skip, then the title.  That is right once.  A console whose
// global options are on disk has shown all of it, so its next launches pass
// the splash screens, attract and the warning and wait on the title screen.
//
// What decides it is the DISK, read once before the game can write the file
// (App::App), plus three variables that each win over it when set:
//
//   DC3_BOOT_INTRO=1 / 0   the original sequence always / never (the one
//                          switch back; web: ?env=DC3_BOOT_INTRO=1)
//   DC3_SHOW_SPLASH=1 / 0  the splash screens only
//   DC3_FAST_BOOT=1        the test suites' boot: attract, the warning, the
//                          title confirm and the save wait all pass by
//                          themselves, to main_screen
//   DC3_FAST_BOOT=0        what it always gave: the original sequence, and no
//                          screen ever passed for the player
//
// This is NOT a profile row and must not become one: it depends on the
// console's state, not on who is playing, and it applies under dev and release
// alike.  It also stays inside what the profile rules allow by default: it
// stops at the title screen, so the confirm that sets the initial profiles and
// $post_load_dest_screen is still the player's (DC3_FAST_BOOT, which makes
// that confirm itself, stays opt-in).  A first launch -- and every test, since
// each runs on its own empty DC3_CACHE_DIR -- is the original sequence.
//
// The warning screen is passed, not removed from the flow: its enter handler
// preloads panels the shell needs, so the screen is still entered and left by
// the same transition the original uses (UIManager::Poll, src/system/ui/UI.cpp).
// ---------------------------------------------------------------------------
static bool EnvSet(const char *v) { return v && *v; }

static bool EnvOff(const char *v) {
    return strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0 || strcasecmp(v, "off") == 0 ||
           strcasecmp(v, "no") == 0;
}

Dc3BootPlan Dc3BootPlanResolve(bool globalOptionsOnDisk, const char *showSplashEnv,
                               const char *fastBootEnv, const char *bootIntroEnv,
                               bool webFastBootParam) {
    Dc3BootPlan plan;
    plan.returning = globalOptionsOnDisk;
    // DC3_FAST_BOOT keeps the parse UIManager::Poll always had: atoi != 0.
    const bool fastOn = EnvSet(fastBootEnv) && atoi(fastBootEnv) != 0;
    const bool fastOff = EnvSet(fastBootEnv) && !fastOn;
    // An explicit DC3_FAST_BOOT=0 asks for what it always gave: the original.
    const bool intro = EnvSet(bootIntroEnv) ? !EnvOff(bootIntroEnv)
                                            : (fastOff || !globalOptionsOnDisk);
    plan.showSplash = EnvSet(showSplashEnv) ? !EnvOff(showSplashEnv) : intro;
    plan.fastBoot = fastOn || webFastBootParam;
    plan.skipIntro = plan.fastBoot || (!intro && !fastOff);
    if (plan.fastBoot)
        plan.why = fastOn ? "DC3_FAST_BOOT" : "?fast_boot=1";
    else if (EnvSet(bootIntroEnv))
        plan.why = intro ? "DC3_BOOT_INTRO=1" : "DC3_BOOT_INTRO=0";
    else if (fastOff)
        plan.why = "DC3_FAST_BOOT=0";
    else
        plan.why = globalOptionsOnDisk ? "this console has been played before"
                                       : "first launch on this console";
    return plan;
}

// A regular, non-empty file: a directory of that name, or a file a failed
// write left empty, is not a console that has been played.
bool Dc3GlobalOptionsOnDisk() {
    struct stat st;
    return stat(Dc3GlobalOptionsPath().c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

const Dc3BootPlan &Dc3BootPlanGet() {
    static bool sDecided = false;
    static Dc3BootPlan sPlan;
    if (!sDecided) {
        sDecided = true;
        const std::string path = Dc3GlobalOptionsPath();
        const bool onDisk = Dc3GlobalOptionsOnDisk();
        bool webFastBoot = false;
#ifdef __EMSCRIPTEN__
        webFastBoot = EM_ASM_INT({
            return ((location.search || "").indexOf("fast_boot=1") >= 0) ? 1 : 0;
        }) != 0;
#endif
        sPlan = Dc3BootPlanResolve(onDisk, getenv("DC3_SHOW_SPLASH"), getenv("DC3_FAST_BOOT"),
                                   getenv("DC3_BOOT_INTRO"), webFastBoot);
        const char *seq = sPlan.fastBoot ? "fast boot to the main menu"
            : sPlan.skipIntro ? "straight to the title screen"
                              : "the original sequence";
        fprintf(stderr,
                "DC3 Boot: %s (%s); splash screens %s; global options %s at %s%s\n", seq,
                sPlan.why, sPlan.showSplash ? "on" : "off", onDisk ? "found" : "not found",
                path.c_str(),
                sPlan.skipIntro && !sPlan.fastBoot ? "; DC3_BOOT_INTRO=1 for the original sequence"
                                                  : "");
    }
    return sPlan;
}

// For PPC units, which declare these with a local `extern` (no struct).
bool Dc3BootShowSplash() { return Dc3BootPlanGet().showSplash; }
bool Dc3BootFast() { return Dc3BootPlanGet().fastBoot; }
bool Dc3BootSkipIntro() { return Dc3BootPlanGet().skipIntro; }

// The native controller-mode policy, DC3_CONTROLLER_MODE.
//
//   unset / empty / "auto"  -> auto (the default): faithful while a real pose
//                              provider runs (a camera feeds the sensor),
//                              forced while only the dummy skeleton does
//   "forced"                -> native stays in controller mode permanently
//   "faithful"              -> the image's Enter/ExitControllerMode bodies and
//                              the helpbar's controller_mode_timeout idle exit
//   anything else           -> auto, with a warning
//
// Why auto, from the image: on the 360 a Kinect is always attached, so the
// shell boots OUT of controller mode (GestureMgr::GestureMgr mInControllerMode
// = 0) and HamNavList::UpdateGestures runs the hand cursor whenever
// !InControllerMode().  A pad press outside controller mode ENTERS controller
// mode and is swallowed, B excepted (ShellInput::OnMsg(ButtonDownMsg)); every
// press restarts the idle timer (HamUI::OnMsg(ButtonDownMsg) exports
// reset_controller_mode_timeout); ShellInput::Poll hands the UI back to the
// hand cursor after controller_mode_timeout (5000 ms) without one.  That is
// the right behaviour exactly when something can move the hand cursor -- a
// running provider.  With only the dummy skeleton nothing can, the 5 s
// handover would strand a pad player in a UI no one can drive, so the policy
// pins controller mode (`forced`).
//
// Under auto the decision is LATCHED, not read live: GestureMgr_NativeInit
// latches it after the provider is chosen, and NativeControllerModePolicyTick
// (HelpBarPanel.cpp, called from ShellInput::Poll) re-latches it once per
// frame from NativePoseProviderRunning() and performs the transition.  Every
// consumer below reads the latched value through Dc3ControllerModeForced(), so
// one frame always sees one policy.
//
// PPC units declare these with a local `extern` inside their #ifdef HX_NATIVE
// blocks, like Dc3EnvFlag.  Every site that consults the policy names it; see
// docs/debugging/native.md "Controller mode policy".
enum Dc3ControllerModeSetting { kCMAuto = 0, kCMForced = 1, kCMFaithful = 2 };

static int ControllerModeSetting() {
    static int sSetting = -1;
    if (sSetting < 0) {
        const char *v = getenv("DC3_CONTROLLER_MODE");
        if (!v || !*v || strcasecmp(v, "auto") == 0) {
            sSetting = kCMAuto;
        } else if (strcasecmp(v, "forced") == 0) {
            sSetting = kCMForced;
        } else if (strcasecmp(v, "faithful") == 0) {
            sSetting = kCMFaithful;
        } else {
            fprintf(stderr,
                    "DC3 Native: DC3_CONTROLLER_MODE='%s' is not 'auto', 'forced' "
                    "or 'faithful'; using auto\n", v);
            sSetting = kCMAuto;
        }
        fprintf(stderr, "DC3 Native: controller mode policy = %s\n",
                sSetting == kCMAuto ? "auto (forced until a pose provider runs)"
                : sSetting == kCMForced ? "forced" : "faithful");
    }
    return sSetting;
}

// auto's latched decision: true = faithful (a provider runs).
static bool sAutoFaithful = false;

bool Dc3ControllerModeIsAuto() { return ControllerModeSetting() == kCMAuto; }

bool Dc3ControllerModeForced() {
    switch (ControllerModeSetting()) {
    case kCMForced:
        return true;
    case kCMFaithful:
        return false;
    default:
        return !sAutoFaithful;
    }
}

bool Dc3ControllerModeLatchAuto(bool providerRunning) {
    if (ControllerModeSetting() != kCMAuto || sAutoFaithful == providerRunning)
        return false;
    sAutoFaithful = providerRunning;
    fprintf(stderr, "DC3 Native: controller mode policy auto -> %s (%s)\n",
            providerRunning ? "faithful" : "forced",
            providerRunning ? "a pose provider is running"
                            : "no pose provider is running");
    return true;
}

// Dc3ControllerModeTimeoutMs — DC3_CONTROLLER_MODE_TIMEOUT_MS, a positive
// override of the helpbar's controller_mode_timeout (ShellInput::unk_0x98,
// default 5000) whenever the policy is faithful (explicit, or auto with a
// provider). Returns -1 when unset or not
// a positive integer (the helpbar value stands). Read once.
int Dc3ControllerModeTimeoutMs() {
    static int sTimeoutMs = -2;
    if (sTimeoutMs == -2) {
        sTimeoutMs = -1;
        const char *v = getenv("DC3_CONTROLLER_MODE_TIMEOUT_MS");
        if (v && *v) {
            char *end = nullptr;
            long ms = strtol(v, &end, 10);
            if (end && *end == '\0' && ms > 0 && ms < 0x7fffffff) {
                sTimeoutMs = (int)ms;
                fprintf(stderr, "DC3 Native: controller mode timeout override = %d ms\n",
                        sTimeoutMs);
            } else {
                fprintf(stderr,
                        "DC3 Native: DC3_CONTROLLER_MODE_TIMEOUT_MS='%s' is not a "
                        "positive integer; ignored\n", v);
            }
        }
    }
    return sTimeoutMs;
}

// The press hold (PLAN H-157): under the faithful policy, how long a keyboard
// or pad press keeps the menus before the hand cursor takes them back.
//
// The image gives them back after controller_mode_timeout, 5 s without a
// press.  That suits a Kinect, which sees an arm at the side as an arm at the
// side.  A webcam at a desk sees the arm of the person at the keyboard cut by
// the edge of the picture, in and out of it from frame to frame, and five
// seconds after the last key that arm was moving the highlight and selecting
// rows (playtest 2026-11-21: "super unusable").  So a press now keeps the
// menus for Dc3ControllerHoldMs() without another one, and raising both hands
// -- the image's own HandsUpGestureFilter, 1.2 s -- gives them to the hands at
// once.  Start still leaves controller mode, a song still takes the menus out
// of it, and the scripts' own exits (loading, the results) are untouched.
//
// Only the idle exit that follows a PRESS is held: the auto policy's handover
// when a camera comes up with no key touched is still the helpbar's 5 s.
//
//   DC3_CONTROLLER_HOLD_MS   the hold in ms; 0 = off (the image's idle exit
//                            alone, and hands up does what it does in the
//                            image).  Unset: DC3_CONTROLLER_MODE_TIMEOUT_MS
//                            when that is set (a run that sets the idle
//                            timeout keeps its timing), else 300000.
int Dc3ControllerHoldMs() {
    static int sHoldMs = -1;
    if (sHoldMs < 0) {
        sHoldMs = Dc3ControllerModeTimeoutMs() > 0 ? Dc3ControllerModeTimeoutMs() : 300000;
        const char *v = getenv("DC3_CONTROLLER_HOLD_MS");
        if (v && *v) {
            char *end = nullptr;
            long ms = strtol(v, &end, 10);
            if (end && *end == '\0' && ms >= 0 && ms < 0x7fffffff)
                sHoldMs = (int)ms;
            else
                fprintf(stderr,
                        "DC3 Native: DC3_CONTROLLER_HOLD_MS='%s' is not a whole number "
                        "of ms (0 = off); ignored\n", v);
        }
        fprintf(stderr, "DC3 Native: controller press hold = %d ms%s\n", sHoldMs,
                sHoldMs == 0 ? " (off)" : "");
    }
    return sHoldMs;
}

namespace {
// A press restarted ShellInput's idle timer since the hands last had the menus.
bool sHoldPressed = false;
// This raise of the hands already gave them the menus (until the hands drop).
bool sHoldHandsUpSpent = false;
// A raise takes the menus only this long after the last press: the person at
// the keyboard is still on it.
const float kHoldHandsUpAfterPressMs = 1500.0f;
const char *kHoldNoticeKey = "controller-hold";
const float kHoldNoticeSeconds = 5.0f;
bool sHoldNoticeUp = false;
std::chrono::steady_clock::time_point sHoldNoticeDown;
// The takeover notice is shown this many times a run; the way back is the
// same every time.
int sHoldTakenNoticesLeft = 3;

void HoldPostNotice(const char *text) {
    NativeNotice::Post(kHoldNoticeKey, text, "", kHoldNoticeSeconds);
    // Taken down again by Dc3ControllerHoldPoll: a notice left posted hides
    // older ones (NativeNotice::Current shows the newest only).
    sHoldNoticeUp = true;
    sHoldNoticeDown = std::chrono::steady_clock::now()
        + std::chrono::milliseconds((long long)(kHoldNoticeSeconds * 1000.0f) + 500);
}

bool HoldOn() { return !Dc3ControllerModeForced() && Dc3ControllerHoldMs() > 0; }
} // namespace

// Every pad/keyboard press of the run (HamUI::OnMsg(ButtonDownMsg), which also
// restarts ShellInput's idle timer).
void Dc3ControllerHoldNotePress() { sHoldPressed = true; }

// ShellInput's idle exit: the timeout to use now, given the helpbar's.
int Dc3ControllerIdleTimeoutMs(int imageMs) {
    if (!HoldOn() || !sHoldPressed)
        return imageMs;
    return Dc3ControllerHoldMs() > imageMs ? Dc3ControllerHoldMs() : imageMs;
}

// A press outside controller mode just took the menus
// (ShellInput::OnMsg(ButtonDownMsg)): say how the hands get them back.
void Dc3ControllerHoldNoteTaken() {
    sHoldPressed = true;
    if (!HoldOn() || sHoldTakenNoticesLeft <= 0)
        return;
    sHoldTakenNoticesLeft--;
    HoldPostNotice("Keyboard or controller has the menus. Raise both hands to go back to gestures.");
}

// Once per ShellInput::Poll.  `handsUp` is the image's HandsUpGestureFilter
// (both hands above the shoulders for 1.2 s), `idleMs` ShellInput's idle
// timer.  True: the hands take the menus this frame -- the caller leaves
// controller mode.
bool Dc3ControllerHoldPoll(bool inControllerMode, bool handsUp, float idleMs) {
    if (sHoldNoticeUp && std::chrono::steady_clock::now() >= sHoldNoticeDown) {
        NativeNotice::Clear(kHoldNoticeKey);
        sHoldNoticeUp = false;
    }
    if (!handsUp)
        sHoldHandsUpSpent = false;
    if (!inControllerMode)
        sHoldPressed = false;
    if (!HoldOn() || !inControllerMode || !handsUp || sHoldHandsUpSpent
        || idleMs < kHoldHandsUpAfterPressMs)
        return false;
    sHoldHandsUpSpent = true;
    sHoldPressed = false;
    fprintf(stderr, "DC3 ControllerMode: both hands raised, the hands take the menus "
                    "(idle_ms=%d)\n", (int)idleMs);
    HoldPostNotice("Gestures have the menus again.");
    return true;
}

// The raise that just took the menus back does not also open the image's
// correct-identity overlay (ShellInput::Poll's hands-up arm); the next one does.
bool Dc3ControllerHoldHandsUpSpent() { return sHoldHandsUpSpent; }

namespace {
    DiscErrorCallbackFunc *gCallback;
}

unsigned long ULSystemLocale() { return 0x0409; } // en-US
unsigned long ULSystemLanguage() { return 1; }    // English

DiscErrorCallbackFunc *SetDiskErrorCallback(DiscErrorCallbackFunc *func) {
    DiscErrorCallbackFunc *old = gCallback;
    gCallback = func;
    return old;
}

DiscErrorCallbackFunc *GetDiskErrorCallback() { return gCallback; }

Symbol GetSystemLanguage(Symbol s) {
    static Symbol eng("eng");
    return eng;
}

Symbol GetSystemLocale(Symbol s) {
    static Symbol locale("en_US");
    return locale;
}

void ShowDirtyDiscError() {
    MILO_LOG("Dirty disc error - ignoring on native\n");
}

bool PlatformDebugBreak() {
    return false;
}

void GetMapFileName(String &s) {
    s = "";
}

void CaptureStackTrace(int, struct StackData *, void *) {}
void AppendStackTrace(FixedString &, void *) {}
void AppendThreadStackTrace(FixedString &, struct StackData *) {}

// Defined in File_Native.cpp
void NativeSetDataDir(const char *dir);
const char *NativeGetDataDir();
void NativeSetOverlayDir(const char *dir);
const char *NativeGetOverlayDir();

static bool FileExistsRaw(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Where the game data is: the directory containing gen/main_xbox.hdr, probed
// as $DC3_DATA first, then orig-assets / ../orig-assets / ../../orig-assets / .
// relative to the CWD (../../orig-assets is the native/build launch). There is
// deliberately no executable-relative fallback: HeadlessBootNoGameData runs
// from an empty cwd with an empty DC3_DATA and requires the "no game data"
// error, which a fallback to the tree's orig-assets would bypass.
// nullptr = no game data. Pure query (no side effects), so
// the test harness asks exactly the question the boot path asks: see
// EnsureEngineInit() in native/tests/test_helpers.cpp and HeadlessBootTest.
const char *NativeFindDataDir() {
    const char *env = getenv("DC3_DATA");
    if (env && *env && FileExistsRaw(MakeString("%s/gen/main_xbox.hdr", env)))
        return env;
    static const char *searchPaths[] = {
        "orig-assets",
        "../orig-assets",
        "../../orig-assets",
        ".",
        nullptr
    };
    for (const char **p = searchPaths; *p; p++) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s/gen/main_xbox.hdr", *p);
        if (FileExistsRaw(buf))
            return *p;
    }
    return nullptr;
}

// Find the game data directory containing gen/main_xbox.hdr
void NativeDetectDataDir() {
    const char *env = getenv("DC3_DATA");
    const char *dir = NativeFindDataDir();
    if (env && *env && dir != env)
        printf("DC3 Native: WARNING - DC3_DATA=%s has no gen/main_xbox.hdr; ignoring it\n", env);
    if (dir) {
        NativeSetDataDir(dir);
        if (dir == env)
            printf("DC3 Native: data dir from DC3_DATA=%s\n", env);
        else
            printf("DC3 Native: data dir=%s\n", dir);
        return;
    }
    printf("DC3 Native: WARNING - could not find game data (gen/main_xbox.hdr)\n");
    printf("  Set DC3_DATA env var or run from the repo root.\n");
}

// Find the DTA overlay directory (native/dta/ relative to repo root).
// Overlay files shadow the archive — used for native-only DTA patches.
//
// The executable's own tree comes first: the overlay is SOURCE, and belongs to
// the checkout the binary was built from.  "<dataDir>/../native/dta" is not
// that in a worktree -- orig-assets there is a symlink to the main checkout's,
// so the kernel resolves "orig-assets/.." to the MAIN checkout and every
// worktree boot (and every ctest run, cwd orig-assets) loaded main's overlay
// DTAs instead of its own.
void NativeDetectOverlayDir() {
    std::string treeDir = NativeTreeDir();
    if (!treeDir.empty()) {
        std::string treeOverlay = treeDir + "/dta";
        if (FileExistsRaw(treeOverlay.c_str())) {
            NativeSetOverlayDir(treeOverlay.c_str());
            printf("DC3 Native: overlay dir=%s\n", treeOverlay.c_str());
            return;
        }
    }
    const char *dataDir = NativeGetDataDir();
    // dataDir is typically "orig-assets" relative to repo root.
    // Overlay dir is "native/dta/" relative to repo root = "<dataDir>/../native/dta/"
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/../native/dta", dataDir);
    if (FileExistsRaw(buf)) {
        NativeSetOverlayDir(buf);
        printf("DC3 Native: overlay dir=%s\n", buf);
        return;
    }
    // Also check "native/dta" directly (running from repo root with dataDir=".")
    if (FileExistsRaw("native/dta")) {
        NativeSetOverlayDir("native/dta");
        printf("DC3 Native: overlay dir=native/dta\n");
        return;
    }
    printf("DC3 Native: no overlay dir found (optional)\n");
}

#ifndef __EMSCRIPTEN__
// Exit (status 1) with a message a player can act on. _exit, not exit: this
// runs inside SystemPreInit, before anything worth flushing exists, and static
// destructors (Dawn's among them) are not safe to run from here.
[[noreturn]] static void NativeFatalNoGameData(const char *hdrPath, const char *why) {
    const char *env = getenv("DC3_DATA");
    fflush(stdout);
    fprintf(stderr,
            "DC3 Native: FATAL - no game data: %s %s.\n"
            "  DC3_DATA is %s%s%s.\n"
            "  Point DC3_DATA at the extracted game data (the directory that contains\n"
            "  gen/main_xbox.hdr), or run from a directory with orig-assets/ in it.\n",
            hdrPath, why, env ? "'" : "", env ? env : "unset", env ? "'" : "");
    fflush(stderr);
    _exit(1);
}
#endif

// Native archive initialization - simplified from ArchiveInit()
// Skips TheContentMgr dependency, directly creates archive from known path
void NativeArchiveInit() {
#ifdef __EMSCRIPTEN__
    // Web port: files are pre-extracted and served via HTTP into MEMFS.
    // No .ark archive needed — disable the CD/archive system so files
    // are opened directly via POSIX (backed by Emscripten MEMFS).
    SetUsingCD(false);
    printf("DC3 Web: archive system bypassed (MEMFS mode)\n");
#else
    Symbol plat = PlatformSymbol(TheLoadMgr.GetPlatform());
    const char *mainArk = MakeString("gen/main_%s", plat);
    printf("DC3 Native: Loading archive %s.hdr\n", mainArk);
    // No archive is a fatal configuration error, reported as one. Archive::Read
    // (decomp code, kept as the image has it) only MILO_ASSERTs on a missing or
    // unreadable header, and asserts are non-fatal natively, so it used to hand
    // back an Archive with an EMPTY hash table; the first file lookup then did
    // HashString(..., 0) -- `% 0` -- and the process died of SIGFPE with no
    // explanation (every engine test on the asset-less CI runner, and any
    // player with a bad DC3_DATA). Check before reading, and check the result.
    // A local copy, not MakeString's rotating buffer: Archive's constructor
    // below makes many MakeString calls of its own.
    char hdrPath[1024];
    snprintf(hdrPath, sizeof(hdrPath), "%s/%s.hdr", NativeGetDataDir(), mainArk);
    if (!FileExistsRaw(hdrPath))
        NativeFatalNoGameData(hdrPath, "does not exist");
    TheArchive = new Archive(mainArk, 0);
    if (TheArchive->NumArkFiles() <= 0)
        NativeFatalNoGameData(hdrPath, "could not be read as a version-6 archive header");
    static int preinitArk = 1;
    TheArchive->SetArchivePermission(1, &preinitArk);
    printf("DC3 Native: Archive loaded, %d ark files\n", TheArchive->NumArkFiles());
    TheBlockMgr.Init();
    printf("DC3 Native: BlockMgr initialized\n");
#endif
}

// Native version of SystemPreInit(argc, argv, config)
// On Xbox this is in System_Xbox.cpp - converts argc/argv to command line string
void SystemPreInit(const char *cmdLine, const char *cfg);

void SystemPreInit(int argc, char **argv, const char *config) {
    // Build command line string from argc/argv
    String cmdLine;
    for (int i = 0; i < argc; i++) {
        if (i > 0) cmdLine += ' ';
        cmdLine += argv[i];
    }
    // InitMakeString before NativeDetectDataDir (which uses MakeString)
    InitMakeString();
    // Detect game data directory before engine init
#ifndef __EMSCRIPTEN__
    NativeDetectDataDir();
    NativeDetectOverlayDir();
#endif
    SystemPreInit(cmdLine.c_str(), config);
}
