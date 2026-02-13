# Phase 1: Primitives

Two new modules providing reusable building blocks for pattern authoring. Purely additive — existing patterns continue working unchanged.

## `scripts/permuter/editor.py` — SourceEditor

Batched, validated byte splicing. Replaces the manual `source[:start] + new + source[end:]` scattered across every pattern.

```python
class SourceEditor:
    def __init__(self, source: bytes): ...
    def replace_node(self, node: Node, replacement: bytes) -> None: ...
    def replace_range(self, start: int, end: int, replacement: bytes) -> None: ...
    def insert_before(self, node: Node, text: bytes) -> None: ...
    def insert_after(self, node: Node, text: bytes) -> None: ...
    def insert_at(self, offset: int, text: bytes) -> None: ...  # arbitrary byte offset
    def delete_node(self, node: Node) -> None: ...
    def delete_range(self, start: int, end: int) -> None: ...
    def swap_nodes(self, a: Node, b: Node) -> None: ...
    def apply(self) -> bytes: ...  # Raises ValueError on overlap
```

### Key properties

- **Overlap detection**: `apply()` raises `ValueError` if any two edits touch the same byte range. Currently overlapping edits silently corrupt source.
- **Multi-edit atomicity**: Accumulate multiple edits, apply once. Edits sorted descending by position for correct splicing.
- **Composable operations**: `swap_nodes` is two `replace_node` calls that can't collide. `insert_before` is a zero-width replace at `node.start_byte`.
- **Order-independent swap**: `swap_nodes` sorts nodes by `start_byte` internally — callers don't need to know which comes first in source.
- **Arbitrary inserts**: `insert_at(offset, text)` for cases like `variable_extraction` that inserts at `get_line_start()`, not at a node boundary.

### Why not just keep doing inline splicing?

Today in `comparison_flip.py`:
```python
new_source = (
    source[:left.start_byte]
    + right_text
    + source[left.end_byte:op_node.start_byte]
    + new_op
    + source[op_node.end_byte:right.start_byte]
    + left_text
    + source[right.end_byte:]
)
```

With SourceEditor:
```python
ed = SourceEditor(source)
ed.swap_nodes(left, right)
ed.replace_node(op_node, new_op)
new_source = ed.apply()
```

Three edits, overlap-checked, intent-clear.

## `scripts/permuter/ast_queries.py` — shared walkers

Replaces 7 duplicate private walkers and 2 duplicate utility functions across pattern files.

```python
# Generic traversal
def walk(node: Node) -> Iterator[Node]: ...
def walk_named(node: Node) -> Iterator[Node]: ...
def find_by_type(node: Node, type_name: str) -> Iterator[Node]: ...

# Domain-specific finders (replace private copies)
def find_comparisons(node: Node, ops: set[str] | None = None) -> Iterator[Node]: ...
    # ops defaults to {"<", ">", "<=", ">=", "==", "!="}
    # comparison_equivalence passes ops={"<", "<=", ">", ">="}
def find_calls(node: Node) -> Iterator[Node]: ...
def find_if_else(node: Node) -> Iterator[Node]: ...

# Text utilities (replace private copies)
def get_indent(source: bytes, node: Node) -> bytes: ...   # 2 copies eliminated
def get_line_start(source: bytes, node: Node) -> int: ... # 1 copy eliminated
def node_text(source: bytes, node: Node) -> bytes: ...    # shorthand
def identifiers_in(node: Node) -> set[str]: ...           # replaces _collect_identifiers
```

### Design note: `find_comparisons` operator filtering

The three patterns that use `_find_comparisons` have **different operator sets**:
- `comparison_flip.py` and `signed_unsigned.py`: all 6 operators `{"<", ">", "<=", ">=", "==", "!="}`
- `comparison_equivalence.py`: only 4 operators `{"<", "<=", ">", ">="}` (equivalence transforms don't apply to == or !=)

Solution: `find_comparisons(node, ops=None)` takes an optional operator set. Default is the full set; callers that need a subset pass their own.

### Duplication inventory

| Function | Currently in | Copies |
|----------|-------------|--------|
| `_find_comparisons` | comparison_flip, comparison_equivalence, signed_unsigned | 3 (different op sets) |
| `_find_calls` | argument_swap | 1 |
| `_find_if_else` | branch_polarity | 1 |
| `_walk` | inline_assignment | 1 |
| `_find_fma_candidates` | fma_reorder (specialized, stays) | 1 |
| `_get_indent` | ternary_swap, variable_extraction | 2 |
| `_get_line_start` | variable_extraction | 1 |
| `_collect_identifiers` | declaration_reorder | 1 |

Note: `_find_fma_candidates` in `fma_reorder.py` has FMA-specific logic (checks for `*` operand in `+`/`-` expression). It stays as a private function — `find_by_type` + a filter lambda would be less readable.

## New test files

### `scripts/permuter/tests/test_editor.py`

- `SourceEditor` with single replacement
- Multi-edit apply (insert + replace in same pass)
- Overlap detection raises `ValueError`
- `swap_nodes` produces correct output
- `delete_node` removes bytes
- Empty edit list returns original source
- Zero-width inserts (before/after)

### `scripts/permuter/tests/test_ast_queries.py`

- `find_comparisons` on sample C++ matches all comparison operators
- `find_calls` finds nested and top-level calls
- `find_if_else` only yields if-statements with else clauses
- `get_indent` returns correct whitespace for nested blocks
- `identifiers_in` collects all variable names in expression trees
- Results match output of the private functions they replace (verified against existing patterns on test fixture source)

## Files

| File | Action |
|------|--------|
| `scripts/permuter/editor.py` | **New** |
| `scripts/permuter/ast_queries.py` | **New** |
| `scripts/permuter/tests/test_editor.py` | **New** |
| `scripts/permuter/tests/test_ast_queries.py` | **New** |
