# Recovering the names of 48 `/OPT:ICF` fold survivors — dc3-decomp, 2026-08-19

**Repo: dc3-decomp (title 373307D9).** rb3 and rb3-xenon share symbol names and
address ranges with this tree; every symbol, address and number below is dc3's,
measured on `fix/icf-survivor-names-20260819` off `eda64e956`.

Task #112. Population: the 48 addresses / 5,420 B in
`docs/analysis/report-absent-rows-20260818/recoverable-merged-names.json`, all
scoring 0 % in `build/373307D9/report.json`.

## Result

| tier | rows | bytes | outcome |
|---|---:|---:|---|
| PROVEN_BODY — installed | 32 | 4,208 | body test passes with ≥1 relocation |
| WEAK_NO_RELOC — installed | 11 | 276 | body test passes but the body has **zero** relocations |
| REFUSED — left at 0 % | 5 | 936 | real code differences, not naming |
| **total** | **48** | **5,420** | |

**43 names recovered, 5 refused.** Whole-build effect, predicted before the
rebuild and observed after:

```
predicted   +43 functions, +4,484 B   (32 PROVEN 4,208 + 11 WEAK 276)
observed    matched_functions   29,445 -> 29,488     (+43)
            matched_code     4,961,036 -> 4,965,520  (+4,484)
```

They agree exactly. A full per-function diff of `report.json` across the rebuild
shows **43 new rows, 43 vanished rows, and zero other movement** — no
regressions, no incidental improvements, `total_functions` unchanged at 48,344.

On the canonical headline (`scripts/progress_metrics.py`, authorable
`norm==100`), also exactly +43 functions and nothing else:

```
before (eda64e956)   91.36%   29,430 / 32,213
after                91.49%   29,473 / 32,213
```

The 43 fall short of the 48-row / 5,420 B population by the 5 refusals below
(936 B). The gap is the finding, not a shortfall to be closed: those five are
real code differences and leaving them at 0 % keeps them visible as work.

Identity is zero-mismatch, not a rendered `100.0`: `run_objdiff` reports *all
equal* instruction counts on every sample taken —
`CharTransDraw::Handle` 109/109, `FitnessCalorieSortMgr::Handle` 79/79,
`PlaylistHeaderNode::SetItemCountString` 38/38, `AsyncFile::Fail` 2/2.

`dtk xex split` fixed point re-verified: `config/373307D9/symbols.txt`'s md5 is
unchanged across two consecutive full `ninja` runs after the edit, so the
depfile edge does not self-refire.

## Why the row scored 0 %

`/OPT:ICF` folds byte-identical COMDATs to one address.
`orig/373307D9/ham_xbox_r.map` co-lists every folded member at that address
*together with the .obj that contributed it*. dtk's splitter did not consume the
map for these 48, so `symbols.txt` carries a synthesised `merged_<addr>` /
`merged_<Shape>` placeholder. objdiff pairs target→base **by name within a
unit**; our object defines a real mangled name there and the target side is
called `merged_…`, so the row reads 0 % while our bytes are already correct and
already present.

## The evidence, and what was refused as evidence

### The `recoverable_name` field was NOT used

`recoverable-merged-names.json` ships a `recoverable_name` per address. It is a
**stale `decomp.db` spelling, not a map-derived choice, and it is the wrong fold
member on 14 of the 43 rows this lane recovered.** Three examples:

| address | unit | stale field says | map says (from this unit's .obj) |
|---|---|---|---|
| `0x8235E578` | `char/CharBoneOffset` | `?Handle@PhotoSpotlightPositioner@@` | `?Handle@CharBoneOffset@@` |
| `0x825F9508` | `os/AsyncFile` | `?IsMaxGain@RecipeTable@TrueColor@@` | `?Fail@AsyncFile@@` |
| `0x825D6820` | `os/PlatformMgr_Xbox` | `?SetPurchaseMade@SingleItemEnumCompleteMsg@@` | `?SetPurchaseMade@MultipleItemsEnumCompleteMsg@@` |

`scripts/analysis/icf_survivor_names.py` ignores the field entirely and
re-derives every candidate from the shipped map. The disagreements are printed
per row so the drift stays legible, but they are never inputs.

### The body test (`scripts/analysis/icf_survivor_names.py`)

dtk's split asm prints, for every instruction, both the **linked bytes** and the
**operand's symbol name** (`bl "?Sym@DataArray@@…"`, `lis r11,
"?sActive@MessageTimer@@1_NA"@ha`). The target's relocation *target names* are
therefore directly readable, and the test does not have to blind itself to
branch displacements. A candidate name N passes only if all four hold:

1. our COMDAT for N and the target body are the **same length**;
2. the **sets of relocated offsets** are equal;
3. at every relocated offset the two sides **name the same callee/datum**,
   modulo fold-equivalence (two names on one address are one thing to the
   linker);
4. every **non-relocated word is byte-equal** — including internal branch
   displacements, which are self-relative and so directly comparable.

Clause 4 is what `scripts/analysis/icf_pairing_bodytest.py` gives up (it masks
every `b`/`bc`), and it is what makes a pass mean *our body IS the body at that
address* rather than *our body is the same shape*.

### Negative control — the test discriminates

`docs/analysis/icf-survivor-names-20260819/negative_control.py` runs each target
body against **every function symbol the unit's own object defines**, not just
the map members at that address:

```
13,633 candidate names tried, 73 passed  (0.535%)
```

The 73 are in-TU fold twins, which is what `/OPT:ICF` folding *means*, not test
slack.

### Two instrument defects found while building it

Both were manufacturing **false refusals**, and both are the kind that would
have been read as bugs in the source:

* **`IMAGE_REL_PPC_PAIR` (0x12) is not a symbol reference.** It carries the
  REFHI/REFLO displacement in its `SymbolTableIndex` field. Reading that as a
  symbol index resolves to symbol 0, `@comp.id`, and because the PAIR shares the
  offset of the REFHI it pairs with, it **overwrote the real target name**. Six
  rows were being refused as `reloc names ?sActive@MessageTimer@@1_NA vs ours
  @comp.id`.
* **A relocation whose target is itself a `merged_<addr>` placeholder** was
  compared by string, refuting two rows (`ObjPtrVec::Set` at `0x823E8A38` and
  `ObjPtrVec::erase` at `0x823EA0B8`) that reference each other across two
  folded addresses.

Same shape as the correction recorded in `comdat-tier2-triage-20260819.md`
(a mask asymmetry that reported 89 false differences): **a body test's
refusals are a claim about the instrument until the instrument has a negative
control.**

### `fold_proof.py`'s zero-relocation refusal re-verified

The brief required checking that the existing suite's cheapness guard still
fires before trusting anything downstream of it. Exercised directly:

```
code, zero-reloc, real stub bytes    -> cheap = True
code, zero-reloc, 400 B unique body  -> cheap = True     <-- CODE is always cheap when reloc-free
data, 4 B                            -> cheap = True
data, 64 B all-zero                  -> cheap = True
data, 64 B nonzero                   -> cheap = False
```

Intact, and *stricter* than this lane's WEAK tier: `fold_proof.py` refuses any
relocation-free **code** body regardless of length, which is the guard that kept
`?Handle@HamDirector@@`'s `OnSaveFaceanims` pair honest. This lane's WEAK rows
are consistent with it — they are likewise not certified on bytes, and are
installed on the map instead.

One thing that is **not** a defect, recorded so it is not "fixed" later:
`icf_pairing_bodytest.py` also ignores `IMAGE_REL_PPC_PAIR`'s bogus symbol
index, but only incidentally — it collects relocated offsets into a *set* and
uses them only for masking, and a PAIR shares its REFHI's offset, so the set is
unchanged. The defect only bites a tool that maps offset → *name*, which is what
this lane's test does. `fold_proof.py` already filters PAIR explicitly.

## The ambiguity, stated rather than hidden

**27 of the 43 addresses have more than one member our own object defines that
passes the body test.** At `0x8237A778` both
`??$_Copy_Construct@UEyeDesc@CharEyes@@…` and
`??$_Param_Construct@UEyeDesc@CharEyes@@U12@@…` pass, both from
`char:CharEyes.obj`. They are byte-identical *by construction* — that is what
folding means — and **no body test can ever choose between them.**

The tiebreak is the map's contributing-`.obj` column: prefer the member the
linker says came from the `.obj` that owns this address range. That is the rule
`docs/analysis/comdat-tier2-triage-20260819.md` derived for dtk's splitter, and
it is the only choice stable under a re-split. All 43 installed names satisfy
it. It changed one pick materially:

* `0x8298DFF0`, unit `meta_ham/PlaylistSortNode` —
  `?SetItemCountString@MQSongHeaderNode@@` (`meta_ham:MQSongSortNode.obj`, first
  in map order) → `?SetItemCountString@PlaylistHeaderNode@@`
  (`meta_ham:PlaylistSortNode.obj`), which is the unit the address actually lies
  in.

Where every passing member comes from this unit's own `.obj` (e.g. `0x82412D08`
carries `BinStreamEnum` reads for `EaseType`, `Rate` and `StopMode`, all from
`flow:FlowSetProperty.obj`), the choice is genuinely **arbitrary among equals**
and is resolved by sorting the names. That is a labelling convention, not a
claim, and it is recorded here so nobody later reads the pick as evidence about
which spelling the original TU "really" used.

## The WEAK_NO_RELOC tier — installed, but discount it independently

11 rows (276 B) pass the body test with a body that has **zero relocations**.
`{ return mField; }` is 8 identical bytes and every such accessor in the image is
byte-identical to every other, so byte-identity here does not discriminate
between fold members and is not, on its own, evidence. Compare the standing
precedent: `fold_proof.py` correctly **refused** to certify `OnSaveFaceanims`
from bytes alone because both bodies were 16 zero-relocation bytes.

They are installed anyway, on a different and sufficient basis: for each, the
shipped linker map states that the name is at that address *and* that its
contributing `.obj` is this unit's own `.obj`, and our object defines it. The
map is the linker's own statement about the image it produced. The tier is kept
separate in the tooling and in `adjudication.json` so a later reader can
discount these 276 B without touching the other 4,208 B.

Five of the eleven are one-instruction-plus-`blr` accessors in `os/AsyncFile`,
`os/ArkFile`, `os/PlatformMgr` and `os/User`.

## The 5 refusals — real gaps, and all of them metric-invisible

Every candidate for these five is absent from **both** `symbols.txt` and
`report.json`. Nothing anywhere in the build scores them. Renaming them would
have moved the metric without any code being fixed — the exact failure the brief
forbids.

| address | unit | target | ours | what it is |
|---|---|---:|---:|---|
| `0x823AAA20` | `char/CharSignalApplier` | 436 B | 64 B | **`CharSignalApplier::Handle` is a one-line stub** — `return Hmx::Object::Handle(d, b);`. The target is the full `MessageTimer`/`SyncProperty` Handle body. |
| `0x8237A7E8` | `char/CharEyes` | 84 B | 60 B | `_Param_Construct<CharInterestState>`: the copy is 24 B larger than ours, while the sibling `_Copy_Construct<EyeDesc>` at `0x8237A778` matches at 60 B exactly. Points at `CharEyes::CharInterestState` having a member our struct lacks. |
| `0x82984FC8` | `meta_ham/PlaylistSongProvider` | 76 B | 100 B | `~PlaylistSongProvider` — **ours is 24 B bigger** than the original. |
| `0x825C6868` | `obj/TypeProps` | 100 B | 88 B / 96 B | `ObjPtrList<T,U>::remove` — shape difference, both peers short. |
| `0x823EA0B8` | `flow/FlowManager` | 240 B | 240 B | `ObjPtrVec<T,U>::erase` — **pure register allocation.** Same length, same four relocated offsets (`0x4`, `0xb4`, `0xdc`, `0xec`), same relocation targets; 9 of 60 words differ and every one is a register-field-only difference (`7c7b1b78` = `mr r27,r3` vs `7c7d1b78` = `mr r29,r3`; r27↔r29, r28↔r27, r26↔r29). |

The last two corroborate `comdat-tier2-triage-20260819.md`'s lead that the
`ObjPtr` containers carry metric-invisible divergences, and **refine it**: that
document measured `ObjPtrVec::erase` as `-36 B` against the
`ObjPtrVec<Spotlight>` peer in `world/LightPreset`. Measured here against the
unit-owned peer `ObjPtrVec<FlowNode>` in `flow/FlowManager`, the sizes are
**equal** and only registers differ. Peer selection changes the diagnosis, so
the size delta in that table should not be carried forward as a property of
`erase` itself.

`CharTransDraw::Handle` at `0x8239EA20` — same 436 B shape, same fold family —
is now proven at 100 %, so it is a direct template for writing
`CharSignalApplier::Handle`.

Not attempted here; all five want their own lane, and the `ObjPtr` two touch
`src/system/obj/ObjPtr_p.h`, which is included by most of the tree.

### Coordination note for the `ObjPtr` lane

`fix/objptr-icf-bodies` is concurrently experimenting on exactly these
containers (`EXPERIMENT: drop __declspec(noinline) from ObjPtrVec::Set, restore
plain erase body`). It does not touch any file this lane touches, so there is no
merge collision — but there is a **measurement** interaction worth knowing:

`?Set@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@…` at `0x823E8A38` was scored
*nowhere* before this lane and is now a live 84 B row at 100 %. That lane
therefore gains a regression gate it did not have: a change to `ObjPtrVec::Set`
that was previously invisible to the metric will now show up. `erase` at
`0x823EA0B8` remains unscored (refused above), so it still needs the body test
rather than the metric to evaluate.

## What was deliberately not touched

* **`scripts/symbol_aliases.json`.** Each of the 43 addresses already has a
  group there whose `folded` list independently corroborates the map; the rename
  only makes the group's `survivor` field stale. The file says do not hand-edit,
  and `scripts/gen_icf_alias_map.py` renders all members at the survivor
  **address**, so `reloc_eq` keeps working unchanged (`icf_aliases.map` still
  loads 8,719 entries). Regenerating via decomp-synth's
  `build_icf_alias_inputs.py` will re-mint these groups with the new survivor.
* **dtk's splitter.** The general fix — teach dtk to prefer the fold member whose
  contributing `.obj` owns the address range — is the right one and is scoped in
  `comdat-tier2-triage-20260819.md`; it lives in `../jeff`, needs a manual
  `cargo build --release`, and `bin/objdiff-cli`/dtk are shared with rb3 and
  rb3-xenon, so its blast radius is three repos. This lane fills in the 48
  addresses in the config the splitter already reads, which is what
  `symbols.txt` is for (cf. `scripts/extract_decomp_symbols.py`, whose whole
  purpose is "update symbols.txt with real names").

## Reproduce

```bash
python3 scripts/analysis/icf_survivor_names.py --verbose --json /tmp/icf48.json
python3 docs/analysis/icf-survivor-names-20260819/negative_control.py   # from the repo root
python3 scripts/analysis/icf_survivor_names.py --apply --include-weak   # installs the 43
```

Full per-row adjudication, including every candidate considered, the map's
contributing `.obj` for each, the in-unit fold twins and the refusal reasons, is
in `docs/analysis/icf-survivor-names-20260819/adjudication.json`.
