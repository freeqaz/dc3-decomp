#include "rndobj\Rnd_NG.h"
#include "Env_NG.h"
#include "PostProc.h"
#include "math\Color.h"
#include "math\Vec.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj/Cam.h"
#include "rndobj\Flare.h"
#include "rndobj\Fur_NG.h"
#include "rndobj\Lit_NG.h"
#include "rndobj\Mat_NG.h"
#include "rndobj\Overlay.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\ShadowMap.h"
#include "rndobj\SoftParticleBuffer.h"
#include "rndobj\Stats_NG.h"
#include "rndobj\Tex.h"
#include "utl\MakeString.h"
#include "rndobj\PostProc_NG.h"
#include "rndobj\DOFProc_NG.h"

extern "C" void OnlyReturns();

NgStats gNgStats[3];
NgStats *TheNgStats = &gNgStats[0];

NgRnd::NgRnd()
    : mViewport(), mLowRes(0), mShadowMap(0), mShadowCam(0), mOcclusionQueryMgr(0), mParticleBuffer(0),
      mInited(0) {}

NgRnd::~NgRnd() {}

#ifdef HX_NATIVE
// The image's renderer is DxRnd, whose Offscreen() (rnddx9/Rnd.s 8260FE78)
// answers GetRenderTarget(0) != BackBuffer(): true while anything renders to a
// texture. WgpuRnd inherited this class's `return false`, so a RndTexRenderer
// drawing a character into a texture read as on-screen. Character::DrawShowing
// then prepped a self-shadow there (the image skips it offscreen), and the
// extrude pass -- DrawLodOrShadow mode 4 with no mShadow falls back to
// DrawOpaque -- reached a forced CharTransDraw holding the character itself:
// unbounded recursion (party route, after Strike a Pose: RndTexRenderer::
// DrawToTexture -> player0 -> projection_trans_draw.td -> player0 -> ...).
// Rendering to a texture is selecting a camera with a target texture
// (RndTexRenderer::DrawToTexture: SetTargetTex, Select ... SetTargetTex(null)).
bool NgRnd::Offscreen() const {
    RndCam *cam = RndCam::Current();
    return cam && cam->TargetTex();
}
#endif

void NgRnd::PreInit() {
    if (!mInited) {
        mInited = true;
        Rnd::PreInit();
        REGISTER_OBJ_FACTORY(NgEnviron)
        REGISTER_OBJ_FACTORY(NgMat)
        NgLight::Init();
        REGISTER_OBJ_FACTORY(NgFur)
        RndShadowMap::Init();
        REGISTER_OBJ_FACTORY(RndSoftParticleBuffer)
        CreateDefaults();
    }
}

void NgRnd::Init() {
    PreInit();
    TheShaderMgr.Init();
    mPointTestQueries.reserve(0x200);
    Rnd::Init();
    mParticleBuffer = Hmx::Object::New<RndSoftParticleBuffer>();
}

void NgRnd::ReInit() { TheShaderMgr.InitShaders(); }

void NgRnd::Terminate() {
    RELEASE(mOcclusionQueryMgr);
    RELEASE(mParticleBuffer);
    TheShaderMgr.Terminate();
    NgPostProc::Terminate();
    NgDOFProc::Terminate();
    RndShadowMap::Terminate();
    OnlyReturns();
    Rnd::Terminate();
}

void NgRnd::SetShadowMap(RndTex *tex, RndCam *cam, const Hmx::Color *color) {
    mShadowMap = tex;
    mShadowCam = cam;
    if (cam) {
        const Vector3 &v3 = cam->WorldXfm().m.y;
        Vector4 v4(v3.x, v3.y, v3.z, 1);
        TheShaderMgr.SetPConstant(kPS_ShadowCamDir, v4);
    }
    if (color) {
        Vector4 v4 = Vector4(color->red, color->green, color->blue, color->alpha);
        TheShaderMgr.SetPConstant(kPS_ShadowColor, v4);
    }
    if (tex) {
        TheShaderMgr.SetPConstant(kPS_Texture, tex);
    }
}

void NgRnd::RemovePointTest(RndFlare *flare) {
    Rnd::RemovePointTest(flare);
    FOREACH (it, mPointTestQueries) {
        if (it->mFlare == flare) {
            mOcclusionQueryMgr->ReleaseQuery(it->mAreaQueryIdx);
            mOcclusionQueryMgr->ReleaseQuery(it->mPointQueryIdx);
            mPointTestQueries.erase(it);
            return;
        }
    }
}

#ifdef HX_NATIVE
extern void FlushPostProcessingForOverlay();
#endif

void NgRnd::DoPostProcess() {
    Rnd::DoPostProcess();
#ifdef HX_NATIVE
    // The image's NgRnd runs the post chain here, at Rnd::EndWorld: the world
    // is resolved and graded, and whatever the frame draws after EndWorld
    // (WorldDir's mHUD -- WorldDir::DrawShowing calls TheRnd.EndWorld() before
    // mHUD->DrawShowing() when explicit_postproc is set -- and the UI) lands on
    // the graded frame, ungraded. The WebGPU backend composites the post chain
    // in a separate pass; FlushPostProcessingForOverlay() is that pass, run at
    // the same point. Without it the dc3 backend graded at EndDrawing, so the
    // game HUD (postprocs_before_draw FALSE in its .milo) was drawn into the
    // world target and graded with it, and the grade itself ran with whatever
    // camera/postproc was current by then.
    if (!Offscreen())
        FlushPostProcessingForOverlay();
#endif
}

void NgRnd::CreateLargeQuad(int, int, LargeQuadRenderData &) {
#ifndef HX_NATIVE
    MILO_FAIL("NgRnd::CreateLargeQuad not implemented!");
#endif
}

void NgRnd::DrawLargeQuad(
    const LargeQuadRenderData &, const Transform &, RndMat *, ShaderType
) {
#ifndef HX_NATIVE
    MILO_FAIL("NgRnd::DrawLargeQuad not implemented!");
#endif
}

void NgRnd::SetVertShaderTex(RndTex *, int) {
#ifndef HX_NATIVE
    MILO_FAIL("NgRnd::SetVertShaderTex not implemented!");
#endif
}

void NgRnd::ResetStats() {
    if (mProcCmds == kProcessWorld || mProcCmds == kProcessAll) {
        TheNgStats = &gNgStats[0];
    } else if (mProcCmds == kProcessPost) {
        TheNgStats = &gNgStats[1];
    } else {
        TheNgStats = &gNgStats[2];
    }
    memset(TheNgStats, 0, sizeof(NgStats));
    TheNgStats->mCams++;
}

// w8-l: 99.808690 normalized, and the ONLY function keeping this unit from
// 100% (25/26).  Exactly two rows are charged, both in the address setup:
//   [5] addi r8, r11, 0x4  (target)  vs  0x8 (ours)
//   [8] addi r5, r11, 0x8  (target)  vs  0x4 (ours)
// Everything else is equal.  The two rows mean only this: in the INNERMOST add
// of the chain the image fuses mParts into the fmadds and leaves mPartSys as
// the standalone fmuls, while we do the reverse.  The value computed is the
// same, and the constants follow their fields on both sides (target [4] f0 =
// __real@3ba3d70a = 0.005 paired with +0x8 = mPartSys; [39] f13 =
// __real@3974aaf1 = 0.00023333334 paired with +0x4 = mParts).  Field offsets
// are corroborated by NgRnd::UpdateOverlay, which is matched in this unit.
// Refuted, each a full ninja + report.json read:
//   - swapping the two innermost terms       -> 99.808690 (EXACTLY inert)
//   - writing both innermost muls constant-first -> 99.808690 (inert)
//   - naming the mPartSys product in a local  -> 99.808690 (inert)
//   - flat left-associated sum (same accumulation order, no nesting)
//                                            -> 52.530434
//   - binding `NgStats &stats = gNgStats[idx]` -> 18.591305
// The right-nested spelling below is therefore the correct shape; MSVC
// canonicalises which half of the bottom pair gets fused and no source
// rewriting found here moves it.
float EstimateDraw(int idx) {
    return (float)gNgStats[idx].mMotionBlurs * 0.003f + ((float)gNgStats[idx].mFlares * 0.017f + ((float)gNgStats[idx].mMultiMeshInsts * 0.001f + ((float)gNgStats[idx].mLightsApprox * 0.01f + ((float)gNgStats[idx].mLightsReal * 0.001f + ((float)gNgStats[idx].mCams * 0.0068f + ((float)gNgStats[idx].mMats * 0.0097f + ((float)gNgStats[idx].mBones * 0.00126f + ((float)gNgStats[idx].mMutMeshes * 0.0112f + ((float)gNgStats[idx].mRegMeshes * 0.0028f + ((float)gNgStats[idx].mPartSys * 0.005f + (float)gNgStats[idx].mParts * 0.00023333334f))))))))));
}

float NgRnd::UpdateOverlay(RndOverlay *overlay, float y) {
    if (overlay == mStatsOverlay) {
        mStatsOverlay->Clear();
        if (mProcCmds == kProcessWorld || mProcCmds == kProcessPost) {
            *mStatsOverlay
                << MakeString("faces %d %d\n", gNgStats[0].mFaces, gNgStats[1].mFaces);
            *mStatsOverlay
                << MakeString("parts %d %d\n", gNgStats[0].mParts, gNgStats[1].mParts);
            *mStatsOverlay << MakeString(
                "part_sys %d %d\n", gNgStats[0].mPartSys, gNgStats[1].mPartSys
            );
            *mStatsOverlay << MakeString(
                "reg_meshes %d %d\n", gNgStats[0].mRegMeshes, gNgStats[1].mRegMeshes
            );
            *mStatsOverlay << MakeString(
                "mut_meshes %d %d\n", gNgStats[0].mMutMeshes, gNgStats[1].mMutMeshes
            );
            *mStatsOverlay
                << MakeString("bones %d %d\n", gNgStats[0].mBones, gNgStats[1].mBones);
            *mStatsOverlay
                << MakeString("mats %d %d\n", gNgStats[0].mMats, gNgStats[1].mMats);
            *mStatsOverlay
                << MakeString("cams %d %d\n", gNgStats[0].mCams, gNgStats[1].mCams);
            *mStatsOverlay << MakeString(
                "lights (real) %d %d\n", gNgStats[0].mLightsReal, gNgStats[1].mLightsReal
            );
            *mStatsOverlay << MakeString(
                "lights (approx) %d %d\n",
                gNgStats[0].mLightsApprox,
                gNgStats[1].mLightsApprox
            );
            *mStatsOverlay << MakeString(
                "multimesh instances %d %d\n",
                gNgStats[0].mMultiMeshInsts,
                gNgStats[1].mMultiMeshInsts
            );
            *mStatsOverlay << MakeString(
                "multimesh batches %d %d\n",
                gNgStats[0].mMultiMeshBatches,
                gNgStats[1].mMultiMeshBatches
            );
            *mStatsOverlay
                << MakeString("flares %d %d\n", gNgStats[0].mFlares, gNgStats[1].mFlares);
            *mStatsOverlay << MakeString(
                "motion blur %d %d\n", gNgStats[0].mMotionBlurs, gNgStats[1].mMotionBlurs
            );
            *mStatsOverlay << MakeString(
                "spotlights %d %d\n", gNgStats[0].mSpotlights, gNgStats[1].mSpotlights
            );
            *mStatsOverlay
                << MakeString("est draw %.1f %.1f\n", EstimateDraw(0), EstimateDraw(1));
            TheNgStats = &gNgStats[2];
        } else {
            *mStatsOverlay << MakeString("faces %d\n", gNgStats[0].mFaces);
            *mStatsOverlay << MakeString("parts %d\n", gNgStats[0].mParts);
            *mStatsOverlay << MakeString("part_sys %d\n", gNgStats[0].mPartSys);
            *mStatsOverlay << MakeString("reg_meshes %d\n", gNgStats[0].mRegMeshes);
            *mStatsOverlay << MakeString("mut_meshes %d\n", gNgStats[0].mMutMeshes);
            *mStatsOverlay << MakeString("bones %d\n", gNgStats[0].mBones);
            *mStatsOverlay << MakeString("mats %d\n", gNgStats[0].mMats);
            *mStatsOverlay << MakeString("cams %d\n", gNgStats[0].mCams);
            *mStatsOverlay << MakeString("lights (real) %d\n", gNgStats[0].mLightsReal);
            *mStatsOverlay
                << MakeString("lights (approx) %d\n", gNgStats[0].mLightsApprox);
            *mStatsOverlay
                << MakeString("multimesh instances %d\n", gNgStats[0].mMultiMeshInsts);
            *mStatsOverlay
                << MakeString("multimesh batches %d\n", gNgStats[0].mMultiMeshBatches);
            *mStatsOverlay << MakeString("flares %d\n", gNgStats[0].mFlares);
            *mStatsOverlay << MakeString("motion blur %d\n", gNgStats[0].mMotionBlurs);
            *mStatsOverlay << MakeString("spotlights %d\n", gNgStats[0].mSpotlights);
            *mStatsOverlay << MakeString("est draw %.1f\n", EstimateDraw(0));
            TheNgStats = &gNgStats[2];
        }
        return y;
    } else {
        return Rnd::UpdateOverlay(overlay, y);
    }
}
