#include "rndobj/Mat_NG.h"
#include "BaseMaterial.h"
#include "ShaderMgr.h"
#include "math/Color.h"
#include "rnddx9/RenderState.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Env.h"
#include "rndobj/ShaderMgr.h"

NgMat *NgMat::sCurrent;

RndRenderState::ClampMode sTexWrapClampModes[6] = {
    (RndRenderState::ClampMode)2, (RndRenderState::ClampMode)0,
    (RndRenderState::ClampMode)6, (RndRenderState::ClampMode)6,
    (RndRenderState::ClampMode)1, (RndRenderState::ClampMode)0
};

NgMat::NgMat() {}

NgMat::~NgMat() {
    if (sCurrent == this)
        sCurrent = nullptr;
}

bool NgMat::AllowFog() const {
    return mBlend != kBlendDest && mBlend != kBlendAdd && mBlend != kBlendSubtract
        && mBlend != kBlendSrcAlphaAdd;
}

bool NgMat::AllowHDR() const {
    return (mBlend != kBlendSrcAlpha && mBlend != kBlendSrcAlphaAdd
            && mBlend != kPreMultAlpha)
        && !mAlphaCut && !mAlphaWrite;
}

void NgMat::SetupShader(bool b1, bool b2) {
    if (b2)
        SetupAmbient();
    if (this != sCurrent || mDirty) {
        if (mDirty & 2) {
            RefreshState();
        }
        SetBasicState();
        if (b2) {
            SetRegularShaderConst(b1);
        }
        mDirty = 0;
        sCurrent = this;
    }
}

void NgMat::SetBasicState() {
    RndRenderState::CullMode cm;
    switch (mCull) {
    case kCullNone:
        cm = (RndRenderState::CullMode)0;
        break;
    case kCullRegular:
        cm = (RndRenderState::CullMode)2;
        break;
    case kCullBackwards:
        cm = (RndRenderState::CullMode)6;
        break;
    default:
        cm = (RndRenderState::CullMode)2;
        break;
    }
    TheRenderState.SetCullMode(cm);
    TheRenderState.SetBlendEnable(mBlendEnable);
    TheRenderState.SetBlendOp(mBlendOp);
    TheRenderState.SetBlend(unk23c, unk240, unk23c, unk240);
    TheRenderState.SetAlphaTestEnable(mAlphaCut);
    if (mAlphaCut) {
        TheRenderState.SetAlphaFunc((RndRenderState::TestFunc)5, mAlphaThreshold);
    }
    TheRenderState.SetDepthTestEnable(mDepthTestEnable);
    TheRenderState.SetDepthWriteEnable(mDepthWriteEnable);
    TheRenderState.SetDepthFunc(mDepthFunc);
    if (mStencilMode == kStencilIgnore) {
        TheRenderState.SetStencilTestEnable(false);
    } else {
        TheRenderState.SetStencilTestEnable(true);
        TheRenderState.SetStencilFunc(mStencilFunc, 0);
        TheRenderState.SetStencilOp(
            (RndRenderState::StencilOp)0, (RndRenderState::StencilOp)0, unk250
        );
    }
    RndRenderState::ClampMode cur = sTexWrapClampModes[mTexWrap];
    if (mDiffuseTex) {
        TheRenderState.SetTextureClamp(0, cur);
    }
    if (mDiffuseTex2) {
        TheRenderState.SetTextureClamp(6, cur);
    }
    if (mNormalMap) {
        TheRenderState.SetTextureClamp(1, cur);
    }
    if (mSpecularMap) {
        TheRenderState.SetTextureClamp(2, cur);
    }
    if (mEmissiveMap) {
        TheRenderState.SetTextureClamp(3, cur);
    }
    if (cur == 6) {
        bool white = mTexWrap == kTexBorderWhite;
        TheRenderState.SetBorderColor(0, white);
        TheRenderState.SetBorderColor(6, white);
        TheRenderState.SetBorderColor(1, white);
        TheRenderState.SetBorderColor(2, white);
        TheRenderState.SetBorderColor(3, white);
    }
}

void NgMat::SetupAmbient() {
    f32 x, y, z, w;
    if (mUseEnviron) {
        const Vector4 &v4 =
            reinterpret_cast<const Vector4 &>(RndEnviron::Current()->AmbientColor());
        x = v4.x;
        y = v4.y;
        z = v4.z;
        w = v4.w;
    } else {
        x = 1.0f;
        y = 1.0f;
        z = 1.0f;
        w = 1.0f;
    }
    Vector4 v4_1(x, y, z, w);
    TheShaderMgr.SetVConstant((VShaderConstant)1, v4_1);
    Vector4 v4_2(x, y, z, w);
    TheShaderMgr.SetPConstant((PShaderConstant)1, v4_2);
}
