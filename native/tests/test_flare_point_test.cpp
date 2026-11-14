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

} // namespace
