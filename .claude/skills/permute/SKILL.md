---
name: permute
description: Run the source permuter on a function to find signed/unsigned and variable extraction improvements. Use when working on a function that isn't matching 100% and you want to automatically try source variations.
argument-hint: "[symbol-or-function] [--batch]"
allowed-tools: Bash(venv/bin/python *), Bash(ninja *), Read, Grep, Glob
---

# Permuter Skill

Run the source permuter to automatically try signed/unsigned casts and variable extraction
patterns on a decomp function, scoring each variant with objdiff.

## Arguments

`$ARGUMENTS`

## Modes

### Single Function Mode (default)

The permuter auto-resolves symbols from `decomp.db` and `objdiff.json`. Just pass
the symbol — it handles source path and function name lookup automatically.

**Run it directly:**

```bash
venv/bin/python -m scripts.permuter --symbol '$0'
```

The `--symbol` argument accepts:
- A mangled symbol: `?Seek@AsyncFile@@UAAHHH@Z`
- A qualified C++ name: `AsyncFile::Seek`
- A partial name: `Seek` (matches via LIKE query)

The permuter will:
1. Look up the mangled symbol, source path, and qualified name from the DB
2. Extract the function from source
3. Run diagnosis (diff_ops, register swaps, clusters) to guide pattern selection
4. Generate and score variants (comparison flips, variable extractions, declaration reorders, etc.)
5. Auto-apply the best improvement (use `--no-apply` to skip)

**Optional flags:**
- `--no-apply` — don't auto-apply the best variant
- `--no-guided` — try all patterns blindly (skip diagnosis-guided filtering)
- `--no-compose` — disable two-step pattern composition
- `--no-bsf-guided` — disable BSF-guided declaration reordering
- `--patterns NAME,...` — only run specific patterns
- `--max-variants N` — limit variant count (default: 100)
- `--dry-run` — list variants without building/scoring
- `--list-patterns` — show available pattern names

**Report results** to the user:
- Baseline match percentage
- Number of variants tested
- Any improvements found (with diffs)
- Whether the improvement was applied

### Batch Mode (`--batch`)

When `--batch` is in the arguments, run the batch validator across multiple functions.

```bash
venv/bin/python -m scripts.permuter.batch_validate $ARGUMENTS
```

Pass through any additional flags like `--limit`, `--min-pct`, `--max-pct`, `--apply-all`.

## Tips

- The permuter works best on functions at 50-99% match — it finds signed/unsigned mismatches
  and variable extraction opportunities that are hard to spot manually.
- Build failure rate should be under 10%. If it's higher, something may be wrong.
- For functions with no variants generated, the function likely has no comparisons or
  variable expressions that match the permuter's patterns.
- If a variant improves the match, review the diff carefully before applying — the permuter
  finds mechanical fixes, not semantic ones.
