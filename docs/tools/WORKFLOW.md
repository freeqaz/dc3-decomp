# Decomp Tool Workflow

Decision guide for DC3 decompilation tools. Each tool serves a specific purpose - use the right one for your task.

## Quick Reference

| Scenario | Tool | Command |
|----------|------|---------|
| Starting a new function | `analyze-function` | `./bin/analyze-function "Foo::Bar"` |
| Quick m2c decompilation | `decompile.sh` | `tools/decompile.sh "Foo::Bar"` |
| Full analysis with m2c | `analyze-function --m2c` | `./bin/analyze-function "Foo::Bar" --m2c` |
| Quick match % check | `objdiff-cli diff` | `./bin/objdiff-cli diff -p . "Foo::Bar"` |
| Find work targets | `report query` | `./bin/objdiff-cli report query build/373307D9/report.json --functions --min-percent 90 --max-percent 99` |
| Batch triage | `report analyze` | `./bin/objdiff-cli report analyze build/373307D9/report.json --min-percent 90 --limit 50` |
| Generate initial C from assembly | `m2c` | See [m2c.md](m2c.md) |
| Understand call graph | Ghidra MCP | `gen_callgraph` via MCP |
| Diagnose why code doesn't match | `objdiff-cli diff --verdict` | `./bin/objdiff-cli diff -p . "Foo::Bar" --verdict` |

## Tool Selection

### analyze-function

**When:** Starting work on any function, regardless of match percentage.

**Why:** Combines objdiff verdict + Ghidra decompilation + cross-references in one view. Provides complete context before you start writing code.

```bash
./bin/analyze-function "Game::PollShuttle"
./bin/analyze-function "Game::PollShuttle" -f json  # For programmatic use
./bin/analyze-function "Game::PollShuttle" --no-xrefs  # Faster, skip xrefs
```

**Output includes:**
- Match percentage and verdict
- Ghidra pseudo-C decompilation
- Functions that call this one (callers)
- Functions this one calls (callees)
- Suggested next commands

### objdiff-cli diff

**When:** Iterating on code, need fast feedback on match status.

**Why:** Faster than analyze-function, doesn't require Ghidra connection.

```bash
# Interactive TUI (default)
./bin/objdiff-cli diff -p . "Game::Poll"

# With verdict analysis
./bin/objdiff-cli diff -p . "Game::Poll" --verdict

# Rebuild before diffing (edit-compile-check loop)
./bin/objdiff-cli diff -p . "Game::Poll" --build --verdict

# JSON output for scripting
./bin/objdiff-cli diff -p . "Game::Poll" -f json --verdict
```

**Key flags:**
- `--verdict` - Get fixability classification (LIKELY_FIXABLE, AT_LIMIT, etc.)
- `--build` - Run ninja before diffing (great for iteration)
- `--analyze` - Detect mismatch patterns without full verdict
- `-C <N>` - Show N instructions of context around mismatches (like grep -C)
- `--full-listing` - Show all instructions, not just mismatches

**Note:** Markdown is now the default output format (no `-f` needed).

### objdiff-cli report query

**When:** Finding functions to work on.

```bash
# Near-matches (good targets)
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-percent 99 --limit 20

# Small, easy functions
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-size 200 --sort-by size --sort-order asc

# Functions in a specific subsystem
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --unit "default/lazer/game/*" --min-percent 50
```

### objdiff-cli report analyze

**When:** Batch triage - want verdicts for many functions at once.

```bash
./bin/objdiff-cli report analyze build/373307D9/report.json \
  --min-percent 90 --max-percent 99 --limit 50 -f json-pretty
```

**Output groups functions by verdict:**
- `LIKELY_FIXABLE` - Control flow differences, worth investigating
- `MAYBE_FIXABLE` - Register swaps or comparison style issues
- `AT_LIMIT` - Linker-merged calls or bool masks, accept current match
- `NEEDS_INVESTIGATION` - Mixed signals, needs manual review

### m2c / decompile.sh

**When:** Starting a function with 0% match or very low match. Need initial C structure.

**Why:** Generates C code from assembly, giving you a starting point rather than writing from scratch.

**Quick workflow (recommended):**
```bash
# One-command decompilation from target binary
tools/decompile.sh "Foo::Bar"

# With Ghidra type context (better output)
tools/decompile.sh "Foo::Bar" --context

# Or use analyze-function with m2c
./bin/analyze-function "Foo::Bar" --m2c
```

**Manual workflow:**
```bash
# Get disassembly from objdiff and convert to m2c format
# --project-dir enables automatic jump table resolution for switch statements
./bin/objdiff-cli diff -p . "Foo::Bar" -f json --include-instructions | \
  python3 tools/objdiff_to_m2c.py --project-dir . | \
  python3 ~/code/milohax/m2c/m2c.py -t ppc -

# With Ghidra type context for better output
python3 tools/ghidra/export_types.py --function "Foo::Bar" -o /tmp/ctx.h
python3 ~/code/milohax/m2c/m2c.py -t ppc --context /tmp/ctx.h input.s
```

**Note:** m2c output needs cleanup. Use it as a starting point, then refine.

### Ghidra MCP

**When:** Need to understand function relationships, find callers, or search by semantics.

**Key tools:**
- `decompile_function` - Get pseudo-C for any function
- `gen_callgraph` - Visualize call relationships
- `list_cross_references` - Find all references to a function
- `search_code` - Semantic search over decompiled code
- `search_strings` - Find functions by string usage

**Note:** `analyze-function` already includes Ghidra decompilation and xrefs. Use Ghidra MCP directly only for advanced queries (call graphs, semantic search).

## Workflows

### New Function (any match %)

```
1. ./bin/analyze-function "Foo::Bar" --m2c
   - Understand what the function does (Ghidra decompile)
   - Check current match % and verdict
   - Get m2c decompilation for starting point
   - Note callers/callees for context

2. If 0% match and complex:
   - Use the m2c output from step 1 as starting point
   - Or use: tools/decompile.sh "Foo::Bar" --context
   - Or reference RB3 decomp for shared engine code

3. Write/edit C++ code

4. Iterate:
   ./bin/objdiff-cli diff -p . "Foo::Bar" --build --verdict

5. When verdict shows AT_LIMIT or 100%: done
```

### Near-Match (90%+) Tweaking

```
1. ./bin/objdiff-cli diff -p . "Foo::Bar" --verdict

2. Check verdict:
   - LIKELY_FIXABLE: Apply suggested patterns
   - MAYBE_FIXABLE: Try variable reordering, comparison tweaks
   - AT_LIMIT: Accept current match, move on

3. If fixable, iterate:
   - Edit code
   - ./bin/objdiff-cli diff -p . "Foo::Bar" --build --verdict
   - Repeat until 100% or AT_LIMIT
```

### Finding Work Targets

```
# Option A: Find by match percentage
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-percent 99 --limit 20

# Option B: Batch triage with verdicts
./bin/objdiff-cli report analyze build/373307D9/report.json \
  --min-percent 90 --limit 50 -f json-pretty | \
  jq '.results.LIKELY_FIXABLE'

# Option C: Find small functions (easier to match)
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 80 --max-size 300 --sort-by size --sort-order asc
```

### Verifying a Match

```bash
# Quick check
./bin/objdiff-cli report function build/373307D9/report.json "Foo::Bar"

# Full verification
./bin/objdiff-cli diff -p . "Foo::Bar" --verdict
```

## Verdict Reference

| Verdict | Meaning | Action |
|---------|---------|--------|
| `COMPLETE` | 100% match | Done |
| `LIKELY_FIXABLE` | Control flow diffs, low merged ratio | Investigate if/else, loops |
| `MAYBE_FIXABLE` | Register swaps, comparison style | Try reordering variables |
| `AT_LIMIT` | Linker-merged calls, bool masks | Accept current %, move on |
| `NEEDS_INVESTIGATION` | Mixed patterns | Manual analysis needed |

## Common Patterns

### Linker-Merged Functions (verify then accept)
Target calls `merged_*` functions. Before accepting as unfixable:
1. Look up what symbols share the merged address (`./bin/merged-symbols <addr>`)
2. Verify YOUR call target is in that set
3. If verified: accept current match, move on
4. If NOT in set: you may be calling the wrong function - investigate

### Bool Mask (usually unfixable)
Differences in `clrlwi`/`rlwinm` for bool return handling. Compiler optimization.

### Control Flow (often fixable)
Branch instruction differences (`beq` vs `bne`). Check:
- if/else ordering
- Loop structure
- Comparison operators (`>` vs `>=`)

### Register Allocation (sometimes fixable)
Consistent register swaps. Try:
- Reordering variable declarations
- Reordering struct members
- Changing parameter order (if confirmed via DWARF)

## diff_inspect — Deep Mismatch Analysis

**When:** `objdiff --verdict` tells you something is wrong but you need to understand WHY.

**Why:** Provides structured analysis of mismatch patterns that objdiff's verdict summarizes but doesn't break down.

### Direct Usage

```bash
# Root cause analysis (start here)
python3 scripts/analysis/diff_inspect.py --symbol "Foo::Bar" --diagnose

# With worktree support
python3 scripts/analysis/diff_inspect.py --symbol "Foo::Bar" --diagnose --project-dir /tmp/claude/my-branch

# From existing JSON
python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --diagnose
python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --clusters
python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --regswaps
python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --offsets
python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --replaces

# Compare two snapshots (before/after)
python3 scripts/analysis/diff_inspect.py --compare baseline.json current.json
```

### MCP Tool (for agents)

```
mcp__orchestrator__run_diff_inspect
  symbol: "Foo::Bar"
  mode: "diagnose"              # or clusters/regswaps/offsets/replaces/compare/save_baseline
  project_dir: "/tmp/worktree"
```

### Mode Selection Guide

| Mode | Use When | Output |
|------|----------|--------|
| `diagnose` | First analysis — don't know what's wrong | Root cause summary with actionable suggestions |
| `clusters` | Seeing scattered insert/delete mismatches | Contiguous mismatch groups with context |
| `regswaps` | Verdict mentions register allocation | GPR/FPR swap pairs and frequency |
| `offsets` | Seeing offset differences in memory ops | Offset shift histogram + outlier detection |
| `replaces` | Many "replace" diffs, unclear which matter | Categorizes noise (trivial) vs real (structural) |
| `compare` | Want to see if edits improved things | Delta table: match% change, mismatch deltas |
| `save_baseline` | About to start editing, want a reference point | Saves current state for later `compare` |
