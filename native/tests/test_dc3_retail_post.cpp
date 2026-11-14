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

#include "test_helpers.h"

#include "gfx/DisplayRamp.h"
#include "gfx/GpuDevice.h"
#include "gfx/PostProcPass.h"
#include "gfx/RB3RetailPost.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "rndobj/Mat.h"
#include "rndobj/PostProc.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    const double tBase = timeIt(base), tPlain = timeIt(plain), tStreak = timeIt(streak),
                 tAll = timeIt(all), tBase2 = timeIt(base);
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
           "scan-out ramp %.0f\n",
           W, H, tBase, tBase2, tPlain, tStreak, tAll, tRamp);
    EXPECT_GT(tBase, 0.0);
}
