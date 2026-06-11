# 97 — Execution Wave 4 (feet/IK live-gate attack, flip-list adjudication, near-miss cohort, suite hygiene)

**Date:** 2026-06-11. **Planner:** Fable (orchestrator). **Predecessors:** Waves 1–3
landed (see `92/94/96-WAVE-*-RESULTS.md`); main at the wave-3 landing. Current state:
native boots to **game_screen (playing)**; milo-tests baseline 372/386 + 22 new
regression tests; `authorable_done` = **97.80% fns / 95.66% bytes** on FRESH unicorn
evidence (1,069 certs, only 9 stale); genuine open residual **459 fns / 213,648 bytes**.

Orchestrator corrections since 96 was written (read before trusting older docs):
- The unicorn refresh was re-run **on the main plane** post-merge (post `.c`-stale
  rebuild): flip-list = **57 candidate bugs** (not the worktree 60), committed at
  `data/unicorn_refresh_main_d5491b67.json`. Use THIS file, not the worktree numbers.
- **`parsedate` needed no source fix** — its 99.8% was main-plane stale-obj skew; after
  `clean_stale_objects.sh` learned `.c` files and 115 stale objs were rebuilt, it scores
  100/100 COMPLETE. Lesson: any sub-100 reading on a `.c` unit measured before
  2026-06-11 is suspect; re-measure before "fixing".

## Global rules

Rules 1–5 of `91-EXECUTION-WAVE-1.md` + the wave-2/3 additions apply verbatim (worktree
per lane via `scripts/setup_worktree.sh /home/free/code/milohax/wt-wave4-<lane>
wave4/<lane>`; no main commits; no decomp.db writes; no git stash; no Co-Authored-By;
build-plane named on every match% claim; native builds RelWithDebInfo +
`-DDawn_DIR=/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn`; milo-tests cwd
= `orig-assets/`; sandbox-skip only for boots). New:

- After your worktree's ninja warmup, run `scripts/clean_stale_objects.sh` (now handles
  `.c`) **before any run_objdiff measurement** — fresh objs only.

## Lane A — feet/IK toe-vs-ankle, against the live gate (Opus) — 96 follow-up #1

The gate `FeetNotBelowFloorDuringGameplay` now RUNS (boot reaches gameplay) and fails
with sharp telemetry: ankle plants at +1.6..+2.0, **toe sinks to −4.30** (L 801/818,
R 781/818 samples below floor). Read `19-feet-ik-wave3.md` + `18-feet-ik-lane-c.md`
first. CLOSED leads you must NOT re-litigate: DoFSM int-vs-float, mConstraints
population. Prior plant-repair experiments (CharIKFoot Push 13/14, Dc3CleanPlant) were
gated off as destabilizing — your fix must not regress the gameplay boot or the 22
wave-3 regression tests.

1. The divergence is toe-vs-ankle: the ankle IK plants but the TOE chain sinks. Compare
   the toe path (CharIKFoot toe handling / foot roll / toe bone offsets) between Xbox
   asm and our source; instrument the live gate (telemetry exists) to bisect WHERE the
   toe Z first diverges from the ankle-relative pose (decode output is proven clean —
   `ClipPoseFixture` 12/12 with toe above floor in isolation, so the corruption happens
   between clip evaluation and final pose under gameplay polling).
2. The suspect path is `HamDriver.cpp:95-101` (song-move/poll-order) — confirm or refute
   with empirical poll-order capture; if refuted, follow the telemetry, not the doc.
3. Deliverable: gate green, OR a measured-improvement report (worst toe Z before/after,
   samples-below-floor count) plus the divergence pinned to a specific function with asm
   evidence. PPC neutrality required on any shared-source edit.

## Lane B — flip-list adjudication → fixes with tests (Opus) — 96 follow-up #2

Input: `docs/investigations/2026-06-10-roadmap-to-100/data/unicorn_refresh_main_d5491b67.json`
(the MAIN-plane flip list: 57 candidate real bugs under prior "equivalent" certs + 16
new-evidence real-bug class rows).

1. Triage order: the **object_memory / call_count / unmapped** classes first (strongest
   signal, ~9 rows), then **`Rand::Seed`** (verifier-confirmed MT-state high-16-bit
   divergence, same signed-arith family as the wave-2 `Rand::Int` fix), then
   cap_exhausted_decomp. The call_arg class (19) is mostly `__FILE__`/MakeString pointer
   noise — deprioritize, sample 3 to confirm noise.
2. For each adjudicated-REAL bug: asm-grounded diagnosis → behavior-pinning milo-tests
   case (failing pre-fix where feasible) → source fix that does not regress (ideally
   improves) PPC match → re-run unicorn on the fn (scripts/unicorn/refresh_frontier.py
   supports the frontier; a single-fn re-check via the runner is fine) to confirm the
   verdict flips to EQUIVALENT.
3. For adjudicated-FALSE rows: record the artifact class so the next refresh
   auto-classifies them (extend the adjudication rules in refresh_frontier.py if the
   pattern is mechanical).

Acceptance: ≥10 rows adjudicated with evidence (mix of real/false is fine); ≥3 real
bugs fixed-with-tests including `Rand::Seed`; unicorn verdicts confirmed flipped for
the fixes; updated flip-list JSON or adjudication table committed.

## Lane C — near-miss cohort + live-bug continuation (Opus) — 96 follow-ups #3/#5

1. **Single-blocker LikelyFixable cohort** (doc 16, re-measure each on YOUR fresh
   worktree first — the `.c`-rebuild may have moved them): `HttpReqCurl::WriteMemoryCallback`,
   `CharIKRod::Copy`, `IdentityInfo::Identified`, `CharIKHead::Poll`,
   `CheatsManager::CallCheatScript`, and PropSync (FPR regswap — permuter/fpr lever,
   see memory of stream3 fpr wins). Each fix completes an entire unit. Target: ≥4 units
   to 100% normalized.
2. **Live-bug continuation:** `ThreadTask::Replace` (obj/Task, 82.7% — resolve the
   erase-vs-remove intent via Ghidra/DWARF, the wave-3 deferral), then sweep the
   unexamined remainder of the doc-11 53-fn set with per-function asm diagnosis (the
   CSHA1 lesson: real bugs hide off the named list). Fix what's behavioral, floor-cert
   what's cosmetic (note it for certify evidence — do NOT write the DB).
3. Decomp patterns catalog (`docs/decomp/patterns/`) and the asm-archaeology playbook
   apply — these are exactly that class of function.

Acceptance: ≥4 single-blocker units at 100% normalized (worktree plane, stated);
ThreadTask::Replace resolved (fixed or floor-evidenced); ≥5 more of the 53-set
dispositioned with evidence; tests for every behavioral fix.

## Lane D — suite hygiene + cadence wiring (Sonnet) — 96 follow-ups #7/#8 + corrections

1. **Wire the unicorn cadence**: extend `scripts/nightly_measurement_guard.sh` with a
   `--unicorn` stage: `refresh_frontier.py --run` (~33s) → `apply_refresh.py
   --only-fresh-source` (verify that flag exists; if not, add it to apply_refresh.py —
   it should skip rows whose unicorn_source_hash matches) → `reconcile_db.py --fix` →
   `certify_floor.py --apply`. Dry-run end-to-end against a DB COPY; document in the
   script header. Do not crontab.
2. **Re-measure the remaining 12 single-blocker rows** (doc 16) on main post-`.c`-rebuild
   (sequential run_objdiff, main plane) — the parsedate lesson says some may now be 100.
   Update doc 16's reconciliation section with a fresh table; list any newly-promotable
   units (the orchestrator promotes via sync).
3. **Triage (investigate-only, no fixes) the 8 pre-existing milo-tests failures**
   (ObjectLifetime MergeDirs/MergeScope — likely the known ~ObjectDir NullifyAllRefs
   cascade bug; MeshVertexLoading skinning-decode; RndCamProjection GPU-math; the 400s
   AssetLoading timeout). For each: reproduce, one-paragraph root-cause hypothesis with
   evidence pointer, route (engine-repo vs dc3, which wave). Write
   `21-milo-tests-triage.md`.

Acceptance: guard script `--unicorn` stage proven on a DB copy; doc 16 refreshed with
main-plane numbers; doc 21 exists with all 8 failures triaged.

## Verification + orchestrator follow-up

Same adversarial Sonnet verify + one repair round. Verifiers re-measure on the named
plane; for lanes B/C re-run the new tests and run_objdiff yourself; for lane A re-run
the feet gate. Orchestrator afterward: merge `wave4/*` (watch for native/CMakeLists.txt
test-registration collisions between A/B/C — union them), run sync + unicorn refresh +
recert on main, commit `98-WAVE-4-RESULTS.md`.
