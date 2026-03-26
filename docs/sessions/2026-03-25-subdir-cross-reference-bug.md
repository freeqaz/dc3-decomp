# Subdirectory Cross-Reference Bug: FindObject Can't Reach Sibling Scopes

**Date:** 2026-03-25
**Status:** Resolved — ObjOwnerPtr::RefOwner decomp bug fixed
**Symptom:** `win_blue_P1_low_mov01_audio.anim couldn't find win_blue_P2_low_mov.snd in WorldDir`

## Summary

The original working theory was wrong. Current evidence says the reproducible
`vo1.flow`/`.snd` warnings are a **load-order issue inside the voice-bank milo**, not proof
that `MergeDirs` or inline-subdir ownership is wrong. `mini_blue`'s root `WorldDir`
already finds the referenced sound recursively after load.

## Update: Same-Day Follow-Up

Further investigation narrowed this down and invalidated the proposed "flatten all inline
subdirs in `ObjectDir::PostLoad`" fix:

1. Loading `world/shared/camshots/gen/mini_blue.milo_xbox` directly still emits the
   `vo1.flow`/`win_blue_*.snd` warnings, **without any FileMerger or MergeDirs path**.
   That means the warning itself is not evidence of a directory-merge bug.

2. Raw strings in `mini_blue.milo_xbox` show it directly references
   `../../../sfx/loc/eng/vo_bank_iconmanblue.milo`, so the voice bank is a **child subdir
   of `mini_blue`**, not a sibling venue milo.

3. Using the existing `milo-tests` binary under GDB, `mini_blue`'s root `WorldDir`
   successfully resolves `FindObject("win_blue_P2_low_mov.snd", false, true)` after load.
   So the parent camshot dir can already find the sound recursively at runtime.

4. The exact same `Couldn't find win_blue_low_01.snd from vo1.flow` warning reproduces
   when loading `sfx/loc/eng/gen/vo_bank_iconmanblue.milo_xbox` directly. This points to
   **load-order inside the voice-bank milo** (`vo1.flow` loads before the `Sound` objects),
   not to world/venue merge semantics.

5. A focused native regression test now shows `vo_bank_iconmanblue.milo_xbox` loads as a
   `character_vo` root and resolves `win_blue_low_01.snd` from that same runtime scope.
   In other words, `character_vo::play_vo`'s `{$this find ...}` succeeds locally after
   load, exactly where the authored DTA expects it to.

6. Because Xbox `ObjectDir::FindObject` has no general runtime parent fallback, and this
   case already works from both the `mini_blue` root and the `character_vo` bank scope on
   native, the current evidence does **not** support adding a global parent-backpointer
   walk or a blanket flatten pass in `ObjectDir::PostLoad`.

## Corrected Interpretation

There are two separate behaviors here:

- **Load-time warnings from `vo1.flow`**:
  `DataNode::Load` on object literals runs while the `Flow` is loading. If referenced
  `Sound` objects haven't been created yet, the load emits `Couldn't find ... from vo1.flow`.
  This reproduces in the standalone voice-bank milo and is therefore unrelated to
  directory merging.

- **Runtime lookup from the camshot root**:
  `mini_blue` itself can find `win_blue_P2_low_mov.snd` recursively after load, which is
  exactly the lookup pattern Xbox `FindObject(..., false, true)` supports.

- **Runtime lookup from `character_vo::play_vo`**:
  the loaded `vo_bank_iconmanblue` root is itself `character_vo`, and
  `FindObject("win_blue_low_01.snd", false, true)` resolves in that same dir. So the live
  `{$this find $foley_snd_name}` path does not require merge flattening or parent-scope
  fallback.

The remaining open question is narrower: whether there is a real **runtime** failure from a
specific object scope that Xbox resolves differently, or whether the observed symptom was
only the load-time `vo1.flow` warnings.

## Original Hypothesis

After `AddedSubDir` calls `SetSubDir(true)` → `SetName(nullptr, nullptr)`, the subdir's `mDir` becomes `nullptr` and `mLoader` is deleted. The native fallback in `FindObject` (Dir.cpp:964) checks `Dir() == this && mLoader` — both conditions fail post-loading.

On Xbox, `FileMerger::FinishLoading` calls `MergeDirs` which flattens inline subdir objects into the parent venue's hash table. There's already a native-specific flatten pass at `FileMerger.cpp:240-252`:

```cpp
#ifdef HX_NATIVE
for (ObjDirItr<Hmx::Object> it(mergerDir, true); it != nullptr; ++it) {
    if (it->Dir() != mergerDir) {
        if (!mergerDir->FindObject(it->Name(), false, false)) {
            it->SetName(it->Name(), mergerDir);
        }
    }
}
#endif
```

**However**, this flatten pass only runs inside `FileMerger::FinishLoading` — it doesn't help when milos are loaded directly via `DirLoader` (as subdirectories during venue PreLoad), bypassing FileMerger entirely.

## Superseded Merge-Hypothesis Notes

### 1. The .snd objects exist but are in a sibling milo

Parsed `mini_blue.milo` entry table: 25 objects (13 HamCamShot + 12 PropAnim). **Zero Sound/Sfx objects.** The .snd names appear only as references inside PropKeys data (target of `trigger_sound` property animation).

The actual Sound objects were found in `ark_5` inside voice-bank audio milos:

```
...win_blue_P1_high_mov.wav....Sound....win_blue_P1_high_mov.snd....SynthSample...
...Sound....win_blue_P2_low_mov.snd....Sound....win_blue_P2_med_mov.snd...
```

Each `.snd` is class `Sound` with an associated `SynthSample` + `.wav`.

### 2. Xbox behavior: references also fail at load time

`ObjOwnerPtr::RefOwner()` on Xbox returns `mObject ? mObject->RefOwner() : nullptr`. During `PropKeys::Load`, `mObject` is null → RefOwner returns null → `ObjRefConcrete::Load` falls into the else branch → silent null. **Xbox also doesn't resolve these references at load time.**

The DTA handler `play_vo` (world_objects.dta:911-921) resolves sounds at **runtime**:
```dta
($foley_snd_name {sprintf "%s.snd" {basename {$cam_shot name}}})
($foley_snd {$this find $foley_snd_name})
{if $foley_snd {$foley_snd play}}
```

What changed in the diagnosis is the meaning of `$this`: for `play_vo`, it is the loaded
`character_vo` bank object, not a random camshot subdir. Native now proves that this
runtime lookup succeeds from the bank's own scope after load.

### 3. FindObject scope chain is broken post-loading

`ObjectDir::OnFind` calls `FindObject(name, false, true)`:

1. Search local hash table — not there
2. Search mSubDirs recursively — mini_blue has no subdirs
3. `parentDirs=false` — skip parent walk
4. Native fallback (line 964): `Dir() == this && mLoader` — Dir() is nullptr (not this), mLoader is nullptr → **dead**

That fallback analysis is still true in isolation, but current evidence says it is **not**
the mechanism behind this voice-bank case.

### 4. Unit test gap

`test_merge_scope_parity.cpp:VenueMergeSubdirObjectsFindableFromTop` tests `worldRoot->FindObject(name, false, true)` — searching **from the root**. No test searches from one subdir trying to find objects in a sibling subdir.

## Superseded Architecture Sketch

```
mini_blue WorldDir
├── HamCamShot / PropAnim content
└── child reference to vo_bank_iconmanblue.milo
   └── character_vo root
      ├── vo.flow / per-cam flows
      ├── win_blue_*.snd
      └── related lipsync/audio assets
```

This better matches the asset strings and runtime test results than the earlier sibling-dir
merge sketch.

## Why The Proposed Fix Is Not Safe

The native-specific flatten pass in `FileMerger.cpp` is still useful for non-proxy merge
parity, but it does **not** explain the `vo1.flow` warnings:

- `vo_bank_iconmanblue.milo_xbox` reproduces them when loaded standalone
- `mini_blue` root lookup already succeeds after load
- flattening all inline subdirs in `ObjectDir::PostLoad` would globally rewrite authored
  inline-dir structure and change behavior far outside this voice-bank case

So a blanket `PostLoad` flatten pass should not be used as the next step for this issue.

## Next Investigation Step

Prove or disprove a real runtime mismatch at the actual call site before changing engine
semantics:

1. Identify the concrete object used as `$this` for the runtime `play_vo` / camshot path.
2. Verify that object's post-load `FindObject(name, false, true)` behavior on native.
3. Compare that to the Xbox-style expectation from the decompiled `FindObject` logic.
4. Only if the runtime caller truly needs a different scope should we change ownership or
   merge behavior.

If the only failing symptom is the load-time `vo1.flow` warnings, then the correct Xbox
parity is likely to **leave them alone**.

## Superseded Fix Direction

The native-specific flatten pass in `FileMerger.cpp:240-252` is the right approach — it matches Xbox's flat-scope behavior. The issue is that this only runs for FileMerger-managed loads. Venue subdirectories loaded during `ObjectDir::PreLoad` (inline dirs) go through `DirLoader` directly and don't get the flatten treatment.

Options (in order of preference):

1. **Ensure flatten happens for all inline subdirs** — After all inline subdirs complete loading in `ObjectDir::PostLoad`, run the same flatten pass that FileMerger uses. This matches Xbox's behavior without adding new mechanisms.

2. **Fix FindObject fallback for subdirs** — When `mIsSubDir` is true and local search fails, walk up to the parent dir that owns this subdir. Requires storing a parent back-pointer (set in `AddedSubDir`, cleared in `RemovingSubDir`).

3. **Change OnFind to use parentDirs=true** — Would work but changes behavior for all `{find}` DTA calls, potentially breaking other things.

Option 1 is closest to Xbox parity. The flatten pass should run in `ObjectDir::PostLoad` after all subdirs are loaded and added, ensuring all nested objects are registered in the parent's hash table.

## Files Involved

| File | Lines | Purpose |
|------|-------|---------|
| `src/system/obj/Dir.cpp` | 932-978 | `FindObject` with broken native fallback |
| `src/system/obj/Dir.cpp` | 449-455 | `SetSubDir` nulls mDir |
| `src/system/obj/Dir.cpp` | 706-715 | `AddedSubDir` calls SetSubDir |
| `src/system/obj/Dir.cpp` | 1334-1398 | `PostLoad` processes inline subdirs |
| `src/system/obj/ObjPtr_p.h` | 198-209 | `ObjOwnerPtr::RefOwner` Xbox vs native |
| `src/system/char/FileMerger.cpp` | 240-252 | Native flatten pass (model for fix) |
| `native/tests/test_merge_scope_parity.cpp` | 447-488 | Missing sibling cross-ref test |
| `orig-assets/extracted/world/world_objects.dta` | 911-921 | DTA `play_vo` handler |

## Key DTA Sound Trigger Path

```
HamCamShot activated → play_character_vo runs on the HamCamShot →
  $vo_bank is the character's `character_vo` bank →
  $foley_snd_name = sprintf("%s.snd", basename(cam_shot.name))
  $foley_snd = {$vo_bank find $foley_snd_name}
  if $foley_snd: {$foley_snd play}
```

The `trigger_sound` PropAnim path (PropKeys with kFloat type animating `trigger_sound` property via `SyncProperty`) is a secondary mechanism. The primary sound trigger is the DTA `play_vo` handler, which runs at runtime and requires `FindObject` to reach the Sound objects.

## Load-Time Warning Disposition

The `Couldn't find ... from vo1.flow` warnings are now best treated as **likely expected
load-time misses** until a runtime failure is proven. They reproduce in standalone
`vo_bank_iconmanblue.milo_xbox`, which means they are not a symptom of merge behavior.

## Follow-Up Work

### 1. Prove the real runtime caller
Done: `play_character_vo` is a HamCamShot-side DTA handler, and `play_vo` runs on the
`character_vo` bank object. Native asset loading now proves that bank scope resolves both
`vo.flow` and `win_blue_low_01.snd` after load.

### 2. Add a focused regression test
Done in native `milo-tests`: load `mini_blue.milo_xbox` and
`vo_bank_iconmanblue.milo_xbox`, then verify:
- `mini_blue` root finds `win_blue_P2_low_mov.snd` recursively after load
- `vo_bank_iconmanblue` resolves as `character_vo`
- that `character_vo` scope finds both `vo.flow` and `win_blue_low_01.snd`

### 3. Do not change global merge semantics yet
Do not add a blanket flatten pass to `ObjectDir::PostLoad` or a general parent fallback
until the runtime mismatch is demonstrated at the real call site.

## Resolution: ObjOwnerPtr::RefOwner Decomp Bug (2026-03-25)

The deeper investigation revealed the **root cause** was a decomp error in
`ObjOwnerPtr::RefOwner()`, not a merge/flatten issue.

### The Bug

The DC3 decomp had:
```cpp
template <class T>
Hmx::Object *ObjOwnerPtr<T>::RefOwner() const {
#ifdef HX_NATIVE
    return mOwner->RefOwner();  // Native workaround
#else
    return mObject ? mObject->RefOwner() : nullptr;  // Xbox decomp — WRONG
#endif
}
```

The `#else` branch returns `nullptr` when `mObject` is null (which it always is during
initial loading of ObjOwnerPtrs created with null targets, e.g. `PropKeys::mTarget` via
`AddKeys(nullptr, nullptr, type)->Load(d)`). This breaks `ObjRefConcrete::Load` because
`RefOwner()` returning null means no directory is available for `FindObject` — the reference
silently fails with "No dir to find".

### RB3 Reference Proof

RB3 (Rock Band 3, same Milo engine) has a different but equivalent architecture:
```cpp
// rb3/src/system/obj/ObjPtr_p.h line 104
virtual Hmx::Object *RefOwner() { return mOwner; }
```

RB3's `ObjOwnerPtr::RefOwner()` returns `mOwner` directly — the owning object, not the
pointed-to object. This ensures `ObjRefConcrete::Load` can always find the owner's directory
and resolve references during loading.

### The Fix

Removed the `#ifdef HX_NATIVE` guard and unified both platforms to use `mOwner->RefOwner()`:
```cpp
template <class T>
Hmx::Object *ObjOwnerPtr<T>::RefOwner() const {
    return mOwner->RefOwner();
}
```

This is semantically equivalent to RB3's `return mOwner` (since `Hmx::Object::RefOwner()`
returns `this`).

### Verification

- **Decomp match%**: No regressions. PropKeys::Load remains 100%, RndCamAnim::Load 100%,
  RndTransAnim::Load 99.6% (pre-existing regalloc), RndLightAnim::Load 99.7% (pre-existing
  regswap).
- **Native tests**: All non-pre-existing tests pass (10/10 merge+asset tests).
- **ReplaceRefsFrom**: The fix actually *improves* `ReplaceRefsFrom` behavior, which uses
  `RefOwner() == from` where `from` is the OWNER object. The old code returned the target
  (wrong match), the new code returns the owner (correct match).

### Remaining Native Divergences

The `#ifdef HX_NATIVE` blocks in `ObjRefConcrete::Load` (parent-dir-chain fallback) and
`FindObject` (ProxyDir/ParentDir fallback) remain. These compensate for incomplete
merge-flatten parity, not for the RefOwner bug. They should be addressed as part of a
separate merge pipeline convergence effort.
