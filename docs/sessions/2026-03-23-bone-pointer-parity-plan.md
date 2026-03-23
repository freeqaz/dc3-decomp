# Bone Pointer Parity Plan — 2026-03-23

## Goal

Resolve the native backup-character deformation bug where backup dancer forearms are stretched
or rigid-body deformed instead of animating per-bone.

This plan is intentionally staged:

1. Add a small, targeted native fix to prove the diagnosis.
2. Lock the desired behavior into failing tests first.
3. Use those tests as the north star while converging toward Xbox-equivalent merge behavior.

## Problem Statement — REVISED

The original hypothesis was a scope/identity split: the servo animates one set of bone
`RndTransformable`s while skinned meshes read a different set with the same names.

**This hypothesis was disproven.** The bone pointer identity test passes with 0 mismatches
(179 bones across 9 skinned meshes, all pointing at the same objects the servo uses). The
merge infrastructure — including the post-merge subdir registration in
`FileMerger::FinishLoading()` and `CharUtlFindBoneTrans` — already produces correct pointer
identity on native.

**The forearm deformation bug has a different root cause.** Since bone pointers are identical,
the problem is upstream: either the servo is not writing correct transforms to those bones, or
the bone offset matrices / bind poses are wrong after the merge. Possible causes:

1. **CharServoBone::ReallocateInternal not called** after the backup outfit merge — the servo's
   `mMeshes` vector (which maps bones to RndTransformables) might be stale from a prior outfit
   or never populated for the backup character's skeleton layout.
2. **CharClip data not loaded** — the backup outfit's animation clips may fail to load or bind,
   so the servo has nothing to animate with. The servo would write identity or rest-pose
   transforms instead of per-frame animation.
3. **Bone offset matrices (bind pose)** from `RndMesh::BoneOffsetAt()` may not match the
   skeleton that the servo is driving. If the backup outfit's meshes were authored against a
   different skeleton than the one the servo resolves, the skin matrices would be wrong even
   with correct world transforms.
4. **CharTwistSolver / CharIKRod** not running for backup characters — twist bones (forearm
   twist, upper arm twist) are procedurally driven. If the twist solver isn't polling for
   backup characters, those bones stay at rest pose while parent bones animate, creating the
   visible stretch artifact.
5. **WorldXfm dirty flags** not propagating through the bone hierarchy after servo writes — if
   `DirtyLocalXfm()` doesn't invalidate the world transform cache for children, downstream
   bones would read stale transforms.

## Session Findings — 2026-03-23

### Test infrastructure fixed

The `BackupOutfitBonePointersMatchServoDirectory` test was failing with 0 skinned meshes
because `HamCharacter::PostLoad()` starts **async** outfit loading via
`TheFileMergerOrganizer`. This sets `mOrganizer = TheFileMergerOrganizer` on the
FileMerger. In the game, `TheLoadMgr.Poll()` runs every frame and eventually drains the
organizer. In tests with no game loop, `mOrganizer != this` blocked
`FileMerger::StartLoadInternal` from launching any sync loaders.

**Fix**: Added `FileMerger::ForceReleaseOrganizer()` (native-only) — cancels pending async
loads and resets `mOrganizer = this`, allowing sync `StartLoad(false)` to proceed.

After the fix, the test runs the full outfit merge pipeline and checks bone pointer identity.

### Bone pointer identity is already correct

```
backup outfit pointer audit: skinnedMeshes=9 checkedBones=179 unresolved=0 mismatches=0
```

Every mesh bone pointer in the backup outfit (`lush01_bd01`) matches the corresponding
`CharUtlFindBoneTrans` result from `bone.servo->Dir()`. The native merge infrastructure
(post-merge subdir registration, `MergeDirs`, `MergeAction` filter) produces correct
pointer identity without any additional fixups.

### Null GPU backend crash (sandbox-specific)

When draining the organizer naturally via `TheLoadMgr.Poll()` under the Dawn Null GPU
backend (sandbox mode where Vulkan ICD is blocked), the default outfit merge triggers a
glibc "corrupted double-linked list" abort during `ReplaceList`. This does **not**
reproduce with a real GPU (RTX 3090, tested both in gdb and standalone). The crash is
specific to object lifecycle differences in the Null backend (likely
texture/material cleanup ordering). `ForceReleaseOrganizer` sidesteps this entirely.

### FileMerger organizer lifecycle

```
HamCharacter::PostLoad
  → FileMerger::StartLoadInternal(async=true)
    → TheFileMergerOrganizer->AddFileMerger(this)
      → fm->mOrganizer = TheFileMergerOrganizer   // blocks sync StartLoad
      → creates FileMergerOrganizerLoader          // needs TheLoadMgr.Poll() to dispatch
```

When the organizer dispatches the load, it calls `FileMerger::LaunchNextLoader()`, which
creates a `DirLoader`. The DirLoader loads the outfit .milo from the archive
(`CachedPath` transforms `foo.milo` → `foo/gen/foo.milo_xbox`). After all mergers complete
(or fail), `RemoveFileMerger` resets `mOrganizer = merger`.

Key insight: `GetOutfitModel()` returns paths from the CHARACTERS macro in
`config/macros.dta` (loaded from the ark archive). The `mOutfitDir` field is only used to
determine `mIsCampaignChar` — it does **not** participate in path construction.

## Phase 0: Regression Tests — COMPLETE

### Test 1: Bone pointer identity — PASSES

`AssetLoadingTest.BackupOutfitBonePointersMatchServoDirectory`

- Loads `main.milo_xbox` from MILO_LIB
- Force-releases the organizer
- Loads backup outfit `lush01_bd01` via `StartLoad(false)` (sync, from archive)
- Verifies all 179 mesh bone pointers match servo-directory resolution
- **Status: PASSES (0 mismatches)**

This test is a guardrail, not a bug detector. The visual bug has a different cause.

### Existing tests — STILL PASSING

- `AssetLoadingTest.MainCharacterFileMergerConfiguresOutfitAndVisemeByDefault` — passes
- `MergeScopeParityTest.Synthetic*` (5 tests) — all pass

### Twist pollable inventory is not a backup-merge regression

`AssetLoadingTest.BackupOutfitLoadsTwistPollablesWithExpectedBindings` now audits the
twist pollables both **before** and **after** the backup outfit merge.

Current result:

```
main-before-backup:  fore=2 upper=0 neck=0
backup-after-merge:  fore=2 upper=0 neck=0
```

The important conclusion is that backup merging does **not** drop `CharUpperTwist` or
`CharNeckTwist` objects. The standalone main character asset already only carries the two
`CharForeTwist` pollables, and the backup outfit preserves that exact state.

That means:

1. The new test is useful as a guardrail for **forearm twist binding parity**.
2. Missing upper/neck twist objects are a **normal authored asset state** here, not a
   merge bug by themselves.
3. Any native/viewer path that assumes “some twist pollables exist, therefore all twist
   work is covered” is incorrect for this asset layout.

### Native viewer fallback bug fixed, but not the main forearm root cause

While following up on the twist inventory test, we found a real bug in
`native/src/char/CharTwistSolver.cpp`:

- `SolveAll()` previously returned as soon as it found **any** twist pollable.
- On this asset, that meant the two authored `CharForeTwist` objects suppressed the
  fallback upper-arm and neck twist math entirely.

Fix:

- `SolveAll()` now treats authored twist pollables on a **per-twist-type** basis:
  - authored `CharForeTwist` objects are polled when present
  - fallback upper-arm twist still runs if no authored `CharUpperTwist` exists
  - fallback neck twist still runs if no authored `CharNeckTwist` exists

Regression:

- Added `MiloViewerPosePipeline.TwistSolverFallsBackPerTwistType`
- Also linked `src/char/CharTwistSolver.cpp` into `milo-tests`, since the new test exposed
  that the test binary was not previously linking the twist solver TU at all

However, this fix did **not** move the main crouch parity gap. Re-running the direct-vs-driver
pose dump on `crouching_great_01` still reports the same large mismatch:

```
max_pos=28.0206 (bone_L-hand.mesh world)
max_mat=1.77445 (bone_L-upperArm.mesh world)
```

Per-bone breakdown from the fresh dumps is more informative:

```
bone_L-upperArm.mesh:  local_mat=1.4804  world_mat=1.7745
bone_L-hand.mesh:      local_mat=1.0677  world_mat=1.1247
bone_L-foreTwist1.mesh local_mat=0.0000  world_mat=1.6022
bone_L-foreTwist2.mesh local_mat=0.0000  world_mat=1.3059
bone_R-upperArm.mesh:  local_mat=1.2636  world_mat=1.4888
bone_R-hand.mesh:      local_mat=0.5084  world_mat=1.3164
bone_R-foreTwist1.mesh local_mat=0.0000  world_mat=1.3575
bone_R-foreTwist2.mesh local_mat=0.0000  world_mat=1.2010
```

This is the key narrowing:

- `foreTwist1/2` local transforms already match between direct pose and driver pose
- the divergence starts upstream on `upperArm` and `hand`
- the large `foreTwist` world-space drift is a downstream consequence of bad parent/hand
  poses, not evidence that the forearm twist solver itself is the primary bug

So the remaining root-cause work should stay focused on the **driver/runtime pose path**
(`CharDriver`, `CharClipDriver`, clip evaluation, or related procedural hand logic), not on
backup merge scope or forearm twist binding.

## Phase 1: Targeted Verification Fix — NEEDS REVISION

The original Phase 1 (re-resolve mesh bone pointers after merge) is **not the right fix**
because bone pointers are already correct. The Phase 1 fix needs to target the actual
deformation cause.

### New investigation targets

1. **Is `CharServoBone::ReallocateInternal` called for backup outfits?** Add logging to
   confirm the servo re-resolves its bone list after the backup outfit merge.

2. **Are animation clips loaded?** Check whether `CharClipGroup` and `CharClip` objects
   exist in the character after the backup outfit merge. If clips don't load, the servo
   has no animation data.

3. **Is `CharServoBone::PoseMeshes` being called at runtime?** The POSEMESHES diagnostic
   log already exists. Check whether it fires for backup characters during gameplay, and
   whether the bone count and transforms look reasonable.

4. **Do twist solvers poll for backup characters?** Check `CharTwistSolver` instances in
   the backup character directory — are they present? Are they in the poll list?

5. **Bone offset matrix audit**: For a known-broken mesh+bone pair, compare the
   `BoneOffsetAt()` matrix between the backup outfit and a working outfit.

### Suggested next test

```cpp
// After outfit merge:
// 1. Verify bone.servo has non-zero mMeshes (ReallocateInternal ran)
// 2. Verify CharClip objects exist (animation data loaded)
// 3. Verify twist solvers exist (CharTwistSolver in character dir)
```

## Phase 2: Root-Cause Audit — REVISED

The audit should now focus on animation and transform propagation, not merge scope:

1. **Servo bone count**: Does `bone.servo->mMeshes.size()` match the skeleton bone count
   after the backup outfit merge?
2. **Clip binding**: Are CharClip objects bound to the correct bones? Does `CharClipGroup`
   have the right clip list?
3. **Twist solver binding**: Are `CharTwistSolver` objects present and polling?
4. **Frame-by-frame transform audit**: At runtime, capture bone world transforms for a few
   frames and verify they change (not static identity matrices).

## Phase 3: Global Fix — UNCHANGED

The merge infrastructure is working correctly for bone pointer identity. Phase 3 work
(Xbox-equivalent merge behavior) may still be needed for other reasons (parent-dir fallback
removal, subdir flattening), but it is not the cause of the deformation bug.

## Validation Matrix

### Unit / integration

- `AssetLoadingTest.BackupOutfitBonePointersMatchServoDirectory` — PASSES
- `AssetLoadingTest.MainCharacterFileMergerConfiguresOutfitAndVisemeByDefault` — PASSES
- `MergeScopeParityTest.Synthetic*` — ALL PASS

### Runtime (next steps)

- headless run with `lush01_bd01` backup outfit
- verify `POSEMESHES` log shows bone.servo with correct bone count
- verify `BONE DIAG` log shows non-identity transforms varying per frame
- check CharTwistSolver presence and activity

### Visual

- backup dancer forearms still stretched — **BUG NOT YET FIXED**
- need to identify whether the issue is animation (no clips), procedural (no twist
  solvers), or transform propagation (stale dirty flags)

## Risks

- the real cause may be in animation clip loading, which is a deeper system
- twist solver issues would require understanding the character poll graph
- the Null GPU backend crash limits test automation in sandboxed CI

## Files Changed This Session

| File | Change |
|------|--------|
| `src/system/char/FileMerger.h` | Added `ForceReleaseOrganizer()` (native-only) |
| `src/system/char/FileMerger.cpp` | Implemented `ForceReleaseOrganizer()` + brace fix |
| `native/tests/test_asset_loading.cpp` | Added organizer release before sync StartLoad |

## Useful Commands

```bash
cd native/build
./milo-tests --gtest_filter=AssetLoadingTest.MainCharacterFileMergerConfiguresOutfitAndVisemeByDefault
./milo-tests --gtest_filter=AssetLoadingTest.BackupOutfitBonePointersMatchServoDirectory
./milo-tests --gtest_filter=MergeScopeParityTest.Synthetic*
```
