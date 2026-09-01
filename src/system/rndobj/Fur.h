#pragma once
#include "math\Color.h"
#include "obj/Object.h"
#include "obj/Object.h"
#include "rndobj\Tex.h"
#include "rndobj\Wind.h"
#include "utl/BinStream.h"
#include "utl\MemMgr.h"

// size 0x9c
/** "Parameters for fur shading, to be set on a material" */
class RndFur : public Hmx::Object {
public:
    OBJ_CLASSNAME(Fur);
    OBJ_SET_TYPE(Fur);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);
    /** ??_7RndFur@@6B@ slots +0x58 and +0x5C (target 24 slots, we had 22).
     *  Both ?Prep@RndFur@@UBA_NPAVRndMesh@@PAVRndMat@@@Z and
     *  ?Shell@RndFur@@UBA_NHPAVRndMesh@@PAVRndMat@@@Z sit at 0x82AEAE70
     *  ("li r3,0; blr") in ham_xbox_r.map.  Because both bodies ICF-folded to
     *  the SAME address, the vtable cannot witness their relative order.
     *  ESTABLISHED 2026-09-01 by the CALL SITES instead: DxMesh::DrawFur
     *  (Mesh.s 0x8262231C / 0x826223F8) dispatches the 2-argument
     *  (RndMesh*, RndMat*) call through slot 0x58 and the 3-argument
     *  (int, RndMesh*, RndMat*) call through slot 0x5C, which is this
     *  order.  Writing that body reproduces both loads with no offset
     *  mismatch. */
    virtual bool Prep(class RndMesh *, class RndMat *) const { return false; }
    virtual bool Shell(int, class RndMesh *, class RndMat *) const { return false; }

    bool LoadOld(BinStreamRev &);
    RndTex* GetFurDetail() const { return mFurDetail; }
    int Layers() const { return mLayers; }
    RndWind* GetWind() const { return mWind; }
    float GetFluidity() const { return mFluidity; }

    OBJ_MEM_OVERLOAD(0x1A)
    NEW_OBJ(RndFur)
    static void Init() { REGISTER_OBJ_FACTORY(RndFur); }

protected:
    RndFur();

    /** "Number of passes" */
    int mLayers; // 0x2c
    /** "Length of fur" */
    float mThickness; // 0x30
    /** "Curvature exponent". Ranges from 0 to 3. */
    float mCurvature; // 0x34
    /** "Bunch shells towards surface". Ranges from 0 to 1. */
    float mShellOut; // 0x38
    /** "Bunch opacity towards surface". Ranges from 0 to 1. */
    float mAlphaFalloff; // 0x3c
    /** "Maximum stretch" */
    float mStretch; // 0x40
    /** "Maximum lateral motion" */
    float mSlide; // 0x44
    /** "Strength of gravity". Ranges from 0 to 1. */
    float mGravity; // 0x48
    /** "Langor of motion". Ranges from 0 to 1. */
    float mFluidity; // 0x4c
    /** "Tint at hair roots" */
    Hmx::Color mRootsTint; // 0x50
    /** "Tint at hair ends" */
    Hmx::Color mEndsTint; // 0x60
    /** "Detail map for finer fur.  Only the alpha channel is used." */
    ObjPtr<RndTex> mFurDetail; // 0x70
    /** "Tiling for fur detail map.
        UVs of fur_detail are multiplied by this value."
        Ranges from 2.0e-2 to 100.
    */
    float mFurTiling; // 0x84
    /** "Wind Object, if set, blows on the fur." */
    ObjPtr<RndWind> mWind; // 0x88
};
