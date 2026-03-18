# FileMerger Flatten Analysis — Why Native Misses Objects

**Date**: 2026-03-18
**Status**: Analysis complete, fixes identified
**Related**: `docs/sessions/2026-03-18-native-merge-pipeline-gaps.md`

## The Core Problem

On Xbox, `FileMerger` merges loaded `.milo` content into a **flat scope** — every CharClip, texture, mesh etc. ends up in a single `ObjectDir`'s hash table. `Find<T>(name)` always works because everything is in one place.

On native, the same `MergeDirs` / `MergeObjectsRecurse` code runs, but **three gaps** prevent complete flattening.

## Gap 1: `DefaultSubdirAction` Skips Subdirs

**File**: `src/system/obj/Utl.cpp:27-47`

```cpp
MergeFilter::SubdirAction
MergeFilter::DefaultSubdirAction(ObjectDir *dir, Subdirs subdirs) {
    switch (subdirs) {
    case kAllSubdirs:
        return kMergeMerge;       // ← should always recurse...
    case kInlineSubdirs:
        if (dir->InlineSubDirType() == kInlineNever
            || dir->InlineSubDirType() == kInlineCachedShared)
            return kMergeKeep;    // ← SKIPPED
    case kMergeInlinedMoveSharedSubdirs:
        if (dir->InlineSubDirType() == kInlineNever
            || dir->InlineSubDirType() == kInlineCachedShared)
            return kMergeReplace; // ← moved, not flattened
    ...
    }
}
```

This looks fine for `kAllSubdirs` (returns `kMergeMerge`). But `FileMerger::FilterSubdir` doesn't always use `kAllSubdirs` — it uses whatever `merger->mSubdirs` is set to. Most mergers use `kMergeInlinedMoveSharedSubdirs` (the default). For those, subdirs tagged `kInlineNever` get **moved** (`kMergeReplace`) rather than **flattened** (`kMergeMerge`). Their objects stay in a separate subdir scope — invisible to `Find<T>()` on the parent.

This IS the same behavior on Xbox. The difference is that Xbox code rarely hits this path because the async timing means the objects are available when needed. On native, the timing differs.

## Gap 2: MergeAction and Container Objects

**File**: `src/system/char/FileMerger.cpp:347-351`

```cpp
if (dynamic_cast<RndGroup *>(o2) || dynamic_cast<CharClipGroup *>(o2)
    || dynamic_cast<CharPollGroup *>(o2)) {
    return (Action)0;  // kMerge — merge properties, don't replace
}
```

This returns `kMerge` (0), not `kKeep`. Groups DO get merged, they just don't get replaced. The objects inside groups are separate objects that get merged individually via the hash table walk. **This is not actually a bug.**

## Gap 3: Async Load Timing — The Real Killer

**File**: `src/system/char/FileMerger.cpp:435-437`

```cpp
#ifdef HX_NATIVE
    async = true;  // Cooperative polling only
#endif
```

On Xbox, `StartLoadInternal` can run **synchronously** (`async = false`). The entire load-merge pipeline completes before control returns. By the time game code calls `Find<CharClip>(name)`, everything is merged.

On native, **async is forced to true**. `FileMergerOrganizer` queues the load and polls it over multiple frames. If game code runs `ClipPlayer::Init()` or `GetRoutineCrossoverClips()` before the merge completes, the target dir is valid but partially populated.

## Gap 4: FindObject Fallback is Load-Time Only

**File**: `src/system/obj/Dir.cpp:861-879`

The native fallback chain (ProxyDir + ParentDir walk) only fires when `Dir() == this && mLoader` — i.e., during active loading/deserialization. Once loading finishes, `mLoader` is null. Runtime `Find<T>()` calls get no fallback.

## Fix Strategy

Two independent problems requiring two independent fixes.

### Fix A: Async Timing (highest value)

Don't force `async = true` on desktop builds. The comment says it's to prevent "sync-poll hang on web." Guard it to Emscripten only:

```cpp
#ifdef HX_NATIVE
#ifdef __EMSCRIPTEN__
    async = true;  // Cooperative polling only — prevents sync-poll hang on web
#endif
#endif
```

This lets desktop native builds load synchronously like Xbox, while web still uses cooperative polling.

Alternative: add a sync point before clip lookups:

```cpp
if (TheHamDirector->GetFileMerger()->IsLoading()) {
    TheHamDirector->GetFileMerger()->PollUntilDone();
}
```

### Fix B: Post-Merge Flatten Pass (structural)

When a subdir gets `kMergeReplace`'d (moved to the target as a subdir), its objects become children of that subdir — not the parent dir. `Find<T>(name, false)` won't find them because it only searches the local hash table when `parentDirs=false`.

Add a post-merge flattening pass in `FileMerger::FinishLoading`:

```cpp
void FileMerger::FinishLoading(Loader *ldr) {
    // ... existing merge logic ...

#ifdef HX_NATIVE
    // After MergeDirs, any subdirs that got kMergeReplace'd still have
    // their objects in a separate scope. Flatten them into the target.
    if (!merger->mProxy) {
        ObjectDir *mergerDir = merger->MergerDir();
        for (ObjDirItr<Hmx::Object> it(mergerDir, true); it != nullptr; ++it) {
            if (it->Dir() != mergerDir) {
                if (!mergerDir->FindObject(it->Name(), false, false)) {
                    it->SetName(it->Name(), mergerDir);
                }
            }
        }
    }
#endif
    PostMerge(merger, dl, true);
}
```

### Fix C: Defensive Null Checks (belt and suspenders)

Add null checks in `ClipPlayer::GetRoutineCrossoverClips` as described in the pipeline gaps session doc. Prevents crash even if merge is incomplete.

## Recommendation

1. **Immediate**: Fix async timing for desktop builds (Fix A — `#ifdef __EMSCRIPTEN__` guard)
2. **Structural**: Add post-merge flatten pass (Fix B — handles subdirs moved instead of merged)
3. **Defensive**: Null checks in ClipPlayer (Fix C — crash prevention)

The timing fix is highest-value. Most "missing clip" bugs are because the merge hasn't finished yet, not because the merge logic itself is wrong. The structural fix handles edge cases where subdir types prevent flattening even after merge completes.

## Key Files

| File | What |
|------|------|
| `src/system/obj/Utl.cpp:27-47` | `DefaultSubdirAction` — subdir filter logic |
| `src/system/obj/Utl.cpp:353-415` | `MergeObjectsRecurse` — the flattening algorithm |
| `src/system/char/FileMerger.cpp:197-221` | `FinishLoading` — where merge happens |
| `src/system/char/FileMerger.cpp:335-376` | `MergeAction` — per-object filter |
| `src/system/char/FileMerger.cpp:432-437` | `StartLoadInternal` — async forcing |
| `src/system/obj/Dir.cpp:861-879` | `FindObject` — native load-time fallback |
