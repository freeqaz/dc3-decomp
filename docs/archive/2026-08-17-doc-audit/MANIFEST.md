# Manifest — 2026-08-17 Documentation Audit

42 files (33 markdown + 9 support scripts) archived on 2026-08-17.

The common failure was not that these documents were bad. It is that they were
**status snapshots and hardcoded worklists written without an expiry**, kept in
the live documentation tree, and cited months after their contents stopped being
true. One of them was measured as *actively misleading* three months after it
was written. The replacements —
[`../../STATE_OF_THE_DECOMP.md`](../../STATE_OF_THE_DECOMP.md) and
[`../../decomp/REMAINING_WORK.md`](../../decomp/REMAINING_WORK.md) — carry a
measurement date, print the command that produced every number, and ship
**queries instead of worklists**.

**These files are preserved byte-for-byte. Every correction is in this manifest,
not in the file.**

---

## Ground truth as of 2026-08-17

Recorded here so a future reader can date this archive and see exactly how far
each claim below had drifted. Measured at `924ab0c5e`, re-verified in a clean
worktree at `2b7382e93` with a full `ninja` build (identical results).

**MATCHED** (`build/373307D9/report.json`, authorable denominator):

| | |
|---|---|
| Functions | **91.21 %** — 29,383 / 32,213 |
| Code bytes | **77.41 %** — 4,910,452 / 6,343,156 |
| Complete units | 416 / 967 (43.02 %) |
| Remaining | 2,830 fns / 1,250,152 bytes |
| XEX-total (XDK-diluted) | 60.81 % fns / 43.18 % bytes |

**DONE-WITH-CERTS** (`decomp.db`, 33,560 non-excluded rows — a *different*
population): COMPLETE 29,655, AT_LIMIT 3,628, unverdicted 277.

**Report provenance:** objdiff-cli 4.2.3, commit `88b425bc3bad`,
`functionRelocDiffs=name_check`.

**The ruler changed in 2026-08.** The report used to be built with
`functionRelocDiffs=None`; it is now built with `name_check`, which charges a
relocation whose target *symbol name* differs even when the instruction bytes
match. That accounts for the entire −1.23 pp / −432-function / −2.14 pp-bytes
step against the 2026-06-21 snapshot. It is a measurement correction, not lost
code. Consequence for this archive: **every percentage in every file here was
measured on the old, more forgiving ruler and cannot be differenced against a
current number.**

Other durable facts, for comparison against the claims below:

- Stubs (`is_stub = 1`, non-excluded): **494 fns / 117,920 bytes** —
  `synth_xbox` 219, `os` 101, `rnddx9` 52.
- 1,910 of the 3,628 AT_LIMIT rows are `merged_*` ICF placeholders absent from
  the report; report-visible AT_LIMIT is 1,651 fns / 939,372 bytes.
- 640 AT_LIMIT rows / 466,036 bytes carry a real-bug unicorn divergence class —
  41 % of all AT_LIMIT bytes. A 2026-08-04 blind audit of regswap AT_LIMIT
  certificates scored 3/10.
- **875 rows marked COMPLETE in `decomp.db` are not 100 % in a fresh report**
  (540 in the 90–99.99 band / 143,888 B; 335 at 0 % / 35,104 B). DB hygiene is a
  separate lane in flight (coordinator task #101).
- Two clean builds of the same commit differ by ~±160 functions, almost all
  12–52-byte dynamic-initializer/atexit thunks. Single-function deltas on those
  are noise.

---

## `status-snapshots/`

Point-in-time project status. All five were written in 2026-02, all quote
headline percentages, and no two of them agree with each other.

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `STATUS.md` | 2026-02-26 | "Complete (non-SDK) 29,890 / 32,328 (**92.5 %**)", "AT_LIMIT 1,652", "Remaining workable 786", "Done 97.6 %". The 92.5 % is a *DB verdict* percentage presented next to build percentages, which is the exact conflation that produces wrong numbers; AT_LIMIT is now 3,628 rows, and "786 remaining workable" is off by an order of magnitude against 2,830 report-remaining functions. | [`STATE_OF_THE_DECOMP.md`](../../STATE_OF_THE_DECOMP.md) |
| `TO_100_TRACKING.md` | 2026-02-26 | A per-unit "path to 100 %" tracker plus a list of functions "Confirmed AT_LIMIT (truly unfixable)". The confirmations do not survive: 640 AT_LIMIT rows now carry a real-bug unicorn divergence class and a blind cert audit scored 3/10. | [`decomp/REMAINING_WORK.md`](../../decomp/REMAINING_WORK.md) |
| `DECOMP_PROGRESS_PLAN.md` | 2026-02-26 | "Current Overall Progress: 92.4 % COMPLETE, 4.7 % AT_LIMIT", "Orchestrator Status 97.1 %", plus session-by-session fix logs. Superseded numerically; the per-function *techniques* it records (goto for branch polarity, integer stores for zero floats, `== 1` vs `!x`) are real and live on in `decomp/TECHNICAL_NOTES.md` and `decomp/patterns/`. | [`STATE_OF_THE_DECOMP.md`](../../STATE_OF_THE_DECOMP.md), `decomp/patterns/INDEX.md` |
| `LINKING_STATUS.md` | 2026-02-26 | A linking-infrastructure state table (LNK4006 = 756, unresolved = 0, `/FORCE:MULTIPLE` only, ICF alias handling). Numbers are a 2026-02 snapshot. The *architecture* it describes is still broadly how linking works. Cited from `plans/CLEAN_LINK_PROJECT.md`, which has been annotated. | `docs/tools/BUILD_SYSTEM.md`, `docs/plans/CLEAN_LINK_PROJECT.md` |
| `GAP_ANALYSIS.md` | 2026-02-17 | "Total Functions 47,835 / non-excluded 31,814", "Fuzzy Match ~43.9 %", "Matched Code Bytes 35.96 %", "COMPLETE 10,719 (33.7 %)". Roughly half the current values — this is the oldest snapshot in the set and the most likely to mislead a skim reader. | [`STATE_OF_THE_DECOMP.md`](../../STATE_OF_THE_DECOMP.md) |

**Broken internal links:** `GAP_ANALYSIS.md` links to `LOW_HANGING_FRUIT.md` and
`SUBAGENT_STRATEGY.md` as siblings. Those landed in `decomp-planning/`, so the
links inside the frozen file are dead. They resolve to
`../decomp-planning/LOW_HANGING_FRUIT.md` and
`../decomp-planning/SUBAGENT_STRATEGY.md`.

---

## `decomp-planning/`

Hardcoded worklists. Every one of them names specific functions at specific
percentages. This is the class of document that
[`decomp/REMAINING_WORK.md`](../../decomp/REMAINING_WORK.md) exists to stop
being written.

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `BATCH_TARGETS.md` | 2026-02-27 | Tier-1 table of "15 workable functions, never attempted" at 94–99.3 %, and a Tier-2 list of units with "150+ untracked functions". Both are 2026-02 snapshots. **Its "Gotcha: Ninja Doesn't Track Header Dependencies" section is now flatly false** — ninja tracks header deps automatically via `/showIncludes` with wibo path rewriting, and the `touch src/... && ninja` ritual it prescribes is no longer needed (see `CLAUDE.md`). The durable part — the SQL recipes and the struct-offset-bug detection method — was lifted into `REMAINING_WORK.md` before archiving. | [`decomp/REMAINING_WORK.md`](../../decomp/REMAINING_WORK.md) |
| `LOW_HANGING_FRUIT.md` | 2026-02-27 | "31,385 COMPLETE (97.1 %), 961 AT_LIMIT, **2 remaining workable**", "25,546 functions at 100 % out of 47,463", "45.44 % fuzzy match". The "2 workable functions left — effectively all triaged" claim is the most damaging line in the archive: it asserts the project was out of work while 2,830 functions were and are below 100 %. | [`decomp/REMAINING_WORK.md`](../../decomp/REMAINING_WORK.md) |
| `STUB_BURNDOWN.md` | 2026-02-21 | "Total stubs analyzed: **74**", with per-stub implementation status. The real population is **494 stubs / 117,920 bytes**; the document covered under 15 % of it. Already flagged as "3 months stale and actively misleading" by `investigations/2026-06-10-roadmap-to-100/99v-WAVE-24-RESULTS.md`. | [`STATE_OF_THE_DECOMP.md` § Stubs](../../STATE_OF_THE_DECOMP.md#stubs) |
| `STUB_ROADMAP.md` | 2026-02-21 | A hand-curated list of functions with explicit `// TODO` comments and empty bodies (`SynthSample360::NewInst`, `Game::StartIntro`, …). Superseded by the `is_stub` column, which is machine-maintained and complete. | `query_functions(is_stub=True)` |
| `DIVERGENCE_BURNDOWN.md` | 2026-02-20 | Per-class unicorn divergence counts: return_value 15, call_arg 17, object_memory 15, stack_layout 92, call_count 352. Current DB: return_value 8, call_arg 64, object_memory 24, stack_layout 114, call_count 367 — and the class taxonomy has since grown the `cap_exhausted*` and `wild_jump_match` classes, which are now the largest real-bug buckets. Its P0/P1/P2/P3 prioritisation is also superseded by the real-bug / artefact split. | [`decomp/REMAINING_WORK.md` § 3](../../decomp/REMAINING_WORK.md#3-unicorn-divergent-by-class-the-real-bug-oracle) |
| `REGRESSION_ROADMAP.md` | 2026-03-08 | "Overall fuzzy match 48.11 %", "90 functions (40.7 KB) regressed vs the og baseline", "+364.3 KB / +2237 functions net". A specific two-tree comparison against `og-dc3-decomp` at `b14f7df76` that has not been re-run. Its warning that reverting the `Object.h` iterator change is *toxic* (19 cascading regressions) is still the operative reason not to try it. | `decomp/TECHNICAL_NOTES.md` § Header edits shift inlining TU-wide |
| `HEADER_REGRESSION_ANALYSIS.md` | 2026-03-10 | "72 regressions (36.1 KB) vs og baseline", "523 headers differ", plus a 12-item action list. The counts are a 2026-03 snapshot and the action items were partly done and partly abandoned. **Its mechanism finding is durable and was salvaged before archiving**: header edits shift MSVC inlining TU-wide, and `HX_NATIVE` guards were only 62 of the 523 changed headers and were *not* the dominant cause. | `decomp/TECHNICAL_NOTES.md` § Header edits shift inlining TU-wide |
| `SUBAGENT_STRATEGY.md` | 2026-02-13 | A manual parallel-agent playbook from before the orchestrator MCP tools existed. Describes hand-briefing agents and hand-merging results; the orchestrator, the skills, and the worktree tooling replaced all of it. | `docs/tools/INDEX.md`, `docs/tools/WORKFLOW.md`, `docs/tools/orchestrator/` |
| `UNIMPLEMENTED_STUBS.md` | 2026-03-17 | A ~2,000-line generated list of link-glue symbols present in the original binary but absent from decomp source. Generated output committed as documentation; regenerate rather than read. | Regenerate from the link tooling; `docs/tools/BUILD_SYSTEM.md` |

**Preserved internal links:** `LOW_HANGING_FRUIT.md` → `SUBAGENT_STRATEGY.md`
and `HEADER_REGRESSION_ANALYSIS.md` → `REGRESSION_ROADMAP.md` both still resolve
— those pairs landed in the same directory.

---

## `experiments/`

Tooling and methodology experiments from 2026-02/03. Several describe systems
that were built, measured, and then superseded; they are kept for the
measurements, not the recommendations.

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `codex-coordination-workflow.md` | 2026-02-11 | Workflow for consulting GPT-5.3-Codex via OpenRouter for decomp analysis, with model IDs and a 400K context figure. The workflow is not in use. **`scripts/codex_helper.py` still exists in the repo** and is still executable — archiving the document did not remove the script. If you are deleting one, decide about both. | — (workflow retired) |
| `context-enrichment/` (9 files) | 2026-02-11 → 02-19 | An A/B testing programme for prompt enrichments (diff patterns, function types, RB2 layouts, attempt diffs, matched siblings, callee signatures), each with a status/token-cost/expected-impact table. The enrichments that worked were absorbed into the orchestrator's `recon` / `run_analyze_function` output; the A/B harness is gone. `scripts/analysis/analyze_enrichment.py` still documents `--output docs/context-enrichment/` in its usage string, which now points at a moved directory. | `docs/tools/` (orchestrator tooling), `recon` skill |
| `meta-strategy/` (6 markdown + 9 scripts) | 2026-02-11 → 02-23 | An "ease × impact × confidence" priority-scoring model, with `INDEX.md`, `GOALS.md`, `SCORING_MODEL.md`, `APPENDIX_RESEARCH.md`, `SQL_QUERIES.md` and a `scripts/` directory. The model's outputs (`priority_score`, `ease_score`, `impact_score`, `confidence_score`, `fan_in`, `reachable_100`, `primary_pattern`) are still columns in `decomp.db` but **have not been recomputed since 2026-02 and must not be used for triage**. `SQL_QUERIES.md`'s durable recipes were lifted into `REMAINING_WORK.md`; the ones that were not lifted are the ones keyed on `current_percent`, `verdict` or the scoring columns — i.e. the untrustworthy ones. **The 9 scripts moved with the directory**; their own usage strings still say `docs/meta-strategy/scripts/...` and now need the `docs/archive/2026-08-17-doc-audit/experiments/` prefix. | [`decomp/REMAINING_WORK.md`](../../decomp/REMAINING_WORK.md) |
| `PHASE1_DESIGN.md` | 2026-02-12 | Design document for Phase 1 of the unicorn function runner (COFF extraction, 5 relocation types, trampoline mocking, state comparison). The runner was built and is in production; this is the pre-build design, not its documentation. | `docs/tools/UNICORN_FUNCTION_RUNNER.md`, `unicorn-query` skill |
| `PARITY_FAILURES.md` | 2026-03-05 | A ledger of parity-oracle failures in `native/tests`, with an allowed-fail list. The native test suite has moved substantially since (the shared engine now passes 371/371 in `milo-tests`); the specific failures listed are not current. | `docs/native/TESTING.md` |
| `TEST_VALUE_AUDIT.md` | 2026-03-05 | A high/low-value audit of every test in `native/tests/*.cpp` as of 2026-03-05, with a rubric. The rubric is reusable; the per-test verdicts are against a test suite that has since changed. | `docs/native/TESTING.md` |

**Preserved internal link:** `TEST_VALUE_AUDIT.md` → `PARITY_FAILURES.md`
resolves — both landed in `experiments/`.

---

## Not archived, and why

| Candidate | Decision |
|---|---|
| `docs/runtime/XENIA_HEADLESS_STATUS.md` | The audit listed this as a duplicate of `docs/plans/XENIA_HEADLESS_STATUS.md`. **It is not a duplicate.** The `plans/` copy is a three-line pointer that explicitly redirects to the `runtime/` copy, which is the substantive document (Vulkan pipeline, multi-frame capture, scripted input, guest memory patches). Archiving the `runtime/` copy would have archived the live document and orphaned four inbound links. **Both kept, neither moved.** |
| `docs/decomp/patterns/` (whole directory) | **Must never move.** `objdiff-cli` identifies this project by probing for `docs/decomp/patterns/PERMUTER_ROI_ANALYSIS.md`; if that file moves, objdiff silently stops emitting every DC3 documentation link in its analysis output — no error, just missing links. See the banner in [`../../INDEX.md`](../../INDEX.md). |
| `docs/PROGRESS_METRICS.md` | Hardcoded as `DEFAULT_MD` in `scripts/progress_metrics.py`. Regenerated in place instead of moved. |
| `docs/decomp/RB3_REFERENCE.md` | Header numbers were stale ("30.7 % code matched", "45.2 % functions"); a dated correction note was appended rather than moving the file, which is still useful for its directory-compatibility matrix. |

## Moved, but not archived

These were relocated in the same pass for tidiness, not because they are
superseded:

| From | To |
|---|---|
| `docs/session/sIdentityXfm-per-tu-headers.md` | `docs/sessions/2026-03-19-sIdentityXfm-per-tu-headers.md` (the singular `docs/session/` directory held exactly one file and is now removed) |
| `docs/native/SESSION40_PLAN.md` | `docs/sessions/SESSION40_PLAN.md` |
| `docs/native/SESSION41_PLAN.md` | `docs/sessions/SESSION41_PLAN.md` |

`docs/link/`, `docs/testing/` and `docs/unicorn_runner/` became empty as a result
of this archive and were removed.
