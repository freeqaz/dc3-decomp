#ifdef HX_NATIVE

#include "Skeleton_Native.h"
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h" // SkeletonCallback
#include "gesture/SkeletonHistory.h"
#include "gesture/SkeletonUpdate.h"
#include "obj/Task.h"
#include <vector>
#ifdef ENABLE_NCNN
#include "pose/InternalPoseProvider.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstring>

// Lightweight SkeletonHistory for native -- follows the MocapSkeletonIterator
// pattern. Inherits SkeletonHistoryArchive (ring buffer storage) and
// SkeletonHistory (PrevSkeleton lookup). Populated each frame in
// GestureMgr_NativePoll() to mirror Xbox's SkeletonUpdate::UpdateCallbacks().
class NativeSkeletonHistory : public SkeletonHistoryArchive, public SkeletonHistory {
public:
    bool PrevSkeleton(
        const Skeleton &s, int targetMs, ArchiveSkeleton &out, int &elapsedMs
    ) const override {
        bool found = PrevFromArchive(*this, s, targetMs, out, elapsedMs);
        // DC3_SCORING_DEBUG=1: once-per-second archive-lookup liveness counters,
        // so a crash-free run can be distinguished from silently-dead lookups.
        static bool sDebug = getenv("DC3_SCORING_DEBUG") != nullptr;
        if (sDebug) {
            static unsigned sCalls = 0, sHits = 0;
            static std::chrono::steady_clock::time_point sLastPrint;
            sCalls++;
            if (found)
                sHits++;
            std::chrono::steady_clock::time_point now =
                std::chrono::steady_clock::now();
            if (now - sLastPrint > std::chrono::seconds(1)) {
                sLastPrint = now;
                fprintf(stderr, "DC3 SCORING: PrevSkeleton calls=%u hits=%u\n",
                    sCalls, sHits);
            }
        }
        return found;
    }
};

static NativeSkeletonHistory *sNativeHistory = nullptr;

// Minimal CameraInput for native -- reports connected, no real frame data.
// Only used to satisfy SkeletonUpdateData::mCameraInput pointer.
class NativeCameraInput : public CameraInput {
public:
    const SkeletonFrame *PollNewFrame() override { return nullptr; }
};

static NativeCameraInput *sNativeCameraInput = nullptr;
#ifdef ENABLE_NCNN
static InternalPoseProvider *sInternalPose = nullptr;
#endif

// Native implementation of GestureMgr::Init — replaces the early return stub.
// Called from game startup to initialize skeleton tracking via webcam + YOLO pose.
void GestureMgr_NativeInit() {
    // Always create the camera input stub -- needed for PostUpdate pipeline.
    if (!sNativeCameraInput)
        sNativeCameraInput = new NativeCameraInput();

    // Create skeleton history so displacement-based scoring works on native.
    // This replaces SkeletonUpdate's history (which requires Xbox NUI hardware).
    if (!sNativeHistory) {
        sNativeHistory = new NativeSkeletonHistory();
        SkeletonUpdate::SetNativeHistoryFallback(sNativeHistory);
    }

    // In headless mode (tests, CLI tools), skip the pose server entirely.
    // The dummy skeleton in GestureMgr_NativePoll provides a neutral standing
    // pose so skeleton-gated paths still work without a real camera.
    if (getenv("MILO_HEADLESS")) {
        printf("Native: headless mode, using dummy skeleton (no pose server)\n");
        if (TheGestureMgr) {
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

            const char *modelPath = getenv("DC3_POSE_MODEL");
            if (!modelPath) modelPath = "yolo11n-pose.pt";

            if (TheSkeletonProvider->Start(socketPath, modelPath, camIdx)) {
                printf("Native: external pose server started\n");
            } else {
                printf("Native: pose tracking unavailable (no ncnn, no pose server)\n");
            }
        }
    }

pose_ready:

    if (TheGestureMgr) {
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
    SkeletonUpdate::SetNativeHistoryFallback(nullptr);
    delete sNativeHistory;
    sNativeHistory = nullptr;
    delete sNativeCameraInput;
    sNativeCameraInput = nullptr;
}

// Called each frame by GestureMgr::Poll() to update skeleton slots
// from the YOLO pose server (or a dummy skeleton), then run the
// filtering pipeline.
void GestureMgr_NativePoll(GestureMgr *mgr) {
    // Archive the PREVIOUS frame's finalized pose before it is overwritten
    // this frame, matching Xbox SkeletonUpdate::UpdateCallbacks archive-then-poll
    // ordering. On the first frame every slot is untracked (ctor) -> ClearHistory,
    // so nothing garbage is archived.
    if (sNativeHistory) {
        for (int i = 0; i < NUM_SKELETONS; i++) {
            Skeleton &skel = mgr->GetSkeleton(i);
            if (skel.IsTracked()) {
                sNativeHistory->AddToHistory(i, skel);
            } else {
                sNativeHistory->ClearHistory(i);
            }
        }
    }

    // Real per-frame elapsed delta (Xbox gets this from NUI_SKELETON_FRAME).
    // Displacement scoring integrates these, so a garbage value poisons it.
    static std::chrono::steady_clock::time_point sPrevTime;
    static bool sHavePrevTime = false;
    int elapsedMs;
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (sHavePrevTime) {
            long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - sPrevTime)
                          .count();
            if (ms < 1) ms = 1;
            if (ms > 200) ms = 200;
            elapsedMs = (int)ms;
        } else {
            elapsedMs = 33;
            sHavePrevTime = true;
        }
        sPrevTime = now;
    }

    bool providerRunning = false;

#ifdef ENABLE_NCNN
    // Try internal pose pipeline first
    if (sInternalPose && sInternalPose->IsRunning()) {
        providerRunning = true;
        sInternalPose->Poll();

        NativeSkeletonProvider::PersonData persons[NativeSkeletonProvider::kMaxPersons];
        int numPersons = 0;
        sInternalPose->FillPersonData(persons, NativeSkeletonProvider::kMaxPersons, numPersons);

        // Use a temporary NativeSkeletonProvider to access FillSkeleton
        // (which has friend access to Skeleton's protected members)
        static NativeSkeletonProvider sFillHelper;
        for (int i = 0; i < NUM_SKELETONS; i++) {
            Skeleton &skel = mgr->GetSkeleton(i);
            if (i < numPersons && persons[i].valid) {
                sFillHelper.FillSkeleton(skel, persons[i]);
                NativeSkeletonProvider::FinalizeSkeletonFrame(skel, i, elapsedMs);
            } else if (skel.IsTracked()) {
                NativeSkeletonProvider::MarkUntracked(skel);
            }
        }
    }
#endif

    // Fall back to external pose server
    if (!providerRunning && TheSkeletonProvider && TheSkeletonProvider->IsRunning()) {
        providerRunning = true;
        TheSkeletonProvider->Poll();
        int numPersons = TheSkeletonProvider->NumPersons();
        for (int i = 0; i < NUM_SKELETONS; i++) {
            Skeleton &skel = mgr->GetSkeleton(i);
            if (i < numPersons && TheSkeletonProvider->GetPerson(i).valid) {
                TheSkeletonProvider->FillSkeleton(skel, i);
                NativeSkeletonProvider::FinalizeSkeletonFrame(skel, i, elapsedMs);
            } else if (skel.IsTracked()) {
                NativeSkeletonProvider::MarkUntracked(skel);
            }
        }
    }

    if (!providerRunning) {
        // No pose provider running at all — provide a dummy skeleton in slot 0
        // so skeleton-gated code paths (scroll behavior, enter anims) still run.
        // Remaining slots stay untracked. A transient 0-person dropout from a
        // running provider intentionally does NOT reach here (see MarkUntracked
        // above) so slot 0 history is never poisoned with dummy poses.
        Skeleton &slot0 = mgr->GetSkeleton(0);
        NativeSkeletonProvider::FillDummySkeleton(slot0);
        NativeSkeletonProvider::FinalizeSkeletonFrame(slot0, 0, elapsedMs);
        for (int i = 1; i < NUM_SKELETONS; i++) {
            Skeleton &skel = mgr->GetSkeleton(i);
            if (skel.IsTracked()) {
                NativeSkeletonProvider::MarkUntracked(skel);
            }
        }
    }

    // Set active skeleton so GetActiveSkeletonTrackingID() returns a
    // valid ID and HamNavList::Poll() finds our skeleton.
    if (mgr->GetActiveSkeletonTrackingID() <= 0) {
        mgr->SetActiveSkeletonTrackingID(1);
    }

    // Run the quality filter + identity tracking pipeline.
    // On Xbox this is done by SkeletonUpdate's thread; on native we
    // do it synchronously here.
    Skeleton *skelPtrs[NUM_SKELETONS];
    for (int i = 0; i < NUM_SKELETONS; i++) {
        skelPtrs[i] = &mgr->GetSkeleton(i);
    }

    SkeletonUpdateData data;
    data.mSkeletonsLeft = skelPtrs;
    data.mSkeletonsRight = skelPtrs;
    data.mFrame = nullptr;
    data.mHistory = sNativeHistory;
    data.mCameraInput = sNativeCameraInput;

    mgr->PostUpdate(&data);

    // Drive the SkeletonUpdate scoring callbacks (MoveDir/Game/HamVisDir) that
    // Xbox runs from SkeletonUpdate::UpdateCallbacks/PostUpdate. Update() must
    // run before PostUpdate() (PostUpdateFilters reads the FilterQueue::Poll
    // output produced by Update). mgr->PostUpdate above (GestureMgr identity
    // tracking) is orthogonal and intentionally kept.
    std::vector<SkeletonCallback *> cbs =
        SkeletonUpdate::NativeCallbacks(); // copy: callbacks may register/unregister
    for (size_t i = 0; i < cbs.size(); i++)
        cbs[i]->Update(data);
    for (size_t i = 0; i < cbs.size(); i++)
        cbs[i]->PostUpdate(&data);
}

#endif // HX_NATIVE
