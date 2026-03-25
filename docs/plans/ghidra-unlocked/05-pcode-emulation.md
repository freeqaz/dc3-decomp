# 05 — P-Code Emulation for Behavioral Testing

Priority: **Tier 1**  
Readiness: **Spike required**  
Effort: **High**

## Why This Matters

This can become a powerful “behavioral confidence” tool for functions that are semantically right but structurally stubborn.

## What The Review Found

The capability is real, but the current plan was too broad for a first pass.

### Relevant APIs

From `../ghidra`:

- newer emulation APIs under `ghidra.pcode.emu.*`
- `EmulatorHelper` in `ghidra.app.emulator.EmulatorHelper`

### Important constraint

`EmulatorHelper` is marked deprecated in Ghidra 12.1. It is still attractive for a quick program-backed spike because it already gives us:

- register read/write
- memory read/write
- stack helpers
- breakpoints
- execution-address tracking

But it should not be treated as the long-term architecture without proving the workflow first.

## Recommended Scope

### V1 spike only

Support only:

- executing a single target-program function
- manually specified register arguments
- optional seeded memory blocks
- return-value and selected-register inspection
- simple termination via LR sentinel or breakpoint

Do not include in V1:

- target vs compiled object differential execution
- automatic test-vector generation
- broad call-stub libraries
- bulk behavioral campaigns

## Recommended Plan

### Phase 0 — Leaf-function feasibility

Pick a small leaf or near-leaf function and prove we can:

- set registers
- seed stack and object memory
- run until return
- inspect result registers and touched memory

If that fails cleanly, stop. Do not expand scope.

### Phase 1 — CLI spike

Add:

- `tools/ghidra/pcode_emu.py`

Likely build the first version on `EmulatorHelper` for ergonomics.

Output:

- completion status
- steps executed
- selected registers
- memory ranges requested by the caller
- breakpoint/termination reason

### Phase 2 — Stubbing model

Only after V1 works:

- define how external calls are trapped and stubbed
- decide whether we stay on `EmulatorHelper` or migrate behind a small execution abstraction toward `PcodeEmulator`

## Design Notes

- Keep this out of the MCP server at first. A standalone spike script is lower risk and easier to iterate.
- Differential testing against compiled `.obj` files is a separate project. Loading, relocations, and environment setup make it materially harder than target-only execution.
- This feature should complement Unicorn, not replace it.

## Acceptance Criteria

- A documented spike can execute at least one real DC3 function end-to-end.
- The output is stable enough to compare return registers across repeated runs.
- Limitations are written down before any “behavioral equivalence” claims are made.
