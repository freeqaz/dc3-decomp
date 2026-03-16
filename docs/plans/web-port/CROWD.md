# WorldCrowd System — Native Port Status

## Current Status: Billboard Crowd Rendering Working (2026-03-16)

The crowd billboard rendering pipeline is fully functional on native. DCI venue loads
10 WorldCrowd objects with 80 billboard instances across 35 character types. Billboard
quads render at instance positions via RndMultiMesh. The engine runs at 1700 draw calls
per frame on game_screen with crowd + venue + HUD.

Key fixes this session:
- **Force3DCrowd(false) on native**: The .milo saves `force=true` which moves all
  MultiMesh instances to `m3DChars` (for 3D character rendering). On native, we only
  have billboard rendering, so we always use `Force3DCrowd(false)` to keep instances
  in the MultiMesh.
- **Skip impostor RTT on native**: The original engine renders 3D characters to an
  impostor texture per-frame, then displays that texture on billboard quads. This is
  extremely expensive (~1000 extra draw calls). On native, we skip RTT and draw
  billboards directly with the impostor material.
- **GPU rendering on by default**: Changed from `MILO_RENDER=1` opt-in to
  `MILO_NORENDER=1` opt-out.

Previous fixes:
- `HamDirector::GetWorld()` falls back to `mVenue` when merger is absent
- `HamDirector::Enter()` native path works without merger
- `OriginalChoreoRemixer::Init()` bypassed on native (corrupt move graph data)
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

4. **Force3DCrowd handling** (`Load` rev > 0xC):
   - .milo saves `force` flag (true for all DCI crowd objects)
   - `Force3DCrowd(true)` moves instances from MultiMesh → `m3DChars` (for 3D RTT)
   - **On native**: Always `Force3DCrowd(false)` — keeps instances in MultiMesh for billboard rendering

5. **Impostor rendering pipeline** (`DrawShowing`):
   - **On Xbox**: `Draw3DChars()` → impostor camera setup → render character to texture → billboard draw
   - **On native**: Skip RTT, draw billboard quads directly via `DrawMultiMeshWithEnviron(mmesh)`

6. **Animation** (`HamWardrobe::PlayCrowdAnimation`):
   - Iterates `mCrowdMembers` (populated via DTA `{$hamwardrobe add_crowd $this}`)
   - Calls `CharDriver::PlayGroup()` on each crowd character
   - Returns early on native (null clip subdirs)

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
- Billboard quad creation via BuildBillboard (4-vert quads)
- RndMultiMesh instancing (TransformListAlloc LP64 fix)
- Instance transforms loaded from .milo binary data (80 instances across 10 objects)
- Force3DCrowd(false) keeps instances in MultiMesh for billboard rendering
- Billboard rendering via DrawMultiMeshWithEnviron (skip impostor RTT)
- Null clip guards prevent crashes (CharClipGroup, CharDriver)
- DCI venue: 10 WorldCrowd objects, 35 characters, 80 instances
- Full game flow: auto-nav to game_screen, HamDirector::Enter(), VenueEnter()
- Stable rendering at 1700 draw calls per frame (venue + crowd + HUD)

## What Needs Work

### Impostor Texture Content

Billboard quads render as white/placeholder because the impostor RTT (rendering 3D
characters to the impostor texture) is skipped on native. To get actual crowd
silhouettes, need either:
- Optimized character RTT (expensive, ~1000 draw calls per frame)
- Pre-baked impostor textures (static crowd character snapshots)
- Simplified crowd geometry (flat colored quads)

### Crowd Animation (clip subdirs)

Crowd characters reference clips from subdirs like `char/crowd/anim/shared_clips.milo`.
These are loaded by the Xbox DTA pipeline but not by the native port's simplified loading.
The `ObjPtrVec<CharClip>` entries resolve to null without these subdirs.
`PlayCrowdAnimation` returns early on native to avoid null clip traversal.

### DTA-Scripted Venues

Venues like throneroom need the DTA init/enter script pipeline to populate crowd data.
This is a larger task requiring DTA script execution for WorldCrowd handlers.

### Venue Component Files

Venue components (`_buildings`, `_sky`, `_set`, etc.) fail to load because App.cpp
looks for `.milo` but files are `.milo_xbox`. Needs path suffix fix.

## Venues with WorldCrowd

| Venue | Pattern | Status |
|-------|---------|--------|
| dci | Data-baked | Working (10 objects, 80 instances, 1700 draw calls) |
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
| `src/system/world/Crowd.cpp` | Force3DCrowd(false) on native; skip impostor RTT; simplified billboard draw |
| `src/system/rndobj/MultiMesh.h` | TransformListAlloc uses malloc/free on native |
| `src/system/hamobj/HamWardrobe.cpp` | PlayCrowdAnimation early-returns on native |
| `src/system/hamobj/HamDirector.cpp` | GetWorld() falls back to mVenue; Enter() native path |
| `src/system/hamobj/OriginalChoreoRemixer.cpp` | Init() bypassed on native |
| `src/system/hamobj/MoveVariant.cpp` | CacheLinks null guard for null variant pointer |
| `src/system/hamobj/MoveMgr.cpp` | LoadMoveData null guard for missing move_graph |
| `src/system/char/CharDriver.cpp` | Null clip guard in PlayGroup |
| `src/system/char/CharClipGroup.cpp` | Null clip guards in GetClip, FindClip, Copy |
| `src/App.cpp` | VenueEnter() called after explicit venue load |
| `native/src/platform/Rnd_Wgpu.cpp` | GPU rendering on by default (MILO_NORENDER to disable) |
