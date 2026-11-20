// test_canvas2d.cpp -- the engine's batched 2D canvas (gfx/Canvas2D.h), the
// drawing layer of the natively drawn UI (lane np-ui-engine / engine np-ui2d).
//
// Two halves, as the engine splits it:
//   Canvas2DBatch.*  CPU: quad -> instance record, clip stack, batch breaks.
//                    No device; plain TESTs.
//   Canvas2D.*       GPU: draws into a render target through WgpuRnd's pass
//                    and reads the pixels back (EngineTestFixture; skipped
//                    with no device).
//
// Canvas2DGpuTest.RenderedProof draws the lane's fixed proof scene and writes
// it as a PNG: to $MILO_CANVAS2D_PROOF_PNG when set, else into the test's
// scratch directory.
//
// Sabotages tried, 2026-11-20, one edit to the engine at a time, each restored
// (24 edits, every one red, and every test here red under at least one):
//   Canvas2DBatch.cpp: CornerNdc's y sign; max instead of min scale for the
//     radius; default softness scaled; batch break ignoring the clip;
//     Intersect taking min; no cull; clip edges floored instead of rounded;
//     u1 not carried; ClearPending closing the session; SetTarget ignored
//     while open; logical scale fixed at 1; PopClip taking the base clip.
//   Canvas2D.cpp: no texel clamp in the shader (the sub-rect bleeds); each of
//     the four coverage channels reading its neighbour; no ring term; arena
//     offset always 0; no scissor restore after a flush; OnPassEnd doing
//     nothing; Begin() without the in-pass check.
//   Rnd_Wgpu.cpp: DrawRect not flushing the canvas; no canvas prewarm.
// Two of the first round were NOT caught and the tests were changed for them:
// floor-vs-round on a clip edge (ClipRounds... had no edge past .5) and the
// red coverage mode reading green (the glyph's RGB channels were equal; it now
// holds one channel per band and all four modes are drawn).

#include "test_helpers.h"

#include "gfx/Canvas2D.h"
#include "gfx/Canvas2DBatch.h"
#include "gfx/PipelineCount.h"
#include "gfx/Screenshot.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"

#include "math/Geo.h"
#include "obj/Object.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// CPU half
// ---------------------------------------------------------------------------

Canvas2DTarget Target(float vx, float vy, float vw, float vh, int tw, int th) {
    Canvas2DTarget t;
    t.vpX = vx;
    t.vpY = vy;
    t.vpW = vw;
    t.vpH = vh;
    t.targetW = tw;
    t.targetH = th;
    t.baseClip = {0, 0, tw, th};
    return t;
}

Canvas2DQuad Solid(float x, float y, float w, float h, RndTex* tex = nullptr) {
    Canvas2DQuad q;
    q.rect = {x, y, w, h};
    q.tex = tex;
    return q;
}

RndTex* FakeTex(uintptr_t n) { return reinterpret_cast<RndTex*>(n * 0x1000); }

TEST(Canvas2DBatch, LogicalFrameCoversTheViewport) {
    Canvas2DBuilder b;
    b.Begin(1280.0f, 720.0f, Target(0, 0, 1920, 1080, 1920, 1080));
    Canvas2DQuad q = Solid(0, 0, 1280, 720);
    q.softness = 0.0f; // no padding: the corners are the rect's own
    ASSERT_TRUE(b.Quad(q));
    ASSERT_EQ(b.Instances().size(), 1u);
    const Canvas2DInstance& in = b.Instances()[0];
    EXPECT_FLOAT_EQ(in.cx, 960.0f);
    EXPECT_FLOAT_EQ(in.cy, 540.0f);
    EXPECT_FLOAT_EQ(in.hx, 960.0f);
    EXPECT_FLOAT_EQ(in.hy, 540.0f);
    float tl[2], br[2], bl[2];
    Canvas2DBuilder::CornerNdc(in, 0, tl);
    Canvas2DBuilder::CornerNdc(in, 5, br);
    Canvas2DBuilder::CornerNdc(in, 1, bl);
    // Origin top-left, y down: the top-left corner is NDC (-1, +1).
    EXPECT_FLOAT_EQ(tl[0], -1.0f);
    EXPECT_FLOAT_EQ(tl[1], 1.0f);
    EXPECT_FLOAT_EQ(br[0], 1.0f);
    EXPECT_FLOAT_EQ(br[1], -1.0f);
    EXPECT_FLOAT_EQ(bl[0], -1.0f);
    EXPECT_FLOAT_EQ(bl[1], -1.0f);
}

TEST(Canvas2DBatch, QuadIsPlacedInViewportPixels) {
    Canvas2DBuilder b;
    // A 640x360 viewport at (100, 50) of a 1000x500 target; logical 1280x720.
    b.Begin(1280.0f, 720.0f, Target(100, 50, 640, 360, 1000, 500));
    EXPECT_FLOAT_EQ(b.ScaleX(), 0.5f);
    EXPECT_FLOAT_EQ(b.ScaleY(), 0.5f);
    Canvas2DQuad q = Solid(200, 100, 400, 200);
    q.radius = 24.0f;
    q.border = 4.0f;
    q.softness = 8.0f;
    ASSERT_TRUE(b.Quad(q));
    const Canvas2DInstance& in = b.Instances()[0];
    // Viewport-relative pixels (the viewport's own offset is the rasteriser's).
    EXPECT_FLOAT_EQ(in.cx, 200.0f);
    EXPECT_FLOAT_EQ(in.cy, 100.0f);
    EXPECT_FLOAT_EQ(in.hx, 100.0f);
    EXPECT_FLOAT_EQ(in.hy, 50.0f);
    EXPECT_FLOAT_EQ(in.radius, 12.0f);
    EXPECT_FLOAT_EQ(in.border, 2.0f);
    EXPECT_FLOAT_EQ(in.softness, 4.0f);
    EXPECT_FLOAT_EQ(in.pad, 2.0f) << "half the falloff lies outside the rect";
    EXPECT_FLOAT_EQ(in.ndcX, 2.0f / 640.0f);
    EXPECT_FLOAT_EQ(in.ndcY, 2.0f / 360.0f);
    // The padded top-left corner: (100 - 2, 50 - 2) px of the viewport.
    float tl[2];
    Canvas2DBuilder::CornerNdc(in, 0, tl);
    EXPECT_FLOAT_EQ(tl[0], 98.0f * 2.0f / 640.0f - 1.0f);
    EXPECT_FLOAT_EQ(tl[1], 1.0f - 48.0f * 2.0f / 360.0f);
}

TEST(Canvas2DBatch, DefaultSoftnessIsOneDevicePixelAtAnyScale) {
    for (float scale : {0.5f, 1.0f, 3.0f}) {
        Canvas2DBuilder b;
        b.Begin(100.0f, 100.0f, Target(0, 0, 100 * scale, 100 * scale, 400, 400));
        ASSERT_TRUE(b.Quad(Solid(10, 10, 50, 50)));
        EXPECT_FLOAT_EQ(b.Instances()[0].softness, 1.0f) << "scale " << scale;
        EXPECT_FLOAT_EQ(b.Instances()[0].pad, 0.5f) << "scale " << scale;
    }
}

TEST(Canvas2DBatch, RadiusTakesTheSmallerAxisScaleAndIsClamped) {
    Canvas2DBuilder b;
    // x scale 2, y scale 1.
    b.Begin(100.0f, 100.0f, Target(0, 0, 200, 100, 200, 100));
    Canvas2DQuad q = Solid(0, 0, 50, 40);
    q.radius = 10.0f;
    ASSERT_TRUE(b.Quad(q));
    EXPECT_FLOAT_EQ(b.Instances()[0].radius, 10.0f) << "min(2, 1) x 10";
    q.radius = 500.0f;
    ASSERT_TRUE(b.Quad(q));
    // Half sizes are 50 x 20 px: the radius cannot exceed the short half side.
    EXPECT_FLOAT_EQ(b.Instances()[1].radius, 20.0f);
    q.radius = -3.0f;
    ASSERT_TRUE(b.Quad(q));
    EXPECT_FLOAT_EQ(b.Instances()[2].radius, 0.0f);
}

TEST(Canvas2DBatch, ColoursUvsAndModeAreCarried) {
    Canvas2DBuilder b;
    b.Begin(100.0f, 100.0f, Target(0, 0, 100, 100, 100, 100));
    Canvas2DQuad q = Solid(0, 0, 10, 10, FakeTex(1));
    q.u0 = 0.25f;
    q.v0 = 0.5f;
    q.u1 = 0.75f;
    q.v1 = 1.0f;
    q.tl = {1, 0, 0, 1};
    q.tr = {0, 1, 0, 0.5f};
    q.bl = {0, 0, 1, 0.25f};
    q.br = {1, 1, 1, 0};
    q.texMode = Canvas2DTexMode::kCoverageA;
    ASSERT_TRUE(b.Quad(q));
    const Canvas2DInstance& in = b.Instances()[0];
    EXPECT_FLOAT_EQ(in.u0, 0.25f);
    EXPECT_FLOAT_EQ(in.v0, 0.5f);
    EXPECT_FLOAT_EQ(in.u1, 0.75f);
    EXPECT_FLOAT_EQ(in.v1, 1.0f);
    EXPECT_FLOAT_EQ(in.tl[0], 1.0f);
    EXPECT_FLOAT_EQ(in.tr[1], 1.0f);
    EXPECT_FLOAT_EQ(in.tr[3], 0.5f);
    EXPECT_FLOAT_EQ(in.bl[2], 1.0f);
    EXPECT_FLOAT_EQ(in.bl[3], 0.25f);
    EXPECT_FLOAT_EQ(in.br[3], 0.0f);
    EXPECT_FLOAT_EQ(in.mode, 4.0f);
    // The same quad with no texture draws the white texture in colour mode: a
    // coverage mode there would read the white texel's channel (1) anyway, but
    // the record says what the shader does.
    q.tex = nullptr;
    ASSERT_TRUE(b.Quad(q));
    EXPECT_FLOAT_EQ(b.Instances()[1].mode, 0.0f);
}

TEST(Canvas2DBatch, BatchesBreakOnTextureAndClipOnly) {
    Canvas2DBuilder b;
    b.Begin(100.0f, 100.0f, Target(0, 0, 100, 100, 100, 100));
    RndTex* a = FakeTex(1);
    RndTex* c = FakeTex(2);
    // Three untextured, two of texture a: 2 batches.
    for (int i = 0; i < 3; i++) ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10)));
    for (int i = 0; i < 2; i++) ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10, a)));
    ASSERT_EQ(b.Batches().size(), 2u);
    EXPECT_EQ(b.Batches()[0].tex, nullptr);
    EXPECT_EQ(b.Batches()[0].first, 0u);
    EXPECT_EQ(b.Batches()[0].count, 3u);
    EXPECT_EQ(b.Batches()[1].tex, a);
    EXPECT_EQ(b.Batches()[1].first, 3u);
    EXPECT_EQ(b.Batches()[1].count, 2u);
    // Back to an earlier texture is a NEW batch: order is z-order, so a quad
    // never joins a batch that is not the last one.
    ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10, c)));
    ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10, a)));
    ASSERT_EQ(b.Batches().size(), 4u);
    EXPECT_EQ(b.Batches()[3].tex, a);
    EXPECT_EQ(b.Batches()[3].first, 6u);
    EXPECT_EQ(b.Batches()[3].count, 1u);
    // Same texture, new clip: a new batch carrying the clip.
    b.PushClip({0, 0, 50, 50});
    ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10, a)));
    ASSERT_EQ(b.Batches().size(), 5u);
    EXPECT_EQ(b.Batches()[4].scissor, (Canvas2DScissor{0, 0, 50, 50}));
    // A push that does not change the rectangle does not break the batch.
    b.PushClip({0, 0, 80, 80});
    ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10, a)));
    EXPECT_EQ(b.Batches().size(), 5u);
    EXPECT_EQ(b.Batches()[4].count, 2u);
    b.PopClip();
    b.PopClip();
    ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10, a)));
    ASSERT_EQ(b.Batches().size(), 6u);
    EXPECT_EQ(b.Batches()[5].scissor, (Canvas2DScissor{0, 0, 100, 100}));
    // Every instance is in exactly one batch, in order.
    uint32_t next = 0;
    for (const Canvas2DBatch& batch : b.Batches()) {
        EXPECT_EQ(batch.first, next);
        next += batch.count;
    }
    EXPECT_EQ(next, (uint32_t)b.Instances().size());
}

TEST(Canvas2DBatch, ClipStackIntersects) {
    Canvas2DBuilder b;
    // Scale 2: logical 100x100 on a 200x200 viewport at (10, 20).
    b.Begin(100.0f, 100.0f, Target(10, 20, 200, 200, 300, 300));
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{0, 0, 300, 300}));
    b.PushClip({10, 10, 50, 40}); // px: x 30..130, y 40..120
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{30, 40, 100, 80}));
    b.PushClip({30, 0, 100, 30}); // px: x 70..270, y 20..80 -> x 70..130, y 40..80
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{70, 40, 60, 40}));
    b.PushClip({80, 80, 10, 10}); // disjoint
    EXPECT_TRUE(b.CurrentClip().Empty());
    bool culled = false;
    EXPECT_FALSE(b.Quad(Solid(0, 0, 100, 100), &culled));
    EXPECT_TRUE(culled) << "an empty clip culls everything";
    b.PopClip();
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{70, 40, 60, 40}));
    b.PopClip();
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{30, 40, 100, 80}));
    b.PopClip();
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{0, 0, 300, 300}));
    b.PopClip(); // one pop too many: the session's base clip stays
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{0, 0, 300, 300}));
    EXPECT_EQ(b.ClipDepth(), 1u);
}

TEST(Canvas2DBatch, ClipRoundsToDevicePixelsAndStaysInsideTheTarget) {
    Canvas2DBuilder b;
    b.Begin(100.0f, 100.0f, Target(0, 0, 150, 150, 150, 150)); // scale 1.5
    // 10.2 * 1.5 = 15.3 -> 15; (10.2 + 20.5) * 1.5 = 46.05 -> 46.
    EXPECT_EQ(b.ToScissor({10.2f, 10.2f, 20.5f, 20.5f}), (Canvas2DScissor{15, 15, 31, 31}));
    // To the NEAREST boundary, not the one below: 10.4 * 1.5 = 15.6 -> 16;
    // (10.4 + 20) * 1.5 = 45.6 -> 46.
    EXPECT_EQ(b.ToScissor({10.4f, 10.2f, 20.0f, 20.5f}), (Canvas2DScissor{16, 15, 30, 31}));
    // Beyond the target on every side: the target.
    EXPECT_EQ(b.ToScissor({-50, -50, 500, 500}), (Canvas2DScissor{0, 0, 150, 150}));
    // The base clip (the renderer's scissor) bounds every push.
    Canvas2DTarget t = Target(0, 0, 150, 150, 150, 150);
    t.baseClip = {20, 30, 40, 50};
    b.Begin(100.0f, 100.0f, t);
    b.PushClip({0, 0, 100, 100});
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{20, 30, 40, 50}));
}

TEST(Canvas2DBatch, IntersectIsSymmetricAndEmptyWhenDisjoint) {
    const Canvas2DScissor a{0, 0, 10, 10}, c{5, 6, 10, 10}, d{10, 0, 5, 5};
    EXPECT_EQ(Canvas2DBuilder::Intersect(a, c), (Canvas2DScissor{5, 6, 5, 4}));
    EXPECT_EQ(Canvas2DBuilder::Intersect(c, a), (Canvas2DScissor{5, 6, 5, 4}));
    EXPECT_TRUE(Canvas2DBuilder::Intersect(a, d).Empty()) << "touching edges share no pixel";
}

TEST(Canvas2DBatch, QuadsOutsideTheClipAreCulled) {
    Canvas2DBuilder b;
    b.Begin(100.0f, 100.0f, Target(0, 0, 100, 100, 100, 100));
    b.PushClip({20, 20, 30, 30});
    bool culled = false;
    EXPECT_FALSE(b.Quad(Solid(60, 20, 10, 10), &culled));
    EXPECT_TRUE(culled);
    EXPECT_FALSE(b.Quad(Solid(20, 0, 10, 10), &culled));
    EXPECT_TRUE(culled);
    // One that only its soft edge brings into the clip is kept...
    Canvas2DQuad glow = Solid(52, 20, 10, 10);
    glow.softness = 8.0f; // pad 4: reaches x 48
    EXPECT_TRUE(b.Quad(glow, &culled));
    EXPECT_FALSE(culled);
    // ...and one that straddles the clip is kept whole (the scissor cuts it).
    EXPECT_TRUE(b.Quad(Solid(40, 40, 30, 30), &culled));
    EXPECT_EQ(b.Instances().size(), 2u);
    // Degenerate quads are dropped, not counted as culled.
    EXPECT_FALSE(b.Quad(Solid(25, 25, 0, 10), &culled));
    EXPECT_FALSE(culled);
    EXPECT_FALSE(b.Quad(Solid(25, 25, 10, -1), &culled));
    EXPECT_FALSE(culled);
    EXPECT_EQ(b.Instances().size(), 2u);
}

TEST(Canvas2DBatch, ClosedBuilderTakesNothingAndClearKeepsTheSession) {
    Canvas2DBuilder b;
    EXPECT_FALSE(b.Quad(Solid(0, 0, 10, 10))) << "before Begin";
    b.Begin(100.0f, 100.0f, Target(0, 0, 100, 100, 100, 100));
    b.PushClip({0, 0, 50, 50});
    ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10)));
    // A flush drops what was drawn; the session and its clip go on.
    b.ClearPending();
    EXPECT_TRUE(b.Instances().empty());
    EXPECT_TRUE(b.Batches().empty());
    EXPECT_TRUE(b.IsOpen());
    EXPECT_EQ(b.CurrentClip(), (Canvas2DScissor{0, 0, 50, 50}));
    ASSERT_TRUE(b.Quad(Solid(0, 0, 10, 10)));
    EXPECT_EQ(b.Batches()[0].first, 0u) << "instance indices restart after a flush";
    b.End();
    EXPECT_FALSE(b.IsOpen());
    EXPECT_FALSE(b.Quad(Solid(0, 0, 10, 10))) << "after End";
    EXPECT_EQ(b.Instances().size(), 1u) << "End keeps what is pending for the last flush";
}

TEST(Canvas2DBatch, ViewportChangeMovesLaterQuadsOnly) {
    Canvas2DBuilder b;
    b.Begin(100.0f, 100.0f, Target(0, 0, 100, 100, 400, 400));
    ASSERT_TRUE(b.Quad(Solid(10, 10, 20, 20)));
    b.SetTarget(Target(0, 0, 400, 400, 400, 400));
    ASSERT_TRUE(b.Quad(Solid(10, 10, 20, 20)));
    EXPECT_FLOAT_EQ(b.Instances()[0].hx, 10.0f);
    EXPECT_FLOAT_EQ(b.Instances()[1].hx, 40.0f);
    EXPECT_FLOAT_EQ(b.Instances()[1].ndcX, 2.0f / 400.0f);
}

// ---------------------------------------------------------------------------
// GPU half
// ---------------------------------------------------------------------------

#ifdef MILO_READBACK_GPU_RENDER_TARGET

struct Rgba {
    int r, g, b, a;
};

struct Frame {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    Rgba At(int x, int y) const {
        const uint8_t* p = &px[((size_t)y * w + x) * 4];
        return {p[0], p[1], p[2], p[3]};
    }
};

::testing::AssertionResult Near(const Frame& f, int x, int y, int r, int g, int b, int tol = 2) {
    const Rgba p = f.At(x, y);
    if (std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "pixel (" << x << "," << y << ") is " << p.r << "," << p.g << "," << p.b
           << ", expected " << r << "," << g << "," << b << " (+-" << tol << ")";
}

// The scene's background: every "untouched" check compares against it exactly.
constexpr int kBgR = 32, kBgG = 32, kBgB = 48;
const Canvas2DColor kBg = {kBgR / 255.0f, kBgG / 255.0f, kBgB / 255.0f, 1.0f};

::testing::AssertionResult Untouched(const Frame& f, int x, int y) {
    return Near(f, x, y, kBgR, kBgG, kBgB, 0);
}

class Canvas2DGpuTest : public EngineTestFixture {
protected:
    bool Ready() const { return gWgpuRnd && gWgpuRnd->Gpu().IsReady() && TheCanvas2D(); }

    // Runs `draw` inside a pass over a w x h render target and reads it back.
    template <class F> bool Render(int w, int h, Frame& out, F&& draw) {
        RndTex* tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(w, h, 32, RndTex::kRendered, false, nullptr);
        RndCam* cam = Hmx::Object::New<RndCam>();
        cam->SetTargetTex(tex);

        TheRnd.BeginDrawing();
        RndCam* prev = RndCam::Current();
        cam->Select();
        draw();
        cam->SetTargetTex(nullptr);
        if (prev) prev->Select();
        TheRnd.EndDrawing();

        bool ok = ReadbackGpuRenderTarget(tex, out.px, out.w, out.h) && out.w == w && out.h == h;
        delete cam;
        delete tex;
        return ok;
    }

    // A 32 bpp bitmap texture from RGBA bytes. A 32 bpp RndBitmap stores
    // B, G, R, A (measured here: bytes written as R,G,B,A came back with red
    // and blue exchanged), so the bytes are reordered for it.
    static RndTex* MakeTex(int w, int h, const std::vector<uint8_t>& rgba) {
        std::vector<uint8_t> bgra(rgba);
        for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
        RndTex* tex = Hmx::Object::New<RndTex>();
        RndBitmap bmp;
        bmp.Create(w, h, 0, 32, 0, nullptr, (void*)bgra.data(), nullptr);
        tex->SetBitmap(bmp, nullptr, true, RndTex::kRegular);
        return tex;
    }
};

// A 4x4 texture of four 2x2 blocks: TL red, TR green, BL blue, BR white.
std::vector<uint8_t> QuadrantTexels() {
    std::vector<uint8_t> px(4 * 4 * 4);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            uint8_t* p = &px[(y * 4 + x) * 4];
            const bool right = x >= 2, bottom = y >= 2;
            p[0] = (!right && !bottom) || (right && bottom) ? 255 : 0;
            p[1] = right ? 255 : 0;
            p[2] = bottom ? 255 : 0;
            p[3] = 255;
            if (right && !bottom) p[2] = 0;
            if (!right && bottom) p[1] = 0;
        }
    }
    return px;
}

// An 8x8 "glyph" of four vertical bands, two texels wide, each holding exactly
// ONE channel at 255 and the other three at 0: alpha, red, green, blue from
// left to right. A coverage mode draws its own band and no other, so reading
// the wrong channel moves the lit band.
std::vector<uint8_t> CoverageTexels() {
    static const int kChannelOfBand[4] = {3, 0, 1, 2}; // A, R, G, B
    std::vector<uint8_t> px(8 * 8 * 4, 0);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) px[(y * 8 + x) * 4 + kChannelOfBand[x / 2]] = 255;
    }
    return px;
}

TEST_F(Canvas2DGpuTest, BeginNeedsAPass) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    Canvas2D* c = TheCanvas2D();
    ASSERT_FALSE(gWgpuRnd->IsInPass());
    EXPECT_FALSE(c->Begin(1280.0f, 720.0f)) << "outside a pass";
    EXPECT_FALSE(c->IsOpen());
    c->FillRect({0, 0, 10, 10}, {1, 1, 1, 1}); // a no-op on a closed canvas
    EXPECT_FALSE(c->HasPending());
    Frame f;
    bool began = false, badSize = true;
    ASSERT_TRUE(Render(64, 64, f, [&] {
        badSize = c->Begin(0.0f, 720.0f);
        began = c->Begin(64.0f, 64.0f);
        c->End();
    }));
    EXPECT_FALSE(badSize) << "a non-positive logical size";
    EXPECT_TRUE(began);
    EXPECT_FALSE(c->IsOpen());
}

// The lane's proof scene: one of everything, at 1 logical unit = 1 pixel.
TEST_F(Canvas2DGpuTest, RenderedProof) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    Canvas2D* c = TheCanvas2D();
    RndTex* quadrants = MakeTex(4, 4, QuadrantTexels());
    RndTex* glyph = MakeTex(8, 8, CoverageTexels());
    const Canvas2DColor yellow = {1.0f, 0.8f, 0.0f, 1.0f};
    const Canvas2DColor cyan = {0.0f, 1.0f, 1.0f, 1.0f};
    const Canvas2DColor magenta = {1.0f, 0.0f, 1.0f, 1.0f};
    const Canvas2DColor orange = {1.0f, 0.5f, 0.0f, 1.0f};

    Frame f;
    Canvas2D::FrameStats stats;
    ASSERT_TRUE(Render(1280, 720, f, [&] {
        ASSERT_TRUE(c->Begin(1280.0f, 720.0f));
        c->FillRect({0, 0, 1280, 720}, kBg);

        // Row 1: solid quads, a half-transparent one, two gradients.
        c->FillRect({40, 40, 100, 100}, {1, 0, 0, 1});
        c->FillRect({160, 40, 100, 100}, {0, 1, 0, 0.5f});
        c->FillRect({280, 40, 100, 100}, {0, 0, 1, 1});
        Canvas2DQuad vg;
        vg.rect = {420, 40, 200, 100};
        vg.SetVerticalGradient({1, 1, 1, 1}, {0, 0, 0, 1});
        c->Quad(vg);
        Canvas2DQuad cg;
        cg.rect = {660, 40, 200, 100};
        cg.tl = {1, 0, 0, 1};
        cg.tr = {0, 1, 0, 1};
        cg.bl = {0, 0, 1, 1};
        cg.br = {1, 1, 1, 1};
        c->Quad(cg);

        // Row 2: rounded rects at radius 0 / 8 / 24, a 2 px ring, a soft
        // shadow under a card.
        c->FillRect({40, 180, 160, 120}, yellow, 0.0f);
        c->FillRect({240, 180, 160, 120}, yellow, 8.0f);
        c->FillRect({440, 180, 160, 120}, yellow, 24.0f);
        Canvas2DQuad ring;
        ring.rect = {640, 180, 160, 120};
        ring.radius = 16.0f;
        ring.border = 2.0f;
        ring.SetColor(cyan);
        c->Quad(ring);
        Canvas2DQuad shadow;
        shadow.rect = {880, 190, 160, 120};
        shadow.radius = 16.0f;
        shadow.softness = 24.0f;
        shadow.SetColor({0, 0, 0, 0.8f});
        c->Quad(shadow);
        c->FillRect({870, 180, 160, 120}, {0.94f, 0.94f, 0.94f, 1.0f}, 16.0f);

        // Row 3: the whole quadrant texture, its top-right block alone (a UV
        // sub-rectangle), and the four-band glyph texture as coverage from
        // alpha and from red, as plain colour, and as coverage from green and
        // from blue.
        Canvas2DQuad whole;
        whole.rect = {40, 340, 128, 128};
        whole.tex = quadrants;
        c->Quad(whole);
        Canvas2DQuad sub = whole;
        sub.rect = {200, 340, 128, 128};
        sub.u0 = 0.5f;
        sub.v0 = 0.0f;
        sub.u1 = 1.0f;
        sub.v1 = 0.5f;
        c->Quad(sub);
        Canvas2DQuad covA;
        covA.rect = {360, 340, 128, 128};
        covA.tex = glyph;
        covA.texMode = Canvas2DTexMode::kCoverageA;
        covA.SetColor(magenta);
        c->Quad(covA);
        Canvas2DQuad covR = covA;
        covR.rect = {520, 340, 128, 128};
        covR.texMode = Canvas2DTexMode::kCoverageR;
        c->Quad(covR);
        // The same glyph as plain colour: black where alpha is 1, nothing
        // where it is 0 (texel x colour).
        Canvas2DQuad plain = covA;
        plain.rect = {680, 340, 128, 128};
        plain.texMode = Canvas2DTexMode::kColor;
        plain.SetColor({1, 1, 1, 1});
        c->Quad(plain);
        Canvas2DQuad covG = covA;
        covG.rect = {840, 340, 128, 128};
        covG.texMode = Canvas2DTexMode::kCoverageG;
        c->Quad(covG);
        Canvas2DQuad covB = covA;
        covB.rect = {1000, 340, 128, 128};
        covB.texMode = Canvas2DTexMode::kCoverageB;
        c->Quad(covB);

        // Row 4: a row of quads taller and longer than its clip.
        c->PushClip({40, 520, 290, 60});
        for (int i = 0; i < 5; i++) c->FillRect({40.0f + 110.0f * i, 510, 100, 80}, orange, 10.0f);
        c->PopClip();
        // After the pop, drawing is unclipped again.
        c->FillRect({700, 520, 100, 60}, orange);
        c->End();
        stats = c->Stats();
    }));

    // Save before judging, so a failing run still leaves the picture.
    const char* env = getenv("MILO_CANVAS2D_PROOF_PNG");
    const std::string path = env && *env ? std::string(env) : TestScratchDir() + "/canvas2d_proof.png";
    EXPECT_TRUE(WritePNG(path.c_str(), f.px.data(), f.w, f.h)) << path;
    printf("Canvas2D proof: %s  quads=%u culled=%u drawCalls=%u flushes=%u\n", path.c_str(),
           stats.quads, stats.culled, stats.drawCalls, stats.flushes);

    // --- background ---
    EXPECT_TRUE(Untouched(f, 5, 5));
    EXPECT_TRUE(Untouched(f, 1275, 715));

    // --- solid quads: exact pixel edges at the default softness ---
    EXPECT_TRUE(Near(f, 90, 90, 255, 0, 0)) << "centre of the red quad";
    EXPECT_TRUE(Near(f, 40, 40, 255, 0, 0)) << "its first pixel";
    EXPECT_TRUE(Near(f, 139, 139, 255, 0, 0)) << "its last pixel";
    EXPECT_TRUE(Untouched(f, 39, 90)) << "left of the red quad";
    EXPECT_TRUE(Untouched(f, 140, 90)) << "right of the red quad";
    EXPECT_TRUE(Untouched(f, 90, 39));
    EXPECT_TRUE(Untouched(f, 90, 140));
    // Straight alpha 0.5 green over the background: (16, 144, 24).
    EXPECT_TRUE(Near(f, 210, 90, 16, 144, 24)) << "half-transparent green";
    EXPECT_TRUE(Near(f, 330, 90, 0, 0, 255));

    // --- vertical gradient: white at the top, black at the bottom ---
    EXPECT_TRUE(Near(f, 520, 40, 254, 254, 254, 3));
    EXPECT_TRUE(Near(f, 520, 90, 126, 126, 126, 4)) << "middle of the gradient";
    EXPECT_TRUE(Near(f, 520, 139, 1, 1, 1, 3));
    EXPECT_TRUE(Near(f, 430, 90, 126, 126, 126, 4)) << "constant along x";
    // --- four corner colours, bilinear ---
    EXPECT_TRUE(Near(f, 661, 41, 255, 0, 0, 8));
    EXPECT_TRUE(Near(f, 858, 41, 0, 255, 0, 8));
    EXPECT_TRUE(Near(f, 661, 138, 0, 0, 255, 8));
    EXPECT_TRUE(Near(f, 858, 138, 255, 255, 255, 8));
    EXPECT_TRUE(Near(f, 760, 90, 128, 128, 128, 4)) << "centre: the mean of the four";

    // --- rounded rects ---
    // radius 0: the corner pixel is filled.
    EXPECT_TRUE(Near(f, 40, 180, 255, 204, 0));
    EXPECT_TRUE(Near(f, 120, 240, 255, 204, 0)) << "centre, radius 0";
    // radius 8 at (240,180): the corner pixel is outside the arc, (243,183)
    // inside it.
    EXPECT_TRUE(Untouched(f, 240, 180)) << "corner pixel, radius 8";
    EXPECT_TRUE(Near(f, 243, 183, 255, 204, 0));
    EXPECT_TRUE(Near(f, 320, 240, 255, 204, 0)) << "centre, radius 8";
    // radius 24 at (440,180): (441,181) and (445,183) lie outside the arc
    // centred (464,204); (448,188) lies inside it (22.6 px from the centre).
    EXPECT_TRUE(Untouched(f, 441, 181)) << "corner pixel, radius 24";
    EXPECT_TRUE(Untouched(f, 445, 183));
    EXPECT_TRUE(Untouched(f, 598, 298)) << "the opposite corner";
    EXPECT_TRUE(Near(f, 448, 188, 255, 204, 0));
    EXPECT_TRUE(Near(f, 520, 240, 255, 204, 0)) << "centre, radius 24";
    EXPECT_TRUE(Near(f, 440, 240, 255, 204, 0)) << "the straight edge is exact";
    EXPECT_TRUE(Untouched(f, 439, 240));
    // The arc is antialiased: somewhere along the corner's diagonal there is
    // a pixel that is neither background nor fill.
    // The arc is antialiased over about one device pixel: on the row one pixel
    // below the top edge the arc is shallow, so it crosses 3 pixels (454..456)
    // that are neither background nor fill.
    {
        int partial = 0;
        for (int x = 440; x < 480; x++) {
            const Rgba p = f.At(x, 181);
            if (p.r > kBgR + 8 && p.r < 247) partial++;
        }
        EXPECT_GE(partial, 1) << "no antialiased pixel on the radius-24 arc";
        EXPECT_LE(partial, 4) << "the arc's edge is wider than one device pixel";
    }

    // --- 2 px ring at (640,180) 160x120 ---
    EXPECT_TRUE(Near(f, 640, 240, 0, 255, 255)) << "ring, outer pixel";
    EXPECT_TRUE(Near(f, 641, 240, 0, 255, 255)) << "ring, inner pixel";
    EXPECT_TRUE(Untouched(f, 642, 240)) << "inside the ring";
    EXPECT_TRUE(Untouched(f, 720, 240)) << "the ring's centre is not filled";
    EXPECT_TRUE(Untouched(f, 639, 240));
    EXPECT_TRUE(Near(f, 799, 240, 0, 255, 255)) << "ring, right side";
    EXPECT_TRUE(Near(f, 720, 180, 0, 255, 255)) << "ring, top side";
    EXPECT_TRUE(Near(f, 720, 299, 0, 255, 255)) << "ring, bottom side";
    EXPECT_TRUE(Untouched(f, 720, 297));

    // --- soft shadow (rect 880..1040 x 190..310, softness 24) ---
    // To the right of the card (which ends at x 1030) the shadow fades out
    // over 24 px centred on its edge at x 1040.
    {
        int prev = -1;
        bool monotonic = true;
        for (int x = 1032; x <= 1056; x++) {
            const int v = f.At(x, 250).r;
            if (v < prev) monotonic = false;
            prev = v;
        }
        EXPECT_TRUE(monotonic) << "the shadow does not fade monotonically";
        EXPECT_LT(f.At(1032, 250).r, 12) << "near full shadow just inside its edge";
        const int mid = f.At(1040, 250).r;
        EXPECT_GT(mid, 10);
        EXPECT_LT(mid, 26) << "about half the shadow on its edge";
        EXPECT_TRUE(Untouched(f, 1053, 250)) << "past the falloff";
    }
    EXPECT_TRUE(Near(f, 950, 240, 240, 240, 240)) << "the card on top of the shadow";

    // --- textures ---
    // The whole 4x4 texture: block centres.
    EXPECT_TRUE(Near(f, 72, 372, 255, 0, 0)) << "top-left block";
    EXPECT_TRUE(Near(f, 136, 372, 0, 255, 0)) << "top-right block";
    EXPECT_TRUE(Near(f, 72, 436, 0, 0, 255)) << "bottom-left block";
    EXPECT_TRUE(Near(f, 136, 436, 255, 255, 255)) << "bottom-right block";
    // The UV sub-rectangle (0.5,0)-(1,0.5) is the green block and nothing
    // else: its first column and last row must not filter in the red block to
    // the left or the white one below.
    EXPECT_TRUE(Near(f, 264, 404, 0, 255, 0)) << "sub-rect centre";
    EXPECT_TRUE(Near(f, 200, 404, 0, 255, 0)) << "sub-rect left edge bleeds its neighbour";
    EXPECT_TRUE(Near(f, 264, 467, 0, 255, 0)) << "sub-rect bottom edge bleeds its neighbour";
    EXPECT_TRUE(Near(f, 327, 340, 0, 255, 0));
    // The glyph's bands are 32 px wide in a 128 px quad: alpha, red, green,
    // blue, centred 16, 48, 80 and 112 px from the quad's left edge. Each
    // coverage mode lights its own band in the quad's colour and leaves the
    // other three untouched.
    static const struct {
        int quadX;
        int litBand; // 0 A, 1 R, 2 G, 3 B
        const char* name;
    } kCoverage[4] = {{360, 0, "coverage A"}, {520, 1, "coverage R"}, {840, 2, "coverage G"},
                      {1000, 3, "coverage B"}};
    for (const auto& cv : kCoverage) {
        for (int band = 0; band < 4; band++) {
            const int x = cv.quadX + 16 + 32 * band;
            if (band == cv.litBand) {
                EXPECT_TRUE(Near(f, x, 404, 255, 0, 255)) << cv.name << ", its own band " << band;
            } else {
                EXPECT_TRUE(Untouched(f, x, 404)) << cv.name << " drew band " << band;
            }
        }
    }
    // Colour mode: texel x colour, so black where the glyph is opaque (the
    // alpha band) and nothing where its alpha is 0, whatever its RGB.
    EXPECT_TRUE(Near(f, 696, 404, 0, 0, 0)) << "colour mode, alpha band";
    EXPECT_TRUE(Untouched(f, 728, 404)) << "colour mode, red band";
    EXPECT_TRUE(Untouched(f, 760, 404)) << "colour mode, green band";
    EXPECT_TRUE(Untouched(f, 792, 404)) << "colour mode, blue band";

    // --- clip (40,520)-(330,580) ---
    EXPECT_TRUE(Near(f, 90, 550, 255, 128, 0)) << "first quad, inside the clip";
    EXPECT_TRUE(Near(f, 200, 550, 255, 128, 0));
    EXPECT_TRUE(Near(f, 329, 550, 255, 128, 0)) << "last pixel inside the clip";
    EXPECT_TRUE(Untouched(f, 330, 550)) << "first pixel outside the clip";
    EXPECT_TRUE(Untouched(f, 350, 550)) << "the straddling quad's clipped part";
    EXPECT_TRUE(Untouched(f, 420, 550)) << "a quad wholly outside the clip";
    EXPECT_TRUE(Untouched(f, 90, 519)) << "above the clip";
    EXPECT_TRUE(Near(f, 90, 520, 255, 128, 0));
    EXPECT_TRUE(Near(f, 90, 579, 255, 128, 0));
    EXPECT_TRUE(Untouched(f, 90, 580)) << "below the clip";
    EXPECT_TRUE(Near(f, 750, 550, 255, 128, 0)) << "after PopClip, unclipped";

    // --- batching: what the scene costs ---
    // 23 quads submitted; the two right-most of the clipped row are wholly
    // outside the clip and never reach the GPU.
    EXPECT_EQ(stats.quads, 23u);
    EXPECT_EQ(stats.culled, 2u);
    // Untextured run, quadrant texture, glyph texture, the clipped row, the
    // unclipped quad after it: 5 draws for 23 quads, in one flush.
    EXPECT_EQ(stats.drawCalls, 5u);
    EXPECT_EQ(stats.flushes, 1u);

    delete quadrants;
    delete glyph;
}

// Logical units are not pixels: 1280x720 logical on a 640x360 target.
TEST_F(Canvas2DGpuTest, LogicalSizeScalesToTheTarget) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    Canvas2D* c = TheCanvas2D();
    Frame f;
    ASSERT_TRUE(Render(640, 360, f, [&] {
        ASSERT_TRUE(c->Begin(1280.0f, 720.0f));
        c->FillRect({0, 0, 1280, 720}, kBg);
        c->FillRect({100, 100, 200, 100}, {1, 0, 0, 1});          // px 50..150 x 50..100
        c->FillRect({400, 100, 200, 100}, {0, 1, 0, 1}, 48.0f);   // px 200..300, radius 24
        c->End();
    }));
    EXPECT_TRUE(Near(f, 50, 50, 255, 0, 0));
    EXPECT_TRUE(Near(f, 149, 99, 255, 0, 0));
    EXPECT_TRUE(Untouched(f, 49, 75));
    EXPECT_TRUE(Untouched(f, 150, 75));
    EXPECT_TRUE(Untouched(f, 100, 49));
    EXPECT_TRUE(Untouched(f, 100, 100));
    // Radius 48 logical is 24 px here: the same corner facts as the proof's
    // radius-24 rect, at (200,50).
    EXPECT_TRUE(Untouched(f, 201, 51));
    EXPECT_TRUE(Untouched(f, 205, 53));
    EXPECT_TRUE(Near(f, 208, 58, 0, 255, 0));
    EXPECT_TRUE(Near(f, 250, 75, 0, 255, 0));
}

// The canvas, Rnd::DrawRect and the canvas again, in call order; two flushes
// in one frame; the renderer's scissor back after a clipped session.
TEST_F(Canvas2DGpuTest, InterleavesWithDrawRectInCallOrder) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    Canvas2D* c = TheCanvas2D();
    Frame f;
    Canvas2D::FrameStats stats;
    bool pendingBefore = false, pendingAfter = true;
    ASSERT_TRUE(Render(256, 256, f, [&] {
        ASSERT_TRUE(c->Begin(256.0f, 256.0f));
        c->FillRect({0, 0, 256, 256}, kBg);
        c->FillRect({20, 20, 120, 120}, {1, 0, 0, 1});            // red, first
        pendingBefore = c->HasPending();
        // The game's own rect, drawn while the canvas is open (target pixels).
        TheRnd.DrawRect(Hmx::Rect(60, 60, 120, 120), Hmx::Color(0, 1, 0, 1), nullptr, nullptr,
                        nullptr);                                 // green, second
        pendingAfter = c->HasPending();
        c->FillRect({100, 100, 120, 120}, {0, 0, 1, 1});          // blue, third
        // A clipped quad, and the session ends with the clip still pushed.
        c->PushClip({200, 10, 20, 20});
        c->FillRect({190, 0, 60, 60}, {1, 1, 0, 1});
        c->End();
        // The renderer's own scissor is back: a rect drawn now is not cut.
        TheRnd.DrawRect(Hmx::Rect(10, 230, 236, 16), Hmx::Color(1, 1, 1, 1), nullptr, nullptr,
                        nullptr);
        stats = c->Stats();
    }));
    EXPECT_TRUE(pendingBefore);
    EXPECT_FALSE(pendingAfter) << "Rnd::DrawRect did not flush the canvas first";
    EXPECT_TRUE(Near(f, 40, 40, 255, 0, 0)) << "red alone";
    EXPECT_TRUE(Near(f, 80, 80, 0, 255, 0)) << "DrawRect's green is over the canvas's red";
    EXPECT_TRUE(Near(f, 160, 80, 0, 255, 0)) << "green alone";
    EXPECT_TRUE(Near(f, 120, 120, 0, 0, 255)) << "the canvas's blue is over both";
    EXPECT_TRUE(Near(f, 200, 200, 0, 0, 255)) << "blue alone";
    EXPECT_TRUE(Near(f, 210, 20, 255, 255, 0)) << "the clipped quad, inside its clip";
    EXPECT_TRUE(Untouched(f, 195, 20)) << "the clipped quad, outside its clip";
    EXPECT_TRUE(Untouched(f, 225, 40));
    EXPECT_TRUE(Near(f, 20, 238, 255, 255, 255)) << "a DrawRect after End() is cut by the canvas's clip";
    EXPECT_TRUE(Near(f, 240, 238, 255, 255, 255));
    EXPECT_EQ(stats.flushes, 2u) << "one flush before DrawRect, one at End";
    EXPECT_EQ(stats.drawCalls, 3u);
}

// A session left open is flushed and closed when its pass ends.
TEST_F(Canvas2DGpuTest, PassEndFlushesAndCloses) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    Canvas2D* c = TheCanvas2D();
    Frame f;
    ASSERT_TRUE(Render(64, 64, f, [&] {
        ASSERT_TRUE(c->Begin(64.0f, 64.0f));
        c->FillRect({0, 0, 64, 64}, {0, 1, 0, 1});
        // no End()
    }));
    EXPECT_FALSE(c->IsOpen());
    EXPECT_FALSE(c->HasPending());
    EXPECT_TRUE(Near(f, 32, 32, 0, 255, 0)) << "the quads of an unended session were lost";
}

// The first canvas frame compiles nothing: the pipelines are made with the
// device (WgpuRnd::PrewarmFixedKeyPipelines).
TEST_F(Canvas2DGpuTest, PipelinesAreWarm) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    EXPECT_EQ(gWgpuRnd->PrewarmFixedKeyPipelines(), 0u) << "the warm is idempotent";
    Canvas2D* c = TheCanvas2D();
    Frame f;
    uint64_t created = 0;
    ASSERT_TRUE(Render(64, 64, f, [&] {
        const uint64_t before = GpuPipelineCreateCount();
        ASSERT_TRUE(c->Begin(64.0f, 64.0f));
        c->FillRect({0, 0, 64, 64}, {1, 0, 0, 1}, 8.0f);
        c->End();
        created = GpuPipelineCreateCount() - before;
    }));
    EXPECT_EQ(created, 0u) << "a canvas draw into a render target compiled a pipeline";
    // Each pass kind a UI can draw in is already there.
    for (const MeshPassContext& ctx :
         {gWgpuRnd->OverlayPassContext(), gWgpuRnd->WorldPassContext(false),
          gWgpuRnd->TexturePassContext(false), gWgpuRnd->TexturePassContext(true)}) {
        EXPECT_EQ(c->Warm((uint32_t)ctx.targetFormat, ctx.hasDepth, ctx.toTexture, ctx.sampleCount), 0)
            << "samples " << ctx.sampleCount << " depth " << ctx.hasDepth << " toTexture "
            << ctx.toTexture;
    }
}

#endif // MILO_READBACK_GPU_RENDER_TARGET

} // namespace
