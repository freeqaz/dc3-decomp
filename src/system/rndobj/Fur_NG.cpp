#include "Fur_NG.h"
#include "rnddx9/RenderState.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/Shader.h"
#include "rndobj/ShaderOptions.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "math/Vec.h"

bool NgFur::Prep(RndMesh *, RndMat *) const {
    TheShaderMgr.SetPConstant((PShaderConstant)12, mFurDetail);
    TheRenderState.SetTextureFilter(12, (RndRenderState::FilterMode)1, false);
    return true;
}
bool NgFur::Shell(int param1, RndMesh *param2, RndMat *param3) const {
    double tmp1 = 0.0;
    double tmp2 = 0.0;
    double tmp3 = 0.0;

    Vector4 v1(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 v2(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 v3(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 v4(0.0f, 0.0f, 0.0f, 0.0f);

    TheShaderMgr.SetPConstant((PShaderConstant)0x32, v1);
    TheShaderMgr.SetPConstant((PShaderConstant)0xc, v2);
    TheShaderMgr.SetPConstant((PShaderConstant)0x33, v3);
    TheShaderMgr.SetPConstant((PShaderConstant)0xb, v4);

    RndShader::SelectConfig(param3, (ShaderType)8, false);

    if (param1 == 0) {
        TheRenderState.SetBlend((RndRenderState::Blend)1, (RndRenderState::Blend)0, (RndRenderState::Blend)1, (RndRenderState::Blend)1);
        TheRenderState.SetDepthTestEnable(true);
        TheRenderState.SetDepthWriteEnable(true);
        TheRenderState.SetDepthFunc((RndRenderState::TestFunc)1);
    }
    return true;
}

NgFur::NgFur() {}
