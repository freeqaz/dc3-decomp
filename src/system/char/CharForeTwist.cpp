#include "char/CharForeTwist.h"
#include "math/Rot.h"
#include "math/Trig.h"
#include "math/Utl.h"
#include "obj/Object.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static bool DebugArmPollablesEnabled() {
    static int sEnabled = -1;
    if (sEnabled == -1) {
        const char *env = getenv("MILO_DEBUG_ARM_POLLABLES");
        sEnabled = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return sEnabled != 0;
}

static bool DebugForeTwistLegacy90Enabled() {
    static int sEnabled = -1;
    if (sEnabled == -1) {
        const char *env = getenv("MILO_DEBUG_FORETWIST_LEGACY90");
        sEnabled = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return sEnabled != 0;
}

float LimitAng(float ang) {
    float r = fmod(ang + PI, 2.0f * PI);
    return r < 0 ? r + PI : r - PI;
}

CharForeTwist::CharForeTwist() : mHand(this), mTwist2(this), mOffset(0), mBias(0) {}

BEGIN_HANDLERS(CharForeTwist)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(CharForeTwist)
    SYNC_PROP(hand, mHand)
    SYNC_PROP(twist2, mTwist2)
    SYNC_PROP(offset, mOffset)
    SYNC_PROP(bias, mBias)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharForeTwist)
    SAVE_REVS(4, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mOffset;
    bs << mHand;
    bs << mTwist2;
    bs << mBias;
END_SAVES

BEGIN_COPYS(CharForeTwist)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(CharForeTwist)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mOffset)
        COPY_MEMBER(mHand)
        COPY_MEMBER(mTwist2)
        COPY_MEMBER(mBias)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(4, 0)

BEGIN_LOADS(CharForeTwist)
    LOAD_REVS(bs)
    ASSERT_REVS(4, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mOffset;
    d >> mHand;
    d >> mTwist2;
    if (d.rev > 1 && d.rev < 3) {
        int dummy;
        d >> dummy;
    }
    if (d.rev > 3)
        d >> mBias;
END_LOADS

void CharForeTwist::Poll() {
    if (!mHand || !mTwist2 || !mHand->TransParent() || !mTwist2->TransParent())
        return;
    const Transform &parentxfm = mHand->TransParent()->WorldXfm();
    const Transform &handxfm = mHand->WorldXfm();
    float clamped = Clamp(-1.0f, 1.0f, Dot(parentxfm.m.y, handxfm.m.z));
    Vector3 v98;
    Cross(parentxfm.m.y, handxfm.m.z, v98);
    float clamp2 = Clamp(-1.0f, 1.0f, Dot(parentxfm.m.x, v98));
    float effectiveOffset = mOffset;
    if (DebugForeTwistLegacy90Enabled()) {
        // Debug-only experiment: CharacterTest::AddDefaults() still uses +/-90
        // offsets, unlike the authored 0/180 values loaded in gameplay.
        effectiveOffset = strstr(Name(), "_R") ? -90.0f : 90.0f;
    }
    float newbias = mBias * DEG2RAD;
    float tan2res = std::atan2(clamp2, clamped);
    float angle = LimitAng(effectiveOffset * DEG2RAD + tan2res + newbias);
    float finalfloat = angle - newbias;
    static int sDebugForeCount = 0;
    if (DebugArmPollablesEnabled() && sDebugForeCount < 24) {
        fprintf(
            stderr,
            "FORETWIST name='%s' dir='%s' hand='%s' twist2='%s' "
            "offset=%.1f bias=%.1f handLocalX=%.3f twist2LocalX=%.3f "
            "clamped=%.4f clamp2=%.4f atan2=%.4f final=%.4f\n",
            Name(),
            Dir() ? Dir()->Name() : "(null)",
            mHand->Name(),
            mTwist2->Name(),
            effectiveOffset,
            mBias,
            mHand->LocalXfm().v.x,
            mTwist2->LocalXfm().v.x,
            clamped,
            clamp2,
            tan2res,
            finalfloat
        );
        sDebugForeCount++;
    }
    if (IsNaN(finalfloat))
        return;
    Hmx::Matrix3 m58;
    MakeRotMatrixX(finalfloat * 0.33333f, m58);
    RndTransformable *twistparent = mTwist2->TransParent();
    Transform tf88;
    tf88.v = parentxfm.v;
    Multiply(m58, parentxfm.m, tf88.m);
    twistparent->SetWorldXfm(tf88);
#ifdef HX_NATIVE
    // Back-compute mLocalXfm so it survives dirty cascades from later pollables.
    // CharUpperTwist may call SetWorldXfm on upperArm after us, dirtying our bones.
    {
        Transform invParent;
        Invert(twistparent->TransParent()->WorldXfm(), invParent);
        Multiply(tf88, invParent, twistparent->mLocalXfm);
    }
#endif
    RndTransformable *hand = mHand;
    RndTransformable *twist2 = mTwist2;
    Interp(tf88.v, handxfm.v, twist2->mLocalXfm.v.x / hand->mLocalXfm.v.x, tf88.v);
    Multiply(m58, tf88.m, tf88.m);
    mTwist2->SetWorldXfm(tf88);
#ifdef HX_NATIVE
    {
        Transform invParent;
        Invert(twistparent->WorldXfm(), invParent);
        Multiply(tf88, invParent, mTwist2->mLocalXfm);
    }
#endif
}

void CharForeTwist::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    changedBy.push_back(mHand);
    change.push_back(mTwist2);
    if (mTwist2)
        change.push_back(mTwist2->TransParent());
}
