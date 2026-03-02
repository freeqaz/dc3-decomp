#include "test_helpers.h"

#include "math/Rand.h"
#include "obj/Task.h"
#include "os/Joypad.h"
#include "os/ThreadCall.h"
#include "utl/Locale.h"

#include <thread>
#include <chrono>

// All tests use EngineTestFixture (full headless engine boot)
class SubsystemTest : public EngineTestFixture {};

// ---------------------------------------------------------------------------
// Random number generator
// ---------------------------------------------------------------------------

TEST_F(SubsystemTest, RandomSeeded) {
    float values[10];
    for (int i = 0; i < 10; i++) {
        values[i] = RandomFloat();
        EXPECT_GE(values[i], 0.0f);
        EXPECT_LT(values[i], 1.0f);
    }
    // At least one value should differ from the first
    bool allSame = true;
    for (int i = 1; i < 10; i++) {
        if (values[i] != values[0]) {
            allSame = false;
            break;
        }
    }
    EXPECT_FALSE(allSame) << "All 10 RandomFloat() values were identical";
}

// ---------------------------------------------------------------------------
// ThreadCall round-trip
// ---------------------------------------------------------------------------

static int sThreadCallResult = -1;

static int ThreadCallTestFunc() {
    return 42;
}

static void ThreadCallTestCallback(int result) {
    sThreadCallResult = result;
}

TEST_F(SubsystemTest, ThreadCallRoundTrip) {
    sThreadCallResult = -1;
    ThreadCall(ThreadCallTestFunc, ThreadCallTestCallback);

    // Poll until callback fires or timeout
    for (int i = 0; i < 1000 && sThreadCallResult == -1; i++) {
        ThreadCallPoll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(sThreadCallResult, 42)
        << "ThreadCall callback was not invoked with expected result";
}

// ---------------------------------------------------------------------------
// TaskMgr
// ---------------------------------------------------------------------------

TEST_F(SubsystemTest, TaskMgrPoll) {
    float secs = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    EXPECT_GE(secs, 0.0f);

    // Poll should not crash
    TheTaskMgr.Poll();

    float delta = TheTaskMgr.DeltaSeconds();
    EXPECT_GE(delta, 0.0f);
}

// ---------------------------------------------------------------------------
// Locale
// ---------------------------------------------------------------------------

TEST_F(SubsystemTest, LocaleInitialized) {
    // Localize with a bogus token — should not crash
    bool success = false;
    TheLocale.Localize(Symbol("test_nonexistent_token"), success);
    // We don't assert success==true since the token doesn't exist,
    // just that it didn't crash
}

// ---------------------------------------------------------------------------
// Joypad
// ---------------------------------------------------------------------------

TEST_F(SubsystemTest, JoypadPoll) {
    // Poll should not crash
    JoypadPoll();

    // Getting pad data for pad 0 should return a valid pointer
    JoypadData *pad = JoypadGetPadData(0);
    EXPECT_NE(pad, nullptr);
}
