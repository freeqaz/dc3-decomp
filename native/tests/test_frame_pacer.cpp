// CPU-use guards (lane np-cpu-perf, PLAN H-14):
//   * the frame cap's decision (DC3_FPS_CAP x windowed/headless x display rate)
//     and that the pacer SLEEPS rather than spins;
//   * the engine's backend-label switch (MILO_GPU_LABELS) and the render
//     pipeline cache key DrawRect2D/particles use.
// The engine-run halves (headless stays uncapped; DC3_FPS_CAP caps it when
// asked) are in test_headless_boot.cpp.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

#include "gfx/GpuDevice.h"
#include "gfx/RenderPipelineCache.h"
#include "platform/FramePacer_Native.h"

namespace {

double ThreadCpuSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

double WallSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Sets (or unsets, value == nullptr) an env var for one scope.
class ScopedEnv {
public:
    ScopedEnv(const char *name, const char *value) : mName(name) {
        const char *old = getenv(name);
        mHad = old != nullptr;
        if (mHad) mOld = old;
        if (value)
            setenv(name, value, 1);
        else
            unsetenv(name);
    }
    ~ScopedEnv() {
        if (mHad)
            setenv(mName.c_str(), mOld.c_str(), 1);
        else
            unsetenv(mName.c_str());
    }

private:
    std::string mName, mOld;
    bool mHad = false;
};

} // namespace

// --- the decision ------------------------------------------------------------

TEST(FrameCap, HeadlessIsUncappedUnlessAsked) {
    // The gate's flow tests run headless faster than real time (UI time is a
    // fixed 1/30 s per frame there). A default cap would slow every one of them.
    EXPECT_EQ(Dc3FrameCapDecide(nullptr, false, 0.0).fps, 0.0);
    EXPECT_EQ(Dc3FrameCapDecide("", false, 0.0).fps, 0.0);
    EXPECT_EQ(Dc3FrameCapDecide("garbage", false, 0.0).fps, 0.0);
    EXPECT_EQ(Dc3FrameCapDecide("0", false, 0.0).fps, 0.0);
    // Asked for: exactly N, no display to snap to.
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("30", false, 0.0).fps, 30.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("60", false, 144.0).fps, 60.0);
}

TEST(FrameCap, WindowedDefaultIsTheImagesSixtySnappedToTheDisplay) {
    // Never slower than 60, every frame held the same number of refreshes.
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide(nullptr, true, 144.0).fps, 72.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide(nullptr, true, 120.0).fps, 60.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide(nullptr, true, 165.0).fps, 82.5);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide(nullptr, true, 240.0).fps, 60.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide(nullptr, true, 119.88).fps, 59.94);
    // At or under 60 Hz (or under 2x): vsync alone paces, no second clock.
    EXPECT_EQ(Dc3FrameCapDecide(nullptr, true, 60.0).fps, 0.0);
    EXPECT_EQ(Dc3FrameCapDecide(nullptr, true, 59.94).fps, 0.0);
    EXPECT_EQ(Dc3FrameCapDecide(nullptr, true, 75.0).fps, 0.0);
    EXPECT_EQ(Dc3FrameCapDecide(nullptr, true, 90.0).fps, 0.0);
    // Display rate unknown: the plain cap.
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide(nullptr, true, 0.0).fps, 60.0);
    // A malformed value keeps the default rather than silently uncapping.
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("sixty", true, 144.0).fps, 72.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("-5", true, 144.0).fps, 72.0);
}

TEST(FrameCap, WindowedOverrides) {
    for (const char *off : {"0", "off", "OFF", "no", "false"})
        EXPECT_EQ(Dc3FrameCapDecide(off, true, 144.0).fps, 0.0) << off;
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("30", true, 144.0).fps, 36.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("30", true, 60.0).fps, 30.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("144", true, 144.0).fps, 0.0);
    EXPECT_DOUBLE_EQ(Dc3FrameCapDecide("45", true, 0.0).fps, 45.0);
}

// --- the pacer ---------------------------------------------------------------

TEST(FramePacer, SleepsRatherThanSpins) {
    Dc3FramePacer pacer;
    pacer.SetTarget(200.0); // 5 ms
    const int frames = 40;  // ~200 ms
    const double w0 = WallSeconds(), c0 = ThreadCpuSeconds();
    for (int i = 0; i < frames; i++)
        pacer.Pace();
    const double wall = WallSeconds() - w0, cpu = ThreadCpuSeconds() - c0;
    EXPECT_GE(wall, 0.9 * frames / 200.0) << "the pacer did not hold the frame rate";
    EXPECT_LT(wall, 5.0 * frames / 200.0);
    // A spin (busy-wait, sleep(0), yield loop) burns ~100% of the wall time.
    EXPECT_LT(cpu, 0.25 * wall) << "the pacer used " << cpu << " s of CPU in " << wall
                                << " s: it spins instead of sleeping";
    // Nearly every frame slept (a loaded machine can overshoot one sleep enough
    // that the next deadline has already passed).
    EXPECT_GE(pacer.PacedFrames(), frames * 3 / 4);
    EXPECT_GT(pacer.SleptSeconds(), 0.8 * frames / 200.0);
}

TEST(FramePacer, UncappedIsFree) {
    Dc3FramePacer pacer;
    pacer.SetTarget(0.0);
    const double w0 = WallSeconds();
    for (int i = 0; i < 10000; i++)
        pacer.Pace();
    EXPECT_LT(WallSeconds() - w0, 0.05);
    EXPECT_EQ(pacer.PacedFrames(), 0);
}

TEST(FramePacer, WorkInTheFrameIsNotAddedToThePeriod) {
    // 3 ms of work in a 10 ms frame: the frame takes 10 ms, not 13.
    Dc3FramePacer pacer;
    pacer.SetTarget(100.0);
    const int frames = 20;
    const double w0 = WallSeconds();
    for (int i = 0; i < frames; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        pacer.Pace();
    }
    const double wall = WallSeconds() - w0;
    EXPECT_LT(wall, 1.15 * frames / 100.0 + 0.01);
    EXPECT_GE(wall, 0.9 * frames / 100.0);
}

TEST(FramePacer, ALongFrameDoesNotBuyABurst) {
    Dc3FramePacer pacer;
    pacer.SetTarget(100.0);
    pacer.Pace();
    std::this_thread::sleep_for(std::chrono::milliseconds(150)); // a 15-frame hitch (a load)
    pacer.Pace();                                                 // resynchronises
    const double w0 = WallSeconds();
    for (int i = 0; i < 5; i++)
        pacer.Pace();
    // Paced again at once: 5 frames take ~50 ms, not ~0 (catching up).
    EXPECT_GE(WallSeconds() - w0, 0.04);
}

// --- engine: backend labels and the pipeline cache key ------------------------

TEST(GpuLabels, OffUnlessAskedOrUnderACaptureLayer) {
    ScopedEnv a("MILO_GPU_LABELS", nullptr), b("VK_INSTANCE_LAYERS", nullptr),
        c("VK_LOADER_LAYERS_ENABLE", nullptr), d("ENABLE_VULKAN_RENDERDOC_CAPTURE", nullptr);
    EXPECT_FALSE(GpuLabelsWanted());
    {
        ScopedEnv on("MILO_GPU_LABELS", "1");
        EXPECT_TRUE(GpuLabelsWanted());
    }
    {
        ScopedEnv layer("VK_INSTANCE_LAYERS", "VK_LAYER_LUNARG_gfxreconstruct");
        EXPECT_TRUE(GpuLabelsWanted()) << "a GFXReconstruct capture would lose its object names";
        ScopedEnv off("MILO_GPU_LABELS", "0");
        EXPECT_FALSE(GpuLabelsWanted()) << "MILO_GPU_LABELS=0 must win";
    }
    {
        ScopedEnv rdoc("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1");
        EXPECT_TRUE(GpuLabelsWanted());
    }
    {
        ScopedEnv rdoc("ENABLE_VULKAN_RENDERDOC_CAPTURE", "0");
        EXPECT_FALSE(GpuLabelsWanted());
    }
    {
        ScopedEnv loader("VK_LOADER_LAYERS_ENABLE", "*validation");
        EXPECT_TRUE(GpuLabelsWanted());
    }
}

TEST(RenderPipelineCacheKey, EveryPerDrawInputSeparatesPipelines) {
    // A collision here would draw a rect or particle system with another
    // draw's blend, target or depth state.
    wgpu::BlendState base{};
    base.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha,
                  wgpu::BlendFactor::OneMinusSrcAlpha};
    base.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::Zero};
    const auto fmt = wgpu::TextureFormat::BGRA8Unorm;
    const auto all = wgpu::ColorWriteMask::All;
    const auto key = RenderPipelineCache::MakeKey(base, fmt, all, true, 4, 0.0f, 1);
    EXPECT_EQ(key, RenderPipelineCache::MakeKey(base, fmt, all, true, 4, 0.0f, 1));

    auto blendVariant = [&](auto mutate) {
        wgpu::BlendState b = base;
        mutate(b);
        return RenderPipelineCache::MakeKey(b, fmt, all, true, 4, 0.0f, 1);
    };
    EXPECT_NE(key, blendVariant([](auto &b) { b.color.operation = wgpu::BlendOperation::Max; }));
    EXPECT_NE(key, blendVariant([](auto &b) { b.color.srcFactor = wgpu::BlendFactor::One; }));
    EXPECT_NE(key, blendVariant([](auto &b) { b.color.dstFactor = wgpu::BlendFactor::One; }));
    EXPECT_NE(key, blendVariant([](auto &b) { b.alpha.operation = wgpu::BlendOperation::Max; }));
    EXPECT_NE(key, blendVariant([](auto &b) { b.alpha.srcFactor = wgpu::BlendFactor::Zero; }));
    EXPECT_NE(key, blendVariant([](auto &b) { b.alpha.dstFactor = wgpu::BlendFactor::One; }));
    EXPECT_NE(key, RenderPipelineCache::MakeKey(base, wgpu::TextureFormat::RGBA8Unorm, all, true,
                                                4, 0.0f, 1));
    EXPECT_NE(key, RenderPipelineCache::MakeKey(base, fmt, wgpu::ColorWriteMask::Red, true, 4,
                                                0.0f, 1));
    EXPECT_NE(key, RenderPipelineCache::MakeKey(base, fmt, all, false, 4, 0.0f, 1));
    EXPECT_NE(key, RenderPipelineCache::MakeKey(base, fmt, all, true, 1, 0.0f, 1));
    EXPECT_NE(key, RenderPipelineCache::MakeKey(base, fmt, all, true, 4, 0.5f, 1));
    EXPECT_NE(key, RenderPipelineCache::MakeKey(base, fmt, all, true, 4, 0.0f, 0));
}
