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

### Phase 2 — results (six lanes, all landed at `3b251bedd`)

| measure | wave start `bb2e759eb` | phase-1 close `980e4d5e4` | phase-2 close `3b251bedd` |
|---|---:|---:|---:|
| Matched functions | 31,228 | 31,310 | **31,352** |
| Matched code | 5,544,916 B | 5,557,336 B | **5,562,916 B** |
| XEX-total headline | 48.747 % | 48.856 % | **48.905 %** |
| Fuzzy | 55.4588 | 55.5577 | **55.5859** |
| **Authorable functions (canonical)** | 96.80 % | 97.05 % | **97.19 %** |
| Authorable complete units | 623 / 967 | 629 / 967 | **640 / 967** |
| Remaining authorable | 1,032 fns / 620,028 B | 950 fns | **906 fns / 601,508 B** |

Phase 2 contributed **+42**; the wave total is **+124** matched functions and
**+18,000 B** of matched code. Every lane landed with **zero DOWN rows** and all
four build guards at exit 0 on both sides of its merge.

| lane | merge | commits | matched | what it was |
|---|---|---:|---:|---|
| `w8-k` | `f5a8d0a80` | 4 | +9 | the 0 % rows >= 200 B phase 1 left |
| `w8-h` | `c41bcd7a1` | 5 | +4 | `rndobj/`, `rnddx9/`, `world/` |
| `w8-g` | `a153a143b` | 10 | +12 | `utl/`, `os/`, `obj/`, `math/` |
| `w8-l` | `04b7b6332` | 10 | +3 | single-function unit completions |
| `w8-j` | `bd1be9e1e` | 9 | +8 | `synth/`, `ui/`, `net/`, `flow/`, `meta/` |
| `w8-i` | `3b251bedd` | 12 | +6 | `char/`, `gesture/`, `hamobj/` |

Those per-lane commit counts are taken from each merge's own parent range
(`<merge>^1..<merge>^2`). Counting `980e4d5e4..<branch>` instead reports
4/5/10/10/9/12 as 6/11/21/31/40/53, because each branch was rebased onto main
*after* the previous lane merged and therefore contains every earlier lane's
work. The corrected figures sum to 50, which with this phase's 3 doc commits is
exactly the 53 non-merge commits git reports since the phase-1 close.

### A config rename is invisible to an UP/DOWN diff

`w8-j` measured **+8** matched with only **three** UP rows. The other five were
`symbols.txt` re-anchors, and a re-anchor changes the report *key*: the row
leaves under one name and reappears under another, so it is a disappearance plus
an appearance rather than a delta. Keying the comparison on the symbol name
alone additionally collapses any symbol that also moved units — it reported 3
gone / 3 new where the truth was 5 and 5.

**Reconcile on `(unit, name)` and require the arithmetic to close before
merging.** For `w8-j`: `3 UP + 5 new-matched - 0 gone-matched = +8`.

The check that makes a re-anchor a *free* win, rather than the rename that just
moves a 0 % from one object to another, is that **every displaced name measured
0.0000 and every replacement measured 100.0000**. Verify both sides. All five of
`w8-j`'s cleared it, which is why that lane's re-anchors are the genuine
wrong-object-binding class:

| address | was bound to | belongs to | row before / after |
|---|---|---|---|
| `0x8255A0A0` | `MakeString<ReqType>` | XLSPConnection | 0.0 -> 100.0 |
| `0x82563B08` | `MakeString<char[14]>` | JsonUtils | 0.0 -> 100.0 |
| `0x82793CA8` | `operator<< ObjDirPtr<HamScrollSpeedIndicator>` | UILabel | 0.0 -> 100.0 |
| `0x82860908` | `jpeg_free_small` | zutil | 0.0 -> 100.0 |
| `0x82E1C720` | `__uninitialized_copy<const ActionRec*>` | HeldButtonPanel | 0.0 -> 100.0 |

### Behavioural bugs fixed in phase 2

Each was adjudicated against the target listing by the coordinator, not accepted
from the lane that reported it.

- **`CharSignalApplier::Handle` skipped its own superclass** — the body was a
  bare `return Hmx::Object::Handle(d, b);` where the image forwards through
  `CharWeightable` first. 93 of 109 instructions were deleted before the fix.
- **`RndMesh::TransformNormal` dropped its transpose**, taking column dots where
  the image takes row dots. **99.683 before and after** — the canonical ruler
  forgives the permutation, so this bug is invisible to the score and was only
  found by reading the listing for correctness.
- **`SpotMeshEntry::_M_erase` divided the element count twice.** The target has
  exactly one `divw.` at `0x8282292C`.
- **`PlatformMgrOpCompleteMsg` carried a name suffix the image never had** —
  the literal is `platform_mgr_op_complete`, and no DTA under `orig-assets`
  references the longer spelling.
- **`SkeletonExtentTracker::GetViewBox` anchored the box to the wrong edge.**
  At `0x82DFE79C` the image loads `lfs f0, 0x30(r4)` and that same `f0` reaches
  `stfs f0, 0x4(r3)`; the `0x38` field is consumed only by the height `fsubs`
  and never stored. Field names confirmed with `lookup_struct_offset` rather
  than inferred from shape: `0x30` is `mMinY`, `0x38` is `mMaxY`. Every view box
  sat at the top of the tracked extent instead of the bottom.
- **`MoveAsyncDetector` instantiated the wrong `MakeString`** — gratuitous
  `(char *)` casts gave us `MakeString<char*>` where the image calls
  `MakeString<const char*>` at `0x8252EBA8`. Visible **only** under `name_check`.

### Native gate

**Green on `3b251bedd`, the fully merged wave.** `gate exit 0` — 506 registered,
437 executed, 437 passed, **0 failed**, 69 skipped against a budget of 69. The
skipped-suite block is **byte-identical** to the `a153a143b` run (48
`GameplayTelemetryTest`, 4 `MoggDecodeTest`, 4 `BinkFFmpeg`, 3 `MoggV0xETest`,
3 `FFmpegIntegration`, 2 `BikAudioTest`, 2 `AudioDevice`, and one each of
`ManualReproTest`, `HeadlessBootTest`, `ExtractBik`), so coverage neither shrank
nor grew and there is no driver or asset artefact behind the count. Log:
`~/tmp/dc3-wells/w8/native-gate-3b251bedd.log`.

⚠ Compare the gate's own **"Skipped suites"** block, not a regex over the log.
A first attempt here matched `[A-Za-z0-9_]+\.[A-Za-z0-9_]+` across the whole
file and reported the two runs as differing — every difference it found was a
ctest *duration* (`0.04`, `0.68`, `0.70`). A broken check that reports a
difference is luckier than one that reports a match, but neither is evidence.

### Where the 0 % band ended up

| band | fns | bytes |
|---|---:|---:|
| exactly 0 % | 18 | 2,976 |
| under 80 % | 33 | 31,052 |
| 80–95 % | 172 | 79,492 |
| 95–99.9 % | 506 | 406,416 |
| 99.9 – under 100 % | 114 | 79,888 |
| **total remaining** | **843** | **599,824** |

link_glue and the vendor prefixes excluded. Including link_glue the total is
919, which is exactly 13 more than `progress_metrics.py`'s 906 — its deduped
shadow rows. The instrument agrees with itself.

**The 0 % class went from 119 real rows / 19,984 B to 18 rows / 2,976 B.** Of
those 18, six are structurally unscoreable: `merged_8237A7E8` (CharEyes) and
`merged_ObjPtrListRemove` (TypeProps) carry *our own* `merged_*` label, and the
`UI.cpp` and `jcmaster.c` rows were recorded in-source by their lanes. The
genuine residue is about a dozen rows — six `AmbientOcclusion` STL sort
instantiations (656 B) plus `LightPreset::erase` and
`ObjPtrList<EventTrigger>::Unlink` — all of which answer to the explicit
instantiation lever that paid throughout this wave.

**The two largest survivors are a judgement call, not a gap.** `fn_8263A168`
(504 B) and `fn_8263A360` (556 B) in `rndobj/Mesh` are 1,060 B, 36 % of the
remaining zero bytes. `ham_xbox_r.map` shows `?PackVector@@` and
`?FillCompressedVertex@@` **twice each**, bare `f` both times, in
`rnddx9:Mesh.obj` *and* `rndobj:Mesh.obj`; `symbols.txt` binds the rnddx9
addresses, where they score 96.19 % and 99.96 %. A rebind moves the naming
rather than creating it. But "zero-sum" understates the choice in one direction:
the score banked on the rnddx9 side is **partial credit**, which feeds fuzzy and
the code percentages and contributes **nothing** to `matched_functions` or
`matched_code`, since neither row is at 100. So the real trade is up to **+2
matched functions and +1,060 B of matched code** if the rndobj copies reach 100,
against a certain loss of ~1,041 B of partial credit. Testable, and worth
testing — decide it by measuring rather than by quoting this paragraph.

### Carried findings, added in phase 2

- **`scripts/symbol_aliases.json` group 348 is missing a fold.** Group 348 is
  `OnlyReturns@0x823e3b70` with 523 folded members, and
  `??0PaddedJointPos@@QAA@XZ` is in **no** group at all — an unwitnessed pair in
  the alias *source*, upstream of `icf_aliases.map`. `name_check` therefore
  charges an ICF fold it should forgive, holding `??0SkeletonFrame@@QAA@XZ` at
  **99.20** while it is 100.0 normalized.
- **`HamDirector::CollideList` is a class-layout finding.** Both sides load
  `0x114(r3)` and dispatch slot `0x2c`, but the image uses the primary vtable
  unadjusted where we emit `addi r3, r11, 0x9c`. The cause is the base order of
  `class RndDir : public ObjectDir, public RndDrawable, ...` in
  `src/system/rndobj/Dir.h`; changing it moves every RndDir consumer.

Both were left alone deliberately, for the same reason as the `authorable.py`
correction above: each would move a ruler, a denominator or a class layout
underneath every lane's before/after. They belong after the wave, each on its
own commit, stating the before and after explicitly.
