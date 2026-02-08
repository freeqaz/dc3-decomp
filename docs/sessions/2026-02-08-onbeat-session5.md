# RhythmBattle::OnBeat - Session 5

**Date**: 2026-02-08
**Symbol**: `?OnBeat@RhythmBattle@@AAAXXZ`
**File**: `src/system/hamobj/RhythmBattle.cpp` (lines 683-1429)
**Start**: 93.3% | **End**: 93.3% (no change)
**Experiments**: E27-E30 (all reverted)

## Summary

Session 5 investigated the stack frame layout difference (0x760 vs target 0x750, +16 bytes) and two code-level discrepancies: an extra DataArray::Release call (54 vs 53) and a missing InTheZone call (26 vs 27). All experiments regressed and were reverted.

## Experiments

### E27: Add missing InTheZone call (line 832)
**Goal**: Fix InTheZone count 26->27 by forcing eager evaluation of `||`

| Variant | Change | Match% | Notes |
|---------|--------|--------|-------|
| E27a | Pre-cache to `int p1z, p2z` locals | 93.0% | +6 insert, +4 delete |
| E27b | Bitwise OR `\|` instead of `\|\|` | 93.1% | Different codegen, still worse |

**Verdict**: REVERTED. Short-circuit evaluation difference is inherent to the `||` operator. Cannot force both calls without adding stack locals that worsen the frame.

### E28: Restructure bars_between_vo_suggestion (lines 897-901)
**Goal**: Fix Release count 54->53 by restructuring `handled` assignment

| Variant | Change | Match% | Notes |
|---------|--------|--------|-------|
| E28a | Scoped `{DataNode handled2 = ...}` | 93.1% | +3 insert, +6 delete, offset swaps 2->8 |
| E28b | Remove `handled.Int()` call entirely | 93.2% | Better but still -0.1% |
| E28c | Separate `DataNode bbvs` local | 93.0% | Extra local adds to stack |

**Verdict**: REVERTED. The second HandleType result assignment to `handled` creates a necessary temporary. Any restructuring either adds stack space or changes register allocation.

### E29: Eliminate r28 (RndAnimatable*) local
**Goal**: Reduce frame by replacing pointer with bool flag

| Variant | Change | Match% | Notes |
|---------|--------|--------|-------|
| E29a | `bool r28 = false` / `r28 = true` | 93.0% | Register swaps 122->191 (!) |

**Verdict**: REVERTED. The pointer type is critical for register allocation. Changing to bool causes massive register swap cascade (r23<->r24 dominated at 52 instructions).

### E30: Variable declaration reorder (b22 before goofy)
**Goal**: Fix -20 delta group by changing declaration order

| Variant | Change | Match% | Notes |
|---------|--------|--------|-------|
| E30a | Move `bool b22` after `focusPanel`, before `goofy` | 93.0% | Register swaps 122->198 (!!) |

**Verdict**: REVERTED. Declaration reordering in a 4170-instruction function is catastrophic for register allocation. The compiler's allocation is extremely sensitive to variable declaration positions.

## Analysis of Remaining Mismatches

Deep cluster analysis revealed the root causes of the remaining ~6.7%:

### Stack Frame (+16 bytes, 509 instructions)
The dominant +16 offset delta (228 instructions) plus +8 delta (165 instructions) account for ~29% of non-matching instructions. These are caused by differing stack slot allocation that we cannot control at the source level.

### Cluster 21 (idx 1354-1372, 15 deletes)
The target inlines the `mEndBeat < curBeat + 12.0f` float comparison and bool logic (`b43 || outOfRange`) with different instruction scheduling. Our compiler evaluates conditions in a different order. This is instruction scheduling, not code-level.

### Register Swaps (123 instructions, 17 pairs)
Dominated by r10<->r9 (20), r10<->r11 (18), r21<->r22 (15). These are allocation differences from the +16 frame offset propagating through the register allocator.

### LINKER_MERGED (43 calls)
Unfixable ICF differences.

### BOOL_MASK (2 instructions)
Unfixable compiler bool return handling.

## Conclusion

93.3% is the practical limit for this function. The remaining mismatches are:
1. **Stack frame layout** (+16 bytes) - compiler stack allocator difference, not controllable
2. **Instruction scheduling** - compiler reorders condition evaluation
3. **Register allocation** - cascading effect of stack frame difference
4. **Linker merging** (ICF) - unfixable
5. **Bool masks** - unfixable

All code-level approaches to fix the InTheZone count (26 vs 27) and Release count (54 vs 53) introduce worse regressions than the mismatches they fix. The function is at its limit.

## Cumulative Stats (Sessions 1-5)

| Session | Experiments | Kept | Start | End |
|---------|-------------|------|-------|-----|
| 1-2 | E1-E11 | 7 | ~80% | 93.3% |
| 3 | E12-E19 | 0 | 93.3% | 93.3% |
| 4 | E20-E26 | 0 | 93.3% | 93.3% |
| 5 | E27-E30 | 0 | 93.3% | 93.3% |
| **Total** | **30** | **7** | **~80%** | **93.3%** |
