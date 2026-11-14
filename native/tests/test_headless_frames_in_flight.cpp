// test_headless_frames_in_flight.cpp -- a headless frame loop may not run
// more than GpuDevice::HeadlessFramesInFlight() frames ahead of the GPU
// (PLAN H-75, lane np-soak3; engine GpuDevice::EndHeadlessFrame).
//
// A windowed frame is paced by the swapchain's present.  A headless one had
// nothing: WgpuRnd::EndDrawing submitted and returned, so an uncapped loop
// (every flow test, every soak run with --headless; 200-7000 fps once the
// bind-group cache landed) queued frames as fast as the CPU could record
// them, and the driver grew its descriptor, command-buffer and staging pools
// to the deepest backlog it ever saw.  MEASURED (scripts/playtest/mem_songs.py,
// 10 Perform songs, headless GPU, DC3_FAST_TIME): song select rss 621 -> 833
// MB and /dev/nvidia* mappings 61 -> 115 MB unthrottled, against flat with
// two frames in flight.  The same backlog made the main menu after boot
// "bimodal" in soak #6 (600 or 860 MB for one route).
//
// The test submits frames of real GPU work (big buffer copies, far slower
// than recording them), each with a completion flag, and checks after every
// EndHeadlessFrame that the frame two back has retired.
//
// Sabotage, MEASURED (np-soak3, engine worktree, one at a time):
//   * EndHeadlessFrame's wait loop disabled (ProcessEvents only) -> both
//     pacing tests fail: "36 of 38 frames returned from EndHeadlessFrame with
//     the frame two back still on the GPU" and "21 of 21 frames: EndDrawing
//     returned with older frames still queued";
//   * WgpuRnd::EndDrawing not calling EndHeadlessFrame ->
//     EndDrawingPacesTheHeadlessLoop fails, 19 of 21 (the direct test still
//     passes, as it should: it calls EndHeadlessFrame itself);
//   * default mHeadlessMaxInFlight 3 -> DefaultIsTwoFrames fails (3 != 2).
// Restored: 3 of 3 pass.

#include "test_helpers.h"

#include "platform/Rnd_Wgpu.h"
#include "rndobj/Rnd.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

class HeadlessFramesInFlightTest : public EngineTestFixture {
protected:
    static bool HaveHeadlessGpu() {
        return gWgpuRnd && gWgpuRnd->Gpu().IsReady() && gWgpuRnd->Gpu().IsHeadless();
    }

    // GPU work that takes the GPU far longer than the CPU takes to submit it:
    // kCopies copies of a kBytes buffer.
    static constexpr uint64_t kBytes = 64ull << 20;
    static constexpr int kCopies = 6;

    struct Work {
        wgpu::Buffer a, b;
    };
    static Work MakeWork() {
        wgpu::Device dev = gWgpuRnd->Gpu().Device();
        wgpu::BufferDescriptor d{};
        d.size = kBytes;
        d.usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
        return {dev.CreateBuffer(&d), dev.CreateBuffer(&d)};
    }
    static void SubmitWork(const Work &w) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        for (int i = 0; i < kCopies; i++) {
            if (i & 1)
                enc.CopyBufferToBuffer(w.b, 0, w.a, 0, kBytes);
            else
                enc.CopyBufferToBuffer(w.a, 0, w.b, 0, kBytes);
        }
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
    }
    // A flag set when everything submitted so far has retired.
    static void TrackDone(std::vector<std::shared_ptr<std::atomic<bool>>> &done) {
        auto flag = std::make_shared<std::atomic<bool>>(false);
        done.push_back(flag);
        gWgpuRnd->Gpu().Queue().OnSubmittedWorkDone(
            wgpu::CallbackMode::AllowProcessEvents,
            [flag](wgpu::QueueWorkDoneStatus, wgpu::StringView) { flag->store(true); });
    }
    static void Drain() {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        bool idle = false;
        gpu.Instance().WaitAny(
            gpu.Queue().OnSubmittedWorkDone(
                wgpu::CallbackMode::WaitAnyOnly,
                [&idle](wgpu::QueueWorkDoneStatus, wgpu::StringView) { idle = true; }),
            UINT64_MAX);
        gpu.Instance().ProcessEvents();
    }
};

TEST_F(HeadlessFramesInFlightTest, DefaultIsTwoFrames) {
    if (IsSkipped()) return;
    if (!HaveHeadlessGpu()) GTEST_SKIP() << "no headless GPU device";
    if (getenv("MILO_HEADLESS_FRAMES_IN_FLIGHT"))
        GTEST_SKIP() << "MILO_HEADLESS_FRAMES_IN_FLIGHT is set in this environment";
    EXPECT_EQ(gWgpuRnd->Gpu().HeadlessFramesInFlight(), 2);
}

TEST_F(HeadlessFramesInFlightTest, FrameTwoBackHasRetiredAfterEndHeadlessFrame) {
    if (IsSkipped()) return;
    if (!HaveHeadlessGpu()) GTEST_SKIP() << "no headless GPU device";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    const int was = gpu.HeadlessFramesInFlight();
    gpu.SetHeadlessFramesInFlight(2);
    Drain();
    Work w = MakeWork();
    std::vector<std::shared_ptr<std::atomic<bool>>> done;
    int late = 0, checked = 0;
    for (int k = 0; k < 40; k++) {
        SubmitWork(w);
        TrackDone(done);
        gpu.EndHeadlessFrame();
        gpu.Instance().ProcessEvents();  // deliver the flags of retired frames
        if (k >= 2) {
            checked++;
            if (!done[k - 2]->load()) late++;
        }
    }
    Drain();
    gpu.SetHeadlessFramesInFlight(was);
    EXPECT_EQ(late, 0) << late << " of " << checked
                       << " frames returned from EndHeadlessFrame with the frame two back "
                          "still on the GPU: the headless loop is not paced";
}

// The same, through the real frame: WgpuRnd::EndDrawing must reach
// EndHeadlessFrame.  Each "frame" is BeginDrawing + our heavy submit +
// EndDrawing.
TEST_F(HeadlessFramesInFlightTest, EndDrawingPacesTheHeadlessLoop) {
    if (IsSkipped()) return;
    if (!HaveHeadlessGpu()) GTEST_SKIP() << "no headless GPU device";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    const int was = gpu.HeadlessFramesInFlight();
    gpu.SetHeadlessFramesInFlight(2);
    Drain();
    Work w = MakeWork();
    std::vector<std::shared_ptr<std::atomic<bool>>> done;
    int late = 0, checked = 0;
    for (int k = 0; k < 24; k++) {
        TheRnd.BeginDrawing();
        SubmitWork(w);
        TrackDone(done);
        TheRnd.EndDrawing();
        gpu.Instance().ProcessEvents();
        if (k >= 3) {  // EndDrawing's own submit is the newest frame
            checked++;
            if (!done[k - 3]->load()) late++;
        }
    }
    Drain();
    gpu.SetHeadlessFramesInFlight(was);
    EXPECT_EQ(late, 0) << late << " of " << checked
                       << " frames: EndDrawing returned with older frames still queued";
}

}  // namespace
