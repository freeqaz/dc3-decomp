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
    Vector3 v;
    BoneVec(bone, cs, v);
    return Length(v);
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

// RESIDUAL (w7-bl, 98.28 canonical, 5 of 175 rows; w7-br held): two register-allocator
// decisions, both downstream of identical arithmetic.
//  (1) `bone1` -- the image leaves the ternary's result in the scratch r7 in
//      BOTH branches (`addi r7, r7, 0xb` / `addi r7, r7, 0x6`) and pays a
//      `mr r4, r7` at the BoneLength call site; MSVC here coalesces that move
//      into the addi (`addi r4, r7, 0xb`), so our code is ONE instruction
//      shorter (692 vs 696 bytes).  r4 is only clobbered on the MILO_FAIL path,
//      which branches away before the call, so the coalesce is legal for both.
//      All six sibling ternaries land in the same registers as the image.
//  (2) The second `xori rX, rY, 0x1` (bone3's negated side) is scheduled four
//      slots later in the image, after the three clrlwi/clrrwi; MSVC packs the
//      two xori adjacently.  Same instructions, same registers, same order of
//      the seven `addi` results (indices 60-66).
// NEGATIVE RESULT (w7-br): computing bone3 before bone1/bone2 (so its xori
// temp is dead before bone1's addi) is 98.3 -> 98.2, 15 rows: the two xori
// swap clrlwi/clrrwi partners and all three bone addi's recolour, and the
// `mr r4, r7` at 0x8243459C is still not coalesced.  Both branches of the
// image define bone1 in r7 and every other ternary result lands in the
// register we use, so the un-coalesced move is a hint the allocator did not
// take, not a live range we can shorten from source.
// Both MakeString rows in the Function Call Diff are ICF folds (the assert
// format string and the "Unsupported joint %i" one), not wrong callees.
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

// RESIDUAL (w7-br, 95.9 canonical, 44 of 222 rows; w7-bl left it at 95.70/45):
// two scheduling residuals, both inside the joint-copy blocks.
//  (1) The kUnk5 branch is 3 instructions SHORT because MSVC cross-jumps our
//      `limbDir.x -= nearJoint.x` into the arm/leg tail (our `b` lands on the
//      shared `lfs 0x60 / lfs 0x80 / fsubs f13` pair instead of on the store
//      block).  It merges because our two branches assign the same FPRs to x;
//      the image's do not (kUnk5 `fsubs f13, f10, f13` vs arm/leg
//      `fsubs f13, f12, f13`), so it emits all three subtractions inline and
//      jumps straight to the stfs triple.  No source spelling reached that.
//  (2) The image loads nearJoint's three components BEFORE limbDir's and
//      round-robins the three 16-byte joint copies limb/near/origin; we
//      complete near+origin first.  Pure store scheduling -- same set, same
//      slots (limbDir 0x60, upDir 0x70, nearJoint/crossDir 0x80, origin 0x90).
// Lever that DID pay (0.7pp, 3 rows): the kUnk5 subtraction is written z,y,x
// below -- that order is what the image emits, and writing it x,y,z made MSVC
// spill two intermediates to 0x60/0x68 inside the branch (2 extra stfs).
// Failed spellings, all measured in this worktree:
//   - `Subtract(limbDir, nearJoint, limbDir)` in all three branches: byte-inert
//     (95.00, identical 37/1/6/4 row split) -- MSVC canonicalises Set() back to
//     three component subtractions.
//   - assigning limbDir before nearJoint in all three branches: same 95.70 but
//     48->63 rows, and it moves nearJoint off the 0x80 slot (`addi r4, r31,
//     0xc0` picks up a +48 offset diff at index 48).
//   - arm/leg subtraction reordered z,x,y to match the image's emission order
//     there: same 95.70, 45->48 rows.
// w7-br: the arm/leg subtraction is written y,x,z below.  The image computes
// z (the branch-selected `fsubs f0, f0, f0` at 0x82434270), then x
// (`fsubs f13, f12, f13` at 0x82434278), then y (0x82434284); with x,y,z in
// the source MSVC emitted z, y, x and with z,x,y (w7-bl) 48 rows.  y,x,z gives
// the image's z, x, y emission: 95.70 -> 95.9, and the kUnk5 block stops
// cross-jumping into the x subtraction (its `b` now lands on the stfs triple
// at 0x82434288 like the image's) at the cost of the x/y fsubs register
// colouring and two moved stores.  NEGATIVE RESULTS (w7-br): kUnk5 in x,y,z
// with the arm/leg y,x,z is 95.7 (47 rows); `Vector3 nearJoint;` declared
// first and assigned AFTER limbDir in all three branches is 95.7 with 63
// rows -- the image's dead `addi r4, r31, 0xc0` at 0x824340F8 (the
// PaddedJointPos -> const Vector3& conversion of pj[kJointHipLeft]) moves
// to +0xf0, which pins nearJoint = pj[HipLeft] as the FIRST statement of
// the kUnk5 block.  What is left is which 16-byte copy's stores the
// scheduler issues first (image: limb/near/origin round-robin from
// 0x8243422C; ours: near+origin, then limb) and, downstream of that, which
// of limb.{x,y} / near.{x,y} is loaded first for the subtraction.
// The MakeString<char const(&)[13], int const&, char const(&)[5]> vs our
// <[11], int const&, [49]> in the Function Call Diff is an ICF fold -- both
// sides load the SAME two string symbols at indices 14/15 -- not a wrong callee.
void BaseSkeleton::MakeCameraToPlayerXfm(
    SkeletonCoordSys cs,
    Transform &xfm,
    const Vector3 *joints,
    const Vector3 &floorNormal
) {
    MILO_ASSERT((kCoordLeftArm) <= (cs) && (cs) < (kNumCoordSys), 0x14F);

    const PaddedJointPos *pj = (const PaddedJointPos *)joints;

    // Two working vectors, each used for two things in turn: limbDir starts out
    // holding the far joint and becomes (far - near) in place, and crossDir
    // starts out holding the near joint and is then overwritten by the cross
    // product.  The shipped code has exactly four Vector3 frame slots here.
    Vector3 limbDir;
    Vector3 upDir = floorNormal;
    Normalize(upDir, upDir);

    Vector3 crossDir;
    Vector3 origin;

    if (cs == kCoordLeftArm || cs == kCoordRightArm) {
        int originIdx = (cs == kCoordLeftArm) ? kJointShoulderLeft : kJointShoulderRight;
        // The near joint is a block-scoped working copy: it shares the frame word
        // with crossDir, whose first definition is the Cross() output below.
        Vector3 nearJoint = pj[kJointShoulderLeft];
        limbDir = pj[kJointShoulderRight];
        origin = pj[originIdx];
        // Both shoulders are forced to a common depth, so the limb direction is
        // always flat in z; which joint's z wins depends on the side.
        if (cs == kCoordLeftArm)
            limbDir.z = nearJoint.z;
        else
            nearJoint.z = limbDir.z;
        limbDir.y -= nearJoint.y;
        limbDir.x -= nearJoint.x;
        limbDir.z -= nearJoint.z;
    } else if (cs == kCoordLeftLeg || cs == kCoordRightLeg) {
        int originIdx = (cs == kCoordLeftLeg) ? kJointHipLeft : kJointHipRight;
        Vector3 nearJoint = pj[kJointHipLeft];
        limbDir = pj[kJointHipRight];
        origin = pj[originIdx];
        if (cs == kCoordLeftLeg)
            limbDir.z = nearJoint.z;
        else
            nearJoint.z = limbDir.z;
        limbDir.y -= nearJoint.y;
        limbDir.x -= nearJoint.x;
        limbDir.z -= nearJoint.z;
    } else if (cs == kUnk5) {
        Vector3 nearJoint = pj[kJointHipLeft];
        limbDir = pj[kJointHipRight];
        origin = pj[kJointHipCenter];
        limbDir.z -= nearJoint.z;
        limbDir.y -= nearJoint.y;
        limbDir.x -= nearJoint.x;
    }

    Normalize(limbDir, limbDir);

    Cross(upDir, limbDir, crossDir);
    Normalize(crossDir, crossDir);

    Hmx::Matrix3 mat;
    mat.x = limbDir;
    mat.y = upDir;
    mat.z = crossDir;
    xfm.m = mat;
    xfm.v = origin;
}
