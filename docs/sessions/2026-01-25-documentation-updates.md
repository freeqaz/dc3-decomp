# Documentation Updates - pyghidra-mcp v0.1.6 Integration

**Date**: 2026-01-25
**Scope**: Comprehensive update to reflect new pyghidra-mcp architecture with FastMCP/Uvicorn transport

## Overview

This document summarizes all documentation updates made to reflect the new pyghidra-mcp v0.1.6 integration with XEX support. The major change is the migration from custom SSE mode on port 8765 to FastMCP with Uvicorn on port 8000.

## Files Updated

### 1. Configuration Files

#### `.mcp.json` ✅
**Changes**:
- Updated `pyghidra` server URL from `http://127.0.0.1:8765/mcp` to `http://127.0.0.1:8000/mcp/v1`
- Updated `pyghidra-stdio` command parameters:
  - **Removed**: `--project-path`, `--wait-for-analysis`, `--no-force-analysis`
  - **Added**: `--transport stdio`, `--project-name DC3`, `--project-directory`, binary path
- Updated `GHIDRA_INSTALL_DIR` from `/opt/ghidra` to `/home/free/code/milohax/vmx128-research/ghidra-test/ghidra_12.0_DEV`
- **Added**: New `GHIDRA_USER_HOME=/tmp/claude/ghidra_user` environment variable

**Impact**: Ensures both SSE and stdio transports connect to correct port and endpoint

---

### 2. Tool Documentation

#### `docs/tools/GHIDRA.md` ✅
**Changes**:
- Updated version reference: `v0.1.13+` → `v0.1.6+`
- Changed all port references: `8765` → `8000`
- Updated service architecture description from custom SSE to FastMCP/Uvicorn
- **Added**: New "XEX Binary Support" section documenting:
  - Automatic XEX2 magic number detection
  - Automatic PowerPC:BE:64:Xenon language specification
  - XEXLoaderWV extension integration
- **Added**: Environment variable `GHIDRA_USER_HOME` with rationale
- **Added**: MCP Server Configuration section with:
  - Service Architecture explanation
  - Updated startup commands with new parameters
  - Removed parameters documentation
- **Expanded**: Troubleshooting section with:
  - Port 8000 conflict resolution
  - GHIDRA_USER_HOME permission issues
  - XEX file recognition problems
  - New debug steps for v0.1.6+

**Impact**: Users have clear, accurate documentation for new service architecture

#### `docs/tools/GHIDRA_SETUP.md` ✅
**Changes**:
- Updated all port references: `8765` → `8000`
- **Added**: Environment variables documentation:
  - `GHIDRA_USER_HOME=/tmp/claude/ghidra_user` (read-only filesystem avoidance)
  - `GHIDRA_INSTALL_DIR` pointing to VMX128 build
- Updated service CLI parameters to match new format
- **Added**: Service lifecycle commands documentation (start, stop, status, restart, logs)
- **Added**: XEX Support section explaining:
  - Automatic XEX2 detection via magic number
  - Automatic PowerPC:BE:64:Xenon language specification
  - Expected log output showing successful detection
- Updated version to v0.1.6+

**Impact**: New users can set up service correctly without outdated parameter errors

#### `docs/tools/GHIDRA_MCP_INTEGRATION.md` ✅
**Changes**:
- **Added**: Complete 4-component architecture section:
  1. XEXLoaderWV Extension
  2. pyghidra-mcp Fork (v0.1.6+ with FastMCP)
  3. Ghidra User Home (writable directory)
  4. Service Script (lifecycle management)
- Updated version: `0.1.13+` → `0.1.6+`
- Changed port: `8765` → `8000`
- Updated endpoint: `/mcp` → `/mcp/v1`
- **Added**: Environment Setup section documenting:
  - Why GHIDRA_INSTALL_DIR is needed
  - Critical importance of GHIDRA_USER_HOME
  - What each variable controls
- **Added**: Service Management section documenting:
  - All service script commands
  - Expected outputs and log messages
  - Environment variable handling
- **Added**: Comprehensive XEX Integration section with:
  - XEX2 magic number detection code
  - Automatic language detection implementation
  - XEXLoaderWV functionality
  - Reference to PYGHIDRA_MCP_XEX_SUPPORT.md
- **Updated**: Session Management section with:
  - FastMCP/Uvicorn architecture details
  - New endpoint and transport
  - Advantages over old SSE mode
  - Curl examples with correct endpoint
- **Enhanced**: Workflow Integration with:
  - Service Setup phase
  - Service Maintenance phase

**Impact**: Complete reference architecture for new integration

#### `docs/tools/ANALYZE_FUNCTION.md` ✅
**Changes**:
- Updated MCP server URL: `http://127.0.0.1:8765/mcp` → `http://127.0.0.1:8000/mcp/v1`
- **Added**: Service Startup subsection with:
  - Commands to start/check/debug service
  - Expected initialization time
  - Log file location
- **Added**: XEX Binary Support subsection documenting:
  - Native XEX file support via XEXLoaderWV
  - No ELF conversion needed
  - Automatic language specification
- **Expanded**: Troubleshooting section with:
  - Service Connection Issues subsection with step-by-step resolution
  - Port configuration details (8765 → 8000 change)
  - Service startup failure causes
  - XEX Binary Issues subsection
  - Enhanced existing troubleshooting entries

**Impact**: Users can properly diagnose and fix service connection issues

#### `docs/tools/WORKFLOW.md` ✅
**Status**: Verified - no changes needed
**Reason**: Workflow guide references service generically; still accurate

#### `docs/tools/INDEX.md` ✅
**Status**: Verified - no changes needed
**Reason**: Index is still accurate; detailed docs have been updated

---

### 3. Implementation Scripts

#### `tools/ghidra/mcp_client.py` ✅
**Changes**:
- Updated default MCP_URL: `http://127.0.0.1:8765/mcp` → `http://127.0.0.1:8000/mcp/v1`
- **Added**: FastMCP compatibility comments
- **Enhanced**: SSE response parser documentation for compatibility with both old and new transports
- **Improved**: Error messages with service management command hint
- **Added**: Comprehensive troubleshooting section documenting:
  - Port configuration (8765 → 8000)
  - Connection error handling
  - Service startup procedures
  - Response format compatibility

**Compatibility**: ✅ Works with new port 8000 and FastMCP transport

#### `tools/analyze_function.py` ✅
**Changes**:
- Updated MCP_URL: `http://127.0.0.1:8765/mcp` → `http://127.0.0.1:8000/mcp/v1`
- **Added**: Comment explaining FastMCP port change

**Compatibility**: ✅ No breaking changes

---

### 4. New Documentation

#### `docs/tools/PYGHIDRA_MCP_XEX_SUPPORT.md` ✅ (Created)
**Content**:
- Comprehensive XEX support integration guide
- Architecture overview with 4 components
- Installation instructions for XEXLoaderWV
- Detailed enhancements to pyghidra-mcp:
  - XEX2 magic number detection
  - Language specification support
  - Automatic XEX detection
- Usage examples and troubleshooting
- Testing procedures
- Future enhancement ideas

**Impact**: Complete reference for XEX-specific functionality

---

## Summary of Changes by Category

### Port Changes
| Old | New | Files |
|-----|-----|-------|
| 8765 | 8000 | .mcp.json, GHIDRA.md, GHIDRA_SETUP.md, GHIDRA_MCP_INTEGRATION.md, ANALYZE_FUNCTION.md, mcp_client.py, analyze_function.py |

### Endpoint Changes
| Old | New | Reason |
|-----|-----|--------|
| `/mcp` | `/mcp/v1` | FastMCP uses versioned API endpoints |

### Transport Changes
| Old | New | Reason |
|-----|-----|--------|
| Custom SSE on port 8765 | FastMCP/Uvicorn on port 8000 | Production-grade HTTP server, simplified architecture |

### CLI Parameter Changes
| Removed | Added | Impact |
|---------|-------|--------|
| `--project-path` | `--project-directory` | Clearer parameter name |
| `--wait-for-analysis` | (automatic) | New version handles analysis automatically |
| `--no-force-analysis` | (automatic) | New version is smarter about analysis |
| (none) | `--transport` | Explicit transport specification |
| (none) | `--project-name` | Separate project naming from directory |

### New Environment Variables
| Variable | Value | Purpose |
|----------|-------|---------|
| `GHIDRA_USER_HOME` | `/tmp/claude/ghidra_user` | Avoid read-only filesystem errors |
| `GHIDRA_INSTALL_DIR` | `/home/free/.../ghidra_12.0_DEV` | Point to VMX128-enabled Ghidra |

### Version Changes
| Component | Old | New | Reason |
|-----------|-----|-----|--------|
| pyghidra-mcp | 0.1.13+ | 0.1.6+ | FastMCP architecture |
| Transport | Custom SSE | FastMCP/Uvicorn | Production-grade server |

---

## Impact Assessment

### Critical for Users
1. ⚠️ **Port changed to 8000**: All service connections must use new port
2. ⚠️ **New GHIDRA_USER_HOME required**: Prevents read-only filesystem errors
3. ⚠️ **CLI parameters changed**: Old commands will fail with new service

### New Capabilities
1. ✨ **XEX support automatic**: XEX files detected and handled transparently
2. ✨ **Better service stability**: FastMCP/Uvicorn is production-grade
3. ✨ **Clearer configuration**: Separated concerns in parameters

### Backward Compatibility
1. ❌ **Not backward compatible** with old service commands
2. ❌ **Port hardcoded** - old 8765 references will fail
3. ✅ **Client code compatible** - mcp_client.py handles both transports

---

## Migration Checklist

For users updating from old configuration:

- [ ] Update `.mcp.json` with new port (8000) and parameters
- [ ] Set `GHIDRA_USER_HOME=/tmp/claude/ghidra_user`
- [ ] Update GHIDRA_INSTALL_DIR to VMX128 build path
- [ ] Run `./tools/ghidra/pyghidra-service.sh restart`
- [ ] Verify service starts: `./tools/ghidra/pyghidra-service.sh status`
- [ ] Check logs for XEX detection: `./tools/ghidra/pyghidra-service.sh logs | grep "XEX"`
- [ ] Test connection: `python3 tools/ghidra/mcp_client.py`

---

## Testing Status

### Verified Working
✅ `.mcp.json` - Port 8000, new CLI params, environment variables
✅ `GHIDRA.md` - Port references, XEX section, troubleshooting
✅ `GHIDRA_SETUP.md` - Environment vars, CLI params, XEX section
✅ `GHIDRA_MCP_INTEGRATION.md` - Architecture, environment setup, XEX integration
✅ `ANALYZE_FUNCTION.md` - Service connection, XEX support, troubleshooting
✅ `mcp_client.py` - Port 8000 connection, FastMCP compatibility
✅ `analyze_function.py` - Updated MCP URL
✅ `PYGHIDRA_MCP_XEX_SUPPORT.md` - Complete XEX integration guide

### Service Verification
✅ Service starts on port 8000
✅ XEX file detected automatically
✅ Analysis completes successfully
✅ Service responds to MCP requests

---

## Related Documents

- **Implementation**: `docs/tools/PYGHIDRA_MCP_XEX_SUPPORT.md` - XEX integration details
- **Original Plan**: Plan file (completed phases 1-6)
- **Architecture**: `docs/tools/GHIDRA_MCP_INTEGRATION.md` - Full architectural reference
- **Setup Guide**: `docs/tools/GHIDRA_SETUP.md` - Quick start guide

---

## Questions?

Refer to:
1. **"How do I start the service?"** → `docs/tools/GHIDRA_SETUP.md`
2. **"What's the new architecture?"** → `docs/tools/GHIDRA_MCP_INTEGRATION.md`
3. **"How does XEX work now?"** → `docs/tools/PYGHIDRA_MCP_XEX_SUPPORT.md`
4. **"Service won't connect"** → `docs/tools/GHIDRA.md` Troubleshooting section
5. **"How do I use analyze-function?"** → `docs/tools/ANALYZE_FUNCTION.md`
