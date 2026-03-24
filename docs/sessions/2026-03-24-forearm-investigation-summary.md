# Forearm Investigation Summary — 2026-03-24

## Problem Description

The live bug is a **runtime character pose/parity issue** affecting backup dancers. In-game,
the forearm/hand chain deforms incorrectly: forearms look stretched or rigid, with the visual
feel of an axis being flipped or a parent-space pose being wrong. The important narrowing from
recent pose dumps is:

- `foreTwist1/2` **local** transforms already match between direct clip pose and driver pose
- the divergence starts **upstream** on `upperArm` and `hand`
- the large `foreTwist` **world-space** drift is a downstream consequence, not strong evidence
  that `CharForeTwist` itself is the primary root cause

This means the bug is most likely in the **runtime/driver pose path** or a parent/world-space
propagation path, not in raw clip decode.

## Screenshot / Visual Understanding

I compared the archived crouch reference screenshots:

- `archive/screenshots-old/pose_regression/goldens/crouch_great_mid.png`
- `archive/screenshots-old/pose_regression/captures/crouch_great_mid.png`

The golden pose has the expected raised left arm and bent forearm. The bad capture looks much
closer to a near-rest arm pose, not a subtle twist-only error. That reinforces the conclusion
that this is likely a **runtime pose application / overwrite / omission** problem rather than
just a bind-pose mismatch in the forearm twist bones.

## What Was Confirmed

### 1. Backup merge is not losing arm pollables

I added a new regression test:

- `AssetLoadingTest.BackupOutfitPreservesArmPollableInventory`

File:

- `native/tests/test_asset_loading.cpp`

This test loads `main.milo_xbox`, inventories arm-related authored pollables, then merges the
backup outfit (`lush01_bd01`) and verifies that preexisting arm pollable class counts do not
drop.

Result from the one-off diagnostic run:

- main-before-backup:
  - `CharBlendBone`: 7
  - `CharForeTwist`: 2
- backup-after-merge:
  - `CharBlendBone`: 7
  - `CharForeTwist`: 2

No `CharIKHand`, `CharIKFingers`, `CharSleeve`, `CharPosConstraint`, or `CharBoneTwist`
objects were present on this broken backup character path.

This is an important narrowing:

- the backup character is **not** running hand IK or finger IK
- the authored arm-side procedural writers that remain are basically:
  - `CharForeTwist`
  - shoulder/thigh/spine `CharBlendBone`s

### 2. The authored `CharBlendBone`s are not hand/forearm writers

Diagnostic output showed these `CharBlendBone` objects:

- `shoulder_L-pos.blendbone`
- `shoulder_L-twist.blendbone`
- `shoulder_R-pos.blendbone`
- `shoulder_R-twist.blendbone`
- `spine_twist2.blendbone`
- `thigh_L-twist.blendbone`
- `thigh_R-twist.blendbone`

The shoulder blendbones interpolate between `bone_*_shoulderTwist1.mesh` and
`bone_*_shoulderTwist5.mesh`, targeting only the intermediate shoulder twist bones
(`shoulderTwist2/3/4`). They do **not** directly target `upperArm` or `hand`.

That does not completely rule them out as contributors, but it makes them weaker suspects than
the main driver/runtime path.

### 3. `CharIKHand::IKElbow()` had a real bug, but this asset path does not use it

I previously fixed:

- `src/system/char/CharIKHand.cpp`

Specifically:

- `if (mElbowSwing >= (unsigned int)1)` was changed to `if (0.0f < mElbowSwing)`

This matches the decompile and matters in general, because authored values like `.7` were being
suppressed before. However, for the current backup-dancer forearm issue, that path appears to be
irrelevant because the backup character inventory above does **not** contain active `CharIKHand`
pollables.

### 4. Several decompile checks did not reveal a fresh smoking gun

These functions were checked and did **not** show a clear semantic mismatch:

- `CharBlendBone::Poll()` — ~98.5%, decompile matches current logic
- `CharClipDriver::PreEvaluate(float, float, float)` — ~97.5%, no obvious semantic split
- `CharClipDriver::Evaluate(float, float, float)` — ~98.5%, no obvious semantic split
- `CharPosConstraint::Poll()` — 100%
- `CharBoneOffset::Poll()` — 100%
- previously: `MakeRotQuat`, `RndTransformable::SetWorldXfm`, `CharDriver::Poll` also did not
  expose a concrete parity bug

## Follow-Up Narrowing

Additional runtime probing on 2026-03-24 changed the ranking of suspects.

### 1. Live gameplay is not using arm `CharIKHand`

I added debug-only logging behind `MILO_DEBUG_ARM_POLLABLES=1` in:

- `src/system/char/CharIKHand.cpp`

Running the full scripted YMCA gameplay flow showed that `CharIKHand` does poll at runtime,
but only for foot IK objects:

- `left.ikfoot`
- `right.ikfoot`

These appear on:

- `backup0`
- `backup1`
- `player0`
- `player1`
- `iconman`

They are bound to:

- hand=`bone_L/R-ankle.mesh`
- finger=`spot_L/R-toe.trans`
- `moveElbow=0`

Important consequence:

- the live forearm/elbow bug is **not** being driven by an arm `CharIKHand::IKElbow()` path
- the earlier `CharIKHand` audit was still useful, but it is no longer the top suspect for this
  specific issue

### 2. Live gameplay really is using authored `CharForeTwist`

I added debug-only logging behind `MILO_DEBUG_ARM_POLLABLES=1` in:

- `src/system/char/CharForeTwist.cpp`

That run confirmed that gameplay is instantiating and polling:

- `foreTwist_L.ik`
- `foreTwist_R.ik`

for:

- `backup0`
- `backup1`
- `player0`
- `player1`
- `iconman`

The loaded authored values are:

- left: `offset=0`, `bias=0`
- right: `offset=180`, `bias=0`

This matches the property comment in `CharForeTwist.h` for `mOffset`, but it conflicts with:

- the top-level class comment in `CharForeTwist.h`, which says left/right are usually `90/-90`
- `CharacterTest::AddDefaults()`, which still seeds `90/-90`

That inconsistency is now one of the best leads in the tree.

### 3. The live `CharForeTwist` values look mirrored in a suspicious way

With the authored gameplay values (`0/180`), the runtime probe reported small mirrored twist
angles, for example:

- left: `final=+0.2941`
- right: `final=-0.2941`

I then added a debug-only experiment behind `MILO_DEBUG_FORETWIST_LEGACY90=1` that forces the
older `CharacterTest` convention:

- left: `offset=90`
- right: `offset=-90`

Under that override, the same live poses jumped to much larger positive values, for example:

- left: `final=+1.8649`
- right: `final=+1.2767`

This does **not** prove that the correct fix is “hardcode `90/-90`.” It does prove that the
forearm path is highly sensitive to the offset convention and that we now have a deterministic
way to probe it.

### 4. `CharForeTwist` is now a first-class suspect again

Earlier notes deprioritized `CharForeTwist` because direct-vs-driver pose dumps suggested that
`foreTwist1/2` local transforms already matched while drift started upstream on `upperArm` and
`hand`.

That narrowing is still useful, but the runtime probe changes the interpretation:

- the bug may still be upstream of the twist bones
- however, the authored forearm-twist constants and basis interpretation now look suspicious
  enough that `CharForeTwist::Poll()` can no longer be treated as a secondary suspect

## Updated Working Theory

The best current theory is:

- this is **not** a backup merge identity problem
- this is **not** an arm `CharIKHand` / elbow-IK problem during live gameplay
- this may be a **forearm-twist offset/basis-convention problem** in `CharForeTwist::Poll()`
- or a closely related transform-space issue feeding `CharForeTwist`:
  - `parentxfm.m.y`
  - `handxfm.m.z`
  - `Dot/Cross` sign
  - `MakeRotMatrixX`
  - the `twist2LocalX / handLocalX` placement ratio

There is also a nearby math-convention smell worth checking:

- `Hmx::Matrix3::RotateAboutZ()` in `src/system/math/Mtx.h`
- `MakeRotMatrixZ()` in `src/system/math/Rot.cpp`

These use opposite sign conventions. That is not the direct forearm path, but it is evidence
that our rotation helpers are not fully self-consistent.

## Where To Continue Investigation

### Highest-priority suspects now

1. `CharForeTwist::Poll()`
2. math helpers feeding twist-space basis/orientation
3. `CharBones::ScaleAdd`
4. world/local propagation around `RndTransformable`

### Specific next steps

1. Compare the actual `CharForeTwist` basis inputs, not just the final angle
   - Add targeted logging for:
     - `parentxfm.m.x/y/z`
     - `handxfm.m.x/y/z`
     - `twistparent` world matrix after the first `Multiply`
     - `mTwist2` world matrix after the second `Multiply`
   - Do this for one left/right pair on the same gameplay frame.
   - Goal: determine whether the authored `0/180` values are wrong on native/web, or whether the
     inputs are mirrored before `CharForeTwist` even runs.

2. Audit the rotation helper conventions used by the twist path
   - Reconcile:
     - `MakeRotMatrixX()` in `src/system/math/Rot.cpp`
     - `RotateAboutX()` in `src/system/math/Rot.cpp`
     - `Matrix3::RotateAboutZ()` in `src/system/math/Mtx.h`
     - `MakeRotMatrixZ()` in `src/system/math/Rot.cpp`
     - `Multiply(const Vector3&, const Hmx::Quat&, Vector3&)` in `src/system/math/Rot.cpp`
   - `Multiply(vec, quat)` is used directly by `CharUpperTwist::Poll()` and
     `CharNeckTwist::Poll()`, so it is still a plausible shared sign/basis offender.

3. Keep `CharBones::ScaleAdd` on the list, but no longer as the only top suspect
   - `HamDriver::LayerClip::Play()` still uses `bones.ScaleAdd(...)`, so runtime pose
     accumulation remains relevant.
   - However, the new live foretwist probes mean the next pass should not assume the bug lives
     only in clip blending.

4. Add a deterministic forearm guardrail
   - A useful next regression would be a native integration test or viewer probe that records:
     - `foreTwist_L.ik` effective angle
     - `foreTwist_R.ik` effective angle
     - `bone_L/R-foreTwist2.mesh` world matrices
   - This should be done on a fixed post-intro gameplay frame, not a viewer-only pose.

5. If a proof-only hack is needed, keep it env-gated
   - Current debug env vars:
     - `MILO_DEBUG_ARM_POLLABLES=1`
     - `MILO_DEBUG_FORETWIST_LEGACY90=1`
   - These are acceptable for narrowing parity gaps, but should not ship as the final fix.

## Files Changed In This Session

- `native/tests/test_asset_loading.cpp`
  - added `AssetLoadingTest.BackupOutfitPreservesArmPollableInventory`

- `src/system/char/CharIKHand.cpp`
  - debug-only runtime probe for `CharIKHand` pollables

- `src/system/char/CharForeTwist.cpp`
  - debug-only runtime probe for live foretwist values
  - debug-only `MILO_DEBUG_FORETWIST_LEGACY90` experiment

## Verification Runs

Inventory test:

```sh
cd native/build
cmake --build . --target milo-tests -- -j$(nproc)
./milo-tests --gtest_filter=AssetLoadingTest.BackupOutfitPreservesArmPollableInventory --gtest_color=no
```

Runtime pollable probe:

```sh
env DC3_DATA=orig-assets MILO_RENDER=1 MILO_HEADLESS=1 MILO_FATAL_FAILS=0 \
  DC3_SHOW_SPLASH=0 MILO_DEBUG_ARM_POLLABLES=1 \
  MILO_MAX_FRAMES=9050 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  timeout 180 native/build/dc3-native 2>&1 | rg 'FORETWIST|ARM-POLL|ARM-ELBOW'
```

Legacy foretwist offset experiment:

```sh
env DC3_DATA=orig-assets MILO_RENDER=1 MILO_HEADLESS=1 MILO_FATAL_FAILS=0 \
  DC3_SHOW_SPLASH=0 MILO_DEBUG_ARM_POLLABLES=1 MILO_DEBUG_FORETWIST_LEGACY90=1 \
  MILO_MAX_FRAMES=9050 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  timeout 180 native/build/dc3-native 2>&1 | rg 'FORETWIST'
```

Key results:

- live gameplay arm IK is foot-only, not elbow/arm IK
- live gameplay really does poll authored `CharForeTwist`
- gameplay currently loads `0/180` offsets with `0` bias
- forcing `90/-90` materially changes the computed twist angles, making the foretwist
  offset/basis interpretation the best next branch to pursue

## Follow-up Session (2026-03-24 continued)

### Deep investigation findings

1. **CharForeTwist::Poll() matches target**: Verified against Ghidra decompilation and RB3
   reference — the math is identical. No semantic bug in the forearm twist algorithm.

2. **All known decompiler bugs already have native fixes**:
   - `Multiply(Vector3, Quat, Vector3)` in Rot.cpp — `#ifdef HX_NATIVE` fix present
   - `Multiply(Transform, Transform, Transform)` in mtx.cpp — `#ifdef HX_NATIVE` fix present
   - `CharBones::RotateBy` compressed paths — `#ifdef HX_NATIVE` fixes present
   - `CharBones::RotateTo` compressed paths — `#ifdef HX_NATIVE` fixes present
   - `CharBones::RotateBy` uncompressed path — verified correct (no fix needed)

3. **NEW BUG FOUND: `CharBones::RotateTo` uncompressed path** (lines 1251-1254):
   The PPC decomp has cross-product terms negated in x, y, z components of the quaternion
   multiplication (w is correct). Same class of decompiler register swap as the compressed
   paths. **Missing `#ifdef HX_NATIVE` fix applied** — adds correct quaternion multiply
   formula matching the pattern used by the compressed paths.

4. **Milo-viewer direct-pose path now produces correct poses**: Running the crouching_great_01
   clip produces the expected dynamic crouch, matching the golden reference. The archived "bad"
   captures in `archive/screenshots-old/` appear to be from an older build.

5. **MakeRotMatrixZ vs Matrix3::RotateAboutZ sign mismatch**: These two functions produce
   rotation matrices with opposite sign conventions (clockwise vs counterclockwise). However,
   CharForeTwist only uses X-axis rotations, so this doesn't affect the forearm path directly.

6. **GPU skinning pipeline verified correct**: The `TransformToMat4` row-major storage combined
   with WGSL's `mat4x4f * vec4f` column-vector multiply produces the correct row-vector
   transformation. No matrix convention mismatch.

### File changed

- `src/system/char/CharBones.cpp`
  - Added `#ifdef HX_NATIVE` fix for uncompressed `RotateTo` quaternion multiply
    (lines 1251-1254). Cross-product terms in x/y/z were negated due to decompiler
    register swap. Follows same fix pattern as compressed paths at lines 959-970,
    992-1003, 1168-1180, 1210-1222.

### Updated status

The viewer's direct-pose path works correctly. If the gameplay forearm issue persists,
it is likely in the CharDriver/CharClipDriver pipeline or backup-dancer-specific setup,
not in the math functions or rendering pipeline. The `RotateTo` uncompressed fix is a
correctness fix regardless (affects any clip using `kApplyRotateTo` with `kCompressNone`).

## Latest Narrowing (2026-03-24 later)

The investigation moved past the “missing elbow bone” theory.

### What is now ruled out

1. `bone.servo` is not dropping the elbow channels
   - New regression:
     - `AssetLoadingTest.BoneServoCarriesAndAppliesForeArmRotZChannels`
   - Result:
     - `bone_L/R-foreArm.rotz` are present on `bone.servo`
     - clip evaluation writes non-zero elbow angles into the servo buffer
     - `servo->PoseMeshes()` applies those angles correctly to `bone_L/R-foreArm.mesh`

2. Live render-time arm transforms are not straight
   - The env-gated render probe at:
     - `MILO_DEBUG_ARM_CHAIN_FRAME=8000`
     - `MILO_DEBUG_ARM_CHAIN_DIR=player0`
   - shows that live gameplay has a bent:
     - `upperArm -> foreArm -> hand`
   - chain in render-time world space.

3. Live render-time foretwist bones are not stuck at the upper arm
   - I extended the same render-time probe to dump:
     - `foreTwist1`
     - `foreTwist2`
   - At the inspected live gameplay frame, both left and right foretwist bones had distinct
     world positions between upper arm and hand. So the authored `CharForeTwist` path is
     producing meaningful placement by the time the renderer reads bone transforms.

### What was newly confirmed

1. The visible outfit meshes are heavily weighted to foretwist bones
   - New regression:
     - `AssetLoadingTest.SkinnedMeshesCarryNontrivialForeTwistWeights`
   - On merged backup-outfit meshes such as:
     - `lush_bd_outfit.1.mesh`
     - `lush_bd_outfit_lod1*.mesh`
   - the compressed skinned vertex data shows large nontrivial weight totals on:
     - `bone_L/R-foreTwist1.mesh`
     - `bone_L/R-foreTwist2.mesh`
   - This proves the foretwist bones materially affect the rendered forearm volume.

2. These meshes are compressed-only in native
   - The same audit showed:
     - `raw=0`
     - `compressed>0`
   - for the relevant outfit body meshes.
   - That means the bad deformation path is specifically going through the compressed skinned
     vertex pipeline at runtime.

3. Generic matrix conventions still do not look like the root cause
   - New synthetic tests in `native/tests/test_mesh_loading.cpp` show:
     - uncompressed skinned GPU-style matrix application matches CPU skinning
     - compressed synthetic skinning also matches closely after proper big-endian serialization,
       with only small expected quantization error
   - So there is no broad “all GPU skinning is transposed” bug.

### Current best boundary

The live bad forearm shape happens **after**:

- clip decode
- `bone.servo`
- elbow (`foreArm.rotz`) application
- authored `CharForeTwist`
- render-time world-transform propagation

and **before / during** the final deformation of compressed outfit meshes.

### Best remaining suspects

1. Asset-specific assumptions in compressed skinned vertex decode
   - `native/src/gfx/VertexFormats.cpp`
   - especially how real DC3 outfit meshes use:
     - `UDEC4N` bone weights
     - `UBYTE4` bone indices
   - even though the generic synthetic decode path is basically correct.

2. Real-asset weight/index interpretation on forearm vertices
   - The next step should inspect representative vertices from:
     - `lush_bd_outfit.1.mesh`
   - and determine which slots / weights are driving vertices around the elbow and wrist.

3. Actual live mesh/bone-palette use versus the diagnostic body mesh
   - Confirm that the visually broken forearm is coming from the same skinned mesh instances
     whose bone palettes and weights were audited.

### New commands / probes that were useful

Render-time arm chain dump on a post-intro gameplay frame:

```sh
env DC3_DATA=orig-assets MILO_RENDER=1 MILO_HEADLESS=1 MILO_FATAL_FAILS=0 \
  DC3_SHOW_SPLASH=0 MILO_DEBUG_ARM_CHAIN_FRAME=8000 MILO_DEBUG_ARM_CHAIN_DIR=player0 \
  MILO_MAX_FRAMES=8050 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  timeout 180 native/build/dc3-native >/tmp/arm_chain_8000_twist.log 2>&1
```

Foretwist runtime probe:

```sh
env DC3_DATA=orig-assets MILO_RENDER=1 MILO_HEADLESS=1 MILO_FATAL_FAILS=0 \
  DC3_SHOW_SPLASH=0 MILO_DEBUG_ARM_POLLABLES=1 \
  MILO_MAX_FRAMES=9050 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  timeout 180 native/build/dc3-native >/tmp/foretwist_runtime.log 2>&1
rg 'FORETWIST' /tmp/foretwist_runtime.log
```

## Continued Investigation (2026-03-24 later session)

### Viewer architecture fix: use engine poll path

The milo-viewer was using a custom `CharTwistSolver::SolveAll()` with manual `ObjDirItr<CharPollable>`
iteration that bypassed the engine's `CharPollGroup` dependency sorting. This was replaced with
direct `Character::Poll()` calls, matching the game engine's exact rendering path:

- `Character::Poll()` → `RndDir::Poll()` → iterates `mPolls[]` (contains `CharPollGroup`)
- `CharPollGroup::Poll()` iterates its `mPolls` list sorted by `CharPollableSorter::Sort()`
  (topological sort via `PollDeps()` declarations)
- Ordering: CharDriver → CharServoBone → CharIKHand → CharUpperTwist → CharForeTwist → etc.

The old viewer path had a pollable ordering bug: `CharForeTwist` could run before
`CharUpperTwist`. When `CharUpperTwist` later calls `upperArm->SetWorldXfm()`, `SetDirty()`
cascades through foreArm → foreTwist1 → foreTwist2 → hand, destroying the twist bone world
transforms that `CharForeTwist` just computed. Those bones then recompute from clip-derived
locals instead of twist-solver values.

However, **this viewer fix is not the root cause of the gameplay arm issue**, since `dc3-native`
already uses the engine's native `Character::Poll()` path and still exhibits the bug.

### Files changed

- `native/src/viewer/ViewerAnimation.cpp`
  - `AdvanceBeat()`: replaced manual pollable loop + `CharTwistSolver::SolveAll()` with
    `character->Poll()`
  - `DirectPose()`: replaced manual pollable loop + `CharTwistSolver::SolveAll()` with
    `PoseMeshesWithFacing()` + `character->Poll()`
  - Removed `#include "char/CharTwistSolver.h"`

- `native/src/viewer/ViewerCapture.cpp`
  - Replaced two `CharTwistSolver::SolveAll()` calls with `charAnim.character->Poll()`
  - Removed `#include "char/CharTwistSolver.h"`

- `native/src/char/CharTwistSolver.cpp`
  - Fixed pollable ordering: split single iteration into two passes (CharUpperTwist first,
    then CharForeTwist) — still used by CharTwistSolver itself but no longer called from viewer
  - Enhanced one-time hierarchy dump: includes constraint type, full local rotation matrix,
    upper twist bones
  - Added periodic post-solve arm geometry check (bendSin, foreArmRotIdentity, constraint)

### Compressed vertex decode verified correct

Exhaustive review of the compressed skinned vertex pipeline confirmed every step is
mathematically correct:

1. **Field name swap**: `VertexFormats.cpp` correctly accounts for the misleading field names
   in `CompressedVertex_Xbox`. `mBoneIndices` (offset 28) = bone WEIGHTS (UDEC4N),
   `mBoneWeights` (offset 32) = bone INDICES (UBYTE4). Unpack functions read from correct fields.

2. **Big-endian byte swap**: `bswap32` correctly converts from big-endian file format.
   Round-trip `FillCompressedVertex` → `SaveCompressedVertex` (WriteEndian) → `ReadChunks`
   (raw read) → `UnpackCompressedSkinnedVertices` (bswap32 + extract) is correct.

3. **UDEC4N bit extraction**: Bits [0:9]=weight.x, [10:19]=weight.y, [20:29]=weight.z,
   [30:31]=weight.w. Matches `PackVector` packing exactly.

4. **UBYTE4 byte extraction**: After bswap, byte 0=idx0, byte 1=idx1, byte 2=idx2, byte 3=idx3.
   Matches packing `(idx3<<24)|(idx2<<16)|(idx1<<8)|idx0`.

5. **Weight-index pairing**: GPU shader correctly pairs `boneWeights[i]` with `boneIndices[i]`.

6. **Struct layout**: `CompressedVertex_Xbox` is 36 bytes (9 × 4-byte int), same on both
   platforms. `GpuVertexSkinned` has correct attribute offsets matching WGSL shader layout.

7. **Matrix conventions**: `TransformToMat4` row-major + WGSL column-major = correct row-vector
   transformation.

### Updated suspect ranking

#### 1. `RemoveInvalidBones()` stale bone index — HIGH priority

`Mesh.cpp:1083` erases null bone entries from `mBones`, shifting all subsequent indices.
But compressed vertex data still has the **old** indices baked in. If any bones fail to resolve
during outfit merge, forearm vertices would skin to wrong bone matrix slots.

This would explain why:
- Synthetic tests pass (no null bones to remove)
- Real outfit merges show wrong deformation (bones removed → index shift → wrong skinning)

**Next step**: Add a diagnostic log in `RemoveInvalidBones()` to see if any bones are being
removed from outfit meshes at runtime.

#### 2. `CharForeTwist` offset convention — MEDIUM priority

Authored values `0/180` produce tiny twist angles (±0.29 rad) vs legacy `90/-90` which produces
larger angles (+1.87/+1.28 rad). Prior session confirmed `0/180` matches what the original game
loads, so this may be correct behavior. But it remains worth investigating whether the native
port's rotation math produces the same output as the Xbox 360 for these inputs.

#### 3. Compressed vertex decode — RULED OUT

No bug found in the byte-level data flow. Every step from pack to unpack to GPU upload is
mathematically correct.

### Deep-dive analysis (2026-03-24 continued)

#### What was verified correct

1. **CharForeTwist::Poll() math**: Verified against Ghidra decompilation and RB3 reference.
   Identical algorithm — no semantic bug. The offset values `0/180` are what the game data
   loads; the math handles them correctly.

2. **All PPC decompiler register-swap bugs already have native fixes**:
   - `Multiply(Vector3, Quat, Vector3)` in Rot.cpp
   - `Multiply(Transform, Transform, Transform)` in mtx.cpp
   - `CharBones::RotateBy` compressed paths (ByteQuat, ShortQuat)
   - `CharBones::RotateTo` compressed paths (ByteQuat, ShortQuat)
   - `CharBones::RotateBy` uncompressed — verified correct (no fix needed)

3. **Arm bone hierarchy mapped**: The forearm uses two parallel chains from `upperArm`:
   ```
   upperArm ─┬─ foreArm ─── hand          (elbow bend chain, foreArm.rotz)
              └─ foreTwist1 ─ foreTwist2   (twist distribution, procedural)
   ```
   The `bone_*-foreArm.mesh` bone IS the elbow joint, stored as `TYPE_ROTZ` in clips.
   The hand is parented to foreArm (NOT to foreTwist2). CharForeTwist reads the foreArm's
   world transform as `parentxfm` and distributes twist from there to the foreTwist bones.

4. **Real compressed vertex data inspected** (test: `InspectForearmVertexBoneAssignments`):
   - `lush_bd_outfit.1.mesh` has 35 bones in its palette
   - Forearm vertices correctly reference `bone_L-foreTwist2.mesh` (idx 15, ~60-90% weight)
     and `bone_L-hand.mesh` (idx 24, ~10-34% weight)
   - All bone indices are in range, weight sums ≈ 1.0
   - Bone offsets have reasonable bind-pose values

5. **CPU skinning test verified correct** (test: `CpuSkinForearmVertexFromCompressedMesh`):
   With proper clip pose + twist solve, the skin matrices for foreTwist2 and hand are
   **correctly different** (not identical), and CPU-skinned vertex positions are geometrically
   reasonable. ARM-CHECK shows `bendSin=0.7640 bent`. The data and math are both correct in
   a controlled test environment.

6. **GPU pipeline verified correct**:
   - `TransformToMat4` row-major + WGSL column-major = correct row-vector transformation
   - `object.world` correctly uses identity for skinned meshes (no double-transform)
   - Bone matrices uploaded via `FillBoneUniforms` match CPU-side computation

#### New bug found and fixed

**`CharBones::RotateTo` uncompressed path** (lines 1251-1254): Missing `#ifdef HX_NATIVE` fix.
The PPC decompiled code has cross-product terms negated in the quaternion multiplication's
x/y/z components (w is correct). Same class of decompiler register-swap error as the compressed
paths. Added correct quaternion multiply formula. Affects any clip using `kApplyRotateTo` with
`kCompressNone` data. PPC match unchanged at 74.0%.

#### Key insight: test environment vs live gameplay

The CPU skinning test proves the data pipeline is correct in isolation. But the live gameplay
bug persists. This means the issue is in **execution context** — something about how the live
game's rendering pipeline interacts with bone transforms at draw time, not in the data or math
themselves.

### Refined suspect ranking

#### 1. `RemoveInvalidBones()` stale bone index — HIGHEST priority

`Mesh.cpp:1083` erases null bone entries from `mBones`, shifting all subsequent indices.
Compressed vertex data still has the **old** indices baked in. If any bones fail to resolve
during outfit merge, forearm vertices would skin to wrong bone matrix slots.

This would explain:
- Synthetic/unit tests pass (no null bones to remove in controlled setup)
- Real outfit merges show wrong deformation (bones removed → index shift → wrong skinning)
- The "straight forearm" look: if forearm vertices get mapped to a wrong bone (e.g., the
  parent bone), all vertices deform as if attached to a single bone → rigid straight segment

**Concrete next steps**:
1. Add diagnostic log in `RemoveInvalidBones()` to count removed bones and print their names
2. Run the full gameplay flow and check if outfit mesh bones are being removed
3. If confirmed: either skip removal for compressed meshes or rebuild the index mapping

#### 2. Render-time bone transform staleness — MEDIUM priority

The rendering pipeline reads `WorldXfm()` during `FillBoneUniforms`. If bones are marked
dirty (by a late poll or SetWorldXfm) but not yet recomputed when `FillBoneUniforms` runs,
the shader would get stale transforms. This would make foreTwist bones appear at their
pre-twist positions (identity local rotation → straight arm).

**Concrete next steps**:
1. Add a `Dirty()` check in `FillBoneUniforms` for arm bones
2. If any arm bone is dirty at draw time, log it — this would prove the bone wasn't finalized

#### 3. `CharForeTwist` offset convention — LOW priority

Authored `0/180` produces smaller twist angles than legacy `90/-90`, but the investigation
confirmed this matches the original game data. The visual impact is likely correct behavior,
not a bug. Deprioritized.

#### 4. Compressed vertex decode — RULED OUT

Exhaustive byte-level and end-to-end verification. No bug found.

### Screenshots

Reference screenshots in `archive/screenshots/arm-bend-test/`:
- `frame_02000.png` through `frame_05000.png` — gameplay frames showing the arm rigidity issue
- Characters are animating (different poses per frame) but forearms follow upper arm direction
  too closely, lacking natural elbow crook

## ROOT CAUSE FOUND AND FIXED (2026-03-24 final session)

### Root cause: pollable ordering + SetWorldXfm dirty cascade

The forearm rigidity was caused by **CharUpperTwist::Poll() running AFTER CharForeTwist::Poll()**
in the CharPollGroup topological sort. The two pollables have no direct dependency edge between
them, so their relative ordering is arbitrary and platform-dependent.

The execution sequence was:

1. **CharForeTwist::Poll()** runs, calls `SetWorldXfm()` on foreTwist1 and foreTwist2 with
   correct twist rotations. `SetWorldXfm()` writes `mWorldXfm` and clears `mDirty`, but does
   NOT update `mLocalXfm`.

2. **CharUpperTwist::Poll()** runs, calls `SetWorldXfm()` on `mUpperArm` (the upper arm bone).
   This cascades `SetDirty()` to all children of upperArm, including foreTwist1 and foreTwist2.

3. At **render time**, `FillBoneUniforms()` calls `WorldXfm()` on each bone. For foreTwist1/2,
   `mDirty=true`, so `WorldXfm_Force()` recomputes the world transform from the **stale**
   `mLocalXfm` (the clip-derived bind-pose local, NOT the twist-solver output). The correct
   world transforms that CharForeTwist wrote are discarded.

4. The result: foreTwist1/2 skin matrices are **identical** to the upper arm's skin matrix.
   Forearm vertices deform as if attached to the upper arm — straight/rigid forearms.

### Diagnostic evidence

Tracing confirmed the exact sequence:
```
FORETWIST-WRITE foreTwist_L.ik (backup0)     # CharForeTwist writes twist bones
FORETWIST-WRITE foreTwist_R.ik (backup0)
UPPERARM-SETWORLDXFM bone_L-upperArm (backup0) # CharUpperTwist writes upperArm → dirties children
FORETWIST-DIRTY bone_L-foreTwist1 wasDirty=0    # foreTwist1 was clean, now dirty
FORETWIST-DIRTY bone_L-foreTwist2 wasDirty=0    # cascade to child
```

Before fix, skin matrices at draw time (6 decimal places):
- upperArm skin[0..3]:  `0.942566 0.089409 0.321832`
- foreTwist1 skin[0..3]: `0.942566 0.089409 0.321832` (IDENTICAL)
- foreTwist2 skin[0..3]: `0.942566 0.089409 0.321832` (IDENTICAL)

After fix:
- upperArm skin[0..3]:  `0.948758 0.081575 0.305294`
- foreTwist1 skin[0..3]: `0.470068 -0.396509 0.788472` (DIFFERENT — twist applied)
- foreTwist2 skin[0..3]: `0.456815 -0.496763 0.737782` (DIFFERENT — more twist)

### Fix

After `SetWorldXfm()` in CharForeTwist::Poll() and CharUpperTwist::Poll(), also compute and
store the corresponding `mLocalXfm` so that dirty-cascade recomputation produces the correct
world transform. Guarded by `#ifdef HX_NATIVE` — PPC decomp code is unchanged.

The local is computed as: `mLocalXfm = childWorld * Inverse(parentWorld)`.

This is robust regardless of poll ordering: even if a later pollable dirties the bone,
`WorldXfm_Force()` will recompute the correct world from the updated local.

### Files changed

- `src/system/char/CharForeTwist.cpp`
  - After each `SetWorldXfm()` call, compute and store `mLocalXfm` (HX_NATIVE only)

- `src/system/char/CharUpperTwist.cpp`
  - Same fix: after `SetWorldXfm()` on mUpperArm and mTwist1, update `mLocalXfm`

- `src/system/rndobj/Trans.h`
  - Added `friend class CharUpperTwist` (CharForeTwist was already a friend)

### What was ruled out

1. **RemoveInvalidBones stale bone index** — no bones removed during gameplay
2. **Render-time bone transform staleness (dirty=1)** — all bones were dirty=0 at draw time
   (misleading: dirty was cleared by WorldXfm_Force recomputing from stale local)
3. **Compressed vertex decode** — exhaustive byte-level verification, all correct
4. **CharForeTwist math** — verified identical to Ghidra decompilation and RB3 reference
5. **CharBones decompiler bugs** — all had `#ifdef HX_NATIVE` fixes already
6. **GPU skinning pipeline** — matrix conventions verified correct
