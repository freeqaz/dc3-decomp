# 99b — Execution Wave 6 (knee-bend mechanism, residual grind, suite-green, view definition)

**Date:** 2026-06-11. **Planner:** Fable (orchestrator). **Predecessors:** Waves 1–5
landed (`92/94/96/98/99-WAVE-*-RESULTS.md`); main at the wave-5 landing (`095fe01f`).
State: gameplay boots; 45-test regression set green; `authorable_done` 97.80% fns /
95.66% bytes; open residual 459 fns / 213,648 bytes (census: doc 23); flip-list fully
adjudicated for priority classes (0 further real bugs); engine pin at `f75339a`.

## Global rules

Rules 1–5 of `91-EXECUTION-WAVE-1.md` + all prior wave additions apply (worktree per
lane: `scripts/setup_worktree.sh /home/free/code/milohax/wt-wave6-<lane> wave6/<lane>`;
ninja warmup + `clean_stale_objects.sh` before measuring; build-plane named; no main
commits / decomp.db writes / git stash / Co-Authored-By; RelWithDebInfo + Dawn_DIR;
milo-tests cwd `orig-assets/`; sandbox-skip only for boots). Do-not-break gates: the
gameplay boot and the 45-test regression set. A new lesson from wave 5: if two lanes
might plausibly fix the SAME bug, the lane spec assigns it to exactly one — do not fix
bugs outside your lane's scope even if you see them (report them instead).

## Lane A — the knee-bend mechanism (Opus) — the feet endgame

Wave-5 ground truth (`docs/sessions/2026-06-09-xenia-xbox-foot-truth.md`, 98/99 results
Lane A): at the same pelvis height (35.2), the Xbox knee bends to **−58°** and plants
the toe at ~0.01; native pure-anim knee is −20° and the toe is at −3.8. The leg
foot-plant IK chain is INERT on native: `.ikfoot` serialized data has
`mMoveElbow=false` → `shoulderParent=0` → IKElbow never bends the knee, AND the knee
dep was dropped from `PollDeps`. Forcing `mMoveElbow=true` DIVERGES wildly (+21.3) —
that is NOT the mechanism. Poll-order and margin levers are REFUTED — do not revisit.

1. **Answer the central question: what bends the Xbox knee to −58° when
   `mMoveElbow=false`?** Candidates to check in the Xbox asm (use
   run_analyze_function / Ghidra on CharIKFoot::DoFSM's callees, CharIKLeg-class code,
   and whatever consumes the `.ikfoot` chain): a separate knee/leg solver beyond
   IKElbow; CharServoBone regulation on the knee; a two-bone analytic IK inside
   CharIKFoot's plant phase; or pose-space correction in the clip layer that native
   skips. The Xenia live-debug rig (memory: xenia GDB works) can read the Xbox solver's
   intermediate values if static asm is ambiguous.
2. Implement the mechanism natively (engine-side if it lives in shared solving code —
   coordinate the same way wave-5 lane C did with a milo-native-engine branch), gated
   so default behavior changes only when correct. PPC neutrality on shared edits.
3. Deliverable: `FeetNotBelowFloorDuringGameplay` GREEN (worst toe within noise of 0),
   or the mechanism definitively named with asm/Xenia evidence + measured improvement.
   Boot + 45 tests stay green.

## Lane B — open-residual asm-archaeology grind (Opus) — the decomp push

Doc 23 (`23-open-residual-census.md`) ranks the 459 open fns; the top-20 by bytes are
the worklist. The asm-archaeology playbook (memory + docs/sessions/2026-06-09 playbook)
took 60%-class functions to 85-100% repeatedly.

1. Work the top-20 list top-down, skipping rows the census marks as floor-class
   (cap_exhausted* with diff_op:none). For each: run_diff_inspect diagnose →
   asm-archaeology (logic first, lowering second) → permuter for the last cosmetic
   gap. Use the patterns catalog; banked levers (fpr reorder, int-abs-ternary,
   unnamed-temp ctor-return, MakeString per-site) are all in memory/patterns.
2. Target: **≥8 functions materially improved (+10pts or to 100%)**, measured on your
   worktree plane with before/after; zero PPC regressions elsewhere (the touched-unit
   sweep run_objdiff check).
3. Any function that turns out to be a floor: record the evidence string for
   certify_floor (artifact class + diagnose output) in your report — do not write the
   DB.

## Lane C — suite to fully green (Opus)

The suite is the native port's trust anchor; make it green and honest.

1. **`CompressedSkinningMatchesCpuSkinningForSyntheticBones`** — fails on main in
   isolation (pre-existing). Wave-5's engine bswap fixed vertex POSITIONS; skinning
   weights/indices may have the same BE-truncation class bug in the engine unpack
   path, or the test's synthetic data predates the fix. Root-cause and fix
   (engine-side branch if needed).
2. **The AssetLoading 400s hang and the flaky death test** (doc 21 / wave-5 lane D
   filtered them out): root-cause the hang (wave-5 cascade fix may have already
   resolved it — verify first), and make the death test deterministic or properly
   conditional.
3. **Audit the 85 SKIPPED tests**: classify (GPU-required / asset-required /
   platform-gated / stale-disabled), unskip what can run in this environment, and
   produce the honest "expected green census" for CI (which filter, which count).
   Deliverable: a one-command suite invocation that is fully green and a doc section
   defining it as the gate.

## Lane D — done-view definition + small tooling (Sonnet)

1. **The 170-fn db-only slice** (COMPLETE + unicorn-EQUIVALENT + current=100 +
   normalized NULL — absent from report.json, part of the 1,469 db-only authorable
   symbols): determine WHY they're absent (jeff boundary churn? renamed symbols? dead
   rows?) by sampling 10 across units and tracing each against jeff's current split
   output. Then either (a) the view counts verdict=COMPLETE+current=100 rows with
   normalized NULL as done (if they're real but unmeasurable), or (b) they're stale
   ledger rows that reconcile should flag for deletion. Implement the chosen rule in
   `certify_floor.py`'s view + a reconcile note; dry-run + apply runbook.
2. **Add `scripts/bump-engine.sh`** (CLAUDE.md references it but it does not exist):
   reads engine HEAD, updates `MILO_ENGINE_PIN` in native/CMakeLists.txt, prints the
   old→new SHAs and a reminder to build. Trivial, test it with --dry-run.
3. **Doc hygiene**: `00-INDEX.md` is stale (pre-wave). Add a "Waves 1–6 execution"
   section to the index listing plan/results pairs and the current headline numbers
   (97.80%/95.66%, 459 open, gameplay boots), so the folder's entry point reflects
   reality.

## Verification + orchestrator follow-up

Same adversarial Sonnet verify + one repair round. Verifiers: re-run suites from
orig-assets; re-measure match% on the named plane; for lane B re-run run_objdiff on
every claimed improvement; for lane A re-run the gate. Orchestrator afterward: merge
`wave6/*` (single-owner rule should prevent wave-5-style competing fixes; still check
merge-tree), engine pin bump if lane A/C touched the engine, sync + unicorn refresh +
recert, commit `99c-WAVE-6-RESULTS.md`.
