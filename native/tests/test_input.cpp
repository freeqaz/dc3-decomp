// Input system tests — Joypad button mapping + delta logic, Keyboard queue
#include "test_helpers.h"
#include "os/Joypad.h"

// ============================================================================
// JoypadData unit tests (no engine init needed — pure struct logic)
// ============================================================================

TEST(JoypadData, DefaultConstructorZeroed) {
    JoypadData data;
    EXPECT_EQ(data.mButtons, 0u);
    EXPECT_EQ(data.mNewPressed, 0u);
    EXPECT_EQ(data.mNewReleased, 0u);
    EXPECT_FALSE(data.mConnected);
    EXPECT_EQ(data.mType, kJoypadNone);
    EXPECT_FLOAT_EQ(data.mSticks[0][0], 0.0f);
    EXPECT_FLOAT_EQ(data.mSticks[0][1], 0.0f);
    EXPECT_FLOAT_EQ(data.mSticks[1][0], 0.0f);
    EXPECT_FLOAT_EQ(data.mSticks[1][1], 0.0f);
    EXPECT_FLOAT_EQ(data.mTriggers[0], 0.0f);
    EXPECT_FLOAT_EQ(data.mTriggers[1], 0.0f);
}

TEST(JoypadData, ButtonMaskBits) {
    // Verify button enum values produce correct bitmask positions
    EXPECT_EQ(1 << kPad_L2, 0x001);
    EXPECT_EQ(1 << kPad_R2, 0x002);
    EXPECT_EQ(1 << kPad_L1, 0x004);
    EXPECT_EQ(1 << kPad_R1, 0x008);
    EXPECT_EQ(1 << kPad_Tri, 0x010);
    EXPECT_EQ(1 << kPad_Circle, 0x020);
    EXPECT_EQ(1 << kPad_X, 0x040);
    EXPECT_EQ(1 << kPad_Square, 0x080);
    EXPECT_EQ(1 << kPad_Select, 0x100);
    EXPECT_EQ(1 << kPad_Start, 0x800);
    EXPECT_EQ(1 << kPad_DUp, 0x1000);
    EXPECT_EQ(1 << kPad_DRight, 0x2000);
    EXPECT_EQ(1 << kPad_DDown, 0x4000);
    EXPECT_EQ(1 << kPad_DLeft, 0x8000);
}

TEST(JoypadData, IsButtonInMask) {
    JoypadData data;
    data.mButtons = (1 << kPad_X) | (1 << kPad_Start);
    EXPECT_TRUE(data.IsButtonInMask(kPad_X));
    EXPECT_TRUE(data.IsButtonInMask(kPad_Start));
    EXPECT_FALSE(data.IsButtonInMask(kPad_Circle));
    EXPECT_FALSE(data.IsButtonInMask(kPad_DUp));
}

// ============================================================================
// Delta computation tests (simulates what JoypadPoll does)
// ============================================================================

TEST(JoypadDelta, PressDetection) {
    unsigned int oldButtons = 0;
    unsigned int newButtons = (1 << kPad_X) | (1 << kPad_DUp);

    unsigned int pressed  = newButtons & ~oldButtons;
    unsigned int released = oldButtons & ~newButtons;

    EXPECT_EQ(pressed, newButtons); // all new
    EXPECT_EQ(released, 0u);
}

TEST(JoypadDelta, ReleaseDetection) {
    unsigned int oldButtons = (1 << kPad_X) | (1 << kPad_Circle);
    unsigned int newButtons = (1 << kPad_Circle); // X released

    unsigned int pressed  = newButtons & ~oldButtons;
    unsigned int released = oldButtons & ~newButtons;

    EXPECT_EQ(pressed, 0u);
    EXPECT_EQ(released, (unsigned int)(1 << kPad_X));
}

TEST(JoypadDelta, SimultaneousPressAndRelease) {
    unsigned int oldButtons = (1 << kPad_X);
    unsigned int newButtons = (1 << kPad_Circle); // X released, Circle pressed

    unsigned int pressed  = newButtons & ~oldButtons;
    unsigned int released = oldButtons & ~newButtons;

    EXPECT_EQ(pressed, (unsigned int)(1 << kPad_Circle));
    EXPECT_EQ(released, (unsigned int)(1 << kPad_X));
}

TEST(JoypadDelta, HeldButtonNotReported) {
    unsigned int oldButtons = (1 << kPad_Start);
    unsigned int newButtons = (1 << kPad_Start); // still held

    unsigned int pressed  = newButtons & ~oldButtons;
    unsigned int released = oldButtons & ~newButtons;

    EXPECT_EQ(pressed, 0u);
    EXPECT_EQ(released, 0u);
}

// ============================================================================
// TranslateSticksToButs tests
// ============================================================================

TEST(TranslateSticks, LeftStickRight) {
    JoypadData data;
    data.mDistFromRest = 0.5f;
    data.mSticks[0][0] = 0.9f;  // LX far right
    data.mSticks[0][1] = 0.0f;
    data.mSticks[1][0] = 0.0f;
    data.mSticks[1][1] = 0.0f;

    unsigned int mask = 0;
    TranslateSticksToButs(data, mask);

    EXPECT_TRUE(mask & (1 << kPad_LStickRight));
    EXPECT_FALSE(mask & (1 << kPad_LStickLeft));
    EXPECT_FALSE(mask & (1 << kPad_LStickUp));
    EXPECT_FALSE(mask & (1 << kPad_LStickDown));
}

TEST(TranslateSticks, LeftStickLeft) {
    JoypadData data;
    data.mDistFromRest = 0.5f;
    data.mSticks[0][0] = -0.9f; // LX far left
    data.mSticks[0][1] = 0.0f;
    data.mSticks[1][0] = 0.0f;
    data.mSticks[1][1] = 0.0f;

    unsigned int mask = 0;
    TranslateSticksToButs(data, mask);

    EXPECT_TRUE(mask & (1 << kPad_LStickLeft));
    EXPECT_FALSE(mask & (1 << kPad_LStickRight));
}

TEST(TranslateSticks, LeftStickDown) {
    JoypadData data;
    data.mDistFromRest = 0.5f;
    data.mSticks[0][0] = 0.0f;
    data.mSticks[0][1] = 0.9f; // LY positive = down
    data.mSticks[1][0] = 0.0f;
    data.mSticks[1][1] = 0.0f;

    unsigned int mask = 0;
    TranslateSticksToButs(data, mask);

    EXPECT_TRUE(mask & (1 << kPad_LStickDown));
    EXPECT_FALSE(mask & (1 << kPad_LStickUp));
}

TEST(TranslateSticks, LeftStickUp) {
    JoypadData data;
    data.mDistFromRest = 0.5f;
    data.mSticks[0][0] = 0.0f;
    data.mSticks[0][1] = -0.9f; // LY negative = up
    data.mSticks[1][0] = 0.0f;
    data.mSticks[1][1] = 0.0f;

    unsigned int mask = 0;
    TranslateSticksToButs(data, mask);

    EXPECT_TRUE(mask & (1 << kPad_LStickUp));
    EXPECT_FALSE(mask & (1 << kPad_LStickDown));
}

TEST(TranslateSticks, RightStickDiagonal) {
    JoypadData data;
    data.mDistFromRest = 0.5f;
    data.mSticks[0][0] = 0.0f;
    data.mSticks[0][1] = 0.0f;
    data.mSticks[1][0] = 0.9f;  // RX right
    data.mSticks[1][1] = -0.9f; // RY up

    unsigned int mask = 0;
    TranslateSticksToButs(data, mask);

    EXPECT_TRUE(mask & (1 << kPad_RStickRight));
    EXPECT_TRUE(mask & (1 << kPad_RStickUp));
    EXPECT_FALSE(mask & (1 << kPad_RStickLeft));
    EXPECT_FALSE(mask & (1 << kPad_RStickDown));
}

TEST(TranslateSticks, BelowThresholdNoButtons) {
    JoypadData data;
    data.mDistFromRest = 0.5f;
    data.mSticks[0][0] = 0.3f; // below threshold
    data.mSticks[0][1] = -0.3f;
    data.mSticks[1][0] = 0.1f;
    data.mSticks[1][1] = 0.0f;

    unsigned int mask = 0;
    TranslateSticksToButs(data, mask);

    EXPECT_EQ(mask, 0u);
}

// ============================================================================
// Button enum sanity
// ============================================================================

TEST(JoypadEnum, XboxAliases) {
    // Xbox aliases should map to the same values as PS-style names
    EXPECT_EQ(kPad_Xbox_A, kPad_X);
    EXPECT_EQ(kPad_Xbox_B, kPad_Circle);
    EXPECT_EQ(kPad_Xbox_X, kPad_Square);
    EXPECT_EQ(kPad_Xbox_Y, kPad_Tri);
    EXPECT_EQ(kPad_Xbox_LB, kPad_L1);
    EXPECT_EQ(kPad_Xbox_RB, kPad_R1);
    EXPECT_EQ(kPad_Xbox_LT, kPad_L2);
    EXPECT_EQ(kPad_Xbox_RT, kPad_R2);
}

TEST(JoypadEnum, NumButtons) {
    EXPECT_EQ(kPad_NumButtons, 24);
}

// ============================================================================
// Trigger threshold tests (simulates native JoypadPoll trigger logic)
// ============================================================================

TEST(TriggerMapping, RemapRange) {
    // GLFW triggers are [-1, 1], remap to [0, 1] via (x + 1) * 0.5
    float glfwReleased = -1.0f;
    float glfwHalf = 0.0f;
    float glfwFull = 1.0f;

    EXPECT_FLOAT_EQ((glfwReleased + 1.0f) * 0.5f, 0.0f);
    EXPECT_FLOAT_EQ((glfwHalf + 1.0f) * 0.5f, 0.5f);
    EXPECT_FLOAT_EQ((glfwFull + 1.0f) * 0.5f, 1.0f);
}

TEST(TriggerMapping, ThresholdLogic) {
    float threshold = 0.3f;
    // Below threshold — no button
    float lt = 0.2f;
    EXPECT_FALSE(lt > threshold);
    // Above threshold — button pressed
    lt = 0.5f;
    EXPECT_TRUE(lt > threshold);
}

// ============================================================================
// Directional action helpers
// ============================================================================

TEST(JoypadHelpers, DirectionalAction) {
    EXPECT_TRUE(DirectionalAction(kAction_Up));
    EXPECT_TRUE(DirectionalAction(kAction_Down));
    EXPECT_TRUE(DirectionalAction(kAction_Left));
    EXPECT_TRUE(DirectionalAction(kAction_Right));
    EXPECT_FALSE(DirectionalAction(kAction_Confirm));
    EXPECT_FALSE(DirectionalAction(kAction_Cancel));
    EXPECT_FALSE(DirectionalAction(kAction_Start));
}

TEST(JoypadHelpers, MovedLeftStick) {
    EXPECT_TRUE(MovedLeftStick(kPad_LStickUp));
    EXPECT_TRUE(MovedLeftStick(kPad_LStickDown));
    EXPECT_TRUE(MovedLeftStick(kPad_LStickLeft));
    EXPECT_TRUE(MovedLeftStick(kPad_LStickRight));
    EXPECT_FALSE(MovedLeftStick(kPad_RStickUp));
    EXPECT_FALSE(MovedLeftStick(kPad_DUp));
    EXPECT_FALSE(MovedLeftStick(kPad_X));
}
