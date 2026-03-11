# G3P2 Record-Form Fusion — Reverse Engineering Results

## Overview

G3P2 (`FUN_10c0f14e`) is pass group 3, pass 2 in c2.dll's optimization
pipeline. It converts `subf + compare-to-zero` pairs into `subf.`
(subtract-and-record) at the IL level. G5P10 (the PPC code generator) later
emits the actual `subf.` instruction.

Binary patching confirmed: disabling G3P2 removes `subf.` from output while
all other patterns remain.

## Architecture

### Dispatcher: `FUN_10c0f14e` (159 bytes)

```
record_form_dispatch(IL_state* state):
    FUN_10c04faf(state)              // pre-pass setup
    for each IL_node in state->body:
        if (node.has_line_info):
            update_debug_position()
        if (node.needs_processing):
            FUN_10c0d57e(state, node, &collect_list)  // worker
    if (collect_list != NULL):
        emit_collected(state, collect_list)
    FUN_10c05137(state)              // post-pass cleanup
    if (opt_level != 0 && !(state->flags & 0x40000)):
        call G2P2 (FUN_10b3668d)     // chain to algebraic pass
```

### Worker: `FUN_10c0d57e` (3899 bytes)

Massive opcode switch. For each IL node, examines the opcode and operands
to determine if record-form fusion is possible.

### Eligibility Checker: `FUN_10c123b9` (92 bytes)

Determines if a given IL opcode + constant can use record-form:

```c
bool record_form_eligible(int opcode, uint constant) {
    int op_class = opcode_class_table[opcode];  // DAT_10c39b18

    switch (op_class) {
        // These opcodes: record-form only when comparing against zero
        case 0x1a: case 0x1c: case 0x32:
        case 0x4a: case 0x4d: case 0x58: case 0x5a:
        case 0x3d:
            return constant == 0;

        // These opcodes: need aligned constant
        case 0x47: case 0x2e:
            return (constant & 3) == 0;

        // General case: constant must fit in signed 15-bit
        default:
            if ((constant & 0xFFFF8000) != 0 &&
                (constant & 0xFFFF8000) != 0xFFFF8000)
                return false;
            return true;
    }
}
```

## How Record-Form Fusion Works

### Step 1: G3P2 Marks IL Nodes

The worker function identifies patterns like:
```
SUBTRACT rA, rB    ; opcode 1
COMPARE rA, 0      ; opcode following
BRANCH_IF ...
```

And transforms them to:
```
SUBTRACT_AND_RECORD rA, rB    ; opcode changed to 0xB
BRANCH_IF ...                  ; compare eliminated
```

The opcode transformation map observed in the decompilation:
- Opcode `1` → `0xB` (subtract → subtract-and-record)
- Opcode `0x121` → `0x121` kept but with operand restructuring
- Opcode `0x181` → negated operand + `0xB`
- Opcode `0x26A` → `0x26C`

### Step 2: G5P10 Emits `subf.`

When G5P10 (the PPC code generator) sees the modified IL opcode (e.g., 0xB
instead of 1), it emits `subf.` instead of `subf + cmpwi 0`.

### Step 3: Xenon Scheduler May Reorder

The Xenon scheduler (`FUN_10b71d8f`) within G5P10 may reorder `subf.`
relative to other instructions for pipeline efficiency.

## Eligibility Rules

Record-form fusion requires ALL of:
1. The comparison is against a "compatible" constant (usually zero)
2. The preceding operation is an arithmetic/logical op that has a record-form
   variant in the PPC ISA
3. No intervening instructions between the operation and the compare that
   modify the same register

### Opcode Classes That Support Record-Form

From `FUN_10c123b9`, the opcode class table (`DAT_10c39b18`) maps IL opcodes
to internal classes. The classes that allow record-form:

| Class | Record-Form Condition | Likely PPC Instructions |
|-------|----------------------|------------------------|
| 0x1a, 0x1c, 0x32, 0x4a, 0x4d, 0x58, 0x5a, 0x3d | Only with zero | `subf.`, `add.`, `and.`, `or.`, `xor.`, etc. |
| 0x47, 0x2e | Aligned constant | Operations with immediate operands |
| Others | Signed 15-bit constant | General case |

## Source-Level Control

The key source-level pattern that triggers record-form fusion:

```cpp
// Generates subf. (record-form)
while (high - low >= 0)    // subtraction + compare-to-zero → fused

// Generates subf + cmpwi (separate)
while (high >= low)        // direct compare, no subtraction to fuse
```

This was proven empirically (Locale::FindDataIndex 97.2→100%) and confirmed
by G3P2 decompilation: the pass specifically looks for subtract+compare-zero
patterns in the IL.

## Key Addresses

| Address | Size | Name | Role |
|---------|------|------|------|
| `0x10c0f14e` | 159b | `record_form_dispatch` | G3P2 entry: iterates IL, calls worker |
| `0x10c0d57e` | 3899b | `record_form_worker` | Main opcode switch, transforms IL |
| `0x10c123b9` | 92b | `record_form_eligible` | Checks if opcode+constant can use record-form |
| `0x10c39b18` | — | `opcode_class_table` | Maps IL opcodes to internal classes |
| `0x10c0a2e2` | — | `record_form_helper` | Called for opcodes 0x26E-0x26F |
| `0x10c08e38` | — | `emit_compare_node` | Emits separate compare when fusion fails |

## Connection to G5P10

G3P2 and G5P10 form a two-stage pipeline for record-form instructions:

1. **G3P2** (IL-level): Identifies compare-after-arithmetic patterns, merges
   them into a single IL node with a modified opcode
2. **G5P10** (PPC codegen): Sees the modified opcode, emits `subf.` instead
   of `subf` + `cmpwi`

Disabling either stage removes `subf.` from the output:
- G3P2 disabled: IL not marked → G5P10 emits separate `subf` + `cmpwi`
- G5P10 disabled: No PPC code emitted at all

## Implications for DC3 Decomp

1. **`subf.` is source-controllable**: Write `while (a - b >= 0)` instead of
   `while (a >= b)` to trigger fusion. Proven on Locale::FindDataIndex.

2. **Record-form only works with zero comparison**: For most opcode classes,
   the constant must be exactly 0. `while (a - b >= 1)` will NOT produce
   `subf.`.

3. **G3P2 chains to G2P2**: After record-form fusion, G3P2 calls the
   algebraic pass (`FUN_10b3668d`) if optimization is enabled. This means
   record-form decisions interact with algebraic simplifications.

4. **The `.` suffix on any PPC instruction** (not just `subf.`) comes from
   the same mechanism. `add.`, `and.`, `or.`, etc. are all produced by G3P2
   marking + G5P10 emitting.
