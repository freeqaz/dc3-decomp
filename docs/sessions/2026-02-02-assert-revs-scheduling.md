# ASSERT_REVS Instruction Scheduling Investigation

**Date**: 2026-02-02
**Status**: Confirmed unfixable (compiler scheduling quirk)

## Problem

The second `MILO_FAIL` in `ASSERT_REVS` has a 2-instruction scheduling mismatch. Target puts `addi r7, r27, 0x4` (gRevs[2] address) before stack-relative `addi r4`/`addi r6`, our build puts it after. Affects ~146 Load functions at 98.6% match.

Target order (instructions 55-57):
```
addi r7, r27, 0x4   ; &gRevs[2] -> r7 (arg5, T4)
addi r4, r1, 0x54   ; stack addr -> r4 (arg2, T1)
addi r6, r1, 0x64   ; stack addr -> r6 (arg4, T3)
```

Our order:
```
addi r4, r1, 0x54   ; stack addr -> r4
addi r6, r1, 0x64   ; stack addr -> r6
addi r7, r27, 0x4   ; &gRevs[2] -> r7
```

All three instructions are independent `addi` operations setting up arguments for `MakeString<const char*, Symbol, int, unsigned short>`. The ordering difference is a pure compiler scheduling heuristic.

## Experiments (all failed)

| # | Approach | Match% | Notes |
|---|----------|--------|-------|
| 1 | `#pragma optimize("t", on)` | 74.2% | Destructive - changed too much codegen |
| 2 | `#pragma optimize("y", off)` | 98.6% | No effect |
| 3 | `#pragma optimize("g", off)` | 27.9% | Destructive - disabled global opts |
| 4 | Per-file `/Ot` in objects.json | 74.2% | Same as #1, `/Ot` overrides `/Os` from `/O1` |
| 5 | `*(const unsigned short *)((const char *)gRevs + 4)` | 98.6% | Compiler generates identical code |
| 6 | `const unsigned short *_gAltRevP = &gRevs[2]` | 98.6% | Local pointer optimized away |
| 7 | `const unsigned short * volatile _gAltRevP` | 96.0% | Changed to 1 replace but added extra stw/lwz |
| 8 | `const unsigned short &_altMax = gRevs[2]` before `if` | 98.6% | Reference optimized away |
| 9 | `__forceinline` identity wrapper function | 98.6% | Completely inlined to same code |
| 10 | Non-static array (remove `static`) | 92.9% | Added stack init overhead |
| 11 | Condition `d.altRev > gRevs[2]` instead of `> rev2` | 98.6% | Constant-folded, no effect |
| 12 | `__pragma(auto_inline(off))` inside function | N/A | Build error: must be outside function |

## Analysis

The three `addi` instructions are all setting up `const T&` reference arguments for MakeString. They write to different registers (r4, r6, r7) and have no data dependencies between them. The compiler is free to schedule them in any order.

The target compiler (original Xbox 360 MSVC) schedules the global-relative address (r7 = gRevs base + 4) before the stack-relative addresses (r4, r6 = r1 + offset). Our compiler (same version, same flags) consistently does stack-relative first. Every source-level trick to influence this ordering either:
- Gets optimized away to identical codegen
- Adds unwanted extra instructions
- Destructively changes the entire function's codegen

## Conclusion

This is an unfixable compiler instruction scheduling difference. The ASSERT_REVS macro should remain as-is. The 2 replace instructions cap affected Load functions at 98.6% match.
