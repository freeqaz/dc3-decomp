---
name: xenia-gameplay
description: Run DC3's original Xbox 360 debug XEX on Xenia (Linux/Vulkan) and navigate to gameplay. Captures headless frames of menus, song select, loading, and in-game venues with 3D characters. Use when testing original binary behavior, capturing IK telemetry, or comparing Xbox vs native rendering.
argument-hint: "[song-script] [capture-interval] [timeout-ms]"
allowed-tools: Bash, Read, Glob, Write
---

# Xenia Gameplay Skill

**All Bash calls MUST use `dangerouslyDisableSandbox: true`.** Vulkan requires GPU/ICD access.

Full docs: [docs/debugging/xenia.md](docs/debugging/xenia.md)

## Quick Start — Boot to YMCA Gameplay

```bash
XENIA=/home/free/code/milohax/xenia/build/bin/Linux/Checked/xenia-headless
DC3=/home/free/code/milohax/dc3-decomp

mkdir -p /tmp/xenia-gameplay
$XENIA \
  --target=$DC3/orig-assets/debug.xex \
  --gpu=vulkan \
  --dc3_nui_patch_layout=original \
  --dc3_crt_skip_nui=true \
  --fake_kinect_data=true \
  --scripted_input_file=$DC3/scripts/dc3-input-flows/xenia-ymca.txt \
  --dump_frames_path=/tmp/xenia-gameplay \
  --headless_capture_interval=300 \
  --headless_timeout_ms=180000 2>&1 | tee /tmp/xenia-gameplay.log
```

Convert frames:
```bash
for f in /tmp/xenia-gameplay/frame_*.ppm; do
  [[ "$f" == *_raw.ppm ]] && continue
  magick "$f" "${f%.ppm}.png"
done
```

## Required Flags

| Flag | Why |
|------|-----|
| `--fake_kinect_data=true` | Enables calibration bypass (7 patches for player assignment) |
| `--scripted_input_file=` | Screen-aware input script — waits for each screen before pressing buttons |
| `--dump_frames_path=` | Activates headless frame capture (`headless_frame_dump_` flag) |
| `--gpu=vulkan` | Actual GPU rendering (vs `null` for no rendering) |

## Screen-Aware Input Scripts

Scripts use the native port's format (`scripts/dc3-input-flows/`):

```
wait_screen attract_screen
+10 confirm

wait_screen main_screen
+10 confirm
```

The NopInputDriver reads `TheUI->mCurrentScreen->mName` from guest memory to detect screen transitions. `+N` values are frame-relative delays (N × 50ms).

Available scripts:
- `scripts/dc3-input-flows/xenia-ymca.txt` — full flow to YMCA gameplay

## Game Flow

```
attract_screen → A (skip)
title_screen → A
main_screen → A (Dance)
choose_mode_screen → A (Perform)
song_select_screen → DOWN×3, A (YMCA)
seldiff_screen → A (difficulty — overlay on song_select)
multiuser_screen → auto-advances (calibration bypass)
loading_screen → auto
game_screen → gameplay!
```

## Capture Tips

- `--headless_capture_interval=300` captures every 300th VdSwap (~5s at 60fps)
- Lower intervals (100) give more frames but slow the game
- First ~5 VdSwaps are always black (game still booting)
- Frame filenames: `frame_NNNN.ppm` (gamma-corrected) + `frame_NNNN_raw.ppm` (linear)

## Known Issues

- Venue lighting is dark during gameplay (Xenia shader translation gaps)
- XMA audio SIGSEGV at `0x82E7732C` — non-fatal, no audio playback
- Debug text overlays visible (expected for debug XEX)

## Key Addresses (Original XEX)

| Address | What |
|---------|------|
| `0x82F1A8E0` | `TheUI` (UIManager* global) |
| `+0x48` | UIManager.mCurrentScreen (UIScreen*) |
| `+0x20` | UIScreen.mName (char*) |
| `0x82F60034` | `TheGameData` (HamGameData* global) |
| `0x82942EB8` | `MultiUserGesturePanel::Poll` (patched by calibration bypass) |

## Reference Screenshots

- `archive/screenshots/references/dc3_gameplay_ui.jpg` — target gameplay look
- `archive/screenshots/xenia-gameplay/` — captured Xenia frames
