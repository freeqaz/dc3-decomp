#pragma once
// ncnn-based pose estimation: YOLO detector + RTMPose top-down
//
// Two-stage pipeline:
//   1. YOLO-n detector finds person bounding boxes
//   2. RTMPose-t estimates 17 COCO keypoints per person
//
// Build with -DENABLE_NCNN=ON to enable. Falls back to external pose server
// (DC3_POSE=external) or dummy skeleton when ncnn is not available.

#include <vector>
#include <string>

struct PoseKeypoint {
    float x, y;       // normalized [0,1] image coordinates
    float confidence;  // 0.0 - 1.0
};

struct PoseDetection {
    float bbox[4];     // x1, y1, x2, y2 in normalized coords
    float bboxConf;    // detection confidence
    PoseKeypoint keypoints[17]; // COCO 17 keypoints
    int trackId;       // assigned by tracker (-1 if untracked)
};

#ifdef ENABLE_NCNN
#include <ncnn/net.h>
#endif

class PoseEstimator {
public:
    PoseEstimator();
    ~PoseEstimator();

    // Load detector + pose models from directory
    // Expects: <modelDir>/yolo-det.param, yolo-det.bin, rtmpose.param, rtmpose.bin
    bool Init(const std::string &modelDir, bool useGPU = false);
    void Release();
    bool IsReady() const { return mReady; }

    // Run detection + pose estimation on an RGB image
    // pixels: RGB24 row-major, width x height
    // Returns detected persons with keypoints
    void Detect(const uint8_t *pixels, int width, int height,
                std::vector<PoseDetection> &results);

private:
    bool mReady = false;

#ifdef ENABLE_NCNN
    // Stage 1: person detector
    ncnn::Net mDetectorNet;
    int mDetInputSize = 320;  // YOLO input resolution

    // Stage 2: pose estimator (top-down, runs per-person crop)
    ncnn::Net mPoseNet;
    int mPoseInputW = 192;
    int mPoseInputH = 256;

    void RunDetector(const uint8_t *pixels, int width, int height,
                     std::vector<PoseDetection> &detections);
    void RunPoseModel(const uint8_t *pixels, int width, int height,
                      const float bbox[4], PoseKeypoint *outKeypoints);
#endif
};
