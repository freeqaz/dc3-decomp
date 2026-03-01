// DC3 Native Port - Joypad via GLFW gamepad + keyboard fallback
// Replaces Joypad_Stub.cpp

#include "os/Joypad.h"
#include "os/JoypadMsgs.h"
#include "os/Debug.h"
#include "os/System.h"

#include <GLFW/glfw3.h>

// Set by Rnd_Wgpu during Init()
extern GLFWwindow *gNativeWindow;

static const float kTriggerThreshold = 0.3f;

void JoypadInit() {
    MILO_LOG("[Native] JoypadInit\n");
    DataArray *cfg = SystemConfig("joypad");
    JoypadInitCommon(cfg);
    JoypadReset();
}

void JoypadReset() {
    ResetAllUsersPads();
    // Set pad 0 as connected analog controller with default user
    JoypadData *pad = JoypadGetPadData(0);
    pad->mConnected = true;
    pad->mType = kJoypadAnalog;
    pad->mControllerType = "xbox";
    pad->mNumAnalogSticks = 2;
    pad->mTranslateSticks = true;
}

void JoypadTerminate() {
    JoypadTerminateCommon();
}

void JoypadPoll() {
    if (!gNativeWindow)
        return;

    for (int pad = 0; pad < kNumJoypads; pad++) {
        JoypadData *data = JoypadGetPadData(pad);
        if (!data->mConnected)
            continue;

        unsigned int newButtons = 0;

        // --- GLFW Gamepad ---
        GLFWgamepadstate gpState;
        bool hasGamepad = (glfwJoystickIsGamepad(pad) && glfwGetGamepadState(pad, &gpState));

        if (hasGamepad) {
            // Face buttons (Xbox layout → Milo PS-style enum)
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_A])           newButtons |= (1 << kPad_X);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_B])           newButtons |= (1 << kPad_Circle);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_X])           newButtons |= (1 << kPad_Square);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_Y])           newButtons |= (1 << kPad_Tri);

            // Bumpers
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER]) newButtons |= (1 << kPad_L1);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER])newButtons |= (1 << kPad_R1);

            // Menu buttons
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_BACK])        newButtons |= (1 << kPad_Select);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_START])       newButtons |= (1 << kPad_Start);

            // Thumbstick clicks
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB])  newButtons |= (1 << kPad_L3);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB]) newButtons |= (1 << kPad_R3);

            // D-pad
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP])    newButtons |= (1 << kPad_DUp);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN])  newButtons |= (1 << kPad_DDown);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT])  newButtons |= (1 << kPad_DLeft);
            if (gpState.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]) newButtons |= (1 << kPad_DRight);

            // Triggers (remap [-1,1] → [0,1])
            float lt = (gpState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]  + 1.0f) * 0.5f;
            float rt = (gpState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
            data->mTriggers[0] = lt;
            data->mTriggers[1] = rt;
            if (lt > kTriggerThreshold) newButtons |= (1 << kPad_L2);
            if (rt > kTriggerThreshold) newButtons |= (1 << kPad_R2);

            // Sticks
            data->mSticks[0][0] = gpState.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
            data->mSticks[0][1] = gpState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
            data->mSticks[1][0] = gpState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
            data->mSticks[1][1] = gpState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
        }

        // --- Keyboard as pad 0 fallback ---
        if (pad == 0) {
            if (glfwGetKey(gNativeWindow, GLFW_KEY_UP)    == GLFW_PRESS) newButtons |= (1 << kPad_DUp);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_DOWN)  == GLFW_PRESS) newButtons |= (1 << kPad_DDown);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_LEFT)  == GLFW_PRESS) newButtons |= (1 << kPad_DLeft);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_RIGHT) == GLFW_PRESS) newButtons |= (1 << kPad_DRight);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_ENTER) == GLFW_PRESS) newButtons |= (1 << kPad_X);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_ESCAPE)== GLFW_PRESS) newButtons |= (1 << kPad_Circle);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_SPACE) == GLFW_PRESS) newButtons |= (1 << kPad_Start);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_TAB)   == GLFW_PRESS) newButtons |= (1 << kPad_Select);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_Q)     == GLFW_PRESS) newButtons |= (1 << kPad_L1);
            if (glfwGetKey(gNativeWindow, GLFW_KEY_E)     == GLFW_PRESS) newButtons |= (1 << kPad_R1);
        }

        // Translate analog sticks to digital buttons
        if (data->mTranslateSticks) {
            TranslateSticksToButs(*data, newButtons);
        }

        // Compute deltas
        unsigned int oldButtons = data->mButtons;
        data->mNewPressed  = newButtons & ~oldButtons;
        data->mNewReleased = oldButtons & ~newButtons;
        data->mButtons = newButtons;

        // Broadcast button messages
        for (int b = 0; b < kPad_NumButtons; b++) {
            if (data->mNewPressed & (1 << b)) {
                JoypadAction action = ButtonToAction((JoypadButton)b, data->mControllerType);
                ButtonDownMsg msg(data->mUser, (JoypadButton)b, action, pad);
                JoypadPushThroughMsg(msg);
            }
        }
        for (int b = 0; b < kPad_NumButtons; b++) {
            if (data->mNewReleased & (1 << b)) {
                JoypadAction action = ButtonToAction((JoypadButton)b, data->mControllerType);
                ButtonUpMsg msg(data->mUser, (JoypadButton)b, action, pad);
                JoypadPushThroughMsg(msg);
            }
        }
    }
}
