// ncnn-based YOLO-Pose single-stage pose estimation
// Detects persons and estimates 17 COCO keypoints in one forward pass.

#include "pose/PoseEstimator.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

PoseEstimator::PoseEstimator() {}
PoseEstimator::~PoseEstimator() { Release(); }

#ifdef ENABLE_NCNN

bool PoseEstimator::Init(const std::string &modelDir, bool useGPU) {
    if (mReady) return true;

    mNet.opt.use_vulkan_compute = useGPU;
    mNet.opt.num_threads = 2;
    // Use FP32 to avoid precision issues with FP16
    mNet.opt.use_fp16_packed = false;
    mNet.opt.use_fp16_storage = false;
    mNet.opt.use_fp16_arithmetic = false;

    std::string paramPath = modelDir + "/model.ncnn.param";
    std::string binPath = modelDir + "/model.ncnn.bin";

    if (mNet.load_param(paramPath.c_str()) != 0) {
        fprintf(stderr, "PoseEstimator: failed to load %s\n", paramPath.c_str());
        return false;
    }
    if (mNet.load_model(binPath.c_str()) != 0) {
        fprintf(stderr, "PoseEstimator: failed to load %s\n", binPath.c_str());
        return false;
    }

    mReady = true;
    printf("PoseEstimator: loaded YOLO-Pose model from %s (GPU=%d, input=%d)\n",
           modelDir.c_str(), useGPU, mInputSize);
    return true;
}

void PoseEstimator::Release() {
    mNet.clear();
    mReady = false;
}

void PoseEstimator::Detect(const uint8_t *pixels, int width, int height,
                           std::vector<PoseDetection> &results) {
    results.clear();
    if (!mReady || !pixels) return;

    // Preprocess: letterbox resize to mInputSize x mInputSize
    // YOLO expects RGB, normalized to [0,1]
    int targetW = mInputSize, targetH = mInputSize;
    float scale = std::min((float)targetW / width, (float)targetH / height);
    int newW = (int)(width * scale);
    int newH = (int)(height * scale);
    int padW = (targetW - newW) / 2;
    int padH = (targetH - newH) / 2;

    ncnn::Mat input = ncnn::Mat::from_pixels_resize(
        pixels, ncnn::Mat::PIXEL_RGB,
        width, height, newW, newH);

    // Pad to target size with gray (114/255)
    ncnn::Mat padded(targetW, targetH, 3, (size_t)4u);
    ncnn::copy_make_border(input, padded, padH, targetH - newH - padH,
                           padW, targetW - newW - padW,
                           ncnn::BORDER_CONSTANT, 114.0f);

    const float normVals[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    padded.substract_mean_normalize(nullptr, normVals);

    // Run inference
    ncnn::Extractor ex = mNet.create_extractor();
    ex.input("in0", padded);

    ncnn::Mat output;
    ex.extract("out0", output);

    ParseYOLOPoseOutput(output, width, height, results);
}

void PoseEstimator::ParseYOLOPoseOutput(
    const ncnn::Mat &output, int imgW, int imgH,
    std::vector<PoseDetection> &results)
{
    // YOLO-Pose output format (after transpose):
    // Each column is a detection: [x, y, w, h, conf, kp0_x, kp0_y, kp0_conf, ...]
    // Total: 4 (bbox) + 1 (conf) + 17*3 (keypoints) = 56 values per detection

    int numDetections = output.w;
    int numValues = output.h;

    // Collect raw detections above confidence threshold
    struct RawDet {
        float x1, y1, x2, y2, conf;
        float kpts[17][3]; // x, y, conf per keypoint
    };
    std::vector<RawDet> rawDets;

    // Compute letterbox parameters to unmap coordinates
    float scale = std::min((float)mInputSize / imgW, (float)mInputSize / imgH);
    float padW = (mInputSize - imgW * scale) * 0.5f;
    float padH = (mInputSize - imgH * scale) * 0.5f;

    for (int i = 0; i < numDetections; i++) {
        // Read values for detection i from transposed output
        // output is [numValues x numDetections] in ncnn channel-major layout
        const float *col = (const float *)output.data + i;
        int stride = numDetections; // stride between rows

        float cx = col[0 * stride];
        float cy = col[1 * stride];
        float w  = col[2 * stride];
        float h  = col[3 * stride];
        float conf = col[4 * stride];

        if (conf < mConfThreshold) continue;

        RawDet det;
        // Unmap from letterbox to original image coordinates (normalized)
        det.x1 = ((cx - w * 0.5f) - padW) / (imgW * scale);
        det.y1 = ((cy - h * 0.5f) - padH) / (imgH * scale);
        det.x2 = ((cx + w * 0.5f) - padW) / (imgW * scale);
        det.y2 = ((cy + h * 0.5f) - padH) / (imgH * scale);
        det.conf = conf;

        // Clamp bbox to [0,1]
        det.x1 = std::max(0.0f, std::min(1.0f, det.x1));
        det.y1 = std::max(0.0f, std::min(1.0f, det.y1));
        det.x2 = std::max(0.0f, std::min(1.0f, det.x2));
        det.y2 = std::max(0.0f, std::min(1.0f, det.y2));

        // Read 17 keypoints
        for (int k = 0; k < 17; k++) {
            int base = 5 + k * 3;
            float kx = col[(base + 0) * stride];
            float ky = col[(base + 1) * stride];
            float kc = col[(base + 2) * stride];

            // Unmap from letterbox
            det.kpts[k][0] = (kx - padW) / (imgW * scale);
            det.kpts[k][1] = (ky - padH) / (imgH * scale);
            det.kpts[k][2] = 1.0f / (1.0f + expf(-kc)); // sigmoid
        }

        rawDets.push_back(det);
    }

    // NMS
    std::sort(rawDets.begin(), rawDets.end(),
              [](const RawDet &a, const RawDet &b) { return a.conf > b.conf; });

    auto iou = [](const RawDet &a, const RawDet &b) -> float {
        float ix1 = std::max(a.x1, b.x1), iy1 = std::max(a.y1, b.y1);
        float ix2 = std::min(a.x2, b.x2), iy2 = std::min(a.y2, b.y2);
        float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);
        float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
        float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
        return inter / (areaA + areaB - inter + 1e-6f);
    };

    std::vector<bool> suppressed(rawDets.size(), false);
    for (size_t i = 0; i < rawDets.size(); i++) {
        if (suppressed[i]) continue;

        PoseDetection pd = {};
        pd.bbox[0] = rawDets[i].x1;
        pd.bbox[1] = rawDets[i].y1;
        pd.bbox[2] = rawDets[i].x2;
        pd.bbox[3] = rawDets[i].y2;
        pd.bboxConf = rawDets[i].conf;
        pd.trackId = -1;

        for (int k = 0; k < 17; k++) {
            pd.keypoints[k].x = rawDets[i].kpts[k][0];
            pd.keypoints[k].y = rawDets[i].kpts[k][1];
            pd.keypoints[k].confidence = rawDets[i].kpts[k][2];
        }

        results.push_back(pd);

        for (size_t j = i + 1; j < rawDets.size(); j++) {
            if (!suppressed[j] && iou(rawDets[i], rawDets[j]) > mNmsThreshold)
                suppressed[j] = true;
        }

        if (results.size() >= 6) break;
    }
}

#else // !ENABLE_NCNN

bool PoseEstimator::Init(const std::string &, bool) {
    fprintf(stderr, "PoseEstimator: ncnn not available (build with -DENABLE_NCNN=ON)\n");
    return false;
}
void PoseEstimator::Release() {}
void PoseEstimator::Detect(const uint8_t *, int, int, std::vector<PoseDetection> &r) {
    r.clear();
}

#endif // ENABLE_NCNN
