# C++ Permuter

Tree-sitter based source permuter for automatic code variation generation. Unlike the original decomp-permuter (C only), this tool works with C++ source code.

See also:
- [Diagnosis-Guided Permuter](guided-permuter.md) — design for objdiff-driven targeting
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

### inline_assignment (22% win rate)

Folds consecutive assignment + call into inline assignment argument.

```cpp
// Before
era = pEra->GetName();
CampaignEraProgress *p = GetEraProgress(era);

// After
CampaignEraProgress *p = GetEraProgress(era = pEra->GetName());
```

## How It Works

1. **Extract**: Uses tree-sitter-cpp to parse the source file and extract the target function
2. **Generate**: Each pattern walks the AST and generates source variants via byte-level splicing
3. **Score**: Writes each variant to disk, runs `ninja`, and scores with `./bin/objdiff-cli`
4. **Report**: Sorts variants by match percentage and reports improvements

Key design decisions:
- **Byte-level splicing**: All mutations operate on raw bytes using tree-sitter node ranges. No regex or string parsing.
- **Scope-aware**: Variable extraction respects compound_statement boundaries (won't extract loop-scoped variables to function scope)
- **File restoration**: Scorer uses a context manager to guarantee source file restoration even on errors
- **Independent variants by default**: Each variant is generated from the original source; composition (chaining pattern pairs) is available via `--compose`

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
├── __main__.py          # CLI entry point
├── types.py             # FunctionContext, Variant, ScoreResult dataclasses
├── extractor.py         # tree-sitter function extraction + reparse
├── scorer.py            # ninja build + objdiff scoring
├── generator.py         # Pattern application + composition orchestration
├── composer.py          # Multi-step pattern composition (--compose)
├── editor.py            # SourceEditor — byte-level AST splicing primitive
├── ast_queries.py       # Reusable AST query helpers
├── diagnosis.py         # Diagnosis dataclass + objdiff mismatch parsing
└── patterns/
    ├── __init__.py      # Auto-imports all patterns
    ├── base.py          # Pattern ABC with auto-registration
    ├── variable_extraction.py
    ├── signed_unsigned.py
    ├── inline_assignment.py
    ├── declaration_reorder.py
    ├── argument_swap.py
    ├── branch_polarity.py
    ├── commutative_swap.py
    ├── comparison_equivalence.py
    ├── comparison_flip.py
    ├── empty_size_swap.py
    ├── fma_reorder.py
    └── ternary_swap.py
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

## Tips

- **Start with near-matches**: The permuter works best on functions already at 90%+ match
- **Use --dry-run first**: Review generated variants before committing to builds
- **Single pattern testing**: Use `--patterns variable_extraction` to test one pattern at a time
- **JSON for scripting**: Use `--json` for integration with orchestrator or batch processing

## See Also

- [objdiff documentation](../tools/objdiff.md)
- [Archived: decomp-permuter](../tools/permuter.md) (C only, not compatible with DC3)
