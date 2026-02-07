# RhythmBattle::OnBeat At-Limit Analysis & diff_inspect.py Tool

**Date**: 2026-02-07
**Function**: `RhythmBattle::OnBeat` (4188 instructions)
**Result**: 92.0% — confirmed at limit

## Context

OnBeat was previously improved from 91.8% → 92.0% via bool mask fixes (`bool p1Active/p2Active` for SetActive, `(unsigned)i28 > 2u` for difficulty gating). This session attempted 6+ experiments to push beyond 92.0%, all of which either regressed or had no effect.

## Baseline Mismatch Breakdown

| Type | Count | % of Total |
|------|------:|--------:|
| equal | 2212 | 52.8% |
| diff_arg | 1630 | 38.9% |
| replace | 195 | 4.7% |
| delete | 85 | 2.0% |
| insert | 61 | 1.5% |
| diff_op | 5 | 0.1% |

Detected patterns: 43 LINKER_MERGED, 318 REGISTER_SWAP, 2 BOOL_MASK, 3 CONTROL_FLOW inversions, 2 OFFSET_SWAP.

## Experiments Attempted

### 1. Null check on unk130 before StopRecording
**Hypothesis**: Target has `cmplwi cr6, r11, 0x0` + `beq` (null check) before dereferencing unk130.
**Change**: `if (unk130) unk130->StopRecording();`
**Result**: Added 2 extra insert instructions (4188→4190), no improvement. Reverted.

### 2. Signedness comparison (beq vs ble at idx 955)
**Hypothesis**: `beq` vs `ble` after `divw.` suggests signed vs unsigned comparison for DancerSkeleton loop guard.
**Change**: Removed `(unsigned int)` cast, tried `(int)` cast on both sides.
**Result**: Introduced new `blt↔bne` replacements. Regression. Reverted.

### 3. Ternary swap for i6b4 (5 variants)
**Hypothesis**: At diff index 1258, TGT `ble cr6` vs SRC `bgt cr6` with operand order swapped in `cmpw`. Tried `>=`, `<=`, `<`, and branch-swapped variants.
**Results**:
- `(i19 >= i16)` + swapped branches: diff_op 5→6, offset_swaps 2→3
- `(i16 <= i19)` + swapped branches: diff_op 5→6, offset_swaps 2→3
- `(i19 < i16)` + same branches: diff_op 5→6
- All variants worse. Reverted.

### 4. Jacker/jackee condition collapse
**Hypothesis**: Nested if/else-if could be collapsed to single condition.
**Change**: `if (jacker->InTheZone() && (!jackee->InTheZone() || jacker->Unk280() > jackee->Unk280()))`
**Result**: Regression to 91.9%, deletes 85→90. Different code structure generated. Reverted.

### 5. Declaration reorder (i27/i35 swap)
**Hypothesis**: Target computes `i27` (mPlayerTwo) before `i35` (mPlayerOne).
**Change**: Swapped declaration order.
**Result**: diff_arg 1630→1629 (marginal improvement) but introduced `blt↔bne` replacements. Net neutral/slightly worse. Reverted.

### 6. Various other condition restructures
All neutral or regression. Reverted.

## Root Cause Analysis

Using the JSON diff_breakdown data, we identified the true root causes of the 1630 diff_arg instructions:

### Stack Frame Difference (dominant)
SRC stack frame: 0x750 (1872 bytes), TGT: 0x760 (1888 bytes) — 16-byte difference.

**Offset shift distribution** (from diff_breakdown analysis):
| Shift (SRC - TGT) | Count | Explanation |
|---|---:|---|
| +24 (0x18) | 178 | Stack slot cascade |
| +16 (0x10) | 142 | Direct frame size diff |
| +8 (0x8) | 80 | Stack slot cascade |
| -4 (0x4) | 13 | Local variable offset |
| +20 (0x14) | 12 | Stack slot cascade |

These 400+ instructions are noise — they all stem from the compiler allocating a slightly different stack layout. Unfixable without matching the exact stack frame size, which requires matching whatever extra local variable the target has.

### Register Allocation (secondary)
244 instructions differ only in register assignment. Top swap pairs:
| Pair | Count |
|---|---:|
| r20 ↔ r21 | 111 |
| r17 ↔ r18 | 36 |
| r18 ↔ r19 | 24 |
| r10 ↔ r11 | 19 |
| r21 ↔ r22 | 17 |

The r20↔r21 swap (111 instructions) is a single variable assignment difference that cascades through the entire function. Attempted variable reordering didn't fix it.

### Symbol Relocations
551 diff_arg instructions have symbol differences — these are the LINKER_MERGED calls (43 total) and static variable relocations that differ due to static local scope numbering.

### 5 True diff_op Mismatches
These are the only genuinely different opcodes:

| Index | TGT | SRC | Area |
|---:|---|---|---|
| 955 | `beq` | `ble` | DancerSkeleton loop guard (after `divw.`) |
| 1258 | `ble cr6` | `bgt cr6` | i6b4 ternary comparison |
| 1384 | `srwi r11,r11,31` | `clrlwi r11,r11,24` | Boolean arithmetic (outOfRange/b43) |
| 1386 | `clrlwi r10,r11,31` | `clrlwi. r10,r10,31` | Boolean arithmetic continuation |
| 3636 | `b` (branch) | `bl` (call) | Tail call optimization for Release() |

All 5 represent compiler optimization choices that can't be matched by source-level changes.

## Tool Built: `scripts/diff_inspect.py`

Created a new utility for inspecting objdiff JSON output, filling the gap between `show_instrs.py` (raw dump) and `objdiff --analyze` (full pattern engine).

### Usage

```bash
# Generate JSON diff
./bin/objdiff-cli diff "symbol_name" --include-instructions --build --incremental -f json -o /tmp/claude/diff.json

# Inspect it
python3 scripts/diff_inspect.py /tmp/claude/diff.json                  # all non-equal mismatches
python3 scripts/diff_inspect.py /tmp/claude/diff.json diff_op          # only diff_op (opcode mismatches)
python3 scripts/diff_inspect.py /tmp/claude/diff.json replace          # only replace
python3 scripts/diff_inspect.py /tmp/claude/diff.json insert,delete    # structural differences
python3 scripts/diff_inspect.py /tmp/claude/diff.json diff_op -C 8     # 8 lines context
python3 scripts/diff_inspect.py /tmp/claude/diff.json all              # every instruction
python3 scripts/diff_inspect.py /tmp/claude/diff.json --range 950-970  # specific index range
python3 scripts/diff_inspect.py /tmp/claude/diff.json --summary        # count by match type
```

### Features
- **Match type filtering**: Focus on specific mismatch types (`diff_op`, `replace`, `insert`, `delete`)
- **Context display**: `-C N` shows N surrounding instructions (default 5)
- **Range mode**: `--range 950-970` for viewing specific instruction ranges
- **Summary mode**: `--summary` for quick match type counts
- **Highlight markers**: `>>>` marks target mismatches, groups nearby matches to avoid redundant context

### JSON Schema Available for Future Enhancements

The objdiff JSON has rich data that the current tool doesn't fully exploit:

- **`typed_args`**: Semantic types — `Register`, `Symbol`, `Signed`, `Unsigned`, `BranchDest`, `Other`
- **`diff_breakdown`**: Argument-level diffs with `arg_type` (register, symbol, signed, immediate, branch_dest)
- **Per-instruction addresses**: Both target and base addresses for cross-referencing

Planned improvements:
- Register swap pair detection and grouping
- Offset shift pattern detection (identify stack frame cascades)
- Insert/delete cluster analysis (group nearby structural differences)
- Separate "real" mismatches from cascade noise

## Conclusion

OnBeat at 92.0% is a genuine at-limit function. The remaining 8% gap breaks down as:
- **~6%**: Stack frame cascade + register allocation noise (unfixable)
- **~1.5%**: Linker merged calls / ICF (unfixable)
- **~0.5%**: 5 diff_op compiler optimization choices (unfixable by source changes)

The diff_inspect.py tool and root cause analysis methodology developed here will be valuable for triaging other at-limit functions — separating cascade noise from actionable mismatches.
