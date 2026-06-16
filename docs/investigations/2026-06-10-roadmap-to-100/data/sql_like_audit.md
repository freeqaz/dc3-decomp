# SQL-LIKE-over-symbols audit (wave-10 Lane C)

Date: 2026-06-16 · Worktree: `wt/w10-hygiene` · Base: main `188871ad`

## Why

Wave 9 found `symbol NOT LIKE '??_%'` in `certify_floor.py`: SQL `LIKE` treats `_`
as a **single-char wildcard**, so `'??_%'` matches EVERY `??`-prefixed symbol
(`??0` ctors, `??1` dtors, `??4` operators, `??$` templates), not just the literal
`??_` artifact prefix. Used as `NOT LIKE`, it **excluded** all of them — hiding
6,835 authorable fns / ~1.0 MB from band queries until 2026-06-11.

This audit greps the entire `scripts/` tree for `LIKE '<literal>'` clauses over
`symbol` / `demangled` / `unit` columns where the literal carries an **unescaped
`_` or `%`** in a position meant to be literal, classifies each as buggy or
latent, and records the disposition.

## Method

- `grep -rn "LIKE" scripts/ --include=*.py --include=*.sh`, filtered to
  symbol/demangled/unit columns (excluding `LIKELY_FIXABLE` enum noise).
- For each underscore-bearing literal, ran a read-only count against the live
  main `decomp.db` comparing the unescaped pattern vs the ESCAPE'd pattern to
  measure **actual** over/under-match (not just theoretical).
- Parameterized clauses (`LIKE ?`) that pass a *user-supplied* glob/pattern at
  runtime are **correct by design** (the caller intends `%`/`_` as wildcards) and
  are excluded from the bug class.

## Findings ledger

| # | Site | Literal | Column | Buggy? | Measured impact (main DB) | Disposition |
|---|------|---------|--------|--------|---------------------------|-------------|
| 1 | `scripts/unicorn/refresh_frontier.py:83` | `??_%`, `merged_%`, `fn_%`, `lbl_%` (`ARTIFACT_PREFIXES`) | symbol | **YES** | partial frontier 1299 (buggy) vs 1407 (fixed) → **108 `??` partials hidden** from the unicorn frontier | **FIXED** — added local `_like_prefix_clause` (escapes `_`/`%`/`~`), applied to both SDK + artifact clauses |
| 2 | `scripts/batch_check.py:57` | `??__F%`,`??__E%`,`??_9%`,`??_E%`,`??_G%`,`??$MakeString%` (`BOILERPLATE_SYMBOL_PREFIXES`) when `skip_boilerplate=True` | symbol | **YES** | union over-excluded **607** symbols; **146** of them real authorable work (e.g. `??YReplicator` op+=, `??AReplicator` op[], `??0HeadOrientationRuntime` ctor) wrongly skipped | **FIXED** — escape `_` per-prefix with `ESCAPE '\'` (matches orchestrator `query_functions`) |
| 3 | `scripts/orchestrator/database.py:658,659` (`get_next_function`) | `merged_%`, `fn_%` | symbol | latent | escaped vs unescaped delta = **0** today (all `merged_<hex>`/`fn_<addr>`); footgun if a future symbol aliases | **HARDENED** — added shared `like_prefix_clause` + `_EXCLUDE_MERGED`/`_EXCLUDE_FN`/`_EXCLUDE_STLPMTX` constants; applied here |
| 4 | `scripts/orchestrator/database.py:848,849` (`query_functions`) | `merged_%`, `%stlpmtx_std::%` | symbol/demangled | latent | delta = 0 | **HARDENED** (the `skip_boilerplate` branch at 853 was already ESCAPE'd by wave-9) |
| 5 | `scripts/orchestrator/database.py:1201,1202` (`count_*`) | `merged_%`, `fn_%` | symbol | latent | delta = 0 | **HARDENED** |
| 6 | `scripts/orchestrator/database.py:1655,1656` | `merged_%`, `%stlpmtx_std::%` | symbol/demangled | latent | delta = 0 | **HARDENED** |
| 7 | `scripts/orchestrator/database.py:1741,1742` (aliased `f.`) | `merged_%`, `%stlpmtx_std::%` | f.symbol/f.demangled | latent | delta = 0 | **HARDENED** |
| 8 | `scripts/get_progress.py:136` | `fn_%` (positive — counts funclets) | symbol | latent | escaped vs unescaped funclet count = 47/47 (delta 0) | **HARDENED** |
| 9 | `scripts/get_progress.py:220,221` | `merged_%`, `%stlpmtx_std::%` | symbol/demangled | latent | top-units count 196/196 (delta 0) | **HARDENED** |
| 10 | `scripts/find_hidden_work.py:50,63,138` | `merged_%` (×3) | symbol | latent | delta 0 | **HARDENED** |
| 11 | `scripts/sync_objdiff.py:419` | `merged_%` | symbol | latent | delta 0 | **HARDENED** |
| 12 | `scripts/analysis/ceiling_calculator.py:335` | `merged_%` | symbol | latent | delta 0 | **HARDENED** |
| 13 | `scripts/analysis/reclassify_at_limit.py:108-110` | `merged_%`, `fn_%`, `%stlpmtx_std::%` | symbol/demangled | latent | delta 0 | **HARDENED** |
| 14 | `scripts/certify_floor.py` (`like_prefix_clause`, lines 120-131, 212-215, 265-267, 466-468, 586-587) | `merged_`, `lbl_`, `fn_`, `??_`, SDK prefixes | symbol/unit | clean | — | **Already-fixed (wave 9)** — confirmed correct; do not re-touch |
| 15 | `scripts/atexit_fuzzy_verify.py:94,96` | `??\_\_F%`, `merged\_%` | symbol | clean | — | **Already ESCAPEs** — confirmed |
| 16 | `scripts/reconcile_db.py:153` | `merged_` via Python `.startswith()` | symbol | clean | — | **Python startswith** — no SQL wildcard; confirmed |
| 17 | `scripts/progress_metrics.py` | (no symbol/demangled LIKE) | — | clean | — | **No SQL LIKE over symbols** — confirmed |

### Correct-by-design (parameterized user patterns — NOT bugs)

These pass a runtime `?` parameter where `%`/`_` are *intended* as wildcards, so
they are left as-is: `ai_advisor.py:109` (`demangled LIKE ?`),
`context_collector.py:1875` (`demangled LIKE ? OR symbol LIKE ?`),
`rb2_dwarf.py:419` (`name LIKE ?`), `database.py:1520/1537`
(name-resolution `LIKE ?`), `ceiling_calculator.py:346`,
`reclassify_at_limit.py:119,123`, `unicorn/query.py:42,70`,
`at_limit_rb3_candidates.py:132,133`, `analysis/reclassify_at_limit.py:119`
(glob→LIKE via `*`→`%` is deliberate), `sync_objdiff.py:415` / `database.py:763`
(`unit NOT LIKE/GLOB ?` over `SDK_UNIT_PREFIXES = ["default/xdk/","default/lib/binkxenon/"]`
— **no `_` in those prefixes**, safe).

### `unit LIKE '%xdk%'` family (no underscore → safe)

`get_progress.py:116,123,126,219`, `sync_match_percent.py:155`
(`'default/xdk/%'`), `at_limit_rb3_candidates.py` default `'%xdk%'`: the literals
contain no `_`, and `/` is not a wildcard. Safe; left unchanged.

## Summary

- **2 real bugs found + fixed**: `refresh_frontier.py` (108 `??` partials hidden
  from the unicorn frontier) and `batch_check.py` skip_boilerplate (146 real
  authorable rows over-excluded).
- **11 latent sites hardened** (measured delta = 0 today, but the same
  `_`-as-wildcard footgun that caused wave 9): all `merged_`/`fn_`/`stlpmtx_std::`
  literals now ESCAPE the literal underscore. Behaviour-preserving on the current
  DB; corrective for any future aliasing symbol.
- **4 already-clean confirmed** (certify_floor, atexit_fuzzy_verify, reconcile_db,
  progress_metrics).
- A shared `like_prefix_clause()` helper now lives in `orchestrator/database.py`
  (mirrors `certify_floor.like_prefix_clause`) so the fix has a single canonical
  form for future call sites.
- Regression test: `scripts/orchestrator/tests/test_like_prefix_escape.py` (7
  tests, all green).

### Python string-literal trap (caught + fixed during this work)

Inside non-raw Python strings (incl. `f"""..."""` and triple-quoted SQL),
`ESCAPE '\'` collapses to `ESCAPE ''` (empty) because `\'` is an escaped quote —
which SQLite rejects at runtime with *"ESCAPE expression must be a single
character"*. The correct source form is `ESCAPE '\\'` (double backslash) so the
rendered SQL has a single `\`. All edited triple-quoted/f-string SQL was
re-validated via AST extraction to confirm every `ESCAPE '...'` renders exactly
one character.
