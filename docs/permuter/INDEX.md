# C++ Permuter

Tree-sitter based source permuter for automatic code variation generation. Unlike the original decomp-permuter (C only), this tool works with C++ source code.

See also:
- [Diagnosis-Guided Permuter](guided-permuter.md) — objdiff-driven targeting and BSF-guided register allocation
- [BSF Engine](bsf-engine.md) — compiler trace integration for register allocation analysis
- [Permuter Evolution](evolution/OVERVIEW.md) — primitives, pattern migration, and composition layer

## Installation

```bash
pip install tree-sitter tree-sitter-cpp
```

## Quick Start

```bash
# List available patterns
python -m scripts.permuter --list-patterns

# Dry run - show variants without building
python -m scripts.permuter \
    --symbol "?BurnXfm@RndMesh@@QAAXXZ" \
    --source src/system/rndobj/Mesh.cpp \
    --function "RndMesh::BurnXfm" \
    --dry-run

# Run and score all variants (stops on 100% match, auto-applies best improvement)
python -m scripts.permuter \
    --symbol "?BurnXfm@RndMesh@@QAAXXZ" \
    --source src/system/rndobj/Mesh.cpp \
    --function "RndMesh::BurnXfm"
```

## CLI Options

| Option | Description |
|--------|-------------|
| `--symbol` | Mangled symbol name for objdiff |
| `--source` | Path to .cpp source file |
| `--function` | Qualified C++ function name (e.g. `RndMesh::BurnXfm`) |
| `--patterns` | Comma-separated pattern names, or `all` (default: all) |
| `--max-variants` | Maximum variants to generate (default: 100) |
| `--no-stop-on-perfect` | Continue scoring even after a 100% match is found |
| `--no-apply` | Do not auto-apply the best improving variant |
| `--json` | Output results as JSON |
| `--dry-run` | Generate and list variants without building/scoring |
| `--unit` | Unit name for unicorn execution equivalence guard rail |
| `--compose` | Enable composition: chain pattern pairs for multi-step transforms |
| `--no-guided` | Disable diagnosis-guided pattern filtering |
| `--no-bsf-guided` | Disable BSF-guided declaration reordering (use blind pairwise) |
| `--bsf-required` | Fail if BSF tracing fails instead of falling back to unguided |
| `--list-patterns` | List available patterns and exit |

## Patterns

### variable_extraction (42% win rate)

Extracts inline function calls into `auto` local variables. Useful for nudging register allocation.

```cpp
// Before
MILO_ASSERT(display < mElements.size(), 0x74);

// After
auto _tmp0 = mElements.size();
MILO_ASSERT(display < _tmp0, 0x74);
```

### signed_unsigned (30% win rate)

Wraps comparison operands in type casts. Useful for fixing signed/unsigned comparison codegen.

```cpp
// Before
if (ptr != 0)

// Variants generated
if ((int)ptr != 0)
if ((unsigned int)ptr != 0)
if ((unsigned long)ptr != 0)
if (ptr > 0)  // for != 0 comparisons
```

### declaration_reorder (30% win rate)

Reorders variable declarations within a function to fix register allocation mismatches. The PowerPC compiler assigns callee-saved registers (r19-r31) based on declaration/first-use order, so swapping declaration order can fix register swap pairs.

Supports **BSF-guided mode** (default when BSF tracing is available): traces the compiler's register allocator to map colors to GPRs, then generates only the swaps targeting diagnosed register pairs instead of blind C(n,2) enumeration. Falls back to unguided pairwise when tracing fails or `--no-bsf-guided` is set. See [BSF Engine](bsf-engine.md) for details.

```cpp
// Before
int count = GetCount();
float scale = GetScale();

// After (swap declaration order)
float scale = GetScale();
int count = GetCount();
```

### declaration_movement

Moves a variable declaration earlier or later in a function body. Similar to `declaration_reorder` but operates on a single declaration at a time, sliding it up or down relative to other statements.

### inline_assignment (22% win rate)

Folds consecutive assignment + call into inline assignment argument.

```cpp
// Before
era = pEra->GetName();
CampaignEraProgress *p = GetEraProgress(era);

// After
CampaignEraProgress *p = GetEraProgress(era = pEra->GetName());
```

### commutative_swap

Swap operands in commutative expressions: `a + b` → `b + a`, `a * b` → `b * a`, etc. Applies to `add`, `fadd`, `mul`, `fmul`, `and`, `or`, `xor`.

### comparison_flip

Swap comparison operands: `a == b` → `b == a`, `a < b` → `b > a`.

### comparison_equivalence

Try equivalent comparison forms: `x != 0` → `x > 0` (for unsigned), `x < y` → `!(x >= y)`.

### branch_polarity

Invert condition and swap if/else bodies: `if (x) { A } else { B }` → `if (!x) { B } else { A }`.

### ternary_swap

Convert simple if-else to ternary and vice versa.

### fma_reorder

Reorder FMA expressions: `1.0f - x*y` ↔ `x*y - 1.0f`. Targets `fnmsubs`/`fmsubs` mismatches.

### argument_swap

Swap function call arguments when overloads or implicit conversions may change codegen.

### empty_size_swap

Swap between `.empty()` and `.size() == 0` (or `!= 0`) which can differ in codegen.

### comma_split

Split comma expressions into separate statements, or merge sequential statements into comma expressions.

## How It Works

1. **Diagnose**: Run objdiff on baseline, parse instruction diff into `Diagnosis` (register swaps, opcode mismatches, offset shifts)
2. **Filter**: Each pattern's `relevant()` method checks if the diagnosis contains signals it can fix
3. **Generate**: Relevant patterns walk the AST and generate source variants via byte-level splicing
4. **Score**: Writes each variant to disk, runs `ninja`, and scores with `./bin/objdiff-cli`
5. **Report**: Sorts variants by match percentage and reports improvements

Key design decisions:
- **Byte-level splicing**: All mutations operate on raw bytes using tree-sitter node ranges. No regex or string parsing.
- **Scope-aware**: Variable extraction respects compound_statement boundaries (won't extract loop-scoped variables to function scope)
- **File restoration**: Scorer uses a context manager to guarantee source file restoration even on errors
- **Independent variants by default**: Each variant is generated from the original source; composition (chaining pattern pairs) is available via `--compose`
- **Diagnosis-driven**: Patterns only generate variants when diagnosis signals match (reduces ~100 blind variants to ~5-20 targeted ones)
- **BSF-guided**: Declaration reordering uses compiler register allocator traces when available

## Example Output

```
Extracting RndMesh::BurnXfm from src/system/rndobj/Mesh.cpp...
Found function with 1 statements (428 bytes)
Generated 14 variants
[1/14] varext_0: Extract 'mChildren.end()' into auto _tmp0... 95.23% IMPROVED
[2/14] varext_1: Extract '(*it)->LocalXfm()' into auto _tmp1... 93.12% same
...

======================================================================
RESULTS (baseline: 93.12%)
======================================================================
  varext_0                      95.23%  +2.11%
    Extract 'mChildren.end()' into auto _tmp0
  signunsign_3                  93.12% (same)
    Cast right of '!=' to (int)
...

Best improvement: varext_0 at 95.23%
  Extract 'mChildren.end()' into auto _tmp0
```

## JSON Output

Use `--json` for structured output suitable for integration with other tools:

```json
{
  "baseline": 93.12,
  "results": [
    {
      "name": "varext_0",
      "pattern": "variable_extraction",
      "description": "Extract 'mChildren.end()' into auto _tmp0",
      "match_percent": 95.23,
      "build_success": true,
      "error": null,
      "delta": 2.11
    }
  ]
}
```

## Module Structure

```
scripts/permuter/
├── __init__.py          # Public API exports
├── __main__.py          # CLI entry point + diagnosis-guided orchestration
├── types.py             # FunctionContext, Variant, ScoreResult dataclasses
├── extractor.py         # tree-sitter function extraction + reparse
├── scorer.py            # ninja build + objdiff scoring
├── generator.py         # Pattern application + composition orchestration
├── composer.py          # Multi-step pattern composition (--compose)
├── editor.py            # SourceEditor — byte-level AST splicing primitive
├── ast_queries.py       # Reusable AST query helpers
├── diagnosis.py         # Diagnosis dataclass + objdiff mismatch parsing
└── patterns/
    ├── __init__.py              # Auto-imports all patterns
    ├── base.py                  # Pattern ABC with auto-registration
    ├── argument_swap.py
    ├── branch_polarity.py
    ├── comma_split.py
    ├── commutative_swap.py
    ├── comparison_equivalence.py
    ├── comparison_flip.py
    ├── declaration_movement.py
    ├── declaration_reorder.py   # BSF-guided mode
    ├── empty_size_swap.py
    ├── fma_reorder.py
    ├── inline_assignment.py
    ├── signed_unsigned.py
    ├── ternary_swap.py
    └── variable_extraction.py

scripts/permuter/
├── batch_auto.py        # Batch automation: sweep workable functions with hill_climber
├── batch_validate.py    # Batch validation: single-pass permuter sweep
├── batch_triage.py      # Batch triage: diagnose + classify near-match functions
├── hill_climber.py      # Iterative hill-climbing loop for a single function

tools/compiler_trace/           # BSF engine (see bsf-engine.md)
├── bsf_trace.py                # GDB-based BSF call tracing + per-function partitioning
├── bsf_diff.py                 # Diff two BSF traces
├── asm_diff.py                 # ASM listing extraction + register swap detection (strict/relaxed)
├── regmap_solver.py            # Color→GPR mapping + guided swap generation (isolation-aware)
├── invoker.py                  # CompilerInvoker (wibo + cl.exe wrapper)
├── gdb_script.py               # GDB script generation for BSF breakpoints
└── tests/
    ├── test_bsf_engine.py      # 30 integration tests (incl. partition tests)
    └── test_bsf_pipeline.py    # 3 end-to-end pipeline tests
```

## Adding New Patterns

Create a new file in `scripts/permuter/patterns/`:

```python
from .base import Pattern
from ..types import FunctionContext, Variant

class MyPattern(Pattern):
    name = "my_pattern"  # Auto-registers via __init_subclass__

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        # Walk ctx.statements or ctx.body_node
        # Yield Variant objects with modified source
        for node in walk_tree(ctx.body_node):
            if is_target(node):
                new_source = splice(ctx.file_source, node, replacement)
                yield Variant(
                    name=f"mypattern_{counter}",
                    pattern_name=self.name,
                    description="What this variant does",
                    source=new_source,
                )
```

Import it in `patterns/__init__.py` to register it.

## Hill Climbing

The `hill_climber` runs iterative improve-apply-repeat rounds on a single function:

```bash
python -m scripts.permuter.hill_climber \
    --symbol "?Poll@LabelNumberTicker@@UAAXXZ" \
    --source src/system/ui/LabelNumberTicker.cpp \
    --function "LabelNumberTicker::Poll" \
    --max-rounds 10 --compose --json
```

Stops on: 100% match, plateau (N rounds without improvement), max rounds, or all noise.

## Batch Automation

Sweep all workable functions automatically:

```bash
# Sweep all workable functions
python -m scripts.permuter.batch_auto --target workable --max-rounds 5

# Target specific unit
python -m scripts.permuter.batch_auto --target unit --unit "system/rndobj/Shader"

# Dry run — show triage without running
python -m scripts.permuter.batch_auto --target workable --dry-run --limit 20

# Resume from previous run
python -m scripts.permuter.batch_auto --resume logs/permuter/auto_20260303_120000
```

Features: resume support (progress.json), per-function JSON logs, triage filtering, grouped by source file.

## Test Suite

```bash
# Permuter pattern tests (65 tests)
python -m pytest scripts/permuter/tests/test_patterns.py -v

# BSF engine integration tests (30 tests, requires wibo + MSVC + GDB)
python -m pytest tools/compiler_trace/tests/test_bsf_engine.py -v

# BSF pipeline e2e tests (3 tests, requires wibo + MSVC + GDB)
python -m pytest tools/compiler_trace/tests/test_bsf_pipeline.py -v
```

## Tips

- **Start with near-matches**: The permuter works best on functions already at 90%+ match
- **Use --dry-run first**: Review generated variants before committing to builds
- **Single pattern testing**: Use `--patterns variable_extraction` to test one pattern at a time
- **JSON for scripting**: Use `--json` for integration with orchestrator or batch processing
- **BSF tracing needs ptrace**: Run outside the sandbox or with `dangerouslyDisableSandbox: true`

## See Also

- [objdiff documentation](../tools/objdiff.md)
- [Archived: decomp-permuter](../tools/permuter.md) (C only, not compatible with DC3)
