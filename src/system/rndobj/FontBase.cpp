#include "rndobj/FontBase.h"
#include "os/Debug.h"
#include "rndobj/Font.h"
#include "obj/Object.h"
#include "utl/BinStream.h"
#include "utl/UTF8.h"

float RndFontBase::Kerning(unsigned short us1, unsigned short us2) const {
    if (DataOwner() != this) {
        return DataOwner()->Kerning(us1, us2);
    } else if (us1 == 0 || us2 == 0)
        return 0;
    else if (!mMonospace && mKerningTable) {
        return mBaseKerning + mKerningTable->Kerning(us1, us2);
    } else
        return mBaseKerning;
}

bool RndFontBase::CharDefined(unsigned short us) const {
    if (DataOwner() != this) {
        return DataOwner()->CharDefined(us);
    } else
        return HasChar(us);
}

String RndFontBase::GetASCIIChars() const {
    if (DataOwner() != this) {
        return DataOwner()->GetASCIIChars();
    } else
        return WideVectorToASCII(mChars);
}

void RndFontBase::SetBaseKerning(float f1) {
    MILO_ASSERT(DataOwner() == this, 0x65);
    mBaseKerning = f1;
}

void RndFontBase::SetKerning(const std::vector<KernInfo> &kernInfo) {
    MILO_ASSERT(DataOwner() == this, 0x7C);
    if (kernInfo.empty()) {
        RELEASE(mKerningTable);
    } else {
        if (!mKerningTable) {
            mKerningTable = new KerningTable();
        }
        mKerningTable->SetKerning(kernInfo, this);
    }
}

void RndFontBase::SetASCIIChars(String str) {
    if (DataOwner() != this) {
        MILO_ASSERT(0, 0x167);
    } else {
        ASCIItoWideVector(mChars, str.c_str());
    }
}

#ifdef HX_NATIVE
BEGIN_LOADS(RndFontBase)
    LOAD_REVS(bs)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mChars;
    d >> mMonospace;
    d >> mBaseKerning;
    bool hasKerning;
    d >> hasKerning;
    RELEASE(mKerningTable);
    if (hasKerning) {
        mKerningTable = new KerningTable();
        mKerningTable->Load(d, this);
    }
END_LOADS
#endif

void RndFontBase::Save(BinStream &bs) {
    bs << 0;
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mChars;
    bs << mMonospace;
    bs << mBaseKerning;
    bs << (mKerningTable != nullptr);
    if (mKerningTable) {
        mKerningTable->Save(bs);
    }
}

#ifndef HX_NATIVE
INIT_REVS(0, 0)
BEGIN_LOADS(RndFontBase)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mChars;
    d >> mMonospace;
    d >> mBaseKerning;
    bool hasKerning;
    d >> hasKerning;
    if (hasKerning) {
        mKerningTable = new KerningTable();
        mKerningTable->Load(d, this);
    }
END_LOADS
#endif

bool RndFontBase::HasChar(unsigned short us) const {
    if (DataOwner() != this) {
        return DataOwner()->HasChar(us);
    } else {
        for (int i = 0; i < mChars.size(); i++) {
            if (mChars[i] == us)
                return true;
        }
        return false;
    }
}

RndFontBase::RndFontBase() : mMonospace(0), mBaseKerning(0), mKerningTable(nullptr) {}

BEGIN_HANDLERS(RndFontBase)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndFontBase)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void RndFontBase::GetKerning(std::vector<KernInfo> &kernInfo) const {
    const RndFontBase *owner = DataOwner();
    while (owner != this) {
        owner = DataOwner();
    }
    if (owner->mKerningTable) {
        owner->mKerningTable->GetKerning(kernInfo);
    } else {
        kernInfo.clear();
    }
}

BEGIN_COPYS(RndFontBase)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY_AS(RndFontBase, f)
    MILO_ASSERT(f, 0x42);
    COPY_MEMBER_FROM(f, mChars)
    COPY_MEMBER_FROM(f, mMonospace)
    if (ty != kCopyShallow) {
        if (ty != kCopyFromMax || DataOwner() == f) {
            mBaseKerning = DataOwner()->mBaseKerning;
            std::vector<KernInfo> kerns;
            DataOwner()->GetKerning(kerns);
            SetKerning(kerns);
        }
    }
END_COPYS
