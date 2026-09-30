// Native game-FLOW regression tests (branch native-gameflow).
//
// Each test pins the image's behaviour at a place where an `#ifdef HX_NATIVE`
// addition used to short-circuit the game's own menu / movie flow, cites the
// decisive instructions of the target listing (build/373307D9/asm/**), and was
// watched FAILING against the pre-fix native body.  The gameplay-flow leads of
// the same branch (PoseFatalities::Poll, GamePanel::StartGame,
// MultiUserGesturePanel) need a whole song and are pinned by DtaFlowTest in
// test_dta_flow.cpp instead.

#include "test_helpers.h"

#include "meta/MoviePanel.h"
#include "movie/Movie.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "os/JoypadMsgs.h"
#include "platform/FFmpegMovieImpl.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"
#include "ui/UIScreen.h"

namespace {

int DtaIntVar(const char *name) {
    DataNode &n = DataVariable(name);
    return n.Type() == kDataInt ? n.Int() : 0;
}

// ---------------------------------------------------------------------------
// FFmpegMovieImpl::Poll on a movie that did not open.
//
// Image (?Poll@BinkMovieImpl@@UAA_NXZ): CheckOpen(true) is false once no open
// is pending, and the `mBink && mInternalBufs` test fails with no HBINK, so it
// falls through to `return false` -- a movie that failed to open is DONE.
// The native impl returned true ("still playing") for !mOpen, which is what
// MoviePanel's native IsOpen guard was compensating for.
// ---------------------------------------------------------------------------

class NativeGameflowMovieTest : public EngineTestFixture {};

TEST_F(NativeGameflowMovieTest, UnopenedMovieReportsDone) {
    FFmpegMovieImpl movie;
    ASSERT_FALSE(movie.BeginFromFile(
        "videos/ngf_does_not_exist.bik", 0.0f, false, false, false, false, 0, nullptr,
        kLoadFront
    )) << "precondition: the file does not exist";
    ASSERT_FALSE(movie.IsOpen());
    EXPECT_FALSE(movie.Poll())
        << "a movie that failed to open must poll as done (BinkMovieImpl::Poll "
           "returns false with no HBINK), not as still playing";
}

// ---------------------------------------------------------------------------
// MoviePanel::Poll when the movie did not open.
//
// Image (?Poll@MoviePanel@@UAAXXZ): UIPanel::Poll, the kUnloaded test, then
// straight into `bl Poll@Movie` and, on false with no UI transition,
// HandleType(movie_done).  There is no IsOpen() test anywhere in the body.
// The native `if (!mMovie.IsOpen()) return;` meant a screen whose .bik is
// missing (every one: the extracted tree ships no .bik) never got movie_done,
// so attract_screen never advanced on its own; a native-only skip_selected
// in UIScreen::OnMsg(ButtonDownMsg) was then needed to leave it.
// ---------------------------------------------------------------------------

TEST_F(NativeGameflowMovieTest, PanelFiresMovieDoneWhenTheVideoDidNotOpen) {
    Movie::Init();
    UIManager ui;
    UIManager *savedUI = TheUI;
    TheUI = &ui;
    // Settle the UI on an empty screen so InTransition() is a real false
    // (a bare UIManager's transition state is only set by Init/GotoScreen).
    UIScreen *scr = Hmx::Object::New<UIScreen>();
    ui.GotoScreen(scr, false, false);
    for (int i = 0; i < 16 && (ui.InTransition() || ui.CurrentScreen() != scr); i++)
        ui.Poll();

    DataVariable("ngf_movie_done") = 0;
    MoviePanel *panel = new MoviePanel();
    panel->SetName("ngf_movie_panel", ObjectDir::Main());
    DataArray *def = DataReadString(
        "(preload FALSE) (loop FALSE) (audio TRUE) (videos attract)"
        " (movie_done {set $ngf_movie_done {+ $ngf_movie_done 1}})"
    );
    panel->SetTypeDef(def);
    def->Release();
    panel->CheckLoad();     // MoviePanel::Load: SystemConfig videos/attract, PlayMovie
    panel->CheckIsLoaded(); // FinishLoad -> kDown
    ASSERT_NE(panel->GetState(), UIPanel::kUnloaded) << "precondition: loaded";
    ASSERT_FALSE(ui.InTransition()) << "precondition: no UI transition";

    panel->Poll();
    EXPECT_EQ(DtaIntVar("ngf_movie_done"), 1)
        << "MoviePanel::Poll did not fire movie_done for a video that did not "
           "open; the image has no IsOpen() test in Poll";

    panel->CheckUnload();
    delete panel;
    TheUI = savedUI;
    delete scr;
}

// ---------------------------------------------------------------------------
// UIScreen::OnMsg(ButtonDownMsg).
//
// Image (?OnMsg@UIScreen@@IAA?AVDataNode@@ABVButtonDownMsg@@@Z, 827A3AF0):
// `lwz r11, 0x38(r4)` (mBack) / beq to the unhandled return; with mBack set,
// Int(4) == 2 (kAction_Cancel) -> go_back_screen.  Nothing else.  The only
// thing that sends a screen `skip_selected` is movie_overlay_panel's DTA
// `skip`, from its own nav list's NAV_SELECT_MSG.  The native addition fired
// skip_selected on ANY button reaching a screen whose typedef defines it --
// Cancel included, and presses a nav list drops during its enter animation.
// ---------------------------------------------------------------------------

class NativeGameflowUITest : public EngineTestFixture {};

TEST_F(NativeGameflowUITest, ButtonOnAMovieScreenIsNotSkipSelected) {
    DataVariable("ngf_skip") = 0;
    UIScreen *scr = Hmx::Object::New<UIScreen>();
    DataArray *def =
        DataReadString("(panels) (skip_selected {set $ngf_skip {+ $ngf_skip 1}})");
    scr->SetTypeDef(def);
    def->Release();

    ButtonDownMsg cancel(nullptr, kPad_Circle, kAction_Cancel, 0);
    ButtonDownMsg confirm(nullptr, kPad_X, kAction_Confirm, 0);
    scr->Handle(cancel, false);
    scr->Handle(confirm, false);
    EXPECT_EQ(DtaIntVar("ngf_skip"), 0)
        << "a button reaching the screen fired skip_selected "
        << DtaIntVar("ngf_skip")
        << " times; the image's UIScreen::OnMsg(ButtonDownMsg) only turns "
           "Cancel into go_back_screen, and only when the screen has a `back`";

    // Control: the handler is live -- sending the message directly runs it.
    int before = DtaIntVar("ngf_skip");
    static Message skip("skip_selected");
    scr->HandleType(skip);
    EXPECT_EQ(DtaIntVar("ngf_skip"), before + 1)
        << "harness broken: skip_selected handler not live";
    delete scr;
}

} // namespace
