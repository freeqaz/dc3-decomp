# Phase C: WorldCrowd Rendering

**Status**: Research complete — blocked by asset loading + DTA pipeline
**Last Updated**: 2026-03-16

## Problem

Some venues (e.g., dclive) contain WorldCrowd objects that render billboard impostor crowds. These are currently invisible on native because:
1. Crowd-related assets aren't loaded (clip subdirs, crowd meshes)
2. WorldCrowd methods are weak stubs in the native build
3. DTA pipeline issues prevent animation clip loading

**Note**: Not all venues use WorldCrowd. glitterati does NOT have crowd objects; dclive DOES.

## Architecture

### WorldCrowd Pipeline

```
1. OBJECT CREATION
   WorldCrowd embedded in venue .milo_xbox (e.g., dclive.milo_xbox)
   Factory: REGISTER_OBJ_FACTORY(WorldCrowd) in WorldInit()

2. DATA LOADING (WorldCrowd::Load)
   - Placement mesh (defines instance positions)
   - CharData[] entries, each with:
     - CharDef: Character* reference, height, density, radius, materials
     - Instance transforms (world positions per character type)

3. MESH CREATION (CreateMeshes → BuildBillboard)
   - For each character: BuildBillboard() creates 4-vert quad mesh
   - Material: gImpostorMat (alpha-cut at 0x80 threshold)
   - Each quad wrapped in RndMultiMesh

4. RENDERING (DrawShowing)
   - Draw3DChars(): render 3D characters → impostor textures via RTT
   - For each char type: iterate RndMultiMesh instances → draw billboard quads
   - Billboard mesh uses impostor texture as diffuse map

5. ANIMATION
   HamWardrobe::PlayCrowdAnimation() → CharDriver::PlayGroup() on each member
   Triggered by DTA: {$hamwardrobe add_crowd $this}
```

### Class Hierarchy
- `WorldCrowd` : `RndDrawable`, `RndPollable`
- `WorldCrowd3DCharHandle` : `RndTransformable`, `RndDrawable` (per-instance wrapper)
- `CharDef` — archetype: Character*, height, density, radius, materials
- `CharData::Char3D` — instance: transform, index, colors, handle

### Global Impostor Resources
```cpp
RndTex *gImpostorTex[kNumLods];   // kNumLods = 3 (LOD textures)
RndCam *gImpostorCamera;           // Camera for RTT
RndMat *gImpostorMat;              // Material with alpha-cut
```
Initialized on first WorldCrowd construction (`gNumCrowd++ == 0`).

## Why Crowd Is Broken on Native

### 1. Missing Asset Loading

The venue loading in `App.cpp` loads the main venue .milo plus component suffixes (`_buildings`, `_sky`, `_set`, etc.), but does NOT load:

| Missing Asset | Purpose |
|---|---|
| `world/shared/gen/crowd_plane_small.milo_xbox` | Crowd placement mesh |
| `char/crowd/anim/shared_clips.milo` | Crowd animation clips |
| Other crowd character subdirs | Individual crowd character models |

On Xbox, DTA scripting triggers async loading of these via the full merger system:
- Venue selection triggers async loading
- DTA handlers fire `{$hamwardrobe add_crowd $this}` to register crowd chars
- Character clip subdirs are loaded via the DTA character loading pipeline

Result: Crowd characters' `ObjPtrVec<CharClip>` entries resolve to null — clips never loaded.

### 2. Weak Stubs vs Real Symbols

`Crowd.cpp` and `Crowd3DCharHandle.cpp` are already in `native/CMakeLists.txt` (lines 928-929). The real implementations compile as strong symbols. The weak stubs in `engine_stubs_generated.cpp` (stubs #405-#414, #940) should be overridden at link time for functions with platform-independent mangled names (e.g., `DrawShowing`, `BuildBillboard`, `AssignRandomColors`).

**However**: Functions with STL parameter types (e.g., `Set3DCharXfm` taking `std::_List_iterator<CharData>`) have different mangled names between STLport (PPC) and libstdc++ (native). The stubs use PPC-era mangled names via `asm()` directives, so they become orphan symbols — nothing in native code calls them. The real native implementations use libstdc++ mangled names and link correctly.

**Potential issue**: If any code path references the STLport-mangled name (unlikely in pure native build), the weak stub would be called instead of the real function. Verify by checking if the stubs get linked (`nm` the binary).

### 3. Native 3D Path Already Exists

`Crowd.cpp:666-681` has an `#ifdef HX_NATIVE` block that renders 3D characters directly (no impostor textures). This path is correct but bypasses the impostor optimization.

## Implementation Plan

### Step 1: Add Crowd Assets to Native Loading

In `App.cpp` or the venue loading code, add:
```cpp
// After loading main venue .milo
LoadMilo("world/shared/gen/crowd_plane_small.milo_xbox");
// Load crowd character clip subdirs referenced by the venue
```

This requires tracing what assets each venue's WorldCrowd objects reference.

### Step 2: Remove Weak Stubs / Compile Crowd.cpp

Check if `Crowd.cpp` is already in `native/CMakeLists.txt`. If so, remove the 11 weak stubs from `engine_stubs_generated.cpp` so the real implementations link.

If not in CMakeLists, add it and fix compilation errors (LP64, STL differences, `__fsel` intrinsic).

### Step 3: Test 3D Direct Path First

The `#ifdef HX_NATIVE` 3D direct rendering path is simpler — no impostor textures needed. Get this working first:
1. Verify crowd characters load (non-null Character* in CharDef)
2. Verify instance transforms are populated
3. DrawShowing() calls Draw3DChars() which directly renders each character

### Step 4: (Optional) Impostor Pipeline

For performance, implement the impostor pipeline:
1. `gImpostorTex` creation works (RTT infra in Tex_Wgpu.cpp)
2. `gImpostorCamera` → render character to texture
3. `BuildBillboard()` → create quad mesh
4. `RndMultiMesh::DrawShowing()` → draw quads at instance positions

### Step 5: Wire Animation

Ensure `HamWardrobe::PlayCrowdAnimation()` fires (may depend on DTA handler fix — see [DTA_HANDLER_ANALYSIS.md](DTA_HANDLER_ANALYSIS.md)).

## Dependencies

- **DTA handler execution** (see DTA_HANDLER_ANALYSIS.md) — crowd registration via `{$hamwardrobe add_crowd $this}` requires working DTA dispatch
- **Asset loading pipeline** — crowd character subdirs must be loadable
- **RTT infrastructure** (for impostor path) — `MakeDrawTarget` / `FinishDrawTarget` in Tex_Wgpu.cpp

## Key Files

| File | Purpose |
|------|---------|
| `src/system/world/Crowd.cpp` (1339 lines) | Core implementation |
| `src/system/world/Crowd.h` (154 lines) | Header |
| `src/system/world/Crowd3DCharHandle.cpp/h` | Per-instance wrapper |
| `src/system/rndobj/MultiMesh.cpp/h` | Billboard instance drawing |
| `src/system/hamobj/HamWardrobe.cpp` | `PlayCrowdAnimation()` |
| `native/src/engine_stubs_generated.cpp` | Weak stubs to remove |
| `native/src/platform/Tex_Wgpu.cpp` | RTT implementation |

## Venues with WorldCrowd

| Venue | Has WorldCrowd? | Notes |
|-------|:---:|-------|
| glitterati | NO | Abstract dance stage |
| dclive | YES | Outdoor concert venue with audience |
| houseparty | ? | Needs verification |
| rollerrink | ? | Needs verification |
| dci | ? | Needs verification |
| bid | ? | Needs verification |

To verify: Load each venue and check for WorldCrowd objects in the mDraws list.
