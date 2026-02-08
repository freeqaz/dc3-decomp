# RhythmBattle::OnBeat Session 4 - Continued Optimization

**Date**: 2026-02-08
**Symbol**: `?OnBeat@RhythmBattle@@AAAXXZ`
**Baseline**: 93.3% (4171 instructions, 3 diff_op, 3 BOOL_MASK, 3 CONTROL_FLOW inversions)
**File**: `src/system/hamobj/RhythmBattle.cpp`

## Deep Analysis (using diff_inspect MCP tools)

Key findings from diagnosis:
- **Stack frame +16 bytes** (0x760 vs 0x750): affects 228 instructions, unfixable
- **Register swaps**: 141 instructions across 34 pairs, mostly unfixable
- **3 diff_ops**: idx 1243 (ble/bgt), idx 1369 (blt/bne), idx 3619 (b/bl tail call)
- **Cluster 20** (idx 1354-1372): 17-instruction mismatch from compound condition evaluation order (15 delete, 2 insert). Target evaluates eagerly, our compiler short-circuits.
- **Cluster 7** (idx 974-978): 5 deletes — target has extra virtual call in scorer loop. InTheZone count: target 27, ours 26.
- **idx 1158**: `li r22, 0x5` (target) vs `lwz r23, 0xc0` (ours) — constant propagation vs stack load for unk124.
- **Cluster 16** (idx 1318): Extra `xori r11, r11, 0x1` from `b36 == 0 ? 5 : 6` ternary.

## Experiment Log

### E12: Cache player pointers for SetWindow calls (OFFSET_SWAP [1233,1235])
- **Change**: Cache mPlayerOne/mPlayerTwo to locals before SetWindow calls
- **Result**: REVERTED. Fixed OFFSET_SWAP (0x3c,0x50) but added 1 instruction (insert 44→45). Fallback (swap call order) also worse (diff_arg +1).

### E13: DataNode ternary to if/else (OFFSET_SWAP [678,679])
- **Change**: Convert ternary to if/else for remainingValue
- **Result**: REVERTED. diff_op 3→4, insert 44→45, delete 64→66.

### E14: Split compound condition (CONTROL_FLOW idx 1369)
- **Change**: Pre-computed bool fallback: `bool triggerPhase = mFinale ? ... : ...`
- **Result**: REVERTED. Dropped 93.3% → 92.9%. Full if/else-if split too invasive.

### E15: Move static Symbols out of if scope (CONTROL_FLOW idx 1287)
- **Change**: Move finale_tandance statics to enclosing bare block
- **Result**: REVERTED. Dropped 93.3% → 93.2%.

### E16: Player pointer caching in swag jack section (REGISTER_SWAP)
- **Change**: Cache p1/p2 before swag jack conditions
- **Result**: REVERTED. Traded improvements (replace -4, BOOL_MASK -1, fixed OFFSET_SWAP) for regressions (diff_arg +3, delete +3, REGISTER_SWAP +8). Net negative.

### E17: Move worked_it_progress Message scope (CONTROL_FLOW idx 1243)
- **Change**: Move static Message out of if block
- **Result**: REVERTED. Dropped 93.3% → 93.1%.

### E18: Cache player ptrs before ZoneValue/Unk26c checks
- **Change**: Add `RhythmBattlePlayer *p1 = mPlayerOne; p2 = mPlayerTwo;` before condition
- **Result**: REVERTED. Reduced inserts (-2) but increased diff_arg (+10) and register swaps (117→133). The diff_op fix came from E19 alone.

### E19: unk124 ternary to arithmetic (KEPT)
- **Change**: `unk124 = b36 == 0 ? 5 : 6` → `unk124 = (int)b36 + 5`
- **Result**: **KEPT**. Fixed one diff_op (3→2), one BOOL_MASK (3→2), one CONTROL_FLOW inversion (3→2). Minor regression: delete +2, register swaps +5.

### E19b: unk124 ternary swap
- **Change**: `unk124 = b36 ? 6 : 5`
- **Result**: Identical to baseline — no effect. Compiler generates same code.

### E19c: jackState local variable
- **Change**: `int jackState = (int)b36 + 5; unk124 = jackState; ... i6b4 = jackState;`
- **Result**: REVERTED. Dropped to 93.2%, diff_op back to 3.

### E20a: i6b4 inverted comparison
- **Change**: `(i16 <= i19)` with swapped branches
- **Result**: REVERTED. diff_op back to 3, OFFSET_SWAP increased.

### E20b: i6b4 if/else form
- **Change**: if/else instead of ternary for i6b4
- **Result**: Same numbers as E19 ternary. Reverted for code cleanliness.

### E21: iPrev Max to ternary
- **Change**: `Max(i - 1, 0)` → `i > 0 ? i - 1 : 0`
- **Result**: REVERTED. Dropped to 93.2%, new COMMUTATIVE_OP_ORDER pattern.

### E22: unk10c comparison
- **Change**: `unk10c >= 1` → `unk10c > 0`
- **Result**: REVERTED. Dropped to 93.0%.

### E23: !i6cc form
- **Change**: `inMindControl = i6cc == 0` → `inMindControl = !i6cc`
- **Result**: Identical output. No effect.

### E24: bool zone1/zone2
- **Change**: `int zone1 = ...` → `bool zone1 = ...`
- **Result**: REVERTED. Dropped to 93.1%, BOOL_MASK 2→4.

### E25: Cache jacker/jackee InTheZone
- **Change**: Pre-compute `bool jackerInZone = jacker->InTheZone()` before conditions
- **Result**: REVERTED. Dropped to 93.0%.

### E26: Reorder b6f0/i27/i35 assignments
- **Change**: Swap order of b6f0 and i27 assignments
- **Result**: Identical output. No effect.

## Final Result
- **Starting**: 93.3% (4171 instr, 3 diff_op, 44 insert, 64 delete, 3 BOOL_MASK, 3 CF inversions)
- **Ending**: 93.3% (4170 instr, 2 diff_op, 43 insert, 66 delete, 2 BOOL_MASK, 2 CF inversions)
- **Net**: -1 diff_op, -1 BOOL_MASK, -1 CONTROL_FLOW inversion, -1 instruction, -1 insert, +2 delete, +5 reg swaps
- **Kept changes**: E19 only (`unk124 = (int)b36 + 5`)

## Remaining Mismatch Analysis

| Category | Count | Status |
|----------|-------|--------|
| LINKER_MERGED | 43 calls | Unfixable (ICF) |
| Stack offset +16 | 228 instr | Unfixable (compiler stack layout) |
| Stack offset +8 | 165 instr | Unfixable |
| REGISTER_SWAP | 122 instr, 17 pairs | Exhausted (~20 experiments) |
| CONTROL_FLOW | 2 inversions + 3 replacements | 1 fixable (idx 1243 ble/bgt), resists all attempts |
| BOOL_MASK | 2 instr | Usually unfixable |
| OFFSET_SWAP | 2 swaps | Fixable individually but trade-offs negative |
| diff_op tail call | 1 (idx 3619 b/bl) | Unfixable (compiler optimization) |
| Symbol relocations | 554 instr | Unfixable (static local positions) |

## Conclusion

Function is at its practical limit at 93.3%. The remaining ~6.7% mismatch is dominated by compiler-level differences (stack frame sizing, register allocation, ICF merging, symbol relocation) that cannot be controlled from C++ source. The single remaining fixable diff_op (ble/bgt at idx 1243) was attacked from 6 different angles in this session with no success, suggesting it's also effectively at limit.
