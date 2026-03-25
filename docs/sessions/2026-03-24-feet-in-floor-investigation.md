# Feet In Floor Investigation — 2026-03-24

## Problem
Characters' feet clip through the floor on the native port.

## Resolution: DECOMP BUG IN HamIKEffector::Poll()

The IK ground clamping code had `.v.y` (Y = forward axis) where the original binary
uses `.v.z` (Z = up/height axis). Ghidra decompilation of target @ 0x824c21e8 confirmed
via stack variable mapping (local_188 = QuatXfm.v.z). Six member access bugs fixed.

## Coordinate System
- Milo: X=right, Y=forward, Z=up. Ground plane at Z=0 (shadow_plane = 0,0,1,0)
- sFlipYZ in view matrix swaps Y↔Z for D3D/WebGPU rendering
- Character rest-pose: pelvis Z≈42.5, head Z≈64 (from bone ground truth tests)

## Ruled Out
- **Matrix convention**: Row-major → WGSL column-major with `M*v` verified correct
- **Bone offset loading**: `operator>>(BinStream&, RndBone&)` loads mOffset; constructor Reset() is just pre-init
- **Compressed vertex unpacking**: Bone indices/weights correctly byte-swapped
- **sFlipYZ**: Applied uniformly via view matrix to ALL geometry
- **Depth compare**: Correctly `Less` for opaque on both platforms
- **Identity world for skinned**: Correct — bone matrices include full parent chain

## Active Hypotheses

### H1: HamIKEffector coordinate mismatch
HamIKEffector.cpp:388 compares `.v.y` (forward) against `groundHeight` (which is `.v.z` = up).
The IK system operates in 2D (only x,y; z always 0). May use projected coordinate space.
If projection isn't working or IK isn't running, feet won't be clamped to ground.

### H2: Skinned mesh WorldXfm not truly identity
If character body mesh has a non-identity WorldXfm (from parent chain), using identity
drops that offset. Bones already include it, so identity SHOULD be correct, but need to verify.

### H3: Missing ground reference in native
HamIKEffector::GetGroundHeight() needs mGround or mCharacter. If venue ground reference
object isn't loaded, fallback uses character's own Z as ground height (42.5 instead of 0).

### H4: HamIKSkeleton::NeutralWorldXfm coordinate space
The IK operates in a 2D projected space. NeutralWorldXfm might transform bone positions
into a character-relative 2D space where Y=up. Need to verify this transformation.

### H5: CharServoBone bone_facing initial Z position
If bone_facing.pos Z component isn't properly set, the character's root vertical position
could be wrong. CharServoBone::Poll() uses mFacingPos to position the character.

## ROOT CAUSE FOUND

**HamIKEffector::Poll() has `.v.y` where target binary uses `.v.z` for ground clamping.**

Ghidra decompilation of target @ 0x824c21e8 confirms:
- `local_188` (= QuatXfm.v.z, height axis) is compared to groundHeight
- Our decomp uses `effQ.v.y` (forward axis) — wrong member!

### All bugs in HamIKEffector::Poll():
1. Ankle ground clamp: `effQ.v.y < groundHeight` → should be `effQ.v.z`
2. Ankle ground assign: `effQ.v.y = groundHeight` → should be `effQ.v.z`
3. Ankle restore: `effQ.v.x = savedPos.x` → should be `effQ.v.y = savedPos.y`
4. Ankle clampFactor: `neutralQ.v.y` → should be `neutralQ.v.z`
5. Pelvis blend: `effQ.v.y` in interpolation formula → should be `effQ.v.z`
6. Pelvis denominator: `effQ.v.y - groundHeight` → should be `effQ.v.z - groundHeight`

### Verification
- Floor at Z=0 (shadow_plane 0,0,1,0; venue GLI_Floor.mesh worldPos.z=0)
- Character pelvis at Z=42.5
- Milo: X=right, Y=forward, Z=up
- Target clamps Z (height); decomp clamps Y (forward) — feet never constrained vertically
