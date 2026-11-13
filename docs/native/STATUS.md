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
| Story | Choose profile → difficulty → song 1 → `campaign_perform_endgame_screen` → next song → `campaign_intro_perform_continue_screen`, with no input after the difficulty. The Perform tutorial shows only on a console that has not seen it. See [Reaching Story and Fitness](#reaching-story-and-fitness). | headless GPU run with screenshots, plus no-GPU runs, 2026-11-13 (`np-modes`) |
| Fitness | A fitness song starts from the `playlists` row, not row 0. See [Reaching Story and Fitness](#reaching-story-and-fitness). | headless runs, 2026-11-13 (`np-modes`) |
| Party / Crew | Not reached; expected to hit the same second-player wall as Battle | inferred |
| Options, store | Not reached | — |
| Audio | **Not verified by ear.** Headless skips audio; under Xvfb `ma_device_init` fails | — |
| Windowed | Starts at 1280×720 and real keys drive it into gameplay. Space opens pause but doesn't close it; Esc is B, not quit, although the log says "press ESC to exit". | Xvfb |
| Visual | Black box over the final-results header; controller-mode help bar stays on during gameplay; "raise both hands" bubble on most menus (doubled in party); title background purple vs blue on Xbox; missing glyphs (`S` in Vibration-Medium, `%`/`f` in Eagle-Light) | screenshots |
| Noise | No asserts, Debug::Fail or crashes in any controller-driven run | logs |

### Reaching Story and Fitness

These notes correct three survey findings (PLAN G-2, G-3, G-4). None of the
three was a bug in the game.

- **The tutorial is tracked per console, not per profile.** `has_seen_tutorial`
  reads `ProfileMgr::mTutorialsSeen`, which is saved in the global options cache
  (`DC3_CACHE_DIR/global/global`, 0x39 bytes, int at 0x28), not in
  `DC3_SAVE_DIR/profile_0_save.dat`. A copied save dir with no cache dir is a
  console that has never seen a tutorial, so Story correctly shows
  `tutorial_perform_it_screen_*` there. That is what the survey run did: its
  driver created the save dir empty. Story's gate is `enter_gameplay`
  (`ui/global.dta:211`). `campaign_intro` inherits `gameplay_mode perform`
  through `campaign`, so Story uses the same gate as Perform. Tests:
  `StoryTutorialGateSeenTest`, `StoryTutorialGateFreshTest`.
- **Story's endgame advances by itself, but only after about 4 s of real time.**
  At gameover the profile gets a "High Solo Score!" passive message, shown for
  4000 ms of **wall-clock** time (`PassiveMessageQueue`, a `Timer`, which matches
  the original). `xp_overlay_panel`'s poll waits while
  `{passive_messenger has_messages}` is true, and `perform_endgame_panel` waits
  for the XP overlay to finish. After that the screen goes to
  `playlist_nextsong_screen`, or to `campaign_intro_perform_continue_screen`
  after the playlist's last song, and the next song loads. In a headless GPU run
  the endgame showed the bubble and reached `playlist_nextsong_loading_screen`
  3,500 frames later, untouched, and song 2 then played. Headless runs with
  `MILO_NORENDER=1` reach about 5,000–9,000 frames per second, and headless UI
  time advances a fixed 1/30 s per frame. So those 4 s cost 20k–36k frames
  (~650 UI-seconds). The survey driver gave up after about 12k frames: 39
  confirms, each followed by a 300-frame wait. Its confirm went to
  `perform_continue_panel`, which skips the results. The ~10k-frame "stuck"
  transition to `playlist_nextsong_screen` that followed was about 2 s of
  real-time loading. **Drive these screens with wall-clock waits**
  (`/api/screen/wait/<screen>?timeout=`), not frame counts. The same 4 s message,
  "Raise both hands to change Profile.", shows from boot. In fast headless runs
  that covers thousands of menu frames, so check it before reading G-7's bubble
  as a bug.
- **`fitness_screen` is a menu, not a song list.** `FitnessProvider::UpdateList`
  builds exactly four rows: 0 `fitness_option` (toggles the active profile's
  fitness mode, and that is all it does), 1 `fitness_set_fitness_goals`,
  2 `fitness_body_profile`, 3 `playlists`. A fitness song starts from row 3:
  `song_select_playlist_tutorial_check_screen` (playlist tutorial on first use)
  → `song_select_playlist_screen` (gamemode `playlist_perform`) → a playlist
  row → `playlist_play` → `multiuser_screen` → song. With fitness mode on,
  `continue_next_screen` takes its fitness-playlist branch at the endgame. The
  row needs an **active profile**, and under `DC3_FAST_BOOT` only
  title_screen's confirm signs one in: the boot skip sets
  `$post_load_dest_screen` but not the initial profiles. With no profile,
  fitness shows its `no_profile` view and an empty list. Test:
  `FitnessPlaylistFlowTest` (`scripts/dc3-input-flows/fitness-playlist.txt`).

## Pose input — see [POSE_INPUT.md](POSE_INPUT.md)

| Area | State | How measured |
|---|---|---|
| Synthetic performer | All-perfect ratings, ~1.68 M at 100 s; still all perfect with webcam-like noise and 150 ms lateness | ran |
| MediaPipe on a video file | Scored (wrong-dance clip → mostly bad/ok) | ran |
| Motionless skeleton | SIGSEGV at ~130 s (zero variance in `RhythmDetector`) — **fixed** by the sensor noise floor (PLAN P-1); 45k-frame still run exits 0 | ran |
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
breakages, five of them fixable from Linux; on branch `np-macprep` the
Linux-fixable parts are done (engine half as patch files). The Linux libc++ proxy
build (libc++ 23, generated shim, branch `np-macprep`) compiles every
`dc3-native` TU, links, and plays: `ymca` flow to `game_screen`,
`state=playing`, 8000-frame cap, exit 0.

## Build and CI health

| Item | State |
|---|---|
| GitHub `native-build` workflow | Failed 36 of the last 40 runs at "Build native targets". Build-step causes found and fixed on branch `np-macprep` (H-3, not pushed): on pre-GCC-15 runners a checked-in GCC-15 `stl_iterator.h` and a date-keyed `random_shuffle` shim, on 26.04 a nonexistent gtest-discovery cwd and an asset-less `HeadlessBootTest` crash. Still red after that: with no assets, 320 tests in 58 suites crash in the archive load (container replay) |
| `wgpu-window-test` | Deleted on `np-macprep` (H-2); `ninja all` builds again |
| Dead dc3 renderer copies | `native/src/gfx/*`, `native/src/platform/{Rnd,Mesh,Tex,Part}_Wgpu.cpp` deleted on `np-macprep` (H-1). Other dead copies remain in the `REMOVE_ITEM DC3_NATIVE_CORE_SOURCES_ENGINE` list |
| HX_NATIVE footprint | 987 guard lines in 314 files (`grep -rc` over `src/`, conditional lines) |
