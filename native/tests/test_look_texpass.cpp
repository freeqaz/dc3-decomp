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
#include <cstring>
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
        EXPECT_TRUE(milo::LookTexPassMsaa(enh, milo::LookAdapterClass::Discrete) && !milo::LookTexPassMsaa(enh, milo::LookAdapterClass::OtherNonDiscrete));
        EXPECT_TRUE(enh.texPassScale && !enh.texPassScaleIntegrated);
        EXPECT_TRUE(milo::LookTexPassScale(enh, milo::LookAdapterClass::Discrete) && !milo::LookTexPassScale(enh, milo::LookAdapterClass::OtherNonDiscrete));
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
        milo::TexPassGpuSize(enh, milo::LookAdapterClass::Discrete, 1024, 1024, c.rows, w, h);
        EXPECT_EQ(w, c.side) << c.rows << " rows";
        EXPECT_EQ(h, c.side) << c.rows << " rows";
    }
    int w = 0, h = 0;
    milo::TexPassGpuSize(enh, milo::LookAdapterClass::Discrete, 640, 480, 540, w, h);  // x1.5 keeps the shape
    EXPECT_EQ(w, 960);
    EXPECT_EQ(h, 720);
    milo::TexPassGpuSize(enh, milo::LookAdapterClass::Discrete, 640, 480, 720, w, h);  // x2
    EXPECT_EQ(w, 1280);
    EXPECT_EQ(h, 960);
    milo::TexPassGpuSize(enh, milo::LookAdapterClass::Discrete, 640, 480, 2160, w, h);  // the longer side at the cap
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
            milo::TexPassGpuSize(enh, milo::LookAdapterClass::Discrete, 1024, 1024, f.outputH, w, h);
            EXPECT_EQ(w, win.side) << win.h << " rows, scale " << scale;
        }
    }
}

// The side TexPassGpuSize gives a 1024x1024 target at `rows` under the look
// in force (both sides: the target is square), for the discrete class unless
// another is named (np-look49).
int Side1024(int rows, milo::LookAdapterClass c = milo::LookAdapterClass::Discrete) {
    int w = 0, h = 0;
    milo::TexPassGpuSize(milo::Look(), c, 1024, 1024, rows, w, h);
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

// ---- np-look49: three adapter classes for the look's per-adapter rules ----
// The four rules (the depth of field's near field and near spread, the texture
// pass's MSAA and size) are decided by milo::LookAdapterClass: discrete,
// Apple (not discrete, vendor 0x106B, Metal), other non-discrete. Everything
// that sizes the frame keeps GpuDevice::IsDiscreteGpu() and is not tested
// here. Sabotages, each seen red and reverted: the Metal condition dropped ->
// TheClassificationTable (MoltenVK read Apple); the vendor condition dropped
// -> TheClassificationTable (Intel graphics on Metal read Apple); other
// non-discrete given the Apple msaa value -> OtherNonDiscreteIsTodays,
// TheSettersWriteAllThreeClasses, TheClassLineWording and
// EnhancedTurnsOnStep1RetailAndNativeNot; other non-discrete given the Apple
// near field -> OtherNonDiscreteIsTodays, TheClassLineWording and
// DC3LookDofNear.TheNearFieldFollowsTheAdapterClass; the msaa setter not
// writing the Apple value -> TheSettersWriteAllThreeClasses; the scale picker
// returning the discrete value for Apple -> TheAppleValues,
// TheClassLineWording.
namespace {

using milo::LookAdapterClass;
using milo::LookAdapterType;
using milo::LookProfile;

// One rule's outcome on each class.
struct ClassOutcomes {
    bool nearField, nearSpread, texPassMsaa, texPassScale;
};

ClassOutcomes OutcomesFor(LookAdapterClass c) {
    const milo::LookSettings &s = milo::Look();
    return {milo::LookDofNearField(s, c), milo::LookDofNearSpread(s, c), milo::LookTexPassMsaa(s, c),
            milo::LookTexPassScale(s, c)};
}

std::string Describe(const ClassOutcomes &o) {
    char b[96];
    snprintf(b, sizeof(b), "near field %d, near spread %d, msaa %d, scale %d", (int)o.nearField,
             (int)o.nearSpread, (int)o.texPassMsaa, (int)o.texPassScale);
    return b;
}

bool Same(const ClassOutcomes &a, const ClassOutcomes &b) {
    return a.nearField == b.nearField && a.nearSpread == b.nearSpread && a.texPassMsaa == b.texPassMsaa
           && a.texPassScale == b.texPassScale;
}

const LookProfile kProfiles[] = {LookProfile::Enhanced, LookProfile::Retail, LookProfile::Native};

}  // namespace

// The classification, walked without a device.
TEST(LookAdapterClasses, TheClassificationTable) {
    const uint32_t kNvidia = 0x10DE, kAmd = 0x1002, kIntel = 0x8086, kApple = 0x106B;
    struct Row {
        LookAdapterType type;
        uint32_t vendor;
        bool metal;
        LookAdapterClass want;
        const char *what;
    };
    const Row rows[] = {
        {LookAdapterType::Discrete, kNvidia, false, LookAdapterClass::Discrete, "an NVIDIA card on Vulkan"},
        {LookAdapterType::Discrete, kAmd, true, LookAdapterClass::Discrete, "an Intel Mac's Radeon on Metal"},
        {LookAdapterType::Discrete, kApple, true, LookAdapterClass::Discrete, "a discrete adapter stays discrete"},
        {LookAdapterType::Integrated, kApple, true, LookAdapterClass::Apple, "Apple silicon on Metal"},
        {LookAdapterType::Integrated, kApple, false, LookAdapterClass::OtherNonDiscrete,
         "Apple silicon through MoltenVK (Vulkan)"},
        {LookAdapterType::Integrated, kIntel, true, LookAdapterClass::OtherNonDiscrete,
         "an Intel Mac's Intel graphics on Metal"},
        {LookAdapterType::Integrated, kAmd, false, LookAdapterClass::OtherNonDiscrete, "the 890M on Vulkan"},
        {LookAdapterType::Cpu, kApple, true, LookAdapterClass::OtherNonDiscrete, "a CPU adapter, even Apple's"},
        {LookAdapterType::Cpu, kNvidia, false, LookAdapterClass::OtherNonDiscrete, "a CPU adapter"},
        {LookAdapterType::Unknown, kApple, true, LookAdapterClass::OtherNonDiscrete,
         "an unknown type, even Apple's"},
        {LookAdapterType::Unknown, 0, false, LookAdapterClass::OtherNonDiscrete, "an unknown type"},
        {LookAdapterType::Integrated, 0, true, LookAdapterClass::OtherNonDiscrete, "vendor 0 on Metal"},
        {LookAdapterType::Integrated, 0, false, LookAdapterClass::OtherNonDiscrete, "vendor 0 on Vulkan"},
    };
    for (const Row &r : rows)
        EXPECT_EQ((int)milo::ClassifyLookAdapter(r.type, r.vendor, r.metal), (int)r.want) << r.what;
    milo::LookAdapterFacts none;  // a device that never got an adapter (the web build before its adapter)
    EXPECT_EQ((int)milo::ClassifyLookAdapter(none), (int)LookAdapterClass::OtherNonDiscrete);
    EXPECT_EQ((int)GpuDevice().LookClass(), (int)LookAdapterClass::OtherNonDiscrete)
        << "a device with no adapter is other non-discrete";
}

// The pass-for-pass promise for the handheld and the web: the other
// non-discrete class gets, in every profile and for each of the four rules,
// exactly what today's one non-discrete class (the ...Integrated values) got,
// and the discrete class what it got. The table is today's values, written
// out: a change to either class's value has to change this table too.
TEST(LookAdapterClasses, OtherNonDiscreteIsTodays) {
    struct Row {
        LookProfile profile;
        ClassOutcomes discrete, other;
    };
    const Row today[] = {
        {LookProfile::Enhanced, {true, true, true, true}, {true, true, false, false}},
        {LookProfile::Retail, {false, false, false, false}, {false, false, false, false}},
        {LookProfile::Native, {false, false, false, false}, {false, false, false, false}},
    };
    for (const Row &r : today) {
        milo::ScopedLookOverride look(r.profile);
        const milo::LookSettings &s = milo::Look();
        const ClassOutcomes other = OutcomesFor(LookAdapterClass::OtherNonDiscrete);
        const ClassOutcomes integrated = {s.postDofNearFieldIntegrated, s.postDofNearSpreadIntegrated,
                                          s.texPassMsaaIntegrated, s.texPassScaleIntegrated};
        EXPECT_TRUE(Same(other, r.other)) << milo::LookProfileName(r.profile) << ": other non-discrete "
                                          << Describe(other) << ", today " << Describe(r.other);
        EXPECT_TRUE(Same(other, integrated)) << milo::LookProfileName(r.profile)
                                             << ": the picker is the ...Integrated values";
        const ClassOutcomes discrete = OutcomesFor(LookAdapterClass::Discrete);
        EXPECT_TRUE(Same(discrete, r.discrete)) << milo::LookProfileName(r.profile) << ": discrete "
                                                << Describe(discrete) << ", today " << Describe(r.discrete);
        // With step 2 forced on, a handheld got the discrete GPU's size (one
        // pair of numbers), and still does: the size table at each row count
        // is the discrete class's.
        if (r.profile != LookProfile::Native) {
            ASSERT_TRUE(milo::SetLookTexPassScaleForInspection(1));
            for (int rows : {360, 540, 713, 720, 1080, 1440})
                EXPECT_EQ(Side1024(rows, LookAdapterClass::OtherNonDiscrete), Side1024(rows))
                    << milo::LookProfileName(r.profile) << " " << rows << " rows";
        }
    }
}

// The Apple class's values, one table: enhanced near field, near spread,
// msaa and scale on (np-look49, timed on an M3 Air by the landing session on
// 2026-11-21); retail and native all off; the size rule's cap 1536 and base
// rows 720 in every profile (the numbers act only with the scale on). A
// change to LookProfile.cpp's kApple* constants changes this table, and the
// size table below.
TEST(LookAdapterClasses, TheAppleValues) {
    struct Row {
        LookProfile profile;
        ClassOutcomes apple;
        int cap, baseRows;
    };
    const Row kApple[] = {
        {LookProfile::Enhanced, {true, true, true, true}, 1536, 720},
        {LookProfile::Retail, {false, false, false, false}, 1536, 720},
        {LookProfile::Native, {false, false, false, false}, 1536, 720},
    };
    for (const Row &r : kApple) {
        milo::ScopedLookOverride look(r.profile);
        const ClassOutcomes apple = OutcomesFor(LookAdapterClass::Apple);
        EXPECT_TRUE(Same(apple, r.apple)) << milo::LookProfileName(r.profile) << ": Apple " << Describe(apple)
                                          << ", the table " << Describe(r.apple);
        EXPECT_EQ(milo::LookTexPassMaxSide(milo::Look(), LookAdapterClass::Apple), r.cap)
            << milo::LookProfileName(r.profile);
        EXPECT_EQ(milo::LookTexPassBaseRows(milo::Look(), LookAdapterClass::Apple), r.baseRows)
            << milo::LookProfileName(r.profile);
    }
    // Enhanced's Apple size table: a 1024 projection texture is 1024 (step 1
    // alone; never below its own size) up to 720 output rows, 1536 from 1080
    // rows up (2048 at 1440 cut to the cap); a 512 one grows from 720 rows.
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    struct Size {
        int rows, side1024, side512;
    };
    const Size kAppleSizes[] = {
        {360, 1024, 512}, {540, 1024, 512}, {713, 1024, 512},
        {720, 1024, 512}, {1080, 1536, 768}, {1440, 1536, 1024},
    };
    for (const Size &z : kAppleSizes) {
        for (int own : {1024, 512}) {
            int w = 0, h = 0;
            milo::TexPassGpuSize(milo::Look(), LookAdapterClass::Apple, own, own, z.rows, w, h);
            const int want = own == 1024 ? z.side1024 : z.side512;
            EXPECT_EQ(w, want) << "Apple, " << own << " texture, " << z.rows << " rows";
            EXPECT_EQ(h, want) << "Apple, " << own << " texture, " << z.rows << " rows";
        }
    }
}

// The size rule's two numbers are per class: on a local copy with Apple's
// pair moved, Apple's size follows Apple's pair, discrete and other
// non-discrete their own; and the size setter writes all three classes for
// each number, its rule returning each to the profile's.
// Sabotages, each seen red and reverted: the int chooser returning the
// discrete cap / rows for Apple -> red (and TheAppleValues, the two line
// tests; rows also TheClassTokenReachesTheSize); SettingsFor reading Apple's
// cap / rows from the discrete field -> the same; the size setter not writing
// Apple's cap / rows -> red (it went unseen while the test set 1536 and 720,
// Apple's own numbers: the test now sets 1024 and 480).
TEST(LookAdapterClasses, TheSizeNumbersArePerClass) {
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    milo::LookSettings copy = milo::Look();
    copy.texPassMaxSideApple = 1536;  // the profile's own as of the M3 timing; set so the table stands alone
    copy.texPassBaseRowsApple = 720;
    copy.texPassMaxSideIntegrated = 1024 + 512 + 256;  // 1792: its own pair
    copy.texPassBaseRowsIntegrated = 480;
    auto side = [&](LookAdapterClass c, int rows) {
        int w = 0, h = 0;
        milo::TexPassGpuSize(copy, c, 1024, 1024, rows, w, h);
        EXPECT_EQ(w, h);
        return w;
    };
    struct Row {
        int rows, discrete, apple, other;
    };
    const Row rows[] = {
        {713, 2028, 1024, 1521},   // Apple: x0.99, never below the texture's own
        {1080, 2048, 1536, 1792},  // Apple: x1.5; other: x2.25 at its cap
        {1440, 2048, 1536, 1792},
    };
    for (const Row &r : rows) {
        EXPECT_EQ(side(LookAdapterClass::Discrete, r.rows), r.discrete) << r.rows << " rows, discrete";
        EXPECT_EQ(side(LookAdapterClass::Apple, r.rows), r.apple) << r.rows << " rows, Apple";
        EXPECT_EQ(side(LookAdapterClass::OtherNonDiscrete, r.rows), r.other) << r.rows << " rows, other";
    }
    const LookAdapterClass classes[] = {LookAdapterClass::Discrete, LookAdapterClass::Apple,
                                        LookAdapterClass::OtherNonDiscrete};
    // 1024 and 480: no class's own number, so a class the setter skips shows.
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(1024, 0));
    for (LookAdapterClass c : classes) {
        EXPECT_EQ(milo::LookTexPassMaxSide(milo::Look(), c), 1024) << "cap, class " << (int)c;
        EXPECT_EQ(milo::LookTexPassBaseRows(milo::Look(), c), c == LookAdapterClass::Apple ? 720 : 360)
            << "0 leaves the rows, class " << (int)c;
    }
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(0, 480));
    for (LookAdapterClass c : classes) {
        EXPECT_EQ(milo::LookTexPassMaxSide(milo::Look(), c), 1024) << "0 leaves the cap, class " << (int)c;
        EXPECT_EQ(milo::LookTexPassBaseRows(milo::Look(), c), 480) << "rows, class " << (int)c;
    }
    ASSERT_TRUE(milo::SetLookTexPassSizeForInspection(-1, -1));
    const milo::LookSettings &s = milo::Look();
    EXPECT_EQ(s.texPassMaxSide, 2048);
    EXPECT_EQ(s.texPassMaxSideApple, 1536);
    EXPECT_EQ(s.texPassMaxSideIntegrated, 2048);
    EXPECT_EQ(s.texPassBaseRows, 360);
    EXPECT_EQ(s.texPassBaseRowsApple, 720);
    EXPECT_EQ(s.texPassBaseRowsIntegrated, 360);
}

// Every inspection setter of the four rules writes all three classes: on
// forces each on, off each off, and the rule (-1) returns each class to the
// profile's own value.
TEST(LookAdapterClasses, TheSettersWriteAllThreeClasses) {
    const LookAdapterClass classes[] = {LookAdapterClass::Discrete, LookAdapterClass::Apple,
                                        LookAdapterClass::OtherNonDiscrete};
    const char *names[] = {"discrete", "Apple", "other"};
    struct Rule {
        const char *what;
        bool (*set)(int);
        bool (*get)(const milo::LookSettings &, LookAdapterClass);
    };
    const Rule rules[] = {
        {"near field", [](int m) { return milo::SetLookDofNearForInspection(m, false); }, milo::LookDofNearField},
        {"near spread", [](int m) { return milo::SetLookDofNearForInspection(m, true); }, milo::LookDofNearSpread},
        {"texture pass msaa", milo::SetLookTexPassMsaaForInspection, milo::LookTexPassMsaa},
        {"texture pass scale", milo::SetLookTexPassScaleForInspection, milo::LookTexPassScale},
        {"depth of field edge rule", milo::SetLookDofEdgeForInspection, milo::LookDofEdge},  // np-look51
    };
    for (LookProfile p : {LookProfile::Enhanced, LookProfile::Retail}) {
        milo::ScopedLookOverride look(p);
        for (const Rule &r : rules) {
            bool profiles[3];
            for (int i = 0; i < 3; i++) profiles[i] = r.get(milo::Look(), classes[i]);
            for (int mode : {1, 0}) {
                ASSERT_TRUE(r.set(mode));
                for (int i = 0; i < 3; i++)
                    EXPECT_EQ(r.get(milo::Look(), classes[i]), mode != 0)
                        << milo::LookProfileName(p) << " " << r.what << " " << mode << " on " << names[i];
            }
            ASSERT_TRUE(r.set(-1));
            for (int i = 0; i < 3; i++)
                EXPECT_EQ(r.get(milo::Look(), classes[i]), profiles[i])
                    << milo::LookProfileName(p) << " " << r.what << " the rule on " << names[i];
        }
    }
    // The rule is the profile's even after a token moved every class.
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    ASSERT_TRUE(milo::SetLookTexPassMsaaForInspection(0));
    ASSERT_TRUE(milo::SetLookTexPassMsaaForInspection(-1));
    EXPECT_TRUE(milo::LookTexPassMsaa(milo::Look(), LookAdapterClass::Apple)) << "enhanced's Apple msaa is on";
    EXPECT_FALSE(milo::LookTexPassMsaa(milo::Look(), LookAdapterClass::OtherNonDiscrete));
    {
        milo::ScopedLookOverride native(LookProfile::Native);
        for (const Rule &r : rules) EXPECT_FALSE(r.set(1)) << "Native refuses " << r.what;
    }
}

// The class line's wording (other sessions' scripts grep it).
TEST(LookAdapterClasses, TheClassLineWording) {
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    auto line = [](LookAdapterType type, uint32_t vendor, bool metal, const char *backend) {
        milo::LookAdapterFacts f;
        f.type = type;
        f.vendorId = vendor;
        f.metalBackend = metal;
        f.backendName = backend;
        testing::internal::CaptureStderr();
        milo::PrintLookAdapterClass(f);
        return testing::internal::GetCapturedStderr();
    };
    auto begins = [](const std::string &s, const char *prefix) { return s.rfind(prefix, 0) == 0; };
    const std::string discrete = line(LookAdapterType::Discrete, 0x10DE, false, "Vulkan");
    EXPECT_TRUE(begins(discrete, "[look] adapter class: discrete GPU;")) << discrete;
    EXPECT_EQ(discrete.find("not a discrete GPU"), std::string::npos) << discrete;
    EXPECT_NE(discrete.find("; depth of field near field 1, near spread 1, edge 1, texture pass msaa 1, scale 1 "
                            "(cap 2048, base rows 360) [type discrete, vendor 0x10de, backend Vulkan] "
                            "(profile enhanced)\n"),
              std::string::npos)
        << discrete;
    const std::string apple = line(LookAdapterType::Integrated, 0x106B, true, "Metal");
    EXPECT_TRUE(begins(apple, "[look] adapter class: Apple GPU (not a discrete GPU);")) << apple;
    EXPECT_NE(apple.find("; depth of field near field 1, near spread 1, edge 0, texture pass msaa 1, scale 1 "
                         "(cap 1536, base rows 720) [type integrated, vendor 0x106b, backend Metal] "
                         "(profile enhanced)\n"),
              std::string::npos)
        << apple;
    const std::string other = line(LookAdapterType::Integrated, 0x1002, false, "Vulkan");
    EXPECT_TRUE(begins(other, "[look] adapter class: not a discrete GPU;")) << other;
    EXPECT_NE(other.find("; depth of field near field 1, near spread 1, edge 0, texture pass msaa 0, scale 0 "
                         "(cap 2048, base rows 360) [type integrated, vendor 0x1002, backend Vulkan] "
                         "(profile enhanced)\n"),
              std::string::npos)
        << other;
    const std::string cpu = line(LookAdapterType::Cpu, 0, false, "Vulkan");
    EXPECT_TRUE(begins(cpu, "[look] adapter class: not a discrete GPU;")) << cpu;
    EXPECT_NE(cpu.find("[type cpu, vendor 0x0000, backend Vulkan]"), std::string::npos) << cpu;
    // The setters' lines keep their words and gain the Apple value.
    testing::internal::CaptureStderr();
    milo::SetLookTexPassMsaaForInspection(-1);
    const std::string set = testing::internal::GetCapturedStderr();
    EXPECT_EQ(set, "[look] texture pass msaa 1, 0 on a non-discrete GPU, 1 on an Apple GPU (the rule) "
                   "(inspection, profile enhanced)\n");
    testing::internal::CaptureStderr();
    milo::SetLookTexPassSizeForInspection(1536, 0);
    const std::string size = testing::internal::GetCapturedStderr();
    EXPECT_EQ(size, "[look] texture pass size cap 1536, 1536 on a non-discrete GPU, 1536 on an Apple GPU, base rows "
                    "360, 360 on a non-discrete GPU, 720 on an Apple GPU (the rule) (inspection, profile "
                    "enhanced)\n");
    testing::internal::CaptureStderr();
    milo::SetLookTexPassSizeForInspection(-1, 0);
    const std::string sizeRule = testing::internal::GetCapturedStderr();
    EXPECT_EQ(sizeRule, "[look] texture pass size cap 2048, 2048 on a non-discrete GPU, 1536 on an Apple GPU (the "
                        "rule), base rows 360, 360 on a non-discrete GPU, 720 on an Apple GPU (the rule) "
                        "(inspection, profile enhanced)\n");
}

// ---- np-look51: the depth of field's edge rule (LookSettings::postDofEdge) ----
// Decided by adapter class like the near field: enhanced on for a discrete
// GPU, off for Apple (kAppleDofEdge, until the landing session's M3 timing)
// and for anything else (until the 890M's); retail and Native off. The
// setter is in TheSettersWriteAllThreeClasses; its line and the class
// lines' "edge %d" are below and in the two line tests above; the GPU side is
// test_dc3_look_post.cpp DofEdgeRule*. Sabotages, each seen red and reverted
// (item51/build/sab): SettingsFor reading Apple's value from the discrete
// field -> TheEdgeRuleByClass, TheEdgeRuleLine, TheClassLineWording,
// TheClassTokenLine, TheDofEdgeTokens and the PostProcPass GPU test; the setter
// not writing the non-discrete class -> TheSettersWriteAllThreeClasses,
// TheEdgeRuleLine, TheDofEdgeTokens and the PostProcPass test; the setter not
// refusing Native -> NativeRefusesTheEdgeRule, TheSettersWriteAllThreeClasses,
// TheDofEdgeTokens and the PostProcPass test.
TEST(LookAdapterClasses, TheEdgeRuleByClass) {
    struct Row {
        LookProfile profile;
        bool discrete, apple, other;
    };
    const Row rows[] = {
        {LookProfile::Enhanced, true, false, false},
        {LookProfile::Retail, false, false, false},
        {LookProfile::Native, false, false, false},
    };
    for (const Row &r : rows) {
        milo::ScopedLookOverride look(r.profile);
        const milo::LookSettings &s = milo::Look();
        EXPECT_EQ(milo::LookDofEdge(s, LookAdapterClass::Discrete), r.discrete) << milo::LookProfileName(r.profile);
        EXPECT_EQ(milo::LookDofEdge(s, LookAdapterClass::Apple), r.apple) << milo::LookProfileName(r.profile);
        EXPECT_EQ(milo::LookDofEdge(s, LookAdapterClass::OtherNonDiscrete), r.other)
            << milo::LookProfileName(r.profile);
        EXPECT_EQ(s.postDofEdge, r.discrete);
        EXPECT_EQ(s.postDofEdgeApple, r.apple);
        EXPECT_EQ(s.postDofEdgeIntegrated, r.other);
    }
    // The picker picks by class: a local copy with each class's own value.
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    for (int bits = 0; bits < 8; bits++) {
        milo::LookSettings copy = milo::Look();
        copy.postDofEdge = bits & 1;
        copy.postDofEdgeApple = bits & 2;
        copy.postDofEdgeIntegrated = bits & 4;
        EXPECT_EQ(milo::LookDofEdge(copy, LookAdapterClass::Discrete), (bool)(bits & 1)) << bits;
        EXPECT_EQ(milo::LookDofEdge(copy, LookAdapterClass::Apple), (bool)(bits & 2)) << bits;
        EXPECT_EQ(milo::LookDofEdge(copy, LookAdapterClass::OtherNonDiscrete), (bool)(bits & 4)) << bits;
    }
}

TEST(LookAdapterClasses, NativeRefusesTheEdgeRule) {
    milo::ScopedLookOverride native(LookProfile::Native);
    for (int mode : {1, 0, -1}) {
        EXPECT_FALSE(milo::SetLookDofEdgeForInspection(mode)) << mode;
        const milo::LookSettings &s = milo::Look();
        EXPECT_FALSE(s.postDofEdge || s.postDofEdgeApple || s.postDofEdgeIntegrated)
            << "a refused token changes nothing; mode " << mode;
    }
}

// The setter's line, in the near field's wording.
TEST(LookAdapterClasses, TheEdgeRuleLine) {
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    auto line = [](int mode) {
        testing::internal::CaptureStderr();
        milo::SetLookDofEdgeForInspection(mode);
        return testing::internal::GetCapturedStderr();
    };
    EXPECT_EQ(line(1), "[look] depth of field edge rule 1, 1 on a non-discrete GPU, 1 on an Apple GPU (inspection, "
                       "profile enhanced)\n");
    EXPECT_EQ(line(0), "[look] depth of field edge rule 0, 0 on a non-discrete GPU, 0 on an Apple GPU (inspection, "
                       "profile enhanced)\n");
    EXPECT_EQ(line(-1), "[look] depth of field edge rule 1, 0 on a non-discrete GPU, 0 on an Apple GPU (the rule) "
                        "(inspection, profile enhanced)\n");
    milo::ScopedLookOverride retail(LookProfile::Retail);
    EXPECT_EQ(line(1), "[look] depth of field edge rule 1, 1 on a non-discrete GPU, 1 on an Apple GPU (inspection, "
                       "profile retail)\n");
}

// The three tokens through Rnd's handler. Sabotage: dof_edge_off calling the
// setter with 1 -> red (seen, reverted).
TEST_F(LookTexPassTokens, TheDofEdgeTokens) {
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    const milo::LookSettings &s = milo::Look();
    auto all = [&]() { return (int)s.postDofEdge + 2 * (int)s.postDofEdgeApple + 4 * (int)s.postDofEdgeIntegrated; };
    EXPECT_EQ(all(), 1) << "enhanced: discrete only";
    Token("dof_edge_off");
    EXPECT_EQ(all(), 0);
    Token("dof_edge_on");
    EXPECT_EQ(all(), 7);
    Token("dof_edge_rule");
    EXPECT_EQ(all(), 1);
    milo::ScopedLookOverride native(milo::LookProfile::Native);
    Token("dof_edge_on");
    EXPECT_EQ(all(), 0) << "Native refuses the token";
}

// ---- np-look49: the adapter class token ({rnd native_look adapter_*}) ----
// SetLookAdapterClassForInspection forces the class every caller of
// GpuDevice::LookClass() gets (LookAdapterClassInEffect), whatever the
// adapter; adapter_rule returns each adapter to its own class; Native
// refuses. Sabotages, each seen red and reverted, are below each test.
namespace {

milo::LookAdapterFacts Facts(LookAdapterType type, uint32_t vendor, bool metal, const char *backend) {
    milo::LookAdapterFacts f;
    f.type = type;
    f.vendorId = vendor;
    f.metalBackend = metal;
    f.backendName = backend;
    return f;
}

}  // namespace

// Sabotages: LookAdapterClassInEffect ignoring the forced class -> red (and
// TheClassTokenReachesTheTexturePass); GpuDevice::LookClass() classifying the
// facts itself -> red (and the texture pass test); the rule token not
// clearing the forced class -> red.
TEST(LookAdapterClasses, TheClassTokenReachesEveryPickersCaller) {
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    const milo::LookAdapterFacts adapters[] = {
        Facts(LookAdapterType::Discrete, 0x10DE, false, "Vulkan"),
        Facts(LookAdapterType::Integrated, 0x106B, true, "Metal"),
        Facts(LookAdapterType::Integrated, 0x1002, false, "Vulkan"),
    };
    const LookAdapterClass classes[] = {LookAdapterClass::Discrete, LookAdapterClass::Apple,
                                        LookAdapterClass::OtherNonDiscrete};
    // What each class's callers get under enhanced (the two tables above).
    const ClassOutcomes want[] = {{true, true, true, true}, {true, true, true, true},
                                  {true, true, false, false}};
    GpuDevice noAdapter;  // a caller's accessor; its own class is other non-discrete
    for (int f = 0; f < 3; f++) {
        for (int c = 0; c < 3; c++) {
            ASSERT_TRUE(milo::SetLookAdapterClassForInspection(false, classes[c]));
            const LookAdapterClass got = milo::LookAdapterClassInEffect(adapters[f]);
            EXPECT_EQ((int)got, (int)classes[c]) << "adapter " << f << " forced " << c;
            EXPECT_TRUE(Same(OutcomesFor(got), want[c])) << "adapter " << f << " forced " << c << ": "
                                                         << Describe(OutcomesFor(got));
            EXPECT_EQ((int)noAdapter.LookClass(), (int)classes[c]) << "GpuDevice::LookClass() forced " << c;
        }
    }
    ASSERT_TRUE(milo::SetLookAdapterClassForInspection(true));
    for (int f = 0; f < 3; f++)
        EXPECT_EQ((int)milo::LookAdapterClassInEffect(adapters[f]), (int)classes[f])
            << "adapter_rule: adapter " << f << " has its own class again";
    EXPECT_EQ((int)noAdapter.LookClass(), (int)LookAdapterClass::OtherNonDiscrete);
    // The class stands for the adapter: a profile switch keeps it.
    ASSERT_TRUE(milo::SetLookAdapterClassForInspection(false, LookAdapterClass::Apple));
    ASSERT_TRUE(milo::SetLookProfileForInspection(LookProfile::Retail));
    EXPECT_EQ((int)milo::LookAdapterClassInEffect(adapters[0]), (int)LookAdapterClass::Apple);
    EXPECT_TRUE(Same(OutcomesFor(LookAdapterClass::Apple), {false, false, false, false})) << "retail's Apple";
    ASSERT_TRUE(milo::SetLookAdapterClassForInspection(true));
}

// Sabotage: the setter not refusing Native -> red.
TEST(LookAdapterClasses, NativeRefusesTheClassToken) {
    const milo::LookAdapterFacts discrete = Facts(LookAdapterType::Discrete, 0x10DE, false, "Vulkan");
    milo::ScopedLookOverride native(LookProfile::Native);
    EXPECT_FALSE(milo::SetLookAdapterClassForInspection(false, LookAdapterClass::Apple));
    EXPECT_EQ((int)milo::LookAdapterClassInEffect(discrete), (int)LookAdapterClass::Discrete)
        << "a refused token changes nothing";
    EXPECT_FALSE(milo::SetLookAdapterClassForInspection(true));
}

// The token's line says the class is forced and what the adapter really is;
// adapter_rule prints the ordinary class line.
TEST(LookAdapterClasses, TheClassTokenLine) {
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    const milo::LookAdapterFacts saved = milo::LoggedLookAdapter();
    milo::LogLookAdapterClass(Facts(LookAdapterType::Discrete, 0x10DE, false, "Vulkan"));
    testing::internal::CaptureStderr();
    milo::SetLookAdapterClassForInspection(false, LookAdapterClass::Apple);
    const std::string forced = testing::internal::GetCapturedStderr();
    EXPECT_EQ(forced, "[look] adapter class forced for inspection: Apple GPU (not a discrete GPU); the adapter "
                      "is a discrete GPU [type discrete, vendor 0x10de, backend Vulkan]; depth of field near "
                      "field 1, near spread 1, edge 0, texture pass msaa 1, scale 1 (cap 1536, base rows 720) "
                      "(profile enhanced)\n");
    testing::internal::CaptureStderr();
    milo::SetLookAdapterClassForInspection(false, LookAdapterClass::OtherNonDiscrete);
    const std::string other = testing::internal::GetCapturedStderr();
    EXPECT_EQ(other.rfind("[look] adapter class forced for inspection: not a discrete GPU; the adapter is a "
                          "discrete GPU [",
                          0),
              0u)
        << other;
    testing::internal::CaptureStderr();
    milo::SetLookAdapterClassForInspection(true);
    const std::string rule = testing::internal::GetCapturedStderr();
    EXPECT_EQ(rule, "[look] adapter class: discrete GPU; depth of field near field 1, near spread 1, edge 1, "
                    "texture pass msaa 1, scale 1 (cap 2048, base rows 360) [type discrete, vendor 0x10de, backend "
                    "Vulkan] (profile enhanced)\n");
    milo::LogLookAdapterClass(saved);
}

// ---- np-look56: the web build's class, from the browser's adapter info ----
// A browser reports no adapter type, vendor id or backend (emdawnwebgpu:
// Unknown / CPU for a fallback, 0, WebGPU), so GpuDevice_Web.cpp records the
// page's GPUAdapterInfo strings (LookAdapterFacts::web) and the class is
// decided from the vendor string, texture-compression-bc and fallback.
namespace {

milo::LookAdapterFacts WebFacts(const char *vendor, bool bc, bool fallback, const char *arch = "") {
    milo::LookAdapterFacts f;
    f.web = true;
    f.backendName = "WebGPU";
    f.type = fallback ? LookAdapterType::Cpu : LookAdapterType::Unknown;
    f.webHasBC = bc;
    f.webFallback = fallback;
    milo::SetLookAdapterWebStrings(f, vendor, strlen(vendor), arch, strlen(arch));
    return f;
}

}  // namespace

// The browser's rule, walked without a device, with the Apple-in-a-browser
// constant passed both ways. Sabotages (each seen red, then reverted; README
// of dc3 lane item 56): "nvidia" mapped to other -> red; the fallback check
// dropped -> red; the BC condition dropped -> red; the constant ignored
// (Apple whatever its value) -> red; the facts overload ignoring f.web -> red.
TEST(LookAdapterClasses, TheBrowserClassificationTable) {
    const LookAdapterClass D = LookAdapterClass::Discrete, A = LookAdapterClass::Apple,
                           O = LookAdapterClass::OtherNonDiscrete;
    struct Row {
        const char *vendor;
        bool bc, fallback;
        LookAdapterClass wantAppleOff, wantAppleOn;
        const char *what;
    };
    const Row rows[] = {
        {"nvidia", true, false, D, D, "an NVIDIA GPU in Chrome"},
        {"nvidia", false, false, D, D, "an NVIDIA GPU without BC is still NVIDIA's"},
        {"nvidia", true, true, O, O, "a fallback adapter that says nvidia"},
        {"apple", true, false, O, A, "an Apple silicon Mac (BC)"},
        {"apple", false, false, O, O, "an iPhone or iPad (no BC)"},
        {"apple", true, true, O, O, "a fallback adapter that says apple"},
        {"amd", true, false, O, O, "AMD: a desktop card or a handheld's iGPU"},
        {"intel", true, false, O, O, "Intel"},
        {"arm", false, false, O, O, "Arm"},
        {"qualcomm", false, false, O, O, "Qualcomm"},
        {"", true, false, O, O, "an empty vendor"},
        {"", true, true, O, O, "an empty vendor on a fallback adapter"},
        {"made-up-vendor", true, false, O, O, "a made-up vendor"},
        // Case: compared exactly (the specification's identifiers are lower case).
        {"NVIDIA", true, false, O, O, "NVIDIA in capitals is not a normalised identifier"},
        {"Nvidia", true, false, O, O, "mixed case"},
        {"nvidia ", true, false, O, O, "a trailing space"},
        {"Apple", true, false, O, O, "Apple in capitals"},
    };
    for (const Row &r : rows) {
        EXPECT_EQ((int)milo::ClassifyWebLookAdapter(r.vendor, r.bc, r.fallback, false), (int)r.wantAppleOff)
            << r.what << " (Apple in a browser off)";
        EXPECT_EQ((int)milo::ClassifyWebLookAdapter(r.vendor, r.bc, r.fallback, true), (int)r.wantAppleOn)
            << r.what << " (Apple in a browser on)";
        // Through the facts, with the constant as it stands.
        const LookAdapterClass want = milo::LookAppleInBrowser() ? r.wantAppleOn : r.wantAppleOff;
        EXPECT_EQ((int)milo::ClassifyLookAdapter(WebFacts(r.vendor, r.bc, r.fallback)), (int)want)
            << r.what << " (the facts)";
    }
    EXPECT_EQ((int)milo::ClassifyWebLookAdapter(nullptr, true, false, true), (int)O) << "no vendor at all";
    // The constant starts off: a Mac in a browser is other non-discrete, as
    // before np-look56, until a Mac in a browser is timed.
    EXPECT_FALSE(milo::LookAppleInBrowser());
    // The browser's facts decide alone: native fields a browser never gives
    // do not move it, and native facts never take the browser rule.
    milo::LookAdapterFacts webAmd = WebFacts("amd", true, false);
    webAmd.type = LookAdapterType::Discrete;
    webAmd.vendorId = 0x10DE;
    EXPECT_EQ((int)milo::ClassifyLookAdapter(webAmd), (int)O) << "web facts ignore the native fields";
    milo::LookAdapterFacts nativeUnknown;  // the native facts the web build had before np-look56
    milo::SetLookAdapterWebStrings(nativeUnknown, "nvidia", 6, "ampere", 6);
    EXPECT_EQ((int)milo::ClassifyLookAdapter(nativeUnknown), (int)O)
        << "without f.web the strings are not read (the native rule: unknown type)";
}

// The facts' strings are copied, truncated to fit, and need no terminator.
TEST(LookAdapterClasses, TheBrowserStringsAreCopied) {
    milo::LookAdapterFacts f;
    const char vendorBytes[] = {'n', 'v', 'i', 'd', 'i', 'a', 'X'};  // no terminator; length 6 is "nvidia"
    milo::SetLookAdapterWebStrings(f, vendorBytes, 6, nullptr, 0);
    EXPECT_STREQ(f.webVendor, "nvidia");
    EXPECT_STREQ(f.webArchitecture, "");
    milo::SetLookAdapterWebStrings(f, "apple", (size_t)-1, "metal-3", (size_t)-1);  // WGPU_STRLEN
    EXPECT_STREQ(f.webVendor, "apple");
    EXPECT_STREQ(f.webArchitecture, "metal-3");
    const std::string longVendor(100, 'x');
    milo::SetLookAdapterWebStrings(f, longVendor.data(), longVendor.size(), "", 0);
    EXPECT_EQ(strlen(f.webVendor), (size_t)milo::kLookAdapterWebStringSize - 1);
    // A long vendor that starts "nvidia" is not "nvidia".
    const std::string nvidiaLong = "nvidia" + std::string(60, 'z');
    f.web = true;
    milo::SetLookAdapterWebStrings(f, nvidiaLong.data(), nvidiaLong.size(), "", 0);
    EXPECT_EQ((int)milo::ClassifyLookAdapter(f), (int)LookAdapterClass::OtherNonDiscrete);
}

// The class line names what the browser's class was decided from (the
// vendor string, BC, fallback) and the backend reads WebGPU; the forced
// line too. Sabotage: the browser's description dropped (the native one
// printed for web facts) -> red.
TEST(LookAdapterClasses, TheBrowserClassLine) {
    milo::ScopedLookOverride look(LookProfile::Enhanced);
    const milo::LookAdapterFacts saved = milo::LoggedLookAdapter();
    const milo::LookAdapterFacts nvidia = WebFacts("nvidia", true, false, "ampere");
    testing::internal::CaptureStderr();
    milo::PrintLookAdapterClass(nvidia);
    const std::string line = testing::internal::GetCapturedStderr();
    EXPECT_EQ(line, "[look] adapter class: discrete GPU; depth of field near field 1, near spread 1, edge 1, "
                    "texture pass msaa 1, scale 1 (cap 2048, base rows 360) [browser vendor \"nvidia\", "
                    "architecture \"ampere\", texture-compression-bc 1, fallback 0, backend WebGPU] (profile "
                    "enhanced)\n");
    testing::internal::CaptureStderr();
    milo::PrintLookAdapterClass(WebFacts("apple", true, false, "metal-3"));
    const std::string mac = testing::internal::GetCapturedStderr();
    EXPECT_EQ(mac, "[look] adapter class: not a discrete GPU; depth of field near field 1, near spread 1, edge 0, "
                   "texture pass msaa 0, scale 0 (cap 2048, base rows 360) [browser vendor \"apple\", "
                   "architecture \"metal-3\", texture-compression-bc 1, fallback 0, backend WebGPU] (profile "
                   "enhanced)\n");
    milo::LogLookAdapterClass(nvidia);
    testing::internal::CaptureStderr();
    milo::SetLookAdapterClassForInspection(false, LookAdapterClass::OtherNonDiscrete);
    const std::string forced = testing::internal::GetCapturedStderr();
    EXPECT_EQ(forced, "[look] adapter class forced for inspection: not a discrete GPU; the adapter is a discrete "
                      "GPU [browser vendor \"nvidia\", architecture \"ampere\", texture-compression-bc 1, "
                      "fallback 0, backend WebGPU]; depth of field near field 1, near spread 1, edge 0, texture "
                      "pass msaa 0, scale 0 (cap 2048, base rows 360) (profile enhanced)\n");
    ASSERT_TRUE(milo::SetLookAdapterClassForInspection(true));
    milo::LogLookAdapterClass(saved);
}

// On the device: the forced class reaches the texture pass at its next
// clearing pass (Tex_Wgpu's caller of LookTexPassMsaa), and nothing that
// sizes the frame moves. Sabotage: GetGpuTexPassTarget deciding by
// IsDiscreteGpu (the two-way flag) in place of LookClass() -> red (forced
// other non-discrete still drew 4 samples on a discrete GPU).
TEST_F(TexPassGpuTest, TheClassTokenReachesTheTexturePass) {
    ScopedPredicate yes(&AnyTarget);
    milo::ScopedLookOverride look(milo::LookProfile::Enhanced);
    GpuDevice &gpu = gWgpuRnd->Gpu();
    const bool discrete = gpu.IsDiscreteGpu();
    const int rows = gpu.OutputHeight();
    const LookAdapterClass own = milo::ClassifyLookAdapter(gpu.LookAdapter());
    RndTex *tex = NewTarget(64, 64, RndTex::kRendered);
    struct Step {
        bool rule;
        LookAdapterClass forced;
        uint32_t samples;
    };
    const uint32_t k4 = WgpuRnd::kTexPassMsaaSamples;
    const Step steps[] = {
        {false, LookAdapterClass::OtherNonDiscrete, 1},
        {false, LookAdapterClass::Apple, k4},
        {false, LookAdapterClass::OtherNonDiscrete, 1},
        {false, LookAdapterClass::Discrete, k4},
        {true, LookAdapterClass::Discrete, own == LookAdapterClass::OtherNonDiscrete ? 1u : k4},
    };
    for (const Step &st : steps) {
        ASSERT_TRUE(milo::SetLookAdapterClassForInspection(st.rule, st.forced));
        EXPECT_EQ((int)gpu.LookClass(), (int)(st.rule ? own : st.forced));
        EXPECT_EQ(Samples(tex), st.samples) << "forced " << (int)st.forced << " rule " << st.rule;
        EXPECT_EQ(gpu.IsDiscreteGpu(), discrete) << "the frame's two-way flag does not move";
        EXPECT_EQ(gpu.OutputHeight(), rows) << "nor the frame's size";
    }
    delete tex;
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET  // TexPassScaleTest
// On the device: the forced class reaches the size of a target drawn like the
// frame (Tex_Wgpu's WantedGpuSize passes GpuDevice::LookClass() to
// TexPassGpuSize). A 64x64 target at 1080 output rows: discrete x3 (192),
// Apple x1.5 under its base rows 720 (96); at 713 rows Apple keeps its own
// size, discrete x1.98 (127). Sabotage: WantedGpuSize passing
// LookAdapterClass::Discrete -> red.
TEST_F(TexPassScaleTest, TheClassTokenReachesTheSize) {
    struct Step {
        LookAdapterClass forced;
        int rows;
        uint32_t side;
    };
    const Step steps[] = {
        {LookAdapterClass::Apple, 1080, 96},    {LookAdapterClass::Discrete, 1080, 192},
        {LookAdapterClass::Apple, 713, 64},     {LookAdapterClass::Discrete, 713, 127},
        {LookAdapterClass::Apple, 1440, 128},
    };
    for (const Step &st : steps) {
        ASSERT_TRUE(milo::SetLookAdapterClassForInspection(false, st.forced));
        SetTexPassOutputRowsForTest(st.rows);
        Draw();
        EXPECT_EQ(GpuWidth(), st.side) << "class " << (int)st.forced << ", " << st.rows << " rows";
    }
    ASSERT_TRUE(milo::SetLookAdapterClassForInspection(true));  // the fixture's look scope restores it too
}
#endif
