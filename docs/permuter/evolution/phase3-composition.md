# Phase 3: Composition Layer

**Status**: **Complete**.

Enable multi-step transformations by chaining pattern outputs through tree-sitter re-parse cycles. Also adds per-pattern budget allocation to prevent variant starvation.

## The problem

Real-world matching often needs combined fixes. Example: extract a nested call into an `auto` variable (variable_extraction), then reorder that new declaration relative to existing ones (declaration_reorder). Today these are independent — each pattern sees only the original source.

## Re-parse function

The key unlock. Tree-sitter parsing is ~1ms; the expensive part is building/scoring.

```python
# Added to scripts/permuter/extractor.py

def reparse_variant(
    original_ctx: FunctionContext,
    new_source: bytes,
) -> FunctionContext:
    """Re-parse modified source to get fresh AST nodes.

    Finds same function by name in the re-parsed tree.
    Preserves file_path and diagnosis from original context.
    Raises ValueError if function can't be found (bad syntax from pattern).
    """
```

## Composer

```python
# scripts/permuter/composer.py

def compose_variants(
    ctx: FunctionContext,
    stage_a: Pattern,
    stage_b: Pattern,
    max_per_stage: int = 10,
    max_total: int = 50,
) -> Iterator[Variant]:
    """Apply pattern B to each output of pattern A.

    For each variant from stage_a, re-parse it and run stage_b.
    O(A * B) variant count, capped by max_per_stage and max_total.
    Yields Variant with combined name like "varext_0+declreorder_3".
    """
```

## High-value composition pairs

Based on pattern win rates and PowerPC compiler behavior:

| Stage A | Stage B | Rationale |
|---------|---------|-----------|
| `variable_extraction` | `declaration_reorder` | Extract creates new `auto` decl; reorder fixes register alloc for it |
| `inline_assignment` | `comparison_flip` | Fold changes which expressions are in comparison context |
| `comparison_equivalence` | `signed_unsigned` | Change `i < 2` to `i <= 1`, then cast one operand |

## Generator integration

`generate_variants()` gets an optional `compose_pairs` parameter:

```python
def generate_variants(
    ctx: FunctionContext,
    patterns: list[Pattern],
    max_variants: int = 100,
    compose_pairs: list[tuple[str, str]] | None = None,
) -> Iterator[Variant]:
    # Phase 1: Independent variants (existing behavior, unchanged)
    ...

    # Phase 2: Composed variants (new, fills remaining budget)
    if compose_pairs:
        for name_a, name_b in compose_pairs:
            for variant in compose_variants(ctx, get_pattern(name_a), get_pattern(name_b)):
                yield variant
```

## Per-pattern budget allocation

Today's global 100-variant cap causes starvation. Replace with proportional allocation:

```python
def allocate_budgets(
    patterns: list[Pattern],
    total_budget: int,
    ctx: FunctionContext,
) -> dict[str, int]:
    """Allocate variant budget proportionally by win rate.
    Minimum 3 per relevant pattern to avoid starvation."""
```

Historical win rates (from batch validation):
- `variable_extraction`: 42%
- `signed_unsigned`: 30%
- `inline_assignment`: 22%
- `declaration_reorder`: 20%
- `comparison_flip`: 15%
- Others: 2-10%

## Test evolution

New `ComposedFixture` dataclass for multi-step verification:

```python
@dataclass
class ComposedFixture:
    id: str
    stage_a_pattern: str
    stage_b_pattern: str
    description: str
    seeded_source: str
    intermediate_contains: str  # verify stage A output
    expected_source: str        # verify final output
    func_name: str
    diagnosis: Diagnosis
    match_mode: str = "normalized"
```

Example fixture — extract + reorder:

```python
ComposedFixture(
    id="compose_varext_then_declreorder",
    stage_a_pattern="variable_extraction",
    stage_b_pattern="declaration_reorder",
    description="extract call then reorder new declaration",
    func_name="test_func",
    diagnosis=diag_with_gpr_swaps(),
    seeded_source="""\
void test_func(int x) {
    int a = 1;
    check(x < getSize(), 0x42);
}
""",
    intermediate_contains="auto _tmp0 = getSize();",
    expected_source="""\
void test_func(int x) {
    auto _tmp0 = getSize();
    int a = 1;
    check(x < _tmp0, 0x42);
}
""",
)
```

## Inspiration

### decomp-permuter (simonlindholm)

Uses `PERM_GENERAL(a, b, c)` macros — source-level annotations marking permutation sites. Not applicable here (we don't annotate source), but the concept of "marked sites" could inform a future interactive mode.

### Grammar-guided mutation

Random AST node selection + type-aware substitution. Our system is already more targeted (diagnosis-guided), but random composition walks could work as an exploration mode when diagnosis is absent.

### Iterative hill-climbing

Build variant A, score it. If it improved, re-parse and try pattern B on the improved source. Repeat until plateau. This is the natural extension of composition — not just A→B, but A→score→B→score→C→score. Deferred to a future phase since it requires tighter integration with the scorer.

## Files

| File | Action |
|------|--------|
| `scripts/permuter/composer.py` | **New** |
| `scripts/permuter/extractor.py` | Add `reparse_variant()` |
| `scripts/permuter/generator.py` | Add composition phase + budget allocation |
| `scripts/permuter/tests/test_patterns.py` | Add `ComposedFixture` + test runner |
