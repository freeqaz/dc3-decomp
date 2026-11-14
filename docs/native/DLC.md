# DLC on the native port

Native dc3 plays downloadable content from an extracted Xbox 360 package
directory. Measured with the Dance Central 2 DLC Mega Pack (32 songs). Milkshake
was selected, played to the end, and scored, and a second boot played it again
from the song cache.

## Use it

```bash
scripts/dlc/import.sh /path/to/DanceCentral2DLCMegaPack   # -> orig-assets/dlc/dancecentral2dlcmegapack
DC3_DLC_DIR=$PWD/orig-assets/dlc native/build/dc3-native
```

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
- **`DC3_DLC_DIR` takes a `:`-separated list of roots.**
  - Every subdirectory of a root is one package. Dot-directories are skipped.
  - Packages mount in name order.
  - Unset means no DLC. This is the default because DLC songs shift song-list indices, and the input flows count presses.

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
- **Input flow.** `scripts/dc3-input-flows/milkshake.txt`: Milkshake is data index 45, 43 downs from the top.
- **Full song.** Headless, `DC3_POSE=external DC3_POSE_PERFORM=choreo DC3_FAST_TIME=1`, beginner difficulty:
  - 65 of 65 scored moves rated Perfect.
  - Final score 1,781,040, with the 5-star `acc_gold_star` award.
  - The run reached `perform_endgame_screen`, then the XP reward screen.

Also measured: the Dance Central 2 Import Pack for DC3 (`DC2ExportPackforDC3`,
45 songs). It imported with the same script. Sandstorm from it scored 61 of 61
Perfect for 1,659,408 (headless, FAST_TIME). That is one song only; see
PLAN D-3.

## Not done

See PLAN Track D.
