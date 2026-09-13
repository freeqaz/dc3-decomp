# Relocation names are unmetered: a function can call the wrong callee at 100%

> **STATUS 2026-08-20: FIXED for `name_check`, in the objdiff fork.** The
> mechanism described below was real and is worth reading — it is why this class
> went unmeasured for months — but `match_percent_normalized` is no longer blind
> to it *provided your `bin/objdiff-cli` postdates the fix*. Nothing rebuilds
> that binary for you, and it is a symlink shared with `../rb3` and
> `../rb3-xenon`, so check its mtime before trusting a row. See
> [the 2026-08-20 analysis](../../analysis/2026-08-20-reloc-normalized-unfold.md)
> for the whole-binary A/B, the three noise carve-outs, and the adjudication of
> every function that left the matched set.

**Project: dc3-decomp (Dance Central 3, Xbox 360, MSVC PPC, title `373307D9`).**
Every number below is dc3-decomp's, measured 2026-08-19 at `main` = `49bb1f8bf`.
`../rb3` and `../rb3-xenon` share symbol names and address ranges; do not
re-check any of these against them.

## The mechanism

```
match_percent_normalized = diff_score − arg_diff_score
```

and objdiff folds **relocation penalties into `arg_diff_score` by design**
(`objdiff-core/src/diff/code.rs:1626-1641`). The penalty is therefore subtracted
back out under *every* `functionRelocDiffs` value, `name_check` included. **There
is no `-c` flag for it.** On top of that, `scripts/orchestrator/mcp_server.py`
and `scripts/sync_objdiff.py` hard-code `functionRelocDiffs=none`, so
`run_objdiff`'s headline and `decomp.db.current_percent` are blind twice over.

Demonstrated: repointing all 13 `bl` sites of a 100%-matched function at a
nonexistent decoy — **changing zero instruction bytes** — left `run_objdiff`
printing 64 equal / **0 mismatches** and normalized at exactly `100.0`.

So the standing rule *"a divergence on a 100%-matched function must be a harness
artifact"* is unsafe **even at a zero-mismatch instruction count**, because a
wrong callee produces zero mismatches.

## The one surface that does charge it

`report.json`'s `fuzzy_match_percent` under the graded `name_check` ruler. It
costs ~0.01–0.2 points on one function out of 48,344, i.e. it is visible to the
tool and invisible to the number anyone reads.

The isolating measurement is the **delta between two reports that differ in
exactly one config key**:

```
graded (name_check) score  <  relocation-blind (none) score
```

Nothing else can move between the two legs. Both legs load
`build/373307D9/icf_aliases.map`, so folds the project has already adjudicated
never enter the population.

## The population, re-derived — and the filter that nearly hid half of it

| | rows | bytes |
|---|---|---|
| functions scored | 48,344 | — |
| **carry ≥1 relocation-name charge** (graded < blind, named) | **508** | **433,788** |
| …`fn_`/`lbl_` MSVC EH funclets, excluded by default | 120 | — |
| …of the 508, **otherwise PERFECT** (100% blind) | 42 | 17,084 |

⚠ **The first version of this write-up, and of the gate, defined the population
as "100% blind and <100% graded" — 186 rows / 32,388 B, of which 113 funclets
and 73 named.** That is a defensible *reporting slice* (on those rows, closing
the name crosses the row and pays its full size, because `matched_code` is
all-or-nothing) and an **indefensible population**: it silently drops every
wrong-callee bug that happens to sit on a row which *also* has instruction
mismatches. Which is most of them — 42 of 508, i.e. **8%**.

It was the end-to-end negative control that caught this, not review. Re-applying
the `createFilter` bug produced **no output at all**, because
`EQEffect::SetParameter` is 84.7% under the blind ruler and the filter threw it
away. That is precisely the "silent `continue` on exactly the population that
mattered" failure this project has now found eight times.

A related trap: under the old definition, *every* row necessarily had
`other_charges == 0`, and the first draft reported that as a finding. It was a
tautology — the filter had selected for it.

A *different* wider set — `match_percent_normalized == 100` with graded
`fuzzy < 100` — is 384 rows / 145,968 B, but that set is contaminated with
register permutations, which normalization also forgives. Do not use it for this
class.

## The discriminator

Resolve **both** names in `orig/373307D9/ham_xbox_r.map` and compare addresses.

| verdict | meaning |
|---|---|
| `FOLD` | both names at the same address — ICF, benign |
| `DIFFERENT_ADDRESS` | both present, different addresses — **real divergence** |
| `BASE_NOT_IN_MAP` | our name absent from the image — a lead, but **weak**: an ICF fold loser is also absent |
| `TARGET_NOT_IN_MAP` | dtk synthetic (`merged_*`, `OnlyReturns`) — usually a fold |
| `NEITHER_IN_MAP` | statics and local-scope symbols the map never lists |

## Check the instrument first

Three of the loudest "bugs" in this population were **the config, not the
source**, and each one reads exactly like a source bug until you look at the
target's own bytes.

**1. A config address that contradicts the shipped map.**
`config/373307D9/symbols.txt` placed `?sJointParents@BaseSkeleton@@2QBW4SkeletonJoint@@B`
at `0x8202EE20` size `0xA0`; the map says `0x8202EEC0` size `0x50`. The name
therefore landed on the block `MirrorJoint` actually reads, and the row read as
*"MirrorJoint indexes the joint-PARENT table"* — a shipped gameplay bug. It was
not. The target's own `.rdata` settles it entry for entry:

```
0x8202EE20  0,1,2,3, 8,9,10,11, 4,5,6,7, 15,16,17, 12,13,14, 19,18   == gMirrorJoints
0x8202EEC0  20,0,1,2,2,4,5,6,2,8,9,10,0,12,13,0,15,16,14,17          == sJointParents
```

Cross-checking every config symbol against the map — 211,852 config symbols,
118,000 map names, **107,552 in both** — found **exactly one** disagreement, and
it was this one. The config is not broadly untrustworthy; it was wrong in
precisely the place that manufactured a false story.

**2. A run of config names shifted by one slot, invisible to the map.**
All twelve `??__FgShaderX` atexit thunks charged a relocation naming the *next*
shader global. `.data` globals are not in the shipped map, so the check above
cannot see this. The check that can is **internal consistency**: a global object
begins with its own vtable pointer, so

```
config says gShaderParticles @0x82F14FEC  ...holds ??_7RndShaderSimple@@6B@
config says gShaderStandard  @0x82F14FF4  ...holds ??_7RndShaderMultimesh@@6B@
```

is impossible. Eleven names each belonged one slot later, `gShaderSimple` was
missing entirely, and the last slot (`lbl_82F15018`, holding
`??_7RndShaderSyncTrack@@6B@`) had no name.

**3. A unique config symbol at an address its own use contradicts.**
`?gRevs@?1??Load@ThreeDSound@@UAAXAAVBinStream@@@Z@4QBGB` is the only symbol of
its shape in 211,852 (there are 248 `INIT_REVS` sites). Config puts it at
`0x820BE6FC` size `0x8`, but the target's `Load` does `subi r7, r28, 0x4` off
it — reading *before* the array — and the block's second word is a relocation to
`??_R4ThreeDSound@@6BRndHighlightable@@@`, i.e. not `unsigned short` data at all.
`0x820BE6FC` is our `gAltRev` and `0x820BE6F8` is our `gRev` (the "float"
`5.51e-40` in `lbl_820BE6F0` is the bytes `00 06 00 00`, i.e. `gRev = 6`).
**Left unfixed** — the row is a config artifact but the correct name is not
recoverable, and deleting a name is destructive.

**4. A config anonymous-namespace hash the shipped image never had.**
Three `WRONG_CALLEE` rows (`__uninitialized_copy`, `__uninitialized_fill_n` x2,
over `` `anonymous namespace'::Unlockable ``, all at 91.5% with byte-identical
bodies) charge target `_Copy_Construct` against our `_Param_Construct`. Both are
placement-new one-liners, so `/OPT:ICF` folds them — and the shipped map says so
outright, carrying **both** names at `0x828B5998`:

```
0005:00585998  ??$_Copy_Construct@UUnlockable@?A0xf8e4b4b5@@…   828b5998  meta_ham:MetagameRank.obj
0005:00585998  ??$_Param_Construct@UUnlockable@?A0xf8e4b4b5@@…  828b5998  meta_ham:MetagameRank.obj
```

Our build emits the `?A0xf8e4b4b5` spelling, i.e. a member of that very group,
so the call is the same bytes to the same code. The row survives only because
**`symbols.txt` names that address with a different anon-namespace hash**,
`?A0x9d17dd81`, which the map does not contain at any address. The counts settle
it: `9d17dd81` appears **3 times in `config/373307D9/symbols.txt` and 0 times in
`orig/373307D9/ham_xbox_r.map`**, while `f8e4b4b5` appears 70 and 81 times
respectively. The three mis-hashed entries are exactly the two `_Copy_Construct`
and one `__destroy_range` symbols behind these rows.

This is why `gen_icf_alias_map.py`'s retail-map widening cannot reach them: the
group is keyed by name, and the target-side name is not in the map to be
grouped. **Adjudicate these three as PROVEN FOLD, not source work.** Renaming
them in `symbols.txt` would make the name honest but would not close the rows —
our objects reference only one member of the group, so the widening gate ("two
or more names our own objects reference") still declines — and it re-triggers
the split for no metric gain. Left as a documented instrument defect.

Both instrument checks now run inside
`scripts/analysis/reloc_name_gate.py`, before any row is adjudicated.

## What the class actually contained

Fixed (all verified by map address, none visible to `match_percent_normalized`):

| what | evidence |
|---|---|
| `Task` had a user-declared default ctor | `??0Task@@QAA@XZ` is in **no** map entry; `PropertyTask`/`AnimTask` call `??0Object@Hmx@@QAA@XZ` with one `bl` and one vptr store — MSVC inlining an *implicit* intermediate base ctor and eliding its vptr store |
| a local `operator>>` shadowed a template | `??5@Y...` vs `??$?5VTransformArea@@@@Y...` (@824af8f8) |
| `MakeString<char>` vs `<unsigned char>` | `8268f6e8` vs `825f7ae0` — two addresses, not a fold |
| a string literal deduced `const char(&)[2]` | `??$MakeString@$$BY01$$CBDPBDPBD@@` vs `??$MakeString@PBDPBDPBD@@` (@823a32f8) |
| `kArkBlockSize` was not `const` | `?kArkBlockSize@@3HA` vs `3HB` |
| `gPhysicalType` was `char*` not `const char*` | `3PADA` vs `3PBDB` — MSVC repeats a pointee `const` in the trailing cv slot, so `PBD…B` is `const char *`, **not** `const char *const`; the target stores to it |
| Bink SDK declared C++ instead of C | map carries `BinkSetIO` @82ee7c38 and `BinkSetMemory` @82ee9e20 **unmangled** |
| two `MILO_ASSERT` texts | the macro stringifies, so the assert *text* is image content: `mHeight` (8 B) vs `mHeight != 0` (13 B); `Abs<float>(…` (51 B) vs `Abs(…` (44 B) |
| two global renames | `gDataArrayConditional`→`gConditional`; `award_sort_map`→`award_sort_indices` |

Whole-build against the same tree's own baseline: **matched_code +9,488 B, 35
rows improved, 24 rows to exactly 100.0, 0 regressed.**

### `MILO_ASSERT` stringifies — the assert text is image content

`MILO_ASSERT(Abs<float>(x) <= k, …)` bakes `Abs<float>(x) <= k` into `.rdata`.
`docs/decomp/patterns/fixable-abs-overload-shim.md` prescribes `Abs<float>` for
the **lowering**, and it is right about that — but the spelling is *also*
observable, and no percentage charges it. A file-local macro keeps both:
`#` suppresses expansion of the stringified argument, so the recorded text is
`Abs(…)` while the emitted call is still `Abs<float>`.

### `#include` spelling and order are observable

`__FILE__` records the path by which a header was **first** reached, and MSVC
bakes that into every `MEM_OVERLOAD` / `MILO_ASSERT` expanded there. The
separator itself is evidence:

```
BinkMovieSys.cpp   target: e:\lazer_build_gmc1\system\src\movie/MovieImpl_p.h
MovieSys.cpp       target: e:\lazer_build_gmc1\system\src\movie\MovieImpl_p.h
```

Same header, same build. One TU reached it through the `-I` path as
`movie/MovieImpl_p.h`; the other reached it bare from its own directory. Fixing
one by changing the shared header breaks the other (`-0.45`); hoisting the
second TU's own bare include above the shared header gives both what they want.

**Read the decoded string, never the mangled name.** `??_C@_0DD@KHGMMELO@…` and
`??_C@_0DD@LAENHEGL@…` truncate to the same 32 characters and look identical.

## What is left

427 of the 508 rows still carry a non-exempt charge (354,492 B), but most of
those are rows with hundreds of instruction mismatches whose float-constant-pool
relocations differ *because* of the other bugs. The tranche worth reading first
is the **24 rows that are otherwise perfect** (7,372 B) — on those the name is
the only defect:


* **`__FILE__` header-provenance, 11 rows ≈1,100 B.** Ours records a bare
  `.cpp` basename where the target records a header — our class declaration or
  its `MEM_OVERLOAD` lives in the wrong file (`CharBones`, `AllocInfo`,
  `FlowSlider`, `FlowSetProperty`) — or both record headers but different ones
  (`FileStream`, `MultiMesh`→`utl/PoolAlloc.h`, `CriticalSection`,
  `UsbMidiGuitar`→`os\UsbMidiGuitar.h`). Real and actionable, but each is a
  header-layout change. `MultiMesh`'s is **PCH-pinned**: `PoolAlloc.h` is in
  `decomp_pch.h`, so no per-TU include can change its spelling.
* ~~**`??_B` vs `?$Sn@` local-static guard spelling, 5 rows ≈2,950 B.** The target
  emits a bit-packed guard (`??_B<scope>@<fn>@5<bit>@`); we emit a per-static
  `unsigned int` guard (`?$Sn@<scope>@<fn>@4IA`). No source lever found —
  probable compiler-mode floor.~~
  **FIXED 2026-08-21. All five are at a zero-mismatch 100 and the "compiler-mode
  floor" reading was wrong on both halves** — see
  [the guard-name section below](#the-b-vs-s-guard-name-was-never-the-defect).
* **`rijndael_test`, 1,608 B.** `?key192@…@4QBEB` (const `unsigned char[]`) vs a
  const `unsigned int[]` at `820c4fe0` — read-only COMDAT data fold, benign.
* **`ThreeDSound::Load`, 800 B.** Config artifact, above.
* **`SampleInst360` 276 B / `~AppLabel` 108 B.** Our callee is absent from the
  image and the target's is a known fold winner (`DrawHighlightMat` ≡
  `Sound::Sample` @8261d060; `~HamLabel` @82517e00). Probable folds the alias
  map does not cover because our spelling is not a target symbol.
* ~~**`NgPostProc::CheckHueConverge`, 180 B — REAL, unresolved.** The target calls
  the 75-member `merged_Returns1` group at `82E2AB00`, i.e. a predicate whose
  body is `li r3,1; blr`. Our `RndPostProc::ColorXfmEnabled()` has a real body,
  and the target's own `?ColorXfmEnabled@RndPostProc@@QBA_NXZ` sits separately at
  `8266b0a8`. So the target's predicate is a different, trivially-true one — and
  none of the 75 fold members is a `PostProc` method, so its name is not
  recoverable from the map.~~
  **RETIRED 2026-08-21 — it is a fold, and the entry was reading the wrong
  callee.** `reloc_name_gate.py` resolves the charged pair as
  `?IsLocal@LocalUser@@UBA_NXZ` (target) vs **`?DoHueConverge@RndPostProc@@QBA_NXZ`**
  (ours) — not `ColorXfmEnabled` — and both sit at `82e2ab00`, so the map itself
  says they are the same code. `DoHueConverge` *is* in the 75-member group; the
  entry above searched the group for a `PostProc` method and concluded there was
  none, which was a search over the wrong name. See the ICF-fold section below.

## The `??_B` vs `$S` guard name was never the defect

**2026-08-21.** The five rows struck out above closed without anything
compiler-mode changing. Two facts, both measured against this build's own
`cl.exe` at `/O1 /Oi /EHsc /TP`:

1. **What picks the name.** MSVC emits `??_B<scope>@<fn>@5<scope>@` for a
   function-local static inside a **COMDAT** function (inline member, in-class
   member, function template) and `?$S<n>@<scope>@<fn>@4IA` — `n` a per-TU
   counter — inside an ordinary out-of-line function. A probe TU with six
   ordinary shapes gave `$S` six times; one with five COMDAT shapes gave `??_B`
   five times. Nothing about bit-packing distinguishes them: both forms are one
   guard word with one bit per static.
2. **Why we still lost.** `scripts/obj_guard_patcher.py` already renames `$S` to
   `??_B` to match the original. It keys on the **scope ordinal**, so while ours
   disagreed it declined to fire — silently, which is why the name looked like
   the primary defect instead of the shadow of one.

The actual defect is a **third `MILO_ASSERT` spelling**. `MILO_ASSERT`'s
`do { if (!c) { … } } while (0)` opens 5 lexical scopes and `MILO_ASSERT_EXPR`
opens 0; these functions need one that opens 3, i.e. the same body with the
`do`/`while` peeled off. That is now `MILO_ASSERT_IF` in `os/Debug.h`, carrying
the same "do not use at a new call site unless the target's scope index demands
it" warning as `MILO_ASSERT_EXPR`, plus a dangling-`else` hazard note.

Two of the five needed one further fact: after the assert fix their ordinal was
still off by exactly the cost of an `else` (2), **in opposite directions**, which
identifies which side of the test the original put the token branch on.
`SongSelectPlaylistProvider::Text` is `if (IsCustom() && IsEmpty())` with the
static in the `if`; `CampaignSongProvider::Text` is the mirror image. Both
rewrites are De Morgan duals of what we had and both took the row to 100.

**The general lesson:** when a symbol *name* differs and a patcher exists that is
supposed to reconcile it, check whether the patcher fired before concluding the
name is a floor. A patcher that declines silently looks exactly like a compiler
that cannot be steered.

## Every "tier 1" row of the 2026-08-21 census is an ICF fold

The whole-binary census ranked six rows as "the name is the only defect". Run
through `reloc_name_gate.py`, **five of the six resolve both names to the same
address in `ham_xbox_r.map`** — i.e. the linker itself says they are one function:

| row | pair | address |
|---|---|---|
| `SaveLoadManager::Poll` / `::SetState` | `GetNumRotFeatures@SkeletonPCAFeatureConverter` ≡ `GetGlobalOptionsSize@ProfileMgr` | `829fb500` |
| `SampleInst360::SampleInst360` | `DrawHighlightMat@RndShaderMgr` ≡ `GetData@SynthSample360` | `8261d060` |
| `__introsort_loop<CuePoint>` | `__median<AllocInfo*>` ≡ `__median<CuePoint>` | `8254e800` |
| `NgPostProc::CheckHueConverge` | `IsLocal@LocalUser` ≡ `DoHueConverge@RndPostProc` | `82e2ab00` |
| `vector<Label>::push_back` | `_Copy_Construct<pair<const String,unsigned int>>` ≡ `_Copy_Construct<Label>` | `8273b9e8` |

The sixth, `SaveLoadManager::Poll`'s other pair, is `MakeString<CamShotFrame::BlendEaseMode>`
(@`82610090`) against our `MakeString<SaveLoadMode>`, which is **absent from the
map** — the fold-loser signature. Every `MakeString<W4 enum>` instantiation has
an identical body, so there is only ever one survivor.

**None of these is a source edit**; making the source name the survivor would
mean writing a call the program does not make. The gap is that
`build/373307D9/icf_aliases.map` does not admit these groups, and
`gen_icf_alias_map.py` explicitly forbids hand-adding one — the input has to be
regenerated through decomp-synth's validated
`tools/il_witness/build_icf_alias_inputs.py`.

### …but an alias group does NOT rescue an *unpaired* symbol, and that has been assumed twice

⚠ An alias group makes two names compare equal as **relocation targets**. It
does **not** pair two differently-spelled **symbols**, so it cannot move a
function that reads `0.0%` because the target defines the ICF survivor's
spelling while our object defines the folded one. Adding the fold is still the
right thing to record — it is true, and it is what stops the row being
re-reported as a wrong-callee bug — but **do not expect a percentage for it.**

Measured on `main` at `a5eefa51d` (2026-09-01), whole binary: **118 functions /
8,172 B sit at `match_percent_normalized == 0.0` while their name is ALREADY a
member of an alias group.** Among them are `??_E?$CSampleXAPOBase@
VCompressionEffect@@UParams@1@@ATG@@MAAPAXI@Z` (100 B) and
`??_E?$StandardEffect@VCompressionEffect@@@@UAAPAXI@Z` (76 B), members long
before the 2026-09-01 harvest ran, and `?Process@?$CSampleXAPOBase@VWahEffect@@
UParams@1@@ATG@@UAAXIPBU…@Z` (132 B). Adding the two missing `??_E` siblings
(`GainEffect`, `HeadsetPlaybackEffect`) left them at `0.0%` too — 
`matched_code`, `matched_functions` and `fuzzy_match_percent` were byte-for-byte
unchanged across that widening.

⚠ **The mechanism named here as the one that "would pair them" does NOT pair
them, and enabling it would be a mistake. Measured 2026-09-01, task #185 — do
not re-open this.** `diff::reconcile_global_byte_matches`
(`objdiff-core/src/diff/mod.rs:1136`), gated on `objdiff-cli report generate
--global-byte-eq`, is **not** an alias-pairing pass at all. It is the rb3-xenon
**case-B identity transfer**: it promotes an unmatched base-defined method to
100% when its reloc-masked + reloc-name signature uniquely matches a body
carved into *some other unit's* target obj. Three independent reasons it is
wrong here:

1. **It is a measured no-op on DC3.** `report generate` run with and without
   the flag over the *same* prebuilt objects, worktree `globaleq-185` @
   `10dc2a23b`, objdiff-cli `4.2.8 (358c715835cc, xxh3 9b2bb6f1f3a21062)`:
   the two `report.json` files are **byte-identical** (sha256 `03627c37d1361fa4…`),
   **0 promotions**, headline unchanged in every field (`matched_code`
   5,074,704 / 11,373,948; `matched_functions` 29,971 / 48,357; `fuzzy`
   54.481575; `masked_equal` 1,351). The pass's own funnel
   (`OBJDIFF_CASEB_DEBUG=1`) says why: its retail index needs a per-symbol
   retail VA, the carved COFF objs carry no `.note.split` VA array, so the only
   indexable bodies are the still-anonymous `fn_<VA>` ones — **168 of 38,416**
   target code symbols >44 B. `sig_in_retail_index=0` of 1,870 signed base
   methods. DC3's `symbols.txt` coverage is too *good* for this pass: only
   1,690 of 456,643 target symbols (0.37%) are still anonymous.
2. **Its honesty gate is unsatisfiable here.** Rule 3 requires
   `--global-byte-eq-oracle`, a per-VA **rb3-Wii BinDiff** attribution
   (`unified_id_rb3wii.json`); the CLI refuses to run without it. No DC3 oracle
   exists. rb3-xenon's covers rb3 VAs `0x82260000`–`0x82c221e8`, which
   **numerically overlap DC3's code range** — pointing it here would
   mis-attribute silently rather than error.
3. **Relaxing it manufactures false 100%s.** Counterfactual, probe binary
   `4.2.8 (210aab60ca30-dirty, xxh3 d88dbd70c9514e19)` built in an isolated
   worktree (deployed `bin/objdiff-cli` untouched), index relaxed to accept
   named retail bodies, oracle bypassed: **3 promotions, and 2 of the 3 are
   false** — `__uninitialized_copy<XAUDIO2_SEND_DESCRIPTOR>` (Synth) paired to
   `__uninitialized_copy<KernInfo>` (Font), retail `0x82e2f6d8` vs
   `0x82704b70`; `__uninitialized_fill_n<XAUDIO2_SEND_DESCRIPTOR>` (Synth) to
   `__uninitialized_fill_n<HamMoveKey>` (HamDirector), `0x82e2f880` vs
   `0x8246e2f0`. Different types, different units, different retail addresses.
   Rules 1 (>44 B), 2 (injectivity both sides) and 5 (reloc-name-carrying
   signature) all passed; **only the oracle would have caught them**, exactly as
   that function's own comment warns ("verified: 4 un-oracle'd STL folds").
   **The worst part:** the Synth *target* obj defines both of those symbols
   under their own correct names. The per-unit diff had already paired them
   correctly, at **49.29%** and **42.00%**. The pass would overwrite two
   genuinely-wrong decompilations with 100% — laundering, not recovery.
   Only the third promotion (`?MemOrPoolFreeSTL@@YAXHPAXPBDH1@Z` → 
   `?MemOrPoolFree@@YAXHPAXPBDH1@Z`, both at `0x827cb500`, already an alias
   group) was legitimate: **60 B of the 8,172**, 0.7% of the target population,
   bought at the price of two fabricated matches.

So the headline is structurally unreachable for this row class, and that is the
correct outcome, not a gap to close. The 8,172 B stays unbooked.

Where the alias set *does* pay is the relocation charge, and that is
measurable: the same widening took the charged population from **386 rows to
383 (−288 B)** with **0 rows entering and 0 scores dropping**
(`scripts/analysis/reloc_name_gate.py`, before/after on one tree).

Re-derive both tiers whole-binary, with printed denominators, from:

* `scripts/retail_map_fold_reconcile.py` — linker-map address sharing + a
  byte-identity gate; reaches any name our objects **define**.
* `scripts/weak_alias_reconcile.py` — the COFF `IMAGE_WEAK_EXTERN_SEARCH_ALIAS`
  aux record; the only tier that can speak about a name our objects merely
  **reference**, which is why the retail-map tier's gate 5 drops those as
  "no body to check". Both are ledgered, both are `--apply`-idempotent, and each
  re-attaches the other's members rather than clobbering them (the 2026-09-01
  failure mode: a retail re-mint silently dropped two weak-tier names while the
  weak ledger still claimed they were installed).

## Two families that look like bugs and are register permutation

Worth knowing before re-mining this population, because both are visually loud:

* **RTTI operands to `__RTDynamicCast`.** `UIManager::GotoFirstScreen` shows
  `??_R0?AVUIScreen@@@8` and `??_R0?AVObject@Hmx@@@8` apparently swapped across
  four instructions — but the *final* register→value mapping is identical
  (`r5` = SrcType, `r6` = TargetType on both sides). Only which of `r10`/`r11`
  stages which `lis` differs. Same story on `GatherObjectsFromGroup<RndMesh>` and
  `GatherObjectsFromDir<RndMesh>`.
* **`__real@` float constants.** `DirectionGestureFilterSingleUser::Draw` looks
  like a 0.1/0.2 swap; it is `f28`/`f29` holding the two constants the other way
  round, with `f1` and `f2` receiving the correct values at the call.

`name_check` charges these only because the relocation happens to sit on the
permuted instruction. Canonical normalization forgives register permutation, so
the row's `match_percent_normalized` is unaffected — which makes a graded-fuzzy
row with only `__real@`/`??_R0` pairs a weak lead, not a finding.

## The standing check

```bash
python3 scripts/analysis/reloc_name_gate.py --project . --json-out /tmp/rows.json
python3 scripts/analysis/reloc_name_gate.py --selftest      # negative control
```

It **lists**; it does not classify a row away. `split_reloc_residency.py` would
have buried `createFilter` as a candidate ICF fold, so every standing row prints
its charged pairs with both map addresses and the reader adjudicates. The only
judgement is three named exemption buckets (anon-namespace placeholder, MSVC
scope counter, dtk synthetic fold name) and **every bucket's count prints on
every run, next to the denominator** (48,344 rows scanned).

`--selftest` is the *unit-level* negative control: it replays the recorded
`createFilter` charge through the same adjudication path and asserts it is
**not** exempted, plus five more must-not-swallow cases. **It is not sufficient
on its own** — it passed while the end-to-end control was failing, because the
defect was in the population filter, upstream of anything `--selftest` touches.
Run both. Two of those exist because the first
version of the scope-counter exemption was wrong — its regex matched only the
letter-run encoding (`?HP@??Foo`) and silently missed the bare-digit one
(`?9??Foo`), and it has to *not* fire when the variable differs rather than the
counter, or it would have swallowed `ThreeDSound::Load`.

End-to-end control (**run this, not just `--selftest`**): re-declare
`createFilter` `extern "C"` in `src/system/dsp/EQEffect.cpp`, rebuild, re-run.
The row must appear with

```
2164 B  blind=84.7153 graded=84.4935  other_charges=213
  default/system/dsp/EQEffect :: ?SetParameter@EQEffect@@QAAXHM@Z
    [BASE_NOT_IN_MAP]
      TARGET ?createFilter@@YAXW4FilterType@@W4FilterBand@@IMMPAUFILTER@@H@Z  @['82e5c228']
      OURS   createFilter  @-
```

Assert on the **pair**, not on the population count: that row is in the
population either way (its float-pool relocations also differ), so the count is
508 with and without the bug. Reverting must take the `createFilter` pair to
zero occurrences in the output.

## See also

* [rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md) — the earlier,
  narrower version of this lesson (rounding, not relocation).
* `CLAUDE.md` → Known Patterns, first bullet.

## Reading a gate row: three measured facts before you touch source

*(dc3-decomp, lane `fix/well-i-relocname-deep`, 2026-08-23. Population re-derived
in this worktree: 419 rows scanned, **331 standing after exemptions**, 275,716 B,
727 charged pairs. `objdiff-cli 4.2.8 (358c715835cc, xxh3 9b2bb6f1f3a21062)`.)*

### 1. Half the standing population cannot pay a canonical point — by design

The gate's population is defined on **`fuzzy_match_percent`, blind vs graded**.
`fuzzy` is charged for every relocation-name difference. `match_percent_normalized`
— the number `report.json` reports and the number `matched_functions` is gated on
— is **not**: it still *folds* three noise classes into `arg_diff_score`, register
save/restore helpers being the big one (see the first bullet of CLAUDE.md → Known
Patterns). So a row can sit in the population and have literally nothing to win.

Measured directly, by generating a second report under `-c functionRelocDiffs=none`
and differencing `match_percent_normalized` per row:

| bucket (by class of the charged pairs) | rows | bytes | rows with canonical headroom | those bytes |
|---|---:|---:|---:|---:|
| `__savegprlr_N` / `__restgprlr_N` only | 181 | 145,912 | **18** | 18,916 |
| mixed / named symbol | 117 | 107,156 | 114 | 105,448 |
| float pool only | 22 | 16,296 | 21 | 14,884 |
| string literal only | 11 | 6,352 | 11 | 6,352 |
| **TOTAL** | **331** | **275,716** | **164** | **145,600** |

**167 of 331 rows (50.5%) have zero canonical headroom**, and 163 of those are
pure register-save-helper rows. The gate is right to list them — it lists rather
than classifies, on purpose — but a lane costing the class must divide by 164,
not 331. Reproduce with `bin/objdiff-cli report generate -p . -o blind.json -c
functionRelocDiffs=none` and diff `match_percent_normalized` against
`build/373307D9/report.json`.

### 2. 11% of charged pairs are hoist ORDER, and for RTTI it is 100%

The gate pairs relocations **by instruction position**, which is the only pairing
a positional diff can offer. When MSVC hoists N `lis/addi` pairs to the top of a
block and schedules them differently, all N pairs charge even though both sides
reference **the same N symbols**. `scripts/analysis/reloc_order_vs_identity.py`
compares the two sides' relocation multiset restricted to the class the pair
straddles; equal multiset means ordering, not naming:

    reg_save_helper  432 pairs,   0 ORDER    rtti              10 pairs,  10 ORDER
    named_symbol     117 pairs,   0 ORDER    vtable             2 pairs,   2 ORDER
    float_pool        80 pairs,  32 ORDER    string_literal    37 pairs,  26 ORDER
    TOTAL            727 pairs,  83 ORDER (11.4%)

**Every RTTI charge in the population is scheduling.** `UIManager::GotoFirstScreen`
charges `??_R0?AVObject@Hmx@@@8` vs `??_R0?AVUIScreen@@@8` *and* the reverse — a
pair charged in both directions is the signature, and it is not a `dynamic_cast`
naming the wrong type. Conversely **0 of 117 named-symbol charges are ordering**:
when both sides of the pair are ordinary named symbols, the disagreement is real.

The tool ships a `--selftest` that was watched failing under two deliberate
sabotages (laundering everything as ORDER; dropping the class restriction) before
being trusted.

### 3. A relocation-name disagreement can be an *anchor* choice, at the same address

`?MemFindAddrHeap@@YAHPAX@Z` charges `?gHeaps@@3PAVMemHeap@@A` (target) vs
`?gNumHeaps@@3HA` (ours), filed `NEITHER_IN_MAP` because both are file-static and
the shipped map lists neither. It reads as "we read the wrong global". We do not.
From `config/373307D9/symbols.txt` — which resolves the statics `ham_xbox_r.map`
omits — `gHeaps` is `0x830E5458` and `gNumHeaps` is `0x830E56EC`, i.e. exactly
`gHeaps + 0x294`, and the target's asm is

```
lis  r11, "?gHeaps@@3PAVMemHeap@@A"@ha
addi r11, r11, "?gHeaps@@3PAVMemHeap@@A"@l
lwz  r7,  0x294(r11)          ; <- gNumHeaps, reached off gHeaps' anchor
```

MSVC materialised `&gHeaps` first and reached the loop bound by displacement
rather than emitting a second `@ha`. Both sides read the same word of memory. The
source is not wrong; the *anchor* differs, and no spelling of `i < gNumHeaps`
changes that on its own. **Adjudicate a `NEITHER_IN_MAP` / `BASE_NOT_IN_MAP` pair
against `config/373307D9/symbols.txt`, not only against the linker map** — the map
resolves neither name here, symbols.txt resolves 648 of the 727 pairs.

### 4. …and the tool now files that shape as `ANCHOR_DISPLACEMENT`, not `IDENTITY` (2026-09-11)

Section 3 was written as advice to a reader; the tool kept saying `IDENTITY`,
and two more rows of exactly that shape were handed to a lane as real
wrong-variable bugs the same day. Both were artifacts:

* `?MemPushTemp@@YAXXZ` — target `lis/addi r11, ?gNumHeaps@@3HA; lbz r10, -0x13(r11)`.
  `0x830E56EC − 0x13 = 0x830E56D9` = `gInitted`, internal linkage, present only
  as `lbl_830E56D9` in `symbols.txt` and never in the map. Ours anchors on
  `gInitted` directly. Control: `MemPushHeap` in the same object anchors the
  *other* way round.
* `?PreInitSystem@@…` / `?InitSystem@@…` — target anchors on `gUsingCD`
  (`0x82F652E8`) and reads `gSystemConfig` at `-0x8(r30)`; we anchor on
  `gSystemConfig`. The anchor choice survived reordering the declarations, reading
  the global directly, and binding a reference to it — it follows no source lever.

MSVC materialises **one** `lis/addi` anchor for a pair of same-section globals and
reaches the neighbour by a compile-time displacement on the load/store. The
relocation names only the anchor. A positional pair of two different names can
therefore address the same byte, and a name-only comparison cannot see it.

**Mechanism.** `reloc_name_gate.py --json-out` now records, per charged pair, the
displacements the anchor register's consumers apply on each side
(`target_disps` / `base_disps`; `anchor_displacements()` walks forward from the
charged `lis`/`addi` row until a call, a branch, a reload of the register, or 16
rows). `reloc_order_vs_identity.py` then, after the ORDER multiset test fails and
with at least one **non-zero** displacement in play, resolves `anchor ± disp` in
`config/373307D9/symbols.txt` (plus `ham_xbox_r.map` for named globals):

* the two sides reach a **common address** → `ANCHOR_DISPLACEMENT`
  (PreInitSystem: `gUsingCD − 8 == gSystemConfig + 0`);
* one side's name is unresolvable (a static dtk could only call `lbl_<addr>`) and
  the other side's displaced address lands **exactly on an `lbl_`** →
  `ANCHOR_DISPLACEMENT` (MemPushTemp);
* the displaced address resolves to a **named third symbol**, or to nothing, or
  every displacement is zero, or there is no address index → stays `IDENTITY`.

**Tightened 2026-09-13**, after `MakeBSPTree` was handed to a lane as a
wrong-variable bug and turned out to be this shape. Two things were wrong, and
the defect report named neither — both names resolve in `symbols.txt`, so the
"named third symbol" rule was never reached:

* `anchor_displacements` only recognised the completing `addi reg, reg, sym@l` —
  the **same** destination register. MSVC also emits `lis r11, s@ha ; addi r19,
  r11, s@l`, and the anchor then lives in r19: every consumer reads the new
  register, the walk fell through to *"something else wrote r11"* and stopped, so
  that side reported **no displacements at all** and the pair was rejected before
  any address was resolved. The walk now follows the move, gated on the `addi`
  naming the same symbol the `lis` did.
* the reach-set intersection did not include each **anchor's own address**. A side
  that emits `lis/addi X` has materialised `&X` whether or not the 16-row forward
  walk captured a `+0` consumer, so the test was a claim about which consumers the
  walk saw. `{at}` and `{ab}` are now unioned in, which is exactly *"the displaced
  address equals the other side's anchor address"* and nothing wider — the rule
  `PreInitSystem` was already getting for free from an incidental `0` in its
  displacement list. **Measured: 0 of the 7 live `ANCHOR_DISPLACEMENT` pairs on
  this tree depend on it**; it is a consistency fix so the two paths agree, not a
  reclassifier.

The verdict has its own column next to ORDER and IDENTITY; `--no-anchor` restores
the pre-2026-09-11 reading; `--anchor-out` dumps the reclassified rows with the
resolved address in each pair's `note`. The `--selftest` carries both fixtures
above (expect `ANCHOR_DISPLACEMENT`, in both directions), a `bl` pair and a
two-globals-at-`+0` pair (expect `IDENTITY`), a displacement onto a named third
symbol and onto nothing (expect `IDENTITY`), and no-index / no-data vacuity
controls; it exits non-zero on any failed expectation, and the four
`ANCHOR_DISPLACEMENT` expectations were watched failing before the filter existed.
The 2026-09-13 tightening added four more (25 checks total), three of them watched
failing first, plus a negative control — displacements that reach **neither**
anchor stay `IDENTITY` — and two in `reloc_name_gate --selftest` for the
register-move walk (21 there), one of which is a control that an `addi` naming a
*different* symbol must not hijack the walk. The `MakeBSPTree` fixture is driven
through `pair_displacements()` over verbatim instruction rows rather than
hand-written `target_disps`, because a hand-written `[4]` would have passed
against the broken walker.

**Sweep, this tree at `f7a9ae81e` (branch `w3-u2`, from `d3dcc6fcd`), objdiff
4.2.8, one full `ninja`** (255 standing rows / 202,924 B, 542 charged pairs).
Before = the filter as it stood on 2026-09-11; after = with the tightening:

| class | pairs | ORDER | ANCHOR_DISP before → after | IDENTITY before → after |
|---|---:|---:|---:|---:|
| `named_symbol` | 44 | 12 | 6 → **7** | 26 → **25** |
| every other class (13) | 498 | 68 | 0 → 0 | 430 → 430 |
| TOTAL | 542 | 80 | 6 → 7 | 456 → 455 |

**Exactly one pair moves: `MakeBSPTree`'s** `?gBSPDirTol@@3MA[4]` vs
`?gBSPMaxDepth@@3HA[0]`, *"both anchors reach 0x82F0F698"*. Row counts do **not**
move — 224 IDENTITY (186,824 B) / 6 ANCHOR_DISPLACEMENT (1,624 B) / 25 ORDER_ONLY
before and after — because `MakeBSPTree` carries a *second* charged pair
(`_List_base<BSPFace>::clear` vs `27088`, no relocation on our side) and the row
verdict is the strongest pair verdict. A row can be mostly artifact and still be
a lead; read the pair list, not the row.

The six `ANCHOR_DISPLACEMENT` **rows** are still exactly the MemMgr/System family:
`PreInitSystem`, `InitSystem` (`gUsingCD[0]` vs `gSystemConfig[8]`, both reach
`0x82F652E8`), `MemPushTemp`, `MemPopTemp`, `MemPopHeap` (`gNumHeaps − 0x13 =
lbl_830E56D9`, ours `gInitted` unresolvable) and `MemFindAddrHeap`
(`gHeaps[4, 660]` reaches `gNumHeaps`, the section-3 row). The same set comes out
whether the displacements are read from the gate's JSON or re-derived from the
diff (the fallback path for an older `rows.json`).

⚠ The population also **shrank** between 2026-09-11 and 2026-09-13 for reasons
unrelated to this filter (269 → 255 rows, 571 → 542 pairs): `rijndael_test`,
`fft_matrix_inverse_columnwise` and the `XinputJoypadThreadStart` pair left the
list when `w3-e2`'s config work landed. Re-derive before quoting.

Two things the filter deliberately does **not** take, both live rows:
`BlurShadowRT` (ours anchors the local static `kWeights` and reads `−4` —
`kWeights - 1` pointer arithmetic; the target's `0x82001214` is a named string
literal, so it is not the same byte) and `CharDebug::DisplayObject`, where our
side's "name" is the literal `328` — **no relocation at all** on our instruction —
and `target + 0x148` happens to be an `lbl_`. The first cut of the filter
laundered that one; a missing relocation is the loudest form of the bug, and it is
now guarded and sabotage-tested. That guard is right, and it is also the **next**
noise class: see the note under the table.

**The 21 surviving `named_symbol` IDENTITY rows (26,732 B)**, which are what a lane
should be looking at now — and **10 of them are one shape**. Five entries that a
previous lane refuted by hand are marked ✗ and are kept in the table on purpose,
so the next reader does not re-chase them:

| B | function | pair (target vs ours) |
|---:|---|---|
| 5072 | `MoveDir::UpdateOverlay` | `?ClosestMoveFrame@MoveDir@@` vs `?CurrentMoveMode@@` — and the reverse (both directions charged = order, but the multisets differ elsewhere) |
| 2228 | `Locale::Init` | `??$FastSort@$02@LocaleChunkSort@@` vs `?LocaleChunkSortFunc@@` |
| 2160 | `ArcDetector::UpdateOverlay` | `MakeString<float>` vs `MakeString<float,float>` |
| 1644 | `MakeBSPTree` | `_List_base<BSPFace>::clear` vs `27088` (no relocation on our side). ✗ **The second pair is gone** — `?gBSPDirTol@@3MA` vs `?gBSPMaxDepth@@3HA` is now `ANCHOR_DISPLACEMENT` (removed by the filter, 2026-09-13): they are 4 bytes apart at `0x82F0F694`/`0x82F0F698` and the target reads `0x4(r19)` off a `gBSPDirTol` anchor. Not a wrong variable. |
| 1584 | `RndShaderProgram::Cache` | `?MakeString@@YAPBDPBD@Z` vs `MakeString<const char*, u64, const char*, const char*, const char*>` |
| 1168 | `StartVoiceThreadEntry` | `?TheXboxSynth@@` vs `?gCommitTag@@`; `GetTickCount` vs `CriticalSection::Enter`. ✗ **Refuted by hand**: a 12-symbol hoist cascade — the two sides materialise the same pool of addresses in a different order, and the positional pairing charges the seam. Not a wrong callee and not a wrong global. |
| 656 | `XboxEnumeration::Poll` | `MakeString<unsigned>` vs `MakeString<ulong,ulong,ulong>`; and vs `TextStream::operator<<` |
| 648 | `CacheResource` | `?MovieExtension@@` vs `??1String@@` |
| 644 | `CharDebug::DisplayObject` | `?mesh@?8??DisplayObject…` vs `328` (no relocation on our side). ✗ **Refuted by hand**: a **liveness** difference, not a hardcoded constant — the target keeps the static's address live in `r31` and names it in the `@l`, we re-reach it as `0x148(r31)`. Same byte. |
| 588 | `FreestyleMoveRecorder::DrawDebug` | `lbl_82F0F2E0` vs `8`; `lbl_82F620A4` vs `-4` (no relocation on our side, twice). ✗ **Refuted by hand, and the folded side is the opposite of what this table used to imply: OURS is the folded one.** The target emits `lwz r11, lbl_82F620A4@l(r30)` — a separate `lis`/`addi` naming each global — while we reach the neighbour by a plain `-0x4(r30)`. The mirror of `MakeBSPTree`. |
| 104 | `operator<<(BinStream&, vector<TransformCrowd>)` | `TransformCrowd::Save` vs `operator<<(…ObjRefConcrete<WorldCrowd>…)` |
| 3100, 1524, 1388, 732, 704, 660, 592, 536, 508, 492 (**10,236 B**) | `RndParticleSys::Load`, `CharEyes::Load`, `CharIKHand::Load`, `HamNavList::PreLoad`, `SkeletonViz::PreLoad`, `RhythmBattlePlayer::Load`, `HamDirector::Load`, `RndSpline::Load`, `RndAmbientOcclusion::Load`, `CharIKFoot::Load` | `?TheDebug@@3VDebug@@A` vs `gRev` (`gAltRev` once) — **ten rows, one shape** (`RndParticleSys::Load` joined since 2026-09-11): a hoist-order swap the multiset test cannot forgive because the target's copy of our file-static `gRev` is an unnamed `lbl_` (e.g. `lbl_820108F8`), so the two sides' symbol sets differ by a *name*, not a variable. ✗ **Refuted by hand**: it is MSVC's **CSE anchor pick** for the `ASSERT_REVS` block, and it follows no source lever — **613 of the 645 sibling `Load`s are already at 100% with our exact spelling**, so the only change that reaches all ten breaks those 613. Not a wrong variable; do not "fix" it. |

**The next noise class, and why the filter cannot take it yet.** Both
`FreestyleMoveRecorder::DrawDebug` and `CharDebug::DisplayObject` are the
`MakeBSPTree` shape with the sides swapped: **we** fold, the target names. The
filter refuses them because our instruction carries no relocation at all, and that
guard has to stay — a missing relocation really is the loudest form of the bug.
Closing them needs a different input: resolve **our** base register back to the
`lis`/`addi` that defined it (a different instruction from the charged row) and
compare that anchor's address plus the literal displacement. Nothing does that
today.

Reproduce: `python3 scripts/analysis/reloc_name_gate.py --project . --json-out rows.json --limit 0`
then `python3 scripts/analysis/reloc_order_vs_identity.py --rows rows.json --project . --list-identity --anchor-out anchors.json`.

### Four experiments that made things WORSE — do not retry blind

Every one of these "fixed" the charged constant and cost more in code shape than
the name was worth. Canonical (`name_check`, `report.json` ruler), whole-`ninja`
rebuild each time:

| function | change | before | after |
|---|---|---:|---:|
| `?ScreenRect@HiResScreen@@…` | `1.0 / (float)tiling` → `1.0f / …` (target's constant) | 80.833 | **77.500** |
| `?CalculateAOAtPoint@RndAmbientOcclusion@@…` | `val * 0.5f + 0.5f` → `* 0.5 + 0.5` (target's double) | 89.897 | **88.900** |
| `?SetupPanInfo@MoggClip@@…` | `-f2 / 2.0` → `-f2 * 0.5f` (target emits `fmadds` with `-0.5f`) | 92.742 | **78.387** |
| `?Relativize@CharBonesSamples@@…` | `1300.0 / 32767.0f` → `1300.0f / …`, matching its two siblings | 97.136 | **94.212** |
| `?MemFindAddrHeap@@…` | index loop → pointer-increment loop (the file's own idiom, and the target's shape) | 87.292 | **69.000** |

The literal's *type* is an instruction-selection input on Xenon MSVC (`frsp`,
`fdivs` vs `fmuls`, `fmadds` vs `fsubs` under `/fp:fast`), so the "wrong" spelling
is frequently the one the rest of the function's shape depends on. Charging the
constant is a lead about **what the original computed**, not an instruction to
retype the literal.

### What did work

Three rows, all adjudicated from the target asm rather than from the name:

* `?HolmesClientCacheFile@@YA_NPADPBD@Z` — `AutoSlowFrame`'s threshold is
  `20000.0f`, we had `25.0f`. A real shipped-behaviour bug. 93.595 → **93.681**.
* `?BuildCone@Spotlight@@IAAXAAUBeamDef@1@@Z` — `0.4188790f` (0x3ed6774f) is one
  ULP off the correctly-rounded `2.0f*PI/15.0f` (0x3ed67750) the target holds, and
  the line above already reads `1.0f / 15.0f`. 89.716 → **89.751**.
* `?Poll@CharSleeve@@UAAXXZ` — `-3.858268f` → `-3.8582677f` (9.8/2.54). Byte-correct
  constant, **metric-neutral**: that row's charge was a 1.0f-vs-gravity hoist swap
  all along, so it is 93.998 canonical before and after. Recorded so nobody
  re-derives the ULP as a lead.

Whole-binary A/B over all three: `matched_functions` 29,885 → 29,885,
`matched_code` 5,048,168 → 5,048,168 B, **2 functions up, 0 down**. Both winning
rows are far from 100%, so neither crosses and neither pays bytes — expected for
this class, and the reason to report functions-up/down alongside the headline.

## Bare vs mangled statics: a linkage claim, not a spelling

*(dc3-decomp, lane `w3-t`, 2026-09-11, worktree at `85d2ca315`.)*

MSVC/Xenon names a file-scope `static` datum with its **bare** identifier in
the COFF symbol table (storage class 3: `gSystemConfig`, `sCompressDone`), and
a datum with external linkage with the global-scope mangling (storage class 2:
`?gNumHeaps@@3HA`). The dtk-split target objects take every name from
`config/373307D9/symbols.txt`, so when that file says `?gRndThread@@3PAXA` and
`src/system/rndobj/Rnd.cpp` says `static HANDLE gRndThread`, every access is a
charged `name_check` row for a variable whose section and offset are right —
`Rnd::Init` (99.74) and `Rnd::Terminate` (99.67) had nothing else left, and
`run_objdiff` filed the four rows as `ADDRESS_RELOCATION_NOISE … no source
mutation can close them`, which was wrong on both counts.

**Measured, two independent ways.** Over the 980 paired units, by COFF symbol
table: **786** bare statics on our side, **370** `?x@@3` globals (plain
identifier) on the target side, **2** bare-vs-mangled disagreements, both in
`Rnd.obj`, reaching **3 functions / 9 relocation sites**; **0** in the mirror
direction (ours mangled, target bare). Of the 786, 554 are not defined in the
target object at all and `symbols.txt` spells them bare, 146 have no
`symbols.txt` entry, 84 the target also spells bare. The `reloc_gate.json` this
lane was handed (2026-09-10 23:29) lists the same two functions but with `12`
in the `base` column — it predates `186b0038d`, which reconstructed the
`Rnd.cpp` access pattern, so it describes an older source shape; on the current
tree objdiff's JSON renders our side as `gRndThread` at all four sites. **Count
this class from the objects, not from a gate run you did not make yourself.**
The gate's other bare-looking rows are not this class: `gUsingCD` vs `gSystemConfig`
(`PreInitSystem`/`InitSystem`) and `?gNumHeaps@@3HA` vs `gInitted`
(`MemPopHeap`/`MemPopTemp`/`MemPushTemp`) are our code reading a *different*
variable than the target — real leads, wrong bucket.

**The linker map lists no static data at all** (`ham_xbox_r.map`'s "Static
symbols" section is 25,565 rows, every one a function), so for a static the
`symbols.txt` spelling is hand-authored, and **114 of the 478 `?x@@3` object
entries in `symbols.txt` are absent from the map**. Those 114 are the population
at risk: each is a linkage claim the map cannot confirm, and it holds only while
our source happens to declare the variable non-`static`. The two `Rnd` entries
had in fact been corrected to bare upstream in `3dbdf45a3` (2026-03-13); the
merge `5660129ad`/`5ecac7b59` resolved the conflict back to the mangled form.

**Mechanism chosen: a check, not a seventh patcher.** A rename pass over our
object (the brief's preferred design) would close the rows and hide exactly the
two defects that produce them: a `symbols.txt` entry the map cannot confirm
(fix: spell it bare), or a `static` the original did not have (the map carries
the mangled name; fix: drop the `static`, which then matches storage class and
all). `scripts/verify_data_symbol_spelling.py --check` runs in `post-compile`
before the manifest is emitted, so an unfixed tree is never vouched for and
`run_objdiff` refuses it; its message names which of the two fixes applies,
from the map. Its target-side enumeration is done twice — COFF symbol table and
dtk's own `.s` `.obj` directives — and a disagreement is exit 4, a refusal,
never a "clean" over a subset. `--selftest` plants seven defects that must go
red (both COFF name encodings, both directions, an undefined-external target,
both cross-check directions) and exits 5 if none did; `tests/test_data_symbol_spelling.py`
pins each reason, and `tests/sabotage_objs_patched.py` M26–M31 edit the script
and require those cases to fail.

**Whole-binary delta**, same worktree, full `ninja` before and after the
`symbols.txt` fix: `matched_functions` **30,746 → 30,748**, `matched_code`
**5,293,956 → 5,294,444 B**; 3 functions up (`Rnd::Init` → 100.0,
`Rnd::Terminate` → 100.0, `Rnd::DrawPreClear` 99.43 → 99.48), **0 down**.
`PreInitSystem` is unchanged at 97.5, as expected — it is not this class.

Expected side effect of any `symbols.txt` edit, not a regression: the split
rewrites the target objects, so the next `pattern_census.py` run will read
every unit as target-stale against its recorded object baseline
(`pattern_scan_units`); re-derive rather than trust the old scan.
