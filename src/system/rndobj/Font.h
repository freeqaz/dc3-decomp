#pragma once
#include "math/Geo.h"
#include "obj/Object.h"
#include "rndobj\Bitmap.h"
#include "rndobj\Mat.h"
#include "rndobj\Mesh.h"
#include "rndobj\Tex.h"
#include "utl/BinStream.h"
#include "utl\MemMgr.h"

class RndFontBase : public Hmx::Object {
    friend class UIFontImporter;
    friend class RndText;
public:
    class KernInfo {
    public:
        unsigned short mFirstChar, mSecondChar;
        float kerning; // 0x4
    };

    OBJ_CLASSNAME(FontBase);
    OBJ_SET_TYPE(FontBase);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);
    virtual float CharWidth(unsigned short) const { return 0; }
    virtual float CharAdvance(unsigned short) const { return 0; }
    virtual bool CharAdvance(unsigned short, unsigned short, float &) const {
        return false;
    }
    virtual float Kerning(unsigned short, unsigned short) const;
    virtual bool CharDefined(unsigned short) const;
    virtual float AspectRatio() const { return 0; }
    virtual RndMat *Mat() const { return nullptr; }
    virtual const RndFontBase *DataOwner() const { return this; }
    virtual float FontUnit() const { return 0; }
    virtual float FontUnitInverse() const { return 1.0f / FontUnit(); }
    virtual void Print() const {}
    /** 0x82AEAE70 'li r3,0; blr' -- FALSE in the original; only RndFont
     * overrides it to true, RndFont3d inherits this. */
    virtual bool BitmapFont() const { return false; }

    OBJ_MEM_OVERLOAD(0x1C)
    NEW_OBJ(RndFontBase)
    static void Init() { REGISTER_OBJ_FACTORY(RndFontBase) }

    void SetBaseKerning(float);
    void SetKerning(const std::vector<KernInfo> &);
    void GetKerning(std::vector<KernInfo> &) const;
    bool IsMonospace() const { return mMonospace; }
    const std::vector<unsigned short> &Chars() const { return mChars; }
    float BaseKerning() const { return mBaseKerning; }

protected:
    RndFontBase();
    virtual bool HasChar(unsigned short) const;

    String GetASCIIChars() const;
    void SetASCIIChars(String);

    std::vector<unsigned short> mChars; // 0x2c
    bool mMonospace; // 0x38
    float mBaseKerning; // 0x3c
    class KerningTable *mKerningTable; // 0x40
};

class KerningTable {
public:
    class Entry {
    public:
        Entry *next; // 0x0
        int key; // 0x4
        float kerning; // 0x8
    };
    KerningTable();
    ~KerningTable();
    float Kerning(unsigned short, unsigned short);
    void GetKerning(std::vector<RndFontBase::KernInfo> &) const;
    void SetKerning(const std::vector<RndFontBase::KernInfo> &, RndFontBase *);
    Entry *Find(unsigned short, unsigned short);
    void Save(BinStream &);
    void Load(BinStreamRev &, RndFontBase *);
    bool Valid(const RndFontBase::KernInfo &, RndFontBase *);

    int Key(unsigned short us0, unsigned short us2) {
        return (us0 & 0xFFFF) | ((us2 << 0x10) & 0xFFFF0000);
    }
    int Size() const { return mNumEntries * sizeof(Entry) + 0x88; }
    int TableIndex(unsigned short us0, unsigned short us2) { return (us0 ^ us2) & 0x1F; }

    MEM_OVERLOAD(KerningTable, 0x162);

    int mNumEntries; // 0x0
    Entry *mEntries; // 0x4
    Entry *mTable[32]; // 0x8
};

class RndFont : public RndFontBase {
public:
    struct CharInfo {
        int mPage; // 0x0
        float mU;
        float mV;
        float charWidth; // 0xc
        float mAdvance;
    };
    virtual ~RndFont();
    virtual bool Replace(ObjRef *, Hmx::Object *);
    /** ?BitmapFont@RndFont@@ is at 0x82E2AB00 ('li r3,1; blr'), 'f'. */
    virtual bool BitmapFont() const;
    OBJ_CLASSNAME(Font);
    OBJ_SET_TYPE(Font);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);
    virtual float CharWidth(unsigned short) const;
    virtual float CharAdvance(unsigned short) const;
    virtual bool CharAdvance(unsigned short, unsigned short, float &) const;
    virtual bool CharDefined(unsigned short) const;
    virtual float AspectRatio() const { return mCellSize.y / mCellSize.x; }
    virtual RndMat *Mat() const {
        if (mMats.size() > 0)
            return (RndMat *)mMats[0];
        else
            return nullptr;
    }
    virtual const RndFontBase *DataOwner() const { return mTextureOwner; }
    virtual float FontUnit() const { return mCellSize.x; }
    virtual void Print() const;

    OBJ_MEM_OVERLOAD(0x7C)
    NEW_OBJ(RndFont)
    static void Init() { REGISTER_OBJ_FACTORY(RndFont) }

    RndMat *Mat(int) const;
    RndTex *ValidTexture(int) const;
    void SetCellSize(float, float);
    int CharPage(unsigned short) const;
    void BleedTest();
    bool
    CharWidthAdvanceCoords(unsigned short, float &, float &, Vector2 &, Vector2 &) const;
    int NumMats() const { return mMats.size(); }
    float DeprecatedSize() const { return mDeprecatedSize; }

protected:
    RndFont();
    virtual bool HasChar(unsigned short) const;
    virtual void SetASCIIChars(String);

    void SetCharInfo(CharInfo *, RndBitmap &, const Vector2 &, int);
    void UpdateChars();
    void SetBitmapSize(const Vector2 &);

    ObjPtrVec<RndMat> mMats; // 0x44
    ObjOwnerPtr<RndFont> mTextureOwner; // 0x60
    std::map<unsigned short, CharInfo> mCharInfoMap; // 0x74
    Vector2 mCellSize; // 0x8c
    float mDeprecatedSize; // 0x94
    std::vector<Vector2> mMaterialOffsets; // 0x98
    bool mPacked; // 0xa4
};

class RndFont3d : public RndFontBase {
public:
    struct CharInfo {
        CharInfo() : mMesh(nullptr) {}
        ~CharInfo() {}

        Box unk0; // 0x0
        float advance; // 0x20
        ObjPtr<RndMesh> mMesh; // 0x24
        bool visible; // 0x38

        MEM_OVERLOAD(CharInfo, 0x12A);
    };
    virtual ~RndFont3d() { Clear(); }
    OBJ_CLASSNAME(Font3d);
    OBJ_SET_TYPE(Font3d);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);
    virtual float CharWidth(unsigned short) const;
    virtual float CharAdvance(unsigned short) const;
    virtual bool CharAdvance(unsigned short, unsigned short, float &) const;
    virtual float Kerning(unsigned short, unsigned short) const;
    virtual float AspectRatio() const;
    virtual RndMat *Mat() const;
    virtual const RndFontBase *DataOwner() const;
    virtual float FontUnit() const { return mTextureOwner->mCellSize.x; }
    virtual float FontUnitInverse() const { return mTextureOwner->mInvCellSize.x; }

    OBJ_MEM_OVERLOAD(0x10A)
    NEW_OBJ(RndFont3d)
    static void Init() { REGISTER_OBJ_FACTORY(RndFont3d) }

    CharInfo *GetCharInfo(unsigned short) const;
    Vector3 CharOriginOffset() const;
    bool CharWidthAdvanceMesh(unsigned short, float &, float &, RndMesh **) const;

protected:
    RndFont3d();

    virtual bool HasChar(unsigned short) const;

    void Clear();

    ObjPtr<RndMat> mMat; // 0x44
    ObjOwnerPtr<RndFont3d> mTextureOwner; // 0x58
    Vector3 mCellSize; // 0x6c
    Vector3 mInvCellSize; // 0x7c
    Vector3 unk8c; // 0x8c
    std::map<unsigned short, CharInfo *> mCharInfoMap; // 0x9c
};

class BitmapLocker {
    friend class RndFont;

public:
    BitmapLocker(RndFont *, int);
    ~BitmapLocker();
    void LoadPage(int);

private:
    RndFont *mFont; // 0x0
    RndTex *mTex; // 0x4
    RndBitmap *mBitmapPtr; // 0x8
    RndBitmap mBitmap; // 0xc
};
