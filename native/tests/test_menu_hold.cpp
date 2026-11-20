// Menu gestures on a webcam (np-menu-gestures): hold to select's timing rule
// and its switch, with no game, GPU or socket.
//
//   MenuHoldTimerTest     Dc3MenuGesture::HoldTimer, the timing rule of hold to
//                         select (DC3_MENU_SELECT=hold|both): a hand on a row
//                         selects it after settle + hold; a row change, a
//                         dropped arm or a scrolling list starts it again; a
//                         select (of any list) and a list that appears under a
//                         hand already held out leave it disarmed until the
//                         hand moves, drops or the re-arm time passes; under
//                         `both` a swipe makes it wait.
//                         Sabotaged, one defect at a time in
//                         HoldTimer::Tick (each one failed the test named and,
//                         but for the last, no other):
//                           - no Disarm after its own fire     -> NoSecondSelect...
//                           - no Disarm on a fresh list        -> AListThatAppears...
//                           - a row change keeps mHeld         -> ARowChange...
//                           - `blocked` ignored                -> AScrollingList...
//                           - a swipe does not pause           -> ASwipePauses...
//                           - `lastSelect` ignored             -> ASelectOfAnotherList...
//                           - a dropped arm keeps mHeld        -> DroppingTheArmStarts...
//                           - a moved hand does not re-arm     -> MovingTheHandReArms...
//                           - a dropped arm does not re-arm    -> DroppingTheArmReArms
//                           - fires after `hold`, no `settle`  -> AHandOnARowSelects...
//                             and six more
//   MenuSelectSwitchTest  the DC3_MENU_SELECT names and the runtime switch.
//
// The lists themselves (the slide as progress, the select, no select under
// swipe) are MenuSelectFlow in test_native_gesture_menus.cpp.

#include <gtest/gtest.h>

#include "platform/MenuGesture_Native.h"
#include "platform/NativeSettings.h"

#include <cmath>
#include <functional>
#include <vector>

using Dc3MenuGesture::HoldParams;
using Dc3MenuGesture::HoldTimer;

namespace {

const float kTick = 1.0f / 30.0f;

// One hand in front of one list, ticked at 30 Hz.
struct Rig {
    HoldTimer timer;
    HoldParams p; // the defaults: settle 0.3, hold 1.2, rearm 2.5
    float now = 100.0f;
    HoldTimer::Input in;
    std::vector<float> fires; // times since the rig's start
    float t0 = 100.0f;
    float lastProgress = 0.0f;
    bool sawProgress = false;
    // The list's select reaches every timer as `lastSelect`
    // (HamNavList::SetSelecting -> NoteSelect).  Off: the timer's own rule alone.
    bool feedSelect = true;

    Rig() {
        in.row = 2;
        in.handHeight = 0.5f;
        in.engaged = false;
    }
    HoldTimer::Result Step() {
        now += kTick;
        in.now = now;
        HoldTimer::Result r = timer.Tick(in, p);
        if (r == HoldTimer::kFire) {
            fires.push_back(now - t0);
            if (feedSelect)
                in.lastSelect = now;
        }
        if (r == HoldTimer::kProgress)
            sawProgress = true;
        lastProgress = timer.Progress();
        return r;
    }
    void Run(float seconds) {
        int n = (int)std::lround(seconds / kTick);
        for (int i = 0; i < n; i++)
            Step();
    }
    float Since() const { return now - t0; }
    // Arm down for a moment (so the timer is armed), then the hand on the row.
    void RaiseArm() {
        in.engaged = false;
        Run(0.5f);
        in.engaged = true;
        t0 = now;
    }
};

} // namespace

TEST(MenuHoldTimerTest, AHandOnARowSelectsAfterSettlePlusHold) {
    Rig r;
    r.RaiseArm();
    // nothing shows while the highlight settles
    r.Run(0.25f);
    EXPECT_FALSE(r.sawProgress);
    EXPECT_FLOAT_EQ(r.timer.Progress(), 0.0f);
    // then the progress rises, monotonically, to the select
    float last = 0.0f;
    while (r.fires.empty() && r.Since() < 5.0f) {
        HoldTimer::Result res = r.Step();
        if (res == HoldTimer::kProgress) {
            EXPECT_GE(r.timer.Progress(), last);
            EXPECT_LE(r.timer.Progress(), 1.0f);
            last = r.timer.Progress();
        }
    }
    ASSERT_EQ(r.fires.size(), 1u);
    EXPECT_NEAR(r.fires[0], r.p.settle + r.p.hold, 0.07f) << "settle 0.3 + hold 1.2";
    EXPECT_GT(last, 0.9f) << "the progress had all but filled when it fired";
}

TEST(MenuHoldTimerTest, ARowChangeStartsTheHoldAgain) {
    Rig r;
    r.RaiseArm();
    r.Run(1.0f);
    EXPECT_TRUE(r.sawProgress);
    r.in.row = 3; // the hand moved to the next row: 1.0 s in, 0.5 s short
    r.in.handHeight = 0.55f;
    r.Step();
    EXPECT_FLOAT_EQ(r.timer.Progress(), 0.0f) << "the progress is gone at once";
    r.Run(1.3f);
    EXPECT_TRUE(r.fires.empty()) << "2.3 s on the list, but only 1.3 s on this row";
    r.Run(0.4f);
    ASSERT_EQ(r.fires.size(), 1u);
    EXPECT_NEAR(r.fires[0], 1.0f + r.p.settle + r.p.hold, 0.1f);
}

TEST(MenuHoldTimerTest, DroppingTheArmStartsTheHoldAgain) {
    Rig r;
    r.RaiseArm();
    r.Run(1.2f);
    r.in.engaged = false; // off the list for a moment
    r.Run(0.1f);
    EXPECT_FLOAT_EQ(r.timer.Progress(), 0.0f);
    r.in.engaged = true;
    r.Run(1.3f);
    EXPECT_TRUE(r.fires.empty()) << "the 1.2 s before the drop do not count";
    r.Run(0.4f);
    EXPECT_EQ(r.fires.size(), 1u);
}

TEST(MenuHoldTimerTest, NoSecondSelectFromAHandThatHasNotMoved) {
    Rig r;
    r.feedSelect = false; // the timer disarms itself when it fires
    r.RaiseArm();
    while (r.fires.empty() && r.Since() < 5.0f)
        r.Step();
    ASSERT_EQ(r.fires.size(), 1u);
    float first = r.fires[0];
    // The hand stays exactly where it is (the next screen's list is under it).
    r.Run(r.p.rearm - 0.2f);
    EXPECT_EQ(r.fires.size(), 1u) << "disarmed for the re-arm time";
    EXPECT_FALSE(r.timer.Armed());
    r.Run(0.2f + r.p.settle + r.p.hold + 0.2f);
    ASSERT_EQ(r.fires.size(), 2u) << "after the re-arm time a full hold selects again";
    EXPECT_GE(r.fires[1] - first, r.p.rearm + r.p.settle + r.p.hold - 0.1f);
}

TEST(MenuHoldTimerTest, ASelectOfAnotherListDisarmsThisOne) {
    Rig r;
    r.RaiseArm();
    r.Run(1.0f);
    r.in.lastSelect = r.now; // the other hand's list (or a swipe) selected
    r.Run(1.5f);
    EXPECT_TRUE(r.fires.empty()) << "2.5 s on the row, but a select came in between";
    EXPECT_FALSE(r.timer.Armed());
}

TEST(MenuHoldTimerTest, MovingTheHandReArmsAtOnce) {
    Rig r;
    r.RaiseArm();
    r.Run(2.0f);
    ASSERT_EQ(r.fires.size(), 1u);
    r.Run(0.3f);
    r.in.handHeight += r.p.rearmMove + 0.02f; // a deliberate move to another row
    r.in.row = 3;
    r.Step();
    EXPECT_TRUE(r.timer.Armed());
    float moved = r.Since();
    r.Run(r.p.settle + r.p.hold + 0.2f);
    ASSERT_EQ(r.fires.size(), 2u);
    EXPECT_NEAR(r.fires[1] - moved, r.p.settle + r.p.hold, 0.12f);
    EXPECT_LT(r.fires[1] - r.fires[0], r.p.rearm) << "sooner than the re-arm time alone";
}

TEST(MenuHoldTimerTest, DroppingTheArmReArms) {
    Rig r;
    r.RaiseArm();
    r.Run(2.0f);
    ASSERT_EQ(r.fires.size(), 1u);
    r.in.engaged = false;
    r.Run(r.p.rearmDown + 0.1f);
    r.in.engaged = true;
    r.Step();
    EXPECT_TRUE(r.timer.Armed());
    r.Run(r.p.settle + r.p.hold + 0.2f);
    EXPECT_EQ(r.fires.size(), 2u);
}

TEST(MenuHoldTimerTest, AListThatAppearsUnderAHeldHandIsDisarmed) {
    // The first tick of a list already has the hand on it (a new screen came
    // up under an arm still held out from the last select).
    Rig r;
    r.in.engaged = true;
    r.t0 = r.now;
    r.Run(r.p.rearm - 0.2f);
    EXPECT_TRUE(r.fires.empty());
    EXPECT_FALSE(r.sawProgress) << "no progress shows either";
    r.Run(0.2f + r.p.settle + r.p.hold + 0.2f);
    EXPECT_EQ(r.fires.size(), 1u);

    // ... and so is one whose ticks stopped for a while (a transition).
    Rig g;
    g.RaiseArm();
    g.Run(1.0f);
    g.now += 1.0f; // no ticks for a second
    g.Run(1.0f);
    EXPECT_TRUE(g.fires.empty()) << "1 s before the gap + 1 s after is not a 1.5 s hold";
    EXPECT_FALSE(g.timer.Armed());
}

TEST(MenuHoldTimerTest, AScrollingListDoesNotHold) {
    Rig r;
    r.RaiseArm();
    r.in.blocked = true; // the hand is in the scroll zone / the list is moving
    r.Run(3.0f);
    EXPECT_TRUE(r.fires.empty());
    EXPECT_FALSE(r.sawProgress);
    r.in.blocked = false;
    float freed = r.Since();
    r.Run(r.p.settle + r.p.hold + 0.2f);
    ASSERT_EQ(r.fires.size(), 1u);
    EXPECT_NEAR(r.fires[0] - freed, r.p.settle + r.p.hold, 0.1f) << "a whole hold after the scroll";
}

TEST(MenuHoldTimerTest, ASwipePausesTheHoldAndALongOneRestartsIt) {
    // both: a short swipe attempt hides the progress and stops the count...
    Rig r;
    r.RaiseArm();
    r.Run(1.0f);
    r.in.swipe = true;
    r.Run(0.3f);
    EXPECT_FLOAT_EQ(r.timer.Progress(), 0.0f);
    r.in.swipe = false;
    r.Run(0.3f);
    EXPECT_TRUE(r.fires.empty()) << "1.6 s on the row, 0.3 s of it swiping: 1.3 s held";
    r.Run(0.4f);
    ASSERT_EQ(r.fires.size(), 1u);
    EXPECT_NEAR(r.fires[0], r.p.settle + r.p.hold + 0.3f, 0.12f);

    // ... one that lasts starts the hold again.
    Rig g;
    g.RaiseArm();
    g.Run(1.0f);
    g.in.swipe = true;
    g.Run(g.p.swipePause + 0.1f);
    g.in.swipe = false;
    float ended = g.Since();
    g.Run(1.3f);
    EXPECT_TRUE(g.fires.empty());
    g.Run(0.4f);
    ASSERT_EQ(g.fires.size(), 1u);
    EXPECT_NEAR(g.fires[0] - ended, g.p.settle + g.p.hold, 0.12f);
}

TEST(MenuHoldTimerTest, ADisengagedHandNeverSelects) {
    Rig r;
    r.in.engaged = false;
    r.Run(10.0f);
    EXPECT_TRUE(r.fires.empty());
    EXPECT_FALSE(r.sawProgress);
}

TEST(MenuSelectSwitchTest, NamesParseAndTheSwitchSwitches) {
    EXPECT_EQ(Dc3MenuSelectParse("swipe"), kDc3MenuSelectSwipe);
    EXPECT_EQ(Dc3MenuSelectParse("hold"), kDc3MenuSelectHold);
    EXPECT_EQ(Dc3MenuSelectParse("both"), kDc3MenuSelectBoth);
    EXPECT_EQ(Dc3MenuSelectParse("HOLD"), kDc3MenuSelectHold);
    EXPECT_EQ(Dc3MenuSelectParse("dwell"), -1);
    EXPECT_EQ(Dc3MenuSelectParse(""), -1);
    EXPECT_EQ(Dc3MenuSelectParse(nullptr), -1);
    for (int m = 0; m < kDc3MenuSelectCount; m++)
        EXPECT_EQ(Dc3MenuSelectParse(Dc3MenuSelectName(m)), m);

    int was = Dc3MenuSelect();
    Dc3SetMenuSelect(kDc3MenuSelectHold);
    EXPECT_EQ(Dc3MenuSelect(), kDc3MenuSelectHold);
    Dc3SetMenuSelect(kDc3MenuSelectBoth);
    EXPECT_EQ(Dc3MenuSelect(), kDc3MenuSelectBoth);
    Dc3SetMenuSelect(77); // not a mode: ignored
    EXPECT_EQ(Dc3MenuSelect(), kDc3MenuSelectBoth);
    Dc3SetMenuSelect(was);
    EXPECT_EQ(Dc3MenuSelect(), was);
}
