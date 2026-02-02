#include "rndobj/ColorXfm.h"
#include "utl/BinStream.h"

void RndColorXfm::Reset() { mColorXfm.Reset(); }

void RndColorXfm::AdjustContrast() {
    Transform tf58;
    tf58.Reset();
    float contrast = mContrast / 100.0f;
    if (contrast > 0) {
        contrast = 1.0f / (contrast * -0.9921875f + 1.0f);
    } else {
        contrast = -contrast * 0.992126f + 1.0f;
    }
    float f2 = (1.0f - contrast) * 0.5f;
    tf58.m[2][2] = contrast;
    tf58.m[1][1] = contrast;
    tf58.m[0][0] = contrast;
    tf58.v.Set(f2, f2, f2);
    Multiply(mColorXfm, tf58, mColorXfm);
}

void RndColorXfm::AdjustBrightness() {
    Transform tf;
    tf.Reset();
    float set = (mBrightness + 100.0f) / 200.0f + -0.5f;
    tf.v.Set(set, set, set);
    Multiply(mColorXfm, tf, mColorXfm);
}

void RndColorXfm::Save(BinStream &bs) const {
    bs << 0;
    bs << mColorXfm;
    bs << mHue << mSaturation << mLightness;
    bs << mContrast << mBrightness;
    bs << mLevelInLo << mLevelInHi;
    bs << mLevelOutLo << mLevelOutHi;
}

bool RndColorXfm::Load(BinStream &bs) {
    int rev;
    bs >> rev;
    if (rev > 0)
        return false;
    else {
        bs >> mColorXfm;
        bs >> mHue >> mSaturation >> mLightness >> mContrast >> mBrightness;
        bs >> mLevelInLo >> mLevelInHi;
        bs >> mLevelOutLo >> mLevelOutHi;
        return true;
    }
}

RndColorXfm::RndColorXfm()
    : mHue(0), mSaturation(0), mLightness(0), mContrast(0), mBrightness(0),
      mLevelInLo(0, 0, 0), mLevelInHi(1, 1, 1), mLevelOutLo(0, 0, 0),
      mLevelOutHi(1, 1, 1) {
    Reset();
}

void RndColorXfm::AdjustLightness() {
    Transform tf58;
    tf58.Reset();
    float lit = mLightness / 100.0f;
    float f1 = 0;
    float f3;
    if (lit >= 0) {
        f3 = 1.0f - lit;
        f1 = lit;
    } else {
        f3 = lit + 1.0f;
    }
    tf58.m[2][2] = f3;
    tf58.m[1][1] = f3;
    tf58.m[0][0] = f3;
    tf58.v.Set(f1, f1, f1);
    Multiply(mColorXfm, tf58, mColorXfm);
}

void RndColorXfm::AdjustColorXfm() {
    mColorXfm.Reset();
    AdjustHue();
    AdjustSaturation();
    AdjustLightness();
    AdjustContrast();
    AdjustBrightness();
    AdjustLevels();
}
