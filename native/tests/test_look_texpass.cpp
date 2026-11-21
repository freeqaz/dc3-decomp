// test_look_texpass.cpp -- np-look47: a render-to-texture target drawn like
// the frame (LookSettings::texPassMsaa, engine LookProfile.h).
//
// Break It Down's dancer is drawn into projection.tex (a RndTexRenderer's
// kRendered 1024x1024 target) and shown on a quad; the frame round her has 4x
// MSAA at the look's render scale. The enhanced look draws such a target with
// the frame's sample count, resolved into the texture (step 1). Retail and
// Native keep the one-sample target at its own size.
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
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Group.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "rndobj/TexRenderer.h"

#include <cstdlib>
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
    }
    for (milo::LookProfile p : {milo::LookProfile::Retail, milo::LookProfile::Native}) {
        milo::ScopedLookOverride look(p);
        const milo::LookSettings &s = milo::Look();
        EXPECT_FALSE(s.texPassMsaa || s.texPassMsaaIntegrated) << milo::LookProfileName(p);
    }
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
        // Step 1 alone never changes a target's size.
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
#endif

} // namespace
