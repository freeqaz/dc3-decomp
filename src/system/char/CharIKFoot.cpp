#include "char/CharIKFoot.h"
#include "CharIKHand.h"
#include "char/Character.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Trans.h"
#ifdef HX_NATIVE
#include <cstdlib>
// FEET-IN-FLOOR FIX (opt-out: DC3_FEET_PLANT_FIX_OFF=1). The leg foot-plant IK must
// bend the leg; native loads mMoveElbow=false for the *.ikfoot, which disables the
// IKElbow knee bend AND drops the knee/thigh dependency from CharIKHand::PollDeps, so
// the sorter polls the IK before the skeleton pose and the bend is overwritten. See
// CharIKFoot::Load (forces mMoveElbow=true) + Character::SyncObjects (IK sorts last).
bool Dc3FeetPlantFix() {
    // OPT-IN (DC3_FEET_PLANT_FIX=1) while WIP: with the fix on, the leg IK survives the
    // move pose (knee bends ~-36deg, was discarded) BUT the foot-plant solve currently
    // DIVERGES on native (foot flies/sinks wildly), so it stays OFF by default until the
    // IK-solve stabilization lands. See docs/sessions/2026-06-09-xenia-xbox-foot-truth.md.
    static int v = -1;
    if (v < 0)
        v = getenv("DC3_FEET_PLANT_FIX") ? 1 : 0;
    return v != 0;
}
// When the fix is on, the leg IK runs ONCE per frame from HamDirector::Poll AFTER the
// song-move pose (set true around that re-run). During the normal char poll it is skipped
// (the move pose would overwrite it, and running it twice destabilizes the foot-plant FSM).
bool gDc3DirectorIKReRun = false;
#endif

CharIKFoot::CharIKFoot() : mFootBone(this), mFootFsmState(0), mData(this), mDataIndex(0) {
    mFootBone = Hmx::Object::New<RndTransformable>();
    mFootBone->DirtyLocalXfm().Reset();
}

CharIKFoot::~CharIKFoot() { delete mFootBone; }

BEGIN_HANDLERS(CharIKFoot)
    HANDLE_SUPERCLASS(CharIKHand)
END_HANDLERS

BEGIN_PROPSYNCS(CharIKFoot)
    SYNC_PROP(data, mData)
    SYNC_PROP(data_index, mDataIndex)
    SYNC_SUPERCLASS(CharIKHand)
END_PROPSYNCS

BEGIN_SAVES(CharIKFoot)
    SAVE_REVS(6, 0)
    SAVE_SUPERCLASS(CharIKHand)
    bs << mData;
    bs << mDataIndex;
END_SAVES

BEGIN_COPYS(CharIKFoot)
    COPY_SUPERCLASS(CharIKHand)
    CREATE_COPY(CharIKFoot)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mData)
        COPY_MEMBER(mDataIndex)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(6, 0)

BEGIN_LOADS(CharIKFoot)
    LOAD_REVS(bs)
    ASSERT_REVS(6, 0)
    LOAD_SUPERCLASS(CharIKHand)
    if (d.rev < 6) {
        Symbol s;
        d >> s;
    }
    if (d.rev < 5) {
        int i;
        if (d.rev > 1)
            d >> i;
        if (d.rev > 2)
            d >> i;
        if (d.rev > 3)
            d >> i;
    } else {
        d >> mData;
        d >> mDataIndex;
    }
#ifdef HX_NATIVE
    // A foot-plant IK has to move the knee/thigh to plant the foot. Native loads
    // mMoveElbow=false here (the leg over-extends and the foot sinks); Xbox renders a
    // bent, planted knee. Force the elbow-move path so IKElbow bends the knee and
    // PollDeps declares the knee/thigh dep (which the poll-order fix relies on).
    if (Dc3FeetPlantFix())
        mMoveElbow = true;
#endif
END_LOADS

void CharIKFoot::Enter() {
    mFootFsmState = 0;
    mFootBlendTime = 0.0f;
}

void CharIKFoot::PollDeps(std::list<Hmx::Object *> &l1, std::list<Hmx::Object *> &l2) {
    CharIKHand::PollDeps(l1, l2);
}

void CharIKFoot::Poll() {
#ifdef HX_NATIVE
    {
        // EXPERIMENT (DC3_IK_CHARFOOT_SKIP=1): skip CharIKFoot entirely. DoFSM
        // writes the foot bone LOCAL z = mFinger (toe-target) world z, and a local
        // write SURVIVES the render recompute (unlike HamIKEffector's SetWorldXfm).
        // So this is the proximate driver of the rendered foot Z. Disambiguates
        // whether CharIKFoot (fed a sunk toe-target) sinks the foot, vs the raw
        // anim. Remove before shipping.
        static int sCharFootSkip = -1;
        if (sCharFootSkip < 0)
            sCharFootSkip = getenv("DC3_IK_CHARFOOT_SKIP") ? 1 : 0;
        if (sCharFootSkip)
            return;
    }
    {
        static int sCharIKFootPollLog = 0;
        if (sCharIKFootPollLog < 5) {
            sCharIKFootPollLog++;
            fprintf(stderr,
                "DC3_IK_DIAG CharIKFootPoll[%d]: path=%s mFinger=%p mHand=%p "
                "mData=%p mFootBone=%p\n",
                sCharIKFootPollLog,
                PathName(this),
                (void*)mFinger.Ptr(), (void*)mHand.Ptr(),
                (void*)mData.Ptr(), (void*)mFootBone.Ptr());
        }
    }
    // Single-run gate: skip the leg IK during the normal char poll; it is re-run once
    // from HamDirector::Poll after the move pose (see Dc3FeetPlantFix / gDc3DirectorIKReRun).
    if (Dc3FeetPlantFix() && !gDc3DirectorIKReRun)
        return;
#endif
    if (mFinger && mHand && mData) {
        mTargets.clear();
        mTargets.push_back(IKTarget(mFootBone, 0));
        DoFSM(Character::Current(), mFootBone->DirtyLocalXfm());
        CharIKHand::Poll();
        mTargets.clear();
    }
}

void CharIKFoot::DoFSM(Character *mMe, Transform &tf) {
    mFootTransform = mFinger->WorldXfm();
    if (mMe && mMe->Teleported())
        mFootFsmState = 0;
    float deltasecs = TheTaskMgr.DeltaSeconds();
    if (deltasecs < 0.0f)
        deltasecs = 0.0f;
    tf.m = mFinger->WorldXfm().m;
    tf.v.z = mFinger->WorldXfm().v.z;
#ifdef HX_NATIVE
    // EXPERIMENT (DC3_IK_FOOTPLANT=1): clamp the foot IK GOAL Z to the floor.
    // CharIKHand::Poll (called from CharIKFoot::Poll) solves the leg to reach this
    // target and writes the leg bone LOCALs (which SURVIVE render, unlike
    // HamIKEffector's SetWorldXfm). Tests whether a floor-clamped goal re-plants
    // the rendered foot during the dance crouch (the toe-target sinks to -4 with
    // the over-extended leg). Remove before shipping.
    if (getenv("DC3_IK_FOOTPLANT")) {
        if (tf.v.z < 0.0f)
            tf.v.z = 0.0f;
    }
#endif
    mFootPosition.z = tf.v.z;
    float f10;
    bool b2 = false;
    float vecat = mData->LocalXfm().v[mDataIndex];
    if (!(vecat < 1.0f)) {
        b2 = true;
    } else {
        if (vecat <= 0.0f) {
            ;
        } else {
            if (mFootFsmState == 1) {
                f10 = 0.6f;
            } else {
                f10 = 0.5f;
            }
            if (tf.v.z < f10) {
                b2 = true;
            }
        }
    }
#ifdef HX_NATIVE
    if (gDc3DirectorIKReRun && getenv("DC3_IK_DIAG")) {
        static int sFsmLog = 0;
        if (sFsmLog < 40) {
            sFsmLog++;
            const Transform &fw = mFinger->WorldXfm();
            fprintf(stderr, "DC3_IK_DIAG DoFSM[%d] %s fsm=%d vecat=%.3f b2=%d "
                    "fingerW=(%.2f,%.2f,%.2f) tf.v=(%.2f,%.2f,%.2f) footPos=(%.2f,%.2f,%.2f)\n",
                    sFsmLog, PathName(this), mFootFsmState, vecat, b2 ? 1 : 0,
                    fw.v.x, fw.v.y, fw.v.z, tf.v.x, tf.v.y, tf.v.z,
                    mFootPosition.x, mFootPosition.y, mFootPosition.z);
        }
    }
#endif
    if (mFootFsmState == 0) {
        const Transform &wt = mFinger->WorldXfm();
        tf.v.x = wt.v.x;
        tf.v.y = wt.v.y;
        if (b2) {
            mFootPosition = tf.v;
            mFootFsmState = 1;
        }
    }
    if (mFootFsmState == 1) {
        if (!b2) {
            mFootFsmState = 2;
            mFootBlendTime = Distance(mFinger->WorldXfm().v, tf.v);
        } else {
            Vector3 v3c;
            Subtract(mFinger->WorldXfm().v, mFootPosition, v3c);
            float len = Length(v3c);
            if (len > 0.125f)
                v3c *= 0.125f / len;
            Add(mFootPosition, v3c, tf.v);
            return;
        }
    }
    if (mFootFsmState == 2) {
        Vector3 delta;
        Subtract(mFinger->WorldXfm().v, mFootPosition, delta);
        float len = Length(delta);
        mFootBlendTime = Min(-(deltasecs * 25.0f - mFootBlendTime), len);
        if (mFootBlendTime <= 0.0f)
            mFootFsmState = 0;
        else
            delta *= (len - mFootBlendTime) / len;
        Add(mFootPosition, delta, tf.v);
        if (b2) {
            mFootPosition = tf.v;
            mFootFsmState = 1;
        }
    }
}
