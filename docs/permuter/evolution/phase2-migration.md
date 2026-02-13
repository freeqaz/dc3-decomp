# Phase 2: Pattern Migration

Migrate all 12 existing patterns to use `SourceEditor` and `ast_queries` from Phase 1. Each migration is a pure refactor — the existing 46 PatternFixture tests are the safety net.

## Migration batches

Patterns grouped by shared dependency. Each batch can be a separate commit.

### Batch 1: Comparison patterns

**Files**: `comparison_flip.py`, `comparison_equivalence.py`, `signed_unsigned.py`

All three contain a private `_find_comparisons()` with different operator sets. Replace with `find_comparisons(node, ops=...)`:

```python
from ..ast_queries import find_comparisons

# comparison_flip and signed_unsigned: default (all 6 ops)
for cmp in find_comparisons(stmt):

# comparison_equivalence: restricted set (no == or !=)
for cmp in find_comparisons(stmt, ops={"<", "<=", ">", ">="}):
```

Additionally, `comparison_flip.py` has the most complex inline splicing (3-site swap of left/op/right). Replace with:

```python
ed = SourceEditor(ctx.file_source)
ed.swap_nodes(left, right)
ed.replace_node(op_node, new_op)
new_source = ed.apply()
```

**Test coverage**: `cmpflip_less_to_greater`, `cmpeq_lt_to_le`, `signunsign_neq_to_gt` fixtures.

### Batch 2: argument_swap

**File**: `argument_swap.py`

Replace `_find_calls()` with `find_calls()` from `ast_queries`. The 2-arg swap currently does a 4-segment byte splice that assumes `arg_a` comes before `arg_b` in source. Replace with:

```python
ed = SourceEditor(source)
ed.swap_nodes(arg_a, arg_b)  # SourceEditor sorts by start_byte internally
new_source = ed.apply()
```

Note: The current code's comment says "arg_a comes first in the source" — this holds because `named_children` preserves source order, but `SourceEditor.swap_nodes` removes this fragile assumption.

**Test coverage**: `argswap_two_identifiers` fixture.

### Batch 3: ternary_swap + variable_extraction

**Files**: `ternary_swap.py`, `variable_extraction.py`

Both have private `_get_indent()` copies. Replace with `get_indent()` from `ast_queries`. `variable_extraction` also has `_get_line_start()` — replace with `get_line_start()`.

For `variable_extraction`, the current code inserts at `get_line_start()` (beginning of the line), not at `stmt.start_byte`. Use `insert_at` instead of `insert_before`:

```python
ed = SourceEditor(source)
line_start = get_line_start(source, stmt)
ed.insert_at(line_start, decl_line)       # insert at line start, not node start
ed.replace_node(call_node, var_name)
new_source = ed.apply()
```

Note: `insert_before(stmt, ...)` would insert at `stmt.start_byte` which skips leading whitespace on the line. The pattern needs the full-line position to preserve indentation.

`ternary_swap` has complex multi-site construction (building if/else blocks from scratch). The `SourceEditor` helps with the single-node replacement part but the block text construction stays as string building — that's fine.

**Test coverage**: 5 ternary_swap fixtures + `varext_nested_call`.

### Batch 4: inline_assignment

**File**: `inline_assignment.py`

Replace `_walk()` with `walk()` from `ast_queries`. The splice removes stmt A and replaces a use site — two non-overlapping edits:

```python
ed = SourceEditor(source)
ed.delete_range(stmt_a.start_byte, stmt_a_end)  # remove assignment
ed.replace_node(use_node, inline_expr)           # replace variable with assignment expr
new_source = ed.apply()
```

**Test coverage**: `inline_fold_into_call` fixture.

### Batch 5: declaration_reorder

**File**: `declaration_reorder.py`

The existing `_apply_reorder()` already implements a reverse-sorted multi-replacement — essentially a manual `SourceEditor`. Replace with:

```python
ed = SourceEditor(source)
for orig_node, new_node in zip(original, reordered):
    if orig_node is not new_node:
        ed.replace_node(orig_node, node_text(source, new_node))
new_source = ed.apply()
```

Also replace `_collect_identifiers()` with `identifiers_in()` from `ast_queries`.

**Test coverage**: `declreorder_swap_pair` fixture.

### Batch 6: branch_polarity

Replace `_find_if_else` with `find_if_else` from ast_queries (direct drop-in — exact same logic).

**Fixture**: `brpol_invert_condition`

### Patterns NOT migrated (staying as-is)

| Pattern | Reason |
|---------|--------|
| `empty_size_swap` | Domain-specific helpers (`_arg_count`, `_get_field_operator`, `_get_unary_op`) |
| `commutative_swap` | `_collect_chain` is domain-specific; splice is simple single-replacement |
| `fma_reorder` | `_find_fma_candidates` is specialized; only 2 simple splices |

These patterns can be migrated later if the helpers prove useful elsewhere. For now, they work and have test coverage.

## After migration

Writing a new pattern goes from ~80 lines of boilerplate (walker + splicing + indentation) to ~30 lines of intent:

```python
from ..editor import SourceEditor
from ..ast_queries import find_comparisons, get_indent

class MyNewPattern(Pattern):
    name = "my_new_pattern"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return any(d.target_opcode == "cmpwi" for d in diagnosis.diff_ops)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for cmp in find_comparisons(ctx.body_node):
            op = cmp.child_by_field_name("operator")
            if op and op.text == b"<":
                ed = SourceEditor(ctx.file_source)
                ed.replace_node(op, b"<=")
                # Adjust RHS...
                yield Variant(
                    name=f"mypattern_{counter}",
                    pattern_name=self.name,
                    description="...",
                    source=ed.apply(),
                )
                counter += 1
```

## Verification

After each batch:

```bash
python -m pytest scripts/permuter/tests/test_patterns.py -v
```

All 46 tests must pass. If a fixture fails, the migration introduced a behavioral change — fix it before proceeding.
