// HamNavList must NOT stop its own animation when a UI transition completes.
//
// Regression test for "menu list items never draw on native" (main,
// choose_mode, results, complete ... every HamNavList screen), 2026-09-30.
//
// The chain, measured on main_screen's right_hand.hnl (ui/main/main.milo):
//   * HamNavList::Enter() sets mPendingEnterAnim; the next Poll() calls
//     PlayEnterAnim(), which puts the ribbon (main_ribbon) into TestEntering
//     and Animate()s the nav list over the ribbon's enter.anim range (0..20).
//     HamListRibbon::Draw drives enter.anim from the nav list's frame, and
//     enter.anim fades the ribbon's label placeholder alpha 0 -> 1.
//   * HamListRibbon::DrawRibbon copies GetLabelTotalAlpha() -- the
//     placeholder's alpha -- into every element's UIListElementDrawState::
//     mAlpha (image: `stfs f1, 0x24(r11)` @824816A4), and UIListSlot::Draw
//     draws each item's label/mesh with that alpha.
//   * native carried an HX_NATIVE-only handler, HamNavList::OnMsg(
//     UITransitionCompleteMsg), that called StopAnimation().  The image's
//     HamNavList has no such handler (HamNavList.s never references
//     transition_complete).  On native the screen's transition completes on
//     the same frame the list enters, so the enter animation was killed before
//     it advanced one frame; Poll() then saw !IsAnimating(), cleared
//     TestEntering and left enter.anim at frame 0 -> placeholder alpha 0 ->
//     every item drawn at alpha 0.000, forever.
//
// This test pins the one invariant that broke: delivering transition_complete
// to a HamNavList leaves a running animation running.
#include "test_helpers.h"

#include "hamobj/HamNavList.h"
#include "obj/Task.h"
#include "ui/UIScreen.h"

class HamNavListTransitionTest : public EngineTestFixture {};

TEST_F(HamNavListTransitionTest, TransitionCompleteDoesNotStopAnimation) {
    HamNavList *list = Hmx::Object::New<HamNavList>();
    ASSERT_NE(list, nullptr);

    // An explicit 0..20 range (enter.anim's range on main_ribbon): the list
    // has no ribbon resource here, so the StartFrame()/EndFrame() overload
    // would be an empty 0..0 animation.
    list->RndAnimatable::Animate(
        0.0f, 20.0f, kTaskUISeconds, 1.0f, 0.0f, nullptr, kEaseLinear, 0.0f, false
    );
    ASSERT_TRUE(list->IsAnimating())
        << "control: Animate() did not start an AnimTask, so this test "
           "would pass vacuously";

    UITransitionCompleteMsg msg(nullptr, nullptr);
    list->Handle(msg, false);

    EXPECT_TRUE(list->IsAnimating())
        << "transition_complete stopped the HamNavList's animation. The "
           "image's HamNavList does not handle transition_complete; stopping "
           "the enter animation here leaves the ribbon's enter.anim at frame "
           "0, whose label alpha (0) is copied into every list element -- "
           "the list items then draw fully transparent.";

    list->StopAnimation();
    EXPECT_FALSE(list->IsAnimating());
    delete list;
}
