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
| P-9 | ~~Web pose~~ **DONE 2026-11-13 (`np-web-pose`)**: `PublishFrame` seam shared with the socket path, `dc3_pose_push`, tasks-vision in a worker, geometry ported (1.8e-15 m vs Python on identical landmarks; 0.71 mm max end to end). Fake camera: dance 29,859 · still 7,240 · stand-in 7,581 · empty frame pauses. Follow-ups: real webcam. (`DetectFrac` `no-move-key` on web was the Beginner difficulty the test driver picked, not a web gap — `np-web-movekey`; web and desktop now agree per measure.) | M–L | Fake-camera dance clip, still person and empty frame score differently in Chromium |

## Track G — gameplay and modes (desktop findings, also apply to web)

| # | Item | Size | Done when |
|---|---|---|---|
| G-1 | Battle / Party / Crew need a second skeleton: dummy and providers feed only slot 0, so `pause_on_skeleton_loss` pauses at ~beat 4 | M | Battle with two synthetic performers reaches results |
| G-2 | ~~Story re-shows the Perform tutorial on a profile that has seen it~~ **Done, not a bug** (`np-modes`, `eb2b5542b`): tutorials-seen is per console, stored in the global options cache, and the surveyed run started from an empty cache dir. See [STATUS](STATUS.md#reaching-story-and-fitness) | S–M | Story from a saved profile skips the tutorial: `StoryTutorialGateSeenTest` / `FreshTest` |
| G-3 | ~~Story `campaign_perform_endgame_screen` never advances~~ **Done, not a bug** (`np-modes`): the endgame waits for a 4000 ms wall-clock passive message ("High Solo Score!"). A no-GPU headless run does 5–9k frames per second, so the frame-counting driver gave up first. See [STATUS](STATUS.md#reaching-story-and-fitness) | S–M | Story song → endgame → next song → `campaign_intro_perform_continue_screen`, unattended (headless runs, 2026-11-13) |
| G-4 | ~~Fitness: find how a playlist starts~~ **Done, navigation** (`np-modes`, `eb2b5542b`): row 0 only toggles fitness mode, and songs start from row 3 `playlists` → playlist → `playlist_play`. See [STATUS](STATUS.md#reaching-story-and-fitness) | S | Fitness playlist song reaches gameplay; `FitnessPlaylistFlowTest` |
| G-5 | Windowed keys: Space to toggle pause, a quit key that matches the startup log; X/Y/LT/RT bindings (`milo-native-engine/src/platform/Joypad_Native.cpp`, shared with rb3) | S | Manual check in a window |
| G-6 | Black box over the final-results header and empty centre panel. **Root-caused (`np-visual`); fix belongs in `../milo-native-engine`, proposed there, not landed.** The box is `mod_frame_debloom.mesh` (`center_panel_noleader.grp`, `ui/endgame/perform_endgame.milo`): blend Multiply, white, **alpha 0**. The engine fragment shader's alpha fringe fade (`standard_wgsl.inc`, `finalColor *= a / kAlphaFringeFadeStart`) takes it to black, and Multiply makes black a full darken. The image's Multiply (`NgMat`: src Zero, dst SrcColor) ignores alpha, so white leaves the frame alone, and its own `NgMat::SetupFade` fades Multiply/Darken toward (1,1,1,1). Setting the material's alpha to 1 at runtime removes the box. Proposed fix: fade toward the blend's identity, white for Multiply/Darken, through a per-pipeline WGSL `override`. Verified in a scratch engine copy. The empty centre is the authored no-LIVE layout (`center_panel.grp`, which holds the minileaderboard and score, stays hidden). | M | Final-results screenshot matches the Xbox layout |
| G-7 | Controller-mode help bar stays on in gameplay; "raise both hands" bubble on most menus, doubled in party. **Help bar closed: `1b7d936af` (`np-visual`).** Under `DC3_CONTROLLER_MODE=forced` the helpbar now leaves controller mode at the image's gameplay-panel exit and re-enters when focus leaves gameplay. GestureMgr stays pinned. Guard: `DtaFlowTest.HelpbarHiddenDuringGameplay`. **Bubble: original behaviour, no fix.** It is one `identification_failed` passive message per newly tracked, unrecognised skeleton (`SkeletonIdentifier::OnMsg(SkeletonIdentifiedMsg)`; the native `NuiIdentityIdentify` completes with no enrolment, as a stranger's would on the 360). It shows for `PassiveMessageQueue::mDisplayDurationMs` = 4 s and is never re-triggered for the same tracking id (`mIdentificationTimeout`; `NuiIdentityIdentify` hit count stays 1). The survey drove main → multiuser in ~2 s, so every shot fell inside those 4 s; the party run had two synthetic performers, hence two bubbles | S–M | Screenshots match Xbox behaviour |
| G-8 | Title/main background purple vs blue (`archive/screenshots/references/dc3_main_menu.jpg`). **Not reproduced as a bug (`np-visual`):** the original XEX under Xenia draws the same purple/blue main-menu waves (`archive/screenshots/xenia-gameplay/04_main_menu.png`), so the blue reference JPEG (a retail capture, unknown state) is not evidence of a native fault. Reopen only with a same-state side-by-side against the original binary that differs | M | Side-by-side matches |
| G-9 | Missing glyphs: `S` in Vibration-Medium (`results_cluster.milo` `score_fmt`), `%`/`f` in Eagle-Light. **Closed: original behaviour, no fix (`np-visual`).** Neither a font-loading nor a charset bug: `score_fmt` is `"%s"` and `fitness_hud_calories` is `"%.1f<alt> kCal</alt>"` (`ui/locale/eng/locale_keep.dta`), printf templates that the label shows raw at `UILabel::PostLoad` (its `caps_mode` upper-cases `%s` to `%S`) before the game formats them. The fonts are digit fonts. The formatted values draw correctly. Every function on the path is 100% (`UILabel::PostLoad`/`SetTextToken`/`SetTokenFmtImp`/`SetDisplayText`, `RndText::SetText`/`ConvertTextToWide`/`ReplaceMissingCharacters`), so the debug image notifies too (same reading as `docs/sessions/2026-09-30-native-assert-harvest.md` rows 49/52) | S | ~~No missing-glyph NOTIFY lines~~: the NOTIFY is the image's own |
| G-10 | Audio verified by ear on Linux; Options and store screens reached | S | Owner confirms; screenshots of both |

## Track W — web ([WEB.md](WEB.md))

| # | Item | Size | Done when |
|---|---|---|---|
| W-1 | Pose input = P-9 | — | — |
| W-2 | ~~Playwright version~~ **DONE (`np-web-pose`)**: `scripts/web/package.json` pins Playwright 1.60.0 (headless shell 1223); `npm ci --prefix scripts/web`. `smoke-test.mjs` PASSES from a worktree. | S | `smoke-test.mjs` runs from a worktree |
| W-3 | Swap the release/debug optimisation levels back: the default page is `-O0` | S | `flags.make` shows `-O3` for release |
| W-4 | Configure clones imgui with full history per build dir and can hang; `index.html` not copied if the debug half fails | S | Offline/shallow fetch; `index.html` always deployed |
| W-5 | Public deployment: stale build and unauthenticated asset API. **Owner decision.** | — | Owner's call |
| W-6 | Heap headroom: ~460 MB of 512 MB in one venue; measure all venues, then raise the cap or trim | M | Max heap over every venue recorded |
| W-7 | ~20 missing SFX sidecars under `sfx/gen/xma_pcm/` | S | No 404s in a full-song run |
| W-8 | Player-facing page: hide the dev console, pause/step buttons and status bar behind `?dev` | S | Default page shows only the game |

## Track M — macOS ([MACOS.md](MACOS.md))

| # | Item | Size | From Linux? |
|---|---|---|---|
| M-1 | Generate the libc++ `wrap_iter.h` shim at configure time. **Linux side done** (`0bed35b3e` generator, `CharEyes.cpp` `::pow` guard `5502e9e56`): Linux libc++ 23 proxy build compiles, links, reaches `game_screen`; emsdk web build uses the generated shim. Engine copy: scratch patch `engine-cmake-shim-apple.patch`, not landed | M | yes, then confirm on Mac |
| M-2 | Move the Apple `-fms-compatibility`/PCH exemption to the engine files; delete dc3's dead gfx/audio copies (H-1). **dc3 side done** (`6d7f2c7fa`: the `if(APPLE)` block names the engine's `MetalSurface.mm`/`GpuDevice.cpp`/`AudioDevice.cpp` via `TARGET_DIRECTORY milo-engine`). Engine-side copy of the exemption: scratch patch, not landed | S | yes (engine change: heads-up first) |
| M-3 | `malloc_usable_size` / `<malloc.h>` in `Dir.cpp` and a test. **Done** (`1edf01e06`; PPC `Dir.obj` byte-identical) | S | yes |
| M-4 | Remove the duplicate definitions (23, not 22); gate "links without `--allow-multiple-definition`" on Linux. **dc3 side done** (`ecfd3e581`): 17 removed; the 6 left are engine fallbacks, allowlisted in `native/tests/link_duplicates_allow.txt` and gated by ctest `LinkHygiene.*` (`scripts/native/check_link_duplicates.py`, fails on a new duplicate AND on a stale allowlist line). With scratch patch `engine-weaken-duplicates.patch` applied: 0 duplicates | M | yes |
| M-5 | Darwin branch in `scripts/native_configure.sh` (Dawn prebuilt, `BUILD_TESTS=OFF`). **Done** (`8db01cdd7`), checked by `MILO_CONFIGURE_PLATFORM=Darwin MILO_CONFIGURE_DRY_RUN=1`; not run on a Mac | S | yes |
| M-6 | `/proc/self/exe` and `/proc/self/statm` uses. **`/proc/self/exe` done** (`c84d7ddae`, `native/src/platform/ExecutablePath.h`, 6 sites); `/proc/self/statm` left (fails soft) | S | yes |
| M-7 | First build and windowed run on the owner's Mac; then a macOS CI job | M | no |

## Track H — hygiene that unblocks the above

| # | Item | Size |
|---|---|---|
| H-1 | Delete dead dc3 copies `native/src/gfx/*`, `native/src/platform/{Rnd,Mesh,Tex,Part}_Wgpu.cpp`, the unused source lists, and the wrong CMake comment near the `dc3-web` target. **Done** (`6d7f2c7fa`; defined-symbol sets of all four linked binaries unchanged). Still dead, not in scope: the `list(REMOVE_ITEM DC3_NATIVE_CORE_SOURCES_ENGINE ...)` files | S |
| H-2 | Fix or delete `wgpu-window-test`. **Deleted** (`6d7f2c7fa`): a GLFW+WebGPU smoke window superseded by every engine target; `ninja all` builds again | S |
| H-3 | GitHub `native-build` red in 36 of 40 runs: find the cause. **Fixed on branch `np-ci`; the replay is green; not pushed.** Build step (`0bed35b3e`, `4eb2a1525`, `6c32e1d97`): on pre-GCC-15 runners, a checked-in GCC-15 `stl_iterator.h` (`redefinition of '__niter_base'`) and a date-keyed `random_shuffle` shim in `msvc_compat.h`; on 26.04, a gtest-discovery cwd that did not exist. Test gate (`np-ci`): with no `orig-assets`, `NativeArchiveInit` built an `Archive` from a missing header. The non-fatal assert left it an empty hash table, and the first file lookup did `HashString(name, 0)`, a modulo by zero, so the process died of SIGFPE. That killed 320 tests in 58 suites, and it killed `dc3-native` for any player with a bad `DC3_DATA`. Now: the boot exits 1 with `FATAL - no game data`, pinned by `HeadlessBootNoGameData`; `EnsureEngineInit()` skips (`[no-game-data]`) before booting when the boot path's own probe finds no data; the workflow runs `scripts/native_test.sh --no-assets`, which enforces a floor of **262 verified gtest passes** (`native/tests/no_assets_floor.txt`) and fails on any test ctest scored PASSED that ran nothing. Replay at `186f5a7d7` (ubuntu:26.04, every workflow step): job SUCCESS. 672 registered, 264 executed, 262 verified, 0 failed, 0 ghosts, 408 skipped, 323 of them at the data check. Local `scripts/native_test.sh` at the same commit: 672 registered, 602 passed, 0 failed, 70 skipped, which equals the budget; exit 0. See [TESTING.md](TESTING.md#no-game-data-ci). Remaining: push, and check the first real runner's numbers against the replay | S–M |
| H-4 | `milo_viewer.cpp` sets `MILO_RENDER=0`, which the engine reads as "set", so GPU is forced on. **Done** (`d1f464c8c`; same bug in `milo-tex-export`, `milo-mat-export`, `milo2gltf`) | S |
| H-5 | `FFmpegMovieImpl.cpp` compiled from both repos: possible duplicate. **Done** (`a2267dcf8`): a real duplicate hidden by archive pull order; engine copy excluded (dc3's has the fixes), dc3's byte-identical `FFmpegAudioReader` deleted. Fixes for the engine copy: scratch patch `engine-ffmpeg-movie-fixes.patch` | S |
| H-6 | Crowd impostors cached once per character, so the crowd never animates | M |
| H-7 | `native_assert_harvest.py` stalls on tutorial screens | S |
| H-8 | CLAUDE.md and the CMake comment call the gameplay gate "one ~2 min run"; it is ~50 min | S |

## Owner decisions pending

- Public web deployment (W-5).
- Which Mac / macOS / Xcode; Metal Dawn still installed or use Google's prebuilt; Python pose server acceptable on Mac (M-7, P-8).
- Shared-engine changes (G-5, M-2, M-4) need a heads-up to the rb3 owner before landing.
