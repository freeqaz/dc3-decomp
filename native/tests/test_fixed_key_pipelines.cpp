// test_fixed_key_pipelines.cpp -- the pipelines whose keys are fixed once the
// device is up are compiled before the first frame, not on it (np-gfx
// spotperf lane round 2, branch np-framecost, 2026-11-15).
//
// The depth of field, spot beam, point-test and shadow caster passes own
// their pipelines outside PipelineManager, and each compiled them on first
// use -- which for all of them is DC3's first venue frame, one frame that
// also builds the venue's meshes and textures (milo-native-engine
// docs/native/dc3-backend-for-rb3-wii.md section 38.2). Their keys do not
// depend on content: the post intermediate is the surface format, the scene
// depth Depth24PlusStencil8 x kMSAASamples, the caster map Depth32Float, and
// the beams have none. So WgpuRnd::Init compiles them
// (PrewarmFixedKeyPipelines), and this pins it: after a frame, asking for
// them again creates nothing. Init built them, and built them for the keys
// the frame uses (headless, SurfaceFormat() used to be the BGRA8Unorm default
// until the first frame, so the depth of field's three were built for the
// wrong format and compiled again). A frame that draws beams is not reachable
// from this harness (SpotBeamFrameCostTest's frames draw 0: the DC3 composite
// does not run), so the beam half is checked by asking, not by drawing.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round2):
//  - the PrewarmFixedKeyPipelines call removed from WgpuRnd::Init -> the
//    second ask creates the ones the frame did not;
//  - GpuDevice::Init's headless SurfaceFormat() fix removed -> the second ask
//    creates the 3 depth-of-field pipelines.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter).

#include "test_helpers.h"

#include "gfx/PipelineCount.h"
#include "gfx/RB3RetailPost.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Rnd.h"

#include <string>
#include <vector>

namespace {

class FixedKeyPipelinesTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

std::string Join(const std::vector<std::string> &v) {
    std::string s;
    for (const std::string &x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

} // namespace

TEST_F(FixedKeyPipelinesTest, AreCompiledAtInitForTheKeysTheFrameUses) {
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    TakeRecentPipelineLabels();
    TheRnd.BeginDrawing();
    TheRnd.EndDrawing();
    const uint64_t before = GpuPipelineCreateCount();
    const uint64_t created = gWgpuRnd->PrewarmFixedKeyPipelines();
    EXPECT_EQ(created, GpuPipelineCreateCount() - before) << "the return value is the count";
    EXPECT_EQ(created, 0u) << "asked again after a frame, it compiled: "
                           << Join(TakeRecentPipelineLabels());
}

// The enhanced look's post pipelines (np-gfx-post round 10: the DC3 chain's
// 16-bit / filtered bloom family and the depth of field's tent + B-spline)
// join the warm for the look that uses them, and only for it. The bloom
// family is per LookSettings combination (precision on: RGBA16Float, else
// RGBA8), so each combination is asked for: the filter-only family is never
// warmed at Init under either profile (only native_look post_bloom_filter
// selects it), which makes that row non-vacuous whatever this process booted
// with. Sabotage (np-gfx-post round 10 part 3a):
//  - the PrewarmDC3Enhanced call removed from PrewarmFixedKeyPipelines -> the
//    filter-only row (and, under DC3_LOOK=retail, every row) is not ready;
//  - PrewarmDof called without look.postDofFilter -> the DOF rows are not
//    ready (nothing else in this harness runs the depth of field on gWgpuRnd).
TEST_F(FixedKeyPipelinesTest, EnhancedPostJoinsTheWarmForTheLookThatUsesIt) {
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    milo::ScopedLookOverride keep(milo::LookProfile::Enhanced);
    struct Case {
        bool precision, bloomFilter, dofFilter;
    } cases[] = {
        {false, true, false}, {true, false, false}, {true, true, true}, {false, false, true},
    };
    for (const Case &c : cases) {
        ASSERT_TRUE(milo::SetLookPostFieldsForInspection(c.precision, c.bloomFilter, c.dofFilter));
        TakeRecentPipelineLabels();
        gWgpuRnd->PrewarmFixedKeyPipelines();
        const std::string made = Join(TakeRecentPipelineLabels());
        const PostProcPass &post = gWgpuRnd->PostProc();
        if (c.precision || c.bloomFilter)
            EXPECT_TRUE(post.DC3EnhancedReady(c.precision))
                << "precision " << c.precision << " filter " << c.bloomFilter
                << ": the bloom family RunDC3Bloom selects is not warm (this ask made: " << made << ")";
        if (c.dofFilter && WgpuRnd::kMSAASamples >= 2)
            EXPECT_TRUE(post.DofEnhancedReady()) << "postDofFilter: the tent / B-spline DOF pipelines are not warm";
        EXPECT_EQ(gWgpuRnd->PrewarmFixedKeyPipelines(), 0u) << "the warm is idempotent";
    }
}

// The warm builds the pipelines the enhanced frame asks for, not merely some
// pipelines: a fresh DC3 chain, warmed for each bloom combination, runs that
// combination's plain, streak and glare arms and compiles nothing new.
// Sabotage: PrewarmDC3Enhanced's family index inverted (precision ? 0 : 1) ->
// red at the ready assert; with that assert removed too, each combination's
// frames compile the ten DC3Look* pipelines (so the frame check bites alone).
TEST_F(FixedKeyPipelinesTest, WarmedEnhancedBloomFramesCompileNothing) {
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    constexpr int W = 320, H = 180;
    auto tex = [&](wgpu::TextureFormat f, wgpu::TextureUsage u) {
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)W, (uint32_t)H, 1};
        d.format = f;
        d.usage = u;
        return gpu.Device().CreateTexture(&d);
    };
    wgpu::Texture scene = tex(wgpu::TextureFormat::RGBA8Unorm,
                              wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment);
    wgpu::Texture frame = tex(gpu.SurfaceFormat(), wgpu::TextureUsage::RenderAttachment);
    wgpu::TextureView sv = scene.CreateView(), fv = frame.CreateView();
    struct Combo {
        bool precision, filter;
    } combos[] = {{true, true}, {false, true}, {true, false}};
    for (const Combo &c : combos) {
        RB3RetailPost post;
        post.Init(gpu);
        post.PrewarmDC3Enhanced(c.precision, c.filter, gpu);
        ASSERT_TRUE(post.DC3EnhancedReady(c.precision));
        TakeRecentPipelineLabels();
        const uint64_t before = GpuPipelineCreateCount();
        for (int arm = 0; arm < 3; arm++) {
            RetailPostParams p;
            p.dc3 = true;
            p.bloom = true;
            p.bloomColor[0] = p.bloomColor[1] = p.bloomColor[2] = 0.5f;
            p.screenAspect = 0.5625f;
            p.streak = arm == 1;
            p.streakAttenuation = 0.5f;
            p.glare = arm == 2;
            p.enhPrecision = c.precision;
            p.enhFilter = c.filter;
            wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
            post.Run(enc, sv, sv, W, H, fv, p, gpu);
            wgpu::CommandBuffer cmd = enc.Finish();
            gpu.Queue().Submit(1, &cmd);
        }
        std::vector<std::string> made;
        for (const std::string &l : TakeRecentPipelineLabels())
            if (l.rfind("DC3Look", 0) == 0) made.push_back(l);
        EXPECT_TRUE(made.empty()) << "precision " << c.precision << " filter " << c.filter
                                  << ": the warmed chain's frames compiled " << Join(made)
                                  << " (all creates: " << GpuPipelineCreateCount() - before << ")";
        post.Terminate();
    }
}
