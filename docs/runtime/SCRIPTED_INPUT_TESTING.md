# Scripted Input Testing for Xenia Headless

## Goal

Enable automated input injection into Xenia headless mode to:
1. Navigate past title screen without rendering
2. Compare behavior between original and decompiled XEX
3. Reach gameplay states (start a song) for validation
4. Create regression tests for decomp work

## Current State

**Xenia headless uses `NopInputDriver`** which returns `X_ERROR_DEVICE_NOT_CONNECTED` for all input queries. Games see no controller attached.

```cpp
// src/xenia/hid/nop/nop_input_driver.cc
X_RESULT NopInputDriver::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  return X_ERROR_DEVICE_NOT_CONNECTED;  // ← Game can't progress
}
```

## Solution: ScriptedInputDriver

Create a new HID driver that reads a script file and replays controller inputs.

### Architecture

```
┌─────────────────────────────────────────────────┐
│ Xenia Headless                                  │
│                                                 │
│  ┌──────────────────┐     ┌─────────────────┐ │
│  │ Game Code        │────→│ GetState()      │ │
│  │ (XInputGetState) │     │ GetCapabilities()│ │
│  └──────────────────┘     └─────────────────┘ │
│                                   │             │
│                                   ↓             │
│                     ┌──────────────────────────┐│
│                     │ ScriptedInputDriver      ││
│                     │                          ││
│                     │ - Load script file       ││
│                     │ - Track timestamp        ││
│                     │ - Return button states   ││
│                     └──────────────────────────┘│
│                                   ↑             │
│                                   │             │
│                     ┌──────────────────────────┐│
│                     │ input_script.txt         ││
│                     │                          ││
│                     │ 0.0: CONNECT             ││
│                     │ 1.5: PRESS A             ││
│                     │ 2.0: RELEASE A           ││
│                     │ 3.0: PRESS START         ││
│                     └──────────────────────────┘│
└─────────────────────────────────────────────────┘
```

### Input Script Format

Human-readable text format with timestamps:

```
# input_script.txt - Dance Central 3 Title Screen Navigation

# Format: timestamp(sec) ACTION [params]

# Connect controller
0.0 CONNECT user=0

# Wait for title screen to load (3 seconds)
3.0 WAIT

# Press A to start (skip intro video)
3.5 PRESS A
3.7 RELEASE A

# Wait for menu
4.0 WAIT

# Navigate to "Play Now"
5.0 PRESS DPAD_DOWN
5.2 RELEASE DPAD_DOWN

# Select
5.5 PRESS A
5.7 RELEASE A

# Wait for song list
6.0 WAIT

# Select first song
7.0 PRESS A
7.2 RELEASE A

# Wait for difficulty screen
8.0 WAIT

# Select "Easy"
9.0 PRESS A
9.2 RELEASE A

# Song should start loading...
10.0 WAIT

# End of script - hold inputs until timeout
```

### Implementation

#### 1. Create ScriptedInputDriver

```cpp
// src/xenia/hid/scripted/scripted_input_driver.h
#ifndef XENIA_HID_SCRIPTED_SCRIPTED_INPUT_DRIVER_H_
#define XENIA_HID_SCRIPTED_SCRIPTED_INPUT_DRIVER_H_

#include <fstream>
#include <queue>
#include <string>
#include <chrono>

#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {
namespace scripted {

struct InputEvent {
  double timestamp;  // Seconds from start
  enum Type {
    CONNECT,
    DISCONNECT,
    PRESS_BUTTON,
    RELEASE_BUTTON,
    SET_TRIGGER,
    SET_STICK,
    WAIT
  } type;

  // Event parameters
  uint32_t user_index;
  uint16_t buttons;      // For PRESS/RELEASE
  uint8_t left_trigger;  // For SET_TRIGGER
  uint8_t right_trigger;
  int16_t thumb_lx;      // For SET_STICK
  int16_t thumb_ly;
  int16_t thumb_rx;
  int16_t thumb_ry;
};

class ScriptedInputDriver final : public InputDriver {
 public:
  explicit ScriptedInputDriver(xe::ui::Window* window, size_t window_z_order);
  ~ScriptedInputDriver() override;

  X_STATUS Setup() override;

  // Load script from file
  bool LoadScript(const std::string& script_path);

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

 private:
  void UpdateState();  // Process events up to current time

  std::queue<InputEvent> events_;
  std::chrono::steady_clock::time_point start_time_;

  // Current controller state (user 0-3)
  bool connected_[4] = {false};
  uint32_t packet_number_[4] = {0};
  X_INPUT_GAMEPAD gamepad_[4] = {};
};

}  // namespace scripted
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_SCRIPTED_SCRIPTED_INPUT_DRIVER_H_
```

#### 2. Implement Script Parser

```cpp
// src/xenia/hid/scripted/scripted_input_driver.cc
bool ScriptedInputDriver::LoadScript(const std::string& script_path) {
  std::ifstream file(script_path);
  if (!file.is_open()) {
    XELOGE("Failed to open input script: {}", script_path);
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#') continue;

    // Parse: "timestamp ACTION [params]"
    std::istringstream iss(line);
    InputEvent event;

    iss >> event.timestamp;

    std::string action;
    iss >> action;

    if (action == "CONNECT") {
      event.type = InputEvent::CONNECT;
      event.user_index = 0;
      // Parse user=N if present
      std::string param;
      if (iss >> param) {
        if (param.find("user=") == 0) {
          event.user_index = std::stoi(param.substr(5));
        }
      }
    } else if (action == "PRESS") {
      event.type = InputEvent::PRESS_BUTTON;
      std::string button;
      iss >> button;
      event.buttons = ParseButton(button);  // Helper function
      event.user_index = 0;
    } else if (action == "RELEASE") {
      event.type = InputEvent::RELEASE_BUTTON;
      std::string button;
      iss >> button;
      event.buttons = ParseButton(button);
      event.user_index = 0;
    } else if (action == "WAIT") {
      event.type = InputEvent::WAIT;
    }
    // ... more actions (STICK, TRIGGER, etc.)

    events_.push(event);
  }

  XELOGI("Loaded {} input events from {}", events_.size(), script_path);
  start_time_ = std::chrono::steady_clock::now();
  return true;
}

X_RESULT ScriptedInputDriver::GetState(uint32_t user_index,
                                        X_INPUT_STATE* out_state) {
  if (user_index >= 4) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  // Process events up to current time
  UpdateState();

  if (!connected_[user_index]) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  out_state->packet_number = packet_number_[user_index];
  out_state->gamepad = gamepad_[user_index];
  return X_ERROR_SUCCESS;
}

void ScriptedInputDriver::UpdateState() {
  auto now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration<double>(now - start_time_).count();

  // Process all events up to current timestamp
  while (!events_.empty() && events_.front().timestamp <= elapsed) {
    InputEvent& event = events_.front();

    switch (event.type) {
      case InputEvent::CONNECT:
        connected_[event.user_index] = true;
        XELOGI("Controller {} connected at t={:.2f}s",
               event.user_index, elapsed);
        break;

      case InputEvent::PRESS_BUTTON:
        gamepad_[event.user_index].buttons |= event.buttons;
        packet_number_[event.user_index]++;
        break;

      case InputEvent::RELEASE_BUTTON:
        gamepad_[event.user_index].buttons &= ~event.buttons;
        packet_number_[event.user_index]++;
        break;

      // ... handle other event types
    }

    events_.pop();
  }
}
```

#### 3. Register Driver in Xenia Headless

```cpp
// src/xenia/app/emulator_headless.cc
#include "xenia/hid/scripted/scripted_input_driver.h"

void EmulatorHeadless::Setup() {
  // ...

  // Check for input script flag
  auto script_path = cvars::input_script;

  if (!script_path.empty()) {
    // Use scripted input driver
    auto scripted = std::make_unique<hid::scripted::ScriptedInputDriver>(
        nullptr, 0);
    scripted->LoadScript(script_path);
    emulator_->input_system()->AddDriver(std::move(scripted));
    XELOGI("Using scripted input from: {}", script_path);
  } else {
    // Use nop driver (no input)
    auto nop = std::make_unique<hid::nop::NopInputDriver>(nullptr, 0);
    emulator_->input_system()->AddDriver(std::move(nop));
  }
}
```

#### 4. Add Command-Line Flag

```cpp
// src/xenia/hid/hid_flags.cc
DEFINE_string(input_script, "",
              "Path to input script file for automated testing in headless mode.",
              "HID");
```

## Usage

### 1. Create Input Script

```bash
cat > scripts/test_title_screen.txt << 'EOF'
# Connect controller
0.0 CONNECT user=0

# Wait 3 seconds for title screen
3.0 WAIT

# Press A to skip intro
3.5 PRESS A
3.7 RELEASE A

# End script
EOF
```

### 2. Run Test

```bash
xenia-headless \
  --target=build/373307D9/default.xex \
  --input_script=scripts/test_title_screen.txt \
  --headless_timeout_ms=30000 \
  --log_level=1
```

### 3. Compare Original vs Decompiled

```bash
# Test original XEX
xenia-headless \
  --target=orig/373307D9/default.xex \
  --input_script=scripts/test_song_start.txt \
  2>&1 | tee original.log

# Test decompiled XEX
xenia-headless \
  --target=build/373307D9/default.xex \
  --input_script=scripts/test_song_start.txt \
  2>&1 | tee decomp.log

# Compare logs
diff -u original.log decomp.log
```

## Advanced Features

### Memory Snapshots

Capture memory state at specific checkpoints:

```
# Script with checkpoints
5.0 PRESS A
5.2 RELEASE A
5.5 CHECKPOINT "after_title_screen"
```

```cpp
// In ScriptedInputDriver
case InputEvent::CHECKPOINT:
  emulator_->DumpMemorySnapshot(event.checkpoint_name);
  break;
```

### Event Recording

Record inputs from a real playthrough:

```bash
# Play game with full Xenia, record inputs
xenia --target=game.xex --record_inputs=recorded.txt

# Replay in headless mode
xenia-headless --target=game.xex --input_script=recorded.txt
```

### Assertions

Validate game state during script execution:

```
# Assert memory value
10.0 ASSERT_MEMORY addr=0x82100000 value=0x12345678

# Assert no crash
10.0 ASSERT_RUNNING
```

## Test Suite Examples

### Test 1: Title Screen Navigation

```bash
# scripts/tests/01_title_screen.txt
0.0 CONNECT
3.0 PRESS A
3.2 RELEASE A
5.0 CHECKPOINT "main_menu"
```

**Expected:** Game reaches main menu, no crashes

### Test 2: Song Selection

```bash
# scripts/tests/02_song_select.txt
0.0 CONNECT
3.0 PRESS A
3.2 RELEASE A
5.0 PRESS A
5.2 RELEASE A
7.0 CHECKPOINT "song_list"
```

**Expected:** Song list loads, no crashes

### Test 3: Start Song

```bash
# scripts/tests/03_start_song.txt
0.0 CONNECT
3.0 PRESS A
3.2 RELEASE A
5.0 PRESS A
5.2 RELEASE A
7.0 PRESS A
7.2 RELEASE A
10.0 CHECKPOINT "song_loading"
15.0 CHECKPOINT "song_started"
```

**Expected:** Song loads and starts gameplay

## Benefits

1. **Automated Regression Testing**
   - Run tests after each decomp function to catch regressions
   - Validate behavior matches original

2. **Deep State Validation**
   - Reach gameplay states without manual interaction
   - Test code paths that require input progression

3. **Reproducible Testing**
   - Same inputs every time
   - Deterministic for debugging

4. **CI/CD Integration**
   - Run tests automatically on every commit
   - Fail build if behavior diverges from original

## Implementation Effort

| Task | Effort | Priority |
|------|--------|----------|
| Create ScriptedInputDriver skeleton | 2 hours | High |
| Implement script parser | 3 hours | High |
| Add button/stick support | 2 hours | High |
| Add command-line flag | 30 min | High |
| Create test scripts (title screen) | 1 hour | High |
| Memory snapshot support | 3 hours | Medium |
| Assertion framework | 4 hours | Medium |
| Input recording from full Xenia | 6 hours | Low |

**Total (MVP):** ~8.5 hours for basic scripted input support

## Next Steps

1. **Implement ScriptedInputDriver** in Xenia
2. **Create basic test script** for Dance Central 3 title screen
3. **Validate both XEXs reach same state** with identical inputs
4. **Expand scripts** to reach song start
5. **Add to CI/CD** for automated regression testing

## Alternative: Record from Full Xenia

If implementing ScriptedInputDriver is too much work, we could:

1. Run full Xenia with rendering
2. Play through manually with real controller
3. Log all `XInputGetState()` calls with timestamps
4. Replay the log in headless mode

This requires less Xenia modification but loses the scriptability benefits.
