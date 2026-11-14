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
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "gfx/PointTestStandIns.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
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

    // DC3 does not wait for a readback after the submit (round 3,
    // rndshape::kPointTestWaitAfterSubmit), so in this unpaced loop an answer
    // can take more than the one frame retail's does: draw paced frames until
    // it lands, within a bound.
    std::vector<float> strength;
    for (int frame = 0; frame < 30; frame++) {
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        flare->DrawShowing();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        strength.push_back(mat->GetColor().red);
        if (frame >= 2 && strength.back() > 0.999f) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const Hmx::Rect &a = flare->GetArea();
    ASSERT_GT(a.w * a.h, 100.0f) << "the flare rect is on screen and larger than a few pixels";
    // One step (mSteps 1), range (0,0) -> t 1, nothing occludes: full strength.
    std::string seq;
    for (float v : strength) seq += std::to_string(v) + " ";
    EXPECT_NEAR(strength.back(), 1.0f, 1.0e-4f)
        << "flare strength " << seq << "over a " << a.w << "x" << a.h
        << " rect (a constant answer would give 1/area or 1000/area)";

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

    // As Frame, the way WgpuRnd delivers without the after-submit wait (DC3,
    // rndshape::kPointTestWaitAfterSubmit false): with `late` the previous
    // frame's answers are still being read back at this world end and arrive
    // at the next one, oldest first; meanwhile, given `standIns`, each late
    // key gets its last real answer (gfx/PointTestStandIns.h).
    std::vector<PointTestPass::Answer> held;
    static void ToFlare(const PointTestPass::Answer &a, void *) {
        NativePointTestResultFn fn = GetNativePointTestResultFn();
        if (!fn) return;
        NativePointTestResult r;
        r.key = a.key;
        r.pointDone = a.pointDone;
        r.visible = a.visible;
        r.areaDone = a.areaDone;
        r.area = a.area;
        fn(r);
    }
    float FrameNoWait(FakeTester &t, bool visible, float areaFraction, bool late,
                      PointTestStandIns *standIns) {
        std::vector<NativePointTest> prior = t.queued;
        t.queued.clear();
        mat->SetColor(0.0f, 0.0f, 0.0f);
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        flare->DrawShowing();
        if (prev) prev->Select();
        std::vector<PointTestPass::Answer> answers;
        for (const NativePointTest &q : prior) {
            PointTestPass::Answer a;
            a.key = q.key;
            a.pointDone = q.pointTest;
            a.visible = visible;
            a.areaDone = q.areaTest;
            a.area = areaFraction * q.rect[2] * q.rect[3];
            answers.push_back(a);
        }
        std::vector<PointTestPass::Answer> deliver;
        deliver.swap(held);
        if (late)
            held = answers;
        else
            deliver.insert(deliver.end(), answers.begin(), answers.end());
        for (const PointTestPass::Answer &a : deliver) {
            if (standIns) standIns->Note(a);
            ToFlare(a, nullptr);
        }
        if (late && standIns) {
            std::vector<const void *> keys;
            for (const PointTestPass::Answer &a : held) keys.push_back(a.key);
            standIns->Deliver(keys, ToFlare, nullptr);
        }
        TheRnd.EndDrawing();
        return mat->GetColor().red;
    }
    // Covered for a while, then uncovered with the first uncovered answer a
    // frame late. Returns the strengths from the last covered frame on.
    std::vector<float> UncoverWithALateAnswer(FakeTester &t, PointTestStandIns *standIns) {
        for (int i = 0; i < 8; i++) FrameNoWait(t, false, 0.0f, false, standIns);
        std::vector<float> seq;
        seq.push_back(FrameNoWait(t, false, 0.0f, false, standIns));
        seq.push_back(FrameNoWait(t, true, 1.0f, true, standIns));
        for (int i = 0; i < 7; i++) seq.push_back(FrameNoWait(t, true, 1.0f, false, standIns));
        return seq;
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

// DC3 does not wait after the submit for point-test answers that were not ready
// at world end (round 3): an answer is sometimes a frame late. Retail
// RndFlare::DrawShowing reads a frame without an answer as a flare just come
// into view and snaps mStep to the end of its fade on the next one, so a
// steps-5 flare (CouchFlare*, rollerrink's food and disco-ball flares) whose
// uncovering answer is late pops from 0 to full. The late key's last answer
// stands in (PointTestStandIns), and the flare fades in a frame later instead.
//
// Sabotage tried (each fails this test or the next): Deliver delivers nothing;
// Note keeps the first answer instead of the latest; Forget leaves the entry;
// kPointTestWaitAfterSubmit true for dc3-decomp.
TEST_F(FlareOcclusionTest, LateAnswerStillFadesInRatherThanPopping) {
    FakeTester t;
    ScopedTester st(&t);
    // Control: without a stand-in the scenario does pop, so it can tell.
    held.clear();
    std::vector<float> bare = UncoverWithALateAnswer(t, nullptr);
    float bareJump = 0.0f;
    for (size_t i = 1; i < bare.size(); i++) bareJump = std::max(bareJump, bare[i] - bare[i - 1]);
    ASSERT_GT(bareJump, 0.5f) << "control: a late answer with no stand-in should snap";

    // A fresh flare state, then the same frames with stand-ins.
    for (int i = 0; i < 3; i++) Frame(t, false, 0.0f);
    held.clear();
    PointTestStandIns standIns;
    std::vector<float> seq = UncoverWithALateAnswer(t, &standIns);
    for (size_t i = 1; i < seq.size(); i++)
        EXPECT_LE(seq[i] - seq[i - 1], 0.2f + 1e-3f)
            << "frame " << i << ": " << seq[i - 1] << " -> " << seq[i]
            << " (steps 5: at most 1/5 a frame)";
    EXPECT_NEAR(seq[1], 0.0f, 1e-4f) << "the late frame keeps the covered answer";
    EXPECT_NEAR(seq[3], 0.2f, 1e-3f) << "the fade starts a frame later than on time";
    EXPECT_NEAR(seq.back(), 1.0f, 1e-3f) << "and completes";
}

TEST_F(FlareOcclusionTest, StandInsFollowTheLatestAnswerAndForgetCancelledKeys) {
    PointTestStandIns s;
    int k1 = 0, k2 = 0;
    PointTestPass::Answer a;
    a.key = &k1;
    a.pointDone = true;
    a.visible = true;
    a.areaDone = true;
    a.area = 10.0f;
    s.Note(a);
    a.visible = false;
    a.area = 3.0f;
    s.Note(a);
    std::vector<PointTestPass::Answer> got;
    auto collect = [](const PointTestPass::Answer &x, void *u) {
        static_cast<std::vector<PointTestPass::Answer> *>(u)->push_back(x);
    };
    EXPECT_EQ(s.Deliver({&k1, &k2, &k1}, collect, &got), 1)
        << "one stand-in per key; none for a key never answered";
    ASSERT_EQ(got.size(), 1u);
    EXPECT_FALSE(got[0].visible) << "the latest answer, not the first";
    EXPECT_FLOAT_EQ(got[0].area, 3.0f);
    s.Forget(&k1);
    got.clear();
    EXPECT_EQ(s.Deliver({&k1}, collect, &got), 0) << "a cancelled flare gets no stand-in";
}

// The shape constant, and that the frame end really does not block: real
// flare tests through WgpuRnd's PointTestPass over several frames.
TEST_F(FlareOcclusionTest, DC3FrameEndDoesNotWaitForPointTests) {
    EXPECT_FALSE(rndshape::kPointTestWaitAfterSubmit)
        << "dc3-decomp takes a late answer instead of waiting after the submit";
    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady()) return;  // no device: the constant is the test
    const uint64_t waits = gWgpuRnd->PointTestEndWaits();
    int queued = 0;
    for (int i = 0; i < 6; i++) {
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        flare->DrawShowing();
        queued += gWgpuRnd->PendingPointTests();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
    }
    ASSERT_GT(queued, 0) << "no flare test reached the GPU pass: the check below would be vacuous";
    EXPECT_EQ(gWgpuRnd->PointTestEndWaits(), waits)
        << "FinishPointTestReadback blocked on a readback";
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
