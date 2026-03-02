# Headless Testing — dc3-native

Run dc3-native without a window using scripted button presses and automatic screenshots to verify UI state.

## Quick Start

```bash
# 1. Create an input script
cat > /tmp/nav.txt << 'EOF'
# Skip attract screen
60 start
# Navigate main menu
200 down
230 confirm
EOF

# 2. Run headless with scripted input + screenshots
MILO_RENDER=1 MILO_HEADLESS=1 \
  MILO_INPUT_SCRIPT=/tmp/nav.txt \
  MILO_SCREENSHOT_DIR=/tmp/ui_test \
  MILO_SCREENSHOT_FRAMES=59,100,220,250 \
  timeout 60 ./native/build/dc3-native

# 3. Check captured frames
ls /tmp/ui_test/frame_*.png
```

## Environment Variables

| Variable | Required | Description |
|----------|----------|-------------|
| `MILO_RENDER` | Yes | Set to `1` to enable GPU rendering |
| `MILO_HEADLESS` | Yes | Set to `1` for headless mode (no GLFW window) |
| `MILO_INPUT_SCRIPT` | No | Path to input script file. If unset, no input is injected |
| `MILO_SCREENSHOT_DIR` | No | Directory for auto-captured screenshots |
| `MILO_SCREENSHOT_FRAMES` | No | Comma-separated frame numbers to capture (default: `100,600,900,1500`) |

## Input Script Format

Plain text, one event per line:

```
frame button    # optional comment
```

- **frame** — integer frame number (from `Rnd::GetFrameID()`)
- **button** — case-insensitive button name (see table below)
- Lines starting with `#` and blank lines are ignored
- Buttons are pressed for exactly 1 frame (press on N, auto-release on N+1)
- Events don't need to be in order — the file is sorted by frame at load time

### Button Names

| Name(s) | Milo Button | Xbox Equivalent |
|----------|-------------|-----------------|
| `start` | `kPad_Start` | Start |
| `confirm`, `a` | `kPad_X` | A |
| `cancel`, `b` | `kPad_Circle` | B |
| `x` | `kPad_Square` | X |
| `y` | `kPad_Tri` | Y |
| `up` | `kPad_DUp` | D-pad Up |
| `down` | `kPad_DDown` | D-pad Down |
| `left` | `kPad_DLeft` | D-pad Left |
| `right` | `kPad_DRight` | D-pad Right |
| `option`, `back`, `select` | `kPad_Select` | Back |
| `l1`, `lb` | `kPad_L1` | LB |
| `r1`, `rb` | `kPad_R1` | RB |
| `l2`, `lt` | `kPad_L2` | LT |
| `r2`, `rt` | `kPad_R2` | RT |
| `l3`, `ls` | `kPad_L3` | LS (left stick click) |
| `r3`, `rs` | `kPad_R3` | RS (right stick click) |

Note: Milo uses PlayStation-style internal names (`kPad_X` = confirm, `kPad_Circle` = cancel) but the Xbox 360 layout maps A to confirm and B to cancel.

## How It Works

In headless mode (`gNativeWindow == NULL`), `JoypadPoll()` reads from the script instead of GLFW:

1. On first poll, `LoadInputScript()` reads and parses the `MILO_INPUT_SCRIPT` file
2. Each frame, `GetScriptedButtons()` returns a bitmask of buttons active on that frame
3. The existing delta computation (`mNewPressed`, `mNewReleased`) handles press/release edges
4. `ButtonDownMsg` / `ButtonUpMsg` are dispatched through `JoypadPushThroughMsg` as normal

Windowed mode is completely unaffected — GLFW gamepad + keyboard fallback works as before.

## Examples

### Skip attract screen and take a screenshot

```bash
echo "60 start" > /tmp/input.txt

MILO_RENDER=1 MILO_HEADLESS=1 \
  MILO_INPUT_SCRIPT=/tmp/input.txt \
  MILO_SCREENSHOT_DIR=/tmp/shots \
  MILO_SCREENSHOT_FRAMES=59,90 \
  timeout 30 ./native/build/dc3-native
```

### Navigate a multi-step menu sequence

```
# input.txt — navigate to Play mode
60 start          # skip attract
200 confirm       # select first menu item
300 down          # move to second option
320 down          # move to third option
340 confirm       # select it
500 confirm       # confirm sub-menu
```

### Run without input (just rendering)

```bash
# No MILO_INPUT_SCRIPT = no input, just renders headlessly
MILO_RENDER=1 MILO_HEADLESS=1 \
  MILO_SCREENSHOT_DIR=/tmp/attract \
  MILO_SCREENSHOT_FRAMES=30,60,120,300 \
  timeout 20 ./native/build/dc3-native
```

## Troubleshooting

**"cannot open" error** — Check the script file path exists and is readable.

**No input seems to happen** — Verify the frame numbers in your script align with where the game actually is. The attract screen runs for a while before accepting input. Try later frame numbers.

**No screenshots captured** — Make sure `MILO_SCREENSHOT_DIR` points to an existing directory and the frame numbers match your timeline.

**Windowed mode keyboard not working** — Scripted input only applies in headless mode. In windowed mode, use arrow keys, Enter (confirm), Escape (cancel), Space (start), Tab (back).

## Implementation

Source: [`native/src/platform/Joypad_Native.cpp`](../../native/src/platform/Joypad_Native.cpp)

Screenshot capture: [`native/src/platform/Rnd_Wgpu.cpp`](../../native/src/platform/Rnd_Wgpu.cpp) (auto-screenshot setup in `WgpuRnd::Init`)
