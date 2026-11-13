# Native port

DC3 running natively on Linux x86_64, macOS arm64 and in the browser
(Emscripten + WebGPU), built from the decompiled `src/` plus the shared engine
in `../milo-native-engine` (rendering, audio, input, file I/O).

| Page | What it is |
|---|---|
| [PLAN.md](PLAN.md) | Roadmap to a playable game, by track (pose, gameplay, web, macOS, hygiene) |
| [STATUS.md](STATUS.md) | What was measured to work, dated |
| [PLAYTEST.md](PLAYTEST.md) | Playtest runbook: set a box up, `scripts/play.sh`, where to stand, what to send back |
| [POSE_INPUT.md](POSE_INPUT.md) | Webcam → Kinect skeleton → scoring |
| [WEB.md](WEB.md) | Web build, runtime, test scripts, deployment |
| [MACOS.md](MACOS.md) | macOS state and bring-up |
| [TESTING.md](TESTING.md) | `scripts/native_test.sh`, gates, exit codes, skip budget |
| [SCORING_ENV_VARS.md](SCORING_ENV_VARS.md) | Scoring and pose env vars |
| [SAVE_PERSISTENCE_PLAN.md](SAVE_PERSISTENCE_PLAN.md) | Save/cache backend and its on-disk contract |
| [reference/](reference/) | Rendering, audio/video, animation, loading and boot, porting, viewer, live-state probing, code style |
| [../debugging/native.md](../debugging/native.md) | Debugging, and the master env-var table |

## Linux quick start

```bash
scripts/native_configure.sh                  # finds Dawn (dc3-decomp-deps), clang; exit 9 names what is missing
cmake --build native/build --target dc3-native
scripts/native_test.sh                       # builds first, then the gate; read its printed numbers

scripts/dc3-agent-test.sh --windowed         # play: HTTP debug server on 127.0.0.1:9090, fast boot, telemetry
scripts/dc3-agent-test.sh --headless         # agent/CI runs; drive with /api/input/press
```

A playtest (a person dancing in front of a webcam, Linux): `scripts/play.sh`;
setup and the runbook are in [PLAYTEST.md](PLAYTEST.md).

macOS: `scripts/mac/bootstrap.sh --fix` checks and installs the prerequisites,
builds and launches; see [MACOS.md](MACOS.md).

Keyboard (windowed; printed once at startup as `DC3 Native: keys: ...`):

| Key | Button | | Key | Button |
|---|---|---|---|---|
| Arrows | D-pad | | Space | Start |
| Enter | A | | Tab | Back |
| Esc, Backspace | B | | Q / E | LB / RB |
| X | X | | Z / C | LT / RT |
| Y | Y (Options on the shell help bar) | | **Ctrl+Q** | **quit** |

Esc is B, not quit: quit with Ctrl+Q or by closing the window (both rc 0).
Space opens the pause menu but does not close it, as on the 360 (PLAN G-13);
resume with Continue. A key held with Ctrl, Alt or Super is not a pad press.
Debug keys: `` ` `` toggles the ImGui panel, F12 captures a frame. The map is
the engine's (`../milo-native-engine/src/platform/JoypadKeyMap.h`); rb3 has its
own keyboard. A GLFW gamepad gets the full Xbox layout. Saves go to
`DC3_SAVE_DIR` (default `~/.local/share/dc3/saves`).

### Profiles: dev by default

A plain `dc3-native` (or the web page) runs the **dev profile**: the developer
Options rows, the original debug build's dev menu, its on-screen overlays, the
HTTP debug server on 127.0.0.1, the quiet telemetry API, a rate-limited
`MILO_NOTIFY`/`WARN`/`FAIL` console, the input, move-rating and frame-time logs.
`DC3_PROFILE=release` (web `?profile=release`) gives the player defaults. Any
single variable still overrides either profile (`DC3_HTTP=0`,
`DC3_DEV_OPTIONS=1`, ...). Nothing in the dev profile changes scoring, timing,
song content or the screens you see; fast boot, fast time and the synthetic
performer stay opt-in. Full table and reasons:
[debugging/native.md "Profiles"](../debugging/native.md#profiles-dc3_profile).

| `DC3_PROFILE` | Options rows | Dev menu | Overlays | HTTP server | Console |
|---|---|---|---|---|---|
| `dev` (default) | retail + Camera Blend, Debug Overlay | on | on | 127.0.0.1, 9090 if free in a window | `MILO_<KIND>:` lines, `Joypad:`, `DC3 MOVE_RATING`, `[PERF]` |
| `release` | retail only | off | off | off | quiet |

Pose input starts automatically on a windowed boot; see
[POSE_INPUT.md](POSE_INPUT.md) for running it with a camera, a video file or
the synthetic performer.

History: the docs this set replaced are in
[../archive/2026-11-13-native-doc-audit/](../archive/2026-11-13-native-doc-audit/MANIFEST.md).
