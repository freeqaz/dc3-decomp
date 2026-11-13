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
    // Lane history (11 lines moved): docs/decomp/notes/system/math/DoubleExponentialSmoother.md
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

// Lane history (13 lines moved): docs/decomp/notes/system/math/DoubleExponentialSmoother.md
// > We used to skip the write on a zero length and never touched mPrevLevel. (w14-f)
void Vector2DESmoother::Smooth(Vector2 v, float dt, bool normalize) {
    // Residual (canonical 100, fuzzy 99.87013, 1 row): the same commutative
    // operand order as the Vector3 expansions, in this function's SECOND
    // inlined DoubleExponentialSmoother::Smooth (mY). The c2 operand dump puts
    // `change` at nodes 7 and the clamped `newBeta` at nodes 6, so the order is
    // fixed by node count before the sid hash, and no uniform spelling can make
    // the mY expansion differ from mX. 13 named/operator/const-ref spellings
    // measured inert, 1 worse; see the note in Vector3DESmoother::Smooth.
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
    // Lane history (55 lines moved): docs/decomp/notes/system/math/DoubleExponentialSmoother.md
    // > Residual (100 normalized, fuzzy 99.74, 3 rows): commutative operand order in the
    // > 2nd and 3rd inlined DoubleExponentialSmoother::Smooth (w25-oa, c2's commutative
    // > sort key, C2RS-BRIDGE 8.7).
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
