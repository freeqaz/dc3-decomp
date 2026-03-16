# WorldCrowd System — Native Port Status

## Current Status: Factory Enabled, Crowd Not Visible (2026-03-16)

WorldCrowd factory is now registered on native. Null clip guards prevent crashes.
Verified: dclive venue boots to main menu with 546 draw calls/frame, no crashes.
PlayCrowdAnimation still returns early — crowd animation clips not fully loaded.

~~WorldCrowd factory registration was guarded by `#ifndef HX_NATIVE` in `World.cpp`.~~ Guard removed — factory now registered on native. Null clip guards in CharClipGroup and CharDriver prevent SIGSEGV. TransformListAlloc LP64 pointer truncation fixed in MultiMesh.h.

## Architecture

### How WorldCrowd Works

WorldCrowd is a billboard impostor crowd rendering system:

1. **Object creation**: WorldCrowd objects are embedded in venue `.milo_xbox` files. Not all venues have them — `glitterati` does NOT, but `dclive`, `dci`, `streetside`, `houseparty`, `rollerrink`, `throneroom`, and `default` do.

2. **Data loading** (`WorldCrowd::Load`):
   - Placement mesh (defines where crowd instances go)
   - List of `CharData` entries, each with a `Character*` reference (crowd char model) + height
   - Instance transforms per character type (world positions from the placement mesh)

3. **Mesh creation** (`CreateMeshes` → `BuildBillboard`):
   - Creates a 4-vert billboard quad per character type
   - Billboard uses `gImpostorMat` with the impostor texture
   - Each quad is wrapped in an `RndMultiMesh` for instanced rendering

4. **Impostor rendering pipeline** (`DrawShowing`):
   - `Draw3DChars()` — renders 3D crowd characters to impostor textures via RTT
   - For each character type: iterate `RndMultiMesh` instances and draw billboard quads at world positions
   - Billboard mesh uses impostor texture as diffuse map

5. **Animation** (`HamWardrobe::PlayCrowdAnimation`):
   - Iterates `mCrowdMembers` (populated via DTA `{$hamwardrobe add_crowd $this}`)
   - Calls `CharDriver::PlayGroup()` on each crowd character
   - Looks up stance-specific clip groups (e.g. `stance_idle_realtime_idle`)

### Venue Flow on Xbox (DTA-driven)

```
DTA merger system requests venue load
  → Async loads venue .milo (contains WorldCrowd objects)
  → WorldCrowd::Load() creates billboard meshes, loads instance transforms
  → DTA handler fires {$hamwardrobe add_crowd $this}
  → HamWardrobe registers crowd characters
  → Crowd clip subdirs loaded via character loading pipeline:
    - char/crowd/gen/crowd_f_*.milo_xbox (character models)
    - char/crowd/anim/shared_clips.milo (animation clips)
  → HamDirector::Enter calls PlayCrowdAnimation("realtime_idle", ...)
```

### Venue Flow on Native (simplified)

```
App.cpp loads venue .milo + component suffixes (_buildings, _sky, _set, etc.)
  → WorldCrowd objects NOT created (factory disabled)
  → No crowd clip subdirs loaded
  → No crowd rendering
```

## Root Cause Analysis

### Why it crashes when enabled

When `REGISTER_OBJ_FACTORY(WorldCrowd)` is enabled on native:

1. **WorldCrowd objects created** — 5+ per venue, with character references ✓
2. **CreateMeshes / BuildBillboard** — creates billboard quads ✓
3. **Instance transforms loaded** — world positions for each crowd member ✓
4. **CRASH: `HamDirector::Enter`** calls `PlayCrowdAnimation("realtime_idle", 2, true)`
   - `HamWardrobe::PlayCrowdAnimation` iterates crowd members
   - Calls `CharDriver::PlayGroup("stance_idle_realtime_idle", ...)`
   - `CharClipGroup::GetClip(0)` accesses `ObjPtrVec<CharClip>` which contains null entries
   - `ObjPtrVec::swap` calls `SetObjConcrete` → `AddRef` on null → SIGSEGV

### Why clips are null

Crowd characters reference clips from subdirectories like `char/crowd/anim/shared_clips.milo`. These subdirs are loaded by the Xbox DTA pipeline but NOT by the native port's simplified venue loading in App.cpp.

The `ObjPtrVec<CharClip>` entries in each `CharClipGroup` store `ObjRefConcrete<CharClip>` nodes that reference clip objects by name. When the clip subdir isn't loaded, these references resolve to null.

### Secondary issue: TransformListAlloc (64-bit crash)

`RndMultiMesh::Instance` uses a custom STL allocator (`TransformListAlloc`) backed by `FixedSizeAlloc`. The free list uses `int*` and casts pointers via `*cur = (int)next` — this **truncates 64-bit pointers to 32 bits**. Fixed by bypassing the pool allocator on native (`malloc`/`free` in `MultiMesh.h`).

## What Needs to Be Done

### Phase 1: Load crowd clip subdirs

The native port needs to load crowd character subdirs alongside the venue:

1. After loading venue components, detect WorldCrowd objects in the venue dir
2. For each crowd character referenced by a WorldCrowd:
   - Load the character's clip subdir (e.g., `char/crowd/anim/shared_clips.milo`)
   - The path is stored in the character's `mClips` ObjDirPtr
3. Register these clips before `HamDirector::Enter` runs

### Phase 2: Enable WorldCrowd factory

1. Remove `#ifndef HX_NATIVE` guard in `World.cpp`
2. Remove `return;` guard in `HamWardrobe::PlayCrowdAnimation`
3. Test with a venue that has crowds (e.g., `dclive`)

### Phase 3: Verify impostor rendering pipeline

1. Verify `Draw3DChars()` renders to impostor textures via RTT
2. Verify billboard quads bind the impostor texture correctly
3. Verify `RndMultiMesh::DrawShowing()` places billboards at correct positions

## Venues with WorldCrowd

| Venue | Has WorldCrowd | Crowd count |
|-------|---------------|-------------|
| dci | Yes | 5 |
| dclive | Yes | unknown |
| default | Yes | unknown |
| glitterati | **No** | - |
| houseparty | Yes | unknown |
| rollerrink | Yes | unknown |
| streetside | Yes | unknown |
| throneroom | Yes | unknown |

## Files Changed

| File | Change |
|------|--------|
| `src/system/world/World.cpp` | WorldCrowd factory guarded `#ifndef HX_NATIVE` |
| `src/system/rndobj/MultiMesh.h` | TransformListAlloc uses malloc/free on native |
| `src/system/hamobj/HamWardrobe.cpp` | PlayCrowdAnimation early-returns on native |
| `src/system/char/CharDriver.cpp` | Null clip guard in PlayGroup |
| `src/system/char/CharClipGroup.cpp` | Null clip guard in GetClip |
| `src/system/world/Crowd.cpp` | Removed `#ifndef HX_NATIVE` around CreateMeshes |
