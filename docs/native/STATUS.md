# Native port — measured status

**Measured 2026-11-13 at `865405327`** by four survey lanes (desktop, web, pose,
macOS). Every row says how it was established. Re-measure before quoting; this
page is a dated snapshot, and [PLAN.md](PLAN.md) is where open work lives.

## One-line answer

With a controller, the Linux desktop build is playable: Perform mode runs from
boot to the results screen and back, and saves persist. The web build plays a
whole song too. **Neither can be played by dancing yet**: the webcam path has
never run against a real camera, crashes when the player holds still, and does
not exist on web. macOS last ran in March and would not build today.

## Linux desktop

| Area | State | How measured |
|---|---|---|
| Test gate | `scripts/native_test.sh`: 654 registered / 584 executed / 584 passed / 0 failed / 70 skipped (budget 70), exit 0 | ran |
| Gameplay gate | `--all-gates`: all 48 `GameplayTelemetryTest` pass, including feet-above-floor and no-bone-garbage. Each test is its own 51–89 s engine run, so the gate takes ~50 min. | ran (stopped at test 402 by a timeout, 0 failures) |
| Perform | Boot → title → song select → multiuser → tutorial (fresh profile) → loading → gameplay → results → song select → main, on YMCA / Better Off Alone / Give It Up in three venues; two songs back to back | headless GPU runs |
| Save | `profile_0_save.dat` written at endgame; reloaded after restart; tutorial skipped and score shown | ran |
| Rehearse | Runs to `practice_endgame_screen` | ran |
| Battle | **Pauses at ~beat 4** (`pause_on_skeleton_loss`: only slot 0 has a skeleton) | ran |
| Story | **Re-shows the Perform tutorial on a profile that has seen it; endgame screen never advances** | ran |
| Fitness | Could not start a song (may be navigation, not a bug) | ran |
| Party / Crew | Not reached; expected to hit the same second-player wall as Battle | inferred |
| Options, store | Not reached | — |
| Audio | **Not verified by ear.** Headless skips audio; under Xvfb `ma_device_init` fails | — |
| Windowed | Starts at 1280×720 and real keys drive it into gameplay. Space opens pause but doesn't close it; Esc is B, not quit, although the log says "press ESC to exit". | Xvfb |
| Visual | Black box over the final-results header; controller-mode help bar stays on during gameplay; "raise both hands" bubble on most menus (doubled in party); title background purple vs blue on Xbox; missing glyphs (`S` in Vibration-Medium, `%`/`f` in Eagle-Light) | screenshots |
| Noise | No asserts, Debug::Fail or crashes in any controller-driven run | logs |

## Pose input — see [POSE_INPUT.md](POSE_INPUT.md)

| Area | State | How measured |
|---|---|---|
| Synthetic performer | All-perfect ratings, ~1.68 M at 100 s; still all perfect with webcam-like noise and 150 ms lateness | ran |
| MediaPipe on a video file | Scored (wrong-dance clip → mostly bad/ok) | ran |
| Motionless skeleton | **SIGSEGV at ~130 s** (zero variance in `RhythmDetector`) | ran |
| Live webcam | Never tried (no camera on the measurement box) | — |
| Gesture menus, player silhouette/preview | Absent | read |
| `scripts/pose_scoring_gate.sh` | Passes (~11 min); compares maximums only and stops before the song ends | ran |

## Web — see [WEB.md](WEB.md)

| Area | State | How measured |
|---|---|---|
| Build | Fresh worktree, `scripts/web/build.sh`, ~3 min (release) | ran |
| Play | Whole song in Chromium on a real GPU, ~60 fps (rAF-capped), no page errors; song audio played the whole track | headless-shell |
| Input | Keyboard/gamepad only; **no pose path** (dummy skeleton scored 7,581, 0 stars) | ran |
| Memory | ~460 MB of the 512 MB cap during gameplay | ran |
| Variants | "release" is `-O0`, "debug" is `-O3`; labels are swapped | read flags |
| Test scripts | `scripts/web/*.mjs` cannot launch their browser here (Playwright/browser version mismatch) | ran |
| Public deploy | Serves a 2026-07-27 build | read unit |

## macOS — see [MACOS.md](MACOS.md)

Ran windowed on a real Mac in March 2026. Not built on a Mac since. Seven known
breakages, five of them fixable from Linux. The Linux libc++ proxy build compiles
873/874 files, links with zero undefined symbols, and reaches `game_screen`.

## Build and CI health

| Item | State |
|---|---|
| GitHub `native-build` workflow | Failed 36 of the last 40 runs at "Build native targets"; cause unknown (the log fetch was truncated) |
| `wgpu-window-test` | Does not compile (since the gfx move to the engine) |
| Dead dc3 renderer copies | `native/src/gfx/*`, `native/src/platform/{Rnd,Mesh,Tex,Part}_Wgpu.cpp` compile into no building target and have drifted from the engine |
| HX_NATIVE footprint | 987 guard lines in 314 files (`grep -rc` over `src/`, conditional lines) |
