// test_reflection_cull.cpp -- the culling of a WorldReflection's mirrored redraw
// (item 20 (b); engine doc dc3-backend-for-rb3-wii.md section 49).
//
// WorldReflection::DrawShowing mirrors the camera and draws its list in
// DrawMode 8. DC3's RndShader::CheckForceCull (rndobj/Shader.cpp) gives EVERY
// material D3D cull mode 6 in that mode -- the mode a kCullBackwards material
// gets in a normal draw -- whatever the material asks. So a two-sided
// (kCullNone) material shows only its mirrored front faces, and a
// kCullBackwards one keeps mode 6. Natively the material key used to flip only
// a non-None cull (two-sided stayed two-sided, Backwards became Regular); under
// the DC3 shape it now culls Backwards for every material in the reflection
// pass, first pass and NextPass alike.
//
// This is a faithfulness change, not the dci "pale reflected stars" fix it was
// first written as: the dci before/after pins are identical, and that pale
// cast was a state mismatch (the hardware player had the Golden Boomy
// unlocked; item 20 (b) README). What it changes per venue is in the
// item 20 (b) venue sweep.
//
// Sabotages: kReflectionForcesReverseCull set false in RndShape_DC3.h (the old
// flip: None stays None, Backwards becomes Regular); ReflectionCull returning
// the material's own cull; the NextPass not passing through ReflectionCull;
// CurrentMeshPassContext not marking DrawMode 8 as the reflection.
//
// No GPU, no game data.

#include "test_helpers.h"

#include "platform/LookProfile.h"
#include "platform/MeshPipelineKey.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Mat.h"
#include "rndobj/Rnd.h"

namespace {

class ReflectionCullTest : public EngineTestFixture {};

WgpuCull OwnCull(Cull cull) {
    return cull == kCullNone ? WgpuCull::None
        : cull == kCullBackwards ? WgpuCull::Backwards : WgpuCull::Regular;
}

TEST_F(ReflectionCullTest, EveryMaterialCullsBackwardsInTheReflection) {
    for (Cull cull : {kCullNone, kCullRegular, kCullBackwards}) {
        RndMat *mat = Hmx::Object::New<RndMat>();
        mat->SetCull(cull);
        MeshPassContext ctx;
        ctx.targetFormat = wgpu::TextureFormat::RGBA8Unorm;
        const PipelineKey own = MaterialPipelineKey(mat, false, false, false, 0, ctx);
        ctx.reflection = true;
        const PipelineKey mirrored = MaterialPipelineKey(mat, false, false, false, 0, ctx);
        EXPECT_EQ(own.cull, OwnCull(cull)) << "cull " << (int)cull;
        EXPECT_EQ(mirrored.cull, WgpuCull::Backwards) << "cull " << (int)cull;

        // A two-sided NextPass in the reflection culls as retail's mode 6 too.
        RndMat *next = Hmx::Object::New<RndMat>();
        next->SetCull(kCullNone);
        EXPECT_EQ(MeshNextPassPipelineKey(mirrored, next, ctx).cull, WgpuCull::Backwards);
        ctx.reflection = false;
        EXPECT_EQ(MeshNextPassPipelineKey(own, next, ctx).cull, WgpuCull::None);
        delete next;
        delete mat;
    }
}

// {rnd native_look refl_cull_flip|refl_cull_force|refl_cull_rule}: the
// inspection switch behind the item 20 (b) venue before/after. flip is the old
// native rule, force is retail's, rule goes back to the shape's constant.
TEST_F(ReflectionCullTest, InspectionSwitchSelectsFlipForceOrRule) {
    MeshPassContext ctx;
    ctx.targetFormat = wgpu::TextureFormat::RGBA8Unorm;
    ctx.reflection = true;
    RndMat *mat = Hmx::Object::New<RndMat>();
    auto mirrored = [&](Cull cull) {
        mat->SetCull(cull);
        return MaterialPipelineKey(mat, false, false, false, 0, ctx).cull;
    };
    milo::SetReflectionCullForInspection(0);
    EXPECT_EQ(mirrored(kCullNone), WgpuCull::None);
    EXPECT_EQ(mirrored(kCullRegular), WgpuCull::Backwards);
    EXPECT_EQ(mirrored(kCullBackwards), WgpuCull::Regular);
    milo::SetReflectionCullForInspection(1);
    EXPECT_EQ(mirrored(kCullNone), WgpuCull::Backwards);
    milo::SetReflectionCullForInspection(-1);
    EXPECT_EQ(milo::ReflectionCullInspection(), -1);
    EXPECT_EQ(mirrored(kCullNone), WgpuCull::Backwards);  // the DC3 shape
    delete mat;
}

TEST_F(ReflectionCullTest, GlyphPagesStayTwoSidedInTheReflection) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetCull(kCullRegular);
    MeshPassContext ctx;
    ctx.targetFormat = wgpu::TextureFormat::RGBA8Unorm;
    ctx.reflection = true;
    EXPECT_EQ(MaterialPipelineKey(mat, false, true, true, 0, ctx).cull, WgpuCull::None);
    delete mat;
}

TEST_F(ReflectionCullTest, OnlyDrawModeEightIsTheReflection) {
    if (!gWgpuRnd) GTEST_SKIP() << "no renderer";
    const Rnd::Mode old = TheRnd.DrawMode();
    TheRnd.SetDrawMode((Rnd::Mode)8);
    const bool reflection = CurrentMeshPassContext().reflection;
    TheRnd.SetDrawMode(Rnd::kDrawNormal);
    const bool normal = CurrentMeshPassContext().reflection;
    TheRnd.SetDrawMode(old);
    EXPECT_TRUE(reflection);
    EXPECT_FALSE(normal);
}

} // namespace
