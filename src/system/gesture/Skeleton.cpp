#include "gesture\Skeleton.h"
#include "ArchiveSkeleton.h"
#include "IdentityInfo.h"
#include "gesture\GestureMgr.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\SkeletonHistory.h"
#include "gesture\JointUtl.h"
#include "math\DoubleExponentialSmoother.h"
#include "obj\Data.h"
#include "os\Debug.h"
#include "os\System.h"
#include "xdk\NUI.h"
#include "xdk\XAPILIB.h"
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

// FILE scope, not function-local scope: the image materialises ONE base
// (lbl_82F0BFD0 = sJointRemap) and reaches the other two tables by
// displacement off it -- `addi r8, r3, 0xa0` (0xa0 == sizeof sJointRemap)
// for sTrackingMap, `subi r7, r3, 0x4` for sJointTrackingMap -- which is
// only possible if the three live contiguously in one .rdata section.
// MSVC puts each FUNCTION-LOCAL static in its own COMDAT, so as locals they
// emitted three separate lis/addi pairs against three mangled
// `?sJointRemap@?1??Create@...` symbols and could never be folded.
// NEGATIVE RESULT (w7-an, 2026-09-14): merely REORDERING the three
// function-local statics to match the image's data order was byte-identical
// inert (71.9 both ways, same 58/14/19/25 row counts) -- the COMDAT split is
// what blocks the fold, not the order.  Moving them to file scope: 71.9 -> 74.0.
static const int sJointRemap[kNumJoints][2] = {
    {0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7},
    {8, 8}, {9, 9}, {10, 10}, {11, 11}, {12, 12}, {13, 13}, {14, 14},
    {18, 15}, {15, 16}, {16, 17}, {17, 18}, {19, 19}
};
static const SkeletonTrackingState sTrackingMap[] = {
    kSkeletonNotTracked, kSkeletonPositionOnly, kSkeletonTracked
};
static const int sJointTrackingMap[] = { 0, 1, 2 };

// XNAMath's XMVector3Transform, inlined.  The image's splat order (Z, Y, X)
// and its multiply-add order (Z*r[2]+r[3], then Y*r[1], then X*r[0]) are that
// function's body verbatim, in both of Create's loops, which is why it is
// spelled as a helper here rather than open-coded twice.
// RESIDUAL (w7-an, 74.0 canonical): the image RELOADS all four mat.r[] rows
// inside each loop body -- `addi r5, r1, 0x90` / `lvx128 v63, r0, r5` and
// three more, re-executed every iteration in the joint loop and again at the
// top of the skeleton loop -- while MSVC hoists ours into the preheaders
// (`lvx128 v0/v13/v12/v11`).  That is ~22 of the 99 remaining rows.
// NEGATIVE RESULT (w7-an, 2026-09-14): neither passing the matrix by
// `const XMMATRIX &` (this spelling) nor by `const XMMATRIX *` through a
// named `const XMMATRIX *pmat = &mat;` blocks the hoist: all three spellings
// produce a BYTE-IDENTICAL diff (74.0 canonical, same 45/14/16/24 row
// counts).  MSVC fully forwards the inlined parameter back to the local, so
// the loads are provably invariant whatever the indirection.
// NEGATIVE RESULT (w7-ay, 2026-09-14, floor 74.0 held): two more spellings
// aimed at the same hoist were byte-for-byte inert (74.0, same 45/14/16/24
// rows): (1) `const XMMATRIX M` BY VALUE -- XNAMath's Xbox CXMMATRIX -- the
// 64-byte copy is elided and the loads still hoist; (2) reading the rows
// through the `__lvx(&M.r[i], 0)` intrinsic -- MSVC hoists the intrinsic
// loads exactly like the field reads.  Since even an intrinsic load hoists,
// the image's per-iteration `lvx128 v63, r0, r5` (0x82435B54) and the
// per-skeleton reload at 0x82435C94 are not an aliasing effect of any
// spelling of the transform; they are consistent with the allocator
// rematerialising the four rows from their stack slots (which is also why
// they land in v59-v63 and force the `vmaddcfp128`/`vmaddfp128` encodings at
// 0x82435B84 rather than our 4-operand `vmaddfp` on v0-v13).  The hip-centre
// store order below (y, x, z -- the joint loop's order) is the one of the
// three tried (z,y,x / x,y,z / y,x,z) that reproduces the image's z, y, x
// stores at 0x82435CDC-0x82435CF0; the remaining rows there are which
// element is held in f13 across the other two, scheduling only.
static XMVECTOR XMVector3Transform(XMVECTOR V, const XMMATRIX &M) {
    XMVECTOR Z = __vspltw(V, 2);
    XMVECTOR Y = __vspltw(V, 1);
    XMVECTOR X = __vspltw(V, 0);
    XMVECTOR Result = __vmaddfp(Z, M.r[2], M.r[3]);
    Result = __vmaddfp(Y, M.r[1], Result);
    Result = __vmaddfp(X, M.r[0], Result);
    return Result;
}

void SkeletonFrame::Create(const NUI_SKELETON_FRAME &nui_frame, int elapsed) {
    mFrameNumber = nui_frame.dwFrameNumber;
    mElapsedMs = elapsed;

    sUpVectorSmoother.Smooth(
        Vector3(nui_frame.vNormalToGravity.x, nui_frame.vNormalToGravity.y, nui_frame.vNormalToGravity.z),
        elapsed * 0.001f,
        false
    );

    mFloorNormal = sUpVectorSmoother.Value();
    mFloorClipPlane.Set(
        nui_frame.vFloorClipPlane.x,
        nui_frame.vFloorClipPlane.y,
        nui_frame.vFloorClipPlane.z,
        nui_frame.vFloorClipPlane.w
    );

    // Pass smoothed floor normal (with w=0) to NuiTransformMatrixLevel
    XMVECTOR gravVec;
    gravVec.x = mFloorNormal.x;
    gravVec.y = mFloorNormal.y;
    gravVec.z = mFloorNormal.z;
    gravVec.w = 0.0f;
    XMMATRIX mat = NuiTransformMatrixLevel(gravVec);


    // First pass: transform joint positions by gravity matrix
    XMVECTOR transformed[6 * kNumJoints];
    for (int s = 0; (unsigned int)s < 6; s++) {
        if (nui_frame.SkeletonData[s].eTrackingState == NUI_SKELETON_TRACKED) {
            for (int j = 0; j < kNumJoints; j++) {
                transformed[s * kNumJoints + j] =
                    XMVector3Transform(nui_frame.SkeletonData[s].SkeletonPositions[j], mat);
            }
        }
    }

    // Second pass: copy NUI data to SkeletonData
    for (int s = 0; s < (unsigned long)6; s++) {
        const NUI_SKELETON_DATA &nuiSkel = nui_frame.SkeletonData[s];
        SkeletonData &data = mSkeletonDatas[s];
        data.mTracking = sTrackingMap[nuiSkel.eTrackingState];
        if (data.mTracking == kSkeletonTracked) {
            data.mQualityFlags = nuiSkel.dwQualityFlags;
            for (int j = 0; j < kNumJoints; j++) {
                int dst = sJointRemap[j][0];
                int src = sJointRemap[j][1];
                data.mRawPositions[dst].z = nuiSkel.SkeletonPositions[src].z;
                data.mRawPositions[dst].y = nuiSkel.SkeletonPositions[src].y;
                data.mRawPositions[dst].x = nuiSkel.SkeletonPositions[src].x;
                data.mJointPositions[dst].y = transformed[s * kNumJoints + src].y;
                data.mJointPositions[dst].x = transformed[s * kNumJoints + src].x;
                data.mJointPositions[dst].z = transformed[s * kNumJoints + src].z;
                data.mJointTrackingState[dst] = sJointTrackingMap[nuiSkel.eSkeletonPositionTrackingState[src]];
            }
        }
        data.mTrackingID = nuiSkel.dwTrackingID;
        data.mClippedFlags = nuiSkel.dwEnrollmentIndex;

        // Transform hip center by gravity matrix
        XMVECTOR hipResult = XMVector3Transform(nuiSkel.Position, mat);
        data.mHipCenter.y = hipResult.y;
        data.mHipCenter.x = hipResult.x;
        data.mHipCenter.z = hipResult.z;
    }
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
            return 0 < (unsigned int)(iref + 1);
        }
    }

    CameraDisplacement camDisp;
    camDisp.unk0 = i4;
    ArchiveSkeleton archiveSkeleton;
    bool ok = PrevTrackedSkeleton(history, i4, iref, archiveSkeleton);
    if (ok) {
        for (int i = 0; kNumJoints > i; i++) {
            Vector3 prevPos;
            archiveSkeleton.JointPos(cs, (SkeletonJoint)i, prevPos);
            Subtract(mTrackedJoints[i].mJointPos[cs], prevPos, disps[i]);
        }
    } else {
        memset(disps, 0, sizeof(camDisp.unk8));
    }
    camDisp.unk4 = iref;
    memcpy(camDisp.unk8, disps, sizeof(camDisp.unk8));
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
    mCamDisplacements.clear();
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

// RESIDUAL (w7-br, 97.5 canonical, 49 of 209 rows; was 91.13 under w7-bl):
// ONE induction-variable choice left in the kNumJoints loop, and the flat
// register renumbering it forces.  The image keeps i*0x10 as a plain OFFSET
// (r27, `mr r27, r20` zeroed at 0x82436BD0, `addi r27, r27, 0x10` at
// 0x82436C5C) and adds it to TWO hoisted bases -- `add r26, r27, r22` at the loop
// head (0x82436BEC) (r22 = data+0x144, &mJointPositions[0]) and `add r10, r23, r27` in the
// tail (0x82436C50; r23 = data+4, &mRawPositions[0]).  MSVC here strength-reduces the
// same two uses into two WALKING POINTERS (`addi r26, r26, 0x10` /
// `addi r28, r28, 0x10`), one register lighter: that one register is why the
// image saves twelve callee-saved GPRs (`bl __savegprlr_20`, frame 0xe0) and
// we save eleven (`__savegprlr_21`, 0xd0), and every remaining register-swap
// row is that cascade (r21<->r23, r20<->r22, r27<->r28).
// LEVERS (w7-br), each measured with run_objdiff in the worktree:
//   * `TrackedJoint &tj = mTrackedJoints[i];` bound at the loop HEAD but
//     used only for the two tail stores (the inner loop keeps the
//     `mTrackedJoints[i].mJointPos[j]` subscript): 91.13 -> 95.6.  This is
//     what gives MSVC the image's i*0x74 offset IV (r29) with the two bases
//     `this` (inner loop) and `this+4` (r24, head `add r28, r24, r29` at 0x82436BE4), and
//     the free trip test `cmpwi cr6, r29, 0x910` at 0x82436C60 -- w7-bl's
//     note recorded the same binding as neutral, but it had measured it at
//     the TAIL, after the inner loop, where it is.
//   * `switch (mTracking)` with `case kSkeletonNotTracked: Init()` and
//     `case kSkeletonTracked:` instead of the if/else-if pair: 95.9 -> 96.5.
//     The image's dispatch is `cmpwi r11, 0x0` on cr0 (0x82436B1C) / `beq` at
//     0x82436B24 then `cmpwi cr6, r11, 0x2` / `bne`; the nested ifs put
//     both compares on cr6.  Same behaviour (kSkeletonPositionOnly does
//     nothing on both spellings).
//   * a `const Vector3 *jointPositions` local declared AFTER the
//     MakeCameraToPlayerXfm loop (whose args stay inline) and used inside
//     the joint loop: 96.1 -> 97.5.  Declared BEFORE that loop it is 96.5
//     (the image materialises floorNormal, then data+0x144, then mPlayerXfms
//     -- the call's right-to-left argument order -- after `li r29, 1`, so
//     the pointer is not a pre-loop local); with no local at all MSVC
//     derives &mJointPositions[i] from the mRawPositions walker
//     (`addi r26, r29, 0x140`), 96.1.
// NEGATIVE RESULTS (w7-br), all exactly inert or worse: a matching
// `const Vector3 *rawPositions` local for the tail copy (91.1 -- MSVC then
// walks the raw pointer AND re-derives the joint address from it); binding
// `const Vector3 &pos = jointPositions[i];` at the head instead of the two
// inline subscripts (identical 49 rows); `if (mTracking)` for the dispatch
// (identical to the != spelling).
// The three MakeString rows in the Function Call Diff are ICF folds (the assert
// format strings), not wrong callees.  Control flow is faithful: the `beq` at
// 0x82436B24 goes to Init() and the `bne` two instructions later returns.
void Skeleton::Poll(int skel_idx, const SkeletonFrame &frame) {
    MILO_ASSERT((0) <= (skel_idx) && (skel_idx) < (6), 0x1F8);
    if (mSkeletonIdx != skel_idx && TheGestureMgr) {
        IdentityInfo *identityInfo = TheGestureMgr->GetIdentityInfo(skel_idx);
        MILO_ASSERT(identityInfo, 0x1FC);
        identityInfo->Reset(skel_idx);
    }

    mSkeletonIdx = skel_idx;
    mElapsedMs = frame.mElapsedMs;
    const SkeletonData &data = frame.mSkeletonDatas[skel_idx];
    mTrackingID = data.mTrackingID;
    unkab0 = data.mHipCenter;
    mTracking = data.mTracking;
    switch (mTracking) {
    case kSkeletonNotTracked:
        Init();
        break;
    case kSkeletonTracked: {
        mQualityFlags = data.mQualityFlags;
        if (TheGestureMgr) {
            IdentityInfo *identityInfo = TheGestureMgr->GetIdentityInfo(skel_idx);
            MILO_ASSERT(identityInfo, 0x211);
            if (identityInfo->EnrollmentIndex() != data.mClippedFlags) {
                identityInfo->SetEnrollmentIndex(data.mClippedFlags);
            }
        }

        for (int i = 1; i < kNumCoordSys; i++) {
            BaseSkeleton::MakeCameraToPlayerXfm(
                (SkeletonCoordSys)i,
                mPlayerXfms[i - 1],
                (const Vector3 *)data.mJointPositions,
                frame.mFloorNormal
            );
        }

        // The inner loop must keep the `mTrackedJoints[i].mJointPos[j]`
        // subscript rather than go through `tj`: the image re-forms that
        // address from `this` every iteration -- `slwi r11, r31, 4` /
        // `add r11, r11, r29` (i*0x74) / `add r11, r11, r30` (this) /
        // `addi r5, r11, 0x4` at 0x82436BF0 -- and that is what keeps
        // i*0x74 an offset IV.  `tj` itself is bound here, at the head,
        // so that `&mTrackedJoints[i]` is `add r28, r24, r29` (r24 = this+4)
        // and stays live across the inner loop's calls.
        const Vector3 *jointPositions = (const Vector3 *)data.mJointPositions;
        for (int i = 0; i < kNumJoints; i++) {
            TrackedJoint &tj = mTrackedJoints[i];
            for (int j = 0; j < kNumCoordSys; j++) {
                Vector3 &dst = mTrackedJoints[i].mJointPos[j];
                if (j == 0) {
                    dst = jointPositions[i];
                } else {
                    MultiplyTranspose(jointPositions[i], mPlayerXfms[j - 1], dst);
                }
            }
            tj.mJointConf = (JointConfidence)data.mJointTrackingState[i];
            tj.mSmoothedPos = data.mRawPositions[i];
        }

        for (int i = 0; i < kNumBones; i++) {
            mCamBoneLengths[i] = BaseSkeleton::BoneLength((SkeletonBone)i, kCoordCamera);
        }

        mCamDisplacements.clear();

        Vector4 clipPlane = frame.mFloorClipPlane;
        unkac4 = (mTrackedJoints[kJointHipRight].mJointPos[kCoordCamera].y
                  + mTrackedJoints[kJointHipLeft].mJointPos[kCoordCamera].y)
            * 0.5f + clipPlane.w;
        break;
    }
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

    DWORD flags = 1;
    if (enrollmentIdx != -1) {
        flags = 0x21;
    }
    if (enrollmentIdx == -3) {
        enrollmentIdx = -2;
    }

    HRESULT hr = NuiIdentityEnroll(mTrackingID, enrollmentIdx, flags, IdentityCallback, info);
    MILO_ASSERT(hr != E_INVALIDARG, 0x26E);

    if (hr < 0) {
        if (hr != (HRESULT)0x8000000A) {
            return false;
        }
    }

    if (hr == 0) {
        info->SetIdentified(true);
    } else {
        GestureMgr::sIdentityOpInProgress = true;
    }
    return true;
}

int Skeleton::IdentityCallback(void *pvContext, NUI_IDENTITY_MESSAGE *pMessage) {
    IdentityInfo *info = (IdentityInfo *)pvContext;
    MILO_ASSERT(pvContext != NULL, 0x280);
    MILO_ASSERT(pMessage != NULL, 0x281);

    switch (pMessage->MessageId) {
    case NUI_IDENTITY_MESSAGE_ID_FRAME_PROCESSED:
        break;
    case NUI_IDENTITY_MESSAGE_ID_COMPLETE:
        info->SetIdentified(true);
        info->SetProfileMatched(pMessage->Data.Complete.bProfileMatched != 0);
        if ((unsigned int)info->EnrollmentIndex()
            != pMessage->Data.Complete.dwEnrollmentIndex) {
            info->SetEnrollmentIndex(pMessage->Data.Complete.dwEnrollmentIndex);
        }
        break;
    default:
        MILO_ASSERT(false, 0x297);
        break;
    }

    if (!TheGestureMgr->IDEnabled()) {
        MILO_LOG("An identification operation that was in progress was canceled.\n");
        GestureMgr::sIdentityOpInProgress = false;
        return 0;
    }
    return 1;
}
