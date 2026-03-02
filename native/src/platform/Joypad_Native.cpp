// DC3 Native Port - Joypad via GLFW gamepad + keyboard fallback
// Replaces Joypad_Stub.cpp
//
// Headless input: set MILO_INPUT_SCRIPT to a text file with timed button presses.
// Format: one "frame button" pair per line (# comments, blank lines OK).
// Button names: start, confirm/a, cancel/b, up, down, left, right,
//               option/back, l1/lb, r1/rb, l2/lt, r2/rt, x, y
// Example:
//   60 start        # press Start on frame 60 to skip attract
//   120 down        # navigate down
//   150 confirm     # select menu item

#include "os/Joypad.h"
#include "os/JoypadMsgs.h"
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Rnd.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

// Set by Rnd_Wgpu during Init()
extern GLFWwindow *gNativeWindow;

static const float kTriggerThreshold = 0.3f;

// ============================================================================
// Headless scripted input
// ============================================================================

struct ScriptedInput {
    int frame;
    JoypadButton button;
};

static std::vector<ScriptedInput> gInputScript;
static bool gInputScriptLoaded = false;

static JoypadButton ParseButtonName(const char *name) {
    // Confirm / A
    if (!strcmp(name, "confirm") || !strcmp(name, "a"))     return kPad_X;
    // Cancel / B
    if (!strcmp(name, "cancel") || !strcmp(name, "b"))      return kPad_Circle;
    // Start
    if (!strcmp(name, "start"))                              return kPad_Start;
    // Option / Back / Select
    if (!strcmp(name, "option") || !strcmp(name, "back") ||
        !strcmp(name, "select"))                             return kPad_Select;
    // D-pad
    if (!strcmp(name, "up"))                                 return kPad_DUp;
    if (!strcmp(name, "down"))                               return kPad_DDown;
    if (!strcmp(name, "left"))                               return kPad_DLeft;
    if (!strcmp(name, "right"))                              return kPad_DRight;
    // Bumpers
    if (!strcmp(name, "l1") || !strcmp(name, "lb"))          return kPad_L1;
    if (!strcmp(name, "r1") || !strcmp(name, "rb"))          return kPad_R1;
    // Triggers
    if (!strcmp(name, "l2") || !strcmp(name, "lt"))          return kPad_L2;
    if (!strcmp(name, "r2") || !strcmp(name, "rt"))          return kPad_R2;
    // Face buttons by Xbox name
    if (!strcmp(name, "x"))                                  return kPad_Square;
    if (!strcmp(name, "y"))                                  return kPad_Tri;
    // Sticks
    if (!strcmp(name, "l3") || !strcmp(name, "ls"))          return kPad_L3;
    if (!strcmp(name, "r3") || !strcmp(name, "rs"))          return kPad_R3;

    return (JoypadButton)-1;
}

static void LoadInputScript() {
    gInputScriptLoaded = true;
    const char *path = getenv("MILO_INPUT_SCRIPT");
    if (!path || !path[0]) return;

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("DC3 Native: MILO_INPUT_SCRIPT: cannot open '%s'\n", path);
        return;
    }

    char line[256];
    int lineNum = 0;
    while (fgets(line, sizeof(line), f)) {
        lineNum++;
        // Strip comment
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        // Parse "frame button"
        int frame;
        char btnName[64];
        if (sscanf(line, "%d %63s", &frame, btnName) != 2) continue;

        // Lowercase the button name
        for (char *p = btnName; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') *p += 32;
        }

        JoypadButton btn = ParseButtonName(btnName);
        if ((int)btn < 0) {
            printf("DC3 Native: MILO_INPUT_SCRIPT:%d: unknown button '%s'\n", lineNum, btnName);
            continue;
        }

        gInputScript.push_back({frame, btn});
    }
    fclose(f);

    // Sort by frame for efficient processing
    std::sort(gInputScript.begin(), gInputScript.end(),
              [](const ScriptedInput &a, const ScriptedInput &b) { return a.frame < b.frame; });

    printf("DC3 Native: loaded %d input events from '%s'\n", (int)gInputScript.size(), path);
    for (size_t i = 0; i < gInputScript.size(); i++) {
        printf("  frame %d: button %d\n", gInputScript[i].frame, (int)gInputScript[i].button);
    }
}

// Returns button bitmask for the current frame from the input script.
// Buttons are pressed for exactly 1 frame (press on N, release on N+1).
static unsigned int GetScriptedButtons(int frame) {
    unsigned int buttons = 0;
    for (size_t i = 0; i < gInputScript.size(); i++) {
        if (gInputScript[i].frame == frame) {
            buttons |= (1 << gInputScript[i].button);
        }
    }
    return buttons;
}

// ============================================================================
// Joypad lifecycle
// ============================================================================

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

// ============================================================================
// Poll — reads GLFW input (windowed) or scripted input (headless)
// ============================================================================

void JoypadPoll() {
    // Lazy-load input script on first poll
    if (!gInputScriptLoaded) LoadInputScript();

    int currentFrame = (int)TheRnd.GetFrameID();

    for (int pad = 0; pad < kNumJoypads; pad++) {
        JoypadData *data = JoypadGetPadData(pad);
        if (!data->mConnected)
            continue;

        unsigned int newButtons = 0;

        if (gNativeWindow) {
            // --- Windowed mode: GLFW Gamepad ---
            GLFWgamepadstate gpState;
            bool hasGamepad = (glfwJoystickIsGamepad(pad) && glfwGetGamepadState(pad, &gpState));

            if (hasGamepad) {
                // Face buttons (Xbox layout -> Milo PS-style enum)
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

                // Triggers (remap [-1,1] -> [0,1])
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
        } else if (pad == 0) {
            // --- Headless mode: scripted input (pad 0 only) ---
            newButtons = GetScriptedButtons(currentFrame);
            if (newButtons && !gInputScript.empty()) {
                printf("DC3 Input: Frame %d — scripted buttons 0x%x\n", currentFrame, newButtons);
            }
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

        // Native button-to-action mapping (DTA config may not be loaded)
        auto nativeButtonToAction = [](JoypadButton btn) -> JoypadAction {
            switch (btn) {
            case kPad_X:        return kAction_Confirm;     // A button
            case kPad_Circle:   return kAction_Cancel;      // B button
            case kPad_Start:    return kAction_Start;
            case kPad_Select:   return kAction_Option;
            case kPad_DUp:      return kAction_Up;
            case kPad_DDown:    return kAction_Down;
            case kPad_DLeft:    return kAction_Left;
            case kPad_DRight:   return kAction_Right;
            case kPad_L1:       return kAction_PageUp;      // LB
            case kPad_R1:       return kAction_PageDown;    // RB
            case kPad_Square:   return kAction_ViewModify;  // X button
            case kPad_Tri:      return kAction_ShellOption;  // Y button
            case kPad_LStickUp:    return kAction_Up;
            case kPad_LStickDown:  return kAction_Down;
            case kPad_LStickLeft:  return kAction_Left;
            case kPad_LStickRight: return kAction_Right;
            default:            return kAction_None;
            }
        };

        // Broadcast button messages
        for (int b = 0; b < kPad_NumButtons; b++) {
            if (data->mNewPressed & (1 << b)) {
                JoypadAction action = ButtonToAction((JoypadButton)b, data->mControllerType);
                if (action == kAction_None)
                    action = nativeButtonToAction((JoypadButton)b);
                ButtonDownMsg msg(data->mUser, (JoypadButton)b, action, pad);
                JoypadPushThroughMsg(msg);
            }
        }
        for (int b = 0; b < kPad_NumButtons; b++) {
            if (data->mNewReleased & (1 << b)) {
                JoypadAction action = ButtonToAction((JoypadButton)b, data->mControllerType);
                if (action == kAction_None)
                    action = nativeButtonToAction((JoypadButton)b);
                ButtonUpMsg msg(data->mUser, (JoypadButton)b, action, pad);
                JoypadPushThroughMsg(msg);
            }
        }
    }
}
