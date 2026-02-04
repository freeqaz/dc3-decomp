# MILO_NOTIFY_ONCE Sweep - Phase 3: Database Fix & Validation

**Date**: 2026-02-04
**Status**: Complete
**Previous**: `2025-02-04-MILO_NOTIFY_ONCE-sweep-phase2.md`

## Summary

Fixed a critical categorization bug in the merged symbol detection that was causing false positives for AddToStrings candidates.

## Problem Fixed

The `MERGED_SYMBOL_CATEGORIES` mapping in `detect_patterns.py` incorrectly categorized `merged_824D1870` as `addtostrings` when it actually contains 901 MakeString variants.

| Symbol | Before (Wrong) | After (Correct) |
|--------|----------------|-----------------|
| `merged_824D1870` | `addtostrings` | `makestring` |
| `merged_82372AA0` | (unmapped) | `addtostrings` |

## Changes Made

**File**: `docs/meta-strategy/scripts/detect_patterns.py`

```python
# Before (incorrect)
MERGED_SYMBOL_CATEGORIES = {
    "merged_AddToStrings": "addtostrings",
    "merged_824D1870": "addtostrings",  # WRONG - 901 MakeString variants!
    ...
}

# After (correct)
MERGED_SYMBOL_CATEGORIES = {
    "merged_AddToStrings": "addtostrings",
    "merged_82372AA0": "addtostrings",  # Actual AddToStrings address
    "merged_824D1870": "makestring",    # 901 MakeString variants
    ...
}
```

## Validation Results

### Category Distribution (After Fix)

| Category | Call Count | Notes |
|----------|------------|-------|
| `unknown` | 630 | Various merged symbols not yet categorized |
| `makestring` | 251 | MakeString variants (unfixable, expected) |
| `setobjconcrete` | 22 | ObjPtr merged calls |
| `addtostrings` | 7 | True MILO_NOTIFY_ONCE candidates |

### AddToStrings Functions (True Positives)

| Function | Match % | Status |
|----------|---------|--------|
| `MiniLeaderboardDisplay::DrawShowing` | 99.99% | Essentially complete |
| `Normalize(Quat)` | 99.87% | Essentially complete |
| `HamIKSkeleton::SetBone` | 98.97% | Near complete |
| `EndCmd` | 96.52% | Near complete |
| `CharBonesSamples::FracToSample` | 96.27% | Near complete |
| `ClipPlayer::PlayNormal` | 94.53% | Good progress |
| `RndTexBlender::DrawBlendList` | 81.10% | Needs work |

## Impact

- **Before fix**: ~40 functions incorrectly flagged as AddToStrings candidates
- **After fix**: 7 true AddToStrings candidates identified
- Most candidates are already at 95%+ match - the MILO_NOTIFY_ONCE sweep was effective

## Workable Function Landscape

Current state of functions that could potentially reach 100%:

| Range | Count |
|-------|-------|
| 99%+ | 329 |
| 95-99% | 187 |
| 90-95% | 102 |
| 80-90% | 274 |

## Next Steps

Potential work streams:

1. **Quick wins sweep** - Target 99%+ functions for trivial fixes
2. **Remaining AddToStrings** - `DrawBlendList` (81%) and `PlayNormal` (94.5%) could benefit from MILO_NOTIFY_ONCE patterns
3. **Unit-focused completions** - Pick compilation units and complete all functions
4. **SetObjConcrete patterns** - 22 functions with ObjPtr merged calls

## Commands Reference

```bash
# Re-run pattern detection
./docs/meta-strategy/scripts/detect_patterns.py --limit 2000 -v

# Query AddToStrings candidates
sqlite3 decomp.db "SELECT symbol, current_percent FROM functions WHERE has_addtostrings = 1 ORDER BY current_percent"

# Category distribution
sqlite3 decomp.db "SELECT category, COUNT(*) FROM merged_symbols GROUP BY category ORDER BY COUNT(*) DESC"

# Workable functions by range
sqlite3 decomp.db "
SELECT
    CASE
        WHEN current_percent >= 99 THEN '99%+'
        WHEN current_percent >= 95 THEN '95-99%'
        WHEN current_percent >= 90 THEN '90-95%'
        ELSE '80-90%'
    END as range,
    COUNT(*) as count
FROM functions
WHERE excluded = 0 AND current_percent >= 80 AND current_percent < 100
  AND (reachable_100 = 1 OR reachable_100 IS NULL)
GROUP BY range ORDER BY range DESC"
```
