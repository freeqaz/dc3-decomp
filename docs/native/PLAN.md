# Native port — roadmap to playable

**Goal:** a person stands in front of a webcam and plays Dance Central 3 —
menus, a whole song, believable scoring, results, saves — on Linux, macOS and in
the browser.

Measured state: [STATUS.md](STATUS.md) (2026-11-13). Each item names its owning
files and the check that proves it done. An item is done when its check passes,
not when code lands. When an item closes, record the commit next to it; when the
whole track closes, collapse it to one line.

Size: S < half a day · M ≈ 1–2 days · L = several days.

## Milestones

1. **M1 — Dance on Linux.** P-1, P-2, P-5, P-6, G-5 done: a real webcam plays a
   whole Perform song with no crash, and a deterministic replay test guards it.
2. **M2 — Dance in the browser.** P-9, W-2, W-3 done: the page asks for the
   camera, tracks the player, and the score responds.
3. **M3 — Whole game by dancing.** P-3, P-4, P-7, G-1..G-4 done: gesture menus,
   the player on screen, calibrated scoring, and every mode reachable.
4. **M4 — Mac.** Track M done, verified on the owner's Mac.

## Track P — pose input ([POSE_INPUT.md](POSE_INPUT.md))

| # | Item | Size | Done when |
|---|---|---|---|
| P-1 | ~~Motionless-skeleton SIGSEGV~~ **DONE 2026-11-13 (`np-fix-still`)**: zero variance in `RhythmDetector::AnalyzeData` → NaN → `SymbolKeys::SetFrame` past the end. The image has the same hazard; a Kinect never repeats a joint bit for bit, so the native sensor now adds a 1 µm noise floor to every delivered joint (`Skeleton_Native.cpp` `SensorNoiseFloor`), covering the confidence hold and the 2D providers' constant z. Test: `NativeSensorNoiseFloorTest`. | S–M | Still-skeleton run completes the song; a regression test fails without the fix |
| P-2 | Live webcam on Linux: camera mode of `pose_server.py`, wall-clock timestamps, HFOV, a visible "no camera" message instead of a silent 5 s stall | S–M | Owner plays a song on a webcam; no-camera boot shows a message |
| P-3 | Gesture menus: `DC3_CONTROLLER_MODE=faithful` path, hand meshes unhidden in `native/src/platform/MeshFilter.cpp`, swipe poses in `scripts/synthetic_kinect.py` | M–L | A synthetic performer navigates main menu → song select → song with no button presses |
| P-4 | Player on screen: native `LiveCameraInput` stand-in fed webcam RGB + segmentation mask; `StreamRenderer`, `DepthBuffer3D`, `MeshFilter` | L | Silhouette/preview visible in gameplay screenshots |
| P-5 | Tests: ctest for the socket reader, COCO mapping and `AssignSlots`; deterministic keypoint-replay fixture keyed on song time; gate covers the whole song and more than maximums | S–M | New tests sabotage-checked |
| P-6 | Remove the dead ncnn/YOLO path, its log line and the tracked `native/yolo11n-pose.pt` | S | Windowed boot tries the MediaPipe server first; weights gone from the tree |
| P-7 | Scoring believability: calibrate on a recorded human; webcam latency knob vs Kinect-tuned `latency_offset` in `config/scoring.dta` | M | Standing still no longer earns "awesome"; a recorded human gets a plausible spread |
| P-8 | macOS pose: `_NSGetExecutablePath`, camera permission for the Python child | S–M | Pose server starts on the owner's Mac |
| P-9 | Web pose: provider seam + exported `dc3_pose_push`, MediaPipe tasks-vision + getUserMedia, geometry ported from `pose_mediapipe.py`. **In progress: `np-web-pose`.** | M–L | Fake-camera dance clip, still person and empty frame score differently in Chromium |

## Track G — gameplay and modes (desktop findings, also apply to web)

| # | Item | Size | Done when |
|---|---|---|---|
| G-1 | Battle / Party / Crew need a second skeleton: dummy and providers feed only slot 0, so `pause_on_skeleton_loss` pauses at ~beat 4 | M | Battle with two synthetic performers reaches results |
| G-2 | Story re-shows the Perform tutorial on a profile that has seen it | S–M | Story from a saved profile skips the tutorial |
| G-3 | Story `campaign_perform_endgame_screen` never advances | S–M | Story song → endgame → next screen |
| G-4 | Fitness: find how a playlist starts; fix if it is a bug | S | Fitness song starts, or documented as navigation |
| G-5 | Windowed keys: Space to toggle pause, a quit key that matches the startup log; X/Y/LT/RT bindings (`milo-native-engine/src/platform/Joypad_Native.cpp`, shared with rb3) | S | Manual check in a window |
| G-6 | Black box over the final-results header and empty centre panel | M | Final-results screenshot matches the Xbox layout |
| G-7 | Controller-mode help bar stays on in gameplay; "raise both hands" bubble on most menus, doubled in party | S–M | Screenshots match Xbox behaviour |
| G-8 | Title/main background purple vs blue (`archive/screenshots/references/dc3_main_menu.jpg`) | M | Side-by-side matches |
| G-9 | Missing glyphs: `S` in Vibration-Medium (`results_cluster.milo` `score_fmt`), `%`/`f` in Eagle-Light | S | No missing-glyph NOTIFY lines |
| G-10 | Audio verified by ear on Linux; Options and store screens reached | S | Owner confirms; screenshots of both |

## Track W — web ([WEB.md](WEB.md))

| # | Item | Size | Done when |
|---|---|---|---|
| W-1 | Pose input = P-9 | — | — |
| W-2 | `scripts/web/*.mjs` launch a WebGPU-capable browser on this box (Playwright vs headless-shell version). In `np-web-pose`. | S | `smoke-test.mjs` runs from a worktree |
| W-3 | Swap the release/debug optimisation levels back: the default page is `-O0` | S | `flags.make` shows `-O3` for release |
| W-4 | Configure clones imgui with full history per build dir and can hang; `index.html` not copied if the debug half fails | S | Offline/shallow fetch; `index.html` always deployed |
| W-5 | Public deployment: stale build and unauthenticated asset API. **Owner decision.** | — | Owner's call |
| W-6 | Heap headroom: ~460 MB of 512 MB in one venue; measure all venues, then raise the cap or trim | M | Max heap over every venue recorded |
| W-7 | ~20 missing SFX sidecars under `sfx/gen/xma_pcm/` | S | No 404s in a full-song run |
| W-8 | Player-facing page: hide the dev console, pause/step buttons and status bar behind `?dev` | S | Default page shows only the game |

## Track M — macOS ([MACOS.md](MACOS.md))

| # | Item | Size | From Linux? |
|---|---|---|---|
| M-1 | Generate the libc++ `wrap_iter.h` shim at configure time | M | yes, then confirm on Mac |
| M-2 | Move the Apple `-fms-compatibility`/PCH exemption to the engine files; delete dc3's dead gfx/audio copies (H-1) | S | yes (engine change: heads-up first) |
| M-3 | `malloc_usable_size` / `<malloc.h>` in `Dir.cpp` and a test | S | yes |
| M-4 | Remove the 22 duplicate definitions; gate "links without `--allow-multiple-definition`" on Linux | M | yes |
| M-5 | Darwin branch in `scripts/native_configure.sh` (Dawn prebuilt, `BUILD_TESTS=OFF`) | S | yes |
| M-6 | `/proc/self/exe` and `/proc/self/statm` uses | S | yes |
| M-7 | First build and windowed run on the owner's Mac; then a macOS CI job | M | no |

## Track H — hygiene that unblocks the above

| # | Item | Size |
|---|---|---|
| H-1 | Delete dead dc3 copies `native/src/gfx/*`, `native/src/platform/{Rnd,Mesh,Tex,Part}_Wgpu.cpp`, the unused source lists, and the wrong CMake comment near the `dc3-web` target | S |
| H-2 | Fix or delete `wgpu-window-test` | S |
| H-3 | GitHub `native-build` red in 36 of 40 runs: find the cause | S–M |
| H-4 | `milo_viewer.cpp` sets `MILO_RENDER=0`, which the engine reads as "set", so GPU is forced on | S |
| H-5 | `FFmpegMovieImpl.cpp` compiled from both repos: possible duplicate | S |
| H-6 | Crowd impostors cached once per character, so the crowd never animates | M |
| H-7 | `native_assert_harvest.py` stalls on tutorial screens | S |
| H-8 | CLAUDE.md and the CMake comment call the gameplay gate "one ~2 min run"; it is ~50 min | S |

## Owner decisions pending

- Public web deployment (W-5).
- Which Mac / macOS / Xcode; Metal Dawn still installed or use Google's prebuilt; Python pose server acceptable on Mac (M-7, P-8).
- Shared-engine changes (G-5, M-2, M-4) need a heads-up to the rb3 owner before landing.
