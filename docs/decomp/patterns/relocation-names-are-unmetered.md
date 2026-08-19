# Relocation names are unmetered: a function can call the wrong callee at 100%

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
100% under functionRelocDiffs=none   AND   <100% under the graded name_check ruler
```

Both legs load `build/373307D9/icf_aliases.map`, so folds the project has
already adjudicated never enter the population.

## The population, re-derived

| | rows | bytes |
|---|---|---|
| functions scored | 48,344 | — |
| 100% blind, <100% graded (incl. `fn_`/`lbl_` funclets) | 186 | 32,388 |
| …of which `fn_`/`lbl_` MSVC EH funclets | 113 | 4,060 |
| **named functions** | **73** | **28,328** |

An earlier scoping run quoted **198 / 37,496**; that was the same measurement on
a slightly older tree and it did not separate the funclets. **113 of the 186 are
funclets**, so the actionable population is roughly a third of the headline. If
you want one number for this class, use the named one.

A *wider* set — `match_percent_normalized == 100` with graded `fuzzy < 100` — is
384 rows / 145,968 B, but that set is contaminated with register permutations,
which normalization also forgives. Do not use it for this class.

**Every one of the 186 had `other_charges == 0`**: the relocation-name charge was
the *only* thing wrong with the row. That is what makes the class worth a lane —
closing the name closes the row.

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

## What is left (24 standing rows, 7,372 B)

* **`__FILE__` header-provenance, 11 rows ≈1,100 B.** Ours records a bare
  `.cpp` basename where the target records a header — our class declaration or
  its `MEM_OVERLOAD` lives in the wrong file (`CharBones`, `AllocInfo`,
  `FlowSlider`, `FlowSetProperty`) — or both record headers but different ones
  (`FileStream`, `MultiMesh`→`utl/PoolAlloc.h`, `CriticalSection`,
  `UsbMidiGuitar`→`os\UsbMidiGuitar.h`). Real and actionable, but each is a
  header-layout change. `MultiMesh`'s is **PCH-pinned**: `PoolAlloc.h` is in
  `decomp_pch.h`, so no per-TU include can change its spelling.
* **`??_B` vs `?$Sn@` local-static guard spelling, 5 rows ≈2,950 B.** The target
  emits a bit-packed guard (`??_B<scope>@<fn>@5<bit>@`); we emit a per-static
  `unsigned int` guard (`?$Sn@<scope>@<fn>@4IA`). No source lever found —
  probable compiler-mode floor.
* **`rijndael_test`, 1,608 B.** `?key192@…@4QBEB` (const `unsigned char[]`) vs a
  const `unsigned int[]` at `820c4fe0` — read-only COMDAT data fold, benign.
* **`ThreeDSound::Load`, 800 B.** Config artifact, above.
* **`SampleInst360` 276 B / `~AppLabel` 108 B.** Our callee is absent from the
  image and the target's is a known fold winner (`DrawHighlightMat` ≡
  `Sound::Sample` @8261d060; `~HamLabel` @82517e00). Probable folds the alias
  map does not cover because our spelling is not a target symbol.
* **`NgPostProc::CheckHueConverge`, 180 B — REAL, unresolved.** The target calls
  the 75-member `merged_Returns1` group at `82E2AB00`, i.e. a predicate whose
  body is `li r3,1; blr`. Our `RndPostProc::ColorXfmEnabled()` has a real body,
  and the target's own `?ColorXfmEnabled@RndPostProc@@QBA_NXZ` sits separately at
  `8266b0a8`. So the target's predicate is a different, trivially-true one — and
  none of the 75 fold members is a `PostProc` method, so its name is not
  recoverable from the map.

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

`--selftest` is the negative control: it replays the recorded `createFilter`
charge through the same adjudication path and asserts it is **not** exempted,
plus five more must-not-swallow cases. Two of those exist because the first
version of the scope-counter exemption was wrong — its regex matched only the
letter-run encoding (`?HP@??Foo`) and silently missed the bare-digit one
(`?9??Foo`), and it has to *not* fire when the variable differs rather than the
counter, or it would have swallowed `ThreeDSound::Load`.

End-to-end control: re-declare `createFilter` `extern "C"` in
`src/system/synth/EQEffect.cpp`, rebuild, re-run; the row must appear with

```
?createFilter@@YAXW4FilterType@@MMMMPAUFilterCoeff@@@Z   vs   createFilter
```

## See also

* [rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md) — the earlier,
  narrower version of this lesson (rounding, not relocation).
* `CLAUDE.md` → Known Patterns, first bullet.
