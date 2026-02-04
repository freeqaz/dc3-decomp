# Batch Patch Merge Strategy

**Date:** 2026-02-02

## Problem

When batch-running N agents in parallel, multiple agents can modify the same `.cpp` file for different functions. Patches are extracted as `git diff HEAD` against the worktree's base commit — but the main repo advances as earlier patches land. Later patches have stale context lines and fail or partially apply via `--reject`.

With the recent change to apply style-only patches (not just improvement patches), collision rate increases.

### Current Architecture

- Worktrees sync to main repo HEAD at **acquire** time
- Agents work in isolation, unaware of each other's changes
- Patches extracted as static diffs against worktree base
- Applied to main repo serially as agents finish (first-done-first-applied)
- Conflicts handled by `git apply --reject` (partial apply, `.rej` cleanup)

Key files:
- `scripts/orchestrator/worktree_pool.py` — `extract_patch()`, `acquire()`/`release()`
- `scripts/orchestrator/patch_applier.py` — `apply_patch_to_main()`, `PatchApplier.maybe_apply()`
- `scripts/orchestrator/core.py` — `_execute_session()` (step 14: patch apply), batch loops

## Chosen Strategy: Batch Accumulate + Agent Merge

### Flow

```
agents work in parallel
    |
patches accumulate in a list (not applied immediately)
    |
every N completions (or batch end):
    1. fresh worktree on HEAD
    2. try git apply for each patch
    3. patches that fail -> handed to an Opus agent with refactor-staff
    4. agent reads failed patches + current file state, merges semantically
    5. result committed to main
```

### Key Components

#### 1. `PatchAccumulator` (new class in `patch_applier.py`)

Replaces the immediate `maybe_apply()` call in `_execute_session`:

- Stores `(patch, symbol, metadata)` tuples as agents complete
- When `len(pending) >= batch_size` or `flush()` is called, triggers a merge cycle
- Tracks which patches applied cleanly vs needed agent help
- Writes all raw patches to `generated-patches/<batch_timestamp>/` for crash recovery and batch grouping

```python
class PatchAccumulator:
    def __init__(self, main_repo: Path, batch_size: int = 10, ...):
        self.pending: list[PatchEntry] = []
        self.batch_size = batch_size
        ...

    def add(self, patch, symbol, start_percent, end_percent, exit_status) -> dict:
        """Store patch for later batch application. Returns immediately."""
        ...

    async def maybe_flush(self) -> Optional[MergeResult]:
        """If pending >= batch_size, trigger merge cycle. Called after each agent completes."""
        ...

    async def flush(self) -> MergeResult:
        """Force merge all pending patches. Called at batch end."""
        ...
```

#### 2. Merge Cycle (the interesting part)

When flush triggers:

1. **Clean apply pass**: Try each accumulated patch with `git apply` in order. Most patches touching different files or different functions in the same file will apply cleanly.

2. **Collect failures**: Group failed patches by file. Each failure has the raw diff hunks and the target file path.

3. **Agent merge pass**: For files with conflicts, spawn an Opus agent that receives:
   - The current state of each conflicting file (on HEAD, after clean patches applied)
   - The raw patch hunks that failed (`.rej` content)
   - The symbol name and intent of each patch
   - Instructions to merge the changes semantically and clean up with refactor-staff methodology
   - A requirement to run objdiff on each affected function to verify no regressions

4. **Commit**: All changes (clean applies + agent merges) committed together as one atomic update.

#### 3. Integration into `core.py`

In `_execute_session`, step 14 changes from:

```python
# Current: apply immediately
apply_result = self.patch_applier.maybe_apply(patch=patch, ...)
```

To:

```python
# New: accumulate for batch merge
accum_result = self.patch_accumulator.add(patch=patch, symbol=func["symbol"], ...)

# Check if we should trigger a merge cycle
merge_result = await self.patch_accumulator.maybe_flush()
if merge_result:
    logger.info(f"Merge cycle: {merge_result.clean_count} clean, {merge_result.agent_merged_count} agent-merged")
```

The batch loops (`run_batch`, `run_batch_with_targets`) call `flush()` after all agents complete to handle remaining patches.

### Trade-offs

| Aspect | Assessment |
|--------|-----------|
| **Correctness** | Better than current. LLM understands semantic intent ("this adds a function body, that changes a header") and merges correctly. Current `--reject` is purely textual. |
| **Robustness** | Good. Raw patches always saved to `generated-patches/`. If merge agent crashes, patches are recoverable. Clean-apply path handles the common case without agent cost. |
| **Incremental progress** | Results visible in chunks of N (default 10) rather than one-at-a-time. Tunable via `batch_size`. |
| **Cost** | One Opus agent call per merge cycle, but **only when conflicts exist**. If all patches apply cleanly, no agent needed. |
| **Complexity** | Moderate. New `PatchAccumulator` class + merge logic. Agent prompt is straightforward (here are the patches, here are the files, merge them). |

### Configuration

```python
DecompOrchestrator(
    auto_apply=True,
    patch_batch_size=10,       # merge every 10 patches
    merge_model="opus",        # model for conflict resolution agent
)
```

### Patch File Naming

Patches are grouped by batch under a timestamp-prefixed directory, and individually named to convey status at a glance.

**Directory structure:**

```
generated-patches/
  20260202_143012/          # batch timestamp (YYYYMMDD_HHMMSS)
    CharBones__Set_45pct.patch
    CharClip__Poll_100pct.patch
    RndDir__Replace_82pct_REGRESSED.patch
  20260202_151530/
    ...
```

**Naming rules:**

- `<safe_symbol>_<end_percent>pct.patch` — normal case
- `<safe_symbol>_<end_percent>pct_REGRESSED.patch` — when `end_percent` is more than 1% below `start_percent`

The batch directory groups patches that were accumulated together and should be applied as a unit. On crash recovery, you can feed an entire batch directory to a merge cycle. The `_REGRESSED` suffix makes it easy to `ls *REGRESSED*` and see which patches need investigation.

**Implementation in `PatchAccumulator`:**

```python
def _patch_filename(self, symbol: str, start_percent: float, end_percent: float) -> str:
    safe_name = symbol.replace("?", "").replace("@", "_")[:50]
    suffix = "_REGRESSED" if (end_percent - start_percent) < -1.0 else ""
    return f"{safe_name}_{end_percent:.0f}pct{suffix}.patch"
```

The batch directory is created once per `PatchAccumulator` instance (or per merge cycle, if accumulator spans multiple cycles) using the timestamp of when the first patch in the batch was added.

### Crash Recovery

All raw patches are written to their batch directory in `generated-patches/` immediately on `add()`. If the orchestrator dies mid-batch:
- Patches from completed agents are on disk, grouped by batch
- The merge cycle hasn't run yet, so main repo is clean
- On restart, point a new merge cycle at the batch directory to resume

### Design Decisions

#### Patch ordering within a merge cycle

Patches are applied in **completion order** (insertion order into the accumulator). This is sufficient because:

- Patches touching different files never conflict regardless of order.
- Patches touching the same file but different functions usually have non-overlapping hunks — `git apply` handles these fine in any order.
- Patches touching overlapping regions of the same file will fail the clean-apply pass regardless of order and get routed to the agent merge pass, which resolves them semantically.

No smarter ordering heuristic is needed. The clean-apply pass is a fast filter; the agent merge pass handles the hard cases.

#### Merge agent prompt: what metadata to include

Each `PatchEntry` already stores `start_percent`, `end_percent`, and `exit_status`. The merge agent prompt must include these so it can prioritize correctly:

- **Improvement patches** (`end_percent > start_percent`) take priority over **style-only patches** (`end_percent == start_percent`). If two patches conflict on the same function, the improvement patch wins.
- **exit_status** is included so the agent knows not to trust patches from `"stuck"` agents as strongly as `"complete"` ones.
- The prompt explicitly lists each patch with: symbol, start%, end%, status, and a one-line description of the change intent (extracted from the agent's `notes` field in `record_attempt`).

#### Commit granularity

All patches in a merge cycle (clean applies + agent merges) are committed as **one atomic commit**. Rationale:

- Each patch targets a different function. `git bisect` at the function level isn't meaningful here — if a function regresses, the objdiff verification in the merge cycle catches it before commit.
- One commit per patch would add 10+ commits per cycle, creating noise in `git log` with no practical benefit.
- The commit message lists all symbols included, so `git log --grep` can still find when a specific function was changed.

If this becomes a problem in practice (e.g., needing to revert a single function's change), the raw patches in `generated-patches/` can be used to reconstruct individual changes.

#### Regression handling in the merge agent

The merge agent receives the symbol list from all conflicting patches and must run `objdiff` on each one after merging. The behavior on regression:

1. **Agent applies all conflicting patches semantically** to the current file state.
2. **Agent runs objdiff on each affected symbol.** The prompt includes the expected `end_percent` from each patch's metadata.
3. **If a symbol regresses below its `start_percent`**: the agent reverts that specific change, logs a warning, and continues with the remaining patches. The failed patch is preserved in `generated-patches/` for manual retry.
4. **If a symbol is between `start_percent` and `end_percent`**: acceptable — context line shifts can cause minor differences. The agent keeps it.
5. **If the merge agent itself errors out** (crash, timeout): fall back to `git apply --reject` for the clean hunks only. Failed patches are logged and preserved. The batch is not blocked.

This means the worst case is: some patches don't land in this cycle. They're never lost (raw patches on disk) and can be retried in a future cycle or manually.

#### `add()` return type

`PatchAccumulator.add()` returns a lightweight dict for logging only:

```python
{"index": 5, "symbol": "...", "stored": True}
```

No apply result — that comes from `flush()`. The caller doesn't need to act on `add()`'s return value.

### Resolved Open Questions

1. **One agent per file group vs one agent for all conflicts?** — Start with one agent for all conflicts in a merge cycle. If prompts get too large, split by file.
2. **Should the merge agent also run objdiff?** — Yes. It receives the symbol list and expected percentages from patch metadata. Regressions are reverted per-symbol.
3. **What if the merge agent itself fails?** — Fall back to `git apply --reject` for the clean hunks, log the failures. Don't block the batch. Raw patches preserved on disk.
4. **Should we rebase worktrees between merge cycles?** — Not needed. Worktrees are ephemeral and sync to HEAD at acquire time. The merge cycle applies to main, so the next acquire will pick up the merged state.
