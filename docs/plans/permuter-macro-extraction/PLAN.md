# Plan: Permuter Macro Extraction via Synthetic Reparse

**Status**: Reviewed. Approach validated, 3 issues fixed (see Review Notes below).

## Problem

The permuter's `extract_function()` uses tree-sitter to find `function_definition` nodes, but **1,471 functions** are defined via macros (`BEGIN_LOADS`, `BEGIN_SAVES`, `BEGIN_COPYS`, `BEGIN_HANDLERS`, `BEGIN_PROPSYNCS`, etc.). The batch_validate errors on these with "no output (exit 1)" because tree-sitter doesn't recognize macro invocations as function definitions.

## Why Tree-Sitter Can't Handle This

Tree-sitter is a **syntactic parser**, not a preprocessor. When it sees `BEGIN_HANDLERS(Game)`, it parses it as a call expression — it has no way to know the macro expands to `DataNode Game::Handle(DataArray *_msg, bool _warn) {`. Tree-sitter deliberately skips preprocessing for speed and incremental parsing.

Alternatives considered and rejected:

| Approach | Why Not |
|---|---|
| Run `cpp` first | Expands ALL macros + `#include`s. Byte offsets destroyed, source unrecognizable. |
| Custom tree-sitter grammar | Tree-sitter doesn't support per-project macro definitions. |
| Clang AST | Overkill, slow, requires full build environment. |

## Solution: Regex Detection + Synthetic Reparse + OffsetNode Proxy

When normal tree-sitter extraction fails:

1. **Regex-detect** the `BEGIN_XXX(ClassName)` / `END_XXX` macro pair
2. **Extract** the body bytes between the markers
3. **Wrap** in a synthetic function: `void _f() {\n` + body + `\n}\n`
4. **Parse** the synthetic source with tree-sitter (gets clean AST of body statements)
5. **Wrap** all returned nodes in `OffsetNode` proxies that shift `start_byte`/`end_byte` back to original file positions
6. Return `FunctionContext` with `file_source = original file` (macros intact)

### Why This Works

The body bytes between `BEGIN_*`/`END_*` are **byte-identical** in the synthetic and original files. Patterns splice into `ctx.file_source` (the original file) using byte offsets from nodes. With OffsetNode adjusting those offsets, the splice replaces the correct bytes in the original file. The compiler then preprocesses and compiles the modified file normally.

### Offset Calculation

```
Synthetic:  "void _f() {\n" + body_bytes + "\n}\n"
                              ^-- byte 13 in synthetic

Original:   ...BEGIN_XXX(Class)\n + body_bytes + END_XXX...
                                   ^-- byte `body_start` in original

Offset = body_start - 13

OffsetNode.start_byte = inner_node.start_byte + offset
  → correctly points into original file
```

## Macro Definitions Reference

From `src/system/obj/Object.h`:

| Macro | Expands To | END Macro |
|---|---|---|
| `BEGIN_LOADS(C)` | `void C::Load(BinStream &bs) {` | `END_LOADS` → `}` |
| `BEGIN_SAVES(C)` | `void C::Save(BinStream &bs) {` | `END_SAVES` → `}` |
| `BEGIN_COPYS(C)` | `void C::Copy(const Hmx::Object *o, CopyType ty) {` | `END_COPYS` → `}` |
| `BEGIN_HANDLERS(C)` | `DataNode C::Handle(DataArray *_msg, bool _warn) { Symbol sym = ...; MessageTimer timer(...);` | `END_HANDLERS` → `if (_warn) MILO_NOTIFY(...); return DATA_UNHANDLED; }` |
| `BEGIN_CUSTOM_HANDLERS(C)` | Same as HANDLERS but with `dynamic_cast` in END | `END_CUSTOM_HANDLERS` → similar |
| `BEGIN_PROPSYNCS(C)` | `bool C::SyncProperty(...) { if (_i == _prop->Size()) return true; else { Symbol sym = ...;` | `END_PROPSYNCS` → `return false; } }` |

### Actual Macro Counts (from exhaustive grep)

| Macro | Occurrences | Files |
|---|---|---|
| BEGIN_HANDLERS | 417 | 402 |
| BEGIN_PROPSYNCS | 289 | 285 |
| BEGIN_COPYS | 244 | 240 |
| BEGIN_SAVES | 230 | 223 |
| BEGIN_LOADS | 220 | 213 |
| BEGIN_CUSTOM_PROPSYNC | 64 | 45 |
| BEGIN_CUSTOM_HANDLERS | 7 | 7 |
| **Total** | **1,471** | **427 unique files** |

All macros are at column 0 (never indented). Some files have multiple macros of the same type (e.g., `Sequence.cpp`: 28 macros for 8 classes).

### Method Name → Macro Mapping

```python
_MACRO_MAP = {
    "Load":         [("BEGIN_LOADS",           "END_LOADS")],
    "Save":         [("BEGIN_SAVES",           "END_SAVES")],
    "Copy":         [("BEGIN_COPYS",           "END_COPYS")],
    "Handle":       [("BEGIN_HANDLERS",        "END_HANDLERS"),
                     ("BEGIN_CUSTOM_HANDLERS", "END_CUSTOM_HANDLERS")],
    "SyncProperty": [("BEGIN_PROPSYNCS",       "END_PROPSYNCS")],
}
# Note: BEGIN_CUSTOM_PROPSYNC (64 occurrences) is deferred — it generates
# a free function PropSync(), not a Class::Method, requiring different matching.
```

## Known Limitations

### Semicolonless Inner Macros

Inner macros (`SAVE_REVS(1, 0)`, `LOAD_SUPERCLASS(...)`, `HANDLE_ACTION(...)`) have no explicit semicolons — the semicolons/braces are inside macro expansions. Tree-sitter error recovery triggers for these.

Typical Load body:
```cpp
LOAD_REVS(bs)                  // ← no semicolon → tree-sitter error node
ASSERT_REVS(10, 0)             // ← same
LOAD_SUPERCLASS(Hmx::Object)   // ← same
if (d.rev < 9) {               // ← real C++, parses fine
    RndTransformableRemover t;
    t.Load(bs);
}
d >> mField;                   // ← real C++, parses fine
```

Tree-sitter error recovery isolates the macro calls and correctly parses subsequent valid C++ statements.

### Realistic Permutation Value by Function Type

| Type | Count | Body content | Tree-sitter quality | Permutation value |
|---|---|---|---|---|
| Load | ~220 | Macro preamble + real C++ (if/else, decls, `>>` exprs) | Good after preamble | **HIGH** |
| Save | ~230 | Macro preamble + `bs << field;` lines | Good after preamble | **MEDIUM** |
| Copy | ~244 | Almost all macros (COPY_MEMBER, etc.) | Poor — mostly error nodes | **LOW** |
| Handler | ~424 | Almost all macros (HANDLE_ACTION, etc.) | Poor — mostly error nodes | **LOW** |
| PropSync | ~353 | Almost all macros (SYNC_PROP, etc.) | Poor — mostly error nodes | **LOW** |

Real permutation value is concentrated in **Load (~220) and Save (~230)** functions. The other ~800 functions will be extractable but patterns will find few permutation targets within them.

### BEGIN_CUSTOM_PROPSYNC Not Covered

64 occurrences across 45 files. This macro generates a **free function** `PropSync(ClassName&, ...)`, not a member function. It requires different name-matching logic since there's no `Class::Method` pattern. Deferred for a future extension.

## Implementation Details

All changes in `scripts/permuter/extractor.py` (~125 new lines, ~10 modified).

### 1. OffsetNode Class (~50 lines)

Wraps a tree-sitter `Node` and shifts byte offsets by a fixed amount. Required properties (confirmed by exhaustive grep of all 13 pattern files):

| Property | Behavior | Used by |
|---|---|---|
| `start_byte` | Adjusted (+offset) | All 12 pattern files |
| `end_byte` | Adjusted (+offset) | All 12 pattern files |
| `type` | Delegated | All 13 files |
| `text` | Delegated (body bytes identical) | 12 files |
| `child_by_field_name()` | Returns wrapped child | All 13 files |
| `children` | Returns wrapped children | 7 files |
| `named_children` | Returns wrapped children | 5 files |
| `parent` | Returns wrapped parent | 4 files |
| `__eq__` / `__hash__` | Based on inner node identity | See below |

Not needed (confirmed zero usage): `is_named`, `child_count`, `named_child_count`, `next_sibling`, `prev_sibling`.

**Critical: `__eq__` and `__hash__` are required.** `inline_assignment.py:154` compares `parent != stmt` where `parent` is a freshly-created OffsetNode from `.parent` traversal and `stmt` is an existing OffsetNode. Without `__eq__`, Python uses identity comparison, which always fails for different wrapper instances → the parent-chain loop never terminates at the statement boundary.

```python
def __eq__(self, other):
    if isinstance(other, OffsetNode):
        return self._inner == other._inner
    return NotImplemented

def __hash__(self):
    return hash(self._inner.id)
```

Tree-sitter's `Node.__eq__` compares underlying C node pointers, so two Python wrappers of the same C node compare equal. Our `__eq__` delegates to this.

### 2. `_find_macro_region()` (~25 lines)

Regex-searches for `BEGIN_XXX(ClassName)` at line start, then finds the **first** `END_XXX` occurring after that match. Returns `(macro_start, body_start, body_end, macro_end)` byte offsets.

- `macro_start`: byte offset of `BEGIN_XXX`
- `body_start`: byte after the newline following `BEGIN_XXX(...)`
- `body_end`: byte before `END_XXX` (trim trailing whitespace/newline)
- `macro_end`: byte after `END_XXX` (or after trailing newline)

**Important**: `END_XXX` takes no class name argument, so files with multiple `BEGIN_XXX` for different classes (e.g., `Sequence.cpp` with 28 macros, `NavListNode.cpp` with 4 `BEGIN_HANDLERS`) require positional matching — search forward from the BEGIN match, not globally.

### 3. `_try_macro_extraction()` (~30 lines)

- Parse `func_name` → `(class_name, method_name)` via `::` split
- Look up `method_name` in `_MACRO_MAP`
- Call `_find_macro_region()` for each candidate macro pair
- Extract body bytes, wrap in `void _f() {\n` + body + `\n}\n`
- Parse synthetic with tree-sitter
- Calculate offset: `body_start - len(SYNTHETIC_PREFIX)`
- Return `FunctionContext` with:
  - `func_node`: OffsetNode-wrapped function_definition from synthetic parse
  - `body_node`: OffsetNode-wrapped compound_statement
  - `statements`: OffsetNode-wrapped named_children of body
  - `file_source`: original file bytes (unchanged)
  - `func_byte_range`: `(macro_start, macro_end)`

### 4. Modify `extract_function()` (~10 lines)

After normal extraction fails, call `_try_macro_extraction()` before raising ValueError. Include macro-defined function names in the "available functions" error message.

### No Changes Needed

- `types.py` — FunctionContext unchanged
- All patterns — OffsetNode is transparent
- `generator.py` — no change
- `scorer.py` — no change
- `batch_validate.py` — no change

## Verification Plan

1. **Dry-run** on a Load function:
   ```bash
   python -m decomp_synth --source src/system/char/CharBone.cpp \
     --function CharBone::Load --symbol <sym> --dry-run
   ```

2. **Dry-run** on a Handler function:
   ```bash
   python -m decomp_synth --source src/system/char/CharBone.cpp \
     --function CharBone::Handle --symbol <sym> --dry-run
   ```

3. **Full scoring** on a known macro function to verify build+score works.

4. **batch_validate** rerun to confirm reduced "no output (exit 1)" errors.

---

## Review Notes (2026-02-13)

### Issues Found and Fixed in This Document

1. **OffsetNode needs `__eq__`/`__hash__`** — Without this, `inline_assignment.py:154` (`parent != stmt`) silently fails because `.parent` creates new OffsetNode instances that differ by identity from the original `stmt`. Fixed: added `__eq__`/`__hash__` requirement to OffsetNode spec.

2. **END_XXX matching must be positional** — Files like `Sequence.cpp` (28 macros) and `NavListNode.cpp` (4 `BEGIN_HANDLERS`) have multiple macros of the same type. `END_XXX` takes no class name, so regex must find the *first* END after the matched BEGIN. Fixed: clarified in `_find_macro_region()` spec.

3. **Macro count was understated** — Actual count is 1,471 (not ~1,200). Added detailed breakdown and realistic permutation-value assessment showing Load+Save (~450) are the high-value targets.

### Confirmed Safe (No Changes Needed)

- **`is not` identity check** (`declaration_reorder.py:227`) — Patterns reorder the SAME OffsetNode objects from `ctx.statements`, not new wrappers. Identity comparison works correctly.
- **`.index(node)` lookup** (`declaration_reorder.py:63`) — Same-object reuse means `.index()` finds by identity. Also safe with `__eq__` added.
- **SourceEditor compatibility** (`editor.py`) — Uses `_HasByteRange` protocol (duck type). OffsetNode's adjusted `start_byte`/`end_byte` properties satisfy the protocol and correctly target the original file.
- **`ast_queries.walk()` compatibility** — All recursive walkers use duck typing on `.children`/`.type`. No `isinstance(node, Node)` checks anywhere.
- **Other `.parent` usages** — `variable_extraction.py`, `commutative_swap.py`, `empty_size_swap.py` all just check `parent.type`, never compare parent against another node.

### Deferred

- **BEGIN_CUSTOM_PROPSYNC** (64 occurrences) — Generates free function `PropSync()`, not member function. Needs different name-matching logic. Not in scope for initial implementation.
