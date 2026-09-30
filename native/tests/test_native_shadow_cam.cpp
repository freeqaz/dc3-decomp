// Native-shadow regression tests: RndCam::WorldToScreen / ScreenToWorld.
//
// The native build used to compile its own WorldToScreen/ScreenToWorld that
// rebuilt the camera from GetViewProjectXfms() (the D3D-style view*projection
// the renderer uploads, NDC y UP). The image projects through the camera's
// cached Milo transforms instead (Cam.s):
//
//   WorldToScreen  82628298..A8  addi r4,r30,0x180 ; bl Multiply(Vector3,Transform)
//                                  -> mWorldProjectXfm
//                  826282B4       lfs f1,0x58(r1)   -> returns projected z
//   ScreenToWorld  82628374       addi r4,r3,0x1c0  -> mInvWorldProjectXfm
//                  826283A8/B0    fmuls by f1       -> x,y scaled by depth
//
// and mLocalProjectXfm.m.z.y (0x124) is -1/tan(fov/2) (UpdateLocal
// 82628674..98: lfs __real@bf800000; fmuls; stfs 0x124(r31)), so a point
// ABOVE the view centre lands at screen y < 0.5 (screen y grows DOWN, the
// same convention Rnd::DrawRect / DrawString use). Native had it mirrored.
//
// For an orthographic camera (mYFov == 0) the local projection has an
// all-zero z column (UpdateLocal 82628620..48 stores only 0x100, 0x124,
// 0x140, 0x158), so WorldToScreen returns 0 and ScreenToWorld lands on the
// camera plane with x/y scaled by the depth argument.

#include "test_helpers.h"

#include "math/Mtx.h"
#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"

#include <cmath>

namespace {

class NativeShadowCamTest : public EngineTestFixture {};

constexpr float kFov = 0.6024178f;
constexpr float kEps = 1.0e-4f;

RndCam *MakeCam(const char *name, float yFov) {
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetName(name, ObjectDir::Main());
    cam->SetLocalRot(Hmx::Matrix3::GetIdentity());
    cam->SetLocalPos(Vector3(0.0f, 0.0f, 0.0f));
    cam->SetScreenRect(Hmx::Rect(0.0f, 0.0f, 1.0f, 1.0f));
    cam->SetFrustum(1.0f, 1000.0f, yFov, 1.0f);
    cam->UpdatedWorldXfm();
    return cam;
}

} // namespace

// Milo camera space: x right, y forward, z up. A point half-way to the top
// edge of the frustum must land half-way between centre (0.5) and the TOP of
// the screen (0.0).
TEST_F(NativeShadowCamTest, WorldToScreenScreenYGrowsDownward) {
    RndCam *cam = MakeCam("ns_cam_w2s_persp", kFov);
    const float depth = 10.0f;
    const float halfUp = depth * std::tan(kFov * 0.5f) * 0.5f;

    Vector2 above;
    float d = cam->WorldToScreen(Vector3(0.0f, depth, halfUp), above);
    EXPECT_NEAR(d, depth, kEps);
    EXPECT_NEAR(above.x, 0.5f, kEps);
    EXPECT_NEAR(above.y, 0.25f, 1.0e-3f);

    Vector2 below;
    cam->WorldToScreen(Vector3(0.0f, depth, -halfUp), below);
    EXPECT_NEAR(below.y, 0.75f, 1.0e-3f);
    delete cam;
}

TEST_F(NativeShadowCamTest, ScreenToWorldScreenYGrowsDownward) {
    RndCam *cam = MakeCam("ns_cam_s2w_persp", kFov);
    const float depth = 10.0f;
    const float halfUp = depth * std::tan(kFov * 0.5f) * 0.5f;

    Vector3 w;
    cam->ScreenToWorld(Vector2(0.5f, 0.25f), depth, w);
    EXPECT_NEAR(w.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(w.y, depth, 1.0e-3f);
    EXPECT_NEAR(w.z, halfUp, 1.0e-3f);
    delete cam;
}

// Ortho: projected z is identically 0, so the image returns 0 (and skips the
// perspective divide); x passes through m.x.x = 1, y through m.z.y = -1/ratio.
TEST_F(NativeShadowCamTest, WorldToScreenOrthoReturnsZeroDepth) {
    RndCam *cam = MakeCam("ns_cam_w2s_ortho", 0.0f);
    const float ratio = TheRnd.YRatio();

    Vector2 s;
    float d = cam->WorldToScreen(Vector3(0.5f, 7.0f, 0.5f * ratio), s);
    EXPECT_EQ(d, 0.0f);
    EXPECT_NEAR(s.x, 0.75f, kEps);
    EXPECT_NEAR(s.y, 0.25f, kEps);
    delete cam;
}

// Ortho inverse: m.x.x = 1, m.y.z = -ratio, z row all zero -- the result sits
// on the camera plane (forward 0) and x/y are scaled by the depth argument.
TEST_F(NativeShadowCamTest, ScreenToWorldOrthoScalesByDepthOnCameraPlane) {
    RndCam *cam = MakeCam("ns_cam_s2w_ortho", 0.0f);
    const float ratio = TheRnd.YRatio();

    Vector3 w;
    cam->ScreenToWorld(Vector2(0.75f, 0.25f), 4.0f, w);
    EXPECT_NEAR(w.x, 2.0f, kEps);          // (0.75*2-1) * 4
    EXPECT_NEAR(w.y, 0.0f, kEps);          // no forward component
    EXPECT_NEAR(w.z, 2.0f * ratio, kEps);  // -((0.25*2-1) * 4) * ratio
    delete cam;
}
