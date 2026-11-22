// PLAN H-18: the frame is fitted into the window with its aspect kept.
//
// FitPresentRect (engine gfx/PresentScale) picks the rectangle a windowed
// frame (the 1280x720 Rnd virtual resolution) is drawn into inside a window
// of any size: full width with bars above and below for a window taller than
// 16:9, full height with bars left and right for a wider one, centred.
// PresentRectIsIntegerScale picks nearest sampling for an exact 1x/2x/...
// scale (a HiDPI 2560x1440 framebuffer) and linear for anything else.
// The windowed end-to-end checks are NativeWindowTest in test_native_exit.cpp.

#include <gtest/gtest.h>

#include "test_helpers.h"

#include "gfx/BindGroupCount.h"
#include "gfx/GpuDevice.h"
#include "gfx/PipelineCount.h"
#include "gfx/PresentScale.h"
#include "obj/DataFile.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Rnd.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace {

PresentRect R(int x, int y, int w, int h) {
    PresentRect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

std::string Str(const PresentRect &r) {
    return std::to_string(r.w) + "x" + std::to_string(r.h) + "+" + std::to_string(r.x) + "+" +
           std::to_string(r.y);
}

#define EXPECT_RECT(actual, expected) EXPECT_EQ(Str(actual), Str(expected))

TEST(PresentScale, SameSizeIsTheWholeSurface) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1280, 720), R(0, 0, 1280, 720));
}

TEST(PresentScale, SameAspectFillsTheSurface) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1920, 1080), R(0, 0, 1920, 1080));
    EXPECT_RECT(FitPresentRect(1280, 720, 640, 360), R(0, 0, 640, 360));
}

// Taller than 16:9 (a 16:10 monitor): full width, bars above and below.
TEST(PresentScale, ATallerSurfaceIsLetterboxed) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1600, 1000), R(0, 50, 1600, 900));
    EXPECT_RECT(FitPresentRect(1280, 720, 1920, 1200), R(0, 60, 1920, 1080));
    EXPECT_RECT(FitPresentRect(1280, 720, 1000, 900), R(0, 168, 1000, 563));
}

// Wider than 16:9 (an ultrawide, a short window): full height, bars left and right.
TEST(PresentScale, AWiderSurfaceIsPillarboxed) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1800, 700), R(278, 0, 1244, 700));
    EXPECT_RECT(FitPresentRect(1280, 720, 3440, 1440), R(440, 0, 2560, 1440));
}

// The rectangle never leaves the surface and the leftover splits evenly (the
// odd pixel to the right/bottom), for every size up to a 4k window.
TEST(PresentScale, TheRectangleIsInsideTheSurfaceAndCentred) {
    for (int w = 1; w <= 3840; w += 37)
        for (int h = 1; h <= 2160; h += 41) {
            PresentRect r = FitPresentRect(1280, 720, w, h);
            ASSERT_GE(r.x, 0) << w << "x" << h;
            ASSERT_GE(r.y, 0) << w << "x" << h;
            ASSERT_LE(r.x + r.w, w) << w << "x" << h;
            ASSERT_LE(r.y + r.h, h) << w << "x" << h;
            ASSERT_TRUE(r.w == w || r.h == h) << "touches neither pair of edges: " << w << "x" << h;
            ASSERT_LE(w - r.w - 2 * r.x, 1) << w << "x" << h;
            ASSERT_LE(h - r.h - 2 * r.y, 1) << w << "x" << h;
            ASSERT_GE(w - r.w - 2 * r.x, 0) << w << "x" << h;
            ASSERT_GE(h - r.h - 2 * r.y, 0) << w << "x" << h;
        }
}

// No stretching: the picture keeps the frame's aspect within a pixel.
TEST(PresentScale, TheAspectIsKept) {
    for (int w = 64; w <= 3840; w += 53)
        for (int h = 64; h <= 2160; h += 47) {
            PresentRect r = FitPresentRect(1280, 720, w, h);
            // |r.w/r.h - 16/9| within one pixel of rounding on the short side.
            long long err = (long long)r.w * 720 - (long long)r.h * 1280;
            if (err < 0) err = -err;
            ASSERT_LE(err, 1280) << w << "x" << h << " -> " << Str(r);
        }
}

TEST(PresentScale, NoSizeIsAnEmptyRectangle) {
    EXPECT_RECT(FitPresentRect(1280, 720, 0, 720), R(0, 0, 0, 0));
    EXPECT_RECT(FitPresentRect(1280, 720, 1280, 0), R(0, 0, 0, 0));
    EXPECT_RECT(FitPresentRect(0, 720, 1280, 720), R(0, 0, 0, 0));
    EXPECT_RECT(FitPresentRect(1280, 720, -5, 720), R(0, 0, 0, 0));
}

// Huge sizes do not overflow (64-bit products).
TEST(PresentScale, LargeSizesDoNotOverflow) {
    EXPECT_RECT(FitPresentRect(16384, 9216, 32768, 18432), R(0, 0, 32768, 18432));
    EXPECT_RECT(FitPresentRect(65536, 36864, 32768, 20000), R(0, 784, 32768, 18432));
}

TEST(PresentScale, WholeNumberScalesAreNearest) {
    EXPECT_TRUE(PresentRectIsIntegerScale(R(0, 0, 1280, 720), 1280, 720));
    EXPECT_TRUE(PresentRectIsIntegerScale(R(0, 0, 2560, 1440), 1280, 720));
    EXPECT_TRUE(PresentRectIsIntegerScale(FitPresentRect(1280, 720, 3840, 2400), 1280, 720));
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 1920, 1080), 1280, 720));
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 640, 360), 1280, 720));
    // Each axis a whole multiple, but not the same one: a stretch, not a scale.
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 2560, 720), 1280, 720));
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 0, 0), 1280, 720));
}

} // namespace

// ---- np-present60 (dc3 item 60): the upscale filter -------------------------
// A frame drawn SMALLER than its present rectangle (a row cap, a thermal step)
// read with a clamped Catmull-Rom cubic (PresentScale::SetUpscaleFilter,
// LookSettings::presentUpscale) instead of one bilinear tap. Real device: the
// engine's own pass into an offscreen RGBA8 target, read back. Sabotages, each
// seen red and reverted, are listed above each test and in the item 60
// evidence folder (archive/np-gfx-2026-11-14/lanes/sharp/item60/README.md).
namespace {

using Filter = PresentScale::UpscaleFilter;

class PresentUpscaleGpu : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady()) GTEST_SKIP() << "no GPU device";
    }
    GpuDevice &Gpu() { return gWgpuRnd->Gpu(); }
    wgpu::Texture Frame(int w, int h, const std::vector<uint8_t> &rgba) {
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)w, (uint32_t)h, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture t = Gpu().Device().CreateTexture(&d);
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = t;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = w * 4;
        wgpu::Extent3D ext = {(uint32_t)w, (uint32_t)h, 1};
        Gpu().Queue().WriteTexture(&dst, rgba.data(), rgba.size(), &layout, &ext);
        return t;
    }
    // `frame` through `ps` into a W x H target; the target's RGBA bytes.
    std::vector<uint8_t> Run(PresentScale &ps, const wgpu::Texture &frame, int W, int H) {
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)W, (uint32_t)H, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        wgpu::Texture t = Gpu().Device().CreateTexture(&d);
        wgpu::Device dev = Gpu().Device();
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        ps.Encode(dev, enc, frame, t.CreateView(), wgpu::TextureFormat::RGBA8Unorm, W, H);
        wgpu::CommandBuffer cmd = enc.Finish();
        Gpu().Queue().Submit(1, &cmd);
        std::vector<uint8_t> out((size_t)W * H * 4);
        EXPECT_TRUE(Gpu().ReadbackTexture(t, W, H, out.data(), out.size()));
        return out;
    }
    std::vector<uint8_t> Run(Filter f, const wgpu::Texture &frame, int W, int H) {
        PresentScale ps;
        ps.SetUpscaleFilter(f);
        return Run(ps, frame, W, H);
    }
};

// A pseudo-random frame (every texel its own level), alpha 255.
std::vector<uint8_t> Noise(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    uint32_t x = 12345;
    for (size_t i = 0; i < px.size(); i++) {
        x = x * 1664525u + 1013904223u;
        px[i] = (i & 3) == 3 ? 255 : (uint8_t)(x >> 24);
    }
    return px;
}

int Differ(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    int n = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        n += a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2];
    return n;
}

// Vertical step edges (40 -> 220 and back, blocks of 7 texels, so the output
// pixels land at every phase of an edge) stretched to W: the edge spread from
// all rising edges, binned at 1/20 output pixel; its 10-90 rise in output
// pixels and the overshoot past either level as a fraction of the step.
struct Edge { double rise, overshoot; };
Edge StepEdge(const std::vector<uint8_t> &out, int W, int fw) {
    const double s = (double)W / fw;
    std::map<int, std::pair<double, int>> bins;
    double over = 0;
    for (int x = 0; x < W; x++) {
        const double v = (out[(size_t)x * 4] - 40.0) / 180.0;  // row 0
        over = std::max(over, std::max(v - 1.0, -v));
        const double fx = (x + 0.5) / s;  // frame position
        const int k = (int)std::lround(fx / 7.0);
        if (k < 1 || k * 7 >= fw - 1 || (k & 1) == 0) continue;  // rising edges only (odd k)
        const double d = (x + 0.5) - k * 7.0 * s;                 // output px from the edge
        if (std::fabs(d) > 4) continue;
        auto &b = bins[(int)std::lround(d * 20)];
        b.first += v;
        b.second++;
    }
    std::vector<std::pair<double, double>> esf;
    for (auto &kv : bins) esf.push_back({kv.first / 20.0, kv.second.first / kv.second.second});
    auto cross = [&](double t) {
        for (size_t i = 0; i + 1 < esf.size(); i++)
            if (esf[i].second < t && esf[i + 1].second >= t)
                return esf[i].first + (t - esf[i].second) / (esf[i + 1].second - esf[i].second) *
                                          (esf[i + 1].first - esf[i].first);
        return (double)NAN;
    };
    return {cross(0.9) - cross(0.1), over};
}

std::vector<uint8_t> Steps(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = &px[((size_t)y * w + x) * 4];
            p[0] = p[1] = p[2] = (x / 7) & 1 ? 220 : 40;
            p[3] = 255;
        }
    return px;
}

}  // namespace

// The setter never called: a 4/3 stretch is today's one bilinear tap to the
// bit (the retail look, rb3, rb3-xenon), and no upscale pipeline exists. The
// reference is fs_copy (SetKeepAlpha), the same one tap with alpha kept: an
// alpha-255 frame gives the same bytes. Sabotage: PresentScale's default
// filter CatmullRom5Clamp -> red (9216 of 9216 pixels differ).
TEST_F(PresentUpscaleGpu, NeverCalledIsTodaysOneTap) {
    const int fw = 96, fh = 54, W = 128, H = 72;
    wgpu::Texture f = Frame(fw, fh, Noise(fw, fh));
    PresentScale def;
    const std::vector<std::string> before = TakeRecentPipelineLabels();
    std::vector<uint8_t> a = Run(def, f, W, H);
    for (const std::string &l : TakeRecentPipelineLabels())
        EXPECT_NE(l, "PresentScaleUpscale") << "no upscale pipeline with the setter never called";
    EXPECT_EQ(def.LastUpscale(), Filter::Bilinear);
    PresentScale copy;
    copy.SetKeepAlpha(true);
    copy.SetUpscaleFilter(Filter::CatmullRom5Clamp);  // the copy never takes it
    EXPECT_EQ(Differ(a, Run(copy, f, W, H)), 0) << "the default present is not today's one tap";
    EXPECT_EQ(copy.LastUpscale(), Filter::Bilinear);
    // And a second default pass over the same frame (the no-change control).
    EXPECT_EQ(Differ(a, Run(Filter::Bilinear, f, W, H)), 0);
}

// On: a step edge stretched by 4/3 and by 1.6 rises at least 5 % faster
// than bilinear's and overshoots at most 0.01 of the step (the clamp). A
// texel-aligned step seen at 4/3 has three output phases, at 1.6 five, so the
// edge spread is the one those phases give: measured on the RTX 3090, 0.955
// against 1.140 output px at 4/3 (0.84) and 1.156 against 1.278 at 1.6 (0.90). The unclamped cubic
// overshoots, so the overshoot check can fail. Sabotages: the clamp removed
// from fs_up_cr5_c -> red (overshoot 0.078); fs_up_cr5_c clamping one
// bilinear tap instead of the cubic -> red (rise 1.140 and 1.278, not
// narrower); the upscale's viewport moved one pixel right -> red (the black
// column: 0.22 below the step).
TEST_F(PresentUpscaleGpu, AStepEdgeRisesFasterWithNoOvershoot) {
    struct Case { int fw, W; } cases[] = {{384, 512}, {320, 512}};  // 4/3 and 1.6
    for (const Case &c : cases) {
        const int fw = c.fw, W = c.W, fh = 8;
        wgpu::Texture f = Frame(fw, fh, Steps(fw, fh));
        const int H = (int)std::lround((double)fh * W / fw);
        const Edge bil = StepEdge(Run(Filter::Bilinear, f, W, H), W, fw);
        const Edge cr = StepEdge(Run(Filter::CatmullRom5Clamp, f, W, H), W, fw);
        const Edge cr9 = StepEdge(Run(Filter::CatmullRom9Clamp, f, W, H), W, fw);
        const Edge raw = StepEdge(Run(Filter::CatmullRom5, f, W, H), W, fw);
        printf("[present60] scale %.3f: rise bilinear %.3f, cr5 clamped %.3f (overshoot %.4f), cr9 clamped %.3f "
               "(%.4f), cr5 unclamped %.3f (%.4f)\n",
               (double)W / fw, bil.rise, cr.rise, cr.overshoot, cr9.rise, cr9.overshoot, raw.rise, raw.overshoot);
        ASSERT_FALSE(std::isnan(bil.rise) || std::isnan(cr.rise));
        EXPECT_LE(cr.rise, 0.95 * bil.rise) << "scale " << (double)W / fw;
        EXPECT_LE(cr.overshoot, 0.01) << "scale " << (double)W / fw;
        EXPECT_LE(cr9.rise, 0.95 * bil.rise);
        EXPECT_LE(cr9.overshoot, 0.01);
        EXPECT_GT(raw.overshoot, 0.02) << "the unclamped cubic rings: the overshoot measure can see it";
        EXPECT_LE(bil.overshoot, 0.01);
    }
}

// Weights sum to one: every 8-bit level as a flat block comes out that exact
// level (glyph interiors, flat fills), and a flat frame's border pixels do
// not bleed black in from outside the frame. Sabotages: the 5-read weights
// left unnormalised (no divide) -> red (the unclamped 5-read form); the
// upscale's viewport moved one pixel right, so a black column comes in at the
// edge -> red (border). A zero-border sampler cannot be made in WebGPU, so
// that sabotage does not exist.
TEST_F(PresentUpscaleGpu, FlatLevelsAreExactAndTheBorderDoesNotBleed) {
    const int fw = 16 * 16, fh = 16 * 16;  // 16x16 blocks of 16 texels: level = block index
    std::vector<uint8_t> px((size_t)fw * fh * 4);
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            uint8_t *p = &px[((size_t)y * fw + x) * 4];
            p[0] = p[1] = p[2] = (uint8_t)((y / 16) * 16 + x / 16);
            p[3] = 255;
        }
    wgpu::Texture f = Frame(fw, fh, px);
    for (Filter flt : {Filter::CatmullRom5Clamp, Filter::CatmullRom9Clamp, Filter::CatmullRom5, Filter::CatmullRom9}) {
        for (int W : {fw * 4 / 3, fw * 8 / 5}) {
            const std::vector<uint8_t> out = Run(flt, f, W, W);
            const double s = (double)W / fw;
            int bad = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++) {
                    const double fx = (x + 0.5) / s, fy = (y + 0.5) / s;
                    const double bx = std::fmod(fx, 16.0), by = std::fmod(fy, 16.0);
                    if (bx < 3 || bx > 13 || by < 3 || by > 13) continue;  // block interiors only
                    const int level = ((int)fy / 16) * 16 + (int)fx / 16;
                    const uint8_t *o = &out[((size_t)y * W + x) * 4];
                    bad += o[0] != level || o[1] != level || o[2] != level;
                }
            EXPECT_EQ(bad, 0) << "filter " << (int)flt << " width " << W;
        }
    }
    std::vector<uint8_t> flat((size_t)fw * fh * 4, 200);
    wgpu::Texture g = Frame(fw, fh, flat);
    const int W = fw * 4 / 3;
    const std::vector<uint8_t> out = Run(Filter::CatmullRom5Clamp, g, W, W);
    int bad = 0;
    for (int i = 0; i < W; i++)
        for (int j : {0, 1, W - 2, W - 1}) {
            bad += out[((size_t)i * W + j) * 4] != 200;
            bad += out[((size_t)j * W + i) * 4] != 200;
        }
    EXPECT_EQ(bad, 0) << "border pixels of a flat 200 frame";
}

// A scale of 1, a downscale (with and without the box filter) and a
// whole-number scale are unchanged with the setter on, and LastUpscale says
// so. Sabotage: Encode's rule taking `rect.w >= fw` and no whole-scale
// exclusion -> red (LastUpscale at 1x and 2x; the cubic at 1x is the frame's
// own texels, so the bytes alone could not see it). The 4/3 control is red
// when fs_up_cr5_c clamps one bilinear tap instead of the cubic.
TEST_F(PresentUpscaleGpu, ScaleOneDownscaleAndWholeScalesAreUnchanged) {
    const int fw = 96, fh = 54;
    wgpu::Texture f = Frame(fw, fh, Noise(fw, fh));
    struct Case { int W, H; bool box; } cases[] = {{96, 54, false}, {64, 36, false}, {64, 36, true}, {192, 108, false}};
    for (const Case &c : cases) {
        PresentScale off, on;
        off.SetBoxFilter(c.box);
        on.SetBoxFilter(c.box);
        on.SetUpscaleFilter(Filter::CatmullRom5Clamp);
        EXPECT_EQ(Differ(Run(off, f, c.W, c.H), Run(on, f, c.W, c.H)), 0) << c.W << "x" << c.H << " box " << c.box;
        EXPECT_EQ(on.LastUpscale(), Filter::Bilinear) << c.W << "x" << c.H;
    }
    PresentScale on;
    on.SetUpscaleFilter(Filter::CatmullRom5Clamp);
    EXPECT_NE(Differ(Run(Filter::Bilinear, f, 128, 72), Run(on, f, 128, 72)), 0) << "the control: 4/3 does change";
    EXPECT_EQ(on.LastUpscale(), Filter::CatmullRom5Clamp);
}

// A steady stretched frame with the filter on creates no pipeline and no
// bind group after its first frame. Sabotage: the upscale pipeline created
// on every Encode -> red.
TEST_F(PresentUpscaleGpu, ASteadyFrameCreatesNothing) {
    const int fw = 96, fh = 54;
    wgpu::Texture f = Frame(fw, fh, Noise(fw, fh));
    PresentScale ps;
    ps.SetUpscaleFilter(Filter::CatmullRom5Clamp);
    Run(ps, f, 128, 72);
    const uint64_t p0 = GpuPipelineCreateCount(), b0 = GpuBindGroupCreateCount();
    for (int i = 0; i < 3; i++) Run(ps, f, 128, 72);
    EXPECT_EQ(GpuPipelineCreateCount(), p0);
    EXPECT_EQ(GpuBindGroupCreateCount(), b0);
}

// The look, by adapter class as the depth of field's edge rule is: retail and
// Native off; enhanced on for a discrete GPU and for anything else (the 890M
// timed it under its limit: np-present60b), off for an Apple GPU (the M3 has
// not timed it); Native refuses; the three
// tokens through Rnd's handler write all three classes' fields; a drawn frame
// hands GpuDevice the choice for the adapter class in effect, so the class
// token reaches the present; GpuDevice's setter picks the clamped 5-read
// cubic. Sabotages, each seen red and reverted (item60/sab/G..K): Enhanced's
// presentUpscale false; Enhanced's presentUpscaleIntegrated true (since
// np-present60b: false, and Enhanced's presentUpscaleApple true); the picker
// reading the discrete field for every class; BeginDrawing passing
// Look().presentUpscale without the class; present_up_off calling the setter
// with 1; the setter not writing the non-discrete class.
class PresentUpscaleLook : public EngineTestFixture {
protected:
    static void Token(const char *t) {
        char text[96];
        snprintf(text, sizeof(text), "(nil native_look %s)", t);
        DataArray *file = DataReadString(text);
        TheRnd.Handle(file->Array(0), true);
        file->Release();
    }
};

TEST_F(PresentUpscaleLook, EnhancedOnAllButAppleAndTheTokens) {
    if (IsSkipped()) return;
    using milo::LookAdapterClass;
    auto fields = [] {
        const milo::LookSettings &s = milo::Look();
        return (int)s.presentUpscale + 2 * (int)s.presentUpscaleApple + 4 * (int)s.presentUpscaleIntegrated;
    };
    auto picked = [](const milo::LookSettings &s) {
        return (int)milo::LookPresentUpscale(s, LookAdapterClass::Discrete) +
               2 * (int)milo::LookPresentUpscale(s, LookAdapterClass::Apple) +
               4 * (int)milo::LookPresentUpscale(s, LookAdapterClass::OtherNonDiscrete);
    };
    {
        milo::ScopedLookOverride retail(milo::LookProfile::Retail);
        EXPECT_EQ(fields(), 0);
        EXPECT_EQ(picked(milo::Look()), 0);
    }
    {
        milo::ScopedLookOverride native(milo::LookProfile::Native);
        EXPECT_EQ(fields(), 0);
        Token("present_up_on");
        EXPECT_EQ(fields(), 0) << "Native refuses the token";
    }
    milo::ScopedLookOverride enhanced(milo::LookProfile::Enhanced);
    EXPECT_EQ(fields(), 5) << "enhanced: a discrete GPU and anything else, not Apple (1 discrete, 2 Apple, 4 anything else)";
    EXPECT_EQ(picked(milo::Look()), 5);
    // The picker picks by class: a local copy with each class's own value.
    for (int bits = 0; bits < 8; bits++) {
        milo::LookSettings copy = milo::Look();
        copy.presentUpscale = bits & 1;
        copy.presentUpscaleApple = bits & 2;
        copy.presentUpscaleIntegrated = bits & 4;
        EXPECT_EQ(picked(copy), bits);
    }
    Token("present_up_off");
    EXPECT_EQ(fields(), 0);
    Token("present_up_on");
    EXPECT_EQ(fields(), 7) << "on: all three classes";
    Token("present_up_off");
    Token("present_up_rule");
    EXPECT_EQ(fields(), 5) << "the rule gives the look's values back";
    if (gWgpuRnd && gWgpuRnd->Gpu().IsReady()) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        const bool was = gpu.PresentUpscale();
        gpu.SetPresentUpscale(true);
        EXPECT_TRUE(gpu.PresentUpscale());
        gpu.SetPresentUpscale(false);
        EXPECT_FALSE(gpu.PresentUpscale());
        // A drawn frame asks for the choice of the adapter class in effect.
        const LookAdapterClass classes[] = {LookAdapterClass::Discrete, LookAdapterClass::Apple,
                                            LookAdapterClass::OtherNonDiscrete};
        auto drawn = [&](LookAdapterClass c, bool before) {
            EXPECT_TRUE(milo::SetLookAdapterClassForInspection(false, c));
            gpu.SetPresentUpscale(before);
            TheRnd.BeginDrawing();
            TheRnd.EndDrawing();
            return gpu.PresentUpscale();
        };
        for (int c = 0; c < 3; c++) {
            EXPECT_EQ(drawn(classes[c], c == 1), c != 1) << "the rule, class " << c;
        }
        Token("present_up_on");
        for (int c = 0; c < 3; c++) EXPECT_TRUE(drawn(classes[c], false)) << "present_up_on, class " << c;
        Token("present_up_off");
        for (int c = 0; c < 3; c++) EXPECT_FALSE(drawn(classes[c], true)) << "present_up_off, class " << c;
        Token("present_up_rule");
        EXPECT_TRUE(milo::SetLookAdapterClassForInspection(true));
        gpu.SetPresentUpscale(was);
    }
}

// For the rig (item 60 item 7): the pass alone, each filter, a 1920x1080
// frame into 2560x1440 and 2536x1426, milliseconds per pass from the CPU
// clock over 100 passes in one submit (best of 5). Prints; asserts only that
// it ran. Read the "[present60] time" lines.
TEST_F(PresentUpscaleGpu, TimeEachFilter) {
    const int fw = 1920, fh = 1080;
    wgpu::Texture f = Frame(fw, fh, Noise(fw, fh));
    for (int W : {2560, 2536}) {
        const int H = W == 2560 ? 1440 : 1426;
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)W, (uint32_t)H, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        wgpu::Texture t = Gpu().Device().CreateTexture(&d);
        wgpu::TextureView v = t.CreateView();
        wgpu::Device dev = Gpu().Device();
        std::vector<uint8_t> px(4 * 4 * 4);
        for (Filter flt : {Filter::Bilinear, Filter::CatmullRom5Clamp, Filter::CatmullRom9Clamp}) {
            PresentScale ps;
            ps.SetUpscaleFilter(flt);
            auto run = [&](int n) {
                wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
                for (int k = 0; k < n; k++) ps.Encode(dev, enc, f, v, wgpu::TextureFormat::RGBA8Unorm, W, H);
                wgpu::CommandBuffer cmd = enc.Finish();
                Gpu().Queue().Submit(1, &cmd);
                Gpu().ReadbackTexture(t, 4, 4, px.data(), px.size());
            };
            run(10);
            double best = 1e9;
            for (int trial = 0; trial < 5; trial++) {
                auto t0 = std::chrono::steady_clock::now();
                run(100);
                best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 100);
            }
            printf("[present60] time %dx%d into %dx%d, %s: %.4f ms a pass\n", fw, fh, W, H,
                   flt == Filter::Bilinear ? "bilinear (1 read)"
                   : flt == Filter::CatmullRom5Clamp ? "cr5 clamped (5 reads + 3 gathers)"
                                                     : "cr9 clamped (9 reads + 3 gathers)",
                   best);
            EXPECT_LT(best, 1000.0);
        }
    }
}
