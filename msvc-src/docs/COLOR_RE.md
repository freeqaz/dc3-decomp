# COLOR Register Allocator — Reverse Engineering Results

## Overview

The COLOR register allocator lives in `regasg.c` (source path from assert:
`e:\bt\278379\vctools\compiler\be\p2\regasg.c`). Entry point at `0x10bc6487`
in c2.dll.

**Key finding**: MSVC's PPC register allocator is **NOT** graph coloring
despite the "COLOR" name. It is a **linear scan** allocator with priority
tables, interference sets, and a spill-on-conflict fallback.

## Architecture

### Entry Point: `color_init` (0x10bc6487, 23 bytes)

```
color_init(IL_state* state):
    lock()                          // FUN_10c1b7ef
    color_dispatch(state)           // FUN_10bc62b6
    unlock(state)                   // FUN_10c1b808
```

### Dispatcher: `color_dispatch` (0x10bc62b6, 465 bytes)

1. **Clears register state**: `memset(reg_state, 0, 0x594)` — 1428 bytes = 357 registers × 4 bytes
2. **Allocates interference bitsets** for each register (up to 261 + 96 VMX = 357)
3. **Selects allocation order table** based on calling convention and optimization flags:
   - `DAT_10c3de20 == 1` → convention-specific variant
   - `DAT_10c6fd9c == 2` → Xenon variant
   - `DAT_10c2e980` → additional flag
   - Result stored in `DAT_10c6fdf4` (pointer to selected order table)
4. **Selects allocation strategy**:
   - `(flag1 == 0 || flag2 != 0) && !(state->flags & 0x20)` → `color_alloc_simple` (0x10bc514a)
   - Else → `color_alloc_complex` (0x10bc5494)
5. **Iterates IL nodes** in a linked list, calling:
   - `color_process_node` (0x10bc4ded) — builds interference data
   - `color_assign_regs` (0x10bc61bb) — assigns physical registers

### Register State Buffer

- **Base**: `DAT_10c3d730` (1428 bytes, zeroed per function)
- **Layout**: `int reg_state[357]` — one slot per physical register
- **Index 0x21 (33)**: special skip (CR? LR?)
- **0 = free**, non-zero = pointer to the virtual register occupying it
- **Register descriptor table**: `DAT_10c2f088`, stride 0x60 (96 bytes per register)
  - Offset 0x10: type field (bits 15:12):
    - `1` = GPR (regs 1-32)
    - `5` = FPR (regs 34-65)
    - `0xc` = VMX (regs 229-268)

## Allocation Order Tables — THE KEY FINDING

Six GPR allocation variants exist. All share the same structure:

**Phase 1: Volatile (caller-saved) first**
```
r12, r11, r10, r9, r8, r7, r6, r5, r4
```

**Phase 2: Callee-saved (non-volatile) — r31 FIRST, descending**
```
[r32?], r31, r30, r29, r28, ..., r14 or r13
```
The exact cutoff varies by table variant (r17, r15, r14, or r13).

**Phase 3: Volatile again (spill candidates)**
```
r12, r11, r10, ..., r4
```

**Phase 4: Callee-saved again (wrap-around / second chance)**
```
[r32], r31, r30, ...
```

### Table Variant Selection

The dispatcher selects which table to use based on:
- `DAT_10c3de20` — optimization level (1 = /O1, 2 = /O2)
- `DAT_10c6fd9c` — target platform (2 = Xenon)
- `DAT_10c2e980` — calling convention flag

| Condition | Table | Notable Difference |
|-----------|-------|-------------------|
| O1 + !Xenon + flag | variant 1 (0x10c3b768) | callee-saved down to r17, r32 before r31 |
| O1 + !Xenon + !flag | variant 2 (0x10c3b7d0) | callee-saved down to r17, no r32 before r31 |
| O2 + Xenon + flag | variant 3 (0x10c3b6a0) | callee-saved down to r18, r32 before r31 |
| O2 + Xenon + !flag | variant 4 (0x10c3b5c0) | callee-saved down to r15, r32 before r31 |
| O2 + !Xenon + !flag | variant 5 (0x10c3b630) | callee-saved down to r15, no r32 before r31 |
| O2 + !Xenon + flag | variant 6 (0x10c3b708) | callee-saved down to r18, no r32 before r31 |

### DC3 Uses Variant 3 or 6

DC3 compiles with `/O2` (optimization level 2) for Xbox 360 (Xenon). The
calling convention flag determines which table is used.

**Confirmed**: Callee-saved allocation is **r31-first, descending** in ALL
table variants. This matches our diff-test finding from N=2..15 variable tests.

### FPR Allocation Order

FPR table (`DAT_10c3bee0`):
```
f0(34), f13(47), f12(46), ..., f1(35)    [volatile first, descending]
f31(65), f30(64), ..., f14(48)            [callee-saved, f31 first descending]
```

This also confirms: FPR allocation is independent of GPR, f31-first descending.

### VMX128 Allocation Order

VMX table (`DAT_10c3b8c0`):
```
vr101(229), vr114(242), vr113(241), ..., vr102(230)  [volatile first]
vr132(260), vr131(259), ..., vr115(243)                [callee-saved]
vr133(261), vr134(262), ...                            [extended VMX128]
```

## Register Selection Algorithm: `color_select_reg` (0x10bc58d5, 1891 bytes)

This is the largest function in the allocator. It selects a physical register
for a virtual register operand.

### Algorithm (simplified):

1. **Try coalescing**: If the virtual reg has a "hint" (from copy propagation,
   stored at offset 0x30), try the hinted physical register first. Success if:
   - The hinted register is free (`reg_state[hint] == 0`)
   - No interference exists (checked via `FUN_10b26f37` on bitset)

2. **Try VMX range** (if register class = 0xc and VMX is enabled):
   - Walk the VMX order table, pick first free + non-interfering

3. **Walk the primary allocation order table** (selected by dispatcher):
   - For each register in the table:
     - Check `reg_state[reg] == 0` (free)
     - Check no interference with current virtual reg
     - On first success → assign and return
   - The table is walked with **position tracking** (`DAT_10c6fe0c`, `DAT_10c6fe08`, `DAT_10c6fe10`):
     the current position advances, so successive allocations continue from where the last one stopped.
     This creates the **round-robin / advancing pointer** behavior.

4. **Spill fallback** (if no free register found):
   - Walk the table again, compute **spill cost** for each occupied register
   - `FUN_10bc4be9` computes the cost as the number of IL nodes until the
     next use of the currently-assigned virtual register (distance to next use)
   - Pick the register with the highest cost (longest until next use = cheapest to spill)
   - Call `FUN_10bc4eae` to generate spill/reload code

### Critical Insight: Advancing Pointer

The position pointers (`DAT_10c6fe0c`, `DAT_10c6fe08`, `DAT_10c6fe10`) track
where in the allocation order table the last assignment was made. The next
allocation continues from that point, wrapping around.

This means:
- **First allocated variable gets r31** (first callee-saved after volatiles are used)
- **Second gets r30**, etc.
- But if a volatile reg frees up (dead variable), it gets reused before advancing
  into more callee-saved regs

This is linear scan behavior, NOT graph coloring. The advancing pointer is the
mechanism that creates the "first-declared = r31" pattern we observed in
differential testing.

## Spill Cost: `color_spill_cost` (0x10bc4be9, 220 bytes)

The spill cost calculator walks forward in the IL from the current node, counting
nodes until the next use of the register being considered for spilling.

```c
int color_spill_cost(IL_node* current, int phys_reg, IL_node** next_use_out) {
    int cost = 0;
    IL_node* node = current->next;
    while (node->type != END_MARKER) {
        cost++;
        if (node has instruction operand referencing phys_reg &&
            live ranges overlap) {
            break;
        }
        node = node->next;
    }
    *next_use_out = node;
    return cost;
}
```

Longer distance = higher cost = BETTER candidate for spilling (Belady's optimal
replacement variant).

## Conflict Resolution: `color_resolve_conflict` (0x10bc6038, 387 bytes)

When a register conflict is detected (a virtual reg needs a physical reg that's
occupied), this function:

1. Checks if the conflict can be resolved by coalescing/moving
2. If not, generates spill code for the occupant
3. Clears the register state slot
4. Re-runs `color_select_reg` for the displaced virtual register

## NO Graph Coloring / BSF

**The BSF (Best So Far) graph coloring hypothesis is DISPROVED.**

The allocator:
- Has no interference graph construction
- Has no graph coloring loop (simplicial elimination, Chaitin-Briggs, etc.)
- Uses a simple linear scan with an advancing pointer into a priority table
- Falls back to spilling based on distance-to-next-use

The "~7 variable" threshold we observed is NOT a BSF trigger. It's likely an
artifact of:
- Volatile registers running out (9 volatile GPRs: r4-r12)
- The advancing pointer wrapping around in the callee-saved range
- Interference from compiler temporaries consuming volatile regs

## Implications for the Permuter

1. **Register allocation is fully deterministic**: given the same IL sequence,
   the same physical registers will be assigned. No randomness.

2. **Declaration order matters because of the linear scan**: first live range
   to reach the allocation point gets the first available register (r31 for
   callee-saved).

3. **Compiler temporaries consume registers before user variables**: loop
   counters, vtable lookups, and intermediate values claim volatile regs first,
   pushing user variables into callee-saved regs.

4. **The spill cost formula is simple**: distance to next use. This means we
   CAN predict when spills will occur and which register gets spilled.

5. **No BSF → no graph coloring worklist ordering differences**: the register
   swap mismatches we see between our build and the target are NOT from
   different graph coloring worklist orders. They're from differences in:
   - IL node ordering (different source → different IL → different allocation order)
   - Compiler temporary creation (inlining decisions change which temps exist)
   - Live range lengths (different code shapes → different interference patterns)

## Source File Reference

From the assertion in `color_select_reg`:
```
e:\bt\278379\vctools\compiler\be\p2\regasg.c
```

This is the register assignment module in MSVC's backend (`be`) phase 2 (`p2`).
Build number 278379 matches the Xbox 360 MSVC 16.00.11886 toolchain.
