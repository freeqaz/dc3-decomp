# pyghidra-mcp XEX Support Integration

## Overview

This document describes the integration of XEX (Xbox 360 executable) support into pyghidra-mcp for the DC3 decompilation project. The integration enables Ghidra to load and analyze Xbox 360 binaries through the pyghidra-mcp MCP server.

## Architecture

### Components

1. **XEXLoaderWV Extension**: Ghidra extension that recognizes and loads XEX2 format binaries
   - Location: `ghidra_12.0_DEV/Extensions/XEXLoaderWV/`
   - Provides PowerPC:BE:64:Xenon language specification

2. **pyghidra-mcp Fork**: Local fork with XEX support enhancements
   - Location: `tools/pyghidra-mcp-fork/`
   - Installed in editable mode via pip
   - Automatically loaded for custom modifications

3. **Ghidra User Home**: Writable directory for Ghidra caches and project data
   - Location: `/tmp/claude/ghidra_user/`
   - Avoids read-only filesystem issues

4. **Service Script**: Updated pyghidra-service.sh
   - Manages pyghidra-mcp service lifecycle
   - Configures environment variables for XEX support

## Installation

### Step 1: XEXLoaderWV Extension

The extension is pre-installed at:
```
/home/free/code/milohax/vmx128-research/ghidra-test/ghidra_12.0_DEV/Extensions/XEXLoaderWV/
```

Verification:
```bash
ls -la ghidra_12.0_DEV/Extensions/XEXLoaderWV/lib/XEXLoaderWV.jar
```

### Step 2: Local pyghidra-mcp Fork

The fork is located at:
```
tools/pyghidra-mcp-fork/
```

Already installed in editable mode. To reinstall:
```bash
source venv/bin/activate
pip install -e tools/pyghidra-mcp-fork/
```

### Step 3: Environment Configuration

Set in `tools/ghidra/pyghidra-service.sh`:
```bash
export GHIDRA_INSTALL_DIR="/home/free/code/milohax/vmx128-research/ghidra-test/ghidra_12.0_DEV"
export GHIDRA_USER_HOME="/tmp/claude/ghidra_user"
```

## Enhancements to pyghidra-mcp

### 1. XEX2 Magic Number Detection

**File**: `tools/pyghidra-mcp-fork/pyghidra_mcp/context.py`

Added `_is_binary_file()` static method that recognizes XEX format:

```python
@staticmethod
def _is_binary_file(path: Path) -> bool:
    """
    Quick header-based check for common binary formats.
    Recognizes ELF (0x7f 'ELF'), PE ('MZ'), and XEX ('XEX2').
    """
    try:
        with path.open("rb") as f:
            header = f.read(4)
            if header.startswith(b"XEX2"):
                return True
            # ... other format checks
    except Exception as e:
        logger.debug(f"Could not read file header for {path}: {e}")
        return False
```

### 2. Language Specification Support

**File**: `tools/pyghidra-mcp-fork/pyghidra_mcp/context.py`

Extended `import_binary()` with optional language parameters:

```python
def import_binary(
    self, binary_path: str | Path,
    language: str | None = None,
    compiler: str | None = None
) -> Program:
    """
    Imports a single binary into the project.

    Args:
        binary_path: Path to the binary file.
        language: Optional Ghidra language ID (e.g., "PowerPC:BE:64:Xenon").
        compiler: Optional compiler spec ID within the language.
    """
    # ... implementation
    if language is None:
        program = self.project.importProgram(binary_path)
    else:
        from ghidra.program.util import DefaultLanguageService
        from ghidra.program.model.lang import LanguageID, CompilerSpecID

        service = DefaultLanguageService.getLanguageService()
        lang = service.getLanguage(LanguageID(language))
        comp = lang.getDefaultCompilerSpec() if compiler is None else \
               lang.getCompilerSpecByID(CompilerSpecID(compiler))
        program = self.project.importProgram(binary_path, lang, comp)
```

### 3. Automatic XEX Detection

**File**: `tools/pyghidra-mcp-fork/pyghidra_mcp/server.py`

Added automatic language detection in `init_pyghidra_context()`:

```python
def _detect_binary_language(binary_path: Path) -> tuple[str | None, str | None]:
    """Detect binary format and return language/compiler IDs if needed."""
    try:
        with binary_path.open("rb") as f:
            header = f.read(4)
            if header.startswith(b"XEX2"):
                # Xbox 360 executable
                return "PowerPC:BE:64:Xenon", None
    except Exception as e:
        logger.debug(f"Could not detect language for {binary_path}: {e}")
    return None, None
```

When importing binaries:
```python
for bin_path in bin_paths:
    language, compiler = _detect_binary_language(Path(bin_path))
    if language:
        logger.info(f"Detected XEX binary, using language: {language}")
    pyghidra_context.import_binary(bin_path, language=language, compiler=compiler)
```

## Usage

### Starting the Service

```bash
./tools/ghidra/pyghidra-service.sh start
```

Expected output:
```
Starting pyghidra-mcp service...
  Project: /home/free/code/milohax/dc3-decomp/ghidra_projects/DC3
  Binary: /home/free/code/milohax/dc3-decomp/orig/373307D9/default.xex
  ...
Service process is running (PID: XXXXX)
```

### Checking Service Status

```bash
./tools/ghidra/pyghidra-service.sh status
```

### Viewing Logs

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

### Stopping the Service

```bash
./tools/ghidra/pyghidra-service.sh stop
```

## XEX Import Workflow

1. **Detection**: XEX file detected by magic number check (XEX2)
2. **Language Selection**: Automatically set to PowerPC:BE:64:Xenon
3. **Import**: Ghidra imports binary using XEXLoaderWV extension
4. **Analysis**: Ghidra performs architectural analysis
5. **Ready**: Binary available for decompilation via MCP tools

## Limitations and Workarounds

### Read-Only Filesystem

**Problem**: If Ghidra installation is on a read-only filesystem, import will fail.

**Solution**: Use writable temporary directory for `GHIDRA_USER_HOME`:
```bash
export GHIDRA_USER_HOME="/tmp/claude/ghidra_user"
```

### Auto-Detection

XEX language is auto-detected. For explicit control, use:
```python
pyghidra_context.import_binary(
    binary_path,
    language="PowerPC:BE:64:Xenon"
)
```

## Testing

### Verify XEX Support

```bash
# Check if default.xex loads successfully
./tools/ghidra/pyghidra-service.sh logs | grep "Detected XEX"

# Check analysis completion
./tools/ghidra/pyghidra-service.sh logs | grep "Analysis.*complete"
```

### Manual Import Test

```python
from pathlib import Path
from pyghidra_mcp.context import PyGhidraContext
import pyghidra

pyghidra.start()
ctx = PyGhidraContext("test_project", "test_projects")
ctx.import_binary(
    Path("orig/373307D9/default.xex"),
    language="PowerPC:BE:64:Xenon"
)
```

## Future Enhancements

1. **XEXP Support**: Add support for XEXP (Xbox 360 patch format)
2. **Auto-Detection Improvement**: Better detection for mixed binary sets
3. **Performance**: Cache language specs to avoid repeated lookups
4. **Upstream**: Consider contributing XEX support back to pyghidra-mcp

## Troubleshooting

### "No load spec found" Error

**Cause**: XEXLoaderWV extension not installed or not recognized by Ghidra

**Solution**:
1. Verify extension exists: `ls ghidra_12.0_DEV/Extensions/XEXLoaderWV/lib/XEXLoaderWV.jar`
2. Restart service to reload extensions
3. Check `GHIDRA_INSTALL_DIR` points to correct Ghidra build

### "Read-only file system" Error

**Cause**: Ghidra trying to write to read-only directory

**Solution**:
1. Ensure `GHIDRA_USER_HOME` points to writable directory
2. Check `/tmp/claude/` is writable
3. Verify Ghidra project directory is writable

### Service Won't Start

**Cause**: Multiple issues possible

**Debugging**:
```bash
# Check logs
tail -50 /tmp/claude/pyghidra-mcp-dc3.log

# Check if process is actually running
ps aux | grep pyghidra

# Try manual import to get better error messages
source venv/bin/activate
python -m pyghidra_mcp --transport stdio orig/373307D9/default.xex
```

## Related Documentation

- [WORKFLOW.md](./WORKFLOW.md) - General decomp workflow
- [OBJDIFF_CLI_USAGE.md](./OBJDIFF_CLI_USAGE.md) - Comparing compiled output
- [docs/decomp/TECHNICAL_NOTES.md](../decomp/TECHNICAL_NOTES.md) - Technical details
