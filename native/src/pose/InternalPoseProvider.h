#pragma once
// Internal pose estimation pipeline — replaces external Python pose server
//
// Pipeline: CameraCapture → PoseEstimator (ncnn) → PoseTracker → PersonData
//
// Runs on a background thread, same double-buffer pattern as the external
// socket-based NativeSkeletonProvider.
//
// Enable: DC3_POSE=internal (default when ncnn available)
// Disable: DC3_POSE=external (use legacy Python pose server)
// Model dir: DC3_POSE_MODELS (default: native/models/)

#include "pose/PoseEstimator.h"
#include "pose/PoseTracker.h"
#include "pose/CameraCapture.h"
#include "platform/Skeleton_Native.h"
#include <thread>
#include <mutex>
#include <string>

class InternalPoseProvider {
public:
    InternalPoseProvider();
    ~InternalPoseProvider();

    // Initialize the pipeline. Returns false if models or camera unavailable.
    bool Start(const std::string &modelDir = "native/models",
               int cameraIndex = 0, bool useGPU = false);
    void Stop();
    bool IsRunning() const { return mRunning; }

    // Call each frame from main thread — swaps latest detections
    void Poll();

    // Access results (after Poll)
    int NumPersons() const { return mNumPersons; }

    // Worker-frame generation snapshotted by the last Poll(). The worker runs at
    // camera rate (slower than the game loop); scoring gates archive/fill on this
    // changing so a single camera frame is not integrated multiple times.
    unsigned Generation() const { return mGenerationFront; }

    // Fill NativeSkeletonProvider::PersonData from our detections
    void FillPersonData(NativeSkeletonProvider::PersonData *outPersons,
                        int maxPersons, int &outCount) const;

private:
    void WorkerThread();

    PoseEstimator mEstimator;
    PoseTracker mTracker;
    CameraCapture mCamera;

    std::thread mWorkerThread;
    volatile bool mRunning = false;

    // Double-buffered results
    std::mutex mSwapMutex;
    std::vector<PoseDetection> mFrontDetections;
    std::vector<PoseDetection> mBackDetections;
    int mNumPersons = 0;

    // Worker bumps mGenerationBack per produced frame; Poll() latches Front.
    unsigned mGenerationBack = 0;
    unsigned mGenerationFront = 0;

    // COCO-to-DC3 mapping reuse (same as NativeSkeletonProvider)
    void MapDetectionToPersonData(const PoseDetection &det,
                                  NativeSkeletonProvider::PersonData &out) const;

    // Coordinate mapping
    // See NativeSkeletonProvider (Skeleton_Native.h) for the derivation: the
    // Kinect projection u = 160 + 285.63*x/z over 320x240, recovered from the
    // target disassembly, gives a 3.361 x 2.521 m extent at z = 3.0. The old
    // 2.4 x 1.8 under-scaled x/y by ~1.40x against this struct's own z.
    float mViewWidth = 3.361f;
    float mViewHeight = 2.521f;
    float mViewDepth = 3.0f;
};
