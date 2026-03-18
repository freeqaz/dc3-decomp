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

    // COCO-to-DC3 mapping reuse (same as NativeSkeletonProvider)
    void MapDetectionToPersonData(const PoseDetection &det,
                                  NativeSkeletonProvider::PersonData &out) const;

    // Coordinate mapping
    float mViewWidth = 2.4f;
    float mViewHeight = 1.8f;
    float mViewDepth = 3.0f;
};
