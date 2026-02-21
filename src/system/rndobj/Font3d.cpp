#include "rndobj/Font.h"

bool RndFont3d::HasChar(unsigned short us) const {
    return mCharInfoMap.find(us) != mCharInfoMap.end();
}

RndFont3d::RndFont3d()
    : mMat(this), mTextureOwner(this, this), unk6c(0, 0, 0), unk7c(0, 0, 0),
      unk8c(0, 0, 0) {}

void RndFont3d::Clear() {
    FOREACH (it, mCharInfoMap) {
        delete it->second;
    }
    mCharInfoMap.clear();
    mChars.clear();
    RELEASE(mKerningTable);
}
