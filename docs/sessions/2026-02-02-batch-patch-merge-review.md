# Batch Patch Merge Strategy — Review

**Date:** 2026-02-02
**Reviewing:** `docs/sessions/2026-02-02-batch-patch-merge-strategy.md`

## Issues

### 1. The merge agent is an expensive hammer for a cheap nail

The plan jumps from "textual `git apply` failed" straight to "spawn Opus" without trying `git apply --3way`, which uses the base commit as a merge base. The actual conflict scenario is narrow: two agents editing different functions in the same `.cpp` file where context lines overlap. `--3way` handles this cleanly in most cases for free.

**Suggestion**: Add a `git apply --3way` pass between clean apply and agent merge. This will resolve the vast majority of same-file-different-function conflicts without an agent call.

### 2. No `git add` / `git commit` between clean-apply passes

After applying the first few clean patches, the working tree is dirty. Subsequent `git apply` calls on the same file may fail not because of a real conflict, but because the file was already modified by an earlier clean patch. The plan doesn't address staging between patches within the merge cycle.

**Suggestion**: Either `git add` after each successful apply (so subsequent applies see the updated index), or apply all patches to a fresh checkout in sequence with `--index`.

### 3. The `_patch_filename` sanitization is fragile

```python
safe_name = symbol.replace("?", "").replace("@", "_")[:50]
```

MSVC mangled symbols contain `$`, `<`, `>`, and `@` sequences that produce adjacent underscores. Truncating at 50 chars can produce collisions (two different symbols sharing a prefix), silently overwriting patches.

**Suggestion**: Append a hash suffix (e.g., `sha256(symbol)[:8]`) to the truncated name to guarantee uniqueness.

### 4. Race condition: `maybe_flush()` is async but `add()` is sync

The accumulator stores patches in a list. `maybe_flush()` is async and triggers a merge cycle. If two agents complete near-simultaneously, `add()` could be called while `flush()` is mid-execution — both reading/mutating `self.pending`. Python's asyncio is single-threaded so this is technically safe *if* you never `await` between reading and clearing `self.pending` in `flush()`. But the merge cycle involves spawning agents (lots of awaits). The plan doesn't call this out.

**Suggestion**: In `flush()`, immediately do `batch, self.pending = self.pending, []` before any async work.

### 5. Batch directory timestamp can collide

The batch directory uses `YYYYMMDD_HHMMSS` granularity. If `maybe_flush()` triggers twice within the same second (unlikely but possible with `batch_size=1` during testing), you get a collision.

**Suggestion**: Add milliseconds or use a monotonic counter.

### 6. The "one agent for all conflicts" prompt can blow up

The plan acknowledges this ("If prompts get too large, split by file") but doesn't define "too large" or implement the split. With 10 accumulated patches, if 5 conflict on the same file, you're sending the full file content + 5 raw diffs + metadata + objdiff instructions. This can easily exceed context limits for large files like `Rnd_Xbox.cpp`.

**Suggestion**: Define a concrete threshold (e.g., 100KB total prompt) and implement the file-group split from the start. Don't wait for it to fail in production.

### 7. The commit message "lists all symbols" — at what scale?

With `batch_size=10`, listing 10 symbols is fine. But if `flush()` is called at batch end with 30+ pending patches, the commit message becomes unwieldy.

**Suggestion**: Cap the symbol list in the commit message (e.g., first 10 + "and N more") or structure it as a bullet list.

### 8. Missing: what happens to `PatchApplier` stats?

The plan replaces `PatchApplier.maybe_apply()` with `PatchAccumulator.add()`. But `run_batch` and `run_batch_with_targets` both report `self.patch_applier.stats()` in the summary. The plan doesn't say whether `PatchAccumulator` feeds into `PatchApplier` stats or replaces them entirely. This will break the batch summary output.

### 9. The regression check in the merge agent is underspecified

The plan says "if a symbol regresses below its `start_percent`, the agent reverts that specific change." But the agent is merging multiple patches into the same file. Reverting one function's changes from a file where multiple functions were edited requires surgical precision — the agent needs to understand which hunks belong to which symbol. The plan assumes the agent can do this reliably but doesn't include hunk-to-symbol mapping in the prompt structure.

**Suggestion**: Include explicit markers or line ranges mapping each patch's hunks to specific symbols, so the agent can revert per-symbol cleanly.

### 10. Ambiguity: merge cycle working directory

Step 1 says "fresh worktree on HEAD." Step 5 says "result committed to main." These contradict — is the merge cycle working in `main_repo` directly or in a fresh worktree? The fresh worktree isn't from the pool, so the plan doesn't say where it lives, how it's cleaned up, or what happens if creation fails.

**Clarification needed**: Pick one and document it.

## Minor Notes

- Patch ordering analysis (completion order is fine) is sound.
- Crash recovery via raw patches on disk is a good design.
- The `add()` return type being lightweight is correct.

## Overall Assessment

The core idea is sound: accumulate patches and batch-apply them. The main risk is over-engineering the agent merge path before knowing how often conflicts actually occur. Consider instrumenting the current `--reject` path first to measure conflict rate, then building the agent merge if conflicts are > 5% of patches. If proceeding as-is, the `git apply --3way` gap (point 1) and the missing staging between applies (point 2) are the most likely causes of bugs on day one.
