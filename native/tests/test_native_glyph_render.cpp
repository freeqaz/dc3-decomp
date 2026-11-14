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
#include "platform/LookProfile.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

// Declared here, not via "platform/MaterialSetup.h": dc3's own
// native/src/platform/MaterialSetup.h (a pre-engine copy) is earlier on the
// include path and shadows the engine's.
bool TextMeshKeepsAtlasRGB(RndMesh *mesh, RndMat *mat, bool skinned);
bool MeshDrawsAsGlyph(RndMesh *mesh, bool skinned);
bool MeshShadesAsText(RndMesh *mesh);
void MarkTextGlyphMesh(RndMesh *mesh);
bool IsMarkedTextGlyphMesh(RndMesh *mesh);
// Engine platform/MaterialSetup.h (same tokens: one definition).
enum class CutCoverage : unsigned char { None, AlphaToCoverage, Sharpened };
bool BlendWeightsByAlpha(int blend);
CutCoverage CutCoverageRule(bool alphaCut, int blend, int threshold, milo::LookProfile look,
                            bool shapeAlphaToCoverage);
CutCoverage MaterialCutCoverage(BaseMaterial *mat);
float MaterialAlphaThreshold(bool alphaCut, int threshold);

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

// Lane np-gfx-blend round 2: the text COLOUR path (BuildMaterialParams'
// isTextMesh: forced prelit, atlas alpha as RGB unless the hook keeps the
// atlas RGB) follows the same mark. HamRibbon's trail is unnamed and was shaded
// as text: forced prelit, so its lit material (hamribbon.mat: prelit 0,
// use_environ 0 -- NgMat::SetupAmbient gives it an ambient of 1 and the shader
// options no lights) was replaced by the mesh's vertex colour, which is white
// only by RndMesh::Vert's default. FrameCapture on streetside peak:
// mesh='' mat='hamribbon.mat' ... prelit=1 heuristics: TextMeshDetect.
// Sabotage tried:
//  - MeshShadesAsText back to `!mesh->Name()[0]` -> the unmarked meshes fail;
//  - MeshShadesAsText returning the mark regardless of the hook ->
//    UnmarkingGameKeepsTextColourPath fails.
TEST_F(NativeGlyphRenderTest, OnlyMarkedPagesTakeTextColourPath) {
    GameRenderHook *hook = GetGameRenderHook();
    ASSERT_NE(hook, nullptr) << "DC3's render hook is not registered";
    ASSERT_TRUE(hook->MarksTextGlyphMeshes());

    RndMesh *ribbon = Hmx::Object::New<RndMesh>(); // unnamed, never marked
    EXPECT_FALSE(MeshShadesAsText(ribbon))
        << "an unmarked unnamed mesh (HamRibbon trail, beam, billboard) shades with "
           "its own material, not as a text page";
    RndMesh *page = Hmx::Object::New<RndMesh>();
    MarkTextGlyphMesh(page);
    EXPECT_TRUE(MeshShadesAsText(page)) << "a glyph page keeps the text colour path";
    RndMesh *named = Hmx::Object::New<RndMesh>();
    named->SetName("floor.mesh", ObjectDir::Main());
    EXPECT_FALSE(MeshShadesAsText(named));
    EXPECT_FALSE(MeshShadesAsText(nullptr));
    delete named;
    delete page;
    delete ribbon;
}

// Without the mark (rb3-xenon) the colour path is what it was: every unnamed
// mesh, skinned or not (it never looked at skinning).
TEST_F(NativeGlyphRenderTest, UnmarkingGameKeepsTextColourPath) {
    GameRenderHook *saved = GetGameRenderHook();
    SetGameRenderHook(nullptr);
    RndMesh *unnamed = Hmx::Object::New<RndMesh>();
    RndMesh *named = Hmx::Object::New<RndMesh>();
    named->SetName("floor.mesh", ObjectDir::Main());
    const bool unnamedText = MeshShadesAsText(unnamed);
    const bool namedText = MeshShadesAsText(named);
    SetGameRenderHook(saved);
    EXPECT_TRUE(unnamedText);
    EXPECT_FALSE(namedText);
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

// ---- alpha cut: the look profile's coverage rule ---------------------------
// DC3's alpha cut is the alpha test and nothing more: NgMat::SetBasicState
// (dc3 src/system/rndobj/Mat_NG.cpp) sets SetAlphaTestEnable(mAlphaCut) and
// SetAlphaFunc(5, mAlphaThreshold) -- 5 maps to the 360's D3DCMP_GREATER
// through RndRenderState::SetAlphaFunc's table (0x8208A138) -- and the image
// never calls D3DDevice_SetRenderState_AlphaToMaskEnable. The engine used to
// turn on hardware alpha-to-coverage for every alpha-cut material in the 4x
// MSAA frame, so a premultiplied glyph at alpha a resolved to a * a of its
// colour: song select's faded list rows (Eagle-Light(47), alpha 0.5) drew at
// ~0.6 of the original and every glyph edge thinned.
//
// DC3_LOOK=retail keeps the original (no coverage anywhere). The default
// enhanced look anti-aliases an alpha cut whose edge is a hard step with a
// sharpened coverage mask (CutCoverage::Sharpened), and leaves a cut at 0
// under an alpha-weighted blend -- every glyph -- to the blend alone.
// rb3-xenon (the same shape, kAlphaCutUsesCoverage) keeps hardware coverage.
//
// The rows are real materials from a census of what DC3 draws (scratch
// Mesh_Wgpu instrumentation over boot -> song select -> OMG at 45 s in all six
// venues, peak and normal, 2026-11-14): blend, threshold and first-seen alpha.
// Sabotage, each run and then restored (2026-11-14):
//  - rule: Sharpened also for a cut at 0 under PreMultAlpha -> RuleOnCensus,
//    CensusMaterials and SongSelectListFont red;
//  - rule: Sharpened only for blend Src (the first guess before the census)
//    -> RuleOnCensus (palm, peak band, icons, award star rows) and
//    CensusMaterials (DLV_PalmTree) red;
//  - kAlphaCutUsesCoverage = true in RndShape_DC3.h -> CensusMaterials and
//    SongSelectListFont red (the rule table passes the shape flag itself);
//  - kAlphaTestGreater = false -> AlphaTestIsRetailsGreater red.
namespace {
struct CutRow {
    const char *mat;  // material in the census (venue / screen)
    int blend;
    int threshold;
    CutCoverage enhanced;
};
const CutRow kCensusRows[] = {
    // text: cut at 0 under PreMultAlpha, faded to 0.5 / 0.147 on screen
    {"Eagle-Light(47).mat (song select list font)", BaseMaterial::kPreMultAlpha, 0, CutCoverage::None},
    // button icons: SrcAlpha cut at 80
    {"icons.mat (help bar button glyphs)", BaseMaterial::kBlendSrcAlpha, 80, CutCoverage::Sharpened},
    // dancers' hair cards, opaque
    {"lima05_hair.mat (player dancer hair)", BaseMaterial::kBlendSrc, 110, CutCoverage::Sharpened},
    {"rasa05_hair_backface.mat", BaseMaterial::kBlendSrc, 150, CutCoverage::Sharpened},
    {"dci01_bd03_hair.mat (backup dancer)", BaseMaterial::kBlendSrc, 110, CutCoverage::Sharpened},
    // streetside crowd clothing, opaque, faded to 0.917
    {"tshirt1.mat (streetside)", BaseMaterial::kBlendSrc, 150, CutCoverage::Sharpened},
    // foliage: dclive palms are alpha-blended AND cut at 90
    {"DLV_PalmTree.mat (dclive)", BaseMaterial::kBlendSrcAlpha, 90, CutCoverage::Sharpened},
    {"peakmode_band1.mat (peak)", BaseMaterial::kBlendSrcAlpha, 90, CutCoverage::Sharpened},
    {"peak_cityscape.mat", BaseMaterial::kBlendSrcAlpha, 170, CutCoverage::Sharpened},
    // alpha-blended, cut at 0: the blend fades the edge
    {"peak_columns_1.mat", BaseMaterial::kBlendSrcAlpha, 0, CutCoverage::None},
    {"arrow_glow.mat (overlay ribbon)", BaseMaterial::kBlendSrcAlpha, 0, CutCoverage::None},
    {"star_award.mat (score star)", BaseMaterial::kBlendSrcAlphaAdd, 32, CutCoverage::Sharpened},
    {"peak_carpet_edge.mat", BaseMaterial::kBlendSrcAlphaAdd, 0, CutCoverage::None},
    // multiply ignores alpha: a cut at 0 is still a step
    {"ribbon_dullbloom.mat (main ribbon)", BaseMaterial::kBlendMultiply, 0, CutCoverage::Sharpened},
};
const char *Name(CutCoverage c) {
    switch (c) {
    case CutCoverage::None: return "None";
    case CutCoverage::AlphaToCoverage: return "AlphaToCoverage";
    case CutCoverage::Sharpened: return "Sharpened";
    }
    return "?";
}
}  // namespace

TEST(NativeGlyphRender, AlphaCutCoverageRuleOnCensusMaterials) {
    using milo::LookProfile;
    for (const CutRow &r : kCensusRows) {
        EXPECT_EQ(Name(CutCoverageRule(true, r.blend, r.threshold, LookProfile::Enhanced, false)),
                  Name(r.enhanced))
            << r.mat << " (blend " << r.blend << ", threshold " << r.threshold << ") enhanced";
        EXPECT_EQ(Name(CutCoverageRule(true, r.blend, r.threshold, LookProfile::Retail, false)),
                  Name(CutCoverage::None))
            << r.mat << ": DC3_LOOK=retail is the alpha test alone";
        // rb3-xenon: Native look, shape coverage on -> unchanged hardware coverage.
        EXPECT_EQ(Name(CutCoverageRule(true, r.blend, r.threshold, LookProfile::Native, true)),
                  Name(CutCoverage::AlphaToCoverage))
            << r.mat << ": rb3-xenon must keep alpha-to-coverage";
        EXPECT_EQ(Name(CutCoverageRule(false, r.blend, r.threshold, LookProfile::Enhanced, false)),
                  Name(CutCoverage::None))
            << r.mat << " without its alpha cut";
    }
    EXPECT_EQ(Name(CutCoverageRule(true, BaseMaterial::kBlendDest, 110, LookProfile::Enhanced, false)),
              Name(CutCoverage::None)) << "blend Dest draws nothing";
    EXPECT_EQ(Name(CutCoverageRule(true, BaseMaterial::kBlendSrc, 0, LookProfile::Enhanced, false)),
              Name(CutCoverage::Sharpened)) << "an opaque cut at 0 is a step";
    EXPECT_TRUE(BlendWeightsByAlpha(BaseMaterial::kPreMultAlpha));
    EXPECT_FALSE(BlendWeightsByAlpha(BaseMaterial::kBlendSrc));
}

TEST(NativeGlyphRender, LookProfileSwitch) {
    const char *e = getenv("DC3_LOOK");
    const bool retailEnv = e && strcmp(e, "retail") == 0;
    EXPECT_EQ(milo::Look().Retail(), retailEnv) << "DC3_LOOK=" << (e ? e : "(unset)");
    EXPECT_EQ(milo::Look().Enhanced(), !retailEnv) << "DC3's default look is enhanced";
    {
        milo::ScopedLookOverride retail(milo::LookProfile::Retail);
        EXPECT_TRUE(milo::Look().Retail());
        EXPECT_FALSE(milo::Look().Enhanced());
    }
    EXPECT_EQ(milo::Look().Retail(), retailEnv) << "the override did not restore";
    EXPECT_STREQ(milo::LookProfileName(milo::LookProfile::Enhanced), "enhanced");
    EXPECT_STREQ(milo::LookProfileName(milo::LookProfile::Retail), "retail");
}

// Retail's alpha test passes ABOVE the threshold (D3DCMP_GREATER), so a cut at
// 0 drops alpha-0 texels: the shader discards below the uniform, which must
// therefore sit just past the reference value.
TEST(NativeGlyphRender, AlphaTestIsRetailsGreater) {
    EXPECT_EQ(MaterialAlphaThreshold(false, 110), 0.0f) << "no cut, no test";
    EXPECT_GT(MaterialAlphaThreshold(true, 0), 0.0f) << "alpha 0 passed a cut at 0";
    EXPECT_GT(MaterialAlphaThreshold(true, 110), 110.0f / 255.0f) << "alpha == threshold passed";
    EXPECT_LT(MaterialAlphaThreshold(true, 110), 110.5f / 255.0f) << "the nudge ate a texel step";
}

// Real materials as the game loads them, through MaterialCutCoverage under
// each look. Every file must load and hold the named alpha-cut material, or
// the test proves nothing.
TEST_F(NativeGlyphRenderTest, CensusMaterialsGetTheirCoverage) {
    if (IsSkipped()) return;
    struct Want { const char *file; const char *mat; CutCoverage enhanced; };
    const Want wants[] = {
        {"ui/resource/fonts/default/gen/eagle-light(47).milo_xbox", "Eagle-Light(47).mat", CutCoverage::None},
        {"world/dclive/gen/dclive.milo_xbox", "DLV_PalmTree.mat", CutCoverage::Sharpened},
        {"world/streetside/gen/streetside.milo_xbox", "tshirt1.mat", CutCoverage::Sharpened},
        {"char/main/dancer/gen/lima05.milo_xbox", "lima05_hair.mat", CutCoverage::Sharpened},
    };
    for (const Want &w : wants) {
        ObjectDir *dir = DirLoader::LoadObjects(FilePath(w.file), nullptr, nullptr);
        if (!dir) GTEST_SKIP() << "no archive asset " << w.file;
        RndMat *found = nullptr;
        for (ObjDirItr<RndMat> it(dir, true); it != nullptr; ++it)
            if (strcmp(it->Name(), w.mat) == 0) found = it;
        ASSERT_NE(found, nullptr) << w.mat << " not in " << w.file;
        ASSERT_TRUE(found->GetAlphaCut()) << w.mat << " lost its alpha cut";
        {
            milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
            EXPECT_EQ(Name(MaterialCutCoverage(found)), Name(w.enhanced))
                << w.mat << " (blend " << (int)found->GetBlend() << ", threshold "
                << found->GetAlphaThreshold() << ") enhanced";
        }
        {
            milo::ScopedLookOverride look(milo::LookProfile::Retail);
            EXPECT_EQ(Name(MaterialCutCoverage(found)), Name(CutCoverage::None))
                << w.mat << " retail: the alpha test alone";
        }
        delete dir;
    }
}

// Every alpha-cut material of the song-select list font: no coverage under
// either look (the faded-row case). At least one PreMultAlpha cut must be seen.
TEST_F(NativeGlyphRenderTest, SongSelectListFontGetsNoCoverage) {
    if (IsSkipped()) return;
    ObjectDir *font = DirLoader::LoadObjects(
        FilePath("ui/resource/fonts/default/gen/eagle-light(47).milo_xbox"), nullptr, nullptr);
    if (!font) GTEST_SKIP() << "no archive UI fonts (ui/resource/fonts/default/gen/eagle-light(47).milo_xbox)";
    int cutPremult = 0;
    for (milo::LookProfile p : {milo::LookProfile::Enhanced, milo::LookProfile::Retail}) {
        milo::ScopedLookOverride look(p);
        for (ObjDirItr<RndMat> it(font, true); it != nullptr; ++it) {
            RndMat *mat = it;
            if (!mat->GetAlphaCut()) continue;
            if (mat->GetBlend() == BaseMaterial::kPreMultAlpha) cutPremult++;
            EXPECT_EQ(Name(MaterialCutCoverage(mat)), Name(CutCoverage::None))
                << mat->Name() << " (alpha cut, threshold " << mat->GetAlphaThreshold()
                << ", blend " << (int)mat->GetBlend() << ") look " << milo::LookProfileName(p);
        }
    }
    EXPECT_GT(cutPremult, 0) << "no alpha-cut PreMultAlpha material in " << font->Name()
                             << "; the faded-row case was not examined";
    delete font;
}
