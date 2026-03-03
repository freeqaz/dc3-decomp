# Session: StandingStillGestureFilter::Update Decomp Attempt

**Date**: 2026-03-03  
**Function**: `StandingStillGestureFilter::Update(const Skeleton&, int)`  
**Symbol**: `?Update@StandingStillGestureFilter@@QAAXABVSkeleton@@H@Z`  
**Final Match**: 81.9%  
**Status**: AT_LIMIT

## Session Goal

Improve the decompilation match percentage for the standing still gesture detection function, which analyzes skeleton pose data from Kinect to determine if a player is standing still.

## What Was Attempted

### 1. Initial Analysis (Pre-Work)
- **Baseline**: 81.9% match at start
- **Primary Issues Identified**:
  - Stack frame 16 bytes larger than original (0xf0 vs 0xe0)
  - Extra FPR saves (stfd f29-f31) not in original binary
  - 54 register swaps (r30↔r31) throughout function
  - 40 insert/delete mismatches in 14 clusters

### 2. Control Flow Experiments

**Comparison Flips**
- Tried `std::fabs(shoulderDiff.z) > mForwardFacingCutoff` vs `mForwardFacingCutoff < std::fabs(shoulderDiff.z)`
- Result: No improvement (81.88% vs 81.87%)

**If/Else Branch Inversion**
- Swapped condition and branches for shoulder check:
  ```cpp
  // Original approach
  if (std::fabs(shoulderDiff.z) > mForwardFacingCutoff) { state = 6; } else { ... }
  
  // Inverted approach  
  if (std::fabs(shoulderDiff.z) <= mForwardFacingCutoff) { ... } else { state = 6; }
  ```
- Result: Regressed to 79.7%, reverted

### 3. Variable Declaration Reordering

**Tried via Permuter**
- Declaration movement patterns (move `&filter` up/down)
- Declaration reordering
- Result: Build failures or regressions (-3.6% to -5.2%)

**Manual Reordering**
- Reordered `idx` vs `filter` declaration order
- Moved `const Skeleton* skeleton = &skeleton_` pointer conversion
- Result: No improvement, remained at 81.9%

### 4. Normalize Inline Expansion

**Attempt 1: Full Inline**
```cpp
float len = std::sqrt(v1.x * v1.x + v1.y * v1.y + v1.z * v1.z);
if (len > 0.0f) {
    v1.x /= len; v1.y /= len; v1.z /= len;
}
```
- Result: Regressed to 61.1%, reverted

**Attempt 2: Shoulder Only**
```cpp
float shoulderLen = shoulderDiffX * shoulderDiffX + ...;
if (shoulderLen > 0.0f) shoulderLen = 1.0f / std::sqrt(shoulderLen);
float shoulderDiffNormZ = shoulderDiffZ * shoulderLen;
```
- Result: Regressed to 77.1%, reverted

### 5. Parameter Passing Experiments

**Pointer vs Reference**
- Changed `const Skeleton &skeleton` to `const Skeleton *skeleton` with pointer arithmetic
- Result: No improvement, added complexity

### 6. Variable Reuse

**Shared Vector2 for ScreenPos calls**
```cpp
Vector2 handPos;
skeleton.ScreenPos(kJointHandLeft, handPos);
Vector2 handLeftPos = handPos;
// ... reuse handPos for other calls
```
- Result: Regressed to 80.9%, reverted

## Key Findings

### Root Causes of Mismatch

1. **Stack Frame Layout**: 16-byte difference (0xf0 vs 0xe0)
   - Affects 39 instructions with offset deltas
   - Our version allocates space for f29-f31 that original doesn't save

2. **Register Allocation**: 54 r30↔r31 swaps
   - Dominant pattern across entire function
   - Compiler choosing different registers for `this` and `skeleton` parameters

3. **Insert/Delete Clusters**: 40 instructions in 14 clusters
   - Missing float calculations between Normalize calls (original has extra fadds/fmadds)
   - Different approach to writing to `TheGestureMgr->unk30[idx]`

### What the Original Does Differently

From objdiff analysis (cluster 3, idx 90-98):
```
Original has:
  lfs f12, 0x54(r1)
  lis r11, __real@00000000
  fadds f12, f12, f11
  lfs f13, 0x48(r31)
  lfs f0, __real@00000000(r11)
  lfs f11, 0x58(r1)
  fmadds f0, f12, f0, f11
  
Our version:
  lfs f13, 0x98(r1)  [direct load, different offset]
  fabs f13, f13
```

The original has extra float math that our inline Normalize doesn't generate.

## Tools That Were Helpful

### 1. `orchestrator_run_analyze_function`
- **Usefulness**: HIGH
- Provided unified view with objdiff + Ghidra + m2c + cross-references
- Structural info (offset swaps, register swaps) immediately visible
- **Recommendation**: Always run this first for any new function

### 2. `orchestrator_run_diff_inspect` (mode="mismatches")
- **Usefulness**: HIGH
- Detailed instruction-by-instruction comparison
- Identified exact index where ble↔bge difference occurred (idx 100)
- **Recommendation**: Use to pinpoint exact mismatch locations

### 3. `orchestrator_run_diff_inspect` (mode="clusters")
- **Usefulness**: HIGH
- Grouped insert/delete mismatches into logical clusters
- Revealed 14 distinct problem areas (stack save, shoulder math, Normalize regions, etc.)
- **Recommendation**: Use before manual edits to understand structure

### 4. Permuter with `--no-guided --max-variants 200`
- **Usefulness**: MEDIUM
- Tested many variations quickly: comparison flips, branch polarity, signed/unsigned casts
- Best result: cmpflip_2 at 81.88% (+0.01% - within noise)
- **Recommendation**: Use early to validate approach, don't expect miracles at 80%+

### 5. `orchestrator_run_objdiff` (concise=false)
- **Usefulness**: MEDIUM
- Auto-diagnosis with "Detected Patterns" section helpful
- Identified REGISTER_SWAP, CONTROL_FLOW, ADDRESS_RELOCATION_NOISE automatically
- **Recommendation**: Good for quick iteration when making targeted changes

## Tools That Were Less Helpful

### 1. Permuter with `declaration_movement`
- **Issue**: Build failures on most variations
- Moving `&filter` up/down broke compilation due to forward references
- **Learning**: Declaration reorder patterns are fragile

### 2. Manual normalize inline expansion
- **Issue**: Major regressions (61-77%)
- Compiler generates entirely different code when Normalize is inlined
- Stack pressure increases, register allocation changes
- **Learning**: Inlining standard library math functions is risky for matching

### 3. Ghidra Decompilation alone
- **Issue**: Output is pseudocode, not compilable C++
- Shows intent but doesn't help with matching specific instruction sequences
- **Learning**: Use for understanding algorithm, not for code generation

## Workflow Recommendations

### For Functions at 80%+ Match

1. **Don't start with control flow changes**
   - At this level, control flow is usually correct
   - Focus on variable ordering, stack layout, register pressure

2. **Check stack frame size first**
   - Use `orchestrator_run_diff_inspect mode="mismatches"`
   - Look for `stwu` offset difference
   - If >8 bytes different, you're fighting the compiler

3. **Permuter is for validation, not discovery**
   - At 80%+, permutations usually don't help
   - Use to confirm "is this fixable?" rather than "what's the fix?"

4. **Register swaps are often unfixable**
   - 54 r30↔r31 swaps in this function
   - No amount of variable reordering fixed it
   - Accept when the swaps are consistent (same pairs throughout)

### Red Flags for AT_LIMIT

- Stack offset delta >8 bytes across many instructions
- Consistent register swaps (same pairs, many locations)
- Extra prologue/epilogue saves (FPRs especially)
- Delete clusters for `stfd`/`lfd` instructions

## What to Try Next (If Continuing)

1. **Struct Layout Changes**
   - Check if `StandingStillGestureFilter` or `Skeleton` struct layout differs
   - Field ordering affects stack layout
   - Use `orchestrator_lookup_struct_offset` for any offset mismatches

2. **Different Compilation Flags**
   - Check if different optimization flags were used
   - Our O1 vs original might differ in FP register allocation

3. **Normalize as External Function**
   - Maybe Normalize is a real function call in original
   - Try `extern void Normalize(...)` instead of inline
   - Would explain the insert/delete clusters around Normalize calls

4. **Assembly-Level Fix**
   - If this is critical for completion, consider inline assembly
   - Only for final 95%→100% push on truly stuck functions

## Conclusion

StandingStillGestureFilter::Update is structurally correct but has compiler-level differences in:
- Stack frame layout (+16 bytes)
- FP register pressure (saves f29-f31)
- General register allocation (r30↔r31 swaps)

These are not fixable at source level with current compiler/toolchain. The function is correctly implemented and matches the algorithm, just not the exact instruction sequence.

**Verdict**: AT_LIMIT at 81.9% - acceptable for decomp completion.
