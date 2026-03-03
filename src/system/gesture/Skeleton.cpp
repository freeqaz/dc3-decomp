#include "gesture/Skeleton.h"
#include "ArchiveSkeleton.h"
#include "IdentityInfo.h"
#include "gesture/GestureMgr.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/SkeletonHistory.h"
#include "gesture/JointUtl.h"
#include "math/DoubleExponentialSmoother.h"
#include "obj/Data.h"
#include "os/Debug.h"
#include "os/System.h"
#include "xdk/NUI.h"
#include "xdk/XAPILIB.h"
#include <cmath>

Vector3DESmoother SkeletonFrame::sUpVectorSmoother;

#pragma region SkeletonFrame

float SkeletonFrame::TiltAngle() const { return (PI / 2) - (float)atan2(mFloorNormal.y, mFloorNormal.z); }

void SkeletonFrame::Init() {
    static Symbol kinect("kinect");
    static Symbol up_vector_smoothing("up_vector_smoothing");
    static Symbol smoothing("smoothing");
    static Symbol trend("trend");
    DataArray *cfg = SystemConfig(kinect, up_vector_smoothing);
    sUpVectorSmoother.SetSmoothParameters(
        cfg->FindFloat(smoothing), cfg->FindFloat(trend)
    );
    sUpVectorSmoother.ForceValue(Vector3(0, 1, 0));
}

void SkeletonFrame::Create(const NUI_SKELETON_FRAME &nui_frame, int i2) {
    mFrameNumber = nui_frame.dwFrameNumber;
    mElapsedMs = i2;

    sUpVectorSmoother.Smooth(
        Vector3(nui_frame.vFloorClipPlane.x, nui_frame.vFloorClipPlane.y, nui_frame.vFloorClipPlane.z),
        i2 * 0.001f,
        true
    );

    mFloorNormal = sUpVectorSmoother.Value();
    mFloorClipPlane.Set(
        nui_frame.vFloorClipPlane.x,
        nui_frame.vFloorClipPlane.y,
        nui_frame.vFloorClipPlane.z,
        nui_frame.vFloorClipPlane.w
    );
}

#pragma endregion
#pragma region Skeleton

Skeleton::Skeleton() : mTracking(kSkeletonNotTracked), mTrackingID(-1), unkac4(0) {
    Init();
}

void Skeleton::JointPos(SkeletonCoordSys cs, SkeletonJoint joint, Vector3 &pos) const {
    MILO_ASSERT((0) <= (cs) && (cs) < (kNumCoordSys), 0xDA);
    MILO_ASSERT((0) <= (joint) && (joint) < (kNumJoints), 0xDB);
    pos = mTrackedJoints[joint].mJointPos[cs];
}

bool Skeleton::Displacement(
    const SkeletonHistory *history,
    SkeletonCoordSys cs,
    SkeletonJoint joint,
    int i4,
    Vector3 &disp,
    int &iref
) const {
    ArchiveSkeleton archiveSkeleton;
    if (PrevTrackedSkeleton(history, i4, iref, archiveSkeleton)) {
        Vector3 v3;
        archiveSkeleton.JointPos(cs, joint, v3);
        Subtract(mTrackedJoints[joint].mJointPos[cs], v3, disp);
        return true;
    } else {
        disp.Zero();
        return false;
    }
}

bool Skeleton::Displacements(
    const SkeletonHistory *history,
    SkeletonCoordSys cs,
    int i4,
    Vector3 *disps,
    int &iref
) const {
    FOREACH (it, mCamDisplacements) {
        if (it->unk0 == i4) {
            memcpy(disps, it->unk8, sizeof(it->unk8));
            iref = it->unk4;
            return (iref + 1) != 0;
        }
    }

    CameraDisplacement camDisp;
    camDisp.unk0 = i4;
    bool ok = false;
    ArchiveSkeleton archiveSkeleton;
    if (PrevTrackedSkeleton(history, i4, iref, archiveSkeleton)) {
        for (int i = 0; i < kNumJoints; i++) {
            Vector3 prevPos;
            archiveSkeleton.JointPos(cs, (SkeletonJoint)i, prevPos);
            Subtract(mTrackedJoints[i].mJointPos[cs], prevPos, disps[i]);
            camDisp.unk8[i] = disps[i];
        }
        ok = true;
    } else {
        memset(disps, 0, sizeof(camDisp.unk8));
        memset(camDisp.unk8, 0, sizeof(camDisp.unk8));
    }
    camDisp.unk4 = iref;
    mCamDisplacements.push_back(camDisp);
    return ok;
}

JointConfidence Skeleton::JointConf(SkeletonJoint joint) const {
    MILO_ASSERT((0) <= (joint) && (joint) < (kNumJoints), 0xE1);
    return mTrackedJoints[joint].mJointConf;
}

bool Skeleton::IsTracked() const { return mTracking == kSkeletonTracked; }
int Skeleton::QualityFlags() const { return mQualityFlags; }
int Skeleton::ElapsedMs() const { return mElapsedMs; }

void Skeleton::CameraToPlayerXfm(SkeletonCoordSys cs, Transform &playerXfm) const {
    MILO_ASSERT((kCoordLeftArm) <= (cs) && (cs) < (kNumCoordSys), 0x127);
    playerXfm = mPlayerXfms[cs - 1];
}

void Skeleton::CamJointPositions(Vector3 *positions) const {
    for (int i = 0; i < kNumJoints; i++) {
        *positions++ = mTrackedJoints[i].mJointPos[kCoordCamera];
    }
}

void Skeleton::CamBoneLengths(float *lens) const {
    memcpy(lens, mCamBoneLengths, sizeof(mCamBoneLengths));
}

float Skeleton::BoneLength(SkeletonBone bone, SkeletonCoordSys cs) const {
    if (cs == kCoordCamera) {
        MILO_ASSERT((0) <= (bone) && (bone) < (kNumBones), 0x12F);
        return mCamBoneLengths[bone];
    } else
        return BaseSkeleton::BoneLength(bone, cs);
}

bool Skeleton::IsValid() const {
    if (mSkeletonIdx >= 0) {
        return TheGestureMgr->IsSkeletonValid(mSkeletonIdx);
    } else
        return false;
}

bool Skeleton::IsSitting() const {
    if (mSkeletonIdx >= 0) {
        return TheGestureMgr->IsSkeletonSitting(mSkeletonIdx);
    } else
        return false;
}

bool Skeleton::IsSideways() const {
    if (mSkeletonIdx >= 0) {
        return TheGestureMgr->IsSkeletonSideways(mSkeletonIdx);
    } else
        return false;
}

const TrackedJoint &Skeleton::HandJoint(SkeletonSide side) const {
    return mTrackedJoints[side == kSkeletonLeft ? kJointHandLeft : kJointHandRight];
}

const TrackedJoint &Skeleton::ElbowJoint(SkeletonSide side) const {
    return mTrackedJoints[side == kSkeletonLeft ? kJointElbowLeft : kJointElbowRight];
}

const TrackedJoint &Skeleton::ShoulderJoint(SkeletonSide side) const {
    return mTrackedJoints[side == kSkeletonLeft ? kJointShoulderLeft : kJointShoulderRight];
}

const TrackedJoint &Skeleton::HipJoint(SkeletonSide side) const {
    return mTrackedJoints[side == kSkeletonLeft ? kJointHipLeft : kJointHipRight];
}

const TrackedJoint &Skeleton::KneeJoint(SkeletonSide side) const {
    return mTrackedJoints[side == kSkeletonLeft ? kJointKneeLeft : kJointKneeRight];
}

void Skeleton::ScreenPos(SkeletonJoint joint, Vector2 &pos) const {
    if (mTracking == kSkeletonTracked) {
        JointScreenPos(mTrackedJoints[joint], pos);
    } else
        pos.Zero();
}

bool Skeleton::PrevTrackedSkeleton(
    const SkeletonHistory *history, int i2, int &iref, ArchiveSkeleton &archiveSkeleton
) const {
    MILO_ASSERT(history, 0x169);
    if (mTracking == kSkeletonTracked
        && history->PrevSkeleton(*this, i2, archiveSkeleton, iref)) {
        return archiveSkeleton.IsTracked();
    } else
        return false;
}

bool Skeleton::Velocity(
    const SkeletonHistory &history,
    SkeletonCoordSys cs,
    SkeletonJoint joint,
    int i4,
    Vector3 &velocity,
    int &iref
) const {
    if (Displacement(&history, cs, joint, i4, velocity, iref)) {
        float scale = 1.0f / (iref * 0.001f);
        velocity.x = velocity.x * scale;
        velocity.y = velocity.y * scale;
        velocity.z = velocity.z * scale;
        return true;
    } else {
        velocity.Zero();
        return false;
    }
}

Skeleton &Skeleton::operator=(const Skeleton &other) {
    memcpy(mTrackedJoints, other.mTrackedJoints, sizeof(mTrackedJoints));
    memcpy(mCamBoneLengths, other.mCamBoneLengths, sizeof(mCamBoneLengths));
    memcpy(mPlayerXfms, other.mPlayerXfms, sizeof(mPlayerXfms));
    mTracking = other.mTracking;
    mQualityFlags = other.mQualityFlags;
    mElapsedMs = other.mElapsedMs;
    mTrackingID = other.mTrackingID;
    unkab0 = other.unkab0;
    mSkeletonIdx = other.mSkeletonIdx;
    unkac4 = other.unkac4;
    mCamDisplacements = other.mCamDisplacements;
    return *this;
}

void Skeleton::Init() {
    mTracking = kSkeletonNotTracked;
    mSkeletonIdx = -1;
    mQualityFlags = 0;
    unkab0.Zero();
    for (int i = 0; i < 5; i++) {
        mPlayerXfms[i].Reset();
    }
    for (int i = 0; i < kNumJoints; i++) {
        for (int j = 0; j < kNumCoordSys; j++) {
            mTrackedJoints[i].mJointPos[j].Zero();
        }
        mTrackedJoints[i].mJointConf = kConfidenceNotTracked;
        mTrackedJoints[i].mSmoothedPos.Zero();
    }
    memset(mCamBoneLengths, 0, sizeof(mCamBoneLengths));
    mCamDisplacements = std::vector<CameraDisplacement>();
}

bool Skeleton::ProfileMatched() const {
    IdentityInfo *info = TheGestureMgr->GetIdentityInfo(mSkeletonIdx);
    return info ? info->ProfileMatched() : false;
}

int Skeleton::GetEnrollmentIndex() const {
    IdentityInfo *info = TheGestureMgr->GetIdentityInfo(mSkeletonIdx);
    return info ? info->EnrollmentIndex() : -1;
}

bool Skeleton::NeedIdentify() const {
    return GetEnrollmentIndex() == -1 || GetEnrollmentIndex() == -5;
}

void Skeleton::Poll(int idx, const SkeletonFrame &frame) {
    MILO_ASSERT((0) <= (idx) && (idx) < (6), 0x1F8);
    if (mSkeletonIdx != idx && TheGestureMgr) {
        IdentityInfo *info = TheGestureMgr->GetIdentityInfo(idx);
        MILO_ASSERT(info, 0x1FC);
        info->Reset(idx);
    }

    mSkeletonIdx = idx;
    mElapsedMs = frame.mElapsedMs;
    const SkeletonData &data = frame.mSkeletonDatas[idx];
    mTrackingID = data.mTrackingID;
    unkab0 = data.mHipCenter;
    mTracking = data.mTracking;
    if (mTracking == kSkeletonNotTracked) {
        Init();
        return;
    }
    if (mTracking == kSkeletonTracked) {
        mQualityFlags = data.mQualityFlags;
        if (TheGestureMgr) {
            IdentityInfo *info = TheGestureMgr->GetIdentityInfo(idx);
            MILO_ASSERT(info, 0x211);
            if (info->EnrollmentIndex() != data.mClippedFlags) {
                info->SetEnrollmentIndex(data.mClippedFlags);
            }
        }

        for (int i = 1; i < kNumCoordSys; i++) {
            BaseSkeleton::MakeCameraToPlayerXfm(
                (SkeletonCoordSys)i,
                mPlayerXfms[i - 1],
                data.mJointPositions,
                frame.mFloorNormal
            );
        }

        for (int i = 0; i < kNumJoints; i++) {
            mTrackedJoints[i].mJointPos[kCoordCamera] = data.mJointPositions[i];
            for (int j = 1; j < kNumCoordSys; j++) {
                MultiplyTranspose(
                    data.mJointPositions[i],
                    mPlayerXfms[j - 1],
                    mTrackedJoints[i].mJointPos[j]
                );
            }
            mTrackedJoints[i].mJointConf = (JointConfidence)data.mJointTrackingState[i];
            mTrackedJoints[i].mSmoothedPos = data.mRawPositions[i];
        }

        for (int i = 0; i < kNumBones; i++) {
            mCamBoneLengths[i] = BoneLength((SkeletonBone)i, kCoordCamera);
        }

        if (!mCamDisplacements.empty()) {
            mCamDisplacements.erase(mCamDisplacements.begin(), mCamDisplacements.end());
        }

        unkac4 = (mTrackedJoints[kJointHipLeft].mJointPos[kCoordCamera].y
                  + mTrackedJoints[kJointHipRight].mJointPos[kCoordCamera].y)
            * 0.5f + frame.mFloorClipPlane.w;
    }
}

void Skeleton::PostUpdate() {}

bool Skeleton::RequestIdentity() {
    MILO_ASSERT(!GestureMgr::sIdentityOpInProgress, 0x2A9);
    IdentityInfo *info = TheGestureMgr->GetIdentityInfo(mSkeletonIdx);
    if (info) {
        HRESULT hr = NuiIdentityIdentify(mTrackingID, 0, IdentityCallback, info);
        MILO_ASSERT(hr != E_INVALIDARG, 0x2B1);
        if (hr < 0) {
            if (hr != (HRESULT)0x8000000A)
                return false;
        }
        if (hr == 0) {
            info->SetIdentified(true);
        } else {
            GestureMgr::sIdentityOpInProgress = true;
        }
        return true;
    } else {
        return false;
    }
}

bool Skeleton::EnrollIdentity(int enrollmentIdx) {
    MILO_ASSERT(!GestureMgr::sIdentityOpInProgress, 0x25D);
    IdentityInfo *info = TheGestureMgr->GetIdentityInfo(mSkeletonIdx);
    if (!info) {
        return false;
    }

    DWORD flags = enrollmentIdx == -1 ? 1 : 0x21;
    if (enrollmentIdx == -3) {
        enrollmentIdx = -2;
    }

    HRESULT hr = NuiIdentityEnroll(mTrackingID, flags, IdentityCallback, info);
    MILO_ASSERT(hr != E_INVALIDARG, 0x26E);

    bool immediate = hr == 0;
    if (hr < 0) {
        if (hr != (HRESULT)0x8000000A) {
            return false;
        }
        immediate = false;
    }

    if (immediate) {
        info->SetIdentified(true);
    } else {
        GestureMgr::sIdentityOpInProgress = true;
    }
    return true;
}

int Skeleton::IdentityCallback(void *context, NUI_IDENTITY_MESSAGE *msg) {
    IdentityInfo *info = (IdentityInfo *)context;
    MILO_ASSERT(info != nullptr, 0x280);
    MILO_ASSERT(msg != nullptr, 0x281);

    if (msg->MessageId != NUI_IDENTITY_MESSAGE_ID_FRAME_PROCESSED) {
        if (msg->MessageId == NUI_IDENTITY_MESSAGE_ID_COMPLETE) {
            info->SetIdentified(true);
            info->SetProfileMatched(msg->Data.Complete.bProfileMatched != 0);
            if (info->EnrollmentIndex() != (int)msg->Data.Complete.dwEnrollmentIndex) {
                info->SetEnrollmentIndex(msg->Data.Complete.dwEnrollmentIndex);
            }
        } else {
            MILO_ASSERT(false, 0x297);
        }
    }

    bool enabled = TheGestureMgr->IDEnabled();
    if (!enabled) {
        MILO_LOG("An identification operation that was in progress was canceled.\n");
        GestureMgr::sIdentityOpInProgress = false;
    }
    return enabled;
}
