# SaveLoadManager::Poll Deep Investigation

**Date**: 2026-03-06
**Function**: `SaveLoadManager::Poll` (`?Poll@SaveLoadManager@@QAAXXZ`)
**Size**: 3108 bytes (target), ~3032 bytes (base)
**Match**: 59.8% start → 59.4% end (semantically more correct despite lower %)

## Overview

SaveLoadManager::Poll is a large state machine with ~30 switch cases dispatched via a jump table. It manages save/load/cache operations through a `State` enum (0x0-0x67). The function was blocked by **40 systematic branch polarity inversions** (beq<->bne) and a **16-byte stack frame difference** (target 0x1150 vs ours 0x1140).

## Fixes Applied

### 1. MILO_ASSERT Pattern Fix
**Before**: `if (mState == kS_SaveOverwrite) { MILO_ASSERT(false, 0x2ED); }`
**After**: `MILO_ASSERT(mState != kS_SaveOverwrite, 0x2ED);`

The target's MakeString template instantiation (`MakeString<char[8], int, char[35]>`) showed the condition string was `"mState != kS_SaveOverwrite"`, not `"false"`. The old code generated `MakeString<char[20], int, char[6]>` with condition string `"false"`.

### 2. State Transition: kS_SongCacheMount Success Path
**Before**: `nextState = kS_SongCacheGetSize` (0x1d)
**After**: `nextState = kS_SongCacheUnmount` (0x20)

Confirmed from objdiff at instruction index 188: target has `li r4, 0x20` after `bl AddCacheID`. The Ghidra decompilation shows the target's success path uses `goto LAB_8289c858` which lands at `SVar11 = 0x20` inside the kS_SongCacheGetSize case body -- the original code shares the nextState assignment via a goto, skipping the intermediate state.

### 3. State Transition: kS_GlobalMount/MountStart Success Path
**Before**: `nextState = kS_GlobalRead` (0x30)
**After**: `nextState = kS_GlobalDoneRead` (0x31)

Same pattern as above. Confirmed at instruction index 482: target `li r4, 0x31`. Ghidra shows `goto LAB_8289cc3c` landing at `SVar11 = 0x31` inside kS_GlobalRead case body.

### Pattern: Skipped Intermediate States
Both transitions follow the same pattern: the original code gotos directly into the NEXT case's body (past its IsDone check), sharing the `nextState = X` assignment. Our code uses separate assignments. This is MSVC's tail merging optimization -- two cases that end with the same `li r4, value; b end` get merged to one shared block.

## Root Cause Analysis: 16-Byte Stack Frame Difference

### Target Stack Layout (from Ghidra)
```
0x50: CacheResult local_1100    (4 bytes)
0x54: State local_10fc          (4 bytes)
0x58: Symbol aSStack_10f8       (4 bytes) -- kS_GlobalOptionsSearch
0x5c: Symbol aSStack_10f4       (4 bytes) -- kS_GlobalOptionsCreate2
0x60: Symbol aSStack_10f0       (4 bytes) -- kS_SongCacheMount
0x64: Symbol aSStack_10ec       (4 bytes) -- kS_GlobalMount
0x68: Symbol aSStack_10e8       (4 bytes) -- kS_SongCacheSearch
0x70: BufStream                 (48 bytes)
0xA0: FixedSizeSaveableStream   (104 bytes)
0x110: FormatString             (4116 bytes)
```

### Our Stack Layout (from /FAs listing)
```
0x50: shared (CacheResult, Symbol, etc.)  (4 bytes, aliased)
0x54: shared (State, Symbol, etc.)        (4 bytes, aliased)
0x60: BufStream                           (48 bytes)
0x90: FixedSizeSaveableStream             (104 bytes)
0x100: FormatString                       (4116 bytes)
```

The target compiler keeps **5 separate un-aliased Symbol locals** at 0x58-0x68 (one per switch case), while our compiler aliases them all to offset 0x50. This pushes BufStream from 0x60 to 0x70, shifting everything by exactly 16 bytes.

### Why Can't We Fix It?

Symbol has a trivial destructor (just a `const char*` wrapper), so the compiler is free to alias variables in non-overlapping scopes. Our MSVC PPC build aliases aggressively; the target build does not. This is a compiler-internal optimization heuristic we cannot control from source.

## Root Cause Analysis: Branch Polarity Inversions

Every if/else chain in every switch case exhibits the same pattern:

**Target (positive branching)**:
```asm
cmpwi cr6, r11, VALUE
beq   cr6, ASSIGNMENT_LABEL    ; branch TO the assignment
; ... next check ...
```

**Ours (negative branching)**:
```asm
cmpwi cr6, r11, VALUE
bne   cr6, NEXT_CHECK_LABEL    ; branch PAST the assignment
; ... inline assignment ...
```

This affects all 30+ cases uniformly. The compiler's choice between positive and negative branching is an internal code generation strategy, possibly influenced by frame size, EH structure, or optimization heuristics. No source-level fix was found.

## Experiments Tried (All Failed)

| Experiment | Result | Frame Size |
|---|---|---|
| `volatile int __pad[4]; __pad[0] = 0;` at function scope | Frame SHRANK | 0x1070 |
| `int r; int s;` at switch scope (prevent aliasing) | Offset swaps fixed, but frame SHRANK | 0x1070 |
| Direct `FormatString` construction instead of MILO_FAIL | No effect (trivial destructor) | 0x1140 |
| Declaration order swap (`State s; int r;` vs `int r; State s;`) | No effect on offset swaps | 0x1140 |

Key insight: moving variables to wider scope causes the compiler to reorganize the ENTIRE frame, often making it smaller rather than larger. The compiler's aliasing decisions are holistic, not per-variable.

## Verified State Transitions (All Cases)

Systematically compared all Ghidra case transitions against source. Cases verified correct:
- kS_Start (0x1), kS_AutoloadSearchDevice (0x4), kS_SongCacheSearch (0x14)
- kS_SongCacheRead (0x1b), kS_SongCacheAllocRead (0x1e), kS_SongCacheWrite (0x1f)
- kS_SongCacheUnmount (0x20), kS_SongCacheDone/GlobalDoneWrite/GlobalOptionsWrite (0x21/0x33/0x3e)
- kS_GlobalOptionsSearch (0x27), kS_GlobalMount2 (0x2e), kS_GlobalDoneRead (0x31)
- kS_GlobalWrite (0x32), kS_GlobalUnmount (0x34), kS_GlobalDone (0x35)
- kS_GlobalOptionsCreate2 (0x3b), kS_GlobalOptionsAllocRead (0x3d)
- kS_GlobalOptionsUnmount (0x3f), kS_SaveOverwrite/SaveNoOverwrite (0x46/0x47)
- kS_SaveSongCache (0x52), kS_Abort/Finish (0x65/0x67)

## Remaining Blockers

| Pattern | Count | Fixability |
|---|---|---|
| Branch polarity inversions (beq<->bne) | 40 | Unfixable (compiler) |
| Register swaps (r4<->r5 dominant) | 26 instr / 4 pairs | Unfixable (volatile regs) |
| Offset swap (0x50,0x54) | 3 | Unfixable (aliasing) |
| Stack frame -16 bytes | 2 instr | Unfixable (aliasing) |
| Comparison style (off by 1) | 2 | Maybe fixable |
| Address relocation noise | 3 | Unfixable |

## Key Takeaways

1. **Large switch functions are fragile**: Small changes to any case body can ripple through the entire function's code layout due to basic block merging and branch target sharing.
2. **Variable aliasing is not controllable**: MSVC PPC's cross-case aliasing is an all-or-nothing compiler decision. Attempts to influence it (wider scope, volatile, padding) cause the compiler to reorganize the entire frame unpredictably.
3. **Correct transitions can worsen match%**: Fixing state transitions from 0x1d→0x20 and 0x30→0x31 is semantically correct but changed code layout, reducing match from 60.3% to 59.4%. The fixes should be kept for behavioral correctness.
4. **Ghidra goto labels reveal tail merging**: The `goto LAB_*` patterns in Ghidra's output correspond to MSVC's tail merging optimization where multiple code paths share a single `li r4, VALUE; b end` block.

## Files Modified
- `src/lazer/meta_ham/SaveLoadManager.cpp` (lines 1420, 1601, 1793)
