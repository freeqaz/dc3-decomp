# M2C Failure Analysis Report

**Date**: 2026-01-27 (Updated)
**Source**: Orchestrator database attempt logs
**Previous Version**: 36 attempts, 5.5% success rate

## Executive Summary

Analysis of **63 m2c-related attempts** across **23 unique functions** reveals that m2c decompilation has a **4.8% success rate** (3/63) for guiding implementations to ≥99% match. This is a decrease from the previous 5.5% rate, as more edge cases have been encountered.

**Key Finding**: The primary failure mode is **massive size mismatches** between m2c output and target binary, typically indicating missing implementation rather than incorrect control flow. Secondary failures are **linker-merged function calls** (unfixable compiler optimizations) and **register allocation patterns** beyond C++ code control.

## Statistics

| Metric | Previous | Current | Change |
|--------|----------|---------|--------|
| Total m2c-related attempts | 36 | 63 | +75% |
| Unique functions | 14 | 24 | +71% |
| Success rate (100% match) | 5.5% | 4.8% | -0.7% |

### Exit Status Distribution

| Status | Count | Percentage | Description |
|--------|-------|------------|-------------|
| `at_limit` | 43 | 68.3% | Structural issues preventing further improvement |
| `stuck` | 17 | 27.0% | Blocked on specific issues |
| `complete` | 3 | 4.8% | Successfully achieved 100% match |

## Functions Most Affected

| Function | Attempts | Best Match | Blocker |
|----------|----------|------------|---------|
| NgEnviron::Select | 18 | 11.85% | 8.4x size mismatch, missing light pipeline |
| StorePanel::UpdateOffers | 3 | 72.5% | Register swap patterns |
| Flow::PostLoad | 3 | 6.4% | 12.4x size mismatch, missing property logic |
| UILabel::LabelUpdate | 3 | 59.9% | Linker-merged + register swaps |
| FlowTimer::ChildFinished | 2 | 27.1% | 3.2x size mismatch (64-bit instructions now supported) |
| HamRibbon::ConstructMesh | 2 | 28.1% | 3.5x size mismatch, noisy m2c output |

## Failure Categories

### 1. Size Mismatch - Missing Implementation (Most Common)

Functions where the target binary is **significantly larger** than the current source implementation, indicating m2c only captured a fraction of the actual logic.

| Function | Target Size | Current Size | Ratio | Notes |
|----------|-------------|--------------|-------|-------|
| Flow::PostLoad | 1936 bytes | 156 bytes | **12.4x** | Missing version-branched property loading |
| NgEnviron::Select | 1956 bytes | 232 bytes | **8.4x** | Missing light pipeline, shader setup |
| FlowMultiSetProperty::Activate | 472 bytes | 324 bytes | 1.5x | Missing activation logic |
| FlowTimer::Execute | 792 bytes | 216 bytes | **3.7x** | Missing EventTask creation |
| WorldCrowd::CollideList | 452 bytes | 124 bytes | **3.6x** | Missing collision detection logic |
| FlowTimer::ChildFinished | 752 bytes | 232 bytes | **3.2x** | 64-bit instructions + missing code |
| HamRibbon::ConstructMesh | 1296 bytes | 368 bytes | **3.5x** | Missing mesh generation loops |
| MoveFrame::Load | 1724 bytes | 1184 bytes | 1.5x | Intentionally incomplete source |

**Root Cause**: m2c generates incomplete decompilation when:
- Functions contain inlined code from other compilation units
- Complex control flow patterns aren't recognized
- The source file is a stub or placeholder

### 2. Linker-Merged Functions (LINKER_MERGED) - Unfixable

Compiler-generated merged function calls that cannot be reproduced in C++ source code. These represent **unavoidable mismatches**.

| Function | Merged Symbols | Match % | Impact |
|----------|----------------|---------|--------|
| UIList::SetSelectedSimulateScroll | merged_ObjDirPtr | 99.65% | 3 mismatches |
| Splash::UpdateThread | merged_824D1870 (x4) | 80.4% | 4.8% of calls |
| StorePanel::LoadArt | merged_StringCtor | 70.9% | 1 mismatch |
| ClipDistMap::FindNodes | merged_826A9060 | 68.95% | 1.3% of calls |
| MoveFrame::Load | merged_Read3FloatStruct (x4) | 63.6% | 5 calls |
| FlowMultiSetProperty::Activate | merged_823314D8 | 61.9% | 1 call |
| FlowTimer::Execute | merged_823314D8, merged_82610090 | 23.7% | 3 calls |
| FlowTimer::ChildFinished | merged_823314D8 | 27.1% | 1 call |

**Root Cause**: Link-Time Code Generation (LTCG) merges identical function bodies across compilation units. The merged function addresses are resolved at link time and cannot be predicted from C++ code.

### 3. Register Swap Patterns (REGISTER_SWAP) - Compiler Heuristics

Register allocation differences caused by compiler heuristics that cannot be controlled from C++ source.

| Function | Swaps Detected | Pattern | Match % |
|----------|----------------|---------|---------|
| Splash::UpdateThread | 26 | r28/r31, r1/r31 | 80.4% |
| StorePanel::UpdateOffers | 25-30 | r24-r30, r9-r10 | 72.5% |
| ClipDistMap::FindNodes | 24 | f30/f31, r29/r30, r26/r27 | 68.95% |
| UILabel::LabelUpdate | varies | r29/r30, r10/r11, r11/r27 | 59.9% |
| __median<RndMesh*> | 4 | r29/r31, r27/r30 | 63.1% |
| HamRibbon::ConstructMesh | 11 | r23/r28, r22/r27, r26/r27 | 28.1% |
| HolmesClientPollKeyboard | 6 | r10/r11, r11/r31 | 61.4% |

**Root Cause**: The MSVC compiler uses heuristics for register allocation that depend on compilation context (other functions in TU, optimization level, etc.). Reordering variables or expressions rarely helps.

### 4. M2C_ERROR Markers - Unhandled Instructions

m2c generates `M2C_ERROR` markers for PowerPC instructions it cannot translate.

**Status Update (2026-01-27)**: The following 64-bit PowerPC instructions have been implemented in m2c:

| Instruction | Description | Status |
|-------------|-------------|--------|
| `mftb` | Move From Time Base (64-bit timer) | ✅ Implemented |
| `fcfid` | Floating Convert From Integer Doubleword | ✅ Implemented |
| `ld` / `ldu` / `ldux` | Load Doubleword (+ update variants) | ✅ Implemented |
| `std` / `stdu` / `stdux` | Store Doubleword (+ update variants) | ✅ Implemented |
| `rldicl` | Rotate Left Doubleword Immediate Clear Left | ✅ Implemented |

**Note**: Functions like `FlowTimer::ChildFinished` that previously failed due to these instructions should now decompile correctly. The remaining failures for these functions are likely due to other factors (size mismatch, linker-merged calls, etc.).

### 5. Dead Code Elimination - Compiler Optimizations

The compiler optimizes away code that has no observable side effects.

**Affected Function**: `UIListState::PageScroll` (97.56% match)

**Issue**: The function calls `Scroll(int, bool)` which has an empty implementation. The compiler recognizes this has no side effects and eliminates the entire function body, producing a 164-byte target vs near-empty compilation.

**Root Cause**: Compiler dead code elimination is highly aggressive when it can prove operations have no observable effects.

### 6. Stack Frame / Calling Convention Differences

| Function | Issue | Match % |
|----------|-------|---------|
| UnloadGlitchCB | Stack frame 0x70 vs 0x60 | 55.0% |
| __median | Different __savegprlr prologue (_26 vs _29) | 63.1% |

**Root Cause**: Compiler decisions about stack layout and callee-saved register patterns.

## Success Cases (3 Functions at 100%)

| Function | Final Match | Key Success Factors |
|----------|-------------|---------------------|
| UISlider::SetTypeDef | **100%** | Simple structure: parent call + one method |
| UIManager::ReloadStrings | **100%** | Message broadcasting, simple iteration |
| UIListState::ScrollToTarget | 99.375% | Clear control flow, only XOR operand order diff |

**Common Success Factors**:
1. Small function size (< 200 bytes target)
2. Simple control flow (no nested loops, minimal branches)
3. ~~No 64-bit arithmetic or timing operations~~ (64-bit now supported!)
4. No inlined external functions
5. m2c output was structurally accurate

### Near-Success Cases (High Match but at_limit)

| Function | Match % | Blocker |
|----------|---------|---------|
| UIList::SetSelectedSimulateScroll | 99.65% | 3 linker-merged calls |
| UIListState::PageScroll | 97.56% | Dead code elimination |
| Splash::UpdateThread | 80.44% | 4 linker-merged + 26 register swaps |
| StorePanel::LoadArt | 70.9% | 1 linker-merged + control flow |
| ClipDistMap::FindNodes | 68.95% | 1 linker-merged + 24 register swaps |

## Recommendations

### For m2c Tool Improvements

1. ~~**Add PowerPC64 Instruction Support** (Priority: High)~~ ✅ **COMPLETED**
   - `mftb`, `fcfid`, `ld`/`ldu`/`ldux`, `std`/`stdu`/`stdux`, `rldicl` are now supported
   - 64-bit timing and math functions should now decompile correctly

2. **Improve Size Estimation** (Priority: Medium)
   - Warn when m2c output is significantly smaller than function bounds
   - Helps identify incomplete decompilation early

3. **Better Inline Detection** (Priority: Medium)
   - Flag functions that appear to have inlined code
   - Cross-reference call targets against known addresses

### For Decomp Workflow

1. **Pre-screen Functions Before m2c**
   ```
   Good candidates:
   - Size < 500 bytes
   - Simple control flow (low cyclomatic complexity)
   - 64-bit instructions (mftb, fcfid, ld, std, rldicl) are now supported

   Avoid:
   - Functions > 1000 bytes
   - Complex state machines
   - Shader/rendering code
   ```

2. **Use m2c for Structure, Not Exact Code**
   - m2c is best for understanding *what* a function does
   - Use RB3 reference for *how* to write matching C++
   - Don't expect m2c output to compile-and-match

3. **Recognize Unfixable Limits Early**
   - Linker-merged functions: Accept 1-5% loss per merged call
   - ~~64-bit instructions: Mark function as requiring manual assembly~~ (Now supported!)
   - Size ratio > 2x: Likely missing implementation, not control flow

4. **Track Best Match Achieved**
   - Some functions may have hit their ceiling early
   - Don't retry functions that show linker-merged or size mismatch patterns

## Appendix: Attempt Distribution by Exit Status

### at_limit (42 attempts)

Functions hitting structural limits:

| Function | Attempts | Best % | Primary Blocker |
|----------|----------|--------|-----------------|
| NgEnviron::Select | 14 | 11.85% | Size mismatch (8.4x) |
| UIListState::PageScroll | 2 | 97.56% | Dead code elimination |
| UIList::SetSelectedSimulateScroll | 2 | 99.65% | Linker-merged |
| UILabel::LabelUpdate | 1 | 59.9% | Linker-merged + register |
| Flow::PostLoad | 3 | 6.4% | Size mismatch (12.4x) |
| FlowTimer::Execute | 2 | 23.7% | Linker-merged (x3) |
| WorldCrowd::CollideList | 2 | 25.98% | Size mismatch (3.6x) |
| MoveFrame::Load | 2 | 63.6% | Linker-merged (x5) |
| __median | 2 | 63.1% | Register allocation |
| FlowMultiSetProperty::Activate | 2 | 61.9% | Linker-merged |
| Splash::UpdateThread | 2 | 80.4% | Linker-merged (x4) |
| UnloadGlitchCB | 2 | 55.0% | Stack frame difference |
| UISlider::Update | 2 | 25.1% | Size mismatch (3.1x) |
| ClipDistMap::FindNodes | 2 | 68.95% | Linker-merged + register |
| StorePanel::LoadArt | 1 | 70.9% | Linker-merged |
| UIListState::ScrollToTarget | 2 | 99.375% | XOR operand order |

### stuck (17 attempts)

Functions blocked on specific issues:

| Function | Attempts | Best % | Blocker |
|----------|----------|--------|---------|
| NgEnviron::Select | 4 | 11.85% | Missing light pipeline impl |
| StorePanel::UpdateOffers | 3 | 72.5% | Register swap patterns |
| UILabel::LabelUpdate | 2 | 59.9% | Control flow regressions |
| HolmesClientPollKeyboard | 1 | 61.4% | Register swap patterns |
| HamWardrobe::UpdateOverlay | 1 | 24.8% | Missing struct definitions |
| HamRibbon::ConstructMesh | 2 | 28.1% | Noisy m2c output |
| FlowTimer::ChildFinished | 2 | 27.1% | Size mismatch (was: 64-bit instructions, now supported) |
| MoveParent::PopulateAdjacentParents | 1 | 27.2% | Missing compiler intrinsics |

### complete (3 attempts)

| Function | Match | Method |
|----------|-------|--------|
| UISlider::SetTypeDef | 100% | m2c guided (simple parent call + method) |
| UIManager::ReloadStrings | 100% | m2c guided (message iteration) |
| UIListState::ScrollToTarget | 99.375% | m2c guided (at_limit due to XOR operand order) |

## Conclusion

m2c decompilation provides value primarily as a **structural reference** rather than compilable code. Success is limited to simple functions (< 500 bytes, no 64-bit ops, no inlined code). For complex functions, expect m2c to capture only 10-30% of the actual implementation.

**Recommended approach**:
1. Use m2c to understand function purpose and data flow
2. Cross-reference with RB3 decomp for matching patterns
3. Accept linker-merged gaps (typically 1-5% per merged call)
4. ~~Mark functions with 64-bit instructions as requiring special handling~~ (64-bit instructions now supported!)
5. Don't retry functions that show clear size mismatches (> 2x)

---
*Generated from orchestrator database analysis. 63 total attempts across 24 functions.*
