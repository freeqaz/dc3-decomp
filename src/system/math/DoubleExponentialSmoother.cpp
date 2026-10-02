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
    // Residual (98.2% canonical / 97.4% raw, 16 rows, 4 B): all FPR-only.
    //   - rows 61/63 are the known plain commutative two-term same-register
    //     swaps inside the inlined DoubleExponentialSmoother::Smooth
    //     (`fmadds f13,f13,f11,f9` vs `f13,f11,f13,f9`) -- documented backend
    //     floor, see docs/decomp/patterns (stream3 commutative operand order).
    //   - rows 70/72: in the mZ expansion ONLY, the image loads mTrend (0x30)
    //     before mPrevLevel (0x2c); the mX and mY expansions of the same inline
    //     already match, so this is scheduling, not a source order to fix.
    //     Reordering the inline's `oldPrev`/`mTrend` reads would also reshape
    //     Vector2DESmoother::Smooth in the same TU.
    //   - rows 83-98: an f0/f12/f13 permutation plus one extra `lfs f12,
    //     0x0(r29)` the image issues (it RELOADS mZ.mLevel for the Vector3 val
    //     ctor instead of reusing the value it just stored).
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
