# Cert rot in decomp.db — audit, cause, and repair (2026-08-17)

Task #101, dc3-decomp, main repo. Data files: `cert-rot-audit-20260817/`.

`functions` has no audit trail for `verdict`, so the full before/after list for
every row this lane moved is written out here rather than being recoverable
from the database:

| file | rows | what |
|---|---|---|
| `cert-rot-audit-20260817/demoted.json` | 935 | `COMPLETE` -> `NULL` |
| `cert-rot-audit-20260817/promoted.json` | 89 | `AT_LIMIT`/`NULL` -> `COMPLETE` |
| `cert-rot-audit-20260817/phantom_excluded.json` | 1,995 | `excluded=0` -> `excluded=1` |

Each record carries `id`, `unit`, `symbol`, `size`, `from`, `to`, the
`current_percent` it held before, and the value it holds now. Nothing was
deleted: `attempt_count`, `source_patch`, and every row of `attempts` are
untouched, so a demoted row still carries its whole working history.

DB backup taken before any of this: `~/code/db-backups/decomp.db.2026-08-17.xz`
(22 MB) and `permuter_cache.db.2026-08-17.xz` (58 MB), via
`scripts/backup-db.sh`.

## Starting state

Joining every non-excluded row to a freshly built `report.json` on
`(unit, symbol)`:

- **875** rows verdict `COMPLETE` and **not** at 100 % normalized
  — 540 in the 90–99.99 band (143,888 B) and **335 at literal 0 %** (35,104 B)
- **73** `AT_LIMIT` and **11** null-verdict rows now at 100 %
- **2,135** non-excluded rows with no counterpart in the report at all, 1,950
  of them `merged_<addr>` and 45 `fn_<addr>`

## Why the certs rotted — two causes, not one

### Cause 1 (the 335 at 0 %): sync could not see them

`scripts/sync_match_percent.py`'s `load_report()` did

```python
pct = fn.get("fuzzy_match_percent")
if pct is None:
    continue
```

A report entry with no `fuzzy_match_percent` is not an entry with missing data.
It is objdiff saying **the base object defines nothing to pair the target
symbol with**, so there is no fuzzy score to compute; the entry still carries
`match_percent_normalized: 0.0`. `continue` meant such a row was never written
and — crucially — never *demotable*, so `current_percent` kept whatever it last
held from a build where the pairing did exist. Measured on main: 847 non-SDK
entries lack the field, **395 of them sat at `current_percent` 100 with verdict
`COMPLETE`** (335 non-excluded — exactly the 0 % cohort — plus 60 excluded).

**Classification of all 335, checked against the COFF objects, not inferred:**

- the **target** object defines the symbol in **335 / 335** cases
- **our** object defines it in **0 / 335**
- `??_E`/`??_G` weak-external spelling differences: **0 / 335**

So this is **not** a pairing artifact and **not** a symbol rename. There is no
alias that repairs it and no DB rename that would either. It is a real,
metric-invisible fidelity gap: *the target emits a function out of line and our
build does not emit it at all*. Sub-shapes: 239 template/STL instantiations our
TU never instantiates (`??$PropSync@VRndTransformable@@@@…`,
`?allocate@?$StlNodeAlloc@…`), 96 plain functions we inline or never wrote
(`?MakeRotMatrixX@@YAXMAAVMatrix3@Hmx@@@Z` and its Y/Z siblings,
`??_H@YAXPAXIHP6APAX0@Z@Z`).

Two spot-checks with `run_objdiff` agree: `??_GHamSong@@UAAPAXI@Z` and
`??0NetLoaderRef@@QAA@ABU0@@Z` both come back *21 total | 21 insert*, i.e. every
target instruction inserted and nothing on the base side — objdiff's own verdict
is `Stub`.

118 of the 335 have `attempt_count > 0` and there are 194 `attempts` rows
against them, so they were worked before the pairing was lost. That history is
why they are demoted rather than removed.

### Cause 2 (nine of the rest): rounding reached 100 from below

`load_report()` stored `round(pct, 2)`, and the stored number is not a display —
`--promote` and `--demote` both compare it against 100. `round(99.9967, 2)` is
`100.0`, so nine functions kept a `COMPLETE` cert while measurably not matching.
They are real residuals, not noise: `?Save@ObjectDir@@UAAXAAVBinStream@@@Z` at
99.9967 has two `diff_arg` instructions, an offset swap of `0x58` against `0xa4`.
Only 10 rows binary-wide sit in `[99.995, 100)`.

The remaining 533 of the 875 were ordinary rot — certs granted or left standing
while the code moved — and needed no diagnosis beyond a fresh measurement.

## What was changed

**`scripts/sync_match_percent.py`** — two fixes, both in `load_report`:

1. an entry with `match_percent_normalized` but no `fuzzy_match_percent` is
   scored **0.0** instead of skipped;
2. `_round_pct()` keeps the 2-decimal convention but clamps to 99.99 rather than
   letting a sub-100 value round up to 100.

Expect a one-time spike in the run's `Regressions` counter against any database
written under the old rules — 415 on the run that landed this.

**Repair sequence and what each step fixed** (all against the same fresh
report):

| step | promoted | demoted | note |
|---|---|---|---|
| `--build --promote` (the documented resync) | 89 | — | clears **all** 73 `AT_LIMIT`-at-100 and the 11 null-verdict-at-100; 5 more were excluded rows |
| `--demote` | — | 533 | the ordinary 90–99.99 rot |
| `--promote --demote` after fix 1 | 0 | 393 | the unpaired cohort sync had never been able to see |
| `--promote --demote` after fix 2 | 0 | 10 | the round-to-100 band |

**Final state: 0 `COMPLETE` rows below 100 % normalized, 0 `AT_LIMIT` or
null-verdict rows at 100 %.** Demoted total 935 rows / 181,108 B (393 unpaired /
35,828 B; 542 in the 90–99.99 band / 145,280 B). Demotions concentrate in
`default/link_glue` (47), `system/rndobj/Utl` (31), `system/world/LightPreset`
(29), `system/hamobj/HamDirector` (20).

Demotion target is `NULL`, which is what `--demote` has always used and what
`query_functions`' default `status='workable'` treats as workable — a demoted
row goes back in the queue rather than into a new state nothing reads.

## The phantom rows: excluded, not deleted

1,995 non-excluded rows whose symbol is `merged_<addr>` or `fn_<addr>` **and**
which have no counterpart in `report.json` are now `excluded=1` with an
`exclusion_reason` naming this audit. `merged_<addr>` is the linker's name for
an `/OPT:ICF` fold survivor and `fn_<addr>` an MSVC EH funclet; no source file
can be written to match either, and objdiff emits no report entry for them, so
they can never be measured, promoted, or demoted. They were inflating
`AT_LIMIT` by 1,876 — **more than half of it**: the non-excluded `AT_LIMIT`
count falls from 3,554 to 1,678.

`excluded` was chosen over a new flag because the precedent is exact: the wave-9
link-glue exclusions use the same criterion (*absent from report.json*) and the
same mechanism, `exclusion_reason` already carries free-text provenance, and 18,987
rows were already excluded, so every consumer honours it. A new column would
have to be taught to each of them. Published progress numbers are unaffected —
`scripts/progress_metrics.py` reads `report.json`, and the DB is a separate data
plane.

Breakdown: 1,876 `merged_` `AT_LIMIT`, 74 `merged_` `COMPLETE`, 43 `fn_` null,
2 `fn_` `COMPLETE`.

## Left open, deliberately

**140 non-excluded rows are absent from the report but are *not* name-shaped
phantoms** and were left alone. Their units *are* in the report, so this is
per-symbol drift and not a missing unit: `system/utl/DebugGraph` has 36 DB rows
against 6 functions in the report, `link_glue` 18, `char/CharEyes` 7. 108 of
them are `COMPLETE`. Bulk-excluding them on the same criterion would be the easy
move and the wrong one — a symbol that vanished from the report may have
vanished because our build stopped emitting it, which is the *Cause 1* gap and a
real bug rather than bookkeeping. They need the same per-object check the 335
got.

**`current_percent` remains untrustworthy for any row absent from the report.**
Sync reports these but never mutates them, by design. `query_functions` now
marks `is_stub` rows explicitly for this reason.
