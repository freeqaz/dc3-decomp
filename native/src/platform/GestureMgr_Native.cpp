#ifdef HX_NATIVE

#include "Skeleton_Native.h"
#include "NativeSettings.h" // Dc3EnvFlag, Dc3ControllerModeForced
#include "platform/JoypadScriptHook.h"
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h" // SkeletonCallback
#include "gesture/SkeletonHistory.h"
#include "gesture/SkeletonUpdate.h"
#include "hamobj/HamGameData.h"   // TheGameData (player->skeleton binding)
#include "hamobj/HamPlayerData.h" // GetSkeletonTrackingID
#include "obj/Task.h"
#include "os/Debug.h" // TheDebug.AddExitCallback
#include <vector>
#ifdef ENABLE_NCNN
#include "pose/InternalPoseProvider.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstring>

// The native sensor.  On the 360, SkeletonUpdate (the image's skeleton hub:
// history archive, Skeleton::Poll of six slots, the per-player binding, the
// callback fan-out) is fed by the Kinect NUI device.  Natively that device is
// replaced -- PLATFORM -- by this CameraInput, an override camera
// (IsOverride() true, as CameraInput's own default) whose PollNewFrame()
// builds a SkeletonFrame from the pose provider (DC3_POSE=internal|external)
// or, with no provider, the static tracked dummy skeleton.  Everything
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

#ifdef ENABLE_NCNN
static InternalPoseProvider *sInternalPose = nullptr;
#endif

// Persistent trackId -> skeleton-slot assignment (index = slot, value = owning
// trackId, -1 = free). A new trackId claims the lowest free slot and keeps it
// while it persists; when a trackId stops appearing the slot is released so a
// later person cannot inherit the departed person's archive history. Reset when
// no provider runs. Shared by both live branches (dummy always uses slot 0).
static int sSlotTrackId[NUM_SKELETONS] = {-1, -1, -1, -1, -1, -1};

static void ResetSlotMap() {
    for (int s = 0; s < NUM_SKELETONS; s++)
        sSlotTrackId[s] = -1;
}

// The scripted-input `wake` directive's question: would pressing the
// controller-mode wake button change anything? Only outside controller mode
// (never under DC3_CONTROLLER_MODE=forced, where controller mode is pinned on),
// which keeps `wake` a no-op for the native default.
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

// Native implementation of GestureMgr::Init's device half: the pose provider
// (webcam + MediaPipe / ncnn) that the sensor reads.
void GestureMgr_NativeInit() {
    ResetSlotMap();
    JoypadScriptSetWakeNeeded(NativeWakeNeeded);

    // In headless mode (tests, CLI tools), skip the pose server entirely.
    // The dummy skeleton in GestureMgr_NativePoll provides a neutral standing
    // pose so skeleton-gated paths still work without a real camera.
    // An explicit DC3_POSE overrides the skip so headless CI can exercise the
    // live-provider path against a synthetic pose server.
    if (getenv("MILO_HEADLESS") && !getenv("DC3_POSE")) {
        printf("Native: headless mode, using dummy skeleton (no pose server)\n");
        // Controller-mode policy (DC3_CONTROLLER_MODE, docs/debugging/native.md):
        // only `forced` pins controller mode here; `faithful` boots out of it,
        // as GestureMgr::GestureMgr does on the 360.
        if (TheGestureMgr && Dc3ControllerModeForced()) {
            TheGestureMgr->SetInControllerMode(true);
        }
        return;
    }

    const char *poseMode = getenv("DC3_POSE");
    const char *camStr = getenv("DC3_POSE_CAMERA");
    int camIdx = camStr ? atoi(camStr) : 0;

#ifdef ENABLE_NCNN
    // Try internal ncnn-based pose estimation first (unless explicitly set to external)
    if (!poseMode || strcmp(poseMode, "external") != 0) {
        const char *modelDir = getenv("DC3_POSE_MODELS");
        if (!modelDir) modelDir = "native/models";
        bool useGPU = getenv("DC3_POSE_GPU") != nullptr;

        sInternalPose = new InternalPoseProvider();
        if (sInternalPose->Start(modelDir, camIdx, useGPU)) {
            printf("Native: internal pose estimation started (ncnn + RTMPose)\n");
            goto pose_ready;
        }
        printf("Native: internal pose failed, falling back to external server\n");
        delete sInternalPose;
        sInternalPose = nullptr;
    }
#endif

    // Fall back to external Python pose server
    if (!poseMode || strcmp(poseMode, "off") != 0) {
        if (!TheSkeletonProvider) {
            TheSkeletonProvider = new NativeSkeletonProvider();

            const char *socketPath = getenv("DC3_POSE_SOCKET");
            if (!socketPath) socketPath = "/tmp/dc3_pose.sock";

            // MediaPipe BlazePose is the only backend: real per-joint depth
            // (protocol layout 1, DC3-20 in camera-space metres), Apache-2.0.
            // The AGPL YOLO fallback was retired after ground-truth measurement
            // showed MediaPipe dominates it on depth AND detection robustness
            // (tools/pose_corpus/bench_model_z.py, bench_detection.py).
            const char *modelPath = getenv("DC3_POSE_MODEL");
            if (!modelPath) modelPath = "native/models/pose_landmarker_full.task";

            if (TheSkeletonProvider->Start(socketPath, modelPath, camIdx)) {
                printf("Native: external pose server started\n");
            } else {
                printf("Native: pose tracking unavailable (no ncnn, no pose server)\n");
            }
        }
    }

pose_ready:

    // Controller-mode policy (DC3_CONTROLLER_MODE, docs/debugging/native.md):
    // only `forced` pins controller mode here.
    if (TheGestureMgr && Dc3ControllerModeForced()) {
        TheGestureMgr->SetInControllerMode(true);
    }
}

void GestureMgr_NativeTerminate() {
#ifdef ENABLE_NCNN
    if (sInternalPose) {
        sInternalPose->Stop();
        delete sInternalPose;
        sInternalPose = nullptr;
    }
#endif
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
static int AssignSlots(const int *trackIds, int numValid, int *personForSlot) {
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

    // 2. Release slots whose owner is no longer present.
    bool releasedNow[NUM_SKELETONS];
    for (int s = 0; s < NUM_SKELETONS; s++) {
        releasedNow[s] = false;
        if (sSlotTrackId[s] >= 0 && personForSlot[s] < 0) {
            sSlotTrackId[s] = -1;
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
                reassignments++;
                break;
            }
        }
    }
    return reassignments;
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
    for (int pass = 0; pass < 2; pass++) {
        bool requireValid = (pass == 0);
        for (int p = 0; p < 2; p++) {
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
                if (playerData && playerData->GetSkeletonTrackingID() != skel.TrackingID())
                    TheGameData->AssignSkeleton(p, skel.TrackingID());
                break;
            }
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

// Lay this frame's persons into their persistent slots.
static int FillPersonSlots(const NativeSkeletonProvider::PersonData *persons, int numPersons) {
    int trackIds[NUM_SKELETONS], origIdx[NUM_SKELETONS], numValid = 0;
    for (int i = 0; i < numPersons && numValid < NUM_SKELETONS; i++) {
        if (persons[i].valid) {
            trackIds[numValid] = persons[i].trackId;
            origIdx[numValid] = i;
            numValid++;
        }
    }
    int personForSlot[NUM_SKELETONS];
    int reassigns = AssignSlots(trackIds, numValid, personForSlot);
    for (int s = 0; s < NUM_SKELETONS; s++) {
        int k = personForSlot[s];
        if (k >= 0) {
            NativeSkeletonProvider::FillSkeletonData(
                sFrame.mSkeletonDatas[s], persons[origIdx[k]], s
            );
            FinishTrackedSlot(s);
        }
    }
    return reassigns;
}

// A new frame when the provider produced one (it runs at camera rate, slower
// than the game loop: re-integrating the same generation would dilute the
// displacement lookback), null otherwise.  With no provider running, the static
// tracked dummy in slot 0 is a new frame every game frame.  Since move scoring
// is DEFAULT-ON (App.cpp's DC3_NATIVE_SCORING gate) that dummy is the
// default-run SCORING INPUT: it drives the whole pipeline (archive-before-poll
// -> FilterQueue::Poll -> MoveDir) every frame and yields a deterministic
// DetectFrac ~0 -- the correct "player standing still" signal.  Keep it TRACKED:
// untracked would break ShellInput::HasSkeleton()/SkeletonChooser::Poll and drop
// scoring to the errors=1.0 short-circuit.  A transient 0-person dropout from a
// running provider stays on the provider branch (every slot reads untracked)
// and never reaches the dummy.
static const SkeletonFrame *BuildSensorFrame() {
    bool providerRunning = false;
    bool newFrame = false;

#ifdef ENABLE_NCNN
    if (sInternalPose && sInternalPose->IsRunning()) {
        providerRunning = true;
        sInternalPose->Poll();

        static unsigned sNcnnLastGen = 0;
        static bool sNcnnHaveGen = false;
        unsigned gen = sInternalPose->Generation();
        newFrame = !sNcnnHaveGen || gen != sNcnnLastGen;
        sNcnnHaveGen = true;
        sNcnnLastGen = gen;

        if (newFrame) {
            BeginSensorFrame(AcceptedFrameElapsed());
            NativeSkeletonProvider::PersonData persons[NativeSkeletonProvider::kMaxPersons];
            int numPersons = 0;
            sInternalPose->FillPersonData(persons, NativeSkeletonProvider::kMaxPersons, numPersons);
            ScoringDebugTick(true, false, FillPersonSlots(persons, numPersons));
        } else {
            ScoringDebugTick(false, true, 0);
        }
    }
#endif

    if (!providerRunning && TheSkeletonProvider && TheSkeletonProvider->IsRunning()) {
        providerRunning = true;
        TheSkeletonProvider->Poll();

        static uint32_t sExtLastFrameId = 0;
        static bool sExtHaveFrame = false;
        uint32_t frameId = TheSkeletonProvider->FrameId();
        newFrame = !sExtHaveFrame || frameId != sExtLastFrameId;
        sExtHaveFrame = true;
        sExtLastFrameId = frameId;

        if (newFrame) {
            BeginSensorFrame(AcceptedFrameElapsed(TheSkeletonProvider->Timestamp()));
            static NativeSkeletonProvider::PersonData sPersons[NativeSkeletonProvider::kMaxPersons];
            int numPersons = TheSkeletonProvider->NumPersons();
            for (int i = 0; i < numPersons; i++)
                sPersons[i] = TheSkeletonProvider->GetPerson(i);
            ScoringDebugTick(true, false, FillPersonSlots(sPersons, numPersons));
        } else {
            ScoringDebugTick(false, true, 0);
        }
    }

    if (!providerRunning) {
        ResetSlotMap();
        BeginSensorFrame(AcceptedFrameElapsed());
        NativeSkeletonProvider::FillDummySkeletonData(sFrame.mSkeletonDatas[0]);
        FinishTrackedSlot(0);
        newFrame = true;
    }

    if (!newFrame)
        return nullptr;
    NativeAutoAssignPlayers();
    return &sFrame;
}

const SkeletonFrame *NativeSensorCameraInput::PollNewFrame() {
    if (!sSensorFeedEnabled)
        return nullptr;
    return BuildSensorFrame();
}

// Called each frame by GestureMgr::Poll().  The skeleton pipeline no longer runs
// here -- it is SkeletonUpdate's, driven from App's skeleton_post_update step.
void GestureMgr_NativePoll(GestureMgr *mgr) {
    // NATIVE POLICY (pre-existing, not image behaviour): seed the active
    // skeleton id so GetActiveSkeletonTrackingID() is valid and HamNavList::Poll
    // finds a skeleton; the image gets it from the Kinect shell flow.
    if (mgr->GetActiveSkeletonTrackingID() <= 0) {
        mgr->SetActiveSkeletonTrackingID(1);
    }
}

#endif // HX_NATIVE
