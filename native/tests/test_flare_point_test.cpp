// test_flare_point_test.cpp -- an unoccluded, point-tested flare draws at full
// strength natively (lane np-gfx-post, round 4).
//
// RndFlare::DrawShowing sets its material colour to
// environ ambient x clamp(depth-range t x step ratio x occlusion/rect-area). Retail
// (DxRnd::DoPointTests) answers the area test with an occlusion query over the
// flare's rect, so an unoccluded flare reads its whole on-screen area and draws at
// ratio 1. Native has no query in dc3's Rnd::TestPoint; its HX_NATIVE branch
// stores the rect's clipped on-screen area (a449ca1b9) for the same answer.
//
// Why it matters, measured against the original (round 4, DCI normal state):
// DCI's pendant lamps carry three CouchFlare*.flare (OverheadLightFlare.mat,
// additive). Native draws them at the table_flares.env ambient, 0xA1A1A1; the
// hardware footage (retail/dci/01) shows the same glow under the shade. Xenia's
// fork answers EVERY occlusion query with a fixed 1000 samples
// (query_occlusion_fake_sample_count), so a flare whose rect is ~80k pixels draws at
// about 1/80 strength there (OverheadLightFlare.mat read 0x000000..0x030303 in four
// Xenia probes) -- the "+43% lamp" of the round-4 brief is that Xenia artifact,
// not a native defect. This test keeps the native side where the hardware is.
//
// Sabotage tried: the HX_NATIVE branch back to the pre-a449ca1b9
// `SetOcclusionResult(1.0f)` -> strength 6.1e-05 over the test's 128x128 rect,
// fails; a Xenia-like fixed 1000 -> 0.061, fails.

#include "test_helpers.h"

#include "math/Geo.h"
#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Flare.h"
#include "rndobj/Mat.h"
#include "rndobj/Rnd.h"
#include "platform/PointTestHook.h"

#include <vector>

namespace {

class FlarePointTestTest : public EngineTestFixture {};

TEST_F(FlarePointTestTest, UnoccludedAreaTestedFlareDrawsAtFullStrength) {
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetUseEnv(false);  // colour = the flare's strength alone
    mat->SetBlend(BaseMaterial::kBlendAdd);
    mat->SetColor(0.0f, 0.0f, 0.0f);
    RndFlare *flare = Hmx::Object::New<RndFlare>();
    flare->SetMat(mat);
    flare->SetLocalPos(Vector3(0.0f, 100.0f, 0.0f));  // straight ahead, centred
    ASSERT_TRUE(flare->GetPointTest() && flare->GetAreaTest())
        << "the constructor defaults are what DCI's flares use";

    float strength[3] = { -1.0f, -1.0f, -1.0f };
    for (int frame = 0; frame < 3; frame++) {
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        flare->DrawShowing();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        strength[frame] = mat->GetColor().red;
    }
    const Hmx::Rect &a = flare->GetArea();
    ASSERT_GT(a.w * a.h, 100.0f) << "the flare rect is on screen and larger than a few pixels";
    // One step (mSteps 1), range (0,0) -> t 1, nothing occludes: full strength.
    EXPECT_NEAR(strength[2], 1.0f, 1.0e-4f)
        << "flare strength " << strength[0] << ", " << strength[1] << ", " << strength[2]
        << " over a " << a.w << "x" << a.h << " rect (a constant answer would give 1/area or 1000/area)";

    delete flare;
    delete mat;
    delete cam;
}


// Round 2 (lane np-gfx-blend): DC3's Rnd::TestPoint now hands in-view flares to
// the engine's occlusion queries (platform/PointTestHook.h -> gfx/PointTestPass),
// as rb3 does, instead of calling every flare fully visible. These tests stand
// in a fake tester for the GPU and deliver its answers the way WgpuRnd does --
// at world end, after the frame's DrawShowing -- so they pin the consumer half:
// what is queued, how an answer lands in the flare, and retail's fade.
//
// Sabotage tried (each fails at least one test below):
//  - TestPoint ignores the tester (the pre-round-2 HX_NATIVE branch): no test is
//    queued and an occluded flare stays at full strength;
//  - ApplyNativePointTest drops the point answer: the occluded flare never fades;
//  - it drops the area answer: a covered flare stays at full strength;
//  - RemovePointTest no longer cancels: a deleted flare's key stays queued.
struct FakeTester : NativePointTester {
    std::vector<NativePointTest> queued;
    std::vector<const void *> cancelled;
    bool accept = true;
    bool QueuePointTest(const NativePointTest &t) override {
        if (!accept) return false;
        queued.push_back(t);
        return true;
    }
    void CancelPointTests(const void *key) override { cancelled.push_back(key); }
};

struct ScopedTester {
    NativePointTester *prev;
    explicit ScopedTester(NativePointTester *t) : prev(GetNativePointTester()) {
        SetNativePointTester(t);
    }
    ~ScopedTester() { SetNativePointTester(prev); }
};

class FlareOcclusionTest : public EngineTestFixture {
protected:
    RndCam *cam = nullptr;
    RndMat *mat = nullptr;
    RndFlare *flare = nullptr;
    void SetUp() override {
        EngineTestFixture::SetUp();
        cam = Hmx::Object::New<RndCam>();
        cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
        mat = Hmx::Object::New<RndMat>();
        mat->SetUseEnv(false);
        mat->SetBlend(BaseMaterial::kBlendAdd);
        mat->SetColor(0.0f, 0.0f, 0.0f);
        flare = Hmx::Object::New<RndFlare>();
        flare->SetMat(mat);
        flare->SetLocalPos(Vector3(0.0f, 100.0f, 0.0f));
        flare->SetSteps(5);  // DCI's CouchFlare*.flare: steps 5
    }
    void TearDown() override {
        delete flare;
        delete mat;
        delete cam;
        EngineTestFixture::TearDown();
    }
    // One frame: the flare draws (queuing its test), then -- at "world end" --
    // the answers to the previous frame's tests are delivered.
    float Frame(FakeTester &t, bool visible, float areaFraction) {
        std::vector<NativePointTest> prior = t.queued;
        t.queued.clear();
        mat->SetColor(0.0f, 0.0f, 0.0f);
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        flare->DrawShowing();
        if (prev) prev->Select();
        if (NativePointTestResultFn fn = GetNativePointTestResultFn()) {
            for (const NativePointTest &q : prior) {
                NativePointTestResult r;
                r.key = q.key;
                r.pointDone = q.pointTest;
                r.visible = visible;
                r.areaDone = q.areaTest;
                r.area = areaFraction * q.rect[2] * q.rect[3];
                fn(r);
            }
        }
        TheRnd.EndDrawing();
        return mat->GetColor().red;
    }
};

TEST_F(FlareOcclusionTest, InViewFlareIsQueuedWithItsRect) {
    FakeTester t;
    ScopedTester st(&t);
    Frame(t, true, 1.0f);
    ASSERT_EQ(t.queued.size(), 1u) << "TestPoint must hand the flare to the GPU tester";
    const NativePointTest &q = t.queued[0];
    const Hmx::Rect &a = flare->GetArea();
    EXPECT_EQ(q.key, (const void *)flare);
    EXPECT_TRUE(q.pointTest);
    EXPECT_TRUE(q.areaTest);
    EXPECT_FLOAT_EQ(q.rect[0], a.x);
    EXPECT_FLOAT_EQ(q.rect[1], a.y);
    EXPECT_FLOAT_EQ(q.rect[2], a.w);
    EXPECT_FLOAT_EQ(q.rect[3], a.h);
    EXPECT_NEAR(q.screenX, 0.5f, 1e-3f);
    EXPECT_NEAR(q.screenY, 0.5f, 1e-3f);
    EXPECT_FLOAT_EQ(q.world[1], 100.0f) << "offset 0: the flare's own position";
}

TEST_F(FlareOcclusionTest, UnoccludedAnswerDrawsAtFullStrength) {
    FakeTester t;
    ScopedTester st(&t);
    float s = 0.0f;
    for (int i = 0; i < 8; i++) s = Frame(t, true, 1.0f);
    EXPECT_NEAR(s, 1.0f, 1e-4f);
}

TEST_F(FlareOcclusionTest, HalfCoveredRectDrawsAtHalfStrength) {
    FakeTester t;
    ScopedTester st(&t);
    float s = 0.0f;
    for (int i = 0; i < 8; i++) s = Frame(t, true, 0.5f);
    EXPECT_NEAR(s, 0.5f, 1e-3f);
}

TEST_F(FlareOcclusionTest, OccludedFlareGoesOutAndStepsBackIn) {
    FakeTester t;
    ScopedTester st(&t);
    for (int i = 0; i < 8; i++) Frame(t, true, 1.0f);
    // A dancer steps in front: the point query fails and the rect reads 0.
    // Retail (DxRnd::DoPointTests + RndFlare::DrawShowing): the answer arrives
    // at the next world end; the area answer scales the strength at once
    // (ratio = step/steps x visible area/rect area, no smoothing), while the
    // point answer moves mStep one step per frame. So a fully covered flare
    // goes out one frame late, and a partly covered one dims in proportion.
    std::vector<float> seq;
    for (int i = 0; i < 6; i++) seq.push_back(Frame(t, false, 0.0f));
    EXPECT_NEAR(seq[0], 1.0f, 1e-4f) << "the first occluded frame still shows last frame's answer";
    EXPECT_NEAR(seq[1], 0.0f, 1e-4f) << "fully covered: out one frame later, as retail";
    EXPECT_NEAR(seq.back(), 0.0f, 1e-4f);
    // Uncovered again: the area is whole at once but mStep climbs 1/5 per
    // frame (CouchFlare steps 5), so the flare fades back in over 5 frames.
    std::vector<float> back;
    for (int i = 0; i < 7; i++) back.push_back(Frame(t, true, 1.0f));
    EXPECT_NEAR(back[0], 0.0f, 1e-4f);
    EXPECT_NEAR(back[1], 0.2f, 1e-3f) << back[1] << " " << back[2] << " " << back[3];
    EXPECT_NEAR(back[2], 0.4f, 1e-3f);
    EXPECT_NEAR(back[3], 0.6f, 1e-3f);
    EXPECT_NEAR(back[5], 1.0f, 1e-3f);
}

TEST_F(FlareOcclusionTest, TesterRefusalKeepsTheUnoccludedFallback) {
    FakeTester t;
    t.accept = false;  // no frame to test against (headless, a render target)
    ScopedTester st(&t);
    float s = 0.0f;
    for (int i = 0; i < 3; i++) s = Frame(t, true, 1.0f);
    EXPECT_NEAR(s, 1.0f, 1e-4f);
}

TEST_F(FlareOcclusionTest, RemovedFlareCancelsItsTests) {
    FakeTester t;
    ScopedTester st(&t);
    Frame(t, true, 1.0f);
    flare->SetPointTest(false);
    ASSERT_FALSE(t.cancelled.empty());
    EXPECT_EQ(t.cancelled.back(), (const void *)flare);
}

} // namespace
