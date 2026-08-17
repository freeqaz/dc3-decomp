# Unicorn Function Runner — Phase 1 Design

## 1. Overview

Phase 1 delivers a working differential function runner: extract a function from both the decomp and original .obj files, load each into Unicorn PPC32 BE, execute with mocked externals, and compare output state.

**What Phase 1 delivers:**
- COFF extraction from both .obj flavors (decomp multi-symbol sections, original COMDAT sections)
- Relocation patching for all 5 relocation types (REL24, REFHI, REFLO, PAIR, ADDR32)
- Trampoline-based call mocking with call log recording
- Execution-sequence call comparison with best-effort symbol identification
- Auto-fixture mode: zeroed object memory, all mocks return 0
- CLI interface for single-function comparison

**What Phase 1 does NOT deliver:**
- YAML fixture system (manual object/mock configuration) — Phase 2
- Batch mode (all functions in a .obj) — Phase 2
- Permuter integration — Phase 2
- `bctrl` / `bctr` / virtual dispatch / switch table handling — Phase 2
- Intra-TU call execution — Phase 2

**Parent doc:** [docs/tools/UNICORN_FUNCTION_RUNNER.md](../tools/UNICORN_FUNCTION_RUNNER.md)

---

## Implementation Status

**Phase 1 is implemented** (Feb 11, 2026). All 8 modules created, 1021 lines total.

### Design Deviations

1. **Memory map changed**: `TRAMPOLINE_BASE` moved from `0x40000000` to `0x80010000`. The original layout had a 1GB gap between code (`0x80000000`) and trampolines (`0x40000000`), exceeding REL24's ±32MB branch range. Trampolines are now placed immediately after code.

2. **Map-on-demand for unmapped memory**: Instead of stopping on unmapped access (design Section 7.4), the engine maps new 4KB pages dynamically with zeroed content. This allows both sides to continue execution through null pointer dereferences — both see identical zeroed pages, preserving equivalence testing validity.

3. **PPC64 instruction detection**: Added `has_ppc64_insns()` scanner for `std`/`ld` (opcodes 58/62). The Xbox 360 Xenon CPU is a PPC64 chip in 32-bit compat mode, so MSVC uses these 64-bit load/store instructions for callee-saved register preservation. Unicorn PPC32 mode doesn't support them (raises `UC_ERR_EXCEPTION`), so functions using them were skipped in Phase 1. **Phase 2 resolves this** via byte-rewriting `std→stw` and `ld→lwz` (see below).

### Phase 2 Updates (Feb 11, 2026)

4. **PPC64 instruction rewriting**: `rewrite_ppc64_insns()` in `patcher.py` replaces `std` (opcode 62, DS-form) with `stw` (opcode 36, D-form) and `ld` (opcode 58) with `lwz` (opcode 32). Same-size 4B→4B replacement. Both sides get identical rewriting, preserving equivalence testing validity. Unblocked ~1,079 functions across all .obj files.

5. **Batch mode**: `--batch` runs all eligible functions in a unit. `--batch-all` iterates all units in objdiff.json that have both target and base paths.

### Test Results

- **Phase 1**: 19/19 functions tested EQUIVALENT across `Skeleton.obj` and `FileChecksum.obj`
- **Phase 2**: `Skeleton.obj` batch: 22 equivalent, 8 divergent, 0 errors, 0 skipped (30 total, up from 22 eligible)
- Tested: leaf functions, REL24 calls, REFHI/REFLO globals, float operations, different byte sizes between sides
- Exit codes verified: 0=EQUIVALENT, 1=DIVERGENT, 2=ERROR, 3=SKIPPED
- `--list-functions` on `Skeleton.obj`: 30 eligible (was 22), 2 skipped (bctrl only)

---

## 2. Architecture

```
         objdiff.json
              │
              │  unit config: target_path, base_path
              v
    ┌─────────────────────┐
    │   COFF Extractor     │
    │                      │
    │  Decomp .obj ────────┼──► function bytes + relocations
    │  (obj/ path)         │
    │                      │
    │  Original .obj ──────┼──► function bytes + relocations
    │  (src/ path)         │
    └──────────┬───────────┘
               │
               │  (bytes, relocs) × 2
               v
    ┌─────────────────────┐
    │  Relocation Patcher  │
    │                      │
    │  For each side:      │
    │  - Map external      │
    │    symbols to        │
    │    Unicorn addresses │
    │  - Patch instruction │
    │    fields in-place   │
    └──────────┬───────────┘
               │
               │  patched bytes × 2
               v
    ┌─────────────────────┐         ┌─────────────────────┐
    │  Unicorn Instance    │         │  Unicorn Instance    │
    │  (Decomp)            │         │  (Original)          │
    │                      │         │                      │
    │  Execute function    │         │  Execute function    │
    │  Record call log     │         │  Record call log     │
    │  Capture output      │         │  Capture output      │
    └──────────┬───────────┘         └──────────┬───────────┘
               │                                │
               └──────────┬─────────────────────┘
                          v
               ┌─────────────────────┐
               │    Comparator        │
               │                      │
               │  Sequence call match │
               │  r3 return value     │
               │  Modified memory     │
               └──────────┬───────────┘
                          v
                EQUIVALENT / DIVERGENT
```

### Data Flow

1. **Input**: mangled symbol name + unit name (or .obj paths directly)
2. **Extract**: pull function bytes and relocations from both .obj files
3. **Patch**: rewrite relocation sites to point at our Unicorn address space (each side gets its own trampoline map)
4. **Execute**: run both functions with identical initial state
5. **Compare**: execution-sequence call comparison + return value + memory diffs

---

## 3. COFF Extraction

Both sides produce COFF `.obj` files with machine type `0x01F2` (`IMAGE_FILE_MACHINE_POWERPCBE`). The existing `COFFParser` from Phase 0 (`scripts/unicorn_runner/research.py`) handles both flavors.

### 3.1 Decomp .obj (target_path)

**Path pattern**: `build/373307D9/obj/{unit}.obj`

Structure: few large `.text` sections containing many functions.

**Extraction algorithm:**
1. Look up the mangled symbol name in the COFF symbol table
2. Get the symbol's section index and offset within that section
3. Determine function size: find the next symbol in the same section with a higher offset; the function extends from `symbol.value` to `next_symbol.value`
4. Read bytes from `section_data[symbol.value : next_symbol.value]`
5. Filter section relocations: keep only those where `reloc.offset >= symbol.value` and `reloc.offset < next_symbol.value`
6. Adjust relocation offsets to be function-relative: `reloc.offset -= symbol.value`

**Note on `$M` labels**: decomp relocations reference compiler-internal symbols like `$M1234` instead of real function/variable names. Comparison uses execution-sequence matching (Section 8) which is immune to this. Symbol names are resolved via best-effort offset matching for diagnostics only (Section 3.4).

### 3.2 Original .obj (base_path)

**Path pattern**: `build/373307D9/src/{unit}.obj`

Structure: one COMDAT `.text` section per function (hundreds of sections per .obj).

**Extraction algorithm:**
1. Look up the mangled symbol name in the COFF symbol table
2. The symbol's section IS the function — one COMDAT section per function
3. The symbol's `value` field gives the code start offset within the section. This is typically 0, but can be 8+ when the section has a C++ exception handling header (two ADDR32 words at offset 0-7 that point to `__ehfuncinfo` and catch handlers)
4. Function size = `section.raw_size - symbol.value`
5. Read bytes from `section_data[symbol.value:]`
6. Filter section relocations: keep only those where `reloc.offset >= symbol.value`
7. Adjust relocation offsets to be function-relative: `reloc.offset -= symbol.value`

**Note on EH headers**: ~95% of ADDR32 relocations in `.text` sections are at offset 0 or 4, belonging to EH headers before the function code. These are automatically excluded by starting extraction at `symbol.value`.

### 3.3 Relocation Filtering

Both extraction paths produce a list of relocations within the function's byte range. Each relocation entry contains:

```python
{
    "offset": int,        # byte offset within function (where to patch)
    "symbol_index": int,  # COFF symbol table index
    "symbol_name": str,   # resolved symbol name
    "type": int,          # relocation type code
    "type_name": str,     # human-readable type name
}
```

### 3.4 Symbol Resolution for Call Logging

Each side builds its own trampoline map independently. The original's trampolines have real symbol names; the decomp's have `$M` labels. For reporting and diagnostics, we resolve decomp `$M` labels to real names via **best-effort offset matching**: if the decomp has a REL24 at the same function-relative offset as the original, we know they reference the same external function and can label the decomp's trampoline with the original's symbol name.

Offset matching is not required for the primary comparison (which uses execution-sequence matching — see Section 8). It only improves diagnostic output.

---

## 4. Relocation Patching Algorithm

Both sides get the same patching algorithm. We ignore whatever values are currently in the instruction fields (decomp has zeros, original has stale linker residue) and overwrite with our Unicorn-mapped addresses.

### 4.1 Address Assignment

Before patching, assign Unicorn addresses to all relocation targets:

```python
def assign_addresses(relocs):
    trampolines = {}   # symbol_name -> trampoline address
    globals_map = {}    # symbol_name -> global slot address
    next_trampoline = TRAMPOLINE_BASE
    next_global = GLOBAL_BASE

    for reloc in relocs:
        sym = reloc["symbol_name"]
        if reloc["type_name"] == "REL24":
            if sym not in trampolines:
                trampolines[sym] = next_trampoline
                next_trampoline += 8   # each stub is 8 bytes
        elif reloc["type_name"] in ("REFHI", "REFLO"):
            if sym not in globals_map:
                globals_map[sym] = next_global
                next_global += 4       # each global is 4 bytes
        elif reloc["type_name"] == "ADDR32":
            if sym not in globals_map:
                globals_map[sym] = next_global
                next_global += 4

    return trampolines, globals_map
```

### 4.2 REL24 — Branch-and-Link (`bl target`)

Encodes a 24-bit signed PC-relative offset in instruction bits [6:29].

```python
def patch_rel24(code, offset, trampoline_addr, code_base):
    insn = struct.unpack_from(">I", code, offset)[0]
    pc = code_base + offset
    delta = trampoline_addr - pc

    assert -0x2000000 <= delta <= 0x1FFFFFC, f"REL24 out of range: {delta}"

    # Clear bits [6:29], preserve opcode (bits [0:5]) and AA/LK (bits [30:31])
    insn = (insn & 0xFC000003) | (delta & 0x03FFFFFC)
    struct.pack_into(">I", code, offset, insn)
```

### 4.3 REFHI — Upper 16 bits (`lis rN, sym@ha`)

Patches instruction bits [16:31] with the **high-adjusted** half of the target address. The `@ha` adjustment adds 1 to the high half if bit 15 of the address is set (sign extension compensation).

```python
def patch_refhi(code, offset, target_addr):
    insn = struct.unpack_from(">I", code, offset)[0]
    ha = (target_addr >> 16) + ((target_addr & 0x8000) >> 15)
    insn = (insn & 0xFFFF0000) | (ha & 0xFFFF)
    struct.pack_into(">I", code, offset, insn)
```

### 4.4 REFLO — Lower 16 bits (`addi/lwz rN, rN, sym@l`)

Patches instruction bits [16:31] with the low 16 bits of the target address.

```python
def patch_reflo(code, offset, target_addr):
    insn = struct.unpack_from(">I", code, offset)[0]
    lo = target_addr & 0xFFFF
    insn = (insn & 0xFFFF0000) | lo
    struct.pack_into(">I", code, offset, insn)
```

### 4.5 PAIR — Metadata Only

PAIR relocations always follow a REFHI or REFLO and reference `@comp.id`. They carry no patching information — skip them.

```python
def patch_pair(code, offset):
    pass  # No-op
```

### 4.6 ADDR32 — Absolute 32-bit Address

Patches a full 32-bit word with the mapped address.

```python
def patch_addr32(code, offset, target_addr):
    struct.pack_into(">I", code, offset, target_addr & 0xFFFFFFFF)
```

**Note on ADDR32 in `.text` sections**: ~95% of ADDR32 relocations in `.text` are C++ exception handling headers at offset 0-7 of COMDAT sections (pointing to `__ehfuncinfo` and catch handlers). These are automatically excluded during extraction because we start reading at `symbol.value` (Section 3.2). The remaining ADDR32 in `.text` follow the same EH pattern. Real switch/jump tables live in `.rdata` sections, not `.text`.

### 4.7 Patching Pipeline

```python
def patch_function(code_bytearray, relocs, trampolines, globals_map, code_base):
    for reloc in relocs:
        sym = reloc["symbol_name"]
        off = reloc["offset"]
        rtype = reloc["type_name"]

        if rtype == "REL24":
            target = trampolines[sym]
            patch_rel24(code_bytearray, off, target, code_base)
        elif rtype == "REFHI":
            target = globals_map[sym]
            patch_refhi(code_bytearray, off, target)
        elif rtype == "REFLO":
            target = globals_map[sym]
            patch_reflo(code_bytearray, off, target)
        elif rtype == "PAIR":
            pass
        elif rtype == "ADDR32":
            target = globals_map[sym]
            patch_addr32(code_bytearray, off, target)
        else:
            raise ValueError(f"Unknown relocation type: {rtype}")
```

---

## 5. Memory Map

All regions are page-aligned (0x10000 = 64KB). No regions overlap.

```
Address Range              Size    Purpose
─────────────────────────────────────────────────────────
0x10000000 - 0x1000FFFF   64KB    Stack
0x20000000 - 0x2000FFFF   64KB    Object memory (this pointer region)
0x30000000 - 0x3000FFFF   64KB    Globals (one 4-byte slot per unique REFHI/REFLO/ADDR32 target)
0x80000000 - 0x8000FFFF   64KB    Code region (function bytes loaded here)
0x80010000 - 0x8001FFFF   64KB    Trampolines (one 8-byte stub per unique REL24 target)
0xDEAD0000                ---     Sentinel (unmapped — LR target, triggers return detection)
```

> **Note**: Trampolines must be within ±32MB of code for REL24 branch range.

### Region Details

**Stack (0x10000000)**: r1 initialized to `0x10008000` (middle of region). Standard PPC stack grows downward. 32KB headroom in each direction.

**r2 (TOC pointer)**: Initialized to 0. The MSVC Xbox 360 compiler does not use TOC-relative addressing — zero TOCREL16 relocations across the entire codebase. All address materialization uses REFHI/REFLO (`lis`+`addi`) pairs instead. Confirmed via analysis of 6.6M instructions across 3,194 .obj files.

**Object memory (0x20000000)**: r3 (this pointer) set to `0x20000000`. In auto-fixture mode, entire region initialized to zero. Functions read/write member fields at offsets from this base.

**Globals (0x30000000)**: Each unique symbol referenced by REFHI/REFLO or ADDR32 relocations gets a consecutive 4-byte slot starting at `0x30000000`. Initialized to zero in auto-fixture mode. A `lis rN, sym@ha` + `lwz rX, sym@l(rN)` pair will resolve to the correct slot.

**Trampolines (0x40000000)**: Each unique REL24 target gets an 8-byte stub. See Section 6.

**Code (0x80000000)**: Function bytes loaded at base. Address matches typical XEX load address range. Code region is large enough for any single function (64KB >> largest functions at ~16KB).

**Sentinel (0xDEAD0000)**: Not mapped. LR initialized to this address. When the function executes `blr`, the CPU fetches from `0xDEAD0000`, triggering `UC_ERR_FETCH_UNMAPPED`, which we catch as normal return.

---

## 6. Trampoline System

### 6.1 Stub Format

Each trampoline is 8 bytes (2 instructions):

```asm
li r3, 0        # 38 60 00 00 — return 0 (auto-fixture default)
blr             # 4E 80 00 20 — return to caller
```

In auto-fixture mode, all stubs return 0 in r3. Future phases will support per-symbol mock return values.

### 6.2 Call Logging

A `UC_HOOK_CODE` callback is registered on the trampoline region (`begin=0x40000000`, `end=0x4000FFFF`). When execution enters any trampoline stub, the hook records:

```python
call_entry = {
    "call_index": int,        # sequential call number (0-indexed)
    "args": {
        "r3": int,
        "r4": int,
        "r5": int,
        "r6": int,
    },
    "trampoline_addr": int,   # which stub was hit (for diagnostics, not comparison)
    "source_offset": int,     # function-relative offset of the bl that called us
                              # (derived from LR - CODE_BASE - 4)
}
```

The hook fires on the first instruction of the stub (the `li r3, 0`). By this point, the `bl` has already set LR to the return address, so the stub's `blr` returns correctly.

### 6.3 Independent Trampoline Maps

Each side builds its own trampoline map. The maps are independent — decomp trampolines start at `TRAMPOLINE_BASE`, original trampolines also start at `TRAMPOLINE_BASE`. Both sides use the same address space layout, so the same trampoline address `0x40000000` means "the first unique external call target" on both sides.

The trampoline address is logged in each call entry but is NOT used for comparison. Comparison is purely by execution sequence (Section 8).

### 6.4 Symbol Name Resolution for Reporting

For diagnostic output, we label calls with the original's real symbol names. Best-effort offset matching maps decomp `$M` labels to original symbols:

```python
def build_offset_symbol_map(orig_relocs):
    """Map function-relative offsets to original symbol names."""
    return {r["offset"]: r["symbol_name"]
            for r in orig_relocs if r["type_name"] == "REL24"}

def resolve_decomp_call(decomp_trampoline_addr, decomp_relocs, orig_offset_map):
    """Try to find the original symbol name for a decomp trampoline."""
    for r in decomp_relocs:
        if r["type_name"] == "REL24":
            if r["offset"] in orig_offset_map:
                return orig_offset_map[r["offset"]]
    return None  # unresolved — report as $M label
```

Example output:
```
Call #0: ?Find@Symbol@@SAPAV1@PBD@Z     (resolved via offset 0x1C)
Call #1: ?Int@DataNode@@QBEH...@Z        (resolved via offset 0x38)
Call #2: <$M142>                          (no matching offset in original)
```

---

## 7. Execution Engine

### 7.1 Unicorn Setup

```python
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 + UC_MODE_BIG_ENDIAN)

# Map all regions
mu.mem_map(STACK_BASE,      0x10000)
mu.mem_map(OBJECT_BASE,     0x10000)
mu.mem_map(GLOBAL_BASE,     0x10000)
mu.mem_map(TRAMPOLINE_BASE, 0x10000)
mu.mem_map(CODE_BASE,       0x10000)

# Load patched function code
mu.mem_write(CODE_BASE, patched_code)

# Write trampoline stubs
for addr in trampoline_addrs:
    mu.mem_write(addr, TRAMPOLINE_STUB)

# Initialize registers
mu.reg_write(UC_PPC_REG_1,  STACK_BASE + 0x8000)   # SP
mu.reg_write(UC_PPC_REG_2,  0)                      # r2 — unused (no TOC-relative addressing)
mu.reg_write(UC_PPC_REG_3,  OBJECT_BASE)            # this
mu.reg_write(UC_PPC_REG_LR, 0xDEAD0000)             # return sentinel

# Enable FP unit (required for any float instruction)
msr = mu.reg_read(UC_PPC_REG_MSR)
mu.reg_write(UC_PPC_REG_MSR, msr | 0x2000)
```

### 7.2 Hook Registration

```python
# Call logging: fires when execution enters trampoline region
mu.hook_add(UC_HOOK_CODE, hook_trampoline_call,
            begin=TRAMPOLINE_BASE, end=TRAMPOLINE_BASE + 0xFFFF)

# Safety net: catch accesses to unmapped memory
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED,
            hook_unmapped_access)
```

### 7.3 Run Loop and Termination

```python
try:
    mu.emu_start(CODE_BASE, CODE_BASE + func_size, timeout=5_000_000)
    # Reached end of function bytes without branching — unusual but possible
except UcError as e:
    if e.errno == UC_ERR_FETCH_UNMAPPED:
        pc = mu.reg_read(UC_PPC_REG_PC)
        if pc == 0xDEAD0000:
            # Normal return: function executed blr, jumped to LR sentinel
            pass
        else:
            # Unexpected unmapped fetch — likely a bug or unpatched relocation
            raise RuntimeError(f"Unexpected fetch from unmapped 0x{pc:08X}")
    else:
        raise
```

### 7.4 Termination Conditions

| Condition | Meaning | Action |
|-----------|---------|--------|
| `blr` → `0xDEAD0000` | Normal function return | Capture output state |
| Timeout (5s) | Infinite loop or very long function | Report as ERROR |
| Unmapped fetch (not sentinel) | Unpatched relocation or wild branch | Report as ERROR |
| Unmapped read/write | Missing global or bad pointer | Log warning, return 0 |

### 7.5 Indirect Branch Detection

Before execution, scan the function bytes for indirect branch instructions. If found, skip the function with a diagnostic:

```python
def has_indirect_branch(code_bytes):
    """Detect bctr (0x4E800420) and bctrl (0x4E800421)."""
    for i in range(0, len(code_bytes), 4):
        insn = struct.unpack_from(">I", code_bytes, i)[0]
        if insn == 0x4E800421:  # bctrl — indirect call (virtual dispatch)
            return "bctrl"
        if insn == 0x4E800420:  # bctr — indirect branch (switch table or vtable tail call)
            return "bctr"
    return None
```

Both are skipped in Phase 1:
- **`bctrl`** (virtual function calls): requires vtable mocking
- **`bctr`** (switch/jump table dispatch or vtable tail calls): requires loading jump table data from `.rdata` sections, which Phase 1 doesn't handle. Jump tables are rare (17 across 971 .obj files) and live in `.rdata` — the REFHI/REFLO relocations point into `.rdata`, and the `.rdata` entries (ADDR32) point back to code labels. Without populating the `.rdata` data, `bctr` jumps to address 0 and crashes.

---

## 8. Comparator

### 8.1 Comparison Strategy: Execution-Sequence Matching

The goal is to detect functional equivalence, not assembly identity. Both sides execute with identical initial state and identical mocks (all return 0). The comparison checks whether they produce the same **observable behavior**:

1. **Same number of external calls** in the same execution order
2. **Same arguments** at each call position (r3-r6)
3. **Same return value** (r3)
4. **Same memory mutations** (object region + globals)

This is robust against register swaps, basic block reordering, and instruction scheduling differences — which are common in partially-matched functions.

**What this does NOT check**: whether call #N on both sides targets the same function. If decomp accidentally calls FuncB instead of FuncA but passes the same arguments, this comparison would not catch it. In practice this is extremely unlikely — wrong-target bugs almost always produce different argument values. A secondary diagnostic (offset-matched symbol check) catches these cases when offsets align.

### 8.2 Call Log Comparison

```python
def compare_call_logs(decomp_log, orig_log):
    # Primary: count must match
    if len(decomp_log) != len(orig_log):
        return Divergent(
            reason="call_count_mismatch",
            decomp_calls=len(decomp_log),
            orig_calls=len(orig_log),
        )

    for i, (d, o) in enumerate(zip(decomp_log, orig_log)):
        # Primary: arguments must match at each call position
        for reg in ("r3", "r4", "r5", "r6"):
            dv = d["args"][reg]
            ov = o["args"][reg]
            if dv != ov:
                return Divergent(
                    reason="call_arg_mismatch",
                    call_index=i,
                    register=reg,
                    decomp_val=dv,
                    orig_val=ov,
                )

    return Equivalent()
```

Both sides use the same memory map layout, so pointer arguments have the same values (e.g., `0x20000000` for `this`, `0x30000004` for the second global slot). A mismatch in pointer arguments is a real behavioral difference.

### 8.3 Secondary Diagnostic: Offset-Matched Symbol Check

After the primary comparison passes, optionally check whether calls go to the same targets by cross-referencing relocation offsets:

```python
def check_call_targets(decomp_relocs, orig_relocs, decomp_log, orig_log):
    """Best-effort check: do corresponding calls target the same function?"""
    orig_offset_map = {r["offset"]: r["symbol_name"]
                       for r in orig_relocs if r["type_name"] == "REL24"}
    decomp_offset_map = {r["offset"]: r["symbol_name"]
                         for r in decomp_relocs if r["type_name"] == "REL24"}

    warnings = []
    for i, (d, o) in enumerate(zip(decomp_log, orig_log)):
        # Find which relocation offset each call came from
        d_sym = decomp_offset_map.get(d.get("source_offset"))
        o_sym = orig_offset_map.get(o.get("source_offset"))
        if d_sym and o_sym and d_sym != o_sym:
            # Offsets match but different targets — possible bug
            warnings.append(f"Call #{i}: decomp targets {d_sym}, "
                          f"original targets {o_sym}")
    return warnings
```

This is reported as warnings, not failures. If offsets don't match (code reordering), no comparison is attempted for that call.

### 8.4 Return Value Comparison

```python
r3_decomp = decomp_mu.reg_read(UC_PPC_REG_3)
r3_orig   = orig_mu.reg_read(UC_PPC_REG_3)

if r3_decomp != r3_orig:
    return Divergent(reason="return_value_mismatch", decomp=r3_decomp, orig=r3_orig)
```

### 8.5 Modified Memory Comparison

After execution, compare the object memory and globals regions word-by-word:

```python
def compare_memory(decomp_mu, orig_mu, base, size):
    d_mem = decomp_mu.mem_read(base, size)
    o_mem = orig_mu.mem_read(base, size)
    diffs = []
    for i in range(0, size, 4):
        dw_d = struct.unpack_from(">I", bytes(d_mem), i)[0]
        dw_o = struct.unpack_from(">I", bytes(o_mem), i)[0]
        if dw_d != dw_o:
            diffs.append((base + i, dw_d, dw_o))
    return diffs
```

### 8.6 Output Format

```
EQUIVALENT
  Calls: 5 matched (args identical at each position)
    #0 ?Find@Symbol@@SAPAV1@PBD@Z  r3=0x20000000 r4=0x30000000
    #1 ?Int@DataNode@@QBEH...@Z     r3=0x20000000 r4=0x00000000
    ...
  Return: r3 = 0x00000001 (both)
  Memory: 0 diffs in object region, 0 diffs in globals

DIVERGENT
  First mismatch: call #3 argument r4 differs
    Decomp: call #3 r4=0x00000001
    Original: call #3 r4=0x00000000
  Call logs up to divergence:
    #0 args match: r3=0x20000000 r4=0x30000000
    #1 args match: r3=0x20000000 r4=0x00000000
    #2 args match: r3=0x20000000 r4=0x30000008
    #3 MISMATCH:   r4 decomp=0x1 orig=0x0
```

---

## 9. CLI Interface

```bash
# Compare a single function by symbol name
python3 scripts/unicorn_runner/run.py \
    --symbol "?Poll@Game@@UAEXXZ" \
    --decomp-obj build/373307D9/obj/system/meta/Game.obj \
    --orig-obj build/373307D9/src/system/meta/Game.obj

# Same, using unit name (resolves paths via objdiff.json)
python3 scripts/unicorn_runner/run.py \
    --symbol "?Poll@Game@@UAEXXZ" \
    --unit system/meta/Game

# Verbose mode: print disassembly, relocations, call log details
python3 scripts/unicorn_runner/run.py \
    --symbol "?Poll@Game@@UAEXXZ" \
    --unit system/meta/Game \
    --verbose

# List all runnable functions in a unit (no bctr/bctrl)
python3 scripts/unicorn_runner/run.py \
    --unit system/meta/Game \
    --list-functions
```

### Arguments

| Argument | Required | Description |
|----------|----------|-------------|
| `--symbol` | Yes (unless `--list-functions`) | Mangled C++ symbol name |
| `--decomp-obj` | No* | Path to decomp .obj file |
| `--orig-obj` | No* | Path to original .obj file |
| `--unit` | No* | Unit name (e.g. `system/meta/Game`); resolves paths from objdiff.json |
| `--verbose` | No | Print detailed execution trace |
| `--list-functions` | No | List eligible functions in the unit |
| `--timeout` | No | Execution timeout in microseconds (default: 5000000) |

\* Must provide either `--unit` or both `--decomp-obj` and `--orig-obj`.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | EQUIVALENT |
| 1 | DIVERGENT |
| 2 | ERROR (extraction failure, timeout, unmapped fetch, etc.) |
| 3 | SKIPPED (function has bctr/bctrl, or symbol not found in one side) |

---

## 10. Module Layout

```
scripts/unicorn_runner/
├── __init__.py
├── run.py              # CLI entry point
├── coff.py             # COFF parser (evolved from research.py COFFParser)
├── extractor.py        # Function extraction from both .obj flavors
├── patcher.py          # Relocation patching algorithm
├── engine.py           # Unicorn setup, execution, hook management
├── comparator.py       # Ordinal-based comparison logic
├── memory_map.py       # Address constants and region definitions
└── research.py         # Phase 0 research script (kept for reference)
```

### Module Responsibilities

**`coff.py`** — Parse COFF headers, sections, symbols, relocations. Minimal changes from the Phase 0 `COFFParser` class.

**`extractor.py`** — Extract function bytes + relocations from both .obj flavors. Handles the decomp (multi-symbol section, next-symbol boundary sizing) and original (COMDAT, section = function) differences.

**`patcher.py`** — Address assignment and relocation patching. Contains `patch_rel24`, `patch_refhi`, `patch_reflo`, `patch_addr32`, and the top-level `patch_function` pipeline.

**`engine.py`** — Unicorn instance creation, memory mapping, register setup, hook registration, execution, output capture. Returns an `ExecutionResult` with call log, return value, and post-execution memory snapshots.

**`comparator.py`** — Takes two `ExecutionResult` objects and produces an `EQUIVALENT` / `DIVERGENT` verdict. Primary comparison by execution sequence (call count, args, return value, memory). Secondary diagnostic by offset-matched symbol check.

**`memory_map.py`** — Constants for all memory regions, trampoline stub bytes, register definitions. Single source of truth for the address space layout.

**`run.py`** — CLI argument parsing, objdiff.json unit resolution, orchestrates extract → patch → execute → compare pipeline, formats output.

---

## 11. Scope Boundaries

### What Phase 1 Skips

| Feature | Why Skipped | Phase |
|---------|-------------|-------|
| `bctrl` (indirect calls) | Requires vtable mocking | Phase 2 |
| `bctr` (indirect branches) | Switch/jump tables in `.rdata` need populating; vtable tail calls need mocking | Phase 2 |
| YAML fixtures | Auto-fixture (zeroed state) is sufficient for equivalence testing | Phase 2 |
| Batch mode | Single-function comparison first, batch later | Phase 2 |
| Permuter integration | Needs batch mode + scoring system | Phase 2 |
| Intra-TU calls | Functions calling other functions in the same .obj need recursive loading | Phase 2 |
| Float epsilon comparison | Phase 1 uses exact r3 comparison; FPR comparison needs epsilon tuning | Phase 2 |
| FPR return values | Only compare r3 (integer return); f1 (float return) in Phase 2 | Phase 2 |
| decomp.db integration | No automatic status updates yet | Phase 2+ |

### Known Limitations in Auto-Fixture Mode

- **All mocks return 0**: functions that branch on mock return values will follow the "null/zero/false" path only. This is still valid for equivalence testing — both sides see the same mock returns.
- **Zeroed object memory**: member field reads return 0. Functions that dereference member pointers will hit the zero page (which maps to unmapped memory and triggers the safety hook).
- **No global initialization**: global variables are zeroed. Functions that read globals will see 0.

These limitations mean auto-fixture mode tests a restricted execution path. A function can be EQUIVALENT in auto-fixture mode but DIVERGENT with realistic state. However, DIVERGENT in auto-fixture mode is a strong signal of a real behavioral difference.

### Functions Eligible for Phase 1

A function is eligible if:
1. Symbol exists in both decomp and original .obj files
2. No `bctr` or `bctrl` instructions in either version
3. Function size > 0 in both versions

Note: `std`/`ld` (PPC64) instructions are no longer a skip reason — Phase 2 rewrites them to `stw`/`lwz`.

Functions with only REL24/REFHI/REFLO/PAIR/ADDR32 relocations (i.e., all 5 known types) are fully supported. Functions with no relocations (pure leaf functions) are trivially supported.
