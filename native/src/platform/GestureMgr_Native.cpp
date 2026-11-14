#ifdef HX_NATIVE

#include "Skeleton_Native.h"
#include "CameraStream_Native.h"
#include "PoseLevel_Native.h" // NativeSensorLevel: the camera-tilt step
#include "PoseEstimate_Native.h" // DC3_POSE_ESTIMATE: joints the camera cannot see
#ifndef __EMSCRIPTEN__
#include "PosePerformer_Native.h" // Dc3PosePerformerEnabled
#include "PoseSession_Native.h"   // DC3_POSE_RECORD / session replay (PLAN L-1)
#endif
#include "gesture/LiveCameraInput.h"
#include "NativeSettings.h" // Dc3EnvFlag, Dc3ControllerModeForced
#include "platform/JoypadScriptHook.h"
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h" // SkeletonCallback
#include "gesture/SkeletonHistory.h"
#include "gesture/SkeletonUpdate.h"
#include "hamobj/HamGameData.h"   // TheGameData (player->skeleton binding)
#include "hamobj/HamPlayerData.h" // GetSkeletonTrackingID
#include "hamobj/MoveDir.h"       // MoveDir::SongSeconds (DC3_POSE_FILL)
#include "PoseFill_Native.h"      // DC3_POSE_FILL
#include "NativeNotice.h"         // DC3_POSE_STALL_MS
#include "meta_ham/HamUI.h"          // TheHamUI -> ShellInput -> SkeletonChooser
#include "meta_ham/ShellInput.h"
#include "meta_ham/SkeletonChooser.h"
#include "flow/PropertyEventProvider.h" // TheHamProvider (NativeDummyPlayerCount)
#include "obj/Task.h"
#include "os/Debug.h" // TheDebug.AddExitCallback
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <unistd.h> // getpid (default pose socket)

// The native sensor.  On the 360, SkeletonUpdate (the image's skeleton hub:
// history archive, Skeleton::Poll of six slots, the per-player binding, the
// callback fan-out) is fed by the Kinect NUI device.  Natively that device is
// replaced -- PLATFORM -- by this CameraInput, an override camera
// (IsOverride() true, as CameraInput's own default) whose PollNewFrame()
// builds a SkeletonFrame from the pose provider (the pose_server.py socket on
// desktop, dc3_pose_push on web) or, with no provider, the static tracked dummy skeleton.  Everything
// downstream of that frame is the image's own code: SkeletonUpdate::PostUpdate
// copies it into mSkeletonFrame, runs Update() itself (the update thread is
// inactive natively) -> UpdateCallbacks (archive-then-Poll, mSkeletonsLeft by
// player tracking id, every callback's Update) -> every callback's PostUpdate
// in registration order -> Skeleton::PostUpdate.  App's "skeleton_post_update"
// step drives it once per frame, as in the image's main loop.
class NativeSensorCameraInput : public CameraInput {
public:
    const SkeletonFrame *PollNewFrame() override;
};

static NativeSensorCameraInput *sSensor = nullptr;
// App.cpp's move-scoring gate (DC3_NATIVE_SCORING default on, DC3_POSE_SELFTEST
// forces it): with it off the sensor delivers no frames at all.
static bool sSensorFeedEnabled = true;

// The NUI frame clock.  SkeletonUpdate::Update asks NuiSkeletonGetNextFrame
// whether the sensor produced a new skeleton frame; when it did not and the
// camera is connected, Update returns without UpdateCallbacks, and the
// callbacks' PostUpdate get null.  native/src/xbox_link_stubs.cpp's stub calls
// this hook (it used to report a frame on every call): natively the stream
// ticks exactly when the active camera input delivered a new frame this
// PostUpdate, so one provider frame is integrated once however fast the game
// loop runs.  Defined here, not in the stub file, because dc3-web compiles this
// file but not xbox_link_stubs.cpp.
extern "C" {
HRESULT (*gNativeNuiSkeletonFrameSource)(NUI_SKELETON_FRAME *) = nullptr;
}

static HRESULT NativeNuiSkeletonFrameSource(NUI_SKELETON_FRAME *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    CameraInput *cam = handle.GetCameraInput();
    // 0x83010001: any failure HRESULT; the image only tests for 0.
    return (cam && cam->NewFrame()) ? 0 : (HRESULT)0x83010001;
}

// Persistent trackId -> skeleton-slot assignment (index = slot, value = owning
// trackId, -1 = free). A new trackId claims the lowest free slot and keeps it
// while it persists; when a trackId stops appearing the slot is released so a
// later person cannot inherit the departed person's archive history. Reset when
// no provider runs. Shared by both live branches (the dummies use slots 0/1).
static int sSlotTrackId[NUM_SKELETONS] = {-1, -1, -1, -1, -1, -1};

// Dropout hold (PLAN P-12).  MediaPipe loses a dancer who is fully in view,
// mid-song, for up to ~1.4 s (measured: 50 dropouts over 18 songs of real
// footage, longest 1.2 s, and 1.43 s on the cupidshuffle clip of np-dogfood3)
// -- usually on a turn.  A Kinect keeps such a person TRACKED: its skeleton
// comes from the depth image the body never left, occluded joints flagged
// inferred.  The image gives a lost tracking id only GestureMgr::
// sMaxRecoveryTime (1.0 s, SkeletonRecoverer) before SkeletonChooser unbinds
// the player and Game::CheckForSkeletonLoss pauses -- and on a 360 that grace
// exists for a real loss, not for a detector glitch.  So a slot whose person
// vanishes keeps reporting that person, same tracking id, every joint at its
// last good position and flagged inferred, for up to DC3_POSE_HOLD_MS
// (default kDefaultHoldMs) of sensor time.  Bounded: past it the slot is
// released and the image's own recovery window runs, so a player who walks
// out still pauses, kDefaultHoldMs later than on a 360.  NATIVE POLICY.
//
// Not held: a person who was in a slot for less than kHoldMinPresenceMs (a
// one-frame false detection is not someone a Kinect would have acquired), and
// a slot whose position a NEW tracking id has taken (within the image's own
// GestureMgr::sMaxRecoveryDistance, 0.3 m): the tracker decided that is a new
// person, and holding the old one too would put two skeletons on one body.
static const int kDefaultHoldMs = 1000;
static const float kHoldMinPresenceMs = 500.0f;
static const float kHoldReacquireMetres = 0.3f;

struct SlotHold {
    NativeSkeletonProvider::PersonData last; // the slot's last delivered person
    float presentMs = 0.0f; // sensor time the current owner has been delivered
    float heldMs = 0.0f;    // sensor time the slot has been held (0 = not held)
};
static SlotHold sSlotHold[NUM_SKELETONS];

// DC3_POSE_ESTIMATE's per-slot state (configured by EstimateReadMode below).
static Dc3PoseEstimate::Estimator gEstimate;

static void ResetSlotHold(int s) {
    sSlotHold[s].presentMs = 0.0f;
    sSlotHold[s].heldMs = 0.0f;
}

static void ResetSlotMap() {
    for (int s = 0; s < NUM_SKELETONS; s++) {
        sSlotTrackId[s] = -1;
        ResetSlotHold(s);
    }
}

// DC3_POSE_HOLD_MS: 0 turns the hold off (the pre-P-12 behaviour), unset or
// unparsable = kDefaultHoldMs, capped at 5000.  Read per call (cheap), so a
// test can change it between frames.
static int NativePoseHoldMs() {
    const char *v = getenv("DC3_POSE_HOLD_MS");
    if (!v || !*v)
        return kDefaultHoldMs;
    char *end = nullptr;
    long ms = strtol(v, &end, 10);
    if (end == v || *end != '\0' || ms < 0)
        return kDefaultHoldMs;
    return ms > 5000 ? 5000 : (int)ms;
}

// Is a real pose provider feeding the sensor (ncnn, the external pose server
// or the web page's pushes) rather than the dummy skeleton?  The same test
// BuildSensorFrame makes; read by the `auto` controller-mode policy
// (System_Native.cpp, NativeControllerModePolicyTick in HelpBarPanel.cpp).
// A query only -- provider selection lives in GestureMgr_NativeInit.
bool NativePoseProviderRunning() {
#ifdef ENABLE_NCNN
    if (sInternalPose && sInternalPose->IsRunning())
        return true;
#endif
    return TheSkeletonProvider && TheSkeletonProvider->IsRunning();
}

// The scripted-input `wake` directive's question: would pressing the
// controller-mode wake button change anything? Only outside controller mode
// (never while the policy is forced, where controller mode is pinned on), so
// `wake` is a no-op without a pose provider and wakes the shell with one.
static bool NativeWakeNeeded() {
    return !TheGestureMgr || !TheGestureMgr->InControllerMode();
}

// Exit callback, the stand-in for LiveCameraInput::Terminate (which deletes the
// NUI device and, in its dtor, SkeletonUpdate::Terminate).  Registered before
// GestureMgr::Terminate, and exit callbacks run LIFO, so the GestureMgr dtor
// unregisters from a live SkeletonUpdate first, as in the image.  The sensor
// goes last: SkeletonUpdate's ObjOwnerPtr to it would otherwise re-point the
// camera input at a dying object.
static void GestureMgr_NativeSensorTerminate() {
    gNativeNuiSkeletonFrameSource = nullptr;
    SkeletonUpdate::Terminate();
    SkeletonUpdate::SetNativeDefaultCameraInput(nullptr);
    delete sSensor;
    sSensor = nullptr;
}

// Native stand-in for LiveCameraInput::PreInit (GestureMgr::Init calls it
// before constructing TheGestureMgr): the sensor replaces the NUI device as the
// default CameraInput, then SkeletonUpdate::Init (the image runs it from the
// LiveCameraInput ctor) and SkeletonUpdate::CreateInstance, exactly once.
void GestureMgr_NativePreInit() {
    if (SkeletonUpdate::HasInstance())
        return;
    sSensorFeedEnabled = Dc3EnvFlag("DC3_NATIVE_SCORING", true)
        || Dc3EnvFlag("DC3_POSE_SELFTEST", false);
    sSensor = new NativeSensorCameraInput();
    SkeletonUpdate::SetNativeDefaultCameraInput(sSensor);
    SkeletonUpdate::Init();
    SkeletonUpdate::CreateInstance();
    gNativeNuiSkeletonFrameSource = NativeNuiSkeletonFrameSource;
    TheDebug.AddExitCallback(GestureMgr_NativeSensorTerminate);
}

// The model GestureMgr_NativeInit hands the pose server: $DC3_POSE_MODEL, else
// the model in the executable's own tree -- never a path relative to the
// working directory, which is native/build for a windowed boot (PLAN P-10).
std::string GestureMgr_NativePoseModelPath() {
    const char *envModel = getenv("DC3_POSE_MODEL");
    if (envModel && *envModel)
        return envModel;
    return NativeSkeletonProvider::DefaultModelPath();
}

// Native implementation of GestureMgr::Init's device half: the pose provider
// (webcam + MediaPipe) that the sensor reads.
void GestureMgr_NativeInit() {
    ResetSlotMap();
    JoypadScriptSetWakeNeeded(NativeWakeNeeded);
    extern void JoypadSelect_NativeRegister();
    JoypadSelect_NativeRegister();

    // In headless mode (tests, CLI tools), skip the pose server entirely.
    // The dummy skeleton in GestureMgr_NativePoll provides a neutral standing
    // pose so skeleton-gated paths still work without a real camera.
    // An explicit DC3_POSE overrides the skip so headless CI can exercise the
    // live-provider path against a synthetic pose server, and so does the
    // in-process performer (DC3_POSE_PERFORM, PLAN P-13): it spawns nothing,
    // and before it counted here a headless DC3_POSE_PERFORM run silently
    // played the dummy skeleton instead.
#ifdef __EMSCRIPTEN__
    const bool performer = false;
#else
    const bool performer = Dc3PosePerformerEnabled();
#endif
    if (getenv("MILO_HEADLESS") && !getenv("DC3_POSE") && !performer) {
        printf("Native: headless mode, using dummy skeleton (no pose server)\n");
        if (const char *perform = getenv("DC3_POSE_PERFORM"); perform && *perform)
            printf("Native: DC3_POSE_PERFORM='%s' is not a performer mode "
                   "(choreo, wrong, halftempo, still, stand, replay:FILE)\n", perform);
        // Controller-mode policy (DC3_CONTROLLER_MODE, docs/debugging/native.md):
        // only `forced` pins controller mode here; `faithful` boots out of it,
        // as GestureMgr::GestureMgr does on the 360.
        if (TheGestureMgr && Dc3ControllerModeForced()) {
            TheGestureMgr->SetInControllerMode(true);
        }
        return;
    }

    const char *poseMode = getenv("DC3_POSE");
    // DC3_POSE_CAMERA goes to pose_server.py --camera as is: unset or "auto"
    // = the first V4L2 video-capture device, re-resolved whenever the server
    // reopens it (a USB camera re-enumerates under a new /dev/videoN); also an
    // index, a /dev/videoN path or a /dev/v4l/by-id name.
    const char *camStr = getenv("DC3_POSE_CAMERA");
    std::string camSpec = (camStr && *camStr) ? camStr : "auto";

#ifdef __EMSCRIPTEN__
    // Web: nothing to spawn or connect to.  The provider is created idle; the
    // page's camera pipeline starts it with its first dc3_pose_push
    // (PoseInput_Web.cpp) and stops it when the camera goes away.  Until then
    // BuildSensorFrame feeds the dummy, exactly as with no provider.
    (void)camSpec;
    if ((!poseMode || strcmp(poseMode, "off") != 0) && !TheSkeletonProvider) {
        TheSkeletonProvider = new NativeSkeletonProvider();
        printf("Native: web pose provider ready (waiting for dc3_pose_push)\n");
    }
#else
    // Desktop: spawn the MediaPipe pose server (native/scripts/pose_server.py)
    // and read it over a Unix socket.
    if (!poseMode || strcmp(poseMode, "off") != 0) {
        if (!TheSkeletonProvider) {
            TheSkeletonProvider = new NativeSkeletonProvider();

            // Default socket: the documented /tmp/dc3_pose.sock when attaching
            // to a server someone else started (DC3_POSE_NO_SPAWN); per process
            // when this engine spawns its own, since pose_server.py unlinks
            // whatever is at its path -- a second windowed engine on the box
            // took over the first one's socket (np-harness).
            std::string defaultSocket = getenv("DC3_POSE_NO_SPAWN")
                ? std::string("/tmp/dc3_pose.sock")
                : "/tmp/dc3_pose_" + std::to_string((int)getpid()) + ".sock";
            const char *socketPath = getenv("DC3_POSE_SOCKET");
            if (!socketPath) socketPath = defaultSocket.c_str();

            // MediaPipe BlazePose is the only backend: real per-joint depth
            // (protocol layout 1, DC3-20 in camera-space metres), Apache-2.0.
            // The AGPL YOLO fallback was retired after ground-truth measurement
            // showed MediaPipe dominates it on depth AND detection robustness
            // (tools/pose_corpus/bench_model_z.py, bench_detection.py).
            std::string modelPath = GestureMgr_NativePoseModelPath();

            if (TheSkeletonProvider->Start(socketPath, modelPath, camSpec)) {
                printf("Native: external pose server started\n");
            } else {
                printf("Native: pose tracking unavailable (pose server did not start)\n");
            }
        }
    }
#endif

    // Controller-mode policy (DC3_CONTROLLER_MODE, docs/debugging/native.md):
    // `auto` latches from the provider just chosen -- a running one boots the
    // shell out of controller mode, the image's GestureMgr ctor value (the ctor
    // ran before any provider existed, so it read forced).  Then only a forced
    // policy pins controller mode here.  The web provider is idle until the
    // page's first dc3_pose_push, so the web boots forced and
    // NativeControllerModePolicyTick hands over when the camera starts.
    if (Dc3ControllerModeLatchAuto(NativePoseProviderRunning()) && TheGestureMgr) {
        TheGestureMgr->SetInControllerMode(false);
    }
    if (TheGestureMgr && Dc3ControllerModeForced()) {
        TheGestureMgr->SetInControllerMode(true);
    }
}

void GestureMgr_NativeTerminate() {
    if (TheSkeletonProvider) {
        TheSkeletonProvider->Stop();
        delete TheSkeletonProvider;
        TheSkeletonProvider = nullptr;
    }
    // The sensor and SkeletonUpdate outlive TheGestureMgr; see
    // GestureMgr_NativeSensorTerminate.  With the providers gone the sensor
    // falls back to the dummy frame.
    ResetSlotMap();
}

// Resolve stable slot assignments for this frame's persons. trackIds[k] is the
// identity of the k-th valid person (k in [0,numValid)); on return
// personForSlot[slot] is the person index k filling that slot, or -1. Slots
// whose owner departed are released here (the frame reports them untracked).
// Returns the number of newly-assigned slots (slot reassignments this frame).
// A slot whose owner is absent but still within the dropout hold (see
// sSlotHold) keeps its trackId and reads kSlotHeld in personForSlot.
static const int kSlotHeld = -2;

static int AssignSlots(const int *trackIds, const Vector3 *hips, int numValid,
                       float elapsedMs, int *personForSlot) {
    for (int s = 0; s < NUM_SKELETONS; s++)
        personForSlot[s] = -1;

    bool placed[NUM_SKELETONS];
    for (int k = 0; k < numValid; k++)
        placed[k] = false;

    // 1. Persisting trackIds keep their slot.
    for (int k = 0; k < numValid; k++) {
        if (trackIds[k] < 0) continue;
        for (int s = 0; s < NUM_SKELETONS; s++) {
            if (sSlotTrackId[s] == trackIds[k]) {
                personForSlot[s] = k;
                placed[k] = true;
                break;
            }
        }
    }

    // 2. Release slots whose owner is no longer present -- unless the dropout
    //    hold keeps it (see sSlotHold).
    const int holdMs = NativePoseHoldMs();
    bool releasedNow[NUM_SKELETONS];
    for (int s = 0; s < NUM_SKELETONS; s++) {
        releasedNow[s] = false;
        if (sSlotTrackId[s] >= 0 && personForSlot[s] < 0) {
            SlotHold &h = sSlotHold[s];
            bool hold = holdMs > 0 && h.presentMs >= kHoldMinPresenceMs
                && h.heldMs + elapsedMs <= (float)holdMs;
            if (hold) {
                // A newcomer standing where the held person stood is the
                // tracker's verdict that this is someone new.
                const Vector3 &was = h.last.joints[kJointHipCenter];
                for (int k = 0; k < numValid && hold; k++) {
                    if (placed[k] || trackIds[k] < 0)
                        continue;
                    float dx = hips[k].x - was.x, dy = hips[k].y - was.y, dz = hips[k].z - was.z;
                    if (dx * dx + dy * dy + dz * dz <= kHoldReacquireMetres * kHoldReacquireMetres)
                        hold = false;
                }
            }
            if (hold) {
                h.heldMs += elapsedMs;
                personForSlot[s] = kSlotHeld;
                continue;
            }
            sSlotTrackId[s] = -1;
            ResetSlotHold(s);
            releasedNow[s] = true;
        }
    }

    // 3. New trackIds claim the lowest free slot -- but never one released in
    //    THIS frame.  SkeletonUpdate::UpdateCallbacks archives each slot's
    //    previous pose before polling the new frame (AddToHistory if it was
    //    tracked, else ClearHistory), so a same-frame free+reclaim (person A
    //    leaves, person B enters) would file A's last pose as B's history and
    //    corrupt B's displacement lookback.  Held off one frame, the slot reads
    //    untracked once and the image's own ClearHistory empties it.  trackId < 0
    //    is a transient untracked detection and never claims a persistent slot
    //    (mirrors step 1).
    int reassignments = 0;
    for (int k = 0; k < numValid; k++) {
        if (placed[k] || trackIds[k] < 0) continue;
        for (int s = 0; s < NUM_SKELETONS; s++) {
            if (sSlotTrackId[s] < 0 && personForSlot[s] < 0 && !releasedNow[s]) {
                sSlotTrackId[s] = trackIds[k];
                personForSlot[s] = k;
                // A new occupant must not inherit the previous person's held
                // low-confidence joint positions either.
                NativeSkeletonProvider::ResetJointHold(s);
                ResetSlotHold(s);
                gEstimate.ResetSlot(s);
                reassignments++;
                break;
            }
        }
    }
    return reassignments;
}

// The hamprovider says this mode needs two players: the two properties the
// image's Game::CheckForSkeletonLoss reads to want a second bound player.
static bool NativeModeNeedsTwoPlayers() {
    if (!TheHamProvider)
        return false;
    static Symbol requires_2_players("requires_2_players");
    static Symbol is_in_party_mode("is_in_party_mode");
    const DataNode *two = TheHamProvider->Property(requires_2_players, false);
    const DataNode *party = TheHamProvider->Property(is_in_party_mode, false);
    return (two && two->Int() != 0) || (party && party->Int() != 0);
}

// Xbox builds SkeletonUpdateData::mSkeletonsLeft as a 2-entry array indexed by
// PLAYER, by matching each player's assigned skeleton tracking ID
// (HamPlayerData::GetSkeletonTrackingID, snapshotted in SkeletonUpdate::
// PostUpdate) against the 6 slots in UpdateCallbacks -- natively that is now the
// image's own code.  The assignment itself comes from SkeletonChooser
// (TheGameData->AssignSkeleton) and, in EditMode, HamGameData::
// AutoAssignSkeletons.  NATIVE POLICY, not image behaviour: that UI flow does
// not complete in a headless/fast-boot run with no gesture input, so a player
// with no live skeleton is auto-assigned the first unclaimed slot, preferring a
// quality-filter-valid skeleton and falling back to merely tracked (the static
// dummy is tracked before its filter has seen enough frames to be valid).  It
// runs on GestureMgr's skeletons (last frame's, post quality filter) before the
// frame is handed over, so the binding lands in this frame's snapshot.
//
// It must agree with the chooser, or the two undo each other every frame
// (np-tracking, PLAN W-10).  Two rules:
//  * Player 2 is auto-assigned only in a mode that needs two players
//    (NativeModeNeedsTwoPlayers).  Everywhere else the image's way in for a
//    second person is the drop-in: SkeletonChooser round-robins the extra
//    skeleton for a raised hand ("New player detected!  Raise your hand to
//    join.") and, while it waits, UpdatePlayerSkeletonNavData drops that
//    skeleton's id from the player ids, so SetPlayerSkeletonNavData unbinds
//    whoever holds it.  Auto-assigning player 2 to it re-bound and was unbound
//    every frame, and the chooser switched its active player back and forth;
//    each unbind is an Overshell player_quit (perform.dta on_player_quit:
//    start_score_move_index 1000, flash cards cleared) and each rebind a
//    player_join that restarts the drop-in grace period.  Measured: a second
//    detection for one second at song start (a fake-camera cut) left the
//    dancer on player 2 and a whole song with no move scored and no cards.
//  * A skeleton bound here is no longer the chooser's round-robin candidate
//    (SkeletonChooser::NativeSkeletonBound).  Otherwise a person who is moving
//    while unbound (a dancing camera feed in the menus, or after a re-acquire
//    in a song) is bound here and unbound by the chooser every frame until she
//    stands still or raises a hand.
static int sAutoAssignCount[2] = { 0, 0 };

static SkeletonChooser *NativeChooser() {
    ShellInput *shell = TheHamUI.GetShellInput();
    return shell ? shell->GetSkeletonChooser() : nullptr;
}

static void NativeAutoAssignPlayers() {
    if (!TheGameData || !TheGestureMgr)
        return;
    const Skeleton *bound[2] = { nullptr, nullptr };
    for (int p = 0; p < 2; p++) {
        HamPlayerData *playerData = TheGameData->Player(p);
        int wantId = playerData ? playerData->GetSkeletonTrackingID() : -1;
        if (wantId < 0)
            continue;
        for (int s = 0; s < NUM_SKELETONS; s++) {
            const Skeleton &skel = TheGestureMgr->GetSkeleton(s);
            if (skel.IsTracked() && skel.TrackingID() == wantId) {
                bound[p] = &skel;
                break;
            }
        }
    }
    const int autoPlayers = NativeModeNeedsTwoPlayers() ? 2 : 1;
    for (int pass = 0; pass < 2; pass++) {
        bool requireValid = (pass == 0);
        for (int p = 0; p < autoPlayers; p++) {
            if (bound[p])
                continue;
            for (int s = 0; s < NUM_SKELETONS; s++) {
                const Skeleton &skel = TheGestureMgr->GetSkeleton(s);
                if (requireValid ? !skel.IsValid() : !skel.IsTracked())
                    continue;
                if (bound[0] == &skel || bound[1] == &skel)
                    continue;
                bound[p] = &skel;
                HamPlayerData *playerData = TheGameData->Player(p);
                if (playerData && playerData->GetSkeletonTrackingID() != skel.TrackingID()) {
                    TheGameData->AssignSkeleton(p, skel.TrackingID());
                    sAutoAssignCount[p]++;
                }
                break;
            }
        }
    }
    if (SkeletonChooser *chooser = NativeChooser()) {
        for (int p = 0; p < 2; p++) {
            if (bound[p])
                chooser->NativeSkeletonBound((int)(bound[p] - &TheGestureMgr->GetSkeleton(0)));
        }
    }

    static bool sDebug = Dc3EnvFlag("DC3_SCORING_DEBUG", false);
    if (sDebug) {
        static int sPrevIdx[2] = { -2, -2 };
        int idx[2];
        for (int p = 0; p < 2; p++)
            idx[p] = bound[p] ? (int)(bound[p] - &TheGestureMgr->GetSkeleton(0)) : -1;
        if (idx[0] != sPrevIdx[0] || idx[1] != sPrevIdx[1]) {
            sPrevIdx[0] = idx[0];
            sPrevIdx[1] = idx[1];
            fprintf(stderr, "DC3 SCORING: player->slot binding p0=%d p1=%d\n",
                idx[0], idx[1]);
        }
        // How often this had to (re)bind: once per join is normal; every frame
        // means something else unbinds the player each frame.
        static int sReported[2] = { 0, 0 };
        static auto sLast = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - sLast >= std::chrono::seconds(5)) {
            sLast = now;
            if (sAutoAssignCount[0] != sReported[0] || sAutoAssignCount[1] != sReported[1]) {
                fprintf(stderr, "DC3 SCORING: auto-assign rebinds p0=%d p1=%d (total)\n",
                    sAutoAssignCount[0], sAutoAssignCount[1]);
                sReported[0] = sAutoAssignCount[0];
                sReported[1] = sAutoAssignCount[1];
            }
        }
    }
}

// Milliseconds since the previous ACCEPTED frame (Xbox gets this from
// NUI_SKELETON_FRAME). Displacement scoring integrates these, so a garbage value
// poisons it; clamped to [1,200], first accepted frame returns 33.
//
// `sensorTs` is the pose packet's own capture timestamp in seconds (<0: none).
// When successive stamps advance, the elapsed time is theirs, not the game
// loop's wall clock: a real sensor's frame interval is a property of the
// camera, and under DC3_FAST_TIME (song time = 1/120 s per game frame, however
// long the frame took) a wall-clock interval does not even share the song's
// time base -- a replayed choreography then reads as moving too fast or too
// slow and every displacement node mis-scores it.  A stamp that does not
// advance (a looped video, a restarted server) falls back to wall clock.
static int AcceptedFrameElapsed(double sensorTs = -1.0) {
    static std::chrono::steady_clock::time_point sPrevTime;
    static bool sHavePrevTime = false;
    static double sPrevSensorTs = -1.0;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    long ms = 33;
    if (sHavePrevTime) {
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - sPrevTime).count();
        if (sensorTs >= 0.0 && sPrevSensorTs >= 0.0 && sensorTs > sPrevSensorTs)
            ms = (long)((sensorTs - sPrevSensorTs) * 1000.0 + 0.5);
        if (ms < 1) ms = 1;
        if (ms > 200) ms = 200;
    } else {
        sHavePrevTime = true;
    }
    sPrevTime = now;
    sPrevSensorTs = sensorTs;
    return (int)ms;
}

// DC3_SCORING_DEBUG=1: once-per-second frame-gating liveness counters, matching
// the PrevSkeleton counter style. accepted/skipped are per-call increments;
// reassigns accumulates the AssignSlots reassignment count.
static void ScoringDebugTick(bool accepted, bool skipped, int reassigns) {
    static bool sDebug = Dc3EnvFlag("DC3_SCORING_DEBUG", false);
    if (!sDebug) return;
    static unsigned sAccepted = 0, sSkipped = 0, sReassign = 0;
    static std::chrono::steady_clock::time_point sLastPrint;
    if (accepted) sAccepted++;
    if (skipped) sSkipped++;
    sReassign += reassigns;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - sLastPrint > std::chrono::seconds(1)) {
        sLastPrint = now;
        fprintf(stderr, "DC3 SCORING: newFrames=%u skipped=%u slotReassigns=%u\n",
            sAccepted, sSkipped, sReassign);
    }
}

// DC3_POSE_LATENCY_LOG=1: how old each pose frame is when the game takes it,
// capture to SkeletonUpdate, every 5 s:
//   DC3 POSE AGE: capture->game median 61 ms p10 48 p90 79 (n=150)
// pose_server.py stamps a packet with its capture time on time.monotonic()
// (a V4L2 camera's own buffer stamp, CLOCK_MONOTONIC), which is
// steady_clock's clock here, so the difference is the pipeline's latency up
// to the game loop: driver, MediaPipe, socket, poll.  It is most of what
// DC3_POSE_LATENCY_MS declares; what it cannot see is the exposure itself and
// the audio output delay (the player dances to what she hears).  A stamp that
// is not on this clock (the in-process performer stamps song time; the web
// stamps the page's clock) reads outside 0..2 s and is not counted.
static void PoseAgeLog(double captureSecs) {
#ifndef __EMSCRIPTEN__
    static const bool sOn = Dc3EnvFlag("DC3_POSE_LATENCY_LOG", false);
    if (!sOn || captureSecs < 0.0)
        return;
    static std::vector<float> sAges;
    static std::chrono::steady_clock::time_point sLast = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    double nowSecs = std::chrono::duration<double>(now.time_since_epoch()).count();
    double age = nowSecs - captureSecs;
    if (age >= 0.0 && age < 2.0)
        sAges.push_back((float)(age * 1000.0));
    if (now - sLast < std::chrono::seconds(5))
        return;
    sLast = now;
    if (sAges.empty()) {
        fprintf(stderr, "DC3 POSE AGE: no frame stamped on this clock in 5 s\n");
        return;
    }
    std::sort(sAges.begin(), sAges.end());
    size_t n = sAges.size();
    fprintf(stderr, "DC3 POSE AGE: capture->game median %.0f ms p10 %.0f p90 %.0f (n=%zu)\n",
            sAges[n / 2], sAges[n / 10], sAges[(n * 9) / 10], n);
    sAges.clear();
#else
    (void)captureSecs;
#endif
}

// The in-process performer (PosePerformer_Native.h) publishes with the
// default `unlevelled = false`: its synthetic modes play a Kinect.  A replay
// is a recording from a real camera, so it needs the tilt estimate like the
// live webcam it stands in for.
static bool PerformerReplaysRecording() {
#ifdef __EMSCRIPTEN__
    return false;
#else
    static const bool sReplay = [] {
        const char *mode = getenv("DC3_POSE_PERFORM");
        return mode && !strncmp(mode, "replay:", 7);
    }();
    return sReplay && Dc3PosePerformerEnabled();
#endif
}

// The sensor's frame.  memset gives every slot the untracked contract the
// image's own sensorless path (SkeletonUpdate::Update's StubSkeletonFrame arm)
// uses, except the tracking id, set to -1 per slot below as that arm does.
static SkeletonFrame sFrame; // 0x11c8 bytes, main thread only

static void BeginSensorFrame(int elapsedMs) {
    static int sFrameNumber = 0;
    memset(&sFrame, 0, sizeof(sFrame));
    sFrame.mFrameNumber = ++sFrameNumber;
    sFrame.mElapsedMs = elapsedMs;
    // The providers are upright cameras: floor plane y = 0, up = +y (the same
    // frame CharCameraInput hands Poll for the fatality targets).
    sFrame.mFloorNormal.Set(0.0f, 1.0f, 0.0f);
    sFrame.mFloorClipPlane.Set(0.0f, 1.0f, 0.0f, 0.0f);
    for (int s = 0; s < NUM_SKELETONS; s++)
        sFrame.mSkeletonDatas[s].mTrackingID = -1;
}

// mClippedFlags is read by Skeleton::Poll as the slot's NUI enrollment index;
// native has no enrollment, so hand back the one the slot already has rather
// than re-enrolling it every frame.
static void FinishTrackedSlot(int s) {
    if (TheGestureMgr) {
        IdentityInfo *info = TheGestureMgr->GetIdentityInfo(s);
        if (info)
            sFrame.mSkeletonDatas[s].mClippedFlags = info->EnrollmentIndex();
    }
}

// DC3_POSE_ESTIMATE (PoseEstimate_Native.h): joints the camera could not see,
// completed per slot just before the slot layer lays the person in.
namespace {
bool gEstimateRead = false;
int gEstimateModeOverride = -1;
float gEstimateGainOverride = -1.0f;

void EstimateReadMode() {
    if (gEstimateRead)
        return;
    gEstimateRead = true;
    Dc3PoseEstimate::Mode mode;
    if (gEstimateModeOverride >= 0) {
        mode = (Dc3PoseEstimate::Mode)gEstimateModeOverride;
    } else {
        const char *v = getenv("DC3_POSE_ESTIMATE");
        bool ok = true;
        mode = Dc3PoseEstimate::ParseMode(v, &ok);
        if (!ok)
            fprintf(stderr, "DC3_POSE_ESTIMATE='%s' ignored (want off, tracker or kinematic)\n", v);
    }
    float gain;
    if (gEstimateGainOverride >= 0.0f) {
        gain = gEstimateGainOverride;
    } else {
        const char *g = getenv("DC3_POSE_ESTIMATE_GAIN");
        bool ok = true;
        gain = Dc3PoseEstimate::ParseGain(g, &ok);
        if (!ok)
            fprintf(stderr, "DC3_POSE_ESTIMATE_GAIN='%s' ignored (want 0 .. 1)\n", g);
    }
    gEstimate.SetMode(mode);
    gEstimate.SetGain(gain);
    gEstimate.Reset();
}

// The first estimated joint of a run, and then every 30 s of estimates, say
// how much of the body is being estimated.
void EstimateLog(int wrote, float elapsedMs) {
    static bool sSaid = false;
    static float sSinceMs = 0.0f;
    static unsigned sFrames = 0, sJoints = 0;
    sSinceMs += elapsedMs;
    sFrames++;
    sJoints += wrote;
    if ((!sSaid && wrote > 0) || (sSaid && sSinceMs >= 30000.0f && sJoints > 0)) {
        fprintf(stderr, "DC3 POSE ESTIMATE: %s (gain %.2f) %.1f joints a frame estimated (%u frames)\n",
                Dc3PoseEstimate::ModeName(gEstimate.GetMode()), gEstimate.Gain(),
                sFrames ? (double)sJoints / sFrames : 0.0, sFrames);
        sSaid = true;
        sSinceMs = 0.0f;
        sFrames = sJoints = 0;
    }
}
} // namespace

void NativePoseEstimateTestSet(int mode, float gain) {
    gEstimateModeOverride = mode;
    gEstimateGainOverride = gain;
    gEstimateRead = false;
    EstimateReadMode();
}

const Dc3PoseEstimate::Estimator &NativePoseEstimator() { return gEstimate; }

// Lay this frame's persons into their persistent slots.  `elapsedMs` is the
// sensor time since the previous frame (the dropout hold's clock); `real` =
// a camera frame, not one the fill made (only those teach the estimator).
static int FillPersonSlots(const NativeSkeletonProvider::PersonData *persons, int numPersons,
                           float elapsedMs, bool real = true) {
    int trackIds[NUM_SKELETONS], origIdx[NUM_SKELETONS], numValid = 0;
    Vector3 hips[NUM_SKELETONS];
    for (int i = 0; i < numPersons && numValid < NUM_SKELETONS; i++) {
        if (persons[i].valid) {
            trackIds[numValid] = persons[i].trackId;
            hips[numValid] = persons[i].joints[kJointHipCenter];
            origIdx[numValid] = i;
            numValid++;
        }
    }
    int personForSlot[NUM_SKELETONS];
    int reassigns = AssignSlots(trackIds, hips, numValid, elapsedMs, personForSlot);
    for (int s = 0; s < NUM_SKELETONS; s++) {
        int k = personForSlot[s];
        if (k >= 0) {
            NativeSkeletonProvider::PersonData person = persons[origIdx[k]];
            EstimateReadMode();
            EstimateLog(gEstimate.Apply(s, person, elapsedMs * 0.001f, real), elapsedMs);
            NativeSkeletonProvider::FillSkeletonData(sFrame.mSkeletonDatas[s], person, s);
            FinishTrackedSlot(s);
            SlotHold &h = sSlotHold[s];
            h.last = person;
            h.presentMs += elapsedMs;
            h.heldMs = 0.0f;
        } else if (k == kSlotHeld) {
            // What a Kinect reports for a body it can no longer see clearly:
            // still tracked, every joint inferred.  A joint the detector had
            // already lost stays not-tracked, so FillSkeletonData's joint
            // hold puts it at its last good position too.
            NativeSkeletonProvider::PersonData held = sSlotHold[s].last;
            for (int j = 0; j < kNumJoints; j++) {
                if (held.confidence[j] > kConfidenceInferred)
                    held.confidence[j] = kConfidenceInferred;
            }
            NativeSkeletonProvider::FillSkeletonData(sFrame.mSkeletonDatas[s], held, s);
            FinishTrackedSlot(s);
        }
    }
    return reassigns;
}

// How many standing stand-ins the NO-PROVIDER sensor shows.  NATIVE POLICY, not
// image behaviour: the dummy exists so the game is playable with a controller
// and no camera.  With one stand-in a two-player mode is a dead end -- the
// image's Game::CheckForSkeletonLoss pauses the moment player 2 is not
// playing (dance_battle.dta's pause_on_skeleton_loss lets it through from
// beat 4) and the pause screen's Continue stays disabled until two players
// are bound (pause.dta update_skeleton_status_internal).  On a 360 that pause
// is right: it means a real person left.  Without a camera nobody can step
// in, so in exactly the modes that test -- requires_2_players or
// is_in_party_mode, the same two properties CheckForSkeletonLoss reads -- a
// second stand-in appears in slot 1.
//
// Only this branch calls it: with a provider running (socket, web push), the
// people in front of the camera are the truth and a missing player 2 must
// pause, as on the 360.  DC3_DUMMY_PLAYERS=1 keeps the lone stand-in
// regardless of mode (reproduces that pause without a camera);
// DC3_DUMMY_PLAYERS=2 always shows two.  Anything else is the mode rule.
int NativeDummyPlayerCount(bool modeNeedsTwo, const char *policy) {
    if (policy && !strcmp(policy, "1"))
        return 1;
    if (policy && !strcmp(policy, "2"))
        return 2;
    return modeNeedsTwo ? 2 : 1;
}

static int NativeDummyPlayerCount() {
    static const char *sPolicy = getenv("DC3_DUMMY_PLAYERS");
    return NativeDummyPlayerCount(NativeModeNeedsTwoPlayers(), sPolicy);
}

// A new frame when the provider produced one (it runs at camera rate, slower
// than the game loop: re-integrating the same generation would dilute the
// displacement lookback), null otherwise.  With no provider running, the static
// tracked dummy in slot 0 (and, in two-player modes, a second one in slot 1:
// NativeDummyPlayerCount) is a new frame every game frame.  Since move scoring
// is DEFAULT-ON (App.cpp's DC3_NATIVE_SCORING gate) that dummy is the
// default-run SCORING INPUT: it drives the whole pipeline (archive-before-poll
// -> FilterQueue::Poll -> MoveDir) every frame and yields a deterministic
// DetectFrac ~0 -- the correct "player standing still" signal.  Keep it TRACKED:
// untracked would break ShellInput::HasSkeleton()/SkeletonChooser::Poll and drop
// scoring to the errors=1.0 short-circuit.  A transient 0-person dropout from a
// running provider stays on the provider branch (every slot reads untracked)
// and never reaches the dummy.
// DC3_PERF_LOG=1 (the playtest kit sets it): every 5 s of wall time, the
// game loop's rate and how many NEW pose frames the sensor accepted per
// second -- the two numbers PLAN L-3 asks for, measured where they meet.
// BuildSensorFrame runs once per game frame (SkeletonUpdate's PostUpdate).
static void PerfTick(bool providerRunning, bool accepted, int persons) {
    static const bool sOn = Dc3EnvFlag("DC3_PERF_LOG", false);
    if (!sOn)
        return;
    static std::chrono::steady_clock::time_point sStart = std::chrono::steady_clock::now();
    static unsigned sFrames = 0, sPose = 0, sPersons = 0;
    sFrames++;
    if (providerRunning && accepted) {
        sPose++;
        sPersons += persons;
    }
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(now - sStart).count();
    if (secs >= 5.0) {
        fprintf(stderr, "DC3 PERF: fps=%.1f pose_fps=%.1f persons=%.2f window_s=%.1f provider=%d\n",
                sFrames / secs, sPose / secs, sPose ? (double)sPersons / sPose : 0.0, secs,
                providerRunning ? 1 : 0);
        sStart = now;
        sFrames = sPose = sPersons = 0;
    }
}

// DC3_POSE_FILL (PoseFill_Native.h): frames between camera frames, at the
// Kinect's 30 Hz of song time, while the camera is slow.  The default,
// extrap, steps each camera frame on by its joints' velocity for up to one
// camera period, at DC3_POSE_FILL_GAIN of the full step -- deliberately
// generous (owner decision 2026-11-14: over-credit, then calibrate down).
// interp instead runs the stream one camera period behind the camera and
// adds that delay to the scorer's latency (Dc3SetPoseFillDelaySeconds).
//
// While the fill is engaged a camera frame is NOT laid into the slots when it
// arrives: it is levelled (the gravity estimate is sensor state built from
// camera frames only), recorded (DC3_POSE_RECORD writes camera frames as they
// arrived, never fill frames) and buffered; the filler then hands frames to
// DeliverFillFrame when they are due, and those go through the same slot
// layer -- AssignSlots, P-12's dropout hold, the joint hold -- as a camera
// frame does when the fill is off.
namespace {
Dc3PoseFill::Filler gFill;
bool gFillModeRead = false;
Dc3PoseFill::Mode gFillModeOverride = Dc3PoseFill::kExtrap;
bool gFillHaveOverride = false;
double (*gFillClock)() = nullptr;

void FillReadMode() {
    if (gFillModeRead)
        return;
    gFillModeRead = true;
    Dc3PoseFill::Mode mode;
    if (gFillHaveOverride) {
        mode = gFillModeOverride;
    } else {
        const char *v = getenv("DC3_POSE_FILL");
        bool ok = true;
        mode = Dc3PoseFill::ParseMode(v, &ok);
        if (!ok)
            fprintf(stderr, "DC3_POSE_FILL='%s' ignored (want off, interp, hold, extrap or extrapc)\n",
                    v);
    }
    gFill.SetMode(mode);
    const char *g = gFillHaveOverride ? nullptr : getenv("DC3_POSE_FILL_GAIN");
    bool gainOk = true;
    gFill.SetGain(Dc3PoseFill::ParseGain(g, &gainOk));
    if (!gainOk)
        fprintf(stderr, "DC3_POSE_FILL_GAIN='%s' ignored (want 0 .. 1)\n", g);
    gFill.Reset();
}

double FillClock() { return gFillClock ? gFillClock() : (double)MoveDir::SongSeconds(); }

// Engagement changes, and delay changes of 20 ms or more, move the scorer's
// latency; say so once each time.
void FillPublishDelay() {
    static bool sWasEngaged = false;
    static float sLoggedDelay = 0.0f;
    float delay = (float)gFill.Delay();
    Dc3SetPoseFillDelaySeconds(delay);
    bool changed = gFill.Engaged() != sWasEngaged;
    if (changed || std::fabs(delay - sLoggedDelay) >= 0.02f) {
        sWasEngaged = gFill.Engaged();
        sLoggedDelay = delay;
        double med = gFill.RateInterval();
        fprintf(stderr,
                "DC3 POSE FILL: %s (gain %.2f) %s at song %.2f s (camera %.1f frames/s, stream delay "
                "%.0f ms added to the scorer's latency)\n",
                Dc3PoseFill::ModeName(gFill.GetMode()), gFill.Gain(),
                changed ? (sWasEngaged ? "engaged" : "released") : "delay", FillClock(),
                med > 0.0 ? 1.0 / med : 0.0, delay * 1000.0f);
    }
}
} // namespace

void NativePoseFillTestSet(int mode, double (*clock)()) {
    gFillHaveOverride = mode >= 0;
    if (mode >= 0)
        gFillModeOverride = (Dc3PoseFill::Mode)mode;
    gFillClock = clock;
    gFillModeRead = false;
    FillReadMode();
    Dc3SetPoseFillDelaySeconds(0.0f);
}

const Dc3PoseFill::Filler &NativePoseFiller() { return gFill; }

// A frame the filler made due: into the slots exactly as a camera frame goes
// when the fill is off.  Its time base is the camera's (out.ts), so the
// elapsed time is what the frame shows, not the game loop's.
static void DeliverFillFrame(const Dc3PoseFill::Output &out) {
    BeginSensorFrame(AcceptedFrameElapsed(out.ts));
    int reassigns = FillPersonSlots(out.persons, out.numPersons, (float)sFrame.mElapsedMs, out.real);
    ScoringDebugTick(out.real, false, reassigns);
    if (out.levelled)
        NativeLevelSensorFrameBy(sFrame, out.floorNormal);
}

// DC3_POSE_STALL_MS (default kDefaultStallMs, 0 = off): a pose source that
// stops sending frames while it still "runs" -- a pose server hung in
// inference or SIGSTOPped, a camera driver that blocks, a web worker that
// stopped posting -- used to leave every slot at its last frame, tracked,
// with the song scoring a frozen player.  A Kinect never stops: with nobody
// in view it still sends frames with nobody in them.  So once the source has
// been silent this long, the sensor delivers exactly that, one empty frame
// per 1/30 s of wall time: P-12's hold keeps the player for DC3_POSE_HOLD_MS,
// then the slot reads untracked and the image's own skeleton-loss handling
// takes over (Game::CheckForSkeletonLoss pauses a song, "stand in front of
// the sensor").  When frames return, the person is laid in again and the
// pause screen behaves as on a 360.  Wall time, not song time: a stalled
// source must be noticed on the pause screen too.
static const int kDefaultStallMs = 1500;
static const char *kStallNoticeKey = "pose-stall";

static int NativePoseStallMs() {
    const char *v = getenv("DC3_POSE_STALL_MS");
    if (!v || !*v)
        return kDefaultStallMs;
    char *end = nullptr;
    long ms = strtol(v, &end, 10);
    if (end == v || *end != '\0' || ms < 0)
        return kDefaultStallMs;
    return ms > 60000 ? 60000 : (int)ms;
}

namespace {
struct StallWatch {
    std::chrono::steady_clock::time_point lastNew, lastEmpty;
    bool haveNew = false;
    bool stalled = false;
    unsigned stalls = 0;
};
StallWatch gStall;
} // namespace

unsigned NativePoseStalls() { return gStall.stalls; }
bool NativePoseStalled() { return gStall.stalled; }

// Called once per BuildSensorFrame with the provider running.  Returns true
// when it laid an empty frame into the slots (never when this call already
// delivered a frame).
static bool StallWatchTick(bool newFrame, bool delivered) {
    auto now = std::chrono::steady_clock::now();
    if (newFrame) {
        if (gStall.stalled) {
            double secs = std::chrono::duration<double>(now - gStall.lastNew).count();
            fprintf(stderr, "DC3 POSE STALL: frames are back after %.1f s\n", secs);
            NativeNotice::Clear(kStallNoticeKey);
            NativeNotice::Post(kStallNoticeKey, "Pose frames are back - tracking resumed.", "", 4.0f);
        }
        gStall.stalled = false;
        gStall.haveNew = true;
        gStall.lastNew = now;
        return false;
    }
    int stallMs = NativePoseStallMs();
    if (stallMs <= 0 || !gStall.haveNew)
        return false;
    double silentMs = std::chrono::duration<double, std::milli>(now - gStall.lastNew).count();
    if (silentMs < stallMs)
        return false;
    if (!gStall.stalled) {
        gStall.stalled = true;
        gStall.stalls++;
        gStall.lastEmpty = now - std::chrono::milliseconds(100);
        fprintf(stderr,
                "DC3 POSE STALL: no pose frame for %.1f s - the sensor reports nobody until frames "
                "return (DC3_POSE_STALL_MS=%d)\n",
                silentMs / 1000.0, stallMs);
        NativeNotice::Post(kStallNoticeKey,
                           "The pose stream stopped sending frames - pose tracking is paused until "
                           "it is back.",
                           "Pose stream stalled", 20.0f);
        gFill.Reset();
    }
    if (delivered
        || std::chrono::duration<double, std::milli>(now - gStall.lastEmpty).count() < 1000.0 / 30.0)
        return false;
    gStall.lastEmpty = now;
    BeginSensorFrame(AcceptedFrameElapsed());
    ScoringDebugTick(true, false, FillPersonSlots(nullptr, 0, (float)sFrame.mElapsedMs));
    sFrame.mFloorNormal.Set(0.0f, 1.0f, 0.0f);
    return true;
}

static const SkeletonFrame *BuildSensorFrame() {
    bool providerRunning = false;
    bool newFrame = false;
    bool delivered = false;

    FillReadMode();
    if (TheSkeletonProvider && TheSkeletonProvider->IsRunning()) {
        providerRunning = true;
        TheSkeletonProvider->Poll();

        static uint32_t sExtLastFrameId = 0;
        static bool sExtHaveFrame = false;
        uint32_t frameId = TheSkeletonProvider->FrameId();
        newFrame = !sExtHaveFrame || frameId != sExtLastFrameId;
        sExtHaveFrame = true;
        sExtLastFrameId = frameId;

        if (newFrame) {
            PoseAgeLog(TheSkeletonProvider->Timestamp());
            static NativeSkeletonProvider::PersonData sPersons[NativeSkeletonProvider::kMaxPersons];
            int numPersons = TheSkeletonProvider->NumPersons();
            for (int i = 0; i < numPersons; i++)
                sPersons[i] = TheSkeletonProvider->GetPerson(i);
            // SkeletonFrame::Create's gravity step (PoseLevel_Native.h): the
            // slots hold raw camera space; a camera that does not know its
            // gravity gets an estimate, everything else (0,1,0).
            bool unlevelled = TheSkeletonProvider->FrameUnlevelled() || PerformerReplaysRecording();

            Dc3PoseFill::Source src;
            src.arrival = FillClock();
            src.ts = TheSkeletonProvider->Timestamp();
            src.frameId = frameId;
            src.numPersons = numPersons;
            for (int i = 0; i < numPersons; i++)
                src.persons[i] = sPersons[i];
            bool engaged = gFill.Push(src);
            if (!engaged) {
                // The camera frame, now: the path with the fill off.
                BeginSensorFrame(AcceptedFrameElapsed(TheSkeletonProvider->Timestamp()));
                ScoringDebugTick(true, false, FillPersonSlots(sPersons, numPersons, (float)sFrame.mElapsedMs));
                sFrame.mFloorNormal.Set(0.0f, 0.0f, 0.0f); // "not levelled", for the filler
#ifndef __EMSCRIPTEN__
                // A replayed session frame is levelled as it was when recorded
                // (the estimate is session state a one-song replay cannot rebuild).
                if (!Dc3PoseSessionReplayLevel(sFrame, frameId))
#endif
                    NativeSensorLevel(sFrame, sPersons, numPersons, unlevelled, sFrame.mElapsedMs * 0.001f);
                gFill.MarkLevel(sFrame.mFloorNormal);
                if (sFrame.mFloorNormal.x == 0.0f && sFrame.mFloorNormal.y == 0.0f
                    && sFrame.mFloorNormal.z == 0.0f)
                    sFrame.mFloorNormal.Set(0.0f, 1.0f, 0.0f);
#ifndef __EMSCRIPTEN__
                // DC3_POSE_RECORD: what the sensor accepted, for an offline replay.
                Dc3PoseSessionRecord(sPersons, numPersons, frameId, TheSkeletonProvider->Timestamp(),
                                     unlevelled, sFrame);
#endif
                delivered = true;
            } else {
                // Levelled and recorded now, laid into the slots when due.
                static SkeletonFrame sArrival;
                memset(&sArrival, 0, sizeof(sArrival));
                sArrival.mElapsedMs = gFill.ArrivalElapsedMs();
                for (int s = 0; s < NUM_SKELETONS; s++)
                    sArrival.mSkeletonDatas[s].mTrackingID = -1;
#ifndef __EMSCRIPTEN__
                if (!Dc3PoseSessionReplayLevel(sArrival, frameId))
#endif
                    NativeSensorLevel(sArrival, sPersons, numPersons, unlevelled,
                                      sArrival.mElapsedMs * 0.001f);
                gFill.MarkLevel(sArrival.mFloorNormal);
                if (sArrival.mFloorNormal.x == 0.0f && sArrival.mFloorNormal.y == 0.0f
                    && sArrival.mFloorNormal.z == 0.0f)
                    sArrival.mFloorNormal.Set(0.0f, 1.0f, 0.0f);
#ifndef __EMSCRIPTEN__
                Dc3PoseSessionRecord(sPersons, numPersons, frameId, TheSkeletonProvider->Timestamp(),
                                     unlevelled, sArrival);
#endif
            }
        }
        if (gFill.Engaged()) {
            Dc3PoseFill::Output out;
            if (gFill.Poll(FillClock(), out)) {
                DeliverFillFrame(out);
                delivered = true;
            }
        }
        if (StallWatchTick(newFrame, delivered))
            delivered = true;
        FillPublishDelay();
        if (!delivered)
            ScoringDebugTick(false, true, 0);
    }

    if (!providerRunning) {
        gStall = StallWatch(); // a new source starts unwatched
        NativeNotice::Clear(kStallNoticeKey);
        ResetSlotMap();
        gFill.Reset();
        FillPublishDelay();
        BeginSensorFrame(AcceptedFrameElapsed());
        int dummies = NativeDummyPlayerCount();
        for (int d = 0; d < dummies; d++) {
            NativeSkeletonProvider::FillDummySkeletonData(sFrame.mSkeletonDatas[d], d);
            FinishTrackedSlot(d);
        }
        NativeSensorLevel(sFrame, nullptr, 0, false, sFrame.mElapsedMs * 0.001f);
        delivered = true;
    }

    // pose_fps counts camera frames only, never fill frames.
    PerfTick(providerRunning, newFrame || !providerRunning,
             providerRunning && TheSkeletonProvider ? TheSkeletonProvider->NumPersons() : 0);
    if (!delivered)
        return nullptr;
    NativeAutoAssignPlayers();
    return &sFrame;
}

const SkeletonFrame *NativeSensorCameraInput::PollNewFrame() {
    if (!sSensorFeedEnabled)
        return nullptr;
    return BuildSensorFrame();
}

// The skeleton slot a tracking id holds this frame, -1 if none: the camera
// stream (CameraStream_Native.cpp) turns a segmented person into the NUI
// player index slot + 1 with it.
int NativeSlotForTrackId(int trackId) {
    if (trackId == -1) // the free-slot marker, never a person
        return -1;
    for (int s = 0; s < NUM_SKELETONS; s++) {
        if (sSlotTrackId[s] == trackId)
            return s;
    }
    return -1;
}

// PLAN P-4: the LiveCameraInput stand-in.  Created the first time the webcam
// publishes a frame (pose server protocol v3 on desktop, dc3_camera_push on
// web) and handed to GestureMgr, which the image binds to the NUI device at
// construction.  With no camera it is never created, so every
// GetLiveCameraInput() consumer keeps its null path.
static LiveCameraInput *sCameraStandIn = nullptr;

bool NativeCameraStandInAttached() { return sCameraStandIn != nullptr; }

static void NativeCameraPoll(GestureMgr *mgr) {
    if (!sCameraStandIn) {
        if (!NativeCamera::Active())
            return;
        sCameraStandIn = LiveCameraInput::NativeCreate();
        mgr->SetNativeLiveCameraInput(sCameraStandIn);
        printf("Native: camera stream live -> LiveCameraInput stand-in attached\n");
    }
    NativeCamera::Latch(NativeSlotForTrackId);
    // The image's LiveCameraInput is the SkeletonUpdate camera input, and its
    // PollTracking (clear the polled/received flags, poll the colour stream)
    // runs once per skeleton update.  Natively the sensor is that input, so the
    // stand-in is ticked here instead, once per frame.
    sCameraStandIn->PollTracking();
}

// Called each frame by GestureMgr::Poll().  The skeleton pipeline no longer runs
// here -- it is SkeletonUpdate's, driven from App's skeleton_post_update step.
void GestureMgr_NativePoll(GestureMgr *mgr) {
    NativeCameraPoll(mgr);
    // NATIVE POLICY (pre-existing, not image behaviour): seed the active
    // skeleton id so GetActiveSkeletonTrackingID() is valid and HamNavList::Poll
    // finds a skeleton; the image gets it from the Kinect shell flow.
    if (mgr->GetActiveSkeletonTrackingID() <= 0) {
        mgr->SetActiveSkeletonTrackingID(1);
    }
}

#endif // HX_NATIVE
