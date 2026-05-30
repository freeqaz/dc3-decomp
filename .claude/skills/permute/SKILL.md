---
name: permute
description: Run decomp-synth (the guided source-synthesis permuter) on a function — beam search over 112 behaviour-neutral C++ transforms (signed/unsigned, declaration reorder, branch polarity, variable extraction, …), each scored with objdiff, to drive a near-match toward 100%. Use when a function is below 100% and you want to automatically try source variations.
argument-hint: "[symbol-or-function] [--unit GLOB] [--dry-run] [--no-apply]"
allowed-tools: Bash(venv/bin/python *), Bash(ninja *), Read, Grep, Glob
---

# Permuter Skill (`decomp-synth`)

Automatically tries source-level pattern transformations (signed/unsigned casts, declaration
reorder, branch polarity, variable extraction, loop rewrites, etc.) and scores each variant with
objdiff. It AST-scans for relevant patterns first, then **searches only those** — beam search by
default (multi-state), with Ghidra/m2c guidance, N-stage chains, and constrained synthesis all
**on by default**. You only need `--no-*` flags to turn things off.

> **Where it lives:** the engine is the standalone `decomp_synth` package in
> `../decomp-synth` (sibling of this repo), installed editable in this repo's venv. DC3's config
> is `decomp-synth.json` at the repo root. Full reference: `../decomp-synth/docs/README.md`
> (architecture, search strategy) and `../decomp-synth/docs/patterns.md` (pattern catalog).
> Run from the repo root.

## Arguments

`$ARGUMENTS`

## Usage

### Single function (most common)

Tuned for thoroughness on one function (more rounds/variants than the sweep defaults):

```bash
venv/bin/python -m decomp_synth.scan_and_permute --symbol '$0' --max-rounds 10 --max-variants 100 --plateau-limit 3 --chain-depth 5
```

Accepts mangled symbols, qualified names, or partial names:
- `--symbol '?Seek@AsyncFile@@UAAHHH@Z'`
- `--symbol 'AsyncFile::Seek'`
- `--symbol 'Seek'`

### Unit scan

```bash
venv/bin/python -m decomp_synth.scan_and_permute --unit 'system/obj/*'
```

### Bulk / triage sweep

```bash
# Sweep a band of near-matches across the codebase
venv/bin/python -m decomp_synth.scan_and_permute --min-pct 80 --max-pct 99.99 --jobs 16 --limit 200

# Only functions the DB marked AT_LIMIT (re-attack with fresh patterns)
venv/bin/python -m decomp_synth.scan_and_permute --status at_limit --jobs 8 --limit 100
```

## Key flags

| Flag | Default | What it does |
|------|---------|--------------|
| `--symbol NAME` | — | Target one function (repeatable) |
| `--unit GLOB` | all | Scope to units matching glob |
| `--status {at_limit,workable,all}` | all | Filter sweep by decomp.db verdict |
| `--strategy {beam,hill_climb,evolutionary}` | beam | Search strategy; `hill_climb` is greedy + faster per fn |
| `--max-rounds N` | 5 | Hill-climbing rounds per function (use 10 for single fn) |
| `--max-variants N` | 50 | Variants per round (use 100 for single fn) |
| `--plateau-limit N` | 2 | Stop after N rounds w/o improvement (use 3 for single fn) |
| `--chain-depth N` | 5 | Max chain depth for N-stage composition |
| `--max-pct N` | 99.99 | Skip functions already above N% |
| `--min-pct N` | 0 | Skip functions below N% |
| `--limit N` | unlimited | Cap number of functions processed (0 = unlimited) |
| `--jobs N` | 1 | Parallel workers (by source file) |
| `--dry-run` | off | Scan only — show pattern hits, don't search/build |
| `--no-apply` | off | Score variants but don't write changes to source |
| `--patterns X,Y` | all | Only run specific patterns (`--list-patterns` to see all 112) |
| `--no-ghidra` | — | Disable Ghidra-guided patterns |
| `--no-m2c` | — | Disable m2c-guided context loading |
| `--no-chain` | — | Disable multi-stage pattern chains |
| `--no-adaptive` | — | Disable adaptive pattern suppression/boosting |
| `--no-constrained` | — | Disable constraint-directed synthesis |
| `--json` | off | Machine-readable output |

## Tips

- **Start at 90%+.** The search is most effective on near-matches; below ~80% the divergence is usually structural, not register allocation.
- **`--dry-run` first** to see which patterns even hit before paying for builds.
- **`--strategy hill_climb`** for faster, cheaper sweeps; keep the default `beam` for hard single functions.
- **BSF tracing (declaration reorder) needs ptrace** — run outside any seccomp sandbox.

## What to report

After running, tell the user:
- How many functions were scanned and how many improved
- For each improvement: function name, old% → new%, winning pattern(s)
- Whether changes were applied to source (default: yes)
