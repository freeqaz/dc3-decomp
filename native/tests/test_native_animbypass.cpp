// Native menu/flow code must wait for list/panel animations exactly where the
// Xbox image does.
//
// Several HX_NATIVE-only workarounds were added in 2026-03 on the belief that
// "list/panel animations never settle on native".  That belief was produced by
// a native-only HamNavList::OnMsg(UITransitionCompleteMsg) -> StopAnimation()
// handler (removed 2026-09-30, see test_hamnavlist_transition.cpp); with it
// gone, a nav list's enter animation settles in ~20 UI frames.  Each test here
// pins one image behaviour that a workaround had replaced:
//
//   * HamPanel::Exiting (image 828EFA30): UIPanel::Exiting() ||
//     (ShouldUseLocalNavlist() && mNavList && mNavList->IsAnimating()).
//     Native returned false outright.
//   * HamNavList::OnMsg(ButtonDownMsg) (image 82449FC0, `bl IsAnimating` at
//     8244A034): a button is ignored while the list's own animation runs.
//     Native skipped the IsAnimating() term.
//   * UIManager::Poll kTransitionFrom: the image waits on
//     !mCurrentScreen->Entering() with no timeout.  Native force-completed the
//     enter after 90 frames.
#include "test_helpers.h"

#include "gesture/GestureMgr.h"
#include "hamobj/HamNavList.h"
#include "meta_ham/HamPanel.h"
#include "obj/Task.h"
#include "os/JoypadMsgs.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"
#include "ui/UIScreen.h"

namespace {

// A screen whose Entering() the test controls, and whose focus panel it sets.
class ProbeScreen : public UIScreen {
public:
    bool mEnteringFlag = false;
    virtual bool Entering() const { return mEnteringFlag; }
    virtual void Poll() {}
    void ForceFocusPanel(UIPanel *panel) { mFocusPanel = panel; }
};

// A panel whose focus component the test controls.
class ProbePanel : public UIPanel {
public:
    UIComponent *mFocus = nullptr;
    virtual UIComponent *FocusComponent() { return mFocus; }
};

// A UIManager whose transition state the test controls.  The ctor is trivial
// (no Init()), which is all UIManager::Poll's kTransitionFrom branch needs.
class ProbeUI : public UIManager {
public:
    void Force(UIScreen *cur, TransitionState state) {
        mCurrentScreen = cur;
        mTransitionScreen = nullptr;
        mTransitionState = state;
    }
};

// Swap the global UIManager for the duration of a test; make sure a
// GestureMgr exists (HamNavList reads controller mode from it).
struct ScopedGlobals {
    UIManager *savedUI;
    ScopedGlobals(UIManager *ui) : savedUI(TheUI) {
        TheUI = ui;
        if (!TheGestureMgr)
            GestureMgr::Init();
    }
    ~ScopedGlobals() { TheUI = savedUI; }
};

void AnimateEnterRange(HamNavList *list) {
    // enter.anim's range on main_ribbon; the bare list has no ribbon resource,
    // so the StartFrame()/EndFrame() overload would be an empty animation.
    list->RndAnimatable::Animate(
        0.0f, 20.0f, kTaskUISeconds, 1.0f, 0.0f, nullptr, kEaseLinear, 0.0f, false
    );
}

} // namespace

class NativeAnimBypassTest : public EngineTestFixture {};

// HamPanel::Exiting must report "still exiting" while its nav list animates.
TEST_F(NativeAnimBypassTest, HamPanelExitingWaitsForNavListAnimation) {
    // HamPanel's factory is registered by MetaPanel::Init, which the test
    // engine does not run; the ctor is all Exiting() needs.
    HamPanel *panel = new HamPanel();
    HamNavList *list = Hmx::Object::New<HamNavList>();
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(list, nullptr);
    panel->SetNavList(list);

    EXPECT_FALSE(panel->Exiting())
        << "control: an idle nav list and no DTA `exiting` handler must not "
           "block, or the animating case below proves nothing";

    AnimateEnterRange(list);
    ASSERT_TRUE(list->IsAnimating()) << "control: Animate() started no AnimTask";
    EXPECT_TRUE(panel->Exiting())
        << "HamPanel::Exiting ignored its animating nav list.  The image "
           "(828EFA30) returns true while mNavList->IsAnimating(), so a screen "
           "does not leave until its list animation has finished.";

    list->StopAnimation();
    EXPECT_FALSE(panel->Exiting());

    panel->SetNavList(nullptr);
    delete list;
    delete panel;
}

// A button that reaches a focused HamNavList while it animates is ignored.
TEST_F(NativeAnimBypassTest, HamNavListIgnoresButtonsWhileAnimating) {
    ProbeUI ui;
    ScopedGlobals globals(&ui);
    ASSERT_TRUE(TheGestureMgr->InControllerMode())
        << "native pins controller mode; without it ButtonDownMsg is ignored "
           "for a different reason and this test would pass vacuously";

    HamNavList *list = Hmx::Object::New<HamNavList>();
    ProbePanel *panel = new ProbePanel();
    ProbeScreen *screen = new ProbeScreen();
    panel->mFocus = list;
    screen->ForceFocusPanel(panel);
    ui.Force(screen, UIManager::kTransitionNone);
    ASSERT_EQ(TheUI->FocusComponent(), list);

    // `up` from item 0 walks off the top: the handler returns DataNode(0)
    // (handled) without touching the list's resources.
    ButtonDownMsg up(nullptr, kPad_DUp, kAction_Up, 0);

    DataNode idle = list->Handle(up, false);
    EXPECT_NE(idle.Type(), kDataUnhandled)
        << "control: an idle, focused list must handle `up`, or the animating "
           "case below proves nothing";

    AnimateEnterRange(list);
    ASSERT_TRUE(list->IsAnimating());
    DataNode busy = list->Handle(up, false);
    EXPECT_EQ(busy.Type(), kDataUnhandled)
        << "HamNavList handled a button while its animation was running.  The "
           "image's OnMsg(ButtonDownMsg) (82449FC0) requires !IsAnimating(); a "
           "press during the ~20-frame enter animation is dropped on the Xbox.";

    list->StopAnimation();
    screen->ForceFocusPanel(nullptr);
    ui.Force(nullptr, UIManager::kTransitionNone);
    delete screen;
    delete panel;
    delete list;
}

// A screen that is still entering keeps the UI in transition -- no timeout.
TEST_F(NativeAnimBypassTest, UIManagerWaitsForScreenEnterWithoutTimeout) {
    ProbeUI ui;
    ScopedGlobals globals(&ui);
    ProbeScreen *screen = new ProbeScreen();
    screen->mEnteringFlag = true;
    ui.Force(screen, UIManager::kTransitionFrom);

    // Well past the removed native 90-frame force-complete.
    for (int i = 0; i < 200; i++)
        ui.Poll();
    EXPECT_TRUE(ui.InTransition())
        << "UIManager completed a transition while the screen was still "
           "entering.  The image's UIManager::Poll waits on "
           "!mCurrentScreen->Entering() with no frame limit.";

    screen->mEnteringFlag = false;
    ui.Poll();
    EXPECT_FALSE(ui.InTransition())
        << "control: once the screen stops entering the transition completes";

    ui.Force(nullptr, UIManager::kTransitionNone);
    delete screen;
}
