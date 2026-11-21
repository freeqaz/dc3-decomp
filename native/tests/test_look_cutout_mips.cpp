// np-look26: enhanced cutout coverage kept across mip levels
// (milo::LookSettings::cutoutMipCoverage, standard_wgsl.inc cutoutMipBoost).
//
// Long alpha-tested hair strands (oblio04 at dclive, tan01's side hair) broke
// into dotted chains: a strand thinner than a texel of the mip the sampler
// reads is averaged with its transparent surround, its alpha falls under the
// cut threshold (oblio04_hair.mat: Src, alpha_cut 1, threshold 150), and the
// cut keeps only the texels where it peaks. Measured on a held dclive frame:
// threshold 100 made the strands continuous, lod_bias -1 made them more
// dotted. The enhanced look scales the tested alpha by 1 + 0.25 x (mip level),
// world cutout pipelines only. Retail and Native: off; the scene block then
// carries 0 and no retail pipeline reads it (retail's cut is fs_main).
#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Cam.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using milo::Look;
using milo::LookProfile;
using milo::ScopedLookOverride;

namespace {

class LookCutoutMipTest : public EngineTestFixture {
protected:
    bool HaveGpu() const { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

// ---- the render tests' scene ------------------------------------------------
// Pixel rectangles in the frame (x0, y0, x1, y1; half-open).
struct PxRect {
    int x0, y0, x1, y1;
};

// A 256 x 256 texture, white, whose alpha depends on the column only:
// alpha[u % 4] -- vertical lines with a period of 4 texels. The mips are
// RndBitmap::GenerateMips' 2x2 box down to 16 x 16 (256, 128, 64, 32, 16).
constexpr int kTexSize = 256;
RndTex *MakeLineTex(RndBitmap &bmp, const unsigned char (&alpha)[4]) {
    std::vector<unsigned char> px(kTexSize * kTexSize * 4);
    for (int y = 0; y < kTexSize; y++)
        for (int x = 0; x < kTexSize; x++) {
            unsigned char *p = &px[(y * kTexSize + x) * 4];
            p[0] = p[1] = p[2] = 0xFF;
            p[3] = alpha[x % 4];
        }
    bmp.Create(kTexSize, kTexSize, 0, 32, 0, nullptr, px.data(), nullptr);
    bmp.GenerateMips();
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(bmp, nullptr, true, RndTex::kRegular);
    tex->PresyncBitmap();
    return tex;
}

RndMat *MakeFlatMat(float grey) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetColor(grey, grey, grey);
    mat->SetAlpha(1.0f);
    mat->SetBlend(BaseMaterial::kBlendSrc);
    mat->SetPreLit(true);
    mat->SetZMode(kZModeDisable);
    mat->SetCull(kCullNone);
    return mat;
}

// oblio04_hair.mat's cut: Src blend, alpha_cut 1, alpha_threshold 150.
RndMat *MakeHairMat(RndTex *tex) {
    RndMat *mat = MakeFlatMat(1.0f);
    mat->SetAlphaCut(true);
    mat->SetAlphaThreshold(150);
    mat->SetDiffuseTex(tex);
    return mat;
}

// The frame through an identity camera (Milo: +y forward, +z up) with every
// quad on the plane y = 10: the mapping to pixels is then affine, taken from
// RndCam::WorldToScreen (the projection the draw uses), so a quad covers the
// pixel rectangle it is given and its texture is sampled at the footprint
// that rectangle implies.
struct CutoutScene {
    RndCam *cam = nullptr;
    std::vector<RndMesh *> meshes;
    int W = 0, H = 0;
    float cx = 0, cy = 0, ppuX = 0, ppuZ = 0;

    CutoutScene() {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        W = gpu.OutputWidth();
        H = gpu.OutputHeight();
        cam = Hmx::Object::New<RndCam>();
        cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
        Vector2 s0, sx, sz;
        cam->WorldToScreen(Vector3(0.0f, 10.0f, 0.0f), s0);
        cam->WorldToScreen(Vector3(1.0f, 10.0f, 0.0f), sx);
        cam->WorldToScreen(Vector3(0.0f, 10.0f, -1.0f), sz);
        cx = s0.x * W;
        cy = s0.y * H;
        ppuX = (sx.x - s0.x) * W;   // pixels per world unit, right
        ppuZ = (sz.y - s0.y) * H;   // pixels per world unit, down (z falls)
    }
    ~CutoutScene() {
        for (RndMesh *m : meshes) delete m;
        delete cam;
    }
    // A quad over `r` with uv (0,0) at its top-left and (1,1) bottom-right.
    void Add(const PxRect &r, RndMat *mat) {
        RndMesh *mesh = Hmx::Object::New<RndMesh>();
        mesh->Verts().resize(4);
        const float xs[4] = {(float)r.x0, (float)r.x1, (float)r.x1, (float)r.x0};
        const float ys[4] = {(float)r.y0, (float)r.y0, (float)r.y1, (float)r.y1};
        const float us[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        const float vs[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        for (int i = 0; i < 4; i++) {
            RndMesh::Vert &v = mesh->Verts()[i];
            v.pos.Set((xs[i] - cx) / ppuX, 10.0f, -(ys[i] - cy) / ppuZ);
            v.norm.Set(0.0f, -1.0f, 0.0f);
            v.color = Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f);
            v.tex.Set(us[i], vs[i]);
        }
        mesh->Faces().resize(2);
        mesh->Faces()[0].Set(0, 1, 2);
        mesh->Faces()[1].Set(0, 2, 3);
        mesh->SetMat(mat);
        mesh->Sync(0x3f);
        meshes.push_back(mesh);
    }
    // One whole frame (BeginDrawing .. EndDrawing), read back at output size.
    bool Draw(std::vector<uint8_t> &px) {
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        for (RndMesh *m : meshes) m->DrawShowing();
        if (prev) prev->Select(); else RndCam::ClearCurrent();
        TheRnd.EndDrawing();
        px.assign((size_t)W * H * 4, 0);
        return gWgpuRnd->Gpu().ReadbackFrame(px.data(), px.size());
    }
    // Mean green (never swapped by a BGRA target) over `r` inset by 4 px,
    // clear of the quad's own geometric edge.
    double Mean(const std::vector<uint8_t> &px, const PxRect &r) const {
        double sum = 0;
        int n = 0;
        for (int y = r.y0 + 4; y < r.y1 - 4; y++)
            for (int x = r.x0 + 4; x < r.x1 - 4; x++, n++) sum += px[((size_t)y * W + x) * 4 + 1];
        return n ? sum / n : 0.0;
    }
    // Pixels over `r` (whole, no inset) whose colour differs at all.
    size_t Differing(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b,
                     const PxRect &r) const {
        size_t n = 0;
        for (int y = r.y0; y < r.y1; y++)
            for (int x = r.x0; x < r.x1; x++) {
                const size_t i = ((size_t)y * W + x) * 4;
                if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) n++;
            }
        return n;
    }
    // Lit pixels (green over half) over `r`.
    size_t Lit(const std::vector<uint8_t> &px, const PxRect &r) const {
        size_t n = 0;
        for (int y = r.y0; y < r.y1; y++)
            for (int x = r.x0; x < r.x1; x++) n += px[((size_t)y * W + x) * 4 + 1] > 128;
        return n;
    }
};

// The layout, in a 1280 x 720 frame (the headless default; checked below):
//  - kMinified: the 256-texel line texture over 64 x 64 px, 4 texels per pixel
//    on both axes: the sampler and cutoutMipBoost both read mip 2.
//  - kBase: the other line texture over 320 x 320 px, 0.8 texels per pixel:
//    magnified, log2(0.8) = -0.32 < 0, so the sampler reads mip 0 and the
//    boost is exactly 1 (max(lod, 0) = 0) -- with room for float error in the
//    footprint, which at exactly 1:1 could read a hair above 0.
//  - kSolid: an uncut white quad, the white the coverage is measured against.
const PxRect kMinified = {560, 328, 624, 392};
const PxRect kBase = {120, 200, 440, 520};
const PxRect kSolid = {720, 328, 784, 392};
const PxRect kWholeFrame = {0, 0, 1280, 720};

// Minified texture: 2-texel opaque lines every 4 texels, {255, 255, 0, 0}.
// Mip 1 alternates 255 / 0 per texel; mip 2 and below are (255 + 0) x 2 / 4 =
// 127 everywhere (integer box), alpha 127/255 = 0.498 -- a constant field, so
// the tested alpha has no footprint (fwidth 0) and cutoutCoverage is a hard
// step at the threshold, 150/255 = 0.588:
//   boost off: 0.498 x 1           = 0.498 < 0.588 -> coverage 0 (discarded)
//   boost on:  0.498 x (1 + 0.25 x 2) = 0.747 > 0.588 -> coverage 1
// Between those, no phase or rounding matters: a lod a little under 2 blends
// in mip 1, whose bilinear sample at a pixel centre (base texel 4i + 2, mip-1
// coordinate 2i + 1, the boundary of a 255 and a 0 texel) is 0.5 as well; a
// lod over 2 reads 127 still. The boost estimate's lod could only miss 2 by
// float error, and 0.498 x (1 + 0.25 x 1.9) = 0.73 is still well over.
const unsigned char kMinifiedAlpha[4] = {255, 255, 0, 0};
// Base-level texture: {255, 255, 140, 0}. 140/255 = 0.549 sits just under the
// cut, so ANY boost above 1.072 at the base level moves its pixels (the
// sharpened ramp around it covers more samples, or crosses the step): the
// texture can tell "boost 1 at lod 0" from a boost that leaks there.
const unsigned char kBaseAlpha[4] = {255, 255, 140, 0};

} // namespace

// The look contract: on in Enhanced, off in Retail and Native, and the scene
// value follows the bool. Sabotage: SettingsFor not setting it, or
// CutoutMipScaleFor ignoring the bool -- red.
TEST_F(LookCutoutMipTest, OnlyTheEnhancedLookKeepsCoverageAcrossMips) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        EXPECT_TRUE(Look().cutoutMipCoverage);
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), milo::kCutoutMipScale);
        EXPECT_GT(milo::kCutoutMipScale, 0.0f);
    }
    {
        ScopedLookOverride retail(LookProfile::Retail);
        EXPECT_FALSE(Look().cutoutMipCoverage) << "retail: the original's alpha test";
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.0f);
    }
    {
        ScopedLookOverride native(LookProfile::Native);
        EXPECT_FALSE(Look().cutoutMipCoverage) << "rb3 / rb3-xenon unchanged";
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.0f);
    }
}

// {rnd native_look hair_edge_on|hair_edge_off|hair_edge_rule} and the tuning
// scale. Sabotage: the rule mode not restoring the profile's value -- red.
TEST_F(LookCutoutMipTest, TheInspectionSwitchAlternatesAndRestores) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(0));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.0f);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(1));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), milo::kCutoutMipScale);
        ASSERT_TRUE(milo::SetLookCutoutMipScaleForInspection(false, 0.5f));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.5f);
        ASSERT_TRUE(milo::SetLookCutoutMipScaleForInspection(true, 0.0f));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), milo::kCutoutMipScale);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(0));
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(-1));
        EXPECT_TRUE(Look().cutoutMipCoverage) << "hair_edge_rule: enhanced's own value";
    }
    {
        ScopedLookOverride retail(LookProfile::Retail);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(-1));
        EXPECT_FALSE(Look().cutoutMipCoverage) << "hair_edge_rule: retail's own value";
    }
    {
        ScopedLookOverride native(LookProfile::Native);
        EXPECT_FALSE(milo::SetLookCutoutMipCoverageForInspection(1)) << "no switch for rb3";
        EXPECT_FALSE(Look().cutoutMipCoverage);
    }
}

// The value reaches the scene block of a world pass, and a live switch is
// written on the next scene write for the same camera (no pipeline made).
// Sabotage: WriteSceneUniforms not writing it, or the staleness check not
// watching it -- red.
TEST_F(LookCutoutMipTest, TheSceneBlockCarriesTheScale) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (the rule tests ran)";
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    RndCam *prev = RndCam::Current();
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    TheRnd.BeginDrawing();
    cam->Select();
    gWgpuRnd->EnsureSceneUniformsCurrent();
    const float on = gWgpuRnd->LastSceneCutoutMipScale();
    milo::SetLookCutoutMipCoverageForInspection(0);
    gWgpuRnd->EnsureSceneUniformsCurrent();   // same camera: the scale alone moved
    const float off = gWgpuRnd->LastSceneCutoutMipScale();
    milo::SetLookCutoutMipCoverageForInspection(-1);
    gWgpuRnd->EnsureSceneUniformsCurrent();
    const float rule = gWgpuRnd->LastSceneCutoutMipScale();
    if (prev) prev->Select(); else RndCam::ClearCurrent();
    TheRnd.EndDrawing();
    delete cam;
    EXPECT_FLOAT_EQ(on, milo::kCutoutMipScale) << "enhanced world pass";
    EXPECT_FLOAT_EQ(off, 0.0f) << "hair_edge_off reached the block";
    EXPECT_FLOAT_EQ(rule, milo::kCutoutMipScale) << "hair_edge_rule reached the block";
}

// The cutout pipelines that read it build: a world cutout (4 samples, LOD
// bias, fs_cutout with cutoutMipBoost), its text twin (no bias) and the
// single-sampled plain alpha test. A WGSL error or a derivative outside
// uniform control flow fails here.
TEST_F(LookCutoutMipTest, TheCutoutPipelinesBuild) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    PipelineManager &live = gWgpuRnd->Pipelines();
    struct Case { const char *name; uint32_t samples; bool lodBias; bool skinned; };
    const Case cases[] = {
        {"world cutout, static", 4, true, false},
        {"world cutout, skinned (hair)", 4, true, true},
        {"cutout without the bias (text/HUD class)", 4, false, false},
        {"single-sampled alpha test", 1, true, false},
    };
    for (const Case &c : cases) {
        PipelineKey key{};
        key.targetFormat = gpu.SurfaceFormat();
        key.sampleCount = c.samples;
        key.hasDepth = true;
        key.alphaCut = true;
        key.cutoutCoverage = true;
        key.lodBias = c.lodBias;
        key.layout = c.skinned ? VertexLayoutType::Skinned : VertexLayoutType::Static;
        gpu.Device().PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipe = live.GetPipeline(key);
        std::string err;
        wgpu::Future f = gpu.Device().PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly,
            [&err](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) err.assign(msg.data, msg.length);
            });
        gpu.Instance().WaitAny(f, UINT64_MAX);
        EXPECT_TRUE(pipe) << c.name;
        EXPECT_TRUE(err.empty()) << c.name << ": " << err;
    }
}

// ---- render tests (item 26c): the boost as drawn ------------------------------
// The layout above, drawn through the engine's frame (an MSAA world pass:
// the hair material's pipeline is fs_cutout with kTakesLodBias there) and read
// back. Frames: a warm-up, hair_edge_off twice (the no-change control), then
// hair_edge_on.
namespace {

struct CutoutFrames {
    std::vector<uint8_t> off, offAgain, on;
    double white = 0;
    bool ok = false;
};

CutoutFrames DrawCutoutFrames() {
    CutoutFrames f;
    RndBitmap bmpMin, bmpBase;
    RndTex *texMin = MakeLineTex(bmpMin, kMinifiedAlpha);
    RndTex *texBase = MakeLineTex(bmpBase, kBaseAlpha);
    RndMat *black = MakeFlatMat(0.0f);
    RndMat *white = MakeFlatMat(1.0f);
    RndMat *hairMin = MakeHairMat(texMin);
    RndMat *hairBase = MakeHairMat(texBase);
    {
        CutoutScene scene;
        scene.Add(kWholeFrame, black);
        scene.Add(kSolid, white);
        scene.Add(kMinified, hairMin);
        scene.Add(kBase, hairBase);
        std::vector<uint8_t> warm;
        milo::SetLookCutoutMipCoverageForInspection(0);
        f.ok = scene.Draw(warm) && scene.Draw(f.off) && scene.Draw(f.offAgain);
        milo::SetLookCutoutMipCoverageForInspection(1);
        f.ok = f.ok && scene.Draw(f.on);
        milo::SetLookCutoutMipCoverageForInspection(-1);
        if (f.ok) f.white = scene.Mean(f.off, kSolid);
    }
    delete hairBase;
    delete hairMin;
    delete white;
    delete black;
    delete texBase;
    delete texMin;
    return f;
}

bool FrameIsTheLayoutsSize() {
    const GpuDevice &gpu = gWgpuRnd->Gpu();
    return gpu.OutputWidth() == kWholeFrame.x1 && gpu.OutputHeight() == kWholeFrame.y1;
}

} // namespace

// Enhanced: a strand minified to mip 2 keeps its coverage with the boost and
// loses it without (the arithmetic is at kMinifiedAlpha: off 0.498 under the
// 0.588 cut -> coverage 0, on 0.747 over it -> coverage 1; the bounds leave
// 5% for the present dither and the box's 127 for 127.5), and the same
// material drawn magnified (mip 0, boost exactly 1) is the same frame either
// way, which is what keeps close-up hair and anything at native size from
// fattening. Sabotages (README "Render test (item 26c)"): cutoutMipBoost
// returning 1 -- on reads 0; the boost without its `1 +` -- on reads 0 and the
// base-level quad lights 0 px; the boost leaking to the base level (max(lod
// + 1, 0)) -- 10240 px of the base-level quad move; the scene block left at
// 0 -- on reads 0.
TEST_F(LookCutoutMipTest, AMinifiedStrandKeepsItsCoverageAndTheBaseLevelIsUnmoved) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (the rule tests ran)";
    if (!gWgpuRnd->Gpu().IsHeadless()) GTEST_SKIP() << "reads the headless frame";
    if (!FrameIsTheLayoutsSize()) GTEST_SKIP() << "the layout is for a 1280 x 720 frame";
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    const CutoutFrames f = DrawCutoutFrames();
    ASSERT_TRUE(f.ok) << "frame readback failed";
    CutoutScene px;   // the pixel helpers only
    const double covOff = px.Mean(f.off, kMinified) / f.white;
    const double covOn = px.Mean(f.on, kMinified) / f.white;
    const size_t area = (size_t)(kBase.x1 - kBase.x0) * (kBase.y1 - kBase.y0);
    const size_t baseLit = px.Lit(f.off, kBase);
    const size_t baseMoved = px.Differing(f.off, f.on, kBase);
    const size_t control = px.Differing(f.off, f.offAgain, kWholeFrame);
    printf("[ cutout mips ] enhanced: white %.1f; mip 2 coverage off %.4f on %.4f; "
           "mip 0 lit %zu/%zu, moved by the switch %zu px; off vs off %zu px\n",
           f.white, covOff, covOn, baseLit, area, baseMoved, control);
    EXPECT_GT(f.white, 200.0) << "the uncut reference quad was drawn";
    EXPECT_EQ(control, 0u) << "two hair_edge_off frames must be the same frame";
    EXPECT_LT(covOff, 0.05) << "hair_edge_off: alpha 0.498 at mip 2 is under the 0.588 cut";
    EXPECT_GT(covOn, 0.95) << "hair_edge_on: 0.498 x (1 + 0.25 x 2) = 0.747 is over the cut";
    EXPECT_EQ(baseMoved, 0u) << "at the base level the boost is 1: the same frame";
    EXPECT_GT(baseLit, area * 3 / 10) << "the base-level quad shows its lines (2 of 4 columns opaque)";
    EXPECT_LT(baseLit, area * 9 / 10) << "and its gaps (1 of 4 columns alpha 0)";
}

// Retail: the original's alpha test is fs_main and reads no boost, so even
// hair_edge_on (which puts the scale in the scene block) leaves the frame as
// it was -- 0 px differ. Sabotage: fs_main testing the boosted alpha (the
// boost ungated from kCutoutCoverage) -- 4096 px move, the whole minified
// quad. CutoutMipScaleFor returning the scale under retail moves nothing here
// by design (no retail pipeline reads it); OnlyTheEnhancedLookKeeps... is the
// test that goes red for it. The test process's world pipelines take the LOD
// bias whatever the scoped look (PipelineManager::SetMaterialTakesLodBias is
// set at Init from the startup look, enhanced), so kTakesLodBias does not
// hide the boost from this frame.
TEST_F(LookCutoutMipTest, TheRetailCutIsUnmovedByTheSwitch) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (the rule tests ran)";
    if (!gWgpuRnd->Gpu().IsHeadless()) GTEST_SKIP() << "reads the headless frame";
    if (!FrameIsTheLayoutsSize()) GTEST_SKIP() << "the layout is for a 1280 x 720 frame";
    ScopedLookOverride retail(LookProfile::Retail);
    const CutoutFrames f = DrawCutoutFrames();
    ASSERT_TRUE(f.ok) << "frame readback failed";
    CutoutScene px;
    const size_t moved = px.Differing(f.off, f.on, kWholeFrame);
    const size_t control = px.Differing(f.off, f.offAgain, kWholeFrame);
    const size_t baseLit = px.Lit(f.off, kBase);
    printf("[ cutout mips ] retail: white %.1f; mip 2 mean off %.1f on %.1f; mip 0 lit %zu; "
           "moved by the switch %zu px; off vs off %zu px\n",
           f.white, px.Mean(f.off, kMinified), px.Mean(f.on, kMinified), baseLit, moved, control);
    EXPECT_GT(f.white, 200.0) << "the uncut reference quad was drawn";
    EXPECT_GT(baseLit, 0u) << "the cut quads were drawn";
    EXPECT_EQ(control, 0u) << "two hair_edge_off frames must be the same frame";
    EXPECT_EQ(moved, 0u) << "retail's cut is fs_main: hair_edge_on must not move a pixel";
}
