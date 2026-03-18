// Lightweight multi-object tracker for 2-player dance game
// Inspired by ByteTrack (track low-confidence) + OC-SORT (observation-centric update)

#include "pose/PoseTracker.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

PoseTracker::PoseTracker() {}

void PoseTracker::Reset() {
    mTracks.clear();
    mNextId = 1;
}

void PoseTracker::Predict() {
    for (auto &t : mTracks) {
        PredictKalman(t.state);
    }
}

void PoseTracker::PredictKalman(KalmanState &s) {
    s.x += s.vx;
    s.y += s.vy;
}

void PoseTracker::UpdateKalman(KalmanState &s, float cx, float cy, float w, float h) {
    // OC-SORT style: update velocity from observation, not prediction
    float alpha = 0.4f; // smoothing factor
    float newVx = cx - (s.x - s.vx); // velocity from last OBSERVATION, not prediction
    float newVy = cy - (s.y - s.vy);
    s.vx = s.vx * (1.0f - alpha) + newVx * alpha;
    s.vy = s.vy * (1.0f - alpha) + newVy * alpha;
    s.x = cx;
    s.y = cy;
    s.w = w;
    s.h = h;
}

float PoseTracker::ComputeIoU(const KalmanState &track, const float bbox[4]) const {
    // Convert track state to bbox
    float tx1 = track.x - track.w * 0.5f;
    float ty1 = track.y - track.h * 0.5f;
    float tx2 = track.x + track.w * 0.5f;
    float ty2 = track.y + track.h * 0.5f;

    float ix1 = std::max(tx1, bbox[0]);
    float iy1 = std::max(ty1, bbox[1]);
    float ix2 = std::min(tx2, bbox[2]);
    float iy2 = std::min(ty2, bbox[3]);
    float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);

    float areaT = (tx2 - tx1) * (ty2 - ty1);
    float areaD = (bbox[2] - bbox[0]) * (bbox[3] - bbox[1]);
    return inter / (areaT + areaD - inter + 1e-6f);
}

float PoseTracker::ComputeColorDist(const Track &track, const float color[3]) const {
    float dr = track.avgColor[0] - color[0];
    float dg = track.avgColor[1] - color[1];
    float db = track.avgColor[2] - color[2];
    return sqrtf(dr * dr + dg * dg + db * db);
}

void PoseTracker::Update(std::vector<PoseDetection> &detections) {
    // Step 1: predict existing tracks forward
    Predict();

    // Step 2: compute IoU cost matrix between tracks and detections
    int nTracks = (int)mTracks.size();
    int nDets = (int)detections.size();

    // matched[i] = detection index matched to track i, -1 if unmatched
    std::vector<int> trackMatch(nTracks, -1);
    // detMatched[j] = track index matched to detection j, -1 if unmatched
    std::vector<int> detMatch(nDets, -1);

    if (nTracks > 0 && nDets > 0) {
        // Greedy matching by highest IoU (sufficient for <=6 tracks)
        std::vector<std::tuple<float, int, int>> pairs; // (iou, trackIdx, detIdx)
        for (int i = 0; i < nTracks; i++) {
            if (!mTracks[i].active) continue;
            for (int j = 0; j < nDets; j++) {
                float iou = ComputeIoU(mTracks[i].state, detections[j].bbox);
                if (iou > 0.1f) { // minimum IoU threshold
                    pairs.emplace_back(iou, i, j);
                }
            }
        }
        // Sort by IoU descending
        std::sort(pairs.begin(), pairs.end(),
                  [](const auto &a, const auto &b) { return std::get<0>(a) > std::get<0>(b); });

        for (auto &[iou, ti, di] : pairs) {
            if (trackMatch[ti] >= 0 || detMatch[di] >= 0) continue; // already matched
            trackMatch[ti] = di;
            detMatch[di] = ti;
        }
    }

    // Step 3: update matched tracks
    for (int i = 0; i < nTracks; i++) {
        if (trackMatch[i] >= 0) {
            int di = trackMatch[i];
            auto &det = detections[di];
            auto &track = mTracks[i];

            float cx = (det.bbox[0] + det.bbox[2]) * 0.5f;
            float cy = (det.bbox[1] + det.bbox[3]) * 0.5f;
            float w = det.bbox[2] - det.bbox[0];
            float h = det.bbox[3] - det.bbox[1];

            UpdateKalman(track.state, cx, cy, w, h);
            track.lostFrames = 0;
            track.totalFrames++;
            track.lastConf = det.bboxConf;
            det.trackId = track.id;
        }
    }

    // Step 4: mark unmatched tracks as lost
    for (int i = 0; i < nTracks; i++) {
        if (trackMatch[i] < 0 && mTracks[i].active) {
            mTracks[i].lostFrames++;
            if (mTracks[i].lostFrames > mMaxLostFrames) {
                mTracks[i].active = false;
            }
        }
    }

    // Step 5: create new tracks for unmatched detections
    for (int j = 0; j < nDets; j++) {
        if (detMatch[j] >= 0) continue; // already matched

        auto &det = detections[j];
        if (det.bboxConf < 0.3f) continue; // too low confidence to start new track

        Track newTrack = {};
        newTrack.id = mNextId++;
        newTrack.state.x = (det.bbox[0] + det.bbox[2]) * 0.5f;
        newTrack.state.y = (det.bbox[1] + det.bbox[3]) * 0.5f;
        newTrack.state.w = det.bbox[2] - det.bbox[0];
        newTrack.state.h = det.bbox[3] - det.bbox[1];
        newTrack.state.vx = 0;
        newTrack.state.vy = 0;
        newTrack.lostFrames = 0;
        newTrack.totalFrames = 1;
        newTrack.lastConf = det.bboxConf;
        newTrack.active = true;

        det.trackId = newTrack.id;
        mTracks.push_back(newTrack);
    }

    // Step 6: cleanup dead tracks
    mTracks.erase(
        std::remove_if(mTracks.begin(), mTracks.end(),
                       [](const Track &t) { return !t.active; }),
        mTracks.end());
}
