# Mismatch Cluster Analysis — 2026-03-10

Systemic pattern discovery across AT_LIMIT functions, looking for header-level fixes.

## Tools Built

All analysis scripts in `/tmp/claude-1000/`:

- **`analyze_offset16.py`** — Categorizes ±16 offset functions by frame delta direction
- **`analyze_unit_offset16.py`** — Groups ±16 offset functions by compilation unit
- **`find_shared_inserts.py`** — Finds contiguous insert/delete instruction clusters shared across functions
- **`find_call_diffs.py`** — Systematic comparison of differing `bl` (function call) targets
- **`find_units.py`** — Lists units with most 80-99% AT_LIMIT functions

## Finding 1: ±16 Offset Clusters (218 functions)

**Conclusion: NOT a single fixable struct size issue.**

218 functions have ±16 byte offset mismatches. Breakdown:
- frame+16 (target larger): 58 functions
- frame-16 (target smaller): 81 functions
- no-frame-diff (local reorder): 79 functions

This is alignment boundary rounding — different functions hit 16-byte stack alignment boundaries differently based on their local variable mix. Not actionable as a single header fix.

Top affected units: rndobj/Utl (17 funcs), rndobj/Text (14 funcs) — but with diverse offset patterns within each unit.

## Finding 2: Insert/Delete Instruction Clusters

**Key insight**: For `insert` match type, the opcode is in `ins['base']` (WE generate extra code). For `delete`, it's in `ins['target']` (TARGET has code we don't).

### Top Insert Clusters (we generate, target doesn't)
- `mtlr / ld / ld / blr` — epilogue difference, 7 functions across 7 units
- Various prologue/epilogue patterns — compiler save/restore threshold differences

### Top Delete Clusters (target generates, we don't)
- `bl / lfs / lfs` — 12 occurrences in UIList (function call + float loads)
- `li / beq / li / clrlwi.` — 9 across 3 units (boolean materialization difference)
- Various control flow sequences — compiler-internal optimization differences

Most clusters are compiler behavior differences (boolean materialization, instruction fusion, prologue save thresholds) — not header-fixable.

## Finding 3: Call Target Divergence (Most Actionable)

### DataArray::Release() — Bidirectional (28 functions, 13 units)
- 11 functions: target calls Release(), we don't
- 17 functions: we call Release(), target doesn't
- Indicates scope/lifetime differences in DataArray handling, not a simple header fix
- Bidirectional nature rules out a single missing/extra call pattern

### ReadEndian — 8 extra calls across 5 units
- We generate `bl ReadEndian` calls that the target doesn't
- Inlining threshold difference — target inlines ReadEndian, we don't (or vice versa)

### ChallengeHeaderNode::GetItemCount / UIListWidget::ParentList — ICF noise
- `GetItemCount()` at offset 0x58 is ICF-merged with `ParentList()` — both compile to `lwz r3, 0x58(r3); blr`
- 4 functions show "missing call to GetItemCount" but it's actually ParentList inlining difference
- Target doesn't inline `ParentList()` (generates `bl`), our compiler does (generates inline `lwz`)

### __savegprlr_29 vs stw — Prologue save threshold (5 funcs, 5 units)
- Target uses `__savegprlr_29` helper for 3 callee-saved GPRs
- Our compiler uses manual `stw` pairs
- Different threshold for when to use the save helper (our compiler: 4+, target: 3+)

## Finding 4: ByteGrinder Parenthesization Bug (REAL FIX)

**5 functions (op15-op19) with operator precedence error.**

### The Bug
In `src/system/synth/ByteGrinder.cpp`, op15-op19 compute a repeat-and-shift rotation like:
```
((byte << 8) | byte) >> N
```
But the source has:
```cpp
return u8(((w2 & 0xFFFFFF00) | (w & 0xFF) >> 2) + operand);
//                                        ^^ binds tighter than |
```
C operator precedence: `>>` (precedence 5) binds tighter than `|` (precedence 10), so this computes:
```
(w2 & 0xFFFFFF00) | ((w & 0xFF) >> 2)  // shift THEN or = WRONG
```
Instead of:
```
((w2 & 0xFFFFFF00) | (w & 0xFF)) >> 2  // or THEN shift = CORRECT rotation
```

### The Fix
Add parentheses so `|` happens before `>>`:
```cpp
return u8((((w2 & 0xFFFFFF00) | (w & 0xFF)) >> 2) + operand);
```

### Results — All ByteGrinder Fixes Applied

**Root cause**: C operator precedence bug. `>>` (precedence 5) binds tighter than `|` (10) and `^` (9). Expressions like `A | B >> N` compute `A | (B >> N)` (shift then OR) instead of intended `(A | B) >> N` (OR then shift = rotation).

**Fix pattern**: Rewrite from obfuscated `(w2 | byte) >> N` to explicit rotation: `(byte >> N) | (byte << (8-N))`. Use `u8` types for XOR variants to minimize mask mismatches.

| Function | Before | After | Notes |
|----------|--------|-------|-------|
| op15 | 87.1% | **99.3%** | Rotation >>2, srwi/slwi swap |
| op16 | 87.1% | **100%** | Rotation >>3 |
| op17 | 87.1% | **100%** | Rotation >>4 |
| op18 | 87.1% | **100%** | Rotation >>5 |
| op19 | 87.1% | **100%** | Rotation >>6 |
| op20 | 86.8% | **100%** | Rotation >>7 (w3 intermediate) |
| op21 | 77.7% | 90.0% | Rotation >>1 ^ l (fused insn) |
| op22 | 77.7% | 90.0% | Rotation >>2 ^ l |
| op23 | 77.7% | 90.0% | Rotation >>3 ^ l |
| op25 | 77.7% | 90.0% | Rotation >>5 ^ l |
| op26 | 77.7% | 90.0% | Rotation >>6 ^ l |
| op27 | ~77% | 90.0% | Rotation >>7 ^ l (ICF-merged) |
| op28 | 81.8% | 89.7% | Rotation >>5, (rot+l)^l, param order fix |
| op29 | 85.0% | 89.7% | (rot+l)^l pattern |
| op30 | 87.6% | 93.8% | (rot^l)+l, precedence fix |
| op31 | 87.6% | 93.8% | (rot^l)+l, precedence fix |
| op36 | 85.3% | 90.3% | Complement rotation (~w<<6) |
| op37 | 85.3% | 90.3% | Complement rotation (~w<<3) |
| op38 | 85.3% | 90.3% | Complement rotation (~w<<2) |
| op39 | 85.3% | 90.3% | Complement rotation (~w<<5) |

**5 functions → 100%, 15 functions improved by 5-15%.**

### Unfixable Residual (all remaining mismatches)

All non-100% functions share the same 4-mismatch pattern:
1. `mr` vs `clrlwi` — u8 mask at assignment vs deferred
2. `srwi` vs `extrwi` — our compiler fuses shift+mask into one instruction
3. `slwi` vs `clrlslwi` — same fusion
4. `clrlwi` delete — our compiler elides final mask (already masked earlier)

This is a compiler peephole optimization difference — unfixable from source.

## Other ByteGrinder Patterns

| Functions | Pattern | Status |
|-----------|---------|--------|
| op0, op6 | Commutative XOR operand order | AT_LIMIT |
| op9 | Dead code elimination (target keeps dead XOR) | AT_LIMIT (96.3%) |
| op59 | XOR constant pairing swap (0xf↔0x19) | AT_LIMIT |
| op42-op44 | 99.3% with minor mismatches | AT_LIMIT |

## Units with Most 80-99% AT_LIMIT Functions

| Unit | Count |
|------|-------|
| HamDirector | 44 |
| ByteGrinder | 27 → most now 90%+ |
| HamNavList | 24 |
| Character | 23 |

## Summary

- **±16 offsets**: Alignment rounding, not a single fix (218 funcs)
- **Insert/delete clusters**: Mostly compiler behavior, not header-fixable
- **Call divergence**: DataArray::Release() bidirectional (complex), ReadEndian inlining, ParentList ICF noise
- **ByteGrinder**: 20 functions fixed (5 to 100%, 15 to 90%+). Root cause was C operator precedence bugs in byte rotation logic. Unfixable residual is compiler instruction fusion (extrwi/clrlslwi vs srwi/slwi).
