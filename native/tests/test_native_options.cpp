// Options menus on native (PLAN G-12, G-16).
//
// G-12: the help bar's button glyphs and the checkbox glyphs are text in
// colour font atlases (icons_buttons.mat, icons.mat).  The engine's DC3-shape
// mesh path routes a text mesh's atlas alpha into RGB unless the game's render
// hook marks it a colour font, which dropped every glyph's colour: blank white
// discs, and one white square for both checkbox states.  The engine change
// that consults the hook is ~/tmp/np-options-engine.patch; this pins the DC3
// half, the hook's answer.
//
// G-16: Options > A/V > Kinect Tuner asks platform_mgr for the dashboard's
// controller-required UI and opens the tuner from the
// controller_req_op_complete it gets back.  The image answers at once (TRUE)
// when no skeleton is tracked; native answered nothing, so the selection did
// nothing.  The tuner itself is the 360 dashboard's, so native says so.
//
// The flow test that drives the real screens is
// OptionsGameplayFlowTest in test_dta_flow.cpp.

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/PlatformMgr.h"
#include "platform/GameRenderHook.h"
#include "platform/NativeNotice.h"
#include "rndobj/Mesh.h"
#include "xdk/nui/nuidiagnostics.h"

#include <string>

namespace {

class NativeOptionsTest : public EngineTestFixture {};

// Records the messages it is sent.
class MessageProbe : public Hmx::Object {
public:
    DataNode Handle(DataArray *msg, bool) override {
        mLast = msg->Sym(1);
        mArg = msg->Size() > 2 ? msg->Int(2) : -1;
        mCount++;
        return DataNode(0);
    }
    Symbol mLast;
    int mArg = -1;
    int mCount = 0;
};

} // namespace

// An unnamed mesh is RndText's per-font glyph mesh: DC3 samples the atlas's
// RGB, so the hook must call it a colour font.  A named mesh is anything else
// and keeps the engine's default.
TEST_F(NativeOptionsTest, TextGlyphMeshesAreColourFonts) {
    GameRenderHook *hook = GetGameRenderHook();
    ASSERT_NE(hook, nullptr) << "dc3_render_hook.cpp did not register HamRenderHook";

    RndMesh *glyphs = Hmx::Object::New<RndMesh>();
    ASSERT_EQ(glyphs->Name()[0], '\0') << "precondition: a new mesh has no name";
    EXPECT_TRUE(hook->QueryDrawMaterialPolicy(glyphs, nullptr, false, nullptr, nullptr)
                    .isColorIcon)
        << "a text glyph mesh must keep its atlas RGB: DC3's button and checkbox "
           "glyphs carry their colour there";

    RndMesh *named = Hmx::Object::New<RndMesh>();
    named->SetName("icon_2p.mesh", ObjectDir::Main());
    EXPECT_FALSE(hook->QueryDrawMaterialPolicy(named, nullptr, false, nullptr, nullptr)
                     .isColorIcon)
        << "only text meshes are colour fonts";

    delete named;
    delete glyphs;
}

// The image's no-tracked-skeleton arm: controller_req_op_complete TRUE, at
// once, to the object that asked.
TEST_F(NativeOptionsTest, ControllerRequiredAnswersAtOnce) {
    MessageProbe probe;
    ThePlatformMgr.ShowControllerRequiredUI(&probe);
    ASSERT_EQ(probe.mCount, 1)
        << "show_controller_required sent nothing back: options_av's Kinect "
           "Tuner row opens the tuner from controller_req_op_complete";
    EXPECT_EQ(probe.mLast, Symbol("controller_req_op_complete"));
    EXPECT_EQ(probe.mArg, 1) << "the immediate answer is success (TRUE)";

    ThePlatformMgr.ShowControllerRequiredUI(nullptr); // no caller: nothing to answer
}

TEST_F(NativeOptionsTest, KinectTunerSaysItIsTheDashboards) {
    NativeNotice::Clear("kinect_tuner");
    EXPECT_EQ(XShowNuiTroubleshooterUI(), 0u);
    const std::string text = NativeNotice::Text("kinect_tuner");
    EXPECT_NE(text.find("Kinect Tuner"), std::string::npos)
        << "XShowNuiTroubleshooterUI posted no notice; the player got no "
           "feedback.  notice: '" << text << "'";
    NativeNotice::Clear("kinect_tuner");
}
