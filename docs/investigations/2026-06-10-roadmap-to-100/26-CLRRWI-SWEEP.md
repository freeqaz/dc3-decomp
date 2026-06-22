# 26 — ObjPtr-deref no-op-clrrwi sweep

**Date:** 2026-06-11. Sweep to confirm/apply the `pattern_objptr_deref_zeroext_clrrwi`
lever across the open frontier. 6 worktree-isolated agents, unit-disjoint, scan+fix.

## The pattern (what to find)

An EXTRA base-only `clrrwi rX, rX, 0` (same src/dst register, immediate **0**) — equivalently
`rlwinm rX, rX, 0, 0, 31` — that the **target does not have**. On 64-bit PPC GPRs with 32-bit
pointers this is a redundant zero-extension of the low 32 bits (clears bits 32-63), emitted
when the compiler loses track of the zero-extension through a smart-pointer deref
(`ObjPtr<T>::operator->` / `operator T*`), typically when the same ObjPtr is tested in an
`if` and re-dereferenced in the body, or `objPtr->Method()`/`objPtr->field` is used more than
once.

In `run_objdiff` full-listing / `run_diff_inspect mode=mismatches` it appears as an
`insert` (base-only) row whose instruction is `clrrwi rX, rX, 0`. **Confirmed example
(landed a0d5f7a5):** `NgSpotlightDrawer::RenderCone` 98.7→100, `RenderSphere` 87.7→90.2.
The fix that already exists in the same file: `RndMat *mat = sl->mBeam.mMat;` then
`mat->GetColor()...`.

## The fix

Cache the dereferenced smart-pointer result in a plain local once, so the conversion
happens a single time and the pointer stays cleanly zero-extended:
```cpp
const Hmx::Color &c = obj->mPtr->GetColor();   // or: RndMat *m = obj->mPtr;  T *p = objPtr;
... use c / m / p ...
```
Preserve each statement's operand order (don't reorder commutative float ops). The fix is
behavior-identical.

## Global rules (every agent)

1. Create your OWN worktree: `scripts/setup_worktree.sh /home/free/code/milohax/wt-clrrwi-bucketN clrrwi-sweep/bucketN` (run from the main repo). Then `ninja` once in it to prime (REQUIRED — fresh worktrees mis-measure otherwise). Do ALL edits there; pass `project_dir=<your worktree>` to every `mcp__orchestrator__` tool.
2. Never edit main-repo files, never commit to dc3 `main`, never write `decomp.db`, no `git stash`, no Co-Authored-By.
3. Your unit set is disjoint from other agents — you will not collide. Only touch files for the units in your bucket.
4. Load the orchestrator tools via ToolSearch (`select:mcp__orchestrator__run_objdiff,mcp__orchestrator__run_diff_inspect`).

## Per-agent procedure

1. Read your bucket file `/tmp/clrrwi_buckets/bucketN.json` (array of `{symbol, unit, percent, size}`).
2. For EACH function: `run_objdiff(symbol, project_dir=<worktree>, full_listing=true)` (or concise then full only if needed). Record the fresh baseline normalized %. Scan the listing for a base-only `clrrwi rX,rX,0` / `rlwinm rX,rX,0,0,31` (the no-op). **If absent → SKIP (no fix), record as scanned-no-signature.**
3. For a HIT: `run_diff_inspect(symbol, mode=attributed, project_dir=<worktree>)` to get the source line. Identify the smart-pointer deref feeding the no-op (an `ObjPtr<T>`/Hmx-handle member dereferenced in condition+body or multiple times). Apply the cache-the-deref fix in the worktree source. Re-run `run_objdiff`. **Keep only if normalized improved (or raw improved with normalized equal); otherwise REVERT that file hunk.** Never accept a regression on the touched function — and spot-check you didn't regress a sibling function in the same file.
4. Commit all kept improvements in your units to your branch (one commit or per-unit commits, clear messages).

## Return (structured)

`{ bucket, worktree, branch, scanned, signature_hits, fixes:[{unit,symbol,before,after,note}], no_signature_count, reverted:[{symbol,reason}], notes }`. Percentages are your worktree run_objdiff plane.

## Orchestrator follow-up

Merge `clrrwi-sweep/bucket*` (disjoint units → clean), sync+reconcile+certify, run the bare
milo-tests gate, commit `27-CLRRWI-SWEEP-RESULTS.md`, update `pattern_objptr_deref_zeroext_clrrwi`
memory with the measured prevalence.
