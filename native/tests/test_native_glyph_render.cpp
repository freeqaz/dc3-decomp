// Engine halves of PLAN G-6 and G-12's rendering fixes (milo-native-engine
// series "engine-stack": "standard shader: no alpha fringe fade under Multiply
// and Darken" and "Mesh_Wgpu: ask the render hook before routing a text
// atlas's alpha into RGB").
//
//  - Text glyph colour: Mesh_Wgpu asks TextMeshKeepsAtlasRGB() before routing
//    a text mesh's atlas alpha into RGB.  With DC3's HamRenderHook registered
//    (dc3_render_hook.cpp), an unnamed (RndText glyph) mesh keeps its RGB --
//    the help bar's (A)/(B)/(Y) discs and the checkbox glyphs.
//  - Multiply and Darken skip the shader's alpha fringe fade (the pipeline's
//    kSkipFringeFade override, set from BlendSkipsFringeFade), so a white,
//    alpha-0 multiply quad is the identity, not black: list_choose_mode's
//    bloom_block.mat no longer erases the Battle row's "1 vs 1" icon (G-12)
//    and perform_endgame's mod_frame_debloom.mesh no longer draws a black box
//    over the final-results screen (G-6).  The image's NgMat sets Multiply to
//    src Zero / dst SrcColor and Darken to One/One Min: neither reads alpha.

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "gfx/PipelineManager.h"
#include "obj/Object.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"

// Declared here, not via "platform/MaterialSetup.h": dc3's own
// native/src/platform/MaterialSetup.h (a pre-engine copy) is earlier on the
// include path and shadows the engine's.
bool TextMeshKeepsAtlasRGB(RndMesh *mesh, RndMat *mat, bool skinned);
bool MeshDrawsAsGlyph(RndMesh *mesh, bool skinned);

// The pipeline key's WgpuBlend and BaseMaterial::Blend share one numbering;
// BlendSkipsFringeFade takes either.
static_assert((int)WgpuBlend::Multiply == BaseMaterial::kBlendMultiply, "blend numbering");
static_assert((int)WgpuBlend::Darken == BaseMaterial::kDarken, "blend numbering");

TEST(NativeGlyphRender, MultiplyAndDarkenSkipFringeFade) {
    EXPECT_TRUE(BlendSkipsFringeFade(BaseMaterial::kBlendMultiply))
        << "a faded transparent multiply quad turns black";
    EXPECT_TRUE(BlendSkipsFringeFade(BaseMaterial::kDarken))
        << "a faded transparent darken quad turns black";
    // Every other blend keeps the fade: black is its identity (Add, Subtract,
    // Screen, Lighten add nothing) or it weights by alpha anyway.
    for (int blend : {BaseMaterial::kBlendDest, BaseMaterial::kBlendSrc,
                      BaseMaterial::kBlendAdd, BaseMaterial::kBlendSrcAlpha,
                      BaseMaterial::kBlendSrcAlphaAdd, BaseMaterial::kBlendSubtract,
                      BaseMaterial::kPreMultAlpha, BaseMaterial::kScreen,
                      BaseMaterial::kLighten})
        EXPECT_FALSE(BlendSkipsFringeFade(blend)) << "blend " << blend;
}

class NativeGlyphRenderTest : public EngineTestFixture {};

TEST_F(NativeGlyphRenderTest, DC3TextGlyphsKeepAtlasRGB) {
    RndMesh *glyphs = Hmx::Object::New<RndMesh>();
    EXPECT_TRUE(TextMeshKeepsAtlasRGB(glyphs, nullptr, false))
        << "DC3's glyph meshes must sample the atlas RGB (Mesh_Wgpu clears "
           "useAlphaAsRGB on this answer)";
    RndMesh *named = Hmx::Object::New<RndMesh>();
    named->SetName("icon_2p.mesh", ObjectDir::Main());
    EXPECT_FALSE(TextMeshKeepsAtlasRGB(named, nullptr, false));
    delete named;
    delete glyphs;
}

// Lane np-gfx-blend: only an unnamed, unskinned mesh (an RndText glyph page)
// is drawn without depth and culling. DC3's HamRibbon hand trail is unnamed
// too, but skinned, and its material (hamribbon.mat: zMode 2, cull 1) must
// hold -- drawn as text the trail went through the dancer and drew its back
// faces, doubling the additive band (FrameCapture on streetside peak:
// mesh='' mat='hamribbon_02.mat' blend=4 zMode=2 cull=1 SKINNED,
// heuristics TextMeshDetect).
// Sabotage tried: MeshDrawsAsGlyph back to `!mesh->Name()[0]` (the pre-fix
// rule, skinning ignored) -> UnnamedSkinnedMeshKeepsDepthAndCull fails.
TEST_F(NativeGlyphRenderTest, UnnamedSkinnedMeshKeepsDepthAndCull) {
    RndMesh *unnamed = Hmx::Object::New<RndMesh>();
    EXPECT_TRUE(MeshDrawsAsGlyph(unnamed, false)) << "a text glyph page draws without depth";
    EXPECT_FALSE(MeshDrawsAsGlyph(unnamed, true))
        << "a skinned unnamed mesh (HamRibbon) keeps its material's zMode and cull";
    RndMesh *named = Hmx::Object::New<RndMesh>();
    named->SetName("floor.mesh", ObjectDir::Main());
    EXPECT_FALSE(MeshDrawsAsGlyph(named, false));
    EXPECT_FALSE(MeshDrawsAsGlyph(named, true));
    EXPECT_FALSE(MeshDrawsAsGlyph(nullptr, false));
    delete named;
    delete unnamed;
}
