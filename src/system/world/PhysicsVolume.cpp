#include "world/PhysicsVolume.h"
#include "obj/Data.h"
#include "os/Debug.h"
#include "world/PhysicsManager.h"
#include "math/Geo.h"
#include "math/Rot.h"
#include "math/Vec.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "rndobj/Draw.h"
#include "rndobj/Poll.h"
#include "rndobj/Trans.h"

namespace {
    Box gPhysicsVolumeBox;
}

PhysicsVolume::PhysicsVolume()
    : mDetectionVolume(nullptr), mShapeType(kPhysicsVolumeBox), mOverlapCount(0),
      mDirectionalForce(Vector3::ZeroVec()), mTangentialForce(Vector3::ZeroVec()),
      mDirectionalVelocity(Vector3::ZeroVec()), mRadialForce(0),
      mFilter(kCollidePhysicsVolumeDynamicFixed), mActive(true),
      mReportOnOverlaps(false) {}

PhysicsVolume::~PhysicsVolume() { DestroyPhysicsVolume(); }

BEGIN_HANDLERS(PhysicsVolume)
    HANDLE(set_directional_force, OnSetDirectionalForce)
    HANDLE(set_directional_velocity, OnSetDirectionalVelocity)
    HANDLE(set_tangential_force, OnSetTangentialForce)
    HANDLE(iterate_overlaps, OnIterateOverlaps)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(PhysicsVolume)
    SYNC_PROP_SET(active, mActive, SetActiveState(_val.Int()))
    SYNC_PROP(report_on_overlaps, mReportOnOverlaps)
    SYNC_PROP_SET(collision_filter, (int &)mFilter, mFilter = (CollisionFilter)_val.Int();
                  if (mDetectionVolume) mDetectionVolume->SetCollisionFilter(mFilter))
    SYNC_PROP(radial_force, mRadialForce)
    SYNC_PROP(directional_force, mDirectionalForce)
    SYNC_PROP(tangential_force, mTangentialForce)
    SYNC_PROP(directional_velocity, mDirectionalVelocity)
    SYNC_PROP_SET(shape_type, mShapeType, ChangeShapeType(_val.Int()))
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(RndPollable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(PhysicsVolume)
    SAVE_REVS(7, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndTransformable)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mActive;
    bs << mRadialForce;
    bs << mDirectionalForce;
    bs << mDirectionalVelocity;
    bs << mShapeType;
    bs << mReportOnOverlaps;
    bs << mFilter;
    bs << mTangentialForce;
END_SAVES

BEGIN_COPYS(PhysicsVolume)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndTransformable)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(PhysicsVolume)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mShapeType)
        COPY_MEMBER(mOverlapCount)
        COPY_MEMBER(unk128)
        COPY_MEMBER(mDirectionalForce)
        COPY_MEMBER(mTangentialForce)
        COPY_MEMBER(mDirectionalVelocity)
        COPY_MEMBER(mRadialForce)
        COPY_MEMBER(mActive)
        COPY_MEMBER(mReportOnOverlaps)
        COPY_MEMBER(mFilter)
    END_COPYING_MEMBERS
    DestroyPhysicsVolume();
END_COPYS

bool PhysicsVolume::MakeWorldSphere(Sphere &s, bool b) {
    Vector3 v;
    HalfExtends(v);
    s.radius = Length(v);
    s.center = WorldXfm().v;
    return true;
}

void PhysicsVolume::Poll() {
    if (mDetectionVolume) {
        if (mDetectionVolume->GetActiveState() != mActive) {
            mDetectionVolume->SetActiveState(mActive);
        }
        mDetectionVolume->Reset(WorldXfm());
        if (mActive) {
            if (mRadialForce > 0) {
                mDetectionVolume->ApplyRadialForce(mRadialForce);
            }
            if (LengthSquared(mDirectionalForce) > 0) {
                mDetectionVolume->ApplyDirectionalForce(mDirectionalForce);
            }
            if (LengthSquared(mTangentialForce) > 0) {
                mDetectionVolume->ApplyTangentialForce(mTangentialForce);
            }
            if (LengthSquared(mDirectionalVelocity) > 0) {
                mDetectionVolume->ApplyDirectionalLinearVelocity(mDirectionalVelocity);
            }
        }
        if (mOverlapCount) {
            if (mReportOnOverlaps) {
                static Symbol while_has_overlaps("while_has_overlaps");
                Handle(Message(while_has_overlaps, this), false);
            }
        }
    }
}

void PhysicsVolume::Enter() {
    SetActiveState(mActive);
    RndPollable::Enter();
}

void PhysicsVolume::OnCollidableEnter(Hmx::Object *object, ObjectDir *dir) {
    static Symbol object_enter("object_enter");
    Handle(Message(object_enter, this, object, dir), false);
    mOverlapCount++;
}

void PhysicsVolume::OnCollidableExit(Hmx::Object *object, ObjectDir *dir) {
    static Symbol object_exit("object_exit");
    Handle(Message(object_exit, this, object, dir), false);
    if (--mOverlapCount < 0) {
        mOverlapCount = 0;
    }
}

void PhysicsVolume::HalfExtends(Vector3 &v) {
    MakeScale(WorldXfm().m, v);
    v /= 2;
}

void PhysicsVolume::ChangeShapeType(int t) {
    if (mShapeType != t) {
        DestroyPhysicsVolume();
        mShapeType = (PhysicsVolumeType)t;
    }
}

void PhysicsVolume::SetActiveState(bool active) {
    if (mDetectionVolume) {
        if (active) {
            mDetectionVolume->Reset(WorldXfm());
        }
        mDetectionVolume->SetActiveState(active);
    }
    mActive = active;
}

void PhysicsVolume::CreatePhysicsVolume(PhysicsManager *mgr) {
    if (!mDetectionVolume) {
        Vector3 v;
        HalfExtends(v);
        Sphere s;
        s.radius = Length(v);
        s.center = WorldXfm().v;
        SetSphere(s);
        mDetectionVolume =
            mgr->MakeDetectionVolume(this, WorldXfm(), mShapeType, mFilter);
        SetActiveState(mActive);
    }
}

void PhysicsVolume::DestroyPhysicsVolume() {
    if ((unsigned)(void*)mDetectionVolume.Ptr()) {
        delete mDetectionVolume.Ptr();
    }
    mDetectionVolume = nullptr;
}

DataNode PhysicsVolume::OnSetDirectionalForce(const DataArray *args) {
    MILO_ASSERT(args->Size() == 5, 0x180);
    Vector3 v(args->Float(2), args->Float(3), args->Float(4));
    mDirectionalForce = v;
    return 0;
}

DataNode PhysicsVolume::OnSetTangentialForce(const DataArray *args) {
    MILO_ASSERT(args->Size() == 5, 0x187);
    Vector3 v(args->Float(2), args->Float(3), args->Float(4));
    mTangentialForce = v;
    return 0;
}

DataNode PhysicsVolume::OnSetDirectionalVelocity(const DataArray *args) {
    MILO_ASSERT(args->Size() == 5, 0x18E);
    Vector3 v(args->Float(2), args->Float(3), args->Float(4));
    mDirectionalVelocity = v;
    return 0;
}

DataNode PhysicsVolume::OnIterateOverlaps(const DataArray *args) {
    if (!mDetectionVolume->GetActiveState()) {
        return 0;
    } else {
        std::list<std::pair<Hmx::Object *, ObjectDir *> > pairs;
        mDetectionVolume->GetOverlaps(pairs);
        if (pairs.empty()) {
            return 0;
        } else {
            DataNode *var2 = args->Var(2);
            DataNode *var3 = args->Var(3);
            DataNode n2(*var2);
            DataNode n3(*var3);
            FOREACH (it, pairs) {
                *var2 = it->first;
                *var3 = it->second;
                for (int i = 4; i < args->Size(); i++) {
                    args->Command(i)->Execute();
                }
            }
            *var2 = n2;
            *var3 = n3;
            return 0;
        }
    }
}
