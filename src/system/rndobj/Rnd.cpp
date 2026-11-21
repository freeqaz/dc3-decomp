#include "rndobj\Rnd.h"
#include "rndobj\AmbientOcclusion.h"
#include "rndobj\CamAnim.h"
#include "rndobj\Enter.h"
#include "rndobj\Group.h"
#include "rndobj\Morph.h"
#include "rndobj\MotionBlur.h"
#include "rndobj\MultiMeshProxy.h"
#include "rndobj\PollAnim.h"
#include "rndobj\PostProcMgr.h"
#include "rndobj\PropAnim.h"
#include "rndobj\ScreenMask.h"
#include "rndobj\Shockwave.h"
#include "rndobj\SoftParticles.h"
#include "rndobj\Spline.h"
#include "rndobj/TexBlendController.h"
#include "rndobj\TexBlender.h"
#include "math\Color.h"
#include "obj\DataFunc.h"
#include "obj\Dir.h"
#include "obj\Utl.h"
#include "os\Debug.h"
#include "os\OSFuncs.h"
#include "os\System.h"
#include "os\Timer.h"
#include "rndobj\AnimFilter.h"
#include "rndobj\BaseMaterial.h"
#include "rndobj/Cam.h"
#include "rndobj\Console.h"
#include "rndobj\CubeTex.h"
#include "rndobj\DOFProc.h"
#include "rndobj\Dir.h"
#include "rndobj\Draw.h"
#include "rndobj\Env.h"
#include "rndobj\EventTrigger.h"
#include "rndobj\Flare.h"
#include "rndobj\Font.h"
#include "rndobj\FontBase.h"
#include "rndobj\Fur.h"
#include "rndobj\Gen.h"
#include "rndobj\Graph.h"
#include "rndobj\HiResScreen.h"
#include "rndobj\Line.h"
#include "rndobj\Lit.h"
#include "rndobj\LitAnim.h"
#include "rndobj\Mat.h"
#include "rndobj\MatAnim.h"
#include "rndobj\Mesh.h"
#include "rndobj\MeshAnim.h"
#include "rndobj\MeshDeform.h"
#include "rndobj\MetaMaterial.h"
#include "rndobj/Movie.h"
#include "rndobj/MultiMesh.h"
#include "rndobj\MultiMeshProxy.h"
#include "rndobj\Part.h"
#include "rndobj\PartAnim.h"
#include "rndobj\PartLauncher.h"
#include "rndobj\Ribbon.h"
#include "rndobj\Set.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\ShaderOptions.h"
#include "rndobj\Tex.h"
#include "rnddx9\Tex.h"
#include "rndobj\TexProc.h"
#include "rndobj/TexRenderer.h"
#include "rndobj\Text.h"
#include "rndobj\Trans.h"
#include "rndobj\TransAnim.h"
#include "rndobj\TransProxy.h"
#include "rndobj\Utl.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "rndobj\Bitmap.h"
#include "rndobj\Overlay.h"
#include "rndobj\PostProc.h"
#include "rndobj\Wind.h"
#include "utl\Cheats.h"
#include "utl\FileStream.h"
#include "utl\Option.h"
#include "utl\TextStream.h"
#include "os\Joypad.h"
#include "os\PlatformMgr.h"
#include "xdk\XAPILIB.h"

// Rnd & TheRnd;

// The order of this block is load bearing. The target's Rnd.obj .bss run is
//
//   sPostProcPanelCount 0x830A3FA8  gNotifyKeepGoing    0x830A3FB1
//   sTexture            0x830A3FAC  gFailKeepGoing      0x830A3FB2
//   sCompressDone       0x830A3FB0  gFailRestartConsole 0x830A3FB3
//                                   sCompressData       0x830A3FB4
//                                   gRndThread          0x830A3FB8
//                                   gRndTextureEvent    0x830A3FBC
//
// and Init/Terminate/DrawPreClear/CompressThread reach the compression
// hand-off variables through one base register with baked-in displacements
// (Init stores gRndTextureEvent as gRndThread + 4, DrawPreClear reads
// sTexture as gRndTextureEvent - 0x10). MSVC lays .bss out in reverse
// declaration order, so this block is written back-to-front.
// Internal linkage on purpose: MSVC only folds same-TU statics into one
// base register (anonymous-namespace and extern globals each get their own
// relocation), and the target reaches gRndTextureEvent as gRndThread + 4.
static HANDLE gRndTextureEvent;
static HANDLE gRndThread;
static void *sCompressData;
// These three are static for the same reason as the block above: Rnd::Modal
// reaches all three through ONE base register (gFailRestartConsole as the
// anchor, gFailKeepGoing at -1, gNotifyKeepGoing at -2). With external
// linkage MSVC emits a separate lis/addi pair per global, which costs two
// extra callee-saved registers and 16 bytes of frame. Nothing outside this
// TU references them.
// Static for the same reason as the block above, and measured: Rnd::Modal
// reaches all three through ONE base register (gFailRestartConsole as the
// anchor, gFailKeepGoing at -0x1, gNotifyKeepGoing at -0x2; see 0x8266219C
// in build/373307D9/asm/system/rndobj/Rnd.s).  With external linkage MSVC
// emits a separate lis/addi pair per global, which costs two extra
// callee-saved registers and 16 bytes of frame.  Nothing outside this TU
// references them.
static bool gFailRestartConsole;
static bool gFailKeepGoing;
static bool gNotifyKeepGoing;
static bool sCompressDone;
#ifdef HX_NATIVE
static void *sTexture;
#else
static DxTex *sTexture;
#endif
int Rnd::sPostProcPanelCount;

extern int lbl_82F14008;
extern DataArray *lbl_830A4100;
extern int lbl_830A4104;
void MemPrintOverview(int, char *const);

DataNode ModalKeyListener::OnMsg(const KeyboardKeyMsg &k) {
    if (k.GetKey() == 0x12e) {
        if (!GetEnabledKeyCheats() && !TheRnd.ConsoleShowing()) {
            TheRnd.ShowConsole(true);
            return 0;
        } else
            return DATA_UNHANDLED;
    } else {
        if (!TheRnd.ConsoleShowing()) {
            gNotifyKeepGoing = true;
            return 0;
        } else
            return DATA_UNHANDLED;
    }
}

BEGIN_HANDLERS(ModalKeyListener)
    HANDLE_MESSAGE(KeyboardKeyMsg)
END_HANDLERS

DataNode FailKeepGoing(DataArray *) {
    gFailKeepGoing = true;
    return 0;
}

DataNode FailRestartConsole(DataArray *) {
    gFailRestartConsole = true;
    return 0;
}

Rnd::Rnd()
    : mClearColor(0.3f, 0.3f, 0.3f), mWidth(640), mHeight(480), mScreenBpp(16),
      mDrawCount(0), mDrawTimer(), mTimersOverlay(0), mRateOverlay(0), mHeapOverlay(0),
      mWatchOverlay(0), mStatsOverlay(0), mDefaultMat(0), mOverlayMat(0), mOverdrawMat(0),
      mDefaultCam(0), mWorldCamCopy(0), mDefaultEnv(0), mDefaultLit(0), mDefaultCubeTexBlack(nullptr),
      mDefaultCubeTexWhite(nullptr), mRateTotal(0), mRateCount(5), mFrameID(0), mRateGate("    "),
      mFont(nullptr), mSync(1), mGSTiming(0), mShowSafeArea(0), mDrawing(0),
      mWorldEnded(1), mAspect(kWidescreen), mDrawMode(kDrawNormal), mResourceCached(0), mShowShaderCost(0),
      mShrinkToSafe(1), mInGame(0), mVerboseTimers(0), mDisablePostProc(0), unk146(0),
      mWorldCamCopied(0), unk148(0), mWorldEndCallback(0), unk150(0), mPostProcOverride(this),
      mPostProcBlackLightOverride(nullptr), mPreClearDraws(this), mDraws(this), mReleaseImmediate(0),
      mProcCmds(kProcessAll), mLastProcCmds(kProcessAll) {
    for (int i = 0; i < 8; i++)
        mDefaultTex[i] = nullptr;
}

#ifdef HX_NATIVE
#include "platform/LookProfile.h"
#include "platform/rndshape/RndShape.h"
#include "gfx/DisplayRamp.h"
#include "gfx/PostProcPass.h"
// Native inspection (np-gfx-post round 10): {rnd native_look retail|enhanced}
// switches the engine's DC3_LOOK profile at run time, so one frozen frame can
// be graded both ways; post_retail|post_enhanced switch only the post chain's
// fields (the rest of the profile, e.g. alpha-cut coverage, untouched);
// post_precision|post_bloom_filter|post_dof_filter turn on that one post field
// alone (the per-item cost A/B); specular_aa_on|specular_aa_off switch
// specular antialiasing alone (item 18 c's shimmer and cost A/B);
// lod_bias_0|lod_bias_m05|lod_bias_m1 put that world texture LOD bias at
// every render scale and lod_bias_rule restores the look's rule by scale
// (item 19 a's A/B); aniso_1|aniso_4|aniso_16 put the world samplers' max
// anisotropy at that value and aniso_rule restores the look's (item 20 e's
// GPU-cost A/B; text and HUD untouched); refl_cull_force|refl_cull_flip|
// refl_cull_rule set the cull of a WorldReflection's mirrored redraw to
// retail's every-material-Backwards, the old per-material flip, or the shape's
// rule (item 20 b's per-venue before/after from one launch; a cull change
// selects another cached pipeline, nothing is rebuilt per frame);
// noise_rows_on|noise_rows_off|noise_rows_rule switch the post noise map's
// output-row re-tiling (np-look23 a; enhanced's rule on, retail's off);
// dof_depth_weight_on|dof_depth_weight_off|dof_depth_weight_rule switch the
// enhanced depth of field's depth-weighted blur (np-look23 c);
// dof_near_on|_off|_rule and dof_near_spread_on|_off|_rule switch its near
// field (np-look36: an out-of-focus object in front of the focus takes the
// depth-blind blur; the spread mixes its blur over the figure beside it; the
// field compiles four pipelines once, the spread is a uniform; each rule is
// by adapter class, on for a discrete GPU, and the on and off tokens force
// it on any GPU);
// dof_edge_on|dof_edge_off|dof_edge_rule switch the enhanced depth of field's
// edge rule (np-look51, LookSettings::postDofEdge: the composite's mix amount
// from every depth sample of the pixel, not sample 0 alone, so an in-focus
// outline over the blurred background keeps its anti-aliasing; by adapter
// class, on for a discrete GPU; on and off force it on any GPU; the first
// switch on compiles one pipeline);
// hair_edge_on|hair_edge_off|hair_edge_rule switch the enhanced cutout's
// alpha test per MSAA sample (np-look26 d; LookSettings::cutoutSampleTest; a
// uniform, nothing compiles): on = mode E, off = the landed sharpened
// coverage, rule = the look's own value, which is off in every look
// (np-look26 g: measured, not adopted, PLAN H-155);
// hair_edge_b|hair_edge_c|hair_edge_e|hair_edge_centre pick one
// CutoutSampleMode (np-look26 e; inspection only, centre a diagnostic);
// display_tv|display_srgb|display_off|display_rule pick the scan-out curve
// (item 31, rndshape::SetDisplayTypeForInspection): the ramp corrected for a
// TV (the console's D3D::GammaRampCorrected, the default), the ramp alone (a
// VGA-cabled console), no ramp (the front buffer), or DC3_DISPLAY_GAMMA's /
// the default; one 256-entry table upload, nothing compiles after the first;
// crowd_shadow_on|crowd_shadow_off|crowd_shadow_rule switch the soft contact
// shadows under the 3D crowd (np-look32, LookSettings::crowdContactShadow;
// enhanced's rule on, retail's off; nothing compiles after the first);
// rtt_msaa_on|off|rule (step 1) and rtt_scale_on|off|rule (step 2) switch how
// a render-to-texture target the look draws like the frame is drawn
// (np-look47, LookSettings::texPassMsaa / texPassScale: 4x MSAA resolved into
// it, and a GPU texture sized for the output's rows, changed at the target's
// next clearing pass); rtt_off, rtt_step1, rtt_on and rtt_rule set both
// (enhanced's rule both on, retail's both off; rtt_rule also returns the two
// sizes below to the look's);
// rtt_cap_1024|rtt_cap_1536|rtt_cap_2048|rtt_cap_rule set step 2's cap on the
// longer side (texPassMaxSide) and rtt_rows_360|rtt_rows_480|rtt_rows_720|
// rtt_rows_rule its base rows (texPassBaseRows), each number for every adapter
// class (the _rule tokens return each class's to the look's), so one launch
// times one size against another; neither turns step 2 on or off, and a
// target takes the new size at its next clearing pass
// (SetLookTexPassSizeForInspection);
// adapter_apple|adapter_other|adapter_discrete|adapter_rule force the adapter
// class the look's per-adapter rules use (np-look49: the near field and
// its spread, the edge rule, the texture pass's MSAA and size; an Apple
// GPU, any other non-discrete GPU, a discrete GPU) or return it to the
// adapter's own; each rule takes it as it takes its own token, and nothing
// that sizes the frame reads it (SetLookAdapterClassForInspection);
// text_bias_off|text_bias_half|text_bias_on|text_bias_rule put the text and
// HUD LOD bias (np-look34, LookSettings::uiLodBias) at 0 / -0.5 / -1 or the
// look's value; text_levels_2|text_levels_all|text_levels_rule set the text
// and HUD samplers' level clamp (uiMipLevels) to two levels, none, or the
// look's (a sampler and bind-group cache miss, as the aniso tokens);
// text_bias_glyphs|text_bias_all_ui|text_bias_scope_rule give the bias to
// glyph pages only, to every text/HUD draw, or the look's choice (uniforms:
// nothing compiles; item 34b's held-frame A/B);
// {rnd native_look} alone reports it. Returns the profile
// now in force.
static Symbol NativeLook(const DataArray *msg) {
    if (msg->Size() > 2) {
        const Symbol s = msg->Sym(2);
        if (s == "retail") milo::SetLookProfileForInspection(milo::LookProfile::Retail);
        else if (s == "enhanced") milo::SetLookProfileForInspection(milo::LookProfile::Enhanced);
        else if (s == "post_retail") milo::SetLookPostForInspection(false);
        else if (s == "post_enhanced") milo::SetLookPostForInspection(true);
        else if (s == "post_precision") milo::SetLookPostFieldsForInspection(true, false, false);
        else if (s == "post_bloom_filter") milo::SetLookPostFieldsForInspection(false, true, false);
        else if (s == "post_dof_filter") milo::SetLookPostFieldsForInspection(false, false, true);
        else if (s == "specular_aa_on") milo::SetLookSpecularAAForInspection(true);
        else if (s == "specular_aa_off") milo::SetLookSpecularAAForInspection(false);
        else if (s == "lod_bias_0") milo::SetLookLodBiasForInspection(false, 0.0f);
        else if (s == "lod_bias_m05") milo::SetLookLodBiasForInspection(false, -0.5f);
        else if (s == "lod_bias_m1") milo::SetLookLodBiasForInspection(false, -1.0f);
        else if (s == "lod_bias_rule") milo::SetLookLodBiasForInspection(true, 0.0f);
        else if (s == "aniso_1") milo::SetLookAnisotropyForInspection(1);
        else if (s == "aniso_4") milo::SetLookAnisotropyForInspection(4);
        else if (s == "aniso_16") milo::SetLookAnisotropyForInspection(16);
        else if (s == "aniso_rule") milo::SetLookAnisotropyForInspection(0);
        else if (s == "refl_cull_force") milo::SetReflectionCullForInspection(1);
        else if (s == "refl_cull_flip") milo::SetReflectionCullForInspection(0);
        else if (s == "refl_cull_rule") milo::SetReflectionCullForInspection(-1);
        else if (s == "noise_rows_on") milo::SetLookNoiseRowsForInspection(1);
        else if (s == "noise_rows_off") milo::SetLookNoiseRowsForInspection(0);
        else if (s == "noise_rows_rule") milo::SetLookNoiseRowsForInspection(-1);
        else if (s == "dof_depth_weight_on") milo::SetLookDofDepthWeightForInspection(1);
        else if (s == "dof_depth_weight_off") milo::SetLookDofDepthWeightForInspection(0);
        else if (s == "dof_depth_weight_rule") milo::SetLookDofDepthWeightForInspection(-1);
        // np-look36: the depth weight's near field, and its spread.
        else if (s == "dof_near_on") milo::SetLookDofNearForInspection(1);
        else if (s == "dof_near_off") milo::SetLookDofNearForInspection(0);
        else if (s == "dof_near_rule") milo::SetLookDofNearForInspection(-1);
        else if (s == "dof_near_spread_on") milo::SetLookDofNearForInspection(1, true);
        else if (s == "dof_near_spread_off") milo::SetLookDofNearForInspection(0, true);
        else if (s == "dof_near_spread_rule") milo::SetLookDofNearForInspection(-1, true);
        // np-look51: the depth of field's edge rule.
        else if (s == "dof_edge_on") milo::SetLookDofEdgeForInspection(1);
        else if (s == "dof_edge_off") milo::SetLookDofEdgeForInspection(0);
        else if (s == "dof_edge_rule") milo::SetLookDofEdgeForInspection(-1);
        else if (s == "hair_edge_on") milo::SetLookCutoutSampleTestForInspection(1);
        else if (s == "hair_edge_off") milo::SetLookCutoutSampleTestForInspection(0);
        else if (s == "hair_edge_rule") milo::SetLookCutoutSampleTestForInspection(-1);
        else if (s == "hair_edge_b") milo::SetLookCutoutModeForInspection(milo::kCutoutModeB);
        else if (s == "hair_edge_c") milo::SetLookCutoutModeForInspection(milo::kCutoutModeC);
        else if (s == "hair_edge_e") milo::SetLookCutoutModeForInspection(milo::kCutoutModeE);
        else if (s == "hair_edge_centre") milo::SetLookCutoutModeForInspection(milo::kCutoutModeCentre);
        else if (s == "display_tv") rndshape::SetDisplayTypeForInspection(DisplayRamp::kDisplayTV);
        else if (s == "display_srgb") rndshape::SetDisplayTypeForInspection(DisplayRamp::kDisplaySRGB);
        else if (s == "display_off") rndshape::SetDisplayTypeForInspection(0);
        else if (s == "display_rule") rndshape::SetDisplayTypeForInspection(-1);
        else if (s == "crowd_shadow_on") milo::SetLookCrowdShadowForInspection(1);
        else if (s == "crowd_shadow_off") milo::SetLookCrowdShadowForInspection(0);
        else if (s == "crowd_shadow_rule") milo::SetLookCrowdShadowForInspection(-1);
        else if (s == "rtt_msaa_on") milo::SetLookTexPassMsaaForInspection(1);
        else if (s == "rtt_msaa_off") milo::SetLookTexPassMsaaForInspection(0);
        else if (s == "rtt_msaa_rule") milo::SetLookTexPassMsaaForInspection(-1);
        else if (s == "rtt_scale_on") milo::SetLookTexPassScaleForInspection(1);
        else if (s == "rtt_scale_off") milo::SetLookTexPassScaleForInspection(0);
        else if (s == "rtt_scale_rule") milo::SetLookTexPassScaleForInspection(-1);
        else if (s == "rtt_off") {
            milo::SetLookTexPassMsaaForInspection(0);
            milo::SetLookTexPassScaleForInspection(0);
        } else if (s == "rtt_step1") {
            milo::SetLookTexPassMsaaForInspection(1);
            milo::SetLookTexPassScaleForInspection(0);
        } else if (s == "rtt_on") {
            milo::SetLookTexPassMsaaForInspection(1);
            milo::SetLookTexPassScaleForInspection(1);
        } else if (s == "rtt_rule") {
            milo::SetLookTexPassMsaaForInspection(-1);
            milo::SetLookTexPassScaleForInspection(-1);
            milo::SetLookTexPassSizeForInspection(-1, -1);
        }
        else if (s == "rtt_cap_1024") milo::SetLookTexPassSizeForInspection(1024, 0);
        else if (s == "rtt_cap_1536") milo::SetLookTexPassSizeForInspection(1536, 0);
        else if (s == "rtt_cap_2048") milo::SetLookTexPassSizeForInspection(2048, 0);
        else if (s == "rtt_cap_rule") milo::SetLookTexPassSizeForInspection(-1, 0);
        else if (s == "rtt_rows_360") milo::SetLookTexPassSizeForInspection(0, 360);
        else if (s == "rtt_rows_480") milo::SetLookTexPassSizeForInspection(0, 480);
        else if (s == "rtt_rows_720") milo::SetLookTexPassSizeForInspection(0, 720);
        else if (s == "rtt_rows_rule") milo::SetLookTexPassSizeForInspection(0, -1);
        // np-look49: the adapter class the look's per-adapter rules use.
        else if (s == "adapter_apple") milo::SetLookAdapterClassForInspection(false, milo::LookAdapterClass::Apple);
        else if (s == "adapter_other")
            milo::SetLookAdapterClassForInspection(false, milo::LookAdapterClass::OtherNonDiscrete);
        else if (s == "adapter_discrete")
            milo::SetLookAdapterClassForInspection(false, milo::LookAdapterClass::Discrete);
        else if (s == "adapter_rule") milo::SetLookAdapterClassForInspection(true);
        else if (s == "text_bias_off") milo::SetLookUiLodBiasForInspection(false, 0.0f);
        else if (s == "text_bias_half") milo::SetLookUiLodBiasForInspection(false, -0.5f);
        else if (s == "text_bias_on") milo::SetLookUiLodBiasForInspection(false, -1.0f);
        else if (s == "text_bias_rule") milo::SetLookUiLodBiasForInspection(true, 0.0f);
        else if (s == "text_levels_2") milo::SetLookUiMipLevelsForInspection(2);
        else if (s == "text_levels_all") milo::SetLookUiMipLevelsForInspection(0);
        else if (s == "text_levels_rule") milo::SetLookUiMipLevelsForInspection(-1);
        else if (s == "text_bias_glyphs") milo::SetLookUiLodBiasScopeForInspection(1);
        else if (s == "text_bias_all_ui") milo::SetLookUiLodBiasScopeForInspection(0);
        else if (s == "text_bias_scope_rule") milo::SetLookUiLodBiasScopeForInspection(-1);
        else MILO_NOTIFY("native_look: %s is not retail, enhanced, post_retail, post_enhanced, "
                         "post_precision, post_bloom_filter, post_dof_filter, specular_aa_on, "
                         "specular_aa_off, lod_bias_0, lod_bias_m05, lod_bias_m1, "
                         "lod_bias_rule, aniso_1, aniso_4, aniso_16, aniso_rule, "
                         "refl_cull_force, refl_cull_flip, refl_cull_rule, noise_rows_on, "
                         "noise_rows_off, noise_rows_rule, dof_depth_weight_on, "
                         "dof_depth_weight_off, dof_depth_weight_rule, dof_near_on, dof_near_off, "
                         "dof_near_rule, dof_near_spread_on, dof_near_spread_off, "
                         "dof_near_spread_rule, dof_edge_on, dof_edge_off, dof_edge_rule, "
                         "hair_edge_on, "
                         "hair_edge_off, hair_edge_rule, hair_edge_b, hair_edge_c, "
                         "hair_edge_e, hair_edge_centre, display_tv, display_srgb, "
                         "display_off, display_rule, crowd_shadow_on, crowd_shadow_off, "
                         "crowd_shadow_rule, rtt_msaa_on, rtt_msaa_off, rtt_msaa_rule, "
                         "rtt_scale_on, rtt_scale_off, rtt_scale_rule, rtt_off, rtt_step1, "
                         "rtt_on, rtt_rule, rtt_cap_1024, rtt_cap_1536, rtt_cap_2048, "
                         "rtt_cap_rule, rtt_rows_360, rtt_rows_480, rtt_rows_720, "
                         "rtt_rows_rule, adapter_apple, adapter_other, adapter_discrete, "
                         "adapter_rule, text_bias_off, text_bias_half, text_bias_on, "
                         "text_bias_rule, text_levels_2, text_levels_all, text_levels_rule, "
                         "text_bias_glyphs, text_bias_all_ui or text_bias_scope_rule", s.Str());
    }
    return milo::LookProfileName(milo::Look().profile);
}

// Native inspection (np-look23 c): {rnd native_post_skip [mask]} leaves
// stages of the DC3 retail chain out of the frame (PostProcPass::sInspectSkip:
// 1 depth of field, 2 bloom, 4 motion blur, 8 camera velocity blur; 0 = none)
// and returns the mask in force.
static int NativePostSkip(const DataArray *msg) {
    if (msg->Size() > 2) PostProcPass::sInspectSkip = (unsigned)msg->Int(2);
    return (int)PostProcPass::sInspectSkip;
}

// Native inspection (np-look23 a): {rnd native_noise [scale]} scales every
// PostProc's noise intensity as the engine reads it (0 = off, 1 = authored)
// and reports the current PostProc's noise map and tiling.
static DataNode NativeNoise(const DataArray *msg) {
    if (msg->Size() > 2) RndPostProc::sNoiseInspectScale = msg->Float(2);
    const RndPostProc *pp = RndPostProc::Current();
    if (!pp) return DataNode("no current postproc");
    const RndTex *map = pp->GetNoiseMap();
    const Vector2 &base = pp->GetNoiseBaseScale();
    // {rnd native_noise scale "out.ppm"}: also write the map's bitmap as a PPM.
    if (msg->Size() > 3 && map) {
        const RndBitmap &bm = map->Bitmap();
        if (FILE *f = fopen(msg->Str(3), "wb")) {
            fprintf(f, "P6\n%d %d\n255\n", bm.Width(), bm.Height());
            for (int y = 0; y < bm.Height(); y++)
                for (int x = 0; x < bm.Width(); x++) {
                    u8 r, g, b, a;
                    bm.PixelColor(x, y, r, g, b, a);
                    const u8 px[3] = {r, g, b};
                    fwrite(px, 1, 3, f);
                }
            fclose(f);
        }
    }
    static char buf[512];
    snprintf(buf, sizeof(buf),
        "pp=%s map=%s file=%s size=%dx%d base=%g,%g top=%g intensity=%g stationary=%d "
        "midtone=%d inspect_scale=%g",
        pp->Name(), map ? map->Name() : "(none)", map ? map->File().c_str() : "",
        map ? map->Width() : 0, map ? map->Height() : 0, (double)base.x, (double)base.y,
        (double)pp->GetNoiseTopScale(), (double)pp->GetNoiseIntensityAuthored(), (int)pp->GetNoiseStationary(),
        (int)pp->GetNoiseMidtone(), (double)RndPostProc::sNoiseInspectScale);
    return DataNode(buf);
}
#endif

BEGIN_HANDLERS(Rnd)
    HANDLE_ACTION(reset_postproc, RndPostProc::Reset())
    HANDLE_ACTION(reset_dof_proc, RndPostProc::ResetDofProc())
    HANDLE_ACTION(set_postproc_override, SetPostProcOverride(_msg->Obj<RndPostProc>(2)))
    HANDLE_ACTION(
        set_postproc_blacklight_override,
        SetPostProcBlacklightOverride(_msg->Obj<RndPostProc>(2))
    )
    HANDLE_EXPR(get_postproc_override, GetPostProcOverride())
    HANDLE_EXPR(get_selected_postproc, GetSelectedPostProc())
    HANDLE_ACTION(
        set_dof_depth_scale, RndPostProc::DOFOverrides().SetDepthScale(_msg->Float(2))
    )
    HANDLE_ACTION(
        set_dof_depth_offset, RndPostProc::DOFOverrides().SetDepthOffset(_msg->Float(2))
    )
    HANDLE_ACTION(
        set_dof_min_scale, RndPostProc::DOFOverrides().SetMinBlurScale(_msg->Float(2))
    )
    HANDLE_ACTION(
        set_dof_min_offset, RndPostProc::DOFOverrides().SetMinBlurOffset(_msg->Float(2))
    )
    HANDLE_ACTION(
        set_dof_max_scale, RndPostProc::DOFOverrides().SetMaxBlurScale(_msg->Float(2))
    )
    HANDLE_ACTION(
        set_dof_max_offset, RndPostProc::DOFOverrides().SetMaxBlurOffset(_msg->Float(2))
    )
    HANDLE_ACTION(
        set_dof_width_scale, RndPostProc::DOFOverrides().SetBlurWidthScale(_msg->Float(2))
    )
    HANDLE_ACTION(set_aspect, SetAspect((Aspect)_msg->Int(2)))
    HANDLE_EXPR(aspect, mAspect)
    HANDLE_EXPR(screen_width, mWidth)
    HANDLE_EXPR(screen_height, mHeight)
    HANDLE_EXPR(highlight_style, RndDrawable::GetHighlightStyle())
    HANDLE_ACTION(
        set_highlight_style, RndDrawable::SetHighlightStyle((HighlightStyle)_msg->Int(2))
    )
    HANDLE_EXPR(get_normal_display_length, RndDrawable::GetNormalDisplayLength())
    HANDLE_ACTION(
        set_normal_display_length, RndDrawable::SetNormalDisplayLength(_msg->Float(2))
    )
    HANDLE_EXPR(get_force_select_proxied_subparts, RndDrawable::GetForceSubpartSelection())
    HANDLE_ACTION(
        set_force_select_proxied_subparts,
        RndDrawable::SetForceSubpartSelection(_msg->Int(2))
    )
    HANDLE_ACTION(set_sync, SetSync(_msg->Int(2)))
    HANDLE_EXPR(get_sync, GetSync())
    HANDLE_ACTION(set_shrink_to_safe, SetShrinkToSafeArea(_msg->Int(2)))
    HANDLE(show_console, OnShowConsole)
    HANDLE(toggle_timers, OnToggleTimers)
    HANDLE(toggle_overlay_position, OnToggleOverlayPosition)
    HANDLE(toggle_timers_verbose, OnToggleTimersVerbose)
    HANDLE(toggle_overlay, OnToggleOverlay)
    HANDLE_EXPR(show_safe_area, mShowSafeArea)
    HANDLE_ACTION(set_show_safe_area, mShowSafeArea = _msg->Int(2))
    HANDLE(show_overlay, OnShowOverlay)
    HANDLE_EXPR(overlay_showing, RndOverlay::Find(_msg->Str(2), true)->Showing())
    HANDLE(overlay_print, OnOverlayPrint)
    HANDLE_ACTION(hi_res_screen, TheHiResScreen.TakeShot("ur_hi", _msg->Int(2)))
    HANDLE_ACTION(proc_lock, SetProcAndLock(ProcAndLock() == 0))
    HANDLE_ACTION(allow_per_pixel, TheShaderMgr.SetAllowPerPixel(_msg->Int(2)))
    HANDLE_ACTION(reload_shaders, TheShaderMgr.Invalidate((ShaderType)_msg->Int(2)))
    HANDLE_ACTION(reload_shaders_all, TheShaderMgr.Invalidate(kMaxShaderTypes)) {
        static Symbol _s("toggle_error_shaders");
        if (sym == _s) {
            TheShaderMgr.SetShaderErrorDisplay(!TheShaderMgr.GetShaderErrorDisplay());
            return TheShaderMgr.GetShaderErrorDisplay();
        }
    }
    HANDLE_ACTION(set_in_game, SetInGame(_msg->Int(2)))
    HANDLE_ACTION(toggle_in_game, SetInGame(!mInGame))
    HANDLE(clear_color_r, OnClearColorR)
    HANDLE(clear_color_g, OnClearColorG)
    HANDLE(clear_color_b, OnClearColorB)
    HANDLE(clear_color_packed, OnClearColorPacked)
    HANDLE(set_clear_color, OnSetClearColor)
    HANDLE(set_clear_color_packed, OnSetClearColorPacked)
    HANDLE(screen_dump, OnScreenDump)
    HANDLE(screen_dump_unique, OnScreenDumpUnique)
    HANDLE(scale_object, OnScaleObject)
    HANDLE(reflect, OnReflect)
    HANDLE(toggle_heap, OnToggleHeap)
    HANDLE(toggle_watch, OnToggleWatch)
    HANDLE_ACTION(
        fix_vert_order, FixVertOrder(_msg->Obj<RndMesh>(2), _msg->Obj<RndMesh>(3))
    )
    HANDLE(test_draw_groups, OnTestDrawGroups)
    HANDLE_ACTION(
        test_texture_size,
        TestTextureSize(
            _msg->Obj<ObjectDir>(2),
            _msg->Int(3),
            _msg->Int(4),
            _msg->Int(5),
            _msg->Int(6),
            _msg->Int(7)
        )
    )
    HANDLE_ACTION(test_texture_paths, TestTexturePaths(_msg->Obj<ObjectDir>(2)))
    HANDLE_ACTION(test_material_textures, TestMaterialTextures(_msg->Obj<ObjectDir>(2)))
    HANDLE_ACTION(set_gfx_mode, SetGfxMode((GfxMode)_msg->Int(2)))
    HANDLE_EXPR(default_cam, mDefaultCam)
    HANDLE_EXPR(last_proc_cmds, mLastProcCmds)
    HANDLE_EXPR(toggle_all_postprocs, mDisablePostProc = !mDisablePostProc)
#ifdef HX_NATIVE
    HANDLE_EXPR(native_look, NativeLook(_msg))
    HANDLE_EXPR(native_noise, NativeNoise(_msg))
    HANDLE_EXPR(native_post_skip, NativePostSkip(_msg))
#endif
    HANDLE_ACTION(recreate_defaults, CreateDefaults())
    HANDLE_ACTION(
        reload_mat_metaterials, RndMat::ReloadAndUpdateMat(_msg->Obj<ObjectDir>(2))
    )
    HANDLE(toggle_show_metamat_errors, OnToggleShowMetaMatErrors)
    HANDLE(toggle_show_shader_errors, OnToggleShowShaderErrors)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

static void TerminateCallback() {
    RndUtlTerminate();
    TheRnd.Terminate();
}

void Rnd::PreInit() {
    SetName("rnd", ObjectDir::Main());
    TheDebug.AddExitCallback(TerminateCallback);
    DataArray *rndcfg = SystemConfig("rnd");
    rndcfg->FindData("bpp", mScreenBpp, true);
    rndcfg->FindData("height", mHeight, true);
    rndcfg->FindData("clear_color", mClearColor, true);
    rndcfg->FindData("sync", mSync, true);
    rndcfg->FindData("aspect", (int &)mAspect, true);
    if (OptionBool("widescreen", false))
        mAspect = kWidescreen;
    mWidth = ((float)mHeight / Rnd::YRatio()) + 0.5f;
#ifdef HX_NATIVE
    // Xbox config has height=432 (internal render res, upscaled to 720p).
    // Native renders at full GPU surface resolution — override to match.
    mWidth = 1280;
    mHeight = 720;
#endif
    MILO_ASSERT((mScreenBpp == 16) || (mScreenBpp == 32), 0x209);
    SetupFont();
    RndGraph::Init();
    RndUtlPreInit();
    RndDrawable::Init();
    RndFur::Init();
    RndTransformable::Init();
    RndSet::Init();
    RndAnimFilter::Init();
    RndFlare::Init();
    RndCam::Init();
    RndMesh::Init();
    RndMeshDeform::Init();
    RndText::Init();
    RndFontBase::Init();
    RndFont::Init();
    RndFont3d::Init();
    RndEnviron::Init();
    RndTex::Init();
    RndCubeTex::Init();
    RndMovie::Init();
    RndLight::Init();
    RndTransAnim::Init();
    RndLightAnim::Init();
    RndMeshAnim::Init();
    RndMatAnim::Init();
    RndTransProxy::Init();
    RndPartLauncher::Init();
    RndLine::Init();
    RndGenerator::Init();
    RndParticleSys::Init();
    RndParticleSysAnim::Init();
    RndRibbon::Init();
    RndMultiMesh::Init();
    RndMultiMeshProxy::Init();
    RndMorph::Init();
    RndCamAnim::Init();
    REGISTER_OBJ_FACTORY(RndTransformable)
    RndGroup::Init();
    RndDir::Init();
    RndMotionBlur::Init();
    RndTexBlendController::Init();
    RndTexBlender::Init();
    RndTexRenderer::Init();
    RndScreenMask::Init();
    RndSoftParticles::Init();
    REGISTER_OBJ_FACTORY(RndPostProc)
    RndPostProcMgr::Init();
    RndAmbientOcclusion::Init();
    RndOverlay::Init();
    RndPropAnim::Init();
    EventTrigger::Init();
    RndWind::Init();
    RndPollAnim::Init();
    BaseMaterial::Init();
    REGISTER_OBJ_FACTORY(MetaMaterial)
    RndEnterable::Init();
    RndMat::Init();
    RndSpline::Init();
    RndShockwave::Init();
    DOFProc::Init();
    TexProc::Init();
    // this is likely some other rndobj without a NewObject overload
    REGISTER_OBJ_FACTORY(Hmx::Object)
    InitShaderOptions();
    mRateOverlay = RndOverlay::Find("rate", true);
    mHeapOverlay = RndOverlay::Find("heap", true);
    // well ok then
    mWatcher.SetOverlay(mWatchOverlay = RndOverlay::Find("watch", true));
    mWatcher.Init();
    mStatsOverlay = RndOverlay::Find("stats", true);
    mTimersOverlay = RndOverlay::Find("timers", true);
    mRateOverlay->SetCallback(this);
    mHeapOverlay->SetCallback(this);
    mWatchOverlay->SetCallback(this);
    mStatsOverlay->SetCallback(this);
    mTimersOverlay->SetCallback(this);
    mConsole = new RndConsole();
    mWorldEnded = true;
    mDrawing = false;
    mGSTiming = mTimersOverlay->Showing();
    CreateDefaults();
    InitParticleSystem();
    DataRegisterFunc("keep_going", FailKeepGoing);
    DataRegisterFunc("restart_console", FailRestartConsole);
}

void WordWrap(const char *src, int lineWidth, char *dst, int dstSize) {
    char *dstEnd = dst + dstSize - 2;
    const char *srcEnd = src + strlen(src);
    while (true) {
        const char *lastSrcSpace = nullptr;
        char *lastSpace = nullptr;
        int col = 0;
        while (col < lineWidth) {
            if (src >= srcEnd || dst >= dstEnd || *src == '\n')
                break;
            if (*src == ' ') {
                lastSrcSpace = src;
                lastSpace = dst;
            }
            *dst = *src;
            col++;
            src++;
            dst++;
        }
        if (dst == dstEnd || src == srcEnd) {
            *dst = '\0';
            return;
        }
        char *wrapDst = dst;
        if (*src != '\n') {
            if (lastSrcSpace == nullptr || (int)src - (int)lastSrcSpace > 10) {
                wrapDst = dst;
                src = src - 1;
            } else {
                src = lastSrcSpace;
                wrapDst = lastSpace;
            }
        }
        src = src + 1;
        *wrapDst = '\n';
        dst = wrapDst + 1;
    }
}

#ifndef HX_NATIVE
DWORD CompressThread(void *) {
    while (true) {
        WaitForSingleObject(gRndTextureEvent, -1);
        if (sTexture == nullptr)
            break;
        sTexture->DoCompress(sCompressData);
        sCompressDone = true;
    }
    return 0;
}
#endif

void Rnd::Init() {
    DataArray *cfg = SystemConfig("rnd");
    DataArray *stats = cfg->FindArray("timer_stats", false);
    if (stats) {
        if (stats->Int(1)) {
            MILO_LOG("config showing timers\n");
            SetShowTimers(true, true);
        }
    }
    RndUtlInit();
    RndPostProc::Init();
#ifndef HX_NATIVE
    gRndTextureEvent = CreateEventA(nullptr, false, false, "texture_event");
    gRndThread = CreateThread(nullptr, 0, CompressThread, nullptr, 4, nullptr);
    XSetThreadProcessor(gRndThread, 1);
    ResumeThread(gRndThread);
#endif
}

void Rnd::Terminate() {
    RELEASE(mConsole);
    TheDebug.RemoveExitCallback(TerminateCallback);
    RndOverlay::Terminate();
    RndMultiMesh::Terminate();
    DOFProc::Terminate();
    RndMat::Terminate();
    SetName(nullptr, nullptr);
    HANDLE *handles = &gRndThread;
    SetEvent(handles[1]);
    CloseHandle(handles[0]);
    CloseHandle(handles[1]);
}

void Rnd::ScreenDump(const char *file) {
    RndTex *tex = Hmx::Object::New<RndTex>();
    RndBitmap bmap;
    tex->SetBitmap(0, 0, 0, RndTex::kFrontBuffer, false, nullptr);
    tex->LockBitmap(bmap, 1);
    FileStream stream(file, FileStream::kWrite, true);
    if (stream.Fail()) {
        MILO_NOTIFY("Screenshot failed; could not open destination file (%s).", file);
    } else {
        bmap.SaveBmp(&stream);
    }
    delete tex;
}

void Rnd::ScreenDumpUnique(const char *cc) {
    String filename = UniqueFilename(cc, "bmp");
    ScreenDump(filename.c_str());
}

Vector2 &Rnd::DrawString(const char *, const Vector2 &v, const Hmx::Color &, bool) {
    static Vector2 s;
    s = v;
    return s;
}

void AutoTimer::ResetTimers() {
    FOREACH (it, sTimers) {
        it->first.Reset();
    }
}

void Rnd::BeginDrawing() {
    mDrawing = true;
    mWorldEnded = false;
    mDrawTimer.Restart();
    AutoTimer::ResetTimers();
    mLastProcCmds = mProcCmds;
    mProcCmds = mProcCounter.ProcCommands();
    mDefaultCam->Select();
    mDefaultEnv->Select(nullptr);
    if (!TheHiResScreen.IsActive()) {
        mPointTests.clear();
    }
    mDrawCount++;
    if (mPostProcBlackLightOverride) {
        mPostProcBlackLightOverride->SetBloomColor();
    } else if (mPostProcOverride) {
        mPostProcOverride->SetBloomColor();
    } else if (RndPostProc::Current()) {
        RndPostProc::Current()->SetBloomColor();
    }
}

void Rnd::EndDrawing() {
    EndWorld();
    if (MainThread()) {
        {
            static Timer *cpuStop = AutoTimer::GetTimer("cpu");
            if (cpuStop)
                cpuStop->Stop();
        }
        {
            static Timer *drawStop = AutoTimer::GetTimer("draw");
            if (drawStop)
                drawStop->Stop();
        }
        static Timer *t = AutoTimer::GetTimer("overlays");
        AutoTimer at(t, 50.0f, NULL, NULL);
        AutoSlowFrame asf("RndOverlay::DrawAll", 10.0f);
        if (RndCam::Current()->TargetTex()) {
            mDefaultCam->Select();
        }
        RndOverlay::DrawAll(false);
        RndGraph::DrawAll();
        {
            static Timer *cpuStart = AutoTimer::GetTimer("cpu");
            if (cpuStart)
                cpuStart->Start();
        }
        {
            static Timer *drawStart = AutoTimer::GetTimer("draw");
            if (drawStart)
                drawStart->Start();
        }
    }
    mDrawing = false;
    mFrameID++;
}

#ifdef HX_NATIVE
#include "platform/PointTestHook.h"

namespace {
// The answers to a flare test queued by TestPoint, delivered by the engine's
// GPU backend at world end one frame after the test was queued -- where
// retail DxRnd::DoPointTests (rnddx9/Rnd_Xbox.cpp) reads back the previous
// frame's occlusion queries:
//   point query -> SetOcclusionReady(true), SetVisible(samples != 0)
//   area query  -> SetOcclusionResult(samples), SetOcclusionReady(true)
// RndFlare::DrawShowing steps mStep toward mVisible one step per frame and
// scales by mOcclusionResult / rect area, so a flare that goes behind a dancer
// fades out over mSteps frames and a half-covered one draws at half strength.
void ApplyNativePointTest(const NativePointTestResult &r) {
    RndFlare *flare = (RndFlare *)r.key;
    if (r.pointDone) {
        flare->SetOcclusionReady(true);
        // Retail issues a flare without an area test and then, in the same
        // DoPointTests, sets it ready and visible -- after this frame's
        // answers were applied -- so such a flare ends every frame visible.
        // mAreaTest is never loaded (always true on shipped flares).
        flare->SetVisible(flare->GetAreaTest() ? r.visible : true);
    }
    if (r.areaDone) {
        flare->SetOcclusionResult(r.area);
        flare->SetOcclusionReady(true);
    }
}
}
#endif

void Rnd::TestPoint(const Vector3 &pos, RndFlare *flare) {
    if (TheHiResScreen.IsActive())
        return;
    RndCam *cam = RndCam::Current();
    if (cam->TargetTex()) {
        MILO_NOTIFY_ONCE("Flare %s can't be drawn in rendered texture", (char *)flare->Name());
        flare->SetVisible(false);
    } else {
        Vector2 screen;
        float depth = cam->WorldToScreen(pos, screen);
        if (depth >= cam->NearPlane() && depth <= cam->FarPlane()
            && screen.x >= 0.0f && screen.y >= 0.0f && screen.x < 1.0f && screen.y < 1.0f) {
#ifdef HX_NATIVE
            // Retail queues {x, y, ProjectZ(depth)} for DxRnd::DoPointTests'
            // occlusion queries; native, the engine's GPU backend runs them
            // (platform/PointTestHook.h, gfx/PointTestPass) against the
            // world's depth at world end and answers through
            // ApplyNativePointTest a frame later. The backend projects the
            // depth itself, so it gets the tested point, plus the area rect
            // mArea that CalcRect set in DrawShowing just before this call.
            if (NativePointTester *tester = GetNativePointTester()) {
                SetNativePointTestResultFn(ApplyNativePointTest);
                const Hmx::Rect &r = flare->GetArea();
                NativePointTest t;
                t.key = flare;
                t.world[0] = pos.x;
                t.world[1] = pos.y;
                t.world[2] = pos.z;
                t.screenX = screen.x;
                t.screenY = screen.y;
                t.rect[0] = r.x;
                t.rect[1] = r.y;
                t.rect[2] = r.w;
                t.rect[3] = r.h;
                t.screenW = (float)mWidth;
                t.screenH = (float)mHeight;
                t.pointTest = flare->GetPointTest();
                t.areaTest = flare->GetAreaTest();
                if (tester->QueuePointTest(t)) {
                    if (!t.areaTest) {
                        // DoPointTests' else arm, at issue time.
                        flare->SetOcclusionReady(true);
                        flare->SetVisible(true);
                    }
                    return;
                }
            }
            // No occlusion queries (headless, or no frame to test against):
            // treat in-view flares as fully visible. Retail draws the flare's
            // rect as an occlusion query and stores the visible pixel count,
            // which RndFlare divides by the rect's area when mAreaTest is on.
            // Unoccluded, that count is the rect's on-screen area; a constant
            // 1.0 would leave an area-tested flare at ~1/(w*h) strength.
            flare->SetVisible(true);
            const Hmx::Rect &a = flare->GetArea();
            float w = Min<float>((float)mWidth, a.x + a.w) - Max(0.0f, a.x);
            float h = Min<float>((float)mHeight, a.y + a.h) - Max(0.0f, a.y);
            flare->SetOcclusionResult((w > 0.0f && h > 0.0f) ? w * h : 0.0f);
#else
            PointTest pt = { 0, 0, 0, 0 };
            mPointTests.push_back(pt);
            PointTest &back = mPointTests.back();
            back.mFlare = flare;
            back.x = (int)((float)mWidth * screen.x);
            back.y = (int)((float)mHeight * screen.y);
            back.z = cam->ProjectZ(depth);
            // Lane history (6 lines moved): docs/decomp/notes/system/rndobj/Rnd.md
            return;
#endif
        } else {
            flare->SetVisible(false);
        }
    }
    // Lane history (13 lines moved): docs/decomp/notes/system/rndobj/Rnd.md
    // > The two charged rows are the epilogue's `li r10, 0x0` / `stb r10, 0x102` pair:
    // > the image puts the false in r10, we put it in r11 (the true that follows is r11
    // > on both sides). (w26-f13)
    flare->SetOcclusionReady(true);
}

void Rnd::RemovePointTest(RndFlare *flare) {
#ifdef HX_NATIVE
    // Queued and in-flight native tests hold the flare too (TestPoint).
    if (NativePointTester *tester = GetNativePointTester())
        tester->CancelPointTests(flare);
#endif
    if (!TheHiResScreen.IsActive()) {
        for (std::list<PointTest>::iterator it = mPointTests.begin();
             it != mPointTests.end();) {
            if (it->mFlare == flare) {
                it = mPointTests.erase(it);
            } else
                ++it;
        }
    }
}

float Rnd::YRatio() {
    static const float kRatio[5] = { 1.0f, 0.75f, 0.5625f, 0.5625f, 0.6f };
    return kRatio[mAspect];
}

struct SortPostProc {
    bool operator()(PostProcessor *p1, PostProcessor *p2) const {
        return p1->Priority() < p2->Priority();
    }
};

void Rnd::ShowConsole(bool show) { mConsole->SetShowing(show); }
bool Rnd::ConsoleShowing() { return mConsole->Showing(); }

void Rnd::EndWorld() {
    if (!mWorldEnded) {
        if (mWorldEndCallback) {
            mWorldEndCallback();
        }
        DoWorldEnd();
        DoPostProcess();
        mWorldEnded = true;
    }
}

void Rnd::SetShowTimers(bool show, bool verbose) {
    mTimersOverlay->SetShowing(show);
    mVerboseTimers = verbose;
    SetGSTiming(show);
}

void Rnd::SetProcAndLock(bool b) { mProcCounter.SetProcAndLock(b); }

bool Rnd::ProcAndLock() const { return mProcCounter.ProcAndLock(); }

void Rnd::ResetProcCounter() {
    if (mDrawing) {
        mProcCounter.SetCount(-1);
    } else
        mLastProcCmds = ProcessCmd(mLastProcCmds | kProcessWorld);
}

bool Rnd::GetEvenOddDisabled() const { return mProcCounter.EvenOddDisabled(); }
void Rnd::SetEvenOddDisabled(bool b) { mProcCounter.SetEvenOddDisabled(b); }

void Rnd::DrawRectScreen(
    const Hmx::Rect &r,
    const Hmx::Color &c1,
    RndMat *mat,
    const Hmx::Color *cptr1,
    const Hmx::Color *cptr2
) {
    Hmx::Rect rect(r.x * mWidth, r.y * mHeight, r.w * mWidth, r.h * mHeight);
    DrawRect(rect, c1, mat, cptr1, cptr2);
}

const Vector2 &
Rnd::DrawStringScreen(const char *c, const Vector2 &v, const Hmx::Color &color, bool b4) {
    float fwidth = mWidth;
    float fheight = mHeight;
    Vector2 &vres = DrawString(c, Vector2(v.x * fwidth, v.y * fheight), color, b4);
    vres.x /= fwidth;
    vres.y /= fheight;
    return vres;
}

RndPostProc *Rnd::GetPostProcOverride() { return mPostProcOverride; }

RndPostProc *Rnd::GetSelectedPostProc() {
    RndPostProc *selected = nullptr;
    for (std::list<PostProcessor *>::iterator it = mPostProcessors.begin();
         it != mPostProcessors.end();
         ++it) {
        RndPostProc *set = dynamic_cast<RndPostProc *>(*it);
        if (selected) {
            MILO_NOTIFY("More than one postproc selected: %s", PathName(set));
        } else
            selected = set;
    }
    return selected;
}

void Rnd::DoWorldBegin() {
    if (mPostProcBlackLightOverride) {
        mPostProcBlackLightOverride->BeginWorld();
    } else if (mPostProcOverride) {
        mPostProcOverride->BeginWorld();
    } else {
        for (std::list<PostProcessor *>::iterator it = mPostProcessors.begin();
             it != mPostProcessors.end();
             ++it) {
            (*it)->BeginWorld();
        }
    }
}

void Rnd::DoWorldEnd() {
    std::list<PostProcessor *>::iterator it;
    std::list<PostProcessor *>::iterator end;
    if (!mWorldCamCopied) {
        CopyWorldCam(nullptr);
    }
    mWorldCamCopied = false;
    if (mPostProcBlackLightOverride) {
        mPostProcBlackLightOverride->EndWorld();
    } else if (mPostProcOverride) {
        mPostProcOverride->EndWorld();
    } else {
        it = mPostProcessors.begin();
        end = mPostProcessors.end();
        while (it != end) {
            (*it)->EndWorld();
            ++it;
        }
    }
}

void Rnd::DoPostProcess() {
    if (!mDisablePostProc) {
        if (mPostProcBlackLightOverride) {
            mPostProcBlackLightOverride->DoPost();
        } else if (mPostProcOverride) {
            mPostProcOverride->DoPost();
        } else {
            for (std::list<PostProcessor *>::iterator it = mPostProcessors.begin();
                 it != mPostProcessors.end();
                 ++it) {
                (*it)->DoPost();
            }
        }
    }
}

float Rnd::UpdateOverlay(RndOverlay *o, float f) {
    if (o == mRateOverlay) {
        UpdateRate();
    } else if (o == mHeapOverlay) {
        UpdateHeap();
    } else if (o == mWatchOverlay) {
        mWatcher.Update();
    } else if (o == mTimersOverlay) {
        f = DrawTimers(f);
    }
    return f;
}

DataNode Rnd::OnShowOverlay(const DataArray *da) {
    RndOverlay *o = RndOverlay::Find(da->Str(2), false);
    if (o) {
        o->SetShowing(da->Int(3));
        if (da->Size() > 4) {
            o->SetTimeout(da->Float(4));
        }
    }
    return 0;
}

DataNode Rnd::OnOverlayPrint(const DataArray *da) {
    RndOverlay *o = RndOverlay::Find(da->Str(2), true);
    String str;
    for (int i = 3; i < da->Size(); i++) {
        da->Evaluate(i).Print(str, true, 0);
    }
    o->Print(str.c_str());
    return 0;
}

DataNode Rnd::OnReflect(const DataArray *da) {
    RndOverlay *o = RndOverlay::Find(da->Sym(2), true);
    if (o->Showing()) {
        TextStream *idk = TheDebug.SetReflect(o);
        for (int i = 3; i < da->Size(); i++) {
            da->Command(i)->Execute(true);
        }
        TheDebug.SetReflect(idk);
    }
    return 0;
}

DataNode Rnd::OnToggleOverlay(const DataArray *da) {
    RndOverlay *o = RndOverlay::Find(da->Str(2), true);
    o->SetShowing(!o->Showing());
    if (o->Showing()) {
        o->SetDumpCount(1);
    }
    return o->Showing();
}

DataNode Rnd::OnToggleOverlayPosition(const DataArray *) {
    RndOverlay::TogglePosition();
    return 0;
}

DataNode Rnd::OnShowConsole(const DataArray *) {
    ShowConsole(true);
    return 0;
}

DataNode Rnd::OnToggleTimers(const DataArray *) {
    SetShowTimers(mVerboseTimers || !TimersShowing(), false);
    return 0;
}

DataNode Rnd::OnToggleTimersVerbose(const DataArray *) {
    SetShowTimers(mVerboseTimers == 0, mVerboseTimers == 0);
    return 0;
}

DataNode Rnd::OnClearColorR(const DataArray *) { return DataNode(mClearColor.red); }
DataNode Rnd::OnClearColorG(const DataArray *) { return DataNode(mClearColor.green); }
DataNode Rnd::OnClearColorB(const DataArray *) { return DataNode(mClearColor.blue); }
DataNode Rnd::OnClearColorPacked(const DataArray *) { return mClearColor.Pack(); }

DataNode Rnd::OnSetClearColor(const DataArray *da) {
    SetClearColor(Hmx::Color(da->Float(2), da->Float(3), da->Float(4)));
    return 0;
}

DataNode Rnd::OnSetClearColorPacked(const DataArray *da) {
    SetClearColor(
        Hmx::Color(
            (da->Int(2) & 255) * (1.0f / 255.0f),
            ((da->Int(2) >> 8) & 255) * (1.0f / 255.0f),
            ((da->Int(2) >> 0x10) & 255) * (1.0f / 255.0f)
        )
    );
    return 0;
}

DataNode Rnd::OnScreenDump(const DataArray *da) {
    ScreenDump(da->Str(2));
    return 0;
}

DataNode Rnd::OnScreenDumpUnique(const DataArray *da) {
    ScreenDumpUnique(da->Str(2));
    return 0;
}

DataNode Rnd::OnScaleObject(const DataArray *da) {
    RndScaleObject(da->GetObj(2), da->Float(3), da->Float(4));
    return 0;
}

void Rnd::UnregisterPostProcessor(PostProcessor *proc) { mPostProcessors.remove(proc); }

void PreClearCompilerHelper(ObjPtrList<RndDrawable> &list, RndDrawable *draw) {
    for (ObjPtrList<RndDrawable>::iterator it = list.begin(); it != list.end(); ++it) {
        if (*it == draw)
            return;
    }
    list.push_back(draw);
    list.sort(SortDraws);
}

void Rnd::RegisterPostProcessor(PostProcessor *proc) {
    sPostProcPanelCount++;
    mPostProcessors.push_back(proc);
    mPostProcessors.sort(SortPostProc());
}

void Rnd::CopyWorldCam(RndCam *cam) {
    if (mProcCmds & 1) {
        if (!cam)
            cam = RndCam::Current();
        mWorldCamCopy->Copy(cam, kCopyShallow);
        mWorldCamCopy->SetTransParent(nullptr, false);
        mWorldCamCopied = true;
    }
}

RndTex *Rnd::GetNullTexture() { return mDefaultTex[kDefaultTex_Error]; }

void Rnd::SetupFont() {
    mFont = SystemConfig("rnd", "font");
    for (int i = 0; i < 26; i++) {
        DataArray *arr = mFont->Array(i + 66)->Clone(true, false, 0);
        for (int j = 0; j < arr->Size(); j++) {
            DataArray *jArr = arr->Array(j);
            for (int k = 1; k < jArr->Size(); k += 2) {
                jArr->Node(k) = jArr->Float(k) * 0.7f + 0.3f;
            }
        }
        mFont->Node(i + 98) = arr;
        arr->Release();
    }
}

void Rnd::CreateCubeTextures() {
    mDefaultCubeTexBlack = Hmx::Object::New<RndCubeTex>();
    mDefaultCubeTexWhite = Hmx::Object::New<RndCubeTex>();
    for (unsigned int i = 0; i < RndCubeTex::kNumCubeFaces; i++) {
        RndCubeTex::CubeFace cf = (RndCubeTex::CubeFace)i;
        RndBitmap &bm110 = mDefaultCubeTexBlack->GetBitmap(cf);
        RndBitmap &bm114 = mDefaultCubeTexWhite->GetBitmap(cf);
        bm110.Create(32, 32, 0, 32, 0, nullptr, nullptr, nullptr);
        bm114.Create(32, 32, 0, 32, 0, nullptr, nullptr, nullptr);
        for (int j = 0; j < 32; j++) {
            for (int k = 0; k < 32; k++) {
                bm110.SetPixelColor(k, j, 0, 0, 0, 0);
                bm114.SetPixelColor(k, j, 255, 255, 255, 255);
            }
        }
        mDefaultCubeTexBlack->UpdateFace(cf);
        mDefaultCubeTexWhite->UpdateFace(cf);
    }
}

DataNode Rnd::OnToggleWatch(const DataArray *) {
    mWatchOverlay->SetShowing(!mWatchOverlay->Showing());
    return 0;
}

DataNode Rnd::OnToggleShowMetaMatErrors(const DataArray *) {
    TheShaderMgr.ToggleShowMetaMatErrors();
    return 0;
}

DataNode Rnd::OnToggleShowShaderErrors(const DataArray *) {
    TheShaderMgr.ToggleShowShaderErrors();
    return 0;
}

void Rnd::SetPostProcOverride(RndPostProc *pp) {
    MILO_LOG(
        "Rnd::SetPostProcOverride: %s -> %s\n",
        mPostProcOverride == 0 ? "NULL" : PathName(mPostProcOverride),
        pp == 0 ? "NULL" : PathName(pp)
    );
    mPostProcOverride = pp;
    RndOverlay *ppOverlay = RndOverlay::Find("postproc", true);
    if (ppOverlay->Showing()) {
        TextStream *old = TheDebug.Reflect();
        TheDebug.SetReflect(ppOverlay);
        MILO_LOG("SETPROSTPROCOVERRIDE: %s\n", pp == 0 ? "NULL" : PathName(pp));
        TheDebug.SetReflect(old);
    }
}

void Rnd::SetPostProcBlacklightOverride(RndPostProc *pp) {
    mPostProcBlackLightOverride = pp;
    RndOverlay *ppOverlay = RndOverlay::Find("postproc", true);
    if (ppOverlay->Showing()) {
        TextStream *old = TheDebug.Reflect();
        TheDebug.SetReflect(ppOverlay);
        MILO_LOG("SETBLACKLIGHTOVERRIDE: %s\n", pp == 0 ? "NULL" : PathName(pp));
        TheDebug.SetReflect(old);
    }
}

void Rnd::CreateDefaults() {
    RELEASE(mWorldCamCopy);
    RELEASE(mDefaultCam);
    RELEASE(mDefaultEnv);
    RELEASE(mDefaultLit);
    RELEASE(mDefaultMat);
    RELEASE(mOverlayMat);
    RELEASE(mOverdrawMat);
    mWorldCamCopy = ObjectDir::Main()->New<RndCam>("[world cam copy]");
    mDefaultCam = ObjectDir::Main()->New<RndCam>("[default cam]");
    mDefaultEnv = ObjectDir::Main()->New<RndEnviron>("[default env]");
    mDefaultLit = ObjectDir::Main()->New<RndLight>("[default lit]");
    mDefaultLit->SetTransParent(mDefaultCam, false);
    mDefaultLit->SetLightType(RndLight::kDirectional);
    mDefaultEnv->AddLight(mDefaultLit);
    mDefaultEnv->SetUseApproxes(true);
    mDefaultEnv->SetUseApproxGlobal(false);
    mDefaultMat = Hmx::Object::New<RndMat>();
    mDefaultMat->SetUseEnv(false);
    mDefaultMat->SetPreLit(true);
    CreateAndSetMetaMat(mDefaultMat);
    mOverlayMat = Hmx::Object::New<RndMat>();
    mOverlayMat->SetUseEnv(false);
    mOverlayMat->SetPreLit(true);
    mOverlayMat->SetBlend(RndMat::kBlendSrcAlpha);
    mOverlayMat->SetZMode(kZModeForce);
    CreateAndSetMetaMat(mOverlayMat);
    mOverdrawMat = Hmx::Object::New<RndMat>();
    mOverdrawMat->SetUseEnv(false);
    mOverdrawMat->SetBlend(RndMat::kBlendSrcAlpha);
    mOverdrawMat->SetColor(1, 0, 0);
    mOverdrawMat->SetAlpha(0.2);
    CreateAndSetMetaMat(mOverdrawMat);
    for (unsigned int i = 0; i < kDefaultTex_Max; i++) {
        RELEASE(mDefaultTex[i]);
        mDefaultTex[i] = CreateDefaultTexture((DefaultTextureType)i);
    }
    RELEASE(mDefaultCubeTexBlack);
    RELEASE(mDefaultCubeTexWhite);
    CreateCubeTextures();
}

Rnd::CompressTexDesc::~CompressTexDesc() {
    if (callback) {
        callback->TextureCompressed((intptr_t)this);
    }
}

int Rnd::CompressTexture(
    RndTex *tex, RndTex::AlphaCompress a, CompressTextureCallback *cb
) {
    for (std::list<CompressTexDesc *>::iterator it = mCompressTexQueue.begin(); it != mCompressTexQueue.end();
         ++it) {
        if (tex == (*it)->tex) {
            MILO_NOTIFY("%s: texture added to compression twice", PathName(tex));
        }
    }
    CompressTexDesc *desc = new CompressTexDesc(tex, a, cb);
    mCompressTexQueue.push_back(desc);
    return (int)desc;
}

void Rnd::PreClearDrawAddOrRemove(RndDrawable *d, bool b2, bool b3) {
    ObjPtrList<RndDrawable> &list = b3 ? mPreClearDraws : mDraws;
    if (!b2) {
        list.remove(d);
    } else {
        PreClearCompilerHelper(list, d);
    }
}

void Rnd::UpdateRate() {
    mRateTotal += mDrawTimer.GetLastMs();
    static Timer *cpuTimer = AutoTimer::GetTimer("cpu");
    static Timer *gsTimer = AutoTimer::GetTimer("gs");
    if (gsTimer && cpuTimer) {
        if (gsTimer->GetLastMs() > 16.7f) {
            if (!(gsTimer->GetLastMs() <= cpuTimer->GetLastMs() + 0.1f)) {
                mRateGate = " gs ";
            } else {
                mRateGate = " cpu";
            }
        }
    }
    mRateCount--;
    if (mRateCount == 0) {
        int rate = mRateTotal ? (int)(5000.0f / mRateTotal + 0.5f) : 0;
        *mRateOverlay << "rate:" << rate << mRateGate;
        *mRateOverlay << "\n";
        mRateCount = 5;
        mRateTotal = 0.0f;
        mRateGate = "    ";
    }
}

// noinline: Prevents inlining of this stub implementation. Remove once fully implemented.
#include "os\Timer.h"

template <class _T>
__declspec(noinline) auto _outline_Int(_T* _obj) -> decltype(_obj->Int()) {
    return _obj->Int();
}

float Rnd::DrawTimers(float f) {
    static DataArray *timerCfg =
        SystemConfig(Symbol("rnd"))->FindArray(Symbol("timer_script"), false);

    if (timerCfg) {
        timerCfg->ExecuteScript(1, nullptr, nullptr, 1);
    }

    if (mVerboseTimers) {
        AutoTimer::CollectTimerStats();
    }

    int numTimers = 0;
    std::list<std::pair<Timer, TimerStats> > &timers = AutoTimer::Timers();
    for (std::list<std::pair<Timer, TimerStats> >::iterator it = timers.begin();
         it != timers.end();
         ++it) {
        if (it->first.Draw()) {
            numTimers++;
        }
    }

    float bgLeft = 0.025f;
    float rowSpacing = 0.045f;
    float totalHeight = numTimers * rowSpacing;

    Hmx::Rect rect(bgLeft, f, 0.95f, totalHeight);
    Hmx::Color bgColor(0.0f, 0.0f, 0.0f, 0.5f);
    DrawRectScreen(rect, bgColor, mOverlayMat, nullptr, nullptr);

    Hmx::Color barColor(0.5f, 0.5f, 0.5f, 1.0f);
    Hmx::Color budgetExcessColor(0.5f, 0.0f, 0.0f, 1.0f);
    Hmx::Color worstExcessColor(0.0f, 0.5f, 0.5f, 1.0f);

    float scale = 0.019f;
    float barHeight = 0.0268f;

    rect.h = barHeight;

    for (std::list<std::pair<Timer, TimerStats> >::iterator it = timers.begin();
         it != timers.end();
         ++it) {
        // The image materialises &it->first once per iteration (addi r30, r28, 8)
        // and reads every Timer member through it, so the timer is a named
        // reference here rather than a repeated it->first -- bound BEFORE the
        // Draw() test (w14-a: the addi sits above the cmplwi in the image).
        Timer &timer = it->first;
        if (!timer.Draw()) {
            continue;
        }

        float budget = timer.Budget();

        bool overBudget = budget != 0.0f && timer.GetLastMs() > budget;

        if (overBudget) {
            rect.w = budget * scale;
            DrawRectScreen(rect, barColor, nullptr, nullptr, nullptr);
            rect.x += rect.w;
            rect.w = (timer.GetLastMs() - timer.Budget()) * scale;
            DrawRectScreen(rect, budgetExcessColor, nullptr, nullptr, nullptr);
        } else {
            rect.w = timer.GetLastMs() * scale;
            DrawRectScreen(rect, barColor, nullptr, nullptr, nullptr);
        }

        if (timer.GetWorstMs() > timer.GetLastMs()) {
            rect.x += rect.w;
            rect.w = (timer.GetWorstMs() - timer.GetLastMs()) * scale;
            DrawRectScreen(rect, worstExcessColor, nullptr, nullptr, nullptr);
        }

        // Lane history (5 lines moved): docs/decomp/notes/system/rndobj/Rnd.md
        rect.x = bgLeft;
        rect.y += rowSpacing;
    }

    rect.y = f;
    rect.h = totalHeight;
    barColor.red = 0.25f;
    barColor.green = 0.25f;
    barColor.blue = 0.25f;
    rect.w = 0.001f;
    for (int i = 0; i < 10; i++) {
        DrawRectScreen(rect, barColor, nullptr, nullptr, nullptr);
        rect.x += 0.095f;
    }

    barColor.red = 1.0f;
    barColor.green = 1.0f;
    barColor.blue = 1.0f;
    Vector2 pos(bgLeft + 0.00391f, f + 0.00446f);

    for (std::list<std::pair<Timer, TimerStats> >::iterator it = timers.begin();
         it != timers.end();
         ++it) {
        if (!it->first.Draw()) {
            continue;
        }

        float lastMs = it->first.GetLastMs();

        if (lastMs >= 0.05f) {
            if (mVerboseTimers && AutoTimer::CollectingStats()) {
                Symbol name = it->first.Name();
                TimerStats &stats = it->second;
                DrawStringScreen(
                    MakeString(
                        "%s %2.1f (%.2f, %.2f) %.2f",
                        name,
                        lastMs,
                        stats.mAvgMs,
                        stats.mStdDevMs,
                        stats.mMaxMs
                    ),
                    pos,
                    barColor,
                    true
                );
            } else {
                Symbol name = it->first.Name();
                float worstMs = it->first.GetWorstMs();
                DrawStringScreen(
                    MakeString("%s %.2f (%.2f)", name, lastMs, worstMs), pos, barColor, true
                );
            }
        } else {
            DrawStringScreen(it->first.Name().Str(), pos, barColor, true);
        }
        pos.y += rowSpacing;
    }

    return pos.y;
}

void Rnd::DrawPreClear() {
    if (unk150) {
        unk150();
    }
#ifndef HX_NATIVE
    // Texture compression hand-off with CompressThread: when the worker has
    // finished (sCompressDone), swap the compressed DxTex in for the queued
    // RndTex it was built from and delete the original, then start the next
    // queued texture on the thread.
    if (sCompressDone) {
        sTexture->FinishCompress(sCompressData);
        sCompressData = nullptr;
        MILO_ASSERT(sTexture, 0x481);
        CompressTexDesc *desc = mCompressTexQueue.front();
        // w17-a: test the ObjPtr itself and take the local inside the arm -- each
        // conversion of desc->tex homes its value to 0x50, and the image has
        // both stores (one before the branch, one in the arm).
        if (desc->tex) {
            RndTex *tex = desc->tex;
            ReplaceObject(tex, sTexture, false, false, false);
            sTexture = static_cast<DxTex *>(tex);
        }
        mCompressTexQueue.erase(mCompressTexQueue.begin());
        delete desc;
        delete sTexture;
        sTexture = nullptr;
        sCompressDone = false;
    }
    if (!sTexture) {
        // Drop queue entries whose texture went away or that lost their callback.
        // w17-a: the start-next block sits INSIDE the !empty() arm -- the image's
        // empty-queue branch skips straight past it rather than into size().
        if (!mCompressTexQueue.empty()) {
            std::list<CompressTexDesc *>::iterator it = mCompressTexQueue.begin();
            while (it != mCompressTexQueue.end()) {
                CompressTexDesc *desc = *it;
                if (desc->tex && desc->callback) {
                    ++it;
                } else {
                    it = mCompressTexQueue.erase(it);
                    delete desc;
                }
            }
            if (mCompressTexQueue.size() != 0) {
                CompressTexDesc *first = mCompressTexQueue.front();
                sTexture = static_cast<DxTex *>((RndTex *)first->tex);
                RndTex *newTex;
                {
                    MemTemp tmp;
                    newTex = Hmx::Object::New<RndTex>();
                }
                ReplaceObject(sTexture, newTex, false, false, false);
                sCompressData = sTexture->StartCompress(first->alpha);
                MILO_ASSERT(!sCompressDone, 0x4C3);
                SetEvent(gRndTextureEvent);
            }
        }
    }
#else
    // PLATFORM: no CompressThread natively. Each queued request completes at
    // once with the texture left uncompressed; ~CompressTexDesc fires the callback.
    while (!mCompressTexQueue.empty()) {
        CompressTexDesc *desc = mCompressTexQueue.front();
        mCompressTexQueue.pop_front();
        delete desc;
    }
#endif // !HX_NATIVE
    ObjPtrList<RndDrawable> *drawList = mReleaseImmediate ? &mPreClearDraws : &mDraws;
    if (drawList->size() != 0) {
        unk148 = true;
        RndCam *prevCam = RndCam::Current();
        for (ObjPtrList<RndDrawable>::iterator it = drawList->begin();
             it != drawList->end();
             ++it) {
            // The target null-checks each entry: NullifyAllRefs can leave
            // nulled nodes in the list without erasing them.
            RndDrawable *drawable = *it;
            if (drawable) {
                drawable->DrawPreClear();
            }
        }
        if ((prevCam != nullptr) && (prevCam != RndCam::Current())) {
            prevCam->Select();
        }
        unk148 = false;
    }
}

// Lane history (12 lines moved): docs/decomp/notes/system/rndobj/Rnd.md
// > BEHAVIOUR FIX (w14-a): the timer is restarted on BOTH arms.
DataNode Rnd::OnToggleHeap(const DataArray *) {
    int numHeaps = MemNumHeaps() + 1;
    RndOverlay *overlay = mHeapOverlay;
    if (!overlay->Showing()) {
        overlay->SetShowing(true);
    } else if (++lbl_82F14008 >= numHeaps) {
        overlay->SetShowingOnly(false);
        lbl_82F14008 = -1;
        overlay->TimerRef().Restart();
    } else {
        overlay->SetShowing(true);
    }
    return 0;
}

RndTex *Rnd::CreateDefaultTexture(DefaultTextureType textureType) {
    MILO_ASSERT(textureType < kDefaultTex_Max, 0x5E4);
    static const int sDefSize[kDefaultTex_Max][2] = {
        { 8, 8 }, { 8, 8 }, { 8, 8 }, { 8, 8 },
        { 8, 8 }, { 64, 64 }, { 256, 8 }, { 128, 128 }
    };
    static const unsigned char sDefColor[kDefaultTex_Max][4] = {
        { 0,    0,    0,    0xFF },  // kDefaultTex_Black
        { 0,    0,    0,    0 },     // target row 1
        { 0xFF, 0xFF, 0xFF, 0xFF },
        { 0xFF, 0xFF, 0xFF, 0 },
        { 0x7f, 0x7f, 0xFF, 0xFF },
        { 0xFF, 0xFF, 0xFF, 0xFF },  // kDefaultTex_Gradient
        { 0xFF, 0xFF, 0xFF, 0xFF },  // kDefaultTex_Hue
        { 0,    0,    0,    0xFF },  // kDefaultTex_Error (checkerboard)
    };
    int width = sDefSize[textureType][0];
    int height = sDefSize[textureType][1];
    unsigned char red = sDefColor[textureType][0];
    unsigned char green = sDefColor[textureType][1];
    unsigned char blue = sDefColor[textureType][2];
    unsigned char alpha = sDefColor[textureType][3];
    unsigned int order = GetDefaultTexBitmapOrder();
    RndBitmap bmap;
    bmap.Create(width, height, 0, 0x20, order, 0, 0, 0);
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            bmap.SetPixelColor(j, i, red, green, blue, alpha);
        }
    }
    switch (textureType) {
    case kDefaultTex_Gradient:
        for (int i = 0; i < width; i++) {
            unsigned char val = 0xFF - (i * 255) / (width - 1);
            for (int j = 0; j < height; j++) {
                bmap.SetPixelColor(i, j, val, val, val, alpha);
            }
        }
        break;
    case kDefaultTex_Hue:
        for (int i = 0; i < width; i++) {
            Hmx::Color color;
            MakeColor((float)i * (1.0f / 255.0f), 1.0f, 0.5f, color);
            unsigned char thisRed = color.red * 255.0f;
            unsigned char thisGreen = color.green * 255.0f;
            unsigned char thisBlue = color.blue * 255.0f;
            for (int j = 0; j < height; j++) {
                bmap.SetPixelColor(i, j, thisRed, thisGreen, thisBlue, alpha);
            }
        }
        break;
    case kDefaultTex_Error:
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                int iShift = i >> 2;
                if ((iShift ^ (j >> 2)) & 1) {
                    bmap.SetPixelColor(j, i, 0xff, 0x80, 0x40, alpha);
                } else {
                    bmap.SetPixelColor(j, i, 0, 0, 0, alpha);
                }
            }
        }
        break;
    default:
        break;
    }
    EndianSwapBitmap(bmap);
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(bmap, nullptr, true, RndTex::kRegular);
    return tex;
}

void Rnd::UpdateHeap() {
    mHeapOverlay->SetLines((lbl_82F14008 == -1) ? MemNumHeaps() + 1 : 1);

    int heapNum;
    if (lbl_82F14008 == -1) {
        heapNum = -3;
    } else if (lbl_82F14008 == MemNumHeaps()) {
        heapNum = -2;
    } else {
        heapNum = lbl_82F14008;
    }

    char buf[2056];
    MemPrintOverview(heapNum, buf);
    *mHeapOverlay << buf;
}

void Rnd::Modal(Debug::ModalType &type, FixedString &str, bool bb) {
    if (bb) {
        char *s = (char *)str.c_str();
        MILO_LOG("%s\n", s);
    }
    if (CanModal(type)) {
        AutoSlowFrame frame("Rnd::Modal", 6000000.0f);
        char buf[0x1000];
        WordWrap(str.c_str(), 0x5a, buf, 0x1000);
        if (!bb) {
            strcat(buf, "\n\n-- Waiting on Stack Trace --\n");
        } else if (type == Debug::kModalFail) {
            strcat(buf, "\n\n-- Program ended --\n");
        } else {
            strcat(buf, "\n\n-- Press any button to continue --\n");
        }
        bool oldShowing = ConsoleShowing();
        ShowConsole(false);
        if (type != Debug::kModalFail || !bb) {
            RndSplasherSuspend();
        }
        ModalDraw(type, buf);
        if (bb) {
            // Read inside the `if`: the target reuses the register that held
            // `bb` for &ThePlatformMgr immediately after the branch, so the
            // screen-saver state is never live across the test.
            bool oldScreenSaver = ThePlatformMgr.ScreenSaver();
            ThePlatformMgr.SetScreenSaver(false);
            ThePlatformMgr.SetScreenSaver(oldScreenSaver);
            gFailKeepGoing = false;
            gNotifyKeepGoing = false;
            gFailRestartConsole = false;
            ModalKeyListener mkl;
            KeyboardSubscribe(&mkl);
            unsigned int mask = 0x800;
            if (type != Debug::kModalFail) {
                mask = 0xFFFFFFFF;
            }
            while (!(mask & JoypadPollForButton(-1))) {
                KeyboardPoll();
                ModalDraw(type, buf);
                if (type == Debug::kModalFail) {
                    if (gFailKeepGoing) {
                        type = Debug::kModalNotify;
                        break;
                    }
                } else if (gNotifyKeepGoing) {
                    break;
                }
                // Top level, NOT inside the else: the target still emits the
                // redundant `cmpwi r11, 2 / bne` on the type != kModalFail
                // path (0x82662280), which only survives if the test is not
                // dominated by the else branch.
                //
                // Lane history (13 lines moved): docs/decomp/notes/system/rndobj/Rnd.md
                if (type != Debug::kModalFail) continue;
                if (gFailRestartConsole) {
                    XLaunchNewImage(TheSystemArgs.front(), 0);
                    return;
                }
            }
            KeyboardUnsubscribe(&mkl);
            ShowConsole(false);
            ModalDraw(type, "");
            RndSplasherResume();
        }
        ShowConsole(oldShowing);
    }
}
