# Wave 8 — climbing from 0 %, smallest functions first

**Started:** 2026-09-15, base `bb2e759eb` (wave-7 close-out).
**Directive:** work the remaining authorable list from the bottom up — 0 % rows
first, then the small rows, then the hard ones. Opus lanes only.
**Coordinator artefacts:** `~/tmp/dc3-wells/w8/` (worklists, snapshots,
`compare.py`, gate logs). Baseline snapshot `report-baseline-bb2e759eb.json`.

## Where the wave starts

| denominator | functions | bytes |
|---|---|---|
| Authorable matched (canonical) | 96.80 % (31,191 / 32,223) | 87.32 % (5,540,052 / 6,344,596) |
| Remaining authorable | 1,032 | 620,028 B |

Remaining work by band, with what the database claims about each row:

| band | functions | bytes | AT_LIMIT | unadjudicated |
|---|---:|---:|---:|---:|
| 0 % | 195 | 21,872 | 27 | 168 |
| under 80 % | 34 | 25,000 | 16 | 18 |
| 80–95 % | 176 | 77,000 | 157 | 19 |
| 95–99.9 % | 519 | 399,000 | 457 | 58 |
| 99.9 – under 100 % | 119 | 79,000 | 90 | 27 |

## Finding 1: 76 of the 195 zero rows are not work

`default/link_glue` is a synthetic unit — `configure.py` registers it with
`"object": None`, so **there is no target object to diff against**. Every one of
its 76 functions therefore scores 0.0 with a diff that is pure `delete`
(`HDCache::Flush` measures 1 instruction, 1 delete, against an empty target
side), while the unit's own metadata reads `complete: true` and
`complete_code_percent: 100.0`.

Consequences, both measured:

- The 0 % class is **119 real rows / 19,984 B**, not 195 / 21,872.
- The canonical authorable headline is **understated by up to 65 functions**.
  `progress_metrics.py` already dedups 11 link-glue rows that shadow a symbol
  matched in its real unit; the other 65 exist only in link_glue and are counted
  as authorable-but-unmatched even though nothing can ever score them.

Do not send a lane at a link_glue row. If the denominator is to be corrected,
that is a `scripts/authorable.py` change, not decomp work.

## Finding 2: the 0 % class is instantiation placement, not missing bodies

Of the 119 real zero rows, the large majority are template instantiations
(`vector<T>::_M_range_insert_realloc`, `StlNodeAlloc<T>::allocate`,
`PropSync<T>`, `ObjPtrList<T>::Unlink`, `ObjDirItr<T>::Advance`,
`CSampleXAPOBase<T>::Process`), plus compiler-generated funclets and ICF fold
survivors. A prior audit found only **10** genuinely unwritten non-excluded
functions in the whole binary, all SDK boilerplate. So this class is about
*which translation unit emits an instantiation and with what linkage*, not about
writing new game logic.

## Lanes

Six Opus lanes, grouped so no two touch the same file.

| lane | rows | bytes | units |
|---|---:|---:|---|
| `w8-a` | 25 | 3,360 | `rndobj/Utl`, `obj/Utl` |
| `w8-b` | 19 | 1,848 | `hamobj/*`, `lazer/meta_ham/*` |
| `w8-c` | 20 | 2,972 | `char/*` |
| `w8-d` | 15 | 1,100 | `synth_xbox/*` except FFT |
| `w8-e` | 38 | 5,528 | `rndobj/*`, `flow/*`, `os/*`, `ui/*`, misc |
| `w8-f` | 2 | 5,176 | `synth_xbox/FFT` (the AltiVec pair, hardest) |

## Phases after the 0 % band

Derived at dispatch time, link_glue excluded throughout.

| phase | population | rows | bytes | lanes |
|---|---|---:|---:|---|
| 1 | 0 %, real rows | 119 | 19,984 | `w8-a` … `w8-f` |
| 2 | partial, under 200 B | 127 | 15,052 | `w8-g` … `w8-j` |
| 3 | partial, 200 B and over | 721 | 583,160 | not yet assigned |

Phase 2 is thin and wide: 127 rows spread over 88 units, at most four in any
one unit, 85 of them already carrying an AT_LIMIT certificate and 36 never
adjudicated at all. Phase 3 holds 94 % of the remaining bytes, and 664 of its
721 rows are already above 90 %.

## Finding 3: a better phase-3 derivation than band times size

Wave 7 ended saying the band-by-size frontier was exhausted and the next wave
needed a different derivation. Here is one, measured at wave-8 dispatch:

| shape | units | bytes to close |
|---|---:|---:|
| units needing exactly **one** more function | 151 | 120,288 |
| units needing one **or two** | 218 | 201,572 |

That is 218 of the 344 incomplete authorable units, and closing them moves the
complete-units metric (currently 623 / 967) rather than fractions of a
percentage point. The single-function list is saved at
`~/tmp/dc3-wells/w8/unit-completions.txt`, sorted by size — it opens with an
8-byte row in `zlib/zutil` and a 24-byte funclet in `jpeg/jcmaster`, and 4 of
the 20 cheapest are already above 99 %.

## Pending measurement correction

`scripts/authorable.py` counts the 65 link-glue-only rows in the authorable
denominator even though nothing can score them. Correcting it would move the
canonical headline by roughly 0.2 pp. **Deliberately deferred** until the wave's
lanes have landed, so that every lane's before/after comparison in this wave is
taken against one definition. It is a denominator change and must be committed
on its own, stating the before and after explicitly.

## Results

### Phase 1 — the 0 % band (six lanes, all landed at `980e4d5e4`)

| measure | at `bb2e759eb` | at `980e4d5e4` | delta |
|---|---:|---:|---:|
| Matched functions (canonical) | 31,228 | 31,310 | **+82** |
| Matched code | 5,544,916 B | 5,557,336 B | +12,420 B |
| XEX-total headline | 48.747 % | 48.856 % | +0.109 pp |
| Fuzzy | 55.4588 | 55.5577 | +0.0989 |
| **Authorable functions (canonical)** | 96.80 % | **97.05 %** | +0.25 pp |
| Authorable complete units | 623 / 967 | **629 / 967** | +6 |
| Remaining authorable | 1,032 fns / 620,028 B | 950 fns / 608,148 B | −82 / −11,880 B |

The 0 % class went from 119 real rows to **40** (6,048 B). Nine of the
survivors are 200 B or over and went to lane `w8-k`; the rest folded into the
phase-2 pool.

| lane | worklist | closed | headline result |
|---|---:|---:|---|
| `w8-a` | 25 / 3,360 B | 25 | all 25 to 100.0; 22 came back from three stand-ins |
| `w8-b` | 19 / 1,848 B | 18 | + one row 0 → 98.23; `splits.txt` re-cut and a `symbols.txt` name |
| `w8-c` | 20 / 2,972 B | 18 | + 444 B from a row the last one exposed |
| `w8-d` | 15 / 1,100 B | 14 | three functions that were **never defined**; 8 `symbols.txt` rebinds |
| `w8-e` | 38 / 5,528 B | 1 (+5 cascade) | mostly an adjudication — see below |
| `w8-f` | 2 / 5,176 B | 0 | both reconstructed from the listing, 0 → 48.75 / 46.93 |

Two DOWN rows, both adjudicated at merge and both costing **zero** matched
functions: `RndTexBlender::DrawShowing` 99.514 → 99.132 (the same `Mtx.h` change
crossed `DrawBlendList` 99.116 → 100, so the unit went 61 → 62) and
`DxRnd::DrawString` 99.9 → 96.1 on 888 B against 3.2 KB gained. Eleven
renamed/moved keys were each verified as a 0 % → 100 % config correction with no
loss.

**Native gate on `980e4d5e4`: green.** `gate exit 0` — 506 registered, 437
executed, 437 passed, 0 failed, 69 skipped against a budget of 69. The skips are
the expected gated suites (`GameplayTelemetryTest` 48, the Mogg/Bink/FFmpeg
audio families, `HeadlessBootTest.LongRunStability`), so the skip count did not
move and there is no driver-mismatch artefact behind it. Log:
`~/tmp/dc3-wells/w8/native-gate-980e4d5e4.log`. This matters more than usual for
this merge: four behavioural fixes landed in it, including `CharEyes`'s copy
constructor and `PlaylistSongProvider`'s destructor shape.

**Native gate on `a153a143b` (phase 2, lanes k/h/g): green.** `gate exit 0` —
506 registered, 437 executed, 437 passed, 0 failed, 69 skipped against budget 69,
and the skipped suite list is *identical* to the `980e4d5e4` run, so coverage did
not shrink. Run deliberately before the remaining lanes landed, so that the four
behavioural changes in those three merges — `CharSignalApplier::Handle`'s missing
superclass forward, `TransformNormal`'s dropped transpose, `_M_erase`'s double
division, and the `PlatformMgrOpCompleteMsg` name — are attributable on their own
rather than sitting in a six-lane batch to bisect. Log:
`~/tmp/dc3-wells/w8/native-gate-a153a143b.log`.

### What phase 1 proved about the 0 % class

**Almost none of it was missing bodies.** Ranked by rows recovered:

1. **Explicit instantiation** of a container — one line, `template class
   std::list<RndMesh *>;`, recovered five rows in a single unit. A member
   template needs the **member** form; a whole-class `template class` cannot
   reach one, and in this MSVC it does not instantiate implicitly-declared
   special members either. Access checking is exempt, so a private member works.
2. **The orphan-instantiation stand-in** — an unreferenced **external-linkage**
   function in the TU whose odr-use `/OPT:REF` discarded. Twelve of lane `w8-b`'s
   eighteen wins and twenty-two of lane `w8-a`'s twenty-five. That external
   linkage is required is now *measured*: making one `static` cost exactly one
   row, full `ninja` each way.
3. **Address-taking** for a non-template inline — also requires external linkage,
   for the same reason.
4. **Reading `ham_xbox_r.map`'s contributor-object column first**, which decides
   which of the above applies, and which rows are unscoreable from the unit they
   are filed under at all.

Lane `w8-e`'s adjudication is the honest shape of the class. Of its 38 rows:
15 ICF aliases (body contributed by a different TU), 9 placeholders (the row's
*name* is not a target symbol), 14 real gaps. Two thirds structurally
unscoreable, now recorded in source at each site with the map address.

### Behavioural bugs fixed in phase 1

- **`CharEyes::CharInterestState`** had no copy constructor. The image's stores
  `-1.0f` into `mRefractoryTime` (`CharEyes.s:9856-9880`); ours copied it
  memberwise, so vector growth of `mInterests` kept every countdown running where
  the image **clears** it.
- **`PlaylistSongProvider`** declared an empty virtual destructor the image never
  had — at `0x82984FC8` there are no own-vptr stores. MSVC emits those only for a
  *user-declared* destructor in a multiply-inherited class, which is also why the
  image's four-way ICF fold could not have happened with our shape.
- **`SpectralAnalysis`**'s six float vectors were the wrong type
  (`aligned_vector<float>`, not `std::vector<float, XboxAllocator<float>>`).
  Layout and access sites are identical either way; only the constructor's EH
  unwind funclets discriminate. **An EH unwind funclet is a type oracle** — the
  unwind path calls a member's out-of-line destructor by name where the normal
  path inlines it.
- **`CXAPOBase::AddRef`, `CalcInputFrames`, `CalcOutputFrames`** were declared in
  `xapobase.h` and never defined anywhere; only the adjustor thunk was emitted.

### Corrections to this project's own working assumptions

Each of these had already cost someone a wrong conclusion, including this
coordinator's own lane briefs:

- **Target listings are at `build/373307D9/asm/<path>/<Unit>.s`, with no `src/`
  component.** Three briefs in this wave said otherwise; lanes `c`, `e` and `f`
  each found it independently.
- **`fn_<addr>` does not mean EH funclet.** `fn_8263A360` and `fn_8263A168` have
  `.pdata` entries and are file-static `FillCompressedVertex` / `PackVector`. The
  "check funclets first, they're probably artifacts" heuristic would have
  mis-filed 1,060 B.
- **The system `grep` is `ugrep`, which silently skips binary files** — grepping
  an `.obj` returns nothing and reads as "we don't emit it". Use `strings -a`.
- **`run_symbol_sweep(kind="functions")` reports false 100.0s**, found
  independently by lanes `c` and `d`: it pairs through `icf_aliases.map` and
  claims 100.0 for symbols `run_objdiff` and `report.json` both score 0.0. A lane
  trusting it would conclude its whole worklist was already done.
- **A name appearing N times in the map with a bare `f`** is internal linkage in
  N translation units; `symbols.txt` binds exactly one, so a rename there is
  **zero-sum** — it moves the 0 % rather than removing it.
- **`merged_*` is our own label.** Members tagged `icf_aliases.synthetic` can
  never be produced by any source spelling.

### Carried findings not acted on

- `ObjPair` is misplaced: RB3 declares it in `obj/Object.h` beside `ObjMatchPr`,
  we have it in `world/Instance.h`. Moving it lets lane `w8-a`'s one unguarded
  `#include "world\Instance.h"` in `rndobj/Utl.cpp` be deleted. `obj/Object.h` is
  PCH-reached, so this is a whole-tree rebuild and belongs on its own change.
- Report the `run_symbol_sweep(kind="functions")` pairing bug upstream.

### Phase 2 — dispatched at `980e4d5e4`

Six lanes. The pool is every remaining authorable row under 200 B in any band
(158 rows / 18,068 B), plus the two derivations phase 1 made possible.

| lane | rows | bytes | shape |
|---|---:|---:|---|
| `w8-g` | 51 | 5,104 | `utl/`, `os/`, `obj/`, `math/`, `keygen_xbox` — 30 units, 8 at 0 % |
| `w8-h` | 33 | 4,024 | `rndobj/`, `rnddx9/`, `world/` — 21 units, 7 at 0 % |
| `w8-i` | 35 | 4,624 | `char/`, `gesture/`, `hamobj/` — 27 units, 1 at 0 % |
| `w8-j` | 39 | 4,316 | `synth/`, `ui/`, `net/`, `flow/`, `meta/`, misc — 25 units, 15 at 0 % |
| `w8-k` | 9 | 3,032 | the 0 % rows ≥ 200 B that phase 1 left — adjudication as much as closing |
| `w8-l` | 25 | 24,800 | **single-function unit completions**, every row already above 99.9 % |

`w8-l` is the highest-leverage list in the wave: each of its 25 rows is the only
function keeping its unit from 100 %, so each close moves the complete-units
metric directly. 102 more are parked in `~/tmp/dc3-wells/w8/backlog-unit-completions.md`.
