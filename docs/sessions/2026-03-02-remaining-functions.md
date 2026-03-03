# Remaining Functions & Follow-Up Notes (2026-03-02)

Progress: **99.1%** (322 remaining of 34,215 non-excluded)

## Functions Implemented This Session

| Function | Match | Status | Notes |
|----------|------:|--------|-------|
| `BurnXfm(RndMesh*, bool)` | 90.5% | AT_LIMIT | Hand-transposed inverse transform to match stack size, register alloc limits |
| `HamCamShot::Target operator>>` | 100% | COMPLETE | BinStream sig fix, individual bool reads |
| `MoveReplacer::MoveReplacer(copy)` | 99.8% | AT_LIMIT | 1 address reloc on vector copy |
| `RandomPointOnMesh` | 99.2% | AT_LIMIT | Anon namespace hash + scope counter |
| `StackString<128> ctor` | 99.3% | AT_LIMIT | Template instantiation in HamCharacter |
| `RndMesh::SkinVertex` | 97.4% | AT_LIMIT | DC3 uses TransformNormal, no error path |
| `RndPostProc::UpdateColorModulation` | 97.0% | AT_LIMIT | Register swap f0/f13 |
| `RndMesh::CollideShowing` | 83.9% | AT_LIMIT | Register swaps, prologue mismatch |

## Functions Investigated But Skipped

### Too Complex (Large Functions)

These were Ghidra-decompiled but are 200+ instructions with complex logic:

| Function | Unit | Why Skipped |
|----------|------|-------------|
| `RndMesh::SetVolume` | rndobj/Mesh | ~180 lines Ghidra, BSP tree construction, box volumes |
| `RndMesh::OnSync` | rndobj/Mesh | ~200 lines, patch computation with PatchVerts, face iteration |
| `RndMesh::OnCompareEdgeVerts` | rndobj/Mesh | Nested O(n³) loops, std::list<int> adjacency |
| `RndMesh::DeleteBones` | rndobj/Mesh | RTDynamicCast walking, ObjVector erase |
| `RndMesh::InstanceGeomOwnerBones` | rndobj/Mesh | RTDynamicCast, NextName, bone tree cloning |
| `ResetNormals(RndMesh*)` | rndobj/Utl | ~250 lines, tangent basis, angle-weighted normals |
| `MakeNormals(RndMesh*)` | rndobj/Utl | ~200 lines, similar to ResetNormals without tangents |
| `BuildVisit(BSPNode*)` | rndobj/Utl | Recursive BSP traversal, BuildPoly list manipulation |
| `BuildFromBSP(RndMesh*)` | rndobj/Utl | BSP→mesh conversion, vert/face allocation |
| `TessellateMesh(RndMesh*)` | rndobj/Utl | Edge-based subdivision, AO blend verts, set operations |
| `ComputeFaceTangentBasis` | rndobj/Utl | Per-face tangent space computation |
| `TransConstraint::Poll` | hamobj/TransConstraint | ~300 lines, position/scale constraint with easing |
| `TransConstraint::Highlight` | hamobj/TransConstraint | Draw axes + bounding box visualization |
| `SongLayout::SetDefaultPattern` | hamobj/SongLayout | Pattern generation with modular indexing |
| `SongLayout::SetDefaultReplacer` | hamobj/SongLayout | PropAnim key iteration, MoveReplacer population |
| `HamCamShot::OnAllowableNextShots` | hamobj/HamCamShot | ObjDirItr, DataArray construction, shot filtering |
| `CharCameraInput::ResetSkeletonCharOrigin` | hamobj/CharCameraInput | Virtual calls, 0x8fc+ offsets, rotation matrix setup |
| `HamListRibbon::EndFrame` | hamobj/HamListRibbon | Switch on mode, multi-clip endframe resolution |
| `RndPostProc::Interp` | rndobj/PostProc | Large member-by-member interpolation |
| `RndPostProc::LoadRev` | rndobj/PostProc | Versioned deserialization with many fields |
| `RndBitmap::SamePixelFormat` | rndobj/Bitmap | Palette color comparison with format conversion |

### Unit Misattribution (Wrong .obj)

These functions exist in the DB under the wrong compilation unit. The code is already implemented in the correct .cpp but the DB expects it in a different .obj:

| Function | DB Unit | Actual Location |
|----------|---------|-----------------|
| `FlowDistance::RequestStop` | flow/FlowSlider | flow/FlowDistance.cpp |
| `FlowDistance::RequestStopCancel` | flow/FlowSlider | flow/FlowDistance.cpp |
| `AnimPtr::~AnimPtr` | char/CharLipSync | Should be in AnimPtr's own unit |
| `CharBoneTwist::Handle` | char/CharSignalApplier | Should be in CharBoneTwist |
| `PhotoSpotlightPositioner::Handle` | char/CharBoneOffset | Should be in PhotoSpotlightPositioner |
| `CharInterest::Handle` | char/Waypoint | Should be in CharInterest |
| `SongCollision::SyncProperty` | char/FileMergerOrganizer | Should be in SongCollision |
| `TrueColor::ExposureRecipe::SetGlobalGain` | char/CharWeightable | Should be in TrueColor |
| `MsgSinks::Sink::~Sink` | char/Char | Template/inline emitted in wrong TU |
| `ObjectDir::Find<UIPanel>` | hamobj/RhythmDetector | Template emitted in wrong TU |

### Partial Match Needs Investigation

These have existing implementations at 1-14% match — likely structurally wrong:

| Function | Match | Unit | Notes |
|----------|------:|------|-------|
| `FlowSlider::UpdateActivations` | 13.1% | flow/FlowSlider | Full impl exists, likely wrong control flow |
| `CharCameraInput::ResetSkeletonCharOrigin` | 13.8% | hamobj/CharCameraInput | Needs large struct offset knowledge |
| `HamCamShot::OnAllowableNextShots` | 10.9% | hamobj/HamCamShot | ObjDirItr + DataArray construction |
| `RndText::FontMap3d::AllocateMeshes` | 10.1% | rndobj/Text | Complex mesh allocation |
| `RndFont::CharWidthAdvanceCoords` | 9.0% | rndobj/Font | Glyph metric computation |
| `CharCameraInput::PollNewFrame` | 8.7% | hamobj/CharCameraInput | Skeleton frame polling |
| `HamRegulate::Poll` | 8.3% | hamobj/HamRegulate | Regulation with vector math |

## Top Remaining Units

| Unit | Count | Has RB3 Ref? | Difficulty |
|------|------:|:---:|------------|
| rndobj/Shader | 31 | Partial | High — NG rendering pipeline |
| rndobj/Utl | 16 | Yes (some) | Medium-High — mesh operations |
| rndobj/Text | 16 | Partial | High — text layout/rendering |
| hamobj/MoveDir | 12 | No | High — DC3-specific |
| hamobj/FreestyleMoveRecorder | 10 | No | High — DC3-specific |
| rndobj/AmbientOcclusion | 9 | No | High — AO computation |
| rndobj/PostProc_NG | 8 | Partial | High — NG post-processing |
| hamobj/HamDirector | 8 | No | High — DC3 game director |
| rndobj/Env_NG | 7 | Partial | High — NG environment |
| rndobj/Lit_NG | 6 | Partial | High — NG lighting |

## Recommendations

1. **Shader/NG functions** (31+8+7+6 = 52 functions): These are the Xbox 360 "next-gen" rendering pipeline. No RB3 equivalent. Need Ghidra + deep graphics knowledge.

2. **Utl mesh operations** (16 functions): Some have RB3 refs (RandomPointOnMesh done). Remaining are MakeNormals, ResetNormals, BurnXfm, TessellateMesh, BuildFromBSP — all large.

3. **DC3-specific hamobj** (~40 functions): MoveMgr, MoveDir, FreestyleMoveRecorder, HamDirector, HamSkeletonConverter, PoseFatalities — all Ghidra-only, complex game logic.

4. **FlowSlider::UpdateActivations** at 13.1%: Has full implementation, worth debugging with objdiff to find structural issues.

5. **Template instantiations**: Several `ObjDirItr<>`, `ObjectDir::New<>`, `ObjectDir::Find<>` need host functions implemented first to trigger emission.
