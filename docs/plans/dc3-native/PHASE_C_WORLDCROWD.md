# Phase C: WorldCrowd Rendering

**Status**: DONE — 3D crowd characters rendering on dclive venue
**Last Updated**: 2026-03-16

## Summary

WorldCrowd 3D crowd characters are now visible during gameplay. The dclive venue shows
5 crowd character types with 30+ total instances placed across the stage area. The
key bug was in `SetFullness()` which erased all 3D characters after `Set3DCharAll()`
transferred MultiMesh instances to the m3DChars vector.

## Root Cause: SetFullness Bug

When `mForce3DCrowd` is true, `Set3DCharAll()` transfers all MultiMesh instances into
the `m3DChars` vector and clears the instance list. `SetFullness()` then computes:
```cpp
targetChars3D = Min(targetChars3D, (int)instances.size());
// instances.size() == 0 after Set3DCharAll, so targetChars3D = 0
// This pops ALL m3DChars, erasing the crowd
```

**Fix**: Guard the `Min` clamp with `#ifdef HX_NATIVE` / `if (!mForce3DCrowd)`:
```cpp
#ifdef HX_NATIVE
if (!mForce3DCrowd)
#endif
targetChars3D = Min(targetChars3D, (int)instances.size());
```

Also added a bounds guard for `m3DCharsCreated` access which can be empty during
the `Reset3DCrowd → SetFullness` call before `Sort3DCharList` runs.

## Other Fixes

- **SuperEasyRemixer::DumpSongLayout**: Guarded against empty move data vectors
  on native (move graph not loaded when Init runs without Kinect data)
- **ChunkStream seek corruption**: Removed debug code that used `Tell()/Seek()`
  on a ChunkStream (doesn't support seeking, corrupted stream position)
- **HamDirector::SetNativeVenueWorld()**: Added setter for venue world on native
  (DTA merger pipeline does work and sets mVenue via OnFileLoaded)

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

4. 3D CROWD PATH (native)
   - Set3DCharAll() transfers MultiMesh instances → m3DChars vector
   - Draw3DChars() renders characters directly (no impostor textures)
   - Apply3DCharXfm() positions each character at placement transform

5. BILLBOARD PATH (Xbox)
   - DrawShowing iterates RndMultiMesh instances → draw billboard quads
   - Billboard mesh uses impostor texture as diffuse map (RTT)
```

### dclive Venue Crowd Configuration

| Crowd Object | Placement Mesh | Character Types | Instances |
|---|---|---|---|
| PeakCrowd.crd | DLV_Crowdplace01.mesh | 5 (f_00s_01, f_00s_02, m_00s_01-03) | 30 (6 per type) |
| crowd_cameraperson.crd | crowd_cameraperson_placement.mesh | 1 (m_00s_05_cameraperson) | 2 |
| insidecrowd.crd | DLV_Crowdplace01.mesh | 6 (5 standing + f_00s_03_sitting) | 7 |
| outsidecrowd.crd | DLV_Crowdplace02.mesh | 5 (f_00s_01, f_00s_02, m_00s_01-03) | 12 |
| outsidecrowd_cutscene.crd | DLV_Crowdplace02.mesh | 5 | 50 (10 per type) |

Total: ~101 crowd character instances across 5 WorldCrowd objects.

## Key Files

| File | Purpose |
|------|---------|
| `src/system/world/Crowd.cpp` | Core implementation (SetFullness fix at line 790) |
| `src/system/world/Crowd.h` | Header |
| `src/system/world/Crowd3DCharHandle.cpp/h` | Per-instance wrapper |
| `src/system/rndobj/MultiMesh.cpp/h` | Billboard instance drawing |

## Remaining Work

- **Apply3DCharXfm**: The `mHandle` field is nullptr for Char3D entries created by
  Set3DCharAll (handles are created by Build3DChars which requires CamShotCrowd).
  The 3D rendering path works but character positioning via handles is not wired.
- **Crowd animation**: `HamWardrobe::PlayCrowdAnimation()` needs crowd character
  registration via DTA handlers. Currently crowd characters are static.
- **Impostor optimization**: The billboard impostor path (RTT) is not used on native.
  Direct 3D rendering works but is heavier.
