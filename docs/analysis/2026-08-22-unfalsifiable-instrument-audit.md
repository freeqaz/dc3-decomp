# Unfalsifiable-instrument audit — 2026-08-22

A sweep of this repo's measurement, verification and gating surfaces for one
defect class: **an instrument structurally incapable of reporting a problem**,
whose clean result is therefore not evidence of health.

Six defects were fixed in `fix/cannotfail-audit-20260822`, each with a
sabotage A/B recorded in its commit message. This file is the **filed
remainder** — candidates that were confirmed by execution but are out of scope
for one lane, plus the ones that were audited and found sound, so the next
person does not re-derive either list.

Every "REPRO" below was run. Nothing here is a suspicion.

---

## Fixed in this lane (see the commits for the sabotage tables)

| Surface | The defect | Commit |
|---|---|---|
| `scripts/analysis/ruler.py` | Ratchet regex blind to `"-c", f"functionRelocDiffs={x}"`. Reported PASS, claimed a migration that never happened, and asked you to shrink its own baseline. Two more files were invisible the same way. | `f79c99733` |
| `scripts/verify_objs_patched.py` | `--check`/`--emit`/`--verify-manifest` all reported a **verified-patched tree over ZERO objects** (`tree_sha256=e3b0c442…`, the sha of the empty string). `patch_guard` accepted it. | `1b79630f0` |
| `tools/none_guard.py` | Printed "no instruction, no datum and **no literal**" when the string-literal sidecar was absent — a positive claim about a check that did not run. The same file already says "Absent is FAILED, not clean" about `provenance`. | `da010df4c` |
| `scripts/reset_false_complete.py` | Its own `verdict_reason` matched its own `LIKE` selector. A latch: **385 rows** in the live DB were one run away from being demoted and having `current_percent` NULLed, **379 of them at `match_percent_normalized = 100`**. | `4ea14906f` |
| `scripts/orchestrator/symbol_sweep.py` | `run_symbol_sweep(kind='vtable_slots')` **dead on main** (coverage-API signature drift), and the parity test written to prove otherwise was defined after `unittest.main()` and had never executed. | `781ce7fb5` |
| `.github/workflows/native-build.yml`, `scripts/native_test.sh` | CI swallowed every ctest failure (`cmd \|\| echo` exits 0). The skip budget disarmed itself if its file was missing **or reworded**. | `ae166c376` |
| `scripts/certify_floor.py` | "blocked on STALE unicorn" rows were **written**, not blocked. The unit test encoded the defect as the contract. | `b791b14d6` |

---

## Filed — confirmed by execution, not fixed here

> **F1, F2 and F3 were CLOSED on 2026-08-23** by `fix/measurement-guards-20260823`,
> together, as the audit recommended — they shared one measurement path. All
> three now refuse through `scripts/orchestrator/patch_guard.py`; the sabotage
> table (before/after on a genuinely unpatched tree, exit codes read without a
> pipe) is in that lane's commit message. Two things it did **not** close, on
> purpose: `report_result` still never compares the agent's `percent` against a
> measurement (the remainder of F3 — closing it means re-measuring inside a
> live-fleet entry point), and `core.py`'s F4 `if not patch and ... == "complete"`
> exemption is untouched. The sections below are left as written; they are the
> record of what was measured.

### F1. `scripts/recheck_stale.py` writes `verdict='COMPLETE'` with no guard, no build, and the wrong ruler
**Rank: corrupts stored data.** — **CLOSED 2026-08-23.**

`run_objdiff()` (lines 25-39) has `except Exception: pass`, returns
`summary.get("equal_percent", 0.0)`, never calls `patch_guard`, never builds,
and reads `instruction_summary.equal_percent` — the ruler `batch_check.py:69-96`
was explicitly fixed to stop using (it reads 99.67 where the canonical scorer
reads 99.98). `>= 100.0` then writes `current_percent=100, best_percent=100,
verdict='COMPLETE'`.

REPRO: any objdiff-cli upgrade that renames `instruction_summary` makes every
function return `0.0`; the script prints `Errors: 0`, exits 0, and has
re-checked nothing. Or run it after a bare `ninja <one>.obj`, which
`patch_guard.py`'s docstring measures at −1.22 pp of unit
`matched_functions_percent`.

Not fixed here because the right fix is "route it through `patch_guard` and
`batch_check.match_percent_from_diff`", which is a rewrite of its measurement
path, and it shares that path with `batch_promote.py` (below).

### F2. `scripts/batch_promote.py` — a missing `objdiff-cli` is silently "no objdiff data"
**Rank: corrupts stored data.** — **CLOSED 2026-08-23.**

Lines 174-183 catch `FileNotFoundError` explicitly and return `None`. The
pipeline continues to unicorn and `decide_verdict` promotes to `COMPLETE` on
`unicorn_verdict == 'EQUIVALENT'` with `normalized_pct = None`; the write uses
`COALESCE(NULL, current_percent)`, so the row keeps its old percent and gains a
COMPLETE verdict indistinguishable from an earned one. No `patch_guard`, and
`--build` is opt-in.

REPRO: `mv bin/objdiff-cli bin/objdiff-cli.bak && python3
scripts/batch_promote.py --apply --unit 'system/char/*'` →
`COMPLETE (normalized 100%): 0` / `COMPLETE (unicorn equivalent): N` /
`Errors: 0`, exit 0.

Same shared measurement path as F1; fix them together.

### F3. `scripts/orchestrator/mcp_server.py::report_result` — an agent's number is written unverified
**Rank: corrupts stored data.** — **PARTIALLY CLOSED 2026-08-23** (patch guard added, `project_dir` added, `at_limit` now gated too; the `percent`-vs-measurement comparison is still missing).

`percent = args.get("percent", 0)` (line 1276) goes straight to
`update_function_status`. `status="at_limit"` has **no** verification at all.
The one guard on `status="complete"` (1291-1316) ends in
`except Exception: pass  # If check fails, allow the report through`, ignores
the subprocess return code, measures `self.project_root` rather than the
worktree the agent worked in, and only ever tests `base_size == 0` — it never
compares `percent` to anything.

REPRO: `report_result(symbol=<a 43% function>, status="complete", percent=100)`
from an agent that edited nothing → `Result recorded: complete at 100% (stored
to database)`, and `query_functions(status='workable')` never shows it again.

Not fixed here: the fix is to give the tool a `project_dir` and re-measure,
which changes an MCP tool's schema. That needs a server restart to take effect
(CLAUDE.md) and coordination with the running fleet.

### F4. `scripts/orchestrator/core.py:908-915` — "no patch but agent says complete" is trusted
**Rank: corrupts stored data.**

```python
if not patch and agent_result.status == "complete" and agent_result.percent is not None:
    log.info("... Trusting agent — function likely already matching.")
elif not patch and agent_result.percent > (func.get("current_percent") or 0):
    log.warning("... Discarding agent-reported percent.")
```

The `elif` is the correct rule — *no patch means the claim is unsupported* —
and the `if` above exempts exactly the case where the claim is most
consequential. Compounded by `worktree_pool.extract_patch` (510-594), which
returns `None` for three distinct states: no edits, `git` failed, session
unknown.

### F5. `scripts/orchestrator/symbol_sweep.py` — zero examined still reports `complete: true`
**Rank: false "exhausted".**

`_LocalCoverage.is_clean()` is "the arithmetic adds up" and is serialised as
`"complete"`. With `universe == 0`, or with every symbol dropped, it is `True`
and `main()` returns 0. `enumerate_target_symbols` counts
`units_missing_object` into `stats` but not into `universe`, so an unsplit tree
yields universe 0.

Partially mitigated by the fix in `781ce7fb5` (the sweep runs at all again, and
the COVERAGE block always states its denominator and labels TRUNCATED). The
remaining piece is `require_examined()`, which the shared `coverage.py` already
implements. **Not done here because `scripts/orchestrator/tests/test_symbol_sweep.py`
deliberately asserts `complete: True` with drops** (7 examined, 3 dropped) —
changing `complete`'s meaning is a contract change, not a bug fix, and should
be a lane of its own that adds a separate `examined_nothing` flag rather than
redefining `complete`.

### F6. `scripts/reconcile_db.py` certifies clean over rows it structurally cannot see
**Rank: false "exhausted".**

`if fz is None: continue` (line 95) drops every function whose
`fuzzy_match_percent` is null. Verified against the live report: **430 non-SDK
rows have `fuzzy_match_percent: null` and all 430 carry
`match_percent_normalized`** (e.g. `??1ArchiveSkeleton@@UAA@XZ`).
`sync_match_percent.py:139-166` carries a 25-line comment explaining that this
exact `continue` produced the "335 COMPLETE rows at literal 0%" cert-rot
cohort. Check (b) only fires on `current_percent < 100`, so a fossil at exactly
100 passes, and `d_db_only` is excluded from `total_drift` by design.

REPRO: `python3 scripts/reconcile_db.py --db …/decomp.db` → `OK: no drift
detected.` immediately before `sync_match_percent.py --demote --dry-run`
reports a non-zero demote count against the same DB.

### F7. `scripts/nightly_measurement_guard.sh` — three separate false-greens
**Rank: false "exhausted" / disarmed ratchet.**

* Lines 258-270: `CANDIDATE_BUGS=$(python3 - <<PY 2>/dev/null || echo "?")` plus
  `.get("flip_cause_candidate_bug", "?")`. The guard at 266 treats `?` as *not
  an alert*, then line 341 prints the positive claim
  `[unicorn] Cadence complete — no candidate bugs found.` Rename the summary key
  or make the JSON unreadable and the run asserts a clean bill it has no
  evidence for.
* Lines 186-201: if the strict-reloc baseline file is absent it is **created
  from the current count** and `STRICT_EXIT` stays 0 — `rm` the baseline and any
  regression is permanently re-baselined as normal.
* Lines 154-194: nothing asserts a non-empty universe, so
  `genuine_wrong_target 0 <= baseline 41` prints OK over a tree with no built
  objects.

Also `ALL CHECKS PASSED` is printed when only one of three layers ran
(`STRICT_EXIT`/`UNICORN_EXIT` initialise to 0 and stay 0 when their flags are
absent) — "not run" rendered identically to "passed".

### F8. `scripts/setup_worktree.sh:283-305` — the warm-cache validity check asks git a question about objects
**Rank: silently measures the wrong tree.**

`_changed` is built from four `git` invocations whose stderr is discarded,
whose exit status is unchecked, on the left of a pipe (so `pipefail` cannot see
them), terminated by `|| true`. Any of them failing contributes zero lines,
which is arithmetically identical to "nothing changed" — and `_changed == 0`
back-dates every source to 2020-01-01 and marks every reflinked object current.

The live path: `BASE_REF` defaults to `HEAD` and line 173 checks out `$BRANCH`
ignoring `$BASE_REF`, so re-entering an old branch makes all four commands
return zero lines by construction. Every `run_objdiff` in that worktree then
measures **main's** code.

### F9. `scripts/clean_stale_objects.sh` claims freshness over an empty universe
**Rank: confusing, but it gates two documented workflows.**

`done < <(find "${BUILD_DIR}/src" -name "*.obj" … 2>/dev/null)` — process
substitution, so `pipefail` does not reach it — then prints the positive claim
`No stale objects found. All .obj files are newer than PCH.` Run it in a
worktree whose `build/373307D9/src` does not exist yet and it says exactly
that. Separately, `stale` is incremented **inside** the source-mapping success
branch, so a stale object whose source path cannot be derived is neither
touched nor counted.

### F10. `native/tests/test_gates.cpp` — the gate-verification test passes with zero assertions
**Rank: the instrument-check has nothing to check exactly when coverage is worst.**

`MILO_TEST_GATES_ON_STR` defaults to `""` via `#ifndef`, and
`native/CMakeLists.txt:2017-2038` leaves it empty when the asset probes fail —
which is CI's configuration. The loop over `on` is the entire test, so empty
means zero `EXPECT_TRUE` and a PASS. Deleting the `-D` at
`native/CMakeLists.txt:2063-2065` also still compiles and still passes.

### F11. CI still runs bare `ctest`, not `scripts/native_test.sh`
**Rank: known gap, deliberately left.**

`ae166c376` made the CI job able to fail, but CI has no `DC3_DATA`, so every
asset-dependent suite skips and the 69-skip budget would be wildly exceeded.
Making the budget configuration-aware is a real change that cannot be validated
without running CI; a half-migration would be another instrument that looks
complete.

---

## Audited and found SOUND

Listed so the next audit does not repeat them. Each line says **what makes it
able to fail** — that is the property being certified, not "it looked fine".

| Surface | Why it can fail |
|---|---|
| `scripts/verify_split_current.py` | `--check` refuses on a missing stamp, on `state != complete`, and on any config-input hash drift; `--begin`/`--complete` bracket the split so a crashed split leaves the tree explicitly unvouchable. `--stamp-out` digests the stamp, so a passing check on an unmoved tree leaves the file byte-identical. |
| `scripts/orchestrator/patch_guard.py` | Every leg **raises** `UnpatchedTreeError`/`StaleSplitError`: missing verifier, build tool off PATH, build timeout, build non-zero, verify timeout, verify non-zero, split drift. No `check=False`, no ignored return code. |
| `scripts/analysis/coverage.py` | Distinct non-zero exits for truncation (3), unbalanced arithmetic (4), no input (5) and **missing denominator (6)** — exit 6 exists specifically because exit 4 could be bypassed by deleting the `universe()` call. A disarmed tripwire exits differently from a passing one. |
| `scripts/analysis/determinism_check.py` | Has an explicit vacuity guard (`MIN_MEANINGFUL_BYTES`): two empty outputs hash equal, so it classifies them INCONCLUSIVE and `return 1 if (diff or inc) else 0` makes inconclusive a **failure**. |
| `scripts/analysis/ruler_agreement.py` | Checks two paths against each other rather than a constant; exits 2 on `NO_DISCRIMINATION` when the sample could not have detected the defect, and on `checked == 0`; ships a working `--self-test` that injects a known-wrong value. |
| `scripts/measure_progress.sh` | `ninja_is_clean` demands the positive string `"no work to do"` rather than trusting an exit code, and returns distinct codes 2/3; refuses when the dtk/objdiff identity is unknown; fingerprints both reports around the diff to catch a racing rebuild. (One exception: a deleted `.meta` skips provenance verification on a WARNING and does **not** require `--allow-stale`.) |
| `scripts/sync_match_percent.py` | `_round_pct` refuses to let rounding *reach* 100 from below because the stored value is a gate, not a display; scores the 430 null-fuzzy rows `0.0` instead of skipping them; symbols in the DB but absent from the report are flagged, never mutated. |
| `scripts/bump-engine.sh` | Refuses to write a value `CMakeCache.txt` would shadow — it verifies the edit *can take effect* — and re-reads the temp file comparing to `NEW_SHA` before `mv`, so a `sed` that matched nothing is caught. |
| `scripts/native_test.sh` (parser half) | Parses the ctest summary two ways and then **refuses to report a number** (exit 4) if `total` is empty or zero; `CTEST_RC=${PIPESTATUS[0]}` defeats `\| tee`; the ratchet makes an *improvement* a hard failure until locked in. |
| `scripts/pose_scoring_gate.sh` | `[ "${ST_N:-0}" -gt 0 ]` — the "did I measure anything" assertion. Differential by design: two configurations must disagree in a specified direction, so a uniformly-broken kernel cannot satisfy it by being consistent. |
| `scripts/analysis/pattern_census.py` | Returns 4 on an unpatched tree and on `RelocBlindPatternError`, 3 on truncation; refuses `--apply` for a negative control; `refresh_legacy_flags` updates only examined rows and reports how many remain NULL. |
| `scripts/analysis/fold_proof.py` | `return 0 if REFUTED == 0 and UNDECIDABLE == 0 else 1` — **undecidable is a failure**, not a pass. |
| `scripts/ingest_report.py` | The shadow-DB guard is a refusal, not a warning, and names the measured shadow-vs-real gap. `--build-safe`'s never-fail behaviour is declared in its own `--help` and scoped to metadata. |
| `orchestrator/database.py` `objdiff_pattern` | Raises for the blind ruler *and* when no scan is recorded for the ruler — the best in-tree instance of "absence of measurement must not read as absence of the pattern". (It is not reachable from the MCP `query_functions` schema; that is a gap, not a defect in this code.) |
| `scripts/certify_floor.py --check-denominator` | Two **independent implementations** of one predicate (SQL `LIKE`/`ESCAPE` vs Python `str.startswith`), exiting 1 when they disagree, so a future wildcard-escaping bug makes the totals diverge instead of undercounting in lockstep. |

### Checked and NOT reproduced

* **`certify_floor` `permuter_exhausted` on a NULL `end_percent`.** The code
  path exists (`best_end is None` is treated as "no headroom"), but measured on
  a copy of the live DB after `--apply`: **0 of 191** `permuter_exhausted`
  certificates have `MAX(attempts.end_percent) IS NULL`. Not firing today.
* **The 40× stale-certificate estimate.** 40 certificates in the DB carry
  stale-unicorn evidence, but only **1** was written by a current-build run —
  the rest are from earlier builds. The headline count was honest; the word
  "blocked" was not.
* **`native_test.sh` exiting 2 on a pre-existing skip overage.** Not reproduced
  on this box: 449 registered, 380 executed, 380 passed, 0 failed, **69 skipped
  against a budget of 69**, exit 0, both before and after this lane's changes.
