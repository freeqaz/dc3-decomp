# Phase C: WorldCrowd Rendering

**Status**: NOT APPLICABLE — DC3 does not use WorldCrowd
**Last Updated**: 2026-03-16

## Problem

Venues look empty — no crowd characters in the audience area. WorldCrowd is a billboard/3D hybrid instancing system but all its methods are weak stubs in the native build.

## Architecture

### WorldCrowd Class
- Inherits: `RndDrawable`, `RndPollable`
- Source: `src/system/world/Crowd.cpp` (1324 lines)
- Header: `src/system/world/Crowd.h` (154 lines)

### Two Rendering Modes
1. **2D Impostor (Xbox)**: Render character → offscreen texture → billboard quad
2. **3D Direct (Native)**: Call `Character::DrawShowing()` per instance (HX_NATIVE path, lines 659-687)

### Data Flow
```
Venue .milo → WorldCrowd object (in mDraws list)
  → CharData[] (archetype characters with density/radius)
    → RndMultiMesh (instance positions for billboards)
    → m3DChars[] (3D character instances)

Draw:
  RndDir::DrawShowing() iterates mDraws
    → WorldCrowd::DrawShowing()
      → Draw3DChars() — direct Character::DrawShowing() per 3D instance
      → Impostor pipeline — billboard quads (Xbox only)
```

### RndMultiMesh Rendering (Simple Loop)
```cpp
void RndMultiMesh::DrawShowing() {
    for (auto& inst : mInstances) {
        mMesh->SetWorldXfm(inst.mXfm);
        mMesh->DrawShowing();  // → Mesh_Wgpu.cpp
    }
}
```

## Current State: 11 Stubbed Methods

From `engine_stubs_generated.cpp`:
- `WorldCrowd::DrawShowing()` — stub #405
- `WorldCrowd::SetFullness()` — stub #406
- `WorldCrowd::Reset3DCrowd()` — stub #407
- `WorldCrowd::Set3DCharXfm()` — stub #408
- `WorldCrowd::OnIterateFrac()` — stub #409
- `WorldCrowd::Set3DCharList()` — stub #410
- `WorldCrowd::Apply3DCharXfm()` — stub #411
- `WorldCrowd::BuildBillboard()` — stub #412
- `WorldCrowd::AssignRandomColors()` — stub #413
- `WorldCrowd::Mats()` — stub #414
- `WorldCrowd3DCharHandle::SyncProperty()` — stub #940

## Implementation Plan

### Step 1: Check if Crowd.cpp compiles in native build
- Is it included in CMakeLists.txt?
- Does it compile? What errors?

### Step 2: Remove stubs and compile real implementation
- Remove weak stubs from engine_stubs_generated.cpp
- Add Crowd.cpp to native build if not already included
- Fix compilation errors (LP64, STL, platform-specific)

### Step 3: Handle impostor texture pipeline
The native HX_NATIVE path skips impostor textures and renders 3D characters directly.
Key global state needs init:
- `gImpostorTex[3]`, `gImpostorCamera`, `gImpostorMat`
- These may not be needed if we only use the 3D path

### Step 4: Verify character assets load
Crowd characters come from `dc3/char/crowd/` .milo files. Need to verify:
- Character archetype loading works
- RndMultiMesh instance lists populated
- m3DChars vector has entries

### Step 5: Test and screenshot
Take venue screenshots to verify crowd appears in audience area.

## Investigation Result (2026-03-16)

Checked all 6 DC3 venues (glitterati, dclive, houseparty, rollerrink, dci, bid):
- **Zero WorldCrowd objects created** in any venue
- Added constructor trace — confirmed no instantiation during full boot flow
- DC3 is a Kinect dance game with abstract dance stages, NOT concert venues with audiences
- WorldCrowd is inherited from the shared Milo engine (used in Rock Band) but **not used in DC3**
- The `shared_clips.milo_xbox` from `char/crowd/anim/` is loaded but never consumed

**Conclusion**: WorldCrowd rendering is NOT a gap — DC3 venues are intentionally crowd-free.
