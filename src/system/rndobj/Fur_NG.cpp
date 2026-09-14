#include "Fur_NG.h"
#include "rnddx9\RenderState.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\Shader.h"
#include "rndobj\ShaderOptions.h"
#include "rndobj\Mat.h"
#include "rndobj\Mat_NG.h"
#include "rndobj\Mesh.h"
#include "math\Vec.h"

bool NgFur::Prep(RndMesh *, RndMat *) const {
    TheShaderMgr.SetPConstant(kPS_FurDetail, mFurDetail);
    TheRenderState.SetTextureFilter(12, (RndRenderState::FilterMode)1, false);
    return true;
}
bool NgFur::Shell(int layerIdx, RndMesh *mesh, RndMat *mat) const {
    float zeroVal = 0.0f;
    float fShell;
    float curveVal;
    if (layerIdx != 0) {
        fShell = (float)layerIdx / (float)(mLayers - 1);
    } else {
        fShell = zeroVal;
    }
    if (layerIdx != 0) {
        curveVal = (float)pow((double)fShell, (double)mCurvature);
    } else {
        curveVal = zeroVal;
    }

    // Constant 0x32: fur geometry params
    float gravStretch = mGravity * mStretch;
    float gravSlide = mGravity * mSlide;
    Vector4 furGeom(
        mStretch * fShell,
        mSlide * curveVal,
        gravStretch * fShell,
        gravSlide * curveVal
    );
    // Fur geometry (stretch/slide) drives vertex displacement, so it is set as a
    // VERTEX-shader constant: target dispatches vtable+0x24 = SetVConstant(Vector4&),
    // not +0x40 = SetPConstant(Vector4&). The register index value is shared between
    // the VS/PS constant enums (0x32).
    TheShaderMgr.SetVConstant((VShaderConstant)kPS_FurGeometry, furGeom);

    // Constant 0xc: color interpolation between roots and ends tints
    float diffRed = (mEndsTint.red - mRootsTint.red);
    float diffGreen = (mEndsTint.green - mRootsTint.green);
    float diffBlue = (mEndsTint.blue - mRootsTint.blue);
    float diffAlpha = (mEndsTint.alpha - mRootsTint.alpha);
    // Residual (96.86%, 9 rows, 16 B): the image does NOT contract these four
    // multiply-adds.  It emits four grouped `fmuls fN, fN, f31` and then four
    // separate `fadds` (827329 7C..A8); we emit four `fmadds fN*f31+roots`.
    // The same function contracts in TWO other places and the image agrees
    // there -- `fnmsubs f2, f13, f0, f30` (shellExponent) and
    // `fmadds f2, f13, f0, f30` (alphaExp) -- so this is per-expression, which
    // is what docs/decomp/patterns/fixable-fsel-fma.md already records for
    // NgFur::Shell.
    // REFUTED: `#pragma fp_contract(off)` bracketing the whole function is
    // BYTE-INERT here -- identical 96.9%, identical 9 rows, and the two
    // expressions that SHOULD have de-fused if the pragma were honoured did
    // not move either.  This Xenon cl silently accepts and ignores the pragma
    // (no C4068), so the doc's "Category 1: pure OFF" fix does not exist on
    // this toolchain; only a volatile intermediate (which would add the stack
    // traffic the image does not have) or a c2.dll patch would separate them.
    diffRed = diffRed * fShell;
    diffGreen = diffGreen * fShell;
    diffBlue = diffBlue * fShell;
    diffAlpha = diffAlpha * fShell;
    Vector4 furColor(
        mRootsTint.red + diffRed,
        mRootsTint.green + diffGreen,
        mRootsTint.blue + diffBlue,
        mRootsTint.alpha + diffAlpha
    );
    TheShaderMgr.SetPConstant(kPS_FurColor, furColor);

    // Constant 0x33: shell thickness and vertex data
    float oneVal = 1.0f;
    float shellExponent = -(mShellOut * 0.7f - oneVal);
    float shellThickness;
    if (layerIdx != 0) {
        // Residual row [101]: image `fmuls f13, f13, f0` (thickness first), we
        // emit `fmuls f13, f0, f13`.  REFUTED: writing it as
        // `(float)pow(...) * mThickness` is byte-inert -- a plain two-term
        // same-register commutative swap, the known backend floor.
        shellThickness = mThickness * (float)pow((double)fShell, (double)shellExponent);
    } else {
        shellThickness = mThickness / (float)mLayers;
    }

    int numBones = (int)mesh->NumBones();
    float vertCount;
    if (numBones > 1) {
        vertCount = (float)numBones;
    } else {
        vertCount = oneVal;
    }

    Vector4 furShell(shellThickness, vertCount, zeroVal, zeroVal);
    // Fur shell thickness/vertex-count is a VERTEX-shader constant (target dispatches
    // vtable+0x24 = SetVConstant(Vector4&), not +0x40 = SetPConstant(Vector4&)).
    TheShaderMgr.SetVConstant((VShaderConstant)kPS_FurShell, furShell);

    // Constant 0xb: alpha processing params
    float alphaExp = mAlphaFalloff * 2.0f + oneVal;
    float alphaResult;
    if (layerIdx != 0) {
        float fShellFull = (float)layerIdx / (float)mLayers;
        alphaResult = (float)pow((double)fShellFull, (double)alphaExp);
    } else {
        alphaResult = zeroVal;
    }

    float alphaScale = oneVal / (oneVal - alphaResult);
    float alphaBias = -(alphaScale * alphaResult);
    Vector4 furAlpha(alphaScale, alphaBias, mFurTiling, zeroVal);
    TheShaderMgr.SetPConstant(kPS_FurAlpha, furAlpha);

    RndShader::SelectConfig(mat, (ShaderType)8, false);

    if (layerIdx == 0) {
        TheRenderState.SetBlend((RndRenderState::Blend)1, (RndRenderState::Blend)0, (RndRenderState::Blend)1, (RndRenderState::Blend)1);
        TheRenderState.SetDepthTestEnable(true);
        TheRenderState.SetDepthWriteEnable(true);
        TheRenderState.SetDepthFunc((RndRenderState::TestFunc)1);
        NgMat::SetCurrent(nullptr);
    }
    return true;
}

NgFur::NgFur() {}
