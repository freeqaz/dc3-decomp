#pragma once
// Lightweight multi-object tracker (ByteTrack/OC-SORT inspired)
// Maintains persistent player IDs across frames using Kalman filter
// motion prediction + Hungarian matching.
//
// Designed for 2-player dance game: max 6 tracks, optimized for
// non-linear motion and brief occlusions.

#include "pose/PoseEstimator.h"
#include <vector>

class PoseTracker {
public:
    PoseTracker();

    // Update tracks with new detections, assign trackId to each detection
    void Update(std::vector<PoseDetection> &detections);

    // Reset all tracks
    void Reset();

    // Max frames to keep a lost track before deletion
    void SetMaxLostFrames(int n) { mMaxLostFrames = n; }

private:
    struct KalmanState {
        float x, y;      // center position
        float vx, vy;    // velocity
        float w, h;      // bounding box size
    };

    struct Track {
        int id;
        KalmanState state;
        int lostFrames;   // frames since last matched detection
        int totalFrames;  // total frames this track has existed
        float lastConf;
        bool active;

        // Simple color histogram for re-ID (average RGB of bounding box)
        float avgColor[3];
    };

    void Predict();
    void MatchAndUpdate(std::vector<PoseDetection> &detections);
    float ComputeIoU(const KalmanState &track, const float bbox[4]) const;
    float ComputeColorDist(const Track &track, const float color[3]) const;
    void UpdateKalman(KalmanState &state, float cx, float cy, float w, float h);
    void PredictKalman(KalmanState &state);

    std::vector<Track> mTracks;
    int mNextId = 1;
    int mMaxLostFrames = 30; // ~1 second at 30fps
};
