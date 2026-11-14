#include "test_helpers.h"

#include "math/Mtx.h"
#include "math/Rot.h"
#include "hamobj/HamListRibbon.h"
#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Draw.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "platform/UiRenderHeuristics.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Trans.h"
#include "ui/UIListMesh.h"

#include <cmath>
#include <vector>

class RndCamProjectionTest : public EngineTestFixture {};

namespace {
constexpr float kEps = 1.0e-4f;

RndCam *MakeTestCam(const char *name, float nearPlane, float farPlane, float yFov) {
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetName(name, ObjectDir::Main());
    cam->SetLocalRot(Hmx::Matrix3::GetIdentity());
    cam->SetLocalPos(Vector3(0.0f, 0.0f, 0.0f));
    cam->SetScreenRect(Hmx::Rect(0.0f, 0.0f, 1.0f, 1.0f));
    cam->SetFrustum(nearPlane, farPlane, yFov, 1.0f);
    cam->UpdatedWorldXfm();
    return cam;
}
}

TEST_F(RndCamProjectionTest, PerspectiveIdentityProjectionMatchesExpectedMatrix) {
    const float nearPlane = 1.0f;
    const float farPlane = 1000.0f;
    const float yFov = 0.6024178f;
    RndCam *cam = MakeTestCam("test_cam_persp", nearPlane, farPlane, yFov);

    Transform viewXfm;
    Hmx::Matrix4 projMtx;
    cam->GetViewProjectXfms(viewXfm, projMtx);

    EXPECT_NEAR(viewXfm.m.x.x, 1.0f, kEps);
    EXPECT_NEAR(viewXfm.m.x.y, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.m.x.z, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.m.y.x, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.m.y.y, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.m.y.z, 1.0f, kEps);
    EXPECT_NEAR(viewXfm.m.z.x, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.m.z.y, 1.0f, kEps);
    EXPECT_NEAR(viewXfm.m.z.z, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.v.x, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.v.y, 0.0f, kEps);
    EXPECT_NEAR(viewXfm.v.z, 0.0f, kEps);

    const float tanHalf = std::tanf(yFov * 0.5f);
    const float ratio = TheRnd.YRatio();
    const float farRatio = farPlane / (farPlane - nearPlane);

    EXPECT_NEAR(projMtx.x.x, ratio / tanHalf, kEps);
    EXPECT_NEAR(projMtx.y.y, 1.0f / tanHalf, kEps);
    EXPECT_NEAR(projMtx.z.x, 0.0f, kEps);
    EXPECT_NEAR(projMtx.z.y, 0.0f, kEps);
    EXPECT_NEAR(projMtx.z.z, farRatio, kEps);
    EXPECT_NEAR(projMtx.z.w, 1.0f, kEps);
    EXPECT_NEAR(projMtx.w.z, -(nearPlane * farRatio), kEps);
    EXPECT_NEAR(projMtx.w.w, 0.0f, kEps);
}

TEST_F(RndCamProjectionTest, PerspectiveWorldToScreenMatchesExpectedFrustumEdges) {
    const float nearPlane = 1.0f;
    const float farPlane = 1000.0f;
    const float yFov = 0.6024178f;
    const float depth = 10.0f;
    RndCam *cam = MakeTestCam("test_cam_frustum", nearPlane, farPlane, yFov);

    const float tanHalf = std::tanf(yFov * 0.5f);
    const float ratio = TheRnd.YRatio();

    Vector2 screen;

    cam->WorldToScreen(Vector3(0.0f, depth, 0.0f), screen);
    EXPECT_NEAR(screen.x, 0.5f, kEps);
    EXPECT_NEAR(screen.y, 0.5f, kEps);

    cam->WorldToScreen(Vector3(depth * tanHalf / ratio, depth, 0.0f), screen);
    EXPECT_NEAR(screen.x, 1.0f, 2.0e-3f);
    EXPECT_NEAR(screen.y, 0.5f, 2.0e-3f);

    // Top edge of the frustum is screen y = 0: screen y grows DOWN. The image
    // projects through mWorldProjectXfm, whose m.z.y is -1/tan(fov/2)
    // (Cam.s UpdateLocal 82628694/98). This used to expect 1.0 -- the
    // mirrored value the old native-only body produced.
    cam->WorldToScreen(Vector3(0.0f, depth, depth * tanHalf), screen);
    EXPECT_NEAR(screen.x, 0.5f, 2.0e-3f);
    EXPECT_NEAR(screen.y, 0.0f, 2.0e-3f);
}

TEST_F(RndCamProjectionTest, TranslatedCameraKeepsForwardPointCentered) {
    const float yFov = 0.6024178f;
    RndCam *cam = MakeTestCam("test_cam_translated", 1.0f, 1000.0f, yFov);
    cam->SetLocalPos(Vector3(0.0f, -5.0f, 0.0f));
    cam->UpdatedWorldXfm();

    Vector2 screen;
    float depth = cam->WorldToScreen(Vector3(0.0f, 5.0f, 0.0f), screen);
    EXPECT_NEAR(depth, 10.0f, kEps);
    EXPECT_NEAR(screen.x, 0.5f, kEps);
    EXPECT_NEAR(screen.y, 0.5f, kEps);
}

TEST_F(RndCamProjectionTest, ScreenRectScalesProjectionIntoSubview) {
    const float yFov = 0.6024178f;
    const float depth = 10.0f;
    RndCam *cam = MakeTestCam("test_cam_subrect", 1.0f, 1000.0f, yFov);
    cam->SetScreenRect(Hmx::Rect(0.25f, 0.25f, 0.5f, 0.5f));
    cam->UpdatedWorldXfm();

    const float tanHalf = std::tanf(yFov * 0.5f);
    const float ratio = TheRnd.YRatio();

    Vector2 screen;
    cam->WorldToScreen(Vector3(0.0f, depth, 0.0f), screen);
    EXPECT_NEAR(screen.x, 0.5f, kEps);
    EXPECT_NEAR(screen.y, 0.5f, kEps);

    cam->WorldToScreen(Vector3(depth * tanHalf / ratio, depth, 0.0f), screen);
    EXPECT_NEAR(screen.x, 0.75f, 2.0e-3f);
    EXPECT_NEAR(screen.y, 0.5f, 2.0e-3f);
}

TEST_F(RndCamProjectionTest, ChooseModeApproximateLayoutFitsViewportWithoutDebugUiCamHack) {
    RndCam *cam = MakeTestCam("test_cam_choose_mode", 1.0f, 1000.0f, 0.6024178f);
    cam->SetLocalPos(Vector3(0.0f, -768.0f, 0.0f));
    cam->UpdatedWorldXfm();

    const Vector3 chooseModeLabelPositions[] = {
        Vector3(-107.6f, -8.0f, -62.5f),
        Vector3(-107.6f, -8.0f, -87.5f),
        Vector3(-107.6f, -8.0f, -112.5f),
        Vector3(-107.6f, -8.0f, -137.5f),
        Vector3(-107.6f, -8.0f, -162.5f),
    };

    for (size_t i = 0; i < std::size(chooseModeLabelPositions); ++i) {
        Vector2 screen;
        float depth = cam->WorldToScreen(chooseModeLabelPositions[i], screen);
        EXPECT_GT(depth, 0.0f);
        EXPECT_GE(screen.x, 0.0f);
        EXPECT_LE(screen.x, 1.0f);
        EXPECT_GE(screen.y, 0.0f);
        EXPECT_LE(screen.y, 1.0f);
    }
}

TEST_F(RndCamProjectionTest, ChooseModeDebugUiCamHackPushesApproximateLayoutOffscreen) {
    RndCam *cam = MakeTestCam("test_cam_choose_mode_debug_hack", 1.0f, 1000.0f, 0.6024178f);
    cam->SetLocalPos(Vector3(0.0f, -768.0f, 370.0f));
    cam->UpdatedWorldXfm();

    Vector2 screen;
    float depth = cam->WorldToScreen(Vector3(-107.6f, -8.0f, -62.5f), screen);
    EXPECT_GT(depth, 0.0f);
    // The label sits far BELOW the raised camera, so it is pushed off the
    // bottom (screen y grows down, as in the image). Used to expect y < 0,
    // the mirrored value of the old native-only WorldToScreen.
    EXPECT_GT(screen.y, 1.0f);
}

TEST_F(RndCamProjectionTest, OrthographicProjectionMatchesExpectedMatrix) {
    const float nearPlane = 2.0f;
    const float farPlane = 202.0f;
    RndCam *cam = MakeTestCam("test_cam_ortho", nearPlane, farPlane, 0.0f);

    Transform viewXfm;
    Hmx::Matrix4 projMtx;
    cam->GetViewProjectXfms(viewXfm, projMtx);

    const float ratio = TheRnd.YRatio();
    const float farRatio = 1.0f / (farPlane - nearPlane);

    EXPECT_NEAR(projMtx.x.x, 1.0f, kEps);
    EXPECT_NEAR(projMtx.y.y, 1.0f / ratio, kEps);
    EXPECT_NEAR(projMtx.z.x, 0.0f, kEps);
    EXPECT_NEAR(projMtx.z.y, 0.0f, kEps);
    EXPECT_NEAR(projMtx.z.z, farRatio, kEps);
    EXPECT_NEAR(projMtx.z.w, 0.0f, kEps);
    EXPECT_NEAR(projMtx.w.z, -(nearPlane * farRatio), kEps);
    EXPECT_NEAR(projMtx.w.w, 1.0f, kEps);
}

namespace {
class RibbonDrawRecorder : public RndDrawable {
public:
    explicit RibbonDrawRecorder(HamListRibbon *owner) : mOwner(owner) {}

    std::vector<float> mDrawZ;

    void DrawShowing() override {
        mDrawZ.push_back(mOwner->WorldXfm().v.z);
    }

private:
    HamListRibbon *mOwner;
};

class TestHamListRibbon : public HamListRibbon {
public:
    void AddTestDrawable(RndDrawable *d) {
        mDraws.push_back(d);
    }
};

class HiddenTemplateMesh : public RndMesh {
public:
    HiddenTemplateMesh() : mDrawShowingCalls(0), mShowingInsideDraw(false) {}

    void DrawShowing() override {
        mDrawShowingCalls++;
        mShowingInsideDraw = Showing();
    }

    int mDrawShowingCalls;
    bool mShowingInsideDraw;
};

class TestUIListMesh : public UIListMesh {
public:
    void SetMeshForTest(RndMesh *mesh) {
        mMesh = mesh;
    }
};
}

TEST_F(RndCamProjectionTest, HamListRibbonUsesUniformSpacingForFiveVisibleItems) {
    TestHamListRibbon ribbon;
    RibbonDrawRecorder recorder(&ribbon);
    ribbon.AddTestDrawable(&recorder);

    std::vector<HamListRibbonDrawState> states(HamListRibbon::sNumListSelectable);
    for (std::vector<HamListRibbonDrawState>::iterator it = states.begin(); it != states.end(); ++it) {
        it->mActive = false;
        it->mHidden = false;
        it->mSelected = false;
    }

    Transform xfm;
    xfm.Reset();
    ribbon.Draw(xfm, states, false, false);

    ASSERT_EQ(recorder.mDrawZ.size(), (size_t)HamListRibbon::sNumListSelectable);
    for (size_t i = 1; i < recorder.mDrawZ.size(); ++i) {
        EXPECT_NEAR(recorder.mDrawZ[i - 1] - recorder.mDrawZ[i], 25.0f, kEps);
    }
}

// UIListMeshElement::Draw (827C2888, 100% matched, 106/106 instructions) hands
// the list's template mesh to DrawShowing() exactly as the file ships it --
// hidden -- and never touches its Showing flag. This test used to pin a native
// SetShowing(true)/restore around the call, which existed only because the
// native RndMesh::DrawShowing refused hidden named meshes; the image's
// DxMesh::DrawShowing (826229B0) has no such test, and neither does the native
// one now (see NativeSuspectsTest.MeshDrawShowingDrawsAHiddenNamedMesh).
TEST_F(RndCamProjectionTest, UIListMeshDrawDrawsHiddenTemplateMeshAsShipped) {
    TestUIListMesh listMesh;
    HiddenTemplateMesh mesh;
    RndMat *mat = Hmx::Object::New<RndMat>();

    listMesh.SetMeshForTest(&mesh);

    UIListMeshElement element(&listMesh);
    element.mMat = mat;

    mesh.SetShowing(false);

    Transform tf;
    tf.Reset();
    element.Draw(tf, 1.0f, nullptr, nullptr);

    EXPECT_EQ(mesh.mDrawShowingCalls, 1);
    EXPECT_FALSE(mesh.mShowingInsideDraw)
        << "the image draws the template with its Showing flag as shipped (false)";
    EXPECT_FALSE(mesh.Showing());
}

TEST_F(RndCamProjectionTest, TextAlphaFallbackOnlyAppliesToTransparentTextMeshes) {
    EXPECT_TRUE(NativeShouldForceTextAlpha(true, BaseMaterial::kBlendSrcAlpha, 0.0f));
    EXPECT_TRUE(NativeShouldForceTextAlpha(true, BaseMaterial::kBlendSrcAlpha, 0.009f));
    EXPECT_FALSE(NativeShouldForceTextAlpha(false, BaseMaterial::kBlendSrcAlpha, 0.0f));
    EXPECT_FALSE(NativeShouldForceTextAlpha(true, BaseMaterial::kBlendAdd, 0.0f));
    EXPECT_FALSE(NativeShouldForceTextAlpha(true, BaseMaterial::kBlendSrcAlpha, 0.5f));
}

// G-21: RndDrawable::mClipPlanes clip on native. The main menu's MOTD marquee
// (motd.lbl) carries two clip planes, motd_clip_left/right.trans; retail
// DxRnd::PushClipPlanesInternal makes each a D3D user clip plane (through the
// transform's origin, normal -z, keep the side the normal points at), so the
// ticker stops at its box. The native renderer ignored them, and the ticker ran
// to the screen edge. WgpuRnd now turns an edge-on plane into a scissor edge.
namespace {
RndTransformable *MakeClipPlane(const Vector3 &origin, const Vector3 &keepDir) {
    // DxRnd keeps the side -z points at, so z = -keepDir.
    const Vector3 z(-keepDir.x, -keepDir.y, -keepDir.z);
    // Any x, y completing a frame; the clip reads only v and z.
    Vector3 x = std::fabs(z.z) < 0.9f ? Vector3(0.0f, 0.0f, 1.0f) : Vector3(1.0f, 0.0f, 0.0f);
    Vector3 y(z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x);
    x.Set(y.y * z.z - y.z * z.y, y.z * z.x - y.x * z.z, y.x * z.y - y.y * z.x);
    RndTransformable *t = Hmx::Object::New<RndTransformable>();
    t->SetLocalRot(Hmx::Matrix3(x, y, z));
    t->SetLocalPos(origin);
    return t;
}
}

TEST_F(RndCamProjectionTest, ClipPlanesBecomeAScissorAroundTheDrawable) {
    ASSERT_NE(gWgpuRnd, nullptr);
    RndCam *saved = RndCam::Current();
    RndCam *cam = MakeTestCam("test_cam_clip", 1.0f, 1000.0f, 0.6024178f);
    cam->Select();
    ASSERT_EQ(RndCam::Current(), cam);

    // The camera looks down +y from the origin. A box 20 wide and 20 tall
    // 100 units ahead: keep x >= -10, x <= 10, z <= 10.
    RndTransformable *left = MakeClipPlane(Vector3(-10.0f, 100.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
    RndTransformable *right = MakeClipPlane(Vector3(10.0f, 100.0f, 0.0f), Vector3(-1.0f, 0.0f, 0.0f));
    RndTransformable *top = MakeClipPlane(Vector3(0.0f, 100.0f, 10.0f), Vector3(0.0f, 0.0f, -1.0f));
    ObjPtrVec<RndTransformable> planes(cam);
    planes.push_back(right);
    planes.push_back(left);

    int full[4] = {0, 0, 0, 0};
    TheRnd.PushClipPlanes(planes);
    int r[4] = {0, 0, 0, 0};
    ASSERT_TRUE(gWgpuRnd->ClipScissor(r))
        << "PushClipPlanes left no clip in effect: the planes were ignored";
    // The full-target rect, from a push of no usable planes on top of none.
    {
        ObjPtrVec<RndTransformable> none(cam);
        TheRnd.PopClipPlanes(planes);
        none.push_back(MakeClipPlane(Vector3(0.0f, 100.0f, 0.0f),
                                     Vector3(-0.70710678f, 0.0f, -0.70710678f)));
        TheRnd.PushClipPlanes(none); // oblique on screen: no scissor edge
        ASSERT_TRUE(gWgpuRnd->ClipScissor(full));
        TheRnd.PopClipPlanes(none);
        EXPECT_FALSE(gWgpuRnd->ClipScissor(r)) << "PopClipPlanes left a clip in effect";
        TheRnd.PushClipPlanes(planes);
        ASSERT_TRUE(gWgpuRnd->ClipScissor(r));
    }
    const int W = full[2], H = full[3];
    ASSERT_GT(W, 0);
    ASSERT_GT(H, 0);
    EXPECT_EQ(full[0], 0);
    EXPECT_EQ(full[1], 0);
    // x in [-10, 10] at depth 100 is a centred column, narrower than the target.
    EXPECT_GT(r[0], 0) << "left plane did not clip";
    EXPECT_LT(r[0] + r[2], W) << "right plane did not clip";
    EXPECT_LT(r[2], W / 2);
    EXPECT_NEAR(r[0] + r[0] + r[2], W, 2) << "the column is not centred on the view axis";
    EXPECT_EQ(r[1], 0);
    EXPECT_EQ(r[3], H);

    // Nested: the top plane narrows the outer rect, and popping restores it.
    ObjPtrVec<RndTransformable> inner(cam);
    inner.push_back(top);
    TheRnd.PushClipPlanes(inner);
    int n[4];
    ASSERT_TRUE(gWgpuRnd->ClipScissor(n));
    EXPECT_EQ(n[0], r[0]);
    EXPECT_EQ(n[2], r[2]);
    EXPECT_GT(n[1], 0) << "top plane (keep z <= 10) did not clip the top of the screen";
    EXPECT_LT(n[1], H / 2);
    EXPECT_EQ(n[1] + n[3], H);
    TheRnd.PopClipPlanes(inner);
    int back[4];
    ASSERT_TRUE(gWgpuRnd->ClipScissor(back));
    EXPECT_EQ(back[1], r[1]);
    EXPECT_EQ(back[3], r[3]);
    TheRnd.PopClipPlanes(planes);
    EXPECT_FALSE(gWgpuRnd->ClipScissor(back));
    EXPECT_EQ(gWgpuRnd->ClipPlaneDepth(), 0);

    if (saved) saved->Select();
    else RndCam::ClearCurrent();
}
