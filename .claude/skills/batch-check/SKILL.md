---
name: batch-check
description: Batch-check all untracked functions in a unit. Runs objdiff on each, auto-reports 100% matches as COMPLETE. Returns summary with counts and partial-match details. Use this instead of manual query+objdiff+report loops.
argument-hint: "[unit-pattern] [--dry-run] [--skip-boilerplate]"
allowed-tools: mcp__orchestrator__batch_check, mcp__orchestrator__get_progress
---

# Batch Check Skill

Sweep an entire unit (or glob of units) for already-matching functions and auto-mark them
COMPLETE in the database. Replaces the tedious query+objdiff+report loop.

## Arguments

`$ARGUMENTS`

## Steps

1. **Parse the arguments.** `$0` is the unit pattern. It can be:
   - A full unit path: `default/system/char/CharBones`
   - A glob pattern: `default/system/char/*`
   - A short name: `system/char/*` (prepend `default/` if no prefix)

   If `$0` doesn't start with `default/`, prepend it:
   - `system/char/*` becomes `default/system/char/*`
   - `lazer/meta_ham/*` becomes `default/lazer/meta_ham/*`

2. **Run the batch check** using the MCP tool:
   - Call `mcp__orchestrator__batch_check` with `unit_pattern` set to the resolved pattern
   - If `--dry-run` is in arguments, set `dry_run: true`
   - If `--skip-boilerplate` is in arguments, set `skip_boilerplate: true`

3. **Present the results.** The tool returns:
   - **Checked**: total functions diffed
   - **Newly COMPLETE**: functions that matched 100% and were marked COMPLETE
   - **Partial**: functions with >0% but <100% match (listed with percentages)
   - **Unimplemented**: functions with no decomp object (base_size=0)
   - **Failed**: symbols not found by objdiff

4. **Optionally show progress** after the sweep by calling `mcp__orchestrator__get_progress`.

## Flags

- `--dry-run`: Check functions but don't update the database
- `--skip-boilerplate`: Skip atexit destructors, dynamic initializers, MakeString templates, vcall thunks, and vector ctor/dtor iterators

## Tips

- Run without `--dry-run` to auto-mark 100% matches as COMPLETE in one shot
- Use broad globs like `default/system/*` to sweep entire subsystems
- Partial matches in the output are good candidates for manual decomp work
- After a sweep, run `/batch-check` on another unit or check progress with `get_progress`
