# Parity Failures Ledger

Date: 2026-03-05

This file tracks parity-oracle failures and blocked parity coverage in `native/tests`.

## Test Taxonomy

- Parity-oracle tests:
  - Assert exact behavior parity with the Xbox/original engine model.
  - These are allowed to fail while parity work is in progress.
  - Failures are the primary backlog driver.
- Safety tests:
  - Assert crash-safety/invariants (no UAF, no dead iteration, finite values).
  - These should stay green.
- Fixture regression tests:
  - Use real game fixtures/assets.
  - Can be parity-oracle or safety, but should be explicitly labeled.

## Sweep Command

```bash
native/build/milo-tests '--gtest_filter=ObjectLifetimeTest.*:DirLoaderTest.*:MiloDiagnostic.*:BoneGroundTruth.*:ClipPoseFixture.*:MainMiloLoadTest.*'
```

Observed summary (latest targeted sweep):
- command:
  - `native/build/milo-tests '--gtest_filter=DirLoaderTest.*:MiloFiles/LoadMiloParam.*:MiloDiagnostic.*:ObjectLifetimeTest.*'`
- 14 run
- 14 passed
- 0 failed
- 0 skipped

## Active Parity-Oracles (Failing)

None currently failing in the targeted parity subset.

## Blocked Parity Coverage (Skipped)

None in the targeted subset after fixture-path and synthetic-fallback updates.

Historical note: these were previously skipped due fixture/env assumptions, and have now been unblocked.

## Parity-Relevant Green Tests (Keep as Guardrails)

- `ObjectLifetimeTest.ReplaceRefsRedirectsObjPtr`
- `ObjectLifetimeTest.MergeDirsRealFixturesLeaveOnlyLiveEntries`
- `BoneGroundTruth.*`
- `ClipPoseFixture.*`
- `MainMiloLoadTest.LoadMainCharacterMilo`

Note: these provide strong safety/regression coverage, but they do not replace the failing parity-oracle above.

## Staff-Level Triage Order

1. Keep parity-oracle tests strict and run them first on merge/lifetime changes.
2. Expand parity-oracle set for subdir merge ownership (`kMergeInlinedMoveSharedSubdirs`) and repeated merge cycles.
3. Standardize fixture manifests so future tests avoid ad-hoc path probing.

## Policy

- Do not weaken parity assertions to force green.
- If behavior is unknown, write oracle as explicit expected parity and mark as known-failing with this ledger entry.
- Every known parity failure must list: owner area, root-cause hypothesis, unblock condition.
