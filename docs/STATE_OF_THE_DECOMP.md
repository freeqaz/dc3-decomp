# State of the Decomp

**Measured 2026-08-17** against `924ab0c5e`, re-verified in a clean worktree at
`2b7382e93` (identical results). Every number below was produced by a fresh
`ninja` build plus queries against `decomp.db`; each section says how to
regenerate it. If you are reading this more than a month after that date,
regenerate before quoting.

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
| Denominator | 32,213 authorable functions | 33,560 non-excluded rows |
| Headline | **91.21 %** functions (29,383 / 32,213) | **COMPLETE 29,655 + AT_LIMIT 3,628** |
| Byte headline | **77.41 %** code bytes (4,910,452 / 6,343,156) | — (the DB's byte sums are not a build measurement) |
| Units | 416 / 967 authorable units complete (43.02 %) | — |
| Remaining | 2,830 fns / 1,250,152 bytes | 277 rows with no verdict |

**Always state the denominator.** "91.21 %" and "COMPLETE + AT_LIMIT = 99.2 %"
are both true and describe different facts: the first is a build measurement,
the second is a bookkeeping measurement that includes 3,628 rows certified
*unfixable* and a further 875 rows whose COMPLETE certificate no longer holds
(see [Cert rot](#cert-rot)).

A third number floats around and should be labelled when used: the **XEX-total**
(XDK-diluted) headline of **60.81 % functions / 43.18 % bytes**. It divides by
the whole 11.37 MB image including 5.03 MB of Microsoft XDK and RAD Bink code
that has no source in this repo and never will. It is permanently capped near
56 % and is only useful for "how much of the shipped image do we reproduce".

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
SELECT COUNT(*) FROM functions WHERE excluded = 0;                 -- 33,560
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

2,830 authorable functions / 1,250,152 bytes are below 100 % normalized. Joining
the fresh report against `decomp.db`:

| Shape | fns | bytes | share of gap |
|---|--:|--:|--:|
| Certified **AT_LIMIT** | 1,651 | 939,372 | 75 % |
| DB says **COMPLETE**, report says otherwise (*cert rot*) | 875 | 178,992 | 14 % |
| **Unverdicted** (no adjudication at all) | 151 | 92,764 | 7 % |
| In the report, absent from the DB | 120 | 36,444 | 3 % |
| Flagged `is_stub` and still non-zero | 33 | 2,580 | <1 % |

Cross-cut a different way: **844 functions / 125,604 bytes sit at literally 0 %**
— that is 10 % of the gap in functions that produce nothing resembling the
target, which is usually a missing implementation rather than a codegen fight.

*(The audit's own join reported the AT_LIMIT slice as 1,702 fns / 966,036 B.
The 51-function difference is join-method sensitivity around ICF placeholder
symbols; both figures round to the same story, which is "three quarters of the
gap is behind a certificate".)*

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

`is_stub = 1`, non-excluded: **494 functions / 117,920 bytes**. These are bodies
we never wrote — the target has code, our source has `{}` or `return 0`.

By subsystem: `synth_xbox` 219, `os` 101, `rnddx9` 52. The largest single files:

| Unit | stubs | bytes |
|---|--:|--:|
| `default/system/os/PlatformMgr_Xbox` | 69 | 12,592 |
| `default/system/synth_xbox/ExternalMic` | 43 | 6,312 |
| `default/system/synth_xbox/Synth` | 42 | 9,344 |
| `default/system/synth_xbox/GranularSynth` | 15 | 3,872 |
| `default/system/synth_xbox/Mic` | 13 | 5,084 |
| `default/system/rnddx9/ShaderMgr` | 13 | 1,304 |
| `default/system/os/NetworkSocket_Win` | 12 | 1,580 |
| `default/system/rnddx9/Tex` | 11 | 6,532 |

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

**1. Most AT_LIMIT rows are not even in the report.** Of 3,628 AT_LIMIT rows,
**1,910 are `merged_*` ICF placeholders** (163,252 bytes) — synthetic names for
addresses where the linker folded identical machine code. They are bookkeeping,
not work. Report-visible AT_LIMIT is 1,651 functions / 939,372 bytes.

**2. Unicorn says 640 of them still behave differently from the target.**
Filtering AT_LIMIT rows to divergence classes that indicate *real* behavioural
difference (as opposed to build-environment or register-allocation artefacts):

| class | fns |
|---|--:|
| `cap_exhausted` | 377 |
| `cap_exhausted_decomp` | 144 |
| `wild_jump_match` | 50 |
| `call_arg` | 31 |
| `call_count` | 24 |
| `object_memory` | 9 |
| `error` | 3 |
| `return_value` | 2 |
| **total** | **640 fns / 466,036 bytes** |

466,036 bytes is **41 % of all AT_LIMIT bytes** (1,130,304). Nearly half of what
is filed as "certified unfixable" carries a live signal that it is not merely
unmatched but *wrong*.

Across the whole DB, real-class DIVERGENT is **1,145 functions / 650,972 bytes**.

**3. A blind audit of the certificates failed.** On 2026-08-04 a blind sample of
regswap-class AT_LIMIT certificates scored **3 out of 10** — and that sample was
drawn at random, not from the three cases that had motivated the audit. The
label is not worthless, but it does not survive being leaned on.

```sql
-- AT_LIMIT rows carrying a real-bug divergence class
SELECT unicorn_class, COUNT(*), SUM(size) FROM functions
WHERE excluded = 0 AND verdict = 'AT_LIMIT' AND unicorn_verdict = 'DIVERGENT'
  AND unicorn_class IN ('logic','call_count','call_arg','return_value',
                        'object_memory','error','wild_jump_match',
                        'cap_exhausted','cap_exhausted_decomp')
GROUP BY 1 ORDER BY 2 DESC;
```

### Cert rot

The mirror-image problem: **875 rows marked COMPLETE in `decomp.db` are not at
100 % in a fresh report** (178,992 bytes).

| | fns | bytes |
|---|--:|--:|
| in the 90–99.99 % band | 540 | 143,888 |
| at 0 % | 335 | 35,104 |

The 335 at 0 % are the alarming ones — a COMPLETE certificate over a function
that now produces nothing. Most of this is drift: certificates recorded under
the old relocation ruler, or against builds that have since moved.

**DB hygiene is a separate lane already in flight (coordinator task #101). Do
not go fix the database from here.** What follows for a reader of this document
is only the working rule: `verdict` and `current_percent` are hints. Re-measure
with `run_objdiff` before you act on either.

---

## The open frontier, by shape

Ordered by how tractable the shape is, not by size:

| Shape | size | where to start |
|---|---|---|
| **277 unverdicted rows** | 109,676 B | Nobody has looked. Cheapest possible triage. |
| **494 stubs** | 117,920 B | Missing implementations; `synth_xbox`/`os`/`rnddx9` dominate. Reconstruction from target asm. |
| **272 name_check-only residuals** | 66,828 B | 96 adjudicated real bugs, 8 alias gaps, 115 unadjudicated. Symbol spelling, not codegen. |
| **1,145 real-class unicorn DIVERGENT** | 650,972 B | Overlaps AT_LIMIT heavily; the best post-plateau bug oracle in the project. |
| **540 rotted COMPLETE certs in the 90–99.99 band** | 143,888 B | Near-misses with a stale certificate; often a small real fix. |

Queries for all five are in
[`decomp/REMAINING_WORK.md`](decomp/REMAINING_WORK.md). That document ships
**queries only** — deliberately no worklists, because every hardcoded worklist
this project has ever written rotted within weeks.

---

## Caveat: the build is not deterministic to a single function

Two clean builds of the *same commit* differ by roughly **±160 functions**.
Almost all of the delta is 12–52-byte dynamic-initializer and atexit thunks
(`??__E*`, `??__F*`), whose codegen depends on ordering that is not fully
pinned.

Practical consequences:

- **A single-function delta on a thunk-shaped symbol is noise.** Do not report
  it as a win or a regression.
- Compare aggregate headline numbers, not individual thunk rows, when checking
  whether a change helped.
- `query_functions(skip_boilerplate=True)` (the default) filters these out of
  work selection for exactly this reason.

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
