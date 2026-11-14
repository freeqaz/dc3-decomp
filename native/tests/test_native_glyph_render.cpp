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
#include "obj/DirLoader.h"
#include "obj/Dir.h"
#include "platform/GameRenderHook.h"
#include "rndobj/Font.h"
#include "rndobj/Text.h"
#include "utl/FilePath.h"

// Declared here, not via "platform/MaterialSetup.h": dc3's own
// native/src/platform/MaterialSetup.h (a pre-engine copy) is earlier on the
// include path and shadows the engine's.
bool TextMeshKeepsAtlasRGB(RndMesh *mesh, RndMat *mat, bool skinned);
bool MeshDrawsAsGlyph(RndMesh *mesh, bool skinned);
void MarkTextGlyphMesh(RndMesh *mesh);
bool IsMarkedTextGlyphMesh(RndMesh *mesh);

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

// Lane np-gfx-blend: only a text glyph page is drawn without depth test and
// culling. RndText marks every page it creates (Text.cpp AllocateMeshes,
// FontMap3d::AllocateMeshes -> MarkTextGlyphMesh) and DC3's render hook says
// so (MarksTextGlyphMeshes), so the engine asks positively. Every other
// unnamed mesh keeps its material's zMode and cull: spotlight beams, RndRibbon,
// RndLine, crowd billboards, and HamRibbon's skinned hand trail
// (hamribbon.mat: zMode 2, cull 1 -- drawn as text it went through the dancer
// and drew its back faces, FrameCapture on streetside peak:
// mesh='' mat='hamribbon_02.mat' blend=4 zMode=2 cull=1 SKINNED).
// Sabotage tried:
//  - MeshDrawsAsGlyph back to `!mesh->Name()[0] && !skinned` (inferred, round
//    1) -> GlyphPagesAreMarkedNotInferred fails on the unmarked mesh;
//  - back to `!mesh->Name()[0]` (round 0) -> it also fails on the skinned one;
//  - the MarkTextGlyphMesh call removed from Text.cpp AllocateMeshes ->
//    RndTextMarksItsGlyphPages fails.
TEST_F(NativeGlyphRenderTest, GlyphPagesAreMarkedNotInferred) {
    GameRenderHook *hook = GetGameRenderHook();
    ASSERT_NE(hook, nullptr) << "DC3's render hook is not registered";
    ASSERT_TRUE(hook->MarksTextGlyphMeshes());

    RndMesh *beam = Hmx::Object::New<RndMesh>(); // unnamed, never marked
    EXPECT_FALSE(MeshDrawsAsGlyph(beam, false))
        << "an unmarked unnamed mesh (spotlight beam, ribbon, crowd billboard) "
           "keeps its material's zMode and cull";
    EXPECT_FALSE(MeshDrawsAsGlyph(beam, true))
        << "a skinned unnamed mesh (HamRibbon) keeps its material's zMode and cull";

    RndMesh *page = Hmx::Object::New<RndMesh>();
    MarkTextGlyphMesh(page);
    EXPECT_TRUE(IsMarkedTextGlyphMesh(page));
    EXPECT_TRUE(MeshDrawsAsGlyph(page, false)) << "a glyph page draws without depth";

    RndMesh *named = Hmx::Object::New<RndMesh>();
    named->SetName("floor.mesh", ObjectDir::Main());
    EXPECT_FALSE(MeshDrawsAsGlyph(named, false));
    EXPECT_FALSE(MeshDrawsAsGlyph(nullptr, false));
    delete named;
    delete page;
    delete beam;
}

// A game that does not mark its pages (no hook, or one that does not say so:
// rb3-xenon today) keeps the inferred rule: unnamed and not skinned.
TEST_F(NativeGlyphRenderTest, UnmarkingGameKeepsInferredGlyphRule) {
    GameRenderHook *saved = GetGameRenderHook();
    SetGameRenderHook(nullptr);
    RndMesh *unnamed = Hmx::Object::New<RndMesh>();
    RndMesh *named = Hmx::Object::New<RndMesh>();
    named->SetName("floor.mesh", ObjectDir::Main());
    const bool unnamedFlat = MeshDrawsAsGlyph(unnamed, false);
    const bool unnamedSkinned = MeshDrawsAsGlyph(unnamed, true);
    const bool namedFlat = MeshDrawsAsGlyph(named, false);
    SetGameRenderHook(saved);
    EXPECT_TRUE(unnamedFlat);
    EXPECT_FALSE(unnamedSkinned);
    EXPECT_FALSE(namedFlat);
    delete named;
    delete unnamed;
}

// The text code marks the pages it builds: a real font, a real RndText.
TEST_F(NativeGlyphRenderTest, RndTextMarksItsGlyphPages) {
    if (IsSkipped()) return;
    ObjectDir *fonts =
        DirLoader::LoadObjects(FilePath("ui/resource/fonts/gen/default.milo_xbox"), nullptr, nullptr);
    if (!fonts) GTEST_SKIP() << "no archive UI fonts (ui/resource/fonts/gen/default.milo_xbox)";
    RndFont *font = nullptr;
    for (ObjDirItr<RndFont> it(fonts, true); it != nullptr; ++it) {
        font = it;
        break;
    }
    ASSERT_NE(font, nullptr) << "no RndFont in " << fonts->Name();

    RndText *text = Hmx::Object::New<RndText>();
    DataArrayPtr fontProp(Symbol("styles"), 0, Symbol("font"));
    text->SetProperty(fontProp, DataNode(font));
    text->SetTextASCII("Ab");
    text->UpdateText(); // Load/Copy/the update_text handler do this; SetText does not

    int meshes = 0;
    for (std::list<RndTransformable *>::const_iterator it = text->Children().begin();
         it != text->Children().end(); ++it) {
        RndMesh *mesh = dynamic_cast<RndMesh *>(*it);
        if (!mesh) continue;
        meshes++;
        EXPECT_TRUE(IsMarkedTextGlyphMesh(mesh)) << "page mesh " << meshes << " of font "
                                                 << font->Name() << " is not marked";
        EXPECT_TRUE(MeshDrawsAsGlyph(mesh, false));
    }
    EXPECT_GT(meshes, 0) << "the text built no page mesh (font " << font->Name() << ")";
    delete text;
    delete fonts;
}
