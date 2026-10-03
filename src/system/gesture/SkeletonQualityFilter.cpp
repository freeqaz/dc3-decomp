#include "gesture\SkeletonQualityFilter.h"
#include "BaseSkeleton.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\GestureMgr.h"
#include "gesture\Skeleton.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamPlayerData.h"
#include "math\Vec.h"
#include <stdio.h>

#ifndef HX_NATIVE
template int sprintf_s<50>(char (&)[50], const char *, ...);
#endif

SkeletonQualityFilter::SkeletonQualityFilter()
    : mConfidenceLossThreshold(0), mConfidenceRegainThreshold(20), mValid(0),
      mConfidence(0), mSitting(0), mSideways(0), mSidewaysCutoffThreshold(0.55) {}

void SkeletonQualityFilter::Init(float loss, float regain) {
    mConfidenceLossThreshold = loss;
    mConfidenceRegainThreshold = regain;
}

void SkeletonQualityFilter::SetSidewaysCutoffThreshold(float thresh) {
    mSidewaysCutoffThreshold = thresh;
}

void SkeletonQualityFilter::RestoreDefaultSidewaysCutoffThreshold() {
    mSidewaysCutoffThreshold = 0.55;
}

void SkeletonQualityFilter::Update(const Skeleton &skeleton, bool inShellMode) {
    int skeletonIdx = skeleton.SkeletonIndex();
    if (skeletonIdx >= 0 && skeletonIdx < 6) {
        if (!(skeleton.IsTracked() || TheGestureMgr->IsTrackingAllSkeletons())) {
            mValid = false;
            mSitting = false;
            mSideways = false;
            mIsConfident = false;
            return;
        }
        const Vector3 &root = skeleton.GetUnkab0();
        if (root == Vector3::ZeroVec()) {
            mValid = false;
            mSitting = false;
            mSideways = false;
            mIsConfident = false;
            return;
        }
        bool playerIsPlaying = false;
        for (int i = 0; i < 2; i++) {
            if (TheGameData->Player(i)->GetSkeletonTrackingID()
                == skeleton.TrackingID()) {
                playerIsPlaying = TheGameData->Player(i)->IsPlaying();
                break;
            }
        }
        const TrackedJoint *joints = skeleton.TrackedJoints();
        UpdateIsConfident(joints);
        UpdateIsSideways(joints);
        UpdateIsSitting(joints);

        if (!inShellMode && playerIsPlaying) {
            mValid = true;
        } else {
            mValid = mIsConfident && !mSideways && !mSitting;
        }
    }
}

void SkeletonQualityFilter::UpdateIsConfident(const TrackedJoint *joints) {
    mConfidence = 0.0f;
    for (int i = 0; i < kNumJoints; i++) {
        if (joints[i].mJointConf == kConfidenceTracked || i == kJointFootLeft
            || i == kJointFootRight || i == kJointAnkleLeft || i == kJointAnkleRight) {
            mConfidence += 1.0f;
        }
    }
    if (mConfidence < mConfidenceLossThreshold) {
        mIsConfident = false;
    }
    if (mConfidence > mConfidenceRegainThreshold) {
        mIsConfident = true;
    }
}

// w8-l: 99.914894 normalized, the only function short of 100% in this unit
// (10/11).  27 charged rows out of 94, dominated by one callee-saved pair:
// r28 and r29 are exchanged for the whole body (13 of 24 register rows), which
// drags f0/f13, f30/f13 and f29/f12 along with it, plus 4 slot swaps including
// (0x4, 0xec).  A single liveness/scheduling cause upstream of all of them;
// measured only in this wave, no source lever tried.
// w21-as (still 99.914894, same rows): a standalone cl.exe probe reproduces
// this listing exactly once Normalize is the Vec.h inline (an extern
// Normalize makes MSVC reload joint[2] after the call).  Probe-inert, all
// keeping this <-> r28 / joint <-> r29 where the image has joint in r28:
// threshold as fabsf(Dot(vDiff, Vector3(0, 0, 1))) (either arg order, or a
// named axis) -- that spelling does reproduce the image's (x + y) * 0 + z;
// `mSideways = threshold > thresh` and if/else forms; the og-dc3 Dot(vDiff,
// vDiff2) final test (both comparison orders); named refs to joint[8]/[4]/[2]
// (all 6 declaration orders, as TrackedJoint& or Vector3&); the cutoff as a
// local, `cutoff * (mSideways ? 0.9f : 1.0f)`, `if (mSideways) thresh *=
// 0.9f`, and `SkeletonQualityFilter *self = this;`.
void SkeletonQualityFilter::UpdateIsSideways(const TrackedJoint *joint) {
    Vector3 vDiff;
    Subtract(joint[8].mJointPos[0], joint[4].mJointPos[0], vDiff);
    Normalize(vDiff, vDiff);
    float threshold = fabsf((vDiff.x + vDiff.y) * 0.0f + vDiff.z);
    float thresh =
        mSideways ? mSidewaysCutoffThreshold * 0.9f : mSidewaysCutoffThreshold;
    bool side = true;
    if (threshold <= thresh) {
        side = false;
    }
    mSideways = side;
    Subtract(joint[8].mJointPos[0], joint[2].mJointPos[0], vDiff);
    Normalize(vDiff, vDiff);
    Vector3 vDiff2;
    Subtract(joint[4].mJointPos[0], joint[2].mJointPos[0], vDiff2);
    Normalize(vDiff2, vDiff2);
    // The image reads this from .data (0x82F0C194, a mutable static), not an
    // __real@ literal.
    static float sSidewaysDotThreshold = 0.25f;
    if (vDiff.y * vDiff2.y + vDiff.x * vDiff2.x + vDiff.z * vDiff2.z > sSidewaysDotThreshold) {
        mSideways = true;
    }
}

void SkeletonQualityFilter::UpdateIsSitting(const TrackedJoint *joint) {
    Vector3 vDiff;
    Subtract(joint[0xD].mJointPos[0], joint[0xC].mJointPos[0], vDiff);
    Normalize(vDiff, vDiff);
    Vector3 vDiff2;
    Subtract(joint[0x10].mJointPos[0], joint[0xF].mJointPos[0], vDiff2);
    Normalize(vDiff2, vDiff2);
    if (!mSitting) {
        if (vDiff.y > -0.7f && vDiff2.y > -0.7f) {
            mSitting = true;
        }
    } else if (vDiff.y < -0.8f && vDiff2.y < -0.8f) {
        mSitting = false;
    }
}
