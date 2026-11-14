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
#include "platform/rndshape/RndShape.h"

#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Mat.h"
#include "rndobj/Part.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "rndobj/Trans.h"

#include <algorithm>
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

// ---- particle positions are in the system's relative space ------------------
//
// Retail DxParticleSys::DrawShowing (src/system/rnddx9/Part.cpp) draws the
// particles under TheShaderMgr.SetTransform(mRelativeXfm), so the world
// position of a particle is mRelativeXfm * p->pos. The engine billboard used
// p->pos raw, which put streetside's manholeSteam (relative_motion 0.8, its
// motion parent placed after SetRelativeMotion ran) in the middle of the
// stage: the opaque cyan cloud in front of the right backup dancer at peak.
//
// Sabotage tried (np-gfx-blend residual): rndshape::PartWorldPos back to
// "return p->pos" -> FullyRelative... and PartlyRelative... fail (world
// (1,2,3) where (8,21,33) / (9,2,3) is expected); renaming Part.h's
// RelativeXfm() accessor -> those two and DC3ShapeReadsRelativeXfm fail (the
// SFINAE fallback compiles fine and would otherwise silently restore the bug).

namespace {
// UpdateRelativeXfm is protected; the probe only re-exports it.
class RelativeProbeSys : public RndParticleSys {
public:
    RelativeProbeSys() {}
    using RndParticleSys::UpdateRelativeXfm;
};
} // namespace

TEST_F(ParticleColorTest, DC3ShapeReadsRelativeXfm) {
    EXPECT_TRUE(rndshape::PartHasRelativeXfm<RndParticleSys>::value)
        << "RndParticleSys::RelativeXfm() is not visible to the DC3 shape; "
           "PartWorldPos would fall back to drawing relative positions as world";
}

// relative_motion 1: mRelativeXfm is the motion parent's world xfm.
TEST_F(ParticleColorTest, FullyRelativeParticleFollowsParent) {
    RndTransformable *parent = Hmx::Object::New<RndTransformable>();
    Hmx::Matrix3 rot; // 90 degrees about +z: x -> y, y -> -x
    rot.Set(0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    parent->SetLocalRot(rot);
    parent->SetLocalPos(Vector3(10.0f, 20.0f, 30.0f));
    RndParticleSys *sys = Hmx::Object::New<RndParticleSys>();
    sys->SetPool(4, RndParticleSys::kBasic);
    sys->SetRelativeMotion(1.0f, parent);
    RndParticle *p = sys->AllocParticle();
    ASSERT_NE(p, nullptr);
    p->pos = Vector4(1.0f, 2.0f, 3.0f, 1.0f);
    Vector3 w = rndshape::PartWorldPos(sys, p);
    // rot * (1,2,3) = (-2, 1, 3); + (10,20,30)
    EXPECT_NEAR(w.x, 8.0f, kEps);
    EXPECT_NEAR(w.y, 21.0f, kEps);
    EXPECT_NEAR(w.z, 33.0f, kEps);
    delete sys;
    delete parent;
}

// relative_motion 0.8 and a parent that moves after SetRelativeMotion (a venue
// placing the system): UpdateRelativeXfm accumulates 0.8 of the displacement.
TEST_F(ParticleColorTest, PartlyRelativeParticleIsOffsetByAccumulatedMotion) {
    RndTransformable *parent = Hmx::Object::New<RndTransformable>();
    RelativeProbeSys *sys = new RelativeProbeSys();
    sys->SetPool(4, RndParticleSys::kBasic);
    sys->SetRelativeMotion(0.8f, parent);
    parent->SetLocalPos(Vector3(10.0f, 0.0f, 0.0f));
    sys->UpdateRelativeXfm();
    RndParticle *p = sys->AllocParticle();
    ASSERT_NE(p, nullptr);
    p->pos = Vector4(1.0f, 2.0f, 3.0f, 1.0f);
    Vector3 w = rndshape::PartWorldPos(sys, p);
    EXPECT_NEAR(w.x, 9.0f, kEps); // 1 + 0.8 * 10
    EXPECT_NEAR(w.y, 2.0f, kEps);
    EXPECT_NEAR(w.z, 3.0f, kEps);
    delete sys;
    delete parent;
}

// relative_motion 0 (the default): positions are already world.
TEST_F(ParticleColorTest, NonRelativeParticleIsWorld) {
    RndParticleSys *sys = Hmx::Object::New<RndParticleSys>();
    sys->SetPool(4, RndParticleSys::kBasic);
    sys->SetLocalPos(Vector3(5.0f, 5.0f, 5.0f));
    RndParticle *p = sys->AllocParticle();
    ASSERT_NE(p, nullptr);
    p->pos = Vector4(1.0f, 2.0f, 3.0f, 1.0f);
    Vector3 w = rndshape::PartWorldPos(sys, p);
    EXPECT_NEAR(w.x, 1.0f, kEps);
    EXPECT_NEAR(w.y, 2.0f, kEps);
    EXPECT_NEAR(w.z, 3.0f, kEps);
    delete sys;
}

// ---- billboard: the shipped particles vertex shader ------------------------
// Retail's non-velocity-aligned billboard, read from the microcode of every
// `particles` permutation in dc3's xbox_shaders (vertex t0 = size, t1 = angle,
// t2 = swingArm; c47 = 0.5 * camera X, c48 = 0.5 * screen_aspect * camera Z,
// set by DxParticleSys::DrawShowing):
//
//     angle wrapped to [-pi, pi]; cos -> r1.x, sin -> r1.y
//     r1.xy = (cos, sin) * swingArm
//     u = p cos - q sin, w = p sin + q cos      (corner signs p, q times size)
//     pos += 2 sin swing * c47 + 2 cos swing * c48 + u * c47 - w * c48
//
// i.e. up = sin R + S cos U, right = cos R - S sin U, and the centre moves
// swingArm along up. The engine billboard ignored screen_aspect (streetside
// alleySteam: 0.7) and the swing arm (throneroom's spinning smoke, start_offset
// 10), and turned the quad the other way.
//
// Sabotage tried (np-gfx-blend residual round 2), each caught:
//   call site passes retailXbox = false      -> RenderedParticleTakes... fails
//   swing arm dropped                         -> SwingArm... and the render test
//   screen aspect forced to 1                 -> ScalesUp..., SwingArm..., render
//   whole turn reversed (sinA -> -sinA)       -> TurnsClockwise, SwingArm, Oblique
//   one basis row's sign flipped (rotRx)      -> Oblique only (the axis-aligned
//                                                tests cannot see an x-row slip)
//   Part.h ScreenAspect() renamed             -> DC3ShapeReads... and the render

namespace {
const float kR[3] = {1.0f, 0.0f, 0.0f};
const float kU[3] = {0.0f, 0.0f, 1.0f};
const float kPos[3] = {5.0f, 10.0f, 7.0f};
void ExpectVec(const float *v, float x, float y, float z) {
    EXPECT_NEAR(v[0], x, 1.0e-5f);
    EXPECT_NEAR(v[1], y, 1.0e-5f);
    EXPECT_NEAR(v[2], z, 1.0e-5f);
}
} // namespace

TEST_F(ParticleColorTest, DC3ShapeReadsScreenAspect) {
    EXPECT_TRUE(rndshape::PartHasScreenAspect<RndParticleSys>::value)
        << "RndParticleSys::ScreenAspect() is not visible to the DC3 shape; the "
           "billboard would fall back to ignoring screen_aspect and the swing arm";
    RndParticleSys *sys = Hmx::Object::New<RndParticleSys>();
    sys->SetProperty("screen_aspect", DataNode(0.7f));
    rndshape::PartBillboardTerms t = rndshape::PartBillboard(sys);
    EXPECT_TRUE(t.retailXbox);
    EXPECT_NEAR(t.screenAspect, 0.7f, kEps);
    delete sys;
}

TEST_F(ParticleColorTest, RetailBillboardScalesUpByScreenAspect) {
    ParticleQuadAxes q = ParticleBillboardAxes(kPos, kR, kU, 2.0f, 0.0f, true, 0.5f, 0.0f);
    ExpectVec(q.centre, 5.0f, 10.0f, 7.0f);
    ExpectVec(q.right, 2.0f, 0.0f, 0.0f);
    ExpectVec(q.up, 0.0f, 0.0f, 1.0f);
}

// A quarter turn: right goes to -up (down the screen), up to right. Clockwise.
TEST_F(ParticleColorTest, RetailBillboardTurnsClockwise) {
    ParticleQuadAxes q =
        ParticleBillboardAxes(kPos, kR, kU, 2.0f, (float)M_PI_2, true, 1.0f, 0.0f);
    ExpectVec(q.right, 0.0f, 0.0f, -2.0f);
    ExpectVec(q.up, 2.0f, 0.0f, 0.0f);
}

TEST_F(ParticleColorTest, SwingArmMovesTheCentreAlongTurnedUp) {
    ParticleQuadAxes q = ParticleBillboardAxes(kPos, kR, kU, 2.0f, 0.0f, true, 1.0f, 3.0f);
    ExpectVec(q.centre, 5.0f, 10.0f, 10.0f);
    q = ParticleBillboardAxes(kPos, kR, kU, 2.0f, (float)M_PI_2, true, 1.0f, 3.0f);
    ExpectVec(q.centre, 8.0f, 10.0f, 7.0f);
    // The arm is in the same squashed basis as the quad.
    q = ParticleBillboardAxes(kPos, kR, kU, 2.0f, 0.0f, true, 0.5f, 3.0f);
    ExpectVec(q.centre, 5.0f, 10.0f, 8.5f);
}

// The same quarter turn under an oblique camera (every component of R and U
// non-zero in x and z), squashed and swung, so a sign or component slip in
// any row of the basis shows.
TEST_F(ParticleColorTest, RetailBillboardUnderAnObliqueCamera) {
    const float R[3] = {0.6f, 0.0f, 0.8f};
    const float U[3] = {-0.8f, 0.0f, 0.6f};
    ParticleQuadAxes q =
        ParticleBillboardAxes(kPos, R, U, 2.0f, (float)M_PI_2, true, 0.5f, 3.0f);
    ExpectVec(q.right, 0.8f, 0.0f, -0.6f);  // -0.5 U * 2
    ExpectVec(q.up, 1.2f, 0.0f, 1.6f);      // R * 2
    ExpectVec(q.centre, 6.8f, 10.0f, 9.4f); // + R * 3
    q = ParticleBillboardAxes(kPos, R, U, 2.0f, 0.0f, true, 0.5f, 3.0f);
    ExpectVec(q.right, 1.2f, 0.0f, 1.6f);   // R * 2
    ExpectVec(q.up, -0.8f, 0.0f, 0.6f);     // 0.5 U * 2
    ExpectVec(q.centre, 3.8f, 10.0f, 7.9f); // + 0.5 U * 3
}

// Without the retail terms (RB3-Wii, rb3-xenon) the billboard is the old one.
TEST_F(ParticleColorTest, OtherShapesKeepTheOldBillboard) {
    ParticleQuadAxes q =
        ParticleBillboardAxes(kPos, kR, kU, 2.0f, (float)M_PI_2, false, 0.5f, 3.0f);
    ExpectVec(q.centre, 5.0f, 10.0f, 7.0f);
    ExpectVec(q.right, 0.0f, 0.0f, 2.0f);
    ExpectVec(q.up, -2.0f, 0.0f, 0.0f);
}

// ---- velocity-aligned quads: the shader's other path -----------------------
// DxParticleSys::DrawParticles writes (size, 2vx, 2vy, 2vz) for a velocity_align
// system, and DrawShowing sets c49 = (aligned, stretched, constantArea,
// 2 * stretch_scale) and c47.w = !perspective. The microcode (instruction 44 to
// label L17, every permutation) builds, with a = V.X, b = S V.Z (camera axes in
// the relative space), len = |(a, b)| and (A, B) = (a, b) / len:
//
//   L = stretched ? size + 2 stretch_scale len : size
//   W = constantArea ? size^2 / L : size
//   corner = P + (1 - tau)/2 L (A X + B S Z) + sigma W/2 (B X - A S Z)
//
// and, with perspective, a world segment P .. P + 4 stretch_scale V instead of
// the screen-length term. The engine drew every aligned system as an unaligned
// billboard: sparks, fire, throneroom's god_rays and DCI's timeMachine_rays
// came out as round puffs instead of streaks.
//
// Sabotage tried (np-gfx-blend residual round 3), each caught:
//   call site ignores billboard.aligned         -> the render test
//   stretch term dropped (L = size)             -> Stretched..., Aspect..., Relative...
//   width axis sign flipped (A X - B S Z)       -> Unstretched..., Stretched...
//   lead along -velocity (centre = P - up)      -> every pure test with a velocity, render
//   rel used directly instead of inverted       -> RelativeSpace...
//   perspective path uses the screen length     -> Perspective...
//   constant area not applied                   -> ConstantArea...
//   Part.h AlignWithVelocity() renamed          -> DC3ShapeReadsVelocityAlign..., render

namespace {
const float kIdentityRel[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
ParticleAlignTerms Terms(float aspect, bool stretched, float scale, bool carea, bool persp) {
    ParticleAlignTerms t;
    t.screenAspect = aspect;
    t.stretched = stretched;
    t.stretchScale = scale;
    t.constantArea = carea;
    t.perspective = persp;
    return t;
}
} // namespace

TEST_F(ParticleColorTest, DC3ShapeReadsVelocityAlignTerms) {
    EXPECT_TRUE(rndshape::PartHasVelocityAlign<RndParticleSys>::value)
        << "RndParticleSys::AlignWithVelocity() is not visible to the DC3 shape; "
           "every velocity_align system would draw as a round billboard";
    RndParticleSys *sys = Hmx::Object::New<RndParticleSys>();
    sys->SetPool(4, RndParticleSys::kFancy);
    sys->SetProperty("velocity_align", DataNode(1));
    sys->SetProperty("stretch_with_velocity", DataNode(1));
    sys->SetProperty("stretch_scale", DataNode(0.4f));
    sys->SetProperty("constant_area", DataNode(1));
    sys->SetProperty("perspective", DataNode(1));
    rndshape::PartBillboardTerms t = rndshape::PartBillboard(sys);
    EXPECT_TRUE(t.velocityVerts);
    EXPECT_TRUE(t.aligned);
    EXPECT_TRUE(t.stretched);
    EXPECT_TRUE(t.constantArea);
    EXPECT_TRUE(t.perspective);
    EXPECT_NEAR(t.stretchScale, 0.4f, kEps);
    // Stretch off: constant area and perspective go with it (DrawShowing).
    sys->SetProperty("stretch_with_velocity", DataNode(0));
    t = rndshape::PartBillboard(sys);
    EXPECT_TRUE(t.aligned);
    EXPECT_FALSE(t.stretched);
    EXPECT_FALSE(t.constantArea);
    EXPECT_FALSE(t.perspective);
    // A basic pool is never aligned, but its vertices still carry the velocity.
    sys->SetPool(4, RndParticleSys::kBasic);
    t = rndshape::PartBillboard(sys);
    EXPECT_TRUE(t.velocityVerts);
    EXPECT_FALSE(t.aligned);
    delete sys;
}

// Velocity (3, 0, 4) lies in the screen plane: A = 0.6, B = 0.8. Size 2.
TEST_F(ParticleColorTest, UnstretchedAlignedQuadTurnsToTheVelocity) {
    const float v[3] = {3.0f, 0.0f, 4.0f};
    ParticleQuadAxes q = ParticleAlignedAxes(kPos, v, kIdentityRel, kR, kU, 2.0f,
                                             Terms(1.0f, false, 1.0f, false, false));
    ExpectVec(q.up, 0.6f, 0.0f, 0.8f);       // L/2 = 1 along the velocity
    ExpectVec(q.right, 0.8f, 0.0f, -0.6f);   // W/2 = 1 across it
    ExpectVec(q.centre, 5.6f, 10.0f, 7.8f);  // the bottom edge sits on P
}

// stretch_scale 0.5: L = 2 + 2 * 0.5 * 5 = 7.
TEST_F(ParticleColorTest, StretchedAlignedQuadLeadsAlongTheVelocity) {
    const float v[3] = {3.0f, 0.0f, 4.0f};
    ParticleQuadAxes q = ParticleAlignedAxes(kPos, v, kIdentityRel, kR, kU, 2.0f,
                                             Terms(1.0f, true, 0.5f, false, false));
    ExpectVec(q.up, 2.1f, 0.0f, 2.8f);
    ExpectVec(q.right, 0.8f, 0.0f, -0.6f);
    ExpectVec(q.centre, 7.1f, 10.0f, 9.8f);
}

TEST_F(ParticleColorTest, ConstantAreaNarrowsAStretchedQuad) {
    const float v[3] = {3.0f, 0.0f, 4.0f};
    ParticleQuadAxes q = ParticleAlignedAxes(kPos, v, kIdentityRel, kR, kU, 2.0f,
                                             Terms(1.0f, true, 0.5f, true, false));
    const float halfW = 0.5f * 4.0f / 7.0f;  // W = size^2 / L
    ExpectVec(q.right, 0.8f * halfW, 0.0f, -0.6f * halfW);
    ExpectVec(q.up, 2.1f, 0.0f, 2.8f);
}

// Velocity straight into the screen projects to nothing: the quad is a point.
TEST_F(ParticleColorTest, AlignedQuadCollapsesWhenTheVelocityFacesTheCamera) {
    const float v[3] = {0.0f, 5.0f, 0.0f};
    ParticleQuadAxes q = ParticleAlignedAxes(kPos, v, kIdentityRel, kR, kU, 2.0f,
                                             Terms(1.0f, true, 0.5f, false, false));
    ExpectVec(q.up, 0.0f, 0.0f, 0.0f);
    ExpectVec(q.right, 0.0f, 0.0f, 0.0f);
    ExpectVec(q.centre, 5.0f, 10.0f, 7.0f);
}

// screen_aspect 0.5, velocity (0, 0, 4): b = 0.5 * 4 = 2, so L = 2 + 1 * 2 = 4,
// along 0.5 Z.
TEST_F(ParticleColorTest, AlignedQuadUsesTheSquashedUpAxis) {
    const float v[3] = {0.0f, 0.0f, 4.0f};
    ParticleQuadAxes q = ParticleAlignedAxes(kPos, v, kIdentityRel, kR, kU, 2.0f,
                                             Terms(0.5f, true, 0.5f, false, false));
    ExpectVec(q.up, 0.0f, 0.0f, 1.0f);
    ExpectVec(q.right, 1.0f, 0.0f, 0.0f);
}

// perspective: a world segment P .. P + 4 * stretch_scale * V, without the
// particle size, whatever the screen length; the width is still screen-space.
TEST_F(ParticleColorTest, PerspectiveStreakIsTheWorldVelocity) {
    const float v[3] = {3.0f, 6.0f, 4.0f};
    ParticleQuadAxes q = ParticleAlignedAxes(kPos, v, kIdentityRel, kR, kU, 2.0f,
                                             Terms(1.0f, true, 0.3f, false, true));
    ExpectVec(q.up, 1.8f, 3.6f, 2.4f);
    ExpectVec(q.centre, 6.8f, 13.6f, 9.4f);
    ExpectVec(q.right, 0.8f, 0.0f, -0.6f);
    q = ParticleAlignedAxes(kPos, v, kIdentityRel, kR, kU, 2.0f,
                            Terms(1.0f, true, 0.3f, true, true));
    const float halfW = 0.5f * 4.0f / 7.0f;  // size^2 / (size + len)
    ExpectVec(q.right, 0.8f * halfW, 0.0f, -0.6f * halfW);
}

// The velocity is in the relative space, and retail takes the camera axes there
// through Invert(mRelativeXfm.m): under a relative matrix that turns 90 degrees
// and doubles, local (0, -0.5, 0) is world (1, 0, 0), but a = V . (X M^-1) is
// 0.25, not the world dot product 1. L = 2 + 2 * 2 * 0.25 = 3 along X.
TEST_F(ParticleColorTest, RelativeSpaceVelocityGoesThroughTheInverse) {
    const float rel[9] = {0.0f, 2.0f, 0.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f};
    const float v[3] = {0.0f, -0.5f, 0.0f};
    ParticleQuadAxes q =
        ParticleAlignedAxes(kPos, v, rel, kR, kU, 2.0f, Terms(1.0f, true, 2.0f, false, false));
    ExpectVec(q.up, 1.5f, 0.0f, 0.0f);
    ExpectVec(q.right, 0.0f, 0.0f, -1.0f);
    // Perspective uses the world velocity V * M = (1, 0, 0): 4 * 2 * 1 = 8 long.
    q = ParticleAlignedAxes(kPos, v, rel, kR, kU, 2.0f, Terms(1.0f, true, 2.0f, false, true));
    ExpectVec(q.up, 4.0f, 0.0f, 0.0f);
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
// The billboard terms reach the draw: one particle rendered with
// screen_aspect 1, then 0.5, then 1 with a swing arm of 1.5. Squashing halves
// the covered height and keeps the width; the arm moves the quad up the screen
// by the arm's length (at angle 0, up is camera Z).
namespace {
struct PxBox {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    int W() const { return x1 - x0 + 1; }
    int H() const { return y1 - y0 + 1; }
    float CY() const { return 0.5f * (y0 + y1); }
};
bool RenderOneParticle(float aspect, float swing, PxBox &box) {
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(64, 64, 32, RndTex::kRendered, false, nullptr);
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetLocalRot(Hmx::Matrix3::GetIdentity()); // looks down +y, z up
    cam->SetLocalPos(Vector3(0.0f, 0.0f, 0.0f));
    cam->SetFrustum(1.0f, 1000.0f, 0.8f, 1.0f);
    cam->SetTargetTex(tex);
    RndMat *mat = MakeMat(1.0f, 1.0f, 1.0f, 1.0f, BaseMaterial::kBlendSrc, false);
    mat->SetZMode(kZModeDisable);
    RndParticleSys *sys = Hmx::Object::New<RndParticleSys>();
    sys->SetMat(mat);
    sys->SetPool(4, RndParticleSys::kBasic);
    sys->SetBoxExtent(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
    sys->SetLocalPos(Vector3(0.0f, 10.0f, 0.0f));
    sys->SetProperty("screen_aspect", DataNode(aspect));
    PartOverride po;
    po.mask = 1 | 2 | 4 | 0x10;
    po.life = 1000.0f;
    po.speed = 0.0f;
    po.size = 3.0f;
    po.startColor = Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f);
    sys->ExplicitParticles(1, true, po);
    RndParticle *p = sys->ActiveParticles();
    bool ok = p != nullptr;
    if (ok) {
        p->angle = 0.0f;
        p->swingArm = swing;
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        sys->DrawShowing();
        cam->SetTargetTex(nullptr);
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        ok = ReadbackGpuRenderTarget(tex, px, w, h) && w == 64 && h == 64;
        for (int y = 0; ok && y < h; y++)
            for (int x = 0; x < w; x++)
                if (px[(y * w + x) * 4 + 1] > 128) {
                    box.x0 = std::min(box.x0, x); box.x1 = std::max(box.x1, x);
                    box.y0 = std::min(box.y0, y); box.y1 = std::max(box.y1, y);
                }
    }
    delete sys;
    delete mat;
    delete cam;
    delete tex;
    return ok;
}
} // namespace

TEST_F(ParticleColorTest, RenderedParticleTakesScreenAspectAndSwingArm) {
    if (IsSkipped()) return;
    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
        GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    PxBox full, squashed, swung;
    ASSERT_TRUE(RenderOneParticle(1.0f, 0.0f, full)) << "no GPU render target";
    ASSERT_TRUE(RenderOneParticle(0.5f, 0.0f, squashed));
    ASSERT_TRUE(RenderOneParticle(1.0f, 1.5f, swung));
    ASSERT_GT(full.H(), 8) << "the particle did not cover the target";
    EXPECT_NEAR(squashed.H(), full.H() / 2.0f, 1.5f)
        << "screen_aspect 0.5 should halve the height " << full.H() << " -> " << squashed.H();
    EXPECT_NEAR(squashed.W(), full.W(), 1.0f);
    EXPECT_NEAR(swung.H(), full.H(), 1.0f);
    // 1.5 units at distance 10 is half the 3-unit quad: half its height, upward.
    EXPECT_NEAR(full.CY() - swung.CY(), full.H() / 2.0f, 1.5f)
        << "swing arm 1.5 should lift the quad by half its height, centre "
        << full.CY() << " -> " << swung.CY();
}

// The aligned terms reach the draw: a size-2 particle moving along +X, fancy,
// velocity_align and stretch_with_velocity with stretch_scale 0.5, is a quad
// L = 2 + 2 * 0.5 * 1 = 3 long that starts at the particle and leads to the
// right; the same particle without velocity_align is the 2 x 2 billboard
// centred on it.
namespace {
bool RenderMovingParticle(bool aligned, PxBox &box) {
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(64, 64, 32, RndTex::kRendered, false, nullptr);
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetLocalRot(Hmx::Matrix3::GetIdentity()); // looks down +y, z up
    cam->SetLocalPos(Vector3(0.0f, 0.0f, 0.0f));
    cam->SetFrustum(1.0f, 1000.0f, 0.8f, 1.0f);
    cam->SetTargetTex(tex);
    RndMat *mat = MakeMat(1.0f, 1.0f, 1.0f, 1.0f, BaseMaterial::kBlendSrc, false);
    mat->SetZMode(kZModeDisable);
    RndParticleSys *sys = Hmx::Object::New<RndParticleSys>();
    sys->SetMat(mat);
    sys->SetPool(4, RndParticleSys::kFancy);
    sys->SetBoxExtent(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
    sys->SetLocalPos(Vector3(0.0f, 10.0f, 0.0f));
    sys->SetProperty("velocity_align", DataNode(aligned ? 1 : 0));
    sys->SetProperty("stretch_with_velocity", DataNode(1));
    sys->SetProperty("stretch_scale", DataNode(0.5f));
    PartOverride po;
    po.mask = 1 | 2 | 4 | 0x10;
    po.life = 1000.0f;
    po.speed = 0.0f;
    po.size = 2.0f;
    po.startColor = Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f);
    sys->ExplicitParticles(1, true, po);
    RndParticle *p = sys->ActiveParticles();
    bool ok = p != nullptr;
    if (ok) {
        p->angle = 0.0f;
        p->swingArm = 0.0f;
        p->vel.x = 1.0f;
        p->vel.y = 0.0f;
        p->vel.z = 0.0f;
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        sys->DrawShowing();
        cam->SetTargetTex(nullptr);
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        ok = ReadbackGpuRenderTarget(tex, px, w, h) && w == 64 && h == 64;
        for (int y = 0; ok && y < h; y++)
            for (int x = 0; x < w; x++)
                if (px[(y * w + x) * 4 + 1] > 128) {
                    box.x0 = std::min(box.x0, x); box.x1 = std::max(box.x1, x);
                    box.y0 = std::min(box.y0, y); box.y1 = std::max(box.y1, y);
                }
    }
    delete sys;
    delete mat;
    delete cam;
    delete tex;
    return ok;
}
} // namespace

TEST_F(ParticleColorTest, RenderedVelocityAlignedParticleIsAStreak) {
    if (IsSkipped()) return;
    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
        GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    PxBox round, streak;
    ASSERT_TRUE(RenderMovingParticle(false, round)) << "no GPU render target";
    ASSERT_TRUE(RenderMovingParticle(true, streak));
    ASSERT_GT(round.W(), 8) << "the particle did not cover the target";
    const float cx = 0.5f * (round.x0 + round.x1);  // the particle's column
    EXPECT_NEAR(streak.W(), 1.5f * round.W(), 1.5f)
        << "the streak should be 3 units long where the billboard is 2: "
        << round.W() << " -> " << streak.W();
    EXPECT_NEAR(streak.H(), round.H(), 1.0f);
    EXPECT_NEAR(streak.x0, cx, 1.5f)
        << "the streak should start at the particle and lead along +X, x0 "
        << streak.x0 << " (particle column " << cx << ")";
}
#endif
