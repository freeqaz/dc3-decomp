# Ghidra Tooling Phases 3-4: ICF Annotation and Switch Detection

**Date**: 2026-02-09

## Overview

Continued the pyghidra-mcp tooling improvement series with two phases: Phase 3 added automatic annotation of ICF-merged symbols in Ghidra decompilation output, and Phase 4 added switch statement detection via `bctr` instruction analysis. The session also pivoted into investigating whether switch detection belongs in the MCP layer or in Ghidra's native analyzer -- concluding that Ghidra already has built-in switch recovery (`PowerPCAddressAnalyzer.java`) and the real question is why it might be failing for DC3's MSVC-compiled binary.

**Files modified** (all in `/home/free/code/milohax/pyghidra-mcp/`):
- `src/pyghidra_mcp/tools.py` -- annotation functions + detection methods
- `src/pyghidra_mcp/models.py` -- `SwitchInfo` and `SwitchDetectionResult` models
- `tests/unit/test_ppc_simplify.py` -- tests for both phases

---

## Phase 3: ICF-Merged Symbol Annotation

### Problem

ICF (Identical COMDAT Folding) causes the linker to merge functions with identical machine code to a single address. Ghidra shows these as `merged_82331360`, which gives no indication of what the call target actually is. This is common for scalar/vector deleting destructors that share identical codegen across many classes.

### Implementation

Added `annotate_merged_calls()` to `tools.py`. The function:

1. Detects `merged_<8-hex-digit>` patterns via regex (`_MERGED_PATTERN`)
2. Looks up the address in the MSVC linker `.map` file via `MapFileParser.lookup_all_symbols_by_address()`
3. Adds an inline comment with the actual symbol names

Abbreviation rules for common patterns:
- `` `scalar deleting destructor' `` becomes `scalar dtor`
- `` `vector deleting destructor' `` becomes `vector dtor`
- Multiple symbols: shows up to 3 names, then `+N more`

Example transformation:
```c
// Before:
(*merged_82331360)(this, 1);

// After:
(*merged_82331360)(this, 1); /* scalar dtor, vector dtor */
```

### Design Decisions

- **Annotation, not replacement** -- Following the lesson from Phase 2 (LZCOUNT), the original `merged_` name is preserved. Comments add context without hiding information needed for assembly matching.
- **Lazy-initialized map parser** -- Module-level `_map_parser_cache` avoids re-parsing the map file on every call. Falls back gracefully when no map file is available (returns code unchanged).
- **Integrated into decompilation pipeline** -- Called automatically after `annotate_ppc_decompilation()` during any decompile operation.

### Tests

8 test cases in `TestAnnotateMergedCalls`:
- Single symbol annotation
- Multiple merged symbols (scalar/vector dtor abbreviation)
- Case-insensitive hex addresses
- No symbols found (passes through unchanged)
- No map file available (graceful fallback)

---

## Phase 4: Switch Statement Detection

### Pre-Implementation Validation

The assistant recommended skipping Phase 4, arguing:
- Switch detection was marked LOW priority
- Only one brief mention of switch issues in decomp docs
- Switch statements are "usually obvious" from consecutive integer comparisons
- The proposed tool's value was unclear -- detecting `bctr` addresses doesn't directly help correlate to decompiled if-else chains

**The user overruled this recommendation**, stating switches had been a real pain point in decomp work. This was the correct call -- switch detection issues later proved significant enough to warrant a full Ghidra analyzer investigation (see the separate session doc `2026-02-09-ghidra-msvc-switch-detection.md`).

### MCP-Layer Implementation

Added `detect_switch_statements()` method to `GhidraTools` and supporting models.

**Models** (`models.py`):
```python
class SwitchInfo(BaseModel):
    address: str          # Address of bctr instruction
    case_count: int | None  # From cmplwi bounds check
    index_register: str | None  # From lwzx operand
    table_address: str | None   # Jump table base

class SwitchDetectionResult(BaseModel):
    function_name: str
    function_address: str
    switches: list[SwitchInfo]
    note: str  # Interpretation guidance
```

**Detection method** (`_analyze_switch_pattern`):
- Walks backwards up to 20 instructions from each `bctr`
- Looks for `mtctr` (branch target), `lwzx` (jump table load), `cmplwi`/`cmpwi` (bounds check)
- Extracts case count from the immediate operand of the compare instruction
- Extracts index register and table base from `lwzx` operands

**Annotation** (`annotate_switch_statements`):
- Prepends a header comment when switches are detected:
```c
/* SWITCH STATEMENTS DETECTED:
   1. Address 0x82345678 - ~7 cases
   Ghidra if-else chains at these locations are likely switch statements.
*/
```

**Public MCP tool**: `detect_switch_statements(name_or_address)` returns structured `SwitchDetectionResult` for any function.

### Tests

6 test cases for switch detection annotation, covering single/multiple switches, no switches, and case count display.

All 69 tests passed after both phases.

---

## Pivot: MCP vs Native Ghidra Approach

After completing the MCP-layer implementation, the user questioned whether switch detection should live in Ghidra itself rather than as post-processing. This triggered an investigation into the vmx128-research repository and Ghidra's internals.

### Key Findings

1. **Sleigh is not the answer** -- Sleigh (`.sinc` files) defines instruction encoding/decoding only. It cannot perform control flow analysis or switch table recovery.

2. **Ghidra already has native switch detection** -- `PowerPCAddressAnalyzer.java` contains a `SwitchEvaluator` class that:
   - Detects `bctr`/`bcctr` computed jumps
   - Finds `cmplwi` bounds checks via symbolic execution
   - Iterates assumed index values to find all jump targets
   - Creates `AddressTable` entries for the decompiler

3. **vmx128 did not modify switch detection** -- The switch recovery code in the vmx128 Ghidra fork is identical to upstream. All vmx128 changes were to Sleigh instruction definitions (adding VMX128 vector instructions), not to analysis passes.

4. **The real question is why detection fails for DC3** -- The investigation needed to shift from "add switch detection" to "debug why Ghidra's existing detection is failing for MSVC-compiled Xbox 360 binaries."

### Conclusion

The MCP annotation approach (Phase 4) serves as a useful fallback, but the proper fix requires investigating and patching `PowerPCAddressAnalyzer.java`. This became the subject of the follow-up session documented in `2026-02-09-ghidra-msvc-switch-detection.md`, which identified three concrete bugs in the analyzer (blocked `allowAccess`, insufficient predecessor walking, leaked `targetList`) and implemented fixes that recovered all game-relevant switch tables.

---

## Results

| Phase | Feature | Tests | Status |
|-------|---------|-------|--------|
| 3 | ICF-Merged Annotation | 8 new (21 total) | Complete |
| 4 | Switch Detection (MCP) | 6 new (69 total) | Complete |
| -- | Ghidra Native Investigation | -- | Led to analyzer fix session |

The MCP-layer switch detection remains available as a diagnostic tool, while the real fix was implemented at the Ghidra analyzer level in the subsequent session.
