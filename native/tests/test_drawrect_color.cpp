// test_drawrect_color.cpp -- a material rect is coloured the way retail's Xbox
// DrawRect colours it (lane np-gfx-blend).
//
// DxRnd::DrawRect(rect, colour, mat, ...) selects kDrawRectShader, and
// RndShaderDrawRect::Select runs it as the `standard` shader with
// c1 (kVS_AmbientColor) = white and c0 = the material colour, alpha
// premultiplied for kPreMultAlpha (NgMat::SetupShader). xbox_shaders
// `standard` (DC3 and RB3 Xbox carry the same lines), disassembled with xenia's
// shader compiler:
//
//     0x110 (diffuse, prelit)   VS  mul r1, r1, c1 ; mul o2, r1, c0
//     0x010 (diffuse)           VS  mul o2, c1, c0
//                               PS  mul oC0, r0, r2      texel x colour
//
// so a prelit material multiplies the colour argument by the material colour
// and any other material draws the material colour alone. All three functions
// are 100% matched (name_check).
//
// The engine drew the colour argument alone. RndFlare passes white and carries
// its fade in the material colour (environ ambient x fade, material alpha 0.33
// on houseparty's flare.mat), so every flare drew at full texture strength:
// the white disc over houseparty's chandelier.
//
// Sabotage tried (np-gfx-blend): WgpuRnd's colour DrawRect overload back to
// "forward the argument" (the pre-fix code) -> NonPrelitMaterialDrawsItsColour
// reads 255,255,255 where 255,128,64 is expected and PrelitMaterialScalesTheArgument
// 127,127,127 where 128,64,32 is; DrawRectMaterialColor without the
// premultiply -> PreMultAlphaPremultiplies fails.

#include "test_helpers.h"

#include "gfx/DrawRect2D.h"
#include "platform/Rnd_Wgpu.h"

#include "math/Geo.h"
#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Mat.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <utility>
#include <vector>

namespace {

class DrawRectColorTest : public EngineTestFixture {};

constexpr float kEps = 1.0e-5f;

RndMat *MakeMat(float r, float g, float b, float a, BaseMaterial::Blend blend, bool prelit) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetColor(r, g, b);
    mat->SetAlpha(a);
    mat->SetBlend(blend);
    mat->SetPreLit(prelit);
    mat->SetZMode(kZModeDisable);
    return mat;
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET
// Draw one full-target rect with `mat` and `arg` into a 64x48 target and return
// the centre pixel, red/blue ordered so red >= blue (RGBA or BGRA targets).
bool DrawAndRead(RndMat *mat, const Hmx::Color &arg, int &r, int &g, int &b) {
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(64, 48, 32, RndTex::kRendered, false, nullptr);
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetTargetTex(tex);

    TheRnd.BeginDrawing();
    RndCam *prev = RndCam::Current();
    cam->Select();
    Hmx::Rect rect(0.0f, 0.0f, (float)TheRnd.Width(), (float)TheRnd.Height());
    TheRnd.DrawRect(rect, arg, mat, nullptr, nullptr);
    cam->SetTargetTex(nullptr);
    if (prev) prev->Select();
    TheRnd.EndDrawing();

    std::vector<uint8_t> px;
    int w = 0, h = 0;
    bool ok = ReadbackGpuRenderTarget(tex, px, w, h) && w * h == 64 * 48;
    if (ok) {
        const uint8_t *c = &px[((h / 2) * w + w / 2) * 4];
        r = c[0];
        g = c[1];
        b = c[2];
        if (r < b) std::swap(r, b);
    }
    delete cam;
    delete tex;
    return ok;
}
#endif

} // namespace

TEST_F(DrawRectColorTest, MaterialColourWithAlpha) {
    RndMat *mat = MakeMat(0.5f, 0.25f, 1.0f, 0.33f, BaseMaterial::kBlendSrcAlphaAdd, false);
    Hmx::Color k = DrawRectMaterialColor(mat);
    EXPECT_NEAR(k.red, 0.5f, kEps);
    EXPECT_NEAR(k.green, 0.25f, kEps);
    EXPECT_NEAR(k.blue, 1.0f, kEps);
    EXPECT_NEAR(k.alpha, 0.33f, kEps);
    delete mat;
}

TEST_F(DrawRectColorTest, PreMultAlphaPremultiplies) {
    RndMat *mat = MakeMat(0.5f, 0.25f, 1.0f, 0.5f, BaseMaterial::kPreMultAlpha, false);
    Hmx::Color k = DrawRectMaterialColor(mat);
    EXPECT_NEAR(k.red, 0.25f, kEps);
    EXPECT_NEAR(k.green, 0.125f, kEps);
    EXPECT_NEAR(k.blue, 0.5f, kEps);
    EXPECT_NEAR(k.alpha, 0.5f, kEps);
    delete mat;
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET
// Not prelit: `mul o2, c1, c0` -- the material colour alone; the white
// argument (RndFlare's) does not enter.
TEST_F(DrawRectColorTest, NonPrelitMaterialDrawsItsColour) {
    if (IsSkipped()) return;
    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
        GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    RndMat *mat = MakeMat(1.0f, 0.5f, 0.25f, 1.0f, BaseMaterial::kBlendSrc, false);
    int r = 0, g = 0, b = 0;
    ASSERT_TRUE(DrawAndRead(mat, Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f), r, g, b))
        << "no GPU render target";
    EXPECT_NEAR(r, 255, 3) << "pixel " << r << "," << g << "," << b;
    EXPECT_NEAR(g, 128, 3) << "pixel " << r << "," << g << "," << b;
    EXPECT_NEAR(b, 64, 3) << "pixel " << r << "," << g << "," << b;
    delete mat;
}

// Prelit: `mul o2, r1, c0` -- the argument times the material colour.
TEST_F(DrawRectColorTest, PrelitMaterialScalesTheArgument) {
    if (IsSkipped()) return;
    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
        GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    RndMat *mat = MakeMat(1.0f, 0.5f, 0.25f, 1.0f, BaseMaterial::kBlendSrc, true);
    int r = 0, g = 0, b = 0;
    ASSERT_TRUE(DrawAndRead(mat, Hmx::Color(0.5f, 0.5f, 0.5f, 1.0f), r, g, b))
        << "no GPU render target";
    EXPECT_NEAR(r, 128, 3) << "pixel " << r << "," << g << "," << b;
    EXPECT_NEAR(g, 64, 3) << "pixel " << r << "," << g << "," << b;
    EXPECT_NEAR(b, 32, 3) << "pixel " << r << "," << g << "," << b;
    delete mat;
}
#endif
