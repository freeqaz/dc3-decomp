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

- **`scripts/symbol_aliases.json` group 348 is missing 246 of its own members**,
  and this is now a number rather than an anecdote. Counted three ways at
  `0x823e3b70`: `orig/373307D9/ham_xbox_r.map` places **769** names there,
  `symbol_aliases.json` group 348 (`OnlyReturns@0x823e3b70`) lists **523** folded
  members, and the generated `build/373307D9/icf_aliases.map` carries **525**
  entries. So the alias source captures 68 % of one witnessed `/OPT:ICF` fold
  class, and **every missing member charges a relocation-name row that
  `name_check` exists to forgive.**

  Two lanes hit it independently from opposite ends: `??0PaddedJointPos@@QAA@XZ`
  is in no group at all, holding `??0SkeletonFrame@@QAA@XZ` at **99.20** while it
  is 100.0 normalized; and `MetagameRank`'s `vector<Unlockable*>` copy ctor reads
  100.0 with **zero mismatch rows** over all 28 instructions while its whole
  0.179 is one relocation name.

  ⚠ **Do not widen it to buy rows.** That lane was asked to complete its unit and
  declined this route explicitly, which was the right call: changing what the
  ruler forgives binary-wide in order to close a row is laundering, and it would
  move every other lane's before/after. It belongs on its own commit, derived
  from the map rather than hand-edited, stating the before and after headline —
  the same discipline as the deferred `authorable.py` denominator fix.
- **`HamDirector::CollideList` is a class-layout finding.** Both sides load
  `0x114(r3)` and dispatch slot `0x2c`, but the image uses the primary vtable
  unadjusted where we emit `addi r3, r11, 0x9c`. The cause is the base order of
  `class RndDir : public ObjectDir, public RndDrawable, ...` in
  `src/system/rndobj/Dir.h`; changing it moves every RndDir consumer.

Both were left alone deliberately, for the same reason as the `authorable.py`
correction above: each would move a ruler, a denominator or a class layout
underneath every lane's before/after. They belong after the wave, each on its
own commit, stating the before and after explicitly.

### Phase 3 — dispatched at `0e1dcb139` (2026-09-30)

Phases 1 and 2 are closed and pushed (`980e4d5e4..c4a3f18fb`). The frontier at
dispatch, measured from a report built 2026-09-30 01:43 against the tree that
actually exists:

| measure | value |
|---|---:|
| Matched functions (XEX-total) | 31,353 |
| Authorable canonical | 97.19 % (31,316 / 32,221) |
| Remaining authorable | 905 fns / 601,084 B |
| Remaining, link_glue + vendor excluded | 842 fns / 599,400 B |
| **Phase-3 pool (>= 200 B)** | **719 fns / 585,068 B** |
| Incomplete authorable units | 326 |
| Units needing exactly ONE more fn | 149 (127,732 B) |
| Units needing exactly two | 66 (82,900 B) |

Band spread of the phase-3 pool: 7 rows under 50 %, 18 in 50-80, 39 in 80-90,
97 in 90-95, **328 in 95-99 (229,784 B)**, 134 in 99-99.9, 96 above 99.9.

Six lanes, **202 rows / 160,528 B**, a first tranche of 28 % of the pool. Lane
lists are derived per UNIT and the disjointness is asserted programmatically
before dispatch, not merely intended — overlapping lists cost this wave real
work twice (`flow/Flow` to two lanes, `os/PlatformMgr_Xbox` to two more).

| lane | rows | bytes | units |
|---|---:|---:|---|
| `w8-m` | 36 | 28,024 | `rndobj/Utl`, `rndobj/AmbientOcclusion` |
| `w8-n` | 36 | 31,088 | `hamobj/HamDirector`, `MoveDir`, `HamCharacter` |
| `w8-o` | 30 | 20,172 | `utl/MemMgr`, `math/Geo`, `math/SHA1` |
| `w8-p` | 35 | 24,108 | `rndobj/Mesh`, `rnddx9/Mesh`, `rndobj/Text` |
| `w8-q` | 33 | 26,744 | `world/Spotlight`, `SpotlightDrawer`, `SpotlightDrawer_NG`, `LightPreset` |
| `w8-r` | 32 | 30,392 | `gesture/LiveCameraInput`, `DepthBuffer3D`, `StreamRenderer`, `synth_xbox/FFT`, `Voice` |

Worklists: `~/tmp/dc3-wells/w8/phase3-<lane>.txt`, sorted lowest-percentage
first, because that is where the room is.

**Two calibrations carried into the briefs.** `w8-l` closed only 3 of 25
single-function unit completions, all of which were already above 99.9 % — so
"cheapest by size" is no longer the same as "easiest", and the derivation shows
percentage alongside size. And the previous FFT attempt reached 48.75 % / 46.93 %
from 0 % on the two AltiVec functions, so `w8-r` is told to work its other four
units first and report honestly where it lands rather than sinking its budget
there.

⚠ **Another session is live in main** and landed the `HamCamShot` rewind fix at
01:39. `hamobj/` is therefore contested: `w8-n` is told not to touch
`hamobj/HamCamShot.cpp`, and every lane will need a rebase at landing.

### Phase 3 — results for the first five lanes (`w8-r` still out)

| measure | phase-3 dispatch `0e1dcb139` | after five lanes |
|---|---:|---:|
| Matched functions | 31,353 | **31,366** |
| Authorable canonical | 97.19 % | — |

| lane | merge | matched | substance |
|---|---|---:|---|
| `w8-m` | `9c68c6a78` | 0 | `.rdata` section fix; five 0 % rows *proven* unscoreable |
| `w8-p` | `3aa82522b` | +3 | the map's COMDAT column; Mesh rebind measured an exact wash |
| `w8-q` | `16af76f8b` | +5 | inverted `DrawLenses` guard; refuted `w8-p`'s mechanism |
| `w8-n` | `f47a8c5f4` | +2 | read past a stack struct's end; inverted blink predicate |
| `w8-o` | `540476ecb` | +3 | phantom `AddHeap` parameter; SHA1 floor with a number |

Every landing was row-diffed against a **baseline worktree pinned to that lane's
own base**, not against main — another session was merging into main every few
minutes, and a `ninja` there can have its working tree change mid-build and emit
a report describing no commit at all. Main's own report was stale against main's
own HEAD twice inside one hour.

**Native gate green** at `d41a0fe1f`: 509 registered, 440 executed, **440
passed, 0 failed**, 69 skipped against budget 69, skip-suite block identical to
the `980e4d5e4` and `a153a143b` runs. Registered grew 507 → 509, so coverage
rose rather than shrank. Log: `~/tmp/dc3-wells/w8/native-gate-phase3.log`.

### Six behavioural bugs, each adjudicated against the listing

One of them was found **independently by two lanes and a third session**, which
is the strongest corroboration this process produces.

- **`Plane::Set` built a negated normal** — the image computes
  `Cross(diff21, diff31)`. At `0x82535AA4` `fmsubs f0,f8,f10,f6` stores
  `d21.y*d31.z − d21.z*d31.y` to `0x50(r1)` as cross.x. Behaviourally visible,
  not cosmetic: the consumer at `HamSkeletonConverter.cpp:410` negates a/b/c
  **again**, and the four-float `Set` overwrites the plane only on the
  `usePelvis` branch — so on `angle >= 0.2` the hip Z axis fed to IK was the
  image's value negated. `Triangle::Set` winds the same way, so a Plane and a
  Triangle from the same three points disagreed.
- **`SpotlightDrawer::DrawLenses` had guard and assert inverted** — the image
  fails iff `mLensMaterial != 0 && sDiskMesh == 0` (`0x82823EF8`, `0x82823F04`);
  we asserted the wrong condition *and* called `sDiskMesh->SetMat(NULL)` for
  every lens-less spotlight. The assert text is stale: `??_C@_0P@ICJAFDBB@` is
  15 bytes reading `"sl->LensMesh()"` and that object's literal list holds no
  `sDiskMesh` string. **RB3 carries our old inverted shape, so it is not a
  reference here.**
- **`MoveDir::PostUpdate` read past the end of a stack struct** — it cast the
  member *lvalue*, passing `&data->mSkeletonsRight`; the image loads the
  pointer's value (`0x8250532C lwz r31, 0x4(r30)`, `0x82505334 mr r4, r31`).
- **`HamCharacter::SyncObjects` inverted the blink predicate** — the image
  computes `!left.Null() || right.Null()`; at `0x8249189C` the `bne` jumps
  straight to `li r11, 0x1`, a short-circuited `||` an `&&` cannot emit.
- **`AddHeap` had a parameter the image does not have**, retracting a prior
  claim that the image passed an *undefined* 8th argument so `allowTemp` was
  garbage in the shipped game. Nothing was undefined.
- **`MoveAsyncDetector` instantiated `MakeString<char*>`** where the image calls
  `MakeString<const char*>` — visible only under `name_check`.

### The COMDAT arc: a lever, a refutation, and a number I got wrong

`w8-p` found that `ham_xbox_r.map` carries a COMDAT flag (`f i` vs bare `f`)
nobody had read, and closed three rows certified as floors by adding `inline`.
`w8-q` then measured the stated mechanism and **refuted it**: MSVC/Xenon puts
every function we compile into its own COMDAT, so no `.cpp`-vs-header spelling
moves a symbol between map classes. The carrier is the COMDAT **selection type**
at aux-record offset 14, and we emit `NODUPLICATES` for both of `w8-p`'s own
control symbols — reproducing neither class. It also showed the lever did not
move the two floors it was reached for: `BuildBeam` stayed byte-identical at
85.3415, and `ApplyLightingApprox` is out of reach because all its callees are
cross-TU. The page now carries the lever **and** the refutation, with usage
starting at "prove the callee is same-TU".

⚠ **I then misreported the surface twice.** Whole-binary it is **1,898**
selection mismatches (536 ours-`NODUPLICATES`/image-`ANY`, 1,362 reverse) over
78,352 compared — not the "7 in 1,396" I broadcast to three lanes, which was a
legitimate four-unit figure I generalised after reading the whole-binary run
through a `tail -40`. The tool printed correct summary counters; my `tail` kept
the rows and discarded them. A correct instrument plus a truncated read produces
the same wrong belief as a truncated instrument.

### The two rulers, quantified

`matched_functions` counts `match_percent_normalized == 100`; `matched_code`
sums size at **`fuzzy_match_percent == 100`**. Whole-binary, **257 functions /
184,832 bytes read normalized 100.0 and bank zero bytes** — largest
`RndMat::SyncProperty`, 7,920 B at fuzzy 99.9924. That is why `w8-p`'s +3
functions is only +360 B. Also: `run_objdiff`'s parenthesised "(99.5 % raw)" is
`raw_match_percent`, a **third** axis — not fuzzy.

### Coordinator defects in this tranche, and the guard that now blocks them

Four, all mine, each now refused by `~/tmp/dc3-wells/w8/land_preflight.sh`
rather than left to memory. A check that tested for the **absence** of a
`rebase-merge` directory reported "rebase COMPLETE" on a rebase that never
started, and the resulting diff showed 8 phantom DOWN rows. The baseline was
pinned **after** the step that can fail, so a hand-resolved rebase left it a
merge behind and credited 4 of 9 UP rows to the wrong lane. I rebased **two live
lanes** — one by acting on a report the notification had flagged as interim, one
by using a live lane as a self-test fixture (the script now needs `DRY_RUN=1`).
And I ran `ninja` inside a working lane's tree while probing a tool.

### Carried findings from phase 3

- **`HamDirector::CollideList`** — 76 B at 84.16 %, and all six rows are
  downstream of one fact: the base order of `class RndDir` in
  `src/system/rndobj/Dir.h`. Whoever changes that header gets the row free.
- **`SpotlightDrawer::DrawMeshVec`** calls the **wrong virtual** — the image
  dispatches a one-argument virtual at slot 1 of the unadjusted vptr
  (`li r4,0` / `lwz r11,0x0(r30)` / `lwz r11,0x4(r11)`), and
  `RndDrawable::Highlight` is `void(void)`. Spelling it `Highlight()` measures
  10.5 pp worse, so the `reinterpret_cast` is faithful.
- **`HamDirector::OnSetDircut`** is one instruction from 100 % on 356 B: image
  `li r5, 0x0`, ours `mr r5, r28` reusing a CSE'd zero.
- **`?Mod@@YAMMM@Z`** is `f i` in the map and out-of-line in
  `char/CharLipSyncDriver.cpp:18`; many callers.
- ⚠ **`Plane::Set` has no test coverage.** No test in the suite names
  `HamSkeletonConverter`, `Plane`, `BSP`, `Frustum` or `Geo`, so the green gate
  is general-regression evidence only for a change that alters IK behaviour. I
  checked for native-side compensation and found none (no `HX_NATIVE` guard in
  that file), so the composition is faithful — but a targeted test for the hip Z
  axis is genuinely missing and would be the highest-value test to add.

### Phase 3 closed — `w8-r` landed, and what the numbers say about phase 4

`w8-r` merged as `d467b964a`: **+4** (31,367 → 31,371), five UP rows, zero DOWN,
all guards green. Its `StreamBufferData` fix was the **second** convergence of the
tranche — another session landed the same bug as `78fd9a1a4` while the lane was
working, exactly as happened with `Plane::Set`. Main's spelling was kept in both
cases; the lanes' evidence was preserved.

  98.018 -> 100.000  Voice::IsPlaying                        444 B
  93.940 -> 100.000  TextureStore::UpdateFromDepthBuffer     332 B
  94.329 -> 100.000  LiveCameraInput::GetTweakedAutoexposure 328 B
  96.345 -> 100.000  LiveCameraInput::Init                   220 B
  98.374 ->  98.908  LiveCameraInput ctor                   1520 B

Two more bugs, verified against the listing before landing. The **double buffer
never advanced**: the image derives a stream index from the buffer type
(`cmpwi r31, 0x2` → `li r11, 0x1`, else a `subfic`/`subfe` mask giving
`idx = (type != 3) ? type : 0`), then reads `mFrames[mReadIdx]` via
`lwz r11, 0x1458(r11)` = `mStreams` + 0x10; we indexed `mStreams[type]` with a
constant frame, so `kBufferPlayer` always returned null and colour/depth returned
the wrong half of the double buffer on alternate frames. And the ctor built
**`SpeechMgr` from the wrong array** — `mr r25, r3` at `0x82432E78` captures
`FindArray("speech")` and `mr r4, r25` at `0x82432FA8` immediately precedes the
ctor call; `kinectArr` lives in r23 and is never passed.

A third retraction the lane earned: both `speechArr` locals are read
**uninitialised** in the image and faithfully reproduced (nulled under
`HX_NATIVE`), which retracts a prior reading of two ctor rows as "the image
spills and reloads across that join".

**COMDAT cross-validation.** `w8-r` ran the selection audit over its five objects
— 689 compared, 10 mismatches, **zero** actionable — and its per-object counts
(Voice 8, LiveCameraInput 2) match the whole-binary run exactly. FFT's callees
*are* same-TU, so the lever was not excluded by construction there; it was
excluded by measurement finding nothing. FFT moved 0 pp, as the brief's
calibration predicted.

### Phase 3 final

| measure | dispatch `0e1dcb139` | close `d467b964a` |
|---|---:|---:|
| Matched functions | 31,353 | **31,371** (+18) |
| Matched code | 5,563,340 B | **5,568,116 B** |
| XEX-total headline | 48.91 % | **48.95 %** |
| **Authorable canonical** | 97.19 % | **97.25 %** (31,334 / 32,221) |
| Remaining authorable | 905 fns / 601,084 B | **887 fns / 593,996 B** |
| Complete authorable units | 640 / 967 | **640 / 967** |

+18 overall: +17 from the six lanes (0, +3, +5, +2, +3, +4) and the remainder
from the other session's convergent fixes.

**Native gate green** on the fully merged tree: 510 registered, 441 executed,
**441 passed, 0 failed**, 69 skipped against budget 69, skip-suite block
identical to every earlier run. Across this session registered grew 506 → 510
while the skip count never moved, so coverage rose. Log:
`~/tmp/dc3-wells/w8/native-gate-phase3-final.log`.

### The planning finding: closing rows and completing units are nearly disjoint

**Phase 3 closed 18 functions and moved the complete-units metric by exactly
zero.** Units needing one more function is still **149 / 127,732 B**, units
needing two still **66**, incomplete units still **326** — all unchanged. That is
not an error: the lanes were given concentration units (18, 15, 14 rows each), so
a unit going 15 → 14 never touches the one-short bucket.

So the two obvious derivations pull in opposite directions, and a wave has to
choose. Phase 3 chose row count and got it. To move complete-units, phase 4 must
target the 149 one-short units directly — and unlike `w8-l`'s experience (3 of 25,
all above 99.9 %), several of the cheapest now have real room:

| bytes | current | unit |
|---:|---:|---|
| 124 | **78.387 %** | `synth/MoggClip` |
| 80 | 85.000 % | `hamobj/FilterVersion` |
| 80 | 87.000 % | `math/Rand` |
| 72 | 88.889 % | `utl/KnownIssues` |
| 96 | 91.667 % | `utl/EncryptXTEA` |
| 120 | 93.333 % | `synth_xbox/SynapseAPO` |
| 116 | 94.448 % | `os/JoypadClient` |
| 168 | 97.619 % | `ui/UILabelDir` |
| 176 | 97.727 % | `synth/StandardStream` |

⚠ **Exclude** the three cheapest one-short rows from any such list: `jcmaster`
(24 B), `ZlibLicense` (24 B) and `synth_xbox/SynthSample` (132 B) all sit at
0.000 % and were already adjudicated structurally unscoreable by earlier lanes.
Dispatching them would burn a lane on rows nothing can score.

Bands at close: 18 rows at exactly 0 % (2,976 B), 32 under 80 %, 56 in 80–90,
109 in 90–95, **344 in 95–99 (228,436 B)**, 153 in 99–99.9, 112 above 99.9.

### Carried finding: a broken one-shot floods stderr in the IK diagnostic

Found incidentally by lane `w9-d` while adjudicating a gate failure, and left
unfixed deliberately — the file belongs to another session's IK work and no lane
owns it.

`src/system/hamobj/HamIKEffector.cpp` declares one `static int sTotalWeightLog`
and uses it for **two** blocks. The `TypePropsDump` block guards on
`sTotalWeightLog < 3` and **never increments it**; only the later `IkSnap` block
(`sTotalWeightLog < 60`) does. Whenever the first block's conditions hold and the
second's narrower ones do not — which happens once the ankle effector reaches the
gameplay pose — the "one-shot" fires every frame. Measured: **4,594 copies** in
one run, whose log was **508 KB against ~97 KB** for its sibling tests.

⚠ **CORRECTED, and my correction was the reassuring half — which is the half to
verify hardest.** I wrote that this was runtime-gated by `getenv("DC3_IK_DIAG")`
and therefore "not live in an ordinary build". **False.** I saw
`getenv("DC3_IK_DIAG")` at two nearby lines and assumed it enclosed the block at
663 without checking the scope; the block actually sits inside
`if (sFCCount < 40 && fcMain && fcLA && t == kEffectorTypeAnkle …)`, which tests
no flag at all. All three trace blocks ran in **ordinary native builds**.

The session that owns the file measured it from that day's harvest logs, all run
**without** `DC3_IK_DIAG`: **7,201** `TypePropsDump` lines in a perform run and
**8,561** in each of two dance-battle runs. So unbounded stderr was firing every
gameplay frame of every native run, not in a diagnostic configuration.

**Fixed by that session**, not by this wave: `8e155163c`, merged as `310c45d29` —
`TypePropsDump` gets its own counter and all three IK trace blocks now actually
require `DC3_IK_DIAG`. Its own earlier commit `c28f56f1a` had moved the
frame-3000 threshold that used to quiet it, which is why it became loud today.

The lesson for me is narrower than the bug: **checking that a flag exists nearby
is not checking that it guards the block in question.** A `grep` for the flag
name returns hits from every sibling block, and those hits read as reassurance.

⚠ **Related measurement hazard, worth more than the defect.** That gate failure
(`DtaFlowTest.NoCrashCleanExit`, exit 8, `exitCode 124`) was **environmental, not
source**. The lane proved it the right way: checked out the base version of the
only file it had changed, rebuilt both native targets, re-ran, and got an
identical failure at 120.54 s. The box was at **load average 216–235** with six
lanes building concurrently.

So: with a fleet of lanes running, **a per-lane native gate cannot distinguish a
real regression from machine load** — a green is luck and a red is noise — and
each run makes it worse for the others by building the native port and then
running an engine under a wall-clock cap. The remaining lanes were told to skip
the gate entirely and report behavioural changes with proving addresses instead;
the gate is run **once, serially, on merged main**, which is the tree that
actually ships. Note the lane's discriminator is the transferable part, not the
verdict: when a gate fails under load, revert your own change and re-run before
believing it.

### Retraction: I invented a `__LINE__` mechanism that this repo had already refuted

While applying a cross-session handover I wrote, in the commit message of
`5a20b9f55` and in two messages to the session that owns the finding, that
`MILO_ASSERT` bakes `__LINE__` into emitted bytes — and therefore that inserting
or deleting a line before an assert shifts its constant and changes the object.
**That is false**, and I then built on it twice: I preserved a line count that did
not need preserving, and I advised holding a handover after counting 40 assert
macros whose count was irrelevant.

`src/system/os/Debug.h:93` is `#define MILO_ASSERT(cond, line)` — the line is an
**explicit argument**, passed to `MakeString(kAssertStr, __FILE__, line, #cond)`.
Call sites read `MILO_ASSERT(ptr, 0x67)`. The macro uses `__FILE__` but never
`__LINE__`, so no amount of line insertion or deletion can move an assert
constant.

This repo had already measured it, and the write-up predates my claim by two
weeks: `docs/decomp/patterns/comments-are-inert-except-at-__LINE__.md`, indexed
as *"Adding a comment to a .cpp is INERT — the assert-line-shift story is false
here"*. Whole-binary probe: one comment line prepended to **all 1,188** `src/`
sources, full `ninja`, 48,365 functions compared, **exactly 1** moved — and that
one from a literal `__LINE__` at `synth_xbox/SynthSample.cpp:33`. Negative
control: 9 comment lines above 68 assert sites in `Mesh.cpp` and `Dir.cpp` moved
**0**.

**How I got there is the part worth keeping.** I had the disconfirming evidence in
front of me: I read `MILO_ASSERT(type < kBufferNum, 0x1FC)` in a conflict hunk
earlier the same day. An explicit hex line number is *exactly* what refutes the
`__LINE__` story, and I read past it because the story was plausible and explained
an observation someone else had handed me ("deleting an HX_NATIVE block changed
the object"). A mechanism that explains a real observation feels confirmed by it.

**What is actually true** is narrower and still unexplained: deleting one
`HX_NATIVE` block from `Font3d.cpp` changed `Font3d.obj` while every `report.json`
score stayed identical. **Cause undetermined — not asserts.** The honest rule is
the boring one: hash the object after any edit you believe is inert, and if bytes
move with no score change, find the cause rather than naming one.

Consequences corrected: the line-count care in `5a20b9f55` was harmless but
unnecessary; the "preserve line numbers before any assert macro" rule is
withdrawn; and the `HamDirector` dead-native-walk handover is **not** blocked by a
40-assert cost, because that cost does not exist. It needs an object hash and a
row diff like any other edit.

### A concurrent build in the same checkout SIGBUSes the compiler through the PCH

The wave's closing native gate returned wrapper **exit 6 (build failed)** with **18
objects** dying on `clang++: error: clang frontend command failed with exit code
135` — full LLVM stack traces, no source diagnostic. Not load: 300 GB free disk,
33 GB available RAM, load 59 at launch.

**Exit 135 is 128+7, SIGBUS.** In a compiler that means a memory-mapped input was
rewritten underneath it, and the mapped input in a CMake build is the
**precompiled header**. The crash preamble named the site:

```
1. <eof> parser at end of file
2. <invalid>: instantiating function definition 'ObjPtr<Task>::ObjPtr'
3. <invalid>: instantiating function definition 'ObjRefConcrete<Task>::ObjRefConcrete'
```

**What actually happened.** I launched the PPC `ninja` and `scripts/native_test.sh`
*in parallel in the same checkout*, reasoning they were isolated because one writes
`build/373307D9` and the other `native/build`. That is true of the PPC build alone —
but `native_test.sh` **builds and can reconfigure**, and another session had just
landed a change shrinking `obj/ObjPtr_p.h` from 37,618 to 34,606 bytes, so
`native/build`'s PCH needed rebuilding. Compiles that had mmap'd the old PCH met a
rewritten one.

Three measurements, converging:
- Re-running the exact failing command verbatim gives a clean diagnostic, **not** a
  crash: `fatal error: file ObjPtr_p.h has been modified since the precompiled
  header was built: size changed (was 37618, now 34606)`.
- Rebuilding the PCH and the same object through `ninja`, nothing else running,
  **exits 0**.
- The session that owned the suspect commit built `milo-viewer` from scratch in a
  **fresh worktree with a private build dir**: **881/881 objects, exit 0, zero
  crash markers.** Source exonerated.

**Two rules worth keeping.**
1. **"Different build directories" is not isolation when one of the jobs is
   `native_test.sh`.** It builds, and it may reconfigure. Run it alone, and verify
   nothing holds its build dir first — scoped by **cwd**, never by process name.
2. **Gate after any merge that touches a PCH-reached header, not only at the end of
   a wave.** `tree_sha256` identical proves the *PPC* graph is untouched and says
   nothing about whether `native/build`'s PCH survives that header changing size.
   Separate graphs, separate staleness.

⚠ **And a process failure of mine worth recording next to it.** I messaged the
owning session that their commit was the likely cause *before* I had a
reproduction — on a strong correlation (their change shrank exactly the header the
PCH was stale against) plus a plausible mechanism. That is the same shape as the
`__LINE__` error retracted above, committed within the hour, against a different
person's work. I sent the correction as soon as the PCH diagnostic appeared, which
is the only part of the sequence worth repeating. **Correlation plus a plausible
mechanism is not attribution, and the cost of a wrong attribution falls on someone
else's afternoon.**

## Wave 8 closed — gated, with one regression caused and fixed

**Native gate green** on `54365d0c3`: 584 registered, 515 executed, **515 passed,
0 failed**, 69 skipped against a budget of 69, skipped-suite block identical to
every run in this wave. Both patch-state guards exit 0.

| measure | wave start `bb2e759eb` | close `54365d0c3` |
|---|---:|---:|
| Matched functions (XEX-total) | 31,228 | **31,393** (+165) |
| Matched code | 5,544,916 B | **5,574,424 B** |
| **Authorable canonical** | 96.80 % | **97.32 %** (31,356 / 32,221) |
| Complete authorable units | 623 / 967 | **646 / 967** (+23) |
| Remaining authorable | 1,032 fns / 620,028 B | **865 fns / 585,908 B** |
| XEX-total headline | 48.747 % | **49.01 %** |

Twelve lanes across four phases, every landing row-diffed against a baseline
pinned to that lane's own base, **zero DOWN rows** on all twelve, all four build
guards green throughout. **Thirteen behavioural bugs**, each adjudicated against
the target listing by the coordinator rather than accepted on a lane's report —
and two of them independently rediscovered by a concurrent session, which is the
strongest corroboration this process produces.

### The regression I caused, and what it cost to find

`w9-e` removed an invented `- 1` from `HamNavList::OnMsg`'s ScrollDown edge. The
removal is **faithful** — verified twice at `0x8244A124`/`0x8244A128` — and it was
**still a regression**, because that `- 1` was compensating for a *second*
divergence in native scroll behaviour. Without it, four d-pad downs in song select
alternate between display indices 6 and 3, both on a `song_tier_1` header; the
cursor never advances, song select never exits, and 8 DtaFlow tests fail.

Cost: a **five-step bisect across two sessions**, ~6 min per step, plus three
full-gate runs. Reverted at `54365d0c3` with the debt recorded at the line;
measured 9/9 DtaFlow green before landing. The concurrent session owns the native
fix and will re-land the faithful comparison on top.

> ⚠ **Correction (2026-09-30, branch `native-navscroll`): there was no second
> divergence.** Instrumented per press, native scrolls exactly as the listing
> does. Song select enters on index 2 (`song_select.dta`, perform branch:
> `scroll_to_index 2 2`) and the list is `0 playlists, 1 random_song,
> 2 song_tier_0, 3 ymca, 4 betteroffalone, 5 thehustle, 6 song_tier_1,
> 7 starships`. Tier headers are *active* rows (`NavListHeaderNode::IsActive` →
> `IsEnabled`, both 100 %), so the four scripted downs faithfully end **on the
> song_tier_1 header**. The `- 1` only "worked" because it fired the ScrollDown
> one row early and hopped the cursor over index 6 — the `ymca.txt` flow had been
> playing **starships** (`select selected=7 sym='starships'`). The "6 ↔ 3
> alternation" was not the d-pad at all: it was the input runner's three
> multiuser confirms, fired after the multiuser `wait_screen` timed out, toggling
> header mode (song_tier_1 is index 6 expanded, index 3 collapsed). The fix
> *was* the input script: one down, derived from the list. Item 4 below is
> therefore itself retracted.

**The process failure that made it expensive** is worth more than the fix. I
landed six merges containing behavioural changes and gated **once, at the end** —
two of those changes were described in their own merge messages as altering
navigation selection and the RNG sequence. Gating after each behavioural merge
would have named the culprit in one step. The gate exists to catch what review
cannot, and I spent it after the point where it could isolate anything.

**And the blind spot behind it:** I briefed all twelve lanes to read for *meaning*
rather than score, and never once asked whether a **correct** change could break
something that had been compensating for the bug. That is the inverse of the
failure mode I was warning against, it is documented in this repo as "a native
rewrite masked a mis-decompiled function", and I walked into it anyway.

### Four mechanisms I asserted and had to retract, in one session

Recorded together because the pattern matters more than any one of them. Each was
a plausible story that *explained an observation*, which is exactly what makes a
guess feel like a finding.

1. **`__LINE__` in `MILO_ASSERT`** — refuted by a whole-binary probe already in the
   repo, and by the macro signature. I had read `MILO_ASSERT(type < kBufferNum,
   0x1FC)` the same day and read past it.
2. **"A peer's commit broke the native build"** — asserted on correlation plus a
   plausible mechanism, before I had a reproduction. The cause was my own
   concurrent build.
3. **"It's my build directory"** — refuted by the peer's clean-room reproduction;
   the regression was real.
4. **"Re-tune the input script"** — refuted by reading the log I had already
   captured: a cursor that *alternates* cannot be fixed with more presses.
   ⚠ *This retraction was itself wrong* (see the correction above): the
   alternation came from later confirms toggling header mode, not from the
   d-pad, and re-deriving the script's down count from the list was the fix.

What made these recoverable rather than damaging: each was stated precisely enough
to be falsified, and corrected the moment a disconfirming fact arrived. Three of
the six phase-4 lanes used the same discipline on themselves — one reverted its
own 100 % win after tracing callers, one retracted a "do not re-dig" note, one
withdrew a metrics claim after checking the source.

## Wave 9 close — pushed, and a cross-session hand-back landed on a valid instrument

### The push, and the gap between "gated" and "pushed"

`3b19971b4..4dc1df0ac`, **158 commits**. The gate that authorised it ran on
`64f20a202`, alone on the box: **616 registered / 547 executed / 547 passed /
0 FAILED / 69 skipped against a budget of 69**, exit 0.

Main moved **twice** between the gate finishing and the push landing, so the tree
pushed is not the tree gated. The delta is `44e39604b` + its merge `4dc1df0ac`,
and it is **docs only** — `docs/INDEX.md` plus a 297-line `docs/plans/XENIA_ORACLE.md`,
no source — so the gate still covers every line of code that went out. Recorded
because "gated, then pushed" otherwise implies one tree, and on an actively-merged
main that is the default case rather than the exception: the count I asked
approval for was 115, the count at gate time was 156, the count pushed was 158.
**Report the count actually pushed, not the count approved.**

### The DtaFlow regression is independently confirmed fixed

All **11** DtaFlow tests pass, including the two new regression tests
`DtaFlowTest.SongSelectPicksYmca` and `DtaFlowSongSelectScrollTest.ScrollDownEdgeMatchesImage`.
That is the check the earlier failure deserved and did not have: the test that
would have caught the original defect — the "ymca" flow silently playing
`starships` because the scroll comparison carried an extra `- 1` — now exists and
passes. Source confirms the faithful form is what is in the tree
(`src/system/hamobj/HamNavList.cpp:1638`, `>= firstShowing + HamListRibbon::sNumListSelectable`,
**zero** occurrences of the `- 1` form), and `scripts/dc3-input-flows/ymca.txt`
issues a single `+30 down` derived from the list rather than a bumped count.

### Taking delivery of `native-notables`: the hash equality had to be re-measured at one path

The peer session handed back a 6-commit branch and reported, as evidence of PPC
inertness, `tree_sha256` **unchanged** at `3916724ace0333f1` while
`MakeBSPTree` "now compiles the Xbox body". I flagged that as contradictory. **It
was not, and the error was mine**: the guard was `#ifndef HX_NATIVE`, so PPC
already compiled the Xbox body — the branch gives it to *native*. An unchanged PPC
hash is the expected reading.

Two facts make the inertness claim load-bearing rather than lucky, because line
numbers genuinely shift (net **+5** for the `_S_sort` lines and everything after
them inside `MakeBSPTree`):

- **The PPC build carries no debug info at all.** Zero `/Z7` or `/Zi` anywhere in
  `build.ninja`; `Geo.obj`'s cflags are `/nologo /wd4355 /wd4164 /c /GR /O1 /Oi
  /EHsc /TP` plus includes. No CodeView means no line table for a shift to perturb.
- `grep -c __LINE__` is **0** in all five source files the branch touches.

**`tree_sha256` is only evidence at one path.** MSVC writes the source path into
`S_OBJNAME`, so two worktrees legitimately disagree, and the peer's
`3916724ace0333f1` is not comparable to anything measured here. The A/B was
therefore run in **one** worktree (`/home/free/tmp/nn-verify`), full `ninja` at the
base and then at the tip:

| | base `64f20a202` | tip `a6c7b1a7a` |
|---|---|---|
| `tree_sha256` | `375f971236a09d06…fe02d93` | **identical** |
| `matched_functions` | 31,393 | 31,393 |
| row diff | — | **UP 0, DOWN 0**, 0 missing keys either direction |

All four guards exit 0 at the tip. A cross-worktree equality would have been the
wrong instrument *even if it had agreed*.

**Two independently-measured numbers reconciling is itself a check.** The peer
reported 551 executed; this session measured 547 at the base. `test_native_notables.cpp`
adds exactly 4, and 547 + 4 = 551 — so those figures agree rather than conflict,
which is worth confirming before trusting either.

### A narrower hand-back rule than unit ownership

Blanket "my wave surveyed this unit, so hand it back" would have blocked a
productive session across 39 units, most of which this wave only *measured and
refuted*. The rule proposed and used instead: **hand back only if the change
touches a function my wave actually modified.** Two files came back on that basis
(`math/Geo.cpp`, `synth/MoggClip.cpp`); `hamobj/HamPlayerData.cpp` was explicitly
released even though it was offered.

The branch then landed **whole, all 6 commits, rather than split** — MoggClip's fix
depends on `native/tests/test_helpers.cpp` registering `StreamReceiver::sFactory`,
without which the old 27-test segfault returns, and cutting that seam would mean
rewriting commits across it. Landing two released files is cheaper than rewriting
history to honour a rule about ownership.

### A timeout is not a failure, and 41 of 48 is not a result

Worth recording as a peer practice to copy: their off-by-default
`GameplayTelemetry` run hit a 2700 s timeout after 41 of 48 tests, **exit 124**.
Rather than reporting 41/48, or reading 124 as a failure, they derived the missing
7 by `comm` against the passed list, confirmed the selecting regex matched exactly
7, and re-ran those: 7/7, `CTEST_EXIT=0`. In a log summary exit 124 reads as a
failure and 41/48 reads as a partial pass; neither is what happened.

### `native-notables` landed: `c231d7e3f`

Gate on the branch tip `a6c7b1a7a`, in **my** worktree rather than the handing
session's, running alone: **620 registered / 551 executed / 551 passed / 0 FAILED /
69 skipped against a budget of 69**, exit 0. 620 is 616 + the 4 tests
`test_native_notables.cpp` adds, which is the arithmetic that made the peer's
independently-measured 551 and this session's 547 agree instead of conflict. All 11
DtaFlow tests passed, as did the 4 new notables tests.

**A second gate after merging was skipped, on a checked claim rather than an
assumption.** The branch was rebased onto main and then
`git diff <gated> HEAD -- src/ native/ include/ config/` was required to come back
**empty** — it did, so every compiled byte the gate exercised is identical to what
landed. Stated precisely because the convenient version of this was wrong: when
first claimed the delta was docs-only, but main moved twice more during the gate and
the real delta is 4 docs files **plus 4 Python files** under `tools/console/` and
`tools/state_diff/` (a concurrent Xenia-oracle spike). Those are outside both
builds — `grep -c state_diff build.ninja` = 0, likewise `native/CMakeLists.txt` —
so the gate's coverage still holds, but "docs only" was no longer true and the
boundary is: `tools/state_diff/tests/test_state_diff.py` is pytest, not ctest, and
**this session's gate did not run it**.

### Font3d: staged, not run, and its instrument had a defect

The probe is written (`~/tmp/dc3-wells/w8/font3d_probe.sh`) with its prediction
recorded *before* execution — **byte-identical**, because the block is
`#ifdef HX_NATIVE` so PPC never compiled it, the `#else` statement is kept verbatim,
there is no CodeView line table, and `grep -c __LINE__` is 0 in that TU. If that
holds, the original "the object changed" was a measurement artifact rather than a
phenomenon.

**It was not run, for two reasons worth recording.** First, a peer session has a
`ctest -j6` and three `ninja` runs live and the box is at load ~170; adding four
full PPC builds would risk exactly the load-induced DtaFlow flakes this session
complained about receiving. Second, and more usefully: **the probe's instrument was
broken and would have produced a false pass.**
`obj_build_metadata_patcher.normalize(data, offsets)` **returns** masked bytes, it
does not mutate in place. The probe passed a `bytearray` and hashed *that*, so it
would have hashed unmodified data and reported every variant as IDENTICAL —
including the positive control, which would then have read as "vacuous" and
obscured the cause. Caught by checking the signature with `inspect.signature`
before the first run, not by reading the code and assuming. The script now hashes
the return value and carries a mask self-test.

The probe retains the three controls it was written with: assert the compile edge is
wired before trusting any hash; keep builds ≥2 s apart, because `TimeDateStamp` has
1-second granularity and an *unwired* edge therefore agrees with itself inside one
second; and a positive control that must move the bytes or the run proves nothing.

## Wave 10 — completion lanes, a crash, and a stale-work sweep (2026-10-02)

### Result

Five decomp lanes and one tooling lane, each verified and gated by the coordinator
before landing. **31,393 → 31,449 matched (+56)**; authorable units with every function
at canonical 100 **646 → 688 / 966 (+42)**; remaining authorable work **802 → 746
functions (584,224 → 562,668 B)**; XEX headline 49.007 % → 49.164 %. Each landing was UP-only (0 DOWN rows
in any of the five), all five build guards green at each, and a native gate per lane
(623 registered / 554 executed / 554 passed / 0 failed / 69 skipped against a budget of
69, DtaFlow 11/11, every time).

| lane | worklist | functions UP | matched | notes |
|---|---|---:|---|---|
| w10-t | stale tooling | 0 | 31393 → 31393 | ninja-root guard wired; witness regenerated |
| w10-a | 1 fn from 100 | 10 | 31393 → 31403 | `RndGenerator::Generate` behaviour fix |
| w10-b | 1 fn from 100 | 10 | 31403 → 31412 | `SongLayout::SetDefaultPattern` missing store |
| w10-c | 1 fn from 100 | 6 | 31412 → 31416 | no bugs |
| w10-e | 2 fns from 100 | 23 | 31416 → 31439 | 3 behaviour fixes + a `symbols.txt` rename |
| w10-d | 1 fn from 100 | 10 | 31439 → 31449 | `NetCacheLoader::SetState` behaviour fix |

The worklists were derived from a baseline pinned to `a203218ed`: 143 units sat exactly
**one** function from 100 %, 117 of them never named in a phase-1..4 worklist; 66 fresh
units sat two functions away. Lanes were directory-disjoint, so no two lanes touched one
`.cpp` and every rebase was mechanical.

### Behaviour bugs, each adjudicated against the target listing, not taken on report

1. **`RndGenerator::Generate`** inserted new instances at the back; the image passes
   `begin()` (`lwz r11, 0x110(r31)`, the sentinel's `_M_next`, at `0x8270EA10`) to
   `list::insert`. Reversed draw order and mispaired particles with expired instances.
2. **`SongLayout::SetDefaultPattern`** never wrote `SongSection::mSongPattern`; the image
   stores the address of the loop-local copy (`addi r11, r31, 0x90` / `stw r11, 0x74(r31)`).
   A dangling pointer, faithfully: its one dereferencing reader is guarded on the image by
   an assert that fails first, which on native does not stop execution — no less safe
   than the uninitialised value it replaced.
3. **`HamGameData::SetAssociatedPadNum`** unassigned `pPlayer` and reassigned it; the image
   takes the pad from `mPlayers[1 - player]` (`subf r11, r31, r11` / `lwz r31, 0x4(r11)`).
4. **`KinectSharePanel::OnUpload`** had `InternalContext` and `dwCompletionContext` swapped
   (`stw r30, 0x70(r30)` stores `this` at `dwCompletionContext`).
5. **`MetaPerformer::CheckRecommendedPracticeMove`** — **reported by its lane as "same
   machine behaviour", and it was not.** For a move never attempted, `good/total` is
   `0/0 = NaN`; the old `!lastGood && ratio <= 0.49 → false` returned **true**. The image's
   `fcmpu` / `bgt <return 1>` sends the unordered case to `li r3, 0`: **false**.
6. **`NetCacheLoader::SetState`** fell through into the FileLoader teardown for states 1
   and 4; the image's `cmpwi cr6, r11, 3` / `bne cr6, .L_827FBBA0` goes straight to
   `mState = state`.

### Coordinator checks that changed what landed

- **A deleted negative result, restored.** w10-a's `NgFur::Shell` fix removed the only
  record that `#pragma fp_contract(off)` was byte-inert on that function, while
  `docs/decomp/patterns/fixable-fsel-fma.md` recommends the pragma with "Success Rate:
  HIGH". The measurement now lives in the doc, with the lever that worked (struct-member
  products) and the honest gap: whether the pragma does anything in the three TUs that
  still use it is unmeasured.
- **A struct-layout claim checked, not trusted.** w10-c rewrote `StubSkeletonExtra` as
  `{bool; Vector3}` on the claim that `Vector3` carries its own pad word. `Vec.h` shows only
  `x, y, z` at first read; the `u32 PAD` member is 20 lines further down (`Vec.h:85`).
  Checked before accepting, because if the claim were false the struct would shrink from
  0x14 to 0x10.
- **An out-of-line function given new semantics.** w10-d changed `NetLoader::GetBuffer`
  to return null unless loaded. Safe only because the image carries no symbol for it (it
  was inline in the original) and its one caller is already inside `if (IsLoaded())`; the
  two other `GetBuffer` call sites found by grep belong to `NetCacheLoader`.
- **A layout inversion, accepted openly.** w10-b's `Curl_HMAC_init` declares `hmac_opad`
  before `hmac_ipad` so MSVC picks the image's anchor, which *inverts* the two constants'
  placement relative to the image. Accepted because they sit in no measured data section
  and the swap has no behavioural effect; the source comment states the inversion.

### `report.json`'s `complete_units` does not measure completion

It read **968 at wave start and 968 at wave end**, across 42 completed units. It counts
units whose config flag says `complete: True`, which is set by hand — `system/rndobj/Gen`
carried that flag at wave start while `RndGenerator::Generate` sat at 97.698. A claim,
not a measurement. The completion figure above is the measured one: every function in the
unit at canonical 100, as `scripts/progress_metrics.py:105` defines it. Quote that, not
`measures.complete_units`.

### The crash, and what it did and did not damage

The server crashed mid-wave. Every lane had **zero commits and zero dirty files** — all
progress existed only in memory, and the lanes restarted from a full rebuild. Lane briefs
now say *commit as soon as each function closes*.

`git fsck` afterwards reported 14 unreadable commits, which reads as object loss. It was
not: no ref reaches any of them, `git -c core.commitGraph=false fsck` exits 0, and all 14
sat in one stale commit-graph layer dated 2026-09-14 — commits pruned after becoming
unreachable, still listed by a cache. `git commit-graph write --reachable` rebuilt it.
**Disable the cache and re-run fsck before concluding a repository lost data.**

### Stale work taken over

Twenty-two stale worktrees removed; none of this session's remain. Every unique commit was measured for whether its added
lines already exist on `main` before anything was removed, and dirty trees were committed
or archived first.

| branch | disposition |
|---|---|
| `fix/worktree-relative-ninja` | script had landed with **no caller**; wiring redone on current main as w10-t |
| `selfdistill-scoring` witness | main's copy refused itself whole (52 of 53 addresses since named); regenerated in w10-t, `refused` → `validated` |
| `w3-j-perm` (3 dirty edits) | `KinectSharePanel` superseded by w10-e (identical change); `UsbMidiGuitar` and `CameraShot` measured **inert** on current main (99.99357 and 99.99251, unchanged) — not landed |
| `dc3-sweep` | all 23 added `symbols.txt` lines already on main, all 52 removed lines already gone |
| `verify/mcp-gaps`, `verify/scanner-truthfulness`, `ik-test-charlocal` (code) | content already on main |
| `harvest-threadtask-replace` | `ThreadTask::Replace` is 100.0 on main |
| `fix/well-b-icf-attrib`, `ws3-dc3-relocname-measurement`, `verify/kinect-camera` doc, `fix/scope-idx-5` | adjudicated 2026-09-13 in `docs/analysis/stale-worktree-recovery-20260913.md`; verdicts unchanged |

Directories removed; the unmerged branches are kept as the only record of their
original attempts. `scripts/addr_identity_witness.json` from `dc3-addrid-icf` and the
`dc3-sweep` dirty diff are archived under `~/tmp/dc3-wells/w8/archive/`.

### Two instrument traps hit by the coordinator this wave

- **zsh does not word-split** `$c` in `for c in "script --flag" ...; python3 scripts/$c`:
  all five guards printed exit 2 (file not found) on a tree where all five pass. Caught
  by the uniformity — five different checks do not fail identically.
- **`obj_build_metadata_patcher.normalize()` returns its result**; it does not mutate.
  The staged Font3d probe hashed the unmodified input and would have called every variant
  identical, including its positive control. Found via `inspect.signature` before running.

## Wave 11 — three-from-100 units (2026-10-02)

### Result

The one- and two-function tiers were spent going in: of 115 units one function from
100 %, only 5 had never appeared in a wave 8–10 worklist; of 54 units two away, none.
Each of the rest carries a recorded floor in-source. Wave 11 therefore took the fresh
ground: **44 units three functions from 100 %** (132 rows, 87 KB) plus those 5 rows, in
four directory-disjoint lanes.

**31,449 → 31,488 matched (+39)**; all-100 authorable units **688 → 692 / 966**;
remaining authorable **746 → 707 functions (562,668 → 548,580 B)**; XEX 49.164 % →
49.278 %. Every landing UP-only, five build guards green, native gate 623 / 554 / 554 /
0 / 69 with DtaFlow 11/11 on each.

| lane | functions UP | units completed | matched |
|---|---:|---:|---|
| w11-d | 14 | 2 (ByteGrinder, Mic) | 31449 → 31460 |
| w11-c | 8 | 0 | 31460 → 31467 |
| w11-a | 12 | 1 (Pose) | 31467 → 31479 |
| w11-b | 7 + 2 renamed | 1 (Memory_Xbox) | 31479 → 31488 |

Unit yield is low by construction — a three-away unit completes only if all three close
— but it is not the whole return: many of these units now sit one function away, which
is the next wave's worklist.

### Behaviour bugs, adjudicated against the target

1. **`ByteGrinder` `op59`** had its two xor constants on the wrong halves: the image is
   `((w >> 2) ^ 0x0F) | (((w & 3) << 6) ^ 0x19)` (`extrwi`/`xori 0xf`, `clrlslwi`/`xori
   0x19` at `0x8276C930..3C`). The old code returned the wrong byte for **224 of 256**
   inputs.
2. **`DanceRemixer::JumpedMeasureStepsBetween`** advanced `from` in place, so its failure
   message printed the current measure; the image passes from's untouched home slot.
   Message text only.

### Config: the hashless anon-namespace class is closed

`?sDepthRectVerts@?A@@` (rnddx9/Rnd) was the second instance of last wave's
`gXboxDeadzone` defect, from the same commit `391d1b080`. With it renamed, the config holds
**0** hashless `?A@@` names (1 before the merge). Rnd.cpp had no other hashed anon-ns
symbol to copy from, so the hash is `scripts/anon_ns_hash.py`'s prediction for that TU —
which is what our build emits.

Two further renames (w11-b) sit at **ICF-folded addresses**, where the retail map lists
several names for one body and the config must use the name of the unit the split assigns
the address to. Each checked twice: the address lies in that unit's `.text` range, and the
map attributes exactly that name to that unit's object. A rename changes the report key,
so the row diff shows "2 missing keys" — reconciled on `(unit, name)`: both old keys
read 0.000, both new keys 100.000.

### What the coordinator checked beyond each lane's report

- `Mic.h` swaps two punned `int`s for one `unsigned long long` — layout-safe only because
  it sits at offset 0 and 1016 bytes is a multiple of 8.
- `SendOSCFloat`'s buffer shrinks 0x120 → 0x100 to match the image's frame;
  `MakeOSCAddress` writes unbounded, so neither size was ever a guard.
- `BuildSetOfPrevAdjacentMoveParents` calls `PrevAdjacents()` twice — safe because it
  returns a `const&` (`MoveGraph.h:86`); by value, each call would build a new container.
- `Memory_Xbox.cpp`'s `#line 15 "<retail path>"` sits on physical line 14, so it changes the
  file name and not the line numbering.
- Two PCH-reached header edits (`DirLoader.h`, `ObjPtr_p.h`) — whole-binary row diffs,
  DOWN 0 both times.

## Wave 12 — four-plus units and a certified floor reopened (2026-10-02)

With the one- to three-away tiers worked, wave 12 took units **four or more functions from
100 %** in four directory-disjoint lanes (w12-a..d), plus one lane (w12-o) that reopened a
floor three earlier lanes had certified: `ObjPtrVec<T>::operator=`. A server crash mid-wave
interrupted the lanes; all resumed from their committed state with nothing lost.

**31,488 → 31,554 matched (+66)**; authorable canonical **31,451 → 31,517 / 32,221
(97.61 % → 97.82 %)**; all-100 authorable units **692 → 697 / 967**; remaining authorable
**770 → 704 functions (550,264 → 523,472 B)**; XEX matched code 49.28 % → 49.51 %. Every
landing gated on a full ninja in both trees, a whole-binary row diff, five build guards
and the native gate (623 / 554 / 554 / 0 / 69 each time).

| lane | UP | DOWN | units completed | matched |
|---|---:|---:|---|---|
| w12-o | 13 | 0 | 3 (all ObjPtrVec instantiations' units) | 31488 → 31501 |
| w12-b | 11 + 1 renamed | 0 | 0 | 31501 → 31513 |
| w12-c | 18 | 0 | 1 (Cache_Xbox) | 31513 → 31528 |
| w12-d | 16 | 0 | 1 (ErrorNode) | 31528 → 31544 |
| w12-a | 11 | 2 (both bug fixes) | 0 | 31544 → 31554 |

**Correction to the wave 10 and 11 sections.** Their "remaining authorable" figures (802 →
746 → 707) were derived by subtracting each wave's matched delta, not measured:
746 − 39 = 707 exactly. `scripts/progress_metrics.py` (unchanged since 2026-09-28) run on the
wave-12 BEFORE report at `5c7c5fbb7` gives **770 functions / 550,264 B**, and 770 − 66 =
704 reconciles with the measured end figure. Quote the script's output, not a subtraction.

### ObjPtrVec<T>::operator= — 87.838 → 100 on all 13 instantiations

`Node newNode(this)` built its base through `ObjRefConcrete(T1 *)`, whose body is
`if (mObject) mObject->AddRef(this)`. With `nullptr` that branch is dead, but because it
passes the node's address to code the compiler cannot see, X360 MSVC treated `newNode` as
escaped, could not prove `mObject` still null at `~Node`, and kept the null test plus the
ring unlink — 9 instructions the image does not have, and 16 register-only rows they forced.
A protected `ObjRefConcrete() : mObject(nullptr) {}` with no AddRef path lets it fold.
Identical behaviour: with a null argument the old ctor's AddRef and native ref-audit call
both sat inside `if (mObject)`. The tell was the earlier 98.65 % from an explicit `= 0` store:
a store that removes the test means the compiler lacked a proof, not that the image made a
different codegen choice. Written up as
`docs/decomp/patterns/dead-addref-branch-escapes-the-local.md`.

### Behaviour bugs, each adjudicated by the coordinator against the target `.s`

1. **`BinkMovieImpl::MovieOpen`** ran the AutoSlowFrame wrapper when flag bit 26 was set;
   the image (`nor` / `extrwi. r11,r11,1,5` / `beq` to the plain `BinkOpen`) runs it when
   the bit is clear. Only the timing wrapper moved.
2. **`MemTracker::StartLog`** did `*mLog = ts;` right after asserting `mLog` null, so `mLog`
   stayed null; the image stores the stream's address (`stw r30, 0x0(r31)` at `827DADC4`).
3. **`ClipPlayer::GetPrevRoutineTransition`** read the wrong out-parameter of
   `GetRoutineCrossoverClips` — the image reads back `0x50(r1)`, the 4th argument.
4. **`SkeletonUpdate::UpdateCallbacks`** advanced the stub-placement count only on untracked
   slots; the tracked-slot `bne .L_8242DCD0` lands *on* the `addi r29, r29, 1`.
5. **`RhythmDetector::AddFrame`** gated its whole tail on `bestIdx != -1`; the image's
   `beq .L_824D5188` skips only the insert and `SetupFrame`. From a cleared state the old
   code could never fill its buffer.
6. **`RndBitmap::DxtColor`** leaked the next texel's DXT3 alpha nibble into the high nibble
   (image masks with `clrlwi r11,r11,28` before `slwi 4` / `or`).
7. **`RndFont::SetCharInfo`** evaluated `ColumnNonTransparent` once outside both edge scans;
   the image's latches jump back above the `bl` and re-test every column.
8. **`Rnd::TestPoint`** marked every queued point test ready; the in-view path branches past
   `stb r11, 0x148(r27)`.
9. **`RndShader::MatShaderFlagsOK`** warned "fadeout unchecked" for shaders that do not
   check fadeout (debug warning only).

Fixes 6 and 7 cost a fraction of a point each (93.10 → 92.86, 95.87 → 95.60) — the two
DOWN rows of the wave, kept deliberately.

### Levers worth reusing (all measured in-tree)

- A conditional clear `if (c) flag = false;` produces the image's `subfic`/`subfe` −1/0
  mask select; hand-writing the mask never does (three functions, two lanes).
- Reading a member through a pointer accessor (`ContentData()->DeviceID`, not
  `DeviceID()`) stops MSVC hoisting the load above an earlier store (four Cache_Xbox fns).
- Under `/Od`, frame slots follow local variable **names**; `keygen_xbox` now builds
  `/Od /Os` (18 of 20 functions byte-identical; the w8-g note that `/Os` broke 16 functions
  does not reproduce).
- A jump-to-test loop over an owner chain can be a tail call MSVC eliminated
  (`RndFont::CharWidthAdvanceCoords`).
- An explicit `Symbol(s)` temporary reproduces a stack spill of a by-value argument
  (three `Game` functions).

### What the coordinator checked beyond each lane's report

- `symbols.txt` rename `merged_8237A7E8` → `_Copy_Construct<CharInterestState>`: the retail
  map prints two claimants at `0x8237a7e8`, both `char:CharEyes.obj`, inside CharEyes'
  `.text` range; bound once; the second ninja ran no SPLIT.
- Every lane-labelled "same behaviour" respelling re-derived: e.g. `i7 != mInTheZone` sits
  inside `if (mInTheZone == 1 …)` with `i7 ∈ {0, −1}`; `BinStream << bool` writes the same
  one byte as `u8(b)`; `CreateEventA`'s `vec[i]` equals `mPlayerIndices[i]` because the
  vector was inserted at `begin()` of a fresh SubMode; `RndShader::Init`'s reorder assigns
  the same 38 slots (set hash compared).
- `DxTex::mLockedRect` became a struct deriving from `D3DLOCKED_RECT` that clears two
  pointer-sized words — covers `{Pitch, pBits}` on PPC and LP64 and still converts to
  `D3DLOCKED_RECT *`.

### Wave 13 — the frontier was larger than the tiers suggested

The completion tiers (units N functions from 100 %) had made the frontier look spent. A
function-level census says otherwise: of the 704 remaining authorable functions, **~370 at
≥ 90 % had never been named by a lane commit since 2026-09-15** (`git log` per source
file). Wave 13 dispatched five directory-disjoint lanes over them, plus one lane (w13-o)
owning `math/Vec.h` / `math/Mtx.h` to chase the y, z, x component order several wave-12
lanes observed in the image.

## Wave 13 — fresh near-misses, and a linkage lever (2026-10-02)

Wave 13 worked the census from the end of wave 12: functions at ≥ 90 % that no lane commit
since 2026-09-15 had named (372 rows), in five directory-disjoint lanes, plus w13-o owning
`math/Vec.h` / `math/Mtx.h` to test the y, z, x component-order residuals. Another session
landed and pushed two unrelated changes to main mid-wave (`decomp-xex`, `d3ddecl`: XDK data
symbol sizes, the XEX builder, one `d3d9types.h` constant); each lane was rebased onto them
and the baseline re-pinned, and they moved no function rows.

**31,554 → 31,607 matched (+53)**; authorable canonical **31,517 → 31,568 / 32,221
(97.82 % → 97.97 %)**; all-100 authorable units **697 → 702 / 967**; remaining authorable
**704 → 653 functions (523,472 → 497,868 B)**; XEX matched code 49.51 % → 49.70 %
(`scripts/progress_metrics.py` at `c6509d70d`). Every landing UP-only, five build guards
green, native gate 623 / 554 / 554 / 0 / 69 each time.

| lane | UP | units completed | matched |
|---|---:|---|---|
| w13-e | 6 | 0 | 31554 → 31560 |
| w13-a | 14 | 1 (ScreenMask) | 31560 → 31572 |
| w13-o | 5 | 0 | 31572 → 31576 |
| w13-b | 9 | 2 (HamMaster, MoveMgr) | 31576 → 31583 |
| w13-c | 8 | 2 (CharacterTest, world/Dir) | 31583 → 31591 |
| w13-d | 21 | 0 (MemMgr keeps MemTruncate, MemPushHeap) | 31591 → 31607 |

Units completed, measured by diffing the all-functions-100 unit sets of the wave-13 BEFORE
report (`79c3f58e7`) and the close report (`c6509d70d`): exactly these five, none lost.

Yield per lane was lower than wave 12's (≈ 15 % of the rows listed closed): a "fresh" row is
fresh to the commit log, not necessarily easy, and many carried wave 1–7 refutations.

### Behaviour bugs, adjudicated against the target `.s`

1. **`GetExpCode`** (os/Debug.cpp) fell off the end of the function for any unknown code
   ≤ `0xC000008D` — the hand-unrolled tree's first half ended in `default: break;` — so it
   returned whatever was in r3. Every unmatched code in the image reaches the
   "Unhandled Exception %d" block at `.L_825CC524` (`bne` at `825CC38C` / `825CC404`, `bgt`
   at `825CC470`).
2. **`DxRnd::Present`** stored `PIXGetCaptureState() & 2` raw (0/2) into its flag; the image
   normalizes to 0/1 (`rlwinm` / `subic` / `subfe` / `stb` at `82615AD0..DC`).
3. **`EQEffect::SetParameter`** band 4 added in double where the image uses `fadds`
   (precision only).

### The linkage lever (w13-d)

MemMgr's "anchor + displacement" rows had been certified a floor by two lanes. The cause was
linkage: MSVC addresses one global as a fixed offset off another's base register only when
**both are `static`** in the TU. `gHeaps` / `gNumHeaps` / `gNewOperatorAlign` were `extern`
only because someone had hand-written mangled names for them in `symbols.txt` — and those
entries carried no `scope:global`, while the retail map lists no static data at all. Made
static (bare spellings in `symbols.txt`, addresses inside MemMgr's `.bss` split), with three
unreferenced image ints filled by stand-ins so the offsets line up: **eleven functions to
100**. The config holds **102** more file-scope mangled data names without `scope:global`;
wave 14 lane w14-l works through them.

### The math-header question (w13-o)

The y, z, x residuals do **not** come from term order inside the Vec.h / Mtx.h helper
bodies. Under `/fp:fast` MSVC re-sorts a flat sum itself — writing `Dot`'s terms as y, z, x is
byte-identical — and the order it emits depends on surrounding code (scratch TUs: `Length`
alone y, x, z; `Dot` alone y, z, x; `Subtract` then `Length` z, x, y). Every body reordering
lost whole-binary (`Length = sqrt(LengthSquared)`: 2 up, 7 down; explicit parentheses in `Dot`:
5 functions off 100). What worked was changing how a body is **built** — `Multiply(v, t, out)`'s
aliasing arm through a temporary plus `Add`, `Normalize(Matrix3)` via `Cross()` — and fixing
component order at the call site. Negative results are recorded in both headers.

### Other levers (all measured in-tree)

- Moving a small operator inline into the header (COMDAT) stops MSVC propagating its register
  usage into callers: `Edge::operator<` overturned w8-h's 89.6 floor on the `set<Edge>`
  templates (`_M_find` 89.6 → 100). The retail map has no out-of-line copy, so no row is lost.
- Early `return` instead of `else` orders AutoTimer frame slots (WorldDir); `T *self = this`
  pins the full object when a method is entered through a base-class thunk (Spotlight).
- A bare MemPushTemp/MemPopTemp pair is a `MemDoTempAllocations` scope; a hand-written
  `while (!empty()) pop_back()` is the container's own `clear()`.

## Wave 14 — the second fresh pass, and the linkage lever is exhausted (2026-10-02)

Five directory-disjoint lanes over the 291 functions at ≥ 90 % that neither a lane commit
nor a wave 8–13 tagged source comment named, plus w14-l sweeping the 102 config entries that
spell file-scope data with a mangled name and no `scope:global` (the tell behind w13-d's
MemMgr fix).

**31,607 → 31,640 matched (+33)**; authorable canonical **31,568 → 31,600 / 32,221
(97.97 % → 98.07 %)**; all-100 authorable units **702 → 706 / 967**; remaining authorable
**653 → 621 functions (497,868 → 483,144 B)**; XEX matched code 49.70 % → 49.81 %
(`scripts/progress_metrics.py` at `85fd337a3`). Every landing UP-only, five build guards
green, native gate 623 / 554 / 554 / 0 / 69.

| lane | UP | units completed | matched |
|---|---:|---|---|
| w14-l | 0 | 0 (lever exhausted; one hack removed) | 31607 |
| w14-a | 6 | 0 | 31607 → 31610 |
| w14-e | 4 | 0 | 31610 → 31612 |
| w14-d | 13 | 2 (Archive, NetCacheMgr) | 31612 → 31623 |
| w14-b | 8 | 1 (CharDriver) | 31623 → 31630 |
| w14-f | 13 | 1 (UILabel) | 31630 → 31640 |

Units completed measured by diffing the all-100 unit sets of the wave-14 BEFORE report
(`c6509d70d`) and the close report: exactly these four, none lost.

### Behaviour bugs, adjudicated against the target `.s`

1. **`Rnd::OnToggleHeap`** never restarted the overlay timer when toggling off: the off arm
   used `SetShowingOnly(false)`. In the image both arms reach one shared `Timer::Restart`
   (the off arm branches to `.L_82663398`).
2. **`Vector2DESmoother::Smooth`** (latent — the only caller passes `normalize = false`):
   the normalize path skipped the write on a zero length and never set the previous level;
   the image zeroes the inverse and stores both level and previous level on each axis.

### The static-linkage lever is exhausted

w14-l compared per-function relocation sets, target against ours, for all 102 candidates,
with the known MemPushHeap case as a positive control. **No graded function below 100 is
held there by a candidate's linkage.** One was made static anyway (`gBigHunk`/`gSmallHunk`,
0 rows) because it deleted a `&gBigHunk + 1` pointer-arithmetic hack and its separate native
spelling; the two ints moved from MemMgr's `.data` split to PoolAlloc's, the object that
defines them. 34 candidates live in another unit's split; 60 show no displacement pattern.

### A native-only break the PPC tools could not see

w14-f inserted `Flow *self = this;` directly after an `#ifdef HX_NATIVE … } else #endif`.
On PPC the native arm is absent, so the line was an ordinary statement and every PPC check
passed; on native the declaration **became the else-body** and the build failed (native gate
exit 6, "use of undeclared identifier 'self'"). Fixed by declaring it above the native arm;
`Flow.obj` recompiled byte-identical. The wave-15 brief now names this trap.

### Levers (measured in-tree)

- The retail map's `f i` flag on a same-TU callee we define out of line: `inline` closed
  `ObjVector<Strand>::resize` 57.6 → 100 (CharHair::Strand ctor) and moved
  `SyncEffectParams` 96.4 → 99.9. `comdat_selection_audit.py` finds 533 such callees; 234
  sit in 90 units that still have functions below 100 — wave 15's inline lanes.
- `x << 1` instead of `x * 2` stops MSVC sharing the value with a later `(x − 1) * 2`.
- Calling a virtual through a named pointer in a destructor keeps the image's real call.

### Wave 15

Two inline-flag lanes owning those 90 units (plus their fresh rows), two fresh lanes over the
142 untouched rows outside them, and w15-r on the `gRev`/`gAltRev` family: four wave-14 lanes
independently hit `Load`/`PreLoad` residuals where the image addresses `gRev` off `gAltRev`'s
base, which points at the `INIT_REVS` macro in the PCH-reached `obj/Object.h`.

## Wave 15 — two cross-cutting levers tested, one of them real (2026-10-02)

Two inline-flag lanes owning the 90 units where the retail map flags a same-TU callee
`f i` (COMDAT `SELECT_ANY`) but our object emits it `NODUPLICATES`
(`scripts/analysis/comdat_selection_audit.py`), two fresh lanes over the untouched rows
outside them, and w15-r on the `gRev`/`gAltRev` revision-constant family.

**31,640 → 31,664 matched (+24)**; authorable canonical **31,600 → 31,624 / 32,221
(98.07 % → 98.15 %)**; all-100 authorable units **706 → 710 / 967**; remaining authorable
**621 → 597 functions (483,144 → 466,736 B)** (`progress_metrics.py` at `22a5192ac`).
Another session landed a docs-only commit mid-wave (`93af1a873`); w15-r was rebased onto it
with no `src/` or `config/` delta between the gated and pushed tree.

| lane | UP | units completed | matched |
|---|---:|---|---|
| w15-i2 | 2 | 0 | 31640 → 31642 |
| w15-a | 5 | 0 | 31642 → 31645 |
| w15-r | 11 | 1 (CharIKFoot) | 31645 → 31654 |
| w15-i1 | 3 | 0 | 31654 → 31656 |
| w15-b | 9 | 3 (DataArray, DataFlex, MemTrack) | 31656 → 31664 |

### Behaviour and data bugs, adjudicated against the target `.s`

1. **`SpeechMgr::AddDynamicRuleWord`** converted the semantic string into a buffer and never
   used it, so `NuiSpeechAddWordTransition` received an uninitialized `pcwszValue`; the image
   stores the buffer's address into the semantic (`stw r11, 0x58(r31)` at `0x8243B5C0`).
2. **`RndAmbientOcclusion::BuildObjectLists`** sorted `mObjectsTessellate` by its own
   indices; the image sorts by the user's tessellate list (`addi r5, r31, 0x68`). The
   stack-slot swap it caused had been read as a layout residual — **a slot swap can hide a
   wrong argument**.
3. **`CharIKHand` / `CharHair` revision constants**: the image's `gRev` is `0x000D0000` in
   both (`0x82012C6C`, `0x820147F0`); ours were 0xC and 11, so the version-mismatch message
   printed the wrong limit. Invisible to every function score.

### The `gRev`/`gAltRev` rule (w15-r)

MSVC anchors `ASSERT_REVS` on `gRev` when it sits at offset 0 of the file's non-COMDAT
`.rdata`, and on `gAltRev` (reaching `gRev` as `subi rX, rA, 0x4`) when any file-scope const
precedes it. Measured over all 248 `INIT_REVS` sites: 227/227 offset-0 sites anchor on `gRev`
in the image, 8/8 already-matching offset>0 sites on `gAltRev`, and all 10 mismatches were
files whose leading `.rdata` differed — each flipped both ways by adding or moving one const.
Nine `Load`s to 100 by restoring the image's missing const at its address. The macro itself is
right (an array-based variant lost 247 rows whole-binary). This overturns the "follows no
source lever" note in `relocation-names-are-unmetered.md`.

### The inline-flag lever is effectively exhausted

Across 90 audited units and ~180 same-TU callees with a below-100 caller, marking the callee
`inline` closed **one** caller (`DefaultMidiLess` 77.2 → 100 via `MidiRank`). Every other
caller was unmoved (COMDAT selection confirmed flipped). It joins the static-linkage lever
(w14-l) and helper-body reordering (w13-o) on the exhausted list.

### Other levers (measured in-tree)

- The XDK CRT declares `malloc`/`calloc`/`realloc`/`free` `__declspec(noalias)`; without it
  MSVC reloads file statics after `realloc` (three functions to 100).
- Hand-rotated loops (`if (n) do { } while`, hand-stepped pointers) written back as plain
  loops let MSVC rotate them itself (four functions to 100).
- `int abs()` in a float comparison is what survives as the image's runtime `int → float`
  conversion of a constant (`EaseElasticIn`).

### Wave 16

Five directory-owning lanes, each with the remaining fresh rows (169) and a **second opinion**
on the attempted rows at ≥ 99 % (122), briefed with every floor that has fallen since wave 12.

**31,664 → 31,686 matched (+22)**; authorable canonical **31,624 → 31,646 / 32,221
(98.15 % → 98.22 %)**; all-100 authorable units **710 → 713 / 967** (FlowCommand, PartAnim,
TransAnim); remaining authorable **597 → 575 functions (466,736 → 446,748 B)**
(`progress_metrics.py` at `a7cf32774`). Row diff of the wave-15 close (`cbc859d33`) against
`a7cf32774`, both full builds: **31 UP, 0 DOWN**.

| lane | owns | UP | units completed | matched |
|---|---|---:|---|---|
| w16-b | hamobj, world | 8 | 0 | 31664 → 31670 |
| w16-d | os, utl, obj, flow, ui, meta | 5 | 1 (FlowCommand) | 31670 → 31673 |
| w16-c | char, gesture, math, rnddx9 | 1 | 0 | 31673 → 31674 |
| w16-a | rndobj | 12 | 2 (PartAnim, TransAnim) | 31674 → 31682 |
| w16-e | lazer, synth, net, moviebink | 5 | 0 | 31682 → 31686 |

### Behaviour bugs, adjudicated against the target `.s`

1. **`UIListState::Scroll`**: on a circular list with `skipActive`, Scroll returned without
   moving. In the image the skip test lands on the accept block's first instruction
   (`lwz r28, 0x54(r1)`), which stores `mTargetShowing`. An earlier lane read the branch target
   one instruction early and filed the accept block as a CSE leftover.
2. **`Synapse::Synapse`**: `mIirCoeff`'s time constant was `0.00811767578125f` (`0x3c050000`).
   The image loads `__real@3c05b186` at `0x82E4763C`, which is 8.16 ms; spelled `8.16f * 0.001f`,
   which folds to exactly that value. **A literal that is close but not equal is still wrong**:
   check every literal against the image's pool value.

### Floors that fell in the second-opinion pass

- **HamNavList::Poll / SetSelecting**: three lanes certified a floor here. The code called the
  `SlideSoundAnim()` accessor where the image binds the member directly. The extra inline level
  costs one home store per site.
- **MovieInternalBuffers::New** "scheduler tie": the tie came from a block-scoped declaration.
  Moving it to function scope closed the row.
- **`vector<float, XboxAllocator>::_M_fill_assign`**: an empty user copy ctor and dtor on the
  allocator stopped `get_allocator()` from inlining away. The original lacks both.
- **SetBloomBlurWeights**: two file-scope static const tables were folded onto one base + 0x3c.
  The image has function-local static consts.

### The Keys-chain lever (w16-a)

In `d >> keysA >> keysB`, the inline wrapper in `math/Key.h` sequences each call before it forms
the next operand, but the image calls the vector reader directly. Reading through
`(std::vector<Key<T>> &)` casts closed four `Load`s. Only a handful of chains remain
(CamAnim, PartAnim, TransAnim), so they went into the wave-17 brief rather than their own lane.

### Two HX_NATIVE shadow bodies retired

`UtilDrawCigar` and `RndRibbon::ConstructMesh` each existed twice, because their int-cast
pointer arithmetic could not run LP64. Plain loops now serve both builds. The coordinator
re-derived the ConstructMesh face indices against the old byte offsets.

### Tool defect found, not fixed

dtk corrupts a conditional branch that carries an `IMAGE_REL_PPC_REL14` relocation to its own
function. The shipped `40 9A FF 94` (`bne cr6`) at `0x82341FD4` becomes `43 FF FF 94` in the carved
target object, because the branch's condition fields are overwritten with ones. This happens in
CharLipSync's `fill<_Bit_iter>`, which reads 99.857 but matches, and in
`CTReorderInstructions::Read`, the only other relocated conditional branch. The fix belongs in
the shared `../jeff` dtk and is waiting on the owner.

### Other

- `Object::SyncProperty` reaches 100 with an **unreachable** `static Symbol`. The image reserves
  static-guard bit 0x2 and never tests it. The change is behaviour-neutral, but it is an
  invented spelling and is marked as such in the source.
- Stale-branch sweep: every unmerged local branch from June through September was checked
  against main. Each headline function those branches name (82 names, 38 branches) is at or
  above the branch's own claim on main, and the four `ns-*` branches already landed as merges.
  Nothing needed adopting.

### Wave 17

Five directory-owning lanes with the same ownership, briefed with the wave-16 floors. Each lane
gets every fresh row at **any** percentage (148), plus a second opinion on the attempted rows at
95–99.99 % that wave 16 did not re-examine (212).
