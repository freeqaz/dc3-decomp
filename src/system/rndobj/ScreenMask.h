#pragma once
#include "rndobj\Draw.h"
#include "rndobj\Mat.h"
#include "math\Color.h"
#include "math/Geo.h"

/** "Draws full screen quad with material and color." */
class RndScreenMask : public RndDrawable {
public:
    OBJ_CLASSNAME(ScreenMask);
    OBJ_SET_TYPE(ScreenMask);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);
    virtual void DrawShowing();

    OBJ_MEM_OVERLOAD(0x17);
    NEW_OBJ(RndScreenMask)
    static void Init() { REGISTER_OBJ_FACTORY(RndScreenMask) }
#ifdef HX_NATIVE
    // What its quad draws with: the native load-time pipeline warm
    // (native/src/platform/PipelineWarm.cpp) asks for its rect pipeline.
    RndMat *GetMat() const { return mMat; }
#endif

protected:
    RndScreenMask();

    /** "Material to draw on full screen quad" */
    ObjPtr<RndMat> mMat; // 0x40
    /** "Color of full screen quad" */
    Hmx::Color mColor; // 0x54
    /** "The area of the screen in normalized coordinates (0 to 1) to draw into." */
    Hmx::Rect mRect; // 0x64
    /** "Use current camera screen_rect instead of the full screen" */
    bool mUseCamRect; // 0x74
};
