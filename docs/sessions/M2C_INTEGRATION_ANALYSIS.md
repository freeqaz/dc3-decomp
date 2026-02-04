# m2c Integration Analysis for DC3 Decomp

**Date:** 2026-01-26
**Author:** Analysis by Claude
**Status:** Phase 1 Implementation Complete

## Executive Summary

This analysis explores potential integrations between m2c (the Machine-code-to-C decompiler), objdiff (our assembly comparison tool), and Ghidra (our reverse engineering platform). The goal is to identify opportunities to streamline the DC3 decompilation workflow.

**Key Findings:**
1. **objdiff has structured data APIs** that could feed m2c with disassembly data
2. **Ghidra's type/struct recovery** could dramatically improve m2c output quality
3. **m2c already supports PowerPC** with MWCC C++ target (`-t ppc`)
4. **Integration is feasible** but requires tooling work to bridge the data formats

**Recommended Priority:**
1. High: Ghidra types → m2c context (highest impact)
2. Medium: objdiff disassembly → m2c input pipeline
3. Low: Alternative decompiler view in objdiff (significant effort)

---

## 1. Current Tool State

### 1.1 m2c (Machine-code to C)

**Location:** `~/code/milohax/m2c`
**Purpose:** Decompile assembly to C source code for matching
**Input:** GNU-as style assembly (`.s` files)
**Output:** C/C++ source code

**Strengths:**
- Designed specifically for decompilation matching projects
- Supports PowerPC with `-t ppc` (MWCC C++ assumed)
- Context system for type hints (`--context`)
- Stack struct inference (`--stack-structs`)
- Control flow reconstruction (loops, switches)

**Current DC3 Integration:**
- Requires format conversion via `tools/asm_to_m2c.py`
- MSVC symbol parsing fix applied (local fork)
- Used for initial C code generation from assembly

**Limitations:**
- No direct binary input (needs disassembly first)
- Limited type inference without context
- Cannot import Ghidra analysis data

### 1.2 objdiff

**Location:** `~/code/milohax/objdiff`
**Purpose:** Compare compiled objects against target binary
**Architecture:** Rust crates (objdiff-core, objdiff-cli, objdiff-gui)

**Data Formats (conceptual, simplified from objdiff-core):**
```rust
// Simplified representation of key structures
// (actual API differs - see objdiff-core source for details)
pub struct Object {
    pub arch: Box<dyn Arch>,
    pub symbols: Vec<Symbol>,
    pub sections: Vec<Section>,
}

pub struct Relocation {
    pub flags: RelocationFlags,
    pub address: u64,
    pub target_symbol: usize,
    pub addend: i64,
}
```

**Export Capabilities:**
- JSON output via `--format json` (diff command)
- Protobuf bindings in `objdiff-core/src/bindings/`
- Report JSON format for progress tracking

**Notable Features:**
- Full PowerPC disassembly (via `powerpc` crate)
- Symbol demangling (including MSVC)
- Relocation tracking
- Flow analysis for PPC

### 1.3 Ghidra (via pyghidra-mcp)

**Location:** `tools/pyghidra-mcp-fork/`
**Purpose:** Binary analysis, decompilation, cross-references
**API:** MCP (Model Context Protocol) server

**Key Capabilities:**
- `decompile_function()` - P-code to pseudo-C
- `list_cross_references()` - Find callers/callees
- `search_functions_by_name()` - Symbol lookup
- Type/struct recovery from DWARF (when available)
- XEX binary support via XEXLoaderWV

**Current Integration:**
- `analyze-function` script combines objdiff + Ghidra output
- Used for understanding function semantics
- Cache system for decompilation results

---

## 2. Integration Opportunities

### 2.1 Ghidra Type Recovery → m2c Context (HIGH PRIORITY)

**Goal:** Use Ghidra's recovered types to improve m2c decompilation quality.

**Data Flow:**
```
Ghidra Analysis → Export Types → m2c --context → Better C Output
```

**Implementation:**

1. **Export Ghidra Data Types**
   Create a pyghidra tool to export discovered types:
   ```python
   def export_types_for_m2c(program, output_path):
       """Export Ghidra's recovered types as C header for m2c."""
       dtm = program.getDataTypeManager()
       with open(output_path, 'w') as f:
           for dt in dtm.getAllDataTypes():
               if is_struct_or_class(dt):
                   f.write(format_as_c_struct(dt))
   ```

2. **Extract Function Signatures**
   ```python
   def export_function_signatures(program, output_path):
       """Export function prototypes for m2c context."""
       fm = program.getFunctionManager()
       for func in fm.getFunctions(True):
           # Write: return_type func_name(params);
   ```

3. **Generate Combined Context File**
   ```bash
   # Workflow script
   python3 tools/ghidra/export_types.py > /tmp/ghidra_types.h
   cpp -P /tmp/ghidra_types.h > /tmp/m2c_context.h
   python3 ~/code/milohax/m2c/m2c.py -t ppc --context /tmp/m2c_context.h input.s
   ```

**Impact:**
- Better struct field names
- Correct function signatures
- Proper type casts
- Reduced manual cleanup needed

**Effort:** Medium (2-3 days)

### 2.2 objdiff Disassembly → m2c Input (MEDIUM PRIORITY)

**Goal:** Extract target disassembly from objdiff in m2c-compatible format.

**Current State:**
- objdiff's JSON output includes instruction data via `--include-instructions`
- Each instruction has `target.opcode` and `target.args` fields
- Relocation data available for symbol references

**Implementation Options (all proposed, not yet implemented):**

**Option A: CLI Export Command (PROPOSED)**
Would require adding to objdiff-cli:
```rust
// Proposed: objdiff-cli/src/cmd/export.rs
pub fn export_asm(symbol: &str, format: &str) -> Result<String> {
    // Read target object
    // Extract symbol's instructions
    // Format as GNU-as compatible assembly
}
```

Proposed usage:
```bash
./bin/objdiff-cli export "Game::Poll" --format m2c > func.s
python3 ~/code/milohax/m2c/m2c.py -t ppc func.s
```

**Option B: Python Bridge Script (PROPOSED)**
Would need to create `tools/objdiff_to_m2c.py`:
```python
#!/usr/bin/env python3
"""Convert objdiff JSON output to m2c input format."""

import json
import sys

def objdiff_to_m2c(json_data):
    """Convert objdiff JSON instructions to m2c assembly."""
    output = []
    symbol = json_data['symbol']
    output.append(f".global {symbol}")
    output.append(f"{symbol}:")

    for instr in json_data.get('instructions', []):
        if target := instr.get('target'):
            opcode = target['opcode']
            args = target.get('args', '')
            output.append(f"\t{opcode} {args}")

    return '\n'.join(output)

# Usage: objdiff-cli diff -p . "Foo::Bar" -f json --include-instructions | python3 tools/objdiff_to_m2c.py
```

**Impact:**
- Single-command workflow for any function
- No need for separate assembly files
- Consistent with objdiff's view of the target

**Effort:** Low-Medium (1-2 days)

### 2.3 m2c Output Comparison View (LOW PRIORITY)

**Goal:** Show m2c decompilation alongside objdiff's assembly diff.

**Concept:**
```
+------------------+------------------+------------------+
| Target ASM       | Base ASM         | m2c Decompile    |
+------------------+------------------+------------------+
| bl func          | bl func          | func();          |
| lwz r3, 0(r4)    | lwz r3, 0(r4)    | r3 = obj->field; |
| ...              | ...              | ...              |
+------------------+------------------+------------------+
```

**Implementation Challenges:**
- Would require significant objdiff-gui changes
- m2c runs as Python subprocess (latency)
- Mapping assembly lines to C lines is complex

**Alternative:** Keep as separate tool, accessible via command palette

**Effort:** High (1+ week)

### 2.4 Ghidra ↔ m2c Decompiler Comparison (LOW PRIORITY)

**Goal:** Compare Ghidra's decompilation with m2c's to verify understanding.

**Use Case:**
When Ghidra and m2c produce different interpretations, it highlights areas needing investigation.

**Implementation (conceptual, uses proposed tools):**
```bash
# Conceptual script: compare_decompilers.py
# Assumes tools/ghidra/ghidra-decompile.py and tools/objdiff_to_m2c.py exist
ghidra_output=$(python3 tools/ghidra/ghidra-decompile.py "Game::Poll")
m2c_output=$(./bin/objdiff-cli diff -p . "Game::Poll" -f json --include-instructions | \
             python3 tools/objdiff_to_m2c.py | \
             python3 ~/code/milohax/m2c/m2c.py -t ppc -)

diff -u <(echo "$ghidra_output") <(echo "$m2c_output")
```

**Impact:** Educational/verification, not critical path

**Effort:** Low (half day)

---

## 3. Technical Requirements

### 3.1 For Ghidra → m2c Type Export

**Dependencies:**
- pyghidra (already installed)
- Access to Ghidra's DataTypeManager API

**Ghidra APIs Needed:**
```java
// From Ghidra's API
DataTypeManager.getAllDataTypes()
DataType.getName(), getLength(), getFields()
Function.getSignature(), getParameters()
```

**Output Format (m2c context):**
```c
// Types
typedef unsigned int u32;
typedef struct { float x, y, z; } Vector3;

// Function prototypes
int Game_Poll(Game* this);
void CharClip_SetFlags(CharClip* this, int flags);
```

### 3.2 For objdiff → m2c Assembly Export

**Approach:** Use objdiff's JSON output (`--include-instructions -f json`) rather than internal APIs. The JSON provides:
- `symbol` - Function name
- `instructions[].target.opcode` - Instruction mnemonic
- `instructions[].target.args` - Instruction operands

**m2c Expected Format:**
```asm
.global function_name
function_name:
    mflr r0
    bl other_func
    lwz r3, 0(r4)
    blr
```

**Relocation Handling:**
```asm
# objdiff knows: reloc at 0x4 -> "?TheDebug@@3VDebug@@A"
# m2c needs:
    lis r11, ?TheDebug@@3VDebug@@A@ha
    addi r11, r11, ?TheDebug@@3VDebug@@A@l
```

### 3.3 API Stability Considerations

**objdiff:** Actively developed, JSON format may change
**Ghidra:** Stable API, P-code well documented
**m2c:** Stable input format, actively maintained

---

## 4. Implementation Roadmap

> **Status:** Phase 1 complete, Phase 2 partially complete.

### Phase 1: Quick Wins ✅

1. **Create `tools/ghidra/export_types.py`** ✅ DONE (2026-01-26)
   - Exports structs and typedefs from Ghidra via MCP
   - Generates m2c-compatible context header
   - Supports `--function` and `--all` modes

2. **Create `tools/objdiff_to_m2c.py`** ✅ DONE (2026-01-26)
   - Parses objdiff JSON output
   - Generates m2c GNU-as style assembly
   - Handles relocations, branch labels, memory operands

3. **Update `analyze-function` script** ✅ DONE (2026-01-26)
   - Added `--m2c` flag to include m2c output
   - Added `--m2c-context` for type context file
   - Integrated into markdown and JSON output formats

### Phase 2: Integration

4. **Add objdiff export command** (TO DO, optional)
   - Native Rust implementation
   - Faster than Python bridge
   - Could be contributed upstream

5. **Create combined workflow script** ✅ DONE (2026-01-26)
   ```bash
   # tools/decompile.sh "Symbol::Name"
   tools/decompile.sh "CharClip::SetFlags"           # Basic
   tools/decompile.sh "Object::Load" -u unit/path   # With unit
   tools/decompile.sh "Game::Poll" --context        # With Ghidra types
   ```

### Phase 3: Polish

6. **Orchestrator integration** ✅ DONE (2026-01-26)
   - Context collector uses m2c for initial code via `objdiff_to_m2c.py` pipeline
   - Fallback chain: objdiff JSON → asm file → error
   - Optional type context from Ghidra
   - New `m2c_method` field tracks which pipeline succeeded
   - Implemented in `scripts/orchestrator/context_collector.py`

7. **Documentation** ✅ DONE (2026-01-26)
   - Updated this analysis document
   - Scripts include comprehensive usage help
   - See usage examples below

---

## 5. Blockers and Challenges

### 5.1 MSVC Symbol Handling

**Status:** Fixed in local m2c fork
**Risk:** Upstream may not accept fix; need to maintain fork

**Workaround:** Continue using local fork at `~/code/milohax/m2c`

### 5.2 Type Recovery Quality

**Challenge:** Ghidra's type inference is imperfect
**Impact:** Wrong types in context → wrong m2c output

**Mitigation:**
- Manually curate context for critical types
- Use `include/` headers as authoritative source
- Only export high-confidence types from Ghidra

### 5.3 Complex Control Flow

**Challenge:** m2c may produce different control flow than original
**Impact:** Harder to match, more manual cleanup

**Mitigation:**
- Use `--gotos-only` for complex functions
- Compare with Ghidra's interpretation
- Accept as starting point, not final code

### 5.4 C++ Features

**Challenge:** m2c's C++ support is partial
**Impact:** Virtual calls, templates, exceptions not well handled

**Mitigation:**
- Rely on Ghidra for C++ semantic understanding
- Use m2c for code structure, not exact syntax
- Manual adjustment for C++ idioms

---

## 6. Comparison: Ghidra vs m2c

| Aspect | Ghidra | m2c |
|--------|--------|-----|
| **Input** | Binary directly | Assembly text |
| **Analysis depth** | Full program, cross-refs | Single function |
| **Type inference** | Uses DWARF, heuristics | Requires context |
| **Control flow** | P-code based | Direct pattern matching |
| **C++ support** | Good (this pointers, vtables) | Basic |
| **Output style** | Pseudo-C (not compilable) | Matching-focused C |
| **Speed** | Slow (full analysis) | Fast (parse & translate) |
| **Customization** | Ghidra scripts | Context files |

**Recommendation:** Use both complementarily:
- Ghidra for understanding (semantics, types, xrefs)
- m2c for initial code generation (structure, control flow)
- Manual refinement for final matching

---

## 7. Conclusion

Integration between m2c, objdiff, and Ghidra is technically feasible and would provide meaningful workflow improvements. The highest-impact opportunity is exporting Ghidra's type information to improve m2c's context, followed by streamlining the objdiff-to-m2c assembly pipeline.

**Completed Implementations:**
1. ✅ `tools/ghidra/export_types.py` - Type context generation
2. ✅ `tools/objdiff_to_m2c.py` - Assembly format conversion
3. ✅ `tools/analyze_function.py` - Updated with `--m2c` flag
4. ✅ `tools/decompile.sh` - Combined workflow script

**Remaining Next Steps:**
1. ~~Orchestrator integration for automated decompilation workflows~~ ✅ Done
2. Consider contributing objdiff export functionality upstream

---

## 8. Usage Reference

### 8.1 Quick Decompilation (`tools/decompile.sh`)

The recommended way to get m2c output for a function:

```bash
# Basic decompilation
tools/decompile.sh "CharClip::SetFlags"

# With unit disambiguation (for duplicate symbols)
tools/decompile.sh "Object::Load" -u default/system/char/Character

# Output to file
tools/decompile.sh "Game::Poll" -o decompiled.c

# With Ghidra type context (requires Ghidra MCP running)
tools/decompile.sh "CharMirror::Load" --context

# Verbose mode (show pipeline progress)
tools/decompile.sh "CharClip::SetFlags" -v

# Complex control flow (use gotos instead of structured code)
tools/decompile.sh "Parser::Run" --gotos-only

# Disable && / || reconstruction
tools/decompile.sh "Foo::Bar" --no-andor
```

### 8.2 Full Analysis with m2c (`analyze-function --m2c`)

Combines objdiff verdict, Ghidra decompilation, cross-references, and m2c output:

```bash
# Full analysis with m2c decompilation
./bin/analyze-function "Game::Poll" --m2c

# JSON output (for scripting)
./bin/analyze-function "Game::Poll" --m2c -f json

# With custom m2c context file
./bin/analyze-function "Game::Poll" --m2c --m2c-context include/game/Game.h

# Skip Ghidra connection warnings
./bin/analyze-function "Game::Poll" --m2c -q
```

### 8.3 Type Export (`tools/ghidra/export_types.py`)

Export Ghidra-discovered types as C headers for m2c context:

```bash
# Export types relevant to a function
python3 tools/ghidra/export_types.py --function "Game::Poll"

# Export to file
python3 tools/ghidra/export_types.py --function "CharMirror::Load" -o context.h

# Export all discoverable types (from exports)
python3 tools/ghidra/export_types.py --all --limit 50

# Filter by symbol pattern
python3 tools/ghidra/export_types.py --all --pattern "Game.*"

# Output as JSON instead of C header
python3 tools/ghidra/export_types.py --function "Foo::Bar" --json

# Verbose progress
python3 tools/ghidra/export_types.py --function "Foo::Bar" -v
```

### 8.4 objdiff to m2c Conversion (`tools/objdiff_to_m2c.py`)

Convert objdiff JSON output to m2c-compatible assembly:

```bash
# Pipe from objdiff-cli
./bin/objdiff-cli diff -p . "CharClip::SetFlags" -f json --include-instructions | \
    python3 tools/objdiff_to_m2c.py

# From file
python3 tools/objdiff_to_m2c.py -i function.json -o function.s

# With custom symbol name
./bin/objdiff-cli diff -p . "CharClip::SetFlags" -f json --include-instructions | \
    python3 tools/objdiff_to_m2c.py --symbol CharClip_SetFlags

# Use compiled (base) instructions instead of target binary
python3 tools/objdiff_to_m2c.py --use-base -i function.json
```

### 8.5 Complete Pipeline Examples

**Workflow 1: Quick decompilation for a new function**
```bash
# One command to get decompiled C
tools/decompile.sh "CharClip::SetFlags"
```

**Workflow 2: Full analysis before starting work**
```bash
# Get everything: match %, verdict, Ghidra pseudocode, m2c output, xrefs
./bin/analyze-function "Game::Poll" --m2c
```

**Workflow 3: Manual pipeline with type context**
```bash
# Step 1: Export types from Ghidra
python3 tools/ghidra/export_types.py --function "CharMirror::Load" -o /tmp/ctx.h

# Step 2: Get disassembly and convert to m2c format
./bin/objdiff-cli diff -p . "CharMirror::Load" -f json --include-instructions | \
    python3 tools/objdiff_to_m2c.py -o /tmp/func.s

# Step 3: Run m2c with context
python3 ~/code/milohax/m2c/m2c.py -t ppc --context /tmp/ctx.h /tmp/func.s
```

**Workflow 4: Batch decompilation**
```bash
# Get list of near-match functions
./bin/objdiff-cli report query build/373307D9/report.json --functions \
    --min-percent 90 --max-percent 99 --limit 10 -f plain | \
while read func; do
    echo "=== $func ==="
    tools/decompile.sh "$func" 2>/dev/null || echo "Failed"
done
```

---

## 9. Troubleshooting

### 9.1 Common Issues

**"Symbol not found" from objdiff**
- Check symbol name spelling (case-sensitive)
- Try the mangled name: `grep "FuncName" orig/373307D9/ham_xbox_r.map`
- Use `-u` flag for disambiguation: `tools/decompile.sh "Foo::Bar" -u default/system/path`

**"Could not connect to Ghidra MCP"**
- Start the Ghidra service: `./tools/ghidra/pyghidra-service.sh start`
- Check status: `./tools/ghidra/pyghidra-service.sh status`
- View logs: `./tools/ghidra/pyghidra-service.sh logs`

**m2c parse errors**
- Try `--gotos-only` for complex control flow
- Check if assembly has unsupported VMX128 instructions
- Use `--no-andor` if && / || reconstruction fails

**Empty or wrong decompilation**
- Verify the function exists in target binary
- Check for stub/thunk functions (very small)
- Try using Ghidra directly for pseudo-C reference

### 9.2 Prerequisites

- **Python 3.9+** with `requests` library
- **m2c** at `~/code/milohax/m2c` (local fork with MSVC fix)
- **objdiff-cli** at `bin/objdiff-cli` (extended version)
- **Ghidra MCP** (optional, for type context and analysis)

---

## Appendix A: Data Format Examples

### A.1 objdiff JSON Instruction Format

```json
{
  "index": 1,
  "target": {
    "address": "0x65c",
    "opcode": "bl",
    "args": "__savegprlr_29"
  },
  "base": {
    "address": "0xc",
    "opcode": "bl",
    "args": "__savegprlr_29"
  },
  "match_type": "equal"
}
```

### A.2 m2c Assembly Input Format

```asm
.global CharMirror_Load
CharMirror_Load:
    mflr r12
    bl __savegprlr_27
    stwu r1, -0xa0(r1)
    mr r30, r4
    mr r31, r3
    lis r11, ?TheDebug@@3VDebug@@A@ha
    addi r29, r11, ?TheDebug@@3VDebug@@A@l
    blr
```

### A.3 m2c Context Header Format

```c
// Types from Ghidra
typedef unsigned int u32;
typedef int s32;
typedef float f32;

struct BinStream {
    void* mData;
    int mPos;
    int mSize;
};

class CharMirror {
public:
    void* vtable;
    BinStream* mStream;
    int mFlags;

    void Load(BinStream& bs);
};

// Globals
extern Debug TheDebug;
extern TaskMgr TheTaskMgr;

// Function prototypes
void CharMirror_Load(CharMirror* this, BinStream* bs);
void BinStream_ReadEndian(BinStream* this, void* dest, int size);
```

### A.4 Ghidra Decompilation Output

```c
void CharMirror::Load(BinStream *bs) {
    int local_50;

    FUN_82348710(bs, &local_50, 4);
    if ((local_50 & 0xffff) != 0) {
        Debug::Fail(TheDebug, "CharMirror::Load failed", 0);
    }
    // ...
}
```

---

## Appendix B: Implemented Tool Locations

| Tool | Location | Purpose |
|------|----------|---------|
| `decompile.sh` | `tools/decompile.sh` | Combined m2c pipeline (main entry point) |
| `objdiff_to_m2c.py` | `tools/objdiff_to_m2c.py` | Convert objdiff JSON → m2c assembly |
| `export_types.py` | `tools/ghidra/export_types.py` | Export Ghidra types → m2c context |
| `analyze_function.py` | `tools/analyze_function.py` | Full analysis with `--m2c` support |
| `asm_to_m2c.py` | `tools/asm_to_m2c.py` | Convert dtk assembly → m2c format |
| m2c | `~/code/milohax/m2c/m2c.py` | Machine code to C decompiler |

## Appendix C: Related Documentation

- [Tools Index](../tools/INDEX.md) - Quick reference for all tools
- [Decomp Workflow](../tools/WORKFLOW.md) - Decision guide for tool selection
- [objdiff CLI Usage](../OBJDIFF_CLI_USAGE.md) - Detailed objdiff documentation
- [Ghidra MCP Integration](../tools/GHIDRA.md) - Ghidra setup and usage
- [MSVC Symbol Bug Report](M2C_MSVC_SYMBOL_BUG_REPORT.md) - m2c fork details
