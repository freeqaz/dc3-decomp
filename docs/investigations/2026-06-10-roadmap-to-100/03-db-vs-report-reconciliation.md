# 03 — decomp.db vs report.json Reconciliation

## Question

The two progress planes disagree. Snapshot facts to reconcile:
`decomp.db functions` = 52,504 rows; `report.json total_functions` = 48,413.
`db current_percent>=100` = 31,056; `report matched_functions` = 29,236.
`db verdict=COMPLETE` = 31,076, yet only 31,056 rows are at `current_percent>=100` (a 20-row gap).

Goals: (1) read `sync_match_percent.py` and find what drifts; (2) produce the exact
delta ledger db-vs-report by symbol; (3) compute THE authoritative remaining-work
number both ways and reconcile to ~1%; (4) find db-internal contradictions
(verdict vs percent, is_stub vs percent, best>current regressions = "lost wins");
(5) recommend a canonical state query + a drift-detection tool.

## Method (commands run)

- Read `scripts/sync_match_percent.py` end to end.
- `sqlite3 'file:decomp.db?mode=ro'` for all DB aggregates (read-only, never wrote).
- `python3` streaming `build/373307D9/report.json` (15 MB), coercing `measures`
  values (they are JSON **strings**, not numbers).
- Symbol-level join: pickled `{report_all, db}` dicts to `/tmp/join.pkl`, joined on
  the COFF `symbol` / `name` key (exactly what sync joins on).
- 4 fresh `mcp__orchestrator__run_objdiff` calls to ground-truth suspicious rows
  (`DepthBuffer3D::DrawShowing`, `PlatformMgr::Poll`, `MoveDir::UpdateOverlay`,
  `AnimPtr::~AnimPtr`).

## Findings

### F1 — What sync_match_percent.py syncs, and what it CANNOT fix

`load_report()` (lines 61–97) iterates `units[].functions[]`, **skips units under
`default/xdk/`** (`SDK_UNIT_PREFIXES`, line 33), and **skips any function whose
`fuzzy_match_percent` is None** (line 85). It keys the result dict by `fn["name"]`
(the raw mangled COFF symbol). `sync()` (line 251) joins by exact `symbol` string —
**no demangling-based matching, no fuzzy matching**. Consequences:

- Functions in DB but not in report are counted (`not_in_report`, line 304) but
  **never modified** — their `current_percent`/`verdict` go stale forever.
- The percent UPDATE (lines 310–317) does `best_percent = MAX(best_percent, new)` —
  ratchets up only, so `best_percent` can permanently exceed a regressed `current`.
- Promotion to COMPLETE (line 297) is **one-way and `--promote`-gated**; it never
  demotes a COMPLETE whose percent later drops. This is the source of the 20-row gap.
- SDK code is structurally invisible to the DB sync, so any "matched %" headline that
  includes SDK total_code understates real progress (see F5).

### F2 — Symbol join is clean; the surplus is db-only, not a mismatch

Distinct report symbols = 48,362; **every one is present in the DB** (report-only = 0).
DB distinct symbols = 52,504 → **db-only = 4,142**. `report total_functions=48413` =
48,362 distinct + **51 duplicate-symbol entries** (same symbol in two units; all 51
duplicates are `default/link_glue` copies carrying `fuzzy_match_percent=None`, so sync
skips them and there is **no overwrite hazard** — verified: 0 non-None pct overwrites).

Units align: report has 2,054 units, db has 2,055; the only db-only unit is
`default/xdk/LIBCMT/stricmpp` (an SDK unit, irrelevant to work).

### F3 — Delta ledger (the 4,142 db-only rows), counts + byte sums

| bucket | count | bytes | @>=100% |
|---|---:|---:|---:|
| `1_xdk_unit` (SDK, skipped by sync) | 834 | 95,536 | 0 |
| `2_icf_merged` (`merged_*` ICF aliases) | 1,971 | 163,896 | 88 |
| `3_excluded_nonxdk` (excluded=1, mostly `link_glue`) | 1,172 | 57,308 | 1,170 |
| `4_eh_funclet` (`fn_<addr>`) | 2 | 116 | 2 |
| `5_other_nonexcluded` | 163 | 12,520 | 108 |
| **TOTAL** | **4,142** | **329,376** | |

The dominant bucket is **ICF-merged aliases**: report.json represents an
Identical-COMDAT-Folding group by a single canonical symbol; the DB keeps one row per
folded alias. There are 2,725 db-only `merged_*` symbols total (some in xdk units);
**only 49 `merged_*` symbols appear in report** (the canonical winners). So ~2,675
DB `merged_*` rows are alias bookkeeping with no report counterpart by design — not
drift, not dead rows. `link_glue` excluded rows (1,326 in the DB labelled
"Linker glue (duplicates)") are deduped out of report as well.

### F4 — The 20-row COMPLETE-vs-percent gap is fully explained (and directional)

`verdict=COMPLETE AND current_percent<100 (or NULL)` = **20**.
`verdict!=COMPLETE AND current_percent>=100` = **0**.
20 − 0 = 20 = (31,076 COMPLETE − 31,056 at >=100). Exact.

These 20 are **stale COMPLETE verdicts**: functions promoted to COMPLETE, then their
`current_percent` regressed (e.g. `?Handle@UIList@@` 99.998%, `?Handle@Automator@@`
99.996%, `?PostLoad@ObjectDir@@` 99.998%) without a re-sync demotion — because sync
never demotes (F1). All but ~3 are knife's-edge (>=99.9%); 2 are real drops
(`?DrawBeatLine@?A0xe50ea9df@@` at 89.5%). Low byte impact (~25 KB) but a
**correctness bug in the COMPLETE count**.

### F5 — Authoritative remaining-work, both planes, reconciled to 0.27%

report.json `measures` are strings; coerced to float:
- `total_code` = 11,379,348; `matched_code` = 4,983,704 → **headline 43.8% matched**.
- **But `total_code` includes 4,951,700 bytes of SDK (xdk) code that is 0.0% matched**
  (`SDK matched bytes` = 1,723) and is explicitly out of scope. The 43.8% headline is
  diluted by 4.95 MB of code the project will never touch.

**Non-SDK plane (the real frontier):**
- total non-SDK function bytes = **6,427,648**
- bytes in functions <100% = **1,444,832** (report) / **1,444,264** (re-summed by join)
- fuzzy-matched bytes = 6,043,160 → **~94% of in-scope bytes are fuzzy-matched**.

**DB plane** (`current_percent<100 OR NULL, excluded=0, unit NOT LIKE default/xdk/%`):
- = **1,440,944 bytes** across 4,383 rows (1,311 of which are NULL-percent rows,
  111,748 bytes — the never-synced merged/db-only rows).

**Reconciliation:** report-remaining 1,444,264 vs db-remaining 1,440,944 →
**Δ = −3,320 bytes (0.23%)**. The DB carries 163,340 bytes of db-only rows with no
report counterpart (merged aliases) **yet still nets lower**, because the DB's
in-report subset (1,277,604) is **166,660 bytes lower** than report's number — i.e.
the DB has stale-HIGH current_percent for some report functions. See F6.

> **The single authoritative remaining-work number is ≈ 1.44 MB of non-SDK code in
> sub-100% functions** (both planes agree within 0.23%). NOT the 6.4 MB the headline
> "56% unmatched" implies — that figure is 4.95 MB SDK + 1.44 MB real.

### F6 — The real measurement-drift bug: 639 FALSE-COMPLETE rows in the DB

Comparing `db.current_percent` vs `report.fuzzy_match_percent` for shared non-SDK
symbols (|Δ|≥0.5):
- DB **higher** than report: 660 fns, 51,396 bytes.
- of those, **DB>=100 but report<100 (FALSE-COMPLETE): 639 fns, 47,232 bytes.**
- DB **lower** than report: **0 fns** — the DB never lags below report.

The drift is one-directional (DB optimistic, never pessimistic), confirming sync's
ratchet/sticky-COMPLETE behaviour. The FALSE-COMPLETE symbols are
templates/EH-funclets/anon-namespace artifacts: `??1AnimPtr@@UAA@XZ`,
`??$MakeString@...`, `??__F...`, `??_G...`, `_M_erase@...`. **Verified by
run_objdiff: `??1AnimPtr@@UAA@XZ` is a genuine stub (16 insert, 0% real) but the DB
marks it COMPLETE/100%.** These are the objdiff-v4.2.0 funclet/COMDAT pairing
artifacts — the COFF symbol identity changed under them and report re-scored to 0%,
but the DB kept the prior COMPLETE. **~47 KB of the "31,056 done" count is fictional.**

### F7 — `best_percent` is NOT an "achieved-in-this-tree" signal — do not mine it for lost wins

`best_percent > current_percent` (excluded=0, non-xdk) = **1,982 rows / 1,019,532
bytes**, which looks like a huge pile of recoverable "lost wins." It is mostly false:
- `best_percent>=100 AND current_percent<100` = 1,566 rows. **1,410 of these are
  AT_LIMIT** — internally contradictory (AT_LIMIT means it provably cannot reach 100
  in this tree, so best=100 cannot have been achieved here). Only 19 are COMPLETE.
- `best=100 AND current=0` = 672 rows; **867 of all best=100/cur=0 rows have
  is_stub=1** — pure stubs that nonetheless carry best=100.
- **Ground-truthed:** `DepthBuffer3D::DrawShowing` (best=100, cur=0) is a 1,297-insert
  stub; `PlatformMgr::Poll` (best=100, cur=0) is a 1,211-insert stub;
  `MoveDir::UpdateOverlay` (best=100, cur=86.3) is the known AT_LIMIT regswap case —
  run_objdiff says 86.1%, never 100. In all three the DB best=100 is provably bogus.

**Conclusion:** `best_percent` was seeded from an external ground-truth (og-dc3 / RB3 /
a different export) where these functions are 100%, not from a match actually achieved
in *this* source tree. The MAX() ratchet then froze it. The **only credible recovery
class** is `best in [99,100) AND current<best` = **130 fns / 93,812 bytes** (plausible
real ratchet-down regressions) — the rest of the 1 MB is noise.

### F8 — is_stub vs percent contradictions (mis-routing hazard)

- `is_stub=1 AND current_percent>=100` = **1,728 rows / 82,664 bytes** — a function
  cannot be both an unimplemented stub and 100% matched. `is_stub` is stale: the
  function got implemented but the flag was never cleared.
- `is_stub=1 AND 0<current<100` = 51 rows.
- `is_stub=1 total (non-excl, non-xdk)` = 2,433 rows / 221,644 bytes — but ~71% of
  them (1,728) are already at 100%, so `query_functions is_stub=true` over-reports the
  real stub backlog by ~3×. The trustworthy "true stub" set is
  `is_stub=1 AND current_percent<100` ≈ 705 rows.

### F9 — Exclusion plane sanity

`excluded=1` = 18,956 rows (not the implied small number). Reasons:
unlabelled 15,785 (4.97 MB, ≈ the xdk bulk), "Linker glue (duplicates)" 1,326
(67 KB), "XDK/SDK runtime library" 979 (71 KB), "Third-party library" 866 (301 KB).
16,764 excluded rows are xdk-unit rows. The excluded set correctly removes
non-authorable code from any remaining-work sum (used in F5).

## Implications for the roadmap

1. **Headline metric is misleading.** "43.8% matched / 56% remaining" includes 4.95 MB
   of unreachable SDK. The honest in-scope figure is **~94% of 6.43 MB fuzzy-matched,
   ≈1.44 MB remaining**. Publish the non-SDK number as primary.
2. **The "done" definition needs the FALSE-COMPLETE correction.** 639 DB rows / 47 KB
   counted as 100% are stubs/funclets that report scores 0%. The true matched-function
   count is ~639 lower than the DB's 31,056. "Done" should be defined off report.json
   `fuzzy_match_percent==100` for non-SDK, non-excluded, with `merged_*` aliases folded
   to canonical — NOT off `verdict=COMPLETE` (stale) or DB `current_percent` (optimistic).
3. **Do not chase `best_percent` regressions as a recovery lane.** It is externally
   seeded and ~93% noise. Only the 130-fn `[99,100)` band is worth a look.
4. **Re-sync hygiene:** sync must run with a demote path (or COMPLETE must be derived,
   not stored) or the 20-row + 639-row stale-COMPLETE rot will keep growing.

## Tooling gaps found

- `sync_match_percent.py` **never demotes** COMPLETE and **never lowers** a row whose
  report counterpart regressed/disappeared → permanent optimistic drift (F4, F6).
- No reconcile/CI check that report.json and decomp.db agree → 639 FALSE-COMPLETE +
  20 stale-COMPLETE went undetected.
- `best_percent` semantics are undocumented and externally polluted; consumers treat it
  as "achieved here" and would waste effort (F7).
- `is_stub` is not maintained on implementation → 1,728 stale stub flags (F8).
- `measures` values in report.json are JSON strings — any consumer doing arithmetic
  without coercion (like a naive dashboard) silently breaks (`TypeError`/`ValueError`).

## Recommended canonical state query + drift detector

Canonical "state of the decomp" (run against report.json, fold merged aliases):
```sql
-- DB-side canonical (use ONLY as a cache of report; truth = report.json):
SELECT
  COUNT(*)                                                        AS in_scope_fns,
  SUM(size)                                                       AS in_scope_bytes,
  SUM(CASE WHEN current_percent>=100 THEN 1 ELSE 0 END)          AS matched_fns,
  SUM(CASE WHEN current_percent<100 OR current_percent IS NULL
           THEN size ELSE 0 END)                                  AS remaining_bytes
FROM functions
WHERE excluded=0
  AND unit NOT LIKE 'default/xdk/%'
  AND symbol NOT LIKE 'merged_%';        -- fold ICF aliases
```
Nightly `reconcile.py` that **fails loudly** if any of:
- a symbol's `db.current_percent` differs from `report.fuzzy_match_percent` by ≥0.5
  for any shared non-SDK symbol (catches the 639 FALSE-COMPLETE),
- any `verdict=COMPLETE AND current_percent<100` (catches the 20),
- any `is_stub=1 AND current_percent>=100` (catches the 1,728),
- report-only symbols > 0 (catches jeff boundary churn dropping DB rows).
Wire it as a ninja-postbuild step or a pre-commit guard on `decomp.db`.
