# 95 — Execution Wave 3 (gameplay unblock + feet residual, unicorn refresh, live-bug burndown, scoring reconciliation)

**Date:** 2026-06-10. **Planner:** Fable (orchestrator). **Predecessors:** Wave 1
(`92-WAVE-1-RESULTS.md`) and Wave 2 (`94-WAVE-2-RESULTS.md`) — both landed; main is at
`85d2aa78`. Current canonical state: native boots headless to `main_screen`; milo-tests
true baseline **372/386**; `authorable_done` = 97.32% fns / 94.15% bytes with floor
certs; genuine open residual **558 fns / 287,908 bytes**.

Wave 3 executes the 94-doc follow-ups: get native past `main_screen` into gameplay (and
finally test the feet bug), refresh the unicorn evidence behind 843 stale certs, start
the native live-bug burndown with regression tests, and reconcile the
run_objdiff-vs-report scoring discrepancy that refuted the "9 promotable" claim.

## Global rules

Rules 1–5 of `91-EXECUTION-WAVE-1.md` apply verbatim (own worktree via
`scripts/setup_worktree.sh /home/free/code/milohax/wt-wave3-<lane> wave3/<lane>`; no main
commits; no `decomp.db` writes — dry-run + `--apply` runbooks; no `git stash`; no
Co-Authored-By; report contradictions). Wave-2 additions hold (RelWithDebInfo +
`-DDawn_DIR=/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn`; milo-tests cwd
= `orig-assets/`; sandbox-skip only for actual boots). New rule from a Wave-2 lesson
(doc 16 correction):

- **Match-percent claims must name their build plane.** A `run_objdiff` reading taken in
  a worktree is NOT evidence about main (`parsedate` read 100.0% in a Wave-2 worktree
  but is 99.8% on main). When your claim is about main/canonical state, measure with
  `project_dir` = the main repo, or via report.json, and say which you used.

## Lane A — gameplay unblock + feet-bug residual (Opus) — 94 follow-ups #1/#2

1. **Fix `Sound::SynthPoll` double-free** (`src/system/synth/Sound.cpp:174` —
   `cur=*it; it++; erase(it)` erases the NEXT element / can `erase(end())`). Pin
   original Xbox behavior first (run_objdiff/Ghidra: is the iterator dance a decomp
   transcription bug, or faithful-but-UB-on-host?), unit test before fix, fix at root
   (HX_NATIVE guard only if the Xbox semantics genuinely differ). No PPC regression.
2. **Advance the boot as far as it goes**, fixing crashes the same way (root-cause, test,
   guard-only-if-faithful), until you can load a song / reach gameplay via the HTTP API
   (`docs/tools/HTTP_DEBUG_SERVER.md`; `scripts/dc3-agent-test.sh` env). Capture the
   gameplay-path `/api/stubs` ranked worklist and append it to
   `15-native-stub-worklist.md` (boot-only table stays, add the gameplay table).
3. **Run `FeetNotBelowFloorDuringGameplay`.** If it fails, attack the narrowed residual:
   the gameplay song-move/poll-order path (`HamDriver.cpp:95-101`, see
   `18-feet-ik-lane-c.md`). CLOSED leads you must NOT re-litigate: DoFSM int-vs-float
   (refuted with asm), mConstraints population (faithful-empty). Deliver either a green
   gate or ankle/toe telemetry before/after with the residual cause narrowed further.

Acceptance: SynthPoll fixed with test + PPC-neutral; boot reaches strictly further than
`main_screen` (state the new frontier); gameplay stub table captured OR the precise
blocker documented; feet gate run with results either way.

## Lane B — unicorn evidence refresh (Opus) — 94 follow-up #4, roadmap 0.8

843 of 970 floor certs rest on ~98-day-old unicorn data; 344 frontier fns have no
evidence at all. Refresh the behavioral plane.

1. **Locate the unicorn test runner** (the `unicorn_*` columns in decomp.db were filled
   by something — find it: `scripts/`, `../decomp-synth/`, the unicorn-query skill docs).
   Report what you find before building anything new.
2. **Re-run unicorn over the authorable partial frontier** (the 1,314 from
   `certify_floor.py`, or at minimum the 843 stale + 344 no-evidence), with a
   **source-hash freshness gate** stored per row (so staleness is detectable next time,
   not just dated). Write results to a NEW results table or a dry-run update file — do
   NOT write decomp.db; deliver the `--apply` runbook.
3. **Measure the deltas that matter:** how many stale-EQUIVALENT stay EQUIVALENT; how
   many flip (a flip = a real behavior divergence hiding under a cert — list each); how
   many of the 344 no-evidence become certifiable; expected new
   `certify_floor.py --apply` census after your data lands.

Acceptance: runner identified (or a justified rebuild); fresh unicorn verdicts for the
frontier with source-hash provenance; the flip-list (most important deliverable — each
flip is a candidate real bug); apply runbook; zero live-DB writes.

## Lane C — native live-bug burndown with tests (Opus) — roadmap N.4/N.7

The 53 zero-guard live-bug set (43 KB; doc 11, `90-ROADMAP.md` N.4). Start with the
named confirmed/likely entries and work down the list as budget allows:
`DecodeDxt5Alpha` (DXT5 alpha branch polarity, CONFIRMED), `ClipCollide::Collide`,
`CharClipDisplay::SetStartEnd`, `RndLight::Load`.

For each function: (1) diagnose the divergence vs Xbox asm (run_objdiff + diff_inspect);
(2) write a milo-tests unit test pinning ORIGINAL behavior (test first, failing on
current code if the bug is behavioral); (3) fix in shared source so the PPC match
IMPROVES or stays equal (these are real decomp bugs — fixing them should raise match%,
that is the point); (4) record before/after match% measured on YOUR worktree AND note
that final certification happens on main post-merge. Skip a function (with the reason)
if it turns out to be a false positive — doc 11 warned DIVERGENT-at-100% are 70% false
positives, but these 53 are all sub-100.

Acceptance: ≥4 functions fixed-with-tests (more is better), each with asm-grounded
diagnosis, a behavior-pinning test, and non-regressed-or-improved match%; updated
status notes for any false positives found.

## Lane D — scoring-plane reconciliation (Sonnet) — doc 16 correction follow-up

Small and surgical. The Wave-2 single-blocker lane measured 9/20 blockers at 100%
normalized in its worktree; on main they are <100 (parsedate: 99.8%, `subi 0x50` vs
`0x4c, weekday` data-addend) and `--promote` fired 0.

1. **Re-measure all 20 blockers from `16-single-blocker-recert.md` against MAIN**
   (`run_objdiff` with `project_dir=/home/free/code/milohax/dc3-decomp`, sequential —
   do not parallelize builds in the main repo). Produce the corrected table.
2. **Explain the worktree-vs-main divergence mechanism** for at least `parsedate`:
   same source, different normalized score — is it worktree symlinked target objs,
   PCH/header skew, build nondeterminism, or scoring-config differences between the
   report pipeline and run_objdiff? Reproduce it once in a throwaway worktree and
   pin which input differs (compare the two .obj files directly).
3. **Write the verdict into doc 16** (replace the table or append a corrected one) and,
   if the mechanism implies other worktree-measured numbers in waves 1–2 are suspect,
   say exactly which docs/claims need re-measurement.

Acceptance: corrected 20-row table measured on main; the mechanism named with evidence
(obj-level diff), not speculated; blast-radius statement for prior worktree-measured
claims.

## Verification + orchestrator follow-up

Same adversarial Sonnet verify + one repair round. Verifiers: re-measure decomp claims
on the plane the claim names; for Lane C re-run the new milo-tests and run_objdiff
yourself. Orchestrator afterward: merge `wave3/*` (A first), run Lane B's unicorn apply +
`certify_floor.py --apply` re-cert, re-run reconcile, commit `96-WAVE-3-RESULTS.md`.
