// test_scene_bind_group_cost.cpp -- what a frame's camera switches cost in
// bind groups (np-gfx spotperf lane round 4, branch np-framecost4, 2026-11-15).
//
// WgpuRnd::WriteSceneUniforms runs at BeginDrawing and again whenever a draw
// finds a different camera, environ or camera position current
// (EnsureSceneUniformsCurrent): DC3's UI moves one camera per panel, so a song
// frame writes its scene block ~55 times. Each write lands at a new offset of
// the scene ring, and the scene bind group (group 0) binds the ring at that
// offset, so every write created a new bind group: ~55 CreateBindGroup calls a
// frame in menus, song and results, each one a call out of wasm into the
// browser on the web (milo-native-engine docs/native/dc3-backend-for-rb3-wii.md
// section 40). The ring is reset every frame, so a frame that switches cameras
// as the last one did writes the same offsets; the group now comes from the
// renderer's bind-group cache, keyed by every resource it binds.
//
// This pins it:
//  - a frame that switches cameras K times writes the scene block K+1 times,
//    and after the first such frame creates no scene bind group;
//  - one more switch than before (a new offset) creates exactly one new group,
//    on that frame only -- the cache does not hand back a group for an offset
//    it never bound;
//  - on Linux desktop, every wgpuDeviceCreateBindGroup the frames made went
//    through CreateCountedBindGroup (the wrapped count equals the counted one),
//    so [GPUCOUNT] bgCreatesAll= misses no site.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round4):
//  - WriteSceneUniforms back on a fresh CreateCountedBindGroup per write ->
//    "steady frame created scene bind groups", K+1 a frame;
//  - the cache key without the entry offset -> the extra switch creates none
//    ("a new offset reused a group bound at another offset");
//  - the scene site on a bare Device::CreateBindGroup -> the wrapped count
//    exceeds the counted one by K+1 a frame.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter).

#include "test_helpers.h"

#include "gfx/BindGroupCount.h"
#include "gfx/FrameCost.h"
#include "gfx/WgpuCallCount.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"

#include <string>
#include <vector>

namespace {

class SceneBindGroupCostTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

struct Cost {
    uint64_t sceneWrites = 0;
    uint64_t sceneCreates = 0;
    uint64_t counted = 0;  // every CreateCountedBindGroup
    uint64_t raw = 0;      // every wgpuDeviceCreateBindGroup (Linux desktop)
};

std::string List(const std::vector<Cost> &v, uint64_t Cost::*field) {
    std::string s;
    for (const Cost &c : v) s += std::to_string(c.*field) + " ";
    return s;
}

} // namespace

TEST_F(SceneBindGroupCostTest, CameraSwitchesReuseTheirSceneBindGroupsFrameToFrame) {
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";

    // Two cameras at different places: each switch is a scene-uniform write.
    std::vector<RndCam *> cams;
    for (int i = 0; i < 2; i++) {
        RndCam *cam = Hmx::Object::New<RndCam>();
        cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
        cam->SetLocalPos(Vector3(10.0f * i, -200.0f - 50.0f * i, 30.0f));
        cams.push_back(cam);
    }

    auto frame = [&](int switches) {
        Cost c;
        const uint64_t w0 = gWgpuRnd->SceneBgWrites(), s0 = gWgpuRnd->SceneBgCreates();
        const uint64_t n0 = GpuBindGroupCreateCount(), r0 = RawBindGroupCreateCount();
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        for (int i = 0; i < switches; i++) {
            cams[i % 2]->Select();
            gWgpuRnd->EnsureSceneUniformsCurrent();  // what a mesh or particle draw calls first
        }
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        c.sceneWrites = gWgpuRnd->SceneBgWrites() - w0;
        c.sceneCreates = gWgpuRnd->SceneBgCreates() - s0;
        c.counted = GpuBindGroupCreateCount() - n0;
        c.raw = RawBindGroupCreateCount() - r0;
        return c;
    };

    const int kSwitches = 6;
    std::vector<Cost> costs;
    for (int f = 0; f < 8; f++) costs.push_back(frame(kSwitches));

    // Non-vacuous: every switch reached WriteSceneUniforms, and the first
    // frame made its groups.
    for (size_t f = 0; f < costs.size(); f++)
        ASSERT_GE(costs[f].sceneWrites, (uint64_t)kSwitches + 1)
            << "frame " << f << ": BeginDrawing plus each camera switch writes the scene block; "
            << "per frame: " << List(costs, &Cost::sceneWrites);
    EXPECT_GT(costs[0].sceneCreates, 0u)
        << "the first frame creates the scene groups it binds; per frame: "
        << List(costs, &Cost::sceneCreates);
    for (size_t f = 2; f < costs.size(); f++)
        EXPECT_EQ(costs[f].sceneCreates, 0u)
            << "steady frame " << f << " created " << costs[f].sceneCreates
            << " scene bind group(s) for " << costs[f].sceneWrites
            << " unchanged scene writes; per frame: " << List(costs, &Cost::sceneCreates);

    // One switch more than every earlier frame: one offset no frame has bound.
    const Cost more = frame(kSwitches + 1);
    EXPECT_EQ(more.sceneWrites, costs.back().sceneWrites + 1) << "the extra switch is one more write";
    EXPECT_EQ(more.sceneCreates, 1u)
        << "a new offset reused a group bound at another offset, or made more than one";
    const Cost again = frame(kSwitches + 1);
    EXPECT_EQ(again.sceneCreates, 0u) << "and the next frame like it reuses that one too";

    // Every bind group these frames made was counted ([GPUCOUNT] bgCreatesAll).
    if (framecost::RawCallsCounted()) {
        costs.push_back(more);
        costs.push_back(again);
        for (size_t f = 0; f < costs.size(); f++)
            EXPECT_EQ(costs[f].raw, costs[f].counted)
                << "frame " << f << ": " << costs[f].raw << " wgpuDeviceCreateBindGroup calls, "
                << costs[f].counted << " through CreateCountedBindGroup; raw per frame: "
                << List(costs, &Cost::raw) << "counted: " << List(costs, &Cost::counted);
    }

    for (RndCam *c : cams) delete c;
}
