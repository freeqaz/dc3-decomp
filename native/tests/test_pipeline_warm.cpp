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

#include "gfx/GpuDevice.h"
#include "gfx/PipelineCount.h"
#include "gfx/PipelineManager.h"
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
#include <thread>
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

// ---- the hand-managed lifetime of an in-flight warm -------------------------
//
// PipelineManager::PendingWarm is owned by mWarmPending until its entry is
// removed; ReleasePending then deletes it if its callback has run (done), or
// marks it abandoned and the callback deletes it when it runs. Callbacks run
// only in this thread's ProcessEvents / WaitAny (AllowProcessEvents). Each
// case below ends with PipelineManager::LivePendingWarms() back where it
// started: an entry deleted twice or never is a count that does not return
// (and, under ASan, a use-after-free or a leak report). Each runs on a
// headless device of its own, so the device and the manager can be destroyed
// in the orders the case is about without touching the engine's.
//
// Sabotage, one at a time (2026-11-15), each red: ReleasePending deleting an
// unfinished entry -> the removal-first case finds 0 of its 4 entries left
// while their callbacks still hold them; the callback ignoring `abandoned` ->
// 4 abandoned entries never deleted (removal-first, and after the device
// loss); DrainWarms not waiting -> Terminate leaves 8 entries behind.

namespace {

class PipelineWarmLifetimeTest : public EngineTestFixture {
protected:
    bool InitOwn() {
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady()) return false;  // MILO_NORENDER / no adapter
        GpuDeviceDesc desc;
        desc.headless = true;
        desc.width = 64;
        desc.height = 64;
        if (!mGpu.Init(desc)) return false;
        mGpuUp = true;
        mGpu.AcquireHeadlessFrame();  // the headless surface format
        mPm = new PipelineManager();
        mPm->Init(&mGpu);
        mBase = PipelineManager::LivePendingWarms();
        return true;
    }
    void TearDown() override {
        if (mPm) {
            mPm->Terminate();
            delete mPm;
        }
        if (mGpuUp) mGpu.Shutdown();
        EngineTestFixture::TearDown();
    }
    // 18 distinct keys: every blend, with and without alpha write.
    PipelineKey Key(int i) {
        PipelineKey key{};
        key.targetFormat = mGpu.SurfaceFormat();
        key.sampleCount = 1;
        key.hasDepth = false;
        key.blend = (WgpuBlend)(i % 9);
        key.alphaWrite = (i / 9) % 2 != 0;
        return key;
    }
    int Live() const { return PipelineManager::LivePendingWarms() - mBase; }
    // Poll until nothing is outstanding (bounded).
    bool PollOut() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (mPm->WarmsOutstanding() > 0) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            mPm->PollWarms();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    // Run callbacks with no manager in the way, until `live` (bounded).
    bool ProcessUntil(int live) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (Live() != live) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            mGpu.Instance().ProcessEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    GpuDevice mGpu;
    bool mGpuUp = false;
    PipelineManager *mPm = nullptr;
    int mBase = 0;
};

} // namespace

// Callback before removal. PollWarms issues, a later PollWarms' ProcessEvents
// runs the callback (done), and that same PollWarms takes the result in and
// deletes the entry; a draw on a key still in flight waits in GetPipeline
// (WaitAny runs the callback) and then deletes it. Both pipelines land in the
// cache, and the draws after them create nothing.
TEST_F(PipelineWarmLifetimeTest, ACallbackThatRunsFirstLeavesTheEntryToItsOwner) {
    if (IsSkipped()) return;
    if (!InitOwn()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    ASSERT_TRUE(mPm->WarmAsync(Key(0)));
    ASSERT_TRUE(mPm->WarmAsync(Key(1)));
    mPm->PollWarms();  // issues both; their callbacks cannot have run yet
    EXPECT_EQ(Live(), 2);
    // Key 1 by a draw: waits, then the draw deletes the entry.
    const uint64_t c0 = GpuPipelineCreateCount();
    EXPECT_TRUE(mPm->GetPipeline(Key(1)));
    EXPECT_EQ(mPm->GetWarmStats().waited, 1u);
    EXPECT_EQ(Live(), 1);
    // Key 0 by the poll.
    ASSERT_TRUE(PollOut()) << "a warm never finished";
    EXPECT_EQ(Live(), 0);
    EXPECT_EQ(mPm->GetWarmStats().finished, 2u);
    EXPECT_EQ(mPm->GetWarmStats().failed, 0u);
    EXPECT_TRUE(mPm->GetPipeline(Key(0)));
    EXPECT_EQ(GpuPipelineCreateCount(), c0) << "a draw compiled a warmed key";
}

// Removal before callback. The manager goes while its warms are in flight
// (the destructor, as a web drain or a web draw that takes its key early
// does: neither can wait). Its entries are not deleted then -- their
// callbacks still hold them -- but marked abandoned, and each callback
// deletes its own when it runs.
TEST_F(PipelineWarmLifetimeTest, AnEntryRemovedFirstIsDeletedByItsCallback) {
    if (IsSkipped()) return;
    if (!InitOwn()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    for (int i = 0; i < 4; i++) ASSERT_TRUE(mPm->WarmAsync(Key(i)));
    mPm->PollWarms();
    ASSERT_EQ(Live(), 4);
    delete mPm;  // no Terminate: nothing waits
    mPm = nullptr;
    EXPECT_EQ(Live(), 4) << "the destructor deleted an entry its callback still holds";
    ASSERT_TRUE(ProcessUntil(0)) << "abandoned entries left: " << Live();
    for (int i = 0; i < 20; i++) mGpu.Instance().ProcessEvents();
    EXPECT_EQ(Live(), 0);
}

// Shutdown with warms outstanding: Terminate (WgpuRnd's shutdown, before the
// device goes) waits out every one in flight natively, deletes them, and
// drops the ones not yet issued.
TEST_F(PipelineWarmLifetimeTest, TerminateWithWarmsOutstandingDeletesEveryEntry) {
    if (IsSkipped()) return;
    if (!InitOwn()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    for (int i = 0; i < 12; i++) ASSERT_TRUE(mPm->WarmAsync(Key(i)));
    mPm->PollWarms();  // issues kWarmIssuePerFrame, the rest stay queued
    ASSERT_EQ(Live(), PipelineManager::kWarmIssuePerFrame);
    ASSERT_GT(mPm->WarmsOutstanding(), (size_t)PipelineManager::kWarmIssuePerFrame);
    mPm->Terminate();
    EXPECT_EQ(Live(), 0);
    EXPECT_EQ(mPm->WarmsOutstanding(), 0u);
    delete mPm;
    mPm = nullptr;
    for (int i = 0; i < 20; i++) mGpu.Instance().ProcessEvents();
    EXPECT_EQ(Live(), 0) << "entries left after Terminate";
}

// Device loss with warms outstanding. The device is destroyed under them;
// their callbacks still run (failed or not), the frames' PollWarms -- which
// WgpuRnd::BeginDrawing still calls on a lost device -- takes them in, and
// Terminate then has nothing left. A manager deleted after the loss with its
// warms in flight leaves them to their callbacks as above.
TEST_F(PipelineWarmLifetimeTest, ADeviceLostUnderItsWarmsLeavesNoEntryBehind) {
    if (IsSkipped()) return;
    if (!InitOwn()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    for (int i = 0; i < 6; i++) ASSERT_TRUE(mPm->WarmAsync(Key(i)));
    mPm->PollWarms();
    ASSERT_EQ(Live(), 6);
    mGpu.Device().Destroy();
    ASSERT_TRUE(PollOut()) << "warms on a destroyed device never completed";
    EXPECT_EQ(Live(), 0);
    const PipelineManager::WarmStats &s = mPm->GetWarmStats();
    printf("[PipelineWarmLifetime] after the device was destroyed: %llu finished, %llu failed\n",
           (unsigned long long)s.finished, (unsigned long long)s.failed);
    EXPECT_EQ(s.finished + s.failed, 6u);
    // Issued after the loss, then the manager deleted before the callbacks.
    for (int i = 6; i < 10; i++) ASSERT_TRUE(mPm->WarmAsync(Key(i)));
    mPm->PollWarms();
    EXPECT_EQ(Live(), 4);
    delete mPm;
    mPm = nullptr;
    ASSERT_TRUE(ProcessUntil(0)) << "abandoned entries left after the loss: " << Live();
}
