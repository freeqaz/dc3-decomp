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
