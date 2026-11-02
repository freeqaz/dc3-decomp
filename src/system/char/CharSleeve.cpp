#include "char\CharSleeve.h"
#include "char\Character.h"
#include "math\Utl.h"
#include "math\Vec.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj\Rnd.h"
#include "rndobj\Utl.h"

void NormalizeScale(const Vector3 &, float, Vector3 &);

CharSleeve::CharSleeve()
    : mSleeve(this), mTopSleeve(this), mPos(0, 0, 0), mLastPos(0, 0, 0), mLastDT(0),
      mInertia(0.5f), mGravity(1.0f), mRange(0), mNegLength(0), mPosLength(0),
      mStiffness(0.02f) {}

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

// w26-f10 stop note.  Canonical 100.0, fuzzy 99.795: ten `diff_arg` rows, all
// commutative operand order, and in all ten we emit the image's two operands
// reversed (55 and 139 `fadds` on `absed + mPosLength`, 235 and 403 the `fmadds`
// of the inlined ScaleAdd/ScaleAddEq, 323 and 330 two products of the first
// Normalize(tf90.m.z), 453/454/456/460 four of the second).
//
// Not a class rule, and not the 8.8 base-binding lever.  The same expressions
// occur again in this one function with the OPPOSITE order and score equal:
// `absed + mPosLength` at ClampEq's upper bound is equal while the other two
// sites are charged, and of the six products in Normalize two are charged in
// the first copy and four in the second.  So the operand keys are the per-site
// temp sids of C2RS-BRIDGE 8.7 lever 3, not a property of these spellings.
//
// Measured on this row, all with nsearch (per-target, exact for src/system):
//   const float &posLength = mPosLength;  used at one / all three sites
//                                           99.754 (worse)
//   const Vector3 &mx / &mz before Cross     99.774 (worse)
//   const Vector3 &pv2 = ...WorldXfm().v     99.755 (worse)
//   const Vector3 &pmx2 over the whole
//     mTopSleeve block                       92.846 (much worse)
// The CalcError lever (name a single-use subexpression so it changes leaf
// class) has no target here: every charged operand is already a leaf, and
// naming either side of `absed + mPosLength` only moves the sids.  What is
// left is an id-budget question -- how many temps the image mints before this
// block -- of the kind 8.9 describes, and no source spelling tried moves it.
void CharSleeve::Poll() {
    auto _tmp1 = mSleeve->TransParent();
    if (mSleeve && _tmp1) {
        float deltasecs = TheTaskMgr.DeltaSeconds();
        float dvar12 = deltasecs * 60.0f;
        float dv2 = deltasecs * dvar12;
        float gravity_z = (mGravity * dv2) * -3.8582677f;
        auto _tmp0 = powf(1.0f - mStiffness, dvar12 * dvar12);
        RndTransformable *sleeveparent = mSleeve->TransParent();
        float absed = fabsf(mSleeve->LocalXfm().v.z);
        float powed = 1.0f - _tmp0;
        bool b2 = false;
        Character *me = Character::Current();
        if (me && me->Teleported()) {
            mPos = mSleeve->WorldXfm().v;
            Vector3 v9c(0.0f, 0.0f, -(absed + mPosLength));
            // Written out rather than Dot(): with v9c.x/.y both literal 0.0f the
            // left-associated Dot() lets the compiler factor x*0 + y*0 into
            // (x+y)*0, reordering the FMA chain to y,x,z. The target keeps the
            // plain y,z,x chain, which this right-nested form reproduces.
            // dotted_orig is computed first so the clamped copy lands in the
            // register the target uses (declaring dotted first inverts the pair).
            const Vector3 &pmx = sleeveparent->WorldXfm().m.x;
            float dotted_orig = v9c.x * pmx.x + (v9c.y * pmx.y + v9c.z * pmx.z);
            float dotted = dotted_orig;
            ClampEq(dotted, -mRange, mRange);
            ScaleAddEq(v9c, sleeveparent->WorldXfm().m.x, dotted - dotted_orig);
            mPos += v9c;
            Vector3 va8;
            ScaleAdd(sleeveparent->WorldXfm().v, sleeveparent->WorldXfm().m.x, dotted, va8);
            Subtract(mPos, va8, v9c);
            NormalizeScale(v9c, absed + mPosLength, v9c);
            Add(va8, v9c, mPos);
            mLastPos = mPos;
            b2 = true;
            mLastDT = 0;
        }
        Vector3 vb4(mPos);
        if (mLastDT > 0.0f && deltasecs > 0.0f) {
            Vector3 vc0;
            Subtract(mPos, mLastPos, vc0);
            ScaleAddEq(vb4, vc0, (mInertia / mLastDT) * deltasecs);
        }
        vb4.z += gravity_z;
        Vector3 vcc;
        Subtract(vb4, sleeveparent->WorldXfm().v, vcc);
        float dotted2 = Dot(vcc, sleeveparent->WorldXfm().m.x);
        float d4 = (1.0f - powed) * dotted2;
        ClampEq(d4, -mRange, mRange);
        ScaleAddEq(vcc, sleeveparent->WorldXfm().m.x, (d4 - dotted2));
        float len = Length(vcc);
        float interped = (absed - len) * powed + len;
        ClampEq(interped, absed - mNegLength, absed + mPosLength);
        NormalizeScale(vcc, interped, vcc);
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
            ScaleAddEq(vcc, sleeveparent->WorldXfm().m.x, -dotcc);
            Add(sleeveparent->WorldXfm().v, vcc, tf90.v);
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
