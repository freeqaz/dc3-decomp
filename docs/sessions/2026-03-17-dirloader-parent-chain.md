# Session: DirLoader Parent Chain Fix — HUD Flow Target Resolution

**Date**: 2026-03-17
**Status**: COMPLETE
**Prerequisite**: FileMerger convergence Phase 1-4 (commit b9719618e), gNativeHudDir removal

## Problem

After removing the `gNativeHudDir` hack (standalone HUD loading + manual drawing in
App.cpp), HUD flow animations couldn't find their target objects. GPU renders showed
scanline/noise overlays and missing labels because flows couldn't hide/show elements.

**461 "couldn't find" warnings** — flows in subdirs (`.flow` files) couldn't resolve
objects like `move_feedback0`, `phrase_meter0`, `score1.lbl`, `flashcard_00` etc.

## Root Cause Analysis

### The self-referential Dir() problem

During DirLoader loading, each ObjectDir has `Dir() == this` (self-referential). The
existing ObjPtr_p.h parent dir fallback tried to walk `dir->Dir()`, but the
self-reference meant it went nowhere (`steps=0` in all 461 cases).

### The ProxyDir discovery

Debug logging revealed that each flow subdir's Loader had a valid `ProxyDir()` pointing
to the parent ObjectDir (e.g., `proxyDir=boxyman1 (ui/boxyman/boxyman.milo)`). This
mirrors the `FlowPtrGetLoadingDir` pattern already used by `FlowPtr.cpp` — FlowPtr
resolves objects via `Loader()->ProxyDir()` as its fallback.

### The ObjPtrVec::Node::RefOwner() bug (pre-existing, critical)

When the ProxyDir fallback was added to ObjPtr_p.h (which creates cross-dir refs via
`push_back`), a SIGSEGV crash occurred in `AnimTask::AnimTask` at `owner->ClassName()`.

GDB investigation revealed the root cause: the generic native implementation of
`ObjPtrVec::Node::RefOwner()` was **fundamentally wrong**:

```cpp
// WRONG — casts ObjPtrVec* (an ObjRefOwner) directly to Hmx::Object*
return static_cast<Hmx::Object*>(mOwner);

// CORRECT — goes through the container's Owner() accessor
return static_cast<ObjPtrVec<T1, T2>*>(mOwner)->Owner();
```

The `mOwner` field is an `ObjRefOwner*` pointing to the `ObjPtrVec` container. `ObjPtrVec`
inherits from `ObjRefOwner` but NOT from `Hmx::Object`. The `static_cast` produced a bad
pointer. When `AnimTask::AnimTask` iterated the ref ring and called `owner->ClassName()`,
it dispatched through the `ObjPtrVec` vtable — slot 4 was the RTTI type_info pointer, not
a valid function — causing SIGSEGV.

**This bug was always latent.** It was never triggered before because flow animations
couldn't find their targets (461 warnings = 461 silently failed animations). Once the
ProxyDir fix resolved the targets, the animations executed, hit the ref ring iteration,
and crashed on the bad RefOwner.

On Xbox, `ObjPtrVec::Node::RefOwner()` was explicitly specialized in link_glue.cpp for
each template instantiation, returning the correct `Hmx::Object*`. The native generic
version was added later and had the wrong cast.

## Fix Applied

### 1. DirLoader.h — ParentDir/SetParentDir accessors (`#ifdef HX_NATIVE`)

Exposed the previously dead `mParentDir` field so it can be set during subdir loading.

### 2. Dir.h — GetLoader accessor on ObjDirPtr (`#ifdef HX_NATIVE`)

Exposed the ObjDirPtr's mLoader so Dir.cpp can set ParentDir on subdir DirLoaders.

### 3. Dir.cpp — Parent dir propagation (3 sites)

After creating subdir DirLoaders in `LoadSubDir` and `PreLoad` (both inlined and
non-inlined subdirs), set `ParentDir(this)` so the ObjPtr fallback can walk up.

### 4. Dir.cpp — FindObject ProxyDir fallback (`#ifdef HX_NATIVE`)

Added fallback at the end of `FindObject`: when `Dir() == this` (self-referential during
loading) and the dir has a Loader, search via `Loader()->ProxyDir()` and
`Loader()->ParentDir()`. This mirrors FlowPtr's `FlowPtrGetLoadingDir` pattern but works
for ALL object types, not just FlowPtr targets.

This is the key fix — it's at the FindObject level so ALL callers (ObjRefConcrete::Load,
ObjPtrVec::Load, ObjPtrList::Load, FlowPtr, DataNode, etc.) benefit without needing
per-caller fallback logic.

### 5. ObjPtr_p.h — Updated parent walk in 3 Load methods

Changed the existing fallback to start at `searchDir = dir` (instead of `dir->Dir()`) and
use `Loader()->ParentDir()` when `Dir()` is self-referential.

### 6. ObjPtr_p.h — Fixed ObjPtrVec::Node::RefOwner() (pre-existing bug)

Changed `static_cast<Hmx::Object*>(mOwner)` to
`static_cast<ObjPtrVec<T1, T2>*>(mOwner)->Owner()`.

## Results

| Metric | Before | After |
|--------|--------|-------|
| "couldn't find" warnings | 461 | 7 |
| Crashes | None (flows silently failed) | None |
| GPU rendering | Scanline overlay, missing labels | Clean |
| PPC impact | — | Zero (all `#ifdef HX_NATIVE`) |

### Remaining 7 warnings (harmless)

All from `CAMP_STINGER_audio.anim` in `world/glitterati/glitterati_base.milo`:
- 5 campaign-specific voice sounds (`campaign_scene10-1_vo_*.snd`) — campaign content, not loaded on native
- 2 venue stinger sounds (`glitter_musicforstinger.snd`) — venue-specific audio, harmless

## Files Modified

| File | Change |
|------|--------|
| `src/system/obj/DirLoader.h` | `ParentDir()` / `SetParentDir()` accessors |
| `src/system/obj/Dir.h` | `GetLoader()` accessor on ObjDirPtr |
| `src/system/obj/Dir.cpp` | ParentDir propagation (3 sites) + FindObject ProxyDir fallback |
| `src/system/obj/ObjPtr_p.h` | Updated parent walk (3 Load methods) + fixed Node::RefOwner |

## Key Discoveries

1. **ProxyDir is the Milo engine's built-in parent resolution**: FlowPtr already uses
   `Loader()->ProxyDir()` — it's the engine's mechanism for proxy objects to find their
   parent scope. We just extended it to FindObject for all object types.

2. **ObjPtrVec::Node::RefOwner() was fundamentally broken on native**: Any code that
   called `RefOwner()` on a Node and used the result as `Hmx::Object*` would hit UB.
   The bug was latent because it required a specific call path (ref ring iteration with
   `ClassName()` check) that only triggered when flow animations actually found targets.

3. **FindObject-level fix > ObjPtr-level fix**: Putting the ProxyDir fallback in
   FindObject rather than in each ObjPtr Load method is cleaner — it benefits ALL
   callers automatically and creates refs in the normal FindObject path.
