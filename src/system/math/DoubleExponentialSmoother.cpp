#include "math/DoubleExponentialSmoother.h"

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
    mSmootherX.SetCoeffs(alpha, beta);
    mSmootherY.SetCoeffs(alpha, beta);
}

void Vector2DESmoother::ForceValue(Vector2 v) {
    mSmootherX.mLevel = mSmootherX.mPrevLevel = v.x;
    mSmootherX.mTrend = 0;
    mSmootherY.mPrevLevel = mSmootherY.mLevel = v.y;
    mSmootherY.mTrend = 0;
}

Vector2 Vector2DESmoother::Value() const {
    return Vector2(mSmootherX.mLevel, mSmootherY.mLevel);
}

Vector3DESmoother::Vector3DESmoother(Vector3 v, float alpha, float beta)
    : mSmootherX(v.x, alpha, beta), mSmootherY(v.y, alpha, beta),
      mSmootherZ(v.z, alpha, beta) {}

void Vector3DESmoother::SetSmoothParameters(float alpha, float beta) {
    mSmootherX.SetCoeffs(alpha, beta);
    mSmootherY.SetCoeffs(alpha, beta);
    mSmootherZ.SetCoeffs(alpha, beta);
}

Vector3 Vector3DESmoother::Value() const {
    return Vector3(mSmootherX.mLevel, mSmootherY.mLevel, mSmootherZ.mLevel);
}

void Vector3DESmoother::ForceValue(Vector3 v) {
    mSmootherX.SetParams(v.x, v.x, 0);
    mSmootherY.SetParams(v.y, v.y, 0);
    mSmootherZ.SetParams(v.z, v.z, 0);
}
