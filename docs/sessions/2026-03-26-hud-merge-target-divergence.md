# HUD Merge Target Divergence — Investigation & Roadmap

**Date:** 2026-03-26
**Status:** Workaround in place, convergence pending
**Files changed:** `src/system/world/Dir.h`, `src/system/hamobj/HamDirector.cpp`, `src/lazer/game/GamePanel.cpp`

## Symptom

Entering gameplay (song play) crashed with SIGSEGV. The DTA variable `$hud` was null because `get_player_hud` in `hud_objects.dta` could not find `hud_left`/`hud_right` inside `$hud_panel`. The `[player_huds]` array contained nulls, populated by `{$this find "hud_left" FALSE}` on a PanelDir that lacked the merged HUD content.

## Architecture: How Xbox Loads the Gameplay HUD

The gameplay HUD is loaded through the FileMerger pipeline, not by direct `.milo` load:

```
GameMode::SetMode()
  → FileMerger::HandleType(load_game_hud)   [char_objects.dta selects e.g. _default_hud.milo]
  → FileMerger::StartLoad(async)
  → ... loader completes ...
  → FileMerger::FinishLoading()
    → MergeDirs(source, MergerDir(), ...)    [merge HUD objects INTO target]
  → PostMerge → HamDirector::OnFileMerged("game_hud")
    → $hud_panel set, Enter() called, DTA enter handler fires
```

### Merger Target Resolution

Each `FileMerger::Merger` has an `ObjPtr<ObjectDir> mDir` (offset 0x28) that specifies the merge target. `MergerDir()` returns:

```cpp
ObjectDir *MergerDir() {
    if (mDir)         return mDir;               // explicit target
    else              return mDir.Owner()->Dir(); // fallback: FileMerger's parent dir
}
```

The `mDir` is deserialized from the binary `.milo` data at load time (`d >> fm.mDir`). On Xbox, the `game_hud` merger's `mDir` is configured in `director.milo` to point at a PanelDir named `"hud"` — which is the same object as `WorldDir::mHUD`. This means:

1. `_default_hud.milo` content (hud_left, hud_right, flash_cards, scores, Cam.cam, flows) merges **directly into** the WorldDir's mHUD PanelDir
2. The mHUD already has the "hud" DTA type (from `hud_objects.dta`)
3. When mHUD enters, its DTA `enter` handler fires: `{set $hud_panel $this}`
4. `{$this find "hud_left" FALSE}` succeeds because the merged children are in `$this`
5. `[player_huds]` is correctly populated with the merged subdirs
6. WorldDir naturally draws/polls mHUD — all DTA handlers fire on the correct object

**Key invariant on Xbox:** The object that DTA handlers execute on (`$this` in hud_objects.dta) IS the object that received the merged content. They are the same PanelDir.

### What Native Does Differently

On native, the `game_hud` merger's `mDir` resolves to a **different** PanelDir — not the WorldDir's mHUD. This creates two separate "hud" PanelDirs:

| | Xbox | Native |
|---|---|---|
| Merge target | WorldDir::mHUD | Separate merger PanelDir |
| DTA `$this` | WorldDir::mHUD | WorldDir::mHUD (wrong one) |
| Has hud_left/hud_right | Yes (merged in) | No (empty) |
| `$hud_panel` points to | mHUD (correct) | mHUD (wrong — no children) |

The WorldDir's inline mHUD enters after `OnFileMerged`, and its DTA `enter` handler overwrites `$hud_panel` with itself. Since the merged content is in the other PanelDir, `$hud` resolves to null → crash.

## Open Questions for Convergence Investigation

### Q1: Why does `mDir` resolve differently on native?

The `game_hud` merger's `mDir` is an `ObjPtr<ObjectDir>` deserialized from `director.milo` binary data. On Xbox, it resolves to the "hud" PanelDir within the WorldDir. On native, it appears to resolve to null (falling back to the FileMerger's parent dir) or to a different object.

**Hypothesis A — Loading order:** The "hud" PanelDir may not be registered in the directory when `mDir` is deserialized. ObjPtr resolution depends on the target being findable by name in the object's directory at deserialization time. If the WorldDir's children load in a different order on native, the "hud" PanelDir may not exist yet.

**Hypothesis B — ObjPtr cross-directory resolution:** The ObjPtr may need to resolve across directory boundaries. Native's ObjPtr deserialization may handle cross-directory references differently.

**Hypothesis C — mDir is intentionally null:** The merger may rely on the fallback path (`mDir.Owner()->Dir()`), and on Xbox the FileMerger's parent IS the mHUD PanelDir (because the FileMerger is a child of mHUD, not the WorldDir). This would need verification by checking the object hierarchy in director.milo.

**Investigation steps:**
1. Add logging to `FileMerger::Merger` deserialization to print what `mDir` resolves to on native
2. Dump the `game_hud` merger's mDir, mProxy, mName after `PreLoad` completes
3. Check the object hierarchy in director.milo: is the GameModeMerger a child of the WorldDir, or a child of the "hud" PanelDir?
4. Compare `MergerDir()` return value on native vs what it should be (the "hud" PanelDir)

### Q2: What is the object hierarchy in director.milo?

The GameModeMerger (`mGameModeMerger`) is referenced from `HamDirector::mGameModeMerger`. The HamDirector lives in the WorldDir (from director.milo). But where does the GameModeMerger FileMerger object live in the dir tree?

If the tree is:
```
WorldDir (director.milo)
  ├─ HamDirector
  ├─ FileMerger "GameModeMerger"
  │   └─ Merger "game_hud" { mDir → "hud" PanelDir }
  └─ PanelDir "hud" (= mHUD)
      ├─ (empty until merge)
      └─ after merge: hud_left, hud_right, flash_cards, ...
```

Then `mDir` explicitly points to the "hud" PanelDir, and the merge goes there.

But if the tree is:
```
WorldDir (director.milo)
  ├─ HamDirector
  └─ PanelDir "hud" (= mHUD)
      └─ FileMerger "GameModeMerger"   ← FM is INSIDE mHUD
          └─ Merger "game_hud" { mDir = null }
```

Then the fallback `mDir.Owner()->Dir()` returns the "hud" PanelDir (the FileMerger's parent), and it still works. This would explain the native issue: if native places the FileMerger at a different point in the hierarchy, the fallback returns the wrong directory.

**Investigation steps:**
1. Use the `/asset-extract` skill or Ghidra to inspect the director.milo object hierarchy
2. Check what `Dir()` returns for the GameModeMerger FileMerger on native
3. Verify if `mDir` is set or null in the deserialized data

### Q3: Does the WorldDir re-enter mHUD after OnFileMerged?

If the WorldDir's inline mHUD gets entered multiple times (once before the merge, once after), the DTA `enter` handler would fire twice. The second enter would overwrite `$hud_panel` and re-populate `[player_huds]`. If this second enter happens on the wrong mHUD, the data is wrong.

**Investigation steps:**
1. Add logging to the WorldDir::Enter path for mHUD enter
2. Track how many times the hud_objects.dta `enter` handler fires
3. Determine if the `SetHUD` swap prevents the double-enter problem or just masks it

## Current Workaround (What We Shipped)

Three changes, all `#ifdef HX_NATIVE`:

### 1. `WorldDir::SetHUD()` — Dir.h
```cpp
void SetHUD(RndDir *hud) { mHUD = hud; }
```
Public setter for the private mHUD member. Allows the HamDirector to replace the inline mHUD with the merger's PanelDir.

### 2. `HamDirector::OnFileMerged` — HamDirector.cpp
After the game_hud merge completes, replaces the WorldDir's inline mHUD:
```cpp
WorldDir *world = GetWorld();
if (world) {
    world->SetHUD(hudDir);
}
```
This ensures the WorldDir draws/polls the PanelDir that has the merged content. When this PanelDir enters, its DTA `enter` handler correctly sets `$hud_panel = $this` and `[player_huds]` finds `hud_left`/`hud_right`.

**This is a hack.** It swaps mHUD after the merge instead of fixing why the merge goes to the wrong target. On Xbox, this swap is unnecessary because the merge goes directly into mHUD.

### 3. `GamePanel::SetTypeDef` + `HamDirector::OnSelectCamera` — GamePanel.cpp, HamDirector.cpp
Defensive re-resolution of `$hud_panel` from the GameModeMerger before DTA code runs:
```cpp
FileMerger::Merger *gm = fm->FindMerger("game_hud", false);
PanelDir *mergerHud = gm ? dynamic_cast<PanelDir*>(gm->MergerDir()) : nullptr;
if (mergerHud) {
    DataVariable("hud_panel") = (Hmx::Object *)mergerHud;
}
```
**These are defensive hacks.** If the `SetHUD` swap works correctly, these are redundant — the DTA enter handler on the correct mHUD would set `$hud_panel` naturally. They guard against timing edge cases.

## Testable Invariants for Convergence

These can be verified at runtime or in integration tests:

### T1: Merge target identity
```
ASSERT(game_hud_merger->MergerDir() == world->GetHUD())
```
After the GameModeMerger loads, the game_hud merger's `MergerDir()` should be the same object as `WorldDir::mHUD`. On Xbox this is always true. On native, our workaround forces it.

**Convergence goal:** Make this true without the SetHUD hack — fix the mDir resolution so the merge goes directly into mHUD.

### T2: $hud_panel identity
```
ASSERT(DataVariable("hud_panel").GetObj() == world->GetHUD())
```
The DTA variable `$hud_panel` should point to the WorldDir's mHUD. Both the DTA enter handler and the C++ code set this. If the merge target is correct (T1), this follows naturally.

### T3: player_huds populated
```
hud_panel = DataVariable("hud_panel").GetObj()
player_huds = hud_panel->Property("player_huds")
ASSERT(player_huds.Array()->Size() == 2)
ASSERT(player_huds.Array()->Node(0).Type() == kDataObject)  // hud_left
ASSERT(player_huds.Array()->Node(1).Type() == kDataObject)  // hud_right
```
After the HUD enter handler fires, `[player_huds]` must contain two valid RndDir objects (hud_left and hud_right). These are found via `{$this find "hud_left" FALSE}` on the HUD PanelDir, so they must be children of the merge target.

### T4: get_player_hud returns non-null
```
For each player_index in [0, 1]:
  provider = gamedata->PlayerProp(player_index, "provider")
  IF provider is non-null:
    side = provider->Property("side").Int()   // 0=left, 1=right
    hud = player_huds[side]
    ASSERT(hud != null)
```
This verifies the full chain: provider → side → player_huds lookup.

### T5: No SIGSEGV during gameplay
Integration test: boot → navigate to song → enter gameplay → survive 500+ frames without crash. The existing `scripts/dc3-input-flows/ymca.txt` flow exercises this path.

## Flashcard Pipeline Dependency

The flashcard system is the primary consumer of the HUD merge. Understanding its DTA call chain clarifies why the merge target matters:

### DTA call chain (hud_objects.dta)

```
WorldDir::Poll
  → HandleType(select_camera)
    → HamDirector::OnSelectCamera
      → SymbolKeys::SetFrame (song anim prop keys)
        → HamDirector::Handle → PanelDir::Handle [on mHUD]
          → DTA halfbeat handler (hud_objects.dta)
            → {$this update_flashcards $beat}
              → foreach_int $player_index 0 2:
                  {set $hud {$this get_player_hud $player_index}}
                  {$hud set_anim_frame $beat}                     ← NEEDS $hud non-null
                  {size {$hud get (flash_cards)}}                 ← NEEDS flash_cards child
                  {elem {$hud get (flash_cards)} $card_index}     ← NEEDS flashcard objects
                  {{elem ...} get cur_move}                       ← NEEDS HamMove refs
                  {{elem ...} get hidden}                         ← NEEDS visibility state
```

### Objects involved (all must be children of the merge target)

| Object | Type | Role |
|--------|------|------|
| `hud_left` | RndDir | Left-side flashcard container (player 1, side=0) |
| `hud_right` | RndDir | Right-side flashcard container (player 0, side=1) |
| `flash_cards` | property on hud_left/hud_right | Array of flashcard sub-objects |
| `Cam.cam` | RndCam | HUD camera (FOV=0.6 rad, Y=-768) |
| `score_left` / `score_right` | RndDir | Score display containers |
| `left_score.trans` / `right_score.trans` | RndTransformable | Score positioning |
| `show_left_score.anim` / `show_right_score.anim` | RndAnimatable | Score slide-in anims |

All of these are loaded from `_default_hud.milo` and must end up as children of the PanelDir that DTA handlers execute on (i.e., the WorldDir's mHUD). If the merge target is wrong, none of these are findable.

### What was crashing (now fixed by workaround)

The `get_player_hud` function returns null when `[player_huds]` contains nulls. The callers (`update_flashcards`, `set_anim_frame`, beat handlers) don't guard against null `$hud`. The SIGSEGV occurred at `DataArray::Node()` when DTA tried to evaluate `{$hud get (flash_cards)}` with `$hud = <null>`.

### What still needs work after convergence

1. **Flashcard visibility** — prior work noted flashcards render as "additive zero-color" (invisible). This is a material/blend issue separate from the merge target.
2. **Flashcard positioning** — `hud_left`/`hud_right` local transforms are repositioned in OnFileMerged via hardcoded offsets (`x=±100, z=30`). These need tuning or the HUD camera FOV hack needs a proper fix.
3. **Flashcard dock panel** — `flashcard_dock_panel` is a separate UIPanel on game_screen (panel[6]), not part of the HUD merge. It's correctly hidden in quickplay (campaign-only feature).
4. **Count-in animation** — `{$hud do_count_in 0 TRUE}` at hud_objects.dta:851 fires when a new move starts. Needs the HUD to be fully functional.

## Investigation Results (2026-03-26 session)

### Q1 & Q2: mDir resolution — RESOLVED

mDir resolves correctly on native. `hudMDirResolved=1` in telemetry — the game_hud merger's mDir ObjPtr points to the "hud" PanelDir, and `MergerDir()` returns the WorldDir's mHUD. The merge target is correct. **This is NOT the root cause.**

Telemetry invariants T1 (merge target == mHUD) and T2 ($hud_panel == mHUD) both PASS during gameplay. The ObjPtr deserialization and object hierarchy work as expected.

### Root Cause: `~ObjectDir` NullifyAllRefs Cascade

The real problem is in the native `~ObjectDir` destructor (Dir.cpp:66-122). We added a `NullifyAllRefs` cascade to prevent stale-pointer crashes on native (Xbox doesn't have this — it relies on simpler ref counting). This cascade:

1. `CollectCascadeDirs(this, allDirs)` — recursively collects all reachable ObjectDirs via mSubDirs AND `ObjDirItr` (hash table objects)
2. Calls `NullifyAllRefs()` on every object in every collected dir
3. **Problem:** This is too aggressive — it reaches objects that were legitimately reparented to external dirs during MergeDirs, killing their external ObjRef/ObjDirPtr entries

**Specific failure chain for hud_left/hud_right:**

1. Game_hud merge: `MergeDirs` moves hud_left/hud_right into PanelDir "hud" via `SetName` (hash table) — works correctly, `hudHasLeft=1` at frame 340-370
2. `PostMerge` deletes source dir (`game_mode_hud`) — objects that were SetName'd out survive this step
3. `director.milo` DirLoader finishes PostLoad — the PanelDir "hud" has a non-inlined subdir that gets processed, reassigning hud_left to an anonymous subdir (`Dir()=''`)
4. When the anonymous subdir is destroyed, the `~ObjectDir` NullifyAllRefs cascade walks ObjDirItr and finds hud_left (because `hud_left->Dir()` == the dying dir)
5. `NullifyAllRefs()` on hud_left kills all ObjRefs/ObjDirPtrs pointing to it
6. PanelDir "hud" can no longer `FindObject("hud_left")` — `hudHasLeft=0` at frame 380

**On Xbox, steps 4-5 don't exist** — there's no NullifyAllRefs cascade. Objects survive dir destruction as long as they have refs. The DTA find handler (`FindObject` with subdir search) still locates them regardless of which dir they're registered in.

### Headless Timing Fix

The engine's headless mode ran at ~3000fps, completing 9050 frames in ~3 seconds. DTA timeouts based on `{taskmgr ui_seconds}` (wall-clock time) never triggered — e.g., the autosave_warning_screen's 4-second timeout. Fixed by adding non-realtime clock mode:

- `TaskMgr::Poll()` (Task.cpp): when `MILO_HEADLESS=1`, advances `kTaskSeconds`/`kTaskBeats` by 1/30s per frame instead of reading wall clock
- `UIManager::Poll()` (UI.cpp): same fixed delta for `kTaskUISeconds`
- UI seconds reset on screen transitions handled via file-scope accumulator
- Removed `usleep(1000)` workaround from App.cpp headless loop

## Unit Tests Written

### Merge Lifecycle Tests (`native/tests/test_merge_lifecycle.cpp`)

7 tests modeling correct Xbox merge behavior. **5 pass, 2 fail** — the 2 failures define the exact architectural gap in our `~ObjectDir` cascade:

| Test | Result | What it tests |
|------|--------|---------------|
| ObjectsSurviveSourceDirDeletion | PASS | SetName-reparented objects survive source deletion |
| **SubdirsSurviveSourceDirDeletion** | **FAIL** | Subdirs moved via AppendSubDir don't survive cascade |
| MergeDirsPreservesObjectsAfterSourceDeletion | PASS | End-to-end MergeDirs + delete source |
| FindObjectWorksAfterMergeAndSourceDeletion | PASS | Both FindObject modes work |
| NullifyAllRefsCascadeDoesNotKillReparentedObjects | PASS | External ObjDirPtr to reparented ObjectDir survives |
| **MergedObjectsSurviveParentDirReload** | **FAIL** | Subdir remove/re-add cycle loses findability |
| MergeReplaceSubdirsSurviveSourceDeletion | PASS | kMoveAllSubdirs filter works correctly |

### Gameplay Telemetry Tests (extended `test_gameplay_telemetry.cpp`)

5 new Tier 5 tests for HUD merge convergence invariants T1-T4:

| Test | Result | Invariant |
|------|--------|-----------|
| HudMergeTargetMatchesWorldHUD | PASS | T1: MergerDir() == world->GetHUD() |
| HudPanelVariableMatchesWorldHUD | PASS | T2: DataVariable("hud_panel") == world->GetHUD() |
| **HudChildrenFoundAfterMerge** | **FAIL** | T3: hud_left/hud_right findable in merge target |
| HudMDirResolvedDuringGameplay | PASS | mDir ObjPtr resolves to non-null |
| **NoHudDtaErrors** | **FAIL** | No "$hud not function or object" in output |

### Telemetry Fields Added (`GameplayTelemetry.h/cpp`)

- `hudMergeTargetIsHUD` — T1 invariant
- `hudPanelIsHUD` — T2 invariant
- `hudHasLeft` / `hudHasRight` — T3 invariant
- `hudMDirResolved` — diagnostic: whether mDir ObjPtr is non-null

## Action Items

### Next: Fix `~ObjectDir` NullifyAllRefs Cascade (architectural)

The cascade needs to be scoped so it does NOT nullify objects that have been reparented to dirs outside the dying tree. Two approaches:

**Option A — Fix CollectCascadeDirs:** Make it aware of merge boundaries. Skip ObjectDirs found via ObjDirItr whose `Dir()` points to the dying tree but who are actually subdirs of (or registered in) an external dir. This requires checking if the candidate exists as a subdir of any dir outside the cascade set.

**Option B — Fix the reassignment:** Understand why `director.milo` PostLoad reassigns hud_left from PanelDir "hud" to the anonymous subdir. If this reassignment can be prevented, the cascade won't reach hud_left even with current behavior.

**Success criteria:** The 2 failing merge lifecycle tests (`SubdirsSurviveSourceDirDeletion`, `MergedObjectsSurviveParentDirReload`) pass, AND the 2 failing gameplay telemetry tests (`HudChildrenFoundAfterMerge`, `NoHudDtaErrors`) pass.

### Then: Remove HUD Workarounds

Once the cascade is fixed and all tests pass:
1. Remove `SetHUD()` call in `HamDirector::OnFileMerged`
2. Remove `$hud_panel` guard in `GamePanel::SetTypeDef`
3. Remove `$hud_panel` guard in `HamDirector::OnSelectCamera`
4. Remove `WorldDir::SetHUD()` if no longer needed
5. Remove `gSavedHudLeft`/`gSavedHudRight` globals
6. Clean up diagnostic logging in Dir.cpp, Object.cpp, FileMerger.cpp, GameplayTelemetry.cpp

### Downstream work (after convergence)
1. **HUD rendering position** — the white rectangle in gameplay screenshots indicates the HUD camera frustum positioning needs tuning. The flashcard/score transforms are repositioned via hardcoded offsets in OnFileMerged; these may need adjustment once the merge is correct.
2. **Score display** — `GamePanel::Poll` has a C++ score label update path that bypasses DTA. Once the HUD merge is correct, verify that score labels render properly through the DTA path.
3. **$photo_fill null** — separate DTA variable that's also null during gameplay (logged as `$photo_fill not function or object`). May need similar investigation.
4. **Flow cleanup crash** — `UIScreen::UnloadPanels` crashes during screen transitions when Flow objects are destroyed (`ReplaceRefs` null deref). Separate pre-existing issue.
5. **HUD polling** — verify that the merged mHUD gets polled correctly by the WorldDir.
6. **Screenshot comparison test** — gameplay frame should show venue + characters without white rectangles once HUD positioning is correct.
