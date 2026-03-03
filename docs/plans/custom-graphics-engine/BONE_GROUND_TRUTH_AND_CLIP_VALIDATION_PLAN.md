# Bone Ground Truth and Clip Validation Plan

**Status**: Active  
**Created**: 2026-03-02  
**Last Updated**: 2026-03-02  
**Owner**: Native graphics/animation debugging track

## Goal
Establish hard ground truth for skeleton behavior before trusting clip data, then validate that `skeleton_clips` produces correct poses.  
We only move to the next stage after the current gate passes.

## Why This Plan Exists
- Current poses appear wrong at startup and during clip playback.
- We need to distinguish data parsing issues from transform/pose application issues.
- We want both visual evidence (screenshots) and codified regression checks (tests).

## Scope
- In scope:
  - Bone hierarchy and manual bone manipulation correctness
  - Rest/start pose validity checks
  - Clip pose correctness at selected beats
  - Parsing boundary checks if clip pose validation fails
- Out of scope:
  - Shader-only visual polish changes
  - IK/mocap tuning unless it blocks baseline pose validation

## Ground Rules
1. No skipping gates.
2. Every gate needs both:
   - Numeric assertions in tests
   - Screenshot evidence from deterministic camera/setup
3. If a gate fails, stop and debug that layer before proceeding.

## Audit Snapshot (2026-03-02)
- We have enough information to continue Gates 0/1 and to isolate parsing boundaries if Gate 3 fails.
- We do **not** yet have enough to trust screenshot-based clip validation because the screenshot path and `--direct-pose` behavior are currently inconsistent.
- Current highest-confidence root causes are missing runtime decomp bodies in the gesture skeleton path, plus harness/pathing and pose-application-path parity.
- There are still `AT_LIMIT` decomp entries in related units, but current evidence suggests many are tooling-demotion artifacts rather than the primary runtime blocker.
- Additional confirmed gap: several `gesture/SkeletonClip` methods are currently missing bodies in source and native falls back to weak stubs; this must be fixed as explicit decomp work.
- New critical finding: the runtime skeleton update chain also has missing decomp bodies (`GestureMgr::PostUpdate`, `SkeletonUpdate::Update`, `Skeleton::Poll`, etc.) and currently executes weak-stub/no-op paths.

## Critical Runtime Gap Inventory (2026-03-02)
These are source/body gaps verified with objdiff (`Stub`, all inserts). They are now highest-priority implementation work.

| Priority | Unit | Function(s) | Objdiff |
|---|---|---|---|
| P0 | `gesture/GestureMgr` | `GestureMgr::PostUpdate(const SkeletonUpdateData*)` | Stub (201 inserts) |
| P0 | `gesture/SkeletonUpdate` | `SkeletonUpdate::Update()` | Stub (78 inserts) |
| P0 | `gesture/Skeleton` | `Skeleton::Poll(int, const SkeletonFrame&)` | Stub (207 inserts) |
| P0 | `gesture/SkeletonClip` | `PollRecording`, `SwapMoveRecord`, `FillMoveRatings`, `LoadFrame`, `RecordedFrameAt`, `CurRecordedFrame`, `SongStartSeconds`, `PrevSkeleton`, `RecordedFrame::MakeSkeletonFrame` | Stub (all inserts) |
| P1 | `gesture/SkeletonUpdate` | `UpdateCallbacks`, `UpdateFakeArmPos`, `InsertFakeArmPos`, `SkeletonUpdateCallbackSlowdownCB`, `OnToggle*`/`OnCycle*` data funcs | Stub (all inserts) |
| P1 | `gesture/SkeletonViz` | `Poll`, `Visualize`, `SetCamera`, `DrawPoint3D`, `DrawJoints` | Stub (all inserts) |
| P1 | `gesture/Skeleton` | `IdentityCallback`, `EnrollIdentity`, `Displacements` | Stub (all inserts) |
| P1 | `gesture/SkeletonRecoverer` | `WaitingToRecover`, `GetTrackingIDWithRecovery`, `Poll` | Stub (all inserts) |
| P1 | `gesture/SkeletonQualityFilter` | `Update(const Skeleton&, bool)` | Stub (95 inserts) |
| P2 | `gesture/GestureMgr` | `GetSecondarySkeletonIndex`, `DrawSkeletonKinectData` | Stub (all inserts) |

## Implementation Audit (2026-03-02, Pass 1)
Implemented source bodies for the P0 chain plus major `SkeletonClip`/gesture runtime helpers and re-ran objdiff.

Key result:
- these are no longer weak-stub/all-insert functions in our build; they now resolve to strong source implementations.

Objdiff snapshot after implementation:
- `GestureMgr::PostUpdate` -> 68.3%
- `SkeletonUpdate::Update` -> 49.2%
- `Skeleton::Poll` -> 69.2%
- `RecordedFrame::MakeSkeletonFrame` -> 89.4%
- `SkeletonClip::LoadFrame` -> 61.7%
- `SkeletonClip::RecordedFrameAt` -> 86.7%
- `SkeletonClip::CurRecordedFrame` -> 91.2%
- `SkeletonClip::SwapMoveRecord` -> 99.8%
- `SkeletonClip::PollRecording` -> 99.1%
- `SkeletonClip::FillMoveRatings` -> 79.8%
- `SkeletonClip::SongStartSeconds` -> 99.0%
- `SkeletonClip::PrevSkeleton` -> 81.0%
- `SkeletonUpdate::UpdateCallbacks` -> 81.0%
- `SkeletonUpdate::UpdateFakeArmPos` -> 39.0%
- `SkeletonUpdate::InsertFakeArmPos` -> 48.6%
- `SkeletonRecoverer::WaitingToRecover` -> 99.4%
- `SkeletonRecoverer::GetTrackingIDWithRecovery` -> 94.6%
- `SkeletonRecoverer::Poll` -> 63.0%
- `SkeletonQualityFilter::Update` -> 73.6%
- `DrawGestureMgr` -> 91.6%
- `SkeletonViz::Poll` -> 29.1%
- `SkeletonViz::DrawPoint3D` -> 37.2%
- `SkeletonViz::Visualize` -> 66.1%
- `Skeleton::EnrollIdentity` -> 83.8%
- `Skeleton::IdentityCallback` -> 62.9%
- `Skeleton::Displacements` -> 60.6%

Pass 2 focused updates (function-by-function):
- `SkeletonUpdate::UpdateFakeArmPos` -> 98.5% (from 39.0%)
- `SkeletonUpdate::InsertFakeArmPos` -> 79.3% (from 48.6%)
- `SkeletonClip::PollRecording` -> 99.1% (from 96.9%) after `TheHamDirector` re-check split, compare-order alignment, and `MILO_LOG` argument-type fix.
- `SkeletonRecoverer::GetTrackingIDWithRecovery` -> 94.6% (from 90.2%) after return-carrier/control-flow reshaping and distance/threshold ordering fixes; one search/null-path control-flow cluster remains.

## Current Gap Assessment (2026-03-03, Live Objdiff In-Tree, Latest Pass)
- Important: decomp DB status rows for several gesture symbols currently show `COMPLETE (reset: stub with no source implementation)`, which does not reflect local in-tree matching. Gap tracking below uses **live objdiff** against current source.

Near complete (>=95%):
- `SkeletonClip::SwapMoveRecord` -> 99.8%
- `SkeletonClip::PollRecording` -> 99.1%
- `SkeletonClip::SongStartSeconds` -> 99.0%
- `SkeletonUpdate::UpdateFakeArmPos` -> 98.5%
- `SkeletonViz::Visualize` -> 97.4%

Close but still meaningful gaps (80-95%):
- `SkeletonRecoverer::GetTrackingIDWithRecovery` -> 94.6%
- `SkeletonClip::Poll` -> 92.5%
- `SkeletonRecoverer::Poll` -> 90.8%
- `SkeletonClip::CurRecordedFrame` -> 91.2%
- `RecordedFrame::MakeSkeletonFrame` -> 89.4%
- `SkeletonClip::RecordedFrameAt` -> 86.7%
- `Skeleton::Poll` -> 81.0%
- `SkeletonClip::PrevSkeleton` -> 81.0%
- `SkeletonUpdate::UpdateCallbacks` -> 81.0%

Remaining major gaps (<80%):
- `SkeletonUpdate::InsertFakeArmPos` -> 58.2%
- `SkeletonClip::FillMoveRatings` -> 79.8%
- `GestureMgr::PostUpdate` -> 68.3%
- `SkeletonClip::LoadFrame` -> 61.7%
- `SkeletonUpdate::Update` -> 49.2%
- `SkeletonViz::DrawPoint3D` -> 37.2%
- `SkeletonViz::SetCamera` -> 14.2%

Root-cause buckets now driving remaining work:
- MakeString template mismatches still appear in key runtime paths (`Skeleton::Poll`, `SkeletonViz::Visualize`, `RecordedFrame::MakeSkeletonFrame`) and are likely fixable.
- Control-flow shape mismatches remain clustered in `SkeletonRecoverer::GetTrackingIDWithRecovery`, `SkeletonUpdate::InsertFakeArmPos`, `SkeletonClip::{LoadFrame,CurRecordedFrame,RecordedFrameAt}`.
- Prologue/local-variable layout mismatches dominate `SkeletonViz` and some `SkeletonRecoverer`/`SkeletonUpdate` functions; these are declaration-order/code-shape sensitive.

Readiness summary:
- We are close on the `SkeletonClip` recording/playback micro-path pieces and `SkeletonViz::Visualize`, but not yet close on the broader runtime update/camera chain.
- Biggest remaining functional risk to ground-truth bone validation is still in low-match transform/update hot-paths (`SkeletonUpdate::Update`, `GestureMgr::PostUpdate`, `SkeletonViz::SetCamera`, `SkeletonViz::DrawPoint3D`, `SkeletonUpdate::InsertFakeArmPos`).

## Weak Stub Linkage Clarification
- Weak stubs are fallback symbols compiled in `native/src/engine_stubs_generated.cpp`.
- They are **not** links to the original/disassembled game object code.
- When strong source implementations are missing, the linker can resolve calls to weak no-op stubs (`return 0;`).
- Confirmed weak-stubbed hot-path symbols include:
  - `GestureMgr::PostUpdate`
  - `SkeletonUpdate::Update`
  - `Skeleton::Poll`
  - multiple `SkeletonClip` runtime helpers (`PollRecording`, `LoadFrame`, `RecordedFrameAt`, etc.)

## Hot-Path Impact (Why This Matters)
- These paths were previously weak-stubbed, which explained no-op runtime behavior.
- We now have strong source implementations for:
  - `SkeletonUpdate::Update()`
  - `GestureMgr::PostUpdate(...)`
  - `SkeletonViz::Visualize(...)`
  - `SkeletonClip` frame conversion helpers (`RecordedFrameAt`, `RecordedFrame::MakeSkeletonFrame`, etc.)
- Current risk has shifted from "missing body" to "low-match logic/codegen divergence" in specific functions, especially `SkeletonUpdate::Update`, `Skeleton::Poll`, and `SkeletonViz::*`.
- Result: parsing may still be correct while runtime pose state remains wrong if these low-match runtime transforms diverge from target behavior.

## Gate 0: Harness and Tooling Readiness
**Intent**: Confirm we can trust test execution and data access paths.

- [x] Confirm `milo-tests` executes from `native/build`
- [x] Confirm baseline suites pass:
  - `MathType*`
  - `CharBonesSamplesTest*`
- [x] Confirm asset loading path for:
  - `char/main/retarget_skeletons/skeleton_clips.milo`
- [x] Record canonical commands in this doc
- [ ] Resolve known Gate 0 blockers:
  - `FlowDesync.TrackObjectBytes` currently aborts with
    `ASSERT_REVS FAILED: ObjectDir '' version 28 > 2 (or alt 0 > 0)`
  - pose server script path is wrong when run from `native/build`
    (`native/scripts/pose_server.py` resolved relative to `native/build`)

**Pass criteria**
- Test harness is stable and reproducible.
- Asset path is loadable through archive-backed runtime path.

## Gate 1: Bone Topology and Manual Manipulation Ground Truth
**Intent**: Prove basic skeleton mechanics are correct without relying on clip parsing.

- [ ] Add/extend tests for direct local transform edits on canonical bones:
  - Right arm lift (`bone_R-upperArm`, chain to hand)
  - Forward bend (pelvis/spine chain)
  - Knee bend (thigh/knee/ankle chain)
- [ ] Validate numeric kinematics:
  - Expected world-space direction deltas
  - Parent-child continuity constraints
  - No NaN/degenerate transforms
  - Unrelated chains stay stable
- [ ] Capture deterministic screenshots for each manual pose
- [ ] Save screenshots as golden references with fixed camera/beat/frame settings

**Pass criteria**
- All manual pose tests pass numeric assertions.
- Screenshot goldens show expected motion.

## Gate 2: Rest/Start Pose Validity
**Intent**: Confirm we start from a valid pose before clip playback.

- [ ] Add a test that loads character + skeleton resources and inspects initial pose
- [ ] Assert rest pose sanity:
  - Pelvis/spine/head ordering is plausible
  - Left/right symmetry bounds where expected
  - Limb orientation and distances are plausible
  - No detached chain outliers
- [ ] Capture rest pose screenshot golden

**Pass criteria**
- Start pose is valid numerically and visually.

## Gate 3: `skeleton_clips` Pose Validation
**Intent**: Validate clip output once base skeleton behavior is trusted.

- [ ] Select deterministic clip subset and beat sample points
- [ ] Add tests to apply clips and capture canonical bone transforms
- [ ] Assert pose plausibility per beat:
  - No impossible inversions for monitored chains
  - Smooth continuity between adjacent beats
  - Translation/rotation within expected bounds
- [ ] Capture screenshot goldens for selected beats

**Pass criteria**
- Numeric and screenshot checks pass for selected clips/beats.

## Gate 4: Parsing Boundary Isolation (Only If Gate 3 Fails)
**Intent**: Prove/disprove parsing mismatch as root cause.

- [ ] Instrument/validate `CharClip::Load` boundaries:
  - before/after `mFull.Load`
  - before/after `mOne.Load`
- [ ] Add focused tests for stream byte consumption per compression mode
- [ ] Verify `TypeSize`, offsets, and cached padding behavior against expectations
- [ ] Identify first deterministic divergence point and capture failing fixture

**Pass criteria**
- First parsing boundary mismatch is isolated by a deterministic failing test,
  or parsing is ruled out conclusively.

## Root Cause Hypotheses (Current)
1. Remaining low-match runtime decomp in active skeleton path (highest confidence):
   - `GestureMgr::PostUpdate`, `SkeletonUpdate::Update`, `Skeleton::Poll`, and `SkeletonViz` helpers are now implemented but still not fully matched, so behavior may still diverge before clip-level validation.
2. Harness-level false negatives:
   - `FlowDesync.TrackObjectBytes` currently aborts early, so it is not yet reliable as a Gate 0 pass/fail signal.
3. Runtime pathing issue:
   - `NativeSkeletonProvider::Start` launches `native/scripts/pose_server.py` via a relative path, which fails from `native/build`.
4. Pose path parity risk:
   - `milo_viewer` labels `--direct-pose` as `PoseMeshes`, but screenshot path currently uses `PoseMeshes` only when `!directPose`; video path always uses `PoseMeshes`.
5. Parsing still plausible but not yet first-order:
   - `CharClip::Load` boundary instrumentation shows consistent `mFull.Load`/`mOne.Load` progression for `skeleton_clips.milo`, so immediate desync is not yet proven there.
6. Gesture decomp-body gaps beyond `SkeletonClip` (confirmed):
   - `SkeletonUpdate`, `Skeleton`, `SkeletonViz`, `SkeletonRecoverer`, `SkeletonQualityFilter`, and `GestureMgr` all contain missing runtime bodies; several are weak-stubbed in native.

## SkeletonClip Implementation Gap (Must Fix)
These are not optional cleanups; implement and validate each with objdiff:

- [x] `RecordedFrame::MakeSkeletonFrame(SkeletonFrame&, int)`
- [x] `SkeletonClip::LoadFrame(BinStream&, RecordedFrame&, int)`
- [x] `SkeletonClip::PollRecording(const SkeletonFrame&)`
- [x] `SkeletonClip::SwapMoveRecord()`
- [x] `SkeletonClip::FillMoveRatings()`
- [x] `SkeletonClip::CurRecordedFrame(int&, int&) const`
- [x] `SkeletonClip::SongStartSeconds() const`
- [x] `SkeletonClip::PrevSkeleton(const Skeleton&, int, ArchiveSkeleton&, int&) const`
- [x] `SkeletonClip::RecordedFrameAt(...)`
- [x] Re-ran objdiff on all above and recorded current match levels
- [ ] Re-run objdiff on `SkeletonClip::Poll()` and resolve remaining `AT_LIMIT` causes

**Acceptance criteria for this gap**
- No remaining `Stub` verdicts for `gesture/SkeletonClip` runtime methods listed above.
- `SkeletonClip::Poll()` either reaches `COMPLETE` or is documented as true `AT_LIMIT` with root-cause evidence.
- Native behavior no longer depends on weak stub fallbacks for `SkeletonClip` core runtime methods.

## Additional Gesture Gaps (Secondary, After SkeletonClip Stubs)
These are not currently weak-stubbed in the same way, but are still low-match and likely fixable:

- [ ] `SkeletonUpdateThread(void*)` (`SkeletonUpdate.cpp`) — currently low match and likely affected by control flow/prologue/macro expansion
- [ ] `Skeleton::Init()` (`Skeleton.cpp`) — currently low match with structural/prologue differences
- [ ] `SkeletonFrame::Create(const _NUI_SKELETON_FRAME&, int)` (`Skeleton.cpp`) — currently very low match
- [ ] `SkeletonViz::SkeletonViz()` (`SkeletonViz.cpp`) — currently low match

Notes:
- `SkeletonDir`, `NavigationSkeletonDir`, and `DancerSkeleton` are broadly in good shape (no current `AT_LIMIT` functions).
- `operator new/delete` entries in status are noisy; direct objdiff shows high match with relocation-only differences.

## Implementation Order (Updated)
1. P0 runtime chain first:
   - `GestureMgr::PostUpdate`
   - `SkeletonUpdate::Update`
   - `Skeleton::Poll`
2. Complete `SkeletonClip` runtime helpers:
   - all functions listed in **SkeletonClip Implementation Gap (Must Fix)**
3. Fill dependent skeleton runtime helpers:
   - `SkeletonUpdate::UpdateCallbacks` / `UpdateFakeArmPos` / `InsertFakeArmPos`
   - `SkeletonQualityFilter::Update`
   - `SkeletonRecoverer` methods
4. Restore visualization/debug path:
   - `SkeletonViz::Visualize` / `Poll` / `SetCamera` / `DrawPoint3D` / `DrawJoints`
5. Re-run objdiff on all P0/P1 functions and record verdict transitions in this document before re-entering Gate 1/2 screenshot validation.

## External Reference Audit (`../milo-executable-library/MiloEditor`)
- `MiloEditor/MiloLib` is the useful external reference for this effort (not Boomy copy).
- Key findings:
  - `MiloEditor/MiloLib/Assets/Char/CharClip.cs` has active `CharClip` + nested `CharBonesSamples` read/write paths, including:
    - versioned header/data split (`LoadHeader`, `LoadData`)
    - per-type byte sizing + per-sample 16-byte alignment
    - explicit `full` + `one` sample handling in `CharClip::Read`
  - `MiloEditor/MiloLib/Assets/DirectoryMeta.cs` includes active `CharClip` object factories/read/write dispatch.
  - `MiloEditor/MiloLib/Assets/Ham/SkeletonClip.cs` is still marked `TODO: finish this`, so it is not fully authoritative.
  - `Boomy/BoomyDeps/MiloLib/Assets/Ham/SkeletonClip.cs` is effectively the same partial implementation and also marked TODO.
  - The DC3 map at `../milo-executable-library/dc3/9.16.12 (Final Debug) - No Checksum/ham_xbox_r.map` confirms target symbols/addresses for missing functions (e.g., `SkeletonClip::PollRecording`, `SkeletonUpdate::Update`, `SkeletonViz::SetCamera`, `Skeleton::Poll`).
- Practical use:
  - treat `MiloEditor` as a structural/field-order hint source;
  - do not treat it as runtime-truth over DC3 binary behavior.

## Decomp Status Audit (Char/Bones/Clip)
- Query results show many related functions as `COMPLETE`, including key paths:
  - `CharBonesSamples::LoadHeader`, `LoadData`, `Load`
  - `CharClip::Load`, `Save`, `ScaleAdd`, `RotateBy`, `RotateTo`, `PoseMeshes`
  - `CharClipDriver::Evaluate`, `PreEvaluate`, `ExecuteEvent` (mixed statuses but present and largely matched)
- There are still related `AT_LIMIT` symbols (examples: `CharBonesSamples::EvaluateChannel`, `CharDriver::Poll`, `CharClipDriver::Evaluate`), but attempt history indicates several were previously marked 100% and later demoted by tooling/base-size resets.
- Current conclusion:
  - unresolved decomp entries may still matter in edge cases,
  - but they are not yet the strongest evidence for the current clip validation failures.

## Gate 5: Regression Lock-In
**Intent**: Prevent recurrence.

- [ ] Keep fast numeric pose tests in normal test runs
- [ ] Keep screenshot suite as deterministic regression checks (can be gated by env/label)
- [ ] Document update workflow for goldens
- [ ] Add troubleshooting notes for common failure signatures

**Pass criteria**
- Future bone desync regressions are caught with actionable failures.

## Initial Implementation Targets
- Test harness files:
  - `native/tests/test_charbones_serialization.cpp`
  - `native/tests/test_milo_diagnostic.cpp`
  - `native/tests/test_flow_desync.cpp`
  - new test file(s) for pose ground truth and clip validation
- Runtime paths of interest:
  - `src/system/char/CharBonesSamples.cpp`
  - `src/system/char/CharBonesMeshes.cpp`
  - `src/system/char/CharClip.cpp`
  - `src/system/math/Vec.h`
  - `native/src/viewer/milo_viewer.cpp` (for deterministic screenshot plumbing, if reused)

## Canonical Commands
```bash
cd native/build
./milo-tests --gtest_list_tests
./milo-tests --gtest_filter='MathType*:*CharBonesSamplesTest*'
./milo-tests --gtest_filter='FlowDesync.FlowAnimateFieldTrace'
./milo-tests --gtest_filter='DirLoaderTest.*:MiloDiagnostic.*'
# known blocker (currently aborts; keep diagnostic-only until fixed):
./milo-tests --gtest_filter='FlowDesync.TrackObjectBytes'
```

## Work Log
### 2026-03-02
- [x] Created plan doc with gate-based execution.
- [x] Ran `MathType*:*CharBonesSamplesTest*` (16/16 passed).
- [x] Ran `FlowDesync.FlowAnimateFieldTrace` (pass).
- [x] Verified `skeleton_clips.milo` load path through archive-backed runtime; observed `CharClip::Load` boundary logs.
- [x] Confirmed `FlowDesync.TrackObjectBytes` currently aborts on `ASSERT_REVS`.
- [x] Audited external reference library at `../milo-executable-library/MiloEditor/MiloLib`.
- [x] Audited `gesture/SkeletonClip` decomp status and objdiff:
  - confirmed stubbed/missing bodies for key methods (`PollRecording`, `SwapMoveRecord`, `FillMoveRatings`, `LoadFrame`, `CurRecordedFrame`, `SongStartSeconds`, `PrevSkeleton`, `RecordedFrame::MakeSkeletonFrame`)
  - confirmed `SkeletonClip::Poll()` is currently `AT_LIMIT` (~79.8%) and likely fixable
- [x] Expanded audit across adjacent gesture units:
  - additional likely-fixable `AT_LIMIT` candidates: `SkeletonUpdateThread`, `Skeleton::Init`, `SkeletonFrame::Create`, `SkeletonViz::SkeletonViz`
  - `SkeletonDir` / `NavigationSkeletonDir` / `DancerSkeleton` currently have no `AT_LIMIT` symbols
- [x] Confirmed missing/stubbed runtime chain functions outside `SkeletonClip`:
  - `GestureMgr::PostUpdate` (stub)
  - `SkeletonUpdate::Update` and related helpers/datafuncs (stub)
  - `Skeleton::Poll`/`Displacements`/identity helpers (stub)
  - `SkeletonViz::Visualize` + draw/camera helpers (stub)
  - `SkeletonRecoverer` + `SkeletonQualityFilter::Update` (stub)
- [x] Confirmed weak-stub fallback coverage in `native/src/engine_stubs_generated.cpp` for critical hot-path functions and documented that these are no-op fallbacks (not links to original game objects).
- [x] Verified DC3 map symbol presence/addresses for missing functions in `../milo-executable-library/.../ham_xbox_r.map`.
- [x] Re-audited `MiloEditor` and `Boomy` `SkeletonClip` parsers; both remain incomplete/TODO and are field-layout hints only.
- [x] Implemented first-pass source bodies for missing runtime chain functions in `gesture/*`:
  - `GestureMgr::PostUpdate`
  - `SkeletonUpdate::Update` + helper/datafunc set
  - `Skeleton::Poll` + identity/displacement helpers
  - `SkeletonClip` runtime helper set (`LoadFrame`, `RecordedFrameAt`, `CurRecordedFrame`, `PollRecording`, `SwapMoveRecord`, `FillMoveRatings`, `SongStartSeconds`, `PrevSkeleton`, `RecordedFrame::MakeSkeletonFrame`)
  - `SkeletonRecoverer` + `SkeletonQualityFilter::Update`
  - `DrawGestureMgr`
  - `SkeletonViz` visualization path (`Poll`, `DrawPoint3D`, `Visualize`, `SetCamera`, `DrawJoints`)
- [x] Built updated gesture objects successfully via ninja.
- [x] Re-ran objdiff on implemented symbols; documented non-stub status + current match percentages.
- [x] Continued one-function-at-a-time refinement on `SkeletonUpdate` helpers:
  - `UpdateFakeArmPos`: raised to 98.5% with correct fsel clamp structure and intermediate state write.
  - `InsertFakeArmPos`: raised to 79.3% via branch/control-flow rewrite to raw stick/trigger field path (`mSticks`/`mTriggers`), still needs ordering/codegen alignment.
- [x] Continued one-function-at-a-time refinement on `SkeletonClip::PollRecording`; raised to 99.1% and removed all insert/delete/replace mismatches (remaining `diff_arg` primarily MakeString template + relocation noise).
- [x] Continued one-function-at-a-time refinement on `SkeletonRecoverer::GetTrackingIDWithRecovery`; raised to 94.6% with remaining divergence concentrated in one search/null-path control-flow cluster.
- [ ] Fix Gate 0 blockers (TrackObjectBytes assert path + pose server script path).
- [ ] Improve low-match implemented functions (especially `SkeletonUpdate::Update*`, `Skeleton::Displacements`, `SkeletonViz::*`) toward `COMPLETE`.
- [ ] Implement remaining P2 gesture debug helpers (`GestureMgr::GetSecondarySkeletonIndex`, `DrawSkeletonKinectData`) and validate if still required by target/runtime path.

## Gate Progress Snapshot
- Gate 0: `in_progress`
- Gate 1: `pending`
- Gate 2: `pending`
- Gate 3: `pending`
- Gate 4: `pending`
- Gate 5: `pending`
