# 99d — Execution Wave 7 (og-port lane, grind continuation, web verification, funclet pairing)

**Date:** 2026-06-11. **Planner:** Fable (orchestrator). **Predecessors:** Waves 1–6
landed (`92/94/96/98/99/99c-WAVE-*-RESULTS.md`); main at `2c73f652`. State: feet gate
GREEN; bare milo-tests 331/0 green gate; done-with-certs 98.63% fns / 96.07% bytes;
open residual 286 fns / 193,392 bytes.

Wave 7 executes the two never-run roadmap phases (Phase 2 og-ports, Phase 1.2 funclet
pairing), continues the grind, and verifies the whole native fix-train on the web/WASM
target (untested across all six waves).

## Global rules

Rules 1–5 of `91-EXECUTION-WAVE-1.md` + all prior wave additions apply (worktree per
lane: `scripts/setup_worktree.sh /home/free/code/milohax/wt-wave7-<lane> wave7/<lane>`;
ninja warmup + `clean_stale_objects.sh` before measuring; build-plane named; no main
commits / decomp.db writes / git stash / Co-Authored-By; single-owner rule — report
out-of-scope bugs, don't fix them). Do-not-break gates: gameplay boot, the bare
milo-tests green gate (331/0), and the feet gate.

## Lane A — og-dc3 port lane, native-safe half (Opus) — roadmap 2.1/2.3

Source: doc 09 (NOTE: its ~186 number is UNVERIFIED — re-derive your own worklist),
`../og-dc3-decomp/` (same XEX, same compiler; first-class port source). **Binding
gotcha from project memory: verbatim og ports DROP our HX_NATIVE guards** — that broke
web song-load once. Port procedure: diff our file vs og FIRST; graft og bodies; keep
every existing HX_NATIVE block; re-run_objdiff per function.

1. Build the worklist: functions where og has a real body and we have a stub/0%, in
   Xbox-only files first (PlatformMgr_Xbox, NetworkSocket_Win, synth_xbox/Fx*, then
   the 6-unit DSP lane: mkfilter/complex, EnvelopeGenerator, DelayEffect, VorbisMem,
   CompressionEffect, Common_Xbox). Cross-check each against the CURRENT report.json —
   baseline with run_objdiff before porting (worklist artifacts lesson).
2. Port verbatim, measure each (target: og-comparable %, usually 100 or near), keep
   the build green (Xbox AND native — Xbox-only units aren't native-compiled, but
   shared headers can leak; run check_native_compiles.sh).
3. DO NOT port the 95-100 near-miss og cohort (our source has surpassed og — roadmap
   2.5). Acceptance: ≥40 net-new functions ported and measured (state the real
   worklist size you derived); zero native/web regression; zero guard drops.

## Lane B — grind continuation (Opus)

Doc 23's top-20 was worked in wave 6 (5 wins; floors recorded). Continue down the
ranked list with the same method and bar.

1. Re-pull the open set (the `authorable_done` view post-wave-6: 286 fns) ranked by
   bytes; skip certified floors and anything wave-6 lane B dispositioned (see
   99c-WAVE-6-RESULTS + their floor-evidence list). Work the next ~20 candidates.
2. Same method: diagnose → logic first → lowering → permuter last-mile. Same target:
   ≥5 qualifying wins (+10pts or 100%) measured per-fn on your worktree plane —
   the wave-6 verifier caught stale-baseline inflation, so re-baseline each fn
   yourself before claiming a delta.
3. Floors get evidence strings for certify; real behavioral finds get tests (CamShot
   fabsf-class bugs hide here).

## Lane C — web/WASM verification + engine pin (Sonnet)

Six waves of native fixes (json-c, StubTrace, SynthPoll, Rand, SHA1, cascade, vertex
bswap, feet plant) have never been built or run for the web target.

1. Bump the engine pin to current engine HEAD with the new `scripts/bump-engine.sh`
   (engine `8fb669d` — perf changes only; verify with a native build + bare-suite run
   first, since the pin bump affects native too).
2. Build the web port: `scripts/web/build.sh --both` (release + debug). Fix only
   mechanical build breaks (missing guard, LP64/wasm32 size assumption in the new
   code from waves 2–6); anything structural gets reported, not hacked.
3. Boot verification: `python3 native/web/server.py --port 8420` + headless chromium
   (or the project's existing web smoke method — check docs and memory notes: the
   gMainThreadID assert-flood fix and the release/debug deploy layout). Verify the
   page boots past the prior song-load checkpoint; capture console errors. If a real
   browser run is infeasible in this environment, deliver the build green + the exact
   blocker.
4. Acceptance: pin bumped + native suite still green; web release+debug build green;
   boot status reported with evidence.

## Lane D — funclet pairing reconciliation (Sonnet) — roadmap 1.2

~232 unpaired `fn_<addr>` MSVC EH funclets remain in the denominator (doc 05 F5 /
doc 01; objdiff v4.2.0 pairing already matched 1,264+). These are non-authorable;
reconciling them cleans the report tail.

1. Measure the CURRENT unpaired-funclet population from report.json (the ~232 is
   stale). Identify why the byte-signature pairing misses them (objdiff fork
   `funclet` code; the fork is at /home/free/code/milohax/objdiff, read-only unless a
   small fix is warranted — if you change the fork, branch `wave7/funclet-pairing`
   and rebuild objdiff-cli in YOUR worktree only, do not touch the shared
   target/release binary).
2. Classify the misses: pairable-with-a-tweak vs genuinely unpairable (orphaned
   handlers, padding, data-in-text). For pairable: implement + test + measure the
   report delta in your worktree. For unpairable: produce the exclusion list +
   rationale so the denominator can exclude them explicitly.
3. Acceptance: the unpaired population counted and classified with evidence; either a
   working pairing improvement (report delta measured) or the documented exclusion
   list; recommendation for the orchestrator (fork merge + shared-binary rebuild are
   orchestrator steps, not yours).

## Verification + orchestrator follow-up

Same adversarial Sonnet verify + one repair round. Verifiers: re-baseline and re-measure
every claimed match% delta themselves (lane B especially); re-run the green gate; for
lane A spot-check 5 ported functions against og and confirm guard preservation with a
diff; for lane C re-run the web build. Orchestrator afterward: merge `wave7/*`
(merge-tree check), objdiff fork merge + shared binary rebuild if lane D changed it,
sync + unicorn refresh + recert, commit `99e-WAVE-7-RESULTS.md`.
