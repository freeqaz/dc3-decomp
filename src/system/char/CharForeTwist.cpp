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
    // NEGATIVE RESULT (w7-br): the one asymmetry left in the block is which
    // term of Dot(m.y, m.z) is the standalone fmuls -- the image's is the z
    // term (`fmuls f6, f6, f11` at 0x8239CABC, y.z*z.z) and ours the y term --
    // so the first Dot was spelled INLINE, no locals, in the image's z, x, y
    // order.  91.4 -> 88.7 (54 rows, 8 insert / 4 delete): MSVC re-derives the
    // whole load order from the expression and the fourth callee-saved FPR
    // still does not appear.  The standalone term is not source-addressable
    // through the association either.
    // w21-i (stopped at 91.29 canonical, same 52 rows): the image's first Dot
    // IS `y + (x + z)` (fmuls y.z*z.z, fmadds x, fmadds y) -- the association
    // that closed CharIKFingers::CalculateHandDest's factoring cluster -- but
    // spelling it that way here, parenthesised, costs 91.29 -> 87.6 (14 non-
    // register rows).  Swapping the second Dot's operands (Dot(v98, m.x)) is
    // byte-inert.  `Transform &` (RB3's spelling) does not compile: WorldXfm()
    // returns const.  The fourth callee-saved FPR is still not source-reachable.
    // w21-al: 91.29 -> 93.67 (32 rows left: 23 diff_arg, 2 replace, 4 insert,
    // 3 delete, all still inside the Dot/Cross/Dot block).  Binding
    // parentxfm.m.y to a local reference ONCE (lever (a); rb3-xenon spells
    // py/hz this way) is what buys the image's fourth callee-saved FPR: the
    // prologue now opens `subi r12,r1,0x28; bl __savefpr_28` with the 0x110
    // frame, and f28 = newbias, f31 = DEG2RAD as in the image.  What is left:
    // we load handxfm.m.z.x twice (Dot's copy and Cross's copy) where the image
    // loads it once into f10, and the load order still starts h.z.y, not p.y.z.
    // Measured in a standalone /FAs probe of this TU (same cl.exe flags):
    // binding hz as well (rb3-xenon's exact spelling) removes the double load
    // and gives the image's y + (x + z) association, but MSVC then schedules
    // the block worse (+13 rows); all 24 explicit-paren spellings of the first
    // dot with both refs, and all 18 with py only, are worse than Dot(); a
    // Matrix3 & binding, px bound, Vector3 COPIES of py/hz (they stay in
    // memory), Cross written out three ways, newbias in four positions, Clamp
    // before/after Cross, and Dot operand order are inert or worse.
    // w24-c1 (93.67, not reworked; same 32 rows inside the Dot/Cross/Dot
    // block 33-70).  Re-read every value against the image: cross.x/y/z and
    // both dots compute the same products; only the /fp:fast association
    // of the first Dot differs (image y + (x + z), ours x + (z + y)) and the
    // FPR colouring.  w21-al already measured the hz-binding and all 42
    // explicit-paren spellings; not repeated.
    // w26-k (2026-11-02): mechanism, from the c2 tap.  The residual is the
    // SECOND allocator, not COLOR.  This function has five FP colour
    // candidates and all five are long-lived values that land in f0/f13/f31
    // (id12 pri20 tie56 -> f0, id14 pri20 tie50 -> f13, id5 pri14 tie37 -> f0,
    // id13 pri12 tie62 -> f31, id15 pri4 tie134 -> f13); every register in the
    // Dot/Cross block below (f2..f12) is a regasg first-fit selection over the
    // sched3 order (39 of them), so no priority/tie key decides it.  Two
    // concrete consequences of that order: the image loads handxfm.m.z.x ONCE
    // into f10 and reuses it for both Dot and Cross, where we emit `lfs 32(r30)`
    // twice (0xac and 0xc8), and the image's first Dot starts from the .z term
    // (`y + (x + z)`) where ours starts from .y.  Eleven further spellings were
    // measured in w26-k against the c2 replay + objdiff (name_check) ruler and
    // all were inert or worse than the 92.0150 fuzzy below: a `const Vector3
    // &hz` shared by Dot and Cross (inert), the same as a by-value copy (72.9),
    // reversed Dot operands (inert), v98 declared before the Clamp (inert),
    // newbias hoisted above the block (inert), a named `float hzx` feeding an
    // inlined first Dot (89.8), the first Dot written as `y + (x + z)` (90.6),
    // hz bound only for Cross (inert), and parentxfm.m.x bound by name (inert).
    // w31-d (2026-11-03): re-derived independently from the two listings, with
    // the Transform layout the code itself uses (Matrix3 rows at 0x0/0x10/0x20,
    // v at 0x30): target rows 33-72 form Cross.x = py.y*hz.z - py.z*hz.y,
    // Cross.y = py.z*hz.x - py.x*hz.z, -Cross.z = hz.y*py.x - py.y*hz.x, then
    // Dot(py,hz) = y + (x + z) and Dot(px,v98) = x + (z + y); ours forms the
    // same three cross terms (two of them negated-then-fmsubs, same signs) and
    // the same two dots.  Values agree; the deltas are that association and
    // register choice, plus ONE redundant `lfs 0x20(r30)` (we reload h.z.x,
    // the image keeps it in f10) with no store or call between the two loads.
    // Our body is 134 instructions against the image's 133, so nothing here is
    // a shortened-body artifact.  No behaviour defect.  The remaining handle
    // would have to be the regasg first-fit order, not source.
    const Vector3 &py = parentxfm.m.y;
    float clamped = Clamp(-1.0f, 1.0f, Dot(py, handxfm.m.z));
    Vector3 v98;
    Cross(py, handxfm.m.z, v98);
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
    RndTransformable *hand = mHand;
    RndTransformable *twist2 = mTwist2;
    Interp(tf88.v, handxfm.v, twist2->mLocalXfm.v.x / hand->mLocalXfm.v.x, tf88.v);
    Multiply(m58, tf88.m, tf88.m);
    mTwist2->SetWorldXfm(tf88);
}

void CharForeTwist::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    changedBy.push_back(mHand);
    change.push_back(mTwist2);
    if (mTwist2)
        change.push_back(mTwist2->TransParent());
}
