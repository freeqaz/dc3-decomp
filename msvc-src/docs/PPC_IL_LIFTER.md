# PPC To IL Lifter

Date: 2026-03-10
Status: Comprehensive implementation — 80+ PPC instructions, CFG, pattern detection, shape deltas

## Goal

Lift PPC assembly into a normalized IL-like representation for synthesis guidance.
Compares against source-side `_CL_*` IL bundles to derive actionable codegen facts
that feed the permuter's target_facts layer.

## Current Tool

Tool: `msvc-src/tools/ppc_il_lifter.py`

Commands:

```bash
# Lift one function from a .cod listing
python3 msvc-src/tools/ppc_il_lifter.py lift-listing test.cod \
  --function MyFunc [--json] [--delta]

# Compare source IL against lifted PPC
python3 msvc-src/tools/ppc_il_lifter.py compare-source source.cpp \
  --function SomeFunction [--json]

# Profile all functions in a listing
python3 msvc-src/tools/ppc_il_lifter.py profile test.cod [--json]
```

## Supported Lift Families

### Core ALU & Moves (24 mnemonics)
- moves and constants: `mr`, `fmr`, `li`, `lis`
- basic ALU: `add`, `addi`, `addic`, `addic.`, `subf`, `subf.`, `and`, `andi.`, `andis.`, `or`, `ori`, `oris`, `xor`, `xori`, `xoris`, `not`, `nand`, `nor`, `orc`

### Shifts & Masks (12 mnemonics)
- immediate shifts: `srwi`, `slwi`, `srawi`
- register shifts: `srw`, `slw`, `sraw`
- rlwinm family: `clrlwi`, `extrwi`, `clrlslwi`, `rlwinm`, `rlwinm.`
- rotate-insert: `rlwimi`, `rlwimi.`

### Bool Carry Chains (12 mnemonics)
- `addic`, `subfe`, `subfc`, `subfic`, `addze`, `adde`, `subfze`
- `cntlzw`, `eqv`, `andc`, `neg`, `srawi`

### Compares & Branches (14+ mnemonics)
- integer compares: `cmpwi`, `cmplwi`, `cmpw`, `cmplw`
- conditional branches: `beq`, `bne`, `ble`, `bge`, `blt`, `bgt`
- conditional returns: `beqlr`, `bnelr`, etc.
- unconditional: `b` (GOTO), `blr` (RETURN)
- CR field-aware: branches with `cr0`-`cr7` operands

### Float Arithmetic (20 mnemonics)
- binary: `fadd/fadds`, `fsub/fsubs`, `fmul/fmuls`, `fdiv/fdivs`
- fused multiply-add: `fmadd/fmadds`, `fmsub/fmsubs`, `fnmadd/fnmadds`, `fnmsub/fnmsubs`
- unary: `fneg`, `fabs`, `fnabs`
- conversion: `frsp`, `fctiwz`, `fctiw`, `stfiwx`
- compare: `fcmpu`, `fcmpo`
- special: `fsel`, `fres`, `frsqrte`

### Multiply / Divide (6 mnemonics)
- `mullw`, `mulli`, `mulhw`, `mulhwu`, `divw`, `divwu`

### Memory (22 mnemonics)
- base+offset: `lwz`, `lbz`, `lhz`, `lha`, `lfs`, `lfd`, `stw`, `stb`, `sth`, `stfs`, `stfd`
- update-form: `lwzu`, `stwu`, `lbzu`, `stbu`, etc.
- indexed: `lwzx`, `lbzx`, `lhzx`, `lfsx`, `lfdx`, `stwx`, `stbx`, `stfsx`, etc.

### Switch Dispatch & Indirect Calls
- CTR: `mtctr`, `mfctr`, `bctr` (DISPATCH), `bctrl` (INDIRECT_CALL)
- counted loops: `bdnz`, `bdz`, `bdnzeq`, `bdzeq`, etc.

### Prologue / Epilogue
- GPR save: `bl __savegprlr_N` → PROLOGUE_SAVE_GPR
- GPR restore: `bl __restgprlr_N` → EPILOGUE_RESTORE_GPR
- FPR save/restore: `bl __savefpr_N`, `bl __restfpr_N`
- link register: `mflr`, `mtlr`

### Condition Register (5 mnemonics)
- `cror`, `crand`, `crandc`, `crxor`, `mfcr`

### Type Conversion (2 mnemonics)
- sign extension: `extsh` (16-bit), `extsb` (8-bit)

### Other
- `nop` → NOP

## Derived Shape Facts

| Kind | Categories | Confidence | Notes |
|------|-----------|-----------|-------|
| `byte_fusion` | fused_shr_mask, fused_shl_mask, separate_shift_and_mask | 0.75-0.95 | rlwinm pattern |
| `bool_materialization` | zero_test, equality_nonzero, inequality_nonzero, signed_positive, unsigned_ordered, signed_ordered, signed_greater_equal, unsigned_greater_equal, signed_ge_short | 0.80-0.95 | Carry chain detection |
| `switch_dispatch` | switch_table, switch_ctr_chain, switch_if_chain | 0.72-0.92 | bctr/CTR chain vs CMP chain |
| `virtual_dispatch` | vtable_call, vtable_tail_call | 0.90-0.92 | lwz+mtctr+bctrl/bctr |
| `call_shape` | tail_direct_call, direct_call_return, call_sequence_return, cached_return_value | 0.80-0.95 | Direct call/return lowering |
| `float_conversion` | fctiwz_stfiwx_lwz, fctiwz_stfiwx | 0.90 | Float→int pattern |
| `float_fusion` | fused_multiply_add | 0.95 | fmadd/fmsub count |
| `prologue_shape` | register_save | 0.95 | GPR/FPR count, frame size |
| `control_flow` | cfg_complexity, counted_loop | 0.85-0.95 | Block/edge/loop counts |
| `operation_profile` | aggregate | 1.00 | Op counts by category |

Verified examples:

- `il_type_control_cast_vs_and.cpp::cast_shift` -> `byte_fusion=fused_shr_mask`
- `il_bool_materialization.cpp::zero_test` -> `bool_materialization=zero_test`
- `il_bool_materialization.cpp::signed_positive` -> `bool_materialization=signed_positive`
- `il_switch_dispatch.cpp::switch_dense` -> `switch_dispatch=switch_ctr_chain`
- `il_switch_dispatch.cpp::switch_fallthrough` -> `switch_dispatch=switch_if_chain`
- `il_call_return.cpp::call_and_return` -> `call_shape=tail_direct_call`
- `il_call_return.cpp::cached_return` -> `call_shape=cached_return_value`
- `il_call_return.cpp::virtual_call` -> `virtual_dispatch=vtable_tail_call`

## Control Flow Graph

The lifter builds a CFG from lifted ops:
- Basic blocks split at branches, gotos, returns, dispatches
- Successor edges tracked (fall-through + branch targets)
- Loop back-edges detected (heuristic)
- Counted loops from bdnz

## Machine-Readable Shape Deltas

`compute_shape_delta()` compares lifted PPC against source IL:
- Operation count differences by category (arithmetic, bitwise, control_flow, etc.)
- PPC-only and IL-only operation names
- Switch shape comparison (table-based vs if-chain)
- Virtual call comparison
- Branch density comparison

## Target Facts Integration

Shape facts flow into `scripts/permuter/target_facts.py`:

| Shape Kind | Target Fact | Pattern Routing |
|-----------|------------|-----------------|
| byte_fusion | codegen_shape | boost/suppress `u8_to_unsigned_long` |
| bool_materialization | codegen_shape | boost `bool_materialize`, `signed_unsigned` |
| switch_dispatch | codegen_shape | boost/suppress `switch_if_convert` |
| prologue_shape (≥10 GPR) | codegen_shape | boost `variable_extraction` |
| prologue_shape (≥4 FPR) | codegen_shape | boost `signed_unsigned` |
| control_flow:counted_loop | codegen_shape | boost `foreach_to_dowhile` |

## Immediate Next Work

1. Expand call-family lifting:
   - distinguish vtable slot-load shape from generic indirect dispatch
   - distinguish `bctrl` call-return from `bctr` tail-dispatch wrappers
   - detect trivial forwarding wrappers and cached-return wrappers
2. Lift argument materialization:
   - inline argument formation vs temp extraction
   - stack spill / reload around call setup
   - literal and address materialization before calls
3. Widen switch lifting:
   - sparse / mixed switch lowering
   - default-hoist patterns
   - compare-chain families that currently collapse to generic CFG facts
4. Keep measuring permuter impact:
   - the default path now emits real `fact_boost_counts`
   - the next blocker is generator applicability, not shape extraction

## Current Measurement Note

On a real 5-function `scan_and_permute.py --json` slice
(`BinkReader::Poll`, `VorbisReader::~VorbisReader`, `UIList::SetProvider`,
`MemcardMgr::ThreadDone`, `DxMovie::SetFile`), the default-on shape-fact path
produced measurable routing signals:

- `switch_if_convert: 2`
- `tail_call_reorder: 1`
- `temp_elimination: 1`

That means the PPC->IL lifter is no longer just producing descriptive output.
The remaining problem is that some boosted pattern families still do not
generate useful enough variants on real ASTs. One concrete improvement already
landed on the permuter side: `tail_call_reorder` now handles terminal cleanup
wrappers, which unlocked destructor-style candidates that previously produced
zero variants, and a follow-up noise-trimming pass cut most infrastructure-only
tail-call proposals from the direct candidate corpus.

## Tests

- `msvc-src/tools/test_ppc_il_lifter.py`: 173 unit tests covering all instruction families, CFG, patterns, shape facts, deltas
- `scripts/permuter/tests/test_target_facts.py`: 34 tests including 9 new shape routing tests

## Non-Goals

- Inferring exact source-level `CAST` vs `AND` from PPC alone
- Reconstructing SSA or full expression trees
- Replacing objdiff or `/FAs` attribution
