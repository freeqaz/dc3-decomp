#include "math\DoubleExponentialSmoother.h"

DoubleExponentialSmoother::DoubleExponentialSmoother()
    : mLevel(0), mPrevLevel(0), mTrend(0), mAlpha(0), mBeta(0) {}

DoubleExponentialSmoother::DoubleExponentialSmoother(float level, float alpha, float beta)
    : mLevel(level), mPrevLevel(level), mTrend(0), mAlpha(alpha), mBeta(beta) {}

void DoubleExponentialSmoother::Smooth(float value, float delta) {
    float newAlpha = Max(0.0f, mAlpha * delta);
    float newBeta = Max(0.0f, mBeta * delta);
    newAlpha = Min(newAlpha, 1.0f);
    newBeta = Min(newBeta, 1.0f);
    float oldPrev = mPrevLevel;
    mPrevLevel = newAlpha * (value - mLevel) + mLevel;
    mTrend = ((mPrevLevel - oldPrev) - mTrend) * newBeta + mTrend;
    mLevel = mTrend + mPrevLevel;
}

void Vector2DESmoother::SetSmoothParameters(float alpha, float beta) {
    mX.SetCoeffs(alpha, beta);
    mY.SetCoeffs(alpha, beta);
}

void Vector2DESmoother::ForceValue(Vector2 v) {
    mX.SetParams(v.x, v.x, 0);
    mY.SetParams(v.y, v.y, 0);
}

Vector2 Vector2DESmoother::Value() const { return Vector2(mX.mLevel, mY.mLevel); }

Vector3DESmoother::Vector3DESmoother(Vector3 v, float alpha, float beta)
    : mX(v.x, alpha, beta), mY(v.y, alpha, beta), mZ(v.z, alpha, beta) {}

void Vector3DESmoother::SetSmoothParameters(float alpha, float beta) {
    mX.SetCoeffs(alpha, beta);
    mY.SetCoeffs(alpha, beta);
    mZ.SetCoeffs(alpha, beta);
}

Vector3 Vector3DESmoother::Value() const {
    return Vector3(mX.mLevel, mY.mLevel, mZ.mLevel);
}

void Vector3DESmoother::ForceValue(Vector3 v) {
    mX.SetParams(v.x, v.x, 0);
    mY.SetParams(v.y, v.y, 0);
    mZ.SetParams(v.z, v.z, 0);
}

// w14-f: 94.75 -> 97.4.  BEHAVIOUR FIX in the normalize tail (latent: the one
// caller, PartyModeMgr, passes normalize = false).  The image normalises the way
// Vector3DESmoother does: inv = 0 when the length is zero (`beq` to `fmr f13, f0`
// with f0 = 0.0f), so a zero vector stays zero, and BOTH mLevel and mPrevLevel
// receive the normalised value (`stfs f11, 0x4(r3)` / `stfs f11, 0x0(r3)`,
// `stfs f13, 0x4(r11)` / `stfs f13, 0x0(r11)`).  We used to skip the write on a
// zero length and never touched mPrevLevel.  Remaining 6 rows: the mY expansion
// of the inlined DoubleExponentialSmoother::Smooth keeps mLevel in a register
// (`fmr f10, f11`) where the image re-loads it (`lfs f12, 0x0(r11)`) for the
// normalize; a Vector2 val(sx.mLevel, sy.mLevel) temp is worse (9 rows).
// w17-c: `Vector2 val = Value();` (the out-of-line accessor, inlined) gives the
// reload, and the length sums y*y first: non-PCH probe diff 235 -> 10, the one
// row left is the commutative fmadds inside the inlined mY Smooth().
void Vector2DESmoother::Smooth(Vector2 v, float dt, bool normalize) {
    DoubleExponentialSmoother &sx = mX;
    DoubleExponentialSmoother &sy = mY;
    sx.Smooth(v.x, dt);
    sy.Smooth(v.y, dt);
    if (normalize) {
        Vector2 val = Value();
        float len = std::sqrt(val.y * val.y + val.x * val.x);
        float inv;
        if (len != 0) {
            inv = 1.0f / len;
        } else {
            inv = 0;
        }
        float normX = val.x * inv;
        float normY = val.y * inv;
        sx.mTrend = 0;
        sx.mLevel = sx.mPrevLevel = normX;
        sy.mLevel = sy.mPrevLevel = normY;
        sy.mTrend = 0;
    }
}

void Vector3DESmoother::Smooth(Vector3 v, float dt, bool normalize) {
    // Naming the three sub-smoothers as references is what makes the image's
    // register allocation reproduce.  The image keeps THREE base pointers alive
    // across the Normalize() call -- r31 = mX (this+0), r30 = mY (this+0x14),
    // r29 = mZ (this+0x28) -- left over from the three Smooth() expansions, and
    // addresses the whole tail off them.  Written as bare `mX.` / `mY.` / `mZ.`
    // MSVC drops the sub-object bases at the call and re-addresses everything
    // off r31 with 0x18/0x1c/0x2c/0x30 displacements (96.4% -> 98.2%).
    //
    // The chained assignments below are `mLevel = mPrevLevel = norm.c` and not
    // the other way round because a chain evaluates right-to-left: this spells
    // the image's store order, mPrevLevel (0x4) before mLevel (0x0).  Purely a
    // store-order question -- both fields receive the same value.
    //
    // Residual (100 normalized, fuzzy 99.74, 3 rows): commutative operand order
    // in the 2nd and 3rd inlined DoubleExponentialSmoother::Smooth (w25-oa,
    // c2's commutative sort key, C2RS-BRIDGE 8.7). Not a register decision.
    //   - mTrend fmadds (mY and mZ): both factors are subexpressions. The
    //     change ((mPrevLevel - oldPrev) - mTrend) keys SU 1 / count 7
    //     (0x0107xxxx) and the clamped newBeta fsel keys count 6 (0x0106xxxx),
    //     so the change sorts first in every expansion. The image has that order
    //     in the 1st expansion and the out-of-line body, newBeta first in the
    //     2nd and 3rd (and in Vector2DESmoother's 2nd). Per-copy differences can
    //     only come from the low 16 bits (a hash of the operand sids), which
    //     decide only when the counts are equal. So the image's trees had equal
    //     counts, and a fix needs a spelling that gives them equal counts while
    //     emitting the same code. 13 spellings keep 7/6 (+=, named change/trend/
    //     level/prevDiff/beta, single-expression and Clamp() clamps, oldPrev and
    //     newBeta moved, reordered operands). The brief's "base sid mod 4"
    //     hypothesis does not apply: no memory leaves are involved.
    //   - mY mLevel fadds: two temp leaves, mTrend V588 (0x19300) before
    //     mPrevLevel V382 (0x15f80); the image needs mPrevLevel first. Temp keys
    //     are (sid << 6) & 0xffff, so this needs a temp layout where mTrend's sid
    //     wraps past a multiple of 1024 and mPrevLevel's does not. That is not a
    //     small sid shift.
    // Also measured (w25-fa): mX in place of the sx ref is inert; dropping all
    // three refs flips the 2nd expansion's mPrevLevel fmadds the wrong way.
    DoubleExponentialSmoother &sx = mX;
    DoubleExponentialSmoother &sy = mY;
    DoubleExponentialSmoother &sz = mZ;
    sx.Smooth(v.x, dt);
    sy.Smooth(v.y, dt);
    sz.Smooth(v.z, dt);
    if (normalize) {
        // w17-c: Value(), not a ctor from the three mLevels -- it is what
        // makes the image reload mZ.mLevel (probe diff 302 -> 30).
        Vector3 val = Value();
        Vector3 norm;
        Normalize(val, norm);
        sx.mTrend = 0;
        sx.mLevel = sx.mPrevLevel = norm.x;
        sy.mLevel = sy.mPrevLevel = norm.y;
        sy.mTrend = 0;
        sz.mLevel = sz.mPrevLevel = norm.z;
        sz.mTrend = 0;
    }
}
