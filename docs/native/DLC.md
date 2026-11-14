# DLC on the native port

Native dc3 plays downloadable content from an extracted Xbox 360 package
directory. Every song of every package in the DLC archive on this box reached
the results screen (see [Sweeps](#sweeps)): the Dance Central 2 DLC Mega Pack
(32 songs) and the Dance Central 2 Import Pack for DC3 (44) on all four
difficulties, and the 64 Dance Central 1 songs (the DC1 mega pack and the DC1
Import Pack for DC2, 32 each) on Easy and Expert.

## Use it

```bash
scripts/dlc/import.sh /path/to/DanceCentral2DLCMegaPack   # -> orig-assets/dlc/dancecentral2dlcmegapack
scripts/dlc/import.sh /path/to/DC2ExportPackforDC3        # -> orig-assets/dlc/dc2exportpackfordc3
scripts/play.sh                  # dev profile: mounts orig-assets/dlc when it exists
scripts/play.sh --dlc DIR        # another root (':'-separated list)
scripts/play.sh --no-dlc         # none
```

`scripts/play.sh --check` (the preflight) counts what will mount before
anything starts: `dlc  76 song(s) in 2 package(s) under <root>:
dancecentral2dlcmegapack 32, dc2exportpackfordc3 44`, or `off (...)` with the
reasons it can be off. A song in two packages is a WARN.

- **What `import.sh <package> [dlc_root]` does.**
  - Checks the STFS package with `scripts/dlc/stfs_extract.py info`.
  - Extracts it with every block checked against its SHA-1 hash record, and lowercases every path.
  - Stages it in a dot-directory and moves it into `<dlc_root>/<package name, lowercased>`.
  - `dlc_root` defaults to `orig-assets/dlc`.
  - Refuses if the destination already exists.
  - Warns if the package has no `songs/songs.dta`.
- **Exit codes.** On a hash failure, `stfs_extract.py` exits 3 and `import.sh` passes that on. The staging directory is left in place for inspection.
- **Never commit packages.** They are game content. `orig-assets/` is gitignored and stays that way.
- **Paths must be lowercase.** `FileMakePath` lowercases every path the game builds, so the files on disk have to match.
- **Which roots mount** (`NativeDlcRoots`, `ContentMgr_Native.cpp`):

  | Environment | Roots |
  |---|---|
  | `DC3_DLC_DIR` set and non-empty | that `:`-separated list |
  | `DC3_DLC_DIR` set but empty | none (`play.sh --no-dlc`) |
  | `DC3_DLC=1` (a dev-profile default) and `<data>/dlc` is a directory | `<data>/dlc` |
  | otherwise (`DC3_DLC=0`, the release profile, `milo-tests`, no `<data>/dlc`) | none |

  - Every subdirectory of a root is one package. Dot-directories are skipped. Packages mount in name order.
  - **Why on by default in the dev profile.** A 360 lists the songs of every installed package, so with packages on disk this is the original's behaviour for the content present. Measured cost, fast boot to `song_select_screen`, 2 trials each, box load 40-50: no DLC 5.0-5.1 s; both DC2 packs cold 5.3-5.4 s, warm (song cache hit) 5.1-5.2 s; NOTIFY count unchanged (46). The profile table's rule is that a dev row does not change what the game is; this one changes the song list only by the content the user imported.
  - **Flows pick songs by name**, so a mounted package does not move them: `betteroffalone.txt`, `omg.txt` and `milkshake.txt` use the `highlight NAME` directive (scripts/dc3-input-flows/README.txt). The flows that still count presses (`ymca.txt` and `xenia-ymca.txt`, shared with Xenia; `song-select-scroll.txt`, which tests scrolling) run without DLC: ctest pins `DC3_DLC=0` for every test (`MILO_TEST_ENV`), and `scripts/dc3-agent-test.sh` sets `DC3_DLC=0` for an input script with no `highlight`. `native_assert_harvest.py` and `native_cpu_profile.py` pin it too. A box with packages imported therefore registers and skips the same tests as one without.

## How the original mounts DLC

Read from `XboxContentMgr` / `XboxContent` and the song code, not run.

1. **Discovery.** `XboxContentMgr` enumerates installed packages across titles.
   - Each package becomes an `XboxContent` whose root is `cnt%08x`, from a running counter.
   - Its `Root()` is that name with a colon: a device, `cnt00000000:`.
2. **Mount callbacks.** `NotifyMounted` calls `ContentMounted(name, root)` on every callback.
   - `SongMgr` records the root against the package's songs.
   - `HamSongMgr::SongAudioData` prefixes the root to a song's base path (`"%s/%s"`), giving `cnt00000000:/songs/milkshake/milkshake`.
3. **Play-time mounts.** At most six packages are mounted at once (LRU). `PreloadPanel` and the other panels call `MountContent` again at play time.
4. **Song cache.** After the first discovery, a package's songs come from `SongMgr`'s `songcache` container. The package itself is mounted again only to play.

The drive matters for more than file lookup.

- **Song milos list subdirs that live on the disc.** A song milo lists `../../world/shared/director.milo` and `../../sfx/audio_mixer.milo` as subdirs. Disc songs do the same.
- **`change_subdir` remaps them.** The `song` ObjectDir type in `world/world_objects.dta` has a `change_subdir` handler. When the milo's path contains `:`, it remaps those two subdirs to the disc paths (`world/shared/director.milo`, `sfx/audio_mixer.milo`). `ObjectDir::GetSubDirPath` runs this handler.
- **Without a drive, the subdirs resolve inside the package.** They then fail to load (`Could not load: .../world/shared/gen/director.milo_xbox`). Every `song.anim` PropKeys then loses its `HamDirector` target, and `game_screen` crashes in `HamDirector::SetupRoutineBuilderAnims`.
- **`FileMakePath` keeps the drive across `..`.** For example, `cnt0:/songs/x` plus `../../y` gives `cnt0:/y`.

## What native does

The two halves meet at the drive name.

- **`native/src/platform/ContentMgr_Native.cpp`**
  - Each package directory becomes a `NativeContent` with the original's `cnt%08x` root and `cnt%08x:` path.
  - Mounting a package maps that drive to the host directory, and unmounting it unmaps the drive.
  - Discovery queues the package's `songs/` files the way the base `ContentMgr` expects.
  - It logs `DC3 Native: DLC package '<name>' at cnt00000000: -> <dir>`, with ` (cached)` when the song cache already holds the package.
- **Engine `src/platform/File_Native.cpp`** (`../milo-native-engine`, landed in `c8abfc0`)
  - A 16-entry drive table: `NativeMountDrive` / `NativeUnmountDrive`.
  - `FileQualifiedFilename` turns `cnt00000000:/songs/...` into `<host dir>/songs/...`, and `FileIsLocal` answers true for a mapped drive.
  - Every engine reader goes through these, including `AsyncFile`, `FileGetStat` and the FFmpeg movie and preview reader.
- **Formats need no conversion.**
  - Song audio is mogg v0xE, the same as the disc. It decrypts through the same key path: `MoggDlcTest.DecodeDlcMilkshakeMogg`, 2 channels at 44.1 kHz.
  - Milos are `.milo_xbox` and MIDI is `.mid`, both as on the disc.
  - The 256×256 `_prev.bik` previews open through FFmpeg like the disc's.
  - The disc carries `songs/updates/<song>/` files for DLC songs. The Milkshake run raised no "no associated update file" notify, so they are probably found. Nothing else checks this.

### Bug found on the way

`DataArraySongInfo::sSaveVer` was 1 in the decomp, but the image has 2.

- `Load` reads three ints and three vectors for rev < 2 that `Save` never writes.
- So the second boot misread the song cache. It read a 544-million-character Symbol length and overran a 512-byte buffer, then crashed with SIGSEGV at `wait_main_after_saveload_screen`.
- Only content songs enter that cache, so no disc-only run had ever read one back.

## Evidence

- **Tests.** `DlcSongFlowTest.MilkshakeMountsAndReachesGameplay` and `.MilkshakePlaysAgainFromTheSongCache` (`native/tests/test_dta_flow.cpp`) and `MoggDlcTest.DecodeDlcMilkshakeMogg` (`test_mogg_v0xe.cpp`) sit behind the **`DC3_DLC_TESTS`** coverage gate.
  - CMake turns it on only when it finds `<DC3_DLC_TEST_DIR>/dancecentral2dlcmegapack/songs/milkshake/milkshake.mogg` (default root `orig-assets/dlc`) and the game assets. Then it compiles the three tests in and passes the root to them as `DC3_TEST_DLC_DIR`.
  - Without the package the tests are **not registered**. A box without DLC (CI, the GPD) therefore has the same skip count as one with it, and nobody needs to touch `skip_budget.txt`.
  - The gate is decided at configure time. After importing, reconfigure (`cmake native/build`); `TestGates.GateDecisionsMatchTheSourceRule` fails until you do. Registered-but-absent (package deleted after configure) fails the tests, naming the reconfigure.
  - To check the absent case on a box that has it: `cmake -DDC3_DLC_TEST_DIR=/some/empty/dir native/build`.
  - The test mounts a one-package root, so other imported packs do not move Milkshake in the list.
- **Input flow.** `scripts/dc3-input-flows/milkshake.txt` picks Milkshake by name (`+30 highlight milkshake`), so it plays Milkshake whatever else is mounted; `DlcSongFlowTest` asserts the directive was satisfied before it asserts the song.
- **Full song.** Headless, `DC3_POSE=external DC3_POSE_PERFORM=choreo DC3_FAST_TIME=1`, beginner difficulty:
  - 65 of 65 scored moves rated Perfect.
  - Final score 1,781,040, with the 5-star `acc_gold_star` award.
  - The run reached `perform_endgame_screen`, then the XP reward screen.

### Song cache write refused at main_screen (D-8, fixed)

`MILO_NOTIFY: UnmountAsync: !IsDone() current op is 3` came on every boot that
wrote the song cache.

- **Cause.** `CacheMgrNative::Poll` posted `cache_mgr_mount_result` while the op was still `kOpMount`. `SongMgr`'s chain runs from inside that callback: `SaveWrite` → `CacheNative::WriteAsync` (completes inline on native; a thread on the 360) → `cache_write_result` → `SaveUnmount` → `UnmountAsync`. So the unmount was refused, and `SongMgr` stayed in `kSongMgr_SaveUnmount` with the cache mounted.
- **Fix** (`native/src/platform/Cache_Stub.cpp`). `Poll` now clears the op and takes the callback before it handles it, as `CacheMgrXbox::PollMount`/`PollUnmount` clear the op first. Once the unmount is accepted it deletes the `CacheNative` whose `WriteAsync` is still on the stack, so `WriteAsync` now ends its op before posting the result and touches nothing after. Without that second half: heap corruption, SIGSEGV in `HamListRibbon::Draw`'s `operator new` at `main_screen` in the milkshake flow.
- **Measured.** One-package root, first boot to frame 700: 1 notify before, 0 after. Milkshake's first and second (cached) boots reach gameplay. In the sweeps below, 0 of the 432 DLC runs logged the notify.
- **Test.** `CacheNativeCallbackTest.UnmountFromInsideTheMountCallbackIsAccepted` drives `SongMgr`'s chain. Sabotage (the old clear-after-handle order) fails it: UnmountAsync refused, unmount result dropped.

## The six-package mount limit

`XboxContentMgr::MountContent` keeps at most six packages mounted and unmounts
the least recently mounted; `ContentMgr::PollRefresh` stops discovering while
six are mounted. `ContentMgr_Native` ports both.

Measured with eight packages (the DC2 mega pack split into eight packages of
four songs each, symlinked into a scratch root). Song select highlighted one
song of each package by name in turn, and `{content_mgr is_mounted splitNN}`
was polled every 0.5 s for 115 s:

- Never more than 6 mounted at any sample (27 distinct mount sets seen). Discovery mounted 01-06, unmounted them, then mounted 07 and the rest as they were needed.
- Song select's preview (`SongPreview::Start`) mounts the package of each song the highlight passes, so scrolling past songs of all eight packages kept the set turning over: 36 `Mounting content` lines.
- All 8 `highlight` directives were satisfied; 0 FAIL/ASSERT.

The real packages are three (DC1 sweep root) or two (dev default), so a sweep of
them never passes six. Only this split test does.

## Dance Central 1 content and the PoP key (D-5)

**What the original does** (read from the image and its DTAs, not run on a 360):

- **Which titles' packages it lists.** The XEX's alternate title IDs (optional header `0x000407FF`, parsed from both `default.xex` and `debug.xex`) are `0x545607D3` (Dance Central) and `0x373307D2` (Dance Central 2). `XboxContentMgr` enumerates with `XContentCreateCrossTitleEnumerator` for content types 2 (marketplace), 1 (saved game, buffer 5) and `0x7000` (title content, routed to `ContentTitleDiscovered`). So DC1 and DC2 packages are listed on a DC3 console by design.
- **No license gate on songs.** Nothing outside `ContentMgr` reads `LicenseBits`/`GetLicenseBits`: a song in a listed package is playable.
- **The PoP key is a store token.** The archive's `DC1ExportKeyforDC2` (STFS display name "Dance Central 2 PoP Key", title `373307D2`, one 38-byte file, `license/dc2_export.txt`, which reads "This space intentionally left blank.") is the `dc2_pop` special offer in `config/store.dta` (`0x373307D2092E4A00`, content `CA25B1E5…`). Its only use is in `store_main.dta`: owning it lets the store offer `dc1_import` (`0x373307D200000004`, content `EFC80CF8…`) to redeem. `HamStorePanel::ContentDiscovered` returns false for special-offer content (so it is mounted) and `ContentMounted` marks the offer owned. The other offers are `dc2_import` (`0x373307D93450EE00`) and `dc2_gond` (`0x373307D236CF0600`).
- **License bits.** `DC1ExportPackforDC2` carries license bits `00090000067a549c`; every other package in the archive carries `ffff…`. INFERRED: that pack is bound to one console or profile, which a 360 would check when mounting; native has no such check.

**What native does.** It mounts any package under the roots, whatever its
title ID, with no key and no license check. For a song, that is the original's
rule: the original lists DC1 and DC2 packages and does not gate their songs. The
store side (redeeming an import with the PoP key) does not exist on native;
there is no store.

**Measured.** A root with the DC1 mega pack, the DC1 Import Pack for DC2 and the
PoP key: 64 DC1 songs listed (32 + 32), and every one reached results on Easy and Expert
(table below). The PoP key's package has no `songs/` and adds nothing to the
list.

## Sweeps

`scripts/native_song_sweep.py` plays every song it can discover, one
`dc3-native` per song and difficulty: headless, `DC3_FAST_TIME`, the choreo
performer (a synthetic dancer playing the song's own choreography), the
controller in forced mode, the song picked by name. It writes `sweep.json`
(one record per run: results reached, P/A/O/B ratings, final score, modal
counts, load and wall seconds, peak RSS, problems) and `summary.md`.

```bash
python3 scripts/native_song_sweep.py --out OUT --difficulty all                   # disc
python3 scripts/native_song_sweep.py --out OUT --difficulty all --unlock-all      # + Black and Tan
python3 scripts/native_song_sweep.py --out OUT --difficulty all --dlc orig-assets/dlc --only-dlc
```

MEASURED 2026-11-14 (`np-songsweep`), headless, `--no-gpu`, 4-6 jobs on a box
at load average 40-210 (other lanes), so wall times are upper bounds:

| Sweep | Songs | Difficulties | Runs | Clean | FAIL / ASSERT | Moves not Perfect | Wall s (min / median / max) | Peak RSS MB |
|---|---|---|---|---|---|---|---|---|
| Disc | 45 | all 4 | 180 | 180 | 0 / 0 | 10 of 12,004 (2 songs) | 14 / 27 / 83 | 437-522 |
| Disc, `--unlock-all` (Black and Tan) | 1 | all 4 | 4 | 4 | 0 / 0 | 0 of 240 | 63 / 71 / 94 | 439-440 |
| DC2 mega pack + DC2 Import Pack for DC3 | 76 | all 4 | 304 | 303 (+1 rerun) | 0 / 0 | 24 of 18,025 (5 songs) | 16 / 47 / 186 | 356-505 |
| DC1 mega pack + DC1 Import Pack for DC2 + PoP key | 64 | Easy, Expert | 128 | 128 | 0 / 0 | 332 of 6,930 (48 songs) | 21 / 50 / 187 | 429-499 |

- **Every run reached the results screen** except one: `roundandround` Beginner (DC2 import pack) at load ~200. Its input script's 1800-frame wait for `main_screen` ran out during DLC discovery, the script abandoned its song pick, and the run sat on `choose_mode_screen` until the 1800 s cap. Not a game fault: rerun, it played 75/0/0/0 to results, and the sweep now paces the menus at 60 fps (a wait is 30 s of wall time) and ends a run at the first input-script TIMEOUT. `main_screen` waits measured in frames: disc max 420, DC2 DLC max 1,000, DC1 max 1,418.
- **Player 1 signed in at the difficulty screen in all 615 runs that reached it** (pad 0, profile "Player"; PLAN P-16).
- **Moves not Perfect.**
  - Disc: `chingaling` (`Whip_Step`, frac 0, a FAST_TIME artifact, STATUS.md) and `macarena` (PLAN P-11, authored).
  - DC2 DLC: `nasty`, `spiceupyourlife`, `whatislove` and `yeah` each have one Bad at frac 0.0000 on every difficulty (`yeah` 1-2), at the same measure on every difficulty whatever move is there; `myprerogative` Expert has one Awesome. Same shape as `chingaling`: on the real clock `nasty` and `yeah` Easy are all Perfect (59/0/0/0, 47/0/0/0), so it is a FAST_TIME artifact: the image's bar-line lag, PLAN H-52.
  - DC1: 48 of 64 songs, mostly Awesome: the P-11 class (a move scored against frames that are not the ones the player is shown). On Expert at a move's first occurrence, where they are the same frames, 0 of 1,642 ratings are below Perfect. PLAN H-50.
- **Known exceptions** (`np-score2`). The sweep counts two FAST_TIME-only ratings separately, so a sweep's "unexplained" number means something: a not-Perfect rating taken while the tick song position still reads beat 3 of the measure (`songbeat=3` on the `MOVE_RATING` line), and a not-Perfect rating of the same move one measure after it. `summary.md` prints a `known exc.` column and a totals line (`moves rated N: not Perfect X, of which K known exceptions (bar-line lag L + next measure of the same move F, PLAN H-52) -> U unexplained`), and `sweep.json` has `rating_totals` and each run's `bar_line_lag`. `--selftest` checks the counting on a synthetic log. Logs from before `songbeat=` never count as exceptions. MEASURED 2026-11-14 (FAST_TIME, `--no-gpu`, 6 jobs, load 35-90): 10 disc songs Easy + Expert, 1,304 rated, 7 not Perfect = 2 lag + 1 follow-on + 4 unexplained (`macarena` Easy); all of DC1 Easy + Expert, 6,930 rated, 332 not Perfect, 4 lag, 328 unexplained (H-50).
- **New modal kinds against the disc sweep** (text with numbers masked): `DataIndex is not necessarily unique` (`NavListSortMgr`, a `MILO_NOTIFY_ONCE`, every DLC run); `dc_freestyle_PN.shot ... creating target cache` (every DC1 song); `Could not find <move> in expert keys` (16 DC1 songs, PLAN H-50); `new rhythm detector window w/o finalization` with a garbage first window (disc too, PLAN H-51); one song's `left_eye`/`right_eye.lookat ... m.x.x nan`; one `"xp_level_up_reward" needs localization`. The per-song `DataNode::Equal ... game_modes.dta` notify is the image's (STATUS.md).
- **Binary.** The sweeps ran a build of this branch from before its rebase onto `9d4a733c1`; a spot check on the rebased build (`dc4b9b8b7`) with all six packages mounted at once (185 songs listed) played `12step`, `milkshake`, `sandstorm` and `becauseofyou` Easy to results with the same ratings as the sweeps.
- **What "64 disc songs" counts.** The disc's `songs/` has 64 directories: 62 songs plus `gen` and `updates`. Its `songs.dta` has 62 entries: 45 that perform lists, 1 locked by rank (`blackandtan`, listed with `--unlock-all`, the image's own `{toggle_unlock_all}` cheat) and 16 `is_fake` rows (`hb_camp_*`, `mocapbpm*`, `blackandtan_ktbfinale`, `dcimindcontrol`) that perform never lists and that were not swept.


## Not done

See PLAN Track D.
