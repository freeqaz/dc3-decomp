// ncnn-based two-stage pose estimation
// Stage 1: YOLO-n detector (person bounding boxes)
// Stage 2: RTMPose-t (17 COCO keypoints per person)

#include "pose/PoseEstimator.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

PoseEstimator::PoseEstimator() {}
PoseEstimator::~PoseEstimator() { Release(); }

#ifdef ENABLE_NCNN

bool PoseEstimator::Init(const std::string &modelDir, bool useGPU) {
    if (mReady) return true;

    // Configure ncnn
    mDetectorNet.opt.use_vulkan_compute = useGPU;
    mDetectorNet.opt.num_threads = 2;
    mPoseNet.opt.use_vulkan_compute = useGPU;
    mPoseNet.opt.num_threads = 2;

    // Load detector model
    std::string detParam = modelDir + "/yolo-det.param";
    std::string detBin = modelDir + "/yolo-det.bin";
    if (mDetectorNet.load_param(detParam.c_str()) != 0 ||
        mDetectorNet.load_model(detBin.c_str()) != 0) {
        fprintf(stderr, "PoseEstimator: failed to load detector from %s\n", modelDir.c_str());
        return false;
    }

    // Load pose model
    std::string poseParam = modelDir + "/rtmpose.param";
    std::string poseBin = modelDir + "/rtmpose.bin";
    if (mPoseNet.load_param(poseParam.c_str()) != 0 ||
        mPoseNet.load_model(poseBin.c_str()) != 0) {
        fprintf(stderr, "PoseEstimator: failed to load pose model from %s\n", modelDir.c_str());
        return false;
    }

    mReady = true;
    printf("PoseEstimator: loaded detector + pose model from %s (GPU=%d)\n",
           modelDir.c_str(), useGPU);
    return true;
}

void PoseEstimator::Release() {
    mDetectorNet.clear();
    mPoseNet.clear();
    mReady = false;
}

void PoseEstimator::Detect(const uint8_t *pixels, int width, int height,
                           std::vector<PoseDetection> &results) {
    results.clear();
    if (!mReady) return;

    // Stage 1: detect persons
    RunDetector(pixels, width, height, results);

    // Stage 2: estimate pose for each detected person
    for (auto &det : results) {
        RunPoseModel(pixels, width, height, det.bbox, det.keypoints);
        det.trackId = -1; // tracker assigns IDs later
    }
}

void PoseEstimator::RunDetector(const uint8_t *pixels, int width, int height,
                                std::vector<PoseDetection> &detections) {
    // Preprocess: resize to detector input size, normalize to [0,1]
    ncnn::Mat input = ncnn::Mat::from_pixels_resize(
        pixels, ncnn::Mat::PIXEL_RGB,
        width, height, mDetInputSize, mDetInputSize);

    const float normVals[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    input.substract_mean_normalize(nullptr, normVals);

    ncnn::Extractor ex = mDetectorNet.create_extractor();
    ex.input("images", input);

    ncnn::Mat output;
    ex.extract("output0", output);

    // Parse YOLO output: each row is [x_center, y_center, w, h, conf, class0_conf, ...]
    // Filter for person class (class 0) with confidence > 0.5
    const float confThreshold = 0.5f;
    const float nmsThreshold = 0.45f;

    struct RawBox {
        float x1, y1, x2, y2, conf;
    };
    std::vector<RawBox> rawBoxes;

    for (int i = 0; i < output.h; i++) {
        const float *row = output.row(i);
        // YOLO output format varies by export; handle common format
        // [x_center, y_center, w, h, obj_conf, class0_conf...]
        float cx = row[0] / mDetInputSize;
        float cy = row[1] / mDetInputSize;
        float w = row[2] / mDetInputSize;
        float h = row[3] / mDetInputSize;
        float conf = row[4]; // objectness or class conf

        if (conf < confThreshold) continue;

        RawBox box;
        box.x1 = cx - w * 0.5f;
        box.y1 = cy - h * 0.5f;
        box.x2 = cx + w * 0.5f;
        box.y2 = cy + h * 0.5f;
        box.conf = conf;
        rawBoxes.push_back(box);
    }

    // Simple NMS
    std::sort(rawBoxes.begin(), rawBoxes.end(),
              [](const RawBox &a, const RawBox &b) { return a.conf > b.conf; });

    auto iou = [](const RawBox &a, const RawBox &b) -> float {
        float ix1 = std::max(a.x1, b.x1), iy1 = std::max(a.y1, b.y1);
        float ix2 = std::min(a.x2, b.x2), iy2 = std::min(a.y2, b.y2);
        float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);
        float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
        float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
        return inter / (areaA + areaB - inter + 1e-6f);
    };

    std::vector<bool> suppressed(rawBoxes.size(), false);
    for (size_t i = 0; i < rawBoxes.size(); i++) {
        if (suppressed[i]) continue;
        PoseDetection det = {};
        det.bbox[0] = rawBoxes[i].x1;
        det.bbox[1] = rawBoxes[i].y1;
        det.bbox[2] = rawBoxes[i].x2;
        det.bbox[3] = rawBoxes[i].y2;
        det.bboxConf = rawBoxes[i].conf;
        detections.push_back(det);

        for (size_t j = i + 1; j < rawBoxes.size(); j++) {
            if (!suppressed[j] && iou(rawBoxes[i], rawBoxes[j]) > nmsThreshold)
                suppressed[j] = true;
        }

        if (detections.size() >= 6) break; // max 6 persons
    }
}

void PoseEstimator::RunPoseModel(const uint8_t *pixels, int width, int height,
                                  const float bbox[4], PoseKeypoint *outKeypoints) {
    // Crop the person region with some padding
    float padRatio = 0.25f;
    float bw = bbox[2] - bbox[0];
    float bh = bbox[3] - bbox[1];
    float cx = (bbox[0] + bbox[2]) * 0.5f;
    float cy = (bbox[1] + bbox[3]) * 0.5f;
    float cropW = bw * (1.0f + padRatio);
    float cropH = bh * (1.0f + padRatio);

    // Ensure aspect ratio matches model input (192x256 = 0.75)
    float targetAspect = (float)mPoseInputW / mPoseInputH;
    float cropAspect = cropW / (cropH + 1e-6f);
    if (cropAspect > targetAspect) {
        cropH = cropW / targetAspect;
    } else {
        cropW = cropH * targetAspect;
    }

    // Pixel coordinates of crop region
    int cx_px = (int)(cx * width);
    int cy_px = (int)(cy * height);
    int cw_px = (int)(cropW * width);
    int ch_px = (int)(cropH * height);
    int x1 = std::max(0, cx_px - cw_px / 2);
    int y1 = std::max(0, cy_px - ch_px / 2);
    int x2 = std::min(width, x1 + cw_px);
    int y2 = std::min(height, y1 + ch_px);

    // Extract crop from source image
    int cropPixW = x2 - x1;
    int cropPixH = y2 - y1;
    if (cropPixW <= 0 || cropPixH <= 0) return;

    // Create ncnn Mat from crop region (need to copy rows since source is strided)
    std::vector<uint8_t> cropBuf(cropPixW * cropPixH * 3);
    for (int row = 0; row < cropPixH; row++) {
        memcpy(&cropBuf[row * cropPixW * 3],
               &pixels[((y1 + row) * width + x1) * 3],
               cropPixW * 3);
    }

    ncnn::Mat input = ncnn::Mat::from_pixels_resize(
        cropBuf.data(), ncnn::Mat::PIXEL_RGB,
        cropPixW, cropPixH, mPoseInputW, mPoseInputH);

    // Normalize for RTMPose (ImageNet mean/std)
    const float mean[3] = {123.675f, 116.28f, 103.53f};
    const float std[3] = {1.0f / 58.395f, 1.0f / 57.12f, 1.0f / 57.375f};
    input.substract_mean_normalize(mean, std);

    ncnn::Extractor ex = mPoseNet.create_extractor();
    ex.input("input", input);

    ncnn::Mat heatmaps;
    ex.extract("output", heatmaps);

    // Parse heatmaps: 17 channels, each is a spatial heatmap
    // Find peak location in each channel
    int hmW = heatmaps.w;
    int hmH = heatmaps.h;

    for (int k = 0; k < 17 && k < heatmaps.c; k++) {
        const float *hm = heatmaps.channel(k);
        float maxVal = -1e10f;
        int maxX = 0, maxY = 0;

        for (int y = 0; y < hmH; y++) {
            for (int x = 0; x < hmW; x++) {
                float v = hm[y * hmW + x];
                if (v > maxVal) {
                    maxVal = v;
                    maxX = x;
                    maxY = y;
                }
            }
        }

        // Map heatmap coords back to normalized image coords
        float hx = (float)maxX / hmW; // position within crop [0,1]
        float hy = (float)maxY / hmH;

        // Map from crop coords to full image normalized coords
        outKeypoints[k].x = ((float)x1 + hx * cropPixW) / width;
        outKeypoints[k].y = ((float)y1 + hy * cropPixH) / height;
        outKeypoints[k].confidence = 1.0f / (1.0f + expf(-maxVal)); // sigmoid
    }
}

#else // !ENABLE_NCNN

bool PoseEstimator::Init(const std::string &, bool) {
    fprintf(stderr, "PoseEstimator: ncnn not available (build with -DENABLE_NCNN=ON)\n");
    return false;
}
void PoseEstimator::Release() {}
void PoseEstimator::Detect(const uint8_t *, int, int, std::vector<PoseDetection> &results) {
    results.clear();
}

#endif // ENABLE_NCNN
