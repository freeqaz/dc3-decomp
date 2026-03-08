# Session: objdiff false 100% match fix

**Date**: 2026-03-07

## Problem

`measure_progress.sh --regressions` reported 4 false regressions: newly implemented functions (TriggerSelf, Cleanup, LoadRev, MergeSinks) appeared to drop from 100% to their actual match percentages. Investigation revealed these functions had **no source code** at the baseline commit — they were never implemented.

## Root Cause

In `objdiff-cli report generate`, when a function exists in the target `.obj` but has no source implementation, the diff engine returns `match_percent: None`. The report generation code (`report.rs:637`) defaulted this to `100.0` for units marked `complete: true`:

```rust
let match_percent = symbol_diff.match_percent.unwrap_or_else(|| {
    if object.complete.unwrap_or(false) { 100.0 } else { 0.0 }
});
```

The comment said "Support cases where we don't have a target object" — but it also fired when the **source** was missing the symbol, falsely inflating 4,037 functions across 465 complete units.

## Fix

Changed the fallback in `objdiff-cli/src/cmd/report.rs` to only default to `100.0` when there's literally no base/source `.obj` file (the intended case). When the source `.obj` exists but doesn't contain the symbol, it correctly reports `0.0`:

```rust
let match_percent = match symbol_diff.match_percent {
    Some(pct) => pct,
    None if base.is_none() && object.complete.unwrap_or(false) => 100.0,
    None => 0.0,
};
```

Same fix applied to section-level match percent.

## Impact

- **4,037 stub functions** (723 KB) across 465 complete units were falsely counted as 100% match
- Overall progress corrected from ~54% → ~48% (previously inflated by ~6%)
- `measure_progress.sh` no longer reports false regressions when new functions are added
- Report cache (`.cache` files) must be cleared after updating objdiff

## Remaining Work Analysis

After the fix, a clear picture of actionable work in complete units emerges. Run:

```bash
python3 scripts/analysis/remaining_work.py                  # markdown table
python3 scripts/analysis/remaining_work.py --format json    # JSON output
python3 scripts/analysis/remaining_work.py --min-bytes 1000 # filter small units
```

### Summary: 1,709 functions (455 KB) across 192 units

Top categories by remaining bytes (excluding SDK/platform):

| Category | Funcs Left | KB Left | Key Units |
|---|---|---|---|
| system/rndobj | 243 | 81 KB | Shader (15KB), Text, PostProc_NG, Lit_NG |
| system/hamobj | 245 | 61 KB | MoveDir (13KB), RhythmDetector, FreestyleMoveRecorder |
| lazer/meta_ham | 195 | 52 KB | MetagameRank, SaveLoadManager, SkeletonChooser |
| system/world | 126 | 48 KB | Spotlight+SpotlightDrawer (24KB), CameraShot |
| system/synth | 94 | 29 KB | filterdesign, VorbisReader, EQEffect |
| system/utl | 121 | 29 KB | MemTracker, BinkIntegration, Cache_Xbox |
| system/os | 124 | 25 KB | HolmesClient, Debug, NetworkSocket |
| system/char | 122 | 24 KB | Character (47 funcs / 8.5KB) |

### Near-complete units (best bang-for-buck)

| Unit | Done | Left | Bytes |
|------|------|------|-------|
| CameraShot | 151/172 | 14 | 7,228 |
| Joypad | 45/47 | 1 | 2,644 |
| FlowSound | 41/46 | 2 | 2,188 |
| FlowSwitchCase | 32/34 | 1 | 1,188 |
| StandardStream | 91/106 | 12 | 3,232 |
| Cheats | 67/74 | 5 | 2,464 |
| InlineHelp | 75/84 | 6 | 1,156 |
| Dir | 150/176 | 18 | 1,652 |

### Breakdown of previously-inflated stubs

The 4,037 false 100% functions were concentrated in:
- **link_glue** — 1,161 stubs (ALTERNATENAME redirects, not real functions)
- **Xbox SDK wrappers** — PlatformMgr_Xbox, BinkMovieImpl, synth_xbox/*
- **DX9 rendering backend** — rnddx9/Tex, rnddx9/Mesh, rnddx9/Rnd
- **Game engine** — 465 complete units had a combined 2,390 genuine stubs after filtering SDK/platform code

## Files Changed

- `../objdiff/objdiff-cli/src/cmd/report.rs` — Fix false 100% for target-only symbols
- `scripts/analysis/remaining_work.py` — New script for remaining work analysis
