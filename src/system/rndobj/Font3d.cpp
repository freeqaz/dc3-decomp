#include "rndobj/Font.h"
#include "math/Utl.h"
#include "os/Debug.h"

bool RndFont3d::HasChar(unsigned short us) const {
    return mCharInfoMap.find(us) != mCharInfoMap.end();
}

RndFont3d::RndFont3d()
    : mMat(this), mTextureOwner(this, this), mCellSize(0, 0, 0), mInvCellSize(0, 0, 0),
      unk8c(0, 0, 0) {}

void RndFont3d::Clear() {
    FOREACH (it, mCharInfoMap) {
        delete it->second;
    }
    mCharInfoMap.clear();
    mChars.clear();
    RELEASE(mKerningTable);
}

BEGIN_HANDLERS(RndFont3d)
    HANDLE_SUPERCLASS(RndFontBase)
END_HANDLERS

BEGIN_PROPSYNCS(RndFont3d)
    SYNC_SUPERCLASS(RndFontBase)
END_PROPSYNCS

void RndFont3d::Save(BinStream &bs) {
    bs << 0;
    RndFontBase::Save(bs);
    bs << mMat;
    bs << mTextureOwner;
    bs << mCellSize;
    bs << mInvCellSize;
    bs << unk8c;
    int size = mCharInfoMap.size();
    bs << size;
    FOREACH (it, mCharInfoMap) {
        bs << it->first;
        CharInfo *info = it->second;
        bs << info->unk0;
        bs << info->advance;
        bs << info->mMesh;
        bs << info->visible;
    }
}

BEGIN_COPYS(RndFont3d)
    COPY_SUPERCLASS(RndFontBase)
    CREATE_COPY_AS(RndFont3d, f)
    MILO_ASSERT(f, 0xEB);
    mMat.CopyRef(f->mMat);
    mCellSize = f->mCellSize;
    mInvCellSize = f->mInvCellSize;
    unk8c = f->unk8c;
    FOREACH (it, mCharInfoMap) {
        delete it->second;
    }
    mCharInfoMap.clear();
    FOREACH (it2, f->mCharInfoMap) {
        CharInfo *info = new CharInfo();
        *info = *it2->second;
        mCharInfoMap[it2->first] = info;
    }
    if (ty == kCopyShallow || (ty == kCopyFromMax && f->mTextureOwner != f)) {
        COPY_MEMBER_FROM(f, mTextureOwner)
    } else {
        mTextureOwner = this;
    }
END_COPYS

#ifdef HX_NATIVE
BEGIN_LOADS(RndFont3d)
    LOAD_REVS(bs)
    LOAD_SUPERCLASS(RndFontBase)
    bs >> mMat;
    bs >> mTextureOwner;
    bs >> mCellSize;
    bs >> mInvCellSize;
    bs >> unk8c;
    int size;
    bs >> size;
    // Clear existing map
    FOREACH (it, mCharInfoMap) {
        delete it->second;
    }
    mCharInfoMap.clear();
    for (int i = 0; i < size; i++) {
        unsigned short key;
        bs >> key;
        CharInfo *info = new CharInfo();
        bs >> info->unk0;
        bs >> info->advance;
        // TODO: mMesh is ObjRefConcrete — serialized as name string, need to resolve via Dir
        String meshName;
        bs >> meshName;
        bs >> info->visible;
        mCharInfoMap[key] = info;
    }
END_LOADS
#endif

float RndFont3d::CharWidth(unsigned short c) const {
    if (mTextureOwner != this) {
        return mTextureOwner->CharWidth(c);
    }
    MILO_ASSERT(HasChar(c), 0xCA);
    CharInfo *info = mTextureOwner->mCharInfoMap.find(c)->second;
    float w = Max(info->unk0.mMax.x, 0.f);
    MILO_ASSERT(w >= 0.f, 0xCC);
    return FontUnit() * w;
}

bool RndFont3d::CharAdvance(unsigned short us1, unsigned short us2, float &f) const {
    if (mTextureOwner != this) {
        return mTextureOwner->CharAdvance(us1, us2, f);
    }
    std::map<unsigned short, CharInfo *>::const_iterator it = mCharInfoMap.find(us2);
    if (it != mCharInfoMap.end()) {
        CharInfo *info = it->second;
        if (info->unk0.Volume() > 0.0f || info->advance > 0.0f) {
            if (mMonospace) {
                f = 1.0f;
            } else {
                f = FontUnit() * info->advance;
            }
            f += Kerning(us1, us2);
            return true;
        }
    }
    return false;
}

float RndFont3d::CharAdvance(unsigned short us) const {
    if (mTextureOwner != this) {
        return mTextureOwner->CharAdvance(us);
    }
    MILO_ASSERT(HasChar(us), 0xD5);
    if (mMonospace)
        return 1.0f;
    CharInfo *info = mCharInfoMap.find(us)->second;
    float a = info->advance;
    MILO_ASSERT(a >= 0.0f, 0xDB);
    return FontUnit() * a;
}

float RndFont3d::Kerning(unsigned short us1, unsigned short us2) const {
    if (mTextureOwner != this) {
        return mTextureOwner->Kerning(us1, us2);
    }
    return RndFontBase::Kerning(us1, us2) * mInvCellSize.x;
}

float RndFont3d::AspectRatio() const {
    return mTextureOwner->mCellSize.z / mTextureOwner->mCellSize.x;
}

RndMat *RndFont3d::Mat() const { return mMat; }

const RndFontBase *RndFont3d::DataOwner() const { return mTextureOwner; }
