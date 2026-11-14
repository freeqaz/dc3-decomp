// test_retail_lighting.cpp -- DC3's retail light model behind the rndshape
// seam (milo-native-engine DC3SceneLighting.cpp, docs/native/dc3-retail-lighting.md
// in the engine).
//
// The image lights a standard draw with NgEnviron::Select's register set:
// c1 = the environ's ambient as authored, up to three point lights with a
// linear falloff (c62+i, c66+i), the six-face approx box map c80..c85 that
// NgEnviron::UpdateApproxLighting uploads, exponential fog (c90/c91), and at
// most one projected light (slot 3) whose projected_blend 1 form is the dancer
// floor shadow. The native port used to replace all of that with a heuristic
// (0.08 ambient floor, light priority by name, top-4 brightest, point lights
// turned into directionals, synthetic fills, an energy cap). These tests pin
// the retail answers:
//
//   * WriteSceneLighting claims the frame (retailLighting 2) and passes the
//     environ's ambient, point lights and fog through unchanged;
//   * FillMeshApproxLighting copies what NgEnviron uploaded to c80..c85 and
//     the material's recv_proj_lights;
//   * a projected_blend 1 light publishes its cone, and the caster pass
//     hands back exactly the shadow objects with a clip transform that lands
//     each caster where a receiver's (c95 . p, c96 . p) / (c97 . p) reads it;
//     the map is only reported valid on the frame after it was drawn.
//
// CPU only: no GPU, no game data.

#include "test_helpers.h"

#include "gfx/UniformStructs.h"
#include "math/Mtx.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "platform/rndshape/RndShape.h"
#include "rndobj/Env.h"
#include "rndobj/Lit.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/Tex.h"

#include <cmath>
#include <cstring>

class RetailLightingTest : public EngineTestFixture {
protected:
    void SetUp() override {
        mEnv = Hmx::Object::New<RndEnviron>();
        ASSERT_NE(mEnv, nullptr);
        mEnv->SetAmbientColor(Hmx::Color(0.02f, 0.3f, 0.4f));
        mEnv->RndEnviron::Select(nullptr);   // current, without NgEnviron's draw-time work
        // Drop whatever an earlier test left in the caster state.
        rndshape::ProjShadowCasters drain;
        rndshape::CollectProjShadowCasters(drain);
        rndshape::CollectProjShadowCasters(drain);
    }
    void TearDown() override {
        delete mEnv;   // ~RndEnviron clears the current environ
        mEnv = nullptr;
    }
    RndLight *MakeLight(RndLight::Type type, const Vector3 &pos) {
        RndLight *l = Hmx::Object::New<RndLight>();
        l->SetLightType(type);
        l->SetColor(Hmx::Color(1.0f, 0.5f, 0.25f));
        l->SetLocalPos(pos);
        mEnv->LightsReal().push_back(l);
        return l;
    }
    SceneUniforms Write() {
        SceneUniforms s;
        memset(&s, 0, sizeof(s));
        EXPECT_TRUE(rndshape::WriteSceneLighting(s, nullptr));
        return s;
    }
    RndEnviron *mEnv = nullptr;
};

TEST(RetailLightingShape, Dc3SelectsTheRetailModel) {
    // dc3-decomp's RndEnviron is a drawable (rb3-xenon's is not): the seam's
    // whole selection rests on that.
    EXPECT_TRUE(rndshape::kDC3RetailModel);
    EXPECT_TRUE(rndshape::kGammaSpaceShading);
    EXPECT_FALSE(rndshape::kGenericShadowPass);
    EXPECT_TRUE(rndshape::kSelectDefaultEnvEachFrame);
}

TEST_F(RetailLightingTest, AmbientAndPointLightsAreTheEnvironsOwn) {
    RndLight *point = MakeLight(RndLight::kPoint, Vector3(10.0f, 20.0f, 30.0f));
    point->SetRange(100.0f);
    point->SetFalloffStart(40.0f);
    SceneUniforms s = Write();

    EXPECT_EQ(s.retailLighting, 2.0f);
    // c1: the environ's ambient as authored -- no floor, no scale.
    EXPECT_FLOAT_EQ(s.ambientColor[0], 0.02f);
    EXPECT_FLOAT_EQ(s.ambientColor[1], 0.3f);
    EXPECT_FLOAT_EQ(s.ambientColor[2], 0.4f);
    // No directionals, no synthetic fills: retail has no such register.
    EXPECT_EQ(s.numLights, 0.0f);
    // c62/c66: (pos, 1/(fs - range)), (colour, -range * that).
    ASSERT_EQ(s.numPointLights, 1.0f);
    EXPECT_FLOAT_EQ(s.pointLightPos[0][0], 10.0f);
    EXPECT_FLOAT_EQ(s.pointLightPos[0][1], 20.0f);
    EXPECT_FLOAT_EQ(s.pointLightPos[0][2], 30.0f);
    EXPECT_FLOAT_EQ(s.pointLightPos[0][3], 1.0f / (40.0f - 100.0f));
    EXPECT_FLOAT_EQ(s.pointLightColors[0][0], 1.0f);
    EXPECT_FLOAT_EQ(s.pointLightColors[0][1], 0.5f);
    EXPECT_FLOAT_EQ(s.pointLightColors[0][3], -(100.0f * (1.0f / (40.0f - 100.0f))));
    EXPECT_EQ(s.shadowEnabled, 0.0f);

    // A light's intensity lives in its colour (RndLight::SetPackedColor
    // multiplies it in; Intensity() is the max channel), and c66 is that
    // colour: SetPointLightRegisters reads GetColor(). The HUD lane measured
    // score.lit at x3 and static_now_bar.lit at x21.45.
    // (The property path re-packs the colour to 8 bits a channel, as the
    // image's does.)
    point->SetProperty("intensity", DataNode(3.0f));
    SceneUniforms s3 = Write();
    EXPECT_NEAR(s3.pointLightColors[0][0], 3.0f, 0.02f);
    EXPECT_NEAR(s3.pointLightColors[0][1], 1.5f, 0.02f);
    EXPECT_NEAR(s3.pointLightColors[0][2], 0.75f, 0.02f);
}

TEST_F(RetailLightingTest, OnlyTheFirstThreeLitPointLightsInListOrder) {
    for (int i = 0; i < 5; i++) {
        RndLight *l = MakeLight(RndLight::kPoint, Vector3((float)i, 0.0f, 0.0f));
        if (i == 1) l->SetColor(Hmx::Color(0.0f, 0.0f, 0.0f));   // Pack() == 0: skipped
    }
    SceneUniforms s = Write();
    ASSERT_EQ(s.numPointLights, 3.0f);
    EXPECT_EQ(s.pointLightPos[0][0], 0.0f);
    EXPECT_EQ(s.pointLightPos[1][0], 2.0f);
    EXPECT_EQ(s.pointLightPos[2][0], 3.0f);
}

TEST_F(RetailLightingTest, FogIsTheExponentialForm) {
    mEnv->SetFogEnable(true);
    mEnv->SetFogRange(100.0f, 600.0f);
    mEnv->SetFogColor(Hmx::Color(0.1f, 0.2f, 0.3f));
    SceneUniforms s = Write();
    EXPECT_EQ(s.fogEnabled, 2.0f);
    EXPECT_FLOAT_EQ(s.fogStart, 100.0f);
    EXPECT_FLOAT_EQ(s.fogEnd, 600.0f);
    EXPECT_FLOAT_EQ(s.fogColor[2], 0.3f);
}

TEST_F(RetailLightingTest, BoxMapIsWhatNgEnvironUploaded) {
    for (int i = 0; i < 6; i++) {
        TheShaderMgr.SetVConstant((VShaderConstant)(0x50 + i),
                                  Vector4(0.1f * (float)(i + 1), 0.5f, 0.25f * (float)i, 9.0f));
    }
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetProperty("recv_proj_lights", DataNode(1));
    mesh->SetMat(mat);

    float box[6][4] = {};
    float retail[4] = {};
    rndshape::FillMeshApproxLighting(mesh, box, retail);
    for (int i = 0; i < 6; i++) {
        EXPECT_FLOAT_EQ(box[i][0], 0.1f * (float)(i + 1)) << "face " << i;
        EXPECT_FLOAT_EQ(box[i][1], 0.5f) << "face " << i;
        EXPECT_FLOAT_EQ(box[i][2], 0.25f * (float)i) << "face " << i;
    }
    EXPECT_EQ(retail[0], 0.0f);   // no AO in the shipped cache's lit permutations
    EXPECT_EQ(retail[2], 1.0f);   // recv_proj_lights

    mat->SetProperty("recv_proj_lights", DataNode(0));
    rndshape::FillMeshApproxLighting(mesh, box, retail);
    EXPECT_EQ(retail[2], 0.0f);
    delete mesh;
    delete mat;
}

TEST_F(RetailLightingTest, ProjectedShadowLightDrawsItsCastersWhereTheReceiversLook) {
    RndLight *spot = MakeLight(RndLight::kFakeSpot, Vector3(0.0f, 0.0f, 300.0f));
    // Point the cone straight down: the light's +y axis to world -z.
    Hmx::Matrix3 rot;
    rot.x.Set(1.0f, 0.0f, 0.0f);
    rot.y.Set(0.0f, 0.0f, -1.0f);
    rot.z.Set(0.0f, 1.0f, 0.0f);
    spot->SetLocalRot(rot);
    spot->SetRange(400.0f);
    spot->SetTopRadius(5.0f);
    spot->SetBotRadius(120.0f);
    spot->SetProjectedBlend(1);
    RndMesh *caster = Hmx::Object::New<RndMesh>();
    ObjPtrList<RndDrawable> casters(spot);
    casters.push_back(caster);
    spot->SetShadowOverride(&casters);

    // Frame 1: the light is published, but nothing has been drawn yet.
    SceneUniforms s1 = Write();
    EXPECT_EQ(s1.shadowEnabled, 0.0f);
    EXPECT_FLOAT_EQ(s1.projLightDir[2], 1.0f);   // c65 = -m.y
    EXPECT_FLOAT_EQ(s1.projLightColor[1], 0.5f);  // c69

    rndshape::ProjShadowCasters drawn;
    ASSERT_TRUE(rndshape::CollectProjShadowCasters(drawn));
    ASSERT_EQ(drawn.meshes.size(), 1u);
    EXPECT_EQ(drawn.meshes[0], caster);
    EXPECT_EQ(0, memcmp(drawn.clipFromWorld, s1.lightViewProj, sizeof(s1.lightViewProj)));

    // The rasteriser's texel for a world point must be the receiver's uv.
    const Transform p = spot->Projection();
    const Vector3 pts[] = {Vector3(0, 0, 0), Vector3(30, -20, 0), Vector3(-50, 40, 10)};
    for (const Vector3 &w : pts) {
        const float *m = drawn.clipFromWorld;
        float clip[4];
        for (int r = 0; r < 4; r++)
            clip[r] = m[0 * 4 + r] * w.x + m[1 * 4 + r] * w.y + m[2 * 4 + r] * w.z + m[3 * 4 + r];
        ASSERT_GT(clip[3], 0.0f);
        const float texU = 0.5f * clip[0] / clip[3] + 0.5f;
        const float texV = 0.5f - 0.5f * clip[1] / clip[3];
        const float r0 = p.m.x.x * w.x + p.m.y.x * w.y + p.m.z.x * w.z + p.v.x;
        const float r1 = p.m.x.y * w.x + p.m.y.y * w.y + p.m.z.y * w.z + p.v.y;
        const float r2 = p.m.x.z * w.x + p.m.y.z * w.y + p.m.z.z * w.z + p.v.z;
        EXPECT_NEAR(texU, r0 / r2, 1e-5f);
        EXPECT_NEAR(texV, r1 / r2, 1e-5f);
        EXPECT_EQ(clip[2], 0.0f);   // casters at depth 0: a coverage map
    }

    // Frame 2: the map now holds this light's casters.
    SceneUniforms s2 = Write();
    EXPECT_EQ(s2.shadowEnabled, 2.0f);
    ASSERT_TRUE(rndshape::CollectProjShadowCasters(drawn));

    // A frame that never saw the light draws nothing, and the next frame
    // must not read the stale map.
    rndshape::ProjShadowCasters none;
    spot->SetShowing(false);
    SceneUniforms s3 = Write();
    EXPECT_EQ(s3.shadowEnabled, 0.0f);
    EXPECT_FALSE(rndshape::CollectProjShadowCasters(none));
    spot->SetShowing(true);
    SceneUniforms s4 = Write();
    EXPECT_EQ(s4.shadowEnabled, 0.0f);

    rndshape::CollectProjShadowCasters(none);
    spot->SetShadowOverride(nullptr);
    delete caster;
}

TEST_F(RetailLightingTest, GoboLightIsTheAdditiveFormWithItsTexture) {
    // projected_blend 0 (FREE SKATE's mirrorball_spotlight_*): standard.ps
    // 0x10030011 adds (1 - coverage) * tf10 * c69 * sat(c65 . N) to the box
    // term. Without shadow objects tf5 is the FlatNormal default (alpha 0),
    // so there is no map to draw and coverage is 0.
    RndLight *spot = MakeLight(RndLight::kFakeSpot, Vector3(0.0f, 0.0f, 300.0f));
    RndTex *gobo = Hmx::Object::New<RndTex>();
    spot->SetProperty("texture", DataNode(gobo));
    spot->SetProjectedBlend(0);
    SceneUniforms s = Write();
    EXPECT_EQ(s.projLightColor[3], 2.0f);   // the additive form
    EXPECT_FLOAT_EQ(s.projLightColor[0], 1.0f);
    EXPECT_EQ(rndshape::SceneProjTexture(), gobo);
    EXPECT_EQ(s.shadowEnabled, 0.0f);
    rndshape::ProjShadowCasters none;
    EXPECT_FALSE(rndshape::CollectProjShadowCasters(none));

    // Blend 0 without a texture is no projected light at all (CheckProjLight).
    spot->SetProperty("texture", DataNode((Hmx::Object *)nullptr));
    SceneUniforms s2 = Write();
    EXPECT_EQ(s2.projLightColor[3], 0.0f);
    EXPECT_EQ(rndshape::SceneProjTexture(), nullptr);

    // Blend 1 is the multiply form and never binds a gobo.
    spot->SetProperty("texture", DataNode(gobo));
    spot->SetProjectedBlend(1);
    ObjPtrList<RndDrawable> casters(spot);
    RndMesh *caster = Hmx::Object::New<RndMesh>();
    casters.push_back(caster);
    spot->SetShadowOverride(&casters);
    SceneUniforms s3 = Write();
    EXPECT_EQ(s3.projLightColor[3], 1.0f);
    EXPECT_EQ(rndshape::SceneProjTexture(), nullptr);
    rndshape::CollectProjShadowCasters(none);
    spot->SetShadowOverride(nullptr);
    spot->SetProperty("texture", DataNode((Hmx::Object *)nullptr));
    delete caster;
    delete gobo;
}

TEST_F(RetailLightingTest, ShadowLightWithoutCastersIsRejected) {
    RndLight *spot = MakeLight(RndLight::kFakeSpot, Vector3(0.0f, 0.0f, 300.0f));
    spot->SetProjectedBlend(1);   // blend 1 without any shadow object: rejected
    SceneUniforms s = Write();
    EXPECT_EQ(s.shadowEnabled, 0.0f);
    rndshape::ProjShadowCasters none;
    EXPECT_FALSE(rndshape::CollectProjShadowCasters(none));
}

// NgMat::SetupAmbient: a material with use_environ off gets c1 = (1,1,1,1),
// and CalcShaderOpts drops both light bits, so the shipped permutation 0x10
// draws texel * c0 -- unlit, not lit by the scene's environ. Prelit without
// use_environ (0x100) is the vertex colour as authored, which is not "unlit"
// in MatUnlit's sense; prelit with use_environ takes the environ ambient.
// (The np-gfx-blend lane measured the stock engine lighting use_environ 0
// materials with the scene environ.)
TEST_F(RetailLightingTest, MaterialWithoutEnvironIsUnlit) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetPreLit(false);
    mat->SetUseEnv(false);
    EXPECT_TRUE(rndshape::MatUnlit(mat));
    EXPECT_FALSE(rndshape::MatPrelitAmbient(mat));
    mat->SetUseEnv(true);
    EXPECT_FALSE(rndshape::MatUnlit(mat));
    mat->SetPreLit(true);
    EXPECT_TRUE(rndshape::MatPrelitAmbient(mat));
    mat->SetUseEnv(false);
    EXPECT_FALSE(rndshape::MatUnlit(mat));
    EXPECT_FALSE(rndshape::MatPrelitAmbient(mat));
    delete mat;
}
