#include "rndobj/Env_NG.h"
#include "rndobj/Mat_NG.h"
#include "rndobj/Rnd.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/Stats_NG.h"

NgEnviron::NgEnviron()
    : mProjectedBlend(), mNumLightsReal(0), mNumLightsApprox(0), mNumLightsPoint(0),
      mNumLightsProj(0), mHasPointCubeTex(0) {}

void NgEnviron::UpdateApproxLighting(const Vector3 *pos) {
    mNumLightsApprox = 0;
    bool hasLights = !mLightsReal.empty() || !mLightsApprox.empty();
    bool useApprox = UsesApproxLocal() || UsesApproxGlobal();
    if (useApprox && hasLights) {
        static BoxMapLighting sBoxLight;
        static Hmx::Color sBoxResults[6];
        for (int i = 0; i < 6; i++) {
            sBoxResults[i].Set(0, 0, 0);
        }
        if (UsesApproxLocal()) {
            sBoxLight.Clear();
            for (ObjPtrList<RndLight>::iterator it = mLightsApprox.begin();
                 it != mLightsApprox.end(); ++it) {
                if (sBoxLight.QueueLight(*it, 1.0f))
                    mNumLightsApprox++;
            }
            sBoxLight.ApplyQueuedLights(sBoxResults, pos);
        }
        if (UsesApproxGlobal()) {
            unsigned int num = sGlobalLighting.NumQueuedLights();
            if (num > 0) {
                mNumLightsApprox += num;
                sGlobalLighting.ApplyQueuedLights(sBoxResults, pos);
            }
        }
        for (int i = 0; i < 6; i++) {
            Vector4 v(sBoxResults[i].red, sBoxResults[i].green, sBoxResults[i].blue, sBoxResults[i].alpha);
            TheShaderMgr.SetVConstant((VShaderConstant)(0x50 + i), v);
            Vector4 v2(sBoxResults[i].red, sBoxResults[i].green, sBoxResults[i].blue, sBoxResults[i].alpha);
            TheShaderMgr.SetPConstant((PShaderConstant)(0x50 + i), v2);
        }
    } else {
        static Hmx::Color sDefaultColor(0, 0, 0, 0);
        for (int i = 0; i < 6; i++) {
            Vector4 v(sDefaultColor.red, sDefaultColor.green, sDefaultColor.blue, sDefaultColor.alpha);
            TheShaderMgr.SetVConstant((VShaderConstant)(0x50 + i), v);
            Vector4 v2(sDefaultColor.red, sDefaultColor.green, sDefaultColor.blue, sDefaultColor.alpha);
            TheShaderMgr.SetPConstant((PShaderConstant)(0x50 + i), v2);
        }
    }
    if (mNumLightsReal > 0) {
        if (mNumLightsApprox <= 1)
            mNumLightsApprox = 1;
    }
}

void NgEnviron::Select(const Vector3 *pos) {
    mNumLightsReal = 0;
    mNumLightsApprox = 0;
    mNumLightsPoint = 0;
    mNumLightsProj = 0;
    mHasPointCubeTex = false;
    mProjectedBlend = (RndLight::ProjectedBlend)0;

    Rnd::DrawMode mode = TheRnd.GetDrawMode();
    if (mode == 4 || mode == 2 || mode == 6 || mode == 3) {
        RndEnviron::Select(pos);
        NgMat::SetCurrent(0);
        TheNgStats->mLightsReal += mNumLightsReal;
        TheNgStats->mLightsApprox += mNumLightsApprox;
        return;
    }

    ReclassifyLights();
    RndEnviron::Select(pos);
    NgMat::SetCurrent(0);
    TheNgStats->mLightsReal += mNumLightsReal;
    TheNgStats->mLightsApprox += mNumLightsApprox;
}
