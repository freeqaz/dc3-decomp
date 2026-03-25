# 3A: `compare_asm` — Side-by-Side Target vs Base Assembly with Source Attribution

> **Implementation**: New mode in `diff_inspect.py` + skill (`/compare-asm`) for invocation. Heavy lifting in `diff_inspect.py`; the skill just wraps the CLI invocation and presents results. Avoid adding a new MCP tool.

## Motivation

During decomp work on the "last 10%" of a function, the agent currently must mentally fuse four separate data sources:

1. **objdiff mismatches** — instruction-level diff (match_type, diff_breakdown)
2. **Target assembly** — pre-extracted `.s` files under `build/373307D9/asm/`
3. **Base assembly** — `/FAs` listing from current compilation
4. **Ghidra pseudocode** — decompiled C from the target binary

The objdiff `mismatches` mode shows *what* differs but not *where in the source* it comes from. The `asm_listing` mode shows the base with source attribution but has no target context. The `attributed` mode connects mismatches to source lines but still requires cross-referencing the actual assembly on both sides.

A unified `compare_asm` mode would present target and base assembly side-by-side, aligned by objdiff's instruction pairing, annotated with source lines from the `/FAs` listing and cluster boundaries from the existing clusters analysis. This makes diagnosing the remaining mismatches purely mechanical rather than requiring mental merge of four tools.

## Mode Specification

- **Mode name**: `compare_asm`
- **Tool**: `run_diff_inspect` (new enum value alongside `diagnose`, `clusters`, etc.)
- **Input parameters**: Same as existing modes — `symbol` (required), `project_dir` (required), `unit` (optional)

## Pipeline

### Step 1: Run objdiff and get instruction-level diff

Reuse the existing objdiff invocation pattern from `_run_diff_inspect` / `mismatches` mode:

```
objdiff-cli diff -p <project_dir> <symbol> --include-instructions --build --incremental
    -c functionRelocDiffs=none -f json
```

This produces the aligned instruction stream with `match_type`, `target`, `base`, and `diff_breakdown` for each instruction index. The JSON contains:

- `instructions[]` — aligned instruction pairs with `index`, `match_type`, `target.opcode`, `target.args`, `base.opcode`, `base.args`, `diff_breakdown`
- `fuzzy_match_percent`, `raw_match_percent` — overall match scores

### Step 2: Generate `/FAs` listing for the base (current compilation)

Reuse the existing `_run_asm_listing` infrastructure in `mcp_server.py`:

1. Look up `symbol` in `report.json` to find the source file
2. Extract the compile command from `ninja -t commands <obj_target>`
3. Append `/FAs /Fa<tmpdir>/listing.asm` to the compile command
4. Run the compilation
5. Parse the listing with `extract_function()` (from `tools/compiler_trace/asm_diff.py`) and `parse_asm_listing()` (from `scripts/permuter/attribution.py`)

The parsed `AsmListing` provides:
- Ordered `AsmEntry` objects, each with `source_file`, `source_line`, `source_text`, and a tuple of `AsmInstruction` objects
- `prologue_helper` and `callee_saved_count`
- `source_line_for_index(n)` — maps instruction index to source attribution

### Step 3: Load target assembly from pre-extracted `.s` files

Target assembly lives at `build/373307D9/asm/<category>/<UnitName>.s` in GNU assembler syntax. The function boundaries are marked by `.fn` / `.endfn` directives with mangled symbol names:

```gas
.fn "?IsValidObject@@YA_NPAVObject@Hmx@@@Z", global
/* 826DABB8 006CF5B8  7D 88 02 A6 */  mflr r12
/* 826DABBC 006CF5BC  48 2C 2D 71 */  bl __savegprlr_29
...
.endfn "?IsValidObject@@YA_NPAVObject@Hmx@@@Z"
```

To locate the file:
1. From `report.json`, the unit name is available (e.g., `default/rndobj/AmbientOcclusion`)
2. Strip the `default/` prefix and map to `build/373307D9/asm/system/<rest>.s`
3. If not found under `system/`, try other category directories (`lazer/`, `lib/`, etc.)
4. Parse between `.fn "<symbol>"` and `.endfn "<symbol>"` markers

Each target instruction line has the format:
```
/* <vaddr> <file_offset>  <hex_bytes> */  <opcode>  <operands>
```

Extract: address, opcode, operands. The address is useful for cross-referencing with Ghidra.

### Step 4: Normalize both sides

Normalization makes the side-by-side comparison focus on *semantic* differences rather than formatting noise.

#### Target normalization (from `.s` file)

1. **Strip address/hex prefix**: Remove the `/* 826DABB8 006CF5B8  7D 88 02 A6 */` comment, keeping only the opcode and operands
2. **Normalize symbol references**: Replace `"??_R0?AVObject@Hmx@@@8"@ha` / `@l` with a shortened form (e.g., `Object@Hmx@ha`)
3. **Normalize local labels**: Replace `.L_826DAC40` with sequential `L0`, `L1`, etc.
4. **Retain register names as-is**: `r3`, `r31`, `f1`, `cr6` — these are the comparison target

#### Base normalization (from `/FAs` listing)

1. **Strip hex offset prefix**: Remove the `00008  81230000  ` prefix from each instruction line, keeping only the opcode and operands
2. **Normalize symbol references**: Same shortened form as target
3. **Normalize local labels**: `$LN1@FuncName` becomes sequential `L0`, `L1`, etc. — labels must be renumbered to match structural position, not name
4. **Strip source comments**: The `;  42  :  int a = ...` lines are used for attribution but not rendered in the instruction column

#### Shared normalization

- **Whitespace**: Collapse internal whitespace to single spaces
- **Register aliases**: Normalize `sp` to `r1`, `rtoc` to `r2` if they appear
- **Pseudo-ops**: Normalize `subi rN, rM, X` to `addi rN, rM, -X` if one side uses the pseudo-op and the other uses the real instruction (note: objdiff already handles this, but the raw listings may not)
- **Branch targets**: Normalize to relative form (the `.s` file uses absolute labels, the `/FAs` file uses `$LN` labels — after renumbering both to `L0`, `L1`, etc., they become comparable)

### Step 5: Build the aligned side-by-side output

Use the objdiff instruction stream as the alignment backbone. For each instruction index `i`:

1. **Source attribution**: From the `/FAs` `AsmListing`, call `source_line_for_index(i)` on the *base* side instruction index. When the source line changes from the previous instruction, emit a source line separator.

2. **Cluster boundaries**: Run `find_clusters()` from `diff_inspect.py` on the objdiff instructions. When entering or leaving a cluster, emit a cluster boundary marker.

3. **Instruction row**: Format as:

```
<match_indicator> | <idx> | <target_instruction> | <base_instruction> | <annotation>
```

Where:
- `match_indicator`: `=` (equal), `~` (diff_arg), `!` (diff_op), `+` (insert/target only), `-` (delete/base only), `X` (replace)
- `idx`: objdiff instruction index (0-based)
- `target_instruction`: normalized target opcode + operands (from objdiff `target` or from `.s` file for richer context)
- `base_instruction`: normalized base opcode + operands (from objdiff `base` or from `/FAs` listing)
- `annotation`: register swap pairs (from `diff_breakdown`), offset deltas, cluster ID

### Step 6: Annotate with variable-register mapping

From the `/FAs` listing's `parse_asm_listing()` (via `asm_regmap.py`), extract the variable-to-register mapping for the base side. Include this as a header/legend so the reader knows which registers correspond to which source variables.

## Output Format

The output is a Markdown text block suitable for MCP tool response. Structure:

```markdown
## Compare ASM: <demangled_symbol>

**Match**: <fuzzy_match_percent>% (<equal>/<total> instructions)
**Prologue**: <prologue_helper> (<N> callee-saved GPRs)
**Source**: <source_file>

### Variable -> Register Mapping (base)

| Variable | Register |
|----------|----------|
| this     | r31      |
| count    | r30      |
| ...      | ...      |

### Side-by-Side Assembly

```
; --- <source_file>:<line> ---
; <source_text>
  =  |   0 | mflr     r12              | mflr     r12              |
  =  |   1 | bl       __savegprlr_29   | bl       __savegprlr_29   |
  =  |   2 | stwu     r1, -0x70(r1)    | stwu     r1, -0x70(r1)   |
; --- <source_file>:<line> ---
; <source_text>
  =  |   3 | lis      r11, sym@ha      | lis      r11, sym@ha      |
  ~  |   4 | lis      r10, sym@ha      | lis      r9,  sym@ha      | [reg:r10->r9]
  ~  |   5 | addi     r30, r11, sym@l  | addi     r30, r11, sym@l  |
; --- CLUSTER 1 (3I/2D) idx 12-18 ---
  +  |  12 | li       r7, 0x0          | ---                       |
  +  |  13 | li       r4, 0x0          | ---                       |
  -  |  14 | ---                       | mr       r4, r28          |
; --- END CLUSTER 1 ---
  ...
```

### Diagnosis Summary

<Compact summary from existing diagnose mode — root causes, noise budget>
```

## Implementation Plan

### Files to modify

1. **`scripts/orchestrator/mcp_server.py`**
   - Add `"compare_asm"` to the `valid_modes` set (~line 1839)
   - Add `"compare_asm"` to the `enum` in the tool schema (~line 457)
   - Update the mode description string
   - Add `elif mode == "compare_asm":` branch before the `asm_listing` branch (~line 2074)
   - Implement `_run_compare_asm(self, symbol, project_dir, unit)` async method

2. **`scripts/analysis/diff_inspect.py`** (optional, for CLI usage)
   - Add `--compare-asm` flag to argparse
   - Add `cmd_compare_asm()` function that orchestrates the pipeline for direct CLI invocation

3. **New file: `scripts/analysis/asm_normalize.py`**
   - `normalize_target_asm(lines: list[str]) -> list[str]` — normalize pre-extracted `.s` target assembly
   - `normalize_base_asm(lines: list[str]) -> list[str]` — normalize `/FAs` base assembly
   - `extract_target_function(asm_text: str, symbol: str) -> list[str]` — extract function from `.s` file between `.fn` / `.endfn` markers
   - `align_labels(target_lines, base_lines) -> tuple[list, list]` — renumber labels to structural position

4. **New file: `scripts/analysis/compare_asm.py`**
   - `build_side_by_side(objdiff_instrs, target_lines, base_listing, clusters) -> str` — main formatting function
   - `format_instruction_row(idx, match_type, target, base, annotation) -> str` — single row formatter
   - `insert_source_separators(rows, base_listing) -> list[str]` — interleave source attribution
   - `insert_cluster_boundaries(rows, clusters) -> list[str]` — interleave cluster markers

### New functions in `mcp_server.py`

```python
async def _run_compare_asm(self, symbol: str, project_dir: Path, unit: str | None) -> list[TextContent]:
    """Side-by-side target vs base assembly with source attribution."""
    # 1. Run objdiff (same pattern as mismatches mode)
    # 2. Generate /FAs listing (reuse _run_asm_listing infrastructure, refactored to return parsed data)
    # 3. Load target .s file and extract function
    # 4. Normalize both sides
    # 5. Build side-by-side output
    # 6. Append diagnosis summary
```

### Refactoring `_run_asm_listing`

The existing `_run_asm_listing` method combines compilation, parsing, and formatting. For `compare_asm`, we need the intermediate parsed data (the `AsmListing` object and raw lines) without the final formatting. Refactor into:

```python
async def _compile_and_parse_asm(self, symbol: str, project_dir: Path) -> tuple[list[str], AsmListing | None, AsmRegMap | None]:
    """Compile with /FAs and return (raw_func_lines, parsed_listing, regmap).

    Shared by asm_listing and compare_asm modes.
    """
    # ... existing steps 1-6 from _run_asm_listing ...
    return func_lines, listing, regmap

async def _run_asm_listing(self, symbol, project_dir):
    """Original asm_listing mode, now delegates to _compile_and_parse_asm."""
    func_lines, listing, regmap = await self._compile_and_parse_asm(symbol, project_dir)
    # ... existing formatting ...
```

### Target `.s` file lookup

```python
def _find_target_asm(self, symbol: str, project_dir: Path, unit: str | None) -> list[str] | None:
    """Extract target function assembly from pre-extracted .s files.

    Returns normalized instruction lines, or None if not found.
    """
    # 1. Determine unit from report.json (or use provided unit)
    # 2. Map unit name to .s file path:
    #    "default/rndobj/AmbientOcclusion" -> build/373307D9/asm/system/rndobj/AmbientOcclusion.s
    #    Try category dirs: system/, lazer/, lib/, etc.
    # 3. Parse between .fn "symbol" and .endfn "symbol"
    # 4. Return raw instruction lines
```

The unit-to-category mapping can be determined by scanning the `build/373307D9/asm/` subdirectories, or by maintaining a lookup from the `objdiff.json` config which already maps units to target object paths.

## Normalization Rules — Detailed

### Symbol shortening

Both sides reference mangled C++ symbols. For readability in the side-by-side view:

| Raw | Shortened |
|-----|-----------|
| `"??_R0?AVObject@Hmx@@@8"@ha` | `Object@Hmx@ha` |
| `"??_R0?AVRndMesh@@@8"@l` | `RndMesh@l` |
| `"?GetValue@@YAHXZ"` | `GetValue` |
| `"??_C@_0BF@...AmbientOcclusion?4cpp..."` | `<str:AmbientOcclusion.cpp>` |

Implementation: strip `??_R0?AV` prefix and `@@@8` suffix for RTTI descriptors; strip `?` prefix and `@@...` suffixes for simple functions; replace `??_C@` string constants with `<str:...>` using the string content if decodable.

### Label renumbering

Target labels: `.L_826DAC40` (absolute address based)
Base labels: `$LN3@FuncName` (compiler-generated sequential)

Both must be renumbered to `L0`, `L1`, ... in order of first appearance in the instruction stream. This ensures structural equivalence even when addresses or compiler label numbering differ.

### Instruction canonicalization

| Pattern | Canonical form |
|---------|---------------|
| `subi rN, rM, X` | `addi rN, rM, -X` |
| `not rN, rM` | `nor rN, rM, rM` |
| `mr rN, rM` | keep as `mr` (common enough to be readable) |
| `li rN, X` | keep as `li` (alias for `addi rN, r0, X`) |
| `lis rN, X` | keep as `lis` |

Only canonicalize when one side uses the pseudo-op and the other uses the underlying instruction. If both sides use the same form, keep it.

### Operand formatting

- Hex immediates: always use `0x` prefix, lowercase hex digits
- Decimal immediates: keep as decimal for small values (-128 to 127), hex otherwise
- Memory operands: `0x70(r1)` format (hex offset, register in parens)
- CR fields: `cr0` through `cr7` (not numeric)

## Output Size Management

For large functions (200+ instructions), the full side-by-side can exceed MCP response limits. Apply these truncation rules:

1. **Collapse equal runs**: When 5+ consecutive instructions are `equal`, show the first 2 and last 2 with a `... (N equal) ...` placeholder
2. **Prioritize mismatches**: Always show the full context around non-equal instructions (3 lines before/after)
3. **Hard cap**: 150 rendered instruction rows maximum. If exceeded, show only mismatch regions with context
4. **Source attribution**: Only emit source line separators when they are near mismatches (within 5 instructions)

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Symbol not in report.json | Return error with similar symbol suggestions (reuse existing `_suggest_similar_symbols`) |
| `/FAs` compilation fails | Return error with compiler output |
| Target `.s` file not found | Fall back to using only objdiff's `target` instruction data (opcode + args from JSON, no address/hex bytes) |
| Function not found in `/FAs` listing | Return partial output: target-only column with objdiff base data, note that source attribution unavailable |
| Function not found in target `.s` | Return partial output: base-only column with source attribution, note target raw assembly unavailable |

## Verification Plan

### Test function: `IsValidObject` in `AmbientOcclusion.cpp`

This function is a good test case because:
- It has a known target `.s` file at `build/373307D9/asm/system/rndobj/AmbientOcclusion.s`
- It uses `__savegprlr_29` (callee-saved registers — tests prologue matching)
- It has multiple `__RTDynamicCast` calls (tests symbol shortening)
- It has branch labels (tests label renumbering)

### Manual verification steps

1. Run `compare_asm` on the test function
2. Verify that every `equal` row shows identical normalized instructions on both sides
3. Verify that `diff_arg` rows correctly annotate the differing arguments (register swaps, offset shifts)
4. Verify that source line separators appear at the correct boundaries
5. Verify that cluster boundaries match the output of `clusters` mode on the same function
6. Cross-check the variable-register mapping against `asm_listing` mode output

### Automated test

Add a test in `scripts/analysis/tests/` that:
1. Loads a known objdiff JSON fixture
2. Loads a known target `.s` excerpt
3. Loads a known `/FAs` listing excerpt
4. Runs the `build_side_by_side()` function
5. Asserts that the output contains expected source line markers, cluster boundaries, and mismatch annotations
6. Asserts that normalization produces identical strings for known-equal instruction pairs

## Future Extensions

- **Ghidra pseudocode column**: Add a third column with Ghidra decompiled C, aligned by address to target instructions. Requires Ghidra MCP integration (already available).
- **Interactive narrowing**: Allow specifying an index range to zoom into a specific mismatch region, similar to the existing `--range` flag.
- **Diff highlighting**: In terminal output, use ANSI colors to highlight differing registers/immediates within otherwise-matching instruction lines.
- **Register rename preview**: Given detected register swaps, show what the base would look like with registers renamed — helps distinguish "pure swap" from "real difference."
