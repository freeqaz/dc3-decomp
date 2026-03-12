# Ghidra MCP Integration for DC3 Decomp

## Overview

The pyghidra-mcp server provides AI-accessible Ghidra analysis capabilities via the Model Context Protocol. This document analyzes how to leverage these tools to accelerate the DC3 decompilation effort.

**Server Status:** Running at `http://127.0.0.1:8000/mcp/v1` (FastMCP/Uvicorn transport)
**Binary:** `/default.xex` (Dance Central 3 Xbox 360)
**Indexed:** 38,338 decompiled functions + 21,175 strings (59,513 total embeddings)

---

## Architecture

The integration consists of four key components:

### 1. XEXLoaderWV Extension
- **Location**: `ghidra_12.0_DEV/Extensions/XEXLoaderWV/`
- **Purpose**: Ghidra extension that recognizes and loads XEX2 format binaries
- **Provides**: PowerPC:BE:64:Xenon language specification for Xbox 360 executables
- **Detection**: Automatically identifies XEX files by magic number (XEX2)

### 2. pyghidra-mcp Fork
- **Location**: `tools/pyghidra-mcp-fork/`
- **Version**: 0.1.6+ with FastMCP and Uvicorn
- **Installation**: Editable mode via pip (`pip install -e tools/pyghidra-mcp-fork/`)
- **Enhancements**:
  - XEX2 magic number detection in `_is_binary_file()`
  - Automatic language specification for XEX binaries
  - Extended `import_binary()` with language/compiler parameters
  - FastMCP transport for improved performance

### 3. Ghidra User Home
- **Location**: `/tmp/claude/ghidra_user/`
- **Purpose**: Writable directory for Ghidra caches and project data
- **Importance**: Avoids read-only filesystem issues in Ghidra installation directory
- **Environment Variable**: `GHIDRA_USER_HOME`

### 4. Service Script
- **Location**: `tools/ghidra/pyghidra-service.sh`
- **Purpose**: Manages pyghidra-mcp service lifecycle
- **Features**: Start, stop, status, restart, logs commands
- **Configuration**: Sets `GHIDRA_INSTALL_DIR` and `GHIDRA_USER_HOME`

---

## Environment Setup

### Required Environment Variables

**GHIDRA_INSTALL_DIR**
```bash
export GHIDRA_INSTALL_DIR="../ghidra/build/ghidra"
```
- Points to the VMX128-enabled Ghidra build with PowerPC extensions
- Must include the XEXLoaderWV extension for Xbox 360 binary support
- pyghidra uses this to locate Ghidra's installation and extensions

**GHIDRA_USER_HOME**
```bash
export GHIDRA_USER_HOME="/tmp/claude/ghidra_user"
```
- Specifies a writable directory for Ghidra's runtime data
- Critical for avoiding "read-only filesystem" errors
- Stores:
  - Ghidra preferences and settings
  - Analysis caches
  - Temporary project data
  - Extension configurations

**Why These Are Important:**
- The main Ghidra installation may be on a read-only filesystem (VMX128 build)
- Without `GHIDRA_USER_HOME`, Ghidra tries to write to its installation directory
- pyghidra-mcp needs both variables to properly initialize the Ghidra environment
- Misconfiguration causes import failures or analysis errors

---

## XEX Integration

### XEX2 Magic Number Detection

The pyghidra-mcp fork includes automatic detection of Xbox 360 executables:

```python
# From tools/pyghidra-mcp-fork/pyghidra_mcp/context.py
@staticmethod
def _is_binary_file(path: Path) -> bool:
    """Quick header-based check for common binary formats."""
    with path.open("rb") as f:
        header = f.read(4)
        if header.startswith(b"XEX2"):
            return True
        # ... other format checks
```

When an XEX file is detected, the system automatically:
1. Identifies the file as Xbox 360 executable
2. Sets language specification to `PowerPC:BE:64:Xenon`
3. Invokes the XEXLoaderWV extension for parsing
4. Performs PowerPC-specific analysis

### Automatic Language Specification

XEX binaries are automatically assigned the correct language during import:

```python
# From tools/pyghidra-mcp-fork/pyghidra_mcp/server.py
def _detect_binary_language(binary_path: Path) -> tuple[str | None, str | None]:
    """Detect binary format and return language/compiler IDs if needed."""
    with binary_path.open("rb") as f:
        header = f.read(4)
        if header.startswith(b"XEX2"):
            return "PowerPC:BE:64:Xenon", None
    return None, None

# During import:
language, compiler = _detect_binary_language(Path(bin_path))
if language:
    logger.info(f"Detected XEX binary, using language: {language}")
pyghidra_context.import_binary(bin_path, language=language, compiler=compiler)
```

### XEXLoaderWV Extension Role

The extension provides:
- **Format Parsing**: Interprets XEX2 file structure (headers, sections, compression)
- **Memory Mapping**: Maps XEX sections to virtual addresses
- **Symbol Recognition**: Extracts export/import tables
- **PowerPC Support**: Configures Ghidra for Xbox 360's PowerPC Xenon processor
- **Automatic Disassembly**: Disassembles code sections using PowerPC instruction set

**Verification:**
```bash
ls -la ../ghidra/build/ghidra/Extensions/XEXLoaderWV/lib/XEXLoaderWV.jar
```

**For detailed XEX support information**, see [PYGHIDRA_MCP_XEX_SUPPORT.md](PYGHIDRA_MCP_XEX_SUPPORT.md)

---

## Service Management

The `pyghidra-service.sh` script provides a complete service lifecycle management interface:

### Commands

**Start the service:**
```bash
./tools/ghidra/pyghidra-service.sh start
```
Expected output:
```
Starting pyghidra-mcp service...
  Project: $HOME/code/milohax/dc3-decomp/ghidra_projects/DC3
  Binary: $HOME/code/milohax/dc3-decomp/orig/373307D9/default.xex
  Port: 8000
  ...
Service process is running (PID: XXXXX)
```

**Stop the service:**
```bash
./tools/ghidra/pyghidra-service.sh stop
```

**Check service status:**
```bash
./tools/ghidra/pyghidra-service.sh status
```

**Restart the service:**
```bash
./tools/ghidra/pyghidra-service.sh restart
```

**View service logs:**
```bash
./tools/ghidra/pyghidra-service.sh logs
```

Expected successful logs:
```
INFO:pyghidra_mcp.server:Detected XEX binary, using language: PowerPC:BE:64:Xenon
INFO:pyghidra_mcp.context:Importing new program: default.xex-997567
INFO:pyghidra_mcp.context:Analysis for default.xex-997567 complete
INFO:pyghidra_mcp.context:Analysis % complete: 100.0
```

### Environment Variable Handling

The service script automatically sets:
- `GHIDRA_INSTALL_DIR` - Points to VMX128 Ghidra build
- `GHIDRA_USER_HOME` - Points to writable temporary directory
- `UVICORN_PORT` - Service port (8000)
- Python virtual environment activation

These environment variables are critical for proper XEX support and must be configured correctly in the service script.

---

## Available MCP Tools

### Code Analysis
| Tool | Description | Use Case |
|------|-------------|----------|
| `decompile_function` | Get Ghidra's pseudo-C for any function | Compare against our C++ to understand expected behavior |
| `search_code` | Semantic search over decompiled code | Find similar functions, discover patterns |
| `list_cross_references` | Find all xrefs to a function/address | Understand call hierarchy, find callers |
| `gen_callgraph` | Generate MermaidJS call graphs | Visualize function relationships |

### Symbol & String Operations
| Tool | Description | Use Case |
|------|-------------|----------|
| `search_symbols_by_name` | Case-insensitive symbol search | Find functions by partial name |
| `search_strings` | Semantic string search | Find error messages, debug strings, URLs |
| `list_exports` | List exported symbols (regex) | Find public API functions |
| `list_imports` | List imported symbols (regex) | Identify library dependencies |

### Binary Inspection
| Tool | Description | Use Case |
|------|-------------|----------|
| `read_bytes` | Read raw memory at address | Inspect data structures, vtables |
| `list_project_binary_metadata` | Get arch, compiler, format info | Verify binary properties |

---

## Integration Opportunities

### 1. Function Discovery & Prioritization

**Current Pain Point:** Finding high-value work targets requires manual report querying.

**Ghidra MCP Solution:**
```
search_symbols_by_name("GameMode")  -> Find all GameMode-related functions
search_code("switch case poll")     -> Find polling/state machine patterns
list_cross_references("ErrorMsg")   -> Find error handling code paths
```

**Workflow Enhancement:**
- Use `search_code` to find functions with specific patterns (vtable setup, state machines)
- Cross-reference with objdiff report to prioritize unmatched functions
- Build "clusters" of related functions to work on together

### 2. Understanding Original Implementation

**Current Pain Point:** When our C++ doesn't match, we often don't know what the original code was doing.

**Ghidra MCP Solution:**
```
decompile_function("/default.xex", "Game::Poll")
```

Returns Ghidra's pseudo-C decompilation, which can:
- Reveal control flow structure
- Show variable types and counts
- Expose inlined function patterns
- Identify loop structures

**Workflow Enhancement:**
- Before attempting a match, get Ghidra's decompile to understand the target
- Compare Ghidra's output with RB3 reference code to identify shared patterns
- Use decompile to verify our understanding of complex functions

### 3. String-Based Function Discovery

**Current Pain Point:** Finding functions that handle specific features (achievements, UI, network).

**Ghidra MCP Solution:**
```
search_strings("achievement")     -> Find achievement-related code
search_strings("network error")   -> Find network error handling
search_strings("save failed")     -> Find save system code
```

**Workflow Enhancement:**
- Search for feature-specific strings to locate implementation code
- Use string addresses to find referencing functions via xrefs
- Build feature maps: string -> function -> call graph

### 4. Call Graph Analysis

**Current Pain Point:** Understanding how functions relate to each other.

**Ghidra MCP Solution:**
```
gen_callgraph("/default.xex", "Game::Poll", direction="calling")
```

Returns MermaidJS graph showing:
- What functions `Game::Poll` calls
- Or what functions call `Game::Poll` (direction="called")

**Workflow Enhancement:**
- Before matching a function, understand its call graph
- Identify leaf functions (easy targets) vs hub functions (complex)
- Find function clusters that should be matched together

### 5. Cross-Reference for Impact Analysis

**Current Pain Point:** Knowing which functions to prioritize based on impact.

**Ghidra MCP Solution:**
```
list_cross_references("/default.xex", "Symbol::Symbol")
```

Shows all locations that reference `Symbol::Symbol`, helping:
- Identify heavily-used utility functions (high impact)
- Find initialization code that sets up objects
- Trace data flow through the codebase

---

## Proposed Integrated Workflow

### Phase 0: Service Setup
```bash
# Ensure pyghidra-mcp service is running
./tools/ghidra/pyghidra-service.sh status

# If not running, start it
./tools/ghidra/pyghidra-service.sh start

# Verify XEX detection in logs
./tools/ghidra/pyghidra-service.sh logs | grep "Detected XEX"
```

### Phase 1: Target Discovery
```
1. objdiff report query -> Get near-matches (90-99%)
2. For each target:
   a. search_symbols_by_name -> Find related functions
   b. list_cross_references -> Understand call context
   c. decompile_function -> Get Ghidra's pseudo-C
```

### Phase 2: Analysis & Planning
```
1. Compare Ghidra decompile with RB3 reference
2. gen_callgraph -> Understand dependencies
3. search_strings -> Find relevant debug/error strings
4. objdiff diff --analyze -> Get pattern diagnosis
```

### Phase 3: Implementation & Verification
```
1. Write/modify C++ based on analysis
2. ninja build -> Compile
3. objdiff diff --verdict -> Check match status
4. If not matching:
   a. decompile_function -> Re-check Ghidra output
   b. read_bytes -> Inspect specific addresses
```

### Phase 4: Service Maintenance
```bash
# If service becomes unresponsive
./tools/ghidra/pyghidra-service.sh restart

# Check for errors
./tools/ghidra/pyghidra-service.sh logs | tail -50

# Stop service when done
./tools/ghidra/pyghidra-service.sh stop
```

---

## High-Value Use Cases

### A. Bulk Triage of Near-Matches

For the 787 functions at 90-99% match:
1. Get Ghidra decompile for each
2. Compare instruction count with our compiled output
3. Identify functions where Ghidra shows simple patterns (likely fixable)
4. Deprioritize functions where Ghidra shows complex inlining (likely at limit)

### B. vtable Reconstruction

Use `read_bytes` to dump vtable memory:
```
read_bytes("/default.xex", "820facf4", 64)  # GameMode vtable
```

Then cross-reference vtable entries with `search_symbols_by_name` to identify virtual function order.

### C. String Cross-Reference Maps

Build a map of feature -> strings -> functions:
```
search_strings("Campaign")       -> Campaign mode strings
search_strings("Multiplayer")    -> Multiplayer strings
search_strings("Kinect")         -> Kinect integration
```

Use xrefs from string addresses to find implementing code.

### D. Inline Function Pattern Detection

Search for known inline patterns:
```
search_code("strlen")           -> Find strlen inlines
search_code("memcpy")           -> Find memcpy inlines
search_code("vector push_back") -> Find STL patterns
```

Compare Ghidra's decompile with our code to match inline expansions.

---

## Implementation Notes

### Session Management

The server uses FastMCP with Uvicorn for HTTP transport:

**Transport Details:**
- **Protocol**: HTTP with Server-Sent Events (SSE)
- **Port**: 8000
- **Endpoint**: `http://127.0.0.1:8000/mcp/v1`
- **Transport**: FastMCP streamable-http
- **Server**: Uvicorn ASGI

**Session Initialization:**
```bash
# Initialize session (note: endpoint is /mcp/v1 for FastMCP)
curl -X POST http://127.0.0.1:8000/mcp/v1 \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize",...}'

# Use session for subsequent calls
curl -X POST http://127.0.0.1:8000/mcp/v1 \
  -H "mcp-session-id: <session-id>" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call",...}'
```

**Advantages of FastMCP/Uvicorn:**
- Simplified HTTP server setup compared to custom SSE implementations
- Built-in session management and state handling
- Automatic request/response handling and routing
- Better error handling and logging
- Production-grade ASGI server with good performance
- Eliminates the complexity of custom SSE mode

### Sandbox Considerations
Local network access requires either:
- `dangerouslyDisableSandbox: true` for curl commands
- Or configure `.claude/settings.local.json` with `allowedHosts: ["127.0.0.1", "localhost"]`

### ChromaDB Batch Limits
When re-indexing, ensure batch sizes <= 5000 (max is 5461). The local pyghidra-mcp has been patched to handle this.

---

## pyghidra-mcp Reference

**Version:** 0.1.6+ with FastMCP
**Transport:** streamable-http on port 8000 (Uvicorn)
**Project:** `$HOME/code/milohax/dc3-decomp/ghidra_projects/DC3`
**ChromaDB:** 59,513 embeddings (code + strings)

### Tool Signatures

```python
decompile_function(binary_name: str, name_or_address: str) -> DecompiledFunction
search_symbols_by_name(binary_name: str, query: str, offset=0, limit=25) -> SymbolSearchResults
search_strings(binary_name: str, query: str, limit=100) -> StringSearchResults
search_code(binary_name: str, query: str, limit=5) -> CodeSearchResults
list_cross_references(binary_name: str, name_or_address: str) -> CrossReferenceInfos
gen_callgraph(binary_name: str, function_name: str, direction="calling") -> CallGraphResult
list_exports(binary_name: str, query=".*", offset=0, limit=25) -> ExportInfos
list_imports(binary_name: str, query=".*", offset=0, limit=25) -> ImportInfos
read_bytes(binary_name: str, address: str, size=32) -> BytesReadResult
list_project_binaries() -> ProgramInfos
list_project_binary_metadata(binary_name: str) -> BinaryMetadata
```

---

## Next Steps

1. **Create helper scripts** for common MCP queries (decompile, xref, callgraph)
2. **Integrate with objdiff workflow** - auto-fetch Ghidra decompile when analyzing
3. **Build function similarity tool** - compare our C++ against Ghidra pseudo-C
4. **String map generation** - automated feature discovery via string search
5. **vtable dumper** - extract and annotate class vtables

---

*Last updated: 2026-01-25*
