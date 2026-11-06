# Native save persistence — implementing the image's chain

Status: **phases 1-4 landed, phase 5 remaining** (2026-11-06). Written after a read-only survey of
the whole save/load surface, so the next lane does not have to re-derive it.

Goal: profile progress, song cache and global options survive a restart on the native port, with
`sUnlockAll` finally false. The approach is the image's own `SaveLoadManager` state machine running
natively over a local directory — not a native side-channel that writes a blob the image never reads.

## What the image does

`SaveLoadManager::Poll` (`src/lazer/meta_ham/SaveLoadManager.cpp:926-1451`) is a per-frame state
machine, inert until `Activate()` sets the load/save flags. Four groups of states, four external APIs:

| group | states | drives |
|---|---|---|
| autoload device search | `4,5,9,0xB` | `TheMemcardMgr.OnSearchForDevice/SelectDevice/SetDevice/OnLoadGame`, completion via `MCResultMsg` |
| song cache | `0x14..0x23` | `TheCacheMgr->SearchAsync/MountAsync/UnmountAsync/DeleteAsync`, `Cache::ReadAsync/WriteAsync/GetFileSizeAsync` |
| global options | `0x24..0x41` | the same cache API plus `TheProfileMgr.{GetGlobalOptionsSize,SaveGlobalOptions,LoadGlobalOptions}` |
| profile save/load | `0x43..0x66` | `TheMemcardMgr.{IsStorageDeviceValid,OnSaveGame,OnLoadGame,OnDeleteSaves}`, `SaveMemcardAction`/`LoadMemcardAction`, `MCResultMsg` |

A complete profile save is: `AutoSave` → idle → `Start` → song cache (if dirty) → global options (if
dirty) → `0x54` `GetAutosavableProfile` + `IsStorageDeviceValid` → `0x46` `new SaveMemcardAction` →
`TheMemcardMgr.OnSaveGame` → `PreAction` serializes into `mSaveDataBuffer` → completion message →
`SaveLoadProfileComplete` → `0x66` `SaveLoadAllComplete`.

## What is missing on native today

- **`TheCacheMgr` is null.** `CacheMgrInit()` is inside `#ifndef HX_NATIVE`
  (`src/system/os/System.cpp:502-516`), `CacheMgr::CreateCacheMgr()` returns `new CacheMgrXbox()`
  whose constructor is a weak stub (`native/src/engine_stubs_generated.cpp:311-313`), and
  `native/src/platform/Cache_Stub.cpp` is a comment-only placeholder. `CacheMgr`'s base
  `SearchAsync`/`ShowUserSelectUIAsync`/`CreateCacheIDFromDeviceID`/`CreateCacheID` are
  `MILO_FAIL("not supported by this platform")` bodies (`src/system/utl/CacheMgr.cpp:17-42`).
- **`TheMemcardMgr` is never initialized** (`src/lazer/meta_ham/MetaPanel.cpp:172-176`, behind
  `#ifndef HX_NATIVE`), and `native/src/platform/MemcardMgr_Stub.cpp` breaks the message contract:
  `ThreadDone` for save/load calls `SaveLoadProfileComplete` directly and exports **no**
  `MCResultMsg`, so `SaveLoadManager` (which sets `mWaiting` and waits for that message, `OnMsg`
  ~652-660) never advances; `OnSearchForDevice` exports `kMCNoError` where the state-4 handler needs
  `kMCFileExists`; `OnCheckForSaveContainer` performs the entire load inline instead of only testing
  for the file; `Init()` never calls `SetName("memcardmgr", ...)` so DTA `{memcardmgr add_sink …}`
  cannot find it; the constructor leaves the container/device arrays and the save buffer unzeroed.
- **DTA talks to a stub.** `src/App.cpp:494` registers `NativeSaveLoadStub` under the name
  `saveload_mgr`, which answers `is_idle`=1 and ignores `activate`; the real
  `SaveLoadManager::Init()` sits inside `#ifndef HX_NATIVE` (`src/App.cpp:669`).
- **`sUnlockAll` is forced true** (`MetaPanel.cpp:196`) precisely because nothing is persisted: a
  fresh-profile lock set would otherwise be permanent. It flips last, not first.
- `Memcard`/`MCContainer`/`MCFile` are no-ops (`native/src/platform/Memcard_Stub.cpp`,
  `src/system/os/Memcard.cpp:5-20`). They stay dead as long as the native MemcardMgr writes
  `mSaveDataBuffer` directly — they are only needed to port `MemcardMgr_Xbox.cpp` itself.

## The on-disk contract is plain-file safe

The Xbox container contributes a name, a size and device selection; the payload is a flat byte
buffer written verbatim. Concretely: profile = container `savegame`, file `save.dat`, a
`FixedSizeSaveableStream` with version 92, a symbol table, and `HamProfile::SaveFixed`
(`HamMemcardAction.cpp:16-26`); global options = cache and file both named `global`, version 0x1C,
fixed size `GetGlobalOptionsSize()`; song cache = `SONG_CACHE_CONTAINER_NAME` `songcache`,
`gSongCacheSaveVer` 12, plain `BufStream`, no encryption. Profile encryption is self-contained
(`EnableWriteEncryption` writes a seed and XORs with `Rand2`, `src/system/utl/BinStream.cpp:120-125`).

One caveat: the profile stream is **fixed-length**. A short local file leaves an uninitialized tail
that `LoadFixed` would parse, so treat `fileSize != mSaveDataLength` as `kMCCorrupt` rather than
trusting the size check in `FixedSizeSaveable.cpp:153-160` to fire first.

## Phases

1. **Cache layer** — `CacheID`/`Cache`/`CacheMgr` native implementations over
   `$DC3_CACHE_DIR` (else `$HOME/.local/share/dc3/cache`), in `native/src/platform/Cache_Stub.cpp`;
   one permitted matched-source edit, an `#ifdef HX_NATIVE` branch in
   `CacheMgr::CreateCacheMgr()`. **Lane w38-cache.**
2. **MemcardMgr contract** — port the image's `ThreadDone` / `OnSearchForDevice` /
   `OnCheckForSaveContainer` / `OnSaveGame` shapes onto the existing local-file I/O in
   `native/src/platform/MemcardMgr_Stub.cpp`. The image's own `ThreadDone` is quoted in
   `src/system/meta/MemcardMgr_Xbox.cpp:85-117`. **Lane w38-memcard.**
3. **Wiring** — call the native cache init, `TheMemcardMgr.Init()` and `SaveLoadManager::Init()` on
   native, and retire `NativeSaveLoadStub` in the same commit that makes the real manager reachable
   (`SetName` `MILO_FAIL`s on a name collision, `src/system/obj/Object.cpp:582-585`). Every edit in
   matched sources stays inside `#ifdef HX_NATIVE`, verified with a full `ninja` plus a 0 UP / 0 DOWN
   report diff and `run_objdiff` on each touched function.
4. **Profile selection** — `ProfileMgr::InitNative()` currently pre-marks pad 0 `kMetaProfileLoaded`
   (`ProfileMgr.cpp:312-316`), and `GetNewlySignedIn()` only returns unloaded profiles
   (`:714-725`), so the image's autoload finds nothing and skips the file. Pad 0 must stay signed in
   but **unloaded**, and let the load mark it loaded.
5. **`sUnlockAll = false`** — only after a load demonstrably restores campaign/rank state.

### Phase 3 landed — what it changed beyond the four call sites

Wiring the chain has one user-visible consequence, because `enter_gameplay`
gates on `{profile_mgr has_seen_tutorial kTutorialGeneral}` (`ui/global.dta:230`)
and `ProfileMgr::HasSeenTutorial` is

    mTutorialsSeen & tutorial || mGlobalOptionsSaveState != kMetaProfileLoaded

The second clause is the image's "persistence is broken, do not nag" fallback.
Native took it on every boot while nothing loaded the global-options cache, so
the first-run move tutorial was always suppressed.  With the cache wired it
loads, a fresh profile has the bit unset, and the tutorial appears between
`multiuser_screen` and `loading_screen` (`ui/tutorial/tutorial.dta:1014-1109`).
That is the image's behaviour; `scripts/dc3-input-flows/ymca.txt` now drives its
five screens.  Worth knowing before phase 5: the same clause is why a machine
with **no** cache directory behaves like a first-run console, and why the DTA
flow tests' expected screen chain depends on `$DC3_CACHE_DIR`.

### Phase 4 landed — pad 0 signed in, unloaded, and loaded by the load

`ProfileMgr::InitNative()` is now `Init()` and nothing else: the
`SetSaveState(kMetaProfileLoaded)` fiction is gone.  Pad 0 is signed in by the
native platform manager and stays `kMetaProfileUnloaded`, which is what makes
`SaveLoadManager`'s autoload find it (`GetNewlySignedIn` returns unloaded
profiles) and read the file.

**The missing-file path is the image's own.**  With an empty `DC3_SAVE_DIR` the
state machine runs (`SetState` trace, decimal -> hex):

    1 -> 2 -> 0x15 -> 0x20 -> 0x21 -> 0x23 -> 0x26 -> 0x2C -> 0x31 -> 0x33
    -> 0x35 -> 0x38 -> 3 -> 5 -> 0xB -> 0x45 -> 0x46 -> 0x43 -> 3 -> 0x67 -> 0

`0xB` is `OnLoadGame`, whose result is `kMCFileNotFound`; `0x45` re-checks the
container (not found again); `0x46` runs the image's create — a
`SaveMemcardAction` with `createType 1` — which writes `profile_0_save.dat`
(1,153,964 B); `0x43` calls `SaveLoadProfileComplete(profile, 1)`, and that is
the only thing that marks the profile loaded.  So a fresh console and a fresh
native save directory behave the same way: the first boot creates the profile
and then loads it.

The one native-only decision is in `SaveLoadManager::SetState`'s `case 5`
(`kS_AutoloadDeviceFound`: a device was found with no save on it).  The image
branches on `mDeviceIDState == 2` ("a device id was already chosen") between
loading anyway (state 9) and opening the `mc_auto_load_no_save_found` dialog
(state 6, `mc_button_create_data` / `mc_button_cancel`).  A healthy native boot
reached state 5 with `mDeviceIDState == 2` **because the song-cache mount had
chosen the device** — the right answer for the wrong reason.  Native now takes
the load arm unconditionally (`#ifdef HX_NATIVE`), because the save directory
*is* the device and `MemcardMgr` has no second one to offer.  With that forced
back to `mDeviceIDState == 0`, the pre-change binary stops in state 6 with
`get_dialog_msg` = `mc_auto_load_no_save_found_fmt`, writes no file, and leaves
`get_savable_profile_count` at 0 for the whole session.

**The unloaded window is the image's too.**  Nothing loads a profile before the
`wait_main_after_saveload_screen` enter handler calls `{saveload_mgr activate}`,
so attract -> autosave_warning -> title run with pad 0 unloaded on the console as
well.  At `title_screen` the DTA answers `has_active_profile` 0,
`get_savable_profile_count` 0, `get_num_valid_profiles` 0,
`get_first_savable_profile` null, `is_initial_load_done` 0 — no reader sees stale
data, and the readers reachable there are already gated: `title.dta`'s
`check_for_nag` block is behind `{profile_mgr has_active_profile}`;
`SkeletonIdentifier::SetUpInitialProfiles` sets fitness only for a valid
profile, and `HandleProfileLoadComplete` re-runs that same function after the
load; `ProfileMgr::PurgeOldData` tests for `kMetaProfileDelete`;
`AccomplishmentProgress::Poll` is a no-op with no pending awards.  Everything
else (`HamUI::TakeSnapshot`, the endgame paths, `MetaPerformer`, `BustAMovePanel`)
runs after gameplay, i.e. after the load.

## How to know it works

Not "the flow stops erroring". A save must be written, the process restarted, and the *same* profile
values read back — campaign progress, unlocked content, char prefs, playlists. `sUnlockAll` flipping
to false is the end-to-end proof, because it removes the mask that hides a failed load: with the mask
on, a broken load and a working one look identical.
