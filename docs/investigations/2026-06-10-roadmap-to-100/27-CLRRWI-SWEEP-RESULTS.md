# 27 — ObjPtr-deref no-op-clrrwi sweep: RESULTS

**Date:** 2026-06-11. Outcome of the 6-bucket, unit-disjoint, worktree-isolated sweep
described in [26-CLRRWI-SWEEP.md](26-CLRRWI-SWEEP.md). Verdict: the
`pattern_objptr_deref_zeroext_clrrwi` lever is **NARROW, not broad** — it fired cleanly on
exactly **1 of 241** scanned frontier functions.

## Headline numbers

| Metric | Count | Of |
|---|---|---|
| Functions scanned | 241 | (6 buckets) |
| Carried the strict signature (base-only no-op `clrrwi rX,rX,0` / `rlwinm rX,rX,0,0,31`) | 5 | 241 (2.1%) |
| Fixes kept (improved, committed) | **1** | 5 signature hits / 241 scanned |
| Reached 100% | **0** | 1 fix |
| Partial improvement (kept) | 1 | 1 fix |
| Signature hit but NOT fixable (reverted / no-help / wrong-direction) | 4 | 5 hits |
| No signature at all (SKIP) | 236 | 241 (97.9%) |

**Prevalence of the strict signature: 5/241 = 2.1%. Actionable (clean per-unit win): 1/241 = 0.41%.**

## Fix table (kept improvements)

| Bucket | Unit | Symbol | Before → After (norm) | Reached 100? | Note |
|---|---|---|---|---|---|
| 1 | `default/system/rndobj/Spline` | `?SyncDeformedDummyCtrlPoints@RndSpline@@ABAXHH@Z` | 84.4 → **86.0** | No (partial) | Base cached the `vector<CtrlPoint>` begin pointer (loaded once for `numPts = end()-begin()`) and reused it via a no-op `clrrwi r11,r10,0` at idx 80; target re-derefs the member per `&mDeformedCtrlPoints[0]`. Fix: compute `numPts = (int)mDeformedCtrlPoints.size()` so begin is reloaded per use. Behavior-identical. Raw 83.8 → 85.4. Siblings unchanged. Commit `145a28f5`. |

That is the **only** kept change in the entire sweep. Buckets 0, 2, 3, 4, 5 produced zero edits.

## Branches to merge

Only one branch carries a change worth merging; the rest are clean (no commits beyond the
sweep-plan commit `252c9cf5`).

| Branch | Worktree | Commits to merge | Net |
|---|---|---|---|
| `clrrwi-sweep/bucket1` | `/home/free/code/milohax/wt-clrrwi-bucket1` | `145a28f5` | SyncDeformedDummyCtrlPoints 84.4 → 86.0 |
| `clrrwi-sweep/bucket0` | `/home/free/code/milohax/wt-clrrwi-bucket0` | none | clean — nothing to merge |
| `clrrwi-sweep/bucket2` | `/home/free/code/milohax/wt-clrrwi-bucket2` | none | clean — nothing to merge |
| `clrrwi-sweep/bucket3` | `/home/free/code/milohax/wt-clrrwi-bucket3` | none | clean — nothing to merge |
| `clrrwi-sweep/bucket4` | `/home/free/code/milohax/wt-clrrwi-bucket4` | none | clean — nothing to merge |
| `clrrwi-sweep/bucket5` | `/home/free/code/milohax/wt-clrrwi-bucket5` | none | clean — nothing to merge |

**Merge action (not performed here):** fast-forward / cherry-pick only `145a28f5` from
`clrrwi-sweep/bucket1` onto `main`. The other five branches need no merge.

## Signature hits that did NOT yield a fix (the lever's anti-patterns)

These four cases are the most valuable output of the sweep — they map the boundaries of where
the lever **does not apply**. Each had a `clrrwi`-0-family instruction but could not be turned
into a clean win.

1. **`?ParseMarkup@RndText@@...`** (bucket 0, `rndobj/Text`, 85.4%) — TRUE base-only no-op
   `clrrwi r30,r11,0` (idx 278) re-zero-extending a font-map pointer for `memcpy`. **NOT FIXED:**
   buried in a 118-mismatch whole-body regalloc cascade (region 212–318 ~37% match, 27 regswaps).
   Not an isolated zero-extension divergence, so a cache-the-deref edit cannot land cleanly and
   would very likely regress. → **Lever anti-pattern: signature present but embedded in a deep
   regalloc cascade.**

2. **`?merge@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAAXABV1@@Z`** (bucket 1) — base-only
   `clrrwi r30,r9,0` (idx 44), a genuine `it->Obj()` ObjPtr deref. **NOT FIXED:** the deref/find
   logic lives in the **shared template header** `src/system/obj/ObjPtrVec_impl.h`, included by
   many TUs owned by other agents. Per the MakeString-per-TU precedent, a global header toggle is
   rejected for a unit-disjoint sweep — it needs a dedicated decomposition-test pass. Also sits in
   a deep loop-iteration divergence (72.3%, 21 regswaps). → **Lever anti-pattern: the deref is in
   a cross-TU shared template header — not safely exploitable in a disjoint sweep.**

3. **`?MoveLevel@RndConsole@@QAAXH@Z`** (bucket 1, `rndobj/Console`, 88.3%) — `clrrwi r11,r11,0` is
   **TARGET-only (a delete, idx 13)**: our build over-fused `mLevel += level` into a record-form
   `add.`, eliminating the target's separate `clrrwi + cmpwi`. Two behavior-identical rewrites
   tried; neither moved match% (record-form fusion is a backend lowering decision). Reverted. →
   **Lever anti-pattern: INVERSE direction (target-side clrrwi from record-form fusion) = backend
   floor, not source-fixable.**

4. **`?DrawBlacklight@RndText@@SAXXZ`** (bucket 0, `rndobj/Text`, 91.0%) — `clrrwi r3,r11,0` is
   **TARGET-only (delete)**: the base already computes the value cleanly in r3 and AVOIDS the
   zero-extension. This is the literal inverse of the lever. No fix possible/helpful. →
   **Lever anti-pattern: INVERSE direction (target emits the redundant clrrwi, base avoids it).**

### Adjacent near-misses worth noting (not counted as signature hits)

The strict same-register base-only no-op was absent, but related "deref-caching" themes appeared
in a different lowering and were correctly skipped (they do not respond to the cache-the-deref fix):

- **`Flow::Copy`** (bucket 4, `flow/Flow`) — base-only `clrrwi` inserts (idx 56/57/71) but
  **cross-register** and embedded in a 42%-match wholly-divergent inlined vector-iteration loop
  (different iteration strategy). Structural, not a no-op cache site.
- **`SaveAndUploadScores`** (bucket 4) — `clrrwi r23,r11,0` is TARGET-only + cross-register
  (base keeps value in a callee-saved reg; target spills+reloads). Inverse direction.
- **`AnalyzeData`** (bucket 4, `RhythmDetector`) — `clrrwi r5,r9,0` MATCHES both sides and is
  cross-register; nothing to fix.
- **CharIKHand::Highlight / Env_NG / FilterQueue::Poll / DepthBuffer3D::Load / RenderConeDefs**
  (buckets 4/5) — the member-deref-caching theme shows up, but as register-renumbering or
  addi-offset shifts (or as the INVERSE: base caches, target spills), never as the targeted
  same-register clrrwi no-op.

## Why 236/241 had no signature at all

The clrrwi/rlwinm instructions that DO appear across the frontier almost never match the strict
form. Observed non-matching categories (consistent across all 6 buckets):

1. **Matched on both sides** — already emitted identically (e.g. `DoCrucible` 3× `clrrwi r11,r11,0`
   in target AND base; `SetViewport@DxCam`; `MemPrintOverview`; many ObjPtr/Keys iterator-diff
   truncations in `FlowSlider::UpdateActivations`, `ClipPlayer::AnnotateClip`).
2. **Non-zero real masks** — legitimate bit extraction: `clrrwi. rX,rX,3/2/1`, `clrrwi r12,r11,4`
   (stack alignment), `rlwinm. ...,0,30,30` / `,29,29` / `,28,28` (single-bit), `clrlwi imm 24`
   (bool-mask materialization). Not no-ops.
3. **TARGET-only deletes (inverse direction)** — `Trie::store`, `MoveLevel`, `DrawBlacklight`,
   `MemHeap::Init` idx 28, `TryAlloc` idx 77: target has the clrrwi, base lacks it. The cache fix
   can only *remove* a base-side clrrwi, so these are unreachable.
4. **Inside 0-byte stubs / pairing artifacts** — `ObjPtrVec<HamMove>::erase`,
   `ObjPtrList<EventTrigger>::Unlink`, `ObjPtrVec<RndTex>::erase` (target ~276B, base 0): the
   clrrwi is in the target only because the base is an empty stub. Plus ICF/merged-symbol
   "not in target" artifacts (`EaseElasticIn`, `~PackSongListProvider`, `remove@ObjPtrList<RndLight>`).

The dominant **real** divergence classes across the open frontier remain GPR/FPR register-allocation
swaps, FP instruction scheduling + commutative operand order, branch/comparison polarity, stack-frame
deltas, prologue/epilogue style, MakeString template-arg per-TU diffs, and genuine logic/call
differences — none of which is this lever.

## Verdict: NARROW lever

- **Strict-signature prevalence is ~2.1% (5/241), and the actionable rate is ~0.4% (1/241).**
  The single landed win (SyncDeformedDummyCtrlPoints, +1.6 norm, did NOT reach 100) confirms the
  lever is *real* but *rare and small*. It does not generalize into a broad sweep payoff.
- **4 of 5 signature hits were un-exploitable**, and they cluster into three reusable anti-patterns
  that should gate future scans (cheaply, before attempting a fix):
  1. **Inverse direction** — clrrwi is TARGET-only (record-form `add.` fusion, or base avoids the
     zero-ext, or target spills+reloads). Backend lowering floor; not source-fixable. (2 of 4)
  2. **Shared template header** — the ObjPtr deref lives in `ObjPtrVec_impl.h` / `ObjPtrList_impl.h`;
     not safely editable in a unit-disjoint sweep, needs a decomposition-test pass. (1 of 4)
  3. **Buried in a regalloc cascade** — signature present but inside a <40%-match, many-regswap
     whole-body divergence; the isolated cache edit can't land cleanly. (1 of 4)
- **Practical gate for the future:** only pursue the lever when the base-only no-op `clrrwi rX,rX,0`
  is (a) same source/dest register, (b) on the BASE side (insert), (c) in a HIGH-match function
  (>85%) with an otherwise-isolated divergence, and (d) the feeding deref is in a per-unit `.cpp`,
  not a shared template header. Those four conditions held for exactly one function in 241.

The `merge@ObjPtrVec` and `~ObjPtrList` template hits are the only remaining upside: they suggest a
*separate, dedicated* decomposition-test pass on the shared ObjPtr/ObjPtrList template headers could
flip several merged template instantiations at once — but that is explicitly out of scope for a
unit-disjoint sweep and is left as a follow-up.

## Memory update note

`pattern_objptr_deref_zeroext_clrrwi`: measured frontier prevalence ≈ 2.1% strict signature, ≈ 0.4%
actionable. NARROW lever. One win landed (RndSpline::SyncDeformedDummyCtrlPoints 84.4→86.0,
`145a28f5`, did not reach 100). Anti-patterns: inverse-direction (record-form fusion / base-avoids /
target-spills = backend floor), shared-template-header (ObjPtrVec/ObjPtrList `_impl.h`, needs
decomposition-test), regalloc-cascade-buried. Follow-up: dedicated decomposition-test pass on shared
ObjPtr/ObjPtrList template headers (merge@ObjPtrVec hit).
