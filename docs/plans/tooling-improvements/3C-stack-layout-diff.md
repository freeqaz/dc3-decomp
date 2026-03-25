# 3C: Stack Layout Diff Tool

> **Implementation**: New mode `stack_layout` in `diff_inspect.py` + skill (`/stack-layout`) for invocation. Reuses existing `run_diff_inspect` infrastructure. Skill wraps `diff_inspect.py --stack-layout`. Avoid adding a new MCP tool.

## Motivation

When a function is 99.7% matched with 15 offset mismatches, the current `--offsets` mode in `diff_inspect.py` shows a histogram of deltas (e.g., `delta=+16, 15 instructions`) but does not reveal **which source variables** occupy which stack slots, or which two variables are swapped relative to the target. The ClipCollide case study made this concrete: a dominant `off:-16` annotation across 15 instructions pointed to a stack frame layout mismatch, but diagnosing which local variables were in the wrong slots required manual cross-referencing of the `/FAs` listing against target disassembly.

A dedicated stack layout diff tool would make this diagnosis immediate by producing a side-by-side table of stack slot assignments for both our compiled output and the target binary.

## Integration Point

Add `stack_layout` as a new mode in the existing `run_diff_inspect` MCP tool.

**Why not a new MCP tool?** The stack layout diff requires the same symbol resolution, objdiff invocation, and `/FAs` compilation that `run_diff_inspect` already performs (modes `asm_listing` and `offsets`). Adding a mode avoids duplicating plumbing and keeps the tool surface minimal.

**Schema change in `mcp_server.py`:**
```python
"enum": ["diagnose", "clusters", "regswaps", "offsets", "replaces",
         "compare", "save_baseline", "mismatches", "asm_listing",
         "stack_layout"],  # <-- new
```

**CLI entry in `diff_inspect.py`:**
```
python3 scripts/analysis/diff_inspect.py --symbol "?Collide@ClipCollide@@..." --stack-layout
```

## Pipeline

### Stage 1: Source-Side Stack Layout (from `/FAs` listing)

The `/FAs` listing interleaves source comments with PPC assembly. MSVC PPC does not emit explicit `; var$[rbp-N]` annotations like x86 MSVC does. Instead, we reconstruct the source-side stack layout by:

1. **Extract the function** via `PROC NEAR` / `ENDP` markers (reuse `extract_function()` from `tools/compiler_trace/asm_diff.py`).

2. **Parse the prologue** to determine the stack frame size from the `stwu r1, -FRAME(r1)` instruction.

3. **Collect all `r1`-relative memory operations** throughout the function body:
   ```
   stw   rN, OFFSET(r1)     # GPR store to stack
   lwz   rN, OFFSET(r1)     # GPR load from stack
   stfs  fN, OFFSET(r1)     # FPR store to stack
   lfs   fN, OFFSET(r1)     # FPR load from stack
   stfd  fN, OFFSET(r1)     # double store
   lfd   fN, OFFSET(r1)     # double load
   addi  rN, r1, OFFSET     # address-of stack slot (pass-by-ref)
   ```

4. **Correlate with source comments** using the `/FAs` interleaving. When a source comment `; 42   :     Vector3 pos = GetPosition();` precedes a `stfs f1, 0x20(r1)` / `stfs f2, 0x24(r1)` / `stfs f3, 0x28(r1)` sequence, infer that stack offsets `0x20-0x2B` belong to variable `pos` (Vector3, 12 bytes).

5. **Use type knowledge** from `TYPE_SIZES` in `tools/struct_db.py` to validate and extend slot boundaries:
   - If the source declares `Vector3 pos`, the slot spans 12 bytes (with 4 bytes padding to 16).
   - If the source declares `Transform xfm`, the slot spans 64 bytes.
   - For unknown types, infer size from the span of consecutive `r1`-relative accesses attributed to the same variable.

**Key data structure:**
```python
@dataclass
class StackSlot:
    offset: int           # Offset from r1 (positive, within frame)
    size: int             # Slot size in bytes (inferred or from type)
    variable: str | None  # Source variable name (from /FAs) or None
    type_name: str | None # C++ type if known (from source comment parsing)
    access_count: int     # Number of load/store instructions touching this slot
    first_instr_idx: int  # First instruction index that accesses this slot
```

### Stage 2: Target-Side Stack Layout (from objdiff JSON)

The target binary has no source annotations, so stack layout must be inferred purely from instruction patterns:

1. **Parse the prologue** from target instructions in the objdiff JSON. Find the `stwu r1, -FRAME(r1)` to get frame size. The instruction appears as:
   ```json
   {"opcode": "stwu", "args": "r1, -0xN0(r1)"}
   ```

2. **Collect all `r1`-relative memory operations** from target-side instructions in the objdiff diff output:
   ```python
   _STACK_ACCESS_RE = re.compile(
       r'(-?0x[0-9a-fA-F]+|-?\d+)\(r1\)'
   )
   ```
   Filter to only positive offsets within the frame (negative offsets are callee-saved register saves, not locals).

3. **Cluster adjacent offsets into slots** using access patterns:
   - Three consecutive `stfs` at offsets +0, +4, +8 from a base suggest a Vector3 (12 bytes).
   - Four consecutive `stw` at offsets +0, +4, +8, +12 suggest a 16-byte struct.
   - A single `stw` at an isolated offset is a 4-byte slot.
   - A `stfd` indicates an 8-byte double slot.

4. **Assign synthetic names** (`slot_0x20`, `slot_0x30`, etc.) since the target has no variable names. The value comes from comparing the pattern and position against the source side.

**Key data structure** (same `StackSlot`, but `variable` will be `None` and `type_name` inferred from access pattern).

### Stage 3: Correlation and Diff

1. **Align by access pattern**: Match target and source slots by:
   - Same offset: perfect match (no mismatch).
   - Same size + same access pattern but different offset: likely a swapped variable pair.
   - Same calling context: if both slots feed the same `bl` call or are loaded into the same parameter register before a call, they correspond.

2. **Use the offset mismatch data** from `parse_breakdowns()` (the existing `offset_diffs` list). Each `diff_arg` instruction where the offset delta is non-zero and the base register is `r1` is a stack layout mismatch. Group these by the dominant delta to separate:
   - **Frame size shift**: A uniform delta across all offsets (e.g., all off by +16) means the frame is larger/smaller but variables are in the same relative order. This is usually a prologue-level issue (different callee-saved count).
   - **Variable swap**: Two variables at offsets A and B in source appear at B and A in target (or at A+k and B+k after removing the frame shift). The delta histogram will show a symmetric pair.
   - **Mixed**: A combination of frame shift + individual variable reordering.

3. **Detect frame shift vs variable swap**:
   ```
   dominant_delta = most common offset delta
   adjusted_source_offsets = {slot.offset + dominant_delta for slot in source_slots}
   remaining_mismatches = target_offsets - adjusted_source_offsets
   ```
   If `remaining_mismatches` is empty, pure frame shift. Otherwise, the remaining offsets reveal variable swaps.

4. **Identify swapped pairs**: For each remaining mismatch, find the source slot whose adjusted offset matches the target slot. Two slots that cross-reference each other form a swap pair.

### Stage 4: Output

Produce a structured report with three sections:

**Section 1: Frame Summary**
```
STACK LAYOUT DIFF: ClipCollide::Collide
  Source frame: 0x90 (144 bytes)
  Target frame: 0x80 (128 bytes)
  Frame delta: -16 bytes (source larger)
  Callee-saved GPRs: source=6, target=5
```

**Section 2: Side-by-Side Slot Table**
```
| Offset (TGT) | Size | Target Use          | Offset (SRC) | Size | Source Variable     | Status  |
|---------------|------|---------------------|--------------|------|---------------------|---------|
| 0x08          | 4    | stw (2x), lwz (3x) | 0x08         | 4    | i (int)             | MATCH   |
| 0x10          | 16   | stfs×3, lfs×3       | 0x20         | 16   | pos (Vector3)       | SWAPPED |
| 0x20          | 16   | stfs×3, lfs×3       | 0x10         | 16   | normal (Vector3)    | SWAPPED |
| 0x30          | 64   | stfs×12, lfs×12     | 0x30         | 64   | xfm (Transform)     | MATCH   |
```

**Section 3: Diagnosis**
```
ROOT CAUSE: 2 stack variables swapped
  pos (Vector3, 16B) at source 0x20 should be at 0x10
  normal (Vector3, 16B) at source 0x10 should be at 0x20

  Fix: Reorder declarations so 'normal' is declared before 'pos'.
  This is LIKELY FIXABLE via declaration reorder.
```

Or, if unfixable:
```
ROOT CAUSE: Frame size mismatch (source=0x90, target=0x80)
  Source has 1 extra callee-saved GPR (6 vs 5), adding 16 bytes.
  All variable-relative offsets match after adjusting for frame delta.

  This is AT_LIMIT — the extra register is a compiler allocation difference.
```

## Implementation Plan

### Files to Modify

1. **`scripts/analysis/diff_inspect.py`** (primary implementation)
   - Add `cmd_stack_layout(instrs, symbol, project_dir)` function (~200 lines)
   - Add `--stack-layout` CLI flag
   - Add `_parse_stack_frame_from_objdiff(instrs)` helper to extract target stack layout
   - Add `_parse_stack_frame_from_listing(listing_text, symbol)` helper for source side

2. **`scripts/orchestrator/mcp_server.py`** (MCP integration)
   - Add `"stack_layout"` to the `valid_modes` set in `_run_diff_inspect()`
   - Add `"stack_layout"` to the `enum` in the tool schema
   - Route to `cmd_stack_layout()` with the additional `/FAs` compilation step

3. **`tools/compiler_trace/asm_regmap.py`** (optional enhancement)
   - Extract a reusable `parse_stack_accesses(func_lines)` function from the stack-parsing logic, since `asm_regmap.py` already parses `/FAs` listings and understands prologue structure.

### Key Parsing Functions

#### `_extract_frame_size(instrs_or_lines) -> int | None`
Parse `stwu r1, -FRAME(r1)` from either objdiff JSON instructions or `/FAs` listing lines. Return frame size as positive integer.

```python
_STWU_RE = re.compile(r'stwu\s+r1,\s*(-?0x[0-9a-fA-F]+|-?\d+)\(r1\)')
```

#### `_collect_stack_accesses(instrs, side='target') -> list[StackAccess]`
Walk objdiff instructions, extract all `r1`-relative memory operations from the specified side. Return list of `(offset, size, opcode, register, instr_index)` tuples.

```python
@dataclass
class StackAccess:
    offset: int        # Positive offset from r1
    size: int          # 1/2/4/8 based on opcode (stb=1, sth=2, stw/stfs=4, stfd=8)
    opcode: str        # stw, lwz, stfs, lfs, etc.
    register: str      # rN or fN
    instr_index: int   # Position in instruction stream
```

#### `_cluster_into_slots(accesses, type_hints=None) -> list[StackSlot]`
Group accesses by proximity into logical stack slots. Uses `TYPE_SIZES` for validation when type hints are available. Algorithm:
1. Sort accesses by offset.
2. Merge accesses within a stride-aligned window (4-byte aligned for GPR, 4/8-byte for FPR).
3. If three `stfs` at +0/+4/+8 from same base, merge into single 12-byte (Vector3) slot.
4. Single isolated access = one slot of the access's natural size.

#### `_correlate_source_listing(listing_lines, symbol) -> list[StackSlot]`
Walk the `/FAs` listing for the function, tracking source comments and subsequent `r1`-relative instructions. Associate each stack access group with the nearest preceding source variable declaration. Uses `_extract_var_from_source()` from `asm_regmap.py` for declaration parsing and `_DECL_RE` / `_DECL_PTR_RE` patterns.

#### `_diff_layouts(target_slots, source_slots, dominant_delta) -> StackLayoutDiff`
Compare the two slot lists after adjusting for frame size delta. Classify each pair as `MATCH`, `SWAPPED`, `MISSING`, or `EXTRA`. Detect swap pairs by finding cross-references in the offset mapping.

### Type Size Integration

Reuse `TYPE_SIZES` and `guess_type_size()` from `tools/struct_db.py`:

```python
from tools.struct_db import TYPE_SIZES, TEMPLATE_SIZES, guess_type_size
```

This provides sizes for:
- Primitives: `int`(4), `float`(4), `double`(8), `bool`(1), `char`(1)
- Math types: `Vector2`(8), `Vector3`(12), `Vector4`(16), `Transform`(64), `Quat`(16)
- Engine types: `Symbol`(4), `DataNode`(16), `String`(8), `Color`(16), `Sphere`(16)
- Templates: `ObjPtr`(0x14), `ObjDirPtr`(0x10), `ObjPtrList`(0xC)
- Pointers: 4 bytes (ILP32 PPC)

Stack slots are 4-byte aligned on PPC. Types smaller than 4 bytes still occupy 4-byte-aligned slots. `Vector3` occupies 16 bytes on stack (12 bytes + 4 bytes padding for alignment).

### PPC Stack Frame Layout Reference

On Xbox 360 PPC (MSVC), the stack frame layout (growing downward from r1) is:

```
r1 + FRAME - 4   : LR save (via __savegprlr_N or mflr/stw)
r1 + FRAME - 8   : r31 save
r1 + FRAME - 12  : r30 save
...               : more callee-saved GPRs
...               : callee-saved FPRs (stfd, 8 bytes each)
...               : compiler temporaries
...               : local variables (ordered by compiler's linear allocator)
r1 + 8            : parameter save area / outgoing args
r1 + 4            : reserved
r1 + 0            : back chain (previous r1)
```

The local variable region starts above the parameter area and grows upward. The offset of each local variable from `r1` is determined by the compiler's stack layout algorithm, which generally follows declaration order but may reorder for alignment.

**Callee-saved region** (negative from frame top): Offsets in this region should be excluded from variable analysis. Detected by:
- `__savegprlr_N` / `stmw` in prologue
- Individual `stw rN, -offset(r1)` where `13 <= N <= 31`
- Individual `stfd fN, -offset(r1)` where `14 <= N <= 31`

**Local variable region** (positive from r1, above parameter area): The region of interest. Offsets typically start at `0x08` or higher (skipping back chain + reserved word + any parameter save area).

### Opcode-to-Access-Size Mapping

```python
OPCODE_SIZE = {
    'stb': 1, 'lbz': 1,                           # byte
    'sth': 2, 'lhz': 2, 'lha': 2,                 # halfword
    'stw': 4, 'lwz': 4, 'stfs': 4, 'lfs': 4,     # word / single float
    'stfd': 8, 'lfd': 8,                           # doubleword / double
    'stwu': 4, 'lwzu': 4, 'lfsu': 4, 'stfsu': 4, # update variants
    'lhau': 4,                                      # halfword update
}
```

## Verification Plan

### Test Case 1: Known Stack Layout Mismatch (ClipCollide)

ClipCollide::Collide had 15 offset mismatches with dominant delta `-16`. Run:
```
run_diff_inspect symbol="ClipCollide::Collide" mode="stack_layout" project_dir="..."
```

Expected: The tool identifies which two variables are swapped and whether reordering declarations would fix it. If the delta is purely from a callee-saved register count difference, it should say "AT_LIMIT" with the reason.

### Test Case 2: Pure Frame Shift (No Variable Swap)

Find a function where all offset mismatches have the same delta (uniform frame shift from different prologue). The tool should report:
```
All offsets explained by frame delta (+16). No variable reordering possible.
Cause: source uses N+1 callee-saved GPRs vs target's N.
Verdict: AT_LIMIT (prologue mismatch)
```

### Test Case 3: 100% Match (Baseline)

Run on a function that already matches 100%. The tool should report:
```
No stack layout mismatches. Frame sizes match.
```

### Test Case 4: Mixed Frame Shift + Variable Swap

A function where the dominant delta is +16 (frame shift) but 2-4 instructions have a different delta (indicating a variable swap within the frame). The tool should separate the two causes and report both.

## Limitations

### When This Confirms AT_LIMIT

- **Callee-saved register count mismatch**: The frame size differs because our compiler allocates one more/fewer callee-saved register. All variable-relative offsets match after adjusting for the frame delta. No source-level fix exists.
- **Compiler temporary ordering**: The compiler places its own temporaries (loop counters, vtable pointers, spill slots) on the stack in a different order than the target. These have no source-level names and cannot be reordered.
- **Alignment padding differences**: MSVC may insert different amounts of padding between variables for alignment. Cannot be controlled from source.

### When This Reveals Fixable Ordering

- **Declaration order swap**: Two user-declared variables are in opposite stack positions. Reordering their declarations in source may fix the layout (the compiler's linear allocator generally respects declaration order for stack layout, though not guaranteed).
- **Scope-based reordering**: Moving a variable declaration into a narrower scope (e.g., inside an `if` block) can change its stack position relative to variables declared at function scope.
- **Local variable splitting**: A large struct on the stack might be replaceable with individual member variables (or vice versa), changing the stack footprint.

### Known Gaps

- **No DWARF for target**: The target binary has no debug info. All target-side variable inference is heuristic, based on access patterns and type sizes. False groupings are possible when two adjacent 4-byte variables look like one 8-byte variable.
- **Inlined functions**: When a function is inlined, its locals become part of the caller's frame. The `/FAs` listing shows this, but correlating inlined stack slots with target-side slots requires tracking which source lines belong to inlined callees.
- **Compiler temporaries invisible**: The compiler may allocate stack slots for expression evaluation temporaries that have no corresponding source variable. These appear as "unnamed" slots in the source-side analysis.
- **`/FAs` source comment granularity**: Source comments appear at statement granularity, not expression granularity. If one source line declares and initializes multiple stack variables (rare in this codebase), attribution may be ambiguous.

## Dependencies

| Component | Path | What We Need |
|-----------|------|--------------|
| `/FAs` compilation | `tools/compiler_trace/invoker.py` | `CompilerInvoker.compile_with_asm()` |
| Function extraction | `tools/compiler_trace/asm_diff.py` | `extract_function()` |
| Source comment parsing | `tools/compiler_trace/asm_regmap.py` | `_extract_var_from_source()`, `_SOURCE_LINE_RE` |
| Prologue parsing | `tools/compiler_trace/bsf_trace.py` | `_parse_function_info()` |
| Type sizes | `tools/struct_db.py` | `TYPE_SIZES`, `guess_type_size()` |
| Offset histogram | `scripts/analysis/diff_inspect.py` | `parse_breakdowns()`, `compute_offset_histogram()` |
| MCP routing | `scripts/orchestrator/mcp_server.py` | `_run_diff_inspect()` |
| objdiff JSON | `bin/objdiff-cli` | `--include-instructions` JSON output |

## Estimated Effort

- **Stage 1** (source-side parsing): ~120 lines. Reuses existing `/FAs` infrastructure heavily.
- **Stage 2** (target-side parsing): ~80 lines. Simple regex over objdiff JSON.
- **Stage 3** (correlation + diff): ~100 lines. The interesting algorithmic part.
- **Stage 4** (output formatting): ~60 lines. Table rendering + diagnosis text.
- **MCP integration**: ~20 lines. Add mode to existing routing.
- **Total**: ~380 lines of new code, plus tests.
