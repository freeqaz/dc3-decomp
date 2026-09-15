#pragma once
#include "math\Mtx.h"
#include "obj/Object.h"
#include "rndobj\Mat.h"
#include "rnddx9\RenderState.h"

class NgMat : public RndMat {
    friend class RndShaderMultimesh;
    friend class RndShaderStandard;
    friend class RndShaderParticles;
    friend class RndShaderFur;
    friend class RndShaderSyncTrack;
public:
    NgMat();
    virtual ~NgMat();
    OBJ_CLASSNAME(Mat);
    OBJ_SET_TYPE(Mat);

    bool AllowFog() const;
    bool AllowHDR() const;
    void SetupShader(bool, bool);

    static Hmx::Object *NewObject();
    static NgMat *Current() { return sCurrent; }
    static void SetCurrent(NgMat *c) { sCurrent = c; }

protected:
    static NgMat *sCurrent;

    void SetupAmbient();
    void SetBasicState();
    void RefreshState();
    void SetRegularShaderConst(bool);

    float mTexHalfPixelX; // 0x22c
    float mTexHalfPixelY; // 0x230
    float mTexHalfPixelNegX; // 0x234
    float mTexHalfPixelNegY; // 0x238
    RndRenderState::Blend mBlendSrc; // 0x23c
    RndRenderState::Blend mBlendDest; // 0x240
    bool mDepthTestEnable; // 0x244
    bool mDepthWriteEnable; // 0x245
    RndRenderState::TestFunc mDepthFunc; // 0x248
    RndRenderState::TestFunc mStencilFunc; // 0x24c
    RndRenderState::StencilOp mStencilZFail; // 0x250
    Hmx::Matrix4 mTexGenMatrix; // 0x254
    Hmx::Matrix4 mTexGenMatrix2; // 0x294
    int mFadeOut; // 0x2d4
    // One Vector4, not four floats: RefreshState's blend arms store it through
    // an inlined Set(), which is what keeps the four stfs ahead of the mFadeOut
    // store in the image (0x8269D654-D664) -- w7-bt, 97.14 -> 98.9 canonical.
    Vector4 mFadeParams; // 0x2d8
    RndRenderState::BlendOp mBlendOp; // 0x2e8
    bool mBlendEnable; // 0x2ec
};
