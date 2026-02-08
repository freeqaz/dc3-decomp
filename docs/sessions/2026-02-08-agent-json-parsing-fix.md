# Fix: Agent JSON Parsing Failures in Batch Decomp

**Date:** 2026-02-08
**Files modified:** `scripts/orchestrator/mcp_server.py`, `scripts/master_agent_prompt.md`

## Problem

Batch decomp agents were wasting 2-5 tool calls per session trying to get instruction-level diff data from objdiff. The existing MCP tools didn't expose this directly:

- `run_objdiff` with `concise: true` only showed aggregate counts ("12 diff_arg") -- not *which* instructions mismatched
- `run_objdiff` with `concise: false` returned the full instruction table but was too verbose for quick iteration
- No mode existed to just list non-matching instructions

So agents improvised: running `objdiff-cli` directly via Bash with wrong flags (`--json` instead of `-f json`), or piping output through `python3 -c json.load()` on files that were empty or malformed. This burned tokens and produced nothing useful.

## Solution

Three changes to close the gap between what agents need and what tools provide.

### 1. New `mismatches` mode in `run_diff_inspect`

Added a new mode that runs objdiff internally, parses the JSON, filters to non-equal instructions, and returns a compact markdown table:

```
| Idx | Type     | Target                | Base                  | Note         |
|-----|----------|-----------------------|-----------------------|--------------|
|  14 | diff_arg | `lwz r7, 0x48(r3)`   | `lwz r8, 0x48(r3)`   | [reg:r7->r8] |
|  42 | replace  | `bl merged_82331360`  | `bl Symbol::op==`    |              |
```

Capped at 30 mismatches to avoid blowing up context on large functions.

Implementation reuses `fmt_instr()` and `diff_annotation()` from `scripts/diff_inspect.py` (already on `sys.path` via the MCP server's path setup at line 39).

### 2. Mismatch preview in concise `run_objdiff` output

Added a new section to `_format_enrichment_sections()` that extracts non-equal instructions from the JSON data (which the enrichment pipeline already has) and appends a compact list to the concise output. Uses adaptive limits:

| Match % | Limit | Rationale |
|---------|-------|-----------|
| >= 98%  | ALL   | Near-matches have very few mismatches; agents need every detail |
| 90-98%  | 15    | Moderate -- enough context without overwhelming |
| < 90%   | 8     | Bigger functions need structural changes, not instruction-by-instruction analysis |

When truncated, includes a hint: `*(Use run_diff_inspect mode: "mismatches" for full list)*`

This means agents get instruction-level context inline on every `run_objdiff` call without needing a separate tool call. For the common case (98%+ functions close to matching), they see everything.

### 3. Agent prompt safety rule

Added to the Safety Rules section:
> DO NOT run objdiff-cli directly via Bash -- Always use MCP tools

Also documented `mismatches` in the tool reference modes list and the analysis decision tree.

## Technical Details

- The `mismatches` handler in `_run_diff_inspect` runs `objdiff-cli diff -p <dir> <symbol> --include-instructions --build --incremental -f json` directly (same as `save_baseline` and `compare` modes), then processes the JSON in-process rather than shelling out to `diff_inspect.py`
- The enrichment preview in `_format_enrichment_sections` operates on `data["instructions"]` which is already available from the JSON run in `_run_objdiff` -- no extra subprocess needed
- Both paths use lazy `from diff_inspect import ...` to avoid import overhead when these code paths aren't hit

## Verification

- Python syntax validated via `py_compile`
- Import path confirmed working (`scripts/` is on `sys.path` per line 39)
- `ninja` build passes
