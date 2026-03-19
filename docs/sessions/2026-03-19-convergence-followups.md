# Convergence Follow-Up Items

**Date**: 2026-03-19
**Context**: Follow-ups from [convergence-cleanup-summary.md](2026-03-19-convergence-cleanup-summary.md)

---

## 1. SuperEasyRemixer::Init() Explicit Call

**File**: HamDirector.cpp:540-542 (`#ifdef HX_NATIVE`)
**Status**: Resolved — keep as-is

### Current State
```cpp
if (TheMoveMgr && TheMoveMgr->mSuperEasyRemixer) {
    TheMoveMgr->mSuperEasyRemixer->Init();
}
```

Called explicitly in `Initialize()` on native.

### What Init() Does
`OriginalChoreoRemixer::Init()` → `SaveOriginalMoveParents()`:
- Iterates the song layout for each difficulty (Easy/Medium/Hard/Expert)
- Extracts move variant names from layout arrays
- Looks up MoveParent + MoveVariant from the move graph
- Stores in `mMoveParentsByDiff[difficulty]` and `mMoveVariantsByDiff[difficulty]`
- Identifies intro move index and final pose index

### Resolution (2026-03-19 design review)
The original plan was to check if a DTA reset handler fires on native. Investigation
found that `populate_movemgr` is defined in `world_objects.dta` as an **editor-only
script action** (under the `editor` state type in WorldDir, lines 1452-1455). It
requires someone to manually send `{$hamdirector populate_movemgr}` — this never fires
during normal gameplay on *either* platform.

The explicit call on native is the **correct initialization path**, not a workaround
for a missing DTA handler. Xbox's normal game flow also calls Init() through a
different code path during song setup (not through the editor DTA handler).

No action needed. Keep the explicit call.

---

## 2. Venue Component Loading on Xbox

**File**: App.cpp:1058-1094
**Status**: Resolved — architectural, keep as-is

### Current State
App.cpp manually loads 5 component .milo files per venue (native-only):
```
world/{venue}/{venue}_buildings.milo
world/{venue}/{venue}_sky.milo
world/{venue}/{venue}_set.milo
world/{venue}/{venue}_chairs.milo
world/{venue}/{venue}_table_glasses.milo
```
Uses `DirLoader::LoadObjects` + `MergeDirs` with `kMergeInlinedMoveSharedSubdirs`.
Component suffixes are hardcoded in App.cpp:1069 (native-only code).

### Investigation Findings
- DC3 venues do NOT have `extras.fm` (unlike RB3). The extras.fm code in
  HamDirector::OnLoadSong (line 1073-1076) is dead — inherited from BandDirector.
- No DTA handler triggers component loading.
- The `mInlineSubDirType` system supports inlining (kInlineCached, kInlineCachedShared)
  but DC3 doesn't use it for venue components.

### Resolution (2026-03-19 investigation)

**Xbox ships pre-merged venues. Component loading is a permanent native requirement.**

Evidence:
- The base `<venue>.milo_xbox` already contains all geometry as internal subdirectories
  (e.g., `glitterati_base.milo` and `glitterati_geom.milo` embedded as subdirs)
- **Glitterati is the only venue with separate component files** — all 8 other venues
  (bid, dci, dclive, default, houseparty, rollerrink, streetside, throneroom) ship
  only a single base `.milo_xbox` with 0 entry-table objects (geometry in subdirs)
- The game code **never references `_all.milo`** — confirmed by exhaustive search
- Only 3 `_all` files exist in the entire ark (glitterati, pose_fatalities, voice_commander)
  — all are build pipeline artifacts, never loaded at runtime
- The `_all` variant (4.54 MB, 87 entries) is approximately the sum of the 5 components
  (4.74 MB, 68 entries), confirming it's the pre-merged geometry with shared objects added

**Why native needs manual loading**: The native port's DirLoader doesn't fully
reconstruct the subdir hierarchy from the base WorldDir. The base file has 0
entry-table objects (all content is in subdirs), so manual component merging
compensates for the missing subdir reconstruction.

**Future optimization**: Instead of loading 5 component files, could load
`glitterati_all.milo_xbox` directly (single load instead of 5). But this only
helps glitterati — other venues work from their base file alone.

---

## 3. OnLoadSong Crew/Outfit Resolution

**File**: HamDirector.cpp:1007-1044 (`#ifdef HX_NATIVE`)
**Priority**: Low
**Effort**: Small (test + remove)

### Current State
```cpp
static Symbol player_present("player_present");
Symbol outfit = hpd->Outfit();
Symbol character = hpd->Char();
bool playerPresent = false;
if (hpd->Provider()) {
    const DataNode *present = hpd->Provider()->Property(player_present, true);
    playerPresent = present && present->Int() != 0;
}
if (!playerPresent) {
    mCrews[i] = gNullStr;
    mCharacterOutfits[i] = gNullStr;
    continue;
}
if (mCrews[i].Null() && !character.Null()) {
    mCrews[i] = GetCrewForCharacter(character, false);
}
// ... outfit reconstruction
```

### Why It Exists
Native single-player flows can leave the secondary player slot populated with stale
crew state. The Xbox multiuser DTA flow (`multiuser_screen`) properly clears inactive
slots. On native, we skip that screen → stale data remains → wardrobe tries to load
an outfit for a nonexistent player 2.

### Failure Mode Without Workaround (2026-03-19 investigation)

Removing this workaround **crashes**, not just wrong visuals. Two paths:

**Path A: Player 1 has null crew (common — fresh session)**
1. `HamPlayerData::CharacterOutfit(gNullStr)` → `MILO_ASSERT(!crew.Null(), 0x4D)` — assert crash
2. If assert non-fatal: `FindArray(gNullStr)` on CREWS data → `MILO_FAIL("Couldn't find '' in array...")`
3. Additionally: `App.cpp:786` calls `GetCrewForCharacter(player1->Char(), true)` with empty symbol → `MILO_FAIL("Character: has no crew")`

**Path B: Player 1 has stale crew from previous song (less common)**
- Second dancer loads and renders on stage with no one controlling it
- No crash, but wrong visuals

### Action
**Must keep — crash-preventing, not cosmetic.** Stays until native fully implements
the multiuser DTA flow. If `multiuser_screen` properly manages player slots (clearing
crew/outfit for absent players), this workaround becomes redundant.

Do NOT remove during cleanup — accidental removal causes immediate crash on
single-player song load.

---

## 4. Interest Object Creation

**File**: App.cpp:1170-1190
**Priority**: Medium (was Low — elevated due to timing bug)
**Effort**: Medium (timing fix + verification)

### Current State
Creates a fallback audience CharInterest object at (x, y+120, z+24) for each
HamCharacter that has no interests.

### Investigation Findings
CharInterest objects **ARE serialized in character .milo files**:
- Full binary serialization (LOAD_REVS, revision 6.0) in CharInterest.cpp
- Loaded as RndTransformable objects during .milo deserialization
- Discovered via `HamWardrobe::SyncInterestObjects()` which scans the ObjectDir
- Assigned to CharEyes via `Character::SetInterestObjects()`

The FileMerger pipeline should load interests as part of the character outfit merge:
```
OnConfigureFileMerger → Select("outfit") → load outfit .milo
→ outfit objects (including CharInterest) merge into HamCharacter dir
→ HamWardrobe::SyncInterestObjects() discovers and assigns them
```

### Root Cause Analysis (2026-03-19 investigation)

**The root cause is NOT a timing bug — it's a missing call on native.**

**Xbox timing is correct**: `HamDirector::Enter()` only fires after
`AllCharsLoaded()` gate (GamePanel.cpp:950), so `SyncInterestObjects` scans
character dirs that already have outfit data merged. The call chain is:
```
GamePanel::PollForLoading waits for AllCharsLoaded()
→ HamDirector::Enter()
→ SyncScene()
→ SetNewWorld()
→ HamWardrobe::SetDir(mVenue)
→ SyncInterestObjects(mVenue)    // outfit data already merged ✓
```

**Native never calls `SyncInterestObjects` at all.** There is no code path on
native that wires outfit CharInterest objects to CharEyes. The `SetDir` →
`SyncInterestObjects` chain fires from `HamDirector::Enter()` via `SyncScene()`,
but the native venue init path doesn't go through this code.

**The fallback fires because the native port lacks the interest wiring call, not
because of a timing gap between SetDir and outfit merge.**

Additional findings:
- `SyncInterestObjects` has exactly ONE call site: `HamWardrobe::SetDir()` line 350
- `FindInterestObjects` is only exposed as a DTA handler (`find_interest_objects`)
  — no C++ code calls it directly
- `HamCharacter::SyncObjects()` handles bones, lip sync, face servo, blinking — but
  NOT interests
- Mid-session outfit changes (`ChangePlayerCharacter`) have a gap on BOTH platforms
  — no re-scan after async outfit load. Likely intentional (interests set at game start).
- `gFaceAnimInitDone` is set once, never cleared — no re-run on outfit change

### Action (two options, ordered by preference)

**Option A: Wire `SyncInterestObjects` on native** (recommended)
Ensure the native venue enter path calls `SyncInterestObjects` AFTER outfit merges
complete, matching Xbox's `Enter() → SyncScene() → SetNewWorld() → SetDir()` flow.
The most likely fix is to ensure `HamDirector::Enter()` fires on native after venue
+ character loading completes. If `Enter()` already fires, check whether
`SyncScene()` / `SetNewWorld()` is reached.

**Option B: Call `FindInterestObjects` per-character after outfit merge** (fallback)
Add `{$this find_interest_objects}` to the `on_post_delete` DTA handler for outfit
category in `char_objects.dta`. This is more granular (per-character vs whole venue)
but adds a DTA dependency.

**Option C: Keep fallback as-is** (acceptable short-term)
The synthetic interest at (x, y+120, z+24) works — characters gaze at an audience
point. It's inferior to real outfit interests (single point vs multi-point gaze) but
functional. Keep if Option A/B is too invasive.

### Verification Plan
After fix, confirm at runtime:
1. After venue enter + outfit load, `eyes->NumInterests() > 0`
2. Interest objects are from the .milo (not the synthetic fallback)
3. Characters exhibit natural multi-point gaze, not fixed-point stare

---

## 5. SongAnim Routing / Routine Builder

**File**: HamDirector.cpp:551-565 (`#ifndef HX_NATIVE`)
**Priority**: Low (long-term convergence)
**Effort**: Large (full choreography pipeline wiring)

### Current State
```cpp
#ifndef HX_NATIVE
    if (TheHamProvider->Property("merge_moves", true)->Int()) {
        return playerIndex == 0 ? mPlayer1RoutineBuilderAnim
                                : mPlayer2RoutineBuilderAnim;
    }
#endif
    return SongAnimByDifficulty(LegacyDifficulty(hpd->GetDifficulty()));
```

Native always returns the difficulty-specific song.anim (pre-authored clip/move
keyframes from the song .milo). Xbox with `merge_moves=1` returns the routine
builder anim (dynamically populated by the choreography system).

### Corrected Understanding (2026-03-19 deep research)

**The original doc's Options A and B are both wrong.** The pipeline analysis revealed:

**`OnPopulateMoveMgr` is editor-only debug code — NOT the gameplay path.**
- Defined as `populate_movemgr` in `world_objects.dta` under the `editor` state type
- Reads from non-existent relative path (`../meta/move_data.dta`)
- Calls `CleanOriginalMoveData()` which **deletes** existing clip/move data from the world
- Writes debug files (`routine_test_variants.dta`, `routine_test_parents.dta`) to CWD
- Never fires during normal gameplay on either platform

**The actual gameplay choreography path is through `OriginalChoreoRemixer::Reset()`:**
```
DTA start_reset → SuperEasyRemixer::Reset()
  → OriginalChoreoRemixer::Reset()
    → DanceRemixer::Reset()              // clears arrays
    → SelectMove(player, measure)         // for each player×measure
      → AddRoutineMove(player, measure, parent, variant)
        → InsertMoveInSong(variant, measure, player)  // populates routine builder anim
    → UpdateHamDirector()                 // triggers LoadRoutineBuilderData
```

**However, the `easeup_remixer` TypeDef was likely NEVER active** (per session doc
`2026-03-18-choreo-remixer-init-lifecycle.md:234`). This means **Xbox also uses
`merge_moves=0` and difficulty anims in normal perform mode**. The routine builder
is an editor/dev feature for previewing dynamic choreography.

### Native Currently Works Correctly

With `merge_moves=0`, native uses pre-authored difficulty anims (`song.anim` from
the Easy/Medium/Expert proxy dirs in the song .milo). These contain hardcoded clip
and move keyframes per difficulty. `SongInit()` extracts `move` and `clip` PropKeys
from these anims, and the `ClipPlayer` system reads them during gameplay.

Characters animate correctly on native without the routine builder.

### What the Routine Builder Would Enable

Dynamic move remixing — the "choose your move" feature in DC3 where players pick
from variant moves at certain points. Without it, choreography is fixed per
difficulty level. This is a gameplay feature, not a rendering one.

### Crash Risk: InsertMoveInSong Null Deref

If `merge_moves` is set to 1 without proper guards:
1. `SetupRoutineBuilderAnims()` calls `GetWorld()->Find<RndPropAnim>("player_1_routine_builder.anim", true)`
2. On native, `MILO_FAIL` is non-fatal → `Find` returns nullptr
3. `routineBuilderAnim->Copy(anim, kCopyDeep)` crashes immediately
4. Even if that's guarded, `InsertMoveInSong` calls `anim->SetKeyVal(...)` on nullptr

**Do NOT set `merge_moves=1` without confirming the routine builder anims exist in
the merged world.**

### Wiring Plan (Revised)

**Option A: Keep `merge_moves=0`** (recommended — matches Xbox perform mode)
Current state is correct. Native uses difficulty anims, same as Xbox in perform mode.
No code changes needed. The `#ifndef HX_NATIVE` guard on SongAnim routing is correct.

**Option B: Wire `Reset()` after `Init()`** (future — for dynamic choreography)
If dynamic move remixing is wanted:
```cpp
#ifdef HX_NATIVE
if (TheMoveMgr && TheMoveMgr->mSuperEasyRemixer) {
    TheMoveMgr->mSuperEasyRemixer->Init();
    if (mPlayer1RoutineBuilderAnim && mPlayer2RoutineBuilderAnim) {
        TheMoveMgr->ResetRemixer();  // SelectMove → AddRoutineMove → InsertMoveInSong
        // Set merge_moves=1 to enable routine builder path in SongAnim()
    }
}
#endif
```
Prerequisites:
- `player_1_routine_builder.anim` and `player_2_routine_builder.anim` exist in world
- Null guard in `InsertMoveInSong` before `anim->SetKeyVal()`
- `SetupAnims()` must have already run (it does — first call in `Initialize()`)

**Option C: Call `OnPopulateMoveMgr()` directly** (WRONG — do not do this)
~~Trigger the DTA handler~~ — This is editor-only code that destroys song data.
Listed here only to document why this was rejected.

### Key Files
| File | What |
|------|------|
| HamDirector.cpp:564-584 | SongAnim routing (guard is correct as-is) |
| HamDirector.cpp:465-479 | SetupAnims (first call in Initialize) |
| HamDirector.cpp:743-766 | SetupRoutineBuilderAnims (null crash risk) |
| HamDirector.cpp:2112-2126 | OnPopulateMoveMgr (**editor-only, do NOT call**) |
| HamDirector.cpp:2060-2110 | LoadRoutineBuilderData |
| HamDirector.cpp:2800-2826 | OnPopulateFromMoveMgr |
| MoveMgr.cpp:146-161 | InsertMoveInSong (null crash path) |
| MoveMgr.cpp:493-507 | FillRoutineFromParents (editor path) |
| MoveMgr.cpp:509-512 | ResetRemixer (gameplay path) |
| OriginalChoreoRemixer.cpp:56-66 | Reset → SelectMove loop |
| OriginalChoreoRemixer.cpp:89-116 | SelectMove → AddRoutineMove |
| DanceRemixer.cpp:365-387 | AddRoutineMove → InsertMoveInSong |
| OriginalChoreoRemixer.cpp:118-140 | Init / SaveOriginalMoveParents |
| App.cpp:1269 | merge_moves hardcoded to 0 (correct) |

---

## 6. ReplaceNode Suppressed Erase Warnings

**Files**: ObjPtr_p.h:223-245, Object.cpp:21-44, FlowNode.cpp:23-38
**Priority**: Low (monitoring + incremental hardening)
**Effort**: Medium (audit + targeted fixes)

### Mechanism

When `ObjRef::ReplaceList` walks the ref ring to replace all references to an object,
it sets `gInReplaceList = true`. During this walk, `ObjPtrVec::ReplaceNode` may need
to erase a node (when replacing with nullptr in a kObjListNoNull list). But
`std::vector::erase` shifts subsequent elements via assignment operators, which call
`SetObjConcrete` → `Release/AddRef` — corrupting the very ring being walked.

The fix: suppress the erase during ring walks, leaving a null entry in the vector.

```cpp
// ObjPtr_p.h:223-245
if (!gInReplaceList) {
    mNodes.erase(mNodes.begin() + (n - mNodes.begin()));
} else {
    MILO_WARN("ReplaceNode: suppressed erase ...");
    // Node left in vector pointing at nullptr
}
```

### Current Cleanup Mechanisms

1. **Vector destructor** — `~ObjPtrVec()` calls `mNodes.clear()`, safely handles nulls
2. **FlowNode::~FlowNode()** — explicitly detects and erases null entries
3. **Iteration code** — some sites check `if (*it)` before dereferencing

### Known Issues

1. **No immediate cleanup after ReplaceList** — null entries persist until destruction
   (can be many frames). All code iterating the vector during this window sees nulls.

2. **Inconsistent null-checking** — some iteration sites don't check for nulls:
   ```cpp
   // FlowPickOne.cpp — no null check
   FOREACH (it, mChildNodes) {
       items.push_back(it->Obj());  // Could push nullptr
   }
   ```

3. **Size assumptions** — code assuming `size() > 0` means "live children" is wrong
   when suppressed entries exist.

4. **No dedup check** — during merges, duplicate entries can be created in ObjPtrVec.
   The 2026-03-18 staff review noted this: "Deduplicating in ReplaceNode at merge time
   would prevent the root cause."

### Runtime Observations
```
ReplaceNode: suppressed erase during ReplaceList (owner=m:FlowLabel:Enter.flow (ui/choose_mode/choose_mode.milo))
ReplaceNode: suppressed erase during ReplaceList (owner=f:FlowLabel:Enter.flow (ui/choose_mode/choose_mode.milo))
ReplaceNode: suppressed erase during ReplaceList (owner=g:FlowLabel:Enter.flow (ui/choose_mode/choose_mode.milo))
```

These fire during UI screen transitions (choose_mode_screen). The FlowLabel/FlowNode
objects in choose_mode.milo are being replaced during merges, leaving null entries in
child node lists. Currently benign because FlowNode iteration has null checks, but
indicates the mechanism fires in real gameplay.

### Improvement Plan (Ordered by Priority)

**A. Audit iteration sites for missing null checks** (Low effort, high safety)
Grep for all `FOREACH` or `for` loops over ObjPtrVec/ObjPtrList and verify each
has null checks when `gInReplaceList` could have fired. Focus on:
- FlowNode child iteration
- FlowPickOne item collection
- Any ObjPtrVec iteration in merge-adjacent code

**B. Add dedup check in ReplaceNode** (Medium effort)
When replacing a node with a non-null object, check if that object already exists
elsewhere in the vector. If so, erase the redundant entry instead of creating a
duplicate. This prevents the root cause per the staff review.

**C. Deferred cleanup after ReplaceList** (Medium effort)
After `gInReplaceList` is set back to false, iterate affected ObjPtrVecs and remove
null entries. Requires tracking which vectors were affected during the walk (could
use a thread-local list).

**D. Debug instrumentation** (Low effort)
Add `MILO_WARN` to the `SetObjConcrete` stale ref path (ObjPtr_p.h:40-63) which
currently silently drops ring membership. This path fires when an object is being
deleted while another ref still points at it.

### Key Files
| File | What |
|------|------|
| ObjPtr_p.h:223-245 | ReplaceNode suppression logic |
| ObjPtr_p.h:218-220 | Vector destructor cleanup |
| Object.cpp:21-44 | ReplaceList + gInReplaceList flag |
| Object.h:27 | gInReplaceList declaration |
| FlowNode.cpp:23-38 | Manual null cleanup in destructor |
| Utl.cpp:353-415 | MergeObjectsRecurse (triggers ReplaceList) |
| 2026-03-18 staff review | Recommended dedup fix (not yet implemented) |
