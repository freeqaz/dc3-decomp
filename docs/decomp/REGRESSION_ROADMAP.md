# Regression Recovery Roadmap

> **Last updated**: 2026-03-08
> **Baseline**: `b14f7df76` (og-dc3-decomp) → Current working tree
> **Net gain**: +5.64% overall (+364.3 KB, +2237 functions)

## Current State

| Metric | Value |
|--------|-------|
| Overall fuzzy match | 48.11% |
| vs HEAD~1 regressions | 2 functions (200 B) |
| vs og baseline regressions | 90 functions (40.7 KB) |
| vs og baseline improvements | +364.3 KB, +2237 functions |
| **Net balance** | +323.6 KB gained |

The 90 og regressions (40.7 KB) are the cost of gaining +364.3 KB — a **9:1 improvement-to-regression ratio**.

## Regression Categories

### Category A: Accepted Tradeoffs (2 functions, 200 B)

These regress vs HEAD~1 but are the cost of keeping og-baseline improvements:

| Function | Unit | Size | Root Cause |
|----------|------|------|------------|
| AnimPtr::~AnimPtr | CharLipSync | 64 B | Removed dtor to recover 5 HamDirector og functions |
| Synth::SendToPlayHandlers | Synth | 136 B | Kept HEAD Synth.cpp for 3 og Synth recoveries |

### Category B: Pre-existing Header Drift (~88 functions, ~40.5 KB)

These existed before our changes. Caused by header improvements made over many commits:

| Sub-category | Est. Functions | Est. Bytes | Example |
|-------------|---------------|-----------|---------|
| Struct layout changes | ~15 | ~8 KB | Vector3→float fields, padding shifts |
| ObjPtr/ObjList template changes | ~10 | ~4 KB | Iterator semantics, operator+ |
| Virtual function additions | ~8 | ~6 KB | DrawBefore/DrawAfter stubs |
| Cascading inlining from added bodies | ~25 | ~14 KB | Bodies in same TU shift other functions |
| Volatile register swaps | ~8 | ~4 KB | Unfixable compiler artifact |
| Copy ctor removal cascades | ~10 | ~3 KB | Fixed 7 bodyless copy ctors; some residual |

## Key Findings

### Object.h Iterator Revert is Toxic

Reverting `ObjPtrVec::iterator::operator+` from copy-returning to mutating fixes CharClipGroup::HasClip (128 B) but causes **19 cascading regressions** across TUs that use `ObjPtrVec::end()`. The change affects `end()` semantics which cascades into `find() != end()` patterns project-wide. **Do not attempt.**

### HEAD Commit Triage Results

Per-file analysis of HEAD commit (4f09bcfcf) changes:
- **12 files reverted** to HEAD~1: UIListDir, UIList, RhythmBattle, MemMgr, AmbientOcclusion, CharClip, MatAnim, LockedContentPanel, AppLabel, MQSongSortMgr
- **3 files kept** at HEAD: HamDirector.h+CharLipSync.cpp (-5 og regs), UIScreen.cpp (-3 og regs), Synth.cpp (-3 og regs)
- **Remaining files** (Flow, UI.cpp, Cheats, etc.) were already at HEAD and don't affect HEAD~1 regressions

## Recovery Strategies

### Strategy 1: Header Audit (HIGH ROI)

**Target**: Bodyless copy constructor declarations, iterator changes, operator changes

**Evidence**: 7 bodyless copy ctors fixed this session recovered ~1 KB.

**Action items**:

- [x] Scan headers for bodyless copy ctors (7 found, all fixed)
- [ ] Check for remaining bodyless copy ctors in less-touched headers
- [ ] Estimated recovery: 100-300 B (2-5 functions)

### Strategy 2: Per-TU Regression Tracing (MEDIUM ROI)

**Target**: The top 10 regressions by byte impact

| Function | Unit | Regression | Size |
|----------|------|-----------|------|
| SongMetadata ctor | SongMetadata | -51.3% | 1060 B |
| TexRenderer Load | TexRenderer | -29.2% | 880 B |
| Part SyncProperty | Part | -18.4% | 7324 B |
| HamSkeletonConverter Enter | HamSkeletonConverter | -18.0% | 592 B |
| RndText ctor | Text | -14.1% | 684 B |
| PartyModeMgr SubMode | PartyModeMgr | -10.9% | 516 B |

- [ ] Trace SongMetadata ctor regression (no .cpp/.h diff — pure transitive)
- [ ] Trace TexRenderer Load regression (DrawBefore/DrawAfter virtuals added)
- [ ] Trace Part SyncProperty regression (huge function, likely struct layout)
- [ ] Estimated recovery: 500-2000 B if root causes are fixable

### Strategy 3: Function Body Recovery (LOW ROI for regressions, HIGH for overall)

- [ ] For functions at 95%+, run permuter to try small variations
- [ ] Estimated recovery: minimal for regressions; high for overall progress

## Decision Framework

```
Is the regression from a header change?
├─ YES: Can the header change be guarded with #ifdef HX_NATIVE?
│  ├─ YES → Guard it. Check for cascading effects.
│  └─ NO (needed for PPC too) → Accept as drift.
├─ NO: Is it from an added function body?
│  ├─ YES: Does the body recover more than it regresses?
│  │  ├─ YES → Accept as net-positive tradeoff.
│  │  └─ NO → Remove the body or investigate alternatives.
│  └─ NO → Investigate deeper (build system, transitive includes).
```

## Metrics History

| Date | HEAD~1 Regressions | og Regressions | Fuzzy Match |
|------|-------------------|---------------|-------------|
| 2026-03-08 (start) | 56 (35.5 KB) | 70 (36.2 KB) | 48.09% |
| 2026-03-08 (triage) | 2 (200 B) | 90 (40.7 KB) | 48.11% |

## Related Docs

- [Session doc](../sessions/2026-03-08-regression-fix-session.md)
- [Copy ctor pattern](patterns/fixable-copy-ctor.md)
- [Unfixable compiler patterns](patterns/unfixable-compiler.md)
- [OG baseline recovery session](../sessions/2026-03-07-og-baseline-regression-recovery.md)
