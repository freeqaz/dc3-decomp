# Bone Animation Pipeline: Start to Finish

Session: 2026-03-02
Goal: Understand how character animation works from clip data to rendered pixels.

## Overview

The pipeline has 5 stages:

```
1. CharClip (animation data)
   ↓ ScaleAdd: sample at beat, write to buffer
2. CharBones buffer (flat array of pos/quat/rot values)
   ↓ PoseMeshes: write buffer values into mesh LocalXfm
3. Bone mesh transforms (RndTransformable hierarchy)
   ↓ WorldXfm: parent-child chain
4. Skin matrices (offset * WorldXfm)
   ↓ GPU upload
5. Vertex shader (blend across bones)
```

## Stage 1: Animation Data (CharClip / CharBonesSamples)

**Files:** `src/system/char/CharClip.h`, `CharBonesSamples.h`

A `CharClip` stores keyframe animation data. Each clip has:
- `mFull` (CharBonesSamples) — main skeleton animation
- `mOne` (CharBonesSamples) — single-frame data (rest pose delta)
- `mFacing` (CharBonesSamples) — character facing/movement

`CharBonesSamples` stores the actual data:
- `mNumSamples` — number of keyframes
- `mRawData` — the actual bone data per keyframe
- `mFrames` — beat-to-sample index mapping

### Bone Types in the Buffer

```cpp
enum Type {
    TYPE_POS   = 0,  // Vector3 (12 bytes), or compressed (6 bytes)
    TYPE_SCALE = 1,  // Vector3
    TYPE_QUAT  = 2,  // Hmx::Quat (16 bytes), or short (8), or byte (4)
    TYPE_ROTX  = 3,  // float (4 bytes), or short (2)
    TYPE_ROTY  = 4,
    TYPE_ROTZ  = 5,
};
```

The bone buffer is laid out **sequentially by type**, not by bone:
```
[POS for bone0, bone1, bone2, ...]
[SCALE for bone0, bone1, ...]
[QUAT for bone0, bone1, ...]
[ROTX for bone0, ...]
[ROTY for ...]
[ROTZ for ...]
```

**Metadata arrays:**
- `mCounts[type]` — CUMULATIVE count: `mCounts[TYPE_QUAT]` = total POS + SCALE bones
- `mOffsets[type]` — byte offset into buffer for each section
- `mStart` — pointer to allocated buffer (16-byte aligned)

## Stage 2: CharDriver::Poll → ScaleAdd

**Files:** `src/system/char/CharDriver.cpp`, `CharClipDriver.cpp`

Each frame, `CharDriver::Poll()` is called:

1. `PreEvaluate(beat, deltaBeat, deltaSeconds)` — advance clip beat position
2. `Evaluate(beat, deltaBeat, deltaSeconds)` — returns blend sigmoid (0..1)
3. `ScaleDown(*mBones, deltaBeat)` — zeros the bone buffer for animated channels
4. `ScaleAdd(*mBones, weight)` — samples clip at current beat, writes weighted values

`CharClipDriver::ScaleAdd(CharBones& bones, float f)`:
- Computes `mWeight = f * EaseSigmoid(mBlendFrac, 0, 0)`
- Calls `bones.ScaleAdd(mClip, mWeight, mBeat, mDBeat)`
- If blending with next clip, passes `f - mWeight` to next driver

`CharBones::ScaleAdd(CharClip*, float weight, float beat, float dBeat)`:
- Converts beat to sample index
- For each bone type, reads sample data and does `buffer[i] += weight * sample[i]`
- With weight=1.0 after ScaleDown, this effectively sets the buffer values directly

## Stage 3: PoseMeshes — Buffer to Mesh Transforms

**File:** `src/system/char/CharBonesMeshes.cpp`

`CharBonesMeshes` maintains a parallel array `mMeshes[]` that maps each bone index to an `RndTransformable*` in the scene.

### Mesh Lookup (ReallocateInternal)

For each bone name (e.g., "bone_pelvis"), `CharUtlFindBoneTrans()` searches for:
1. `bone_pelvis.cb` → `CharBone::BoneTrans()` (if CharBone exists)
2. `bone_pelvis.trans` → direct RndTransformable
3. `bone_pelvis.mesh` → falls back to mesh

If none found, uses `sDummyMesh` (a shared throwaway transform).

### PoseMeshes Applies Buffer → LocalXfm

```cpp
void CharBonesMeshes::PoseMeshes() {
    iterator curMesh = mMeshes.begin();

    // TYPE_POS: set local positions
    for (pos < scaleOff; pos++, ++curMesh)
        (*curMesh)->SetLocalPos(*pos);

    // TYPE_QUAT: set local rotations from quaternion
    for (quat < quatEnd; quat++, ++curMesh) {
        Normalize(*quat, *quat);
        MakeRotMatrix(*quat, (*curMesh)->DirtyLocalXfm().m);
    }

    // TYPE_ROTX/Y/Z: set local rotation from single axis
    for (rotIt < rotyOff; rotIt++, ++curMesh)
        MakeRotMatrixX(*rotIt, (*curMesh)->DirtyLocalXfm().m);
    // etc.

    // TYPE_SCALE: apply scale to existing rotation
    for (scale < scaleEnd; scale++, ++curMesh) {
        MakeScale(xfm.m, scaleVec);
        xfm.m.x *= scale->x / scaleVec.x;
        // etc.
    }
}
```

**CRITICAL:** The iterator `curMesh` walks through ALL bone types sequentially. The bone ordering is:
- Bones 0..N_pos-1 get POS data
- Bones N_pos..N_pos+N_scale-1 get SCALE data
- Bones N_pos+N_scale..+N_quat get QUAT data
- ...and so on

The `mCounts[]` array tracks cumulative counts, and `PoseMeshes()` uses separate sections of `mMeshes[]` for rotations vs positions. Specifically:

```
curMesh starts at begin() for POS section
curMesh continues from POS end for SCALE (skipped if no scale bones)
BUT for QUAT/ROT sections, curMesh resets:
    curMesh = mMeshes.begin() + mCounts[TYPE_QUAT]
```

This means **the same mesh can appear in both POS and QUAT sections** — bone_pelvis gets its position from POS and its rotation from QUAT. The `mMeshes[]` array has entries for each bone channel, and a bone with both POS and QUAT data appears twice (once in each section).

Wait — actually no. Looking more carefully: `mMeshes` has ONE entry per bone in `mBones[]`. The bones are ordered [all POS bones][all SCALE bones][all QUAT bones][all ROTX...]. Each bone appears ONCE. So `bone_pelvis.pos` is one bone (gets POS), and `bone_pelvis.quat` is a DIFFERENT bone (gets QUAT). They both map to `bone_pelvis.mesh` via `CharUtlFindBoneTrans("bone_pelvis")`.

**KEY INSIGHT:** Multiple bone channels can map to the same mesh. `bone_pelvis.pos` sets the position, `bone_pelvis.quat` would set the rotation. But `PoseMeshes()` iterates with a SINGLE `curMesh` that advances linearly — so it processes POS for its meshes, then SCALE, then QUAT for different meshes. The QUAT section resets `curMesh` to `mMeshes.begin() + mCounts[TYPE_QUAT]`.

## Stage 4: WorldXfm — Parent-Child Transform Hierarchy

**File:** `src/system/rndobj/Trans.h`, `Trans.cpp`

Each `RndTransformable` has:
- `mLocalXfm` — position/rotation relative to parent
- `mParent` — parent transformable (or null for root)
- `mWorldXfm` — cached world-space transform
- `mDirty` — flag, recomputed lazily via `WorldXfm_Force()`

```cpp
const Transform& WorldXfm() {
    return !mDirty ? mWorldXfm : WorldXfm_Force();
}

// WorldXfm_Force: default constraint
mWorldXfm = Multiply(mLocalXfm, mParent->WorldXfm());
```

**Transform convention:** Row-vector: `pos_out = pos_in * M + t`

So `Multiply(child, parent, result)` means:
- `result.m = child.m * parent.m`
- `result.v = child.v * parent.m + parent.v`

The bone hierarchy for Aubrey (example):
```
(root/character)
  └─ bone_pelvis.mesh
      ├─ bone_spine1.mesh
      │   └─ bone_spine2.mesh
      │       └─ bone_spine_upper.mesh
      │           ├─ bone_L-clavicle.mesh → bone_L-upperArm.mesh → ...
      │           └─ bone_R-clavicle.mesh → ...
      ├─ bone_L-thigh.mesh → bone_L-knee.mesh → bone_L-ankle.mesh
      └─ bone_R-thigh.mesh → bone_R-knee.mesh → bone_R-ankle.mesh
```

## Stage 5: Skin Matrix Computation (Draw Time)

**File:** `native/src/platform/Mesh_Wgpu.cpp` line 722-741

Each `RndMesh` has bones: `mBones[]` with `mBone` (RndTransformable*) and `mOffset` (Transform).

The offset transform is the **inverse bind pose** — it transforms from model space to bone-local space.

```cpp
for (int i = 0; i < numBones; i++) {
    Transform skinMatrix;
    Multiply(mesh->BoneOffsetAt(i), boneTrans->WorldXfm(), skinMatrix);
    TransformToMat4(skinMatrix, boneUni.bones[i]);
}
```

This computes: `skinMatrix = offset * worldXfm`

Meaning: vertex position → bone local space (via offset) → world space (via bone's current world transform).

## Stage 6: Vertex Shader (GPU)

**File:** `native/src/gfx/standard_wgsl.inc`

```wgsl
@group(3) @binding(0) var<uniform> bones: array<mat4x4f, 40>;

fn vs_skinned(in: SkinnedVertexInput) -> VertexOutput {
    var skinnedPos = vec4f(0.0);
    for (var i = 0u; i < 4u; i++) {
        let w = weights[i];
        let idx = indices[i];
        if (w > 0.0) {
            let m = bones[idx];
            skinnedPos += w * (m * vec4f(in.position, 1.0));
        }
    }
    let worldPos = (object.world * vec4f(skinnedPos.xyz, 1.0)).xyz;
    out.clipPos = scene.viewProj * vec4f(worldPos, 1.0);
}
```

`TransformToMat4` writes the Transform as:
```
col0 = (m.x.x, m.x.y, m.x.z, 0)  // row 0 of rotation
col1 = (m.y.x, m.y.y, m.y.z, 0)  // row 1
col2 = (m.z.x, m.z.y, m.z.z, 0)  // row 2
col3 = (v.x, v.y, v.z, 1)         // translation
```

WGSL `m * vec4(pos, 1)` computes:
```
result.x = m.x.x*px + m.y.x*py + m.z.x*pz + v.x
```

This matches the Milo row-vector convention: `v_out = v_in * M + t`. ✓

## CharServoBone: Facing System

**File:** `src/system/char/CharServoBone.cpp`

After `PoseMeshes()`, CharServoBone applies the "facing" system:

Special bones:
- `bone_facing.pos` — position offset (character movement)
- `bone_facing.rotz` — rotation around Z axis
- `bone_facing_delta.pos` — frame-to-frame delta

`MoveToFacing(Transform& tf)`:
1. Rotates tf.m and tf.v around Z by `*mFacingRot`
2. Adds `*mFacingPos` to tf.v

This is applied to `mPelvis->DirtyLocalXfm()` — modifying the pelvis bone's local transform to include facing.

## Verified Correct / Investigation Results

### 1. Transform convention mismatch — VERIFIED CORRECT ✓
- Milo uses row-vector convention: `v * M`
- WGSL matrices are column-major
- `TransformToMat4` puts Milo rows into WGSL columns (Mesh_Wgpu.cpp:188-192)
- `m * pos` in shader computes the equivalent of `pos * M_milo`

### 2. Object world transform for skinned meshes — VERIFIED CORRECT ✓
- Mesh_Wgpu.cpp:704-709: skinned meshes use `identity` for object transform
- Bone matrices already produce world-space positions via `offset * WorldXfm`
- No double-transform

### 3. Byte-swapping for big-endian data — VERIFIED CORRECT ✓
- `CharBonesSamples::LoadData` byte-swaps when loading

### 4. LP64 pointer size — VERIFIED CORRECT ✓
- Bone buffer uses `sizeof(Vector3)` = 12, `sizeof(Hmx::Quat)` = 16, same ILP32/LP64
- No pointer-in-buffer issues

### 5. Dirty flag propagation — VERIFIED CORRECT ✓
- `SetDirty_Force()` (Trans.cpp:308) sets `mDirty=true` and cascades to ALL children
- `DirtyLocalXfm()` calls `SetDirty()` → `SetDirty_Force()` if not already dirty
- `WorldXfm()` lazily recomputes: `!mDirty ? mWorldXfm : WorldXfm_Force()`
- So PoseMeshes writing via `DirtyLocalXfm().m` correctly invalidates entire subtree

### 6. Animation is WORKING (2026-03-02 session) ✓
- Tested with aubrey01 + crew_battle_win clip at frames 30/60/90/120/150
- 67 bones stuffed from clips, all resolve via CharUtlFindBoneTrans
- Pelvis moves from T-pose origin to animated positions (e.g. -30,48,41)
- All 5 frame renders show distinct, correct dance poses with no mesh tearing
- Earlier "deformed" renders were from a previous build (before rim fix, PropertyTask stub)

## Remaining Potential Issues

### 1. Shutdown crash (device lost)
- `GpuDevice: device lost (reason 2): Device was destroyed` on exit
- Screenshot saves before crash, so non-blocking, but should fix cleanup order

### 2. Bone diagnostic dump still in draw path
- Mesh_Wgpu.cpp:730-763: one-time bone dump enabled via static bool
- Should be removed or gated behind `--verbose` for production

### 3. Scale bones not fully tested
- PoseMeshes writes scale data after POS but before QUAT section
- Scale is applied by adjusting existing rotation matrix axes
- Haven't confirmed scale bones (e.g. face morphs) work correctly

### 4. Finger/face bones
- crew_battle_win clip may not include finger bone channels
- Finger bones fall back to whatever LocalXfm was loaded from .milo (T-pose)
- This is correct behavior — same as Xbox runtime without finger clip data

## Key Code Locations

| Component | File | Line |
|-----------|------|------|
| Skin matrix computation | `native/src/platform/Mesh_Wgpu.cpp` | 737-761 |
| Object identity for skinned | `native/src/platform/Mesh_Wgpu.cpp` | 704-709 |
| TransformToMat4 | `native/src/platform/Mesh_Wgpu.cpp` | 188-193 |
| vs_skinned shader | `native/src/gfx/standard_wgsl.inc` | (4-bone blend) |
| CharDriver::Poll | `src/system/char/CharDriver.cpp` | (ScaleDown+ScaleAdd) |
| CharServoBone::Poll | `src/system/char/CharServoBone.cpp` | 71+ |
| PoseMeshes (write) | `src/system/char/CharBonesMeshes.cpp` | 93-133 |
| AcquirePose (read) | `src/system/char/CharBonesMeshes.cpp` | 49-91 |
| ReallocateInternal | `src/system/char/CharBonesMeshes.cpp` | 28-47 |
| CharUtlFindBoneTrans | `src/system/char/CharUtl.cpp` | 83-98 |
| SetDirty_Force cascade | `src/system/rndobj/Trans.cpp` | 308-315 |
| WorldXfm_Force | `src/system/rndobj/Trans.cpp` | (constraint-dependent) |
| advanceCharAnim lambda | `native/src/viewer/milo_viewer.cpp` | 1182+ |

## Viewer Animation Wiring

```
milo_viewer.cpp setup:
1. Find Character in base scene (ObjDirItr<Character>)
2. Load clips dir (--clips path.milo_xbox)
3. Create CharDriver if missing: charObj->New<CharDriver>("main.drv")
4. Create CharServoBone if missing: charObj->New<CharServoBone>("bone.servo")
5. Wire: driver->SetBones(servo), driver->SetClips(clipsDir)
6. StuffBones: iterate ALL clips, call clip->StuffBones(*servo)
   → Adds bone names → AddBones → ReallocateInternal → CharUtlFindBoneTrans
7. driver->Enter() (NOT Character::Enter — avoids CharHair/CharCollide crash)
8. driver->Play(clip, flags) with kPlayNow | kPlayLoop

Per-frame (advanceCharAnim lambda):
1. Advance in 0.1-beat steps to avoid huge delta
2. TheTaskMgr.SetSecondsAndBeat(seconds, beat, false)
3. charObj->Driver()->Poll()  → ScaleDown + ScaleAdd (writes bone buffer)
4. activeServo->Poll()        → PoseMeshes (buffer → mesh LocalXfm)
5. Draw path reads bone WorldXfm (lazy recompute from dirty local)
```
