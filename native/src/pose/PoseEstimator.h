#pragma once
// ncnn-based pose estimation using YOLO-Pose (single-stage detection + keypoints)
//
// Single model does both person detection and 17 COCO keypoint estimation.
// Post-processing: NMS on detections, extract keypoints per person.
//
// Build with -DENABLE_NCNN=ON to enable.

#include <cstdint>
#include <vector>
#include <string>

struct PoseKeypoint {
    float x, y;        // normalized [0,1] image coordinates
    float confidence;   // 0.0 - 1.0
};

struct PoseDetection {
    float bbox[4];     // x1, y1, x2, y2 in normalized coords
    float bboxConf;    // detection confidence
    PoseKeypoint keypoints[17]; // COCO 17 keypoints
    int trackId;       // assigned by tracker (-1 if untracked)
};

#ifdef ENABLE_NCNN
#include <net.h>
#endif

class PoseEstimator {
public:
    PoseEstimator();
    ~PoseEstimator();

    // Load YOLO-Pose ncnn model from directory
    // Expects: <modelDir>/model.ncnn.param and model.ncnn.bin
    bool Init(const std::string &modelDir, bool useGPU = false);
    void Release();
    bool IsReady() const { return mReady; }

    // Run detection + pose estimation on an RGB image
    // pixels: RGB24 row-major, width x height
    void Detect(const uint8_t *pixels, int width, int height,
                std::vector<PoseDetection> &results);

    // Configuration
    void SetInputSize(int size) { mInputSize = size; }
    void SetConfThreshold(float t) { mConfThreshold = t; }
    void SetNmsThreshold(float t) { mNmsThreshold = t; }

private:
    bool mReady = false;
    int mInputSize = 320;
    float mConfThreshold = 0.5f;
    float mNmsThreshold = 0.45f;

#ifdef ENABLE_NCNN
    ncnn::Net mNet;
    void ParseYOLOPoseOutput(const ncnn::Mat &output, int imgW, int imgH,
                              std::vector<PoseDetection> &results);
#endif
};
