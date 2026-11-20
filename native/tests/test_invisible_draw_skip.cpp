// A mesh draw that cannot change a pixel is not sent (np-tbdr).
//
// DC3 keeps a menu's hidden layers in the draw list at material alpha 0: a
// captured song-select frame sent 433 mesh draws, 161 of them SrcAlpha or
// SrcAlphaAdd at alpha exactly 0 with no depth write (hidden list rows, toasts,
// the letterbox, the hands-up prompt). Each was rasterised and blended over
// its whole area to leave the target as it was. On an Apple M3 at 1710x962
// that was 2.4-2.9 ms of GPU time in every menu frame (main menu 9.59 ->
// 6.70 ms, song select 9.94 -> 7.55, lobby 6.79 -> 4.32, clock-normalised).
// DrawMeshImmediate asks DrawChangesNothing (platform/MeshPipelineKey.h) and
// returns before binding anything.
//
// What the rule may claim is decided here against the states a pipeline is
// really built with (PipelineManager::MapBlend / MapDepthStencil), not against
// a second copy of the rule: for every key it calls skippable, the blend
// equation at source alpha 0 must give back the destination, and the depth
// and stencil states must write nothing. The readback tests then draw one
// scene with the draws left out and with them sent (SetDrawInvisibleMeshes,
// the MILO_DRAW_INVISIBLE switch) and require the same image.
//
// Sabotage run 2026-11-20 (each applied to the engine, rebuilt, run, restored;
// the unsabotaged control passes 10 of 10 with test_fringe_fade's):
//  - DrawChangesNothing also accepts WgpuBlend::Add -> RuleTable ("blend 2 z 0
//    depth false"), RuleClaimsOnlyWhatThePipelineStatesGive ("called skippable,
//    but at alpha 0 it changes the colour or the alpha") and test_fringe_fade's
//    AddAtAlphaZeroStillAdds (0 against 127) fail;
//  - the z-mode clause removed -> RuleTable ("z mode 1 writes depth"),
//    RuleClaimsOnlyWhatThePipelineStatesGive ("writes depth") and
//    DepthWritingLayerIsStillSent fail (1 draw left out; the far quad shows
//    through, 255 against the backdrop's 102);
//  - the stencil clause removed -> RuleTable and
//    RuleClaimsOnlyWhatThePipelineStatesGive ("writes stencil") fail;
//  - the bloom-mask clause removed -> RuleTable fails;
//  - `materialAlpha != 0` weakened to `materialAlpha > 0.01f` -> RuleTable
//    ("alpha 0.0099999998") and LowAlphaLayerIsStillSent fail (2 draws left
//    out; both halves read the backdrop's 102 for 103 and 104);
//  - `!mat->NextPass()` dropped from DrawMeshImmediate -> NextPassChainIsDrawn
//    fails (1 draw left out; 102 for 166);
//  - the skip never taken (`if (false)`) -> HiddenLayersAreNotSent counts 0
//    left out for 2;
//  - the switch ignored (`if (true)`) -> HiddenLayersAreNotSent counts 2 left
//    out for 0 with SetDrawInvisibleMeshes(true).

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "gfx/PipelineManager.h"
#include "platform/MeshGpuCache.h"
#include "platform/MeshPipelineKey.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"

#include "obj/Data.h"
#include "obj/Object.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Cam.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <algorithm>
#include <cstdint>
#include <vector>

static_assert(rndshape::kSkipInvisibleDraws, "DC3 does not send draws that change nothing");

namespace {

const WgpuBlend kBlends[] = {WgpuBlend::Dest, WgpuBlend::Src, WgpuBlend::Add,
                             WgpuBlend::SrcAlpha, WgpuBlend::SrcAlphaAdd, WgpuBlend::Subtract,
                             WgpuBlend::Multiply, WgpuBlend::PreMultAlpha, WgpuBlend::Screen,
                             WgpuBlend::Lighten, WgpuBlend::Darken};
const WgpuZMode kZModes[] = {WgpuZMode::Disable, WgpuZMode::Normal, WgpuZMode::Transparent,
                             WgpuZMode::Force, WgpuZMode::Decal};
const WgpuStencil kStencils[] = {WgpuStencil::Ignore, WgpuStencil::Write, WgpuStencil::Test};

PipelineKey DrawKey(WgpuBlend blend, WgpuZMode z, WgpuStencil stencil, bool hasDepth) {
    PipelineKey k{};
    k.blend = blend;
    k.zMode = z;
    k.stencil = stencil;
    k.hasDepth = hasDepth;
    return k;
}

struct Rgba {
    float r, g, b, a;
    float operator[](int i) const { return i == 0 ? r : i == 1 ? g : i == 2 ? b : a; }
};

// One channel's blend factor, as WebGPU defines the ones MapBlend uses. `ok`
// goes false on a factor this table does not know, so a new blend cannot pass
// by being evaluated as something else.
float Factor(wgpu::BlendFactor f, const Rgba &src, const Rgba &dst, int ch, bool &ok) {
    switch (f) {
    case wgpu::BlendFactor::Zero: return 0.0f;
    case wgpu::BlendFactor::One: return 1.0f;
    case wgpu::BlendFactor::Src: return src[ch];
    case wgpu::BlendFactor::OneMinusSrc: return 1.0f - src[ch];
    case wgpu::BlendFactor::SrcAlpha: return src.a;
    case wgpu::BlendFactor::OneMinusSrcAlpha: return 1.0f - src.a;
    case wgpu::BlendFactor::Dst: return dst[ch];
    case wgpu::BlendFactor::OneMinusDst: return 1.0f - dst[ch];
    case wgpu::BlendFactor::DstAlpha: return dst.a;
    case wgpu::BlendFactor::OneMinusDstAlpha: return 1.0f - dst.a;
    default: ok = false; return 0.0f;
    }
}

float Blend1(const wgpu::BlendComponent &c, const Rgba &src, const Rgba &dst, int ch, bool &ok) {
    const float s = src[ch], d = dst[ch];
    switch (c.operation) {
    case wgpu::BlendOperation::Add:
        return s * Factor(c.srcFactor, src, dst, ch, ok) + d * Factor(c.dstFactor, src, dst, ch, ok);
    case wgpu::BlendOperation::Subtract:
        return s * Factor(c.srcFactor, src, dst, ch, ok) - d * Factor(c.dstFactor, src, dst, ch, ok);
    case wgpu::BlendOperation::ReverseSubtract:
        return d * Factor(c.dstFactor, src, dst, ch, ok) - s * Factor(c.srcFactor, src, dst, ch, ok);
    case wgpu::BlendOperation::Min: return std::min(s, d);
    case wgpu::BlendOperation::Max: return std::max(s, d);
    default: ok = false; return 0.0f;
    }
}

// True when a fragment of alpha 0 leaves every destination below as it was,
// under this blend state. The sources cover what a shader can write (black,
// a colour, white); the destinations, a target mid-frame.
bool AlphaZeroLeavesTheTarget(const wgpu::BlendState &bs, bool &ok) {
    const Rgba srcs[] = {{0, 0, 0, 0}, {0.25f, 0.5f, 0.75f, 0}, {1, 1, 1, 0}};
    const Rgba dsts[] = {{0, 0, 0, 0}, {0.4f, 0.4f, 0.4f, 1}, {0.2f, 0.6f, 0.9f, 0.5f},
                         {1, 1, 1, 1}, {0.7f, 0.1f, 0.3f, 0}};
    for (const Rgba &s : srcs)
        for (const Rgba &d : dsts) {
            for (int ch = 0; ch < 3; ch++)
                if (Blend1(bs.color, s, d, ch, ok) != d[ch]) return false;
            if (Blend1(bs.alpha, s, d, 3, ok) != d.a) return false;
        }
    return true;
}

bool Keeps(const wgpu::StencilFaceState &f) {
    const auto keep = [](wgpu::StencilOperation op) {
        return op == wgpu::StencilOperation::Keep || op == wgpu::StencilOperation::Undefined;
    };
    return keep(f.failOp) && keep(f.depthFailOp) && keep(f.passOp);
}

class InvisibleDrawTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

} // namespace

// The rule, row by row.
TEST(InvisibleDrawRule, RuleTable) {
    // The two blends that weight the source by its alpha, where nothing writes
    // depth or stencil: the hidden menu layers.
    for (WgpuBlend b : {WgpuBlend::SrcAlpha, WgpuBlend::SrcAlphaAdd}) {
        for (WgpuStencil s : {WgpuStencil::Ignore, WgpuStencil::Test}) {
            EXPECT_TRUE(DrawChangesNothing(DrawKey(b, WgpuZMode::Disable, s, true), 0.0f, 0.0f));
            EXPECT_TRUE(DrawChangesNothing(DrawKey(b, WgpuZMode::Transparent, s, true), 0.0f, 0.0f));
            // No depth attachment (the HUD overlay pass): the z mode is moot.
            for (WgpuZMode z : kZModes)
                EXPECT_TRUE(DrawChangesNothing(DrawKey(b, z, s, false), 0.0f, 0.0f)) << (int)z;
        }
        // A z mode that writes depth draws at any alpha.
        for (WgpuZMode z : {WgpuZMode::Normal, WgpuZMode::Force, WgpuZMode::Decal})
            EXPECT_FALSE(DrawChangesNothing(DrawKey(b, z, WgpuStencil::Ignore, true), 0.0f, 0.0f))
                << "z mode " << (int)z << " writes depth";
        // So does a stencil write, with or without depth.
        EXPECT_FALSE(DrawChangesNothing(DrawKey(b, WgpuZMode::Disable, WgpuStencil::Write, true), 0.0f, 0.0f));
        EXPECT_FALSE(DrawChangesNothing(DrawKey(b, WgpuZMode::Disable, WgpuStencil::Write, false), 0.0f, 0.0f));
        // Any alpha but exactly 0 draws, however small or of whatever sign.
        const PipelineKey hidden = DrawKey(b, WgpuZMode::Disable, WgpuStencil::Ignore, true);
        for (float a : {1.0f, 0.5f, 0.01f, 1.0f / 512.0f, 1e-6f, -0.25f})
            EXPECT_FALSE(DrawChangesNothing(hidden, a, 0.0f)) << "alpha " << a;
        // A bloom mask makes the output alpha the mask (RB3 positive, DC3
        // negative), not the material's.
        EXPECT_FALSE(DrawChangesNothing(hidden, 0.0f, 1.0f));
        EXPECT_FALSE(DrawChangesNothing(hidden, 0.0f, -0.5f));
    }
    // Every other blend draws at alpha 0: Add and PreMultAlpha add their
    // colour whatever the alpha (dclive's DLV_PeakGlass_transparent.mat is
    // alpha 0, Add), Multiply and Darken darken, and so on.
    for (WgpuBlend b : kBlends) {
        if (b == WgpuBlend::SrcAlpha || b == WgpuBlend::SrcAlphaAdd) continue;
        for (WgpuZMode z : kZModes)
            for (bool depth : {false, true})
                EXPECT_FALSE(DrawChangesNothing(DrawKey(b, z, WgpuStencil::Ignore, depth), 0.0f, 0.0f))
                    << "blend " << (int)b << " z " << (int)z << " depth " << depth;
    }
}

// Whatever the rule calls skippable must be a draw the pipeline's own states
// make a no-op, for both of the alpha-channel equations the engine has.
TEST_F(InvisibleDrawTest, RuleClaimsOnlyWhatThePipelineStatesGive) {
    if (IsSkipped()) return;
    ASSERT_NE(gWgpuRnd, nullptr);
    PipelineManager &pm = gWgpuRnd->Pipelines();
    const bool alphaMaxWas = pm.AlphaBlendMax();
    int claimed = 0, noOps = 0;
    for (bool alphaMax : {false, true}) {
        // MapBlend is a pure mapping of the flag; no pipeline is made here.
        pm.SetAlphaBlendMax(alphaMax);
        for (WgpuBlend b : kBlends) {
            bool known = true;
            const bool blendKeeps = AlphaZeroLeavesTheTarget(pm.MapBlend(b), known);
            ASSERT_TRUE(known) << "blend " << (int)b << " uses a factor or operation this test "
                               << "does not evaluate";
            for (WgpuZMode z : kZModes)
                for (WgpuStencil s : kStencils)
                    for (bool depth : {false, true}) {
                        bool writesDepth = false, writesStencil = false;
                        if (depth) {
                            const wgpu::DepthStencilState ds = pm.MapDepthStencil(z, s);
                            writesDepth = ds.depthWriteEnabled == wgpu::OptionalBool::True;
                            writesStencil = !Keeps(ds.stencilFront) || !Keeps(ds.stencilBack);
                        }
                        const bool noOp = blendKeeps && !writesDepth && !writesStencil;
                        noOps += noOp;
                        if (!DrawChangesNothing(DrawKey(b, z, s, depth), 0.0f, 0.0f)) continue;
                        claimed++;
                        const char *why = !blendKeeps ? "changes the colour or the alpha"
                            : writesDepth ? "writes depth" : "writes stencil";
                        EXPECT_TRUE(noOp)
                            << "blend " << (int)b << " z " << (int)z << " stencil " << (int)s
                            << " depth " << depth << " alphaMax " << alphaMax
                            << ": called skippable, but at alpha 0 it " << why;
                    }
        }
    }
    pm.SetAlphaBlendMax(alphaMaxWas);
    // Not vacuous: per alpha equation 2 blends x (2 non-writing z modes x 2
    // stencil modes with depth + 5 x 2 without) = 28 claims, and the states
    // themselves leave room for more (Dest keeps the colour too).
    EXPECT_EQ(claimed, 56);
    EXPECT_GE(noOps, claimed);
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET
namespace {

// A quad at depth y in front of an identity camera (Milo: +y forward, +z up),
// spanning x0..x1 and all of z.
RndMesh *MakeQuad(float x0, float x1, float y, RndMat *mat) {
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    mesh->Verts().resize(4);
    const float xs[4] = {x0, x1, x1, x0};
    const float zs[4] = {-100.0f, -100.0f, 100.0f, 100.0f};
    for (int i = 0; i < 4; i++) {
        RndMesh::Vert &v = mesh->Verts()[i];
        v.pos.Set(xs[i] * y / 10.0f, y, zs[i] * y / 10.0f);
        v.norm.Set(0.0f, -1.0f, 0.0f);
        v.color = Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f);
    }
    mesh->Faces().resize(2);
    mesh->Faces()[0].Set(0, 1, 2);
    mesh->Faces()[1].Set(0, 2, 3);
    mesh->SetMat(mat);
    mesh->Sync(0x3f);
    return mesh;
}

RndMat *MakeMat(float grey, float alpha, BaseMaterial::Blend blend, ZMode z = kZModeDisable) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetColor(grey, grey, grey);
    mat->SetAlpha(alpha);
    mat->SetBlend(blend);
    mat->SetPreLit(true);
    mat->SetZMode(z);
    mat->SetCull(kCullNone);
    return mat;
}

const int kW = 64, kH = 48;
const int kBackdrop = 102; // grey 0.4

// A grey 0.4 opaque backdrop, then the given quads in order, into a render
// target; reads the whole image back.
struct Scene {
    RndTex *tex = nullptr;
    RndCam *cam = nullptr;
    RndMat *grey = nullptr;
    RndMesh *backdrop = nullptr;
    std::vector<RndMesh *> quads;
    int skipped = -1; // draws not sent in the last frame drawn

    Scene() {
        tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(kW, kH, 32, RndTex::kRendered, false, nullptr);
        cam = Hmx::Object::New<RndCam>();
        cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
        cam->SetTargetTex(tex);
        grey = MakeMat(0.4f, 1.0f, BaseMaterial::kBlendSrc);
        backdrop = MakeQuad(-1000.0f, 1000.0f, 500.0f, grey);
    }
    ~Scene() {
        cam->SetTargetTex(nullptr);
        for (RndMesh *q : quads) delete q;
        delete backdrop;
        delete grey;
        delete cam;
        delete tex;
    }
    void Add(float x0, float x1, float y, RndMat *mat) { quads.push_back(MakeQuad(x0, x1, y, mat)); }
    bool Draw(std::vector<uint8_t> &px) {
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        backdrop->DrawShowing();
        for (RndMesh *q : quads) q->DrawShowing();
        skipped = GetInvisibleMeshDrawsSkippedThisFrame();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        int w = 0, h = 0;
        return ReadbackGpuRenderTarget(tex, px, w, h) && w == kW && h == kH;
    }
    // Draws twice (the first frame of a target is its creation) and keeps the
    // second.
    bool DrawSettled(std::vector<uint8_t> &px) { return Draw(px) && Draw(px); }
};

// Green (never swapped by a BGRA target) at the middle of the left / right half.
int Left(const std::vector<uint8_t> &px) { return px[((kH / 2) * kW + kW / 4) * 4 + 1]; }
int Right(const std::vector<uint8_t> &px) { return px[((kH / 2) * kW + 3 * kW / 4) * 4 + 1]; }

// Restores the process's switch whatever a test did to it.
struct DrawInvisibleScope {
    bool was = DrawInvisibleMeshes();
    ~DrawInvisibleScope() { SetDrawInvisibleMeshes(was); }
};

} // namespace

// White SrcAlpha and SrcAlphaAdd layers at alpha 0 over the backdrop: not
// sent, counted, and the image is the one that sending them gives.
TEST_F(InvisibleDrawTest, HiddenLayersAreNotSent) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    DrawInvisibleScope restore;
    RndMat *hidden = MakeMat(1.0f, 0.0f, BaseMaterial::kBlendSrcAlpha);
    RndMat *hiddenAdd = MakeMat(1.0f, 0.0f, BaseMaterial::kBlendSrcAlphaAdd);
    std::vector<uint8_t> skippedPx, sentPx;
    {
        Scene s;
        s.Add(-1000.0f, 0.0f, 10.0f, hidden);
        s.Add(0.0f, 1000.0f, 10.0f, hiddenAdd);

        SetDrawInvisibleMeshes(false);
        ASSERT_TRUE(s.DrawSettled(skippedPx)) << "no GPU render target";
        EXPECT_EQ(s.skipped, 2) << "both hidden layers are left out";

        SetDrawInvisibleMeshes(true);
        ASSERT_TRUE(s.DrawSettled(sentPx));
        EXPECT_EQ(s.skipped, 0) << "MILO_DRAW_INVISIBLE sends them";
    }
    EXPECT_NEAR(Left(sentPx), kBackdrop, 1) << "an alpha-0 SrcAlpha layer shows nothing";
    EXPECT_NEAR(Right(sentPx), kBackdrop, 1) << "an alpha-0 SrcAlphaAdd layer shows nothing";
    EXPECT_EQ(skippedPx, sentPx) << "leaving the draws out changed the image";
    delete hiddenAdd;
    delete hidden;
}

// A layer the rule must not take: alpha 1/128 is not 0. White at 1/128 over
// grey 0.4 is 102 + (255 - 102) / 128 = 103.2, and SrcAlphaAdd adds 255 / 128.
TEST_F(InvisibleDrawTest, LowAlphaLayerIsStillSent) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    DrawInvisibleScope restore;
    SetDrawInvisibleMeshes(false);
    RndMat *faint = MakeMat(1.0f, 1.0f / 128.0f, BaseMaterial::kBlendSrcAlpha);
    RndMat *faintAdd = MakeMat(1.0f, 1.0f / 128.0f, BaseMaterial::kBlendSrcAlphaAdd);
    std::vector<uint8_t> px;
    {
        Scene s;
        s.Add(-1000.0f, 0.0f, 10.0f, faint);
        s.Add(0.0f, 1000.0f, 10.0f, faintAdd);
        ASSERT_TRUE(s.DrawSettled(px)) << "no GPU render target";
        EXPECT_EQ(s.skipped, 0);
    }
    EXPECT_GT(Left(px), kBackdrop) << "SrcAlpha at 1/128 lightens the backdrop";
    EXPECT_NEAR(Left(px), 103, 1);
    EXPECT_NEAR(Right(px), 104, 1) << "SrcAlphaAdd at 1/128 adds 2";
    delete faintAdd;
    delete faint;
}

// An alpha-0 layer whose z mode writes depth is an occluder, and must still be
// one: the white quad behind it stays hidden. The right half is the control
// (no occluder: the white quad shows), so the test cannot pass on a target
// without depth.
TEST_F(InvisibleDrawTest, DepthWritingLayerIsStillSent) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    DrawInvisibleScope restore;
    SetDrawInvisibleMeshes(false);
    RndMat *occluder = MakeMat(1.0f, 0.0f, BaseMaterial::kBlendSrcAlpha, kZModeNormal);
    RndMat *white = MakeMat(1.0f, 1.0f, BaseMaterial::kBlendSrc, kZModeNormal);
    std::vector<uint8_t> px;
    {
        Scene s;
        s.Add(-1000.0f, 0.0f, 10.0f, occluder); // near, left half only
        s.Add(-1000.0f, 1000.0f, 20.0f, white); // behind it, both halves
        ASSERT_TRUE(s.DrawSettled(px)) << "no GPU render target";
        EXPECT_EQ(s.skipped, 0) << "a depth write is a change";
    }
    ASSERT_EQ(Right(px), 255) << "control: the white quad draws where nothing is in front";
    EXPECT_NEAR(Left(px), kBackdrop, 1) << "the alpha-0 occluder's depth hides the quad behind it";
    delete white;
    delete occluder;
}

// A material's NextPass chain draws with the first pass's geometry bindings,
// so an alpha-0 first pass still goes out: its Add pass adds grey 0.25 (64).
TEST_F(InvisibleDrawTest, NextPassChainIsDrawn) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    DrawInvisibleScope restore;
    SetDrawInvisibleMeshes(false);
    RndMat *first = MakeMat(1.0f, 0.0f, BaseMaterial::kBlendSrcAlpha);
    RndMat *glow = MakeMat(0.25f, 1.0f, BaseMaterial::kBlendAdd);
    first->SetProperty("next_pass", DataNode(glow));
    ASSERT_EQ(first->NextPass(), glow) << "the test could not chain a next pass";
    std::vector<uint8_t> px;
    {
        Scene s;
        s.Add(-1000.0f, 0.0f, 10.0f, first);
        ASSERT_TRUE(s.DrawSettled(px)) << "no GPU render target";
        EXPECT_EQ(s.skipped, 0);
    }
    EXPECT_NEAR(Left(px), kBackdrop + 64, 2) << "the Add pass of an alpha-0 first pass";
    EXPECT_NEAR(Right(px), kBackdrop, 1);
    delete first;
    delete glow;
}
#endif // MILO_READBACK_GPU_RENDER_TARGET
