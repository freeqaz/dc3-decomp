// test_pipeline_warm.cpp -- a dir's material pipelines are compiled when it
// finishes loading, not at its first draw (np-gfx spotperf lane round 3,
// branch np-hitch, 2026-11-15).
//
// DirLoader::Cleanup calls NativeWarmLoadedDir (native/src/platform/
// PipelineWarm.cpp) once a dir is loaded, which queues (PipelineManager::
// WarmAsync) the key each of the dir's meshes would ask for, through the
// function the draw itself uses (MeshPipelineKey, engine platform/
// Mesh_Wgpu.cpp), and the frames that follow compile them on Dawn's workers.
// Before this the HUD's score star -- which this test loads -- compiled its
// pipelines in the middle of a song, the first time a star filled.
//
// This pins it: once the warm has finished, drawing every mesh of the dir
// (hidden ones too: a star that is awarded later is hidden until then) and a
// text set in one of the loaded fonts creates no pipeline and waits for none,
// on the first frame and on every frame after it. Non-vacuous: the load queued
// keys, and the draws asked for warmed keys (WarmStats::drawn).
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round3):
//  - WarmAsync drops the first key it is given -> the first frame creates 1;
//  - the NativeWarmLoadedDir call removed from DirLoader::Cleanup -> nothing
//    is queued, and the first frame creates every key.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter), and the
// archive's HUD and UI fonts (skips without them).

#include "test_helpers.h"

#include "gfx/PipelineCount.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Font.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Text.h"
#include "utl/FilePath.h"

#include <chrono>
#include <string>
#include <vector>

namespace {

class PipelineWarmTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

std::string Join(const std::vector<std::string> &v) {
    std::string s;
    for (const std::string &x : v) s += (s.empty() ? "" : "; ") + x;
    return s;
}

// Frames until every queued warm has been compiled (bounded).
bool DrainWarms() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (gWgpuRnd->Pipelines().WarmsOutstanding() > 0) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        TheRnd.BeginDrawing();
        TheRnd.EndDrawing();
    }
    return true;
}

} // namespace

TEST_F(PipelineWarmTest, ALoadedDirDrawsWithNoPipelineCreate) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    TheRnd.BeginDrawing();
    TheRnd.EndDrawing();
    const PipelineManager::WarmStats s0 = gWgpuRnd->Pipelines().GetWarmStats();

    ObjectDir *fonts = DirLoader::LoadObjects(
        FilePath("ui/resource/fonts/gen/default.milo_xbox"), nullptr, nullptr);
    if (!fonts) GTEST_SKIP() << "no archive UI fonts (ui/resource/fonts/gen/default.milo_xbox)";
    ObjectDir *star =
        DirLoader::LoadObjects(FilePath("ui/hud/gen/score_star.milo_xbox"), nullptr, nullptr);
    if (!star) {
        delete fonts;
        GTEST_SKIP() << "no archive HUD (ui/hud/gen/score_star.milo_xbox)";
    }
    const uint64_t queued = gWgpuRnd->Pipelines().GetWarmStats().queued - s0.queued;
    EXPECT_GT(queued, 0u) << "loading the dirs queued no pipeline";
    ASSERT_TRUE(DrainWarms()) << "the warm did not finish in 60 s";

    std::vector<RndMesh *> meshes;
    for (ObjDirItr<RndMesh> it(star, true); it != nullptr; ++it) meshes.push_back(it);
    ASSERT_FALSE(meshes.empty()) << "score_star has no mesh";
    RndFont *font = nullptr;
    for (ObjDirItr<RndFont> it(fonts, true); it != nullptr && !font; ++it) font = it;
    ASSERT_NE(font, nullptr) << "no RndFont in " << fonts->Name();
    RndText *text = Hmx::Object::New<RndText>();
    DataArrayPtr fontProp(Symbol("styles"), 0, Symbol("font"));
    text->SetProperty(fontProp, DataNode(font));
    text->SetTextASCII("0123 Flashcard");
    text->UpdateText();

    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    TakeRecentPipelineLabels();
    std::string seq;
    for (int f = 0; f < 5; f++) {
        const uint64_t c0 = GpuPipelineCreateCount();
        const uint64_t w0 = GpuPipelineWaitCount();
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        for (RndMesh *m : meshes) m->DrawShowing();
        text->DrawShowing();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        const uint64_t creates = GpuPipelineCreateCount() - c0;
        const uint64_t waits = GpuPipelineWaitCount() - w0;
        seq += std::to_string(creates) + "/" + std::to_string(waits) + " ";
        EXPECT_EQ(creates, 0u) << "frame " << f << " compiled " << creates
                               << " pipeline(s) at the draw: " << Join(TakeRecentPipelineLabels());
        EXPECT_EQ(waits, 0u) << "frame " << f << " waited for a warm";
    }
    const PipelineManager::WarmStats s1 = gWgpuRnd->Pipelines().GetWarmStats();
    EXPECT_GT(s1.drawn - s0.drawn, 0u) << "no draw asked for a warmed pipeline (vacuous)";
    printf("[PipelineWarm] queued %llu, finished %llu, drawn %llu of them; meshes %zu; "
           "creates/waits per frame: %s\n",
           (unsigned long long)queued, (unsigned long long)(s1.finished - s0.finished),
           (unsigned long long)(s1.drawn - s0.drawn), meshes.size(), seq.c_str());

    delete cam;
    delete text;
    delete star;
    delete fonts;
}
