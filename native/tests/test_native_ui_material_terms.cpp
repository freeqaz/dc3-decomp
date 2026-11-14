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

TEST(NativeUiMaterialTerms, OverlayPhaseTakesRetailTerms) {
    UiDrawTerms ui = UiDrawTermsFor(true);
    EXPECT_TRUE(ui.gammaShading)
        << "DC3 UI must shade in gamma space (no linear->sRGB encode): the move "
           "cards wash out to pink without it";
    EXPECT_TRUE(ui.retailMaterialTerms)
        << "DC3 UI must keep its materials' lighting and intensify alpha: the "
           "score digits draw half-transparent white without it";
}

TEST(NativeUiMaterialTerms, WorldPhaseUnchanged) {
    // Before EndWorld the world keeps the dc3 shader's linear model (lane
    // np-gfx-light) and the engine's existing material rules.
    UiDrawTerms world = UiDrawTermsFor(false);
    EXPECT_FALSE(world.gammaShading);
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
