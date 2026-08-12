# The anonymous-namespace hash lane: one hash per FILE, assigned per SYMBOL

2026-08-12. `anon_namespace_hash` was the second-biggest `name_check` lane on
dc3 — **714 charged sites across 13 objects**. It is now **26 sites across 3**,
and the 26 that remain are a different problem wearing the same name.

Measured deltas, whole tree, dc3 `373307D9`:

| ruler | before | after | delta |
|---|---|---|---|
| `none` | 43.730614% / 4,973,668 B | 43.730507% / 4,973,656 B | **−12 B, −1 fn** (a false pairing collapsing — see below) |
| `name_check` | 41.731250% / 4,746,272 B | 41.935516% / 4,769,504 B | **+23,232 B, +132 fns, 0 lost** |

Per object, at `name_check`:

| unit | +fns | matched_code |
|---|--:|---|
| `lazer/meta_ham/MetagameRank` | +59 | 4,932 → 13,880 |
| `system/os/HolmesClient` | +42 | 1,948 → 8,360 |
| `system/os/Joypad` | +23 | 3,132 → 8,000 |
| `system/gesture/DrawUtl` | +3 | 888 → 1,456 |
| `system/char/CharBonesSamples` | +1 | 4,440 → 5,096 |
| `system/os/DateTime` | +1 | 4,880 → 5,724 |
| `system/gesture/DepthBuffer3D` | +1 | 6,636 → 6,880 |
| `system/rndobj/Part` | +1 | 27,424 → 27,740 |
| `system/rndobj/Mat` | +1 | 14,384 → 14,760 |

## The `none` ruler moved, and it moved for the right reason

`none` must not move. It did: **−12 bytes, one function lost**,
`??__FgInput@?A0x49b544a7@@YAXXZ` in `system/os/HolmesClient`. Chased to the
end, because a `none` regression is a stop-and-find-out condition.

It is a false pairing collapsing, and the evidence is on the other ruler:

| | `none` before | `none` after | `name_check` before | `name_check` after |
|---|---|---|---|---|
| `??__FgCrit` | 100 | 100 | 96.67 | **100** |
| `??__FgHolmesTarget` | 100 | 100 | 96.67 | **100** |
| `??__FgRequests` | 100 | 100 | 95.0 | **100** |
| `??__FgServerName` | 100 | 100 | 96.67 | **100** |
| `??__FgInput` | 100 | **none** | 95.0 | none |

`??__FgInput` was **never a match**. We do not emit it — retail registers a
destructor for `gInput` and we do not, so retail's 12-byte atexit thunk had no
counterpart in our object. objdiff had paired it against a *different* thunk of
ours, and at `functionRelocDiffs=none` that scores 100, because a 12-byte
atexit thunk's only distinguishing content is the relocation naming the object
it destroys — precisely the blindness `docs/reloc-name-blindness.md` measures.
`name_check` never believed it: 95.0 before, and unmatched after.

The chain of causation is worth recording, because it is a *downstream* gain
this lane unlocked rather than caused directly. With HolmesClient's hash
corrected, `obj_atexit_scope_patcher.py` could for the first time pair our
`??__F_dw@?5??EndCmd@?A0x49b544a7@@…` with retail's `?4??` spelling and renamed
it. That consumed the thunk objdiff had been lending to `??__FgInput`, and the
spurious 100 evaporated. Measured in isolation — this pass applied, the rest of
the chain not re-run — `none` is **unchanged at 43.730614% with 0 functions
lost** and `name_check` is +131; the −12 B and the 132nd function both arrive
with the atexit rename.

Keeping it is the correct disposition: the alternative is preserving a 100 on a
function we do not have. The real finding underneath is a source gap in
`HolmesClient` — retail's `gInput` has a registered destructor and ours does
not.

## What was actually wrong

`scripts/obj_anon_ns_patcher.py` has run as a post-compile ninja step for
months and reported "Would apply patches to 0 files". It was not idle — it had
**given up on exactly the 13 objects that carried all 714 charges**, and
counted them as `Skipped (ambiguous/multiple hashes)`.

Its rule was *one hash of ours becomes one hash of retail's*. It bailed
whenever retail's object held two or more hashes it could not pair, and its
tie-breaker for the near-miss case ("retail has N, one of them is a common
header hash, so patch to the unique remainder") was **actively wrong** on five
more objects.

The reason retail's objects hold several hashes is the mechanism itself. The
hash keys on the CANONICAL PATH of the file the anonymous namespace is
*declared in*, so a `namespace {}` in a HEADER gets one hash shared by every TU
that includes it, while a TU-local one gets the `.cpp`'s. Measured across
retail's whole tree:

- 78 distinct hashes, **71 in exactly one object**, 7 spanning several;
- every multi-object hash carries the **same entity everywhere it appears** —
  `c9fefd64` is `AddToStrings` in **55** objects, `b39b74bf` is `DebugGraph` in
  13, `81ddebd1` is `CuePoint`/`Label` in 5, `f8e4b4b5` is `Unlockable` in 4,
  `53f5bb0a` is `MonthToken`'s `month_symbols` in 2;
- **19 retail objects therefore carry two or three hashes at once.**

We declare those entities in the `.cpp`, so one hash of ours has to become
several of retail's. That is an **assignment problem**, not a missing
mechanism.

## The assignment rule, and why it is evidence and not a heuristic

**The mangled name itself.** Blank the hashes out of the NUL-delimited name a
hash sits in, and ask retail's object what belongs in those positions.

    ?AddToStrings@?A0x0e0dd0d2@@YA_NPBDAAV?$list@VString@@...   (ours)
    ?AddToStrings@?A0x*       @@YA_NPBDAAV?$list@VString@@...   (template)
    ?AddToStrings@?A0xc9fefd64@@YA_NPBDAAV?$list@VString@@...   (retail says)

Over the whole retail tree that is **543 distinct templates and ZERO map to two
different hash tuples**. Where the name matches, the answer is retail's, stated
outright. It is positional, so a name carrying two hashes from two different
files resolves both correctly — `DepthBuffer3D.obj` splits our single
`0e0dd0d2` into `a87adb66`, `8ccc14d7` and `c9fefd64` on three different
symbols, all by exact template match.

Fallbacks, in order, with their firing counts on the current tree:

| rule | count | what it is |
|---|--:|---|
| `template` | 756 | exact name in the paired retail object |
| `template_stripped` | 333 | same, under a stripped `__ehfuncinfo$`/`__unwindtable$` decoration |
| `token` | 599 | the identifier immediately before `?A0x`, from the paired object |
| `token_global` | 41 | that identifier, from anywhere in the retail tree |
| `template_global` | 19 | exact name anywhere in the retail tree |
| `template_ordinal` | 1 | exact name with the lexical-scope ordinal blanked |
| `majority` | 522 | the object's dominant target hash |

(Counts on the settled tree. Before `obj_atexit_scope_patcher` re-fired behind
this pass they were `template` 755 / `template_ordinal` 2: its `?5??`→`?4??`
rename on HolmesClient's `??__F_dw@…EndCmd` turned one ordinal-blanked match
into an exact one.)

Only the `template*` rules are retail stating the answer. The rest exist for
symbols we emit and retail never did — STL instantiations it inlined, EH tables
it did not need — which cannot match a retail name whatever we write; the
fallback keeps the object internally consistent, it does not buy a match.
`majority` is large only because `MetagameRank.obj` alone contributes 460 STL
instantiation names that retail's object does not have.

Two details that matter:

- **Token matching is a fallback because it IS ambiguous**, in exactly one
  place: retail's `MetagameRank.obj` spells `Unlockable` under both `9d17dd81`
  and `f8e4b4b5`. The template rule separates those correctly (10 occurrences
  go to `9d17dd81`, 892 to `f8e4b4b5`).
- **The lexical-scope ordinal is a different lane and must not hide the hash.**
  `?month_symbols@?6??MonthToken@?A0x…@@` (ours) vs
  `?month_symbols@?1??MonthToken@?A0x53f5bb0a@@` (retail) disagree on the
  ordinal *and* the hash; blanking the ordinal lets the hash be read off
  correctly, and leaves the ordinal to `local_static_scope_ordinal`.

Verified per object: no duplicate symbol names are created, and the pass is a
fixed point after one apply (rule 1 then maps our names to themselves), which
`scripts/verify_objs_patched.py` requires of every patcher in the chain.

**A fallback can never manufacture a match**, and this is checkable rather than
argued. If a fallback-assigned name coincided with a retail name, the template
lookup would have found that retail name first and it would not have been a
fallback — so the two rule classes are disjoint by construction. Measured on
the patched tree: **542 anonymous-namespace symbols are now name-identical to
retail's, and all 542 were reached by a `template*` rule. Zero by `token`,
`token_global` or `majority`.** The 522 `majority` assignments buy no match and
were never going to; they keep an object's own references consistent.

## Why this is a post-build rewrite, and why the honest alternative is blocked

No source edit can produce these values. The hash encodes the build machine's
computer name and its canonical paths — the same category of build-environment
input as the `WIBO_COMPUTER_NAME='9QVZU3'` already pinned in
`tools/project.py`. The number this pass moves should be read as *our
instruction stream and its relocation targets agree with retail once the build
host's identity is normalised away*, not as *our source now compiles to this*.

Making cl emit the values for real is strictly better, and it is **blocked
upstream, not merely unattempted**. `docs/plans/ANON_NAMESPACE_HASH_FIX.md`
(2026-02-26) already reverse-engineered the algorithm out of `SigForPbCb` in
`mspdb80.dll` — a CRC-32 chain over the computer name and the normalised path
— and shipped the computer-name half. What it also found, with
`WIBO_SIGFORPBCB_LOG` instrumentation, is the blocker: **under wibo, cl assigns
one hash per translation unit, always from the `.cpp`'s path, never the
declaring header's.** `/Z7`, `/Zi`, `GetShortPathNameW` variants and
de-`inline`ing all failed to move it, and an exhaustive search of 1,338 header
names × 25 directory prefixes × 6 formats produced 0 matches.

That negative bounds the wibo route precisely: it can reach the 71 TU-local
hashes and it can **never** reach the 7 header hashes, whatever the path map
says. Since the header hashes are the whole reason an object needs a split, the
wibo route could not have closed this lane on its own.

Re-probed 2026-08-12 with a live cl oracle
(`scripts/probe_anon_hash.py`, ~0.1 s/compile) and `WIBO_SIGFORPBCB_LOG`. Three
things came back that the doc did not have:

- **The chain's third buffer is a one-byte ORDINAL, not the `"\x00"`
  terminator the doc records.** It is 0 without a PCH and 1 under `/Yu`, for
  any PCH regardless of how many anonymous namespaces the PCH contains — a
  file-table index with the PCH in slot 0, not a namespace counter (three
  anonymous namespaces in one non-PCH TU all share ordinal 0). The doc's
  exhaustive header search swept no such dimension. A re-run that does (5.2M
  paths, ~21M hashes, ordinals 0–3, positive control passing) is also **0
  hits**, so the conclusion survives — but the original evidence for it did
  not cover the space it claimed to.
- **The header path is never hashed at all.** The doc inferred a
  canonicalisation mismatch; the log shows the call is simply not made. Two of
  its proposed mechanisms are also dead: `GetShortPathNameW` *is* called on the
  header's directory (no hash follows), and cl imports
  `GetFileInformationByHandle` without ever calling it (1 IAT line, 0 calls in
  a 59k-line trace).
- **The model is now runnable and validated against retail.**
  `scripts/anon_ns_hash.py --self-test`. `9QVZU3` is confirmed correct — the
  brief's "known wrong for at least `MetagameRank.cpp`" was a misreading, since
  `MetagameRank`'s retail hash is header-sourced and so is not evidence about
  the computer name at all.

### The part that is *not* blocked: the ordinal is why our TU-local hashes miss

Retail's `HolmesClient` hash and ours are the **same path and the same computer
name, differing only in the PCH byte**:

    predict(e:\lazer_build_gmc1\system\src\os, HolmesClient.cpp, ordinal 0) = 49b544a7   retail
    predict(                     … same …    , HolmesClient.cpp, ordinal 1) = 3eb27431   ours

`system/gesture/DrawUtl.cpp` is the same story (`da23fae1` at 1, retail's
`ad24ca77` at 0), and `MetagameRank.cpp` — in `lazer/meta_ham`, which is not in
`config.pch_eligible_dirs` — comes out at ordinal 0 as predicted.

**Verified end to end, not just modelled.** Compiling `HolmesClient.cpp` with
`/FI"decomp_pch.h"` but without `/Yu`, into a scratch `/Fo`, makes cl emit
`?A0x49b544a7` — retail's value, from the compiler. So the plan doc's "8 .cpp
files match" is a floor imposed by the PCH, not a ceiling, and part of this
lane *is* reachable at compile time after all.

Two things bound it, and neither is settled here:

- **It generalises much less far than it looks.** Sweeping all 256 ordinals
  against every retail object's own `.cpp` path reproduces **8 of 123**.
  `Joypad.obj`'s `ca10770b` carries 25 unmistakably Joypad.cpp-local symbols
  and no ordinal predicts it, while `HolmesClient.cpp` *in the same directory*
  is exact. So for most TUs the hashed string is not
  `<name> + <path> + <one byte>` — the third buffer may not always be one byte,
  or the chain may be longer. A failed `predict()` is therefore not evidence
  about a path.
- **The codegen cost is unmeasured.** The no-`/Yu` object is not
  section-identical to the PCH one (extra COMDATs, different `.debug$S`), so
  whether `none` holds under a per-TU PCH opt-out needs its own measurement and
  its own lane. It would also buy no metric on top of this pass, which already
  produces `49b544a7`; what it buys is that the value stops being a post-build
  rewrite. Worth doing for that reason, not for the number.

> **Doc conflict, resolved by the older measurement.** The 2026-08-12
> `namecheck-lane-triage-and-fixers` manifest calls this lane "reachable — a
> reverse `WIBO_PATH_MAP` in wibo's host→Windows conversion, plus the right
> computer name". That is optimistic: it re-derived the *inputs* to the hash
> without knowing the 2026-02-26 finding that wibo's cl only ever hashes the
> `.cpp` path. Read the two together, not the newer one alone.

## The source-structure finding underneath

That our tree needs the split at all is a fact about our sources, and the
patcher masks it. Retail declared these in **headers** and we did not:

`AddToStrings` (`c9fefd64`, 55 objects) · `DebugGraph` (`b39b74bf`, 13) ·
`CuePoint`/`Label` (`81ddebd1`, 5) · `Unlockable` (`f8e4b4b5`, 4) ·
`MonthToken`/`month_symbols` (`53f5bb0a`, 2) · HolmesClient's
`gMachineName`/`gServerName`/`gShareName` (`bd0b8fef`, 1).

Moving those declarations into the headers they belong in would make the header
hashes fall out of the compiler for free *if* the wibo per-TU limitation were
also lifted. Both halves are needed; neither is done here.

## The three sub-problems the lane was split into, and what each turned out to be

**1. Ambiguous assignment — 688 of the 714 sites.** Solved, above. All 132
recovered functions come from here.

**2. `original has no anon ns` — 5 objects.** Anticipated as a real source
difference (we wrapped in an anonymous namespace where retail used file-scope
`static`), with the measured warning from an earlier lane that widening
file-scope linkage *costs bytes* — four of seven files there lost 8 complete
functions at `none`.

**It is worth zero sites, and no edit was made.** Four of the five —
`gesture/SkeletonClip`, `hamobj/SongCollision`, `synth/Sequence`,
`synth_xbox/FxSendReverb` — carry **no `anon_namespace_hash` charge at all**;
they have anonymous-namespace symbols that nothing charged relocation names
against. The fifth, `rnddx9/Rnd`, is not a "no anonymous namespace" case at all
— see below. So the risky linkage-widening edit this sub-problem called for
buys nothing and was not attempted.

**3. `link_glue.obj`.** No retail counterpart exists. Out of scope; unchanged.

## What remains: `?A@@`, a second MSVC spelling

The 26 residual sites are three objects where **retail spells the anonymous
namespace with no hash at all**:

| unit | sites | ours | retail |
|---|--:|---|---|
| `system/os/Joypad_Xbox` | 11 | `?sThreadData@?A0x439b694a@@3U<unnamed-type-sThreadData>@1@A` | `?sThreadData@?A@@3U<unnamed-type-sThreadData>@1@A` |
| `system/rnddx9/Rnd` | 9 | `?sDepthRectVerts@?A0xc0d8487b@@3PAUDepthRectVert@1@A` | `?sDepthRectVerts@?A@@3PAUDepthRectVert@1@A` |
| `system/os/Joypad_Xinput` | 6 | `?gXboxDeadzone@?A0xf503845b@@3MA` | `?gXboxDeadzone@?A@@3MA` |

`?A@@` is still an anonymous namespace — all three of our sources already use
`namespace { … }`, so this is not a source-shape difference in the obvious
sense.

**And it is per SYMBOL, not per TU, per file or per namespace block.** That is
the constraint any explanation has to satisfy. `Joypad_Xinput.cpp` declares
four things in ONE anonymous namespace:

```cpp
namespace {
    XINPUT_CAPABILITIES gCaps[kNumJoypads];
    float gXboxDeadzone;
    bool gCapsValid[kNumJoypads];
    CriticalSection gCritSection;
}
```

and retail's object spells that one namespace two ways: `?gCaps@?A0xf503845b@@`,
`?gCapsValid@?A0xf503845b@@` and the two `gCritSection` init/term thunks under
the hash, `?gXboxDeadzone@?A@@3MA` without it. `Joypad_Xbox.obj` is the same
shape — six hashed names, one hashless. Only `rnddx9/Rnd.obj` is uniformly
hashless, and it has just the one anonymous-namespace symbol.

So a compiler flag, a different cl revision, `/TC` vs `/TP`, a nested
namespace, header-vs-cpp placement and an unavailable path are all ruled out:
none of them can split one block per member. What the three hashless symbols
have in common is that all three are data and none is a function — but `gCaps`
and `gCapsValid` are data in the same block and get the hash, so that is not
the discriminator either.

Probed, and it is a clean negative. Not reproducible in **~65 variants**: 45
flag sets (`/Za /Zl /Z7 /Zi /GS- /Gy /GF- /Gz /Gr /Ox /Od /O2 /Os /Oy /Ob0
/Ob2 /J /vmg /vms /vmv /vmb /Zp1 /Zp16 /GX /EHa /GR- /Yd /Gh /GH /Qpar /openmp
/Gm /MT /MD /LD /Zc:*` …) and 22 source shapes, every one of which produced a
hash. Specifically ruled out by measurement:

- **File-scope `static` is not it, which kills the one-word source fix.** MSVC
  emits a file-scope `static` — and a `static` *inside* an anonymous namespace
  — with a **plain undecorated name**, never `?A@@`. Retail's source cannot
  have written `static float gXboxDeadzone;`.
- **Not the datum's shape.** In one anonymous namespace, all 13 of these got
  the hash, including `sDepthRectVerts`' exact shape (brace-initialised array
  of a locally-declared struct) and `sThreadData`' exact shape (unnamed-struct-
  typed object), plus bare/initialised/const/volatile scalars and an object
  with a dynamic initialiser. Our cl also spells unnamed types
  `U<unnamed-type-X>@1@` exactly as retail does, so a different cl revision is
  out too.
- **Not "the compiler had no file".** `#line 1 ""`, `#line` to a fake path,
  `#line` inside the block, `/FI` forced include, `extern "C++"` wrapping and
  `namespace{namespace{}}` all hashed.
- **Nothing distinguishes them in the object either.** Reading the 18-byte COFF
  symbol records for a hashless and a hashed symbol in the *same* retail
  object: `?gXboxDeadzone@?A@@3MA` and `?gCaps@?A0xf503845b@@…` are
  **identical in every field** — both `IMAGE_SYM_CLASS_EXTERNAL`, both in
  their own COMDAT section with characteristics `0xc0401040`, consecutive
  section numbers, no aux records. Same for `?sThreadData@?A@@` against
  `?tBreed@?A0x439b694a@@` (`0xc0401040`, sections 71 and 72). The prediction
  that the hashless ones would be `STATIC` or non-COMDAT is **refuted**.
- **Not declaration order.** `Joypad_Xbox.cpp` declares `tBreed` *first* and
  `sThreadData` second; retail hashes `tBreed` and not `sThreadData`.

**Cause unknown, and the search space is now small.** The split is visible only
in the name string, matches nothing source-level, nothing flag-level and
nothing object-level — which points at two different name-emission paths inside
cl (front end vs back end) taking different decisions about whether the file
signature is available yet, in a real Windows build we cannot reproduce under
wibo. That is a hypothesis, not a finding.

**This pass cannot reach it**, and the reason is structural rather than a
missing rule: every rewrite it makes is 8 hex characters over 8 hex characters,
so nothing in the object moves. `?A0x<h>@@` is 12 bytes and `?A@@` is 4. A
length-changing rewrite is *tractable* — all 18 occurrences across the three
objects live in the COFF string table, which is the last thing in each file, so
the strings could be reflowed and the symbol records repointed without moving
any section — but it is new machinery in a load-bearing chain for 5 functions,
and a source or flag answer would be strictly better. Left open.
