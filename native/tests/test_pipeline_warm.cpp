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
#include "platform/LookProfile.h"
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

// item 19 (a): a discrete-GPU window resized across the render-scale step
// (1920x1080 at 2x <-> 2560x1440 at 1x) moves the world texture LOD bias
// (milo::MaterialLodBiasAt) and the output-pixel scale text and HUD sample at,
// and that must compile nothing, the FIRST time too. The bias is a value in the
// scene block, not a pipeline constant; the output-pixel scale is a pipeline
// constant, so the cache keys on it (PipelineKey::outputPixelScale: a step
// back finds the old pipelines) and a dir's text and HUD keys are warmed at
// each scale the window can reach (PipelineManager::SetOutputPixelScaleRange,
// from GpuDevice::RenderScaleRange: 1 and 2 on a discrete GPU's window). The
// headless test device draws at 1x and cannot resize, so the step is made the
// way a resize makes it to the renderer: the range a discrete window has is
// given before the dirs load, the bias the scene block carries moves (through
// the inspection switch, which writes the same field) and the manager is told
// the frame's new output-pixel scale (BeginDrawing's SetOutputPixelScale).
// The frame draws the dir the warm test loads, and a text, with a camera, as a
// song draws them; the 2x side is never drawn before the step. Non-vacuous:
// the scene block's bias is read back and has moved.
// Sabotage (item19/a/SABOTAGE.txt): the bias baked back into the pipeline
// (kTakesLodBias given the value, the cache keyed on it) -> the bias step
// creates the world keys; the warm at the current scale only (WarmAtEachScale
// one-scale) -> the step up creates the text page's; SetOutputPixelScale
// erasing the sampling pipelines again -> the step down after up creates them.
TEST_F(PipelineWarmTest, ARenderScaleStepCompilesNoPipeline) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    if (!gWgpuRnd->Pipelines().MaterialTakesLodBias())
        GTEST_SKIP() << "the run's look takes no LOD bias (DC3_LOOK=retail)";
    PipelineManager &pm = gWgpuRnd->Pipelines();
    const float rangeLo = pm.OutputPixelScaleLo(), rangeHi = pm.OutputPixelScaleHi();
    pm.SetOutputPixelScaleRange(1.0f, 2.0f);  // a discrete GPU's window
    ObjectDir *fonts = DirLoader::LoadObjects(
        FilePath("ui/resource/fonts/gen/default.milo_xbox"), nullptr, nullptr);
    if (!fonts) {
        pm.SetOutputPixelScaleRange(rangeLo, rangeHi);
        GTEST_SKIP() << "no archive UI fonts";
    }
    ObjectDir *star =
        DirLoader::LoadObjects(FilePath("ui/hud/gen/score_star.milo_xbox"), nullptr, nullptr);
    if (!star) {
        delete fonts;
        pm.SetOutputPixelScaleRange(rangeLo, rangeHi);
        GTEST_SKIP() << "no archive HUD";
    }
    ASSERT_TRUE(DrainWarms());
    std::vector<RndMesh *> meshes;
    for (ObjDirItr<RndMesh> it(star, true); it != nullptr; ++it) meshes.push_back(it);
    ASSERT_FALSE(meshes.empty());
    RndFont *font = nullptr;
    for (ObjDirItr<RndFont> it(fonts, true); it != nullptr && !font; ++it) font = it;
    ASSERT_NE(font, nullptr);
    RndText *text = Hmx::Object::New<RndText>();
    DataArrayPtr fontProp(Symbol("styles"), 0, Symbol("font"));
    text->SetProperty(fontProp, DataNode(font));
    text->SetTextASCII("0123 Flashcard");
    text->UpdateText();
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    // One frame drawing the dir (and the text) as the song draws them; returns
    // the pipelines it created or waited for, and the bias the world draws read.
    // pixelScale: the output-pixel scale the frame is drawn at (BeginDrawing
    // sets the device's, 1 headless; a 2x frame's is 2).
    auto frame = [&](float pixelScale, float &bias) {
        const uint64_t c0 = GpuPipelineCreateCount(), w0 = GpuPipelineWaitCount();
        TheRnd.BeginDrawing();
        pm.SetOutputPixelScale(pixelScale);
        RndCam *prev = RndCam::Current();
        cam->Select();
        for (RndMesh *m : meshes) m->DrawShowing();
        bias = gWgpuRnd->LastSceneMaterialLodBias();
        text->DrawShowing();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        return (GpuPipelineCreateCount() - c0) + (GpuPipelineWaitCount() - w0);
    };
    TakeRecentPipelineLabels();
    float b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f;
    // The size the window starts at (1x), drawn as the warm left it.
    const uint64_t first = frame(1.0f, b1);
    const std::string firstLabels = Join(TakeRecentPipelineLabels());
    // The step's bias move alone: the scene block carries a bias no frame
    // drew with, and the world draws take it from there.
    ASSERT_TRUE(milo::SetLookLodBiasForInspection(false, -0.25f));
    const uint64_t biasStep = frame(1.0f, b2);
    const std::string biasLabels = Join(TakeRecentPipelineLabels());
    // The whole step, 1x -> 2x -> 1x, with the bias moving each time.
    milo::SetLookLodBiasForInspection(false, -0.75f);
    const uint64_t up = frame(2.0f, b3);
    const std::string upLabels = Join(TakeRecentPipelineLabels());
    milo::SetLookLodBiasForInspection(true, 0.0f);
    const uint64_t down = frame(1.0f, b4);
    const std::string downLabels = Join(TakeRecentPipelineLabels());
    printf("[ScaleStep] bias %.2f -> %.2f -> %.2f -> %.2f; creates+waits: first %llu, bias "
           "step %llu, up %llu, down %llu\n", b1, b2, b3, b4, (unsigned long long)first,
           (unsigned long long)biasStep, (unsigned long long)up, (unsigned long long)down);
    EXPECT_EQ(first, 0u) << firstLabels;
    EXPECT_FLOAT_EQ(b2, -0.25f) << "the world draws read the moved bias (non-vacuous)";
    EXPECT_FLOAT_EQ(b3, -0.75f);
    EXPECT_NE(b1, b2);
    EXPECT_EQ(biasStep, 0u) << biasLabels;
    EXPECT_EQ(up, 0u) << "1x -> 2x: " << upLabels;
    EXPECT_EQ(down, 0u) << "2x -> 1x: " << downLabels;
    pm.SetOutputPixelScale(gWgpuRnd->Gpu().SupersampleScale());
    pm.SetOutputPixelScaleRange(rangeLo, rangeHi);
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
    // Keep running callbacks for a while after the count is settled, so a
    // callback that would touch a deleted entry has run before the case ends
    // (under ASan, a heap-use-after-free there).
    void ProcessFor(std::chrono::milliseconds d) {
        const auto end = std::chrono::steady_clock::now() + d;
        while (std::chrono::steady_clock::now() < end) {
            mGpu.Instance().ProcessEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
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
    ProcessFor(std::chrono::seconds(1));
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
    ProcessFor(std::chrono::seconds(1));
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
