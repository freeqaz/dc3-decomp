#include "char\CharForeTwist.h"
#include "math\Rot.h"
#include "math\Trig.h"
#include "math\Utl.h"
#include "obj/Object.h"
#include <cmath>

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
    // RESIDUAL (w7-ak, 91.4 canonical): 52 of 139 rows, and every one of them is
    // inside the inlined Dot/Cross/Dot block at idx 34-87.  The whole residual has
    // ONE cause: the image needs FOUR callee-saved FPRs there (f28 newbias, f29
    // parentxfm.m.x.x, f30 the cross.y term, f31 DEG2RAD) and so opens with
    // `subi r12,r1,0x28` + `bl __savefpr_28` and a 0x110 frame; we need only three
    // (f29 newbias, f30 m.x.x, f31 DEG2RAD), inline the three `stfd`s and take a
    // 0x100 frame.  The extra pressure is in the SCHEDULE, not the source: both
    // sides emit the same multiset of lfs/fmr/fmuls/fmsubs/fmadds and associate
    // both dot products identically (Dot(x,v98) = x.y*c.y + x.z*c.z + x.x*c.x on
    // both sides); the image merely starts Dot(y,z) from the .z term and loads
    // parentxfm.m.y.z first, where we start from .y and load m.y.y first.
    // NEGATIVE RESULTS: hoisting the Cross above the first Clamp (so v98 is live
    // longer, the obvious way to buy a fourth long-lived FPR) costs 91.4 -> 90.6
    // and adds a row; moving `newbias` above the whole block is exactly inert
    // (same 52 rows, same registers).
    // NEGATIVE RESULT (w7-bl): hand-expanding the block the way RB3's matching
    // source does -- nine named component locals declared in the image's own
    // load order (m.y.z, m.z.z, m.y.x, m.y.y, m.z.x, m.z.y, m.x.y, m.x.z,
    // m.x.x), then `pyz*hzz + pyx*hzx + pyy*hzy` and the three cross terms
    // written with the image's operand order -- costs 91.4 -> 83.7.  It does
    // cut the register swaps from 29 to 9, but MSVC then rebuilds the whole
    // schedule around the explicit temporaries: 3 delete / 6 insert becomes
    // 11 delete / 7 insert.  The Dot()/Cross() calls already produce the
    // image's association and multiply multiset; only the colouring differs,
    // and naming the intermediates is not the way to reach it.
    float clamped = Clamp(-1.0f, 1.0f, Dot(parentxfm.m.y, handxfm.m.z));
    Vector3 v98;
    Cross(parentxfm.m.y, handxfm.m.z, v98);
    float clamp2 = Clamp(-1.0f, 1.0f, Dot(parentxfm.m.x, v98));
    float newbias = mBias * DEG2RAD;
    float tan2res = std::atan2(clamp2, clamped);
    float angle = LimitAng(mOffset * DEG2RAD + tan2res + newbias);
    float finalfloat = angle - newbias;
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
