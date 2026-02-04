# Session: seed_chase Feedback Integration (2026-02-04)

## Context

Incorporated feedback from `docs/sessions/2026-02-04-tools-feedback-seed_chase.md` into documentation and m2c improvement tracking.

The `seed_chase` function in `src/system/oggvorbis/psy.c` went from 89.8% to 100% match by changing `alloca` to `_alloca`. The session identified several pain points and improvement ideas.

## Feedback Items Addressed

### 1. alloca vs _alloca Pattern (NEW)

**Pain point:** The `_RtlCheckStack12` prologue pattern wasn't documented; the fix required recognizing that the intrinsic `_alloca` generates different code than the CRT wrapper `alloca`.

**Solution:** Added new pattern documentation.

**Files changed:**
- `docs/decomp/patterns/fixable-declarations.md` - Added full pattern section with symptoms, detection, fix examples
- `docs/decomp/patterns/INDEX.md` - Added to main table (+10-15%, 100% success rate) and Quick Decision Tree

**Pattern summary:**
- `alloca()` = CRT library wrapper
- `_alloca()` = compiler intrinsic with `_RtlCheckStack12` stack probe
- Symptom: prologue shows `bl _RtlCheckStack12` in target but not in build

### 2. REGISTER_SWAP Guidance Enhancement

**Pain point:** Detection didn't suggest concrete source-level edits or which locals to reorder.

**Solution:** Added actionable heuristics to the REGISTER_SWAP section.

**Files changed:**
- `docs/decomp/patterns/unfixable-compiler.md` - Added "Variable Reordering Heuristics" subsection

**New guidance:**
1. Group variables by usage pattern (used together → declare together)
2. Order by first use
3. Separate integer and float declarations
4. Try reverse declaration order
5. Note: detection doesn't identify *which* variables to reorder (acknowledged limitation)

### 3. Prologue Hints Section (NEW)

**Files changed:**
- `docs/decomp/patterns/INDEX.md` - Added "Prologue Hints" subsection after Quick Decision Tree

**Content:**
- `_RtlCheckStack12` → try `_alloca`
- Stack frame size differs → check local variables
- Different save/restore pattern → may be unfixable

### 4. m2c Improvements Tracking

**Pain point:** m2c output contained unset-register artifacts; hard to trust for tight loops.

**Solution:** Created improvement tracking file in m2c repo (goal: fix m2c, not just document limitations).

**Files created:**
- `../m2c/docs/IMPROVEMENTS.md`

**Tracked improvements:**
- Low-noise output mode (filter `phi_*`, `temp_*`, unset `sp*` artifacts)
- Improved loop analysis for tight loops
- Better unset-register handling with provenance tracking
- Register swap detection hints

## Files Changed Summary

### DC3 Decomp
| File | Change |
|------|--------|
| `docs/decomp/patterns/fixable-declarations.md` | +58 lines (alloca vs _alloca pattern) |
| `docs/decomp/patterns/unfixable-compiler.md` | +45 lines (reordering heuristics) |
| `docs/decomp/patterns/INDEX.md` | +12 lines (table entry, decision tree, prologue hints) |

### m2c
| File | Change |
|------|--------|
| `docs/IMPROVEMENTS.md` | Created (improvement tracking) |

## Outcome

- `alloca` vs `_alloca` is now a documented fixable pattern
- REGISTER_SWAP has actionable guidance instead of just "try reordering"
- Prologue differences have a quick reference section
- m2c improvements are tracked for future implementation rather than just documenting limitations
