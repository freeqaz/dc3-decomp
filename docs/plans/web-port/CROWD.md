# WorldCrowd System — Native Port Status

## Current Status: Full Game Flow Working (2026-03-16)

The full game flow (auto-nav to game_screen) works with DCI venue. `HamDirector::Enter()`
runs successfully — `VenueEnter()` triggers `dir->Enter()` on the venue, firing DTA type
handlers on all WorldCrowd objects. The engine is stable at 500+ draw calls per frame.

Key fixes this session:
- `HamDirector::GetWorld()` falls back to `mVenue` when merger is absent
- `HamDirector::Enter()` native path works without merger (venue enter, crowd init, post-proc)
- `OriginalChoreoRemixer::Init()` bypassed on native (corrupt move graph data from incomplete DTA merger)
- `MoveCandidate::CacheLinks` null guard for null variant pointers
- `MoveMgr::LoadMoveData` null guard for missing move_graph
- `VenueEnter()` called from App.cpp after venue load

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

### Game Flow on Native

```
Auto-nav: attract → title → main → choose_mode → song_select → multiuser → loading → game_screen
  → GamePanel::StartIntro DTA handler fires
    → OriginalChoreoRemixer::Init bypassed on native (corrupt move graph)
  → HamDirector::Enter()
    → mMerger exists (loaded from DTA): normal Enter path runs
    → VenueEnter(mVenue): dir->Enter() triggers DTA type handlers
      → WorldCrowd objects get Enter() → set_fullness, add_crowd
    → HamWardrobe::PlayCrowdAnimation("realtime_idle", 2, true)
  → App.cpp poll loop: explicit venue load + VenueEnter for component milos
```

### DTA Type Handler Flow

When WorldCrowd loads from .milo, `LoadType` calls `SetType("band")` → `SetTypeDef(found)`.
Later, when `dir->Enter()` fires on the venue:
1. `RndPollable::Enter()` calls `HandleType(Message("enter"))`
2. Finds `(band (enter ...))` in world_objects.dta
3. Executes `{$this set_fullness 1 1}` → redistributes crowd instances
4. Executes `{handle ($hamwardrobe add_crowd $this)}` → registers with HamWardrobe

## What Works

- WorldCrowd factory registered (World.cpp)
- Billboard quad creation via BuildBillboard
- RndMultiMesh instancing (TransformListAlloc LP64 fix)
- Null clip guards prevent crashes (CharClipGroup, CharDriver)
- DCI venue: 10 WorldCrowd objects load successfully with placement meshes
- Full game flow: auto-nav to game_screen, HamDirector::Enter(), VenueEnter()
- Stable rendering at 500+ draw calls per frame

## What Needs Work

### Crowd Animation (clip subdirs)

Crowd characters reference clips from subdirs like `char/crowd/anim/shared_clips.milo`.
These are loaded by the Xbox DTA pipeline but not by the native port's simplified loading.
The `ObjPtrVec<CharClip>` entries resolve to null without these subdirs.
`PlayCrowdAnimation` returns early on native to avoid null clip traversal.

### DTA-Scripted Venues

Venues like throneroom need the DTA init/enter script pipeline to populate crowd data.
This is a larger task requiring DTA script execution for WorldCrowd handlers.

### Impostor RTT Verification

`Draw3DChars()` renders 3D characters to impostor textures via render-to-texture.
3D character rendering is disabled on native (WebGPU BGL mismatches).
Billboard rendering should work for static quads.

### Venue Component Files

Venue components (`_buildings`, `_sky`, `_set`, etc.) fail to load because App.cpp
looks for `.milo` but files are `.milo_xbox`. Needs path suffix fix.

## Venues with WorldCrowd

| Venue | Pattern | Status |
|-------|---------|--------|
| dci | Data-baked | Working (10 objects, 581 draw calls) |
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
| `src/system/hamobj/HamDirector.cpp` | GetWorld() falls back to mVenue; Enter() native path |
| `src/system/hamobj/OriginalChoreoRemixer.cpp` | Init() bypassed on native |
| `src/system/hamobj/MoveVariant.cpp` | CacheLinks null guard for null variant pointer |
| `src/system/hamobj/MoveMgr.cpp` | LoadMoveData null guard for missing move_graph |
| `src/system/char/CharDriver.cpp` | Null clip guard in PlayGroup |
| `src/system/char/CharClipGroup.cpp` | Null clip guards in GetClip, FindClip, Copy |
| `src/system/world/Crowd.cpp` | Diagnostic logging for CreateMeshes, Set3DCharAll |
| `src/App.cpp` | VenueEnter() called after explicit venue load |
