// Internal pose estimation pipeline
// Replaces external Python pose_server.py with embedded ncnn inference

#include "pose/InternalPoseProvider.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

InternalPoseProvider::InternalPoseProvider() {}

InternalPoseProvider::~InternalPoseProvider() {
    Stop();
}

bool InternalPoseProvider::Start(const std::string &modelDir,
                                 int cameraIndex, bool useGPU) {
    if (mRunning) return true;

    // Initialize pose estimator (load ncnn models)
    if (!mEstimator.Init(modelDir, useGPU)) {
        fprintf(stderr, "InternalPoseProvider: failed to load models from %s\n",
                modelDir.c_str());
        return false;
    }

    // Open camera
    if (!mCamera.Open(cameraIndex)) {
        fprintf(stderr, "InternalPoseProvider: failed to open camera %d\n", cameraIndex);
        mEstimator.Release();
        return false;
    }

    printf("InternalPoseProvider: started (camera %d, %dx%d, GPU=%d)\n",
           cameraIndex, mCamera.Width(), mCamera.Height(), useGPU);

    mRunning = true;
    mWorkerThread = std::thread(&InternalPoseProvider::WorkerThread, this);
    return true;
}

void InternalPoseProvider::Stop() {
    mRunning = false;
    if (mWorkerThread.joinable()) {
        mWorkerThread.join();
    }
    mCamera.Close();
    mEstimator.Release();
    mTracker.Reset();
}

void InternalPoseProvider::WorkerThread() {
    printf("InternalPoseProvider: worker thread started\n");

    while (mRunning) {
        // Capture frame
        const uint8_t *frame = mCamera.CaptureFrame();
        if (!frame) {
            // No frame ready, brief sleep to avoid busy-wait
            struct timespec ts = {0, 5000000}; // 5ms
            nanosleep(&ts, nullptr);
            continue;
        }

        // Run detection + pose estimation
        std::vector<PoseDetection> detections;
        mEstimator.Detect(frame, mCamera.Width(), mCamera.Height(), detections);

        // Update tracker (assigns persistent IDs)
        mTracker.Update(detections);

        // Swap to back buffer
        {
            std::lock_guard<std::mutex> lock(mSwapMutex);
            mBackDetections = std::move(detections);
            mGenerationBack++;
        }
    }

    printf("InternalPoseProvider: worker thread stopped\n");
}

void InternalPoseProvider::Poll() {
    std::lock_guard<std::mutex> lock(mSwapMutex);
    mFrontDetections = mBackDetections;
    mNumPersons = (int)mFrontDetections.size();
    mGenerationFront = mGenerationBack;
}

void InternalPoseProvider::FillPersonData(
    NativeSkeletonProvider::PersonData *outPersons,
    int maxPersons, int &outCount) const
{
    outCount = 0;
    for (int i = 0; i < (int)mFrontDetections.size() && i < maxPersons; i++) {
        MapDetectionToPersonData(mFrontDetections[i], outPersons[i]);
        outCount++;
    }
    // Clear remaining slots
    for (int i = outCount; i < maxPersons; i++) {
        outPersons[i] = {};
    }
}

void InternalPoseProvider::MapDetectionToPersonData(
    const PoseDetection &det,
    NativeSkeletonProvider::PersonData &out) const
{
    // Reuse the same COCO-to-DC3 joint mapping as NativeSkeletonProvider
    // Convert PoseKeypoint array to float[][3] format for compatibility
    float cocoKpts[17][3];
    for (int i = 0; i < 17; i++) {
        cocoKpts[i][0] = det.keypoints[i].x;
        cocoKpts[i][1] = det.keypoints[i].y;
        cocoKpts[i][2] = det.keypoints[i].confidence;
    }

    // Helper lambdas (same as NativeSkeletonProvider::MapCOCOToDC3)
    auto normalizedToMeters = [&](float nx, float ny) -> Vector3 {
        // X flips like Y — see NativeSkeletonProvider::NormalizedToMeters for
        // the DC3 camera-space convention (player-left is -X).
        float x = (0.5f - nx) * mViewWidth;
        float y = (0.5f - ny) * mViewHeight;
        float z = mViewDepth;
        return Vector3(x, y, z);
    };
    auto kpt = [&](int idx) -> Vector3 {
        return normalizedToMeters(cocoKpts[idx][0], cocoKpts[idx][1]);
    };
    auto kptConf = [&](int idx) -> JointConfidence {
        float c = cocoKpts[idx][2];
        if (c < 0.3f) return kConfidenceNotTracked;
        if (c < 0.6f) return kConfidenceInferred;
        return kConfidenceTracked;
    };
    auto midpoint = [](const Vector3 &a, const Vector3 &b) -> Vector3 {
        return Vector3((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
    };
    auto minConf = [](JointConfidence a, JointConfidence b) -> JointConfidence {
        return (a < b) ? a : b;
    };
    // See NativeSkeletonProvider::MapCOCOToDC3 for why hand/foot are extrapolated
    // rather than aliased onto wrist/ankle.
    auto extrapolate = [](const Vector3 &parent, const Vector3 &tip,
                           float frac) -> Vector3 {
        Vector3 dir;
        Subtract(tip, parent, dir);
        return Vector3(tip.x + dir.x * frac, tip.y + dir.y * frac, tip.z + dir.z * frac);
    };

    // COCO keypoint indices
    enum {
        NOSE=0, L_EYE=1, R_EYE=2, L_EAR=3, R_EAR=4,
        L_SHOULDER=5, R_SHOULDER=6, L_ELBOW=7, R_ELBOW=8,
        L_WRIST=9, R_WRIST=10, L_HIP=11, R_HIP=12,
        L_KNEE=13, R_KNEE=14, L_ANKLE=15, R_ANKLE=16
    };

    // Direct mappings. Head comes from the ear midpoint (closer to Kinect's
    // skull-centre kJointHead than the nose), falling back to the nose.
    JointConfidence earConf = minConf(kptConf(L_EAR), kptConf(R_EAR));
    if (earConf > kConfidenceNotTracked) {
        out.joints[kJointHead] = midpoint(kpt(L_EAR), kpt(R_EAR));
        out.confidence[kJointHead] = earConf;
    } else {
        out.joints[kJointHead] = kpt(NOSE);
        out.confidence[kJointHead] = kptConf(NOSE);
    }
    out.joints[kJointShoulderLeft] = kpt(L_SHOULDER);
    out.confidence[kJointShoulderLeft] = kptConf(L_SHOULDER);
    out.joints[kJointElbowLeft] = kpt(L_ELBOW);
    out.confidence[kJointElbowLeft] = kptConf(L_ELBOW);
    out.joints[kJointWristLeft] = kpt(L_WRIST);
    out.confidence[kJointWristLeft] = kptConf(L_WRIST);
    out.joints[kJointHandLeft] = extrapolate(kpt(L_ELBOW), kpt(L_WRIST), 0.28f);
    out.confidence[kJointHandLeft] = minConf(kptConf(L_WRIST), kptConf(L_ELBOW));
    out.joints[kJointShoulderRight] = kpt(R_SHOULDER);
    out.confidence[kJointShoulderRight] = kptConf(R_SHOULDER);
    out.joints[kJointElbowRight] = kpt(R_ELBOW);
    out.confidence[kJointElbowRight] = kptConf(R_ELBOW);
    out.joints[kJointWristRight] = kpt(R_WRIST);
    out.confidence[kJointWristRight] = kptConf(R_WRIST);
    out.joints[kJointHandRight] = extrapolate(kpt(R_ELBOW), kpt(R_WRIST), 0.28f);
    out.confidence[kJointHandRight] = minConf(kptConf(R_WRIST), kptConf(R_ELBOW));
    out.joints[kJointHipLeft] = kpt(L_HIP);
    out.confidence[kJointHipLeft] = kptConf(L_HIP);
    out.joints[kJointKneeLeft] = kpt(L_KNEE);
    out.confidence[kJointKneeLeft] = kptConf(L_KNEE);
    out.joints[kJointAnkleLeft] = kpt(L_ANKLE);
    out.confidence[kJointAnkleLeft] = kptConf(L_ANKLE);
    out.joints[kJointHipRight] = kpt(R_HIP);
    out.confidence[kJointHipRight] = kptConf(R_HIP);
    out.joints[kJointKneeRight] = kpt(R_KNEE);
    out.confidence[kJointKneeRight] = kptConf(R_KNEE);
    out.joints[kJointAnkleRight] = kpt(R_ANKLE);
    out.confidence[kJointAnkleRight] = kptConf(R_ANKLE);
    out.joints[kJointFootLeft] = extrapolate(kpt(L_KNEE), kpt(L_ANKLE), 0.25f);
    out.confidence[kJointFootLeft] = minConf(kptConf(L_ANKLE), kptConf(L_KNEE));
    out.joints[kJointFootRight] = extrapolate(kpt(R_KNEE), kpt(R_ANKLE), 0.25f);
    out.confidence[kJointFootRight] = minConf(kptConf(R_ANKLE), kptConf(R_KNEE));

    // Synthesized joints
    Vector3 hipCenter = midpoint(kpt(L_HIP), kpt(R_HIP));
    Vector3 shoulderCenter = midpoint(kpt(L_SHOULDER), kpt(R_SHOULDER));
    out.joints[kJointHipCenter] = hipCenter;
    out.confidence[kJointHipCenter] = minConf(kptConf(L_HIP), kptConf(R_HIP));
    out.joints[kJointShoulderCenter] = shoulderCenter;
    out.confidence[kJointShoulderCenter] = minConf(kptConf(L_SHOULDER), kptConf(R_SHOULDER));
    out.joints[kJointSpine] = midpoint(hipCenter, shoulderCenter);
    out.confidence[kJointSpine] = minConf(out.confidence[kJointHipCenter],
                                           out.confidence[kJointShoulderCenter]);

    out.trackId = det.trackId;
    out.valid = true;
}
