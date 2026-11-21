// np-look26 d: the enhanced cutout's alpha test per MSAA sample
// (milo::LookSettings::cutoutSampleTest, standard_wgsl.inc cutoutSampleMask).
//
// Long alpha-tested hair strands (oblio04 at dclive, tan01's side hair) broke
// into dotted chains under the enhanced look: it uploads the whole mip chain
// and filters it 16x anisotropically, so a strand thinner than a texel of the
// level it reads is averaged with its transparent surround and falls under the
// cut (oblio04_hair.mat: Src, alpha_cut 1, threshold 150), and the landed
// coverage (cutoutCoverage) tests that averaged alpha once per pixel. Retail
// reads its top two levels only: its strands alias but do not dissolve.
//
// The enhanced look now takes the original's alpha test at each of the 4x
// pass's samples (the texel alpha at the sample's own uv, one bilinear fetch at
// the level retail would pick for half a pixel, times the material and vertex
// alpha) and the sample mask is the four results. Nothing passes that retail's
// test would not pass at that point, so a fade authored under the cut stays
// invisible. Item 26's boost of the averaged alpha (1 + 0.25 x mip level) was
// rejected for exactly that: it drew dclive's PeakLine floor fades (SrcAlpha,
// threshold 110, alpha just under) as a solid band. Retail and Native: off;
// the scene block then carries 0 and no retail pipeline reads it (retail's cut
// is fs_main). The suite keeps the item-26 name.
//
// np-look26 e: the test is a mode (milo::CutoutSampleMode in the scene block),
// the enhanced default C: retail's test at the pixel centre first, a pass
// drawing the pixel solid, a fail leaving the four results (a fringe). B (the
// four results alone) was faithful but noisy on hair minified past retail's
// level 1. E is B where retail's level resolves the alpha (lod <= 1), C above.
// The render tests run the default mode and, where the geometry makes the
// answer the same, E; B and the centre-only diagnostic are read where they
// show a test can tell the modes apart.
//
// np-look26 f: the default is E. C's two rules are tested through E where the
// geometry is minified past retail's level 1 (lod 2: the strand, the superset
// and the lod-2 fringe), and E must equal B below it (the lod-0 quad whose
// centre passes while two samples fail: C draws it solid, B and E at 2 of 4).
// C is read through its own token. LookSwitchLevelsTest (end of file): after
// {rnd native_look retail} in a run started enhanced, world textures read
// retail's two levels.
#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
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
#include <algorithm>
#include <cmath>
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

// Pixel rectangles with fractional edges (the bit-order quads).
struct PxRectF {
    float x0, y0, x1, y1;
};

// A 256 x 256 white texture whose alpha is alpha(x, y). The mips are
// RndBitmap::GenerateMips' 2x2 box down to 16 x 16 (256, 128, 64, 32, 16).
constexpr int kTexSize = 256;
template <class F> RndTex *MakeAlphaTex(RndBitmap &bmp, F alpha) {
    std::vector<unsigned char> px(kTexSize * kTexSize * 4);
    for (int y = 0; y < kTexSize; y++)
        for (int x = 0; x < kTexSize; x++) {
            unsigned char *p = &px[(y * kTexSize + x) * 4];
            p[0] = p[1] = p[2] = 0xFF;
            p[3] = alpha(x, y);
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

// An alpha-cut material: oblio04_hair.mat's is Src at 150; dclive's PeakLine
// fades are SrcAlpha at 90 and 110.
RndMat *MakeCutMat(RndTex *tex, int threshold, BaseMaterial::Blend blend = BaseMaterial::kBlendSrc) {
    RndMat *mat = MakeFlatMat(1.0f);
    mat->SetBlend(blend);
    mat->SetAlphaCut(true);
    mat->SetAlphaThreshold(threshold);
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
    // A quad over `r` with uv (u0, v0) at its top-left and (u1, v1) at its
    // bottom-right.
    void Add(const PxRectF &r, RndMat *mat, float u0, float v0, float u1, float v1) {
        RndMesh *mesh = Hmx::Object::New<RndMesh>();
        mesh->Verts().resize(4);
        const float xs[4] = {r.x0, r.x1, r.x1, r.x0};
        const float ys[4] = {r.y0, r.y0, r.y1, r.y1};
        const float us[4] = {u0, u1, u1, u0};
        const float vs[4] = {v0, v0, v1, v1};
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
    // The whole texture over `r`.
    void Add(const PxRect &r, RndMat *mat) {
        Add(PxRectF{(float)r.x0, (float)r.y0, (float)r.x1, (float)r.y1}, mat, 0, 0, 1, 1);
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
    uint8_t G(const std::vector<uint8_t> &px, int x, int y) const {
        return px[((size_t)y * W + x) * 4 + 1];   // green: never swapped by a BGRA target
    }
    // Mean green over `r` inset by `inset` px (clear of the quad's own edge).
    double Mean(const std::vector<uint8_t> &px, const PxRect &r, int inset = 4) const {
        double sum = 0;
        int n = 0;
        for (int y = r.y0 + inset; y < r.y1 - inset; y++)
            for (int x = r.x0 + inset; x < r.x1 - inset; x++, n++) sum += G(px, x, y);
        return n ? sum / n : 0.0;
    }
    // Max green over `r` (whole).
    int Max(const std::vector<uint8_t> &px, const PxRect &r) const {
        int m = 0;
        for (int y = r.y0; y < r.y1; y++)
            for (int x = r.x0; x < r.x1; x++) m = std::max(m, (int)G(px, x, y));
        return m;
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
};

// The layout, in a 1280 x 720 frame (the headless default; checked below).
//
// Strands (2-texel opaque lines every 4 texels, alpha columns {255, 255, 0,
// 0}: true coverage one half), minified:
//  - kMin4: 256 texels over 64 px, 4 texels per pixel. The landed coverage
//    reads mip 2 (16x anisotropic, isotropic footprint), where the box mips
//    are (255 + 0) x 2 / 4 = 127 everywhere: 0.498 under the 0.588 cut, a
//    constant field (fwidth 0) -> coverage 0. The per-sample test reads
//    retail's level for the pixel, clamp(log2 4, 0, 1) = 1, where the lines
//    alternate 255 / 0 per texel, 2 level-1 texels per pixel. Pixel p's centre is level-1 coordinate
//    2p + 1, and sample offset o (pixels) sits at 2p + 1 + 2o; the bilinear
//    weight on the even (255) texel is then 0.75 at o = -0.125 (sample 0) and
//    -0.375 (sample 2), 0.25 at +0.375 (1) and +0.125 (3). 0.75 > 0.588 >
//    0.25: exactly samples 0 and 2 pass in every pixel, coverage 2/4. Level 0
//    instead (float error under 1) reads texels 4p+1 / 4p / 4p+3 / 4p+2 dead
//    centre: the same two samples.
//  - kMin8: 256 texels over 32 px, 8 texels per pixel: log2 8 = 3 is
//    clamped to retail's last level, 1. There 4 level-1 texels per pixel, and
//    the samples sit on texel centres 4p + 1 / 4p + 3 / 4p / 4p + 2 for
//    samples 0..3: samples 2 and 3 (even texels) pass, coverage 2/4 again. The
//    full chain (level 3, the box's 127) would cover nothing.
//
// Under the cut (the PeakLine regression), a constant alpha, so every level
// reads the same value:
//  - Src at 150 with alpha 140 (0.549 < 0.588), and SrcAlpha at 110 with
//    alpha 100 (0.392 < 0.431), each minified like kMin4 (mip 2 for the
//    sampler: the boost's 1 + 0.25 x 2 lifted them to 0.82 and 0.59, over
//    their cuts) and magnified, 256 texels over 320 px (mip 0). Retail's test
//    fails at every sample: nothing may be drawn.
//
// Bit order: a quad 1/4 px thin that geometrically covers exactly one of the
// standard 4x positions (6,2) (14,6) (2,10) (10,14) sixteenths in each pixel
// of one row or column, textured at 1 texel per pixel (footprint 1: level 0)
// with alpha 255 on every third texel row or column only. Each sample is
// extreme on one axis: sample 0 reaches furthest up (y -0.375), 1 right
// (x +0.375), 2 left (x -0.375), 3 down (y +0.375). In a test pixel (texel
// index 1 of the period of 3) the bilinear weight on the neighbour texel
// toward an offset o is |o|; so with alpha 255 on the row above (index 0)
// sample 0 reads 0.375, sample 1 (y -0.125) 0.125 and 2, 3 nothing; likewise
// the row below passes only sample 3, the column right only 1, the column left
// only 2, at a threshold of 64 (0.251). A test pixel is then lit, to 1/4 of
// white, iff the sample the shader computed for mask bit j is the one the
// geometry covers: the diagonal of a 4 x 4 matrix. A fifth column, alpha 255
// everywhere, checks the geometry alone (1/4 in every row).
const PxRect kSolid = {960, 40, 1024, 104};
const PxRect kMin4 = {760, 40, 824, 104};
const PxRect kMin8 = {860, 40, 892, 72};
const PxRect kUnderSrcMin = {760, 140, 824, 204};
const PxRect kUnderAlphaMin = {860, 140, 924, 204};
const PxRect kUnderSrcBase = {40, 20, 360, 340};
const PxRect kUnderAlphaBase = {400, 20, 720, 340};
const PxRect kWholeFrame = {0, 0, 1280, 720};
// Superset of retail (mode C's first rule): an isolated strand one pixel wide,
// minified like kMin4 (4 texels per pixel, pixel p = texels 4p..4p+3), alpha
// 255 on texel columns x % 16 in [4, 8), else 0, cut at 217 (0.851). Retail's
// level for the pixel is clamp(log2 4, 0, 1) = 1, where the strand is the two
// level-1 texels 2p, 2p+1 of the pixels p % 4 == 1 (and level 2's texel p,
// for a sampler that reads the chain at lod 2: the same pixels). In a strand
// pixel the centre (level-1 coordinate 2p + 1) reads 1, as do samples 0 and 3
// (2p + 0.75, 2p + 1.25); samples 1 and 2 (2p + 1.75, 2p + 0.25) read 0.75,
// under the cut. Retail's single test passes: the pixel is solid. B: 2 of 4.
// C (and E: lod 2 > 1): the centre passes, full. Off the strand every fetch
// reads at most 0.25: nothing.
const PxRect kStrand = {760, 240, 824, 304};
// Fringe (mode C's second rule): a whole-pixel quad of the bit-order texture
// j (below) at 1 texel per pixel, cut at 64. In a test pixel (texel index 1 of
// the period of 3 on both axes) the centre reads its own texel, 0, and fails;
// exactly sample j passes (weight 0.375 on the lit neighbour; the others read
// 0.125 or 0). The quad covers all four positions, so the pixel resolves to
// exactly 1/4: C keeps B's mask where the centre fails. E reads B here (lod
// 0). Retail's test fails at the centre: retail draws nothing there.
inline int FringeX(int j) { return 40 + 48 * j; }
// np-look26 f. The fringe minified past retail's level 1, where E takes C's
// rules: a 256-texel texture over 64 px (4 texels per pixel, lod 2, read at
// level 1: 2 level-1 texels per pixel, pixel p's centre at level-1 coordinate
// 2p + 1). Level 0 is lit (255) in the 2 x 2 blocks whose block indices
// (x >> 1, y >> 1) have parities (px, py), so level 1 (the box) is 255 exactly
// on the texels with parities (px, py) and 0 elsewhere. Bilinear weight on the
// lit parity, x then y: centre 0.5 x 0.5 = 0.25; sample 0 (-0.125, -0.375)
// reads 0.75 x 0.75 = 0.5625 for (even, even) and at most 0.1875 for the other
// parities; likewise sample 1 (+0.375, -0.125) for (odd, even), sample 2
// (-0.375, +0.125) for (even, odd), sample 3 (+0.125, +0.375) for (odd, odd).
// Cut at 102 (0.400): the centre fails, exactly sample j passes, the pixel
// resolves to 1/4. Retail (two levels, level 1 at the centre: 0.25) draws
// nothing.
inline PxRect Fringe2(int j) { return {40 + 80 * j, 600, 104 + 80 * j, 664}; }
inline int Fringe2Alpha(int j, int x, int y) {
    return ((x >> 1) & 1) == (j & 1) && ((y >> 1) & 1) == (j >> 1) ? 255 : 0;
}
// np-look26 f. Below retail's level 1, E is B: the column texture lit on x % 3
// == 0 (bit texture 2) at 1 texel per pixel (lod 0, level 0) over 64 x 64 px,
// cut at 191 (0.749). In a lit column (pixel i, texel i) the centre reads 1
// and passes; samples 0 and 3 (x -0.125, +0.125) read 0.875 and pass, samples
// 1 and 2 (x +0.375, -0.375) read 0.625 and fail. C draws the pixel solid,
// B (and so E) 2 of 4. Unlit columns read at most 0.375: nothing.
const PxRect kCentrePassLod0 = {400, 600, 464, 664};
constexpr int kFringeY = 400, kFringeLen = 33;
constexpr int kBitLen = 32;
// Bit-order cell (geometry k = the sample position covered, alpha j = the
// sample index that passes, 4 = all): its line's origin pixel.
inline int BitCellX(int j) { return 760 + 48 * j; }
inline int BitCellY(int k) { return 400 + 48 * k; }
// The cells' texture origin is 4 px before the cell (texel 4 = index 1 of the
// period at the cell's first pixel), so the test pixels are every third pixel
// along the line from its start.
constexpr int kBitOrigin = 4;
// The pixels the cell's geometry covers: row y (k = 0: y in [Y, Y + 1/4),
// position (6,2); k = 3: [Y + 3/4, Y + 1), (10,14)) or column x (k = 1:
// [X + 3/4, X + 1), (14,6); k = 2: [X, X + 1/4), (2,10)).
PxRect BitCellPixels(int k, int j) {
    const int X = BitCellX(j), Y = BitCellY(k);
    if (k == 0 || k == 3) return {X, Y, X + kBitLen, Y + 1};
    return {X, Y, X + 1, Y + kBitLen};
}
PxRectF BitCellGeometry(int k, int j) {
    const float X = (float)BitCellX(j), Y = (float)BitCellY(k);
    switch (k) {
    case 0: return {X, Y, X + kBitLen, Y + 0.25f};
    case 3: return {X, Y + 0.75f, X + kBitLen, Y + 1.0f};
    case 1: return {X + 0.75f, Y, X + 1.0f, Y + kBitLen};
    default: return {X, Y, X + 0.25f, Y + kBitLen};
    }
}
const char *const kSamplePos[4] = {"(6,2)", "(14,6)", "(2,10)", "(10,14)"};

} // namespace

// The look contract: on in Enhanced, off in Retail and Native, and the scene
// value follows the bool. Sabotage: SettingsFor not setting it, or setting it
// under retail (S5), or CutoutSampleTestFor ignoring the bool -- red.
TEST_F(LookCutoutMipTest, OnlyTheEnhancedLookTestsEachSample) {
    if (IsSkipped()) return;
    static_assert(milo::kCutoutModeDefault == milo::kCutoutModeE, "np-look26 f: the default is E");
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        EXPECT_TRUE(Look().cutoutSampleTest);
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), (float)milo::kCutoutModeE)
            << "the enhanced look carries the default mode, E";
    }
    {
        ScopedLookOverride retail(LookProfile::Retail);
        EXPECT_FALSE(Look().cutoutSampleTest) << "retail: the original's alpha test";
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), 0.0f);
    }
    {
        ScopedLookOverride native(LookProfile::Native);
        EXPECT_FALSE(Look().cutoutSampleTest) << "rb3 / rb3-xenon unchanged";
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), 0.0f);
    }
}

// {rnd native_look hair_edge_on|hair_edge_off|hair_edge_rule} and the mode
// tokens hair_edge_b|c|e|centre. Sabotage: the rule mode not restoring the
// profile's value, a mode token surviving hair_edge_on or a profile switch --
// red.
TEST_F(LookCutoutMipTest, TheInspectionSwitchAlternatesAndRestores) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        ASSERT_TRUE(milo::SetLookCutoutSampleTestForInspection(0));
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), 0.0f);
        ASSERT_TRUE(milo::SetLookCutoutSampleTestForInspection(1));
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), (float)milo::kCutoutModeDefault);
        for (int m = milo::kCutoutModeB; m <= milo::kCutoutModeCentre; m++) {
            ASSERT_TRUE(milo::SetLookCutoutModeForInspection(m)) << "mode " << m;
            EXPECT_TRUE(Look().cutoutSampleTest);
            EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), (float)m) << "mode token " << m;
        }
        EXPECT_FALSE(milo::SetLookCutoutModeForInspection(0)) << "0 is hair_edge_off, not a mode";
        EXPECT_FALSE(milo::SetLookCutoutModeForInspection(5));
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), (float)milo::kCutoutModeCentre)
            << "a refused mode leaves the last one";
        ASSERT_TRUE(milo::SetLookCutoutSampleTestForInspection(1));
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), (float)milo::kCutoutModeDefault)
            << "hair_edge_on: back to the default mode";
        ASSERT_TRUE(milo::SetLookCutoutModeForInspection(milo::kCutoutModeB));
        ASSERT_TRUE(milo::SetLookCutoutSampleTestForInspection(0));
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), 0.0f) << "hair_edge_off after a mode";
        ASSERT_TRUE(milo::SetLookCutoutModeForInspection(milo::kCutoutModeC));
        ASSERT_TRUE(milo::SetLookCutoutSampleTestForInspection(-1));
        EXPECT_TRUE(Look().cutoutSampleTest) << "hair_edge_rule: enhanced's own value";
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), (float)milo::kCutoutModeDefault)
            << "hair_edge_rule: enhanced's own mode";
        ASSERT_TRUE(milo::SetLookCutoutModeForInspection(milo::kCutoutModeB));
        ASSERT_TRUE(milo::SetLookProfileForInspection(LookProfile::Enhanced));
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), (float)milo::kCutoutModeDefault)
            << "{rnd native_look enhanced}: the profile's own mode";
    }
    {
        ScopedLookOverride retail(LookProfile::Retail);
        ASSERT_TRUE(milo::SetLookCutoutSampleTestForInspection(-1));
        EXPECT_FALSE(Look().cutoutSampleTest) << "hair_edge_rule: retail's own value";
    }
    {
        ScopedLookOverride native(LookProfile::Native);
        EXPECT_FALSE(milo::SetLookCutoutSampleTestForInspection(1)) << "no switch for rb3";
        EXPECT_FALSE(milo::SetLookCutoutModeForInspection(milo::kCutoutModeC)) << "no mode for rb3";
        EXPECT_FALSE(Look().cutoutSampleTest);
        EXPECT_FLOAT_EQ(milo::CutoutSampleTestFor(Look()), 0.0f);
    }
}

// The value reaches the scene block of a world pass, and a live switch is
// written on the next scene write for the same camera (no pipeline made).
// Sabotage: WriteSceneUniforms not writing it, or the staleness check not
// watching it -- red.
TEST_F(LookCutoutMipTest, TheSceneBlockCarriesTheSwitch) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (the rule tests ran)";
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    RndCam *prev = RndCam::Current();
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    TheRnd.BeginDrawing();
    cam->Select();
    gWgpuRnd->EnsureSceneUniformsCurrent();
    const float on = gWgpuRnd->LastSceneCutoutSampleTest();
    milo::SetLookCutoutSampleTestForInspection(0);
    gWgpuRnd->EnsureSceneUniformsCurrent();   // same camera: the switch alone moved
    const float off = gWgpuRnd->LastSceneCutoutSampleTest();
    float modes[5] = {0, 0, 0, 0, 0};
    for (int m = milo::kCutoutModeB; m <= milo::kCutoutModeCentre; m++) {
        milo::SetLookCutoutModeForInspection(m);
        gWgpuRnd->EnsureSceneUniformsCurrent();   // a mode alone moved
        modes[m] = gWgpuRnd->LastSceneCutoutSampleTest();
    }
    milo::SetLookCutoutSampleTestForInspection(-1);
    gWgpuRnd->EnsureSceneUniformsCurrent();
    const float rule = gWgpuRnd->LastSceneCutoutSampleTest();
    if (prev) prev->Select(); else RndCam::ClearCurrent();
    TheRnd.EndDrawing();
    delete cam;
    EXPECT_FLOAT_EQ(on, (float)milo::kCutoutModeE) << "enhanced world pass: the default mode, E";
    EXPECT_FLOAT_EQ(off, 0.0f) << "hair_edge_off reached the block";
    for (int m = milo::kCutoutModeB; m <= milo::kCutoutModeCentre; m++)
        EXPECT_FLOAT_EQ(modes[m], (float)m) << "mode " << m << " reached the block";
    EXPECT_FLOAT_EQ(rule, (float)milo::kCutoutModeE) << "hair_edge_rule reached the block";
}

// The cutout pipelines that read it build: a world cutout (4 samples, LOD
// bias, fs_cutout with cutoutSampleMask), its text twin (no bias) and the
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

// ---- render tests: the per-sample test as drawn ------------------------------
// The layout above, drawn through the engine's frame (an MSAA world pass: the
// cut materials' pipeline is fs_cutout with kTakesLodBias there) and read
// back. Frames: a warm-up, hair_edge_off twice (the no-change control), then
// hair_edge_on twice (the second, a no-change control for on).
namespace {

struct CutoutFrames {
    // on: the default mode (hair_edge_on); mode[m]: mode m's token (1..4).
    std::vector<uint8_t> off, offAgain, on, onAgain, mode[5];
    double white = 0;
    bool ok = false;
};

CutoutFrames DrawCutoutFrames() {
    CutoutFrames f;
    RndBitmap bmpLines, bmp140, bmp100, bmpAll, bmpBit[4], bmpStrand, bmpFringe2[4];
    std::vector<RndTex *> texs;
    std::vector<RndMat *> mats;
    auto mat = [&](RndMat *m) { mats.push_back(m); return m; };
    RndTex *lines = MakeAlphaTex(bmpLines, [](int x, int) { return (x % 4) < 2 ? 255 : 0; });
    RndTex *c140 = MakeAlphaTex(bmp140, [](int, int) { return 140; });
    RndTex *c100 = MakeAlphaTex(bmp100, [](int, int) { return 100; });
    RndTex *all = MakeAlphaTex(bmpAll, [](int, int) { return 255; });
    RndTex *strand = MakeAlphaTex(bmpStrand, [](int x, int) { return (x % 16) >= 4 && (x % 16) < 8 ? 255 : 0; });
    texs = {lines, c140, c100, all, strand};
    RndTex *bit[4];
    for (int j = 0; j < 4; j++) {
        // Sample j passes alone: 255 on the row above (0), the column right
        // (1), the column left (2) or the row below (3) of each test texel
        // (index 1 of a period of 3; see the layout).
        bit[j] = MakeAlphaTex(bmpBit[j], [j](int x, int y) {
            const bool lit = j == 0 ? y % 3 == 0 : j == 3 ? y % 3 == 2 : j == 1 ? x % 3 == 2 : x % 3 == 0;
            return lit ? 255 : 0;
        });
        texs.push_back(bit[j]);
    }
    RndTex *fringe2[4];
    for (int j = 0; j < 4; j++) {
        fringe2[j] = MakeAlphaTex(bmpFringe2[j], [j](int x, int y) { return Fringe2Alpha(j, x, y); });
        texs.push_back(fringe2[j]);
    }
    {
        CutoutScene scene;
        scene.Add(kWholeFrame, mat(MakeFlatMat(0.0f)));
        scene.Add(kSolid, mat(MakeFlatMat(1.0f)));
        RndMat *hair = mat(MakeCutMat(lines, 150));
        scene.Add(kMin4, hair);
        scene.Add(kMin8, hair);
        RndMat *underSrc = mat(MakeCutMat(c140, 150));
        RndMat *underAlpha = mat(MakeCutMat(c100, 110, BaseMaterial::kBlendSrcAlpha));
        scene.Add(kUnderSrcMin, underSrc);
        scene.Add(kUnderSrcBase, underSrc);
        scene.Add(kUnderAlphaMin, underAlpha);
        scene.Add(kUnderAlphaBase, underAlpha);
        scene.Add(kStrand, mat(MakeCutMat(strand, 217)));
        for (int j = 0; j < 4; j++) scene.Add(Fringe2(j), mat(MakeCutMat(fringe2[j], 102)));
        {
            const PxRect &r = kCentrePassLod0;
            const float s = 1.0f / kTexSize;
            scene.Add(PxRectF{(float)r.x0, (float)r.y0, (float)r.x1, (float)r.y1}, mat(MakeCutMat(bit[2], 191)),
                      0.0f, 0.0f, (r.x1 - r.x0) * s, (r.y1 - r.y0) * s);
        }
        for (int j = 0; j <= 4; j++) {
            RndMat *m = mat(MakeCutMat(j < 4 ? bit[j] : all, 64));
            if (j < 4) {
                // The fringe quad: the same texture origin rule as the cells.
                const float X = (float)FringeX(j), Y = (float)kFringeY, s = 1.0f / kTexSize;
                const PxRectF g = {X, Y, X + kFringeLen, Y + kFringeLen};
                scene.Add(g, m, (float)kBitOrigin * s, (float)kBitOrigin * s,
                          (kBitOrigin + kFringeLen) * s, (kBitOrigin + kFringeLen) * s);
            }
            for (int k = 0; k < 4; k++) {
                // 1 texel per pixel, the texture's origin kBitOrigin px before
                // the cell, so no fetch reaches its border.
                const PxRectF g = BitCellGeometry(k, j);
                const float ox = (float)(BitCellX(j) - kBitOrigin), oy = (float)(BitCellY(k) - kBitOrigin);
                const float s = 1.0f / kTexSize;
                scene.Add(g, m, (g.x0 - ox) * s, (g.y0 - oy) * s, (g.x1 - ox) * s, (g.y1 - oy) * s);
            }
        }
        std::vector<uint8_t> warm;
        milo::SetLookCutoutSampleTestForInspection(0);
        f.ok = scene.Draw(warm) && scene.Draw(f.off) && scene.Draw(f.offAgain);
        milo::SetLookCutoutSampleTestForInspection(1);
        f.ok = f.ok && scene.Draw(f.on) && scene.Draw(f.onAgain);
        for (int m = milo::kCutoutModeB; m <= milo::kCutoutModeCentre; m++) {
            milo::SetLookCutoutModeForInspection(m);
            f.ok = f.ok && scene.Draw(f.mode[m]);
        }
        milo::SetLookCutoutSampleTestForInspection(-1);
        if (f.ok) f.white = scene.Mean(f.off, kSolid);
    }
    for (RndMat *m : mats) delete m;
    for (RndTex *t : texs) delete t;
    return f;
}

// A resolved coverage c of white over black, as the read-back frame shows it
// relative to white: the headless frame is read after DC3's display gamma
// ramp (rndshape::DisplayGamma, DisplayRamp.h: byte i -> (i / 256)^g), so c
// reads c^g (0.25 -> 0.308 and 0.5 -> 0.555 at g = 0.85).
double Shown(double c) {
    const float g = rndshape::DisplayGamma(false);
    return g > 0.0f ? std::pow(c, (double)g) : c;
}

bool FrameIsTheLayoutsSize() {
    const GpuDevice &gpu = gWgpuRnd->Gpu();
    return gpu.OutputWidth() == kWholeFrame.x1 && gpu.OutputHeight() == kWholeFrame.y1;
}

#define CUTOUT_RENDER_PRECONDITIONS()                                                     \
    if (IsSkipped()) return;                                                              \
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (the rule tests ran)";                 \
    if (!gWgpuRnd->Gpu().IsHeadless()) GTEST_SKIP() << "reads the headless frame";        \
    if (!FrameIsTheLayoutsSize()) GTEST_SKIP() << "the layout is for a 1280 x 720 frame"
// The per-sample test is a 4x pass's (kCutoutSamples == 4u).
static_assert(WgpuRnd::kMSAASamples == 4, "the bit-order layout is the standard 4x pattern");

} // namespace

// Enhanced: a strand minified below its base level covers the half of each
// pixel it crosses with the switch on, and nothing with it off (the landed
// coverage tests the averaged 0.498 against the 0.588 cut). The bound is the
// sample positions' arithmetic in the layout: exactly 2 of 4 samples pass in
// every pixel at 4 and at 8 texels per pixel, so the resolved mean is half of
// white (shown through the display ramp, Shown); the bounds leave 5% for the
// present dither and the ramp's 10-bit steps. Sabotages: the mask full
// whenever any sample passes (S1) -- 1.0; every sample at the centre uv (S2)
// -- 0 at 4 texels per pixel (the centre's 0.5 is under the cut); the level
// unclamped (S3) -- 0 at 8 texels per pixel; a boost on each sample (S4) --
// 1.0.
TEST_F(LookCutoutMipTest, AMinifiedStrandCoversTheHalfOfThePixelItCrosses) {
    CUTOUT_RENDER_PRECONDITIONS();
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    const CutoutFrames f = DrawCutoutFrames();
    ASSERT_TRUE(f.ok) << "frame readback failed";
    CutoutScene px;   // the pixel helpers only
    const double off4 = px.Mean(f.off, kMin4) / f.white, on4 = px.Mean(f.on, kMin4) / f.white;
    const double off8 = px.Mean(f.off, kMin8) / f.white, on8 = px.Mean(f.on, kMin8) / f.white;
    // The default (E; lod 2 and 3 here, so C's rules) reads the centre first;
    // here it reads the two level-1 texels' mean, 0.5, under the cut, so it
    // keeps B's mask.
    const double b4 = px.Mean(f.mode[milo::kCutoutModeB], kMin4) / f.white;
    const double c4 = px.Mean(f.mode[milo::kCutoutModeC], kMin4) / f.white;
    const double c8 = px.Mean(f.mode[milo::kCutoutModeC], kMin8) / f.white;
    printf("[ cutout samples ] minified strand: B %.4f, C %.4f / %.4f (4 / 8 texels/px)\n", b4, c4, c8);
    EXPECT_NEAR(b4, Shown(0.5), 0.05) << "mode B at 4 texels/px";
    EXPECT_NEAR(c4, Shown(0.5), 0.05) << "mode C at 4 texels/px";
    EXPECT_NEAR(c8, Shown(0.5), 0.05) << "mode C at 8 texels/px";
    const size_t control = px.Differing(f.off, f.offAgain, kWholeFrame);
    const size_t controlOn = px.Differing(f.on, f.onAgain, kWholeFrame);
    printf("[ cutout samples ] enhanced: white %.1f; 4 texels/px coverage off %.4f on %.4f; "
           "8 texels/px off %.4f on %.4f; off vs off %zu px, on vs on %zu px\n",
           f.white, off4, on4, off8, on8, control, controlOn);
    EXPECT_GT(f.white, 200.0) << "the uncut reference quad was drawn";
    EXPECT_EQ(control, 0u) << "two hair_edge_off frames must be the same frame";
    EXPECT_EQ(controlOn, 0u) << "two hair_edge_on frames must be the same frame";
    EXPECT_LT(off4, 0.05) << "hair_edge_off: alpha 0.498 at mip 2 is under the 0.588 cut";
    EXPECT_LT(off8, 0.05) << "hair_edge_off: alpha 0.498 at mip 3 is under the 0.588 cut";
    EXPECT_NEAR(on4, Shown(0.5), 0.05)
        << "hair_edge_on at 4 texels/px: samples 0 and 2 pass, 2 of 4";
    EXPECT_NEAR(on8, Shown(0.5), 0.05)
        << "hair_edge_on at 8 texels/px: level clamped to 1, samples 2 and 3 pass, 2 of 4";
}

// The floor band's regression test: content authored just under the cut
// stays invisible with the switch on, minified (where the rejected boost lit
// it: 0.549 x 1.5 = 0.82 over 0.588, 0.392 x 1.5 = 0.59 over 0.431) and at the
// base level. Nothing in those quads may rise over the black backdrop; 8
// levels of slack for the present dither, against the boost's ~254 (Src) and
// ~150 (SrcAlpha at 0.59). Sabotage: a boost reintroduced on each sample's
// alpha (S4) -- red on both minified quads.
TEST_F(LookCutoutMipTest, AlphaJustUnderTheCutStaysInvisible) {
    CUTOUT_RENDER_PRECONDITIONS();
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    const CutoutFrames f = DrawCutoutFrames();
    ASSERT_TRUE(f.ok) << "frame readback failed";
    CutoutScene px;
    const struct { const char *name; PxRect r; } cases[] = {
        {"Src 150, alpha 140, mip 2", kUnderSrcMin},
        {"Src 150, alpha 140, mip 0", kUnderSrcBase},
        {"SrcAlpha 110, alpha 100 (PeakLine fade), mip 2", kUnderAlphaMin},
        {"SrcAlpha 110, alpha 100 (PeakLine fade), mip 0", kUnderAlphaBase},
    };
    for (const auto &c : cases) {
        const int maxOn = px.Max(f.on, c.r), maxOff = px.Max(f.off, c.r);
        const int maxC = px.Max(f.mode[milo::kCutoutModeC], c.r);
        const int maxB = px.Max(f.mode[milo::kCutoutModeB], c.r);
        printf("[ cutout samples ] under the cut, %s: max green off %d on (E) %d C %d B %d\n", c.name,
               maxOff, maxOn, maxC, maxB);
        EXPECT_LE(maxOn, 8) << c.name << ": hair_edge_on (E) drew content under the cut";
        EXPECT_LE(maxC, 8) << c.name << ": mode C drew content under the cut";
        EXPECT_LE(maxB, 8) << c.name << ": mode B drew content under the cut";
        EXPECT_LE(maxOff, 8) << c.name << ": hair_edge_off drew content under the cut";
    }
}

// Mask bit i is sample i: in the bit-order matrix only the diagonal lights,
// each to 1/4 of white (one sample of four), and the all-pass column lights
// every row to 1/4 (the geometry covers the one position it should). Bounds
// 1/4 (shown through the display ramp) +- 0.05 lit, under 0.03 dark: the
// arithmetic gives exactly 1/4 and 0. A
// backend whose mask bits were another order, or offsets with a flipped y,
// lights off-diagonal cells. Sabotage: every sample at the centre uv (S2) --
// the centre reads its own texel, 0: the diagonal goes dark.
TEST_F(LookCutoutMipTest, MaskBitIIsSampleI) {
    CUTOUT_RENDER_PRECONDITIONS();
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    const CutoutFrames f = DrawCutoutFrames();
    ASSERT_TRUE(f.ok) << "frame readback failed";
    CutoutScene px;
    for (int which = 0; which < 2; which++) {
    const std::vector<uint8_t> &frame = which == 0 ? f.on : f.mode[milo::kCutoutModeC];
    const char *modeName = which == 0 ? "E" : "C";
    for (int k = 0; k < 4; k++) {
        char line[256];
        int n = snprintf(line, sizeof line, "[ cutout samples ] %s geometry %-7s:", modeName,
                         kSamplePos[k]);
        for (int j = 0; j <= 4; j++) {
            // The test pixels: every third along the line from its start.
            const PxRect r = BitCellPixels(k, j);
            double sum = 0;
            int n3 = 0;
            for (int y = r.y0; y < r.y1; y++)
                for (int x = r.x0; x < r.x1; x++)
                    if ((x - r.x0) % 3 == 0 && (y - r.y0) % 3 == 0) sum += px.G(frame, x, y), n3++;
            const double cov = sum / n3 / f.white;
            n += snprintf(line + n, sizeof line - n, " %s%d %.3f", j < 4 ? "bit" : "all",
                          j < 4 ? j : 4, cov);
            if (j == 4 || j == k)
                EXPECT_NEAR(cov, Shown(0.25), 0.05)
                    << modeName << ": geometry " << kSamplePos[k] << ", alpha passing "
                    << (j < 4 ? "mask bit " + std::to_string(j) : std::string("every sample"))
                    << ": one sample of four should be lit, read " << cov;
            else
                EXPECT_LT(cov, 0.03) << modeName << ": geometry " << kSamplePos[k] << " lit by mask bit " << j
                                     << ": bit " << j << " is not that sample position";
        }
        printf("%s\n", line);
    }
    }
}

// Mode C's first rule, the superset of retail: kStrand (layout) drawn through
// the same fixture under the retail look (fs_main, the original's test) and
// under the enhanced default; every pixel retail lights reads full under C
// and E (>= 0.9 of each look's own white; the arithmetic gives 1), and B, the
// four results alone, reads those pixels at 2 of 4 (the test can tell the
// modes apart). The strand's 16 columns x 64 rows must be lit by retail at
// all, or the comparison is vacuous. Sabotage: C's centre pass returning B's
// mask (S1) -- C reads 0.555 there; every sample at the centre uv (S5) does
// not move this test (the centre passes) but reddens the fringe.
TEST_F(LookCutoutMipTest, TheDefaultDrawsEveryPixelRetailDrawsSolid) {
    CUTOUT_RENDER_PRECONDITIONS();
    CutoutFrames retail, enh;
    {
        ScopedLookOverride r(LookProfile::Retail);
        retail = DrawCutoutFrames();
    }
    {
        ScopedLookOverride e(LookProfile::Enhanced);
        enh = DrawCutoutFrames();
    }
    ASSERT_TRUE(retail.ok && enh.ok) << "frame readback failed";
    CutoutScene px;
    int lit = 0, cShort = 0, eShort = 0;
    double cMin = 1e9, eMin = 1e9, bSum = 0, offStrandMax = 0;
    for (int y = kStrand.y0; y < kStrand.y1; y++)
        for (int x = kStrand.x0; x < kStrand.x1; x++) {
            const double r = px.G(retail.off, x, y) / retail.white;
            const double c = px.G(enh.mode[milo::kCutoutModeC], x, y) / enh.white;
            const double e = px.G(enh.on, x, y) / enh.white;
            if (r < 0.9) {
                offStrandMax = std::max(offStrandMax, c);
                continue;
            }
            lit++;
            cMin = std::min(cMin, c);
            eMin = std::min(eMin, e);
            cShort += c < 0.9;
            eShort += e < 0.9;
            bSum += px.G(enh.mode[milo::kCutoutModeB], x, y) / enh.white;
        }
    const double bMean = lit ? bSum / lit : 0.0;
    printf("[ cutout samples ] superset: retail lights %d px; under C min %.4f (%d short), E min "
           "%.4f (%d short); B mean %.4f; C off retail's strand max %.4f\n",
           lit, cMin, cShort, eMin, eShort, bMean, offStrandMax);
    EXPECT_EQ(lit, 16 * 64) << "retail lights the strand's 16 columns x 64 rows";
    EXPECT_EQ(cShort, 0) << "C: a pixel retail draws read less than full (min " << cMin << ")";
    EXPECT_EQ(eShort, 0) << "E, the default (lod 2: C's rule): a pixel retail draws read less than full (min "
                         << eMin << ")";
    EXPECT_NEAR(bMean, Shown(0.5), 0.05) << "B: samples 1 and 2 read 0.75, under the 0.851 cut";
    EXPECT_LT(offStrandMax, 0.03) << "C: off the strand every fetch reads at most 0.25";
}

// Mode C's second rule, the fringe: where the centre fails and exactly one
// sample position passes, the pixel reads one quarter of white (the quad
// covers all four positions; layout, kFringe). Retail draws nothing there.
// Bound Shown(0.25) +- 0.05, as the bit-order matrix. Sabotages: the fringe
// dropped (S2, a centre fail discards) -- 0; every sample at the centre uv
// (S5) -- 0.
TEST_F(LookCutoutMipTest, TheDefaultDrawsAQuarterWhereOneSamplePasses) {
    CUTOUT_RENDER_PRECONDITIONS();
    CutoutFrames retail, enh;
    {
        ScopedLookOverride r(LookProfile::Retail);
        retail = DrawCutoutFrames();
    }
    {
        ScopedLookOverride e(LookProfile::Enhanced);
        enh = DrawCutoutFrames();
    }
    ASSERT_TRUE(retail.ok && enh.ok) << "frame readback failed";
    CutoutScene px;
    for (int j = 0; j < 4; j++) {
        double c = 0, e = 0, r = 0, ctr = 0;
        int n = 0;
        for (int i = 0; i < kFringeLen; i += 3)
            for (int k = 0; k < kFringeLen; k += 3) {
                const int x = FringeX(j) + i, y = kFringeY + k;
                c += px.G(enh.mode[milo::kCutoutModeC], x, y);
                e += px.G(enh.on, x, y);
                ctr += px.G(enh.mode[milo::kCutoutModeCentre], x, y);
                r += px.G(retail.off, x, y);
                n++;
            }
        c /= n * enh.white;
        e /= n * enh.white;
        ctr /= n * enh.white;
        r /= n * retail.white;
        printf("[ cutout samples ] fringe, sample %d passes alone (%d px): C %.4f E %.4f centre-only "
               "%.4f retail %.4f\n", j, n, c, e, ctr, r);
        EXPECT_NEAR(c, Shown(0.25), 0.05) << "C, sample " << j << " alone: one of four";
        EXPECT_NEAR(e, Shown(0.25), 0.05) << "E (lod 0: B), sample " << j << " alone: one of four";
        EXPECT_LT(ctr, 0.03) << "centre-only: the centre fails, nothing";
        EXPECT_LT(r, 0.03) << "retail: the centre fails, nothing";
    }
    // np-look26 f: the fringe at lod 2 (Fringe2), where the default E takes
    // C's rules. Sabotage: C's fringe dropped (S2) -- E and C read 0 here.
    for (int j = 0; j < 4; j++) {
        const PxRect q = Fringe2(j);
        const double e = px.Mean(enh.on, q) / enh.white, c = px.Mean(enh.mode[milo::kCutoutModeC], q) / enh.white;
        const double b = px.Mean(enh.mode[milo::kCutoutModeB], q) / enh.white;
        const double ctr = px.Mean(enh.mode[milo::kCutoutModeCentre], q) / enh.white;
        const double r = px.Mean(retail.off, q) / retail.white;
        printf("[ cutout samples ] fringe at lod 2, sample %d passes alone: E %.4f C %.4f B %.4f centre-only %.4f "
               "retail %.4f\n", j, e, c, b, ctr, r);
        EXPECT_NEAR(e, Shown(0.25), 0.05) << "E (lod 2: C's fringe), sample " << j << " alone: one of four";
        EXPECT_NEAR(c, Shown(0.25), 0.05) << "C, lod 2, sample " << j << " alone";
        EXPECT_NEAR(b, Shown(0.25), 0.05) << "B, lod 2, sample " << j << " alone";
        EXPECT_LT(ctr, 0.03) << "centre-only, lod 2: the centre (0.25) fails";
        EXPECT_LT(r, 0.03) << "retail, lod 2: the centre (0.25) fails";
    }
}

// np-look26 f: below retail's level 1 the default E is B. kCentrePassLod0
// (layout): the centre passes, two samples fail, so C (centre first) draws the
// lit columns solid and B draws them at 2 of 4; E must be B pixel for pixel.
// Sabotage: E taking C's rules at every lod (S6) -- E reads full.
TEST_F(LookCutoutMipTest, BelowLevelOneTheDefaultIsB) {
    CUTOUT_RENDER_PRECONDITIONS();
    ScopedLookOverride e(LookProfile::Enhanced);
    const CutoutFrames f = DrawCutoutFrames();
    ASSERT_TRUE(f.ok) << "frame readback failed";
    CutoutScene px;
    const PxRect &q = kCentrePassLod0;
    double eSum = 0, bSum = 0, cSum = 0, unlitMax = 0;
    int n = 0;
    for (int y = q.y0 + 4; y < q.y1 - 4; y++)
        for (int x = q.x0 + 6; x < q.x1 - 6; x++) {
            if ((x - q.x0) % 3 != 0) {
                unlitMax = std::max(unlitMax, px.G(f.on, x, y) / f.white);
                continue;
            }
            eSum += px.G(f.on, x, y);
            bSum += px.G(f.mode[milo::kCutoutModeB], x, y);
            cSum += px.G(f.mode[milo::kCutoutModeC], x, y);
            n++;
        }
    const double eM = eSum / n / f.white, bM = bSum / n / f.white, cM = cSum / n / f.white;
    const size_t eVsB = px.Differing(f.on, f.mode[milo::kCutoutModeB], q);
    printf("[ cutout samples ] lod 0, centre passes, 2 samples fail (%d px): E %.4f B %.4f C %.4f; E vs B "
           "%zu px differ; unlit max %.4f\n", n, eM, bM, cM, eVsB, unlitMax);
    EXPECT_NEAR(eM, Shown(0.5), 0.05) << "E at lod 0 is B: samples 0 and 3 pass, 2 of 4";
    EXPECT_NEAR(bM, Shown(0.5), 0.05) << "B: 2 of 4";
    EXPECT_GT(cM, 0.9) << "C: the centre passes, solid (the test tells C from B)";
    EXPECT_EQ(eVsB, 0u) << "E must equal B below lod 1";
    EXPECT_LT(unlitMax, 0.03) << "unlit columns: every fetch under the cut";
}

// Retail: the original's alpha test is fs_main and reads no per-sample test,
// so even hair_edge_on (which puts 1 in the scene block) leaves the frame as it
// was -- 0 px differ. Sabotage: the bool on under retail (S5) -- red in
// OnlyTheEnhancedLookTestsEachSample; this frame stays still by construction
// (no retail pipeline is fs_cutout). The test process's world pipelines take
// the LOD bias whatever the scoped look (PipelineManager::
// SetMaterialTakesLodBias is set at Init from the startup look, enhanced), so
// kTakesLodBias does not hide the test from this frame.
TEST_F(LookCutoutMipTest, TheRetailCutIsUnmovedByTheSwitch) {
    CUTOUT_RENDER_PRECONDITIONS();
    ScopedLookOverride retail(LookProfile::Retail);
    const CutoutFrames f = DrawCutoutFrames();
    ASSERT_TRUE(f.ok) << "frame readback failed";
    CutoutScene px;
    const size_t moved = px.Differing(f.off, f.on, kWholeFrame);
    const size_t control = px.Differing(f.off, f.offAgain, kWholeFrame);
    size_t movedByMode[5] = {0, 0, 0, 0, 0};
    for (int m = milo::kCutoutModeB; m <= milo::kCutoutModeCentre; m++) {
        movedByMode[m] = px.Differing(f.off, f.mode[m], kWholeFrame);
        EXPECT_EQ(movedByMode[m], 0u) << "retail: mode token " << m << " moved pixels";
    }
    printf("[ cutout samples ] retail moved by B %zu, C %zu, E %zu, centre %zu px\n", movedByMode[1],
           movedByMode[2], movedByMode[3], movedByMode[4]);
    printf("[ cutout samples ] retail: white %.1f; 4 texels/px mean off %.1f on %.1f; "
           "moved by the switch %zu px; off vs off %zu px\n",
           f.white, px.Mean(f.off, kMin4), px.Mean(f.on, kMin4), moved, control);
    EXPECT_GT(f.white, 200.0) << "the uncut reference quad was drawn";
    EXPECT_EQ(control, 0u) << "two hair_edge_off frames must be the same frame";
    EXPECT_EQ(moved, 0u) << "retail's cut is fs_main: hair_edge_on must not move a pixel";
}

// ---- np-look26 f: a look switch reads the levels the switched look uploads ----
// Textures are uploaded once, with the startup look's level count (Tex_Wgpu,
// Look().bitmapMipLevels: retail 2, enhanced the whole chain). So
// {rnd native_look retail} in a run started enhanced used to sample the whole
// chain through retail's sampler, which is not retail wherever a texture is
// minified past level 1 (26e: oblio04's hair mass read solid where real retail
// has holes). ApplyLookSampling now clamps world samplers to the switched
// look's level count (milo::WorldTexLodMaxLevels). Here: a hand-filled chain
// whose levels 0 and 1 are grey 200 and levels 2 and deeper grey 40, drawn at
// 8 texels per pixel (lod 3) as a plain opaque quad. Reference: the same quad
// in a scope started retail (two levels uploaded). Switched: uploaded in a
// scope started enhanced, then the retail look selected by the inspection
// call. The two must agree within 1 level per channel over the quad; the
// enhanced frame itself must read the deep levels (so the test can fail).
// Sabotage: the clamp removed (S7) -- the switched frame reads ~40 shown.
namespace {
class LookSwitchLevelsTest : public LookCutoutMipTest {};

const PxRect kDeepQuad = {1000, 40, 1032, 72};   // 256 texels over 32 px: lod 3

RndTex *MakeTwoToneChain(RndBitmap &bmp) {
    std::vector<unsigned char> px(kTexSize * kTexSize * 4, 0xFF);
    bmp.Create(kTexSize, kTexSize, 0, 32, 0, nullptr, px.data(), nullptr);
    bmp.GenerateMips();
    int level = 0;
    for (RndBitmap *m = &bmp; m; m = m->nextMip(), level++) {
        const unsigned char v = level <= 1 ? 200 : 40;
        unsigned char *p = m->Pixels();
        for (int i = 0; i < m->Width() * m->Height(); i++) {
            p[i * 4 + 0] = p[i * 4 + 1] = p[i * 4 + 2] = v;
            p[i * 4 + 3] = 0xFF;
        }
    }
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(bmp, nullptr, true, RndTex::kRegular);
    tex->PresyncBitmap();
    return tex;
}

struct DeepFrames {
    std::vector<uint8_t> reference, enhanced, switched, switchedBack;
    int levels = 0;   // levels in the hand-filled chain
    bool ok = false;
};

// Draws the quad (warm-up frame first after every look change: a render scale
// change resizes targets on the next frame).
bool DrawDeep(CutoutScene &scene, std::vector<uint8_t> &out) {
    std::vector<uint8_t> warm;
    return scene.Draw(warm) && scene.Draw(out);
}

DeepFrames DrawDeepFrames() {
    DeepFrames f;
    f.ok = true;
    {
        ScopedLookOverride r(LookProfile::Retail);   // a run started retail
        RndBitmap bmp;
        RndTex *tex = MakeTwoToneChain(bmp);
        for (RndBitmap *m = &bmp; m; m = m->nextMip()) f.levels++;
        RndMat *mat = MakeFlatMat(1.0f), *black = MakeFlatMat(0.0f);
        mat->SetDiffuseTex(tex);
        {
            CutoutScene scene;
            scene.Add(kWholeFrame, black);
            scene.Add(kDeepQuad, mat);
            f.ok = f.ok && DrawDeep(scene, f.reference);
        }
        delete black;
        delete mat;
        delete tex;
    }
    {
        ScopedLookOverride e(LookProfile::Enhanced);   // a run started enhanced
        RndBitmap bmp;
        RndTex *tex = MakeTwoToneChain(bmp);
        RndMat *mat = MakeFlatMat(1.0f), *black = MakeFlatMat(0.0f);
        mat->SetDiffuseTex(tex);
        {
            CutoutScene scene;
            scene.Add(kWholeFrame, black);
            scene.Add(kDeepQuad, mat);
            f.ok = f.ok && DrawDeep(scene, f.enhanced);
            f.ok = f.ok && milo::SetLookProfileForInspection(LookProfile::Retail);   // {rnd native_look retail}
            f.ok = f.ok && DrawDeep(scene, f.switched);
            f.ok = f.ok && milo::SetLookProfileForInspection(LookProfile::Enhanced);
            f.ok = f.ok && DrawDeep(scene, f.switchedBack);
        }
        delete black;
        delete mat;
        delete tex;
    }
    return f;
}

int MaxChannelDiff(const CutoutScene &px, const std::vector<uint8_t> &a, const std::vector<uint8_t> &b,
                   const PxRect &r) {
    int m = 0;
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            for (int c = 0; c < 3; c++) {
                const size_t i = ((size_t)y * px.W + x) * 4 + c;
                m = std::max(m, std::abs((int)a[i] - (int)b[i]));
            }
    return m;
}
} // namespace

// The rule itself: no clamp for any look in a run that never switches (every
// launch, and rb3 / rb3-xenon, whose Native look cannot switch); a clamp to
// retail's two levels after a switch from enhanced; none for the reverse (a
// run started retail has two levels to read; the limit in LookProfile.h).
TEST_F(LookSwitchLevelsTest, TheClampIsOnlyForASwitchToFewerLevels) {
    if (IsSkipped()) return;
    for (LookProfile p : {LookProfile::Native, LookProfile::Retail, LookProfile::Enhanced}) {
        ScopedLookOverride o(p);
        EXPECT_EQ(milo::StartupBitmapMipLevels(), Look().bitmapMipLevels) << "a scope stands for a run started in it";
        EXPECT_EQ(milo::WorldTexLodMaxLevels(Look(), milo::StartupBitmapMipLevels()), 0)
            << milo::LookProfileName(p) << ": never switched, never clamped";
    }
    ScopedLookOverride e(LookProfile::Enhanced);
    const int startedEnhanced = milo::StartupBitmapMipLevels();
    ASSERT_TRUE(milo::SetLookProfileForInspection(LookProfile::Retail));
    EXPECT_EQ(milo::StartupBitmapMipLevels(), startedEnhanced) << "the token does not re-upload";
    EXPECT_EQ(milo::WorldTexLodMaxLevels(Look(), startedEnhanced), 2) << "retail's two levels";
    {
        ScopedLookOverride r(LookProfile::Retail);
        const int startedRetail = milo::StartupBitmapMipLevels();
        ASSERT_TRUE(milo::SetLookProfileForInspection(LookProfile::Enhanced));
        EXPECT_EQ(milo::WorldTexLodMaxLevels(Look(), startedRetail), 0) << "the reverse: nothing to clamp";
    }
    EXPECT_EQ(milo::StartupBitmapMipLevels(), startedEnhanced) << "the inner scope restored it";
}

TEST_F(LookSwitchLevelsTest, ARetailSwitchReadsRetailsTwoLevels) {
    CUTOUT_RENDER_PRECONDITIONS();
    const DeepFrames f = DrawDeepFrames();
    ASSERT_TRUE(f.ok) << "frame readback or look switch failed";
    CutoutScene px;
    const double ref = px.Mean(f.reference, kDeepQuad), enh = px.Mean(f.enhanced, kDeepQuad);
    const double sw = px.Mean(f.switched, kDeepQuad), back = px.Mean(f.switchedBack, kDeepQuad);
    const int swVsRef = MaxChannelDiff(px, f.switched, f.reference, kDeepQuad);
    const int backVsEnh = MaxChannelDiff(px, f.switchedBack, f.enhanced, kDeepQuad);
    printf("[ look switch ] chain of %d levels at lod 3: mean green started-retail %.1f, enhanced %.1f, "
           "enhanced->retail %.1f, ->enhanced again %.1f; max channel diff switched vs started-retail %d, "
           "back vs enhanced %d\n", f.levels, ref, enh, sw, back, swVsRef, backVsEnh);
    EXPECT_GE(f.levels, 4) << "the chain must have levels 2 and deeper";
    EXPECT_GT(ref - enh, 50.0) << "the enhanced frame must read the deep levels (else the test is vacuous)";
    EXPECT_LE(swVsRef, 1) << "enhanced->retail must read what a run started retail reads";
    EXPECT_LE(backVsEnh, 1) << "back to enhanced: the whole chain again";
}
