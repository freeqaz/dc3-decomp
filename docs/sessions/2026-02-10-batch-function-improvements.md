# Batch Function Improvements

**Date:** 2026-02-10

## Overview

Batch improvement session targeting 7 priority functions identified during prior regression fixing work. Used parallel subagents for independent translation units while working directly on UILabel functions in the main thread. All 8 functions (including a bonus GetFontMat discovered during work) were investigated and brought to their achievable limits.

## Results

| Function | Before | After | Change | Status |
|----------|--------|-------|--------|--------|
| UILabel::SetFontMat | 4.2% | 81.3% | +77.1% | at_limit |
| UILabel::GetFontMat | 6.3% | 59.9% | +53.6% | at_limit |
| UILabel::RefreshFontMat | 65.4% | 99.6% | +34.2% | at_limit |
| HiResScreen::Accumulate | 86.4% | 94.5% | +8.1% | at_limit |
| JoypadClient::Poll | 92.7% | 93.1% | +0.4% | at_limit |
| Rnd::PreInit | 96.4% | 96.4% | -- | at_limit |
| MoveMgr::GetRoutinePreferredVariant | 97.5% | 97.5% | -- | at_limit |
| RndPropAnim::ValueFromFrame | 99.9% | 99.9% | -- | at_limit |

## Function Details

### UILabel::SetFontMat (4.2% -> 81.3%)

The function was a stub with no logic. Implemented from scratch using m2c decompilation output (no RB3 reference exists -- DC3-specific function). The function takes a material name string and style index, looks up the font via `UILabelDir::FontObj(Symbol(c))`, handles three error notification cases (missing mat variation, null font resource, no default font), and assigns the font to `mStyles[i].mFont`.

Key implementation challenges:
- First attempt using `Style(i).mFont = font` produced 0% match because the target accesses `mStyles` directly rather than through the `Style()` accessor
- Changed to direct `mStyles[i].mFont` with bounds check: jumped to 81.3%
- Tried goto-based control flow matching m2c output: no improvement
- Tried duplicated if/else structure: dropped to 65.9%, reverted

Remaining gap caused by: LStyle being inlined by our compiler (target calls it as a function), virtual inheritance null checks optimized away, and LINKER_MERGED on SetObjConcrete.

### UILabel::GetFontMat (6.3% -> 59.9%)

Also a stub, discovered and implemented during SetFontMat work. Gets the LabelStyle, retrieves the UILabelDir, and calls `GetMatVariationName` on the font. The target accesses `mStyles` directly with an inline bounds check then calls `LStyle`, but our compiler inlines `LStyle` and uses the `Style()` function call instead. Attempts to reorder access patterns made it worse (47.9%), so the 59.9% version was kept.

### UILabel::RefreshFontMat (65.4% -> 99.6%)

Improved as a side effect of implementing SetFontMat and GetFontMat. The correct implementations in the same TU changed how the compiler optimized RefreshFontMat. Only 2 diff_arg remaining.

### HiResScreen::Accumulate (86.4% -> 94.5%)

Subagent made three changes:
1. Removed explicit `bm.Reset()` call (destructor already handles it)
2. Fixed `Merge()` argument order to match target register layout
3. Inlined `xStep`/`yStep` into `xOff`/`yOff` computations, eliminating intermediate variables

Remaining 5.5% gap is compiler codegen quirks in the `prevTile % mTiling` / `prevTile / mTiling` integer divide/modulo area: extra stack spills, instruction scheduling differences, and register swaps.

### JoypadClient::Poll (92.7% -> 93.1%)

Restructured from a `for` loop with array indexing to a `do-while` loop with explicit pointer increment, and swapped variable declaration order. The remaining 6.9% mismatch (8 of 29 instructions) is entirely a TU-level register allocation difference: the target uses r27 for `ThePlatformMgr` address (saving 5 callee-saved registers), while our build uses r28 (saving 4). Tested loop variants, Timer reference patterns, condition inversions, and declaration order permutations -- none influenced the register allocator.

### Rnd::PreInit (96.4% -- no change)

No source changes made. All 172 mismatches are unfixable or TU-level effects:
- LINKER_MERGED: `MakeString` template merged with 901 other instantiations via ICF
- `__FILE__` path mismatch (build config, our build produces `"Rnd.cpp"` vs original `"src/system/rndobj/Rnd.cpp"`)
- `REGISTER_OBJ_FACTORY(RndWind)` not inlined into PreInit (our compiler generates a call to the COMDAT `RndWind::Init()` instead)
- 9 register swaps across 3 pairs (TU-level allocation differences)

### MoveMgr::GetRoutinePreferredVariant (97.5% -- no change)

Single mismatch: `beqlr cr6` in the target vs `beq cr6, 0x68` in our build at instruction 12. Over 10 code variations tested (early return, ternary, guard clauses, condition inversions, casts). All produce identical codegen. The same compiler uses `beqlr` at instruction 25 in the same function, so it is a backend heuristic choice that cannot be influenced from source.

### RndPropAnim::ValueFromFrame (99.9% -- no change)

6 diff_arg in 3 pairs of `lis`/`lwz` or `lis`/`lfs` instructions loading global symbols (`gNullStr`, `255.0f`, `0.0f`). All 262 instructions have identical opcodes, registers, and symbol names. The diff_arg flag is triggered by COFF relocation metadata: PAIR relocation symbol index encoding and float constant section placement differences between our toolchain and the original.

## Key Findings

### m2c Decompilation for Stub Functions
For functions with no RB3 reference (DC3-specific code), m2c decompilation via the `run_analyze_function` MCP tool was essential. The m2c output for SetFontMat revealed the full control flow including three error notification paths that would have been difficult to reverse-engineer from the assembly diff alone.

### Direct Member Access vs Accessors
The target binary's SetFontMat accesses `mStyles[i].mFont` directly with inline bounds checking, while our compiler routes through the `Style()` accessor function. Using direct member access with manual bounds checks produced a much better match (0% -> 81.3%).

### TU-Level Side Effects
Implementing SetFontMat and GetFontMat caused RefreshFontMat to jump from 65.4% to 99.6% without any direct changes to RefreshFontMat. This demonstrates how TU-level compiler decisions cascade: adding correct code to nearby functions changes register pressure and inlining decisions throughout the translation unit.

### beqlr -- Unfixable Compiler Backend Pattern
`beqlr` combines a conditional branch with a return into a single PowerPC instruction. The MSVC Xbox 360 compiler applies this optimization based on internal heuristics (branch distance, code layout, register liveness) that cannot be controlled from C++ source. This was confirmed by testing 10+ code variations that all produced identical output.

### Subagent Parallelism
Five functions in independent TUs were dispatched to parallel subagents while the main thread worked on the UILabel functions. This allowed all 8 functions to be investigated in a single session despite some subagents running for 10-20 minutes each.

## Files Changed

- `src/system/ui/UILabel.cpp` -- SetFontMat implementation, GetFontMat implementation
- `src/system/rndobj/HiResScreen.cpp` -- Accumulate improvements (Merge args, inlined steps, removed Reset)
- `src/system/os/JoypadClient.cpp` -- Poll loop restructuring

## Conclusions

Of the 7 planned functions (plus the bonus GetFontMat), 5 were improved and 3 remained unchanged at their limits. The largest gains came from implementing stub functions (SetFontMat +77.1%, GetFontMat +53.6%) and from TU-level side effects (RefreshFontMat +34.2%). Functions already above 95% showed minimal or no improvement, confirming that the remaining gaps are compiler backend or linker-level differences that cannot be addressed through source changes.
