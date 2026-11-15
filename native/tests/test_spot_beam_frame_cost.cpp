// test_spot_beam_frame_cost.cpp -- what one frame of an unchanged spotlight
// beam set costs the engine (np-gfx spotperf lane, 2026-11-14).
//
// NgSpotlightDrawer::RenderScene (SpotlightDrawer_NG.cpp, HX_NATIVE) hands
// every beam to the engine's NativeSpotBeamRenderer each frame; WgpuRnd::
// SubmitSpotBeams makes sure the beam's shaft mesh is on the GPU. The shafts
// are Spotlight::Generate's BuildNG* meshes: unnamed, finished with Sync, and
// otherwise left alone. EnsureMeshUploaded took every unnamed mesh for an
// RndText glyph page and re-read it on every call (until np-framecost, which
// made every mesh sync-gated; test_text_frame_cost.cpp), so graphics batch 5
// re-uploaded every beam every frame -- an unpack plus a new vertex and index
// buffer per beam, with the old pair released -- which a GPD handheld
// (Radeon 890M, RADV) measured as ~1.2 ms of main-thread CPU at rollerrink's
// seven beams (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round1).
//
// This pins the frame-invariant work at zero: after the first frame an
// unchanged beam set uploads no mesh and converts no texture, and once the
// beam pass has drawn, a steady frame creates no bind group. And it pins that
// a real geometry change still reaches the GPU: a beam mesh's Sync makes the
// next frame upload exactly that mesh.
//
// Sabotage (round1/README.txt): SubmitSpotBeams back on the per-call re-read
// fails "steady frame uploaded" with one upload per beam per frame.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter).

#include "test_helpers.h"

#include "platform/MeshGpuCache.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/SpotBeamHook.h"
#include "platform/TexGpu.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "world/Spotlight.h"
#include "world/SpotlightDrawer.h"
#include "world/SpotlightDrawer_NG.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

// Passes every submission on to the engine's renderer and keeps the meshes.
struct ForwardingBeamRenderer : NativeSpotBeamRenderer {
    NativeSpotBeamRenderer *real = nullptr;
    int calls = 0;
    std::vector<RndMesh *> meshes;
    bool SubmitSpotBeams(const NativeSpotBeamFrame &f, const NativeSpotBeam *b, int count) override {
        calls++;
        meshes.clear();
        for (int i = 0; i < count; i++) meshes.push_back((RndMesh *)b[i].mesh);
        return real->SubmitSpotBeams(f, b, count);
    }
};

struct FrameCost {
    uint64_t meshUploads = 0;
    uint64_t texCreates = 0;
    uint64_t postGroups = 0;   // the post cache holds SpotBeamPass's groups
    int beamsDrawn = 0;
};

std::string List(const std::vector<FrameCost> &v, uint64_t FrameCost::*field) {
    std::string s;
    for (const FrameCost &c : v) s += std::to_string(c.*field) + " ";
    return s;
}

class SpotBeamFrameCostTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

} // namespace

TEST_F(SpotBeamFrameCostTest, AnUnchangedBeamSetUploadsNothingAfterItsFirstFrame) {
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    if (getenv("MILO_NO_SPOT_BEAMS")) GTEST_SKIP() << "MILO_NO_SPOT_BEAMS: the beams are dropped";
    NativeSpotBeamRenderer *engine = GetNativeSpotBeamRenderer();
    ASSERT_NE(engine, nullptr) << "WgpuRnd registers its beam renderer (dc3 retail post)";
    ForwardingBeamRenderer fwd;
    fwd.real = engine;
    SetNativeSpotBeamRenderer(&fwd);

    NgSpotlightDrawer *drawer = new NgSpotlightDrawer();
    drawer->SetProperty("total", 0.6f);
    drawer->SetShowing(true);
    ASSERT_TRUE(SpotlightDrawer::DrawNGSpotlights());

    // One beam of each shader: cone (rect shaft), sheet, sphere.
    const int shapes[3] = {0, 2, 4};
    std::vector<Spotlight *> spots;
    for (int shape : shapes) {
        Spotlight *s = Hmx::Object::New<Spotlight>();
        s->SetProperty("length", 150.0f);   // Generate -> BuildNGShaft
        s->SetProperty("shape", shape);
        s->SetProperty("top_radius", 4.0f);
        s->SetProperty("bottom_radius", 30.0f);
        s->SetColorIntensity(Hmx::Color(0.9f, 0.5f, 0.25f), 0.75f);
        s->SetLocalPos(Vector3((float)(spots.size() * 40), 200.0f, 120.0f));
        s->SetShowing(true);
        spots.push_back(s);
    }

    BindGroupCache &post = gWgpuRnd->PostBindGroups();
    auto frame = [&]() {
        FrameCost c;
        const uint64_t up0 = GpuMeshUploadCount();
        const uint64_t tex0 = GetGpuTexDebugInfo(nullptr).createCount;
        const uint64_t bg0 = post.Creates() + post.Uncached();
        TheRnd.BeginDrawing();
        drawer->Select();
        for (Spotlight *s : spots) SpotlightDrawer::DrawLight(s);
        drawer->EndWorld();
        drawer->DoPost();
        TheRnd.EndDrawing();
        c.meshUploads = GpuMeshUploadCount() - up0;
        c.texCreates = GetGpuTexDebugInfo(nullptr).createCount - tex0;
        c.postGroups = post.Creates() + post.Uncached() - bg0;
        c.beamsDrawn = gWgpuRnd->SpotBeamsDrawn();
        return c;
    };

    std::vector<FrameCost> costs;
    for (int f = 0; f < 10; f++) costs.push_back(frame());
    ASSERT_EQ(fwd.calls, 10) << "RenderScene submits the beams every frame";
    ASSERT_EQ(fwd.meshes.size(), spots.size()) << "one beam per spotlight";
    // Non-vacuous: the submissions reached the GPU mesh cache.
    EXPECT_EQ(costs[0].meshUploads, spots.size())
        << "the first frame uploads each shaft once; per frame: " << List(costs, &FrameCost::meshUploads);
    for (int f = 1; f < 10; f++) {
        EXPECT_EQ(costs[f].meshUploads, 0u)
            << "steady frame " << f << " uploaded " << costs[f].meshUploads
            << " beam mesh(es); per frame: " << List(costs, &FrameCost::meshUploads);
        EXPECT_EQ(costs[f].texCreates, 0u) << "steady frame " << f << " created a texture";
    }
    // When this harness gets the beam pass to draw (the DC3 composite ran),
    // its steady frames create no bind group either.
    if (costs.back().beamsDrawn > 0) {
        for (int f = 3; f < 10; f++)
            EXPECT_EQ(costs[f].postGroups, 0u)
                << "steady frame " << f << ": " << List(costs, &FrameCost::postGroups);
    }
    printf("[SpotBeamFrameCost] beams drawn last frame: %d; uploads/frame: %s\n",
           costs.back().beamsDrawn, List(costs, &FrameCost::meshUploads).c_str());

    // A real geometry change still reaches the GPU: Sync one shaft, and the
    // next frame uploads exactly that one.
    fwd.meshes[1]->Sync(0x13F);
    const FrameCost synced = frame();
    EXPECT_EQ(synced.meshUploads, 1u) << "a synced beam mesh is re-uploaded, once";
    const FrameCost after = frame();
    EXPECT_EQ(after.meshUploads, 0u) << "and not again";

    SetNativeSpotBeamRenderer(engine);
    for (Spotlight *s : spots) delete s;
    drawer->DeSelect();
    delete drawer;
}
