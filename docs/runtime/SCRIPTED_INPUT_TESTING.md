# Scripted Input Testing for Xenia Headless

## Status: IMPLEMENTED

Scripted input is implemented directly in `NopInputDriver` via the `--scripted_input` CLI flag. No separate driver was needed.

## Usage

```bash
# Basic: press A at 5s, START at 7s
./xenia-headless --gpu=vulkan \
    --target=orig/373307D9/default.xex \
    --scripted_input='5s:A,7s:START,10s:A' \
    --dump_frames_path=/tmp/frames/ \
    --headless_capture_interval=100 \
    --headless_timeout_ms=30000

# Multiple buttons, timing sequences
--scripted_input='3s:A,5s:START,8s:A,10s:DPAD_DOWN,12s:A,15s:A'
```

### Format

Comma-separated `TIME:BUTTON` pairs:
- **TIME**: seconds from boot, e.g. `5s`, `10.5s`
- **BUTTON**: Xbox controller button name

### Supported Buttons

| Button | Description |
|--------|-------------|
| `A` | A button |
| `B` | B button |
| `X` | X button |
| `Y` | Y button |
| `START` | Start button |
| `BACK` | Back button |
| `DPAD_UP` | D-pad up |
| `DPAD_DOWN` | D-pad down |
| `DPAD_LEFT` | D-pad left |
| `DPAD_RIGHT` | D-pad right |
| `LB` / `RB` | Left/right bumper |
| `LS` / `RS` | Left/right stick press |

## Implementation

**File:** `src/xenia/hid/nop/nop_input_driver.cc`

The NopInputDriver was extended to:
1. Parse `--scripted_input` at construction
2. Report controller as connected (returns `X_ERROR_SUCCESS` instead of `X_ERROR_DEVICE_NOT_CONNECTED`)
3. Fire button presses at specified timestamps (200ms press duration)
4. Return proper `X_INPUT_STATE` with button flags and packet numbers

### How It Works

```
NopInputDriver::GetState(user_index=0)
│
├─ Check elapsed time since boot
├─ For each scripted event:
│   ├─ If current_time >= event_time && current_time < event_time + 200ms:
│   │   └─ Set button bit in gamepad.buttons
│   └─ Otherwise: button released
├─ Return X_INPUT_STATE with current button state
└─ Return X_ERROR_SUCCESS (controller connected)
```

## DC3 Boot Screen Navigation

### What We Know

The boot animation runs frames 1-300. After the DC logo animation:
- Frame 275: Card logo tilting/shrinking
- Frame 300+: Screen transition (dark/red)
- The game likely waits for A or START to advance to menus

### Testing Strategy

1. **Baseline** — run without input, capture every 100 frames, see what screens appear
2. **Try A/START** — press at various times after frame 300 (10s+)
3. **Iterate** — narrow down timing based on captured screenshots

### Blocked By

Rendering quality regression — deferred draws currently produce wrong colors (B=0x3F). Need to fix rendering before input testing is meaningful. See [XENIA_HEADLESS_STATUS.md](XENIA_HEADLESS_STATUS.md).

## Future Enhancements

These were planned but not yet needed:

- **Analog sticks** — SET_STICK with X/Y values
- **Triggers** — SET_TRIGGER with pressure values
- **Script files** — load from file instead of CLI arg
- **Memory checkpoints** — dump game state at specific points
- **Input recording** — record from full Xenia playthrough
