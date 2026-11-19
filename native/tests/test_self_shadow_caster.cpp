// test_self_shadow_caster.cpp -- the casters of DC3's character self-shadow
// map (option bit 19, item 16 (a); engine doc dc3-retail-lighting.md section 17).
//
// Character::DrawShowing calls RndShadowMap::PrepShadow, which draws the
// character into the 512x512 shadow map in Rnd::kDrawExtrude (2). The image
// draws those meshes with the shadowmap shader, and RndShader::CheckForceCull
// gives that shader cull mode 6 -- counter-clockwise, the mode a
// kCullBackwards material gets -- whatever the material asks, so the map
// holds each character's far faces and its near faces are not shadowed by
// themselves. Natively the mesh pass context carries selfShadowCaster in that
// draw mode, and the material key culls backwards for it.
//
// Sabotages: the selfShadowCaster override removed from MaterialPipelineKey
// (the material's own cull: the map held the near faces); the override set
// to Regular; CurrentMeshPassContext testing the reflection draw mode instead
// of kDrawExtrude; the NextPass keeping its own cull in the caster pass.
//
// No GPU, no game data.

#include "test_helpers.h"

#include "platform/MeshPipelineKey.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Mat.h"
#include "rndobj/Rnd.h"

namespace {

class SelfShadowCasterTest : public EngineTestFixture {};

WgpuCull ExpectedOwnCull(Cull cull) {
    return cull == kCullNone ? WgpuCull::None
        : cull == kCullBackwards ? WgpuCull::Backwards : WgpuCull::Regular;
}

TEST_F(SelfShadowCasterTest, CastersCullBackwardsWhateverTheMaterialAsks) {
    for (Cull cull : {kCullNone, kCullRegular, kCullBackwards}) {
        RndMat *mat = Hmx::Object::New<RndMat>();
        mat->SetCull(cull);
        MeshPassContext ctx;
        ctx.targetFormat = wgpu::TextureFormat::RGBA8Unorm;
        ctx.toTexture = true;
        const PipelineKey own = MaterialPipelineKey(mat, true, false, false, 0, ctx);
        ctx.selfShadowCaster = true;
        const PipelineKey caster = MaterialPipelineKey(mat, true, false, false, 0, ctx);
        EXPECT_EQ(own.cull, ExpectedOwnCull(cull)) << "cull " << (int)cull;
        EXPECT_EQ(caster.cull, WgpuCull::Backwards) << "cull " << (int)cull;

        // A NextPass in the caster pass culls as the first pass does.
        RndMat *next = Hmx::Object::New<RndMat>();
        next->SetCull(kCullNone);
        EXPECT_EQ(MeshNextPassPipelineKey(caster, next, ctx).cull, WgpuCull::Backwards);
        ctx.selfShadowCaster = false;
        EXPECT_EQ(MeshNextPassPipelineKey(own, next, ctx).cull, WgpuCull::None);
        delete next;
        delete mat;
    }
}

TEST_F(SelfShadowCasterTest, OnlyTheExtrudeDrawModeIsTheCasterPass) {
    if (!gWgpuRnd) GTEST_SKIP() << "no renderer";
    const Rnd::Mode old = TheRnd.DrawMode();
    TheRnd.SetDrawMode(Rnd::kDrawExtrude);
    const bool extrude = CurrentMeshPassContext().selfShadowCaster;
    TheRnd.SetDrawMode(Rnd::kDrawNormal);
    const bool normal = CurrentMeshPassContext().selfShadowCaster;
    TheRnd.SetDrawMode((Rnd::Mode)8);   // the reflection draw
    const bool reflection = CurrentMeshPassContext().selfShadowCaster;
    TheRnd.SetDrawMode(old);
    EXPECT_TRUE(extrude);
    EXPECT_FALSE(normal);
    EXPECT_FALSE(reflection);
}

} // namespace
