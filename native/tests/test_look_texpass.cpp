// test_look_texpass.cpp -- np-look47: a render-to-texture target drawn like
// the frame (LookSettings::texPassMsaa / texPassScale, engine LookProfile.h).
//
// Break It Down's dancer is drawn into projection.tex (a RndTexRenderer's
// kRendered 1024x1024 target) and shown on a quad; the frame round her has 4x
// MSAA at the look's render scale. The enhanced look draws such a target with
// the frame's sample count, resolved into the texture (step 1), and draws it
// into a GPU texture sized for the output's rows (step 2: 1024 up to 360 rows,
// 1536 at 540, 2048 from 720 rows up), re-made only where a pass that
// clears it begins. Retail and Native keep the one-sample target at its own
// size.
//
// Which targets (round 2b, decided by the census, CENSUS.md section 6): a
// texture pass is drawn like the frame when its target is the output of a
// texture renderer that draws a character. Two halves: the engine's
// (TexPassDrawnLikeFrame, engine Tex_Wgpu.cpp: RndTex type exactly kRendered,
// no mip chain) and dc3's predicate (NativeTexPassDrawsCharacter,
// native/src/platform/TexPassRule.cpp: at least one RndTexRenderer has the
// target as output, and every one that does draws a Character without
// force_mips). The engine asks the predicate only where a clearing pass
// begins and keeps the answer until the next one.
//
// Tests of the engine half register a test predicate (ScopedPredicate);
// tests of dc3's half call the predicate dc3 registers, read back through
// GetTexPassLikeFramePredicate.
//
// Sabotage, round 1: TexPassDrawnLikeFrame true for every render target ->
// EngineHalfIsAColourAndDepthTargetWithNoMips fails on kRenderedNoZ,
// kShadowMap, kDepthVolumeMap, kDensityMap; texPassMsaa = true in every
// profile -> EnhancedTurnsOnStep1RetailAndNativeNot fails. Round 2b: the
// predicate answering yes for any renderer -> the Group-draw cases fail;
// the predicate ignoring force_mips -> ACharacterWithForceMipsIsNot fails;
// EnsureRenderTargetData asking the rule again ->
// TheAnswerIsKeptUntilTheNextClearingPass fails. All seen red, reverted.

#include "test_helpers.h"

#include "char/Character.h"
#include "gfx/GpuDevice.h"
#include "obj/DataFile.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Draw.h"
#include "rndobj/Group.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "rndobj/TexRenderer.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

bool NativeTexPassDrawsCharacter(RndTex *tex);

namespace {

// Holds a predicate for one test and puts back the one registered before.
class ScopedPredicate {
public:
    explicit ScopedPredicate(TexPassLikeFrameFn fn) : mOld(GetTexPassLikeFramePredicate()) {
        SetTexPassLikeFramePredicate(fn);
    }
    ~ScopedPredicate() { SetTexPassLikeFramePredicate(mOld); }
private:
    TexPassLikeFrameFn mOld;
};

bool AnyTarget(RndTex *) { return true; }
bool sSwitchAnswer = true;
bool SwitchedTarget(RndTex *) { return sSwitchAnswer; }

// The predicate dc3 registered at static init (TexPassRule.cpp), not a copy.
bool Dc3Rule(RndTex *tex) {
    TexPassLikeFrameFn fn = GetTexPassLikeFramePredicate();
    return fn && fn(tex);
}

RndTex *NewRendered(int w, int h) {
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(w, h, 32, RndTex::kRendered, false, nullptr);
    return tex;
}

RndTexRenderer *NewRenderer(RndTex *out, Hmx::Object *draw) {
    RndTexRenderer *tr = Hmx::Object::New<RndTexRenderer>();
    tr->SetProperty("output_texture", DataNode(out));
    tr->SetProperty("draw", DataNode(draw));
    return tr;
}

class TexPassRuleTest : public EngineTestFixture {};

TEST_F(TexPassRuleTest, Dc3RegistersItsPredicate) {
    EXPECT_EQ(GetTexPassLikeFramePredicate(), &NativeTexPassDrawsCharacter);
}

TEST_F(TexPassRuleTest, ATargetNoRendererDrawsIntoIsNot) {
    RndTex *tex = NewRendered(640, 480);
    EXPECT_FALSE(Dc3Rule(tex));
    delete tex;
}

// keep_me.tex's shape: augmented_photo's TexRenderer.rndtex draws Group.grp.
TEST_F(TexPassRuleTest, AGroupDrawRendererIsNot) {
    RndTex *keep = NewRendered(640, 480);
    RndGroup *grp = Hmx::Object::New<RndGroup>();
    RndTexRenderer *tr = NewRenderer(keep, grp);
    EXPECT_FALSE(Dc3Rule(keep));
    // Re-pointed at a second texture (a snapshot): neither is drawn like the frame.
    RndTex *snap = NewRendered(640, 480);
    tr->SetProperty("output_texture", DataNode(snap));
    EXPECT_FALSE(Dc3Rule(keep));
    EXPECT_FALSE(Dc3Rule(snap));
    delete tr;
    delete grp;
    delete snap;
    delete keep;
}

TEST_F(TexPassRuleTest, ACharacterDrawRendererIs) {
    RndTex *tex = NewRendered(1024, 1024);
    Character *ch = Hmx::Object::New<Character>();
    RndTexRenderer *tr = NewRenderer(tex, ch);
    EXPECT_TRUE(Dc3Rule(tex));
    delete tr;
    delete ch;
    delete tex;
}

TEST_F(TexPassRuleTest, ACharacterWithForceMipsIsNot) {
    RndTex *tex = NewRendered(1024, 1024);
    Character *ch = Hmx::Object::New<Character>();
    RndTexRenderer *tr = NewRenderer(tex, ch);
    tr->SetProperty("force_mips", DataNode(1));
    EXPECT_FALSE(Dc3Rule(tex));
    delete tr;
    delete ch;
    delete tex;
}

TEST_F(TexPassRuleTest, TwoRenderersOneAGroupsIsNot) {
    RndTex *tex = NewRendered(640, 480);
    Character *ch = Hmx::Object::New<Character>();
    RndGroup *grp = Hmx::Object::New<RndGroup>();
    RndTexRenderer *a = NewRenderer(tex, ch);
    RndTexRenderer *b = NewRenderer(tex, grp);
    EXPECT_FALSE(Dc3Rule(tex));
    delete b;
    EXPECT_TRUE(Dc3Rule(tex)) << "control: the Character renderer alone";
    delete a;
    delete grp;
    delete ch;
    delete tex;
}

TEST_F(TexPassRuleTest, ATargetWhoseRendererOutputWasClearedIsNot) {
    RndTex *tex = NewRendered(1024, 1024);
    Character *ch = Hmx::Object::New<Character>();
    RndTexRenderer *tr = NewRenderer(tex, ch);
    ASSERT_TRUE(Dc3Rule(tex));
    tr->SetProperty("output_texture", DataNode((Hmx::Object *)nullptr));
    EXPECT_FALSE(Dc3Rule(tex));
    delete tr;
    delete ch;
    delete tex;
}

// The game's own keep_me.tex, from ui/augmented_photo.milo.
TEST_F(TexPassRuleTest, AugmentedPhotoKeepMeIsNot) {
    const char *root = getenv("MILO_LIB");
    if (!root || !root[0])
        GTEST_SKIP() << "MILO_LIB not set";
    std::string path = std::string(root) + "/ui/gen/augmented_photo.milo_xbox";
    ObjectDir *dir = DirLoader::LoadObjects(FilePath(path.c_str()), nullptr, nullptr);
    ASSERT_NE(dir, nullptr) << path;
    RndTexRenderer *tr = dir->Find<RndTexRenderer>("TexRenderer.rndtex", false);
    ASSERT_NE(tr, nullptr);
    RndTex *keep = dir->Find<RndTex>("keep_me.tex", false);
    ASSERT_NE(keep, nullptr);
    ASSERT_EQ(tr->GetOutputTexture(), keep);
    EXPECT_EQ((int)keep->GetType(), (int)RndTex::kRendered);
    EXPECT_FALSE(Dc3Rule(keep));
    // A snapshot-shaped texture set as the renderer's output (what
    // StreamRecorder-side code does with the photo targets).
    RndTex *snap = NewRendered(640, 480);
    tr->SetProperty("output_texture", DataNode(snap));
    EXPECT_FALSE(Dc3Rule(snap));
    EXPECT_FALSE(Dc3Rule(keep));
    tr->SetProperty("output_texture", DataNode(keep));
    delete snap;
}

TEST(LookTexPass, EnhancedTurnsOnStep1RetailAndNativeNot) {
    {
        milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
        const milo::LookSettings &enh = milo::Look();
        // Discrete GPUs only until an integrated GPU's timing says it fits.
        EXPECT_TRUE(enh.texPassMsaa && !enh.texPassMsaaIntegrated);
        EXPECT_TRUE(milo::LookTexPassMsaa(enh, true) && !milo::LookTexPassMsaa(enh, false));
        EXPECT_TRUE(enh.texPassScale && !enh.texPassScaleIntegrated);
        EXPECT_TRUE(milo::LookTexPassScale(enh, true) && !milo::LookTexPassScale(enh, false));
    }
    for (milo::LookProfile p : {milo::LookProfile::Retail, milo::LookProfile::Native}) {
        milo::ScopedLookOverride look(p);
        const milo::LookSettings &s = milo::Look();
        EXPECT_FALSE(s.texPassMsaa || s.texPassMsaaIntegrated) << milo::LookProfileName(p);
        EXPECT_FALSE(s.texPassScale || s.texPassScaleIntegrated) << milo::LookProfileName(p);
    }
}

// Step 2's size follows the OUTPUT's rows (GpuDevice::OutputHeight), so a
// window gets the same target at render scale 1 and 2. The rule is one
// constant, texPassBaseRows 360: about two texels a screen pixel at 720
// rows, where one (base rows 720, the rule as first built) left the Break It
// Down dancer softer than the main frame's (PLAN H-175).
// Sabotage: texPassBaseRows 480 in place of 360 (720 rows -> 1536, the
// fallback the PLAN row names: taking it means changing this table) -> red
// (seen, reverted); and 720, the first rule (720 rows -> 1024) -> red (seen,
// reverted). Which rows the engine passes (OutputHeight, not the
// drawn WindowHeight) is checked by the running game's pass line, not here:
// the GPU tests below set the rows themselves.
TEST(LookTexPass, GpuSizeFollowsTheOutputRows) {
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    const milo::LookSettings &enh = milo::Look();
    struct Case { int rows; int side; } cases[] = {
        {180, 1024}, {360, 1024}, {540, 1536}, {713, 2028}, {720, 2048},
        {1080, 2048}, {1440, 2048}, {2160, 2048},
    };
    for (const Case &c : cases) {
        int w = 0, h = 0;
        milo::TexPassGpuSize(enh, 1024, 1024, c.rows, w, h);
        EXPECT_EQ(w, c.side) << c.rows << " rows";
        EXPECT_EQ(h, c.side) << c.rows << " rows";
    }
    int w = 0, h = 0;
    milo::TexPassGpuSize(enh, 640, 480, 540, w, h);  // x1.5 keeps the shape
    EXPECT_EQ(w, 960);
    EXPECT_EQ(h, 720);
    milo::TexPassGpuSize(enh, 640, 480, 720, w, h);  // x2
    EXPECT_EQ(w, 1280);
    EXPECT_EQ(h, 960);
    milo::TexPassGpuSize(enh, 640, 480, 2160, w, h);  // the longer side at the cap
    EXPECT_EQ(w, 2048);
    EXPECT_EQ(h, 1536);
    // The rows the engine reads, from the frame rule itself: a window at
    // render scale 1 and 2 (dc3's desktop look: following output, 1280x720
    // logical) leaves the renderer at the window's rows either way. 960x540
    // is the window that tells the two apart (the drawn frame at scale 2 has
    // 1080 rows, which would be 2048); 1920x1080 is at the cap both ways.
    struct Window { int w, h, side; } windows[] = {{960, 540, 1536}, {1920, 1080, 2048}};
    for (const Window &win : windows) {
        for (float scale : {1.0f, 2.0f}) {
            GpuDeviceDesc d;
            d.outputFollowsTarget = true;
            d.logicalWidth = 1280;
            d.logicalHeight = 720;
            d.renderScaleExplicit = scale;
            const FrameSizes f = ResolveFrameSizes(d, true, true, win.w, win.h);
            EXPECT_EQ(f.outputH, win.h) << "scale " << scale;
            EXPECT_EQ(f.drawnH, (int)(win.h * scale)) << "scale " << scale;
            milo::TexPassGpuSize(enh, 1024, 1024, f.outputH, w, h);
            EXPECT_EQ(w, win.side) << win.h << " rows, scale " << scale;
        }
    }
}

// The side TexPassGpuSize gives a 1024x1024 target at `rows` under the look
// in force (both sides: the target is square).
int Side1024(int rows) {
    int w = 0, h = 0;
    milo::TexPassGpuSize(milo::Look(), 1024, 1024, rows, w, h);
    EXPECT_EQ(w, h) << rows << " rows";
    return w;
}

// np-look49: SetLookTexPassSizeForInspection moves the two numbers of step 2's
// rule (rtt_cap_* / rtt_rows_*): > 0 sets, 0 leaves alone, < 0 the profile's.
// Sabotage: the setter ignoring maxSide -> red; 0 returning a value to the
// profile's in place of leaving it -> red (both seen, reverted).
TEST(LookTexPass, TheSizeSetterMovesTheCapAndTheRows) {
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);  // restores both numbers
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(1536, 0));
    EXPECT_EQ(Side1024(1440), 1536) << "cap 1536";
    EXPECT_EQ(Side1024(720), 1536) << "cap 1536";
    EXPECT_EQ(Side1024(540), 1536) << "cap 1536, the rows left at 360";
    EXPECT_EQ(milo::Look().texPassBaseRows, 360) << "0 leaves the rows alone";
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(0, 480));
    EXPECT_EQ(milo::Look().texPassMaxSide, 1536) << "0 leaves the cap alone";
    EXPECT_EQ(Side1024(1080), 1536) << "rows 480 under cap 1536";
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(-1, 0));
    EXPECT_EQ(milo::Look().texPassMaxSide, 2048) << "the cap's rule";
    EXPECT_EQ(milo::Look().texPassBaseRows, 480) << "the cap's rule leaves the rows";
    EXPECT_EQ(Side1024(713), 1521) << "rows 480";
    EXPECT_EQ(Side1024(720), 1536) << "rows 480";
    EXPECT_EQ(Side1024(1080), 2048) << "rows 480, cap 2048";
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(0, 720));
    EXPECT_EQ(Side1024(713), 1024) << "rows 720";
    EXPECT_EQ(Side1024(1080), 1536) << "rows 720";
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(0, 0));
    EXPECT_EQ(milo::Look().texPassMaxSide, 2048) << "0, 0 changes nothing";
    EXPECT_EQ(milo::Look().texPassBaseRows, 720) << "0, 0 changes nothing";
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(1024, 0));
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(-1, -1));
    EXPECT_EQ(Side1024(713), 2028) << "the rule";
    EXPECT_EQ(Side1024(720), 2048) << "the rule";
    EXPECT_EQ(Side1024(1440), 2048) << "the rule";
    EXPECT_EQ(Side1024(2160), 2048) << "the rule";
    // The numbers only: step 2 stays as it was, on and off.
    EXPECT_TRUE(milo::Look().texPassScale && !milo::Look().texPassScaleIntegrated);
    ASSERT_TRUE(milo::SetLookTexPassScaleForInspection(0));
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(1536, 480));
    EXPECT_FALSE(milo::Look().texPassScale || milo::Look().texPassScaleIntegrated);
}

// Under the Native profile (rb3, rb3-xenon) the setter refuses and nothing moves.
// Sabotage: the setter not refusing under Native -> red (seen, reverted).
TEST(LookTexPass, NativeRefusesTheSizeSetter) {
    milo::ScopedLookOverride look(milo::LookProfile::Native);
    EXPECT_FALSE(milo::SetLookTexPassSizeForInspection(1536, 480));
    EXPECT_FALSE(milo::SetLookTexPassSizeForInspection(-1, -1));
    EXPECT_EQ(milo::Look().texPassMaxSide, 2048);
    EXPECT_EQ(milo::Look().texPassBaseRows, 360);
    EXPECT_EQ(Side1024(713), 2028);
    EXPECT_EQ(Side1024(1440), 2048);
    EXPECT_FALSE(milo::Look().texPassScale || milo::Look().texPassScaleIntegrated);
}

// The tokens, through Rnd's handler as the HTTP eval reaches it; rtt_rule
// returns both numbers as well as both steps.
// Sabotage: rtt_rule not returning the two numbers -> red; the setter
// ignoring maxSide -> red (both seen, reverted).
class LookTexPassTokens : public EngineTestFixture {
protected:
    static void Token(const char *t) {
        char text[96];
        snprintf(text, sizeof(text), "(nil native_look %s)", t);
        DataArray *file = DataReadString(text);
        TheRnd.Handle(file->Array(0), true);
        file->Release();
    }
};

TEST_F(LookTexPassTokens, TheSizeTokensAndRttRule) {
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    const milo::LookSettings &s = milo::Look();
    struct Case { const char *token; int cap, rows; } cases[] = {
        {"rtt_cap_1536", 1536, 360}, {"rtt_cap_1024", 1024, 360}, {"rtt_cap_2048", 2048, 360},
        {"rtt_rows_480", 2048, 480}, {"rtt_rows_720", 2048, 720}, {"rtt_rows_360", 2048, 360},
        {"rtt_cap_1536", 1536, 360}, {"rtt_rows_720", 1536, 720}, {"rtt_cap_rule", 2048, 720},
        {"rtt_rows_rule", 2048, 360},
    };
    for (const Case &c : cases) {
        Token(c.token);
        EXPECT_EQ(s.texPassMaxSide, c.cap) << c.token;
        EXPECT_EQ(s.texPassBaseRows, c.rows) << c.token;
    }
    Token("rtt_off");
    Token("rtt_cap_1024");
    Token("rtt_rows_480");
    EXPECT_FALSE(s.texPassScale) << "a size token does not turn step 2 on";
    Token("rtt_rule");
    EXPECT_TRUE(s.texPassMsaa && s.texPassScale) << "rtt_rule: both steps the profile's";
    EXPECT_EQ(s.texPassMaxSide, 2048) << "rtt_rule: the profile's cap";
    EXPECT_EQ(s.texPassBaseRows, 360) << "rtt_rule: the profile's rows";
}

// The box filter a scaled target is read back through: 1.5 texels a texel.
TEST(LookTexPass, ReadbackBoxFilterAveragesTheFootprint) {
    // 3x1 source: red, green, blue -> 2x1: (red + half green), (half green + blue).
    const uint8_t src[12] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    std::vector<uint8_t> dst;
    BoxFilterRGBA8(src, 3, 1, dst, 2, 1);
    ASSERT_EQ(dst.size(), 8u);
    EXPECT_EQ(dst[0], 170); EXPECT_EQ(dst[1], 85); EXPECT_EQ(dst[2], 0); EXPECT_EQ(dst[3], 255);
    EXPECT_EQ(dst[4], 0); EXPECT_EQ(dst[5], 85); EXPECT_EQ(dst[6], 170); EXPECT_EQ(dst[7], 255);
}

class TexPassGpuTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }
    static RndTex *NewTarget(int w, int h, RndTex::Type type) {
        RndTex *tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(w, h, 32, type, false, nullptr);
        return tex;
    }
    static uint32_t Samples(RndTex *tex) {
        GpuTexPassTarget t;
        if (!GetGpuTexPassTarget(tex, t)) return 0;
        return t.samples;
    }
};

TEST_F(TexPassGpuTest, EngineHalfIsAColourAndDepthTargetWithNoMips) {
    ScopedPredicate yes(&AnyTarget);
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    milo::SetLookTexPassMsaaForInspection(1);  // both adapter classes (the scope restores it)
    milo::SetLookTexPassScaleForInspection(0);
    struct Case { RndTex::Type type; uint32_t samples; const char *what; } cases[] = {
        {RndTex::kRendered, WgpuRnd::kTexPassMsaaSamples, "kRendered (projection.tex)"},
        {RndTex::kRenderedNoZ, 1, "kRenderedNoZ (blender output, recorder buffers)"},
        {RndTex::kShadowMap, 1, "kShadowMap (self-shadow map)"},
        {RndTex::kDepthVolumeMap, 1, "kDepthVolumeMap"},
        {RndTex::kDensityMap, 1, "kDensityMap"},
    };
    for (const Case &c : cases) {
        RndTex *tex = NewTarget(64, 64, c.type);
        EXPECT_EQ(Samples(tex), c.samples) << c.what;
        GpuTexPassTarget t;
        ASSERT_TRUE(GetGpuTexPassTarget(tex, t)) << c.what;
        // Step 1 alone never changes a target's size (step 2 is off here).
        EXPECT_EQ(t.gpuWidth, 64u) << c.what;
        EXPECT_EQ(t.gpuHeight, 64u) << c.what;
        if (c.samples > 1) {
            EXPECT_TRUE(t.msaaView && t.msaaDepthView) << c.what;
        } else {
            EXPECT_FALSE(t.msaaView) << c.what;
        }
        delete tex;
    }
    // A kRendered target with a mip chain is a crowd impostor sheet
    // (WorldCrowd's gImpostorTex): one sample, its own size.
    RndTex *impostor = Hmx::Object::New<RndTex>();
    impostor->SetBitmap(64, 128, 32, RndTex::kRendered, true, nullptr);
    ASSERT_GT(impostor->NumMips(), 0);
    EXPECT_EQ(Samples(impostor), 1u) << "kRendered with mips (impostor)";
    delete impostor;
}

TEST_F(TexPassGpuTest, NoPredicateDrawsNothingLikeTheFrame) {
    ScopedPredicate none(nullptr);
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    milo::SetLookTexPassMsaaForInspection(1);
    RndTex *tex = NewTarget(64, 64, RndTex::kRendered);
    GpuTexPassTarget t;
    ASSERT_TRUE(GetGpuTexPassTarget(tex, t));
    EXPECT_EQ(t.samples, 1u);
    EXPECT_FALSE(t.likeFrame);
    EXPECT_FALSE(t.msaaView);
    delete tex;
}

// Asked at a clearing pass only: sampling calls (GetGpuTexView, a pass
// resumed with a load) and a change in the predicate's answer leave the kept
// answer and the 4x pair alone until the next clearing pass.
TEST_F(TexPassGpuTest, TheAnswerIsKeptUntilTheNextClearingPass) {
    ScopedPredicate sw(&SwitchedTarget);
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    milo::SetLookTexPassMsaaForInspection(1);
    milo::SetLookTexPassScaleForInspection(0);
    sSwitchAnswer = false;
    RndTex *tex = NewTarget(64, 64, RndTex::kRendered);
    // A sampling call before any pass: the rule is not asked, one sample.
    EXPECT_TRUE(GetGpuTexView(tex));
    sSwitchAnswer = true;
    GpuTexPassTarget t;
    ASSERT_TRUE(GetGpuTexPassTarget(tex, t, false));
    EXPECT_EQ(t.samples, 1u) << "a resumed pass does not ask";
    ASSERT_TRUE(GetGpuTexPassTarget(tex, t, true));
    EXPECT_EQ(t.samples, WgpuRnd::kTexPassMsaaSamples);
    EXPECT_TRUE(t.likeFrame);
    const wgpu::TextureView pair = t.msaaView;
    sSwitchAnswer = false;
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(GetGpuTexView(tex));
        ASSERT_TRUE(GetGpuTexPassTarget(tex, t, false));
        EXPECT_EQ(t.samples, WgpuRnd::kTexPassMsaaSamples) << "sampling call " << i;
        EXPECT_TRUE(t.likeFrame) << "sampling call " << i;
        EXPECT_TRUE(t.msaaView.Get() == pair.Get()) << "the pair is kept, " << i;
    }
    ASSERT_TRUE(GetGpuTexPassTarget(tex, t, true));
    EXPECT_EQ(t.samples, 1u) << "the next clearing pass takes the new answer";
    EXPECT_FALSE(t.likeFrame);
    EXPECT_FALSE(t.msaaView);
    sSwitchAnswer = true;
    delete tex;
}

TEST_F(TexPassGpuTest, RetailAndNativeKeepOneSample) {
    ScopedPredicate yes(&AnyTarget);
    for (milo::LookProfile p : {milo::LookProfile::Retail, milo::LookProfile::Native}) {
        milo::ScopedLookOverride look(p);
        RndTex *tex = NewTarget(64, 64, RndTex::kRendered);
        GpuTexPassTarget t;
        ASSERT_TRUE(GetGpuTexPassTarget(tex, t));
        EXPECT_EQ(t.samples, 1u) << milo::LookProfileName(p);
        EXPECT_EQ(t.gpuWidth, 64u) << milo::LookProfileName(p);
        EXPECT_EQ(t.gpuHeight, 64u) << milo::LookProfileName(p);
        delete tex;
    }
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET
// TexRendererClearTest's first case under the enhanced look: the clear goes
// through the multisampled pass (WgpuRnd::Clear restarts it) and survives the
// resolve, and the readback is the texture's own size.
TEST_F(TexPassGpuTest, AClearReachesAMultisampledTargetThroughTheResolve) {
    ScopedPredicate yes(&AnyTarget);  // the engine's pass, not dc3's rule
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    milo::SetLookTexPassMsaaForInspection(1);
    RndTex *tex = NewTarget(64, 48, RndTex::kRendered);
    ASSERT_EQ(Samples(tex), WgpuRnd::kTexPassMsaaSamples);
    RndCam *cam = Hmx::Object::New<RndCam>();
    RndGroup *nothing = Hmx::Object::New<RndGroup>();
    RndTexRenderer *tr = Hmx::Object::New<RndTexRenderer>();
    tr->SetProperty("output_texture", DataNode(tex));
    tr->SetProperty("draw", DataNode(nothing));
    tr->SetProperty("cam", DataNode(cam));
    tr->SetProperty("draw_pre_clear", DataNode(0));
    tr->SetProperty("clear_buffer", DataNode(1));
    tr->SetProperty("clear_color", DataNode(Hmx::Color(1.0f, 0.0f, 1.0f).Pack()));
    tr->SetProperty("clear_alpha", DataNode(1.0f));
    TheRnd.BeginDrawing();
    tr->DrawShowing();
    TheRnd.EndDrawing();
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    ASSERT_TRUE(ReadbackGpuRenderTarget(tex, px, w, h));
    EXPECT_EQ(w, 64);
    EXPECT_EQ(h, 48);
    int opaqueMagenta = 0;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        opaqueMagenta += px[i] == 255 && px[i + 1] == 0 && px[i + 2] == 255 && px[i + 3] == 255;
    EXPECT_EQ(opaqueMagenta, 64 * 48);
    delete tr;
    delete nothing;
    delete cam;
    delete tex;
}

// np-look47 step 2 on the GPU. The picture: the target cleared to opaque
// magenta, a green rect over its left half (in the target's own pixels,
// DxRnd::DrawRect's rule, RndShape_DC3.h kRectTargetPixels), drawn at 540
// output rows (SetTexPassOutputRowsForTest: x1.5, a fractional scale, so the
// readback's box filter has footprints that straddle texels), so a 64x64
// target is 96x96 on the GPU. While it draws, the drawable records the viewport and the
// attachment it was given.
class HalfGreen : public RndDrawable {
public:
    float rectW = 32.0f;
    float vp[4] = {0, 0, 0, 0};
    uint32_t attW = 0, attH = 0;
    void DrawShowing() override {
        gWgpuRnd->PassViewportPixels(vp);
        attW = gWgpuRnd->CurrentTargetWidth();
        attH = gWgpuRnd->CurrentTargetHeight();
        TheRnd.DrawRect(Hmx::Rect(0.0f, 0.0f, rectW, 64.0f), Hmx::Color(0.0f, 1.0f, 0.0f, 1.0f), nullptr,
                        nullptr, nullptr);
    }
};

class TexPassScaleTest : public TexPassGpuTest {
protected:
    void SetUp() override {
        TexPassGpuTest::SetUp();
        if (IsSkipped()) return;
        // The draw is a plain drawable, so dc3's rule would say no: these
        // test the engine's pass under a predicate the test switches.
        sSwitchAnswer = true;
        mRule.reset(new ScopedPredicate(&SwitchedTarget));
        mLook.reset(new milo::ScopedLookOverride(milo::LookProfile::Enhanced));
        milo::SetLookTexPassMsaaForInspection(1);
        milo::SetLookTexPassScaleForInspection(1);
        SetTexPassOutputRowsForTest(540);
        mTex = NewTarget(64, 64, RndTex::kRendered);
        mCam = Hmx::Object::New<RndCam>();
        mTr = Hmx::Object::New<RndTexRenderer>();
        mTr->SetProperty("output_texture", DataNode(mTex));
        mTr->SetProperty("draw", DataNode(&mDraw));
        mTr->SetProperty("cam", DataNode(mCam));
        mTr->SetProperty("draw_pre_clear", DataNode(0));
        mTr->SetProperty("clear_buffer", DataNode(1));
        mTr->SetProperty("clear_color", DataNode(Hmx::Color(1.0f, 0.0f, 1.0f).Pack()));
        mTr->SetProperty("clear_alpha", DataNode(1.0f));
        mTr->SetProperty("force", DataNode(1));  // draw at every DrawShowing, as projection.rndtex
    }
    void TearDown() override {
        SetTexPassOutputRowsForTest(0);
        delete mTr;
        delete mCam;
        delete mTex;
        mLook.reset();
        mRule.reset();
        sSwitchAnswer = true;
    }
    void Draw() {
        TheRnd.BeginDrawing();
        mTr->DrawShowing();
        TheRnd.EndDrawing();
    }
    uint32_t GpuWidth() {
        GpuTexPassTarget t;  // a pass that loads: never re-sizes
        return GetGpuTexPassTarget(mTex, t, false, false) ? t.gpuWidth : 0;
    }
    // Texels read back (at the RndTex's own size) that are green / magenta.
    void Count(int &green, int &magenta, int &w, int &h, int &leftGreen) {
        std::vector<uint8_t> px;
        green = magenta = leftGreen = 0;
        ASSERT_TRUE(ReadbackGpuRenderTarget(mTex, px, w, h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const uint8_t *p = &px[((size_t)y * w + x) * 4];
                const bool g = p[0] < 8 && p[1] > 247 && p[2] < 8 && p[3] == 255;
                green += g;
                leftGreen += g && x < 32;
                magenta += p[0] > 247 && p[1] < 8 && p[2] > 247 && p[3] == 255;
            }
    }
    std::unique_ptr<ScopedPredicate> mRule;
    std::unique_ptr<milo::ScopedLookOverride> mLook;
    RndTex *mTex = nullptr;
    RndCam *mCam = nullptr;
    RndTexRenderer *mTr = nullptr;
    HalfGreen mDraw;
};

// Step 2 sizes by the kept answer: a change in the rule's answer moves the
// size only at the next clearing pass.
TEST_F(TexPassScaleTest, TheSizeFollowsTheKeptAnswer) {
    Draw();
    ASSERT_EQ(GpuWidth(), 96u);
    sSwitchAnswer = false;
    EXPECT_TRUE(GetGpuTexView(mTex));
    EXPECT_EQ(GpuWidth(), 96u) << "sampling calls keep the size";
    Draw();
    EXPECT_EQ(GpuWidth(), 64u) << "the next clearing pass takes the new answer";
    sSwitchAnswer = true;
    Draw();
    EXPECT_EQ(GpuWidth(), 96u);
}

// Sabotage: ReadbackGpuRenderTarget returning the GPU texture unfiltered
// reads back 96x96 -> red (seen, reverted).
TEST_F(TexPassScaleTest, AScaledTargetReadsBackAtItsOwnSizeWithThePictureDrawn) {
    Draw();
    EXPECT_EQ(GpuWidth(), 96u) << "540 output rows: a 64 target is drawn at 96";
    int green, magenta, w, h, leftGreen;
    Count(green, magenta, w, h, leftGreen);
    EXPECT_EQ(w, 64);
    EXPECT_EQ(h, 64);
    EXPECT_EQ(leftGreen, 32 * 64) << "the rect's half";
    EXPECT_EQ(green, 32 * 64);
    EXPECT_EQ(magenta, 32 * 64) << "the clear's half";
}

// Sabotage: re-sizing at the sampling call (GetGpuTexView running the
// clearing-pass resize, as round 1's EnsureRenderTargetData did) -> the size
// is 64 after the flip and the picture is gone -> red (seen, reverted).
TEST_F(TexPassScaleTest, ASizeChangeWithNoRedrawKeepsThePicture) {
    Draw();
    ASSERT_EQ(GpuWidth(), 96u);
    // The output changes size (360 rows: the target would be 64) and the
    // scale token flips off; nothing draws into the target, but a draw
    // samples it, and a pass resumes it with a load.
    SetTexPassOutputRowsForTest(360);
    milo::SetLookTexPassScaleForInspection(0);
    EXPECT_TRUE(GetGpuTexView(mTex));
    EXPECT_EQ(GpuWidth(), 96u) << "the size changes only at a clearing pass";
    int green, magenta, w, h, leftGreen;
    Count(green, magenta, w, h, leftGreen);
    EXPECT_EQ(leftGreen, 32 * 64);
    EXPECT_EQ(magenta, 32 * 64);
    // The next clearing pass re-makes it at its own size, and draws it.
    Draw();
    EXPECT_EQ(GpuWidth(), 64u);
    Count(green, magenta, w, h, leftGreen);
    EXPECT_EQ(leftGreen, 32 * 64);
    EXPECT_EQ(magenta, 32 * 64);
}

// A full rect and a full viewport cover all of a scaled target: the viewport
// is the attachment's (96x96), not the texture's 64 nor 64 scaled twice.
// DC3's RndCam::Select sets the viewport in the target's own pixels
// (mTargetTex->Width()), and FrameViewport scales it to the attachment.
// Sabotage: FrameViewport leaving a texture pass's viewport unscaled -> the
// viewport is 64 wide and the rect covers 64 of 96 -> red (seen, reverted).
// (ApplyCameraViewport, the rb3 path, kCamSelectSetsViewport false, is not
// reached by DC3: making it read the attachment's width stays green here.)
TEST_F(TexPassScaleTest, AFullRectAndAFullViewportCoverAScaledTarget) {
    mDraw.rectW = 64.0f;
    Draw();
    EXPECT_EQ(mDraw.attW, 96u);
    EXPECT_EQ(mDraw.attH, 96u);
    EXPECT_FLOAT_EQ(mDraw.vp[0], 0.0f);
    EXPECT_FLOAT_EQ(mDraw.vp[1], 0.0f);
    EXPECT_FLOAT_EQ(mDraw.vp[2], 96.0f);
    EXPECT_FLOAT_EQ(mDraw.vp[3], 96.0f);
    int green, magenta, w, h, leftGreen;
    Count(green, magenta, w, h, leftGreen);
    EXPECT_EQ(green, 64 * 64) << "a full rect leaves no texel of the clear";
}

// A CPU write to a scaled target puts it at its own size and keeps what was
// written, and later passes keep it there.
// Sabotage: UploadRGBAToRndTex not setting cpuWritten -> the next pass makes
// it 96 again -> red (seen, reverted).
TEST_F(TexPassScaleTest, ACpuWriteHoldsTheTargetAtItsOwnSize) {
    Draw();
    ASSERT_EQ(GpuWidth(), 96u);
    std::vector<uint8_t> blue((size_t)64 * 64 * 4, 0);
    for (size_t i = 0; i < blue.size(); i += 4) blue[i + 2] = blue[i + 3] = 255;
    UploadRGBAToRndTex(mTex, blue.data(), 64, 64);
    EXPECT_EQ(GpuWidth(), 64u);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    ASSERT_TRUE(ReadbackGpuRenderTarget(mTex, px, w, h));
    ASSERT_EQ(w, 64);
    EXPECT_EQ(px, blue);
    Draw();
    EXPECT_EQ(GpuWidth(), 64u) << "held at its own size after a CPU write";
}

// np-look49: the size setter (rtt_cap_* / rtt_rows_*) moves a scaled target's
// GPU size at its next clearing pass, not at a sampling call or a resumed
// pass. 540 rows: base rows 360 -> x1.5 (96), cap 80 -> 80, rows 480 ->
// x1.125 (72). The fixture's look scope restores both numbers.
// Sabotage: GetGpuTexView re-sizing at the sampling call -> red; a scaled
// target never re-sized to another scaled size (only to or from its own)
// -> red; the setter ignoring maxSide -> red (all seen, reverted).
TEST_F(TexPassScaleTest, TheSizeSetterTakesEffectAtTheNextClearingPass) {
    Draw();
    ASSERT_EQ(GpuWidth(), 96u);
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(80, 0));
    EXPECT_TRUE(GetGpuTexView(mTex));
    EXPECT_EQ(GpuWidth(), 96u) << "a sampling call or a resumed pass keeps the size";
    Draw();
    EXPECT_EQ(GpuWidth(), 80u) << "the next clearing pass takes the cap";
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(-1, 480));
    EXPECT_TRUE(GetGpuTexView(mTex));
    EXPECT_EQ(GpuWidth(), 80u) << "a sampling call or a resumed pass keeps the size";
    Draw();
    EXPECT_EQ(GpuWidth(), 72u) << "the next clearing pass takes the rows";
    int green, magenta, w, h, leftGreen;
    Count(green, magenta, w, h, leftGreen);
    EXPECT_EQ(w, 64);
    EXPECT_EQ(leftGreen, 32 * 64) << "drawn at the new size";
    EXPECT_EQ(magenta, 32 * 64);
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(-1, -1));
    Draw();
    EXPECT_EQ(GpuWidth(), 96u) << "the rule";
}
#endif

} // namespace
