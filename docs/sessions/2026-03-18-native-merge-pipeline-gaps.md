# Native Merge Pipeline Gaps — ClipPlayer Crash Analysis

**Date**: 2026-03-18
**Status**: Analysis complete, fix levels identified
**Related**: `docs/sessions/2026-03-18-objref-ring-corruption-staff-review.md`

## Crash Summary

SIGSEGV in `ClipPlayer::GetRoutineCrossoverClips` during song playback. `mClipDir->Find<CharClip>(name, false)` returns nullptr for clip names that Xbox always found. After all 4 fallback lookups fail, `c1` remains null. Callers dereference `c1->Name()` without null check.

## Why Xbox Always Found Clips

On Xbox, `FileMerger` **flattens** everything into a single scope. When a song loads:

1. `OnPopulateFromMoveMgr()` creates merger entries for each charclip and transition
2. Each `modular_song_data/charclips/<name>.milo` is loaded by `DirLoader`
3. `FileMerger::FinishLoading` calls `MergeDirs(loadedDir, clipsDir, filter)`
4. `MergeObjectsRecurse` walks the loaded dir's hash table and subdirs
5. Every CharClip ends up in the `clips` ObjectDir's hash table
6. `mClipDir->Find<CharClip>(name)` always succeeds

## Why Native Misses Clips

On native, `FileMerger` loads into **isolated directories** and relies on fallback chains to compensate. The fallback chain is wired into `ObjRefConcrete::Load` and `ObjPtrVec::Load` (deserialization only), but **NOT into `ObjectDir::Find<T>()`** which is what `ClipPlayer` uses at runtime.

### The Fallback Chain (Deserialization Only)

Present in `ObjPtr_p.h:76-98` and `ObjPtr_p.h:302-325`:

```
1. Direct FindObject in local dir          ← Xbox path (always works)
2. Walk Dir()->Dir() parent chain          ← Native fallback #1
3. Walk DirLoader::ParentDir() chain       ← Native fallback #2
4. ObjectDir::Main() last resort           ← Native fallback #3
```

This chain fires during `ObjRefConcrete::Load` (BinStream deserialization) but is NOT available during runtime `Find<T>()` calls.

### Three Reasons Flattening Fails on Native

**1. Subdir filter skips content**

`MergeObjectsRecurse` (Utl.cpp) third pass processes subdirs:

```cpp
for (int i = 0; i < fromDir->SubDirs().size(); i++) {
    ObjectDir *sd = fromDir->SubDirs()[i];
    MergeFilter::SubdirAction sa = filter.FilterSubdir(sd, toDir);
    if (sa == MergeFilter::kMergeKeep) continue;  // SKIPPED
    // ...
}
```

`MergeFilter::DefaultSubdirAction` returns `kMergeKeep` for `kInlineNever` and `kInlineCachedShared` subdirs. Charclip `.milo` files may contain subdirs with those types, meaning their CharClip objects never get flattened into the target scope.

**2. MergeAction filters out containers**

`FileMerger::MergeAction` returns `kIgnore` for `CharClipGroup` and `RndGroup` objects. If a CharClip is only reachable through a CharClipGroup that gets ignored, its contents aren't traversed.

**3. Async load ordering**

FileMerger.cpp:435 forces `async = true` on native (cooperative polling). If `ClipPlayer::Init()` runs before the charclip merge completes, `mClipDir` is valid but partially populated.

## The Specific Crash Path

```
ClipPlayer::Init(RndPropAnim *anim)
  mClipDir = TheHamDirector->ClipDir()     → "clips" ObjectDir (valid but incomplete)
  mClipKeys = anim->GetKeys(...)->AsSymbolKeys()  → valid
  mMasterClipKeys = ...->AsSymbolKeys()            → valid
  return true  (all non-null)

Later, during PlayAnims:

GetRoutineCrossoverClips(beat, clipName, &c1, &c2)
  TheMoveMgr->GetRoutineMeasure() → returns variant names
  mClipDir->Find<CharClip>(variantName) → nullptr (not flattened)
  mClipDir->Find<CharClip>(cc)          → nullptr
  mClipDir->Find<CharClip>(masterKey)   → nullptr (or mMasterClipKeys is null)
  c1 remains nullptr

GetPrevRoutineTransition:
  return GetRoutineTransition(c1->Name(), &curKey)  → SIGSEGV
```

### Secondary Crash: PropKeys Deletion

If the crash occurs inside `GetRoutineCrossoverClips` at `mMasterClipKeys->at(0)`, the cause is different. `mMasterClipKeys` is a raw `Keys<Symbol,Symbol>*` pointing into a PropKeys owned by a RndPropAnim. If the PropAnim's Replace deletes that PropKeys during the ObjRef ring walk (see PropAnim Replace analysis), `mMasterClipKeys` becomes dangling. This connects to the PropAnim ObjOwnerPtr issue documented in the ring corruption review.

## FileMerger Architecture

### Key Classes

| Class | Role |
|-------|------|
| `FileMerger` | Orchestrates async merge pipeline. Contains `Merger` entries. |
| `FileMerger::Merger` | One merge unit: source file, target dir, subdir mode, loaded objects list |
| `MergeFilter` | Decision policy: `kMerge`, `kReplace`, `kKeep`, `kIgnore` per object |
| `MergeObjectsRecurse` | Recursive flattener: walks source hash table + subdirs into target |
| `MergeDirs` | Entry point: merges dir-level properties then calls MergeObjectsRecurse |
| `DirLoader` | Deserializes `.milo` into ObjectDir. Native-only `ParentDir` for fallback. |

### Merge Flow

```
Game::LoadNewSongMoves()
  → fm->FindMerger("song")
  → merger.SetSelected("songs/ymca.milo")
  → fm->StartLoad(async=true)
    → LaunchNextLoader() → new DirLoader(filepath, ..., parentDir)
    → DirLoader deserializes .milo into temp ObjectDir
    → FileMerger::FinishLoading(loader)
      → MergeDirs(loadedDir, merger.mDir, filter)
        → MergeObjectsRecurse(loadedDir, targetDir, filter, true)
          Pass 1: reparent orphaned objects
          Pass 2: merge objects by name (Filter decides action)
          Pass 3: recurse into subdirs (FilterSubdir decides action)
```

### Native-Only Infrastructure

| Location | What | Why |
|----------|------|-----|
| `DirLoader.h:37-40` | `ParentDir` getter/setter | Back-pointer for fallback chain during deserialization |
| `Dir.cpp:610-615` | SetParentDir in LoadSubdir | Propagates parent to subdir loaders |
| `Dir.cpp:1214-1229` | SetParentDir in PostLoad | Propagates parent to inlined dir loaders |
| `FileMerger.cpp:413-421` | Pass `Dir()` as parent to DirLoader | Allows ObjPtr fallback to find world-scope objects |
| `FileMerger.cpp:435-437` | Force async=true | Prevents sync-poll hang on web build |
| `ObjPtr_p.h:76-98` | Parent dir walk in ObjRefConcrete::Load | Deserialization fallback when object not in local dir |
| `ObjPtr_p.h:302-325` | Parent dir walk in ObjPtrVec::Load | Same fallback for vector loads |
| `Dir.cpp:861-879` | ProxyDir/ParentDir in FindObject | Loading-time fallback (only when Dir()==this && mLoader) |

## Recommended Fix (Three Levels)

### Level 1 — Defensive null checks (immediate, prevents crash)

Make `GetRoutineCrossoverClips` return bool, guard `mMasterClipKeys` access, and null-check `c1` in callers.

```cpp
// ClipPlayer.h
bool GetRoutineCrossoverClips(float, const char *, CharClip **, CharClip **);
```

```cpp
// ClipPlayer.cpp
bool ClipPlayer::GetRoutineCrossoverClips(
    float f1, const char *cc, CharClip **c1, CharClip **c2
) {
    if (TheMoveMgr->HasRoutine()) {
        const auto *moveVars =
            TheMoveMgr->GetRoutineMeasure(mPlayerIndex, Round(f1 / 4.0f));
        if (moveVars) {
            if (moveVars->first)
                *c1 = mClipDir->Find<CharClip>(moveVars->first->Name().Str(), false);
            if (moveVars->second)
                *c2 = mClipDir->Find<CharClip>(moveVars->second->Name().Str(), false);
        }
    }
    if (!*c1) {
        *c1 = *c2;
        if (!*c1) {
            *c1 = mClipDir->Find<CharClip>(cc, false);
            if (!*c1 && mMasterClipKeys && mMasterClipKeys->size() > 0) {
                *c1 = mClipDir->Find<CharClip>(
                    mMasterClipKeys->at(0).value.Str(), false);
            }
        }
    }
    if (!*c2) *c2 = *c1;
    return *c1 != nullptr;
}
```

Callers:
```cpp
// GetPrevRoutineTransition
if (!GetRoutineCrossoverClips(beat, prevKey->value.Str(), &c1, &c2))
    return nullptr;

// PushRoutineBuilderClip
if (!GetRoutineCrossoverClips(startBeat, curKey.value.Str(), &c1, &c2))
    return false;
```

### Level 2 — Fix MergeObjectsRecurse subdir flattening (correct)

The gap is that `FilterSubdir` returns `kMergeKeep` for certain subdir types, skipping their contents. When the merge mode is `kAllSubdirs`, this should be overridden to always recurse.

In `FileMerger::FinishLoading`, after the standard `MergeDirs`, explicitly flatten any remaining nested objects:

```cpp
#ifdef HX_NATIVE
// Xbox's MergeObjectsRecurse with kAllSubdirs flattens everything.
// Native may skip subdirs with kInlineNever/kInlineCachedShared
// (FilterSubdir returns kMergeKeep). Walk remaining subdirs and
// register their objects in the target hash table.
if (merger.mSubdirs == MergeFilter::kAllSubdirs) {
    for (ObjDirItr<Hmx::Object> it(loadedDir, true); it != nullptr; ++it) {
        if (it->Dir() != merger.mDir && it != loadedDir) {
            Hmx::Object *existing = merger.mDir->FindObject(it->Name(), false, false);
            if (!existing) {
                MergeObject(it, nullptr, merger.mDir, MergeFilter::kMerge);
            }
        }
    }
}
#endif
```

Alternatively, override `FilterSubdir` at the source — make `MergeFilter::DefaultSubdirAction` ignore the `kInlineNever`/`kInlineCachedShared` check when mode is `kAllSubdirs`:

```cpp
MergeFilter::SubdirAction MergeFilter::DefaultSubdirAction(ObjectDir *dir, Subdirs mode) {
    if (mode == kAllSubdirs)
        return kMergeMerge;  // Always recurse in kAllSubdirs mode
    InlineDirType idt = dir->InlineSubDirType();
    if (idt == kInlineNever || idt == kInlineCachedShared)
        return kMergeKeep;
    // ... rest of logic
}
```

### Level 3 — True flat merge (proper, higher risk)

Replicate Xbox's behavior where DirLoader deserializes directly into the target scope. This requires changes to DirLoader to accept a target ObjectDir for object registration, bypassing the two-step load-then-merge pattern. Higher risk due to interaction with async loading and the existing fallback chains.

Not recommended without extensive testing.

## Key Files

| File | What to look at |
|------|-----------------|
| `src/system/hamobj/ClipPlayer.cpp:302-329` | GetRoutineCrossoverClips — crash site |
| `src/system/hamobj/ClipPlayer.cpp:263-286` | GetPrevRoutineTransition — caller with null deref |
| `src/system/hamobj/ClipPlayer.cpp:440-490` | PushRoutineBuilderClip — caller with null deref |
| `src/system/hamobj/ClipPlayer.h:43-49` | Raw pointer members (mClipKeys, mClipDir, etc.) |
| `src/system/hamobj/HamDirector.cpp:470-479` | SetupAnims — mClipDir initialization |
| `src/system/hamobj/HamDirector.cpp:2710-2851` | OnPopulateFromMoveMgr — dynamic clip loading |
| `src/system/char/FileMerger.cpp:413-437` | Native DirLoader parent + async override |
| `src/system/obj/Utl.cpp:353-415` | MergeObjectsRecurse — the flattening algorithm |
| `src/system/obj/Utl.cpp:91-119` | MergeObject — individual object merge |
| `src/system/obj/ObjPtr_p.h:76-98` | Native fallback chain in ObjRefConcrete::Load |

## Recommendation

Start with **Level 1** (null checks) to stop the crash immediately, then implement **Level 2** (fix `DefaultSubdirAction` for `kAllSubdirs` mode) to make the merge pipeline produce the same flat scope as Xbox. Level 2 is low-risk because it only changes behavior when the merge mode explicitly requests full recursion.
