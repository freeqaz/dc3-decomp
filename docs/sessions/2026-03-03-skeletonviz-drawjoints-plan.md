# SkeletonViz::DrawJoints Session Update (2026-03-03)

## Current verified state

- Function: `?DrawJoints@SkeletonViz@@AAAXABVBaseSkeleton@@PAVVector3@@1_N@Z`
- Unit: `default/system/gesture/SkeletonViz`
- Source of truth match: **67.7% normalized** (`orchestrator_run_objdiff`)
- Recon status: **AT_LIMIT**
- Unicorn verdict: **EQUIVALENT** (behavior matches)
- Binary size: 1468 bytes

## Progress made this session chain

Large jump from earlier low matches (single digits / teens / 26%) to **67.7%** after implementing major missing behavior in `SkeletonViz::DrawJoints`.

Implemented/verified in source:

- Depth normalization path using `BoneLength` calls.
- Per-joint rendering flow with temporary mesh scaling and restore (`SetLocalScale`).
- Clipping debug text via `Rnd::DrawStringScreen` (`"clipped right/left/top/bottom"`).
- Axes drawing path using `UtilDrawAxes`.
- Loop/control-flow reshaping versus earlier placeholder implementation.

Reference locations:

- `src/system/gesture/SkeletonViz.cpp:325`
- `src/system/gesture/SkeletonViz.cpp:334`
- `src/system/gesture/SkeletonViz.cpp:395`
- `src/system/gesture/SkeletonViz.cpp:433`
- `src/system/gesture/SkeletonViz.cpp:455`

## Current mismatch profile (latest diagnostics)

From `run_objdiff` / `run_diff_inspect`:

- Dominant issue: **REGISTER_SWAP** (FPR-heavy, plus some GPR swaps).
- **PROLOGUE_MISMATCH** still present (target saves different register set).
- Small **CONTROL_FLOW** polarity deltas remain.
- Some **ADDRESS_RELOCATION_NOISE** is unfixable noise.
- `run_diff_inspect` shows a dominant stack offset shift around **-16** (shape mismatch in locals/stack layout).

Note: `run_diff_inspect` "match estimate" is heuristic and lower than normalized objdiff; continue using `run_objdiff` as canonical percentage.

## Attempt history snapshot

- Attempts DB now records a high-water mark at **67.7%** for this symbol.
- Prior 26% report is stale relative to current workspace state.

## Recommended next pass (if continuing)

1. Target stack/prologue shape first (locals ordering/count, especially float temps).
2. Then steer FPR allocation by reordering calculations and temporary lifetimes.
3. Re-check with incremental `run_objdiff` after each small edit.
4. If no movement after focused prologue/FPR passes, keep `AT_LIMIT` for triage and move on.

## Hand-off prompt for next agent/session

```text
Function: ?DrawJoints@SkeletonViz@@AAAXABVBaseSkeleton@@PAVVector3@@1_N@Z
Current: 67.7% normalized (AT_LIMIT, unicorn EQUIVALENT)
Do not regress implemented behavior: BoneLength depth path, DrawStringScreen clipping text,
SetLocalScale temp/restore, UtilDrawAxes block.
Main remaining issues: FPR register swaps, prologue/stack shape mismatch (~-16 delta), minor control-flow polarity.
Use run_objdiff as canonical match%; run_diff_inspect diagnose for root-cause guidance.
```
