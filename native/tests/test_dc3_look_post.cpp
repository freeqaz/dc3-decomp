// test_dc3_look_post.cpp -- the enhanced look's post chain (np-gfx-post round 10).
//
// DC3_LOOK=enhanced (milo::LookSettings::postPrecision / postBloomFilter /
// postDofFilter) changes how DC3's retail post chain computes, never what:
// the bloom sets stay saturated per pass, the energy and extent of every term
// stay retail's, and only the world grade (the post run before the HUD/UI
// draws, PostProcPass::SetWorldGrade) is touched. Engine doc §37.11.
//
// Sabotages these were checked against (each made a test here fail):
//   - the enhancement keyed on the look alone, not on SetWorldGrade (the UI path changes)
//   - postPrecision ignored (8-bit sets): the bloom staircase is back
//   - the RGBA16Float family without its per-pass saturate (energy over retail's)
//   - no dither / a dither that changes every frame
//   - postBloomFilter's streak set1 back to retail's 16-of-256 taps (the streak blinks)
//   - the tent downsample back to the box (the bloom wobbles)
//   - the B-spline upsample's weights unnormalised (energy)
//   - DofPass's tap compensation dropped (the blur radius grows)
//   - DofPass's enhanced composite back to bilinear (the creases return)

#include "test_helpers.h"

#include "gfx/BindGroupCache.h"
#include "gfx/DofPass.h"
#include "gfx/GpuDevice.h"
#include "gfx/PostProcPass.h"
#include "gfx/RB3RetailPost.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "obj/DataFile.h"
#include "rndobj/DOFProc.h"
#include "rndobj/PostProc.h"
#include "rndobj/Rnd.h"
#include "ui/UI.h"
#include "ui/UIScreen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

class DC3LookPostTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
        if (getenv("DC3_RETAIL_POST")) GTEST_SKIP() << "DC3_RETAIL_POST selects another chain mode";
    }

    GpuDevice &Gpu() { return gWgpuRnd->Gpu(); }

    wgpu::Texture Upload(int w, int h, const std::vector<uint8_t> &px) {
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)w, (uint32_t)h, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
            wgpu::TextureUsage::RenderAttachment;
        wgpu::Texture t = Gpu().Device().CreateTexture(&d);
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = t;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = (uint32_t)w * 4;
        wgpu::Extent3D ext = {(uint32_t)w, (uint32_t)h, 1};
        Gpu().Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);
        return t;
    }
    wgpu::Texture Frame(int w, int h) {
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)w, (uint32_t)h, 1};
        d.format = Gpu().SurfaceFormat();
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        return Gpu().Device().CreateTexture(&d);
    }
    // The chain alone on `scene` (alpha = the bloom mask); the green channel
    // of the frame (the same in RGBA and BGRA).
    std::vector<float> RunChain(RB3RetailPost &chain, const wgpu::Texture &scene, int w, int h,
                                const RetailPostParams &p) {
        wgpu::Texture frame = Frame(w, h);
        wgpu::TextureView sv = scene.CreateView(), fv = frame.CreateView();
        wgpu::CommandEncoder enc = Gpu().Device().CreateCommandEncoder();
        chain.Run(enc, sv, sv, w, h, fv, p, Gpu());
        wgpu::CommandBuffer cmd = enc.Finish();
        Gpu().Queue().Submit(1, &cmd);
        std::vector<uint8_t> out((size_t)w * h * 4);
        EXPECT_TRUE(Gpu().ReadbackTexture(frame, w, h, out.data(), out.size()));
        std::vector<float> g((size_t)w * h);
        for (size_t i = 0; i < g.size(); i++) g[i] = out[i * 4 + 1];
        return g;
    }
    // The bloom term (debugView 2) read at two exposures, bloomColor and 1/16
    // of it, merged (the bright one wherever it is under 250, else the dim one
    // scaled up), in units of the bright exposure. The 8-bit frame alone
    // rounds a dim, wide term's tails to 0 and clips a bright, narrow one; the
    // streak arm's two filters differ in exactly that (retail's set1 puts a
    // phase's energy into one texel 16x brighter than the average, or none),
    // so one exposure would charge them different measurement errors.
    std::vector<float> RunBloomHdr(RB3RetailPost &chain, const wgpu::Texture &scene, int w, int h,
                                   RetailPostParams p) {
        const auto hi = RunChain(chain, scene, w, h, p);
        for (float &c : p.bloomColor) c /= 16.0f;
        const auto lo = RunChain(chain, scene, w, h, p);
        std::vector<float> g(lo.size());
        for (size_t i = 0; i < g.size(); i++) g[i] = hi[i] < 250.0f ? hi[i] : lo[i] * 16.0f;
        return g;
    }
};

// The bloom term (debugView 2: clamp(b * c6)) of the DC3 chain.
RetailPostParams BloomTerm(bool streak, float c6) {
    RetailPostParams p;
    p.dc3 = true;
    p.bloom = true;
    p.bloomColor[0] = p.bloomColor[1] = p.bloomColor[2] = c6;
    p.streak = streak;
    p.streakAttenuation = 0.5f;
    p.streakAngle = 0.0f;
    p.screenAspect = 0.5625f;
    p.debugView = 2;
    return p;
}

struct Moments {
    double energy = 0, cx = 0, cy = 0, vx = 0, vy = 0, peak = 0;
};
Moments Measure(const std::vector<float> &g, int w, int h) {
    Moments m;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const double v = g[(size_t)y * w + x];
            m.energy += v;
            m.peak = std::max(m.peak, v);
            m.cx += v * x;
            m.cy += v * y;
        }
    if (m.energy <= 0) return m;
    m.cx /= m.energy;
    m.cy /= m.energy;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const double v = g[(size_t)y * w + x];
            m.vx += v * (x - m.cx) * (x - m.cx);
            m.vy += v * (y - m.cy) * (y - m.cy);
        }
    m.vx /= m.energy;
    m.vy /= m.energy;
    return m;
}

// A 2x2 white dot centred at (cx, cy), area-weighted in colour so it moves by
// sub-pixel steps, on black, mask 1 everywhere. The mask is uniform on
// purpose: retail's bloom.ps multiplies the bilinear-filtered colour by the
// bilinear-filtered mask (fs_bloom_taps), which is not linear in the scene
// where both vary, so an area-weighted MASK would make retail's phase-averaged
// energy depend on how finely the test samples the phases.
std::vector<uint8_t> Dot(int w, int h, double cx, double cy) {
    std::vector<uint8_t> px((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const double ox = std::max(0.0, std::min(x + 1.0, cx + 1.0) - std::max((double)x, cx - 1.0));
            const double oy = std::max(0.0, std::min(y + 1.0, cy + 1.0) - std::max((double)y, cy - 1.0));
            const double a = ox * oy;
            if (a <= 0) continue;
            uint8_t *p = &px[((size_t)y * w + x) * 4];
            p[0] = p[1] = p[2] = (uint8_t)std::lround(255.0 * a);
        }
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    return px;
}

}  // namespace

// A post over the UI (EndDrawing, SetWorldGrade not called) is retail under
// either look, byte for byte; the world grade under the retail look is too.
TEST_F(DC3LookPostTest, UiPostIgnoresTheLook) {
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    RndPostProc *prev = RndPostProc::Current();
    pp->Select();
    constexpr int W = 256, H = 144;
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *p = &px[((size_t)y * W + x) * 4];
            const bool line = (x % 37) < 2 || (y % 29) == 0;   // thin bright features
            p[0] = (uint8_t)(line ? 250 : (x * 3 + y) & 63);
            p[1] = (uint8_t)(line ? 240 : (x + y * 2) & 63);
            p[2] = (uint8_t)(line ? 255 : (x ^ y) & 63);
            p[3] = (uint8_t)(line ? 255 : 0);
        }
    wgpu::Texture scene = Upload(W, H, px);
    wgpu::Texture frame = Frame(W, H);
    std::vector<uint8_t> k = {0, 0, 0, 255};
    wgpu::Texture black = Upload(1, 1, k);
    wgpu::TextureView sv = scene.CreateView(), fv = frame.CreateView(), bv = black.CreateView();
    wgpu::TextureView noDepth;
    PostProcPass pass;
    pass.Init(Gpu());
    auto run = [&](milo::LookProfile look, bool world) {
        milo::ScopedLookOverride o(look);
        pass.SetWorldGrade(world);
        wgpu::CommandEncoder enc = Gpu().Device().CreateCommandEncoder();
        pass.Run(enc, sv, scene, W, H, noDepth, 1, fv, bv, Gpu());
        wgpu::CommandBuffer cmd = enc.Finish();
        Gpu().Queue().Submit(1, &cmd);
        pass.SetWorldGrade(false);
        std::vector<uint8_t> out((size_t)W * H * 4);
        EXPECT_TRUE(Gpu().ReadbackTexture(frame, W, H, out.data(), out.size()));
        return out;
    };
    for (int streak = 0; streak < 2; streak++) {
        pp->SetProperty("bloom_streak", DataNode(streak));
        pp->SetProperty("bloom_streak_attenuation", DataNode(0.9f));
        pp->SetProperty("bloom_intensity", DataNode(2.0f));
        const auto retailUi = run(milo::LookProfile::Retail, false);
        const auto enhancedUi = run(milo::LookProfile::Enhanced, false);
        const auto retailWorld = run(milo::LookProfile::Retail, true);
        const auto enhancedWorld = run(milo::LookProfile::Enhanced, true);
        size_t uiDiff = 0, worldDiff = 0, retailDiff = 0;
        for (size_t i = 0; i < retailUi.size(); i++) {
            uiDiff += retailUi[i] != enhancedUi[i];
            retailDiff += retailUi[i] != retailWorld[i];
            worldDiff += retailUi[i] != enhancedWorld[i];
        }
        printf("DC3LookPost %s arm: bytes differing from retail -- UI post enhanced %zu, world retail %zu, "
               "world enhanced %zu of %zu\n", streak ? "streak" : "plain", uiDiff, retailDiff, worldDiff,
               retailUi.size());
        // CPU cost: encoding the world grade (no readback), mean of 100.
        // With the game's bind group cache, as WgpuRnd runs it.
        BindGroupCache cache;
        uint64_t frameId = 0;
        pass.SetBindGroupCache(&cache);
        double us[2];
        for (int e = 0; e < 2; e++) {
            milo::ScopedLookOverride o(e ? milo::LookProfile::Enhanced : milo::LookProfile::Retail);
            pass.SetWorldGrade(true);
            for (int warm = 0; warm < 2; warm++) {
                // Encode only: a Submit would wait on the GPU's queue and
                // charge GPU time to the CPU figure.
                double total = 0;
                for (int i = 0; i < 100; i++) {
                    cache.BeginFrame(++frameId);
                    const auto t0 = std::chrono::steady_clock::now();
                    wgpu::CommandEncoder enc = Gpu().Device().CreateCommandEncoder();
                    pass.Run(enc, sv, scene, W, H, noDepth, 1, fv, bv, Gpu());
                    wgpu::CommandBuffer cmd = enc.Finish();
                    total += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                    Gpu().Queue().Submit(1, &cmd);
                    if (i % 10 == 9) {
                        std::vector<uint8_t> sync((size_t)W * H * 4);   // drain the queue
                        Gpu().ReadbackTexture(frame, W, H, sync.data(), sync.size());
                    }
                }
                us[e] = total / 100;
            }
            pass.SetWorldGrade(false);
        }
        pass.SetBindGroupCache(nullptr);
        cache.Clear();
        printf("DC3LookPost %s arm: CPU per world grade (encode) retail %.1f us, enhanced %.1f us\n",
               streak ? "streak" : "plain", us[0], us[1]);
        EXPECT_EQ(uiDiff, 0u) << "a post over the UI must stay retail under DC3_LOOK=enhanced";
        EXPECT_EQ(retailDiff, 0u) << "DC3_LOOK=retail's world grade is today's";
        EXPECT_GT(worldDiff, 0u) << "the enhanced world grade changes something (non-vacuous)";
    }
    pass.Terminate();
    if (prev) prev->Select(); else pp->Unselect();
    delete pp;
}

// postBloomFilter: the bloom term's energy and extent are retail's, and a dot
// moving by sub-pixel steps keeps its bloom. Retail's 4x4 box puts the bloom
// of a dot anywhere inside a block at the block's centre, so the bloom jumps
// in 4-texel steps as the dot moves (its centroid wobbles about the dot's
// path); the streak set1's 4 bilinear taps read 16 of each block's 256
// texels, so the streak's energy blinks as well. Both arms, 1280x720.
TEST_F(DC3LookPostTest, EnhancedBloomKeepsEnergyAndExtentAndStopsWobbling) {
    constexpr int W = 1280, H = 720;
    RB3RetailPost chain;
    chain.Init(Gpu());
    for (int streak = 0; streak < 2; streak++) {
        const float c6 = streak ? 384.0f : 200.0f;
        // An 8x8 grid of positions, 2 pixels apart, over one 16x16 block of
        // set1 (the coarsest set the dot's position reaches), a quarter pixel
        // off the pixel grid. The 2x2 dot's coverage summed over the grid is
        // the same on every pixel of the block, so the grid's mean energy is
        // the phase-averaged energy exactly, however sparsely a filter samples
        // the block (a 1-D sweep is not: retail's streak set1 reads a few
        // texels in x AND y, and its energy is their product).
        constexpr int kPhases = 64;
        std::vector<Moments> m[2];
        std::vector<double> px, py;
        for (int k = 0; k < kPhases; k++) {
            const double cx = 640.25 + 2.0 * (k % 8), cy = 360.25 + 2.0 * (k / 8);
            px.push_back(cx);
            py.push_back(cy);
            wgpu::Texture scene = Upload(W, H, Dot(W, H, cx, cy));
            for (int e = 0; e < 2; e++) {
                RetailPostParams p = BloomTerm(streak, c6);
                p.enhPrecision = true;   // both sides 16-bit: this test is about the filter
                p.enhFilter = e == 1;
                m[e].push_back(Measure(RunBloomHdr(chain, scene, W, H, p), W, H));
            }
        }
        double energy[2], cv[2], sx[2], sy[2], wobble[2], peak[2];
        for (int e = 0; e < 2; e++) {
            double es = 0, eq = 0, vx = 0, vy = 0, ox = 0, oy = 0, wx = 0, wy = 0;
            peak[e] = 0;
            for (int k = 0; k < kPhases; k++) {
                es += m[e][k].energy;
                eq += m[e][k].energy * m[e][k].energy;
                ox += m[e][k].cx - px[k];
                oy += m[e][k].cy - py[k];
                wx += m[e][k].energy * (m[e][k].cx - px[k]);
                wy += m[e][k].energy * (m[e][k].cy - py[k]);
                peak[e] = std::max(peak[e], m[e][k].peak);
            }
            energy[e] = es / kPhases;
            cv[e] = std::sqrt(std::max(0.0, eq / kPhases - energy[e] * energy[e])) / energy[e];
            ox /= kPhases;
            oy /= kPhases;
            wx /= es;
            wy /= es;
            // The extent of the bloom seen over time: the second moment of
            // every phase's image placed at the dot (their sum, as the eye
            // integrates it), about its own centre. Linear in the images, so
            // a filter that blinks is charged its real footprint.
            for (int k = 0; k < kPhases; k++) {
                const double dx = m[e][k].cx - px[k] - wx, dy = m[e][k].cy - py[k] - wy;
                vx += m[e][k].energy * (m[e][k].vx + dx * dx);
                vy += m[e][k].energy * (m[e][k].vy + dy * dy);
            }
            sx[e] = std::sqrt(vx / es);
            sy[e] = std::sqrt(vy / es);
            // The centroid's RMS departure from the dot's path, a constant
            // offset taken out (the streak's half-texel steps are retail's).
            double w2 = 0;
            for (int k = 0; k < kPhases; k++) {
                const double dx = m[e][k].cx - px[k] - ox, dy = m[e][k].cy - py[k] - oy;
                w2 += dx * dx + dy * dy;
            }
            wobble[e] = std::sqrt(w2 / kPhases);
        }
        printf("DC3LookPost %s arm: energy %.0f -> %.0f (x%.3f), energy CV over %d phases %.4f -> %.4f, "
               "extent sx %.2f -> %.2f sy %.2f -> %.2f px, centroid wobble %.3f -> %.3f px, peak %.0f / %.0f\n",
               streak ? "streak" : "plain", energy[0], energy[1], energy[1] / energy[0], kPhases, cv[0],
               cv[1], sx[0], sx[1], sy[0], sy[1], wobble[0], wobble[1], peak[0], peak[1]);
        EXPECT_NEAR(energy[1] / energy[0], 1.0, 0.03) << "the enhanced filter keeps the bloom's energy";
        EXPECT_NEAR(sy[1] / sy[0], 1.0, 0.06) << "and its extent along y (the streak's own axis)";
        if (!streak) {
            EXPECT_NEAR(sx[1] / sx[0], 1.0, 0.03) << "and along x";
        } else {
            // Across the streak, set1's footprint seen over time is retail's
            // 16 sampled texels, variance 4.25 px^2 per axis; any filter that
            // counts every texel of a 16-texel stride is at least the box,
            // 21.25 px^2. That is the price of the blink, measured +10% on a
            // 6.6 px width; the bound keeps it from growing further.
            EXPECT_GT(sx[1] / sx[0], 1.0) << "the box is wider than retail's taps (non-vacuous)";
            EXPECT_LT(sx[1] / sx[0], 1.12) << "and no wider than the box makes it";
        }
        EXPECT_LT(wobble[1], wobble[0] * 0.5) << "a moving dot's bloom follows it";
        if (streak) EXPECT_LT(cv[1], cv[0] * 0.5) << "and the streak stops blinking";
    }
    chain.Terminate();
}

// postPrecision: a dim, wide glow's bloom term is a smooth ramp, not retail's
// staircase. The glow is wide enough that its true slope stays under one
// output level per pixel, so a 16-bit chain steps by at most a level; retail's
// 8-bit sets step by 1/255, which c6 = 6 makes 6 output levels, spread over a
// few pixels by the composite's bilinear upsample: steps of 2 among runs of 0.
TEST_F(DC3LookPostTest, SixteenBitSetsRemoveTheBloomStaircase) {
    constexpr int W = 1280, H = 720;
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const double r2 = ((x - 640.0) * (x - 640.0) + (y - 360.0) * (y - 360.0)) / (2 * 120.0 * 120.0);
            uint8_t *p = &px[((size_t)y * W + x) * 4];
            p[0] = p[1] = p[2] = (uint8_t)std::lround(10.0 * std::exp(-r2));
            p[3] = 255;
        }
    wgpu::Texture scene = Upload(W, H, px);
    RB3RetailPost chain;
    chain.Init(Gpu());
    int maxStep[2] = {}, bigSteps[2] = {}, peak[2] = {};
    for (int e = 0; e < 2; e++) {
        RetailPostParams p = BloomTerm(false, 6.0f);
        p.enhPrecision = e == 1;
        const auto g = RunChain(chain, scene, W, H, p);
        for (int y = 300; y <= 420; y += 30)
            for (int x = 360; x < 920; x++) {
                const int a = (int)g[(size_t)y * W + x], b = (int)g[(size_t)y * W + x + 1];
                maxStep[e] = std::max(maxStep[e], std::abs(a - b));
                bigSteps[e] += std::abs(a - b) >= 2;
                peak[e] = std::max(peak[e], a);
            }
    }
    printf("DC3LookPost precision: glow ramp (peak %d / %d levels) max step %d -> %d levels, "
           "steps of 2+ levels %d -> %d (5 rows x 560 px)\n",
           peak[0], peak[1], maxStep[0], maxStep[1], bigSteps[0], bigSteps[1]);
    EXPECT_LT(peak[0], 250) << "the ramp is not clamped (the test reads its steps)";
    EXPECT_GT(bigSteps[0], 50) << "retail's 8-bit sets make a staircase (non-vacuous)";
    EXPECT_LE(maxStep[1], 1) << "RGBA16Float sets give a smooth ramp";
    EXPECT_EQ(bigSteps[1], 0);
    chain.Terminate();
}

// postPrecision keeps retail's saturation: the RGBA16Float sets clamp at 1
// after every pass, as the 8-bit sets do, so where precision does not matter
// (a flat white field) the bloom term is retail's. The glare arm is where a
// set would run past 1 unclamped: bloom_glare.ps's 2 - 20 / sum(1 / (1 - w t))
// approaches 2 as w t approaches 1.
TEST_F(DC3LookPostTest, SixteenBitSetsStillSaturateLikeRetail) {
    constexpr int W = 1280, H = 720;
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *p = &px[((size_t)y * W + x) * 4];
            const bool lit = std::abs(x - 640) < 320 && std::abs(y - 360) < 180;
            p[0] = p[1] = p[2] = lit ? 255 : 0;
            p[3] = 255;
        }
    wgpu::Texture scene = Upload(W, H, px);
    RB3RetailPost chain;
    chain.Init(Gpu());
    std::vector<float> g[2];
    for (int e = 0; e < 2; e++) {
        RetailPostParams p = BloomTerm(false, 0.4f);   // 2 * 0.4 stays under 1: an unclamped set shows
        p.glare = true;
        p.enhPrecision = e == 1;
        g[e] = RunChain(chain, scene, W, H, p);
    }
    double maxDiff = 0, peak[2] = {};
    for (size_t i = 0; i < g[0].size(); i++) {
        maxDiff = std::max(maxDiff, (double)std::abs(g[0][i] - g[1][i]));
        for (int e = 0; e < 2; e++) peak[e] = std::max(peak[e], (double)g[e][i]);
    }
    printf("DC3LookPost saturation (glare arm, white field): peak %.0f -> %.0f, max difference %.0f levels\n",
           peak[0], peak[1], maxDiff);
    EXPECT_NEAR(peak[0], 0.4 * 255, 2.0) << "retail's glare set saturates at 1 (non-vacuous)";
    EXPECT_LE(maxDiff, 2.0) << "the 16-bit sets saturate where retail's do";
    chain.Terminate();
}

// postPrecision's dither: static (two runs agree byte for byte), within one
// level, and only under the enhanced look.
TEST_F(DC3LookPostTest, DitherIsStaticAndSubLevel) {
    constexpr int W = 128, H = 96;
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = px[i + 1] = px[i + 2] = 100;
        px[i + 3] = 0;
    }
    wgpu::Texture scene = Upload(W, H, px);
    RB3RetailPost chain;
    chain.Init(Gpu());
    RetailPostParams p;
    p.dc3 = true;
    const auto retail = RunChain(chain, scene, W, H, p);
    p.enhPrecision = true;
    const auto a = RunChain(chain, scene, W, H, p);
    const auto b = RunChain(chain, scene, W, H, p);
    double sum = 0, sq = 0;
    int lo = 255, hi = 0, same = 0, flat = 0;
    for (size_t i = 0; i < a.size(); i++) {
        sum += a[i];
        sq += a[i] * a[i];
        lo = std::min(lo, (int)a[i]);
        hi = std::max(hi, (int)a[i]);
        same += a[i] == b[i];
        flat += retail[i] == 100.0f;
    }
    const double mean = sum / a.size(), sd = std::sqrt(sq / a.size() - mean * mean);
    printf("DC3LookPost dither: retail flat %d/%zu, enhanced mean %.3f sd %.3f range %d..%d, "
           "two runs equal %d/%zu\n", flat, a.size(), mean, sd, lo, hi, same, a.size());
    EXPECT_EQ(flat, (int)a.size()) << "retail: no dither";
    EXPECT_EQ(same, (int)a.size()) << "the dither is the same on every frame";
    EXPECT_NEAR(mean, 100.0, 0.1);
    EXPECT_GT(sd, 0.2);
    EXPECT_GE(lo, 99);
    EXPECT_LE(hi, 101);
    chain.Terminate();
}

// postDofFilter: the depth of field's blur through the tent and the B-spline.
// A bright 4-pixel line on black, at the far plane (blur factor 1), moving by
// sub-pixel steps: the blur's energy and spread are retail's (the taps are
// scaled by DofPass::EnhancedTapScale), and the bilinear read's creases --
// slope breaks every 4 pixels, at the quarter-size texel centres, so the
// profile's second difference piles up on one phase of 4 -- are gone.
TEST_F(DC3LookPostTest, EnhancedDofKeepsTheRadiusWithoutCreases) {
    ASSERT_TRUE(TheDOFProc);
    // The game's size: the Poisson taps' reach in quarter texels grows with
    // the size squared (RetailBlurOffsets), and the compensation with it.
    constexpr int W = 1280, H = 720;
    UIScreen *game = Hmx::Object::New<UIScreen>();
    game->SetName("game_screen", ObjectDir::Main());
    class ProbeUI : public UIManager {
    public:
        void SetCurrent(UIScreen *s) { mCurrentScreen = s; }
    } ui;
    UIManager *savedUI = TheUI;
    TheUI = &ui;
    ui.SetCurrent(game);
    TheDOFProc->Set(nullptr, 2.0f, 0.5f, 1.0f, 0.0f);
    ASSERT_TRUE(TheDOFProc->Enabled());

    wgpu::TextureDescriptor zd{};
    zd.size = {(uint32_t)W, (uint32_t)H, 1};
    zd.format = wgpu::TextureFormat::Depth32Float;
    zd.sampleCount = 4;
    zd.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    wgpu::Texture depth = Gpu().Device().CreateTexture(&zd);
    wgpu::TextureView depthView = depth.CreateView();
    {
        wgpu::RenderPassDepthStencilAttachment za{};
        za.view = depthView;
        za.depthLoadOp = wgpu::LoadOp::Clear;
        za.depthStoreOp = wgpu::StoreOp::Store;
        za.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor rp{};
        rp.label = "TestDofDepthClear";
        rp.depthStencilAttachment = &za;
        wgpu::CommandEncoder enc = Gpu().Device().CreateCommandEncoder();
        enc.BeginRenderPass(&rp).End();
        wgpu::CommandBuffer cmd = enc.Finish();
        Gpu().Queue().Submit(1, &cmd);
    }
    DofPass dof;
    constexpr int kPhases = 16;
    double energy[2] = {}, spread[2] = {}, crease[2] = {};
    for (int k = 0; k < kPhases; k++) {
        // A 12 px bright bar: wide enough that its blurred profile's
        // curvature stands well above the 8-bit output's rounding.
        constexpr double kBar = 12.0;
        const double x0 = 634.0 + 4.0 * k / kPhases;
        std::vector<uint8_t> px((size_t)W * H * 4, 0);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const double cov = std::max(0.0, std::min(x + 1.0, x0 + kBar) - std::max((double)x, x0));
                uint8_t *p = &px[((size_t)y * W + x) * 4];
                p[0] = p[1] = p[2] = (uint8_t)std::lround(255.0 * std::min(1.0, cov));
                p[3] = 255;
            }
        wgpu::Texture scene = Upload(W, H, px);
        wgpu::TextureView sv = scene.CreateView();
        for (int e = 0; e < 2; e++) {
            wgpu::CommandEncoder enc = Gpu().Device().CreateCommandEncoder();
            ASSERT_TRUE(dof.Run(enc, sv, wgpu::TextureFormat::RGBA8Unorm, depthView, 4, W, H, Gpu(), e == 1));
            wgpu::CommandBuffer cmd = enc.Finish();
            Gpu().Queue().Submit(1, &cmd);
            std::vector<uint8_t> out((size_t)W * H * 4);
            ASSERT_TRUE(Gpu().ReadbackTexture(dof.OutputTexture(), W, H, out.data(), out.size()));
            // The profile: the mean of the middle rows' green.
            std::vector<double> prof(W, 0.0);
            for (int y = 344; y < 376; y++)
                for (int x = 0; x < W; x++) prof[x] += out[((size_t)y * W + x) * 4 + 1] / 32.0;
            double s = 0, c = 0, v = 0;
            for (int x = 0; x < W; x++) {
                s += prof[x];
                c += prof[x] * x;
            }
            c /= s;
            for (int x = 0; x < W; x++) v += prof[x] * (x - c) * (x - c);
            energy[e] += s / kPhases;
            // The blur's own radius: the profile's variance less the bar's.
            spread[e] += std::sqrt(std::max(0.0, v / s - kBar * kBar / 12.0)) / kPhases;
            double phase[4] = {}, all = 0;
            for (int x = 1; x + 1 < W; x++) {
                const double d2 = prof[x - 1] - 2.0 * prof[x] + prof[x + 1];
                phase[x & 3] += d2 * d2;
                all += d2 * d2;
            }
            crease[e] += *std::max_element(phase, phase + 4) / all / kPhases;
        }
    }
    printf("DC3LookPost depth of field: energy %.0f -> %.0f (x%.3f), blur radius %.2f -> %.2f px, "
           "creases (share of the profile's curvature on one phase of 4; 0.25 = none) %.3f -> %.3f\n",
           energy[0], energy[1], energy[1] / energy[0], spread[0], spread[1], crease[0], crease[1]);
    EXPECT_NEAR(energy[1] / energy[0], 1.0, 0.03);
    EXPECT_NEAR(spread[1] / spread[0], 1.0, 0.05) << "the blur radius is retail's";
    EXPECT_GT(crease[0], 0.45) << "retail's bilinear read creases (non-vacuous)";
    EXPECT_LT(crease[1], 0.38) << "the B-spline read does not";
    dof.Terminate();
    TheDOFProc->UnSet();
    TheUI = savedUI;
    ui.SetCurrent(nullptr);
    delete game;
}
