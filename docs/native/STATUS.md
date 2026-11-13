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
| Battle | Two people on the sensor: whole song to results, both scored. One person: pauses at ~beat 4 and waits for the second player, as on a 360. No camera: two stand-ins, results. See [Two-player modes](#two-player-modes). | headless GPU runs + `TwoPlayerBattleTest` / `OnePersonBattleTest` / `NoCameraBattleTest`, 2026-11-13 (`np-twoplayer`) |
| Story | Choose profile → difficulty → song 1 → `campaign_perform_endgame_screen` → next song → `campaign_intro_perform_continue_screen`, with no input after the difficulty. The Perform tutorial shows only on a console that has not seen it. See [Reaching Story and Fitness](#reaching-story-and-fitness). | headless GPU run with screenshots, plus no-GPU runs, 2026-11-13 (`np-modes`) |
| Fitness | A fitness song starts from the `playlists` row, not row 0. See [Reaching Story and Fitness](#reaching-story-and-fitness). | headless runs, 2026-11-13 (`np-modes`) |
| Party / Crew | Crew Throwdown with two people: all 4 events, standings, rematch, main; both scored every event. Party Time: events scored for both; with one person its perform event plays solo (the game's own `use_1p_instead`). See [Two-player modes](#two-player-modes). | headless GPU runs, 2026-11-13 (`np-twoplayer`) |
| Options, store | Every Options page reached; settings persist across a relaunch (freestyle, overscan, calibration, crowd volume). Store reaches its offline dialog; credits show. Defects: Options → Gameplay raises 16 `MILO_FAIL`s on the no-voice nav-provider path and its checkboxes never change (PLAN G-12); Kinect Tuner does nothing (G-16) | headless runs, 2026-11-13 (`np-dogfood2`) |
| Movies | **Every desktop `.bik` fails to open** (`avformat_open_input: No such file or directory`; relative path from cwd `native/build`): no intro, no song-select previews, no era/credits movies (PLAN G-15) | logs, 2026-11-13 (`np-dogfood2`) |
| Rehearse dancer | **No dancer drawn** in Rehearse (and Crew "Make Your Move"), with or without pose; telemetry says `player0` exists with clip layers (PLAN G-11) | screenshots, 2026-11-13 (`np-dogfood2`) |
| Post-process | The "awesome" post-process covers the whole screen, HUD and pause menus included, in animated static (engine `PostProcPass.cpp` noise vs the image's `NgPostProc::CheckNoise`; PLAN G-14) | screenshots + live `force_postproc` read, 2026-11-13 (`np-dogfood2`) |
| Audio | **Not verified by ear.** Headless skips audio; under Xvfb `ma_device_init` fails | — |
| Windowed | Starts at 1280×720 and real keys drive it into gameplay. Space opens pause but doesn't close it — the original's behaviour (G-13); Esc is B, not quit, although the log says "press ESC to exit". Closing the window (`WM_DELETE_WINDOW`) or destroying it exits rc 0 with no crash; SIGTERM quits at a frame boundary, rc 143 (H-9) | Xvfb; exits: `NativeExitTest`, 2026-11-13 (`np-exit`) |
| Visual | Black box over the final-results header; controller-mode help bar stays on during gameplay; "raise both hands" bubble on most menus (doubled in party); title background purple vs blue on Xbox; missing glyphs (`S` in Vibration-Medium, `%`/`f` in Eagle-Light) | screenshots |
| Noise | ~~No asserts, Debug::Fail or crashes in any controller-driven run~~ **False** (second pass, `np-dogfood2`): Options → Gameplay raises 16 `MILO_FAIL`s (G-12). The window-close crash in static teardown (H-9) is fixed (`np-exit`) | logs, 2026-11-13 |
| Test gate (2nd pass) | 691 registered / 621 executed / 621 passed / 0 failed / 70 skipped, exit 0 | ran, 2026-11-13 at `09586f7d9` (`np-dogfood2`) |

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

### Two-player modes

PLAN G-1. **The "second-player wall" was not a native bug.** The survey's
Battle run had no pose provider, so the sensor showed one standing dummy in
slot 0. The image's own `Game::CheckForSkeletonLoss` wants two bound players
when the hamprovider says `requires_2_players` or `is_in_party_mode`, and
`dance_battle.dta`'s `pause_on_skeleton_loss` arms after beat 4. So the game
paused, as a 360 does with one person in front of it. The providers were never
limited to slot 0: `BuildSensorFrame`'s provider branch gives every valid
person its own persistent slot (`FillPersonSlots`), and `NativeAutoAssignPlayers`
binds each unbound player to a tracked skeleton. Two people on the socket, or
two in one `dc3_pose_push` frame on web, are two players.

Measured with `scripts/synthetic_kinect.py --people 2` (persons at x = ±0.4 m,
so `SkeletonChooser` puts them on opposite sides), headless GPU, screenshots
read:

| Mode | 2 people | 1 person | No camera (controller) |
|---|---|---|---|
| Dance Battle | Whole song (YMCA) to `dancebattle_perform_endgame_screen` → complete → song select; both scored (244,336 / 244,299) | Pauses at ~beat 4, "Continue" disabled until player 2 is bound (`pause.dta` `update_skeleton_status_internal`). That is the 360's behaviour and stays | Two stand-ins (below); results, scores 0 |
| Person 2 leaves mid-song and returns 15 s later | Pause at once; on return player 2 is rebound, Continue resumes, both finish scored (212,360 / 212,337) | — | — |
| Crew Throwdown | Sign-in → crew announcement → hub → 4 events (battle, perform, perform, showdown battle) → standings each time → rematch → main; both scored every event | Not measured: crew sign-in needs both sides present (DTA) | — |
| Party Time | Hub → perform event → hub → Keep the Beat (Firework, 628,503 / 628,236) → hub; both scored each event | The perform event plays solo to `perform_endgame_screen` with no pause: the game's own `use_1p_instead` / `force_perform` when fewer than two are present | — |

**No camera.** With no provider running the sensor now shows a second
stand-in in a two-player mode (tracking id 2, at x = +0.4 m), so a
controller-only Battle reaches results instead of pausing forever with nobody
able to make it continue. The count is decided per frame from the
hamprovider's `requires_2_players` / `is_in_party_mode`
(`NativeDummyPlayerCount`, `GestureMgr_Native.cpp`), and
`DC3_DUMMY_PLAYERS=1|2` overrides it. It is never added while a provider runs:
then the people in front of the camera are the players, and one person must
get the pause.

**Also fixed.** Under `DC3_FAST_BOOT` the HX_NATIVE boot-advance table in
`UIManager::Poll` treated `tutorial_party_mode_screen_0/_1` as boot screens and
jumped to `main_screen`, so Party Time bounced to the main menu on a console
that had not seen its tutorial. Removed (`UIManager::Poll` is 100.0% under
`name_check` before and after).

**Tests** (`DC3_DTA_FLOW_TESTS`): `TwoPlayerBattleTest` (two synthetic
performers to results, both bound and scored, no pause), `OnePersonBattleTest`
(pause, no results, player 2 unbound), `NoCameraBattleTest` (two stand-ins to
results), plus `NativeDummyPlayersTest` for the policy. Each was sabotaged and
failed: providers filling slot 0 only; a stand-in supplementing a running
provider; the dummy count pinned to 1. The tests seed the global options cache
so no `tutorial_battle_screen_*` intervenes, and run under `DC3_FAST_TIME`
(~8–35 s each).

**Seen on the way, not fixed:** the team-photo boxes on the battle results and
party winner screens are white (PLAN P-4). Without a camera there is nothing to
photograph; with camera frames the snapshot target now renders the crew photo
(alpha 0, see POSE_INPUT.md *Player on screen*), but no run has reached a
results screen showing one yet.

## Pose input — see [POSE_INPUT.md](POSE_INPUT.md)

| Area | State | How measured |
|---|---|---|
| Synthetic performer | All-perfect ratings, ~1.68 M at 100 s; still all perfect with webcam-like noise and 150 ms lateness | ran |
| MediaPipe on a video file | Scored (wrong-dance clip → mostly bad/ok) | ran |
| Motionless skeleton | SIGSEGV at ~130 s (zero variance in `RhythmDetector`) — **fixed** by the sensor noise floor (PLAN P-1); 45k-frame still run exits 0 | ran |
| Live webcam | Never tried (no camera on the measurement box) | — |
| Gesture menus | Whole perform route by gesture alone (title → song → results → main), 10 gesture / 0 button selects; default policy `auto` (faithful with a camera). Web: same policy, title → song select → back by gesture through `dc3_pose_push`. See [POSE_INPUT](POSE_INPUT.md#gesture-menus) | `GestureMenusTest` + its forced sabotage, `scripts/web/gesture-menus.mjs`, 2026-11-13 (`np-gesture-menus`) |
| Player silhouette (P-4) | With a camera stream the depth image inside the menu and gameplay player boxes draws the player (`np-player-view`): synthetic v3 frames and real MediaPipe masks over a video; mirrored like a Kinect. Photos, `DepthBuffer3D`, song-preview movie: not done. No camera: unchanged | screenshots + `CameraViewTest` / `NoCameraViewTest` |
| `scripts/pose_scoring_gate.sh` | Passes (~13 min): five configs, each the whole song under `DC3_FAST_TIME`; judges means, final scores and per-move distances (`scripts/pose_gate_report.py`). Standing still out-scores a different recorded dance (P-7) | ran |

## Web — see [WEB.md](WEB.md)

| Area | State | How measured |
|---|---|---|
| Build | Fresh worktree, `scripts/web/build.sh`, ~3 min (release) | ran |
| Play | Whole song in Chromium on a real GPU, ~60 fps (rAF-capped), no page errors; song audio played the whole track | headless-shell |
| Input | ~~Keyboard/gamepad only~~ Webcam pose since P-9: with a fake camera, tracking runs at ~25 fps and Macarena → Cupid Shuffle play to results with pause/resume (`np-dogfood2`). `MILO_NOTIFY`/`MILO_WARN` now reach the devtools console (W-9, `np-exit`: boot → title, the desktop's notifies plus one web-only `DataNode::Equal` notify) | ran, 2026-11-13 |
| Large songs | `np-dogfood2` (tree at `09586f7d9`, before `185b6f50e`) saw Ice Ice Baby and In Da Club go back to the title after preloading: the preload-budget bug fixed by `185b6f50e`. Re-check: `np-songs` | ran |
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
