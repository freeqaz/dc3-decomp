# RhythmBattlePlayer Objdiff/XCOFF Analysis (2026-02-04)

## Goal
Investigate why `RhythmBattlePlayer::RhythmBattlePlayer()` is stuck at 99.5% and understand
whether the remaining diffs are source-fixable or relocation-only (tooling/format related).

## What We Ran
We used MCP objdiff (`mcp__Decomp_Orchestrator__run_objdiff`) on:
- `??0RhythmBattlePlayer@@IAA@XZ` (protected constructor)

**Advanced objdiff features used:**
- **Instruction summary + verdict**: MCP `run_objdiff` returns match %, diff score, and a
  verdict classification (e.g., NEEDS_INVESTIGATION). This helped confirm there were no
  control-flow or register-swap patterns — only `diff_arg`.
- **Instruction-level mismatches** (`--include-instructions` in CLI runs): we compared the
  exact mismatching instructions and verified they were all symbol/relocation operands
  (`lis/addi/lfs/lwz` pointing at vtables or const pool).
- **Config overrides** (`-c functionRelocDiffs=...` in CLI):
  - `functionRelocDiffs=none` → 100% match (proves relocation-only).
  - `functionRelocDiffs=data_value` → reduced diff_arg count (grouped by data value).
  This allowed us to partition which diffs were pure symbol-name/address differences.
- **Pool relocation toggle** (`-c ppc.calculatePoolRelocations=false` in CLI):
  we checked whether pooled constants were responsible for the diffs (they weren’t; the
  diffs remained, which points back to symbol identity rather than pool mechanics).

## What “function relocation diffs” means (objdiff core)
This setting controls how objdiff compares relocation operands (vtable/RTTI/const pool
symbol references) when deciding whether two instruction arguments “match.”

The logic lives in `objdiff-core/src/diff/code.rs` (`reloc_eq` and `arg_eq`):
- **`FunctionRelocDiffs::None`**: relocations are *relaxed*. If either side is missing a
  reloc or the reloc targets differ, objdiff still treats the operand as matching.
  This is why we got 100% when we set it to `none`: the underlying opcodes were identical,
  and all differences were relocation targets.
- **`FunctionRelocDiffs::NameAddress` (default)**: relocations must match by symbol name
  and addend, or by exact address (if the section names also match). This is the strict mode
  that preserves vtable/RTTI/const-pool identity and is why we see `diff_arg`.
- **`FunctionRelocDiffs::DataValue`**: relaxes symbol identity in some cases by comparing
  the *resolved data value* of data literals. This is why the diff_arg count dropped from 36
  to 22: some symbol name mismatches resolve to the same data contents.

Important detail from the code path:
- If relocation flags don’t match, objdiff treats them as different.
- If section names differ, relocations can’t match (even if symbols are similar).
- For **object/data symbols**, `NameAddress` mode also compares the literal data displayed
  in the instruction (`display_ins_data_literals`) to determine if a literal load should
  be treated as equivalent.

## What “pool relocation toggle” means (PPC backend)
PowerPC compilers often pool constants (floats, symbols, addresses) into a shared data area.
Those pools are accessed by `lis`/`addi` (to load the pool base) followed by loads using
offsets from that base. These intermediate loads **may not have real relocations**, because
only the pool base is relocated.

objdiff handles this by generating **fake pool relocations** in
`objdiff-core/src/arch/ppc/mod.rs`:
- It analyzes instruction flow, tracks which registers hold pool base addresses, and
  synthesizes relocations (`R_PPC_NONE`) for the load instructions that reference pooled
  data.
- These show up in diffs as `<symbol>` relocations and can be toggled with
  `ppc.calculatePoolRelocations`.

So:
- **`ppc.calculatePoolRelocations=true` (default)**: objdiff attempts to show pooled
  constants as relocations, making diffs more semantically meaningful.
- **`ppc.calculatePoolRelocations=false`**: objdiff stops generating those fake relocs,
  which can reduce noise if pooled constants are unstable.

In our case, turning off pool relocation calculation **did not remove the diffs**, which
indicates the remaining mismatches are not just pooled constant analysis artifacts. They are
true relocation identity differences (vtable/RTTI symbols, gNullStr, and string literals).

The default MCP objdiff output (no extra config) reported:
- 99.5% match
- 36 `diff_arg` instructions
- 0 control-flow or opcode mismatches

This means the instruction stream is identical, but some operands differ.
In this case those operands are symbol relocations (vtables, RTTI, constant pool entries).

## Why This Looked Like Relocation-Only
The `diff_arg` list was exclusively symbol loads and constant pool loads:
- vtables: `??_7RndPollable@@6B0@@`, `??_7RndPollable@@6BObject@Hmx@@@`,
  `??_7RhythmBattlePlayer@@6BRndPollable@@@`, `??_7RhythmBattlePlayer@@6BObject@Hmx@@@`
- ObjPtr vtables: `??_7?$ObjPtr@VRndAnimatable@@@@6B@`, `??_7?$ObjPtr@VRndDir@@@@6B@`, etc.
- constants: `__real@00000000`, `?gNullStr@@3PBDB`, `??_C@_04CGFJFPFD@none?$AA@`

These are classic relocation-only differences. The underlying opcodes and register flow match.

## How We Validated "Relocation-Only"
We re-ran objdiff with `functionRelocDiffs` changes (CLI form was used in-shell;
MCP does not expose `-c` flags directly yet):

1. `functionRelocDiffs=none`
   - This tells objdiff to ignore relocation targets when diffing.
   - Result: **100% match**.
   - Interpretation: the codegen is identical; only reloc targets differ.

2. `functionRelocDiffs=data_value`
   - This compares relocations by their underlying data value rather than symbol name.
   - Result: diff_arg count dropped from 36 → 22.
   - Remaining diffs were still symbol relocations: mostly vtables and const pool loads.

This confirmed the mismatch is in **symbol/relocation resolution**, not in algorithm or control flow.

## Why XCOFF Matters Here
The project uses **XCOFF** object files (Xbox 360 toolchain), not ELF/COFF.
We attempted to inspect object files with:
- `llvm-readobj`
- `llvm-objdump`

Both failed with "file not recognized", which is consistent with XCOFF incompatibility.
That means we can’t easily introspect relocations and sections with standard LLVM tools.

So, objdiff is currently the most reliable way to surface relocation-level differences.

## What This Implies For Fixing 99.5%
If all mismatches are relocation-only, the path to 100% is **not** in the ctor body.
Instead, we need to align how vtables/RTTI and constant pools are emitted.

Main suspects:
- **Key function emission** (inline vs out-of-line) for `RndPollable` or ObjPtr templates.
- **Virtual inheritance layout** (RndPollable is `virtual Hmx::Object`).
- **Placement/definition units** (which `.obj` owns the vtable/RTTI and const pool symbols).

If we can make those vtable/RTTI symbols resolve to the same addresses,
the ctor relocations will match.

## What We Were Looking For
We were trying to determine:
1. Are we actually calling/initializing the same functions and vtables?
2. Are the remaining mismatches just relocation address/name differences?
3. Can a change in class declaration (virtuals, out-of-line key functions) stabilize vtable/RTTI?

The evidence strongly indicates:
- The ctor logic is already correct.
- The remaining 0.5% is dominated by relocation identity, not codegen.

## Practical Next Steps
1. **Inspect `RndPollable` key function emission**
   - Compare against map file or Ghidra to see if vtable/RTTI should be emitted in `Poll.obj`.
   - If our key functions are missing or inlined, define out-of-line versions.

2. **Inspect ObjPtr template instantiations**
   - Check if ObjPtr vtables are being emitted in unexpected units.
   - Differences in where template instantiations are emitted can shift symbol addresses.

3. **Treat as "reloc-only" if no fixable emission issue**
   - If vtable/RTTI emission is already aligned and diffs remain relocation-only,
     the function is likely at-limit due to object format/linker symbol placement.

## Summary
We used objdiff to show that `RhythmBattlePlayer::RhythmBattlePlayer` is a **relocation-only mismatch**.
Changing constructor code will not fix the remaining 0.5%. The only way to push to 100%
is to stabilize vtable/RTTI/const pool emission, which likely requires class-level or
linkage-level adjustments rather than local logic tweaks.
