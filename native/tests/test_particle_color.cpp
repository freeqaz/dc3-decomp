// test_particle_color.cpp -- particles are coloured the way retail's Xbox
// `particles` shader colours them (lane np-gfx-blend).
//
// Retail (xbox_shaders `particles`, permutation 0x110, disassembled with
// xenia's shader compiler; RB3's Xbox container has the same lines):
//
//     VS  mul r0, r2, c1        particle colour x c1 (kVS_AmbientColor)
//         mul o2, r0, c0        ... x c0 (kVS_Color)
//     PS  mul oC0, r0, r2       texel x that colour
//
// and the intensify permutation (option bit 53) first multiplies the texel by
// c5.y.  RndShaderParticles::Select calls NgMat::SetupShader(false, true):
// SetupAmbient loads c1 with RndEnviron::Current()->AmbientColor() when the
// material has use_environ and with white otherwise, SetRegularShaderConst
// loads c0 with the material colour (alpha premultiplied for kPreMultAlpha)
// and c5.y with mIntensify + 1.  All three are 100% matched (name_check).
//
// The engine's billboard path drew the particle colour as authored, so a
// particle system lit by a dark environ -- streetside's manhole steam under
// Steam.env, ambient (0.055, 0.067, 0.063) -- drew ~16x too bright: the white
// smoke column over the right-hand backup dancer.
//
// The first tests check the colour model; the last renders a one-particle
// system into a render target and reads the pixel back, so it fails if
// DrawParticlesBillboard stops applying the model (needs a GPU; skips without).
//
// Sabotage tried (np-gfx-blend): DrawParticlesBillboard's DC3 branch back to
// "colour as authored" (the pre-fix code) -> RenderedParticleTakesMaterialAndAmbient
// fails, pixel 255,255,255 where 128,64,32 is expected; ParticleShaderColor
// without the ambient term -> MaterialColourTimesEnvironAmbient and the render
// test fail (pixel 255,127,64).

#include "test_helpers.h"

#include "platform/Part_Wgpu.h"
#include "platform/Rnd_Wgpu.h"

#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Mat.h"
#include "rndobj/Part.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <cmath>
#include <vector>

namespace {

class ParticleColorTest : public EngineTestFixture {};

constexpr float kEps = 1.0e-5f;

RndMat *MakeMat(float r, float g, float b, float a, BaseMaterial::Blend blend, bool useEnv) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetColor(r, g, b);
    mat->SetAlpha(a);
    mat->SetBlend(blend);
    mat->SetUseEnv(useEnv);
    return mat;
}

RndEnviron *MakeEnv(float r, float g, float b, float a) {
    RndEnviron *env = Hmx::Object::New<RndEnviron>();
    env->SetAmbientColor(Hmx::Color(r, g, b));
    env->SetProperty("ambient_alpha", DataNode(a));
    return env;
}

void ExpectColor(const Hmx::Color &c, float r, float g, float b, float a) {
    EXPECT_NEAR(c.red, r, kEps);
    EXPECT_NEAR(c.green, g, kEps);
    EXPECT_NEAR(c.blue, b, kEps);
    EXPECT_NEAR(c.alpha, a, kEps);
}

} // namespace

// c0 x c1: the material colour times the environ ambient, alpha included.
TEST_F(ParticleColorTest, MaterialColourTimesEnvironAmbient) {
    RndMat *mat = MakeMat(0.5f, 0.25f, 1.0f, 0.8f, BaseMaterial::kBlendSrcAlphaAdd, true);
    RndEnviron *env = MakeEnv(0.1f, 0.2f, 0.4f, 0.5f);
    ExpectColor(ParticleShaderColor(mat, env), 0.05f, 0.05f, 0.4f, 0.4f);
    delete env;
    delete mat;
}

// use_environ 0: SetupAmbient loads white, so only the material colour counts.
TEST_F(ParticleColorTest, NoEnvironAmbientWithoutUseEnviron) {
    RndMat *mat = MakeMat(0.5f, 0.25f, 1.0f, 0.8f, BaseMaterial::kBlendSrcAlphaAdd, false);
    RndEnviron *env = MakeEnv(0.1f, 0.2f, 0.4f, 0.5f);
    ExpectColor(ParticleShaderColor(mat, env), 0.5f, 0.25f, 1.0f, 0.8f);
    ExpectColor(ParticleShaderColor(mat, nullptr), 0.5f, 0.25f, 1.0f, 0.8f);
    delete env;
    delete mat;
}

// kPreMultAlpha: SetRegularShaderConst premultiplies c0's rgb by its alpha.
TEST_F(ParticleColorTest, PreMultAlphaPremultipliesMaterialColour) {
    RndMat *mat = MakeMat(0.5f, 0.25f, 1.0f, 0.5f, BaseMaterial::kPreMultAlpha, false);
    ExpectColor(ParticleShaderColor(mat, nullptr), 0.25f, 0.125f, 0.5f, 0.5f);
    delete mat;
}

// intensify: the texel is multiplied by c5.y = 2, all four channels.
TEST_F(ParticleColorTest, IntensifyDoubles) {
    RndMat *mat = MakeMat(0.5f, 0.25f, 0.125f, 0.25f, BaseMaterial::kBlendSrcAlphaAdd, false);
    mat->SetProperty("intensify", DataNode(1));
    ExpectColor(ParticleShaderColor(mat, nullptr), 1.0f, 0.5f, 0.25f, 0.5f);
    delete mat;
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET
// One white particle, opaque kBlendSrc material (1, 0.5, 0.25) under an environ
// whose ambient is 0.5 grey, drawn into a 64x48 target: the pixel under the
// particle is white x material x ambient.
TEST_F(ParticleColorTest, RenderedParticleTakesMaterialAndAmbient) {
    if (IsSkipped()) return;
    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
        GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";

    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(64, 48, 32, RndTex::kRendered, false, nullptr);

    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetLocalRot(Hmx::Matrix3::GetIdentity()); // looks down +y
    cam->SetLocalPos(Vector3(0.0f, 0.0f, 0.0f));
    cam->SetFrustum(1.0f, 1000.0f, 0.8f, 1.0f);
    cam->SetTargetTex(tex);

    RndMat *mat = MakeMat(1.0f, 0.5f, 0.25f, 1.0f, BaseMaterial::kBlendSrc, true);
    mat->SetZMode(kZModeDisable);
    RndEnviron *env = MakeEnv(0.5f, 0.5f, 0.5f, 1.0f);

    RndParticleSys *sys = Hmx::Object::New<RndParticleSys>();
    sys->SetMat(mat);
    sys->SetPool(4, RndParticleSys::kBasic);
    sys->SetBoxExtent(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
    sys->SetLocalPos(Vector3(0.0f, 10.0f, 0.0f));
    PartOverride po;
    po.mask = 1 | 2 | 4 | 0x10; // life, speed, size, start colour
    po.life = 1000.0f;
    po.speed = 0.0f;
    po.size = 30.0f; // covers the whole view at 10 units
    po.startColor = Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f);
    sys->ExplicitParticles(1, true, po);
    ASSERT_NE(sys->ActiveParticles(), nullptr) << "no particle was emitted";

    TheRnd.BeginDrawing();
    RndCam *prev = RndCam::Current();
    cam->Select();
    env->Select(nullptr);
    sys->DrawShowing();
    cam->SetTargetTex(nullptr);
    if (prev) prev->Select();
    TheRnd.EndDrawing();

    std::vector<uint8_t> px;
    int w = 0, h = 0;
    ASSERT_TRUE(ReadbackGpuRenderTarget(tex, px, w, h)) << "no GPU render target";
    ASSERT_EQ(w * h, 64 * 48);
    const uint8_t *c = &px[((h / 2) * w + w / 2) * 4];
    // white x (1, 0.5, 0.25) x 0.5 = (0.5, 0.25, 0.125) -> (128, 64, 32), +-3.
    // An RGBA or BGRA target: red and blue are the two that can swap.
    int r = c[0], g = c[1], b = c[2];
    if (r < b) std::swap(r, b);
    EXPECT_NEAR(r, 128, 3) << "pixel " << (int)c[0] << "," << (int)c[1] << "," << (int)c[2];
    EXPECT_NEAR(g, 64, 3) << "pixel " << (int)c[0] << "," << (int)c[1] << "," << (int)c[2];
    EXPECT_NEAR(b, 32, 3) << "pixel " << (int)c[0] << "," << (int)c[1] << "," << (int)c[2];

    delete sys;
    delete env;
    delete mat;
    delete cam;
    delete tex;
}
#endif
