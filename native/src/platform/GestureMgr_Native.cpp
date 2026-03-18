#ifdef HX_NATIVE

#include "Skeleton_Native.h"
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h"
#ifdef ENABLE_NCNN
#include "pose/InternalPoseProvider.h"
#endif
#include <cstdio>
#include <cstring>

// Minimal CameraInput for native — reports connected, no real frame data.
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
    // Always create the camera input stub — needed for PostUpdate pipeline.
    if (!sNativeCameraInput)
        sNativeCameraInput = new NativeCameraInput();

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
    delete sNativeCameraInput;
    sNativeCameraInput = nullptr;
}

// Called each frame by GestureMgr::Poll() to update skeleton slots
// from the YOLO pose server (or a dummy skeleton), then run the
// filtering pipeline.
void GestureMgr_NativePoll(GestureMgr *mgr) {
    bool hasInput = false;

#ifdef ENABLE_NCNN
    // Try internal pose pipeline first
    if (sInternalPose && sInternalPose->IsRunning()) {
        sInternalPose->Poll();

        NativeSkeletonProvider::PersonData persons[NativeSkeletonProvider::kMaxPersons];
        int numPersons = 0;
        sInternalPose->FillPersonData(persons, NativeSkeletonProvider::kMaxPersons, numPersons);

        for (int i = 0; i < NUM_SKELETONS; i++) {
            Skeleton &skel = mgr->GetSkeleton(i);
            if (i < numPersons && persons[i].valid) {
                // Fill skeleton from internal pipeline results
                for (int j = 0; j < kNumJoints; j++) {
                    skel.mTrackedJoints[j].mJointPos[kCoordCamera] = persons[i].joints[j];
                    skel.mTrackedJoints[j].mSmoothedPos = persons[i].joints[j];
                    skel.mTrackedJoints[j].mJointConf = persons[i].confidence[j];
                }
                skel.mTracking = kSkeletonTracked;
                skel.mTrackingID = persons[i].trackId;
            }
        }
        hasInput = (numPersons > 0);
    }
#endif

    // Fall back to external pose server
    if (!hasInput && TheSkeletonProvider && TheSkeletonProvider->IsRunning()) {
        TheSkeletonProvider->Poll();
        int numPersons = TheSkeletonProvider->NumPersons();
        for (int i = 0; i < NUM_SKELETONS; i++) {
            if (i < numPersons) {
                TheSkeletonProvider->FillSkeleton(mgr->GetSkeleton(i), i);
            }
        }
        hasInput = (numPersons > 0);
    }

    if (!hasInput) {
        // No pose server — provide a dummy skeleton so skeleton-gated
        // code paths (scroll behavior, enter anims) still run.
        NativeSkeletonProvider::FillDummySkeleton(mgr->GetSkeleton(0));
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
    data.mHistory = nullptr;
    data.mCameraInput = sNativeCameraInput;

    mgr->PostUpdate(&data);
}

#endif // HX_NATIVE
