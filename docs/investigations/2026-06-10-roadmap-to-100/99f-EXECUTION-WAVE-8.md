# 99f — Execution Wave 8 (logic-gap attack, archaeology continuation, og structural ports, permuter sweep)

**Date:** 2026-06-11. **Planner:** Fable (orchestrator). **Predecessors:** Waves 1–7
landed (main `af19921f`). State: feet gate GREEN; suite 331/0; web both-profiles green;
done-with-certs 98.63% fns / 96.07% bytes. The remaining open set decomposes into four
bands (loose filter, post-wave-7):

| Band | Fns | Bytes | Right tool |
|---|---|---|---|
| <70% | 58 | 35,272 | logic-gap archaeology (Lane A) |
| 70–90% | 136 | 84,492 | asm-archaeology (Lane B) |
| 90–100% | 179 | 118,580 | permuter sweep (Lane D) |
| zero-start | 30 | 1,164 | fold into Lane A |

## Global rules

Rules 1–5 of `91-EXECUTION-WAVE-1.md` + all prior additions (worktree per lane
`wave8/<lane>`; ninja warmup + `clean_stale_objects.sh`; build-plane named; re-baseline
before claiming deltas; single-owner; no main commits / decomp.db writes / git stash /
Co-Authored-By). Do-not-break gates: gameplay boot, bare milo-tests 331/0, feet gate,
web build (if you touch shared headers, run `scripts/web/build.sh --release` too).

## Lane A — the <70 band: logic-gap attack (Opus)

The 58 fns under 70% (35 KB) + the 30 tiny zero-starts (1.1 KB) are the largest
per-function gaps left — real missing/wrong logic, not lowering noise.

1. Query the band from decomp.db (read-only; excluded=0, is_stub=0, no floor cert,
   norm<70, authorable units, not merged_/lbl_/fn_), rank by size DESC, skip anything
   with a floor-class diagnose (synth_xbox SIMD floors live here — check doc 06 F6/08
   §8 unit list first). Work top-down with full archaeology: Ghidra+m2c synthesis,
   RB3/og references (lookup_rb3, ../og-dc3-decomp), struct verification.
2. The 30 zero-starts (avg ~39 bytes) are quick kills — write them all.
3. Target: ≥10 functions materially improved (+15pts or 100%) including ≥20 of the 30
   zero-starts written; per-fn before/after on your worktree plane.

## Lane B — the 70–90 band: archaeology continuation (Opus)

Same method and bar as waves 6/7 lane B (read their floor lists in 99c/99e results so
you don't re-attempt evidenced floors). 136 fns / 84 KB.

1. Rank by size DESC, skip evidenced floors, work ~25 candidates.
2. Target: ≥6 qualifying wins (+10pts or 100%); floors get evidence strings; real
   behavioral finds get tests.

## Lane C — og Phase 2.2 structural/hybrid ports (Opus)

The cross-platform half wave-7 lane A identified as header-divergence-blocked: Mic,
VoiceControlPanel, ShellInput, char/* (and any others their report lists — read
99e-WAVE-7-RESULTS Lane A). These need the **hybrid procedure** (memory-proven on
VorbisReader): diff our file vs og FIRST; reconcile header divergence deliberately
(our headers may be more correct — check DWARF/asserts before adopting og's); graft og
bodies under the Xbox path; keep every HX_NATIVE block; re-run_objdiff per fn.

1. Build the worklist from og coverage (functions where og ≥95% and we are <50% or
   stub, in the named files). Baseline each with run_objdiff before porting.
2. Port carefully; any header change must be re-verified across ALL units that include
   it (touched-unit sweep with measure_progress or batch run_objdiff on the unit list).
3. Target: ≥15 net-new/recovered functions measured (state your real worklist size);
   zero guard drops; zero regressions on header-sharing units; native + web compile
   green.

## Lane D — permuter sweep over the 90–100 band (Sonnet)

179 fns / 118 KB of near-miss. The permuter infrastructure exists
(`../decomp-synth`, pattern_sweep.py, the permute skill); memory has the safe-sweep
procedure (worktree-only, ninja prime first or the whole sweep invalidates,
incremental harvest, ~45% of "wins" are unsafe — curate).

1. In YOUR worktree (after ninja prime + clean_stale_objects — non-negotiable), run
   the permuter/pattern sweep over the band's symbol list (build it from decomp.db
   read-only). Default + proven opt-in levers (fpr reorder, int_abs_to_ternary,
   comparison_operator_fix, unnamed-temp) — no experimental levers.
2. Harvest + curate with the established rubric (binary = ground truth; reject
   behavior-changing "wins"; re-run_objdiff each candidate). Apply curated wins as
   source edits on your branch, each measured.
3. Target: every sweep win that survives curation lands as a commit with before/after;
   report the sweep stats (attempted/improved/curated-out). Floors the sweep exhausts
   get permuter_exhausted evidence strings.

## Verification + orchestrator follow-up

Same adversarial Sonnet verify + one repair round. Verifiers re-baseline and re-measure
every claimed delta with run_objdiff in the lane's worktree; for lane C additionally
verify guard preservation and run the touched-unit sweep; for lane D re-verify a sample
of 5 curated wins AND 2 rejected ones (confirm the rejection was right). Orchestrator
afterward: merge `wave8/*` (merge-tree check — lanes A/B/C may touch neighboring units;
single-owner is per-function here, check overlaps), sync + unicorn refresh + recert,
commit `99g-WAVE-8-RESULTS.md`.
