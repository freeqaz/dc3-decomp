# Ghidra Setup for DC3 Decomp

Ghidra provides binary analysis and decompilation for the original DC3 executable. Integrated via pyghidra-mcp (v0.1.6+) for AI-assisted workflows.

## Prerequisites

### Java Setup

Add to `~/.profile`:

```bash
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk
export GHIDRA_INSTALL_DIR=/opt/ghidra
export GHIDRA_USER_HOME=/tmp/claude/ghidra_user
export PATH="$GHIDRA_INSTALL_DIR/support:$PATH"
```

**`GHIDRA_USER_HOME`**: Points to a writable directory for Ghidra's user settings and cache. Required when running in environments with read-only home directories or sandboxed processes. Avoids permission errors when Ghidra tries to write to `~/.ghidra/`.

### XEX Loader Extension

Xbox 360 executables require XEXLoaderWV. The extension must be installed in `$GHIDRA_INSTALL_DIR/Extensions/Ghidra/`. See [XEXLOADERWV.md](XEXLOADERWV.md) for build/install instructions and [PYGHIDRA_MCP_XEX_SUPPORT.md](../plans/PYGHIDRA_MCP_XEX_SUPPORT.md) for technical details on XEX handling.

## Symbol Lookup via Map File

**Important:** The binary is stripped - Ghidra won't have function names. Use the linker map file as the primary symbol source:

```bash
# Map file location
orig/373307D9/ham_xbox_r.map    # 119K lines of symbols

# Find function address
grep "GetPresenceMode" orig/373307D9/ham_xbox_r.map
# Output: 0005:00548b58  ?GetPresenceMode@PresenceMgr@@...  82878b58 f  game:PresenceMgr.obj
#                                                          ^^^^^^^^
#                                                          Use this address in Ghidra

# Then decompile by address in Ghidra MCP:
# decompile_function(binary_name, "0x82878b58")
```

### Map File Format

```
Section:Offset    MangledName                    Address    Type  Source
0005:00548b58     ?GetPresenceMode@PresenceMgr@@ 82878b58   f     game:PresenceMgr.obj
```

- `0005:` = .text section (code)
- `f` = function, `i` = inlined
- Address is absolute (base 0x82000000)

## Headless Analysis

Pre-analyze the DC3 binary (one-time, ~4 minutes):

```bash
/opt/ghidra/support/analyzeHeadless /tmp/pyghidra_mcp_projects/dc3-decomp dc3-decomp \
    -import orig/373307D9/default.xex -max-cpu 4
```

## XEX Binary Support

pyghidra-mcp (v0.1.6+) includes automatic Xbox 360 XEX binary detection and handling:

- **Automatic Detection**: Recognizes XEX2 magic number (`0x58455832`) in binary headers
- **Language Specification**: Auto-sets `PowerPC:BE:64:Xenon` for XEX files
- **XEXLoaderWV Integration**: Uses extension from `$GHIDRA_INSTALL_DIR/Extensions/` for import
- **No Manual Configuration**: XEX files are handled transparently by pyghidra-mcp

See [PYGHIDRA_MCP_XEX_SUPPORT.md](../plans/PYGHIDRA_MCP_XEX_SUPPORT.md) for implementation details.

## pyghidra-mcp (MCP Integration)

Configured in `.mcp.json`. The MCP server runs on **port 8000** (default) using FastMCP with Uvicorn transport. The server provides these tools:

### Analysis Tools

| Tool | Description |
|------|-------------|
| `decompile_function` | Decompile function to pseudo-C by name or address |
| `search_symbols_by_name` | Search symbols (case-insensitive substring) |
| `search_code` | Semantic search over decompiled code |
| `list_cross_references` | Find xrefs to/from function or address |
| `gen_callgraph` | Generate mermaid.js call graph |

### Type Seeding Tools

| Tool | Description |
|------|-------------|
| `bulk_create_functions` | Create function objects at addresses where auto-analysis missed them |
| `apply_demangled_signatures` | Apply full signatures (CC, return type, all params) from MSVC mangled names |
| `create_structures` | Create struct types in DTM from struct_db definitions |
| `apply_this_types` | (Legacy) Set `this*` parameter only on member functions |

### Binary Inspection

| Tool | Description |
|------|-------------|
| `list_project_binaries` | Show loaded binaries and analysis status |
| `list_project_binary_metadata` | Architecture, format, hash info |
| `list_exports` / `list_imports` | Symbol tables (with regex filter) |
| `search_strings` | Find strings in binary |
| `read_bytes` | Read raw memory at address |

### Project Management

| Tool | Description |
|------|-------------|
| `import_binary` | Load new binary into project |
| `delete_project_binary` | Remove binary from project |

## Workflow: Decompiling Unknown Functions

1. **Find address from map file:**
   ```bash
   grep "YourFunction" orig/373307D9/ham_xbox_r.map
   ```

2. **Decompile in Ghidra MCP:**
   ```
   decompile_function("/default.xex", "0x82XXXXXX")
   ```

3. **Find callers/callees:**
   ```
   list_cross_references("/default.xex", "0x82XXXXXX")
   ```

4. **Generate call graph:**
   ```
   gen_callgraph("/default.xex", "FUN_82XXXXXX", direction="calling")
   ```

## MCP Server Configuration

### Service Architecture

pyghidra-mcp (v0.1.6+) uses **FastMCP with Uvicorn transport** instead of the legacy custom SSE mode:

- **Transport**: Streamable HTTP (Uvicorn)
- **Port**: 8000 (default, configurable)
- **Protocol**: MCP over HTTP streams
- **Benefits**: Better error handling, graceful shutdown, standard HTTP tooling

### Startup Command

```bash
pyghidra-mcp \
    --transport streamable-http \
    --project-name dc3-decomp \
    --project-directory /tmp/pyghidra_mcp_projects/dc3-decomp
```

**Key Parameters:**
- `--transport streamable-http`: Use Uvicorn transport (required for MCP server mode)
- `--project-name`: Ghidra project name (creates if doesn't exist)
- `--project-directory`: Directory for Ghidra project files

**Removed Parameters** (from older versions):
- `--port`, `--host`: Now handled by FastMCP/Uvicorn defaults
- `--wait-for-analysis`, `--no-force-analysis`: Analysis is automatic on import

### Environment Variables

Required for proper operation:

```bash
export GHIDRA_INSTALL_DIR=/opt/ghidra           # Ghidra installation
export GHIDRA_USER_HOME=/tmp/claude/ghidra_user # User settings/cache (writable!)
```

## Type Seeding Pipeline

Seeds Ghidra with type information from the linker map file, enabling the decompiler to show field names, calling conventions, and full parameter types instead of raw offsets.

### Running the Pipeline

```bash
# New pipeline (recommended): bulk function creation + demangled signatures
python3 tools/ghidra/batch_export_types.py --seed

# Legacy pipeline: struct creation + this-pointer only
python3 tools/ghidra/batch_export_types.py --seed --legacy

# Full pipeline: seed + extract enriched types back to struct_db
python3 tools/ghidra/batch_export_types.py --seed --extract
```

### What the Pipeline Does

| Step | Tool | Effect |
|------|------|--------|
| 1. Parse map | local | Extracts ~79K CODE-section symbols from `ham_xbox_r.map` |
| 2. Bulk create functions | `bulk_create_functions` | Creates ~27K function objects where auto-analysis missed them |
| 3. Demangle signatures | `apply_demangled_signatures` | Applies full signatures to ~53K functions (CC, return type, all params) |
| 4. Supplementary structs | `create_structures` | Creates ~2.1K struct layouts from struct_db |

### Before/After

| Metric | Before (legacy) | After (demangled) |
|--------|-----------------|-------------------|
| Functions in Ghidra | ~42,500 | ~69,400 |
| Signatures applied | ~7,000 (this-only) | ~53,000 (full) |
| "Function not found" rate | ~40% | ~0.01% |
| Calling conventions set | 0 | ~53,000 |
| Parameter types | `this*` only | All parameters |

### When to Re-seed

Re-run `--seed` after:
- Importing a new binary into Ghidra
- Restarting the pyghidra-mcp service with a fresh project
- Updating struct_db with new header-sourced types

The `--extract` step (batch decompilation) benefits from seeding — with full signatures, the decompiler produces much higher-quality inferred types.

## Troubleshooting

### Common Issues (v0.1.6+)

| Error | Cause | Fix |
|-------|-------|-----|
| "No load spec found" | XEX loader not installed | See [XEXLOADERWV.md](XEXLOADERWV.md) |
| "Analysis incomplete" | Large binary still analyzing | Wait and retry |
| "Function not found" | Binary is stripped or no function object | Use address from map file; run `--seed` to bulk-create functions |
| Binary shows as x86/DOS | XEX loader not working | Reinstall extension to `$GHIDRA_INSTALL_DIR/Extensions/` |
| "Failed to init global cache" | `/var/tmp` not writable | Check permissions |
| Service won't start on port 8000 | Port already in use | Check `lsof -i :8000`, kill conflicting process, or change port |
| Permission denied on GHIDRA_USER_HOME | Directory not writable | Ensure `$GHIDRA_USER_HOME` points to writable location (e.g., `/tmp/claude/ghidra_user`) |
| XEX files not recognized | Missing XEXLoaderWV extension | Install to `$GHIDRA_INSTALL_DIR/Extensions/Ghidra/` |
| "Connection refused" to MCP server | Server not running or wrong port | Verify server started, check port 8000 (not 8765) |

### Debug Steps

1. **Verify environment variables:**
   ```bash
   echo $GHIDRA_INSTALL_DIR
   echo $GHIDRA_USER_HOME
   ```

2. **Check XEX extension:**
   ```bash
   ls -la $GHIDRA_INSTALL_DIR/Extensions/Ghidra/ | grep XEX
   ```

3. **Test port availability:**
   ```bash
   lsof -i :8000
   ```

4. **Check GHIDRA_USER_HOME permissions:**
   ```bash
   mkdir -p $GHIDRA_USER_HOME
   touch $GHIDRA_USER_HOME/test && rm $GHIDRA_USER_HOME/test
   ```

## See Also

- [XEXLOADERWV.md](XEXLOADERWV.md) - XEX loader build/install
- [objdiff.md](objdiff.md) - Assembly comparison workflow
- [INDEX.md](INDEX.md) - Quick command reference
