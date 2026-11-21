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
#include "gfx/PipelineCount.h"
#include "gfx/PostProcPass.h"
#include "gfx/RB3RetailPost.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "obj/DataFile.h"
#include "rndobj/Cam.h"
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
    //
    // `fine` (optional): the same merged with a third exposure, 16 bloomColor,
    // wherever that is under 250 -- the halo's tails in 1/16 of a level. At
    // bloomColor they sit under one level, where the 8-bit frame write's
    // rounding is the GPU's own choice (an RTX 3090 rounds many values
    // 0.50-0.56 of a level down, which ones depending on the level; a Radeon
    // 890M and an Apple M3 round at 0.5), so a measure taken there reads the
    // GPU, not the filter.
    std::vector<float> RunBloomHdr(RB3RetailPost &chain, const wgpu::Texture &scene, int w, int h,
                                   RetailPostParams p, std::vector<float> *fine = nullptr) {
        std::vector<float> tails;
        if (fine) {
            for (float &c : p.bloomColor) c *= 16.0f;
            tails = RunChain(chain, scene, w, h, p);
            for (float &c : p.bloomColor) c /= 16.0f;
        }
        const auto hi = RunChain(chain, scene, w, h, p);
        for (float &c : p.bloomColor) c /= 16.0f;
        const auto lo = RunChain(chain, scene, w, h, p);
        std::vector<float> g(lo.size());
        for (size_t i = 0; i < g.size(); i++) g[i] = hi[i] < 250.0f ? hi[i] : lo[i] * 16.0f;
        if (fine) {
            fine->resize(g.size());
            for (size_t i = 0; i < g.size(); i++) (*fine)[i] = tails[i] < 250.0f ? tails[i] / 16.0f : g[i];
        }
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

// The centroid of `g` under a round window about (wx, wy) -- weight 1 out to
// r0, a cos^2 taper to 0 at r1 -- relative to (wx, wy). The window moves with
// the dot and is smooth, so a field that only translates with the dot reads 0
// at every sub-pixel phase; the frame's edges (and what the clamped samplers
// pile up there) stay outside it.
void WindowedCentroid(const std::vector<float> &g, int w, int h, double wx, double wy, double r0, double r1,
                      double &ox, double &oy) {
    double s = 0, sx = 0, sy = 0;
    const int y0 = std::max(0, (int)(wy - r1) - 1), y1 = std::min(h, (int)(wy + r1) + 2);
    const int x0 = std::max(0, (int)(wx - r1) - 1), x1 = std::min(w, (int)(wx + r1) + 2);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const double dx = x - wx, dy = y - wy, r = std::sqrt(dx * dx + dy * dy);
            if (r >= r1) continue;
            double k = 1.0;
            if (r > r0) {
                const double c = std::cos(1.5707963267948966 * (r - r0) / (r1 - r0));
                k = c * c;
            }
            const double v = g[(size_t)y * w + x] * k;
            s += v;
            sx += v * dx;
            sy += v * dy;
        }
    ox = s > 0 ? sx / s : 0;
    oy = s > 0 ? sy / s : 0;
}

// The curvature of `g`'s halo tail on set0's lattice (w/4: texel j covers
// pixels 4j..4j+3), added into acc[j mod 16]. The rows within 8 px of the
// dot are summed into one profile; each lattice value is the mean of the two
// pixels about the texel's centre; |second difference| is taken where the
// dot is 40 to 200 px away on either side, past set0's own reach. A set
// upsampled bilinearly is piecewise linear between its texel centres, so its
// curvature lands only at those: set1's (16 px, x = 16k + 8) between lattice
// j = 4k + 1 and 4k + 2, one adjacent pair of phases of 4. A C2 upsample
// spreads it over all four.
void TailCurvature(const std::vector<float> &g, int w, int h, double cx, double cy, double acc[16]) {
    std::vector<double> prof((size_t)w, 0.0);
    for (int y = std::max(0, (int)cy - 8); y <= std::min(h - 1, (int)cy + 8); y++)
        for (int x = 0; x < w; x++) prof[x] += g[(size_t)y * w + x];
    const int n = w / 4;
    std::vector<double> c((size_t)n);
    for (int j = 0; j < n; j++) c[j] = 0.5 * (prof[4 * j + 1] + prof[4 * j + 2]);
    for (int j = 1; j + 1 < n; j++) {
        const double d = std::fabs(4.0 * j + 2.0 - cx);
        if (d < 40.0 || d > 200.0) continue;
        acc[j % 16] += std::fabs(c[j - 1] - 2.0 * c[j] + c[j + 1]);
    }
}
// The largest share of acc's curvature on one adjacent pair of the 4 phases
// of set1's texel (0.5 = spread evenly; 1 = all at set1's texel centres).
double CreasePairShare(const double acc[16]) {
    double ph[4] = {}, total = 0;
    for (int i = 0; i < 16; i++) {
        ph[i % 4] += acc[i];
        total += acc[i];
    }
    double best = 0;
    for (int i = 0; i < 4; i++) best = std::max(best, ph[i] + ph[(i + 1) % 4]);
    return total > 0 ? best / total : 0;
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
        std::vector<double> wcx[2], wcy[2];   // the windowed centroid, relative to the dot
        std::vector<double> px, py;
        double tailCurv[2][16] = {};
        for (int k = 0; k < kPhases; k++) {
            const double cx = 640.25 + 2.0 * (k % 8), cy = 360.25 + 2.0 * (k / 8);
            px.push_back(cx);
            py.push_back(cy);
            wgpu::Texture scene = Upload(W, H, Dot(W, H, cx, cy));
            for (int e = 0; e < 2; e++) {
                RetailPostParams p = BloomTerm(streak, c6);
                // Both sides 16-bit and linear (no dead zone): this test is
                // about the filters (SixteenBitSetsKeepRetailsDeadZone is
                // about the dead zone).
                p.enhPrecision = true;
                p.enhLinear = true;
                p.enhFilter = e == 1;
                std::vector<float> fine;
                m[e].push_back(Measure(RunBloomHdr(chain, scene, W, H, p, &fine), W, H));
                // The window: whole out to 120 px (the bloom term at
                // bloomColor is under a level by ~100 px), gone at 240,
                // inside the frame at every phase.
                double ox, oy;
                WindowedCentroid(fine, W, H, cx, cy, 120.0, 240.0, ox, oy);
                wcx[e].push_back(ox);
                wcy[e].push_back(oy);
                TailCurvature(fine, W, H, cx, cy, tailCurv[e]);
            }
        }
        double energy[2], cv[2], sx[2], sy[2], wobble[2], peak[2];
        for (int e = 0; e < 2; e++) {
            double es = 0, eq = 0, vx = 0, vy = 0, wx = 0, wy = 0;
            peak[e] = 0;
            for (int k = 0; k < kPhases; k++) {
                es += m[e][k].energy;
                eq += m[e][k].energy * m[e][k].energy;
                wx += m[e][k].energy * (m[e][k].cx - px[k]);
                wy += m[e][k].energy * (m[e][k].cy - py[k]);
                peak[e] = std::max(peak[e], m[e][k].peak);
            }
            energy[e] = es / kPhases;
            cv[e] = std::sqrt(std::max(0.0, eq / kPhases - energy[e] * energy[e])) / energy[e];
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
            // offset taken out (the streak's half-texel steps are retail's):
            // the windowed centroid of the fine exposure. The whole frame's
            // centroid at bloomColor alone read 0.010 px on an RTX 3090 and
            // 0.025 on a Radeon 890M for the same filter -- the tails' 8-bit
            // rounding, not the bloom (the windowed one: 0.005 px on the
            // 3090 and with the 3090 made to round at 0.5 alike).
            double mx = 0, my = 0, w2 = 0;
            for (int k = 0; k < kPhases; k++) {
                mx += wcx[e][k] / kPhases;
                my += wcy[e][k] / kPhases;
            }
            for (int k = 0; k < kPhases; k++) {
                const double dx = wcx[e][k] - mx, dy = wcy[e][k] - my;
                w2 += dx * dx + dy * dy;
            }
            wobble[e] = std::sqrt(w2 / kPhases);
        }
        printf("DC3LookPost %s arm: energy %.0f -> %.0f (x%.3f), energy CV over %d phases %.4f -> %.4f, "
               "extent sx %.2f -> %.2f sy %.2f -> %.2f px, centroid wobble (windowed) %.4f -> %.4f px, "
               "peak %.0f / %.0f\n",
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
        if (!streak) {
            // The plain arm: the shimmer the eye sees is retail's own, a
            // centroid that jumps with the 4x4 box (0.317 px windowed); the
            // enhanced filter takes 90% of it away at least. Measured 0.005
            // px (1.5% of retail) on an RTX 3090 rounding as it does and made
            // to round at 0.5 (as the Radeon 890M and the Apple M3 do), CV
            // 0.0002-0.0003. The tent with
            // its bilinear pairs' weights flattened (0.25 each) reads 0.046
            // px (15% of retail) / CV 0.0017-0.0021 under both roundings.
            // (Coarse sets upsampled bilinearly in the combine read 0.005
            // px: bilinear also moves a centroid linearly; the old
            // whole-frame bound "caught" that only through the tails'
            // rounding, 0.037 px on the 3090. The tail curvature below is
            // what catches it.)
            EXPECT_LT(wobble[1], 0.1 * wobble[0]) << "the tent and the B-spline keep the plain bloom on the dot";
            EXPECT_LT(cv[1], 0.001) << "and its energy steady";
            // The combine's own shape, which neither the centroid nor the
            // rounding sees: the coarse sets upsampled with a B-spline have
            // no creases at set1's texel centres; bilinearly they do.
            const double crease[2] = {CreasePairShare(tailCurv[0]), CreasePairShare(tailCurv[1])};
            printf("DC3LookPost plain arm: halo-tail curvature on set1's texel centres (share on one pair "
                   "of 4 phases; 0.5 = none) %.3f -> %.3f\n",
                   crease[0], crease[1]);
            // Measured on an RTX 3090: retail 0.683, enhanced 0.515, the
            // combine made bilinear (fs_comb_bspline calling bloom_set with
            // bspline false) 0.656. The extent sees that sabotage too, but
            // by 0.5% (sx 45.03 -> 44.81 px), and the wobble not at all
            // (0.0047 -> 0.0050 px).
            EXPECT_GT(crease[0], 0.62) << "retail's bilinear sets crease (non-vacuous)";
            EXPECT_LT(crease[1], 0.58) << "the B-spline combine does not";
        }
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

// postPrecision keeps retail's dead zone. Retail's 8-bit sets round every
// pass to the nearest level, so a dim surface's own glow (colour x mask under
// half a level) and the far tails of a halo are gone; linear 16-bit sets
// bloom them (dci's dark floor rose 1.2 levels on real frames). The shipped
// sets keep it as a knee; enhLinear (tests only) shows what it removes.
// Plain arm, bloom term mean: a uniform dim field (colour 40, mask 3: 0.47
// levels into set0) and a sparse field of 2x2 dots. The mean is taken 100 px
// in from the frame's edges: the dots run onto the border rows, where the
// clamped samplers fold them back on themselves and retail's rounding makes
// a band across the frame (3 levels deep on an RTX 3090, 6 where a GPU rounds
// at 0.5; down to row 47 in retail, 85 in enhanced). Over the whole frame the
// dim dots' mean was that band and nothing else (0.000-0.002 levels inside
// it, 0.101 / 0.225 with it), so it measured the edge and the GPU.
TEST_F(DC3LookPostTest, SixteenBitSetsKeepRetailsDeadZone) {
    constexpr int W = 1280, H = 720;
    struct Field { const char *name; int colour, mask; bool dots; };
    const Field fields[] = {{"uniform dim surface", 40, 3, false},
                            {"dots at 255", 255, 255, true},
                            {"dots at 64", 64, 255, true}};
    RB3RetailPost chain;
    chain.Init(Gpu());
    for (const Field &fd : fields) {
        std::vector<uint8_t> px((size_t)W * H * 4, 0);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t *p = &px[((size_t)y * W + x) * 4];
                const bool on = !fd.dots || ((x % 37) < 2 && (y % 29) < 2);
                p[0] = p[1] = p[2] = on ? (uint8_t)fd.colour : 0;
                p[3] = (uint8_t)fd.mask;
            }
        wgpu::Texture scene = Upload(W, H, px);
        double mean[3] = {};   // retail, shipped enhanced, linear
        for (int k = 0; k < 3; k++) {
            RetailPostParams p = BloomTerm(false, 3.0f);
            p.enhPrecision = p.enhFilter = k > 0;
            p.enhLinear = k == 2;
            const auto g = RunChain(chain, scene, W, H, p);
            constexpr int kMargin = 100;
            for (int y = kMargin; y < H - kMargin; y++)
                for (int x = kMargin; x < W - kMargin; x++) mean[k] += g[(size_t)y * W + x];
            mean[k] /= (double)(W - 2 * kMargin) * (H - 2 * kMargin);
        }
        printf("DC3LookPost dead zone, %s: bloom term mean retail %.3f, enhanced %.3f, linear %.3f levels\n",
               fd.name, mean[0], mean[1], mean[2]);
        if (!fd.dots) {
            EXPECT_LT(mean[0], 0.05) << "retail's 8-bit sets drop the dim surface's glow (non-vacuous)";
            EXPECT_GT(mean[2], 2.0) << "linear sets bloom it (the dark-floor lift)";
            EXPECT_LT(mean[1], 0.05) << "the shipped sets drop it as retail does";
        } else {
            // 15% of retail's -- but not of a mean under the 0.05 level this
            // test calls no glow at all (the uniform field above): a ratio
            // of two such means reads rounding, not the look (it read 0.43 on
            // a Radeon 890M from the edge band alone).
            EXPECT_NEAR(mean[1], mean[0], std::max(0.15 * mean[0], 0.05)) << "the dots' glow keeps retail's energy";
        }
    }
    chain.Terminate();
}

// The retail look's bloom sets are 8-bit, as DC3's are (NgPostProc::
// BloomTextureSet), and each pass rounds to the nearest level. How a GPU turns
// a float into an 8-bit level is the GPU's choice (Vulkan allows either
// neighbour): an RTX 3090 rounds many values 0.50-0.56 of a level down, where
// the Radeon 890M (RADV) and Apple M3 results match rounding at 0.5, and
// through the chain's passes that moved retail's bloom by 3% between them. So the passes round in
// the shader (kQuant8) and the GPU's conversion has nothing left to decide.
// A uniform field puts one value in every texel of every set, so the bloom
// term is exact arithmetic: the plain arm's three sets each hold
// round(colour x mask) (the blur's weights sum to 1), the streak arm's two
// that rounded again after each of its three passes (weights summing to S).
// Where the 3090's conversion turns up depends on the level (measured with the
// passes' own rounding off: of 184 fields 0.505-0.545 past a level below 90,
// 103 came out a level low), so the test sweeps every field colour x mask in
// that band, and a band just under the half, where every rounding agrees.
TEST_F(DC3LookPostTest, RetailSetsRoundEveryPassToTheNearestLevel) {
    constexpr int W = 256, H = 144;   // a uniform field: the size only has to hold the sets
    RB3RetailPost chain;
    chain.Init(Gpu());
    // The streak passes' weight sums (RB3RetailPost's StreakTaps: 1/3 at the
    // centre, atten^i / 3 at +-i, atten = 0.5^(4^pass)).
    double S[3];
    for (int pass = 0; pass < 3; pass++) {
        const double atten = std::pow(0.5, std::pow(4.0, pass));
        S[pass] = 1.0 / 3.0 + 2.0 / 3.0 * (atten + atten * atten + atten * atten * atten);
    }
    // A tie would be measuring fp32, not the rounding: every pass's value is
    // kept 0.005 of a level from a half.
    auto awayFromHalf = [](double x) { return std::fabs(x - std::floor(x) - 0.5) > 0.005; };
    const char *names[4] = {"plain", "streak", "plain, enhFilter", "streak, enhFilter"};
    int fields[4] = {}, wrong[4] = {}, upFrom55[4] = {};
    for (int colour = 200; colour <= 255; colour++)
        for (int mask = 10; mask <= 90; mask++) {
            const double v = colour * mask / 255.0;   // levels into set0
            const double frac = v - std::floor(v);
            if (v > 84.5) continue;   // 3 sets x 85 levels is the bloom term's 255
            if (!(frac > 0.505 && frac < 0.545) && !(frac > 0.455 && frac < 0.495)) continue;
            double streak = std::nearbyint(v), late = std::floor(v + 0.45);
            bool streakOk = true;
            for (int pass = 0; pass < 3; pass++) {
                streakOk = streakOk && awayFromHalf(streak * S[pass]);
                streak = std::nearbyint(streak * S[pass]);
                late = std::floor(late * S[pass] + 0.45);
            }
            std::vector<uint8_t> px((size_t)W * H * 4);
            for (size_t i = 0; i < px.size(); i += 4) {
                px[i] = px[i + 1] = px[i + 2] = (uint8_t)colour;
                px[i + 3] = (uint8_t)mask;
            }
            wgpu::Texture scene = Upload(W, H, px);
            // Retail's passes, and enhFilter alone (family [0]: the tent, the
            // box set1 and the B-spline combine on 8-bit sets; on a uniform
            // field each of those keeps the value, so the same arithmetic).
            for (int k = 0; k < 4; k++) {
                const bool streakArm = k & 1;
                if (streakArm && !streakOk) continue;
                RetailPostParams p = BloomTerm(streakArm, 1.0f);
                p.enhFilter = k >= 2;
                const auto g = RunChain(chain, scene, W, H, p);
                const auto mm = std::minmax_element(g.begin(), g.end());
                const double expect = streakArm ? 2.0 * streak : 3.0 * std::nearbyint(v);
                const double alt = streakArm ? 2.0 * late : 3.0 * std::floor(v + 0.45);
                fields[k]++;
                upFrom55[k] += alt != expect;
                if (*mm.first != expect || *mm.second != expect) {
                    if (wrong[k]++ < 3)
                        ADD_FAILURE() << names[k] << " arm, colour " << colour << " x mask " << mask << " = " << v
                                      << " levels: bloom term " << *mm.first << ".." << *mm.second
                                      << ", rounding to nearest gives " << expect;
                }
            }
        }
    for (int k = 0; k < 4; k++) {
        printf("DC3LookPost 8-bit set rounding, %s arm: %d fields, %d off rounding to nearest "
               "(a GPU rounding up only from 0.55 of a level would put %d off)\n",
               names[k], fields[k], wrong[k], upFrom55[k]);
        EXPECT_GT(upFrom55[k], 10) << names[k] << " arm: the fields must tell the roundings apart (non-vacuous)";
        EXPECT_EQ(wrong[k], 0) << names[k] << " arm";
    }
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

// ---- render scale (LookSettings::renderScale; MILO_RENDER_SCALE) ------------

namespace {

// A headless device drawing at 2x its output size (a headless default is 1x).
bool InitScaled(GpuDevice &scaled, int w, int h) {
    GpuDeviceDesc desc;
    desc.headless = true;
    desc.width = w;
    desc.height = h;
    desc.renderScaleExplicit = 2.0f;
    if (!scaled.Init(desc)) return false;
    scaled.AcquireHeadlessFrame();  // the headless surface format
    return scaled.RenderScaled() && scaled.WindowWidth() == 2 * w && scaled.OutputWidth() == w;
}

wgpu::Texture UploadOn(GpuDevice &gpu, int w, int h, const std::vector<uint8_t> &px,
                       wgpu::TextureUsage extra = wgpu::TextureUsage::RenderAttachment) {
    wgpu::TextureDescriptor d{};
    d.size = {(uint32_t)w, (uint32_t)h, 1};
    d.format = wgpu::TextureFormat::RGBA8Unorm;
    d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | extra;
    wgpu::Texture t = gpu.Device().CreateTexture(&d);
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = t;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = (uint32_t)w * 4;
    wgpu::Extent3D ext = {(uint32_t)w, (uint32_t)h, 1};
    gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);
    return t;
}

}  // namespace

namespace {

// postDofFilter's measurement (EnhancedDofKeepsTheRadiusWithoutCreases and its
// render-scale variant): a 12 px bright bar on black at the far plane (blur
// factor 1) moving by sub-pixel steps, through DofPass retail (e = 0) and
// enhanced (e = 1), on `gpu`, drawn at `s` times 1280x720 (the bar, its path
// and the rows read scale with it, the radius is read back in 1x pixels; `s`
// need not be whole: a frame at its output's resolution is 1.5x at 1080p).
// The game's size: the Poisson taps' reach in quarter texels grows with the
// size squared (RetailBlurOffsets), and the compensation with it.
struct DofProfileStats {
    double energy[2] = {}, spread[2] = {}, crease[2] = {};
};
DofProfileStats MeasureDofBar(GpuDevice &gpu, double s) {
    DofProfileStats st;
    const int W = (int)std::lround(1280 * s), H = (int)std::lround(720 * s);
    UIScreen *game = Hmx::Object::New<UIScreen>();
    game->SetName("game_screen", ObjectDir::Main());
    class ProbeUI : public UIManager {
    public:
        void SetCurrent(UIScreen *sc) { mCurrentScreen = sc; }
    } ui;
    UIManager *savedUI = TheUI;
    TheUI = &ui;
    ui.SetCurrent(game);
    // minBlur 1: the composite's blur factor is clamp(min(max(|t|, minBlur),
    // maxBlur)) = 1 whatever the depth constants, so the output is the blur
    // alone. With minBlur 0 the factor at the far plane is 1 - 2 focal / far
    // of whichever camera is current (DofPass::Run reads RndCam::Current()):
    // 0.998 with none (the defaults, far 1000), 0.9996 under Rnd's
    // [default cam] (far 5000) that DC3RetailPost* leaves selected -- a
    // sliver of the sharp bar whose two edges sit on one phase of 4 at 3x,
    // which the crease share read, so the result depended on test order.
    TheDOFProc->Set(nullptr, 2.0f, 0.5f, 1.0f, 1.0f);
    EXPECT_TRUE(TheDOFProc->Enabled());

    wgpu::TextureDescriptor zd{};
    zd.size = {(uint32_t)W, (uint32_t)H, 1};
    zd.format = wgpu::TextureFormat::Depth32Float;
    zd.sampleCount = 4;
    zd.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    wgpu::Texture depth = gpu.Device().CreateTexture(&zd);
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
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        enc.BeginRenderPass(&rp).End();
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
    }
    DofPass dof;
    constexpr int kPhases = 16;
    for (int k = 0; k < kPhases; k++) {
        // A 12 px bright bar: wide enough that its blurred profile's
        // curvature stands well above the 8-bit output's rounding.
        const double kBar = 12.0 * s;
        const double x0 = (634.0 + 4.0 * k / kPhases) * s;
        std::vector<uint8_t> px((size_t)W * H * 4, 0);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const double cov = std::max(0.0, std::min(x + 1.0, x0 + kBar) - std::max((double)x, x0));
                uint8_t *p = &px[((size_t)y * W + x) * 4];
                p[0] = p[1] = p[2] = (uint8_t)std::lround(255.0 * std::min(1.0, cov));
                p[3] = 255;
            }
        wgpu::Texture scene = UploadOn(gpu, W, H, px);
        wgpu::TextureView sv = scene.CreateView();
        for (int e = 0; e < 2; e++) {
            wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
            const bool ran = dof.Run(enc, sv, wgpu::TextureFormat::RGBA8Unorm, depthView, 4, W, H, gpu, e == 1);
            EXPECT_TRUE(ran);
            if (!ran) break;
            wgpu::CommandBuffer cmd = enc.Finish();
            gpu.Queue().Submit(1, &cmd);
            std::vector<uint8_t> out((size_t)W * H * 4);
            EXPECT_TRUE(gpu.ReadbackTexture(dof.OutputTexture(), W, H, out.data(), out.size()));
            // The profile: the mean of the middle rows' green.
            std::vector<double> prof(W, 0.0);
            const int rows = (int)std::lround(32 * s), y0 = (int)std::lround(344 * s);
            for (int y = y0; y < y0 + rows; y++)
                for (int x = 0; x < W; x++) prof[x] += out[((size_t)y * W + x) * 4 + 1] / (double)rows;
            double sum = 0, c = 0, v = 0;
            for (int x = 0; x < W; x++) {
                sum += prof[x];
                c += prof[x] * x;
            }
            c /= sum;
            for (int x = 0; x < W; x++) v += prof[x] * (x - c) * (x - c);
            st.energy[e] += sum / s / kPhases;
            // The blur's own radius: the profile's variance less the bar's.
            st.spread[e] += std::sqrt(std::max(0.0, v / sum - kBar * kBar / 12.0)) / s / kPhases;
            double phase[4] = {}, all = 0;
            for (int x = 1; x + 1 < W; x++) {
                const double d2 = prof[x - 1] - 2.0 * prof[x] + prof[x + 1];
                phase[x & 3] += d2 * d2;  // the quarter targets' texel: 4 drawn px
                all += d2 * d2;
            }
            st.crease[e] += *std::max_element(phase, phase + 4) / all / kPhases;
        }
    }
    dof.Terminate();
    TheDOFProc->UnSet();
    TheUI = savedUI;
    ui.SetCurrent(nullptr);
    delete game;
    return st;
}

}  // namespace

// postDofFilter: the depth of field's blur through the tent and the B-spline.
// A bright 4-pixel line on black, at the far plane (blur factor 1), moving by
// sub-pixel steps: the blur's energy and spread are retail's (the taps are
// scaled by DofPass::EnhancedTapScale), and the bilinear read's creases --
// slope breaks every 4 pixels, at the quarter-size texel centres, so the
// profile's second difference piles up on one phase of 4 -- are gone.
TEST_F(DC3LookPostTest, EnhancedDofKeepsTheRadiusWithoutCreases) {
    ASSERT_TRUE(TheDOFProc);
    // No camera current here, whatever an earlier test left (restored at the
    // end); a second measurement under a camera follows.
    RndCam *prev = RndCam::Current();
    RndCam::ClearCurrent();
    const DofProfileStats st = MeasureDofBar(Gpu(), 1);
    printf("DC3LookPost depth of field: energy %.0f -> %.0f (x%.3f), blur radius %.2f -> %.2f px, "
           "creases (share of the profile's curvature on one phase of 4; 0.25 = none) %.3f -> %.3f\n",
           st.energy[0], st.energy[1], st.energy[1] / st.energy[0], st.spread[0], st.spread[1],
           st.crease[0], st.crease[1]);
    EXPECT_NEAR(st.energy[1] / st.energy[0], 1.0, 0.03);
    // Uncompensated taps read +4% (the tent and the B-spline add their own
    // variance); 2% separates the two.
    EXPECT_NEAR(st.spread[1] / st.spread[0], 1.0, 0.02) << "the blur radius is retail's";
    EXPECT_GT(st.crease[0], 0.45) << "retail's bilinear read creases (non-vacuous)";
    EXPECT_LT(st.crease[1], 0.38) << "the B-spline read does not";

    // The measurement is the blur alone, so it does not depend on which
    // camera an earlier test left current: the same under a camera with
    // Rnd's [default cam] planes (near 1, far 5000) as under none.
    // Sabotage: MeasureDofBar's minBlur back to 0 -- energy, radius and
    // creases all move with the far plane (this test fails in any order).
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 5000.0f, 0.6024178f, 1.0f);
    cam->Select();
    const DofProfileStats other = MeasureDofBar(Gpu(), 1);
    if (prev)
        prev->Select();
    else
        RndCam::ClearCurrent();
    delete cam;
    for (int e = 0; e < 2; e++) {
        EXPECT_EQ(other.energy[e], st.energy[e]) << (e ? "enhanced" : "retail") << ": energy depends on the camera";
        EXPECT_EQ(other.spread[e], st.spread[e]) << (e ? "enhanced" : "retail") << ": radius depends on the camera";
        EXPECT_EQ(other.crease[e], st.crease[e]) << (e ? "enhanced" : "retail") << ": creases depend on the camera";
    }
}

// The same at render scale 2 (np-gfx's SceneBlurOffsets: the taps keep the
// 1x frame's span, so at 2x they reach twice as many quarter texels). The
// tent and the B-spline add a fixed spread in the quarter targets' OWN
// texels, a quarter as much of the blur at 2x, so the compensation measures
// the taps' spread in those texels (DofPass::Run, texels = qw): retail's
// radius, as at 1x. Sabotage: the spread measured in the 1x quarter's texels
// (the 2x taps compensated as the 1x ones are) -- the radius reads ~3% short,
// red.
TEST_F(DC3LookPostTest, RenderScaledDofKeepsTheEnhancedRadius) {
    ASSERT_TRUE(TheDOFProc);
    GpuDevice scaled;
    if (!InitScaled(scaled, 1280, 720)) GTEST_SKIP() << "no second headless device at 2x";
    const DofProfileStats st = MeasureDofBar(scaled, 2);
    printf("DC3LookPost depth of field at render scale 2: energy %.0f -> %.0f (x%.3f), blur radius "
           "%.2f -> %.2f px (1x pixels), creases %.3f -> %.3f\n",
           st.energy[0], st.energy[1], st.energy[1] / st.energy[0], st.spread[0], st.spread[1],
           st.crease[0], st.crease[1]);
    EXPECT_NEAR(st.energy[1] / st.energy[0], 1.0, 0.03);
    EXPECT_NEAR(st.spread[1] / st.spread[0], 1.0, 0.015) << "the 2x blur radius is retail's";
    EXPECT_GT(st.spread[0], 3.0) << "the blur reached the profile (non-vacuous)";
    scaled.Shutdown();
}

// The same at the non-whole scales of a frame drawn at its output's own
// resolution (np-outres, GpuDeviceDesc::outputFollowsTarget): a 1920x1080
// output against the 1280x720 logical frame is 1.5x at render scale 1 and 3x
// at render scale 2 (3840x2160 drawn). The taps keep the logical frame's span
// (SceneBlurOffsets with RenderScaleFor = drawn / logical) and the tent and
// B-spline compensation is measured in the drawn quarter's own texels, so
// the enhanced radius is retail's at either. Sabotage: the compensation's
// spread measured in the logical quarter's texels -- red at both.
TEST_F(DC3LookPostTest, FollowingOutputDofKeepsTheEnhancedRadius) {
    ASSERT_TRUE(TheDOFProc);
    for (int asked = 0; asked < 2; asked++) {
        GpuDevice d;
        GpuDeviceDesc desc;
        desc.headless = true;
        desc.width = 1920;
        desc.height = 1080;
        desc.outputFollowsTarget = true;
        desc.logicalWidth = 1280;
        desc.logicalHeight = 720;
        desc.renderScaleExplicit = asked ? 2.0f : 0.0f;
        if (!d.Init(desc)) GTEST_SKIP() << "no second headless device";
        const double s = asked ? 3.0 : 1.5;
        ASSERT_FLOAT_EQ(d.RenderScaleFor(d.WindowWidth()), (float)s);
        ASSERT_EQ(d.WindowWidth(), (int)std::lround(1280 * s));
        const DofProfileStats st = MeasureDofBar(d, s);
        printf("DC3LookPost depth of field at a 1920x1080 output, %.1fx the logical frame: energy "
               "%.0f -> %.0f (x%.3f), blur radius %.2f -> %.2f px (1x pixels), creases %.3f -> %.3f\n",
               s, st.energy[0], st.energy[1], st.energy[1] / st.energy[0], st.spread[0], st.spread[1],
               st.crease[0], st.crease[1]);
        EXPECT_NEAR(st.energy[1] / st.energy[0], 1.0, 0.03) << s << "x";
        EXPECT_NEAR(st.spread[1] / st.spread[0], 1.0, 0.015) << s << "x: the blur radius is retail's";
        EXPECT_GT(st.spread[0], 3.0) << "the blur reached the profile (non-vacuous)";
        EXPECT_LT(st.crease[1], 0.38) << s << "x: the B-spline read does not crease";
        // At 1.5x retail's bilinear read creases (0.49); at 3x it barely
        // does (0.31 against the B-spline's 0.31), so only the 1.5x case
        // can show the bound is not vacuous. A relative check at 3x passed
        // or failed on the third decimal, by test order.
        if (!asked) EXPECT_GT(st.crease[0], 0.45) << "retail's bilinear read creases at 1.5x (non-vacuous)";
        d.Shutdown();
    }
}

// The depth of field's constants come from the camera NgDOFProc::Set was
// given, at Set (DOFProc_NG.cpp: scale and bias from that camera's planes),
// not from whichever camera is current when the post pass runs: here another
// camera with other planes is made current between Set and the pass, and the
// Set camera is deleted before the pass (the planes are kept, not a pointer).
// A Set with no camera still reads the current one. Sabotage: the DC3 shape's
// rndshape::DofSetCamPlanes returning false (DofPass back on the current
// camera) -- the constants are the other camera's, red.
TEST_F(DC3LookPostTest, DofTakesThePlanesOfTheCameraSetWasGiven) {
    ASSERT_TRUE(TheDOFProc);
    GpuDevice &gpu = Gpu();
    UIScreen *game = Hmx::Object::New<UIScreen>();
    game->SetName("game_screen", ObjectDir::Main());
    class ProbeUI : public UIManager {
    public:
        void SetCurrent(UIScreen *sc) { mCurrentScreen = sc; }
    } ui;
    UIManager *savedUI = TheUI;
    TheUI = &ui;
    ui.SetCurrent(game);
    RndCam *prev = RndCam::Current();

    constexpr int W = 320, H = 180;
    wgpu::TextureDescriptor zd{};
    zd.size = {(uint32_t)W, (uint32_t)H, 1};
    zd.format = wgpu::TextureFormat::Depth32Float;
    zd.sampleCount = 4;
    zd.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    wgpu::Texture depth = gpu.Device().CreateTexture(&zd);
    wgpu::TextureView depthView = depth.CreateView();
    wgpu::Texture scene = UploadOn(gpu, W, H, std::vector<uint8_t>((size_t)W * H * 4, 128));
    wgpu::TextureView sv = scene.CreateView();
    DofPass dof;
    auto run = [&](float out[4]) {
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        const bool ran = dof.Run(enc, sv, wgpu::TextureFormat::RGBA8Unorm, depthView, 4, W, H, gpu, false);
        EXPECT_TRUE(ran);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        for (int i = 0; i < 4; i++) out[i] = dof.LastCompositeConstants()[i];
    };
    // The shot's camera (a gameplay camera's planes) and another one.
    RndCam *shot = Hmx::Object::New<RndCam>();
    shot->SetFrustum(10.0f, 10000.0f, 0.6024178f, 1.0f);
    RndCam *other = Hmx::Object::New<RndCam>();
    other->SetFrustum(1.0f, 50.0f, 0.6024178f, 1.0f);
    const float focal = 20.0f, blurDepth = 0.5f, maxBlur = 1.0f, minBlur = 0.0f;
    float want[4], wrong[4];
    DofPass::RetailConstants(focal, blurDepth, maxBlur, minBlur, shot->NearPlane(), shot->FarPlane(),
                             shot->ZRange().x, shot->ZRange().y, want);
    DofPass::RetailConstants(focal, blurDepth, maxBlur, minBlur, other->NearPlane(), other->FarPlane(),
                             other->ZRange().x, other->ZRange().y, wrong);
    ASSERT_NE(want[0], wrong[0]) << "the two cameras give different constants (non-vacuous)";

    shot->Select();
    TheDOFProc->Set(shot, focal, blurDepth, maxBlur, minBlur);
    ASSERT_TRUE(TheDOFProc->Enabled());
    other->Select();
    delete shot;
    float got[4];
    run(got);
    printf("DC3LookPost depth of field: Set's camera (near 10, far 10000) range %.4f, offset %.4f; "
           "the current camera (near 1, far 50) would give %.4f, %.4f; the pass read %.4f, %.4f\n",
           want[0], want[1], wrong[0], wrong[1], got[0], got[1]);
    for (int i = 0; i < 4; i++) EXPECT_EQ(got[i], want[i]) << "constant " << i << " is not Set's camera's";

    // No camera given: the current one, as before.
    TheDOFProc->Set(nullptr, focal, blurDepth, maxBlur, minBlur);
    run(got);
    for (int i = 0; i < 4; i++) EXPECT_EQ(got[i], wrong[i]) << "constant " << i << ": a Set with no camera reads the current one";

    dof.Terminate();
    TheDOFProc->UnSet();
    if (prev)
        prev->Select();
    else
        RndCam::ClearCurrent();
    delete other;
    TheUI = savedUI;
    ui.SetCurrent(nullptr);
    delete game;
}

// {rnd native_look post_*}: the per-item tokens turn on their own post field
// alone (the per-item cost A/B in engine doc §37.11 depends on it), and
// post_retail / post_enhanced still set all three. Through Rnd's handler, as
// the HTTP eval reaches it.
TEST_F(DC3LookPostTest, NativeLookPostTokensSetTheirOwnField) {
    milo::ScopedLookOverride keep(milo::LookProfile::Enhanced);
    struct Case {
        const char *token;
        bool precision, bloomFilter, dofFilter;
    } cases[] = {
        {"post_retail", false, false, false},     {"post_precision", true, false, false},
        {"post_bloom_filter", false, true, false}, {"post_dof_filter", false, false, true},
        {"post_enhanced", true, true, true},
    };
    for (const Case &c : cases) {
        char text[96];
        snprintf(text, sizeof(text), "(nil native_look %s)", c.token);
        DataArray *file = DataReadString(text);
        TheRnd.Handle(file->Array(0), true);
        file->Release();
        const milo::LookSettings &s = milo::Look();
        EXPECT_EQ(s.postPrecision, c.precision) << c.token;
        EXPECT_EQ(s.postBloomFilter, c.bloomFilter) << c.token;
        EXPECT_EQ(s.postDofFilter, c.dofFilter) << c.token;
        EXPECT_EQ(s.profile, milo::LookProfile::Enhanced) << c.token << " changed the profile";
    }
}


// The enhanced world grade under a render scale (np-gfx's 2x on a discrete
// GPU): the same frame drawn at 2x and box-filtered down grades like the 1x
// frame, as retail's does (DC3RetailPostGpuTest.
// RenderScaledFrameKeepsTheOutputPixelBloom) -- the enhanced bloom is built
// from the output-sized copy (DC3BloomSourceDownsample), so its tent, combine
// and B-spline keep the 1x frame's extent -- and the enhancement is still on
// at 2x: the 2x frame differs from retail's 2x frame as the 1x frames differ.
// Both arms. Sabotages: the np-gfx merge taking np-gfx's side of
// RunDC3Retail (the rp.enhPrecision / enhFilter lines lost) -- the 2x frame is
// retail's, red; the enhanced bloom reading the 2x scene (RunDC3Bloom given
// sceneView) -- the halo halves, red.
TEST_F(DC3LookPostTest, RenderScaledWorldGradeKeepsTheEnhancedBloom) {
    constexpr int kW = 128, kH = 96;
    GpuDevice scaled;
    if (!InitScaled(scaled, kW, kH)) GTEST_SKIP() << "no second headless device at 2x";
    auto &dev = scaled.Device();
    RndPostProc *pp = Hmx::Object::New<RndPostProc>();
    RndPostProc *prev = RndPostProc::Current();
    pp->Select();
    PostProcPass pass;
    pass.Init(scaled);
    // DC3RetailPostGpuTest's pattern: a dark field outside the bloom mask,
    // bright 4x4 blocks inside it, a 1-px vertical line.
    auto pattern = [](int x, int y, uint8_t *p) {
        const bool block = (x % 32) >= 8 && (x % 32) < 12 && (y % 24) >= 10 && (y % 24) < 14;
        const bool line = x == 70;
        const uint8_t v = block ? 240 : line ? 200 : 20;
        p[0] = v;
        p[1] = line ? 60 : v;
        p[2] = line ? 120 : v;
        p[3] = block ? 255 : 0;
    };
    std::vector<uint8_t> k = {0, 0, 0, 255};
    wgpu::Texture black = UploadOn(scaled, 1, 1, k, wgpu::TextureUsage::None);
    wgpu::TextureView blackView = black.CreateView(), noDepth;
    auto run = [&](int s, milo::LookProfile look) {
        const int w = kW * s, h = kH * s;
        std::vector<uint8_t> px((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) pattern(x / s, y / s, &px[((size_t)y * w + x) * 4]);
        wgpu::Texture scene = UploadOn(scaled, w, h, px);
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)w, (uint32_t)h, 1};
        d.format = scaled.SurfaceFormat();
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        wgpu::Texture frame = dev.CreateTexture(&d);
        wgpu::TextureView sceneView = scene.CreateView(), frameView = frame.CreateView();
        milo::ScopedLookOverride o(look);
        pass.SetWorldGrade(true);
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        pass.Run(enc, sceneView, scene, w, h, noDepth, 1, frameView, blackView, scaled);
        wgpu::CommandBuffer cmd = enc.Finish();
        scaled.Queue().Submit(1, &cmd);
        pass.SetWorldGrade(false);
        std::vector<uint8_t> out((size_t)w * h * 4);
        EXPECT_TRUE(scaled.ReadbackTexture(frame, w, h, out.data(), out.size()));
        std::vector<float> g((size_t)kW * kH * 3);
        for (int y = 0; y < kH; y++)
            for (int x = 0; x < kW; x++)
                for (int c = 0; c < 3; c++) {
                    float sum = 0;
                    for (int j = 0; j < s; j++)
                        for (int i = 0; i < s; i++)
                            sum += out[(((size_t)(y * s + j)) * w + (x * s + i)) * 4 + c];
                    g[((size_t)y * kW + x) * 3 + c] = sum / (s * s);
                }
        return g;
    };
    auto field = [](int x) { return (x % 32) >= 14 && (x % 32) < 20 && (x < 60 || x > 80); };
    for (int streak = 0; streak < 2; streak++) {
        pp->SetProperty("bloom_streak", DataNode(streak));
        pp->SetProperty("bloom_streak_attenuation", DataNode(0.9f));
        pp->SetProperty("bloom_intensity", DataNode(1.0f));
        const auto e1 = run(1, milo::LookProfile::Enhanced), e2 = run(2, milo::LookProfile::Enhanced);
        const auto r1 = run(1, milo::LookProfile::Retail), r2 = run(2, milo::LookProfile::Retail);
        double halo1 = 0, halo2 = 0, worst = 0, sum = 0, look1 = 0, look2 = 0;
        for (int y = 0; y < kH; y++)
            for (int x = 0; x < kW; x++)
                for (int c = 0; c < 3; c++) {
                    const size_t i = ((size_t)y * kW + x) * 3 + c;
                    const double d = std::fabs(e1[i] - e2[i]);
                    worst = std::max(worst, d);
                    sum += d;
                    look1 += std::fabs(e1[i] - r1[i]);
                    look2 += std::fabs(e2[i] - r2[i]);
                    if (field(x) && c == 1) {
                        halo1 += e1[i] - 20;
                        halo2 += e2[i] - 20;
                    }
                }
        const double n = (double)kW * kH * 3;
        printf("DC3LookPost render scale 2, %s arm: enhanced mean |1x - 2x| %.3f, worst %.1f; halo 1x "
               "%.1f 2x %.1f; mean |enhanced - retail| 1x %.3f 2x %.3f\n",
               streak ? "streak" : "plain", sum / n, worst, halo1, halo2, look1 / n, look2 / n);
        EXPECT_GT(halo1, 50.0) << "no bloom reached the field; the comparison proves nothing";
        EXPECT_NEAR(halo2, halo1, 0.1 * halo1) << "the 2x enhanced bloom's spread is not the 1x one's";
        EXPECT_LT(sum / n, 1.0) << "the 2x enhanced frame does not grade like the 1x one";
        EXPECT_LT(worst, 12.0);
        EXPECT_GT(look1 / n, 0.05) << "the enhanced look changes the 1x frame (non-vacuous)";
        EXPECT_GT(look2, 0.5 * look1) << "the enhancement is off (or much weaker) at 2x";
    }
    pass.Terminate();
    if (prev) prev->Select(); else pp->Unselect();
    delete pp;
    scaled.Shutdown();
}

// The box filter's dither (PresentScale::SetDither, GpuDevice::
// SetPresentDither), under a render scale: every output pixel whose 2x2 is
// one 8-bit level -- a flat fill, a glyph's ink -- comes out as that level,
// dithered or not; a 2x2 whose mean falls between two levels comes out, over
// each 4x4 output block, at exactly that mean (without the dither: the
// nearest level, a quarter of a level off); the pattern is fixed (two
// readbacks agree) and keyed on the output pixel, so a letterboxed present
// and the readback write the same pixels. Sabotages: SetPresentDither not
// reaching the readback's PresentScale -- the means are a quarter level off,
// red; the threshold keyed on the target position (in.pos) -- the
// letterboxed present differs, red; a TPDF dither (+-1 level) -- flat fills
// change, red.
namespace {
// RenderScaledPresentDitherKeepsLevelsAndFractions' measurement on `scaled`, a
// headless device drawing 2x its kW x kH output.
void CheckPresentDither(GpuDevice &scaled, const int kW, const int kH, const char *what) {
    const int W2 = 2 * kW, H2 = 2 * kH;
    // Left half: flat 2x2s, a different level per output pixel. Right half: n
    // of each 2x2's four texels one level up (n = 1, 2, 3 by band of 16 output
    // rows; 0 below), the base level by 4x4 output block.
    auto level = [](int ox, int oy) { return (ox * 7 + oy * 13) % 256; };
    auto base = [](int ox, int oy) { return 20 + 9 * ((ox / 4) % 8) + 3 * ((oy / 4) % 4); };
    auto band = [](int oy) { return (oy / 16) % 4; };
    std::vector<uint8_t> px((size_t)W2 * H2 * 4);
    for (int y = 0; y < H2; y++)
        for (int x = 0; x < W2; x++) {
            const int ox = x / 2, oy = y / 2, sub = (y & 1) * 2 + (x & 1);
            int v;
            if (ox < kW / 2) v = level(ox, oy);
            else v = base(ox, oy) + (sub < band(oy) ? 1 : 0);
            uint8_t *p = &px[((size_t)y * W2 + x) * 4];
            p[0] = p[1] = p[2] = (uint8_t)v;
            p[3] = 255;
        }
    wgpu::Texture pattern = UploadOn(scaled, W2, H2, px);
    // Into the headless frame 1:1 (an integer-scale present samples nearest).
    PresentScale copy;
    {
        wgpu::CommandEncoder enc = scaled.Device().CreateCommandEncoder();
        copy.Encode(scaled.Device(), enc, pattern, scaled.AcquireHeadlessFrame(),
                    wgpu::TextureFormat::RGBA8Unorm, W2, H2);
        wgpu::CommandBuffer cmd = enc.Finish();
        scaled.Queue().Submit(1, &cmd);
    }
    auto readback = [&](bool dither) {
        scaled.SetPresentDither(dither);
        EXPECT_EQ(scaled.PresentDither(), dither);
        std::vector<uint8_t> out((size_t)kW * kH * 4);
        EXPECT_TRUE(scaled.ReadbackHeadlessFrame(out.data(), out.size()));
        return out;
    };
    const auto plain = readback(false), dith = readback(true), again = readback(true);
    size_t flatBad[2] = {}, outOfPair = 0, moved = 0;
    double meanErr[2] = {}, meanWorst[2] = {};
    int blocks = 0;
    for (int oy = 0; oy < kH; oy++)
        for (int ox = 0; ox < kW / 2; ox++) {
            const size_t i = ((size_t)oy * kW + ox) * 4;
            for (int c = 0; c < 3; c++) {
                flatBad[0] += plain[i + c] != level(ox, oy);
                flatBad[1] += dith[i + c] != level(ox, oy);
            }
        }
    for (size_t i = 0; i < dith.size(); i++) moved += dith[i] != again[i];
    for (int by = 0; by < kH; by += 4)
        for (int bx = kW / 2; bx < kW; bx += 4) {
            const double want = base(bx, by) + band(by) / 4.0;
            for (int e = 0; e < 2; e++) {
                const auto &img = e ? dith : plain;
                double s = 0;
                for (int y = by; y < by + 4; y++)
                    for (int x = bx; x < bx + 4; x++) {
                        const int v = img[((size_t)y * kW + x) * 4 + 1];
                        s += v;
                        if (e && (v < base(bx, by) || v > base(bx, by) + 1)) outOfPair++;
                    }
                const double err = std::fabs(s / 16.0 - want);
                meanErr[e] += err;
                meanWorst[e] = std::max(meanWorst[e], err);
            }
            blocks++;
        }
    // The present: PresentScale as GpuDevice's present runs it (box filter,
    // dither), letterboxed 3 px into a wider target (not a multiple of the
    // pattern's 4, so a threshold keyed on the target position would show).
    PresentScale present;
    present.SetBoxFilter(true);
    present.SetDither(true);
    std::vector<uint8_t> shown;
    {
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)kW + 6, (uint32_t)kH, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        wgpu::Texture target = scaled.Device().CreateTexture(&d);
        wgpu::Texture frameCopy = UploadOn(scaled, W2, H2, px);
        wgpu::CommandEncoder enc = scaled.Device().CreateCommandEncoder();
        const PresentRect r = present.Encode(scaled.Device(), enc, frameCopy, target.CreateView(),
                                             wgpu::TextureFormat::RGBA8Unorm, kW + 6, kH);
        wgpu::CommandBuffer cmd = enc.Finish();
        scaled.Queue().Submit(1, &cmd);
        EXPECT_EQ(r.x, 3);
        EXPECT_EQ(r.w, kW);
        shown.resize((size_t)(kW + 6) * kH * 4);
        EXPECT_TRUE(scaled.ReadbackTexture(target, kW + 6, kH, shown.data(), shown.size()));
    }
    size_t presentDiff = 0;
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++)
            for (int c = 0; c < 3; c++)
                presentDiff += shown[((size_t)y * (kW + 6) + x + 3) * 4 + c] != dith[((size_t)y * kW + x) * 4 + c];
    printf("DC3LookPost present dither, %s: flat 2x2s changed plain %zu dithered %zu; 4x4 block mean "
           "error (levels) plain %.3f (worst %.3f) dithered %.3f (worst %.3f) over %d blocks; outside "
           "the level pair %zu; frame to frame %zu; letterboxed present vs readback %zu\n",
           what, flatBad[0], flatBad[1], meanErr[0] / blocks, meanWorst[0], meanErr[1] / blocks, meanWorst[1],
           blocks, outOfPair, moved, presentDiff);
    EXPECT_EQ(flatBad[0], 0u);
    EXPECT_EQ(flatBad[1], 0u) << "a flat fill (an exact 8-bit level) changed under the dither";
    EXPECT_GT(meanWorst[0], 0.2) << "undithered the fractions round away (non-vacuous)";
    EXPECT_LT(meanWorst[1], 1e-6) << "the dithered 4x4 blocks keep the 2x2 means exactly";
    EXPECT_EQ(outOfPair, 0u) << "the dither moves a pixel by less than one level";
    EXPECT_EQ(moved, 0u) << "the pattern is fixed";
    EXPECT_EQ(presentDiff, 0u) << "the present and the readback dither alike";
    scaled.SetPresentDither(false);
}
}  // namespace

TEST_F(DC3LookPostTest, RenderScaledPresentDitherKeepsLevelsAndFractions) {
    constexpr int kW = 128, kH = 64;
    GpuDevice scaled;
    if (!InitScaled(scaled, kW, kH)) GTEST_SKIP() << "no second headless device at 2x";
    if (scaled.SurfaceFormat() != wgpu::TextureFormat::RGBA8Unorm)
        GTEST_SKIP() << "the headless frame is not RGBA8Unorm";
    CheckPresentDither(scaled, kW, kH, "2x of 128x64");
    scaled.Shutdown();
}

// The same at a 1080p output (np-outres: the frame at its output's own
// resolution, 1920x1080 against the 1280x720 logical frame, drawn 3840x2160
// at render scale 2): the box filter's dither is keyed on the output pixel,
// so it keeps flat fills and the 2x2 means at that size too.
TEST_F(DC3LookPostTest, FollowingOutputPresentDitherKeepsLevelsAndFractions) {
    GpuDevice d;
    GpuDeviceDesc desc;
    desc.headless = true;
    desc.width = 1920;
    desc.height = 1080;
    desc.outputFollowsTarget = true;
    desc.logicalWidth = 1280;
    desc.logicalHeight = 720;
    desc.renderScaleExplicit = 2.0f;
    if (!d.Init(desc)) GTEST_SKIP() << "no second headless device";
    d.AcquireHeadlessFrame();
    ASSERT_EQ(d.OutputWidth(), 1920);
    ASSERT_EQ(d.WindowWidth(), 3840);
    if (d.SurfaceFormat() != wgpu::TextureFormat::RGBA8Unorm)
        GTEST_SKIP() << "the headless frame is not RGBA8Unorm";
    CheckPresentDither(d, 1920, 1080, "1080p output drawn 3840x2160");
    d.Shutdown();
}

// WgpuRnd asks for the dither each frame from the look: on with
// LookSettings::postPrecision (DC3_LOOK=enhanced), off under retail and with
// only another post field on. Sabotage: the SetPresentDither call removed
// from WgpuRnd::BeginDrawing -- red.
TEST_F(DC3LookPostTest, PresentDitherFollowsPostPrecision) {
    GpuDevice &gpu = Gpu();
    auto frameWith = [&](bool precision, bool bloomFilter) {
        milo::ScopedLookOverride o(milo::LookProfile::Enhanced);
        milo::SetLookPostFieldsForInspection(precision, bloomFilter, false);
        TheRnd.BeginDrawing();
        TheRnd.EndDrawing();
        return gpu.PresentDither();
    };
    gpu.SetPresentDither(true);
    EXPECT_FALSE(frameWith(false, true)) << "postBloomFilter alone does not dither the present";
    EXPECT_TRUE(frameWith(true, false)) << "postPrecision dithers the present";
    {
        milo::ScopedLookOverride o(milo::LookProfile::Retail);
        TheRnd.BeginDrawing();
        TheRnd.EndDrawing();
        EXPECT_FALSE(gpu.PresentDither()) << "DC3_LOOK=retail";
    }
    TheRnd.BeginDrawing();
    TheRnd.EndDrawing();
}

// np-look23 (c): LookSettings::postDofDepthWeight. Retail's downsample_4x is
// depth-blind, so an in-focus figure's colour spreads into the blurred
// background beside it: the bright fringe round every figure in a close-up
// (Xenia and the hardware show it; item23/c). With the enhanced look's
// depth-weighted blur the background beside the figure is background only.
// A bright half (in focus) against a dark half (far plane), through DofPass
// enhanced with and without the weighting. Sabotages (item23/c2/sabotage):
// the half-size factor pre-pass (fs_dw_factor_half) writing 1 -- the fringe
// returns, red; writing 0 -- the far stripes stay sharp, red; the factor read
// one half-size texel toward the background -- red; the composite's division
// removed -- the background goes dark, red; Prewarm skipping the weight's
// pipelines -- red.
TEST_F(DC3LookPostTest, DepthWeightedDofKeepsTheFigureOutOfTheBackgroundBlur) {
    ASSERT_TRUE(TheDOFProc);
    GpuDevice &gpu = Gpu();
    UIScreen *game = Hmx::Object::New<UIScreen>();
    game->SetName("game_screen", ObjectDir::Main());
    class ProbeUI : public UIManager {
    public:
        void SetCurrent(UIScreen *sc) { mCurrentScreen = sc; }
    } ui;
    UIManager *savedUI = TheUI;
    TheUI = &ui;
    ui.SetCurrent(game);
    RndCam *prev = RndCam::Current();
    RndCam::ClearCurrent();
    TheDOFProc->Set(nullptr, 10.0f, 0.5f, 1.0f, 0.0f);   // focal 10, blur depth 0.5, max 1, min 0
    ASSERT_TRUE(TheDOFProc->Enabled());

    constexpr int W = 1280, H = 720, kEdge = W / 2, kStripes = kEdge + 300;
    const uint8_t kFig = 230, kBg = 20, kStripe = 60;
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *p = &px[((size_t)y * W + x) * 4];
            // Far right: 8 px stripes in the far background, which the
            // weighted blur must still blur (the factor pre-pass writing
            // nothing would leave them sharp).
            p[0] = p[1] = p[2] = x < kEdge ? kFig : (x >= kStripes && (x / 8) % 2) ? kStripe : kBg;
            p[3] = 255;
        }
    wgpu::Texture scene = UploadOn(gpu, W, H, px);
    wgpu::TextureView sv = scene.CreateView();
    wgpu::TextureDescriptor zd{};
    zd.size = {(uint32_t)W, (uint32_t)H, 1};
    zd.format = wgpu::TextureFormat::Depth32Float;
    zd.sampleCount = 4;
    zd.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    wgpu::Texture depth = gpu.Device().CreateTexture(&zd);
    wgpu::TextureView depthView = depth.CreateView();
    DofPass dof;
    // Warm as WgpuRnd::PrewarmFixedKeyPipelines does when the look starts
    // with the depth weight on: no Run below may compile a pipeline.
    dof.Prewarm(wgpu::TextureFormat::RGBA8Unorm, gpu, true, true);
    const uint64_t pipesWarm = GpuPipelineCreateCount();
    auto run = [&](bool weighted, std::vector<uint8_t> &out) {
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        const bool ran = dof.Run(enc, sv, wgpu::TextureFormat::RGBA8Unorm, depthView, 4, W, H, gpu, true, weighted);
        EXPECT_TRUE(ran);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        out.assign((size_t)W * H * 4, 0);
        EXPECT_TRUE(gpu.ReadbackTexture(dof.OutputTexture(), W, H, out.data(), out.size()));
    };
    // The focal plane's depth from the composite's own constants (t = 0).
    std::vector<uint8_t> out[2];
    run(false, out[0]);
    const float *c = dof.LastCompositeConstants();
    ASSERT_NE(c[0], 0.0f);
    const float zFocus = -c[1] / c[0];
    ASSERT_GT(zFocus, 0.0f);
    ASSERT_LT(zFocus, 1.0f);
    // Depth: the far plane everywhere, the focal plane under the bright half.
    {
        char wgsl[512];
        snprintf(wgsl, sizeof(wgsl),
                 "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
                 "  let x = f32(i32(i & 1u)) * 4.0 - 1.0;\n"
                 "  let y = f32(i32(i >> 1u)) * 4.0 - 1.0;\n"
                 "  return vec4f(x, y, %.9f, 1.0);\n}\n", zFocus);
        wgpu::ShaderSourceWGSL src;
        src.code = wgsl;
        wgpu::ShaderModuleDescriptor smd{};
        smd.nextInChain = &src;
        wgpu::ShaderModule sm = gpu.Device().CreateShaderModule(&smd);
        wgpu::DepthStencilState ds{};
        ds.format = wgpu::TextureFormat::Depth32Float;
        ds.depthWriteEnabled = wgpu::OptionalBool::True;
        ds.depthCompare = wgpu::CompareFunction::Always;
        wgpu::RenderPipelineDescriptor pd{};
        pd.label = "TestDofFocusDepth";
        pd.vertex.module = sm;
        pd.vertex.entryPoint = "vs";
        pd.depthStencil = &ds;
        pd.multisample.count = 4;
        wgpu::RenderPipeline pipe = gpu.Device().CreateRenderPipeline(&pd);
        wgpu::RenderPassDepthStencilAttachment za{};
        za.view = depthView;
        za.depthLoadOp = wgpu::LoadOp::Clear;
        za.depthStoreOp = wgpu::StoreOp::Store;
        za.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor rp{};
        rp.label = "TestDofDepth";
        rp.depthStencilAttachment = &za;
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
        pass.SetPipeline(pipe);
        pass.SetScissorRect(0, 0, kEdge, H);
        pass.Draw(3);
        pass.End();
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
    }
    run(false, out[0]);
    EXPECT_FALSE(dof.LastRunDepthWeighted());
    run(true, out[1]);
    EXPECT_TRUE(dof.LastRunDepthWeighted());
    EXPECT_EQ(GpuPipelineCreateCount(), pipesWarm) << "Prewarm(enhanced, depthWeighted) warms every DOF pipeline";
    dof.Terminate();
    TheDOFProc->UnSet();
    if (prev) prev->Select(); else RndCam::ClearCurrent();
    TheUI = savedUI;
    ui.SetCurrent(nullptr);
    delete game;

    // Column means of the green over the middle rows.
    double fringe[2], far[2], fig[2], stripes[2];
    for (int e = 0; e < 2; e++) {
        std::vector<double> col(W, 0.0);
        for (int y = 200; y < 520; y++)
            for (int x = 0; x < W; x++) col[x] += out[e][((size_t)y * W + x) * 4 + 1] / 320.0;
        auto mean = [&](int a, int b) { double s = 0; for (int x = a; x < b; x++) s += col[x]; return s / (b - a); };
        fringe[e] = mean(kEdge + 2, kEdge + 12) - kBg;   // the background beside the figure, lifted
        far[e] = mean(kEdge + 120, kEdge + 200);
        fig[e] = mean(kEdge - 60, kEdge - 4);
        // The stripes' contrast: the column means' standard deviation.
        const double m = mean(kStripes + 60, kStripes + 300);
        double v = 0;
        for (int x = kStripes + 60; x < kStripes + 300; x++) v += (col[x] - m) * (col[x] - m);
        stripes[e] = std::sqrt(v / 240.0);
    }
    printf("DC3LookPost depth-weighted DOF: background lift 2-12 px beside the figure %.2f -> %.2f levels; "
           "far background %.2f -> %.2f; figure %.2f -> %.2f\n",
           fringe[0], fringe[1], far[0], far[1], fig[0], fig[1]);
    printf("DC3LookPost depth-weighted DOF: far-background stripes' contrast (sd, levels) %.2f unblurred -> %.2f "
           "-> %.2f\n", (kStripe - kBg) / 2.0, stripes[0], stripes[1]);
    EXPECT_GT(fringe[0], 15.0) << "retail's depth-blind blur lifts the background beside the figure (non-vacuous)";
    // Neither lifted nor darkened: the background beside the figure is the
    // background's own blur (the weighted colour divided back out).
    EXPECT_NEAR(fringe[1], 0.0, 1.5) << "the depth-weighted blur keeps the figure out of the background";
    EXPECT_NEAR(far[1], kBg, 1.0) << "the background far from the figure keeps its level";
    EXPECT_NEAR(far[0], kBg, 1.0);
    EXPECT_NEAR(fig[1], kFig, 1.0) << "the in-focus figure stays sharp";
    // The far background is blurred as much with the weight as without (the
    // half-size factor pre-pass writes the far plane's weight; writing none,
    // the composite would fade back to the sharp scene).
    EXPECT_LT(stripes[0], (kStripe - kBg) / 2.0 * 0.5) << "the unweighted blur blurs the stripes (non-vacuous)";
    EXPECT_NEAR(stripes[1], stripes[0], 1.0) << "the depth-weighted blur blurs the far background as much";
}

// np-look36: LookSettings::postDofNearField / postDofNearSpread. The depth
// weight keeps in-focus colour out of every blurred pixel, which is right
// behind the focus (the test above) and wrong in front of it: a blurred
// object between the camera and an in-focus figure kept a hard cut-out edge
// (item35's crops). A bright in-focus figure (left) beside a dark surface
// NEARER than the focus and out of focus (right): the edge across the
// silhouette must be softer with the near field than without, and no harder
// than the depth-blind (retail) blur's. Mirrored in depth (the dark surface
// BEHIND the focus) the near field changes nothing, to the pixel, so H-151's
// property stands. Warmed with the near field, no switch compiles a pipeline.
// Sabotages (item36/sabotage): dof_near returning 0 -- the near edge stays
// hard, red; dof_near ignoring the sign of t (both sides) -- the mirror moves,
// red; the composite's spread ignoring near.x -- the spread-off row equals the
// spread-on row, red; Prewarm skipping the near pipelines -- red.
TEST_F(DC3LookPostTest, DofNearFieldSoftensANearObjectsEdgeAndLeavesTheFarField) {
    ASSERT_TRUE(TheDOFProc);
    GpuDevice &gpu = Gpu();
    UIScreen *game = Hmx::Object::New<UIScreen>();
    game->SetName("game_screen", ObjectDir::Main());
    class ProbeUI : public UIManager {
    public:
        void SetCurrent(UIScreen *sc) { mCurrentScreen = sc; }
    } ui;
    UIManager *savedUI = TheUI;
    TheUI = &ui;
    ui.SetCurrent(game);
    RndCam *prev = RndCam::Current();
    RndCam::ClearCurrent();
    TheDOFProc->Set(nullptr, 10.0f, 0.5f, 1.0f, 0.0f);
    ASSERT_TRUE(TheDOFProc->Enabled());

    constexpr int W = 1280, H = 720, kEdge = W / 2;
    const uint8_t kFig = 230, kDark = 20;
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *p = &px[((size_t)y * W + x) * 4];
            p[0] = p[1] = p[2] = x < kEdge ? kFig : kDark;
            p[3] = 255;
        }
    wgpu::Texture scene = UploadOn(gpu, W, H, px);
    wgpu::TextureView sv = scene.CreateView();
    wgpu::TextureDescriptor zd{};
    zd.size = {(uint32_t)W, (uint32_t)H, 1};
    zd.format = wgpu::TextureFormat::Depth32Float;
    zd.sampleCount = 4;
    zd.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    wgpu::Texture depth = gpu.Device().CreateTexture(&zd);
    wgpu::TextureView depthView = depth.CreateView();
    DofPass dof;
    dof.Prewarm(wgpu::TextureFormat::RGBA8Unorm, gpu, true, true, true);
    const uint64_t pipesWarm = GpuPipelineCreateCount();
    auto run = [&](bool enhanced, bool weighted, bool nearField, bool spread, std::vector<uint8_t> &out) {
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        EXPECT_TRUE(dof.Run(enc, sv, wgpu::TextureFormat::RGBA8Unorm, depthView, 4, W, H, gpu, enhanced,
                            weighted, nearField, spread));
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        out.assign((size_t)W * H * 4, 0);
        EXPECT_TRUE(gpu.ReadbackTexture(dof.OutputTexture(), W, H, out.data(), out.size()));
    };
    std::vector<uint8_t> tmp;
    run(true, true, false, false, tmp);
    const float *c = dof.LastCompositeConstants();
    ASSERT_NE(c[0], 0.0f);
    const float zFocus = -c[1] / c[0];
    const float zNear = (-1.5f - c[1]) / c[0];   // t = -1.5: in front, fully out of focus
    ASSERT_GT(zNear, 0.0f);
    ASSERT_LT(zNear, zFocus);
    // Depth: zLeft under the figure, zRight under the dark half.
    auto drawDepth = [&](float zLeft, float zRight) {
        wgpu::RenderPassDepthStencilAttachment za{};
        za.view = depthView;
        za.depthLoadOp = wgpu::LoadOp::Clear;
        za.depthStoreOp = wgpu::StoreOp::Store;
        za.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor rp{};
        rp.label = "TestDofNearDepth";
        rp.depthStencilAttachment = &za;
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
        for (int side = 0; side < 2; side++) {
            char wgsl[512];
            snprintf(wgsl, sizeof(wgsl),
                     "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
                     "  let x = f32(i32(i & 1u)) * 4.0 - 1.0;\n"
                     "  let y = f32(i32(i >> 1u)) * 4.0 - 1.0;\n"
                     "  return vec4f(x, y, %.9f, 1.0);\n}\n", side ? zRight : zLeft);
            wgpu::ShaderSourceWGSL src;
            src.code = wgsl;
            wgpu::ShaderModuleDescriptor smd{};
            smd.nextInChain = &src;
            wgpu::ShaderModule sm = gpu.Device().CreateShaderModule(&smd);
            wgpu::DepthStencilState ds{};
            ds.format = wgpu::TextureFormat::Depth32Float;
            ds.depthWriteEnabled = wgpu::OptionalBool::True;
            ds.depthCompare = wgpu::CompareFunction::Always;
            wgpu::RenderPipelineDescriptor pd{};
            pd.label = "TestDofNearDepthPipe";
            pd.vertex.module = sm;
            pd.vertex.entryPoint = "vs";
            pd.depthStencil = &ds;
            pd.multisample.count = 4;
            wgpu::RenderPipeline pipe = gpu.Device().CreateRenderPipeline(&pd);
            pass.SetPipeline(pipe);
            pass.SetScissorRect(side ? kEdge : 0, 0, side ? W - kEdge : kEdge, H);
            pass.Draw(3);
        }
        pass.End();
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
    };
    // 0 retail (depth-blind), 1 weighted, 2 near field, 3 near field + spread.
    std::vector<uint8_t> nearOut[4], farOut[4];
    drawDepth(zFocus, zNear);
    run(false, false, false, false, nearOut[0]);
    run(true, true, false, false, nearOut[1]);
    EXPECT_FALSE(dof.LastRunNearField());
    run(true, true, true, false, nearOut[2]);
    EXPECT_TRUE(dof.LastRunNearField());
    run(true, true, true, true, nearOut[3]);
    run(true, false, true, true, tmp);
    EXPECT_FALSE(dof.LastRunNearField()) << "the near field rides on the depth weight only";
    drawDepth(zFocus, 1.0f);   // mirrored: the dark half behind the focus
    run(true, true, false, false, farOut[1]);
    run(true, true, true, false, farOut[2]);
    run(true, true, true, true, farOut[3]);
    EXPECT_EQ(GpuPipelineCreateCount(), pipesWarm) << "Prewarm(.., nearField) warms every near-field pipeline; "
                                                      "no switch compiles";
    dof.Terminate();
    TheDOFProc->UnSet();
    if (prev) prev->Select(); else RndCam::ClearCurrent();
    TheUI = savedUI;
    ui.SetCurrent(nullptr);
    delete game;

    auto cols = [&](const std::vector<uint8_t> &o) {
        std::vector<double> col(W, 0.0);
        for (int y = 200; y < 520; y++)
            for (int x = 0; x < W; x++) col[x] += o[((size_t)y * W + x) * 4 + 1] / 320.0;
        return col;
    };
    double grad[4], inside[4], outside[4];
    for (int e = 0; e < 4; e++) {
        const std::vector<double> col = cols(nearOut[e]);
        grad[e] = 0.0;
        for (int x = kEdge - 40; x < kEdge + 40; x++) grad[e] = std::max(grad[e], std::fabs(col[x + 1] - col[x]));
        inside[e] = col[kEdge + 2];    // the near object just inside its silhouette
        outside[e] = col[kEdge - 3];   // the figure just outside it
    }
    printf("DC3LookPost DOF near field: largest edge step (levels/px) retail %.1f, weighted %.1f, near %.1f, "
           "near + spread %.1f; near object 2 px in %.1f %.1f %.1f %.1f; figure 3 px out %.1f %.1f %.1f %.1f\n",
           grad[0], grad[1], grad[2], grad[3], inside[0], inside[1], inside[2], inside[3], outside[0], outside[1],
           outside[2], outside[3]);
    EXPECT_GT(grad[1], 150.0) << "the weighted blur keeps the near object's cut-out edge (non-vacuous)";
    EXPECT_LT(grad[2], 0.75 * grad[1]) << "the near field softens the near object's edge";
    EXPECT_LE(grad[2], 1.1 * grad[0]) << "no harder than the depth-blind blur's";
    EXPECT_LT(grad[3], grad[2] - 5.0) << "the spread softens it further";
    EXPECT_GT(inside[2], inside[1] + 40.0) << "the figure shows through the near object's blurred edge";
    EXPECT_LT(outside[3], outside[2] - 10.0) << "the near object's blur spreads over the figure beside it";
    // The far field: the near field changes nothing, to the pixel.
    for (int e = 2; e < 4; e++) {
        size_t diff = 0;
        for (size_t i = 0; i < farOut[1].size(); i++) diff += farOut[e][i] != farOut[1][i];
        EXPECT_EQ(diff, 0u) << "near field (spread " << (e == 3) << ") changed the far field";
    }
    // And the in-focus figure away from the near object is untouched.
    for (int e = 2; e < 4; e++)
        for (int y = 200; y < 520; y += 7)
            for (int x = 0; x < kEdge - 64; x += 5)
                ASSERT_EQ(nearOut[e][((size_t)y * W + x) * 4 + 1], nearOut[1][((size_t)y * W + x) * 4 + 1])
                    << "x " << x << " y " << y;
}

// LookSettings::postDofNearField / Spread follow the look and their tokens
// ({rnd native_look dof_near_on|_off|_rule, dof_near_spread_*}); retail and
// Native take none, and Native refuses the token. Sabotage: the setter
// writing the field for the spread token -- red.
TEST(DC3LookDofNear, FollowsTheLookAndItsSwitch) {
    {
        milo::ScopedLookOverride o(milo::LookProfile::Retail);
        EXPECT_FALSE(milo::Look().postDofNearField) << "retail has no near field";
        EXPECT_FALSE(milo::Look().postDofNearSpread);
        EXPECT_TRUE(milo::SetLookDofNearForInspection(1));
        EXPECT_TRUE(milo::Look().postDofNearField);
        EXPECT_FALSE(milo::Look().postDofNearSpread);
        EXPECT_TRUE(milo::SetLookDofNearForInspection(-1));
        EXPECT_FALSE(milo::Look().postDofNearField);
    }
    {
        milo::ScopedLookOverride o(milo::LookProfile::Native);
        EXPECT_FALSE(milo::Look().postDofNearField) << "Native (rb3, rb3-xenon) has no near field";
        EXPECT_FALSE(milo::Look().postDofNearSpread);
        EXPECT_FALSE(milo::SetLookDofNearForInspection(1));
        EXPECT_FALSE(milo::Look().postDofNearField);
    }
    {
        milo::ScopedLookOverride o(milo::LookProfile::Enhanced);
        const bool field = milo::Look().postDofNearField, spread = milo::Look().postDofNearSpread;
        EXPECT_TRUE(milo::SetLookDofNearForInspection(0, true));
        EXPECT_FALSE(milo::Look().postDofNearSpread);
        EXPECT_EQ(milo::Look().postDofNearField, field) << "the spread token leaves the field";
        EXPECT_TRUE(milo::SetLookDofNearForInspection(!field));
        EXPECT_EQ(milo::Look().postDofNearField, !field);
        EXPECT_TRUE(milo::SetLookDofNearForInspection(-1));
        EXPECT_TRUE(milo::SetLookDofNearForInspection(-1, true));
        EXPECT_EQ(milo::Look().postDofNearField, field);
        EXPECT_EQ(milo::Look().postDofNearSpread, spread);
    }
}

// LookSettings::postDofDepthWeight follows the look and its inspection token
// ({rnd native_look dof_depth_weight_on|off|rule}). Sabotage: the switch
// ignored -- red.
TEST(DC3LookDofDepthWeight, FollowsTheLookAndItsSwitch) {
    {
        milo::ScopedLookOverride o(milo::LookProfile::Retail);
        EXPECT_FALSE(milo::Look().postDofDepthWeight) << "retail keeps the depth-blind blur";
        EXPECT_TRUE(milo::SetLookDofDepthWeightForInspection(1));
        EXPECT_TRUE(milo::Look().postDofDepthWeight);
        EXPECT_TRUE(milo::SetLookDofDepthWeightForInspection(-1));
        EXPECT_FALSE(milo::Look().postDofDepthWeight);
    }
    {
        milo::ScopedLookOverride o(milo::LookProfile::Enhanced);
        EXPECT_TRUE(milo::Look().postDofDepthWeight);
        EXPECT_TRUE(milo::SetLookDofDepthWeightForInspection(0));
        EXPECT_FALSE(milo::Look().postDofDepthWeight);
        EXPECT_TRUE(milo::SetLookDofDepthWeightForInspection(-1));
        EXPECT_TRUE(milo::Look().postDofDepthWeight);
    }
}
