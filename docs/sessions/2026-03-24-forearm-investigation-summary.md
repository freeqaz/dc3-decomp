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
