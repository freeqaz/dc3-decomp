# FFT Inverse Columnwise - At Limit Analysis

**Date**: 2026-02-03
**Function**: `?fft_matrix_inverse_columnwise@@YAHPAMJ0@Z`
**Unit**: `default/system/synth_xbox/FFT`
**Match**: 84.0% (at_limit)
**Attempts**: 12+ total (10 previous Opus sessions, 2 this session)

## Root Cause

16-byte stack frame difference: target 0x190 (400 bytes) vs base 0x1a0 (416 bytes).

The MSVC register allocator creates a VMX spill slot at `r1+0xb0` for v7 (the perm_swap permutation mask). The allocator reuses v7 for an intermediate value (`vmr v7, v0`), requiring a spill/reload pair (`stvx128 v7, r0, r5` / `lvx128 v7, r0, r5`). The target avoids this by keeping v7 dedicated to the mask, reloading it each iteration via `lvx128 v63, r0, r5` then `vor128 v7, v63, v63`.

This single register allocation difference cascades into:
- 38 register swap mismatches (v0<->v13, v11<->v12, f26<->f29, f27<->f28, etc.)
- 6 instruction scheduling differences in the inner VMX loop
- 1 commutative operand order difference (index 93: `add r27,r29` vs `add r29,r27`)
- 5 extra instructions in base (1180 vs 1160 bytes)

## Stack Frame Analysis

Key experiment with `static const` perm masks revealed the frame size relationship:

| Stack perm masks | Frame size | Match |
|-----------------|-----------|-------|
| 0 (all static const) | 0x180 | 64.7% |
| 2 stack + 1 static | - | 71.4% |
| 3 (current) | 0x1a0 | 84.0% |
| Target | 0x190 | 100% |

Target frame (0x190) is exactly 16 bytes between 0-mask (0x180) and 3-mask (0x1a0), corresponding to the extra spill slot.

## Changes Tested (All No Effect or Worse)

Source-level changes that produced **identical 84% output**:
- Variable declaration reordering
- Statement reordering (sin before cos, sin2a before sin2, etc.)
- Moving perm mask loads inside/outside the inner loop
- Removing explicit sin2 copies (compiler generates them anyway)
- Using `__lvx(&perm_swap, 0)` inside loop instead of pre-loaded variable
- Removing `{` scope around deinterleave block
- Pre-loading all 3 perm masks before inner loop
- Commutative operand swap (`stride8 + col_ptr` vs `col_ptr + stride8`)

Source-level changes that **made things worse**:
- `static const` perm masks: 64.7% (adds lis/addi for global addresses, extra GPR saves)
- Mixed static/stack masks: 71.4%
- `__declspec(align(16)) float sv[4]` instead of XMVECTORF32: 80.5% (frame grew to 0x1b0)
- Reordering variable declarations + eliminating src2 pointer: 80.5%

## Key Insight

MSVC's optimizer for this function is 100% deterministic and completely insensitive to source-level statement ordering. The register allocator and instruction scheduler make the same choices regardless of how variables are declared, ordered, or computed. Only type/size changes affect output, and all alternatives tested produce worse results.

## Permanent Changes

- Resolved merge conflict in `src/xdk/LIBCMT/vectorintrinsics.h` (removed duplicate `__vnmsubfp` declaration)
- FFT.cpp restored to baseline (all experimental edits reverted)
