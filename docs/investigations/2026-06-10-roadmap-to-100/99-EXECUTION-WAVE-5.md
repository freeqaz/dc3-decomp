# 99 — Execution Wave 5 (feet over-extension, NullifyAllRefs cascade, flip-list continuation, milo-tests burndown)

**Date:** 2026-06-11. **Planner:** Fable (orchestrator). **Predecessors:** Waves 1–4
landed (see `92/94/96/98-WAVE-*-RESULTS.md`); main at the wave-4 landing. State:
native reaches gameplay; 30 regression tests green; `authorable_done` 97.80% fns /
95.66% bytes; open residual 459 fns / 213,648 bytes; 47 flip-list rows unadjudicated.

Orchestrator corrections since 98 (trust these over older docs):
- **The Rand story is now complete:** with the wave-4 `Seed` fix, table words always
  have bit 31 clear and `Int()` can NEVER go negative — the wave-2 `Int(low,high)`
  guard is defense-in-depth, not load-bearing. The wave-2 test asserting raw-modulo
  leaves range was updated to pin the new invariant
  (`RandIntSignedModulo.FixedSeedDrawsAreNeverNegative`). Do not re-add the old
  expectation.
- All 30 wave-2/3/4 regression tests pass on merged main; smoke gate green; DB
  reconcile fully green (certs re-applied post-wave-4).

## Global rules

Rules 1–5 of `91-EXECUTION-WAVE-1.md` + all wave-2/3/4 additions apply (worktree per
lane via `scripts/setup_worktree.sh /home/free/code/milohax/wt-wave5-<lane>
wave5/<lane>`; ninja warmup + `scripts/clean_stale_objects.sh` before measuring; no
main commits / decomp.db writes / git stash / Co-Authored-By; build-plane named on
every match%; RelWithDebInfo + Dawn_DIR; milo-tests cwd `orig-assets/`; sandbox-skip
only for boots). Do-not-break gates: the gameplay boot (game_screen, EXIT=0) and the
30-test regression suite.

## Lane A — feet/IK leg over-extension root cause (Opus)

Wave-4 lane A pinned the divergence precisely (read `98-WAVE-4-RESULTS.md` Lane A
first): the neutral skeleton AGREES with Xbox (z −0.04 vs +0.017) but the **live
toe-target sinks** (eff/finger.z native −3.71 vs Xbox +0.88); rendered geometry shows
the ankle at floor with the foot pointing straight down and the **leg over-extending**
(pelvis 42.5→32.3, ankle 4.39→0.26); poll order is CONFIRMED IK-before-pose so the IK
write is discarded. An off-by-default clean-plant experiment cut below-floor samples
~84–90% but is nondeterministic (LP64 poll order) and spikes the right leg.

1. Root-cause the **pelvis/leg over-extension**: why does the native pelvis drop
   42.5→32.3 during gameplay when Xbox holds it? That delta alone explains a sunk
   toe with a planted ankle (the chain folds). Candidate areas: world-transform /
   character placement during song playback, IK chain length/scale, the discarded
   IK-write ordering. Use the wave-4 telemetry hooks (FootGeom, eff.z capture) to
   bisect — the divergence point is now instrumentable.
2. Decide the architecturally correct fix for the IK-before-pose discard (poll-order
   is deterministic on Xbox; native LP64 ordering diverges — find WHERE the order is
   established (Poll registration? dirty sort keys?) and pin it deterministically
   rather than papering with clean-plant). Root-fix policy applies (no per-callsite
   hacks); the clean-plant experiment may be superseded/removed if your fix lands.
3. Deliverable: gate green (toe never below floor beyond noise), or worst-toe-Z
   before/after + the over-extension mechanism named with evidence. PPC neutrality on
   shared edits; boot + 30 tests must stay green.

## Lane B — ~ObjectDir NullifyAllRefs cascade (Opus)

4 of the 8 pre-existing milo-tests failures are one bug (doc `21-milo-tests-triage.md`):
the `~ObjectDir` NullifyAllRefs cascade kills reparented objects. Two pre-existing
failing unit tests define target behavior (find them via the triage doc / memory:
ObjectLifetime MergeDirs/MergeScope; also the 400s AssetLoading timeout is this cascade
hanging).

1. Reproduce all 4 failures; read the destructor/NullifyAllRefs path and the Xbox asm
   for `~ObjectDir` (this is shared Milo engine — `lookup_rb3` may show how RB3 solved
   it). Determine the ORIGINAL semantics: what does the Xbox destructor actually do to
   refs owned by a different (reparented) dir?
2. Fix at root in shared source (PPC match must not regress — measure the touched
   dtor/helpers before/after) or, if the divergence is host-lifetime-specific, under
   HX_NATIVE with the mechanism documented.
3. Acceptance: the 2 defining unit tests + the 4 triaged failures pass (target: 6
   fewer failures), no regression in the other 26 suite tests, PPC neutrality shown.

## Lane C — flip-list continuation + vertex-unpack bswap (Opus)

1. **Adjudicate the remaining ~47 flip-list rows** (`data/unicorn_refresh_main_d5491b67.json`
   minus the 10 wave-4 dispositions in `22-fliplist-adjudication.md`). Priority: the
   cap_exhausted_decomp class (22 rows) then cap_exhausted_orig (10). Same bar as
   wave 4: asm-grounded real/false verdict per row; fixes with tests for real bugs
   (PPC match should improve or hold; HX_NATIVE only when host-specific).
2. **Fix the MeshVertexLoading failures**: doc 21 pinned a missing byteswap in the
   native vertex unpack path (big-endian source data read on LE host). Root-fix in the
   loader with a regression test using a real asset vertex buffer; this likely also
   affects rendering correctness beyond the test.
3. Acceptance: ≥15 more rows adjudicated, every real one fixed-with-tests or
   explicitly deferred with reason; MeshVertexLoading green; suite otherwise stable.

## Lane D — suite burndown remainder + open-residual probe (Sonnet)

1. **YRatio/GPU-init failure** (RndCamProjection) and the **flatten-pass over-flatten**
   failure (doc 21): investigate each to a named mechanism; fix if the fix is
   mechanical and risk-free (test-first); otherwise deliver the routed diagnosis
   (which file, which wave).
2. **Open-residual census probe**: from the `authorable_done` view, list the 459 open
   fns grouped by unit and diff-class (read-only SQL + report.json), and rank the top
   20 by bytes with a one-line routability call each (use verdict/diagnose data, do
   not re-diagnose all 459). Write `23-open-residual-census.md` — this becomes Wave
   6's grind worklist.
3. Acceptance: both failures mechanism-named (fixed if mechanical); doc 23 exists with
   the ranked top-20 and class histogram.

## Verification + orchestrator follow-up

Same adversarial Sonnet verify + one repair round. Verifiers re-run tests from
orig-assets, re-measure on the named plane, and for lane A confirm boot + gate
telemetry themselves. Orchestrator afterward: merge `wave5/*` (union any CMakeLists
test-registration collisions), sync + unicorn refresh + recert on main, commit
`99-WAVE-5-RESULTS.md`.
