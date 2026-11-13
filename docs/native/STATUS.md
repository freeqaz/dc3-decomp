# Native port — measured status

**Measured 2026-11-13 at `865405327`** by four survey lanes (desktop, web, pose,
macOS). Every row says how it was established. Re-measure before quoting; this
page is a dated snapshot, and [PLAN.md](PLAN.md) is where open work lives.

## One-line answer

With a controller, the Linux desktop build is playable: Perform mode runs from
boot to the results screen and back, for all 45 quickplay songs, and saves
persist. The web build plays a whole song too, and every song reaches gameplay.

**By dancing (updated 2026-11-13 at `4b14849bc`, from lane reports):** recorded
footage of a real dancer, sent through the live `pose_server` path, plays a
whole song to the results card, with menus driven by gesture and the player's
silhouette on screen; the web build tracks a fake camera and scores. The
hold-still crash is fixed. **No real webcam has driven the game yet**, and a
live player would hit a mid-song "stand in front of the sensor" pause when
tracking drops for ~0.7 s (PLAN P-12). What stands between here and a live
playtest is [PLAN track L](PLAN.md#track-l--live-playtest-readiness). macOS is
prepared from Linux and has not been built on a Mac since March.

## Linux desktop

| Area | State | How measured |
|---|---|---|
| Test gate | `scripts/native_test.sh`: 654 registered / 584 executed / 584 passed / 0 failed / 70 skipped (budget 70), exit 0. On `np-songs` (main `cf2482488` + `PreloadBudgetTest` ×2 + `LargestSongFlowTest`): 706 / 636 / 636 / 0 / 70 (budget 70), exit 0 | ran |
| Gameplay gate | `--all-gates`: all 48 `GameplayTelemetryTest` pass, including feet-above-floor and no-bone-garbage. Each test is its own 51–89 s engine run, so the gate takes ~50 min. | ran (stopped at test 402 by a timeout, 0 failures) |
| Perform | Boot → title → song select → multiuser → tutorial (fresh profile) → loading → gameplay → results → song select → main, on YMCA / Better Off Alone / Give It Up in three venues; two songs back to back. **Every quickplay song (45) plays whole to the results**, none bounces, hangs or crashes; the largest, omg, is a test (`LargestSongFlowTest`). See [Every song](#every-song) | headless GPU runs; no-GPU sweep of all 45, 2026-11-13 (`np-songs`) |
| Save | `profile_0_save.dat` written at endgame; reloaded after restart; tutorial skipped and score shown | ran |
| Rehearse | Runs to `practice_endgame_screen` | ran |
| Battle | Two people on the sensor: whole song to results, both scored. One person: pauses at ~beat 4 and waits for the second player, as on a 360. No camera: two stand-ins, results. See [Two-player modes](#two-player-modes). | headless GPU runs + `TwoPlayerBattleTest` / `OnePersonBattleTest` / `NoCameraBattleTest`, 2026-11-13 (`np-twoplayer`) |
| Story | Choose profile → difficulty → song 1 → `campaign_perform_endgame_screen` → next song → `campaign_intro_perform_continue_screen`, with no input after the difficulty. The Perform tutorial shows only on a console that has not seen it. See [Reaching Story and Fitness](#reaching-story-and-fitness). | headless GPU run with screenshots, plus no-GPU runs, 2026-11-13 (`np-modes`) |
| Fitness | A fitness song starts from the `playlists` row, not row 0. See [Reaching Story and Fitness](#reaching-story-and-fitness). | headless runs, 2026-11-13 (`np-modes`) |
| Party / Crew | Crew Throwdown with two people: all 4 events, standings, rematch, main; both scored every event. Party Time: events scored for both; with one person its perform event plays solo (the game's own `use_1p_instead`). See [Two-player modes](#two-player-modes). | headless GPU runs, 2026-11-13 (`np-twoplayer`) |
| Options, store | Every Options page reached; settings persist across a relaunch (freestyle, overscan, calibration, crowd volume). Store reaches its offline dialog; credits show. Options → Gameplay: **0 `MILO_FAIL`s**, 6 rows on every visit (`np-options`; was 16 FAILs from the native overlay's `num_data`, G-12). Checkbox state was always right; both glyphs and every help-bar button glyph still draw white until the engine patch for G-12 lands (with it: coloured (A)/(B), filled vs outline boxes, the Battle "1 vs 1" icon). Kinect Tuner now says it is the 360 dashboard's (notice `kinect_tuner`, G-16) | headless runs, 2026-11-13 (`np-dogfood2`; `np-options` re-measure) |
| Movies | Desktop movies open out of the archive through the File layer. The intro plays on the attract screen (windowed from `native/build`). Song-select previews open and decode into `preview.tex`, but the on-screen look is unconfirmed. No movie audio (PLAN G-15) | screenshots + RT dump + `NativeTreeEnginePaths.*`, 2026-11-13 (`np-paths`) |
| Rehearse dancer | **No dancer drawn** in Rehearse (and Crew "Make Your Move"), with or without pose; telemetry says `player0` exists with clip layers (PLAN G-11) | screenshots, 2026-11-13 (`np-dogfood2`) |
| Post-process | The "awesome" post-process covers the whole screen, HUD and pause menus included, in animated static (engine `PostProcPass.cpp` noise vs the image's `NgPostProc::CheckNoise`; PLAN G-14). **Root-caused; engine patch verified in a scratch copy, not landed**: the image overlays a stationary striped noise *texture*, which gives a mild brightening, not ±0.5 per-pixel static | screenshots + live `force_postproc` read, 2026-11-13 (`np-dogfood2`) |
| Audio | **Not verified by ear.** Headless skips audio; under Xvfb `ma_device_init` fails | — |
| Windowed | Starts at 1280×720 and real keys drive it into gameplay. Space opens pause but doesn't close it — the original's behaviour (G-13). Every pad button has a key (X, Y, Z = LT, C = RT added; map printed at startup, see [README](README.md)); Esc is B, and Ctrl+Q quits, rc 0 (G-5). Closing the window (`WM_DELETE_WINDOW`) or destroying it exits rc 0 with no crash; SIGTERM quits at a frame boundary, rc 143 (H-9) | Xvfb + xdotool: `NativeExitTest`, 2026-11-13 (`np-exit`, `np-keys`) |
| Visual | Black box over the final-results header; controller-mode help bar stays on during gameplay; "raise both hands" bubble on most menus (doubled in party); title background purple vs blue on Xbox; missing glyphs (`S` in Vibration-Medium, `%`/`f` in Eagle-Light) | screenshots |
| Venue crowd | Drawn in 3D and animated every frame, as the image does (H-6): rollerrink draws 57 members (27 in some camera shots), 0 impostors; CPU cost 0.48–0.89 ms/frame. With the shipped engine the crowd's clothes are untinted and its skin black; the engine patch in PLAN H-6 fixes both (not landed). Before `np-crowd` no crowd member was drawn at all | headless GPU runs + `CrowdTest`, 2026-11-13 (`np-crowd`) |
| Noise | ~~No asserts, Debug::Fail or crashes in any controller-driven run~~ **False** (second pass, `np-dogfood2`): Options → Gameplay raised 16 `MILO_FAIL`s (G-12; 0 since `np-options`). The window-close crash in static teardown (H-9) is fixed (`np-exit`) | logs, 2026-11-13 |
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

### Every song

`np-songs`, 2026-11-13. Every quickplay song (the 45 song rows of song select;
the other song dirs are campaign/minigame/mocap content: `blackandtan*`,
`dcimindcontrol`, `hb_camp_*`, `mocapbpm*`) through the real menus, Perform,
Easy:

- **Desktop, whole songs** (`scripts/native_song_sweep.py --no-gpu`):
  controller-driven menus, in-process `choreo` performer
  (`DC3_POSE_PERFORM=choreo`), `DC3_FAST_TIME`, the venue the game picks.
  45/45 reach gameplay and the results; no bounce, hang, crash, assert or
  `Debug::Fail`. Ratings: all perfect on 43 (macarena and chingaling are in
  the pose table below). Peak RSS 429–510 MB, 14–52 s wall per song.
  Each song's picked row was checked against the log. Each venue's NOTIFY set
  is the same for all its songs, plus one per song:
  `DataNode::Equal ... game_modes.dta`, where `$just_practiced_song` (never set
  before a practice) is compared with the song. That is the image's
  `MILO_NOTIFY_ONCE`, not a native fault. Per song, the only file lines not
  shared by the venue were the loading-music `.mid` (PLAN H-11: no loading
  music; fixed by `np-loadmusic`, the `.mid` now comes from the persistent
  cache and the music streams) and the
  song-select preview movies passed on the way (those were `np-paths`' G-15, fixed on main since).
- **Desktop, GPU** (`--song-seconds 40`, gameplay screenshot at beat 48):
  45/45 reach gameplay with venue, dancers and HUD drawn; ratings all
  perfect to 40 s except macarena (`So_Fly` again). Peak RSS 627–851 MB. The
  "awesome" post-process static covers most frames once the player is flawless
  (the `np-postproc` lane's G-14, an engine fix). 7 of 45 single shots came
  back all black (aroundtheworld, boyfriend, chingaling, omg and
  teachmehowtodougie in dci; stereolove in dclive; supersonic in streetside),
  as did omg in throneroom. Rerun with two shots each (same song and venue), all 10 frames drew. So it is transient and not
  explained (a screenshot race, or a real black frame). **Venues:** omg in
  each of the six venues (`--venue`, which sets `meta_performer
  set_venue_pref`; throneroom included, although quickplay locks it) reaches
  gameplay, all perfect, 732–849 MB. **Results screen:** ymca whole song on
  GPU reached the results with the right card, stars and score (one run;
  the whole-song no-GPU runs reach the results for all 45). The single
  results shot predates the H-10 diagnosis: it read 62/0/0/5, the dropout,
  because the GPU run passed ~3 min of wall clock (the song stream ran out
  ahead of the FAST_TIME song clock; fixed by `np-loadmusic`, PLAN H-10).
- **Web** (`scripts/web/song-sweep.mjs`, release build of this branch,
  Chromium, fresh page per song, gameplay + 30 s): 45/45 reach `game_screen`
  and stay 30 s. Every chain after song select is
  `multiuser → loading → preloading → real_loading → game_screen`, with no
  bounce. That includes **Ice Ice Baby and In Da Club, which bounced
  to the title on `09586f7d9`**. 0 page errors. The same three 404s on every
  song: `watches.dta` (`Watcher`), `devkit:/dancers.dta` (`HamPlayerData`) and
  `devkit:/locale/eng/locale_keep.dta`, all optional debug reads. Since the
  persistent cache (H-11) there is a fourth, once at boot:
  `ui/image/dc_logo_keep.png`, keyed raw because no native renderer runs
  `Rnd::Init`/`RndUtlInit` (PLAN H-11 *Left*). Load
  (`loading_screen` → `game_screen`) took 2.5–12.4 s. The wasm heap
  (byteLength, geometric growth, so a reservation) was 363–523 MB; omg and
  youmakemefeeldc reach 522.6 MB, past the old 512 MB cap and inside the
  1024 MB one. The first try of chingaling picked iamthebest: a dropped
  ArrowDown at 400 ms gaps. At `--gap 800` it plays.
- **Preload bug (PLAN W-6) was web-only.** Desktop reads the ark's compressed
  `.milo_xbox`: the largest set (barks + milo + mid) is mocapbpm93_v3 at
  13.22 MiB, inside loading.dta's 16 MiB. With the ×4 removed, `dc3-native`
  plays omg to gameplay (no `Could not cache`). Web reads the inflated
  extraction: 34 sets are over 16 MiB, and the largest is omg at 25.20 MiB.
  Guards: `PreloadBudgetTest.DesktopReadsTheArk` (every set through the real
  `PreloadPanel::sCache`, plus a control one byte under the largest that must
  evict) and `.WebReadsInflatedFiles` (milo-tests), and
  `LargestSongFlowTest.OmgPreloadsAndReachesGameplay` (`DC3_DTA_FLOW_TESTS`,
  ~18 s). With the ×4 removed, the web test fails on 34 songs. With the
  budget divided by 4, the flow test fails, and the log shows
  `preloading_screen → meta_loading_uisink_default_screen → title_screen`.

Per song (desktop: whole song, no GPU, `choreo` performer, Easy; GPU: to 40 s;
web: to gameplay + 30 s, stand-in player; the venue is the game's random pick
and differs between runs):

| song | venue (desktop run) | ratings P/A/O/B | score | peak RSS MB, no GPU | peak RSS MB, GPU | web load s | web heap MB |
|---|---|---|---|---|---|---|---|
| 12step | dclive | 54/0/0/0 | 1,844,900 | 438 | 633 | 6.1 | 435.5 |
| aint2proud2beg | houseparty | 66/0/0/0 | 2,311,620 | 450 | 730 | 3 | 435.5 |
| aroundtheworld | dci | 65/0/0/0 | 2,289,900 | 478 | 738 | 7.2 | 435.5 |
| bassdownlow | dci | 57/0/0/0 | 2,050,980 | 493 | 764 | 8.9 | 435.5 |
| betteroffalone | dci | 77/0/0/0 | 2,680,860 | 475 | 753 | 4.1 | 435.5 |
| bewareoftheboys | streetside | 50/0/0/0 | 1,758,020 | 450 | 723 | 5.6 | 383.3 |
| boomboompow | dclive | 69/0/0/0 | 2,418,058 | 437 | 644 | 3.2 | 377.8 |
| boyfriend | dci | 57/0/0/0 | 2,050,980 | 495 | 770 | 11.2 | 435.5 |
| calabria | dclive | 69/0/0/0 | 2,486,812 | 446 | 710 | 3 | 435.5 |
| chingaling | dci | 60/0/0/1 | 2,068,050 | 483 | 733 | 3.4 | 435.5 |
| cupidshuffle | dclive | 77/0/0/0 | 2,811,180 | 436 | 633 | 2.9 | 435.5 |
| dabutt | streetside | 57/0/0/0 | 1,936,950 | 456 | 740 | 9.2 | 435.5 |
| discoinferno | rollerrink | 64/0/0/0 | 2,219,310 | 460 | 736 | 3.1 | 435.5 |
| electricboogie | streetside | 61/0/0/0 | 2,134,908 | 451 | 736 | 2.6 | 435.5 |
| everybody | houseparty | 58/0/0/0 | 2,137,860 | 444 | 733 | 2.6 | 435.5 |
| firework | dclive | 73/0/0/0 | 2,578,458 | 447 | 712 | 3.2 | 435.5 |
| getlow | dclive | 65/0/0/0 | 2,282,375 | 443 | 733 | 2.7 | 435.5 |
| giveituptome | dclive | 49/0/0/0 | 1,680,611 | 429 | 627 | 2.8 | 362.9 |
| hello | streetside | 77/0/0/0 | 2,811,180 | 457 | 726 | 5.3 | 457.1 |
| iamthebest | dci | 67/0/0/0 | 2,398,500 | 487 | 737 | 11.5 | 455.8 |
| iceicebaby | streetside | 61/0/0/0 | 2,202,537 | 459 | 774 | 3.2 | 435.5 |
| indaclub | dclive | 49/0/0/0 | 1,711,424 | 439 | 630 | 3.6 | 435.5 |
| iwillsurvive | rollerrink | 65/0/0/0 | 2,241,030 | 470 | 750 | 3.1 | 435.5 |
| letthemusicplay | streetside | 73/0/0/0 | 2,593,980 | 461 | 809 | 8.2 | 457.9 |
| macarena | houseparty | 49/4/0/0 | 1,706,653 | 441 | 783 | 4.8 | 382.7 |
| moveslikejagger | streetside | 87/0/0/0 | 3,093,540 | 468 | 817 | 10.8 | 460.5 |
| mrsaxobeat | dci | 78/0/0/0 | 2,832,218 | 493 | 785 | 3.3 | 435.5 |
| nowthatwefoundlove | houseparty | 57/0/0/0 | 1,985,689 | 442 | 725 | 2.8 | 380.9 |
| omg | dci | 81/0/0/0 | 2,891,441 | 510 | 847 | 4.4 | 522.6 |
| onthefloor | dci | 76/0/0/0 | 2,653,940 | 490 | 754 | 11.6 | 459.9 |
| sambadejaneiro | streetside | 74/0/0/0 | 2,614,692 | 457 | 736 | 2.5 | 457.9 |
| scream | dci | 66/0/0/0 | 2,441,940 | 498 | 766 | 5 | 459.3 |
| sexyandiknowit | dci | 73/0/0/0 | 2,593,980 | 499 | 851 | 4.3 | 460.5 |
| starships | dci | 73/0/0/0 | 2,593,980 | 496 | 759 | 10.3 | 435.5 |
| stereolove | dclive | 68/0/0/0 | 2,305,616 | 439 | 686 | 4.9 | 435.5 |
| supersonic | streetside | 75/0/0/0 | 2,571,517 | 460 | 751 | 4.7 | 435.5 |
| takeovercontrol | dci | 73/0/0/0 | 2,593,980 | 483 | 805 | 7.1 | 459.3 |
| teachmehowtodougie | dci | 41/0/0/0 | 1,442,820 | 493 | 788 | 3.3 | 435.5 |
| thehustle | rollerrink | 59/0/0/0 | 2,224,740 | 457 | 731 | 2.6 | 435.5 |
| turnthebeataround | rollerrink | 77/0/0/0 | 2,811,180 | 460 | 739 | 2.7 | 435.5 |
| whatdoesntkillyou | houseparty | 82/0/0/0 | 2,919,780 | 450 | 802 | 3.8 | 435.5 |
| wildones | dclive | 78/0/0/0 | 2,765,866 | 435 | 657 | 2.6 | 385 |
| ymca | rollerrink | 67/0/0/0 | 2,398,500 | 454 | 734 | 3.4 | 383.3 |
| yougotittherightstuff | streetside | 69/0/0/0 | 2,393,070 | 454 | 735 | 2.9 | 435.5 |
| youmakemefeeldc | dci | 73/0/0/0 | 2,522,663 | 502 | 835 | 4.1 | 522.6 |

## Pose input — see [POSE_INPUT.md](POSE_INPUT.md)

| Area | State | How measured |
|---|---|---|
| Synthetic performer | All-perfect ratings, ~1.68 M at 100 s; still all perfect with webcam-like noise and 150 ms lateness. Over all 45 songs (whole song, `choreo`, `DC3_FAST_TIME`): all perfect on 43; macarena 49/4/0/0, the same on the real clock (`So_Fly_macarena` falls 0.97 → 0.92 → 0.88 → 0.79 across each 4-measure block: awesome in the 3rd and 4th, both blocks; authored: the async detector scores every repetition against the move's first expert occurrence while the Easy dance drifts, PLAN P-11); chingaling 60/0/0/1 under FAST_TIME (`Whip_Step` right after a rest, frac 0, deterministic) but 61/0/0/0 on the real clock. Under `DC3_FAST_TIME`, a run lasting more than ~3 min of wall clock with song time slower than wall (paced or GPU) used to stop scoring when the song stream ran out; fixed 2026-11-13 (PLAN H-10): paced 30, ymca to 50 s, 24/0/0/0 | ran; [Every song](#every-song) |
| MediaPipe on a video file | Scored (wrong-dance clip → mostly bad/ok) | ran |
| Motionless skeleton | SIGSEGV at ~130 s (zero variance in `RhythmDetector`) — **fixed** by the sensor noise floor (PLAN P-1); 45k-frame still run exits 0 | ran |
| Live webcam | Never tried (no camera on the measurement box) | — |
| Pose server from `native/build` | A windowed boot from `native/build` spawns this tree's `pose_server.py` with the tree's model (absolute path), and the server loads it (PLAN P-10) | log, 2026-11-13 (`np-paths`, a video standing in for the camera) |
| Gesture menus | Whole perform route by gesture alone (title → song → results → main), 10 gesture / 0 button selects; default policy `auto` (faithful with a camera). Web: same policy, title → song select → back by gesture through `dc3_pose_push`. See [POSE_INPUT](POSE_INPUT.md#gesture-menus) | `GestureMenusTest` + its forced sabotage, `scripts/web/gesture-menus.mjs`, 2026-11-13 (`np-gesture-menus`) |
| Player silhouette (P-4) | With a camera stream the depth image inside the menu and gameplay player boxes draws the player (`np-player-view`): synthetic v3 frames and real MediaPipe masks over a video; mirrored like a Kinect. Photos, `DepthBuffer3D`, song-preview movie: not done. No camera: unchanged | screenshots + `CameraViewTest` / `NoCameraViewTest` |
| `scripts/pose_scoring_gate.sh` | Passes (~13 min): five configs, each the whole song under `DC3_FAST_TIME`; judges means, final scores and per-move distances (`scripts/pose_gate_report.py`). Standing still out-scores a different recorded dance (P-7) | ran |

## Web — see [WEB.md](WEB.md)

| Area | State | How measured |
|---|---|---|
| Build | Fresh worktree, `scripts/web/build.sh`, ~3 min (release) | ran |
| Play | Whole song in Chromium on a real GPU, ~60 fps (rAF-capped), no page errors; song audio played the whole track. Every quickplay song (45) reaches gameplay and stays 30 s, no bounce (Ice Ice Baby and In Da Club included), 0 page errors; see [Every song](#every-song) | headless-shell; `scripts/web/song-sweep.mjs`, 2026-11-13 (`np-songs`) |
| Input | ~~Keyboard/gamepad only~~ Webcam pose since P-9: with a fake camera, tracking runs at ~25 fps and Macarena → Cupid Shuffle play to results with pause/resume (`np-dogfood2`). `MILO_NOTIFY`/`MILO_WARN` now reach the devtools console (W-9, `np-exit`: boot → title, the desktop's notifies plus one web-only `DataNode::Equal` notify) | ran, 2026-11-13 |
| Large songs | `np-dogfood2` (tree at `09586f7d9`, before `185b6f50e`) saw Ice Ice Baby and In Da Club go back to the title after preloading: the preload-budget bug fixed by `185b6f50e`. Re-check: `np-songs` | ran |
| Memory | wasm byteLength 363–523 MB over the 45 songs (geometric growth, so reservation; true peaks in [WEB.md](WEB.md#heap-per-venue-w-6)); omg and youmakemefeeldc pass the old 512 MB cap, the cap is now 1024 MB | ran (`np-songs`) |
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
