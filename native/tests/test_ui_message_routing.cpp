// UIManager must answer its own messages before it consults the current
// screen -- exactly as the Xbox image does.
//
// Until 0800c3ed2 (native-additions, 2026-09-30) native UIManager::Poll
// carried an HX_NATIVE-only `mSink = trans;` on every completed transition
// (added 2026-03 to "route button input").  The image never writes mSink there: in the target
// ?Poll@UIManager@@UAAXXZ the transition-complete block stores only
// mTransitionState (0x2c), mCurrentScreen (0x48) and mTransitionScreen (0x4c);
// the only writers of mSink (0x50) are the ctor and the `set_sink` handler,
// which no shipped DTA sends.  Because UIManager::Handle checks
// HANDLE_MEMBER_PTR(mSink) FIRST, the native store made every `{ui ...}`
// message visit the current screen (and its focus panel, and that panel's
// PanelDir) before UIManager's own handlers.
//
// That is harmless until something on that path answers the message by
// sending it back to `ui`.  The shell_with_narrator PanelDir type
// (ui/hud/hud_objects.dta) does exactly that:
//     (goto_screen ($screen_name) {if {! {exists milo}} {ui goto_screen $screen_name}})
// party_mode_welcome.milo is such a PanelDir, so the welcome panel's own
// `{ui goto_screen crew_throwdown_multiuser_screen}` looped
// ui -> screen -> focus panel -> PanelDir -> ui ... until the DTA call stack
// overflowed and the process died with SIGSEGV -- on choosing CREW THROWDOWN
// from the main menu (6/6 runs).  NativeAdditionsUITest.
// TransitionDoesNotMakeTheScreenTheSink pins the double delivery the store
// caused; this test pins the crash shape -- a screen that answers by echoing
// the message back to `ui` -- and that the trailing mCurrentScreen dispatch
// still reaches screens without the store.
#include "test_helpers.h"

#include "ui/UI.h"
#include "ui/UIScreen.h"

namespace {

// A screen that answers `in_transition` the way the shell_with_narrator
// PanelDir answers `goto_screen`: by handing it straight back to TheUI.
// Bounded so a regression fails the test instead of killing the process.
class EchoScreen : public UIScreen {
public:
    static const int kMaxEcho = 5;
    int mSeen = 0;
    virtual DataNode Handle(DataArray *msg, bool warn) {
        static Symbol in_transition("in_transition");
        if (msg->Size() > 1 && msg->Type(1) == kDataSymbol && msg->Sym(1) == in_transition) {
            if (++mSeen < kMaxEcho)
                return TheUI->Handle(msg, warn);
            return DataNode(-1);
        }
        return UIScreen::Handle(msg, warn);
    }
    // No panels, no typedef: nothing to enter.
    virtual void Enter(UIScreen *) {}
    virtual bool Entering() const { return false; }
    virtual void Poll() {}
};

class ProbeUI : public UIManager {
public:
    void BeginTransitionTo(UIScreen *to) {
        mCurrentScreen = nullptr;
        mTransitionScreen = to;
        mTransitionState = kTransitionTo;
    }
    void Reset() {
        mCurrentScreen = nullptr;
        mTransitionScreen = nullptr;
        mTransitionState = kTransitionNone;
    }
};

struct ScopedUI {
    UIManager *saved;
    ScopedUI(UIManager *ui) : saved(TheUI) { TheUI = ui; }
    ~ScopedUI() { TheUI = saved; }
};

} // namespace

class UIMessageRoutingTest : public EngineTestFixture {};

TEST_F(UIMessageRoutingTest, UIManagerHandlesItsOwnMessagesBeforeTheScreen) {
    ProbeUI ui;
    ScopedUI scoped(&ui);
    EchoScreen *screen = new EchoScreen();

    // Complete a transition exactly as the running game does.
    ui.BeginTransitionTo(screen);
    ui.Poll();
    ASSERT_EQ(ui.CurrentScreen(), screen)
        << "control: the transition did not complete, so the routing check "
           "below would not exercise the post-transition state";

    Message msg("in_transition");
    DataNode result = ui.Handle(msg, false);

    EXPECT_EQ(screen->mSeen, 0)
        << "`in_transition` reached the current screen before UIManager's own "
           "HANDLE_EXPR.  The image never points mSink at the current screen, "
           "so UIManager answers its own messages first; a screen that "
           "forwards the message back to `ui` (shell_with_narrator's "
           "goto_screen) then recursed until the DTA call stack overflowed.";
    EXPECT_EQ(result.Type(), kDataInt);
    EXPECT_EQ(result.Int(), ui.InTransition() ? 1 : 0)
        << "UIManager's own in_transition answer was replaced by the screen's";

    // Control: a message UIManager does NOT handle still reaches the current
    // screen through HANDLE_MEMBER_PTR(mCurrentScreen), the image's last
    // handler -- removing the native mSink store must not cut screens off.
    Message exiting("exiting");
    DataNode viaScreen = ui.Handle(exiting, false);
    EXPECT_NE(viaScreen.Type(), kDataUnhandled)
        << "`exiting` (a UIScreen handler UIManager lacks) no longer reaches "
           "the current screen";

    ui.Reset();
    delete screen;
}
