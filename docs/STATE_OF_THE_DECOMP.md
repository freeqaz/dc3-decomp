# State of the Decomp

**Measured 2026-08-19** against `eda64e956`, from a clean full `ninja` in a
worktree, with objdiff-cli `4.2.3` (`88b425bc3bad-dirty`,
`functionRelocDiffs=name_check`), plus queries against the shared `decomp.db`.
Each section says how to regenerate it. If you are reading this more than a
month after that date, regenerate before quoting.

> `main` moved to `2184a9641` during the audit that refreshed this file. That
> commit's link-glue dedup drops 11 shadow rows from the authorable
> denominator, so on `main` today the MATCHED headline reads **91.39 %
> (29,430 / 32,202)** rather than the 91.36 % / 32,213 recorded below. Same
> numerator, smaller denominator; nothing else moves. The DB-side numbers are
> from the live shared database and are identical either way.

> **Know which ruler you are quoting.** The canonical headline counts
> `match_percent_normalized == 100`, which **forgives register permutation**
> and benign relocation addends. It is *not* byte identity. `?roll@@YAHH@Z`
> (keygen_xbox) scores 100.0 on it and is counted as matched, and 8 of its 12
> instructions differ — every difference is register allocation. 395 authorable
> functions / 150,108 bytes are in that state, 1.34 % of the matched set. The
> ruler does not forgive wrong constants, offsets or vtable slots.

> This document exists because five predecessor status documents rotted into
> mutually contradictory headlines and had to be archived. The way to keep this
> one honest is to **regenerate it, not edit it**. Numbers here are outputs of
> commands, and the commands are printed next to the numbers.

---

## The two headline metrics (they are not the same population)

The single largest source of wrong numbers in this project is quoting one of
these as if it were the other. They count different things over different
denominators and they will never agree.

| | **MATCHED** | **DONE-WITH-CERTS** |
|---|---|---|
| Source | `build/373307D9/report.json` (objdiff) | `decomp.db` (`functions` table) |
| Question it answers | *Does our build produce the target's bytes?* | *Has a human or agent adjudicated this function?* |
| Denominator | 32,213 authorable functions | 31,425 non-excluded rows |
| Headline | **91.36 %** functions (29,430 / 32,213) | **COMPLETE 28,722 + AT_LIMIT 1,618** |
| Byte headline | **78.19 %** code bytes (4,959,996 / 6,343,156) | — (the DB's byte sums are not a build measurement) |
| Units | 420 / 967 authorable units complete (43.43 %) | — |
| Remaining | 2,783 fns / 1,233,052 bytes | 1,085 rows with no verdict |

**Always state the denominator.** "91.36 %" and "COMPLETE + AT_LIMIT = 96.55 %"
are both true and describe different facts: the first is a build measurement,
the second is a bookkeeping measurement that includes 1,618 rows certified
*unfixable*.

The DONE-WITH-CERTS denominator fell **33,560 → 31,425** on 2026-08-17/18, when
tasks #101 and #104 excluded ~2,135 rows that `report.json` can never score:
`merged_<addr>` ICF fold survivors, `fn_<addr>` MSVC EH funclets, stale split
spellings and unreferenced inline COMDATs. They were excluded, not deleted
(21,122 rows carry `excluded = 1` in total). Any DONE-WITH-CERTS figure recorded
before that date is on the old denominator and is not comparable to this one.

A third number floats around and should be labelled when used: the **XEX-total**
(XDK-diluted) headline of **60.91 % functions / 43.62 % bytes**. It divides by
the whole 11.37 MB image including 5.03 MB (44.2 %) of Microsoft XDK and RAD
Bink code that has no source in this repo and never will. It is permanently
capped near 56 % and is only useful for "how much of the shipped image do we
reproduce".

### Regenerating them

```bash
# MATCHED — build must be fresh, or you are measuring last week
ninja                                                  # rebuilds report.json
python3 scripts/progress_metrics.py                    # print
python3 scripts/progress_metrics.py --markdown \
        --out docs/PROGRESS_METRICS.md                 # regenerate the metrics doc
```

```sql
-- DONE-WITH-CERTS (sqlite3 decomp.db)
SELECT COUNT(*) FROM functions WHERE excluded = 0;                 -- 31,425
SELECT verdict, COUNT(*), SUM(size) FROM functions
WHERE excluded = 0 GROUP BY verdict ORDER BY 2 DESC;               -- COMPLETE / AT_LIMIT / NULL
```

`docs/PROGRESS_METRICS.md` is the generated artifact and carries the report's
build time, objdiff-cli version and relocation mode in its header. Do not
hand-edit it; `scripts/progress_metrics.py` hardcodes that path as its default
output.

### The link-glue pseudo-unit (2026-08-19)

`default/link_glue` is not a translation unit of the original binary. It holds
the ALTERNATENAME scaffolding this repo invented so the link resolves, and every
one of its 55 report rows scores 0 %.

Eleven of those rows name a symbol that **also** lives in a real unit at
normalized 100 % — `floor0_unpack`, `curlx_ultous`, `MemOrPoolFree`,
`FormatString::operator<<(size_t)`, and friends. report.json therefore contained
those functions twice, and the canonical headline counted each one once as
matched and once as unmatched. `progress_metrics.py` now drops exactly those
eleven and prints the count; the authorable denominator went 32,213 → 32,202 and
the headline 91.36 % → 91.39 %.

**The dedup is deliberately narrow.** The other ~36 glue rows have no real
counterpart and stay in the denominator, because some of them name genuine
unwritten work (`FormatString::operator<<` float/long/uint overloads,
`HDCache::Flush`, `HolmesClientPrint`) alongside the pure scaffolding
(`__link_glue_noop`, `_strnicmp`, `gethostbyname`, the curl/jpeg/zlib allocator
hooks). Dropping the whole unit would be one line and would hide real work —
`scripts/test_progress_metrics.py` exists mostly to hold that line, and three of
its six cases are negative controls that fail against exactly that mutation.

Note the curl / jpeg / zlib / Holmes members of that set are low-priority for
the native port; nobody should mine this list for work.

On the database side there was **no** defect: all 1,275 `default/link_glue` rows
in `decomp.db` already carry `excluded = 1`. What was wrong was
`reconcile_db.py`'s check (a), which compared percentages without honouring the
`excluded` flag that every other check honours via `is_authorable()`. It reported
a standing "11 drift items" against a correct database. That is worth
remembering as its own failure shape: **a check that never reads clean is a
check nobody reads**, and real drift would have sat unnoticed behind the
constant. Reconcile now exits 0 on a clean tree.

---

## The 2026-08 ruler change

**A −1.23 pp drop in the headline in 2026-08 was a measurement correction, not
lost code.** Nobody regressed 432 functions. The ruler changed.

Compared against the 2026-06-21 `PROGRESS_METRICS.md` snapshot, the authorable
function headline fell 1.23 pp (−432 functions) and the byte headline fell
2.14 pp. The cause is visible in `report.json`'s own `provenance` block:

```json
"tool_version": "4.2.3",
"tool_commit": "88b425bc3bad",
"diff_config": ["functionRelocDiffs=name_check", ...]
```

The report is now built with **`functionRelocDiffs=name_check`**; it used to be
built with `functionRelocDiffs=None`.

- Under `None`, a relocation was compared by *kind and addend only*. A
  `bl SomeOtherFunction` scored as a match provided the relocation had the same
  shape. Calling the wrong function was free.
- Under `name_check`, the **target symbol name** is compared too. A function
  whose instruction bytes are byte-identical to the target can now be charged
  purely because a call or data reference resolves to a differently-spelled
  symbol.

That is a strictly better ruler, and it exposed a population that the old ruler
could not see: **466 rows with zero instruction residual but relocation-name
charges**. Of those, **272 rows / 66,828 bytes are gated *only* by symbol-name
mismatches** — the code is right, the spelling of something it references is
not. The audit split those 272 into:

| | rows | what it is |
|---|--:|---|
| adjudicated real bugs | 96 | we genuinely reference the wrong symbol |
| missing data-fold aliases | 8 | retail folded two data COMDATs; we need the alias |
| unadjudicated | 115 | not yet classified |

**Consequences for anyone reading older numbers:**

1. Any headline recorded before 2026-08 is on the old ruler and is **not
   comparable** to a current one. Do not compute a delta across the boundary
   and call it progress or regression.
2. `name_check` residuals are a distinct, tractable work class — see
   [`decomp/REMAINING_WORK.md`](decomp/REMAINING_WORK.md#5-name_check-adjudication).
   They are usually not codegen problems at all.
3. The underlying investigation lives in `docs/analysis/`:
   `namecheck-residency-split-20260812/`, `namecheck-source-lanes-20260812.md`,
   `anon-namespace-hash-lane-20260812/`, `dc3-data-comdat-fold-20260812/`.
   Key methodological finding recorded there: use the shipped linker map
   `orig/373307D9/ham_xbox_r.map` (118,000 names, one line per symbol, folds
   stated outright) rather than `scripts/target_symbol_map.json`, which is a
   VA→name function over an ICF-folded link and silently omits every spelling
   that lost a fold vote.

---

## What the remaining 1.25 MB is made of

2,783 authorable functions / 1,233,052 bytes are below 100 % normalized. Joining
the fresh report against `decomp.db` by symbol (2,779 fns / 1,233,008 B resolve;
the residue is name-shape mismatch):

| Shape | fns | bytes | share of gap |
|---|--:|--:|--:|
| Certified **AT_LIMIT** | 1,618 | 924,696 | 75 % |
| **Unverdicted** (no adjudication at all) | 1,050 | 274,576 | 22 % |
| In the report, absent from the DB | 111 | 33,736 | 3 % |

Cross-cutting (these overlap the rows above, they are not extra): **452 fns /
112,260 bytes** are flagged `is_stub` and still non-zero, and **813 functions /
116,956 bytes sit at literally 0 %** — 9.5 % of the gap in bytes but **29 % of
the gap in functions**, which is usually a missing implementation rather than a
codegen fight.

**Cert rot is gone.** The 875-row "DB says COMPLETE, report says otherwise"
slice was closed by task #101: `verdict = 'COMPLETE' AND
match_percent_normalized < 100` now returns **zero** rows. Do **not** re-derive
this check from `current_percent` — that column holds the *fuzzy* percent, and
374 COMPLETE rows are below 100 fuzzy while sitting at exactly 100 normalized.
Quoting it would resurrect a phantom 374-row rot population that does not exist.

### By subsystem

Top authorable subsystems by remaining bytes:

| Subsystem | remaining bytes |
|---|--:|
| `rndobj` | 241,800 |
| `hamobj` | 199,480 |
| `char` | 115,940 |
| `synth_xbox` | 69,952 |
| `meta_ham` | 65,872 |
| `world` | 64,496 |
| `os` | 60,212 |
| `gesture` | 59,276 |
| `utl` | 53,988 |

Absolute bytes flatter the big subsystems. As a *fraction of its own code*, the
two worst are the reference-less deserts:

- **`synth_xbox` — 48.9 % unmatched** (69,952 of 142,928 bytes)
- **`rnddx9` — 43.8 % unmatched** (37,680 of 86,020 bytes)

Together they hold **271 stubs / 76,236 bytes**. These are Xbox-specific audio
and Direct3D 9 back-ends with no counterpart in the RB3 tree, so there is no
reference implementation to port from — they have to be reconstructed from the
target assembly. That work is largely *metric-invisible* in the sense that it
fixes real behaviour long before it moves a percentage.

---

## Stubs

`is_stub = 1`, non-excluded: **455 functions / 113,688 bytes**. These are bodies
we never wrote — the target has code, our source has `{}` or `return 0`.

By subsystem: `synth_xbox` 216, `os` 92, `rnddx9` 51. The largest single files:

| Unit | stubs | bytes |
|---|--:|--:|
| `default/system/os/PlatformMgr_Xbox` | 67 | 12,420 |
| `default/system/synth_xbox/ExternalMic` | 43 | 6,312 |
| `default/system/synth_xbox/Synth` | 41 | 9,280 |
| `default/system/synth_xbox/GranularSynth` | 15 | 3,872 |
| `default/system/rnddx9/ShaderMgr` | 13 | 1,304 |
| `default/system/synth_xbox/Mic` | 12 | 4,972 |
| `default/system/rnddx9/Tex` | 11 | 6,532 |
| `default/system/os/NetworkSocket_Win` | 10 | 1,436 |
| `default/system/synth_xbox/PitchDetector` | 9 | 2,168 |
| `default/system/rnddx9/Mesh` | 9 | 4,040 |

A stub's `current_percent` is the least trustworthy number in the table:
`scripts/sync_match_percent.py` drops any report entry with no
`fuzzy_match_percent`, which is every target-only symbol, so the column keeps
whatever it last held and can read 99.9 % for a function this build emits no
body for. `query_functions` now says so inline (`[STUB: no body emitted; % is
stale]`).

```sql
SELECT unit, COUNT(*), SUM(size) FROM functions
WHERE excluded = 0 AND is_stub = 1
GROUP BY unit ORDER BY 2 DESC LIMIT 20;
```

`is_stub` is one of the few DB columns that is reliably true — see
[`decomp/REMAINING_WORK.md`](decomp/REMAINING_WORK.md#columns-you-cannot-trust).

---

## How much to trust AT_LIMIT

**A certificate is a prior, not a proof.** AT_LIMIT means somebody concluded the
function cannot reach 100 %. Three independent lines of evidence say a large
minority of those conclusions are wrong.

**1. The ICF/funclet placeholders are no longer in the population.** The DB
holds 3,796 AT_LIMIT rows, but **2,178 of them are now `excluded = 1`** — 1,917
`merged_*` ICF fold survivors (synthetic names for addresses where the linker
folded identical machine code), the rest `fn_*` MSVC EH funclets and stale split
spellings. They are bookkeeping, not work, and `report.json` emits no entry for
them. Report-visible AT_LIMIT is **1,618 functions / 924,696 bytes**, of which
only 34 are still `merged_*`.

**2. Unicorn says 136 of them still behave differently from the target.**
Filtering AT_LIMIT rows to divergence classes that indicate *real* behavioural
difference (as opposed to build-environment or register-allocation artefacts).
Re-measured 2026-08-19 after the harness fixes and the oracle re-ingest; the
pre-fix figure was 640 fns / 466,036 bytes and was ~87 % harness artefact:

| class | fns | bytes |
|---|--:|--:|
| `cap_exhausted` | 71 | 37,716 |
| `wild_jump_match` | 40 | 32,896 |
| `call_count` | 20 | 13,896 |
| `return_value` | 3 | 6,708 |
| `cap_exhausted_decomp` | 1 | 1,132 |
| `call_arg` | 1 | 328 |
| **total** | **136** | **92,676** |

92,676 bytes is **10.0 % of all report-visible AT_LIMIT bytes** (924,696) — down
from the 41 % the pre-fix oracle claimed. A tenth of what is filed as "certified
unfixable" still carries a live signal that it is not merely unmatched but
*wrong*.

Across the whole non-excluded DB, real-class DIVERGENT is **2,205 functions /
479,528 bytes**.

**Read `EQUIVALENT` narrowly.** The oracle's two verdicts are not symmetric.
DIVERGENT is informative and well localised — it names the object offset, call
index or register. EQUIVALENT means only "no difference in r3/f1, in r3–r6 at
each logged call, or in the object and global regions, under a single
uniform-byte fixture". Audited 2026-08-19 with six deliberate sabotages: three
were caught, and three still reported EQUIVALENT — calling a *different*
function with identical arguments (detected, but emitted as a warning on the
EQUIVALENT path and never persisted, so `unicorn_reason` is NULL for all 16,989
EQUIVALENT rows); swapping two object fields in a subtraction (the object region
is filled with one repeated byte, so every field holds the same value and
field-confusion bugs are structurally invisible — this is the `RndFlare::Load`
bug class); and a change upstream of a fault both sides hit identically.
`--dual-fixture` does not rescue this: it varies the fill *byte*, not the
per-field values, so its `confidence=high` means "two blind fixtures agreed".

**3. A blind audit of the certificates failed.** On 2026-08-04 a blind sample of
regswap-class AT_LIMIT certificates scored **3 out of 10** — and that sample was
drawn at random, not from the three cases that had motivated the audit. The
label is not worthless, but it does not survive being leaned on.

**3a. `floor_certificate = 'equivalent'` is not a floor claim at all — 10 of 10
busted (2026-08-20).** A fixed-seed blind sample of the 726 AT_LIMIT rows below
100 % carrying that label was audited function by function. **Every one was
source-reachable.** Four went to 100 %, and the sample as a whole moved
**90.07 → 97.46 mean normalized (+73.9 pp total)**, with **zero regressions**
across 48,306 comparable functions. Five of the ten carried live behavioural
bugs — eight distinct defects, none of them cosmetic:
`RndBitmap::PixelOffset` read the 128-entry `hbytes` tables where the target's
`lbzx` relocations resolve to two `size 0x40` symbols, so every 8bpp swizzled
pixel fetch used the wrong table (the correct `bytes02`/`bytes13` pair was
declared in our source and never referenced, so the linker dropped it);
`HamVisDir::PostUpdate` passed a bare index where the target masks it
(`firstTracked ? i : 0`), so with player 0 absent player 1 drove player 2's anim
slot; `RhythmBattlePlayer::AnimateBoxyState` activated `mOutTheZoneOkFlow` on a
path where the target activates nothing; `MeterDisplay::DrawShowing` rebuilt its
localized label string every frame; `RndText::WrapText` dropped the `pen = 2010`
overflow penalty so an over-wide line scored like a fitting one, and wrote
`StyleState::brk` at 0x41 instead of 0x40 so `<nobreak>` markup did nothing.

The reason is structural, not statistical. `scripts/certify_floor.py`
`classify_function()` fires `equivalent` on `unicorn_verdict == 'EQUIVALENT'`
**with no other condition** — no match-percent floor, no pattern gate, no size
gate. All 912 rows carry the identical evidence blob
`{"evidence":"unicorn_equivalent"}`. So the label is a *rename of the unicorn
verdict*, and it inherits every limit of "Read `EQUIVALENT` narrowly" above,
including that field-confusion bugs are structurally invisible to the fixture.
That inheritance is visible in the population: the certified pool runs down to
**37 %** normalized, 98 rows sit below 85 %, and **238 of 729 carry a
`has_control_flow` signal** — a control-flow difference is not in anyone's list
of backend floors. Two of the ten had been ported character-for-character from a
*different binary* (`../rb3-xenon`'s `CharLipSync.cpp`) and never checked against
DC3's target.

Treat `floor_certificate = 'equivalent'` as "the emulator did not notice a
difference", never as "the residual is cosmetic". The companion audit of
`permuter_exhausted` the same day busted 8 of 10.

```sql
-- AT_LIMIT rows carrying a real-bug divergence class
SELECT unicorn_class, COUNT(*), SUM(size) FROM functions
WHERE excluded = 0 AND verdict = 'AT_LIMIT' AND unicorn_verdict = 'DIVERGENT'
  AND unicorn_class IN ('logic','call_count','call_arg','return_value',
                        'object_memory','error','wild_jump_match',
                        'cap_exhausted','cap_exhausted_decomp')
GROUP BY 1 ORDER BY 2 DESC;
```

### Cert rot — recurring, and re-derivable

The mirror-image problem: rows marked COMPLETE in `decomp.db` that are not at
100 % in a fresh report. A COMPLETE verdict tells every future agent to stop
working on the function, so a rotted one is worse than no verdict at all.

**This is not a closed problem and this document will not tell you the count.**
It said "closed … this count is 0" from 2026-08-19 (task #101) to 2026-08-31, by
which date it was 27 rows / 9,176 B. Re-derive it — it takes about six seconds
on a built tree, and **the number moves every time source lands**:

```bash
ninja                                        # the report must be fresh
python3 scripts/analysis/cert_rot_census.py \
        --db /path/to/main/decomp.db --project . [--include-excluded]
```

Two ways the old SQL-only check went wrong, both now handled by that script:

* **Neither DB percentage column is the measurement.** `current_percent` mirrors
  `report.json`'s *fuzzy* percent (see below) and `match_percent_normalized` is
  the right ruler but a **cache** — it is written only by
  `scripts/sync_match_percent.py --promote`, so it is exactly as fresh as the
  last person who ran that. Measured 2026-08-31, it disagreed with a fresh
  report *in both directions*: it hid two rows that had rotted since the last
  sync and listed seven that had since been fixed. The census reads
  `report.json` and joins the DB only for the verdict.
* **`fuzzy_match_percent` absent ≠ 0 % match.** objdiff emits it only for
  symbols we define a body for; a row without it is unpaired, not mismatched.
  All 66 `default/link_glue` rows are of that shape and contributed nine false
  positives to a hand-rolled query. The census drops them **and prints the count
  it dropped**.

⚠ Do not read a matching total as confirmation. On 2026-08-31 a hand-derived
population of 23 rows / 7,668 B and a re-derived one of 23 rows / 7,668 B were
*different sets* that happened to sum alike; the re-derived one had two more
rows and two fewer because a fix had landed in between.

**Do not re-derive this check from `current_percent`.** That column mirrors
`report.json`'s *fuzzy* percent, not the canonical normalized one — verified
2026-08-19 across 30,647 comparable rows: 29,290 agree with fuzzy exactly and
the largest disagreement is 0.013 pp, while against the normalized ruler 885
rows differ by more than 0.5 pp and 577 by more than 1 pp, up to 7.6 pp. So
`verdict = 'COMPLETE' AND current_percent < 100` returns 374 rows that are all
at exactly 100 normalized — a phantom rot population.

The working rule for a reader of this document is unchanged: `verdict` and
`current_percent` are hints. Re-measure with `run_objdiff` before acting on
either.

---

## The open frontier, by shape

Ordered by how tractable the shape is, not by size:

| Shape | size | where to start |
|---|---|---|
| **1,085 unverdicted rows** | 280,788 B | Nobody has looked. Cheapest possible triage; 1,050 of them are below 100 % in the report. |
| **455 stubs** | 113,688 B | Missing implementations; `synth_xbox`/`os`/`rnddx9` dominate. Reconstruction from target asm. |
| **813 functions at literally 0 %** | 116,956 B | 29 % of the gap by function count but only 9.5 % by bytes. Usually a missing implementation, not a codegen fight. |
| **272 name_check-only residuals** | 66,828 B | *Figures as of the 2026-08-12 audit; not re-measured since.* 96 adjudicated real bugs, 8 alias gaps, 115 unadjudicated. Symbol spelling, not codegen. |
| **2,205 real-class unicorn DIVERGENT** | 479,528 B | Overlaps AT_LIMIT heavily; still the best post-plateau bug oracle, but read the EQUIVALENT caveat above before trusting a class label. |

The "540 rotted COMPLETE certs" row that used to sit here has been removed: that
population is now zero (see [Cert rot](#cert-rot--closed)).

Queries for all five are in
[`decomp/REMAINING_WORK.md`](decomp/REMAINING_WORK.md). That document ships
**queries only** — deliberately no worklists, because every hardcoded worklist
this project has ever written rotted within weeks.

---

## REFUTED (2026-08-19): the build *is* deterministic

This section previously claimed two clean builds of the same commit differ by
roughly **±160 functions**, mostly 12–52-byte dynamic-initializer and atexit
thunks. **That does not reproduce.** Measured in the toolchain audit
(`docs/analysis/2026-08-19-toolchain-audit.md`, merge `691174927`): two clean
builds produce `cmp`-identical `report.json`, with **0 of 48,344 functions
differing**.

All 980 rebuilt `.obj` files *do* differ, but only by **2 bytes** — the COFF
timestamp and the embedded path. That object-level churn is almost certainly
what the ±160 figure was really measuring, via a metric that was reading the
relocation-sensitive *fuzzy* percent rather than the canonical one.

**Do not quote ±160, and do not dismiss a single-function delta as build
noise.** If a function moves between two builds of the same commit, something
real changed — investigate it. (Thunks are still filtered out of *work
selection* by `query_functions(skip_boilerplate=True)`, but that is an
ergonomics choice, not a noise floor.)

---

## Where to go next

| Doc | What it holds |
|---|---|
| [`PROGRESS_METRICS.md`](PROGRESS_METRICS.md) | Generated headline numbers with build provenance. Regenerate, never hand-edit. |
| [`decomp/REMAINING_WORK.md`](decomp/REMAINING_WORK.md) | How to *find* work: queries, column trust, triage routing. |
| [`decomp/patterns/INDEX.md`](decomp/patterns/INDEX.md) | Fixable vs unfixable codegen pattern catalog. Start at its **Corrections** section. |
| [`decomp/OBJECT_MATCHING.md`](decomp/OBJECT_MATCHING.md) | What "matching" means at the object level — the mechanics behind the metric. |
| [`decomp/TECHNICAL_NOTES.md`](decomp/TECHNICAL_NOTES.md) | Compiler quirks, including why header edits shift inlining TU-wide. |
| [`decomp/MSVC_X360_REGALLOC.md`](decomp/MSVC_X360_REGALLOC.md) | Register-allocation model — the thing behind most "regswap" verdicts. |
| [`reference/DATABASE_SCHEMA.md`](reference/DATABASE_SCHEMA.md) | `decomp.db` schema and the columns you cannot trust. |
| [`archive/README.md`](archive/README.md) | Superseded status/planning documents, preserved verbatim with a manifest of what went stale. |
