# WorldCrowd System — Native Port Status

## Current Status: Working on Data-Baked Venues (2026-03-16)

WorldCrowd factory is registered on native. Billboard crowd rendering works for venues
that have crowd data baked into .milo files (e.g. DCI — 10 WorldCrowd objects, all with
placement meshes, character refs, and billboard quads). DTA-scripted venues (e.g. throneroom)
have empty crowd data and need the DTA pipeline to populate — not yet supported.

Null clip guards in CharClipGroup and CharDriver prevent crashes from unresolved clip
references. PlayCrowdAnimation returns early on native to avoid null clip group traversal.
TransformListAlloc LP64 pointer truncation fixed in MultiMesh.h.

## Architecture

### Two Venue Crowd Patterns

**Pattern 1 — Data-Baked (works on native)**:
Venues like DCI have all crowd data serialized in the .milo file:
- Placement mesh with instance transforms
- Character references pointing to loaded subdirs
- `WorldCrowd::Load()` → `CreateMeshes()` → billboard quads ready for rendering

**Pattern 2 — DTA-Scripted (not yet supported)**:
Venues like throneroom have empty crowd objects in the .milo (`mNum=0`, no characters,
no placement mesh). The DTA scripting pipeline populates them at runtime:
- `{$this set_type band}` → triggers init handler
- `{$this set_fullness 1 1}` + `{handle ($hamwardrobe add_crowd $this)}`
- FileMerger + HamWardrobe scripting fills in crowd data

### How WorldCrowd Works

WorldCrowd is a billboard impostor crowd rendering system:

1. **Object creation**: WorldCrowd objects are embedded in venue `.milo_xbox` files.

2. **Data loading** (`WorldCrowd::Load`):
   - Placement mesh (defines where crowd instances go)
   - List of `CharData` entries, each with a `Character*` reference + height
   - Instance transforms per character type

3. **Mesh creation** (`CreateMeshes` → `BuildBillboard`):
   - Creates a 4-vert billboard quad per character type
   - Billboard uses `gImpostorMat` with the impostor texture
   - Each quad is wrapped in an `RndMultiMesh` for instanced rendering

4. **Impostor rendering pipeline** (`DrawShowing`):
   - `Draw3DChars()` — renders 3D crowd characters to impostor textures via RTT
   - For each character type: draw billboard quads at world positions via `RndMultiMesh`

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

### Venue Flow on Native (current)

```
App.cpp loads venue .milo + component suffixes (_buildings, _sky, _set, etc.)
  → WorldCrowd objects created for data-baked venues (DCI: 10 objects)
  → CreateMeshes builds billboard quads
  → PlayCrowdAnimation returns early (clip subdirs not loaded)
  → DrawShowing renders billboard quads at instance positions (static, no animation)
```

## What Works

- WorldCrowd factory registered (World.cpp)
- Billboard quad creation via BuildBillboard
- RndMultiMesh instancing (TransformListAlloc LP64 fix)
- Null clip guards prevent crashes (CharClipGroup, CharDriver)
- DCI venue: 10 WorldCrowd objects load successfully with placement meshes

## What Needs Work

### Crowd Animation (clip subdirs)

Crowd characters reference clips from subdirs like `char/crowd/anim/shared_clips.milo`.
These are loaded by the Xbox DTA pipeline but not by the native port's simplified loading.
The `ObjPtrVec<CharClip>` entries resolve to null without these subdirs.

To enable animation:
1. Load crowd clip subdirs alongside the venue
2. Remove `return;` guard in `HamWardrobe::PlayCrowdAnimation`
3. Verify clip groups resolve correctly

### DTA-Scripted Venues

Venues like throneroom need the DTA init/enter script pipeline to populate crowd data.
This is a larger task requiring DTA script execution for WorldCrowd handlers.

### Impostor RTT Verification

`Draw3DChars()` renders 3D characters to impostor textures via render-to-texture.
Needs GPU testing to verify the full pipeline works end-to-end.

## Venues with WorldCrowd

| Venue | Pattern | Status |
|-------|---------|--------|
| dci | Data-baked | Working (10 objects) |
| dclive | Data-baked | Untested |
| default | Unknown | Untested |
| glitterati | **None** | No crowd objects |
| houseparty | Unknown | Untested |
| rollerrink | Unknown | Untested |
| streetside | Unknown | Untested |
| tancinematics | Unknown | Untested (cutscene venue) |
| throneroom | DTA-scripted | Needs DTA pipeline |

## Files Changed

| File | Change |
|------|--------|
| `src/system/world/World.cpp` | WorldCrowd factory enabled (guard removed) |
| `src/system/rndobj/MultiMesh.h` | TransformListAlloc uses malloc/free on native |
| `src/system/hamobj/HamWardrobe.cpp` | PlayCrowdAnimation early-returns on native |
| `src/system/char/CharDriver.cpp` | Null clip guard in PlayGroup |
| `src/system/char/CharClipGroup.cpp` | Null clip guards in GetClip, FindClip, Copy |
| `src/system/world/Crowd.cpp` | Diagnostic logging for CreateMeshes, Set3DCharAll |
