#include "char/CharSleeve.h"
#include "char/Character.h"
#include "obj/Object.h"
#include "rndobj/Rnd.h"
#include "rndobj/Utl.h"

CharSleeve::CharSleeve()
    : mSleeve(this), mTopSleeve(this), mPos(0, 0, 0), mLastPos(0, 0, 0), mLastDT(0),
      mInertia(0.5f), mGravity(1.0f), mRange(0), mNegLength(0), mPosLength(0),
      mStiffness(0.02f), mMe(this) {}

CharSleeve::~CharSleeve() {}

BEGIN_PROPSYNCS(CharSleeve)
    SYNC_PROP(sleeve, mSleeve)
    SYNC_PROP(top_sleeve, mTopSleeve)
    SYNC_PROP(inertia, mInertia)
    SYNC_PROP(gravity, mGravity)
    SYNC_PROP(stiffness, mStiffness)
    SYNC_PROP(range, mRange)
    SYNC_PROP(neg_length, mNegLength)
    SYNC_PROP(pos_length, mPosLength)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharSleeve)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mSleeve;
    bs << mTopSleeve;
    bs << mInertia;
    bs << mGravity;
    bs << mStiffness;
    bs << mRange;
    bs << mNegLength;
    bs << mPosLength;
END_SAVES

BEGIN_COPYS(CharSleeve)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(CharSleeve)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mSleeve)
        COPY_MEMBER(mTopSleeve)
        COPY_MEMBER(mInertia)
        COPY_MEMBER(mGravity)
        COPY_MEMBER(mStiffness)
        COPY_MEMBER(mRange)
        COPY_MEMBER(mNegLength)
        COPY_MEMBER(mPosLength)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(CharSleeve)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    bs >> mSleeve >> mTopSleeve;
    bs >> mInertia >> mGravity >> mStiffness >> mRange >> mNegLength >> mPosLength;
END_LOADS

void CharSleeve::Poll() {
    if (mSleeve && mSleeve->TransParent()) {
        float deltasecs = TheTaskMgr.DeltaSeconds();
        float dvar12 = deltasecs * 60.0f;
        float powed = std::pow(1.0f - mStiffness, dvar12 * dvar12);
        RndTransformable *sleeveparent = mSleeve->TransParent();
        float absed = std::fabs(mSleeve->LocalXfm().v.z);
        bool b2 = false;

        if (mMe && mMe->Teleported()) {
            mPos = mSleeve->WorldXfm().v;
            Vector3 v9c(0.0f, 0.0f, -(absed + mPosLength));
            float dotted = Dot(v9c, sleeveparent->WorldXfm().m.x);
            ClampEq(dotted, -mRange, mRange);
            Vector3 temp;
            ScaleAdd(v9c, sleeveparent->WorldXfm().m.x, dotted, temp);
            Add(mPos, temp, mPos);
            Vector3 va8;
            ScaleAdd(sleeveparent->WorldXfm().v, sleeveparent->WorldXfm().m.x, dotted, va8);
            Subtract(mPos, va8, v9c);
            float len_v9c = Length(v9c);
            if (len_v9c > 0.0f) {
                Scale(v9c, (absed + mPosLength) / len_v9c, v9c);
            }
            Add(va8, v9c, mPos);
            mLastPos = mPos;
            b2 = true;
            mLastDT = 0;
        }

        Vector3 vb4 = mPos;
        if (mLastDT > 0.0f && deltasecs > 0.0f) {
            Vector3 vc0;
            Subtract(mPos, mLastPos, vc0);
            Vector3 scaled;
            Scale(vc0, (mInertia * deltasecs) / mLastDT, scaled);
            Add(vb4, scaled, vb4);
        }

        vb4.z += mGravity * deltasecs * dvar12 * -3.858268f;

        Vector3 vcc;
        Subtract(vb4, sleeveparent->WorldXfm().v, vcc);
        float dotted2 = Dot(vcc, sleeveparent->WorldXfm().m.x);
        float d4 = dvar12 * (1.0f - (1.0f - powed));
        ClampEq(d4, -mRange, mRange);
        Vector3 scaled2;
        Scale(sleeveparent->WorldXfm().m.x, d4 - dvar12, scaled2);
        Add(vcc, scaled2, vcc);

        float len = Length(vcc);
        float one_minus_powed = 1.0f - powed;
        float interped = Interp(len, absed, one_minus_powed);
        ClampEq(interped, absed - mNegLength, absed + mPosLength);

        if (len > 0.0f) {
            Scale(vcc, interped / len, vcc);
        }

        Add(sleeveparent->WorldXfm().v, vcc, vb4);

        Transform tf90;
        tf90.v = vb4;
        Scale(vcc, -1.0f, tf90.m.z);
        Cross(tf90.m.z, sleeveparent->WorldXfm().m.x, tf90.m.y);
        Normalize(tf90.m.z, tf90.m.z);
        Normalize(tf90.m.y, tf90.m.y);
        Cross(tf90.m.y, tf90.m.z, tf90.m.x);
        mSleeve->SetWorldXfm(tf90);

        mLastPos = mPos;
        mLastDT = deltasecs;
        mPos = vb4;
        if (b2)
            mLastPos = mPos;

        if (mTopSleeve) {
            float dotcc = Dot(vcc, sleeveparent->WorldXfm().m.x);
            Vector3 scaled4;
            Scale(sleeveparent->WorldXfm().m.x, -dotcc, scaled4);
            Add(vcc, scaled4, vcc);
            Vector3 temp2;
            Scale(sleeveparent->WorldXfm().v, 1.0f, temp2);
            Add(temp2, vcc, tf90.v);
            Scale(vcc, -1.0f, tf90.m.z);
            Cross(tf90.m.z, sleeveparent->WorldXfm().m.x, tf90.m.y);
            Normalize(tf90.m.z, tf90.m.z);
            Normalize(tf90.m.y, tf90.m.y);
            Cross(tf90.m.y, tf90.m.z, tf90.m.x);
            mTopSleeve->SetWorldXfm(tf90);
        }
    }
}

void CharSleeve::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    if (mSleeve) {
        changedBy.push_back(mSleeve->TransParent());
        change.push_back(mSleeve);
        change.push_back(mTopSleeve);
    }
}

void CharSleeve::Highlight() {
    if (!mSleeve || !mSleeve->TransParent())
        return;
    UtilDrawAxes(mSleeve->WorldXfm(), 1.0f, Hmx::Color(0.0f, 1.0f, 0.0f));
    TheRnd.DrawLine(
        mSleeve->WorldXfm().v,
        mSleeve->TransParent()->WorldXfm().v,
        Hmx::Color(0.0f, 1.0f, 0.0f),
        false
    );
    if (mTopSleeve) {
        UtilDrawAxes(mTopSleeve->WorldXfm(), 1.0f, Hmx::Color(0.0f, 1.0f, 1.0f));
        TheRnd.DrawLine(
            mTopSleeve->WorldXfm().v,
            mTopSleeve->TransParent()->WorldXfm().v,
            Hmx::Color(0.0f, 1.0f, 1.0f),
            false
        );
    }
}

BEGIN_HANDLERS(CharSleeve)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
