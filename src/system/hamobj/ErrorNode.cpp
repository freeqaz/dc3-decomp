#include "hamobj/ErrorNode.h"
#include "ErrorNode.h"
#include "hamobj/CharFeedback.h"
#include "gesture/BaseSkeleton.h"
#include "hamobj/DancerSkeleton.h"
#include "math/Vec.h"
#include "obj/Data.h"
#include "os/Debug.h"

namespace {
    int gSkeletonJointToErrorJoint[kNumJoints] = {
        kErrorJointHipCenter,  kErrorJointSpine,        kErrorJointShoulderCenter,
        kErrorJointHead,       kErrorJointShoulderLeft, kErrorJointElbowLeft,
        kErrorJointWristLeft,  kErrorJointHandLeft,     kErrorJointShoulderRight,
        kErrorJointElbowRight, kErrorJointWristRight,   kErrorJointHandRight,
        kErrorJointHipLeft,    kErrorJointKneeLeft,     kErrorJointAnkleLeft,
        kErrorJointHipRight,   kErrorJointKneeRight,    kErrorJointAnkleRight,
        kErrorJointFootLeft,   kErrorJointFootRight
    };
}

ErrorFrameInput::ErrorFrameInput(
    const SkeletonHistory *history,
    const DancerSkeleton &dancerSkeleton,
    const BaseSkeleton &baseSkeleton,
    float f1
)
    : mSkeleton(dancerSkeleton), mBaseSkeleton(baseSkeleton) {
    dancerSkeleton.CamBoneLengths(mBoneLengths);
    baseSkeleton.CamBoneLengths(mBaseBoneLengths);
    dancerSkeleton.CamJointPositions(mJointPositions);
    baseSkeleton.CamJointPositions(mBaseJointPositions);
    mDisplacements = false;
    int elapsedMs = dancerSkeleton.ElapsedMs();
    if (elapsedMs != -1) {
        int div = (int)(elapsedMs / f1);
        mDisplacements =
            baseSkeleton.Displacements(history, kCoordCamera, div, mBaseJointDisps, div);
    }
    dancerSkeleton.CamJointDisplacements(mJointDisps);
}

void ErrorNodeInput::Set(const Vector3 &v, const Ham1NodeWeight *w) {
    mNodeComponentWeight = v;
    mNodeWeight = w;
}

#pragma region ErrorNode

ErrorNode::ErrorNode(ErrorNodeType e, const DataArray *cfg)
    : mType(e), mXErrorAxis(kJointHipCenter), mZErrorAxis(kJointHipCenter) {
    mNodeName = cfg->Sym(0);
    static Symbol joint("joint");
    static Symbol feedback_limbs("feedback_limbs");
    static Symbol xz_error_axis("xz_error_axis");
    mJoint = (SkeletonJoint)cfg->FindInt(joint);
    DataArray *limbsArr = cfg->FindArray(feedback_limbs, true);
    mFeedbackLimbs = kFeedbackNone;
    for (int i = 1; i < limbsArr->Size(); i++) {
        mFeedbackLimbs |= (FeedbackLimbs)limbsArr->Int(i);
    }
    DataArray *axisArr = cfg->FindArray(xz_error_axis, false);
    if (axisArr) {
        mXErrorAxis = (SkeletonJoint)axisArr->Int(1);
        mZErrorAxis = (SkeletonJoint)axisArr->Int(2);
    } else {
        mXErrorAxis = mZErrorAxis = kNumJoints;
    }
}

void ErrorNode::NormBoneLengths(
    const ErrorFrameInput &input,
    const SkeletonBone (&bones)[kMaxNumNormBones],
    float &totalBoneLengths,
    float &totalBaseBoneLengths
) const {
    totalBoneLengths = 0;
    totalBaseBoneLengths = 0;
    for (int i = 0; i < 3; i++) {
        SkeletonBone curBone = bones[i];
        if (curBone == kNumBones)
            return;
        totalBoneLengths += input.mBoneLengths[curBone];
        totalBaseBoneLengths += input.mBaseBoneLengths[curBone];
    }
}

bool ErrorNode::IsTypeJointMatch(int joint) const {
    return mType & joint && gSkeletonJointToErrorJoint[mJoint] & joint;
}

void ErrorNode::InitNormBones(
    const DataArray *cfg, SkeletonBone (&skelBones)[kMaxNumNormBones]
) {
    static Symbol norm_bones("norm_bones");
    DataArray *bones = cfg->FindArray(norm_bones, true);
    MILO_ASSERT(bones->Size() - 1 <= kMaxNumNormBones, 0x95);
    for (int i = 0; i < kMaxNumNormBones; i++) {
        if (i < bones->Size() - 1) {
            skelBones[i] = (SkeletonBone)bones->Int(i + 1);
        } else {
            skelBones[i] = kNumBones;
        }
    }
}

bool ErrorNode::XZErrorAxis(Vector3 &v, const DancerSkeleton &skeleton) const {
    if (mXErrorAxis == kNumJoints) {
        return false;
    } else {
        Subtract(skeleton.CamJointPos(mXErrorAxis), skeleton.CamJointPos(mZErrorAxis), v);
        return true;
    }
}

ErrorNode *ErrorNode::Create(const DataArray *cfg) {
    ErrorNodeType type = (ErrorNodeType)cfg->FindInt("type");
    if (type == kErrorHam1Euclidean) {
        return new Ham1EuclideanNode(type, cfg);
    } else if (type == kErrorHam1Displacement) {
        return new Ham1DisplacementNode(type, cfg);
    } else if (type == kErrorDisplacement) {
        return new DisplacementNode(type, cfg);
    } else if (type == kErrorPosition) {
        return new PositionNode(type, cfg);
    } else {
        MILO_FAIL("Could not create node of type %i", type);
        return nullptr;
    }
}

#pragma endregion
#pragma region Ham1EuclideanNode

Ham1EuclideanNode::Ham1EuclideanNode(ErrorNodeType e, const DataArray *cfg)
    : ErrorNode(e, cfg) {
    for (int i = 0; i < 3; i++) {
        // 0x24, 0x28 i = 0
        // 0x2c, 0x30 i = 1
        // 0x34, 0x38 i = 2
        mComponentWeightRanges[i][0] = 0;
        mComponentWeightRanges[i][1] = 0;
    }
    static Symbol coord_sys("coord_sys");
    mCoordSys = (SkeletonCoordSys)cfg->FindInt(coord_sys);
    static Symbol base_joint("base_joint");
    mBaseJoint = (SkeletonJoint)cfg->FindInt(base_joint);
    static Symbol component_weight_ranges("component_weight_ranges");
    DataArray *weightArr = cfg->FindArray(component_weight_ranges);
    for (int i = 1; i < 4; i++) {
        DataArray *arr = weightArr->Array(i);
        mComponentWeightRanges[i - 1][0] = arr->Float(0);
        mComponentWeightRanges[i - 1][1] = arr->Float(1);
    }
}

void Ham1EuclideanNode::CalcError(
    const ErrorFrameInput &frame_input, const ErrorNodeInput &node_input, Vector3 &vout
) const {
    MILO_ASSERT(node_input.mNodeWeight, 0x10E);
    Vector3 dancerVec;
    frame_input.mSkeleton.NormPos(mCoordSys, mJoint, dancerVec);
    Vector3 baseVec;
    frame_input.mBaseSkeleton.NormPos(mCoordSys, mJoint, baseVec);
    Vector3 diff;
    Subtract(dancerVec, baseVec, diff);
    Vector3 vToProcess;
    for (int i = 0; i < 3; i++) {
        float set = Max(mComponentWeightRanges[0][i], node_input.mNodeComponentWeight[i]);
        vToProcess[i] = Min(set, mComponentWeightRanges[1][i]);
    }
    ScaleOp op;
    op.mPerfectDist = node_input.mNodeWeight->mPerfectDist;
    op.mType = kErrorScaleDistSq;
    op.mRate = node_input.mNodeWeight->mRate;
    vToProcess *= diff;
    vout.x = ScaleDistToError(op, Length(vToProcess));
}

#pragma endregion
#pragma region BaseDisplacementNode

BaseDisplacementNode::BaseDisplacementNode(ErrorNodeType e, const DataArray *cfg)
    : ErrorNode(e, cfg) {
    static Symbol base_joint("base_joint");
    DataArray *jointArr = cfg->FindArray(base_joint, false);
    if (jointArr) {
        mBaseJoint = (SkeletonJoint)jointArr->Int(1);
    } else {
        mBaseJoint = mJoint;
    }
    InitNormBones(cfg, mNormBones);
}

bool BaseDisplacementNode::Displacements(
    const ErrorFrameInput &frame_input, BaseDisplacementNode::DisplacementData &dispData
) const {
    if (frame_input.mDisplacements) {
        dispData.mJointDisplacement = frame_input.mJointDisps[mJoint];
        dispData.mBaseJointDisplacement = frame_input.mBaseJointDisps[mJoint];
        if (mBaseJoint != mJoint) {
            Subtract(
                dispData.mJointDisplacement,
                frame_input.mJointDisps[mBaseJoint],
                dispData.mJointDisplacement
            );
            Subtract(
                dispData.mBaseJointDisplacement,
                frame_input.mBaseJointDisps[mBaseJoint],
                dispData.mBaseJointDisplacement
            );
        }
        float boneLen, baseBoneLen;
        NormBoneLengths(frame_input, mNormBones, boneLen, baseBoneLen);
        if (baseBoneLen > 0) {
            Scale(
                dispData.mBaseJointDisplacement,
                boneLen / baseBoneLen,
                dispData.mBaseJointDisplacement
            );
            return true;
        }
    }
    dispData.mBaseJointDisplacement.Zero();
    dispData.mJointDisplacement.Zero();
    return false;
}

bool BaseDisplacementNode::Displacements(
    const ErrorFrameInput &frame_input,
    DisplacementData &dispData,
    Ham1DisplacementData &ham1Data
) const {
    bool ok = Displacements(frame_input, dispData);
    // Ham1-style: compute angle-based displacement data
    if (ok) {
        Vector3 &jd = dispData.mJointDisplacement;
        Vector3 &bjd = dispData.mBaseJointDisplacement;
        float jdLen = Length(jd);
        float bjdLen = Length(bjd);
        ham1Data.unk0 = bjdLen > 0 ? (jdLen / bjdLen) : 1.0f;
        if (jdLen > 0.0001f && bjdLen > 0.0001f) {
            Normalize(jd, ham1Data.unk4);
            ham1Data.unk14 = true;
            ham1Data.unk18 = Dot(jd, bjd) / (jdLen * bjdLen);
            ham1Data.unk1c = acosf(Clamp(-1.0f, ham1Data.unk18, 1.0f));
        } else {
            ham1Data.unk4.Zero();
            ham1Data.unk14 = false;
            ham1Data.unk18 = 1.0f;
            ham1Data.unk1c = 0.0f;
        }
    } else {
        ham1Data.unk0 = 1.0f;
        ham1Data.unk4.Zero();
        ham1Data.unk14 = false;
        ham1Data.unk18 = 1.0f;
        ham1Data.unk1c = 0.0f;
    }
    return ok;
}

void DistanceToErrors(const Vector3 &a, const Vector3 &b, const Vector3 &c, Vector3 &d) {
    Subtract(a, b, d);

    d.x *= c.x;
    d.y *= c.y;
    d.z *= c.z;

    for (int j = 0; j < 3; ++j) {
        float x = fabs(d[j]);
        float y = -x;
        float z = (y >= 0.0f) ? 0.0f : x;
        float w = (z - 1.0f >= 0.0f) ? 1.0f : z;
        d[j] = w;
    }
}

void DisplacementNode::CalcError(
    const ErrorFrameInput &frame_input, const ErrorNodeInput &node_input, Vector3 &vout
) const {
    MILO_ASSERT(node_input.mNodeWeight == NULL, 0x1F8);
    DisplacementData dispData;
    if (Displacements(frame_input, dispData)) {
        DistanceToErrors(
            dispData.mJointDisplacement,
            dispData.mBaseJointDisplacement,
            node_input.mNodeComponentWeight,
            vout
        );
    } else {
        vout.Set(1, 1, 1);
    }
}

Ham1DisplacementNode::Ham1DisplacementNode(ErrorNodeType e, const DataArray *cfg)
    : BaseDisplacementNode(e, cfg) {
    static Symbol potential_angle_op("potential_angle_op");
    mPotentialAngleOp.Set(cfg->FindArray(potential_angle_op));
}

void Ham1DisplacementNode::CalcError(
    const ErrorFrameInput &frame_input, const ErrorNodeInput &node_input, Vector3 &vout
) const {
    MILO_ASSERT(node_input.mNodeWeight, 0x19D);
    ErrorData errData;
    DisplacementData dispData;
    Ham1DisplacementData ham1DispData;
    Errors(frame_input, node_input, errData, dispData, ham1DispData);
    vout.x = errData.unk4 * errData.unk8 + errData.unk0;
}

void Ham1DisplacementNode::Errors(
    const ErrorFrameInput &frame_input,
    const ErrorNodeInput &node_input,
    ErrorData &errData,
    DisplacementData &dispData,
    Ham1DisplacementData &ham1Data
) const {
    MILO_ASSERT(node_input.mNodeWeight, 0x190);
    bool ok = Displacements(frame_input, dispData, ham1Data);
    if (!ok) {
        errData.unk0 = 0;
        errData.unk4 = 0;
        errData.unk8 = 0;
        return;
    }
    // Compute scale based on potential angle
    float angleDist = ScaleDistToError(mPotentialAngleOp, ham1Data.unk1c);
    // Compute magnitude error using Ham1NodeWeight
    const Ham1NodeWeight *nw = node_input.mNodeWeight;
    float jdLen = Length(dispData.mJointDisplacement);
    float bjdLen = Length(dispData.mBaseJointDisplacement);
    // Scale base by bone length ratio
    float lenRatio = ham1Data.unk0;
    ScaleOp op1;
    op1.mType = kErrorScaleDistSq;
    op1.mPerfectDist = nw->mPerfectDist;
    op1.mRate = nw->mRate;
    float err1 = ScaleDistToError(op1, fabs(jdLen - bjdLen * lenRatio));
    ScaleOp op2;
    op2.mType = kErrorScaleDistSq;
    op2.mPerfectDist = nw->mPerfectDist2;
    op2.mRate = nw->mRate2;
    float err2 = ScaleDistToError(op2, fabs(ham1Data.unk1c));
    errData.unk0 = angleDist;
    errData.unk4 = err1;
    errData.unk8 = err2;
}

PositionNode::PositionNode(ErrorNodeType e, const DataArray *cfg) : ErrorNode(e, cfg) {
    static Symbol base_joint("base_joint");

    mBaseJoint = (SkeletonJoint)cfg->FindInt(base_joint);
    InitNormBones(cfg, mNormBones);
}

void PositionNode::CalcError(
    const ErrorFrameInput &frame_input, const ErrorNodeInput &node_input, Vector3 &vout
) const {
    MILO_ASSERT(node_input.mNodeWeight == NULL, 0x21C);
    Vector3 jointDiff;
    Vector3 baseJointDiff;
    float desired_bone_len;
    float base_bone_len;
    Vector3 scaledBaseDiff;

    Subtract(
        frame_input.mBaseJointPositions[mJoint],
        frame_input.mBaseJointPositions[mBaseJoint],
        baseJointDiff
    );
    Subtract(
        frame_input.mJointPositions[mJoint],
        frame_input.mJointPositions[mBaseJoint],
        jointDiff
    );
    NormBoneLengths(frame_input, mNormBones, desired_bone_len, base_bone_len);
    MILO_ASSERT(desired_bone_len > 0, 0x22C);

    if (base_bone_len <= 0) {
        vout.Set(1, 1, 1);
    } else {
        Scale(baseJointDiff, desired_bone_len / base_bone_len, scaledBaseDiff);
        DistanceToErrors(jointDiff, scaledBaseDiff, node_input.mNodeComponentWeight, vout);
    }
}

float ScaleFullErrorDist(const ScaleOp &op) {
    if (op.mPerfectDist == -1.0f)
        return 1.0f;
    float invRate;
    if (op.mType == kErrorScaleDist) {
        invRate = 1.0f / op.mRate;
    } else {
        invRate = sqrtf(1.0f / op.mRate);
    }
    return invRate + op.mPerfectDist;
}

void ScaleOp::Set(const DataArray *cfg) {
    static Symbol type("type");
    static Symbol rate("rate");
    static Symbol perfect_dist("perfect_dist");
    mType = (ErrorScaleType)cfg->FindInt(type);
    cfg->FindData(perfect_dist, mPerfectDist);
    cfg->FindData(rate, mRate);
}
