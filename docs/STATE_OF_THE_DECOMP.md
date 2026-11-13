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
| Denominator | 32,419 authorable functions | 31,458 non-excluded rows |
| Headline | **98.30 %** functions (31,869 / 32,419) | **COMPLETE 30,252 + AT_LIMIT 905 = 99.04 %** |
| Byte headline | **92.85 %** code bytes (5,964,136 / 6,423,164) | — (the DB's byte sums are not a build measurement) |
| Units | 798 / 1,000 authorable units complete (79.80 %) | — |
| Remaining | 645 fns / 459,028 bytes | 301 rows with no verdict |

> Figures in this table re-measured **2026-11-09** on `c89fa469d`. "Authorable"
> here means *everything outside `default/xdk/**`* — the same filter the
> 2026-08 version of this table used — so it still counts the 185 RAD Bink
> functions in `default/lib/binkxenon/**`, which will never be written (the
> native port uses FFmpeg). The 2026-08 row read 91.36 % / 78.19 % / 2,783 fns
> and is superseded; the denominators grew slightly (32,213 → 32,419,
> 31,425 → 31,458) because functions kept being written, so this is progress on
> the same filter, not a redefinition.

**Always state the denominator.** "98.30 %" and "COMPLETE + AT_LIMIT = 99.04 %"
are both true and describe different facts: the first is a build measurement,
the second is a bookkeeping measurement that includes 905 rows certified
*unfixable*.

The DONE-WITH-CERTS denominator fell **33,560 → 31,425** on 2026-08-17/18, when
tasks #101 and #104 excluded ~2,135 rows that `report.json` can never score:
`merged_<addr>` ICF fold survivors, `fn_<addr>` MSVC EH funclets, stale split
spellings and unreferenced inline COMDATs. They were excluded, not deleted
(21,122 rows carry `excluded = 1` in total). Any DONE-WITH-CERTS figure recorded
before that date is on the old denominator and is not comparable to this one.

A third number floats around and should be labelled when used: the **XEX-total**
(XDK-diluted) headline of **65.98 % functions / 52.48 % bytes** (31,909 / 48,365
and 5,969,932 / 11,374,864 bytes). It divides by the whole 11.37 MB image
including **5.02 MB (44.2 %) of Microsoft XDK and RAD Bink code** that has no
source in this repo and never will — 15,894 of the 16,163 unwritten functions
are `default/xdk/**` (the D3DXShader compiler, XGRAPHICS, ST) and 185 are
`default/lib/binkxenon/**`. It is capped near 56 % by construction and is only
useful for "how much of the shipped image do we reproduce". **Never quote it as
game work.**

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

⚠ **Superseded 2026-11-13: `configure.py` no longer declares `link_glue` to
objdiff**, so it is out of `report.json` entirely (it is still built and
linked). The "would hide real work" argument below does not survive a check
against the retail map: the unit has no target object, so none of its rows can
*be* a retail function. Of the 76 rows then present, 28 names are absent from
`ham_xbox_r.map`, 13 are the same name scored in its real unit, 25 are ICF-folded
into a survivor scored in its real unit (`FormatString::operator<<(float)` folds
into `(double)` in MakeString, `HDCache::Flush` and `HolmesClientPrint` into
`OnlyReturns`, all at 100 %), and 10 fold into xdk/LIBCMT rows. Authorable
denominator 32,221 → 32,158; matched count unchanged. `dedup_glue_shadows()` in
`progress_metrics.py` now finds nothing to drop, which is correct.

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

## What the remaining 449 KB is made of

> Re-measured **2026-11-09** on `c89fa469d`, same authorable filter as above
> (everything outside `default/xdk/**`). The previous version of this section
> said 2,783 authorable functions / 1,233,052 bytes; that is superseded, and the
> drop is real work rather than a changed definition.

**645 authorable functions / 459,028 bytes** are below 100 % *fuzzy*. Joining
the fresh report against `decomp.db` by symbol:

| Shape | fns | bytes | share of gap |
|---|--:|--:|--:|
| Certified **AT_LIMIT** | — | 337,736 | 73.6 % |
| DB says COMPLETE, report says fuzzy < 100 | 95 | 82,192 | 17.9 % |
| **Unverdicted** (no adjudication at all) | — | 38,240 | 8.3 % |
| COMPLETE by verdict but still short | — | 860 | 0.2 % |

**Most of the gap is no longer "missing code", but 18 % of it still is.**
**249 authorable functions / 80,396 bytes** have no base-side body at all — each
confirmed by diffing it with objdiff (`base_size == 0`), not inferred from the
report:

- **185 fns — `default/lib/binkxenon/**`.** RAD Game Tools' closed-source Bink
  video decoder. There is no reference to port from and the native port does not
  want it (it uses FFmpeg), so these are **permanently unwritten by design**.
  They are the single biggest reason the byte headline reads 92.85 % rather than
  98 %.
- **76 fns — `default/link_glue`**, the pseudo-unit of symbols the decomp emits
  purely to make the Xbox link succeed.
- A handful of stragglers in `system`.

⚠ **Two ways `report.json` manufactures phantom "missing code". Both were hit
on 2026-11-09 by a mechanical pass over this column; both are avoided by diffing
the row instead of reading it.**

1. **`fuzzy_match_percent == null` does not mean "no body".** 12 rows scored
   null — `floor0_*`, `curlx_uztosz`, `zcfree`, `jpeg_get_small`,
   `??6FormatString` … — **diff to 100 % with zero mismatches**. They are
   report-side masked-equal rows (`measures.masked_equal_functions`). The byte
   effect is small (60 B, the report gives them degenerate sizes); the damage is
   to a *worklist*, which will send a lane to implement functions that already
   match.
2. **`fn_<addr>` does not mean "unimplemented".** Eight rows this section
   briefly counted as stubs resolve, via `ham_xbox_r.map`, to real functions we
   already implement and already score under their proper mangled names —
   `fn_8263A360` is `FillCompressedVertex` (556 B, **99.96 %**),
   `fn_8263A168` is `PackVector`, `fn_825CCF20` is `DebugModal` (written in
   `src/App.cpp`), and two more are `??__EsLicense` static initializers, i.e.
   boilerplate nobody writes by hand. `symbols.txt` names those addresses with a
   placeholder, so our object emits the real symbol and objdiff sees
   `base_size 0` for the placeholder *name*. **Resolve a `fn_*` row through the
   map before calling it work.**

Everything else below 100 is a *partial match*, and **zero** authorable functions
sit at literally 0 %.

Everything else below 100 is a *partial match*, and **zero** authorable functions
sit at literally 0 %.

The rest of the missing-function mass is entirely outside the game: **16,163
functions / 5,023,916 bytes** have no base-side counterpart tree-wide, and
15,894 of those are `default/xdk/**` (the Xbox SDK — the D3DXShader compiler,
XGRAPHICS, ST). Neither XDK nor Bink is on the native critical path, which is
why the XEX-total headline looks far worse than the game code does.

### Two pots, and they need different tools

| pot | fns | bytes | what closes it |
|---|--:|--:|---|
| below 100 % **normalized** | 550 | 376,836 | real codegen/semantic work |
| at 100 % normalized, below 100 % **fuzzy** | 95 | 82,192 | **register permutation only** |

The second pot is the one that is easy to misread. `match_percent_normalized`
forgives register permutation, so these rows score a clean 100 and still bank
**nothing**: `matched_code` only banks at fuzzy == 100. Whole-binary that gap is
`complete_code − matched_code` = **374,640 bytes**. On the 95 authorable rows
the residue is callee-saved **rotations** (`r14←15←16`, `r22←23←24`) — one
allocation decision, not one wrong value — so they cannot be closed by fixing an
offset or a constant, and they do not move `match_percent_normalized` at all.
The sharpest example: `SongSequence::OnSongLoaded` is 3,440 B, 860 instructions,
**71 diff_arg rows and 0 other mismatches**, all six of them a rotation. Quote
whichever axis you mean.

**Cert rot is gone.** The 875-row "DB says COMPLETE, report says otherwise"
slice was closed by task #101: `verdict = 'COMPLETE' AND
match_percent_normalized < 100` now returns **zero** rows. Do **not** re-derive
this check from `current_percent` — that column holds the *fuzzy* percent, so
the 95 COMPLETE rows in the table above are below 100 fuzzy while sitting at
exactly 100 normalized. Quoting `current_percent` would present them as rot.
They are the register-permutation pot.

### By subsystem

Top authorable subsystems by remaining bytes (2026-11-09):

| Subsystem | remaining bytes | share of its own code |
|---|--:|--:|
| `rndobj` | 84,876 | 8.7 % |
| `binkxenon` | 78,568 | **100 %** (by design, see above) |
| `hamobj` | 58,876 | 7.1 % |
| `char` | 34,376 | 5.5 % |
| `world` | 30,196 | 9.0 % |
| `synth_xbox` | 24,512 | 17.1 % |
| `os` | 20,956 | 9.3 % |
| `gesture` | 20,508 | 10.0 % |
| `rnddx9` | 16,836 | 19.6 % |
| `net` | 12,648 | 5.4 % |
| `obj` | 11,308 | 5.4 % |
| `utl` | 10,532 | 5.0 % |

As a *fraction of its own code*, the two worst are still the reference-less
deserts: **`rnddx9` — 19.6 % unmatched** (16,836 of 86,020 bytes) and
**`synth_xbox` — 17.1 %** (24,512 of 142,928). These are Xbox-specific audio and
Direct3D 9 back-ends with no counterpart in the RB3 tree, so there is no
reference implementation to port from — they have to be reconstructed from the
target assembly. That work is largely *metric-invisible* in the sense that it
fixes real behaviour long before it moves a percentage.

`binkxenon` is at 100 % unmatched and is **not** work: it is the closed Bink
decoder described above. Exclude it when ranking by fraction, or it dominates a
list it cannot appear on.

---

## Stubs

`is_stub = 1`, non-excluded: **10 functions / 5,736 bytes** as of 2026-11-09
(it was 455 / 113,688 in the 2026-08 measurement — stub reconstruction has
essentially finished). These are bodies we never wrote — the target has code,
our source has `{}` or `return 0`.

| Unit | stubs | bytes |
|---|--:|--:|
| `default/system/synth_xbox/FFT` | 2 | 5,176 |
| `default/system/synth_xbox/Synth` | 4 | 224 |
| `default/system/synth_xbox/HeadsetPlaybackEffect` | 1 | 100 |
| `default/system/synth_xbox/GainEffect` | 1 | 100 |
| `default/system/hamobj/MoveDir` | 1 | 96 |
| `default/system/utl/Locale` | 1 | 40 |

The tail are trivial; `FFT` is essentially the whole remaining stub story and it
is the Xbox-only synth FFT cluster (`fft_altivec`, `fft_recursive`), which also
owns three of the worst partial matches below 90 %. **There are no stubs left in
shared engine code the native port runs.**

A 2026-11-09 sweep of every row `report.json` scores as null *appeared* to find
8 more, including a 1,060 B pair in `rndobj/Mesh`. **All eight were false**: they
are `fn_<addr>` placeholder rows whose real functions we implement and already
score (`FillCompressedVertex` is one of them, at 99.96 %). Resolving each through
`ham_xbox_r.map` cleared all eight — see the warning in
[What the remaining 449 KB is made of](#what-the-remaining-449-kb-is-made-of).


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
them. Report-visible AT_LIMIT is **1,103 functions / 680,540 bytes** (re-measured
2026-11-09), of which only 34 are still `merged_*`.

⚠ **Forward cert rot: 617 of those 1,103 rows (304,716 bytes) now score 100 %
fuzzy** — 56 % of the certs by count, 45 % by bytes. The function was finished
after it was certified and nobody retired the cert. So when someone says "the
remaining work is all certified AT_LIMIT", the number behind that is **486
functions / 375,824 bytes**, not 1,103 / 680,540. Re-measure before quoting the
larger one.

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

92,676 bytes is **13.6 % of all report-visible AT_LIMIT bytes** (680,540) — and
**24.7 % of the 375,824 bytes that are still below 100 %** — down from the 41 %
the pre-fix oracle claimed. A tenth of what is filed as "certified unfixable"
still carries a live signal that it is not merely unmatched but *wrong*. (The
denominator changed on 2026-11-09: the 2026-08 figure of 10.0 % divided by
924,696 before forward cert rot was accounted for.)

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

Ordered by how tractable the shape is, not by size. **Every row re-measured
2026-11-09 unless marked otherwise.**

| Shape | size | where to start |
|---|---|---|
| **310 unverdicted rows** | 94,116 B | Nobody has looked. Cheapest possible triage; 108 of them (43,904 B) are below 100 % in the report, the rest already score 100 and just need the verdict written. |
| **10 stubs** | 5,736 B | Down from 455 / 113,688, and **none left in shared engine code the native port runs**. `synth_xbox/FFT` is 5,176 B of it — Xbox-only. |
| **0 authorable functions at literally 0 %** | 0 B | The 813-row / 116,956 B population is gone; what remains at 0 % is XDK and Bink only. |
| **95 rows at 100 % normalized but < 100 % fuzzy** | 82,192 B | **Register permutation only** — callee-saved rotations. Closing these banks 82 KB of `matched_code` and moves `match_percent_normalized` by exactly 0. Do not look for a wrong offset here. |
| **281 partial rows below 100 % normalized** | 294,748 B | The real codegen/semantic frontier. 73 % of it carries an AT_LIMIT cert, and 45 % of the cert population is already stale (see above), so re-measure before believing a cert. |
| ~~272 name_check-only residuals~~ | **0 B nameable** | *Re-run 2026-11-09 with `scripts/analysis/reloc_name_gate.py`: 62 rows / 78,524 B carry a name charge, 42 rows / 42,512 B survive the exemption buckets, and the slice whose charge is attributable to a **name fix** is **0 rows / 0 B**. The class is closed — the survivors are register-save-helper mismatches and `__real@` address pairs, both of which normalize away. Do not re-open it as a worklist.* |
| **2,205 real-class unicorn DIVERGENT** | 479,528 B | *2026-08-19 figure; not re-measured.* Overlaps AT_LIMIT heavily; still the best post-plateau bug oracle, but read the EQUIVALENT caveat above before trusting a class label. |

The "540 rotted COMPLETE certs" row that used to sit here has been removed: that
population is now zero (see [Cert rot](#cert-rot--closed)).

Queries for all of these are in
[`decomp/REMAINING_WORK.md`](decomp/REMAINING_WORK.md). That document ships
**queries only** — deliberately no worklists, because every hardcoded worklist
this project has ever written rotted within weeks.

---

## REFUTED (2026-08-19): the build is deterministic *as measured* — and was NOT deterministic in its bytes until 2026-08-31

This section previously claimed two clean builds of the same commit differ by
roughly **±160 functions**, mostly 12–52-byte dynamic-initializer and atexit
thunks. **That does not reproduce.** Measured in the toolchain audit
(`docs/analysis/2026-08-19-toolchain-audit.md`, merge `691174927`): two clean
builds produce `cmp`-identical `report.json`, with **0 of 48,344 functions
differing**.

All 980 rebuilt `.obj` files *do* differ. That object-level churn is almost
certainly what the ±160 figure was really measuring, via a metric that was
reading the relocation-sensitive *fuzzy* percent rather than the canonical one.

⚠️ **Corrected 2026-08-31.** This paragraph used to say the objects differ "only
by **2 bytes** — the COFF timestamp and the embedded path", quoting the toolchain
audit. **Both the count and the attribution were wrong**, and the attribution was
the damaging half: naming *the path* as the second field made this look like a
cross-worktree artifact, when in fact there are **two clock-derived fields** and
the build was nondeterministic **in a single tree at a single path**. They are the
COFF `TimeDateStamp` (offsets 4–7, every object) and the CodeView `S_OBJNAME`
signature word in `.debug$S` (582 objects; verified present in `keygen_xbox.obj`
at file offset `0x1e8`, which the original single-object diff walked past). The
path is a genuine *third* difference, but it only appears across checkouts.
Masking exactly the two clock fields makes all 980 compare equal with zero
residual bytes. Fixed at the cause in `ee8902a22` by a sixth post-compile pass
(`scripts/obj_build_metadata_patcher.py`): 0 of 989 differ afterwards, and
`tree_sha256` is stable across rebuilds of one tree — though **not comparable
across worktrees**, since MSVC writes the source path into `S_OBJNAME`'s name
field.

**The refutation itself stands, on the evidence it actually used** — two clean
builds giving `cmp`-identical `report.json`, 0 of 48,344 functions differing, is
an objdiff-score comparison that neither clock field can reach. What was wrong was
the description of the object plane, not the conclusion about the measurement
plane. **Do not read "the build is deterministic" as "an object rebuilt today has
the same bytes as yesterday's":** a per-target `ninja <one>.obj` still produces an
un-normalized object, because the metadata pass runs on the `post-compile` edge.
Byte-A/B after a full `ninja`, or zero the two fields yourself. See `CLAUDE.md`.

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
