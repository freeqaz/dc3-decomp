#include "gesture\BaseSkeleton.h"
#include "os\Debug.h"

const BoneJoints BaseSkeleton::sBones[] = {
    { kBoneHead, kJointHead, kJointShoulderCenter },
    { kBoneCollarRight, kJointShoulderCenter, kJointShoulderRight },
    { kBoneArmUpperRight, kJointShoulderRight, kJointElbowRight },
    { kBoneArmLowerRight, kJointElbowRight, kJointWristRight },
    { kBoneHandRight, kJointWristRight, kJointHandRight },
    { kBoneCollarLeft, kJointShoulderCenter, kJointShoulderLeft },
    { kBoneArmUpperLeft, kJointShoulderLeft, kJointElbowLeft },
    { kBoneArmLowerLeft, kJointElbowLeft, kJointWristLeft },
    { kBoneHandLeft, kJointWristLeft, kJointHandLeft },
    { kBoneLegUpperRight, kJointHipRight, kJointKneeRight },
    { kBoneLegLowerRight, kJointKneeRight, kJointAnkleRight },
    { kBoneLegUpperLeft, kJointHipLeft, kJointKneeLeft },
    { kBoneLegLowerLeft, kJointKneeLeft, kJointAnkleLeft },
    { kBoneBackUpper, kJointShoulderCenter, kJointSpine },
    { kBoneBackLower, kJointSpine, kJointHipCenter },
    { kBoneHipRight, kJointHipRight, kJointHipCenter },
    { kBoneHipLeft, kJointHipCenter, kJointHipLeft },
    { kBoneFootLeft, kJointAnkleLeft, kJointFootLeft },
    { kBoneFootRight, kJointAnkleRight, kJointFootRight }
};

const SkeletonJoint BaseSkeleton::sJointParents[] = {
    kNumJoints,           kJointHipCenter,     kJointSpine,      kJointShoulderCenter,
    kJointShoulderCenter, kJointShoulderLeft,  kJointElbowLeft,  kJointWristLeft,
    kJointShoulderCenter, kJointShoulderRight, kJointElbowRight, kJointWristRight,
    kJointHipCenter,      kJointHipLeft,       kJointKneeLeft,   kJointHipCenter,
    kJointHipRight,       kJointKneeRight,     kJointAnkleLeft,  kJointAnkleRight
};

void BaseSkeleton::CamJointPositions(Vector3 *positions) const {
    for (int i = 0; i < kNumJoints; i++) {
        JointPos(kCoordCamera, (SkeletonJoint)i, positions[i]);
    }
}

void BaseSkeleton::CamBoneLengths(float *lens) const {
    for (int i = 0; i < kNumBones; i++) {
        lens[i] = BoneLength((SkeletonBone)i, kCoordCamera);
    }
}

SkeletonJoint gMirrorJoints[kNumJoints] = {
    kJointHipCenter,     kJointSpine,      kJointShoulderCenter, kJointHead,
    kJointShoulderRight, kJointElbowRight, kJointWristRight,     kJointHandRight,
    kJointShoulderLeft,  kJointElbowLeft,  kJointWristLeft,      kJointHandLeft,
    kJointHipRight,      kJointKneeRight,  kJointAnkleRight,     kJointHipLeft,
    kJointKneeLeft,      kJointAnkleLeft,  kJointFootRight,      kJointFootLeft
};

SkeletonJoint BaseSkeleton::MirrorJoint(SkeletonJoint joint) {
    MILO_ASSERT((0) <= (joint) && (joint) < (kNumJoints), 0xC5);
    return gMirrorJoints[joint];
}

void BaseSkeleton::BoneVec(SkeletonBone bone, SkeletonCoordSys cs, Vector3 &vres) const {
    MILO_ASSERT((0) <= (bone) && (bone) < (kNumBones), 0xD1);
    MILO_ASSERT((0) <= (cs) && (cs) < (kNumCoordSys), 0xD2);
    Vector3 v1;
    JointPos(cs, sBones[bone].joint1, v1);
    Vector3 v2;
    JointPos(cs, sBones[bone].joint2, v2);
    Subtract(v2, v1, vres);
}

float BaseSkeleton::BoneLength(SkeletonBone bone, SkeletonCoordSys cs) const {
    // The image seeds on y*y, then fmadds x, then z (`lfs 0x54` / `fmuls` /
    // `lfs f13, 0x58` / `lfs f0, 0x50`).  Length(v) and LengthSquared(v) both
    // land one swap short (99.875, w8-i); a two-term seed plus a separate z
    // accumulate gives the image's association.
    Vector3 v;
    BoneVec(bone, cs, v);
    float lengthSq = v.y * v.y + v.x * v.x;
    lengthSq += v.z * v.z;
    return std::sqrt(lengthSq);
}

void BaseSkeleton::CalcNormalizedOffset(SkeletonJoint joint, Vector3 &vres) const {
    MILO_ASSERT((0) <= (joint) && (joint) < (kNumJoints), 0x13E);
    vres.Zero();
    SkeletonJoint parent = sJointParents[joint];
    if (parent != kNumJoints) {
        Vector3 v1;
        JointPos(kCoordCamera, joint, v1);
        Vector3 v2;
        JointPos(kCoordCamera, parent, v2);
        Subtract(v1, v2, vres);
        Normalize(vres, vres);
    }
}

void BaseSkeleton::NormOffset(SkeletonJoint joint, Vector3 &v) const {
    CalcNormalizedOffset(joint, v);
}

void BaseSkeleton::NormPos(SkeletonCoordSys cs, SkeletonJoint joint, Vector3 &v) const {
    Vector3 v40;
    JointPos(cs, joint, v40);
    LimbNormPos(cs, joint, true, v40, v);
}

// Lane history (53 lines moved): docs/decomp/notes/system/gesture/BaseSkeleton.md
// > RESIDUAL (w7-bl, 98.28 canonical, 5 of 175 rows; w7-br held): two register-allocator
// > decisions, both downstream of identical arithmetic.
void BaseSkeleton::LimbNormPos(
    SkeletonCoordSys cs,
    SkeletonJoint joint,
    bool normalize,
    const Vector3 &pos,
    Vector3 &result
) const {
    SkeletonJoint rootJoint;
    SkeletonJoint joint1;
    SkeletonJoint joint2;
    SkeletonJoint joint3;
    SkeletonBone bone1;
    SkeletonBone bone2;
    SkeletonBone bone3;

    if (cs == kCoordLeftArm || cs == kCoordRightArm) {
        bool right = cs == kCoordRightArm;
        rootJoint = right ? kJointShoulderRight : kJointShoulderLeft;
        joint1 = right ? kJointElbowRight : kJointElbowLeft;
        joint2 = right ? kJointWristRight : kJointWristLeft;
        joint3 = right ? kJointHandRight : kJointHandLeft;
        bone1 = right ? kBoneArmUpperRight : kBoneArmUpperLeft;
        bone2 = right ? kBoneArmLowerRight : kBoneArmLowerLeft;
        bone3 = right ? kBoneHandRight : kBoneHandLeft;
    } else {
        MILO_ASSERT(cs == kCoordLeftArm || cs == kCoordRightArm || cs == kCoordLeftLeg || cs == kCoordRightLeg, 0x10E);
        bool right = cs == kCoordRightLeg;
        rootJoint = right ? kJointHipRight : kJointHipLeft;
        joint1 = right ? kJointKneeRight : kJointKneeLeft;
        joint2 = right ? kJointAnkleRight : kJointAnkleLeft;
        joint3 = right ? kJointFootRight : kJointFootLeft;
        bone1 = right ? kBoneLegUpperRight : kBoneLegUpperLeft;
        bone2 = right ? kBoneLegLowerRight : kBoneLegLowerLeft;
        bone3 = right ? kBoneFootRight : kBoneFootLeft;
    }

    result = pos;
    if (joint != rootJoint) {
        if (joint == joint1 || joint == joint2 || joint == joint3) {
            float totalLength = BoneLength(bone1, kCoordCamera);
            if (joint == joint2 || joint == joint3) {
                totalLength += BoneLength(bone2, kCoordCamera);
                if (joint == joint3) {
                    totalLength += BoneLength(bone3, kCoordCamera);
                }
            }
            if (normalize && totalLength > 0.0f) {
                totalLength = 1.0f / totalLength;
            }
            result.x = result.x * totalLength;
            result.y = result.y * totalLength;
            result.z = result.z * totalLength;
        } else {
            MILO_FAIL("Unsupported joint %i", joint);
        }
    }
}

// Lane history (11 lines moved): docs/decomp/notes/system/gesture/BaseSkeleton.md
void BaseSkeleton::MakeCameraToPlayerXfm(
    SkeletonCoordSys cs,
    Transform &xfm,
    const Vector3 *joints,
    const Vector3 &floorNormal
) {
    MILO_ASSERT((kCoordLeftArm) <= (cs) && (cs) < (kNumCoordSys), 0x14F);

    const PaddedJointPos *pj = (const PaddedJointPos *)joints;

    Vector3 limbDir;
    Vector3 upDir = floorNormal;
    Normalize(upDir, upDir);

    Vector3 origin;
    if (cs == kCoordLeftArm || cs == kCoordRightArm) {
        origin = pj[(cs == kCoordLeftArm) ? kJointShoulderLeft : kJointShoulderRight];
        Vector3 nearJoint = pj[kJointShoulderLeft];
        Vector3 farJoint = pj[kJointShoulderRight];
        // Both shoulders are forced to a common depth, so the limb direction is
        // always flat in z; which joint's z wins depends on the side.
        if (cs == kCoordLeftArm)
            farJoint.z = nearJoint.z;
        else
            nearJoint.z = farJoint.z;
        limbDir.x = farJoint.x - nearJoint.x;
        limbDir.y = farJoint.y - nearJoint.y;
        limbDir.z = farJoint.z - nearJoint.z;
    } else if (cs == kCoordLeftLeg || cs == kCoordRightLeg) {
        origin = pj[(cs == kCoordLeftLeg) ? kJointHipLeft : kJointHipRight];
        Vector3 nearJoint = pj[kJointHipLeft];
        Vector3 farJoint = pj[kJointHipRight];
        if (cs == kCoordLeftLeg)
            farJoint.z = nearJoint.z;
        else
            nearJoint.z = farJoint.z;
        limbDir.x = farJoint.x - nearJoint.x;
        limbDir.y = farJoint.y - nearJoint.y;
        limbDir.z = farJoint.z - nearJoint.z;
    } else if (cs == kUnk5) {
        origin = pj[kJointHipCenter];
        Vector3 nearJoint = pj[kJointHipLeft];
        Vector3 farJoint = pj[kJointHipRight];
        limbDir.x = farJoint.x - nearJoint.x;
        limbDir.y = farJoint.y - nearJoint.y;
        limbDir.z = farJoint.z - nearJoint.z;
    }

    Normalize(limbDir, limbDir);

    Vector3 crossDir;
    Cross(upDir, limbDir, crossDir);
    Normalize(crossDir, crossDir);

    Hmx::Matrix3 mat;
    mat.x = limbDir;
    mat.y = upDir;
    mat.z = crossDir;
    xfm.m = mat;
    xfm.v = origin;
}
