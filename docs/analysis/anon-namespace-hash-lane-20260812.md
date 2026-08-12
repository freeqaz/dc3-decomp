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
| `template` | 755 | exact name in the paired retail object |
| `template_stripped` | 333 | same, under a stripped `__ehfuncinfo$`/`__unwindtable$` decoration |
| `token` | 599 | the identifier immediately before `?A0x`, from the paired object |
| `token_global` | 41 | that identifier, from anywhere in the retail tree |
| `template_global` | 19 | exact name anywhere in the retail tree |
| `template_ordinal` | 2 | exact name with the lexical-scope ordinal blanked |
| `majority` | 522 | the object's dominant target hash |

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
`namespace { … }`, so this is not a source-shape difference. Something about
how retail compiled these three TUs made cl omit the signature. A plausible
mechanism, untested here, is that the hash comes from `mspdb80.dll` at all:
a TU compiled in a configuration where `SigForPbCb` is unavailable has no
signature to emit.

**This pass cannot reach it**, and the reason is structural rather than a
missing rule: every rewrite it makes is 8 hex characters over 8 hex characters,
so nothing in the object moves. `?A0x<h>@@` is 12 bytes and `?A@@` is 4. A
length-changing rewrite is *tractable* — all 18 occurrences across the three
objects live in the COFF string table, which is the last thing in each file, so
the strings could be reflowed and the symbol records repointed without moving
any section — but it is new machinery in a load-bearing chain for 5 functions,
and a source or flag answer would be strictly better. Left open.
