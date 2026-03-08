#include "hamobj/HamSkeletonConverter.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/JointUtl.h"
#include "gesture/Skeleton.h"
#include "gesture/SkeletonUpdate.h"
#include "hamobj/HamCharacter.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Poll.h"
#include "rndobj/Rnd.h"
#include "rndobj/Trans.h"
#include "rndobj/Utl.h"
#include "utl/Str.h"

HamSkeletonConverter::HamSkeletonConverter()
    : mBones(this), unk28(0), mCharacter(this), mIsActive(0), unk751(0), unk754(0) {}

HamSkeletonConverter::~HamSkeletonConverter() {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    if (handle.HasCallback(this)) {
        handle.RemoveCallback(this);
    }
}

BEGIN_HANDLERS(HamSkeletonConverter)
    HANDLE_ACTION(run_test, 0)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamSkeletonConverter)
    SYNC_PROP(bones, mBones)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HamSkeletonConverter)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mBones;
END_SAVES

BEGIN_COPYS(HamSkeletonConverter)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(HamSkeletonConverter)
    BEGIN_COPYING_MEMBERS
        mBones = c->mBones.Ptr();
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(2, 0)

BEGIN_LOADS(HamSkeletonConverter)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mBones;
END_LOADS

void HamSkeletonConverter::SetName(const char *name, ObjectDir *dir) {
    Hmx::Object::SetName(name, dir);
    mCharacter = dynamic_cast<HamCharacter *>(dir);
}

void HamSkeletonConverter::Enter() {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    if (!handle.HasCallback(this)) {
        handle.AddCallback(this);
    }
    mPelvisMesh = mCharacter->Find<RndTransformable>("bone_pelvis.mesh", true);
    mPelvisInitialZ = mPelvisMesh->LocalXfm().v.z;
    mBoneMeshes.clear();
    mBoneMeshes.resize(kNumJoints);
    for (int i = 0; i < kNumJoints; i++) {
        RndTransformable *t =
            mCharacter->Find<RndTransformable>(MirrorBoneName((SkeletonJoint)i), true);
        mBoneMeshes[i] = t;
    }
    Vector3 z = mBoneMeshes[kJointHipLeft]->WorldXfm().m.z;
    mLeftHipZAxis = z;
    z = mBoneMeshes[kJointHipRight]->WorldXfm().m.z;
    mRightHipZAxis = z;
    mLeftHipZAxisInit = mLeftHipZAxis;
    mRightHipZAxisInit = mRightHipZAxis;
}

void HamSkeletonConverter::Exit() {
    RndPollable::Exit();
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    if (handle.HasCallback(this)) {
        handle.RemoveCallback(this);
    }
}

void HamSkeletonConverter::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    change.push_back(mBones);
}

void HamSkeletonConverter::Highlight() {
    for (int i = 0; i < kNumJoints; i++) {
        Vector3 curV = mJointPositions[i];
        UtilDrawSphere(curV, 1, Hmx::Color(0, 0, 1), nullptr);
        Transform curXfm = mBoneTransforms[i];

        Vector3 scaledX;
        Scale(curXfm.m.x, 4.0f, scaledX);
        Vector3 scaledY;
        Scale(curXfm.m.y, 4.0f, scaledY);
        Vector3 scaledZ;
        Scale(curXfm.m.z, 4.0f, scaledZ);

        Vector3 scaled;
        Add(scaledX, curXfm.v, scaled);
        TheRnd.DrawLine(curXfm.v, scaledX, Hmx::Color(1, 0, 0), false);
        Add(scaledY, curXfm.v, scaled);
        TheRnd.DrawLine(curXfm.v, scaled, Hmx::Color(0, 1, 0), false);
        Add(scaledZ, curXfm.v, scaled);
        TheRnd.DrawLine(curXfm.v, scaled, Hmx::Color(0, 0, 1), false);
    }
}

void HamSkeletonConverter::PostUpdate(const SkeletonUpdateData *data) {
    if (mIsActive && data) {
        BaseSkeleton *skeleton = nullptr;
        for (int i = 0; i < 6; i++) {
            if (data->mSkeletonsLeft[i] && data->mSkeletonsLeft[i]->IsTracked()) {
                skeleton = data->mSkeletonsLeft[i];
                break;
            }
        }
        Set(skeleton);
    }
}

void HamSkeletonConverter::GetParentWorldXfm(
    RndTransformable *t, Transform &xfm, SkeletonJoint parent
) {
    RndTransformable *meshParent = t->TransParent();
    if (streq(meshParent->Name(), "bone_pelvis.mesh")) {
        xfm.m = mPelvisTransform.m;
        xfm.v = mPelvisTransform.v;
    } else if (IsSkeletonBone(meshParent->Name())) {
        MILO_ASSERT(streq(meshParent->Name(), CharBoneName(parent)), 0x2B2);
        xfm.m = mBoneTransforms[parent].m;
        xfm.v = mBoneTransforms[parent].v;
    } else {
        GetParentWorldXfm(meshParent, xfm, parent);
        Multiply(meshParent->LocalXfm(), xfm, xfm);
    }
}

void HamSkeletonConverter::SetQuatBoneValue(String s, Hmx::Quat q) {
    String str(s);
    if (str.find(".mesh") != FixedString::npos) {
        str = str.substr(0, s.length() - 5);
    }
    str += ".quat";
    Hmx::Quat *qPtr = (Hmx::Quat *)mBones->FindPtr(str.c_str());
    // this is stupid but hey if it matches lmao
    qPtr->w = q.w;
    qPtr->x = q.x;
    qPtr->y = q.y;
    qPtr->z = q.z;
}

void HamSkeletonConverter::SetRotzBoneValue(String s, float r) {
    String str(s);
    if (str.find(".mesh") != FixedString::npos) {
        str = str.substr(0, s.length() - 5);
    }
    str += ".rotz";
    float *rPtr = (float *)mBones->FindPtr(str.c_str());
    *rPtr = r;
}

void HamSkeletonConverter::SetPosBoneValue(String s, Vector3 v) {
    String str(s);
    if (str.find(".mesh") != FixedString::npos) {
        str = str.substr(0, s.length() - 5);
    }
    str += ".pos";
    Vector3 *vPtr = (Vector3 *)mBones->FindPtr(str.c_str());
    // this is stupid but hey if it matches lmao
    vPtr->x = v.x;
    vPtr->y = v.y;
    vPtr->z = v.z;
}

void HamSkeletonConverter::RotateTowards(
    const Vector3 &v1, const Vector3 &v2, float f, Vector3 &vout
) {
    if (v1 == v2)
        return;
    else {
        Hmx::Quat q50;
        q50.Reset();
        Hmx::Quat q40;
        MakeRotQuat(v1, v2, q40);
        float angle = acos(Dot(v1, v2));
        if (fabsf(angle) < 1e-9) {
            vout.x = v1.x;
            vout.y = v1.y;
            vout.z = v1.z;
        } else {
            float fabsed = fabsf(f / angle);
            if (fabsed < 1.0f) {
                Interp(q50, q40, fabsed, q40);
                Multiply(v1, q40, vout);
            } else {
                vout.x = v2.x;
                vout.y = v2.y;
                vout.z = v2.z;
            }
        }
    }
}
