// np-look26: enhanced cutout coverage kept across mip levels
// (milo::LookSettings::cutoutMipCoverage, standard_wgsl.inc cutoutMipBoost).
//
// Long alpha-tested hair strands (oblio04 at dclive, tan01's side hair) broke
// into dotted chains: a strand thinner than a texel of the mip the sampler
// reads is averaged with its transparent surround, its alpha falls under the
// cut threshold (oblio04_hair.mat: Src, alpha_cut 1, threshold 150), and the
// cut keeps only the texels where it peaks. Measured on a held dclive frame:
// threshold 100 made the strands continuous, lod_bias -1 made them more
// dotted. The enhanced look scales the tested alpha by 1 + 0.25 x (mip level),
// world cutout pipelines only. Retail and Native: off; the scene block then
// carries 0 and no retail pipeline reads it (retail's cut is fs_main).
#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"

#include <string>

using milo::Look;
using milo::LookProfile;
using milo::ScopedLookOverride;

namespace {

class LookCutoutMipTest : public EngineTestFixture {
protected:
    bool HaveGpu() const { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

} // namespace

// The look contract: on in Enhanced, off in Retail and Native, and the scene
// value follows the bool. Sabotage: SettingsFor not setting it, or
// CutoutMipScaleFor ignoring the bool -- red.
TEST_F(LookCutoutMipTest, OnlyTheEnhancedLookKeepsCoverageAcrossMips) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        EXPECT_TRUE(Look().cutoutMipCoverage);
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), milo::kCutoutMipScale);
        EXPECT_GT(milo::kCutoutMipScale, 0.0f);
    }
    {
        ScopedLookOverride retail(LookProfile::Retail);
        EXPECT_FALSE(Look().cutoutMipCoverage) << "retail: the original's alpha test";
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.0f);
    }
    {
        ScopedLookOverride native(LookProfile::Native);
        EXPECT_FALSE(Look().cutoutMipCoverage) << "rb3 / rb3-xenon unchanged";
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.0f);
    }
}

// {rnd native_look hair_edge_on|hair_edge_off|hair_edge_rule} and the tuning
// scale. Sabotage: the rule mode not restoring the profile's value -- red.
TEST_F(LookCutoutMipTest, TheInspectionSwitchAlternatesAndRestores) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(0));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.0f);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(1));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), milo::kCutoutMipScale);
        ASSERT_TRUE(milo::SetLookCutoutMipScaleForInspection(false, 0.5f));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), 0.5f);
        ASSERT_TRUE(milo::SetLookCutoutMipScaleForInspection(true, 0.0f));
        EXPECT_FLOAT_EQ(milo::CutoutMipScaleFor(Look()), milo::kCutoutMipScale);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(0));
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(-1));
        EXPECT_TRUE(Look().cutoutMipCoverage) << "hair_edge_rule: enhanced's own value";
    }
    {
        ScopedLookOverride retail(LookProfile::Retail);
        ASSERT_TRUE(milo::SetLookCutoutMipCoverageForInspection(-1));
        EXPECT_FALSE(Look().cutoutMipCoverage) << "hair_edge_rule: retail's own value";
    }
    {
        ScopedLookOverride native(LookProfile::Native);
        EXPECT_FALSE(milo::SetLookCutoutMipCoverageForInspection(1)) << "no switch for rb3";
        EXPECT_FALSE(Look().cutoutMipCoverage);
    }
}

// The value reaches the scene block of a world pass, and a live switch is
// written on the next scene write for the same camera (no pipeline made).
// Sabotage: WriteSceneUniforms not writing it, or the staleness check not
// watching it -- red.
TEST_F(LookCutoutMipTest, TheSceneBlockCarriesTheScale) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (the rule tests ran)";
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    RndCam *prev = RndCam::Current();
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    TheRnd.BeginDrawing();
    cam->Select();
    gWgpuRnd->EnsureSceneUniformsCurrent();
    const float on = gWgpuRnd->LastSceneCutoutMipScale();
    milo::SetLookCutoutMipCoverageForInspection(0);
    gWgpuRnd->EnsureSceneUniformsCurrent();   // same camera: the scale alone moved
    const float off = gWgpuRnd->LastSceneCutoutMipScale();
    milo::SetLookCutoutMipCoverageForInspection(-1);
    gWgpuRnd->EnsureSceneUniformsCurrent();
    const float rule = gWgpuRnd->LastSceneCutoutMipScale();
    if (prev) prev->Select(); else RndCam::ClearCurrent();
    TheRnd.EndDrawing();
    delete cam;
    EXPECT_FLOAT_EQ(on, milo::kCutoutMipScale) << "enhanced world pass";
    EXPECT_FLOAT_EQ(off, 0.0f) << "hair_edge_off reached the block";
    EXPECT_FLOAT_EQ(rule, milo::kCutoutMipScale) << "hair_edge_rule reached the block";
}

// The cutout pipelines that read it build: a world cutout (4 samples, LOD
// bias, fs_cutout with cutoutMipBoost), its text twin (no bias) and the
// single-sampled plain alpha test. A WGSL error or a derivative outside
// uniform control flow fails here.
TEST_F(LookCutoutMipTest, TheCutoutPipelinesBuild) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    PipelineManager &live = gWgpuRnd->Pipelines();
    struct Case { const char *name; uint32_t samples; bool lodBias; bool skinned; };
    const Case cases[] = {
        {"world cutout, static", 4, true, false},
        {"world cutout, skinned (hair)", 4, true, true},
        {"cutout without the bias (text/HUD class)", 4, false, false},
        {"single-sampled alpha test", 1, true, false},
    };
    for (const Case &c : cases) {
        PipelineKey key{};
        key.targetFormat = gpu.SurfaceFormat();
        key.sampleCount = c.samples;
        key.hasDepth = true;
        key.alphaCut = true;
        key.cutoutCoverage = true;
        key.lodBias = c.lodBias;
        key.layout = c.skinned ? VertexLayoutType::Skinned : VertexLayoutType::Static;
        gpu.Device().PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipe = live.GetPipeline(key);
        std::string err;
        wgpu::Future f = gpu.Device().PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly,
            [&err](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) err.assign(msg.data, msg.length);
            });
        gpu.Instance().WaitAny(f, UINT64_MAX);
        EXPECT_TRUE(pipe) << c.name;
        EXPECT_TRUE(err.empty()) << c.name << ": " << err;
    }
}
