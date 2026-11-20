// test_reflection_cull.cpp -- the culling of a WorldReflection's mirrored redraw
// (item 20 (b); engine doc dc3-retail-lighting.md section 49).
//
// WorldReflection::DrawShowing mirrors the camera and draws its list in
// DrawMode 8. DC3's RndShader::CheckForceCull (rndobj/Shader.cpp) gives EVERY
// material D3D cull mode 6 in that mode -- the mode a kCullBackwards material
// gets in a normal draw -- whatever the material asks. So a two-sided
// (kCullNone) material shows only its mirrored front faces: at dci the
// star_wipe / speaker cards behind the floor's gold stars are two-sided, and
// drawing their far faces too laid a second, paler copy over the reflected
// stars. Natively the material key used to flip only a non-None cull and keep
// a two-sided material two-sided; under the DC3 shape it now culls Backwards
// for every material in the reflection pass, first pass and NextPass alike.
//
// Sabotages: kReflectionForcesReverseCull set false in RndShape_DC3.h (the old
// flip: None stays None, Backwards becomes Regular); ReflectionCull returning
// the material's own cull; the NextPass not passing through ReflectionCull;
// CurrentMeshPassContext not marking DrawMode 8 as the reflection.
//
// No GPU, no game data.

#include "test_helpers.h"

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
