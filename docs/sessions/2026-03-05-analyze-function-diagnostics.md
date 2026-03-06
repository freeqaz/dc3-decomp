# Session: analyze_function Source Diagnostics + MCPClient Cleanup

**Date**: 2026-03-05
**Motivation**: [CharLipSync session](2026-03-05-charlipsync-session.md) revealed that template type mismatches and assert text differences cause cascading `bl` target mismatches in objdiff, but are hard to spot manually.

## Changes

### 1. MCPClient Consolidation (-305 lines)

Replaced the 350-line inline `MCPClient` class in `tools/analyze_function.py` with a thin adapter over the shared `tools/ghidra/mcp_client.py`. The adapter bridges return type differences:

- `initialize()`: shared raises `MCPError`, adapter returns `bool`
- `decompile_function()`: shared returns `dict`, adapter returns `(code, name, addr)` tuple
- `list_cross_references()`: shared returns `dict`, adapter returns `(callers, callees)` tuple
- `search_symbols()`: shared returns `dict`, adapter returns `list[{name, address}]`

Also deleted `check_service_health()` — it was only used for informational logging in `main()` and the shared client handles connection errors internally.

### 2. Template Type Mismatch Detection

New `detect_template_mismatches()` scans objdiff instruction data for `bl` entries where target and base call different template instantiations of the same class (e.g., `vector<String>::push_back` vs `vector<FilePath>::push_back`).

**How it works**: Parses MSVC mangled names for `?$` (template class prefix), extracts class name and template parameters, compares. No Ghidra needed — pure string parsing of data already in the objdiff JSON.

### 3. MakeString/Assert Text Detection

New `detect_makestring_mismatches()` decodes MSVC mangled array dimensions from `MakeString` template symbols to compare `#cond` string lengths.

**MSVC array dimension encoding**:
- Single digit 0-9: value is digit + 1 (e.g., '6' = 7)
- Multi-digit A-P + '@' terminator: hex encoding (A=0, B=1, ..., P=15)

The parser handles the tricky case where 'D' appears both as a hex digit in dimensions and as the char type marker after the dimension.

### 4. Output Integration

Both diagnostics appear in a new `## Source Diagnostics` section in markdown output, and `source_diagnostics` key in JSON output. Only emitted when findings exist.

## Files Modified

| File | Change |
|------|--------|
| `tools/analyze_function.py` | MCPClient adapter (-305 lines), diagnostics (+~200 lines), dead code removal |
| `tests/test_source_diagnostics.py` | 24 unit tests for detection functions |
| `docs/sessions/2026-03-05-analyze-function-diagnostics.md` | This file |

## Test Coverage

- Template mismatch: same class/different params, identical symbols, non-template, ICF equal-match-type, missing target/base
- MSVC array decoding: single digit, multi-digit (BA@, CD@, A@), leading zero, empty, invalid
- MakeString: cond length mismatch, identical, non-MakeString, file-only difference
- MCPClient adapter: initialize returns bool on connection failure
