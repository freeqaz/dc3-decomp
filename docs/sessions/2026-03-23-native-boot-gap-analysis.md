# Native Boot Gap Analysis — 2026-03-23

## Context

Investigated all systems skipped in the native boot sequence (`App.cpp` lines 280-429 vs 430-638) to determine which have global state that other code depends on without null checks.

7 concurrent subagents analyzed each skipped system. Results below.

## Systems Safe to Skip

| System | Why safe |
|--------|---------|
| **SynthPreInit** | Native `SynthInit()` creates TheSynth singleton directly |
| **audio_mixer.milo** | Redundant — common_bank.milo provides all Fader/FxSend objects |
| **PersistentFileCache** | Xbox perf optimization; `gPersistentCache` never queried after init |
| **Splash system** | `gSplashing`/`TheSplasher` null-checked everywhere; minor `mReleaseImmediate` state diff |
| **LiveCameraInput** | All access guarded by `if (GetLiveCameraInput())`; native has NativeCameraInput stub |
| **KinectGuideThread** | Kinect Guide button overlay; no global state dependencies |
| **TheServer.Init** | Xbox Live auth; null on native, guarded by existing checks |
| **TheRockCentral.Init** | Online services; null on native, DTA errors downgraded |
| **GestureMgr::DebugInit** | Pure debug skeleton visualization overlay |
| **PresenceMgr::Init** | Xbox Live presence strings; `mPresenceModes` null-checked on most paths |
| **SaveLoadManager::Init** | Native already has NativeSaveLoadStub handling DTA calls |

## Systems Missing from Native Boot (Action Required)

### 1. HamUserMgrInit(false)

**What it does**: Creates `TheHamUserMgr` (8 HamUser slots), sets `TheUserMgr` global, registers DTA handlers `foreach_user` and `get_active_user`.

**Risk**: `HamProfile::GetHamUser()` dereferences `TheHamUserMgr` **without a null check** — crash if any profile path is hit.

**Fix**: Add `HamUserMgrInit(false)` to native boot after `HamInit()`.

**Dependencies**: Needs `HamInit()` to have run (HamUser factory registration).

### 2. FixedSizeSaveable::Init(0x5C, 0x1662)

**What it does**: Sets two static ints — `sSaveVersion = 0x5C` (92) and `sMaxSymbols = 0x1662` (5730).

**Risk**: Any save/load operation hits `MILO_ASSERT(sSaveVersion >= 0)` — immediate crash.

**Fix**: Add `FixedSizeSaveable::Init(0x5C, 0x1662)` to native boot before `HamUserMgrInit`.

**Dependencies**: None.

### 3. AccomplishmentManager::Init(SystemConfig("accomplishment_info"))

**What it does**: Creates `TheAccomplishmentMgr` singleton, loads accomplishment/award configs from DTA, registers 20+ DTA handlers (earn_accomplishment, has_new_awards, etc.).

**Risk**: `TheAccomplishmentMgr->Poll()` called without null check in Xbox main loop. Song completion triggers `GetDiscSongs()` → null deref. DTA handlers referencing accomplishments crash.

**Fix**: Add `AccomplishmentManager::Init(SystemConfig("accomplishment_info"))` to native boot after `MetaPanel::Init()`.

**Dependencies**: Needs SystemConfig loaded (from `ham_keep.dta`).

### 4. MetagameRank::Init()

**What it does**: Populates XP rank tables (`gRanksArray`, `gRepeatableTasks`, `gOneTimeTasks`), registers `xp_have_deferred_award` / `xp_deferred_award` script functions, parses unlockables/tiers config.

**Risk**: Song completion → `AwardPointsForTask()` → `gRepeatableTasks->FindArray()` → null deref. `gRanksArray->Size()` → null deref.

**Fix**: Add `MetagameRank::Init()` to native boot after `AccomplishmentManager::Init()`. Also check if `MetagameRank::Preinit()` is needed (it's called inside `MetaPanel::Init()` on Xbox but gated by `#ifndef HX_NATIVE`).

**Dependencies**: Needs SystemConfig loaded. May need ProfileMgr for unlock checks.

### 5. PartyModeMgr::Init()

**What it does**: Creates `ThePartyModeMgr` singleton, reads `party_mode` config (AR objects, titles, scoring), registers with ContentMgr.

**Risk**: DTA handlers in choose_mode, dance_battle, endgame screens call `{partymode_mgr ...}` → null deref. Multiple C++ call sites dereference `ThePartyModeMgr` without null checks.

**Fix**: Add `PartyModeMgr::Init()` to native boot after `MetaPanel::Init()`.

**Dependencies**: Needs SystemConfig loaded. Registers with TheContentMgr (already initialized).

## Implementation Plan

Add these 5 calls to the native boot sequence in `App.cpp` (HX_NATIVE block), in order:

```cpp
// After MetaPanel::Init() / GameInit():
FixedSizeSaveable::Init(0x5C, 0x1662);
HamUserMgrInit(false);
AccomplishmentManager::Init(SystemConfig("accomplishment_info"));
MetagameRank::Init();
PartyModeMgr::Init();
```

All are pure data/config initialization — no Xbox hardware dependencies. Should be safe to call unconditionally on native.

## Verification

After adding:
1. `ninja -C native/build dc3-native milo-tests` — build succeeds
2. `DC3_DTA_FLOW_TESTS=1 native/build/milo-tests --gtest_filter="DtaFlowTest*"` — 7 tests pass
3. `ninja` — PPC decomp build unaffected
