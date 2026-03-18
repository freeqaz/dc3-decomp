# Choreography Remixer Init Lifecycle — Root Cause Analysis

**Date**: 2026-03-18
**Goal**: Understand why the native port needs defensive hacks around the choreography remixer, trace the Xbox DTA-driven lifecycle, and define a plan to make the native port follow the same path — no hacks, just correct initialization.

---

## Executive Summary

The native port has 15 `#ifdef HX_NATIVE` guards across MoveMgr.cpp, OriginalChoreoRemixer.cpp, and SuperEasyRemixer.cpp. These guards exist because `OriginalChoreoRemixer::Init()` can fail on native (move data not loaded yet), leaving the remixer half-initialized. The guards mask this by clamping bad inputs and null-checking everywhere.

On Xbox, Init never fails because it's triggered by DTA at the right point in the loading sequence — after all move data is merged and ready. The fix is to make native follow the same DTA-driven lifecycle, not add more guards.

---

## How Xbox Does It (DTA-driven lifecycle)

### The DTA message sequence

On Xbox, the choreography system is initialized entirely through DTA script handlers. The sequence is:

```
world_panel loads world.milo
  → world.fm fires change_files
    → mMerger wired to HamDirector
      → load_game_song DTA → OnLoadSong()
        → mMerger->Select("song", songPath) → mMerger->StartLoad()
          → song .milo merges into world dir
            → OnFileLoaded("song") fires
              → venue, viz, hud mergers cascade

Meanwhile, Game::IsLoaded() state machine:
  state 0: TheMoveMgr->LoadMoveData(moveData)  ← loads MoveGraph
           SuperEasyRemixer::LoadAllVariants()   ← loads variant data
  state 1: wait for merger
  state 2: wait for audio
  state 3: done → gameplay can start

After loading completes, DTA fires:
  populate_movemgr → OnPopulateMoveMgr() → InitSong()
    ↓ populates MoveGraph.MoveParents() from merged move_graph
    ↓ sets up SongLayout, MoveChoiceSets, etc.

  {reset remixer} → SuperEasyRemixer::Reset()
    ↓ OriginalChoreoRemixer::Reset()
      ↓ DanceRemixer::Reset()
    ↓ sets mDesiredDiffs from player difficulty
    ↓ SelectMove() for each measure — virtual dispatch handles Beginner
```

**Key ordering guarantee**: By the time `populate_movemgr` fires, ALL of the following are true:
- MoveDir ("moves") is merged into the world dir
- MoveGraph is loaded and has MoveParent nodes
- SongLayout is available from the MoveGraph
- PropKeys (move/clip per difficulty) are populated in the song anims
- `MoveParents().size() > 0`

`OriginalChoreoRemixer::Init()` is called (indirectly through `InitSong()`) and succeeds because the data is ready. `SaveOriginalMoveParents()` populates `mMoveParentsByDiff[0..2]`. `SaveSuperEasyMoveParents()` populates `mSuperEasyParents`. Then Reset() runs and SelectMove() works.

### The virtual dispatch chain (Beginner difficulty)

Xbox doesn't need difficulty clamps because:

```
MoveMgr always creates SuperEasyRemixer (constructor line 36)

SelectMove() calls GetMoveParentsByDifficulty(mDesiredDiffs[player])
  → virtual dispatch (this IS a SuperEasyRemixer)
  → SuperEasyRemixer::GetMoveParentsByDifficulty(diff)
    if (diff == kDifficultyBeginner) → return mSuperEasyParents   // handled
    else → OriginalChoreoRemixer::GetMoveParentsByDifficulty(diff) // only 0,1,2
```

The base class `OriginalChoreoRemixer::GetMoveParentsByDifficulty` is only ever called with 0, 1, or 2. The `MILO_ASSERT(aDiff < kNumDifficultiesDC2)` is an "impossible state" invariant, not a runtime check.

---

## How Native Currently Does It (broken lifecycle)

### The native initialization path

```
HamDirector::Initialize() (line 536)
  → logs state for debugging
  → calls TheMoveMgr->mSuperEasyRemixer->Init()   ← LINE 577, #ifdef HX_NATIVE
```

This is a **direct C++ call** that bypasses the DTA sequence. The comment at line 565-571 explains:

> Game::IsLoaded() already loads the MoveGraph and calls LoadAllVariants()
> before we get here. But SaveOriginalMoveParents() (which populates
> per-difficulty move parent arrays from the layout) only runs inside
> OriginalChoreoRemixer::Init(). On Xbox this fires from the DTA reset
> handler; on native we call it here after the merger is complete.

### Why Init() fails

`OriginalChoreoRemixer::Init()` (line 118):
```cpp
void OriginalChoreoRemixer::Init() {
    if (TheMoveMgr->MoveParents().size() == 0) {  // MoveGraph empty?
        TheMoveMgr->InitSong();                     // Try to populate it
        if (TheMoveMgr->MoveParents().size() == 0) { // Still empty?
            MILO_FAIL("Failed to load move graph for: %s\n", ...);
            // Xbox: fatal crash (data should be ready)
            // Native: non-fatal warning, continues with broken state
#ifdef HX_NATIVE
            mTotalMeasures = 0;
            return;  // ← half-initialized
#endif
        }
    }
    SaveOriginalMoveParents();  // populates mMoveParentsByDiff[0..2]
    DanceRemixer::Init(mMoveParentsByDiff[0].size());  // resizes TheMoveMgr arrays
    BridgeGapsInMoveParents(0..2);
    // ...
}
```

When it fails, the state left behind:
- `mTotalMeasures = 0` (set by the early return)
- `mMoveParentsByDiff[0..2]` — **empty** (SaveOriginalMoveParents never ran)
- `mSuperEasyParents` — **empty** (SaveSuperEasyMoveParents never ran)
- `mDesiredDiffs[0..1]` — uninitialized
- TheMoveMgr arrays — **not resized** (DanceRemixer::Init never ran)

### The cascade of guards

Because Init can fail, every downstream consumer needs guards:

| Guard | File:Line | What it protects against |
|-------|-----------|------------------------|
| `mTotalMeasures = 0; return` | OriginalChoreoRemixer.cpp:126 | Init failure → skip rest |
| `if (MoveParents().size() == 0) return` | SuperEasyRemixer.cpp:64 | Init failed upstream |
| `if (aDiff >= kNumDifficultiesDC2) aDiff = Easy` | OriginalChoreoRemixer.cpp:149,160 | **Dead code** — virtual dispatch handles this |
| `if (!layout) return` | OriginalChoreoRemixer.cpp:173 | Layout not loaded |
| `if (MoveParents().size() == 0) return` | SuperEasyRemixer.cpp:225 | LoadAllVariants with no graph |
| `if (!layout) return` | SuperEasyRemixer.cpp:232 | Layout not loaded |
| `if (track.empty()) return` | SuperEasyRemixer.cpp:117 | DumpSongLayout with empty data |
| `if (!dirGraph) return` | MoveMgr.cpp:300 | move_graph not found in dir |
| `if (!mDefaultSongLayout) fallback` | MoveMgr.cpp:401 | SongLayout null |
| `if (songAnim) guard` | MoveMgr.cpp:412 | SongAnim not ready for SetDefaultReplacer |
| `if (!mMovesDir) return` | MoveMgr.cpp:446 | MoveDir not found |
| `if (!pMovePropKeys) continue` | MoveMgr.cpp:458 | PropKeys not populated |
| `!moveDir` vs `(unsigned int)moveDir <= 0` | MoveMgr.cpp:607 | LP64 null check (permanent) |
| `SetType("easeup_remixer")` skip | MoveMgr.cpp:37 | TypeDef config lookup crash |

All of these except the LP64 check (MoveMgr:607) and the SetType skip (MoveMgr:37) are consequences of Init running before data is ready.

### The difficulty clamps are dead code

The clamps at OriginalChoreoRemixer.cpp:149 and :160:
```cpp
if (aDiff >= kNumDifficultiesDC2)
    aDiff = kDifficultyEasy;
```

These are unreachable in both the success and failure paths:
- **Success path**: Virtual dispatch (SuperEasyRemixer) intercepts Beginner before reaching the base class
- **Failure path**: `mTotalMeasures = 0` means Reset()'s SelectMove loop never executes, so GetMoveParentsByDifficulty is never called

The clamps were added based on the false analysis that "SelectMove uses mDesiredDiffs which may be Beginner" — true, but SelectMove's call goes through virtual dispatch, so SuperEasyRemixer handles it. The clamps also don't actually fix anything: if triggered, they return `mMoveParentsByDiff[0]` which is also empty (SaveOriginalMoveParents never ran).

---

## The Fix: Make DTA Drive Init (Like Xbox)

### Investigation results (2026-03-18)

**`populate_movemgr` is editor/debug only.** It's defined in `world_objects.dta` as a debug editor button, not part of any gameplay flow. It's never called during normal "perform" mode on Xbox or native.

**The real DTA-driven init path is through the `easeup_remixer` TypeDef.** The `ham_objects.dta` config defines:

```dta
(SuperEasyRemixer          ;; ← CORRECT — matches SuperEasyRemixer::StaticClassName()
   (types
      (easeup_remixer
         (start_reset
            {resize [player_states] {gamedata max_players}}
            {resize [downgrade_measures] {gamedata max_players}}
            ;; ... initializes dynamic difficulty state vectors ...
            {$this reset})      ;; ← triggers C++ Reset() which calls Init() if needed
         (post_init)            ;; ← fired by DanceRemixer::Init() via HandleType
         (post_reset)           ;; ← fired by DanceRemixer::Reset() via HandleType
         (update_player_performance ...)
         ;; ... dynamic difficulty state machine ...
      )))
```

The lifecycle on Xbox SHOULD be:
1. `MoveMgr` constructor calls `mSuperEasyRemixer->SetType("easeup_remixer")`
2. `OBJ_SET_TYPE` macro looks up `SystemConfig("objects", StaticClassName(), "types")`
3. `StaticClassName()` returns `"OriginalChoreoRemixer"` (from `OBJ_CLASSNAME`)
4. Finds the TypeDef → registers `start_reset`, `post_init`, `post_reset` DTA handlers
5. When gameplay starts, DTA sends `start_reset` → initializes dynamic difficulty → calls `{$this reset}`
6. C++ `Reset()` → `Init()` (if needed) → `SelectMove()` → choreography ready

### Root cause: Config name mismatch breaks SetType

The `OBJ_SET_TYPE` macro uses `StaticClassName()` to look up the config:

```cpp
// OBJ_SET_TYPE expansion (Object.h:622-639)
virtual void SetType(Symbol typeName) {
    static DataArray *types = SystemConfig("objects", StaticClassName(), "types");
    //                                                ^^^^^^^^^^^^^^^^
    //                                                "OriginalChoreoRemixer"
    DataArray *found = types->FindArray(typeName, false);
    if (found) SetTypeDef(found);
    else { MILO_NOTIFY("..."); SetTypeDef(nullptr); }
}
```

Both `SuperEasyRemixer` and `OriginalChoreoRemixer` declare:
```cpp
OBJ_CLASSNAME(OriginalChoreoRemixer);   // StaticClassName() → "OriginalChoreoRemixer"
```

So `SetType("easeup_remixer")` looks up:
```
SystemConfig("objects", "OriginalChoreoRemixer", "types")
```

But the DTA config has the entry under **`SuperEasyRemixer`**, not `OriginalChoreoRemixer`:
```dta
(SuperEasyRemixer (types (easeup_remixer ...)))
```

**`SystemConfig("objects", "OriginalChoreoRemixer", "types")` fails** because there's no `OriginalChoreoRemixer` entry in the objects config. This is why the native port comment says:

> SetType("easeup_remixer") crashes because the objects config has
> (SuperEasyRemixer (types ...)) but OBJ_CLASSNAME is OriginalChoreoRemixer

**This means the `easeup_remixer` TypeDef has likely NEVER been active** — not on Xbox, not on native. The `start_reset` handler, dynamic difficulty state machine, and `post_init`/`post_reset` hooks are dead DTA code.

### Resolution: OBJ_CLASSNAME is the bug, not the DTA

**Confirmed via dc_symbols.txt** — the target binary has SEPARATE `StaticClassName()` for each class:

```
41973: OriginalChoreoRemixer::StaticClassName(void)    → returns "OriginalChoreoRemixer"
41976: SuperEasyRemixer::StaticClassName(void)          → returns "SuperEasyRemixer"
```

And SEPARATE `SetType` static variables:
```
91724: DataArray *`OriginalChoreoRemixer::SetType(...)'::`6'::types
91726: DataArray *`SuperEasyRemixer::SetType(...)'::`6'::types
```

On Xbox, `mSuperEasyRemixer->SetType("easeup_remixer")` dispatches to `SuperEasyRemixer::SetType`, which looks up `SystemConfig("objects", "SuperEasyRemixer", "types")` — **matching the DTA config exactly**.

Our decomp incorrectly has:
```cpp
// SuperEasyRemixer.h — WRONG
OBJ_CLASSNAME(OriginalChoreoRemixer);
```

Should be:
```cpp
// SuperEasyRemixer.h — CORRECT (matches target binary)
OBJ_CLASSNAME(SuperEasyRemixer);
```

The DTA config `(SuperEasyRemixer (types (easeup_remixer ...)))` was always correct. The `OBJ_CLASSNAME` bug caused `SetType` to look up the wrong class name, which crashed, which led to the `#ifndef HX_NATIVE` guard, which disabled the TypeDef, which disabled the DTA-driven init lifecycle, which required the C++ `Init()` hack in `HamDirector::Initialize()`, which could fail due to timing, which required 13 defensive guards.

### Recommended fix path

**Step 1: Fix `OBJ_CLASSNAME` in SuperEasyRemixer.h**

```cpp
// Before (wrong):
OBJ_CLASSNAME(OriginalChoreoRemixer);

// After (matches target binary + DTA config):
OBJ_CLASSNAME(SuperEasyRemixer);
```

**Step 2: Remove the `#ifndef HX_NATIVE` guard around `SetType`**

In MoveMgr constructor (line 37-44):
```cpp
// Before:
#ifndef HX_NATIVE
    mSuperEasyRemixer->SetType("easeup_remixer");
#endif

// After:
mSuperEasyRemixer->SetType("easeup_remixer");
```

**Step 3: Verify the TypeDef registers**

Add `MILO_LOG` after `SetType` to confirm TypeDef is non-null:
```cpp
mSuperEasyRemixer->SetType("easeup_remixer");
MILO_LOG("SuperEasyRemixer TypeDef: %p\n", mSuperEasyRemixer->TypeDef());
```

**Step 4: Verify `start_reset` fires**

Add `MILO_LOG` at the top of `DanceRemixer::Reset()` and `OriginalChoreoRemixer::Init()`. If `start_reset` DTA handler fires, it calls `{$this reset}` which triggers C++ `Reset()`. Log who calls Init — if it's called from within Reset's SelectMove chain (because MoveParents are empty), the DTA flow is working.

**Step 5: Remove the C++ `Init()` call from `HamDirector::Initialize()`**

Once DTA drives the init:
```cpp
// Remove this block from HamDirector::Initialize() (lines 565-579):
#ifdef HX_NATIVE
    if (TheMoveMgr && TheMoveMgr->mSuperEasyRemixer) {
        TheMoveMgr->mSuperEasyRemixer->Init();
    }
#endif
```

**Step 6: Remove defensive guards**

Once Init succeeds via DTA, the 13 downstream guards become dead code. Remove them one at a time, verifying with a full song playthrough after each removal.

### What stays permanently

| Guard | File | Reason |
|-------|------|--------|
| `!moveDir` LP64 null check | MoveMgr.cpp:607 | `(unsigned int)ptr <= 0` truncates on 64-bit — permanent |

### Fallback: `mInitialized` flag

If the DTA path turns out to not work (e.g., `start_reset` never fires because no DTA flow sends it during perform mode, and the TypeDef was genuinely always broken), add a single `mInitialized` flag as a clean guard:

```cpp
// DanceRemixer.h
bool mInitialized = false;

// OriginalChoreoRemixer::Init() — at the END, after all setup succeeds
mInitialized = true;

// OriginalChoreoRemixer::Reset()
if (!mInitialized) {
    MILO_WARN("Reset() called before Init() — skipping (move data not loaded)");
    return;
}
```

This replaces ALL 13 downstream guards with one check at the right level. The C++ `Init()` call from `HamDirector::Initialize()` would still be needed in this scenario, but all the scattered null guards would collapse into this single flag check.

### Target architecture (if TypeDef fix works)

```
MoveMgr constructor → SetType("easeup_remixer") → TypeDef registered
  → DTA handlers: start_reset, post_init, post_reset active

world_panel loads → FileMerger merges song data
  → Game::IsLoaded() loads MoveGraph + variants (already works)
  → DTA flow fires start_reset on remixer
    → initializes dynamic difficulty state
    → calls {$this reset}
      → C++ Reset() → Init() (self-bootstrapping if MoveParents empty) → SelectMove()
  → gameplay starts with correct choreography for all difficulties
```

No `#ifdef HX_NATIVE` needed in:
- OriginalChoreoRemixer::Init() (no early return)
- OriginalChoreoRemixer::GetMoveParentsByDifficulty (no clamp)
- OriginalChoreoRemixer::GetMoveVariantsByDifficulty (no clamp)
- OriginalChoreoRemixer::SaveOriginalMoveParents (no null layout guard)
- SuperEasyRemixer::Init() (no MoveParents check)
- SuperEasyRemixer::DumpSongLayout() (no empty track check)
- SuperEasyRemixer::LoadAllVariants() (no MoveParents/layout guards)
- MoveMgr constructor (no SetType skip)
- MoveMgr::LoadMoveData() (no dirGraph null guard)
- MoveMgr::GetSongLayout() (no fallback allocation, no songAnim guard)
- MoveMgr::SongInit() (no mMovesDir/PropKeys guards)

---

## Appendix: Class Hierarchy

```
Hmx::Object
  └── DanceRemixer
        │   mTotalMeasures, mPendingVariants, mUnscoredMeasures
        │   Init(int) — resizes TheMoveMgr arrays
        │   Reset() — clears arrays, sets mRoutineLoaded
        │
        └── OriginalChoreoRemixer
              │   mMoveParentsByDiff[3], mMoveVariantsByDiff[3]  (DC2 difficulties: Easy/Medium/Expert)
              │   mDesiredDiffs[2]  (per-player desired difficulty)
              │   Init() — calls InitSong if needed, SaveOriginalMoveParents, DanceRemixer::Init
              │   Reset() — sets mDesiredDiffs, calls SelectMove for each measure
              │   SelectMove() — uses virtual GetMoveParentsByDifficulty
              │   GetMoveParentsByDifficulty(int) — returns mMoveParentsByDiff[diff] (virtual)
              │
              └── SuperEasyRemixer
                    mSuperEasyParents, mSuperEasyVariants  (Beginner difficulty storage)
                    Init() — calls OCR::Init(), SaveSuperEasyMoveParents
                    Reset() — just calls OCR::Reset()
                    GetMoveParentsByDifficulty(int) — intercepts Beginner → mSuperEasyParents
                                                       else → OCR::GetMoveParentsByDifficulty
```

### Difficulty enum

```cpp
enum Difficulty {
    kDifficultyEasy = 0,
    kDifficultyMedium = 1,
    kDifficultyExpert = 2,
    kDifficultyBeginner = 3,    // DC3-added, handled by SuperEasyRemixer
    kNumDifficultiesDC2 = 3,    // Array size for DC2 choreography tracks
    kNumDifficulties = 4
};
```

The 3-slot arrays are correct — they store the 3 choreography tracks from the move graph (Easy/Medium/Expert). Beginner is a *remix* of Easy, stored in SuperEasyRemixer's own `mSuperEasyParents` vector and dispatched via virtual override.

---

## Appendix: Struct Offset Verification

OriginalChoreoRemixer member layout (from header offset comments):
- `mMoveVariantsByDiff[3]` at 0x9c — 3 × 12 (vector) = 0x24 bytes
- `mMoveParentsByDiff[3]` at 0xc0 — 3 × 12 = 0x24 bytes
- `mDesiredDiffs[2]` at 0xe4 — 2 × 4 (enum) = 8 bytes
- `unkec[2]` at 0xec — confirmed contiguous

The 3-slot sizing is verified by struct offsets. An OOB access on `mMoveParentsByDiff[3]` would read into `mDesiredDiffs[0]`, corrupting difficulty state.
