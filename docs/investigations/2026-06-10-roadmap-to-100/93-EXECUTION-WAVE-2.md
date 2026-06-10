# 93 — Execution Wave 2 (native unblock + floor certification + feet bug + measurement follow-through)

**Date:** 2026-06-10. **Planner:** Fable (orchestrator). **Predecessor:** Wave 1 landed
through `aa2e788b` — see `92-WAVE-1-RESULTS.md` (all four lanes merged; DB runbook
applied; reconcile green; canonical headline = authorable normalized fns 90.73% /
bytes 78.75% in `docs/PROGRESS_METRICS.md`).

Wave 2 executes the follow-ups Wave 1 queued plus the next roadmap tier: unblock the
native headless boot (which unlocks the live ranked stub worklist), make "done"
auditable via floor certificates, fix the highest-value native gameplay bug (feet/IK),
and wire the measurement guards so drift can't silently return.

## Global rules

Rules 1–5 of `91-EXECUTION-WAVE-1.md` apply verbatim (own worktree per lane via
`scripts/setup_worktree.sh /home/free/code/milohax/wt-wave2-<lane> wave2/<lane>`; no
main commits; no decomp.db writes — DB tools dry-run by default with `--apply` runbooks
for the orchestrator; no `git stash`; no Co-Authored-By; report contradictions instead
of silently improvising). Additional wave-2 notes:

- Native builds: configure with `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (debug mode trips
  GCC-16 hardened libstdc++ bounds asserts on pre-existing bugs — see 92 §Lane D).
  `wgpu-window-test` is a known-broken stale target (its gfx headers moved to the
  engine repo) — build `dc3-native milo-tests` targets, don't chase it.
- milo-tests must run with cwd = `orig-assets/`. The "371/371" baseline is stale; the
  real env baseline is ≈263 pass (main June-9 binary). Treat *deltas you introduce* as
  the gate, not the absolute count, until Lane A re-baselines.
- GPU access requires running outside the sandbox (`dangerouslyDisableSandbox`) — only
  needed for an actual boot, never for builds or unit tests.

## Lane A — native boot unblock + live stub worklist (Opus) — wave-1 follow-ups #2/#3/#7

Source: `92-WAVE-1-RESULTS.md` follow-ups, roadmap N.2 payoff. The two pre-existing
crashes block headless boot and therefore the entire `/api/stubs` payoff.

1. **Fix `CameraManager::RandomizeCategory`** (vector OOB during App construction — it
   crashes dc3-native before the HTTP server binds) and **`CharBones::ScaleDown`**
   (`&mBones[mCounts[TYPE_END]]` one-past-end form that hardened libstdc++ rejects).
   Policy (binding, from project memory): root-cause fixes, NOT per-callsite hacks.
   First pin original behavior: check the Xbox asm via `mcp__orchestrator__run_objdiff`
   / Ghidra for each function — is the OOB a decomp transcription bug (fix in shared
   code, must not regress the PPC match %) or original-game behavior that happens to be
   UB on host (fix under HX_NATIVE guard)? Write a unit test pinning correct behavior
   BEFORE the fix. Report the run_objdiff percent before/after for any shared-code edit.
2. **Re-baseline milo-tests**: with RelWithDebInfo + asset cwd, record the true
   pass/fail/crash census; fix the ctest harness so tests get an asset-aware working
   directory (e.g. `WORKING_DIRECTORY` in `gtest_discover_tests`/ctest config); write the
   census + per-failure one-liner table into your report (this becomes the new gate).
3. **Produce the live ranked stub worklist** (the N.2 intent): after the boot unblock,
   `DC3_STUB_TRACE=1` boot via `scripts/dc3-agent-test.sh` (sandbox-skip OK), let it
   reach the boot flow / attract screen, then `curl localhost:9090/api/stubs`. Commit the
   ranked result as `docs/investigations/2026-06-10-roadmap-to-100/15-native-stub-worklist.md`
   with hit counts and a recommended fix order. If gameplay-session capture is feasible
   (load a song via the HTTP API), capture that too — gameplay-path stubs rank highest.

Acceptance: both crashes fixed at root with tests; dc3-native boots headless far enough
to serve `/api/stubs`; doc 15 exists with real hit counts; any shared-code edit shows
non-regressed run_objdiff percent.

## Lane B — floor certificates + canonical done view (Opus) — roadmap 3.1, Tier-2 #6/#9

Source docs: 08 (floor-vs-routable), 90-ROADMAP Tier 2. Makes "done with only cosmetic
mismatches" queryable instead of vibes.

1. **Schema migration** (idempotent, in a new `scripts/certify_floor.py` or shared
   migration helper): add `floor_certificate` TEXT (enum: `equivalent` |
   `artifact:<class>` | `permuter_exhausted` | `pgo_block_sink` | `icf_merged`),
   `floor_cert_pct` REAL, `floor_cert_build` TEXT, `floor_cert_at` TEXT.
2. **`scripts/certify_floor.py`** (dry-run default, `--apply` for orchestrator): certify
   a function iff normalized<100 AND evidence holds: unicorn EQUIVALENT (but RECORD the
   unicorn run's staleness — audit doc 04 F6 says unicorn data is ~3 months stale; a
   cert from stale unicorn must store that provenance so it can be invalidated), OR
   artifact class (block-sinking/ICF/known-floor pattern flags), OR permuter-exhausted
   (attempts-table evidence). Store cert pct + build so any later percent change
   invalidates the cert (reconcile_db.py check — coordinate, don't edit it heavily;
   a small additional check function is fine, Lane D won't touch reconcile_db.py).
3. **Canonical done view**: a SQL view `authorable_done` (authorable per
   `scripts/authorable.py` prefixes, NOT merged_/lbl_/fn_/??_ artifacts, and
   (normalized==100 OR is_stub=1 OR floor_certificate IS NOT NULL)). Expose a
   `--summary` mode printing done/total authorable fns + bytes with and without certs.
4. **Measure**: dry-run over the current frontier — how many of the ~1,699 partial fns
   are certifiable TODAY from existing evidence, how many are blocked on stale unicorn,
   how many have no evidence at all. These three numbers are the lane's headline.

Acceptance: migration + certify + view all dry-run clean on a DB copy; the three
headline numbers measured; apply runbook written; zero live-DB writes.

## Lane C — the feet/IK bug (Opus) — roadmap N.3

Source: doc 12 F6, doc 11 F7, memory `project_ik_dirty_cascade`, session doc
`docs/sessions/2026-06-03-ik-ground-truth-comparison.md`. Failing test:
`FeetNotBelowFloorDuringGameplay`. Prior session refuted the dirty-cascade theories;
real cause is IK VALUE divergence (native ankle drops 4.39→1.0, toe sinks below floor).

1. **Verify the doc-12 claim live**: CharIKFoot::DoFSM allegedly reads an int where the
   field at offset 0x30/0x34 is float (or vice versa). Check the DWARF/Ghidra struct
   (`mcp__orchestrator__lookup_struct_offset`, ghidra skills), the Xbox asm
   (lwz vs lfs at those offsets), and our header. Report the evidence either way —
   this claim is UNVERIFIED and may be wrong like doc 11's MemAlloc rationale was.
2. **Trace `HamIKEffector::mConstraints` population**: find where the original game
   fills it (Xbox asm xrefs), why native never does (load-order? skipped Poll? guard?),
   and fix at root.
3. Iterate against the failing test until `FeetNotBelowFloorDuringGameplay` passes, or
   if it cannot pass yet, document precisely what improved (ankle height telemetry
   before/after) and what residual divergence remains. Any shared-code edit must show
   non-regressed run_objdiff percent on the touched functions.

Acceptance: doc-12 field claim confirmed-or-refuted with asm evidence; mConstraints
wiring explained with evidence; the failing test passes OR a measured-improvement report
with the residual cause narrowed. No PPC match regressions.

## Lane D — measurement follow-through (Sonnet) — wave-1 follow-up #6, roadmap 1.3, Tier-4

Mechanical, well-scoped. Do these three items exactly; don't expand scope.

1. **Nightly measurement guard**: `scripts/nightly_measurement_guard.sh` that runs
   (a) `python3 scripts/reconcile_db.py` (nonzero exit on drift), (b) optionally
   `--strict` mode: regenerate `report_strict.json` with the objdiff fork's NameOnly
   mode + run `scripts/analysis/reloc_strict_classify.py --jobs 30` and alert if
   `genuine_wrong_target` grows vs a checked-in baseline count file. Document in the
   script header how to wire it to cron/ninja-postbuild; do NOT install a crontab.
2. **Single-blocker unit recert** (roadmap 1.3): from the current report.json, list
   units where exactly one function is <100 normalized AND that function is ≥99.5
   normalized (the "rounding-100" cohort, doc 06 F7 said ~71). For each, run
   `mcp__orchestrator__run_objdiff` on the blocker to get fresh normalized; output a
   table (unit, fn, normalized, verdict-recommendation). READ-ONLY — no DB writes, the
   output is a worklist doc `docs/investigations/2026-06-10-roadmap-to-100/16-single-blocker-recert.md`.
3. **Native compile smoke**: `scripts/check_native_compiles.sh` — configure (if needed)
   + build `dc3-native milo-tests` targets RelWithDebInfo, exit nonzero on failure, <10s
   incremental when clean. Header comment: intended as a pre-merge gate so PPC-only
   commits can't silently break native (the Mesh/AmbientOcclusion failure mode from
   wave 1). Note `wgpu-window-test` is excluded and why.

Acceptance: guard script runs green end-to-end now; doc 16 exists with the real cohort
(report the actual count vs the audit's 71); smoke script proven on the current tree.

## Verification + orchestrator follow-up

Same adversarial Sonnet verify per lane + one repair round as Wave 1. Orchestrator
afterward: merge `wave2/*`, run Lane B's apply runbook (single writer), re-run
reconcile, commit `94-WAVE-2-RESULTS.md`.
