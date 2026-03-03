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
    if (TheSkeletonProvider) return;
    TheSkeletonProvider = new NativeSkeletonProvider();

    // TODO: make these configurable via DataArray or env vars
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

    sNativeCameraInput = new NativeCameraInput();

    // Force controller mode so gesture-gated screens (tutorial, skeleton chooser)
    // don't block waiting for Kinect hand-raise gestures.
    if (TheGestureMgr) {
        TheGestureMgr->SetInControllerMode(true);
        printf("Native: forced controller mode (bypasses Kinect gesture gates)\n");
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
// from the YOLO pose server, then run the filtering pipeline.
void GestureMgr_NativePoll(GestureMgr *mgr) {
    if (!TheSkeletonProvider || !TheSkeletonProvider->IsRunning())
        return;

    TheSkeletonProvider->Poll();

    int numPersons = TheSkeletonProvider->NumPersons();

    // Fill up to 6 skeleton slots (matching Kinect's 6-skeleton max)
    for (int i = 0; i < NUM_SKELETONS; i++) {
        Skeleton &skel = mgr->GetSkeleton(i);
        if (i < numPersons) {
            TheSkeletonProvider->FillSkeleton(skel, i);
        }
    }

    // Build SkeletonUpdateData pointing to mgr's own skeleton slots
    // so PostUpdate() runs the quality filters and identity tracking.
    // On Xbox, SkeletonUpdate::PostUpdate builds this and calls callbacks.
    // On native, we build it here since there's no SkeletonUpdate thread.
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
