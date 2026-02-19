# Scripted Input Testing - Implementation Plan

**Status:** Ready for implementation
**Priority:** High - Enables automated regression testing and deep state validation
**Estimated Effort:** 8-10 hours (MVP), 18-25 hours (full features)
**Owner:** TBD

## Problem Statement

Currently, we can verify the decompiled XEX boots in Xenia headless mode, but we have no visibility into what happens after kernel initialization. The game likely waits for input or GPU events that never come in headless mode.

**We need a way to:**
1. Inject controller inputs in headless mode (no rendering)
2. Navigate past title screen and menus automatically
3. Reach gameplay states (song start) for validation
4. Compare behavior between original and decompiled XEX
5. Create automated regression tests for decomp work

## Solution Overview

Implement a **ScriptedInputDriver** for Xenia headless mode that reads a text-based script file and replays controller inputs with precise timing.

### Architecture

```
Game Code                   Xenia Headless
    │                            │
    │ XInputGetState()           │
    ├────────────────────────────►
    │                            │
    │                     ScriptedInputDriver
    │                            │
    │                     ┌──────┴──────┐
    │                     │ Load Script │
    │                     │ Track Time  │
    │                     │ Return State│
    │                     └──────┬──────┘
    │                            │
    │  X_INPUT_STATE             │
    │◄────────────────────────────
    │                            │
    │  buttons = A pressed       │
    │  packet_number = 42        │
    │                            │
```

### Input Script Format

```
# test_song_start.txt
# Format: timestamp(seconds) ACTION [parameters]

0.0 CONNECT user=0              # Attach virtual controller

# Navigate title screen
3.0 PRESS A                     # Skip intro video
3.2 RELEASE A

5.0 WAIT                        # Let menu load

# Select "Play Now"
6.0 PRESS DPAD_DOWN
6.2 RELEASE DPAD_DOWN
6.5 PRESS A
6.7 RELEASE A

# Select first song
8.0 PRESS A
8.2 RELEASE A

# Select difficulty (Easy)
10.0 PRESS A
10.2 RELEASE A

# Song should start at ~t=12s
12.0 CHECKPOINT song_started
```

## Implementation Tasks

### Phase 1: MVP (8-10 hours)

#### Task 1.1: Create ScriptedInputDriver Class (3 hours)

**Files to create:**
- `xenia/src/xenia/hid/scripted/scripted_input_driver.h`
- `xenia/src/xenia/hid/scripted/scripted_input_driver.cc`
- `xenia/src/xenia/hid/scripted/scripted_hid.h`
- `xenia/src/xenia/hid/scripted/scripted_hid.cc`
- `xenia/src/xenia/hid/scripted/premake5.lua`

**Key components:**
```cpp
class ScriptedInputDriver : public InputDriver {
  struct InputEvent {
    double timestamp;
    enum Type { CONNECT, DISCONNECT, PRESS, RELEASE, WAIT } type;
    uint32_t user_index;
    uint16_t buttons;
  };

  std::queue<InputEvent> events_;
  std::chrono::steady_clock::time_point start_time_;
  bool connected_[4] = {false};
  uint32_t packet_number_[4] = {0};
  X_INPUT_GAMEPAD gamepad_[4] = {};

  bool LoadScript(const std::string& path);
  void UpdateState();  // Process events up to current time
};
```

**Acceptance criteria:**
- [ ] Class compiles and links into xenia-headless
- [ ] Implements InputDriver interface (GetState, GetCapabilities, etc.)
- [ ] Maintains per-user controller state
- [ ] Updates state based on elapsed time

---

#### Task 1.2: Implement Script Parser (3 hours)

**Parser features:**
```cpp
bool ScriptedInputDriver::LoadScript(const std::string& path) {
  // Parse lines: "timestamp ACTION [params]"
  // Support actions:
  //   - CONNECT user=N
  //   - DISCONNECT user=N
  //   - PRESS button
  //   - RELEASE button
  //   - WAIT
  // Store events in priority queue sorted by timestamp
}
```

**Button name mapping:**
```cpp
uint16_t ParseButton(const std::string& name) {
  if (name == "A") return X_INPUT_GAMEPAD_A;
  if (name == "B") return X_INPUT_GAMEPAD_B;
  if (name == "X") return X_INPUT_GAMEPAD_X;
  if (name == "Y") return X_INPUT_GAMEPAD_Y;
  if (name == "START") return X_INPUT_GAMEPAD_START;
  if (name == "BACK") return X_INPUT_GAMEPAD_BACK;
  if (name == "DPAD_UP") return X_INPUT_GAMEPAD_DPAD_UP;
  // ... etc
}
```

**Acceptance criteria:**
- [ ] Parses script file with timestamp + action format
- [ ] Supports all Xbox 360 buttons (A, B, X, Y, bumpers, D-pad, Start, Back)
- [ ] Handles comments (lines starting with #)
- [ ] Validates timestamps are monotonically increasing
- [ ] Reports parse errors with line numbers

---

#### Task 1.3: Integrate into Xenia Headless (2 hours)

**Files to modify:**
- `xenia/src/xenia/app/emulator_headless.cc`
- `xenia/src/xenia/hid/hid_flags.cc`
- `xenia/src/xenia/hid/hid_flags.h`

**Add command-line flag:**
```cpp
// hid_flags.cc
DEFINE_string(input_script, "",
              "Path to input script file for automated testing in headless mode.",
              "HID");
```

**Load driver in headless setup:**
```cpp
// emulator_headless.cc
void EmulatorHeadless::Setup() {
  // ...

  auto script_path = cvars::input_script;

  if (!script_path.empty()) {
    auto scripted = std::make_unique<hid::scripted::ScriptedInputDriver>(
        nullptr, 0);
    if (scripted->LoadScript(script_path)) {
      emulator_->input_system()->AddDriver(std::move(scripted));
      XELOGI("Using scripted input from: {}", script_path);
    } else {
      XELOGE("Failed to load input script: {}", script_path);
    }
  } else {
    auto nop = std::make_unique<hid::nop::NopInputDriver>(nullptr, 0);
    emulator_->input_system()->AddDriver(std::move(nop));
  }
}
```

**Acceptance criteria:**
- [ ] `--input_script` flag recognized by xenia-headless
- [ ] ScriptedInputDriver loaded when flag is provided
- [ ] Falls back to NopInputDriver when no script specified
- [ ] Error messages if script file not found or invalid

---

#### Task 1.4: Create Test Scripts (1 hour)

**Create test script directory:**
```bash
mkdir -p scripts/xenia-inputs/
```

**Basic connection test:**
```bash
# scripts/xenia-inputs/01_connect.txt
0.0 CONNECT user=0
5.0 WAIT
```

**Title screen navigation test:**
```bash
# scripts/xenia-inputs/02_title_screen.txt
0.0 CONNECT user=0
3.0 PRESS A
3.2 RELEASE A
5.0 CHECKPOINT main_menu
```

**Song start test (Dance Central 3):**
```bash
# scripts/xenia-inputs/dc3_song_start.txt
0.0 CONNECT user=0

# Skip intro video
3.0 PRESS A
3.2 RELEASE A

# Wait for main menu
5.0 WAIT

# Navigate to "Play Now"
6.0 PRESS DPAD_DOWN
6.2 RELEASE DPAD_DOWN
6.5 PRESS A
6.7 RELEASE A

# Wait for song list
8.0 WAIT

# Select first song
9.0 PRESS A
9.2 RELEASE A

# Wait for difficulty select
10.0 WAIT

# Select Easy
11.0 PRESS A
11.2 RELEASE A

# Song should load
12.0 CHECKPOINT song_loading
15.0 CHECKPOINT song_started
```

**Acceptance criteria:**
- [ ] At least 3 test scripts created
- [ ] Scripts cover: connection, title screen, song start
- [ ] Scripts documented with comments explaining each step
- [ ] Scripts stored in `scripts/xenia-inputs/` directory

---

#### Task 1.5: End-to-End Testing (1 hour)

**Test with original XEX:**
```bash
cd /tmp/claude/xenia/build/bin/Linux/Checked
./xenia-headless \
  --target=/home/free/code/milohax/dc3-decomp/orig/373307D9/default.xex \
  --input_script=/home/free/code/milohax/dc3-decomp/scripts/xenia-inputs/dc3_song_start.txt \
  --headless_timeout_ms=30000 \
  --log_level=1 \
  2>&1 | tee original_song_start.log
```

**Test with decompiled XEX:**
```bash
./xenia-headless \
  --target=/home/free/code/milohax/dc3-decomp/build/373307D9/default.xex \
  --input_script=/home/free/code/milohax/dc3-decomp/scripts/xenia-inputs/dc3_song_start.txt \
  --headless_timeout_ms=30000 \
  --log_level=1 \
  2>&1 | tee decomp_song_start.log
```

**Compare outputs:**
```bash
diff -u original_song_start.log decomp_song_start.log
```

**Acceptance criteria:**
- [ ] Both XEXs execute without crashes
- [ ] Logs show controller connection at t=0
- [ ] Logs show input events being processed
- [ ] Behavior comparison shows identical or explainable differences
- [ ] Document any divergences found

---

### Phase 2: Advanced Features (10-15 hours)

#### Task 2.1: Analog Stick Support (2 hours)

**Extend script format:**
```
# Analog stick movement (-32768 to 32767)
5.0 STICK_LEFT x=16000 y=0      # Half right
5.5 STICK_LEFT x=0 y=-16000     # Half down
6.0 STICK_LEFT x=0 y=0          # Center

# Trigger support (0-255)
7.0 TRIGGER_LEFT value=128      # Half pressed
7.5 TRIGGER_LEFT value=255      # Full pressed
```

**Implementation:**
```cpp
struct InputEvent {
  // ... existing fields ...
  int16_t thumb_lx, thumb_ly, thumb_rx, thumb_ry;
  uint8_t left_trigger, right_trigger;
};
```

---

#### Task 2.2: Memory Checkpoint Support (3 hours)

**Script syntax:**
```
10.0 CHECKPOINT song_started
```

**Implementation:**
```cpp
case InputEvent::CHECKPOINT:
  XELOGI("CHECKPOINT: {}", event.checkpoint_name);
  // Optional: dump memory region
  if (cvars::dump_checkpoints) {
    emulator_->DumpMemoryRange(
      0x82000000, 0x83000000,
      fmt::format("checkpoint_{}.bin", event.checkpoint_name));
  }
  break;
```

**Acceptance criteria:**
- [ ] Checkpoints logged with timestamp
- [ ] Optional memory dump at checkpoint
- [ ] Checkpoint names visible in test output

---

#### Task 2.3: Assertion Framework (4 hours)

**Script syntax:**
```
# Assert game is still running (no crash)
10.0 ASSERT_RUNNING

# Assert memory value
10.0 ASSERT_MEMORY addr=0x82100000 value=0x12345678

# Assert controller is connected
0.5 ASSERT_CONNECTED user=0
```

**Implementation:**
```cpp
case InputEvent::ASSERT_RUNNING:
  if (!emulator_->is_running()) {
    XELOGE("ASSERTION FAILED: Game crashed before t={:.2f}s", elapsed);
    exit(1);
  }
  break;

case InputEvent::ASSERT_MEMORY:
  uint32_t actual = emulator_->ReadMemory<uint32_t>(event.address);
  if (actual != event.expected_value) {
    XELOGE("ASSERTION FAILED at t={:.2f}s: "
           "Memory[0x{:08X}] = 0x{:08X}, expected 0x{:08X}",
           elapsed, event.address, actual, event.expected_value);
    exit(1);
  }
  break;
```

**Acceptance criteria:**
- [ ] ASSERT_RUNNING detects crashes
- [ ] ASSERT_MEMORY validates memory values
- [ ] ASSERT_CONNECTED validates controller state
- [ ] Failed assertions exit with error code
- [ ] Assertions logged to console

---

#### Task 2.4: Input Recording (6 hours)

**Record inputs from full Xenia playthrough:**

```cpp
// In full Xenia's InputDriver
X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  X_RESULT result = RealGetState(user_index, out_state);

  if (cvars::record_inputs) {
    RecordInputState(user_index, out_state);
  }

  return result;
}

void RecordInputState(uint32_t user_index, X_INPUT_STATE* state) {
  static auto start_time = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration<double>(now - start_time).count();

  // Detect button changes
  static X_INPUT_GAMEPAD prev_state[4] = {};

  uint16_t pressed = state->gamepad.buttons & ~prev_state[user_index].buttons;
  uint16_t released = ~state->gamepad.buttons & prev_state[user_index].buttons;

  if (pressed) {
    fprintf(record_file_, "%.3f PRESS %s\n", elapsed, ButtonToString(pressed));
  }
  if (released) {
    fprintf(record_file_, "%.3f RELEASE %s\n", elapsed, ButtonToString(released));
  }

  prev_state[user_index] = state->gamepad;
}
```

**Usage:**
```bash
# Record a playthrough
xenia --target=game.xex --record_inputs=recorded.txt

# Replay in headless mode
xenia-headless --target=game.xex --input_script=recorded.txt
```

**Acceptance criteria:**
- [ ] `--record_inputs` flag in full Xenia
- [ ] Records all button presses/releases with timestamps
- [ ] Output file is valid script format
- [ ] Recorded script replays correctly in headless mode

---

## Testing Strategy

### Unit Tests

Create unit tests for script parser:

```cpp
// xenia/src/xenia/hid/scripted/scripted_input_driver_test.cc
TEST(ScriptedInputDriver, ParsesBasicScript) {
  ScriptedInputDriver driver(nullptr, 0);
  ASSERT_TRUE(driver.LoadScript("test_scripts/basic.txt"));
  // Validate events loaded correctly
}

TEST(ScriptedInputDriver, RejectsInvalidTimestamp) {
  ScriptedInputDriver driver(nullptr, 0);
  ASSERT_FALSE(driver.LoadScript("test_scripts/invalid_time.txt"));
}
```

### Integration Tests

```bash
#!/bin/bash
# tests/scripted_input_tests.sh

echo "Test 1: Controller connection"
xenia-headless \
  --target=build/373307D9/default.xex \
  --input_script=scripts/xenia-inputs/01_connect.txt \
  --headless_timeout_ms=10000 2>&1 | grep "Controller 0 connected"

if [ $? -eq 0 ]; then
  echo "✓ Test 1 passed"
else
  echo "✗ Test 1 failed"
  exit 1
fi

echo "Test 2: Title screen navigation"
# ... more tests
```

### Regression Tests

Compare original vs decompiled behavior:

```bash
#!/bin/bash
# tests/compare_behavior.sh

SCRIPT="scripts/xenia-inputs/dc3_song_start.txt"

echo "Running original XEX..."
xenia-headless --target=orig/373307D9/default.xex \
  --input_script=$SCRIPT 2>&1 > /tmp/original.log

echo "Running decompiled XEX..."
xenia-headless --target=build/373307D9/default.xex \
  --input_script=$SCRIPT 2>&1 > /tmp/decomp.log

echo "Comparing logs..."
diff -u /tmp/original.log /tmp/decomp.log

if [ $? -eq 0 ]; then
  echo "✓ Behavior matches!"
else
  echo "⚠ Behavior diverges - review diff above"
fi
```

## Success Criteria

### MVP Success (Phase 1)

- [ ] ScriptedInputDriver compiles and integrates into xenia-headless
- [ ] Can load script files with button press/release events
- [ ] Game sees virtual controller as connected
- [ ] Button states update based on script timestamps
- [ ] Both original and decompiled XEX execute same script without crashes
- [ ] Can navigate past title screen in headless mode

### Full Success (Phase 2)

- [ ] Analog stick and trigger support working
- [ ] Memory checkpoints capture game state
- [ ] Assertions validate expected behavior
- [ ] Input recording from full Xenia playthrough works
- [ ] **Can start a song in Dance Central 3 in headless mode**
- [ ] Automated regression tests in CI/CD

## Dependencies

### Required

- Xenia source code (already available at `/tmp/claude/xenia`)
- C++ build environment (gcc/clang with C++17)
- premake5 (for Xenia build system)

### Optional

- Full Xenia with rendering (for input recording feature)
- CI/CD system (GitHub Actions, Jenkins, etc.) for automated testing

## Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Timing issues (events fire too early/late) | High | Medium | Add configurable speed multiplier |
| Game doesn't respond to virtual controller | High | Low | Verify controller capabilities match real device |
| Script parsing bugs crash emulator | Medium | Medium | Thorough unit tests, error handling |
| Recorded inputs don't replay correctly | Medium | Medium | Validate recording format, add replay verification |

## Future Enhancements

### Multi-Controller Support

```
# Two players
0.0 CONNECT user=0
0.0 CONNECT user=1
3.0 PRESS A user=0
3.0 PRESS A user=1
```

### Kinect Skeleton Input

```
# Simulate Kinect skeleton tracking
5.0 KINECT_TRACK user=0 x=0.5 y=1.2 z=2.0
5.0 KINECT_GESTURE user=0 name="wave_right"
```

### Visual Assertions (with rendering)

```
10.0 ASSERT_SCREENSHOT hash=abc123def456  # Compare frame hash
```

## Timeline Estimate

| Phase | Duration | Calendar Time |
|-------|----------|---------------|
| Phase 1 (MVP) | 8-10 hours | 2-3 days (part-time) |
| Phase 2 (Advanced) | 10-15 hours | 3-4 days (part-time) |
| Testing & Polish | 5 hours | 1 day |
| **Total** | **23-30 hours** | **6-8 days (part-time)** |

## References

- [Scripted Input Testing Design Doc](../runtime/SCRIPTED_INPUT_TESTING.md) - Detailed design
- [Boot Analysis](../runtime/BOOT_ANALYSIS.md) - Current boot progress
- [Xenia Input System](https://github.com/xenia-project/xenia/tree/master/src/xenia/hid) - Source code
- [Xbox 360 XInput API](https://docs.microsoft.com/en-us/windows/win32/api/xinput/) - Reference

## Next Actions

1. **Decide priority** - Is this work valuable right now, or should we first test with full Xenia (with GPU)?
2. **Assign owner** - Who will implement this?
3. **Set up development environment** - Ensure Xenia builds successfully
4. **Start with Task 1.1** - Create ScriptedInputDriver skeleton class
5. **Iterate** - Build incrementally, test each component

---

**Document Status:** Ready for implementation
**Last Updated:** 2026-02-18
**Next Review:** After Phase 1 MVP completion
