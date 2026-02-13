# Ghidra Tooling Phase 1: MSVC String Symbol Resolution

**Date**: 2026-02-08
**Goal**: Decode MSVC `??_C@...` string literal symbols in the pyghidra-mcp tool, enhance MapFileParser with string and multi-symbol lookups, and switch from the checked-in fork to the external pyghidra-mcp repo.

## Problem Summary

The pyghidra-mcp tool parses MSVC linker `.map` files to resolve symbol names, but it could not decode **string literal symbols**. MSVC encodes string constants as mangled symbols like `??_C@_0O@EPEJKEFM@nar_bam_trans?$AA@`. Without decoding, the Ghidra MCP integration could not display the actual string values referenced at a given address, making decompilation output harder to read and cross-reference analysis less useful.

Additionally, the project's Python virtualenv was using a checked-in fork at `tools/pyghidra-mcp-fork/` instead of the actively-developed external repo at `/home/free/code/milohax/pyghidra-mcp/`. All new development needed to target the external repo.

---

## MSVC String Literal Encoding

MSVC encodes string constants as symbols with the general format:

```
??_C@_<flag><len>@<hash>@<literal>?$AA@
```

| Component | Meaning |
|-----------|---------|
| `??_C@` | String literal prefix |
| `_0` or `_1` | Encoding flag (0 = single byte, 1 = wide char) |
| `<len>` | Base-32 encoded length |
| `<hash>` | 7-8 uppercase letter hash |
| `<literal>` | The string content with escape sequences |
| `?$AA@` | Null terminator marker |

### Two Length Formats

A key discovery during implementation: the length field has **two encoding formats** that change the symbol's structure:

1. **Single-digit length**: No `@` separator between length digit and hash
   - Example: `??_C@_07LEAMOHCB@App?4cpp?$AA@` -- length `7`, hash `LEAMOHCB`

2. **Letter/multi-char length**: `@` separator between length and hash
   - Example: `??_C@_0O@EPEJKEFM@nar_bam_trans?$AA@` -- length `O`, hash `EPEJKEFM`

### Escape Sequences

Multi-char: `?$CK`=`*`, `?$CF`=`%`, `?$DO`=`>`, `?$DM`=`<`, `?$DN`=`=`
Single-char: `?1`=`/`, `?2`=`\`, `?3`=`:`, `?4`=`.`, `?5`=space, `?6`=`\n`, `?8`=`'`

---

## Implementation

### `decode_msvc_string_literal()` Function

**File**: `/home/free/code/milohax/pyghidra-mcp/src/pyghidra_mcp/symbol_lookup.py`

The function uses a dual-format regex strategy: try the single-digit length format first, then fall back to the letter/multi-char format.

```python
def decode_msvc_string_literal(mangled: str) -> Optional[str]:
    if not mangled.startswith("??_C@"):
        return None

    # Try single-digit length format first (no @ between length and hash)
    match = re.match(r"\?\?_C@_[01]([0-9])([A-Z]{7,8})@(.+)\?\$AA@$", mangled)
    if match:
        literal = match.group(3)
    else:
        # Try letter/multi-char length format (@ between length and hash)
        match = re.match(r"\?\?_C@_[01]([A-Z0-9]+)@([A-Z]{7,8})@(.+)\?\$AA@$", mangled)
        if not match:
            return None
        literal = match.group(3)

    # Decode multi-char escapes first, then single-char
    # ... (escape replacement logic)
    return literal
```

### MapFileParser Enhancements

**File**: `/home/free/code/milohax/pyghidra-mcp/src/pyghidra_mcp/symbol_lookup.py`

Added three new data structures to `__init__`:

```python
self._address_to_string: Dict[int, str] = {}      # addr -> decoded string
self._address_to_symbols: Dict[int, List[str]] = {} # addr -> [mangled, ...]
```

Updated `parse()` to populate these during map file parsing:

```python
# Check if this is a string symbol
string_value = decode_msvc_string_literal(symbol)
if string_value is not None:
    self._address_to_string[rva_base] = string_value

# Store in address->symbols list (for ICF-merged lookup)
if rva_base not in self._address_to_symbols:
    self._address_to_symbols[rva_base] = []
self._address_to_symbols[rva_base].append(symbol)
```

Added two new lookup methods:

- **`lookup_string_by_address(address)`**: Returns the decoded string literal at an address, or `None` if not a string symbol.

- **`lookup_all_symbols_by_address(address)`**: Returns all `SymbolInfo` objects at an address, handling Identical COMDAT Folding (ICF) where the linker merges functions with identical machine code to a single address.

---

## Regex Debugging Journey

The regex went through three iterations before all tests passed.

### Attempt 1: Single-Format Regex

Initial regex: `r"\?\?_C@_[01]([A-Z0-9]+)@([A-Z]+)@(.+)\?\$AA@$"`

**Failure**: `??_C@_07LEAMOHCB@App?4cpp?$AA@` returned `None`. The regex consumed `7LEAMOHCB` as one token for the length field (since `[A-Z0-9]+` is greedy), leaving nothing for the hash.

### Attempt 2: Dual-Format with Fixed Hash Length

Split into two regexes: single-digit format with `[A-Z]{8}` hash, and multi-char format.

**Failure**: `??_C@_0BB@OEPBHON@Couldn?8t?5load?5?$CFs?$AA@` returned `None`. The hash `OEPBHON` is 7 characters, not 8. The assumption that hashes are always 8 characters was wrong.

### Attempt 3: Variable Hash Length (Final)

Relaxed hash constraint to `[A-Z]{7,8}` to handle both 7-char and 8-char hashes. All tests passed.

### Stale Bytecode Cache

After fixing the regex, tests initially still failed because Python's bytecode cache (`.pyc` files) cached the old version of the module. Invalidating the cache resolved the issue.

---

## Fork-to-External-Repo Switch

### Problem

The project's Python virtualenv was configured to use a checked-in fork at `tools/pyghidra-mcp-fork/` via `easy-install.pth`. All new code was being written to the external repo at `/home/free/code/milohax/pyghidra-mcp/`, but tests imported from the fork and could not find the new functions.

### Solution

Updated the editable install path in `venv/lib/python3.10/site-packages/easy-install.pth`:

```
# Before:
/home/free/code/milohax/dc3-decomp/tools/pyghidra-mcp-fork/src

# After:
/home/free/code/milohax/pyghidra-mcp/src
```

A `pip install -e` was attempted first but failed due to proxy connection issues. Directly editing the `.pth` file achieved the same result. The checked-in fork at `tools/pyghidra-mcp-fork/` was kept in the repo but is no longer used.

---

## Tests

**File**: `/home/free/code/milohax/pyghidra-mcp/tests/unit/test_symbol_lookup.py`

Added 11 new tests for `decode_msvc_string_literal()` covering: simple strings, each escape type (period, forward slash, asterisk, apostrophe, percent, greater/less-than, equals, backslash, colon, space), non-string symbol rejection, and malformed symbol rejection. Combined with 11 existing tests (demangling, method/class extraction), **all 22 tests pass**.

---

## Files Changed

```
/home/free/code/milohax/pyghidra-mcp/
├── src/pyghidra_mcp/symbol_lookup.py    # decode_msvc_string_literal() + MapFileParser enhancements
└── tests/unit/test_symbol_lookup.py     # 11 new string literal tests

/home/free/code/milohax/dc3-decomp/
└── venv/lib/python3.10/site-packages/easy-install.pth  # Fork -> external repo path
```

---

## Key Decisions

1. **Dual-format regex over single greedy regex**: The two length formats have fundamentally different structures. Two explicit patterns are clearer than a single regex with complex lookahead.

2. **Variable hash length `{7,8}`**: Real-world symbols showed both 7-char and 8-char hashes.

3. **`.pth` file edit over `pip install -e`**: When pip fails due to network issues, directly editing the `.pth` file achieves the same result for editable installs.

4. **Keep the fork, just stop using it**: The checked-in fork was retained for reference but removed from the import path.

---

## References

- [MSVC Name Mangling](https://en.wikiversity.org/wiki/Visual_C%2B%2B_name_mangling) -- General MSVC mangling documentation
- `symbol_lookup.py` -- Main implementation file with `decode_msvc_string_literal()` and `MapFileParser`
- `test_symbol_lookup.py` -- Unit tests for string decoding and demangling
