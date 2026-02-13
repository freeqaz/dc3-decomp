# Diagnosis-Guided Permuter

Design for making the permuter "smart" — using objdiff mismatch data to target specific areas of a function instead of blindly generating every possible variant.

## Problem

The current permuter is blind. It generates every variant from every pattern at every applicable site in a function, then builds and scores each one. For a function with 15 comparisons, `signed_unsigned` alone generates ~90 variants (15 sites x 6 cast/swap options). Most of these are wasted builds because only 1-2 comparisons actually mismatch.

Meanwhile, we already have rich diagnostic data from objdiff that tells us *exactly* what kind of mismatches exist at the instruction level — register swaps, opcode differences, offset shifts, insert/delete clusters. This data is just not connected to the permuter.

## Design

### Phase 0: Diagnose Baseline

After the scorer builds the baseline and gets the objdiff JSON, parse the full instruction diff through `diff_inspect.parse_breakdowns()` to produce a `Diagnosis` dataclass:

```python
@dataclass
class Diagnosis:
    """Structured mismatch analysis from objdiff baseline."""

    # Instruction-level counts
    total_instructions: int
    match_counts: dict[str, int]  # match_type -> count

    # Register swap pairs: {("r20", "r21"): SwapInfo(count, first_idx, last_idx)}
    reg_swap_pairs: dict[tuple[str, str], SwapInfo]

    # Offset delta histogram: {delta: count}
    offset_deltas: dict[int, int]

    # diff_op instructions (real opcode mismatches)
    diff_ops: list[DiffOp]  # (index, target_opcode, base_opcode)

    # Insert/delete clusters
    clusters: list[Cluster]  # (index_range, size, dominant_opcodes)

    # Noise budget: how many diff_arg instructions are fully explained
    # by register swaps + offset shifts + symbol relocs
    noise_explained: int
    noise_total: int
```

This gets attached to `FunctionContext` as an optional `diagnosis` field.

### Phase 1: Pattern Filtering

Each pattern gets a new optional method:

```python
class Pattern(ABC):
    def relevant(self, diagnosis: Diagnosis) -> bool:
        """Return False to skip this pattern entirely based on diagnosis."""
        return True  # Default: always relevant (backwards compatible)
```

The generator checks `relevant()` before calling `generate()`. Mapping:

| Diagnosis signal | Patterns to activate | Patterns to skip |
|---|---|---|
| `diff_op` with `cmp*` opcodes | `signed_unsigned`, `comparison_equivalence` | |
| `diff_op` with branch opcodes (`beq`↔`bne`, `ble`↔`bgt`) | `signed_unsigned` (zero swap), `branch_polarity` | |
| `diff_op` with `fnmsubs`/`fmsubs` | `fma_reorder` | |
| Insert/delete clusters around loads | `argument_swap`, `variable_extraction`, `inline_assignment` | |
| GPR register swap pairs | `declaration_reorder` | |
| All `diff_arg` explained by noise (offset + symbol + branch) | | **all patterns** (skip entirely) |
| No `diff_op`, no clusters, only register swaps | | everything except `declaration_reorder` |

### Phase 2: Site Targeting

objdiff instruction indices map roughly to source position (earlier statements = lower indices). We don't need a perfect mapping — even approximate targeting helps.

**Approach**: Compute a normalized mismatch position from instruction indices, then map to statement indices:

```python
def mismatch_regions(diagnosis: Diagnosis) -> list[float]:
    """Return normalized positions [0.0-1.0] of mismatch clusters."""
    # e.g., diff_op at instruction 60 of 100 total -> 0.6
    # Map 0.6 to statement index: int(0.6 * len(statements))
```

Patterns receive these regions and focus variant generation on statements in or near the mismatch zones, plus a small window around them.

**For register swaps specifically**: The swap pair's `first_idx` tells us roughly where the register gets assigned. Variables declared early in the function get lower-numbered registers (r31, r30, ...). If `r20↔r21` swap starts at index 5, the fix is likely in the first few declarations — reorder those.

### Phase 3: New Patterns

Patterns unlocked by having diagnosis data, drawn from the [pattern documentation](../decomp/patterns/):

#### `declaration_reorder` (targets: register swaps)

Permute variable declaration order within scopes. The compiler assigns callee-saved registers (r19-r31) based on declaration/first-use order. When diagnosis shows GPR swap pairs, try reordering the declarations most likely to map to those registers.

- **Source**: Variable declaration order pattern (30% success, but high impact)
- **Trigger**: `diagnosis.reg_swap_pairs` has GPR pairs
- **Strategy**: Try all permutations of declarations in the first scope block (where registers are typically assigned). For functions with many declarations, use heuristics: group by usage pattern, order by first use, try reverse order.

#### `commutative_swap` (targets: operand order)

Swap operands in commutative expressions: `a + b` → `b + a`, `a * b` → `b * a`, etc. Applies to `add`, `fadd`, `mul`, `fmul`, `and`, `or`, `xor`.

- **Source**: Commutative operand order pattern (80% success)
- **Trigger**: Insert/delete or `diff_arg` near arithmetic instructions

#### `branch_polarity` (targets: branch direction)

Invert condition and swap if/else bodies: `if (x) { A } else { B }` → `if (!x) { B } else { A }`.

- **Source**: Branch polarity steering pattern (medium success)
- **Trigger**: `diff_op` with branch opcodes (`beq`↔`bne`, `ble`↔`bge`)

#### `comparison_operand_flip` (targets: register selection in comparisons)

Swap comparison operands: `a == b` → `b == a`, `a < b` → `b > a`.

- **Source**: Comparison operand order pattern (high success)
- **Trigger**: `diff_op` or `diff_arg` near `cmp*` instructions

#### `ternary_swap` (targets: control flow structure)

Convert simple if-else to ternary and vice versa.

- **Source**: Ternary vs if-else pattern (75% success)
- **Trigger**: Insert/delete clusters suggesting control flow difference

#### `fma_reorder` (targets: FMA instruction selection)

Reorder FMA expressions: `1.0f - x*y` ↔ `x*y - 1.0f`.

- **Source**: FMA expression order pattern (98% success)
- **Trigger**: `diff_op` with `fnmsubs`/`fmsubs`/`fmadds`/`fnmadds`

### Phase 4: Early Skip

Before generating any variants, check if the function is unfixable at the source level:

```python
def is_unfixable(diagnosis: Diagnosis) -> bool:
    """Return True if diagnosis shows only noise — no source-level fix exists."""
    has_diff_ops = len(diagnosis.diff_ops) > 0
    has_clusters = len(diagnosis.clusters) > 0
    noise_ratio = diagnosis.noise_explained / max(diagnosis.noise_total, 1)

    # All mismatches explained by register swaps + offset shifts + symbol relocs
    if not has_diff_ops and not has_clusters and noise_ratio > 0.95:
        # Only register swaps remain — still try declaration_reorder
        if diagnosis.reg_swap_pairs:
            return False
        return True
    return False
```

## Impact

| Metric | Current (blind) | Guided |
|---|---|---|
| Variants per function | ~60-100 (all patterns x all sites) | ~5-20 (relevant patterns x targeted sites) |
| Build time per function | 60-100 builds | 5-20 builds |
| Register swap coverage | 0% (no pattern) | 30% (declaration_reorder) |
| Wasted builds on noise-only functions | 100% | 0% (early skip) |
| Pattern count | 6 | 12 |

## Compatibility

- `Diagnosis` is optional on `FunctionContext` — existing patterns work unchanged
- `relevant()` defaults to `True` — existing patterns are always activated without diagnosis
- `--no-guided` flag to disable diagnosis and fall back to blind mode
- Guided mode is the default when diagnosis data is available

## File Changes

```
scripts/permuter/
├── types.py             # Add Diagnosis, SwapInfo, DiffOp, Cluster dataclasses
├── diagnosis.py         # NEW: Parse objdiff JSON into Diagnosis
├── generator.py         # Check pattern.relevant() before generate()
├── scorer.py            # Run diagnosis on baseline, attach to context
├── patterns/
│   ├── base.py          # Add relevant() method to Pattern ABC
│   ├── signed_unsigned.py       # Add relevant() + site targeting
│   ├── comparison_equivalence.py # Add relevant() + site targeting
│   ├── variable_extraction.py   # Add relevant() + site targeting
│   ├── inline_assignment.py     # Add relevant() + site targeting
│   ├── argument_swap.py         # Add relevant() + site targeting
│   ├── empty_size_swap.py       # Add relevant() + site targeting
│   ├── declaration_reorder.py   # NEW
│   ├── commutative_swap.py      # NEW
│   ├── branch_polarity.py       # NEW
│   ├── comparison_flip.py       # NEW
│   ├── ternary_swap.py          # NEW
│   └── fma_reorder.py           # NEW
└── __main__.py          # Add --no-guided flag, print diagnosis summary
```
