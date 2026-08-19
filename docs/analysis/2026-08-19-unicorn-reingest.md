# Re-ingesting the unicorn oracle against the fixed harness — 2026-08-19

Branch `fix/unicorn-reingest-20260819`, base `eda64e956`. Harness + DB only;
`src/` and `include/` are untouched, so PPC codegen cannot have moved.

## What this was for

Eight harness defects were fixed on 2026-08-18/19 (merges `ccd4c8036`,
`897d0220f`, `0871d63df`). The `unicorn_*` columns in `decomp.db` had not been
re-measured against the fixed harness outside the ~1,830-row *authorable partial
frontier*, so roughly 26,000 rows — everything at 100% match, everything at 0%,
everything excluded — still carried verdicts from **2026-03**, produced by a
machine where `bl __savegprlr_N` zeroed `this` at the second instruction of the
prologue and 87.5% of functions spun to the instruction cap. Those rows were
what `query_functions(unicorn_verdict=...)` and `/unicorn-query` answered from.

Worse, nothing in the schema could *say* that. `unicorn_signal_version`
describes the **comparator semantics** and sat at 3 across the whole break,
because not one of the eight fixes touched a comparator rule. Date was the only
discriminator, and only for someone who already knew the dates.

## What changed

### 1. Provenance (the durable part)

`HARNESS_VERSION` now lives beside `SIGNAL_VERSION` in
`scripts/unicorn_runner/signal_version.py`, currently **4**, with the h1..h4
changelog written out — what each merge fixed and what it did to the numbers.
Two new columns (schema **v16**) carry it into the DB:

| column | meaning |
|---|---|
| `unicorn_harness_version` | h1..hN. NULL or 1 == pre-2026-08-18. Do not trust. |
| `unicorn_harness_build` | git short rev of the tree that measured it. |

`apply_refresh.py --stamp-legacy` stamps every row that still holds a verdict
but no harness version as h1, so **`WHERE unicorn_harness_version >= 4` is a
complete filter** rather than one that silently drops NULLs. Surfaced as
`query_functions(min_unicorn_harness_version=N)` and `query.py --min-harness N`;
every `query.py` summary now prints a harness-provenance block and marks
known-artifact classes inline.

### 2. `refresh_frontier.py --scope all`

Every refresh this project has run swept `0 < match% < 100` only. That is
precisely why 26k rows never got corrected — the tool could not reach them.
`--scope all` selects every diffable non-stub function *plus* every row that
already carries a verdict: 29,713 rows across 967 units, swept in **277 s** at
`-j 8`.

## Before / after

"Before" is the live DB at 2026-08-19 07:38, i.e. **already including this
morning's frontier-only refresh at h4**. The honest three-column history for the
partial frontier is below it.

### Whole DB

| verdict | before | after |
|---|---:|---:|
| EQUIVALENT | 25,634 | 16,989 |
| DIVERGENT | 2,148 | 12,419 |
| (no verdict) | 24,765 | 23,139 |

| DIVERGENT class | before | after | |
|---|---:|---:|---|
| `data_layout` | 423 | **9,815** | ARTIFACT by definition |
| `cap_exhausted` | 369 | 2,096 | ARTIFACT ~72% |
| `build_env` | 440 | 217 | ARTIFACT |
| `stack_layout` | 172 | 107 | ARTIFACT |
| `call_count` | 377 | 63 | ARTIFACT ~83% |
| `wild_jump_match` | 49 | 47 | not pinpointed |
| `merged_call` | 65 | 22 | ARTIFACT |
| `return_value` | 15 | 10 | see below — 6 of 10 are false |
| `orig_error` | 117 | 8 | ARTIFACT (target's own fault) |
| `cap_exhausted_decomp` | 14 | 8 | ARTIFACT ~97% |
| `object_memory` | 15 | 7 | |
| `cap_exhausted_orig` | 6 | 6 | ARTIFACT ~98% |
| `regalloc` | 19 | 6 | ARTIFACT |
| `call_arg` | 36 | 4 | |
| `merged_arg` | 5 | 1 | ARTIFACT |
| `unmapped_access_mismatch` | 3 | 1 | SUSPECT (ctor-returns-`this` stub) |
| `fpr_precision` | 6 | 1 | ARTIFACT |
| **`error` (decomp_error)** | **17** | **0** | |

**99.4% of the post-ingest DIVERGENT population is in a known-artifact class.**

The DIVERGENT count going *up* 6x is not a regression and is not new bugs. It is
9,392 extra `data_layout` + 1,727 extra `cap_exhausted` rows at 100% match, all
of which previously read `EQUIVALENT` on the strength of a 2026-03 run that
never executed the function bodies. `data_layout` means "the only differing
values are addresses the harness assigns independently per side" — the
comparator's own docstring calls it `ARTIFACT — do not chase these as decomp
bugs`. On a function whose assembly is byte-identical to the target it is
*structurally* impossible for it to be anything else.

### Partial frontier (0 < match% < 100) — three-column history

| | h1/h3 (live DB at 00:49 today) | h4 (07:32 frontier refresh) | h4 scope=all (this ingest) |
|---|---:|---:|---:|
| EQUIVALENT | 472 | 911 | 909 |
| DIVERGENT | 1,366 | 935 | 937 |
| `cap_exhausted` | 1,037 | 328 | 328 |
| `data_layout` | 126 | 423 | 428 |

**Be blunt about attribution: the frontier was already re-ingested before this
lane started** (2026-08-19 07:32:52, build `eda64e956`, 1,825 rows). The
frontier columns above move by ±5 rows, which is run-to-run noise in the
`data_layout` / `orig_error` boundary, not a finding. This lane's actual
contributions are the 100%-band re-ingest (~27,500 rows that had never been
re-measured), the provenance schema, and the analysis below.

### Coverage

29,350 of 29,408 verdict-carrying rows are now h4. The 58 stragglers are stamped
h1 and are visible as such:

* 46 EQUIVALENT, 10 `call_count`, 1 `merged_call`, 1 `orig_error` — rows whose
  symbol is no longer present in both COFF objects (renamed, folded, or removed),
  so the sweep returns SKIPPED and there is nothing to overwrite.

## `decomp_error`: zero rows. Chased anyway.

`decomp_error` — our side faults where the original doesn't — is the
highest-precision signal in the oracle, and the one candidate-bug flip in the
2026-08-18 sweep (`MemDiffEntry::operator<`, missing secondary sort key) came
from it. **The fixed harness produces zero `error`-class rows across all 29,702
swept functions.**

All 18 rows that previously carried `unicorn_class='error'` were re-measured (17
of them DIVERGENT, 1 a stale EQUIVALENT that kept the class). Every one is at
100.0% match, and every one resolved to EQUIVALENT (12) or to an artifact class
(6: five `data_layout`, one `build_env`). Among the twelve that went EQUIVALENT is `??$__linear_insert@PAUMemDiffEntry@@...`, i.e. the STL
cluster the `MemDiffEntry::operator<` fix already repaired — which is the
control that says the signal is still capable of registering a real bug when one
exists, and simply has none left to register.

The eight `verdict='ERROR'` rows are all one unit, `system/utl/GlitchFinder`, and
all one message: `rdata buffer is 0x175d8 bytes, exceeds the 0x10000 RDATA
window`. That is a harness capacity limit introduced by the RDATA clamp in
`0ba1e226f`, not a decomp defect. Those functions are simply unmeasured.

## Residual harness defects identified (not fixed here)

### Defect 9: `return_value` does not know the function's return type

`compare_results` compares `r3` unconditionally. It has no access to the
symbol, so it cannot tell a function that *returns* an integer from one whose
`r3` is dead at return. Of the 10 surviving `return_value` rows:

| match% | signature |
|---:|---|
| 61.99 | `private: void CSHA1::Transform(unsigned int *, unsigned char const *)` |
| 82.43 | `private: float FreestyleMoveRecorder::CompareSkeletonJointDisplacement(...) const` |
| 85.26 | `void KeyChain::getMasher(unsigned char *)` |
| 86.48 | `public: void EQEffect::Reset(void)` |
| 100.0 | `void getKeyImpl(unsigned char *, char *, unsigned char *)` |
| 100.0 | `struct BreedData * GetBreedData(int)` |
| 100.0 | `rijndael_ecb_encrypt` / `rijndael_ecb_decrypt` / `rijndael_test` |
| 100.0 | `public: unsigned int Trie::get_free_node(void)` |

Four return `void` and one returns `float` (value lives in `f1`, not `r3`). All
five are false positives, and **all four partial-band rows in the class are
among them** — so the class contributes nothing actionable to the frontier at
all. Fixing it means threading the symbol name into `classify_divergence`,
which changes the signature and every caller. Cheap, but worth 5 rows.

### Defect 10 (the ctor-returns-`this` stub) — evaluated, not worth it

The brief flagged the trampoline stub's `li r3,0; blr` breaking the
ctor-returns-`this` ABI contract as a possible cheap win, on the same mechanism
as the defect-8 fix. It is real, but after the helper fix there is **exactly one
`unmapped_access_mismatch` row left in the entire 29,408-row corpus**
(`FaceCenter` in `system/rndobj/Mesh`, 94.74%). The blast radius does not
justify the work; recorded here so nobody re-derives the idea from the class
name.

## Honest limits on what an EQUIVALENT here means

Unchanged from the helper-fix lane, and still true:

* **411 of 660 completions log zero calls.** Zero-fill makes loop bounds 0, so a
  function whose calls all live inside a loop runs it zero times. Its EQUIVALENT
  says only "both sides skipped the loop identically."
* **On the frontier, ~189 of 898 EQUIVALENT rows rest on the "both sides hit an
  identical error at an identical PC" rule**, not on both sides completing (the
  helper-fix lane counted this directly). That rule predates all eight defect
  fixes and none of them changed it. The 100%-band was not counted this way, so
  assume a similar fraction there.
* `data_layout` and `cap_exhausted`, the two classes that now hold 96% of all
  DIVERGENT rows, are statements about the emulator's address assignment and
  instruction budget. Neither is evidence about our source.

## Net finding

After the fixed harness, on the population that matters (the partial frontier),
the oracle pinpoints **at most 7 rows** in a real-bug class — 4 `return_value`
(all void/float, i.e. all false), 1 `call_arg` on a constructor, 1
`unmapped_access_mismatch` (known stub artifact), and ~3-4 real ones hiding in
the 22 `call_count` rows at the class's measured 17% real rate. Zero
`decomp_error`, zero `logic`, zero `object_memory`.

**The unicorn oracle is, for practical purposes, exhausted as a bug finder on
this codebase.** Its remaining value is as a *regression* detector — re-run it
after a change and look for new `error`-class rows — and that is what the
provenance columns are for: a verdict you cannot date to a harness version
cannot serve as a baseline.

## Disagreement with `docs/analysis/unicorn-full-resweep-20260819.md`

A parallel lane ran essentially the same whole-binary sweep the same hour and
landed on main as `452d0cc3c` with the opposite decision: **it deliberately did
not apply**, on the grounds that writing 12.8k rows dominated by `data_layout`
"over 25,628 curated EQUIVALENT verdicts would cost the oracle its signal for no
gain." Its numbers and mine agree to within run noise (their 12,862 DIVERGENT /
9,960 `data_layout` vs my 12,419 / 9,815 — they used `batch_to_db --force` over
30,058 rows, I used `refresh_frontier --scope all` over 29,702). **This lane
applied it.** That disagreement should be visible, not smoothed over.

The case for applying:

* The 25,628 EQUIVALENT verdicts were not curated. They are a single 2026-03-04
  batch run on the harness that zeroed `this` in the prologue. "Do not overwrite
  them" preserves a number nobody has grounds to believe.
* Both states are uninformative at the 100% band. The difference is that
  `DIVERGENT/data_layout/h4` is *labelled* uninformative and a stale
  `EQUIVALENT` with no harness column is not. Provenance is what makes the
  overwrite safe, and it did not exist until this branch added it.
* **All 49 rows of that lane's harvest are in this ingest, with matching
  classes** (3 `call_arg`, 32 `call_count`, 7 `object_memory`, 6
  `return_value`, 1 `wild_jump_match`). Applying turns their JSON worklist into
  a live query:
  ```sql
  SELECT symbol FROM functions
   WHERE unicorn_harness_version >= 4
     AND unicorn_class IN ('call_arg','object_memory','return_value')
     AND match_percent_normalized >= 100;
  ```
  Not applying leaves those leads in a file that will rot.

The case against, taken seriously: `unicorn_verdict = 'DIVERGENT'` is read by
`sync_objdiff.py` (default `--only-divergent`), `batch_promote.py` and
`certify_floor.py`. Checked: `sync_objdiff` scans 6x more functions (slower, not
wrong); `batch_promote` and `certify_floor` only act on partial functions, whose
rows moved by ±5. No downstream behaviour changes incorrectly. That lane's
closing recommendation — "`batch_to_db --force` should refuse to write a live
DB" — remains sound and is untouched here; this ingest went through
`apply_refresh.py`, which is the reviewed single-writer path.

**To revert the DB half of this lane without touching the tooling half:**

```sh
scripts/backup-db.sh                       # current state, before undoing
xz -dc ~/code/db-backups/decomp.db.2026-08-19.pre-unicorn-reingest.xz > /tmp/pre.db
# then restore wholesale, or re-apply only the frontier results from it
```

That archive is deliberately named apart from the dated one:
`scripts/backup-db.sh` names its output `decomp.db.<date>.xz` and **overwrites
today's file on every run**, so the plain dated backup would be destroyed by the
next agent who follows the standing "back up first" rule.

The schema, the `HARNESS_VERSION` changelog, `--scope all`, `--min-harness`, the
MCP enum fix and the artifact annotations are independent of that choice and
should survive either way.

## Reproduce

```sh
scripts/backup-db.sh                                     # ALWAYS first
python3 scripts/unicorn/refresh_frontier.py --run --scope all -j 8 \
    --out-db /tmp/uni-reingest/full.db --json /tmp/uni-reingest/full.json
python3 scripts/unicorn/apply_refresh.py \
    --results /tmp/uni-reingest/full.db --stamp-legacy        # dry-run
python3 scripts/unicorn/apply_refresh.py \
    --results /tmp/uni-reingest/full.db --stamp-legacy --apply
python3 scripts/unicorn/query.py --summary-only
python3 scripts/unicorn/test_refresh.py                  # 16 tests
python3 -m pytest scripts/unicorn_runner/tests/          # 203 passed, 15 skipped
```

Unicorn needs RWX `mmap`; run unsandboxed.
