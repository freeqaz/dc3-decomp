# Test Value Audit (native/tests)

Date: 2026-03-05
Scope: all tests under `native/tests/*.cpp`.

## Audit rubric

- High-value:
  - Catches parity regressions or real runtime crashes.
  - Uses realistic fixtures or stresses tricky state transitions.
  - Fails for meaningful behavioral differences.
- Medium-value:
  - Verifies critical serialization/math contracts; mostly deterministic unit checks.
- Low-value:
  - Primarily asserts constants, trivial wrappers, or “does not crash” without strong oracle.
  - Weak signal-to-maintenance ratio when used alone.

## Low-value tests identified

These are not useless; they are low ROI compared to parity/fixture tests and should be consolidated or replaced over time.

### `test_input.cpp`
- `JoypadData.DefaultConstructorZeroed`
- `JoypadData.ButtonMaskBits`
- `JoypadEnum.XboxAliases`
- `JoypadEnum.NumButtons`
- `TriggerMapping.RemapRange`
- `TriggerMapping.ThresholdLogic`

Reason: mostly static constant/math checks; limited regression depth.

### `test_subsystems.cpp`
- `SubsystemTest.LocaleInitialized`
- `SubsystemTest.JoypadPoll`

Reason: effectively smoke tests with weak behavioral oracle.

### `test_audio.cpp`
- `AudioDevice.Singleton`
- `FxSendNative.EffectSlotInitDestroy`

Reason: constructor/identity sanity only; minimal behavioral risk coverage.

### `test_dirloader.cpp` (before edits)
- `StreamPositionTracking`, `LoadSimpleMilo`, `DeadMarkerInRealFile` were frequently skipped due fixture path assumptions.

Reason: skipped tests provide near-zero protection.

## High-value tests that should exist

1. FileMerger/merge parity-oracle tests on name collisions and ref-redirection.
2. Repeated overlay merge sequences on real fixtures (to catch latent UAF/ring corruption).
3. Subdir merge ownership tests (`kMergeInlinedMoveSharedSubdirs`) reproducing the PostMerge crash path.
4. DirLoader fixture tests using archive-backed files that exist in CI/runtime (avoid skip-by-default).
5. Deterministic animation parity tests between viewer output and in-process pose evaluation at selected beats.
6. Loader stress tests with back-to-back loads of same fixture and cross-file loads (detect leaked/stale refs).

## Implemented in this pass

### 1) Reduced skip-driven low-value in DirLoader tests
Updated `native/tests/test_dirloader.cpp` to use known archive-backed fixtures:
- `char/shared/main_resource.milo`
- `char/shared/viseme_resource.milo`
- `char/shared/skeleton_bones_resource.milo`

Also added:
- `DirLoaderTest.RepeatedLoadLeavesOnlyLiveEntries`

### 2) Added high-value merge/lifetime coverage
Updated `native/tests/test_object_lifetime.cpp`:
- Kept strict parity-oracle:
  - `MergeDirsNameCollisionLeavesOnlyLivePointers` (known failing north-star)
- Added fixture stress safety test:
  - `RepeatedFixtureMergesKeepIteratorSafe`

### 3) Made diagnostic test useful by default
Updated `native/tests/test_milo_diagnostic.cpp`:
- `MiloDiagnostic.WalkFile` now defaults to `char/shared/main_resource.milo` when `MILO_DIAG_FILE` is unset.

## Next upgrades (recommended)

1. Replace low-value input constant tests with a table-driven edge matrix over stick quadrants, thresholds, and hysteresis.
2. Add explicit parity fixtures for `MergeObjectsRecurse` behavior (expected object identity/ref counts across merge/delete).
3. Gate CI on parity-oracle subset (allowed-fail list tracked in `docs/testing/PARITY_FAILURES.md`).
4. Add fixture manifest/utility to remove ad-hoc path probing and avoid future skipped tests.
