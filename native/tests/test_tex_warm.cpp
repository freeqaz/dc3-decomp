// test_tex_warm.cpp -- the load-time texture warm queue (np-gfx spotperf
// round 6, branch np-texwarm, PLAN H-116; Tex_Wgpu.cpp QueueGpuTexWarm /
// PumpGpuTexWarms).
//
// DC3's NativeWarmLoadedDir queues every loaded dir's textures and the
// renderer creates them a budget's worth a frame, through the PresyncBitmap a
// draw calls. This pins the queue itself:
//  - a queued texture is created by the pump, counted as a warm create, and
//    its draw then creates nothing;
//  - the budget bounds a pump (at 0 ms: one texture a call), in queue order;
//  - a texture deleted while queued leaves the queue (the pump never touches
//    it);
//  - what PresyncBitmap does not create from a bitmap is not queued (another
//    type, a render target), nor one already created, nor one twice.
// The in-game half -- no texture created at a draw once game_screen is up --
// is PerformCrowdTest (ExpectNoTextureCreatedAfterLoad).
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round6):
//  - CleanupGpuTex not leaving the queue -> the deleted texture stays
//    pending, red;
//  - the pump ignoring its budget -> one call takes all three, red;
//  - PresyncBitmap not counting a warm's create -> GpuTexWarmCreateCount
//    stays, red.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter).

#include "test_helpers.h"

#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"

#include <vector>

namespace {

const int kSize = 32;

// A 32x32 32 bpp bitmap set on `tex` WITHOUT a PresyncBitmap (what a load
// leaves: the bitmap is there, the GPU texture is not).
void SetPixels(RndTex *tex, int seed, RndTex::Type type = RndTex::kRegular) {
    std::vector<unsigned char> px(kSize * kSize * 4);
    for (int i = 0; i < kSize * kSize; i++) {
        px[i * 4 + 0] = (unsigned char)(i * 5 + seed);
        px[i * 4 + 1] = (unsigned char)(i * 11 + seed * 7);
        px[i * 4 + 2] = (unsigned char)(i * 3 + seed * 13);
        px[i * 4 + 3] = 0xFF;
    }
    RndBitmap bmp;
    bmp.Create(kSize, kSize, 0, 32, 0, nullptr, (void *)px.data(), nullptr);
    tex->SetBitmap(bmp, nullptr, true, type);
}

RndTex *Make(int seed, RndTex::Type type = RndTex::kRegular) {
    RndTex *tex = Hmx::Object::New<RndTex>();
    SetPixels(tex, seed, type);
    return tex;
}

unsigned long long Creates() { return GpuTexCreateCount(); }

class TexWarmTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
        mBudget = GpuTexWarmBudgetMs();
        // Whatever an earlier test or the fixture left queued.
        SetGpuTexWarmBudgetMs(1e9);
        PumpGpuTexWarms();
    }
    void TearDown() override {
        SetGpuTexWarmBudgetMs(mBudget);
        EngineTestFixture::TearDown();
    }
    double mBudget = 2.0;
};

} // namespace

TEST_F(TexWarmTest, QueuedTextureIsCreatedByThePumpNotTheDraw) {
    if (IsSkipped()) return;
    // Seeds no other test in the process uploads (a share hit creates nothing).
    RndTex *a = Make(171);
    ASSERT_FALSE(GetGpuTexDebugInfo(a).uploaded) << "SetBitmap created the texture already";
    const unsigned long long c0 = Creates(), w0 = GpuTexWarmCreateCount();
    ASSERT_TRUE(QueueGpuTexWarm(a));
    EXPECT_EQ(GpuTexWarmsPending(), 1u);
    EXPECT_FALSE(QueueGpuTexWarm(a)) << "queued twice";
    EXPECT_EQ(PumpGpuTexWarms(), 1);
    EXPECT_EQ(GpuTexWarmsPending(), 0u);
    EXPECT_TRUE(GetGpuTexDebugInfo(a).uploaded);
    EXPECT_EQ(Creates() - c0, 1u);
    EXPECT_EQ(GpuTexWarmCreateCount() - w0, 1u) << "the pump's create is not counted as a warm";
    // The draw's call finds it done.
    a->PresyncBitmap();
    EXPECT_EQ(Creates() - c0, 1u) << "the draw created it again";
    EXPECT_FALSE(QueueGpuTexWarm(a)) << "an uploaded texture was queued";
    delete a;
}

TEST_F(TexWarmTest, BudgetBoundsAPumpInQueueOrder) {
    if (IsSkipped()) return;
    RndTex *t[3] = {Make(181), Make(182), Make(183)};
    for (RndTex *x : t) ASSERT_TRUE(QueueGpuTexWarm(x));
    SetGpuTexWarmBudgetMs(0.0);  // one texture a call
    EXPECT_EQ(PumpGpuTexWarms(), 1);
    EXPECT_TRUE(GetGpuTexDebugInfo(t[0]).uploaded);
    EXPECT_FALSE(GetGpuTexDebugInfo(t[1]).uploaded);
    EXPECT_EQ(GpuTexWarmsPending(), 2u);
    EXPECT_EQ(PumpGpuTexWarms(), 1);
    EXPECT_TRUE(GetGpuTexDebugInfo(t[1]).uploaded);
    EXPECT_FALSE(GetGpuTexDebugInfo(t[2]).uploaded);
    EXPECT_EQ(PumpGpuTexWarms(), 1);
    EXPECT_EQ(PumpGpuTexWarms(), 0) << "an empty queue pumped something";
    EXPECT_EQ(GpuTexWarmsPending(), 0u);
    for (RndTex *x : t) delete x;
}

TEST_F(TexWarmTest, DeletedWhileQueuedLeavesTheQueue) {
    if (IsSkipped()) return;
    RndTex *a = Make(191), *b = Make(192);
    ASSERT_TRUE(QueueGpuTexWarm(a));
    ASSERT_TRUE(QueueGpuTexWarm(b));
    delete a;
    EXPECT_EQ(GpuTexWarmsPending(), 1u) << "a deleted texture is still queued";
    const unsigned long long w0 = GpuTexWarmCreateCount();
    EXPECT_EQ(PumpGpuTexWarms(), 1);
    EXPECT_EQ(GpuTexWarmCreateCount() - w0, 1u);
    EXPECT_TRUE(GetGpuTexDebugInfo(b).uploaded);
    delete b;
}

TEST_F(TexWarmTest, OnlyRegularBitmapTexturesAreQueued) {
    if (IsSkipped()) return;
    RndTex *lin = Make(201, RndTex::kRegularLinear);
    EXPECT_FALSE(QueueGpuTexWarm(lin)) << "a kRegularLinear texture was queued";
    RndTex *rt = Hmx::Object::New<RndTex>();
    rt->SetBitmap(kSize, kSize, 32, RndTex::kRendered, false, nullptr);
    EXPECT_FALSE(QueueGpuTexWarm(rt)) << "a render target was queued";
    EXPECT_FALSE(QueueGpuTexWarm(nullptr));
    EXPECT_EQ(GpuTexWarmsPending(), 0u);
    delete lin;
    delete rt;
}
