#ifdef HX_NATIVE

#include "Skeleton_Native.h"
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h"
#include <cstdio>

// Minimal CameraInput for native — reports connected, no real frame data.
// Only used to satisfy SkeletonUpdateData::mCameraInput pointer.
class NativeCameraInput : public CameraInput {
public:
    const SkeletonFrame *PollNewFrame() override { return nullptr; }
};

static NativeCameraInput *sNativeCameraInput = nullptr;

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

    if (TheSkeletonProvider) return;
    TheSkeletonProvider = new NativeSkeletonProvider();

    const char *socketPath = getenv("DC3_POSE_SOCKET");
    if (!socketPath) socketPath = "/tmp/dc3_pose.sock";

    const char *modelPath = getenv("DC3_POSE_MODEL");
    if (!modelPath) modelPath = "yolo11n-pose.pt";

    const char *camStr = getenv("DC3_POSE_CAMERA");
    int camIdx = camStr ? atoi(camStr) : 0;

    if (TheSkeletonProvider->Start(socketPath, modelPath, camIdx)) {
        printf("Native skeleton tracking started\n");
    } else {
        printf("Native skeleton tracking failed to start (gameplay will have no body input)\n");
    }

    if (TheGestureMgr) {
        TheGestureMgr->SetInControllerMode(true);
    }
}

void GestureMgr_NativeTerminate() {
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
    bool hasPoseServer = TheSkeletonProvider && TheSkeletonProvider->IsRunning();

    if (hasPoseServer) {
        TheSkeletonProvider->Poll();
        int numPersons = TheSkeletonProvider->NumPersons();
        for (int i = 0; i < NUM_SKELETONS; i++) {
            if (i < numPersons) {
                TheSkeletonProvider->FillSkeleton(mgr->GetSkeleton(i), i);
            }
        }
    } else {
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
