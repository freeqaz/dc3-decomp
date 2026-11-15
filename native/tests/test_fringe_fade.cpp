// DC3 draws no alpha fringe fade (np-gfx-sharp item 8).
//
// The standard shader's `rgb *= a / 0.1` for a < 0.1 (kAlphaFringeFadeStart)
// is a native heuristic from the port's first shader; the image has nothing
// like it. The shipped standard pixel shaders write texel * colour (0x10:
// `mul oC0, r0, r2`; 0x20010 adds the emissive term to rgb only) and leave
// what alpha means to the blend. On DC3 the fade squared every low-alpha
// SrcAlpha layer -- dclive's peak sky cubes (DLV_PeakStageCube0N.mat, mat
// alpha 0.10-0.14 times a texel alpha averaging 0.12-0.50) drew at a third to
// four fifths of Xenia's light -- and erased alpha-0 Add layers outright
// (DLV_PeakGlass_transparent.mat: alpha 0, blend Add). rndshape::
// kAlphaFringeFade = false makes PipelineManager give every DC3 pipeline the
// existing kSkipFringeFade = 1 override, so no pipeline variant is added.
// rb3 and rb3-xenon keep the fade (their shapes set it true).
//
// The assertions are the image's arithmetic: SrcAlpha is src * a + dst *
// (1 - a) on the shader's unattenuated colour, Add is src + dst whatever the
// alpha.
//
// Sabotage tried:
//  - rndshape::kAlphaFringeFade back to true in RndShape_DC3.h -> the
//    static_assert below stops the build; with it removed,
//    DC3ConfiguresNoFringeFade fails, SrcAlphaBelowTenthKeepsItsLight reads
//    the faded 6 for 13, AddAtAlphaZeroStillAdds reads 0 against 127;
//  - PipelineManager::CreatePipeline back to BlendSkipsFringeFade(key.blend)
//    alone -> the two readback tests fail as above (the flag is set but no
//    pipeline reads it).

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "gfx/PipelineManager.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"

#include "obj/Object.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Cam.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <cstdint>
#include <vector>

static_assert(!rndshape::kAlphaFringeFade, "DC3 draws no alpha fringe fade");

namespace {

class FringeFadeTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

} // namespace

TEST_F(FringeFadeTest, DC3ConfiguresNoFringeFade) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    const PipelineManager &pm = gWgpuRnd->Pipelines();
    EXPECT_FALSE(pm.AlphaFringeFade());
    for (int blend : {BaseMaterial::kBlendDest, BaseMaterial::kBlendSrc,
                      BaseMaterial::kBlendAdd, BaseMaterial::kBlendSrcAlpha,
                      BaseMaterial::kBlendSrcAlphaAdd, BaseMaterial::kBlendSubtract,
                      BaseMaterial::kBlendMultiply, BaseMaterial::kPreMultAlpha,
                      BaseMaterial::kScreen, BaseMaterial::kLighten, BaseMaterial::kDarken})
        EXPECT_TRUE(pm.SkipsFringeFade(blend)) << "blend " << blend << " still fades";
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET
namespace {

// A quad at y = 10 in front of an identity camera (Milo: +y forward, +z up),
// spanning x0..x1 and all of z.
RndMesh *MakeQuad(float x0, float x1, RndMat *mat) {
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    mesh->Verts().resize(4);
    const float xs[4] = {x0, x1, x1, x0};
    const float zs[4] = {-100.0f, -100.0f, 100.0f, 100.0f};
    for (int i = 0; i < 4; i++) {
        RndMesh::Vert &v = mesh->Verts()[i];
        v.pos.Set(xs[i], 10.0f, zs[i]);
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

RndMat *MakeMat(float grey, float alpha, BaseMaterial::Blend blend) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetColor(grey, grey, grey);
    mat->SetAlpha(alpha);
    mat->SetBlend(blend);
    mat->SetPreLit(true);
    mat->SetZMode(kZModeDisable);
    mat->SetCull(kCullNone);
    return mat;
}

// Draws a black opaque backdrop, then `left` over x < 0 and `right` over
// x > 0, and reads the green channel (never swapped by a BGRA target) at the
// middle of each half.
struct TwoQuads {
    RndTex *tex = nullptr;
    RndCam *cam = nullptr;
    RndMat *black = nullptr;
    RndMesh *backdrop = nullptr, *qLeft = nullptr, *qRight = nullptr;

    TwoQuads(RndMat *left, RndMat *right) {
        tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(64, 48, 32, RndTex::kRendered, false, nullptr);
        cam = Hmx::Object::New<RndCam>();
        cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
        cam->SetTargetTex(tex);
        black = MakeMat(0.0f, 1.0f, BaseMaterial::kBlendSrc);
        backdrop = MakeQuad(-1000.0f, 1000.0f, black);
        qLeft = MakeQuad(-1000.0f, 0.0f, left);
        qRight = MakeQuad(0.0f, 1000.0f, right);
    }
    ~TwoQuads() {
        cam->SetTargetTex(nullptr);
        delete qRight;
        delete qLeft;
        delete backdrop;
        delete black;
        delete cam;
        delete tex;
    }
    bool DrawAndRead(int &l, int &r) {
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        backdrop->DrawShowing();
        qLeft->DrawShowing();
        qRight->DrawShowing();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (!ReadbackGpuRenderTarget(tex, px, w, h) || w != 64 || h != 48) return false;
        l = px[((h / 2) * w + w / 4) * 4 + 1];
        r = px[((h / 2) * w + 3 * w / 4) * 4 + 1];
        return true;
    }
};

} // namespace

// A white SrcAlpha layer at alpha 0.05 over black is 0.05 * 255 = 12.75. The
// fade made it 0.05 * (0.05 / 0.1) * 255 = 6.4. The alpha-0.5 half is above
// the fade's range either way: it shows the blend path itself is right.
TEST_F(FringeFadeTest, SrcAlphaBelowTenthKeepsItsLight) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    RndMat *low = MakeMat(1.0f, 0.05f, BaseMaterial::kBlendSrcAlpha);
    RndMat *half = MakeMat(1.0f, 0.5f, BaseMaterial::kBlendSrcAlpha);
    int l = -1, r = -1;
    {
        TwoQuads q(low, half);
        ASSERT_TRUE(q.DrawAndRead(l, r)) << "no GPU render target";
        ASSERT_TRUE(q.DrawAndRead(l, r));
    }
    EXPECT_NEAR(r, 128, 3) << "SrcAlpha 0.5 white over black";
    EXPECT_NEAR(l, 13, 2) << "SrcAlpha 0.05 white over black (the fringe fade gives ~6)";
    delete half;
    delete low;
}

// An Add layer adds its colour whatever its alpha: the image's Add is One/One
// and the shipped shader leaves rgb unweighted. DLV_PeakGlass_transparent.mat
// is exactly this (alpha 0, Add, an environment map); the fade drew it black,
// i.e. not at all. Grey 0.5 keeps both halves off the 255 clamp.
TEST_F(FringeFadeTest, AddAtAlphaZeroStillAdds) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    RndMat *zero = MakeMat(0.5f, 0.0f, BaseMaterial::kBlendAdd);
    RndMat *one = MakeMat(0.5f, 1.0f, BaseMaterial::kBlendAdd);
    int l = -1, r = -1;
    {
        TwoQuads q(zero, one);
        ASSERT_TRUE(q.DrawAndRead(l, r)) << "no GPU render target";
        ASSERT_TRUE(q.DrawAndRead(l, r));
    }
    EXPECT_NEAR(r, 128, 3) << "Add alpha 1, grey 0.5 over black";
    EXPECT_NEAR(l, r, 2) << "Add alpha 0 must add the same light (the fringe fade gives 0)";
    delete one;
    delete zero;
}
#endif // MILO_READBACK_GPU_RENDER_TARGET
