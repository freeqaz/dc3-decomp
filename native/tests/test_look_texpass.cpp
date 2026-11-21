// test_look_texpass.cpp -- np-look47: a render-to-texture target drawn like
// the frame (LookSettings::texPassMsaa, engine LookProfile.h).
//
// Break It Down's dancer is drawn into projection.tex (a RndTexRenderer's
// kRendered 1024x1024 target) and shown on a quad; the frame round her has 4x
// MSAA at the look's render scale. The enhanced look draws such a target with
// the frame's sample count, resolved into the texture (step 1). Retail and
// Native keep the one-sample target at its own size. Which targets:
// TexPassDrawnLikeFrame (engine Tex_Wgpu.cpp), the one place the rule lives,
// and OnlyAColourAndDepthTargetIsDrawnLikeTheFrame, its one test, which lists
// the target kinds; the rule is round 1's (RndTex type exactly kRendered, no
// mip chain) until the census lane hands over its successor.
//
// Sabotage (np-look47, before landing): TexPassDrawnLikeFrame returning true
// for every render target -> OnlyAColourAndDepthTargetIsDrawnLikeTheFrame
// fails on kRenderedNoZ, kShadowMap, kDepthVolumeMap and kDensityMap;
// SettingsFor setting texPassMsaa = true in every profile ->
// EnhancedTurnsOnStep1RetailAndNativeNot fails. Both seen red on the 3090 and
// reverted (round 1).

#include "test_helpers.h"

#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Tex.h"

#ifdef MILO_READBACK_GPU_RENDER_TARGET
#include "rndobj/Cam.h"
#include "rndobj/Group.h"
#include "rndobj/Rnd.h"
#include "rndobj/TexRenderer.h"
#endif

#include <vector>

namespace {

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

TEST_F(TexPassGpuTest, OnlyAColourAndDepthTargetIsDrawnLikeTheFrame) {
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

TEST_F(TexPassGpuTest, RetailAndNativeKeepOneSample) {
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
