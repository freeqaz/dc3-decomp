// test_dc3_retail_post.cpp -- DC3's retail post chain (np-gfx-post, 2026-11-14).
//
// The image's NgPostProc::DoPost (dc3-decomp rndobj/PostProc_NG.cpp) and the
// shipped postprocess.ps / bloom.ps / blur.ps microcode, as ported to the
// engine's gfx/RB3RetailPostDC3.cpp and selected by rndshape::kDC3RetailPost:
//
//   mask    standard.ps (bit 22, NgMat::AllowHDR materials in the main frame)
//           writes alpha = sat(luma(rgb) * c7scale * mBloomMultiplier), with
//           c7scale = threshold > 1 ? 1/threshold : 1 (NgPostProc::SetBloomColor)
//   bloom   bloom.ps averages rgb * alpha; the streak arm (mBloomStreak, not
//           glare) runs three blur.ps passes per set with
//           SetBloomBlurWeightsStreak's seven weights: w[3] = 1/3,
//           w[3 +- i] = a^i / 3, a = attenuation^(4^pass)
//   screen  out = 1 - (1 - b * c6) (1 - scene), b = the bloom sets summed,
//           c6 = bloom colour * sBloomLocFactor * BloomIntensity()
//   ramp    DxRnd::SetupGamma's display ramp from (rnd (gamma 0.85))
//
// Sabotages these were checked against (each made a test here fail):
//   - BloomMaskScale ignoring NgMat::AllowHDR (SrcAlpha material masked)
//   - bloom.ps taps dropping the mask (rgb instead of rgb * a)
//   - the composite adding the bloom instead of screen-blending it
//   - streak weights normalised to sum 1 (the image's do not)
//   - half-float bloom sets (the image's are 8-bit and saturate per pass)
//   - DisplayGamma returning 0 (no ramp)
//   motion blur (round 2, NgPostProc::CheckMotionBlur + postprocess.ps bit 24):
//   - k = c105.rgb only (dropping max(r, ...), the exposure term)
//   - the mix reversed (prev + (c - prev) * k)
//   - the feedback never valid (every frame treated as the first)
//   - a stale previous frame reused after motion blur was off for a frame
//   - FillRetailPost ignoring RndPostProc::DoMotionBlur's Pack() gate
//   fade-out (round 2, RndShader*::CalcShaderOpts' !fadeOut):
//   - BloomMaskScale ignoring the environ's fade-out
//   - the rect path (RndShaderDrawRect, no fade test) cancelled by it too
//   - fade-out read as FadeOut() alone (FadeEnd == FadeStart is no fade)
//   glare (round 2, DoBloom's glare arm, bloom_glare.ps + postprocess.ps bit 37):
//   - bit 4's screen blend instead of bit 37's add; the add without its 0.5
//   - nine taps instead of ten; the tap weight not squared; half the step
//   - the plain harmonic mean (no 2 - 2x); the glare pass skipped
//   - FillRetailPost never selecting the glare arm
//   velocity blur (round 3, NgPostProc::DoVelocity / RndVelocityBuffer::Draw,
//   velocity_camera.ps + postprocess.ps bit 42): see the Velocity* tests below
//   and lanes/post/round3/sabotage (archive/np-gfx-2026-11-14)
//   hue converge (round 2, CheckHueConverge + postprocess.ps bit 62) and the
//   post-process selection (Rnd::DoPostProcess' overrides): see
//   HueConvergeIsRetailBit62 / ActivePostProcFollowsRndOverrides and
//   lanes/post/round2/sabotage/sab_hue.log (archive/np-gfx-2026-11-14)

#include "test_helpers.h"

#include "gfx/DisplayRamp.h"
#include "gfx/GpuDevice.h"
#include "gfx/PostProcPass.h"
#include "gfx/RB3RetailPost.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "math/Rot.h"
#include "obj/DataFile.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Mat.h"
#include "rndobj/PostProc.h"
#include "rndobj/Rnd.h"
#include "rndobj/VelocityBuffer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static_assert(rndshape::kDC3RetailPost,
              "dc3's RndPostProc/RndMat must expose BloomLocFactor/GetColorModulation/"
              "GetBloomMultiplier, or the engine's DC3 retail post chain compiles out");

namespace {

class DC3RetailPostTest : public EngineTestFixture {};

// One streak set over a flat field of value v = s * A, as the image computes
// it: bloom.ps writes v, then three blur.ps passes each multiply by that
// pass's weight sum (SetBloomBlurWeightsStreak, dc3-decomp rndobj/Utl.cpp),
// and every pass lands in an 8-bit target (NgPostProc::BloomTextureSet:
// TheRnd.Bpp() = 32), so each result saturates at 1.
float StreakSet(float v, float attenuation) {
    for (int pass = 0; pass < 3; pass++) {
        const float a = std::pow(attenuation, std::pow(4.0f, (float)pass));
        v = std::min(1.0f, v * (1.0f + 2.0f * (a + a * a + a * a * a)) / 3.0f);
    }
    return v;
}

} // namespace

TEST_F(DC3RetailPostTest, BloomMaskScaleIsAllowHDRLumaOverThreshold) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    pp->SetProperty("bloom_threshold", DataNode(9.0f));
    RndPostProc *prev = RndPostProc::Current();
    pp->Select();

    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetProperty("bloom_multiplier", DataNode(2.0f));
    EXPECT_FLOAT_EQ(rndshape::BloomMaskScale(mat), -(2.0f / 9.0f))
        << "an opaque material writes luma * (1/threshold) * bloom_multiplier";

    pp->SetProperty("bloom_threshold", DataNode(0.5f));
    EXPECT_FLOAT_EQ(rndshape::BloomMaskScale(mat), -2.0f) << "threshold <= 1 does not scale";

    // bloom_multiplier 0 (the "debloom" materials, e.g. game_mode_icon's
    // debloom.mat): retail still writes the mask, as 0, so the scale must stay
    // a mask write (negative) whose mask rounds to 0, not "leave alpha alone".
    mat->SetProperty("bloom_multiplier", DataNode(0.0f));
    const float zero = rndshape::BloomMaskScale(mat);
    EXPECT_LT(zero, 0.0f) << "a bloom_multiplier-0 AllowHDR material still writes the mask";
    EXPECT_LT(std::fabs(zero) * 1e6f, 1.0f / 255.0f) << "and the mask it writes is 0";
    mat->SetProperty("bloom_multiplier", DataNode(2.0f));

    mat->SetProperty("blend", DataNode((int)RndMat::kBlendSrcAlpha));
    EXPECT_EQ(rndshape::BloomMaskScale(mat), 0.0f) << "SrcAlpha is not NgMat::AllowHDR";
    mat->SetProperty("blend", DataNode((int)RndMat::kBlendSrc));
    mat->SetProperty("alpha_write", DataNode(1));
    EXPECT_EQ(rndshape::BloomMaskScale(mat), 0.0f)
        << "an alpha_write material writes its own alpha, not the mask";

    if (prev) prev->Select(); else pp->Unselect();
    delete mat;
    delete pp;
}

// The frame alpha is the bloom mask, so how a blended draw combines it matters.
// DC3 follows the draw's own equation (NgMat::SetBasicState passes the colour
// factors for alpha), not MAX: under MAX the song-select "debloom" quads and
// the full-screen Multiply tint (white, AllowHDR) raised the mask to 1 under
// the cards and ~0.9 over the background, which retail footage refutes
// (dc3-backend-for-rb3-wii.md 31.5). Asked of the live renderer's pipelines.
TEST_F(DC3RetailPostTest, BlendedAlphaFollowsTheDrawsEquation) {
    if (!gWgpuRnd) GTEST_SKIP() << "no renderer";
    EXPECT_FALSE(gWgpuRnd->Pipelines().AlphaBlendMax());
    const wgpu::BlendState mul = gWgpuRnd->Pipelines().MapBlend(WgpuBlend::Multiply);
    EXPECT_EQ(mul.alpha.operation, wgpu::BlendOperation::Add) << "Multiply: dst.a * src.a";
    EXPECT_EQ(mul.alpha.srcFactor, mul.color.srcFactor);
    EXPECT_EQ(mul.alpha.dstFactor, mul.color.dstFactor);
    const wgpu::BlendState add = gWgpuRnd->Pipelines().MapBlend(WgpuBlend::Add);
    EXPECT_EQ(add.alpha.operation, wgpu::BlendOperation::Add);
    EXPECT_EQ(add.alpha.srcFactor, wgpu::BlendFactor::One);
    EXPECT_EQ(add.alpha.dstFactor, wgpu::BlendFactor::One);
}

TEST_F(DC3RetailPostTest, DisplayRampIsDefaultDtaGamma) {
    // default.dta (#merged by DC3's config/rnd.dta) sets (gamma 0.85), and DC3
    // applies the ramp to every output, so captures are post-ramp.
    if (getenv("DC3_DISPLAY_GAMMA")) GTEST_SKIP() << "DC3_DISPLAY_GAMMA overrides the config";
    EXPECT_NEAR(rndshape::DisplayGamma(false), 0.85f, 1e-6f);
    EXPECT_NEAR(rndshape::DisplayGamma(true), 0.85f, 1e-6f);
}

// The composite over a flat scene: grey s with alpha A in the bloom mask.
// With A = 0 bloom adds nothing; with A > 0 the frame is the screen blend of
// the streak-bloomed field (b = two sets of s * A * gain).
class DC3RetailPostGpuTest : public EngineTestFixture {
protected:
    static constexpr int kW = 128, kH = 96;
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
        if (getenv("DC3_RETAIL_POST")) GTEST_SKIP() << "DC3_RETAIL_POST selects another chain mode";
    }

    // Mean of the frame's red channel (the field is flat; edges excluded).
    float RunFlat(PostProcPass &pass, uint8_t grey, uint8_t alpha) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        auto &dev = gpu.Device();
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)kW, (uint32_t)kH, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
            wgpu::TextureUsage::RenderAttachment;
        wgpu::Texture scene = dev.CreateTexture(&d);
        std::vector<uint8_t> px(kW * kH * 4);
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = px[i + 1] = px[i + 2] = grey;
            px[i + 3] = alpha;
        }
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = scene;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = kW * 4;
        wgpu::Extent3D ext = {(uint32_t)kW, (uint32_t)kH, 1};
        gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);

        d.format = gpu.SurfaceFormat();
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        wgpu::Texture frame = dev.CreateTexture(&d);

        wgpu::TextureDescriptor b{};
        b.size = {1, 1, 1};
        b.format = wgpu::TextureFormat::RGBA8Unorm;
        b.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture black = dev.CreateTexture(&b);
        const uint8_t k[4] = {0, 0, 0, 255};
        dst.texture = black;
        layout.bytesPerRow = 4;
        wgpu::Extent3D one = {1, 1, 1};
        gpu.Queue().WriteTexture(&dst, k, 4, &layout, &one);

        wgpu::TextureView sceneView = scene.CreateView();
        wgpu::TextureView frameView = frame.CreateView();
        wgpu::TextureView blackView = black.CreateView();
        wgpu::TextureView noDepth;
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        pass.Run(enc, sceneView, scene, kW, kH, noDepth, 1, frameView, blackView, gpu);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        std::vector<uint8_t> out(kW * kH * 4);
        EXPECT_TRUE(gpu.ReadbackTexture(frame, kW, kH, out.data(), out.size()));
        double sum = 0;
        int n = 0;
        for (int y = 8; y < kH - 8; y++)
            for (int x = 8; x < kW - 8; x++) {
                sum += out[(y * kW + x) * 4 + 1];  // green: same in RGBA and BGRA
                n++;
            }
        return (float)(sum / n / 255.0);
    }
};

TEST_F(DC3RetailPostGpuTest, StreakBloomIsScreenBlendOfMaskedScene) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    const float atten = 0.92f;
    pp->SetProperty("bloom_streak", DataNode(1));
    pp->SetProperty("bloom_streak_attenuation", DataNode(atten));
    pp->SetProperty("bloom_streak_angle", DataNode(-45.0f));
    RndPostProc *prev = RndPostProc::Current();
    pp->Select();

    PostProcPass pass;
    pass.Init(gWgpuRnd->Gpu());
    struct Case { uint8_t grey, alpha; float intensity; } cases[] = {
        {77, 0, 0.25f},     // no mask: no bloom
        {77, 128, 0.25f},   // below saturation: the plain screen blend
        {204, 255, 0.1f},   // the sets saturate at 1 after the first pass
    };
    for (const Case &c : cases) {
        pp->SetProperty("bloom_intensity", DataNode(c.intensity));
        const float c6 = pp->GetBloomColor().green * RndPostProc::BloomLocFactor() * pp->BloomIntensity();
        const float got = RunFlat(pass, c.grey, c.alpha);
        const float s = c.grey / 255.0f, a = c.alpha / 255.0f;
        const float b = 2.0f * StreakSet(s * a, atten);  // set0 + set1, set2 black
        const float expected = std::min(1.0f, 1.0f - (1.0f - b * c6) * (1.0f - s));
        const float unclamped = 1.0f - (1.0f - 2.0f * s * a * (StreakSet(1e-3f, atten) / 1e-3f) * c6) * (1.0f - s);
        printf("DC3RetailPost: s=%.3f A=%.3f c6=%.3f got=%.4f expected=%.4f (16-bit sets would give %.4f)\n",
               s, a, c6, got, expected, unclamped);
        EXPECT_NEAR(got, expected, 3.0f / 255.0f)
            << "s=" << s << " A=" << a << ": not the screen blend of the 8-bit streak bloom";
    }
    pass.Terminate();
    if (prev) prev->Select(); else pp->Unselect();
    delete pp;
}

// RndShaderStandard / Particles / Multimesh / SyncTrack::CalcShaderOpts set
// mPseudoHDR only when !fadeOut, fadeOut = env->FadeOut() && FadeEnd !=
// FadeStart for the current environ; RndShaderDrawRect has no such test.
TEST_F(DC3RetailPostTest, BloomMaskOffWhileTheEnvironFadesOut) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    RndPostProc *prevPP = RndPostProc::Current();
    pp->Select();
    RndEnviron *prevEnv = RndEnviron::Current();
    RndEnviron *env = Hmx::Object::New<RndEnviron>();
    env->Select(nullptr);
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetProperty("bloom_multiplier", DataNode(1.0f));
    EXPECT_LT(rndshape::BloomMaskScale(mat), 0.0f) << "no fade: the mask is written";

    env->SetProperty("fade_out", DataNode(1));
    env->SetProperty("fade_start", DataNode(100.0f));
    env->SetProperty("fade_end", DataNode(100.0f));
    EXPECT_LT(rndshape::BloomMaskScale(mat), 0.0f) << "fade_end == fade_start is not a fade";
    env->SetProperty("fade_end", DataNode(400.0f));
    EXPECT_EQ(rndshape::BloomMaskScale(mat), 0.0f) << "a fading environ turns the mask off";
    EXPECT_LT(rndshape::BloomMaskScale(mat, false), 0.0f) << "a rect ignores the fade (RndShaderDrawRect)";
    env->SetProperty("fade_out", DataNode(0));
    EXPECT_LT(rndshape::BloomMaskScale(mat), 0.0f);

    if (prevEnv) prevEnv->Select(nullptr);
    if (prevPP) prevPP->Select(); else pp->Unselect();
    delete mat;
    delete pp;
    delete env;   // ~RndEnviron clears the current environ if it is this one
}

// NgPostProc::CheckMotionBlur: c105 = (weight.rgb * blend, weight.a), on only
// when RndPostProc::DoMotionBlur() holds (blend > 0, weight.Pack() > 0: the
// 8-bit rgb, not alpha).
TEST_F(DC3RetailPostTest, MotionBlurParamsFollowDoMotionBlur) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    RetailPostParams p;
    rndshape::FillRetailPost(pp, 1.0f, p);
    EXPECT_FALSE(p.motionBlur) << "the default post-process (blend 0) has no motion blur";
    pp->SetProperty("motion_blur_blend", DataNode(0.6f));
    // The weight syncs as a packed 0xBBGGRR int (PropSync(Hmx::Color&)); its
    // alpha is the separate motion_blur_exposure property.
    pp->SetProperty("motion_blur_weight", DataNode(0x40FF80));
    pp->SetProperty("motion_blur_exposure", DataNode(0.75f));
    rndshape::FillRetailPost(pp, 1.0f, p);
    ASSERT_TRUE(p.motionBlur);
    const Hmx::Color &w = pp->GetMotionBlurWeight();
    EXPECT_FLOAT_EQ(p.motionBlurParams[0], w.red * 0.6f);
    EXPECT_FLOAT_EQ(p.motionBlurParams[1], 0.6f);
    EXPECT_FLOAT_EQ(p.motionBlurParams[2], w.blue * 0.6f);
    EXPECT_NEAR(p.motionBlurParams[0], 0.3012f, 1e-3f);
    EXPECT_FLOAT_EQ(p.motionBlurParams[3], 0.75f) << "c105.w is the weight's alpha (exposure), unscaled";
    // DoMotionBlur's weight.Pack() > 0 ignores the alpha: black rgb is off.
    pp->SetProperty("motion_blur_weight", DataNode(0));
    rndshape::FillRetailPost(pp, 1.0f, p);
    EXPECT_FALSE(p.motionBlur) << "weight.Pack() == 0 turns motion blur off whatever its exposure";
    pp->SetProperty("motion_blur_weight", DataNode(0xFFFFFF));
    pp->SetProperty("motion_blur_blend", DataNode(0.0f));
    rndshape::FillRetailPost(pp, 1.0f, p);
    EXPECT_FALSE(p.motionBlur) << "blend 0 is off";
    delete pp;
}

// NgPostProc::DoBloom: doGlare = mBloomGlare && !TheHiResScreen.IsActive()
// takes the glare arm, which also excludes the streak arm.
TEST_F(DC3RetailPostTest, GlareArmFollowsBloomGlare) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    pp->SetProperty("bloom_intensity", DataNode(0.6f));
    pp->SetProperty("bloom_streak", DataNode(1));
    RetailPostParams p;
    rndshape::FillRetailPost(pp, 1.0f, p);
    EXPECT_FALSE(p.glare);
    EXPECT_TRUE(p.streak);
    pp->SetProperty("bloom_glare", DataNode(1));
    rndshape::FillRetailPost(pp, 1.0f, p);
    EXPECT_TRUE(p.glare) << "bloom_glare (off the hi-res screen) is the glare arm";
    EXPECT_FALSE(p.streak) << "glare excludes the streak arm";
    delete pp;
}

// The motion-blur composite over flat scenes, frame after frame (RB3RetailPost
// directly, so the run sequence is the test's): out = sat(c + (prev - c) * k),
// k = max(r, c105.rgb), r = sat(c105.w * (prev.a - scene.a) / prev.a), and the
// output alpha sat(scene.a + r * (prev.a - scene.a)) is the next prev.a.
TEST_F(DC3RetailPostGpuTest, MotionBlurIsRetailFeedback) {
    GpuDevice &gpu = gWgpuRnd->Gpu();
    auto &dev = gpu.Device();
    wgpu::TextureDescriptor d{};
    d.size = {(uint32_t)kW, (uint32_t)kH, 1};
    d.format = wgpu::TextureFormat::RGBA8Unorm;
    d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    wgpu::Texture scene = dev.CreateTexture(&d);
    d.format = gpu.SurfaceFormat();
    d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture frame = dev.CreateTexture(&d);
    wgpu::TextureView sceneView = scene.CreateView(), frameView = frame.CreateView();
    RB3RetailPost post;
    post.Init(gpu);
    std::vector<uint8_t> px(kW * kH * 4), out(kW * kH * 4);
    auto frameOf = [&](uint8_t grey, uint8_t alpha, const RetailPostParams &p) {
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = px[i + 1] = px[i + 2] = grey;
            px[i + 3] = alpha;
        }
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = scene;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = kW * 4;
        wgpu::Extent3D ext = {(uint32_t)kW, (uint32_t)kH, 1};
        gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        post.Run(enc, sceneView, sceneView, kW, kH, frameView, p, gpu);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        EXPECT_TRUE(gpu.ReadbackTexture(frame, kW, kH, out.data(), out.size()));
        return out[((kH / 2) * kW + kW / 2) * 4 + 1] / 255.0f;
    };
    RetailPostParams mb;
    mb.dc3 = true;
    mb.motionBlur = true;
    mb.motionBlurParams[0] = mb.motionBlurParams[1] = mb.motionBlurParams[2] = 0.6f;  // PostProc_local
    RetailPostParams off = mb;
    off.motionBlur = false;
    const float tol = 2.5f / 255.0f;
    auto u8 = [](float v) { return std::round(v * 255.0f) / 255.0f; };

    // c105.w = 0 (every shipped post-process): a plain 0.6 feedback.
    float f1 = frameOf(200, 0, mb);
    EXPECT_NEAR(f1, 200 / 255.0f, tol) << "first frame: no previous output, the scene graded";
    float f2 = frameOf(50, 0, mb);
    const float e2 = u8(50 / 255.0f + (f1 - 50 / 255.0f) * 0.6f);
    EXPECT_NEAR(f2, e2, tol) << "second frame: c + (prev - c) * 0.6";
    float f3 = frameOf(50, 0, mb);
    EXPECT_NEAR(f3, 50 / 255.0f + (e2 - 50 / 255.0f) * 0.6f, tol) << "the feedback decays geometrically";
    printf("DC3RetailPost motion blur: 200 -> 50 -> 50 gives %.4f %.4f %.4f\n", f1, f2, f3);

    // Off for one frame, then on: the previous output is gone (retail resolves it
    // every frame; here the first frame back passes the scene through rather
    // than mixing in an output two frames old).
    frameOf(255, 0, off);
    EXPECT_NEAR(frameOf(50, 0, mb), 50 / 255.0f, tol) << "a stale previous frame was mixed in";

    // c105.w > 0, the exposure: where the mask (alpha) fell since last frame the
    // previous output wins by r = (prev.a - scene.a) / prev.a.
    mb.motionBlurParams[3] = 1.0f;
    frameOf(0, 0, off);
    frameOf(200, 255, mb);                  // first frame again: out 200, prev.a = 1
    const float g = frameOf(50, 64, mb);    // scene.a = 0.251
    const float sa = 64 / 255.0f, r = 1.0f - sa, k = std::max(r, 0.6f);
    EXPECT_NEAR(g, 50 / 255.0f + (200 / 255.0f - 50 / 255.0f) * k, tol)
        << "k = max(r, c105.rgb): the exposure term outweighs the 0.6 blend";
    post.Terminate();
}

// DoBloom's glare arm (bloom_glare.ps) and bit 37's composite over a flat,
// fully masked field s: set0 = s after the blur, the glare pass's ten taps from
// uv towards 1 - uv each weigh w = (1 - 4 min(|p - 0.5|^2, 0.25))^2, the result
// 2 - 20 / sum(1 / (1 - w s)) saturates in the 8-bit set, and the composite
// ADDS 0.5 * c6 of it (no screen blend). At the centre every tap has w = 1, so
// the glare is 2s exactly.
TEST_F(DC3RetailPostGpuTest, GlareIsAdditiveRadialHarmonicMean) {
    GpuDevice &gpu = gWgpuRnd->Gpu();
    auto &dev = gpu.Device();
    wgpu::TextureDescriptor d{};
    d.size = {(uint32_t)kW, (uint32_t)kH, 1};
    d.format = wgpu::TextureFormat::RGBA8Unorm;
    d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    wgpu::Texture scene = dev.CreateTexture(&d);
    d.format = gpu.SurfaceFormat();
    d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture frame = dev.CreateTexture(&d);
    wgpu::TextureView sceneView = scene.CreateView(), frameView = frame.CreateView();
    RB3RetailPost post;
    post.Init(gpu);
    std::vector<uint8_t> px(kW * kH * 4), out(kW * kH * 4);
    const uint8_t grey = 77;   // s = 0.302
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = px[i + 1] = px[i + 2] = grey;
        px[i + 3] = 255;
    }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = scene;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = kW * 4;
    wgpu::Extent3D ext = {(uint32_t)kW, (uint32_t)kH, 1};
    gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);

    RetailPostParams g;
    g.dc3 = true;
    g.bloom = true;
    g.glare = true;
    g.bloomColor[0] = g.bloomColor[1] = g.bloomColor[2] = 1.6f;   // c6 can exceed 1
    wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
    post.Run(enc, sceneView, sceneView, kW, kH, frameView, g, gpu);
    wgpu::CommandBuffer cmd = enc.Finish();
    gpu.Queue().Submit(1, &cmd);
    ASSERT_TRUE(gpu.ReadbackTexture(frame, kW, kH, out.data(), out.size()));
    post.Terminate();

    const float s = grey / 255.0f, c6 = 1.6f;
    auto glareAt = [&](float u, float v) {
        const float su = (1.0f - 2.0f * u) * 0.1f, sv = (1.0f - 2.0f * v) * 0.1f;
        float acc = 0, pu = u, pv = v;
        for (int i = 0; i < 10; i++) {
            const float du = pu - 0.5f, dv = pv - 0.5f;
            float w = 1.0f - 4.0f * std::min(du * du + dv * dv, 0.25f);
            w *= w;
            acc += 1.0f / (1.0f - w * s);
            pu += su;
            pv += sv;
        }
        return std::min(1.0f, std::max(0.0f, 2.0f - 20.0f / acc));
    };
    struct Probe { int x, y; } probes[] = {{kW / 2, kH / 2}, {kW / 4, kH / 2}, {kW / 8, kH / 4}, {4, 4}};
    for (const Probe &pr : probes) {
        const float u = (pr.x + 0.5f) / kW, v = (pr.y + 0.5f) / kH;
        const float gl = glareAt(u, v);
        const float expected = std::min(1.0f, s + 0.5f * c6 * gl);
        const float screen = 1.0f - (1.0f - c6 * gl) * (1.0f - s);
        const float got = out[(pr.y * kW + pr.x) * 4 + 1] / 255.0f;
        printf("DC3RetailPost glare: uv=(%.3f %.3f) glare=%.4f got=%.4f expected=%.4f (screen blend %.4f)\n",
               u, v, gl, got, expected, screen);
        EXPECT_NEAR(got, expected, 3.0f / 255.0f)
            << "uv=(" << u << "," << v << "): not scene + 0.5 c6 * bloom_glare";
    }
}

// NgPostProc::CheckHueConverge: c222 = (hue_target / 360 + 0.5, hue_focus,
// blend_amount, brightness_power), bit 62 on when blend_amount > 0.
TEST_F(DC3RetailPostTest, HueConvergeParamsFollowBlendAmount) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    RetailPostParams p;
    rndshape::FillRetailPost(pp, 1.0f, p);
    EXPECT_FALSE(p.hueConverge) << "blend_amount defaults to 0: off";
    pp->SetProperty("hue_target", DataNode(-75.0f));
    pp->SetProperty("hue_focus", DataNode(0.25f));
    pp->SetProperty("blend_amount", DataNode(0.7f));
    pp->SetProperty("brightness_power", DataNode(1.3f));
    rndshape::FillRetailPost(pp, 1.0f, p);
    ASSERT_TRUE(p.hueConverge);
    EXPECT_FLOAT_EQ(p.hueParams[0], -75.0f / 360.0f + 0.5f);
    EXPECT_FLOAT_EQ(p.hueParams[1], 0.25f);
    EXPECT_FLOAT_EQ(p.hueParams[2], 0.7f);
    EXPECT_FLOAT_EQ(p.hueParams[3], 1.3f);
    delete pp;
}

// Rnd::DoPostProcess / BeginDrawing take the blacklight override first, then
// the override, then the selected post-process.
TEST_F(DC3RetailPostTest, ActivePostProcFollowsRndOverrides) {
    RndPostProc *sel = Hmx::Object::New<RndPostProc>();
    RndPostProc *ovr = Hmx::Object::New<RndPostProc>();
    RndPostProc *blk = Hmx::Object::New<RndPostProc>();
    RndPostProc *prev = RndPostProc::Current();
    sel->Select();
    EXPECT_EQ(rndshape::ActivePostProc(), sel);
    TheRnd.SetPostProcOverride(ovr);
    EXPECT_EQ(rndshape::ActivePostProc(), ovr) << "set_postproc_override wins over the selection";
    TheRnd.SetPostProcBlacklightOverride(blk);
    EXPECT_EQ(rndshape::ActivePostProc(), blk) << "the blacklight override wins over both";
    TheRnd.SetPostProcBlacklightOverride(nullptr);
    EXPECT_EQ(rndshape::ActivePostProc(), ovr);
    TheRnd.SetPostProcOverride(nullptr);
    EXPECT_EQ(rndshape::ActivePostProc(), sel);
    if (prev) prev->Select(); else sel->Unselect();
    delete blk;
    delete ovr;
    delete sel;
}

// CPU model of postprocess.ps bit 62 (checked against a Xenos interpreter
// run of postprocess_4000000001200010 over 4000 random inputs, np-gfx-post
// round 2): HSV (EasyRGB), H' = frac(T + 2 dist(H, T) (1 - focus)),
// V' = V^power, back to RGB, mixed in by blend.
static void HueConvergeModel(const float c[3], const float hue[4], float out[3]) {
    const float mx = std::max(std::max(c[0], c[1]), c[2]);
    const float mn = std::min(std::min(c[0], c[1]), c[2]);
    const float delta = mx - mn;
    float h = 0, s = 0;
    if (delta != 0) {
        s = delta / mx;
        float d[3];
        for (int i = 0; i < 3; i++) d[i] = ((mx - c[i]) / 6.0f + delta * 0.5f) / delta;
        if (c[0] == mx) h = d[2] - d[1];
        else if (c[1] == mx) h = 1.0f / 3.0f + d[0] - d[2];
        else h = 2.0f / 3.0f + d[1] - d[0];
        if (h < 0) h += 1;
        if (h > 1) h -= 1;
    }
    const float dd = std::fabs(h - hue[0]);
    const float dist = dd > 0.5f ? 1 - dd : dd;
    float hn = hue[0] + 2 * dist * (1 - hue[1]);
    if (hn > 1) hn -= 1;
    float v = mx;
    if (hue[3] != 1) v = mx == 0 ? 0 : std::exp2(hue[3] * std::log2(std::fabs(mx)));
    float rgb[3] = {v, v, v};
    if (s != 0) {
        const float h6 = hn * 6, i = std::floor(h6), f = h6 - i;
        const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
        const float tab[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
        const int k = i < 5 ? (int)i : 5;
        for (int j = 0; j < 3; j++) rgb[j] = tab[k][j];
    }
    for (int j = 0; j < 3; j++)
        out[j] = std::min(1.0f, std::max(0.0f, c[j] + (rgb[j] - c[j]) * hue[2]));
}

// Bit 62 over flat scenes, alone and after motion blur (where it is last, so
// the next frame's previous output carries it).
TEST_F(DC3RetailPostGpuTest, HueConvergeIsRetailBit62) {
    GpuDevice &gpu = gWgpuRnd->Gpu();
    auto &dev = gpu.Device();
    wgpu::TextureDescriptor d{};
    d.size = {(uint32_t)kW, (uint32_t)kH, 1};
    d.format = wgpu::TextureFormat::RGBA8Unorm;
    d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    wgpu::Texture scene = dev.CreateTexture(&d);
    d.format = gpu.SurfaceFormat();
    d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture frame = dev.CreateTexture(&d);
    wgpu::TextureView sceneView = scene.CreateView(), frameView = frame.CreateView();
    const bool bgra = gpu.SurfaceFormat() == wgpu::TextureFormat::BGRA8Unorm ||
        gpu.SurfaceFormat() == wgpu::TextureFormat::BGRA8UnormSrgb;
    RB3RetailPost post;
    post.Init(gpu);
    std::vector<uint8_t> px(kW * kH * 4), out(kW * kH * 4);
    auto frameOf = [&](const uint8_t rgb[3], const RetailPostParams &p, float res[3]) {
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = rgb[0];
            px[i + 1] = rgb[1];
            px[i + 2] = rgb[2];
            px[i + 3] = 0;
        }
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = scene;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = kW * 4;
        wgpu::Extent3D ext = {(uint32_t)kW, (uint32_t)kH, 1};
        gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        post.Run(enc, sceneView, sceneView, kW, kH, frameView, p, gpu);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        EXPECT_TRUE(gpu.ReadbackTexture(frame, kW, kH, out.data(), out.size()));
        const uint8_t *o = &out[((kH / 2) * kW + kW / 2) * 4];
        res[0] = o[bgra ? 2 : 0] / 255.0f;
        res[1] = o[1] / 255.0f;
        res[2] = o[bgra ? 0 : 2] / 255.0f;
    };
    RetailPostParams hp;
    hp.dc3 = true;
    hp.hueConverge = true;
    const float hue[4] = {-75.0f / 360.0f + 0.5f, 0.1f, 0.8f, 1.5f};   // (120 30 160) wraps
    std::memcpy(hp.hueParams, hue, sizeof(hue));
    const float tol = 2.5f / 255.0f;
    const uint8_t cols[][3] = {{200, 50, 50}, {50, 200, 80}, {40, 60, 220},
                               {230, 210, 40}, {120, 30, 160}, {128, 128, 128}};
    for (const auto &col : cols) {
        const float c[3] = {col[0] / 255.0f, col[1] / 255.0f, col[2] / 255.0f};
        float got[3], exp[3];
        frameOf(col, hp, got);
        HueConvergeModel(c, hue, exp);
        printf("DC3RetailPost hue: (%d %d %d) -> got (%.3f %.3f %.3f) expected (%.3f %.3f %.3f)\n", col[0],
               col[1], col[2], got[0], got[1], got[2], exp[0], exp[1], exp[2]);
        for (int j = 0; j < 3; j++)
            EXPECT_NEAR(got[j], exp[j], tol) << "colour (" << (int)col[0] << " " << (int)col[1] << " "
                                             << (int)col[2] << ") channel " << j;
    }
    // After motion blur: frame 1 (first) = hue(A); frame 2 =
    // hue(B + (frame1 - B) * 0.6), the previous output being post-hue.
    RetailPostParams mb = hp;
    mb.motionBlur = true;
    mb.motionBlurParams[0] = mb.motionBlurParams[1] = mb.motionBlurParams[2] = 0.6f;
    RetailPostParams off;
    off.dc3 = true;
    float dummy[3], f1[3], f2[3];
    frameOf(cols[5], off, dummy);
    frameOf(cols[0], mb, f1);
    frameOf(cols[2], mb, f2);
    const float b[3] = {cols[2][0] / 255.0f, cols[2][1] / 255.0f, cols[2][2] / 255.0f};
    float mixed[3], exp2c[3];
    for (int j = 0; j < 3; j++) mixed[j] = b[j] + (f1[j] - b[j]) * 0.6f;
    HueConvergeModel(mixed, hue, exp2c);
    printf("DC3RetailPost hue after motion blur: got (%.3f %.3f %.3f) expected (%.3f %.3f %.3f)\n", f2[0],
           f2[1], f2[2], exp2c[0], exp2c[1], exp2c[2]);
    for (int j = 0; j < 3; j++)
        EXPECT_NEAR(f2[j], exp2c[j], 3.0f / 255.0f) << "bit 62 after the motion-blur mix, channel " << j;
    post.Terminate();
}

// Frame cost of the chain's passes at 1280x720 (np-gfx-post performance
// report): mean GPU+submit time per Run over 200 runs, one sync at the end.
// Prints the numbers; asserts only that a disabled effect costs nothing
// measurable over the bare composite (no extra passes are recorded for it).
TEST_F(DC3RetailPostGpuTest, PassCostAt720p) {
    GpuDevice &gpu = gWgpuRnd->Gpu();
    auto &dev = gpu.Device();
    const int W = 1280, H = 720;
    wgpu::TextureDescriptor d{};
    d.size = {(uint32_t)W, (uint32_t)H, 1};
    d.format = wgpu::TextureFormat::RGBA8Unorm;
    d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
        wgpu::TextureUsage::RenderAttachment;
    wgpu::Texture scene = dev.CreateTexture(&d);
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (size_t i = 0; i < px.size(); i++) px[i] = (uint8_t)((i * 2654435761u) >> 24);
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = scene;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = W * 4;
    wgpu::Extent3D ext = {(uint32_t)W, (uint32_t)H, 1};
    gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);
    d.format = gpu.SurfaceFormat();
    d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture frame = dev.CreateTexture(&d);
    wgpu::TextureView sceneView = scene.CreateView(), frameView = frame.CreateView();

    RB3RetailPost post;
    post.Init(gpu);
    std::vector<uint8_t> sink((size_t)W * H * 4);
    auto timeIt = [&](const RetailPostParams &p) {
        auto runN = [&](int n) {
            for (int i = 0; i < n; i++) {
                wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
                post.Run(enc, sceneView, sceneView, W, H, frameView, p, gpu);
                wgpu::CommandBuffer cmd = enc.Finish();
                gpu.Queue().Submit(1, &cmd);
            }
            EXPECT_TRUE(gpu.ReadbackTexture(frame, W, H, sink.data(), sink.size()));
        };
        runN(10);
        const auto t0 = std::chrono::steady_clock::now();
        runN(200);
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count() / 200.0;
    };
    RetailPostParams base;
    base.dc3 = true;
    RetailPostParams plain = base;
    plain.bloom = true;
    plain.bloomColor[0] = plain.bloomColor[1] = plain.bloomColor[2] = 1.0f;
    RetailPostParams streak = plain;
    streak.streak = true;
    streak.streakAttenuation = 0.92f;
    streak.streakAngle = -45.0f;
    RetailPostParams all = streak;
    all.vignetteIntensity = 1.05f;
    all.chromaticOffset = 2.0f;
    all.xfm[0][0] = 1.1f;
    RetailPostParams mbOn = base;   // PostProc_local's motion blur, no bloom
    mbOn.motionBlur = true;
    mbOn.motionBlurParams[0] = mbOn.motionBlurParams[1] = mbOn.motionBlurParams[2] = 0.6f;
    RetailPostParams streakMb = streak;   // a venue's: streak bloom + motion blur
    streakMb.motionBlur = true;
    streakMb.motionBlurParams[0] = streakMb.motionBlurParams[1] = streakMb.motionBlurParams[2] = 0.4f;
    RetailPostParams glareMb = plain;     // party_mode_hub PostProc_Win: glare + motion blur
    glareMb.glare = true;
    glareMb.motionBlur = true;
    glareMb.motionBlurParams[0] = glareMb.motionBlurParams[1] = glareMb.motionBlurParams[2] = 0.6f;
    const double tBase = timeIt(base), tPlain = timeIt(plain), tStreak = timeIt(streak),
                 tAll = timeIt(all), tMb = timeIt(mbOn), tStreakMb = timeIt(streakMb),
                 tGlareMb = timeIt(glareMb), tBase2 = timeIt(base);
    post.Terminate();
    // The scan-out ramp (WgpuRnd::EndDrawing): a copy and one full-screen pass.
    DisplayRamp ramp;
    auto rampN = [&](int n) {
        for (int i = 0; i < n; i++) {
            wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
            EXPECT_TRUE(ramp.Apply(enc, frame, frameView, 0.85f, gpu));
            wgpu::CommandBuffer cmd = enc.Finish();
            gpu.Queue().Submit(1, &cmd);
        }
        EXPECT_TRUE(gpu.ReadbackTexture(frame, W, H, sink.data(), sink.size()));
    };
    rampN(10);
    const auto r0 = std::chrono::steady_clock::now();
    rampN(200);
    const double tRamp =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - r0).count() /
        200.0;
    printf("DC3RetailPost cost @%dx%d (us/frame): composite only %.0f (again %.0f), "
           "+plain bloom %.0f, +streak bloom %.0f, streak+vignette+chroma+xfm %.0f, "
           "+motion blur %.0f, streak bloom+motion blur %.0f, glare+motion blur %.0f, scan-out ramp %.0f\n",
           W, H, tBase, tBase2, tPlain, tStreak, tAll, tMb, tStreakMb, tGlareMb, tRamp);
    EXPECT_GT(tBase, 0.0);
}

// ---- round 3: the velocity blur (NgPostProc::DoVelocity, bit 42) ----------
//
// RndVelocityBuffer::Draw (dc3-decomp rndobj/VelocityBuffer.cpp), its native
// bookkeeping DrawNative / TakeNativeFrame, the backend's camera velocity pass
// (velocity_camera.ps) and the composite's ten taps (postprocess.ps bit 42).
// Sabotages each was checked against: lanes/post/round3/sabotage.

namespace {

// The view-projection the backend projects a camera with (WgpuRnd's
// CamSceneMatrices for a camera with no tool-set matrix): view * proj, row-major,
// row vectors.
void CamVP(RndCam *cam, float vp[16]) {
    cam->UpdatedWorldXfm();
    Transform v;
    Hmx::Matrix4 p;
    rndshape::CamViewProjectXfms(cam, v, p);
    const float view[16] = {v.m.x.x, v.m.x.y, v.m.x.z, 0, v.m.y.x, v.m.y.y, v.m.y.z, 0,
                            v.m.z.x, v.m.z.y, v.m.z.z, 0, v.v.x,   v.v.y,   v.v.z,   1};
    const float proj[16] = {p.x.x, p.x.y, p.x.z, p.x.w, p.y.x, p.y.y, p.y.z, p.y.w,
                            p.z.x, p.z.y, p.z.z, p.z.w, p.w.x, p.w.y, p.w.z, p.w.w};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) sum += view[i * 4 + k] * proj[k * 4 + j];
            vp[i * 4 + j] = sum;
        }
}

// velocity_camera.ps on the CPU, the way the image computes it: the linear depth
// from the depth-range constants (c89 = (near, far, 1/(zr.y - zr.x), zr.x/(zr.y -
// zr.x)); z01 = the non-reversed depth), the world position along the
// interpolated far-plane frustum corner (RndCam::GetCamFrustum), last frame's
// view-projection, then v = clamp(prevUV - uv, +-0.02) encoded in 8 bits.
void VelocityModel(RndCam *cur, const float prevVP[16], float depth, float u, float v,
                   float out[3]) {
    Vector4 c89;
    cur->GetDepthRangeValues(c89);
    Vector3 origin, dirs[4];
    cur->GetCamFrustum(origin, dirs);
    const double n = c89.x, f = c89.y;
    const double viewDepth = n * f / (f - depth * (f - n));
    const double t = viewDepth / f;
    // sCorners (0,0) (0,1) (1,0) (1,1): screen (x, y), bilinear.
    double w[3];
    const Vector3 &d00 = dirs[0], &d01 = dirs[1], &d10 = dirs[2], &d11 = dirs[3];
    const double cu = u, cv = v;
    w[0] = (1 - cu) * (1 - cv) * d00.x + (1 - cu) * cv * d01.x + cu * (1 - cv) * d10.x + cu * cv * d11.x;
    w[1] = (1 - cu) * (1 - cv) * d00.y + (1 - cu) * cv * d01.y + cu * (1 - cv) * d10.y + cu * cv * d11.y;
    w[2] = (1 - cu) * (1 - cv) * d00.z + (1 - cu) * cv * d01.z + cu * (1 - cv) * d10.z + cu * cv * d11.z;
    const double P[4] = {origin.x + t * w[0], origin.y + t * w[1], origin.z + t * w[2], 1.0};
    double clip[4] = {0, 0, 0, 0};
    for (int j = 0; j < 4; j++)
        for (int k = 0; k < 4; k++) clip[j] += P[k] * prevVP[k * 4 + j];
    double vx = 0.5 + 0.5 * clip[0] / clip[3] - u;
    double vy = 0.5 - 0.5 * clip[1] / clip[3] - v;
    vx = std::max(-0.02, std::min(0.02, vx));
    vy = std::max(-0.02, std::min(0.02, vy));
    out[0] = (float)(vx * 25 + 0.5);
    out[1] = (float)(vy * 25 + 0.5);
    out[2] = (float)(1.767767 * std::sqrt(vx * vx + vy * vy));
}

// A 4-sample depth target cleared to `d` (the world pass's depth at one value).
wgpu::Texture ClearedDepth(GpuDevice &gpu, int w, int h, float d) {
    wgpu::TextureDescriptor td{};
    td.size = {(uint32_t)w, (uint32_t)h, 1};
    td.format = wgpu::TextureFormat::Depth32Float;
    td.sampleCount = 4;
    td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    wgpu::Texture t = gpu.Device().CreateTexture(&td);
    wgpu::RenderPassDepthStencilAttachment da{};
    wgpu::TextureView view = t.CreateView();
    da.view = view;
    da.depthLoadOp = wgpu::LoadOp::Clear;
    da.depthStoreOp = wgpu::StoreOp::Store;
    da.depthClearValue = d;
    wgpu::RenderPassDescriptor rp{};
    rp.depthStencilAttachment = &da;
    wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    pass.End();
    wgpu::CommandBuffer cmd = enc.Finish();
    gpu.Queue().Submit(1, &cmd);
    return t;
}

}  // namespace

// The bookkeeping of RndVelocityBuffer::Draw, natively: c122 = min(2,
// 41.666668 / (ms since the last Draw + 1)); only the camera EndWorld cached
// (CacheCameraSettings) passes; ready (bit 42) from the second consecutive
// frame after ResetFrame, which CamShot::StartAnim calls on every cut; and the
// backend takes each frame's result once, through the rndshape accessor.
TEST_F(DC3RetailPostTest, VelocityBufferFollowsDrawsBookkeeping) {
    RndVelocityBuffer &vb = RndVelocityBuffer::Singleton();
    RndCam *cam = Hmx::Object::New<RndCam>();
    RndCam *other = Hmx::Object::New<RndCam>();
    ObjPtrList<RndDrawable> none(nullptr);
    bool ready = true;
    float scale = 0.0f;
    RndCam *got = nullptr;
    rndshape::TakeVelocityFrame(ready, scale, got);   // whatever an earlier frame left

    vb.CacheCameraSettings(cam);
    vb.DrawNative(cam, none);
    vb.DrawNative(cam, none);   // the camera is now the last frame's
    vb.ResetFrame();            // a cut
    EXPECT_FALSE(vb.DrawNative(cam, none)) << "the frame after a cut has no previous frame";
    ASSERT_TRUE(rndshape::TakeVelocityFrame(ready, scale, got))
        << "the rndshape accessor must reach RndVelocityBuffer::TakeNativeFrame";
    EXPECT_FALSE(ready);
    EXPECT_EQ(got, cam);
    EXPECT_FALSE(rndshape::TakeVelocityFrame(ready, scale, got)) << "taken once per frame";
    EXPECT_TRUE(vb.DrawNative(cam, none)) << "the second frame after a cut is ready";
    ASSERT_TRUE(rndshape::TakeVelocityFrame(ready, scale, got));
    EXPECT_TRUE(ready);
    EXPECT_NEAR(scale, 2.0f, 1e-4f) << "back-to-back Draws: 41.67 / (~0 + 1), capped at 2";
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_TRUE(vb.DrawNative(cam, none));
    ASSERT_TRUE(rndshape::TakeVelocityFrame(ready, scale, got));
    EXPECT_GT(scale, 0.3f);
    EXPECT_LT(scale, 41.666668f / 61.0f + 1e-3f) << "c122 = 41.67 / (split ms + 1)";
    vb.ResetFrame();
    EXPECT_FALSE(vb.DrawNative(cam, none));
    EXPECT_TRUE(vb.DrawNative(cam, none));

    // Draw only runs for the camera CacheCameraSettings saw.
    vb.CacheCameraSettings(other);
    EXPECT_FALSE(vb.DrawNative(cam, none));
    EXPECT_FALSE(rndshape::TakeVelocityFrame(ready, scale, got)) << "the camera test failed: no frame";
    vb.CacheCameraSettings(cam);
    delete other;
    delete cam;
}

// {rnd toggle_all_postprocs}: Rnd::DoPostProcess skips DoPost, and the frame is
// the scene through the option-free postprocess.ps (no grade, no bloom).
TEST_F(DC3RetailPostGpuTest, TogglePostProcsShowsTheSceneUngraded) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    pp->SetProperty("brightness", DataNode(40.0f));
    RndPostProc *prev = RndPostProc::Current();
    pp->Select();
    PostProcPass pass;
    pass.Init(gWgpuRnd->Gpu());
    auto toggle = [] {
        DataArray *a = DataReadString("{rnd toggle_all_postprocs}");
        ASSERT_NE(a, nullptr);
        a->Evaluate(0);
        a->Release();
    };
    ASSERT_FALSE(TheRnd.DisablePP());
    const float graded = RunFlat(pass, 100, 0);
    toggle();
    EXPECT_TRUE(TheRnd.DisablePP());
    EXPECT_TRUE(rndshape::PostProcDisabled()) << "the accessor must follow Rnd::mDisablePostProc";
    const float off = RunFlat(pass, 100, 0);
    toggle();
    EXPECT_FALSE(rndshape::PostProcDisabled());
    const float again = RunFlat(pass, 100, 0);
    printf("DC3RetailPost toggle_all_postprocs: graded %.4f, disabled %.4f, again %.4f (scene %.4f)\n",
           graded, off, again, 100 / 255.0f);
    EXPECT_GT(std::fabs(graded - 100 / 255.0f), 0.05f) << "the post-process grades (brightness 40)";
    EXPECT_NEAR(off, 100 / 255.0f, 1.5f / 255.0f) << "disabled: the scene ungraded";
    EXPECT_NEAR(again, graded, 1.5f / 255.0f);
    pass.Terminate();
    if (prev) prev->Select(); else pp->Unselect();
    delete pp;
}

class DC3VelocityGpuTest : public DC3RetailPostGpuTest {
protected:
    static constexpr int kVW = 256, kVH = 144;   // a 16:9 frame; tf10 is 128 x 72
    void SetUp() override {
        DC3RetailPostGpuTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) return;
        cur = Hmx::Object::New<RndCam>();
        prev = Hmx::Object::New<RndCam>();
        cur->SetFrustum(1.0f, 1000.0f, 0.75f, 1.0f);
        prev->SetFrustum(1.0f, 1000.0f, 0.75f, 1.0f);
        pp = Hmx::Object::New<RndPostProc>();
        oldPP = RndPostProc::Current();
        pp->Select();
    }
    void TearDown() override {
        if (pp) {
            if (oldPP) oldPP->Select(); else pp->Unselect();
            delete pp;
        }
        delete cur;
        delete prev;
    }
    void Place(RndCam *c, float x, float z, float yawRad, float pitchRad = 0.0f, float rollRad = 0.0f) {
        Transform t;
        t.Reset();
        Hmx::Matrix3 r;
        MakeRotMatrix(Vector3(pitchRad, rollRad, yawRad), r, true);
        t.m = r;
        t.v = Vector3(x, 0.0f, z);
        c->SetLocalXfm(t);
    }
    // One frame through PostProcPass: the scene `px` (kVW x kVH RGBA8), the
    // depth cleared to `depth`, `ready`/`prevVP` as DoVelocity left them.
    // Returns the frame (RGBA8 or BGRA8) and leaves the velocity target in vel.
    void RunFrame(PostProcPass &pass, const std::vector<uint8_t> &px, float depth,
                  std::vector<uint8_t> &frameOut, std::vector<uint8_t> *velOut) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        auto &dev = gpu.Device();
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)kVW, (uint32_t)kVH, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
                  wgpu::TextureUsage::RenderAttachment;
        wgpu::Texture scene = dev.CreateTexture(&d);
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = scene;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = kVW * 4;
        wgpu::Extent3D ext = {(uint32_t)kVW, (uint32_t)kVH, 1};
        gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);
        d.format = gpu.SurfaceFormat();
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        wgpu::Texture frame = dev.CreateTexture(&d);
        wgpu::TextureDescriptor b{};
        b.size = {1, 1, 1};
        b.format = wgpu::TextureFormat::RGBA8Unorm;
        b.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture black = dev.CreateTexture(&b);
        wgpu::Texture depthTex = ClearedDepth(gpu, kVW, kVH, depth);
        wgpu::TextureViewDescriptor dvd{};
        dvd.aspect = wgpu::TextureAspect::DepthOnly;
        wgpu::TextureView depthView = depthTex.CreateView(&dvd);
        wgpu::TextureView sceneView = scene.CreateView(), frameView = frame.CreateView(),
                          blackView = black.CreateView();
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        pass.Run(enc, sceneView, scene, kVW, kVH, depthView, 4, frameView, blackView, gpu);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        frameOut.resize((size_t)kVW * kVH * 4);
        EXPECT_TRUE(gpu.ReadbackTexture(frame, kVW, kVH, frameOut.data(), frameOut.size()));
        if (velOut) {
            const wgpu::Texture &vt = pass.Retail().VelocityTexture();
            ASSERT_TRUE((bool)vt);
            velOut->resize((size_t)(kVW / 2) * (kVH / 2) * 4);
            EXPECT_TRUE(gpu.ReadbackTexture(vt, kVW / 2, kVH / 2, velOut->data(), velOut->size()));
        }
    }
    RndCam *cur = nullptr, *prev = nullptr;
    RndPostProc *pp = nullptr, *oldPP = nullptr;
};

// velocity_camera.ps against its CPU model (the frustum-corner reconstruction
// the image uses, through RndCam's own GetCamFrustum / GetDepthRangeValues, not
// the inverse matrix the backend folds): a camera that turned and slid since
// last frame, a depth plane at view depth ~8.
TEST_F(DC3VelocityGpuTest, VelocityCameraIsRetailReprojection) {
    // A small move (every texel inside the clamp) and a large one (roll and a
    // slide: part of the screen at the +-0.02 clamp, part not).
    struct Move { float x, z, yaw, pitch, roll; bool expectClamp; } moves[] = {
        {-0.25f, 0.05f, 0.012f, 0.004f, 0.0f, false},
        {-0.10f, 0.0f, 0.0f, 0.0f, 0.06f, true},
    };
    for (const Move &mv : moves) {
        PostProcPass pass;
        pass.Init(gWgpuRnd->Gpu());
        Place(prev, mv.x, mv.z, 0.0f, 0.0f, mv.roll);
        Place(cur, 0.0f, 0.0f, mv.yaw, mv.pitch);
        float prevVP[16], curVP[16];
        CamVP(prev, prevVP);
        CamVP(cur, curVP);
        pass.SetVelocity(true, 2.0f, prevVP);   // last frame's Draw (records its VP)
        pass.SetVelocity(true, 2.0f, curVP);    // this frame's
        const double n = 1.0, f = 1000.0, z = 8.0;
        const float depth = (float)(f * (z - n) / (z * (f - n)));   // standard depth of view z
        std::vector<uint8_t> px((size_t)kVW * kVH * 4, 128), frame, vel;
        RunFrame(pass, px, depth, frame, &vel);
        ASSERT_EQ(pass.Retail().VelocityPasses(), 1);
        const int hw = kVW / 2, hh = kVH / 2;
        int bad = 0, moving = 0, clamped = 0;
        float worst = 0;
        for (int j = 0; j < hh; j++)
            for (int i = 0; i < hw; i++) {
                float m[3];
                VelocityModel(cur, prevVP, depth, (i + 0.5f) / hw, (j + 0.5f) / hh, m);
                for (int c = 0; c < 3; c++) {
                    const float g = vel[(j * hw + i) * 4 + c] / 255.0f;
                    const float e = std::fabs(g - m[c]);
                    worst = std::max(worst, e);
                    if (e > 1.5f / 255.0f) bad++;
                }
                if (m[2] > 0.5f / 255.0f) moving++;
                if (std::fabs(m[0] - 0.5f) > 0.4999f || std::fabs(m[1] - 0.5f) > 0.4999f) clamped++;
            }
        printf("DC3 velocity pass: %d x %d, %d moving texels, %d at the +-0.02 clamp, worst |gpu - model| "
               "%.4f, %d channels off\n", hw, hh, moving, clamped, worst, bad);
        EXPECT_EQ(bad, 0) << "velocity_camera.ps: not the image's reprojection";
        EXPECT_GT(moving, hw * hh / 2) << "the camera moved: most texels must carry velocity";
        if (mv.expectClamp) {
            EXPECT_GT(clamped, 0) << "the case must reach the clamp (it is part of what is tested)";
            EXPECT_LT(clamped, hw * hh) << "and leave some of the screen inside it";
        } else {
            EXPECT_EQ(clamped, 0);
        }
        pass.Terminate();
    }
}

// Bit 42's composite: a one-texel vertical line under a camera sliding
// sideways (v uniform), blurred along x by the ten taps plus the centre,
// against a bilinear CPU model of postprocess_0000040000000010.
TEST_F(DC3VelocityGpuTest, VelocityBlurIsRetailBit42) {
    PostProcPass pass;
    pass.Init(gWgpuRnd->Gpu());
    Place(prev, -0.2f, 0.0f, 0.0f);
    Place(cur, 0.0f, 0.0f, 0.0f);
    float prevVP[16], curVP[16];
    CamVP(prev, prevVP);
    CamVP(cur, curVP);
    const float scale = 1.8f;
    pass.SetVelocity(true, scale, prevVP);
    pass.SetVelocity(true, scale, curVP);
    const double n = 1.0, f = 1000.0, z = 8.0;
    const float depth = (float)(f * (z - n) / (z * (f - n)));
    const int x0 = kVW / 2;
    std::vector<uint8_t> px((size_t)kVW * kVH * 4, 0), frame, vel;
    for (int y = 0; y < kVH; y++) {
        uint8_t *p = &px[(y * kVW + x0) * 4];
        p[0] = p[1] = p[2] = 255;   // alpha 0: no bloom mask, so no bloom of the line
    }
    RunFrame(pass, px, depth, frame, &vel);
    const int hw = kVW / 2;
    const uint8_t *vc = &vel[((kVH / 4) * hw + hw / 2) * 4];
    const float velx = vc[0] / 255.0f, vely = vc[1] / 255.0f, velz = vc[2] / 255.0f;
    ASSERT_GE(velz, 0.003f) << "the case must be above bit 42's threshold";
    ASSERT_NEAR(vely, 0.5f, 1.0f / 255.0f) << "a sideways slide: no vertical velocity";
    // The model: taps at uv - 0.023 d + k 0.0046 d, d = (vel.x - 0.5) * c122.
    const float d = (velx - 0.5f) * scale;
    const float st = 0.0046f * d * kVW, b0 = -0.023f * d * kVW;   // in texels
    const float wk[10] = {0.00876f, 0.02700f, 0.06476f, 0.12099f, 0.17603f,
                          0.17603f, 0.12099f, 0.06476f, 0.02700f, 0.00876f};
    auto line = [&](float x) {   // the scene's row, bilinear, x in texel centres
        const float xf = std::floor(x), t = x - xf;
        auto at = [&](int xi) { return xi == x0 ? 1.0f : 0.0f; };
        return at((int)xf) * (1 - t) + at((int)xf + 1) * t;
    };
    const int y = kVH / 2;
    int bad = 0, lit = 0;
    float worst = 0;
    for (int x = x0 - 12; x <= x0 + 12; x++) {
        float m = line((float)x) * 0.19947f;
        for (int k = 0; k < 10; k++) m += wk[k] * line(x + b0 + k * st);
        m = std::min(1.0f, m * 1.00549f);
        const float g = frame[(y * kVW + x) * 4 + 1] / 255.0f;
        worst = std::max(worst, std::fabs(g - m));
        if (std::fabs(g - m) > 3.0f / 255.0f) bad++;
        if (g > 4.0f / 255.0f) lit++;
        printf("  x %+3d: gpu %.4f model %.4f\n", x - x0, g, m);
    }
    printf("DC3 velocity blur: vel (%.4f %.4f %.4f), step %.3f texels, %d lit columns, worst %.4f\n",
           velx, vely, velz, st, lit, worst);
    EXPECT_EQ(bad, 0) << "postprocess.ps bit 42: not the image's taps";
    EXPECT_GE(lit, 5) << "the line must be smeared across several columns";
    pass.Terminate();
}

// Off costs nothing: no velocity target, no pass, the same composite; and the
// frame after a cut (not ready), a first frame with no previous VP, or a depth
// that is not multisampled, add none either.
TEST_F(DC3VelocityGpuTest, VelocityOffAddsNoPassAndNoTarget) {
    PostProcPass pass;
    pass.Init(gWgpuRnd->Gpu());
    Place(prev, -0.25f, 0.0f, 0.0f);
    Place(cur, 0.0f, 0.0f, 0.0f);
    float prevVP[16], curVP[16];
    CamVP(prev, prevVP);
    CamVP(cur, curVP);
    std::vector<uint8_t> px((size_t)kVW * kVH * 4, 90), frame, still;
    RunFrame(pass, px, 0.5f, still, nullptr);
    EXPECT_EQ(pass.Retail().VelocityPasses(), 0);
    EXPECT_FALSE((bool)pass.Retail().VelocityTexture()) << "off: no velocity target allocated";
    pass.SetVelocity(true, 2.0f, prevVP);    // ready, but no previous frame yet
    RunFrame(pass, px, 0.5f, frame, nullptr);
    EXPECT_EQ(pass.Retail().VelocityPasses(), 0) << "no previous view-projection: no pass";
    pass.SetVelocity(false, 2.0f, curVP);    // the frame after a cut
    RunFrame(pass, px, 0.5f, frame, nullptr);
    EXPECT_EQ(pass.Retail().VelocityPasses(), 0) << "not ready: no pass";
    EXPECT_FALSE((bool)pass.Retail().VelocityTexture());
    EXPECT_EQ(frame, still) << "not ready: the composite is the plain one";
    pass.SetVelocity(true, 2.0f, prevVP);    // ready: the pass runs and is consumed
    RunFrame(pass, px, 0.5f, frame, nullptr);
    EXPECT_EQ(pass.Retail().VelocityPasses(), 1);
    RunFrame(pass, px, 0.5f, frame, nullptr);   // no Draw this frame
    EXPECT_EQ(pass.Retail().VelocityPasses(), 1) << "the result is this frame's only";
    pass.Terminate();
}
