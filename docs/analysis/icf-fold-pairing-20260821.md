# Is the remainder of the `/OPT:ICF` fold-survivor class metric-recoverable? — dc3-decomp, 2026-08-21

**Repo: dc3-decomp (title 373307D9).** `../rb3` and `../rb3-xenon` share symbol
names and address ranges with this tree; every symbol, address and number below
is dc3's, measured on `fix/icf-survivors-20260821`. The lane branched from
`2f666acc8`; `main` advanced to `7bff4701f` mid-run, so the branch was rebased and
**the whole-build A/B was re-run from scratch against a baseline worktree rebuilt at
`7bff4701f`**. Both measurements are identical to the digit, and the `dtk xex split`
fixed point was re-verified after the rebase (`symbols.txt` md5
`38726dfd2a5f77ed8d0e02762f62f810`, unchanged across two consecutive full `ninja`
runs).

Task #114, the follow-up to #112 (`docs/analysis/icf-survivor-names-20260819.md`).

## Answer: yes — 341 of 366, and the population was **not** the one the task
## description assumed

**#112's own population is exhausted.** #112 worked the sub-class where dtk's
splitter had *no* name for a fold survivor and wrote a synthesised
`merged_<addr>` / `merged_<Shape>` placeholder. At the merge base,
`build/373307D9/report.json` carries **exactly 6** `merged_*` rows, all at 0 %,
and **5 of the 6 are #112's own documented refusals** — `merged_823AAA20`
(`CharSignalApplier::Handle`), `merged_8237A7E8` (`CharEyes`
`_Param_Construct`), `merged_82984FC8` (`~PlaylistSongProvider`),
`merged_ObjPtrListRemove`, `merged_ObjPtrVecErase`. The 6th,
`merged_SetObjConcrete` at `0x82401CD0` in `flow/FlowAnimate`, appeared after
#112 and **is** recoverable (below). Renaming placeholders is done; there is no
remainder there.

**The remainder is a different and much larger class:** rows where dtk *did*
write a real mangled name into `config/373307D9/symbols.txt` and wrote the
**wrong member of the fold class**. `/OPT:ICF` folds byte-identical COMDATs to
one address; `orig/373307D9/ham_xbox_r.map` co-lists every member there together
with the `.obj` that contributed it; dtk picks one. objdiff pairs target→base by
name within a unit, so when our object emits a *different* member the row reads
0 % while our bytes are already correct and already present at that address.

## The denominator

Derived by query — never a worklist — in
`scripts/analysis/icf_fold_pairing_recover.py`, over `report.json` ×
`symbols.txt` × `ham_xbox_r.map` × our own COFF objects:

```
universe: report.json functions                                48,344
examined: rows at 0.0 match_percent_normalized                 16,870   5,138,064 B
dropped:
  not a fold class: the map lists ONE name at this address     15,262   4,946,212 B
  no built object for the unit (xdk/ or lib/, not decompiled)     751      69,992 B
  address absent from the shipped linker map                      235       6,532 B
  not in symbols.txt (lib/xdk row named by the splitter only)     211      78,060 B
  our object defines no other member of the fold class             32       1,972 B
  our object DOES define this name (a real 0 % code gap)           13         108 B
CANDIDATE population (the ICF pairing class)                      366      35,188 B
```

Not truncated: 366 is the whole class under those filters, and every drop is
named and counted rather than folded into an "other".

## Result

| tier | rows | bytes | basis |
|---|---:|---:|---|
| `PROVEN_BODY` — installed | 205 | 21,464 | strict body test, ≥ 1 relocation |
| `ALIAS_E_TO_G` — installed | 44 | 4,164 | `??_E` is a COFF weak external aliasing `??_G` |
| `WEAK_NO_RELOC` — installed | 92 | 5,844 | body test passes, **zero** relocations — map only |
| `REFUSE_BODY_DIFFERS` | 19 | 3,052 | real code divergence, left at 0 % |
| `REFUSE_TIEBREAK_FAILS` | 5 | 656 | the map attributes every passer to another `.obj` |
| `REFUSE_NAME_CLASH_REPORT` | 1 | 8 | `zcfree` already scores in `default/link_glue` |
| **total** | **366** | **35,188** | |

**341 names recovered, 25 refused.** Whole-build effect, predicted before the
rebuild and observed after against a separately-built baseline worktree at the
same commit:

```
predicted  +341 functions, +31,472 B
observed   matched_functions   29,497 -> 29,838      (+341)
           matched_code     5,002,840 -> 5,034,312   (+31,472)
           matched_functions_percent  61.014812 -> 61.720173
           matched_code_percent       43.987103 -> 44.263820
           fuzzy_match_percent        54.057007 -> 54.333706
```

They agree exactly. A full per-function diff of the two `report.json` files
(by **absolute path, in one process** — `scripts/analysis/report_ab.py`; reading
a relative path twice compares a tree against itself) shows **341 rows appeared,
341 rows vanished, and ZERO rows present in both moved** — on
`match_percent_normalized` *or* on `fuzzy_match_percent`. `total_functions`
unchanged at 48,344. Every one of the 341 new rows reads **100.0 on both
rulers**; the 341 vanished rows were all 0.0. No regressions, no incidental
improvements.

Identity is a zero-mismatch instruction count, not a rendered `100.0`.
`run_objdiff` reports **all equal** on every sample taken: `UIList::Load` 30/30,
`ObjPtr<RndPropAnim>::'scalar deleting destructor'` 30/30,
`UIList::GetUIListDir` 2/2,
`vector<Key<Weight>>::_M_fill_insert_aux` 118/118,
`_Rb_tree<String, DataNode>::swap` 102/102. Say "matched modulo register
permutation", not "byte-identical" — the canonical ruler forgives register
permutation.

`dtk xex split` fixed point re-verified: `symbols.txt`'s md5 is
`38726dfd2a5f77ed8d0e02762f62f810` after the edit and **unchanged across two
consecutive full `ninja` runs**, so the depfile edge does not self-refire.
`bin/objdiff-cli --version` = `4.2.6 (bf7405e3fe07)`, ≥ the required 4.2.5.

The 341 touch **208 distinct units**; the densest are `world/LightPreset` (9),
`hamobj/HamDirector` (8), `char/CharLipSync` (7), `meta_ham/MetaPanel` (7).

## What made dtk pick wrong, measured rather than assumed

Of the 341, **271 (24,596 B)** had a split name the map attributes to a
**different unit's `.obj`** — dtk took a fold member contributed by a foreign
TU and parked it on an address inside this unit's range. That is exactly the
rule `docs/analysis/comdat-tier2-triage-20260819.md` scoped for dtk's splitter:
*prefer the fold member whose contributing `.obj` owns the address range.*
Worked example, `default/system/ui/UIList` at `0x8278AD98`:

```
split name  ?Load@HamList@@UAAXAAVBinStream@@@Z      [hamobj:HamList.obj]
installed   ?Load@UIList@@UAAXAAVBinStream@@@Z       [ui:UIList.obj]
```

The remaining **70 (6,876 B)** had a split name the map also attributes to this
unit's own `.obj`, and 68 of those 70 are the `??_E`/`??_G` relationship below.

## A second relationship was hiding in the same population, and it is not a fold

`??_E<T>` (vector deleting destructor) is emitted by MSVC as an **undefined COFF
weak external whose aux record's `TagIndex` names `??_G<T>`**. The address holds
`??_G`'s COMDAT; `??_E` resolves onto it. That is an alias, not `/OPT:ICF`
folding, and it is why `docs/analysis/2026-08-19-refuted-fold-memberships.md`
had to call `??_EString` "genuinely ambiguous".

The tool **reads the aux record** rather than inferring from the `??_E`/`??_G`
name shape (a name shape is an argument, not a witness), and tiers those 44 rows
separately as `ALIAS_E_TO_G` so they can be discounted independently:

```
0x8272A890  ??_ENode@?$ObjPtrList@VTask@@VObjectDir@@@@UAAPAXI@Z
      aux TagIndex -> ??_GNode@?$ObjPtrList@VTask@@VObjectDir@@@@UAAPAXI@Z, Characteristics 2
```

**Instrument correction.** `scripts/symbol_aliases.json`'s `_comment` describes
this as `SEARCH_ALIAS`. MSVC emits **Characteristics 2**
(`IMAGE_WEAK_EXTERN_SEARCH_LIBRARY`), not 3 (`SEARCH_ALIAS`), on all 75 weak
externals inspected. The load-bearing field is `TagIndex`, which does name
`??_G<T>`; the tool therefore gates on `TagIndex` and reports `Characteristics`
without gating on it. A gate written against the documented value 3 would have
refused every row in this tier.

## The six gates, and the two that refuse a free metric point

1. the row scores 0.0 `match_percent_normalized`;
2. `symbols.txt` places its name at address A and the map co-lists ≥ 2 names at A;
3. our unit's object does **not** define the split name but **does** define
   another member M of that class;
4. M passes the strict body test of `scripts/analysis/icf_survivor_names.py`
   against the **target's own bytes** at A — equal length, equal relocated-offset
   set, equal relocation *target names* modulo fold-equivalence, and every
   non-relocated word byte-equal, internal branch displacements included.
   (Comparing our two spellings against each other says a claim is wrong without
   saying which side is wrong; that gap has burned this project before.)
5. **the map attributes M to this unit's own `.obj`.** This is the gate that
   refuses "rename anything whose bytes happen to match". It cost 5 rows here,
   and following its refusal to its cause found a source bug (below).
6. M is absent from both `symbols.txt` and `report.json`, so installing it
   cannot collide with a name something else already scores. This cost 1 row.

`fold_proof.py`'s cheapness guard was exercised live before anything downstream
of it was trusted, with **both** controls in one run:

```
?Handle@UIListWidget@@ vs ?Handle@HolmesInput@@                -> PROVEN_FOLD  (292 B / 19 rel)
?GetType@AccomplishmentDiscSongConditional@@ vs ?GetType@CmdEditPlaylist@@ -> UNDECIDABLE
    "bodies identical but ZERO relocations ... identity here is CHEAP"
```

It still discriminates and still refuses relocation-free code.

## The ambiguity, stated rather than hidden

**66 of the 341 addresses have more than one member our own object defines that
passes the body test AND that the map attributes to this unit's `.obj`.** They
are byte-identical by construction — that is what folding means — and no body
test can choose between them. Where that happens the pick is resolved by sorting
the names. **That is a labelling convention, not evidence**, and it must not be
read later as a claim about which spelling the original TU "really" used.

The `WEAK_NO_RELOC` tier (92 rows, 5,844 B) is weaker still and is kept separate
for the same reason #112 kept its 11 separate: `{ return mField; }` is 8
identical bytes and every such accessor in the image is byte-identical to every
other, so byte-identity there does not discriminate. They are installed on the
map alone — the linker's own statement that the name is at that address and that
its contributing `.obj` is this unit's. A reader who wants to discount 5,844 B
can do so without touching the other 25,628 B.

## What is NOT metric-recoverable, and the evidence for each

### 19 rows / 3,052 B — `REFUSE_BODY_DIFFERS`: real code divergences the metric cannot see

`/OPT:ICF` only folds byte-identical COMDATs, so retail's members *were* the same
bytes. If our peer is not identical to the target's folded body, **our peer is
wrong** — and because every instantiation folds, the target names the survivor
once and no instantiation of that method is scored anywhere. These are invisible
to `report.json` by construction, which is why they are left at 0 % rather than
renamed.

**Six of the 19 are one pattern**: the target instantiates the STL copy path on
a **`const T*`** iterator (`PBV`) and ours on a **`T*`** (`PAV`), and ours is
consistently **8 bytes larger**:

| address | unit | target | ours | shape |
|---|---|---:|---:|---|
| `0x82343A58` | `char/CharLipSync` | 108 | 116 | `_M_allocate_and_copy<const String*>` vs `<String*>` |
| `0x82591EC0` | `obj/Dir` | 96 | 104 | `__uninitialized_copy<const FilePath*, FilePath*>` vs `<FilePath*, …>` |
| `0x82631E78` | `rndobj/Utl` | 108 | 116 | `_M_allocate_and_copy<const Vector3*>` |
| `0x82633348` | `rndobj/Utl` | 312 | 320 | `_M_range_insert_realloc<const Key<Vector3>*>` |
| `0x826334C0` | `rndobj/Utl` | 312 | 320 | `_M_range_insert_realloc<const Key<Quat>*>` |
| `0x824EAD60` | `hamobj/HollaBackMinigame` | 108 | 116 | `_M_allocate_and_copy<const Symbol*>` |

Same +8 B in every case. That points at **one `const` in an STLport `vector`
signature** — the copy path takes `const_iterator` in retail and `iterator` in
ours. It is a PCH-reached header, so any experiment on it needs a full `ninja`
and a negative control; not attempted here, and it wants its own lane because
the blast radius is the whole tree. `__uninitialized_copy<const SampleMarker*>`
(`utl/WaveFile`, 96 vs 104) and `__uninitialized_copy<const ActionRec*>`
(`meta/HeldButtonPanel`, 96 vs 104) are the same +8 B against a differently-named
peer.

The rest, individually:

| address | unit | target | ours | finding |
|---|---|---:|---:|---|
| `0x8278B718` | `ui/UIList` | 284 | 284 | `ObjPtrList<T>::Unlink` — same size, but the relocation at `0x3c` names retail's assert string `n != NULL && mNodes != NULL` and ours names a different literal. A real assert-text divergence. |
| `0x82848AD0` | `world/LightPreset` | 276 | 240 | `ObjPtrVec<T>::erase` — **there are TWO distinct erase bodies in retail**, 276 B here and 240 B at `0x823EA0B8`. Ours is 240 B. |
| `0x823EA0B8` | `flow/FlowManager` | 240 | 240 | the 240 B `erase`. Our `ObjPtrVec<RndEnviron>` peer differs only at `0xc`: `7c7b1b78` (`mr r27,r3`) vs `7c7d1b78` (`mr r29,r3`) — **pure register allocation**, confirming #112's reading. |
| `0x825C6868` | `obj/TypeProps` | 100 | 88 / 96 | `ObjPtrList<T>::remove` — both peers short, shape difference. |
| `0x823AAA20` | `char/CharSignalApplier` | 436 | 64 | `CharSignalApplier::Handle` is a one-line stub; the target is the full `MessageTimer`/`SyncProperty` body. `CharTransDraw::Handle` at `0x8239EA20` is the same 436 B shape at 100 % and is a direct template. |
| `0x8237A7E8` | `char/CharEyes` | 84 | 60 | `_Param_Construct<CharInterestState>` — 24 B larger than ours; `CharEyes::CharInterestState` is missing a member. |
| `0x82984FC8` | `meta_ham/PlaylistSongProvider` | 76 | 100 | `~PlaylistSongProvider` — ours is 24 B **bigger**. |
| `0x825F5AD8` | `os/Memcard_Xbox` | 4 | 4 | `MemcardXbox::Terminate` — the target's single instruction carries a relocation at `0x0` (a tail branch) and ours is a bare `blr`. |
| `0x8255A0A0` | `net/XLSPConnection` | 104 | 104 | `MakeString<ReqType>` — ours has relocations at `0x14`/`0x24` the target does not. |
| `0x82563B08` | `net/JsonUtils` | 116 | 116 | `MakeString<char[14], int, char[N]>` — same, at `0x10`/`0x24`; the array bound differs too (`$$BY04` vs `$$BY0BK`/`$$BY0CK`). |
| `0x8273C6C8` | `synth/SynthSample` | 96 | 104 | `__uninitialized_fill_n<Label*>` vs our `<SampleMarker*>`. |

### 5 rows / 656 B — `REFUSE_TIEBREAK_FAILS`: gate 5 found a source bug, and closing it is metric-NEGATIVE

All five are `rndobj/AmbientOcclusion`. The map lists, at each address, a
`FacePriority` member contributed by `rndobj:AmbientOcclusion.obj` **and**
`Key<Symbol>` / `Key<bool>` / `Key<float>` members from `rndobj:PropKeys.obj`.
Our object defines only the `Key<float>` one — because
`src/system/rndobj/AmbientOcclusion.cpp` **casts the `vector<FacePriority>`
iterators to `Key<float>*` before `std::sort`**. The two are layout- and
comparison-compatible (`{4 B, float}`, ordering on the second member), which is
exactly why `/OPT:ICF` folded them in retail — but the cast makes our TU emit
`sort<Key<float>*>` where retail's emits `sort<FacePriority*>`.

So gate 5 refused the free rename and pointed at a real source divergence. **The
fix was written, measured and reverted** (commits `5157a8e69` → revert):

```
dropping the cast makes 8 AmbientOcclusion rows installable (1,076 B):
  6 reach 100.0 (__unguarded_linear_insert 76, sort_heap 116, __partial_sort 168,
                 __introsort_loop 188, __make_heap 108, sort 132)
  2 land short (__adjust_heap 99.8889, __final_insertion_sort 99.6296)

but the whole build LOSES more than it gains:
  matched_code  +31,472 B  ->  +30,732 B      (-740 B)
  matched_functions unchanged at +341
  3 rows in OTHER units fall off 100.0:
    rndobj/PropKeys  ?ReSort@PropKeys@@                     100.0 -> 99.8864 (528 B)
    rndobj/PropKeys  __linear_insert<Key<float>*>           100.0 -> 99.8438 (128 B)
    rndobj/CamAnim   PropSync<float>(Keys<float,float>&, …) 100.0 -> 99.9115 (452 B)
```

The three collateral rows are callers whose relocations point at the folded
`Key<float>` survivor; renaming that survivor to the `FacePriority` spelling
removes their `icf_aliases.map` equivalence, so `name_check` charges them a
relocation-name point. Restoring it means hand-editing
`scripts/symbol_aliases.json`, which the file forbids.

Same shape as the rejected `ShaderMacro::operator=` experiment in
`docs/analysis/2026-08-19-refuted-fold-memberships.md`: a naming point traded
for measured code. **Reverted**, and recorded here with the numbers so nobody
re-derives it blindly. The finding that retail sorts `FacePriority*` and we sort
`Key<float>*` stands and is real; it is only the *metric* that refuses to pay
for it, and it would become free if `symbol_aliases.json` were regenerated
through `scripts/gen_icf_alias_map.py`'s own inputs rather than hand-edited.

### 1 row / 8 B — `REFUSE_NAME_CLASH_REPORT`

`0x82860908`, `zlib/zutil`. Split name `?jpeg_free_small@@` (`jpeg:jmemnobs.obj`);
the map's unit-owned member is `zcfree` (`zlib:zutil.obj`) and it passes the body
test. But `zcfree` is **already a 4 B row at 0 % in `default/link_glue`**, so
installing it would put two rows under one name. Refused by gate 6.

### 32 rows / 1,972 B — our object defines no member of the fold class at all

Dropped before adjudication. These are genuinely absent code, not naming: there
is nothing of ours at that address under any spelling. Not this lane's class.

### 13 rows / 108 B — our object DOES define the split name

Also dropped before adjudication, and the drop is the point: the name pairs
correctly and the row still reads 0 %, so the 0 % is a real code gap. Average 8 B
each — stubs.

## Deliberately not done

* **No transitive closure / union-find over fold memberships.** Evaluated in a
  prior lane: 0 verdict changes across 6,657 memberships, fail-open across 1,393
  multiply-addressed names, explicitly rejected. Not revisited.
* **dtk's splitter is still not fixed.** The general fix — prefer the fold member
  whose contributing `.obj` owns the address range — would close 271 of these 341
  at the generator instead of in the config, but it lives in `../jeff`, needs a
  manual `cargo build --release`, and `bin/objdiff-cli`/dtk are shared by symlink
  with `../rb3` and `../rb3-xenon`, so the blast radius is three repos. This lane
  fills in the config the splitter already reads, exactly as #112 did.
* **`scripts/symbol_aliases.json` not touched.** The renames leave some groups'
  `survivor` field stale, but `scripts/gen_icf_alias_map.py` renders all members
  at the survivor **address**, so `reloc_eq` keeps working unchanged — witnessed
  by `icf_aliases.map` still loading 8,719 entries after the rebuild and by zero
  rows moving in place on either ruler.
* **The STLport `const_iterator` lead** (6 rows, +8 B each) is left as a lead.

## Reproduce

```bash
python3 scripts/analysis/icf_fold_pairing_recover.py --project . --verbose \
        --json /tmp/icf114.json
python3 scripts/analysis/icf_fold_pairing_recover.py --project . --apply --include-weak
ninja
python3 scripts/analysis/report_ab.py /abs/baseline/build/373307D9/report.json \
                                      /abs/worktree/build/373307D9/report.json
```
