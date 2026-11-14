// PLAN G-24: DC3's UI shades the way the Xbox 360 DxRnd shades it (engine
// MaterialSetup's overlay-phase rules, opted into by dc3_render_hook.cpp).
//
//  - Gamma space (GameRenderHook::UiShadesInGammaSpace). DxRnd multiplies
//    stored texel and colour values into an A8R8G8B8 frame with no encode;
//    the engine's dc3 shader sRGB-encoded the product, which lifted the move
//    cards' deep purple (front buffer ~80,20,75) to a washed pink (~164,83,165).
//  - Retail material terms (GameRenderHook::UiUsesRetailMaterialTerms). The
//    engine forced every overlay-pass and text mesh to prelit, and scaled only
//    rgb by intensify. The score digits are a lit, intensify font
//    (Eagle-Book(numbers*).mat) that score.grp draws under score.env (violet
//    ambient, magenta score.lit) at font_alpha 0.5; DC3's standard.ps scales
//    the whole texel by c5.y = intensify + 1 (`mul r0, r0, c5.yyyy`), so the
//    retail digits are opaque magenta. Native drew them half-transparent white.
//
// Sabotages tried (each fails at least one test here): HamRenderHook's
// UiShadesInGammaSpace or UiUsesRetailMaterialTerms returning false; the
// engine's UiDrawTermsFor ignoring frameInOverlay (world draws would change);
// MaterialShadesPrelit forcing text prelit regardless of the retail terms;
// MaterialAlphaScale returning 1.

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Env.h"
#include "rndobj/Group.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include <cstring>

// Declared here, not via "platform/MaterialSetup.h": dc3's own
// native/src/platform/MaterialSetup.h (a pre-engine copy) is earlier on the
// include path and shadows the engine's.
struct UiDrawTerms {
    bool gammaShading = false;
    bool retailMaterialTerms = false;
};
UiDrawTerms UiDrawTermsFor(bool frameInOverlay);
bool MaterialShadesPrelit(bool matPrelit, bool isTextMesh, bool isOverlayPass,
                          bool isMultiplyBlend, bool simpleRender,
                          bool retailMaterialTerms);
float MaterialAlphaScale(bool intensify, bool retailMaterialTerms);
bool MaterialShadesUnlit(bool matPrelit, bool matUseEnviron, bool retailMaterialTerms);

TEST(NativeUiMaterialTerms, OverlayPhaseTakesRetailTerms) {
    UiDrawTerms ui = UiDrawTermsFor(true);
    EXPECT_TRUE(ui.gammaShading)
        << "DC3 UI must shade in gamma space (no linear->sRGB encode): the move "
           "cards wash out to pink without it";
    EXPECT_TRUE(ui.retailMaterialTerms)
        << "DC3 UI must keep its materials' lighting and intensify alpha: the "
           "score digits draw half-transparent white without it";
}

TEST(NativeUiMaterialTerms, WorldPhaseTakesTheShapesShadingSpace) {
    // Before EndWorld the hook's UI switches do not apply: the world takes the
    // render shape's own shading space and the engine's existing material
    // rules. For DC3 that space is gamma (the retail light model, lane
    // np-gfx-light: rndshape::kGammaSpaceShading), so the world and the UI now
    // agree; what must stay off outside the overlay is the UI material terms.
    UiDrawTerms world = UiDrawTermsFor(false);
    EXPECT_TRUE(world.gammaShading)
        << "the DC3 world shades in gamma space, as the Xbox 360 renderer does";
    EXPECT_FALSE(world.retailMaterialTerms);
}

TEST(NativeUiMaterialTerms, PrelitAndAlphaRules) {
    // A lit text mesh in the overlay pass: lit under the retail terms, forced
    // prelit without them (the engine's generic UI path, kept for rb3-xenon).
    EXPECT_FALSE(MaterialShadesPrelit(false, true, true, false, false, true));
    EXPECT_TRUE(MaterialShadesPrelit(false, true, true, false, false, false));
    // An authored prelit material, multiply blend and simple render stay prelit.
    EXPECT_TRUE(MaterialShadesPrelit(true, true, true, false, false, true));
    EXPECT_TRUE(MaterialShadesPrelit(false, false, true, true, false, true));
    EXPECT_TRUE(MaterialShadesPrelit(false, false, true, false, true, true));
    // A world mesh outside the overlay pass is untouched either way.
    EXPECT_FALSE(MaterialShadesPrelit(false, false, false, false, false, false));

    EXPECT_FLOAT_EQ(MaterialAlphaScale(true, true), 2.0f)
        << "standard.ps scales the texel's alpha by intensify too";
    EXPECT_FLOAT_EQ(MaterialAlphaScale(true, false), 1.0f);
    EXPECT_FLOAT_EQ(MaterialAlphaScale(false, true), 1.0f);

    // use_environ 0 on a lit material: ambient 1, no lights (NgMat::SetupAmbient).
    EXPECT_TRUE(MaterialShadesUnlit(false, false, true))
        << "the move names (EagleLightHeavy, use_environ 0) go dark under "
           "static_hud.env's black ambient without it";
    EXPECT_FALSE(MaterialShadesUnlit(false, true, true)) << "the score font uses its environ";
    EXPECT_FALSE(MaterialShadesUnlit(true, false, true)) << "prelit stays prelit";
    EXPECT_FALSE(MaterialShadesUnlit(false, false, false)) << "world / no hook: unchanged";
}

// The asset facts the rules are for: the score font is lit and intensify, and
// score.grp leads with score.env, whose ambient and light are what colour the
// digits magenta. If the data said otherwise, the rules above would have no
// score to fix.
class NativeUiMaterialAssetTest : public EngineTestFixture {};

TEST_F(NativeUiMaterialAssetTest, ScoreFontIsLitIntensifyUnderScoreEnv) {
    const char *root = getenv("MILO_LIB");
    if (!root || !root[0])
        GTEST_SKIP() << "MILO_LIB not set";

    std::string font = std::string(root) + "/ui/resource/fonts/gen/eagle-bold(47numbers2).milo_xbox";
    ObjectDir *fontDir = DirLoader::LoadObjects(FilePath(font.c_str()), nullptr, nullptr);
    ASSERT_NE(fontDir, nullptr) << font;
    RndMat *mat = fontDir->Find<RndMat>("Eagle-Book(numbers2).mat", false);
    ASSERT_NE(mat, nullptr);
    EXPECT_FALSE(mat->Prelit());
    EXPECT_TRUE(mat->GetUseEnviron());
    EXPECT_TRUE(mat->GetIntensify());

    std::string score = std::string(root) + "/ui/hud/gen/score.milo_xbox";
    ObjectDir *scoreDir = DirLoader::LoadObjects(FilePath(score.c_str()), nullptr, nullptr);
    ASSERT_NE(scoreDir, nullptr) << score;
    RndGroup *grp = scoreDir->Find<RndGroup>("score.grp", false);
    ASSERT_NE(grp, nullptr);
    RndEnviron *env = scoreDir->Find<RndEnviron>("score.env", false);
    ASSERT_NE(env, nullptr);
    ASSERT_FALSE(grp->Objects().empty());
    EXPECT_EQ(grp->Objects().front(), env)
        << "score.grp draws score.env first, so the labels after it are lit by it";
    const Hmx::Color &amb = env->AmbientColor();
    EXPECT_GT(amb.blue, 0.9f);
    EXPECT_LT(amb.green, 0.1f);
}

// The move names above the flashcards draw with EagleLightHeavy(10_00)2x.mat,
// a lit material that does not use its environ, inside the HUD where
// static_hud.env (ambient black, two point lights) is current. Retail shows
// them white because use_environ 0 means ambient 1 and no lights; lit under
// the environ they drew dark grey. Sabotage: MaterialShadesUnlit returning
// false fails PrelitAndAlphaRules; this pins the asset fact it is for.
TEST_F(NativeUiMaterialAssetTest, MoveNameFontIgnoresTheEnviron) {
    const char *root = getenv("MILO_LIB");
    if (!root || !root[0])
        GTEST_SKIP() << "MILO_LIB not set";
    std::string font = std::string(root)
        + "/ui/resource/fonts/default/gen/eagle-light-heavy(13_90)s2x.milo_xbox";
    ObjectDir *dir = DirLoader::LoadObjects(FilePath(font.c_str()), nullptr, nullptr);
    ASSERT_NE(dir, nullptr) << font;
    RndMat *mat = dir->Find<RndMat>("EagleLightHeavy(10_00)2x.mat", false);
    ASSERT_NE(mat, nullptr);
    EXPECT_FALSE(mat->Prelit());
    EXPECT_FALSE(mat->GetUseEnviron());
    EXPECT_TRUE(MaterialShadesUnlit(mat->Prelit(), mat->GetUseEnviron(), true));
}

// The black diamond on the fitness screens (G-24): MeshFilter's "microphone"
// rule hid the letterbox mic tab (geo_mic*) but not its backing,
// ribbon_blocker.mesh: an opaque (kBlendSrc) diamond parented to mic.grp that
// masks the help-bar rays under the tab. With the tab hidden the blocker drew
// alone, black. It has to follow the tab. (mic.grp itself is the whole
// bottom-right cluster, rays and hand-help shield included, so the rule is
// not "the whole group".) Sabotage: dropping ribbon_blocker.mesh from the rule
// fails this test.
bool ShouldSkipMesh(const char *meshName, RndMat *mat);

TEST_F(NativeUiMaterialAssetTest, LetterboxMicTabBlockerFollowsTheTab) {
    const char *root = getenv("MILO_LIB");
    if (!root || !root[0])
        GTEST_SKIP() << "MILO_LIB not set";
    std::string path = std::string(root) + "/ui/letterbox/gen/letterbox.milo_xbox";
    ObjectDir *dir = DirLoader::LoadObjects(FilePath(path.c_str()), nullptr, nullptr);
    ASSERT_NE(dir, nullptr) << path;
    RndGroup *mic = dir->Find<RndGroup>("mic.grp", false);
    RndMesh *tab = dir->Find<RndMesh>("geo_mictab_scaled.mesh", false);
    RndMesh *blocker = dir->Find<RndMesh>("ribbon_blocker.mesh", false);
    ASSERT_NE(mic, nullptr);
    ASSERT_NE(tab, nullptr);
    ASSERT_NE(blocker, nullptr);
    EXPECT_EQ(blocker->TransParent(), mic) << "the blocker is no longer the mic tab's";
    ASSERT_NE(blocker->Mat(), nullptr);
    EXPECT_EQ(blocker->Mat()->GetBlend(), BaseMaterial::kBlendSrc) << "the blocker is opaque";
    EXPECT_EQ(ShouldSkipMesh(blocker->Name(), nullptr), ShouldSkipMesh(tab->Name(), nullptr))
        << "the mic tab and its black backing must be drawn or hidden together";
}

// The rule tests above pin the helpers; these drive the real builders, so a
// BuildMaterialParams / BuildPassMaterialParams that stops consulting the UI
// terms (or stops calling MaterialShadesUnlit / MaterialShadesPrelit /
// MaterialAlphaScale) fails here even though the helpers still pass. The
// three-argument overloads take the terms explicitly; the two-argument entry
// points the draw path calls forward CurrentUiDrawTerms() to them.
// Outside a render pass CurrentPassHasDepth() is false, so these build as
// overlay-pass draws, which is where the retail terms apply.
//
// Sabotages tried, each verified to fail a test here (engine d35269e): the
// primary builder replacing its uiTerms argument with CurrentUiDrawTerms();
// dropping the MaterialShadesUnlit term from `unlit` (primary, and pass);
// passing retailMaterialTerms=false to MaterialShadesPrelit; not scaling
// color[3] by MaterialAlphaScale (primary, and pass); CurrentUiDrawTerms()
// returning the overlay terms unconditionally.
#include "platform/MaterialSetup.h"
#include "rndobj/BaseMaterial.h"

MaterialParams BuildMaterialParams(RndMat *mat, bool isTextMesh, const UiDrawTerms &uiTerms);
MaterialParams BuildPassMaterialParams(BaseMaterial *pass, const UiDrawTerms &uiTerms);

namespace {
RndMat *MakeMat(bool prelit, bool useEnviron, bool intensify, float alpha) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetPreLit(prelit);
    mat->SetUseEnv(useEnviron);
    mat->SetProperty("intensify", DataNode(intensify ? 1 : 0));
    mat->SetColor(1.0f, 1.0f, 1.0f);
    mat->SetAlpha(alpha);
    return mat;
}
const UiDrawTerms kRetail{true, true};
const UiDrawTerms kEngineDefault{false, false};
} // namespace

class NativeUiMaterialBuilderTest : public EngineTestFixture {};

TEST_F(NativeUiMaterialBuilderTest, PrimaryBuilderAppliesTheUiTerms) {
    ASSERT_NE(gWgpuRnd, nullptr);
    ASSERT_FALSE(gWgpuRnd->CurrentPassHasDepth()) << "test expects an overlay-pass build";

    // A lit text material that uses its environ: lit under the retail terms
    // (the score digits), forced prelit under the engine default.
    RndMat *litText = MakeMat(false, true, false, 1.0f);
    ASSERT_TRUE(litText->GetUseEnviron());
    MaterialParams p = BuildMaterialParams(litText, true, kRetail);
    EXPECT_EQ(p.uniforms.prelit, 0.0f);
    EXPECT_EQ(p.uniforms.unlit, 0.0f);
    EXPECT_EQ(p.uniforms.gammaShading, 1.0f);
    p = BuildMaterialParams(litText, true, kEngineDefault);
    EXPECT_EQ(p.uniforms.prelit, 1.0f);
    EXPECT_EQ(p.uniforms.gammaShading, 0.0f);

    // A lit material that ignores its environ: unlit (ambient 1, no lights)
    // under the retail terms (the move names), not under the default.
    RndMat *noEnv = MakeMat(false, false, false, 1.0f);
    ASSERT_FALSE(noEnv->GetUseEnviron());
    EXPECT_EQ(BuildMaterialParams(noEnv, true, kRetail).uniforms.unlit, 1.0f);
    EXPECT_EQ(BuildMaterialParams(noEnv, true, kEngineDefault).uniforms.unlit, 0.0f);
    // An authored prelit material is not "unlit", whatever its environ.
    RndMat *prelitNoEnv = MakeMat(true, false, false, 1.0f);
    EXPECT_EQ(BuildMaterialParams(prelitNoEnv, false, kRetail).uniforms.unlit, 0.0f);
    EXPECT_EQ(BuildMaterialParams(prelitNoEnv, false, kRetail).uniforms.prelit, 1.0f);

    // Intensify scales alpha too under the retail terms: font_alpha 0.5 on
    // the score font reads opaque.
    RndMat *bright = MakeMat(false, true, true, 0.5f);
    ASSERT_TRUE(bright->GetIntensify());
    p = BuildMaterialParams(bright, true, kRetail);
    EXPECT_FLOAT_EQ(p.uniforms.color[3], 1.0f);
    EXPECT_FLOAT_EQ(p.uniforms.intensify, 2.0f);
    EXPECT_FLOAT_EQ(BuildMaterialParams(bright, true, kEngineDefault).uniforms.color[3], 0.5f);

    delete litText;
    delete noEnv;
    delete prelitNoEnv;
    delete bright;
}

TEST_F(NativeUiMaterialBuilderTest, PassBuilderAppliesTheUiTerms) {
    ASSERT_NE(gWgpuRnd, nullptr);
    RndMat *noEnv = MakeMat(false, false, false, 1.0f);
    EXPECT_EQ(BuildPassMaterialParams(noEnv, kRetail).uniforms.unlit, 1.0f);
    EXPECT_EQ(BuildPassMaterialParams(noEnv, kEngineDefault).uniforms.unlit, 0.0f);
    EXPECT_EQ(BuildPassMaterialParams(noEnv, kRetail).uniforms.gammaShading, 1.0f);
    EXPECT_EQ(BuildPassMaterialParams(noEnv, kEngineDefault).uniforms.gammaShading, 0.0f);

    RndMat *bright = MakeMat(false, true, true, 0.5f);
    EXPECT_FLOAT_EQ(BuildPassMaterialParams(bright, kRetail).uniforms.color[3], 1.0f);
    EXPECT_FLOAT_EQ(BuildPassMaterialParams(bright, kEngineDefault).uniforms.color[3], 0.5f);
    delete noEnv;
    delete bright;
}

// A premultiplied-alpha material (blend 7: One / OneMinusSrcAlpha) reaches the
// shader with rgb scaled by its alpha: NgMat::SetRegularShaderConst
// (Mat_NG.cpp:191, 100% matched) calls PreMultiplyAlpha on the colour before
// setting kVS_Color/kPS_Color. Without it a fading premult label adds its full
// colour until the shader's fringe fade near alpha 0: the fitness HUD's
// time/kCal crossfade drew both labels at full brightness for its 0.33 s
// (Eagle-Light(47).mat is blend 7). Both term sets, both builders; the scale
// uses the authored alpha, before the intensify factor on alpha.
// Sabotages tried, each verified to fail here: MaterialShaderColor not scaling
// rgb (both builders at once); the primary builder copying the raw colour;
// the pass builder copying the raw colour; scaling every blend, not just 7;
// scaling by twice the alpha (the intensify-doubled value).
TEST_F(NativeUiMaterialBuilderTest, PremultipliedAlphaColourIsScaledByAlpha) {
    ASSERT_NE(gWgpuRnd, nullptr);
    RndMat *label = MakeMat(false, false, false, 0.25f);
    label->SetColor(0.8f, 0.6f, 0.4f);
    label->SetBlend(BaseMaterial::kPreMultAlpha);
    for (const UiDrawTerms &terms : {kRetail, kEngineDefault}) {
        for (const MaterialParams &p : {BuildMaterialParams(label, true, terms),
                                        BuildPassMaterialParams(label, terms)}) {
            EXPECT_FLOAT_EQ(p.uniforms.color[0], 0.8f * 0.25f);
            EXPECT_FLOAT_EQ(p.uniforms.color[1], 0.6f * 0.25f);
            EXPECT_FLOAT_EQ(p.uniforms.color[2], 0.4f * 0.25f);
            EXPECT_FLOAT_EQ(p.uniforms.color[3], 0.25f);
        }
    }

    // Any other blend: the colour as authored.
    label->SetBlend(BaseMaterial::kBlendSrcAlpha);
    for (const MaterialParams &p : {BuildMaterialParams(label, true, kRetail),
                                    BuildPassMaterialParams(label, kRetail)}) {
        EXPECT_FLOAT_EQ(p.uniforms.color[0], 0.8f);
        EXPECT_FLOAT_EQ(p.uniforms.color[2], 0.4f);
        EXPECT_FLOAT_EQ(p.uniforms.color[3], 0.25f);
    }

    // Intensify: retail premultiplies the authored alpha, then the shader's
    // intensify doubles rgb and alpha alike (MaterialAlphaScale).
    RndMat *bright = MakeMat(false, true, true, 0.5f);
    bright->SetBlend(BaseMaterial::kPreMultAlpha);
    MaterialParams p = BuildMaterialParams(bright, true, kRetail);
    EXPECT_FLOAT_EQ(p.uniforms.color[0], 0.5f);
    EXPECT_FLOAT_EQ(p.uniforms.color[3], 1.0f);
    delete label;
    delete bright;
}

// The two-argument entry point the draw path uses must forward the current
// phase's terms: outside the overlay phase it equals the engine default.
TEST_F(NativeUiMaterialBuilderTest, DrawPathEntryPointForwardsTheCurrentTerms) {
    ASSERT_NE(gWgpuRnd, nullptr);
    ASSERT_FALSE(gWgpuRnd->FrameInOverlay()) << "test runs outside a frame";
    RndMat *noEnv = MakeMat(false, false, false, 1.0f);
    MaterialParams viaDrawPath = BuildMaterialParams(noEnv, true);
    MaterialParams viaDefault = BuildMaterialParams(noEnv, true, kEngineDefault);
    EXPECT_EQ(viaDrawPath.uniforms.unlit, viaDefault.uniforms.unlit);
    EXPECT_EQ(viaDrawPath.uniforms.prelit, viaDefault.uniforms.prelit);
    EXPECT_EQ(viaDrawPath.uniforms.gammaShading, viaDefault.uniforms.gammaShading);
    delete noEnv;
}
