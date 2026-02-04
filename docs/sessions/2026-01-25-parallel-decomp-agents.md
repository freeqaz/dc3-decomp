# Session: Parallel Decomp Agents

**Date:** 2026-01-25
**Focus:** Running 8 parallel Sonnet agents on <30% match functions

---

## Summary

Launched 8 parallel Sonnet agents to work on functions with <30% match. All agents completed successfully with significant improvements, achieving a combined +400% improvement across all targets.

## Agent Results

### Excellent Results (90%+)

| Function | Before | After | Change | Key Fix |
|----------|--------|-------|--------|---------|
| CheatsInit | 25.45% | **96.0%** | +70.55% | Fully implemented init logic, DataRegisterFunc calls |
| RndLight::Load | 28.15% | **95.0%** | +66.85% | Complete version-branched Load, 16 revisions |
| HolmesXboxPath | 26.81% | **91.6%** | +64.79% | Path conversion logic, segment length validation |

### Good Results (60-90%)

| Function | Before | After | Change | Key Fix |
|----------|--------|-------|--------|---------|
| RndEnviron::Save | 21.87% | **80.5%** | +58.63% | Added 21 missing member field writes |
| ClipDistMap::FindNodes | 24.97% | **79.3%** | +54.33% | Full algorithm from stub with "// finish later" |
| SongSortByLocation::NewShortcutNode | 25.76% | **63.5%** | +37.74% | Implemented from stub returning 0 |

### Moderate Results (40-60%)

| Function | Before | After | Change | Key Fix |
|----------|--------|-------|--------|---------|
| CharMirror::Poll | 22.72% | **49.3%** | +26.58% | Restructured to 3 pointer-based loops |
| UIFontImporter::OnGetGennedBitmapPath | 27.47% | **48.4%** | +20.93% | Implemented font/mat/texture path retrieval |

## Statistics

- **Total improvement:** +400.4% across 8 functions
- **Average improvement:** +50.05% per function
- **Functions now 90%+:** 3 (CheatsInit, RndLight::Load, HolmesXboxPath)
- **Functions now 60%+:** 6
- **Project overall:** 31.01% matched (up from ~30.9%)

## Files Modified

### By Agents
- `src/system/rndobj/Lit.cpp` - RndLight::Load implementation
- `src/system/rndobj/Env.cpp` - RndEnviron::Save implementation
- `src/system/os/HolmesUtl.cpp` - HolmesXboxPath implementation
- `src/system/utl/Cheats.cpp` - CheatsInit implementation
- `src/system/char/ClipDistMap.cpp` - FindNodes implementation
- `src/system/char/CharMirror.cpp` - Poll restructure
- `src/system/char/CharBones.h` - Added friend declaration for CharMirror
- `src/system/ui/UIFontImporter.cpp` - OnGetGennedBitmapPath implementation
- `src/lazer/meta_ham/SongSortByLocation.cpp` - NewShortcutNode implementation

### Post-Agent Cleanup
- `src/system/char/ClipDistMap.cpp` - Fixed floor() ambiguity with std::floor
- `src/system/rndobj/Env.cpp` - Fixed IsLightInList const cast

## Blocking Patterns Encountered

### Linker-Merged Functions (Unfixable)
- `merged_Read3FloatStruct`, `merged_Read4FloatStruct` in RndLight::Load
- `merged_StringCtor` in HolmesXboxPath
- Various merged helpers in other functions

### Register Allocation (Hard to Fix)
- CharMirror::Poll: r9/r11, r23/r29, r24/r29 swaps
- UIFontImporter: r30/r31 swaps
- Most functions had some level of register allocation differences

### Control Flow (Sometimes Fixable)
- Branch condition inversions
- Loop structure differences
- if/else ordering

## Lessons Learned

1. **<30% functions often have substantial missing logic** - Many were stubs with comments like "// finish later" or just returning 0
2. **Load/Save functions benefit from RB3 reference** - Version-branched logic patterns are similar
3. **Init functions need DataRegisterFunc calls** - Often missing callback registrations
4. **Parallel agents effective** - 8 agents completed in ~15 minutes total

## Commands Used

```bash
# Find <30% functions to target
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 0 --max-percent 30 --min-size 50 \
  --sort-by match_percent --sort-order desc --limit 50

# Verify function after changes
./bin/objdiff-cli diff -p . "FunctionName" --verdict -f markdown --build
```

## Next Steps

1. Continue finding <30% functions with substantial missing logic
2. Target more Load/Save functions which have predictable patterns
3. Look for Init functions missing registrations
4. Consider targeting 30-60% functions that might be partially implemented
