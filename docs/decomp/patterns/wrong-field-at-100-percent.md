# A wrong struct field costs one displacement — and a biased `this` hides which field it was

**Written 2026-09-16** (lane det-b). Detector: `scripts/analysis/this_offset_scan.py`.
Companion to [rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md) and
[anchor-displacement-false-wrong-global.md](anchor-displacement-false-wrong-global.md)
— this is the `this`-relative twin of the latter.

## The mechanism

Taxonomy class 1 (`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`)
is 121 behavioural bugs. The dangerous sub-shape is a **same-width, same-kind**
wrong field: `mSteps` where the image reads `mOffset`, `mStart` where it reads
`mCounts[0]`. The instruction is byte-identical except for its 16-bit
displacement, so:

* the emitted code is the same size and the same shape — no insert, no delete,
  no replace, no register pressure change;
* `run_objdiff`'s **headline rounds**, so a one-displacement diff in a
  300-instruction serializer renders `100.0%`;
* the function is never looked at again, because 99.99% reads as done.

`report.json`'s `match_percent_normalized` *does* charge it (exact f32; an
immediate/displacement diff is deliberately not folded into `arg_diff_score`),
so the information was always there — nobody was reading it per-field.
`RndFlare::Load` and `CharUpperTwist::Load` both survived to the 99% band this way.

## Why a naive offset comparison is worthless: `this` is routinely BIASED

Measured on the target object, `?Load@RndFlare@@UAAXAAVBinStream@@@Z`:

```
mr   r31, r3                 ; r31 = this
...
lwz  r11, -0x184(r31)        ; the VPTR -- so r31 is NOT the object base
subi r3,  r31, 0x188         ; -> this_base, passed to RndFlare::CalcScale
subi r3,  r31, 0xc4          ; -> RndTransformable subobject
subi r4,  r31, 0x5c          ; -> mSteps   (0x188 - 0x5c = 0x12c)
subi r4,  r31, 0x60          ; -> mOffset  (0x188 - 0x60 = 0x128)
```

MSVC handed this function a `this` biased by **+0x188**, so every field is
reached by a *negative* displacement. `RndFlare::CalcScale`, in the same class
and the same object, runs unbiased (`stfs f13, 0x180(r31)` = `mScaleFactors`).
The bias is a per-function codegen choice.

Three consequences, each of which has already cost a lane:

1. **The `this` register varies** — r31 in `RndFlare`/`CharUpperTwist`, r30 in
   `RndText` (r31 is the frame pointer there), r29 or r23 in `CharMirror::Poll`
   depending on which side you read. Read the prologue; never assume.
2. **The raw displacement is meaningless on its own.** `-0x5c` is `mSteps` in
   one function and nothing at all in the next. Reduce to `K + displacement`
   where `K` is the register's proven offset from the incoming `this`, or you
   are comparing two different coordinate systems.
3. **A field NAME requires the anchor**, and guessing it invents evidence.
   `PartyModeMgr::ClearTeam`'s coordinates `0x68`/`0x74` are
   `mTeam1Players`/`mTeam2Players` at anchor 0 — and fit **6 of 6** declared
   members at anchor 0x20, where they read as
   `mRightTeamPrevScore`/`mRightTeamStarBonus`: a completely fictional story in
   the exact shape of a true positive. The scanner now prefers anchor 0 on a tie
   and refuses any anchor that does not fit *every* coordinate.

## What the detector does

`this_offset_scan.py` compares, per function pair, the multiset of
**this-relative coordinates** the two sides actually touch:

* an address that is **dereferenced** (any D-form/DS-form load or store), and
* an address **live in an argument register at a call** — because that is how a
  serializer touches a field (`subi r4,r31,0x5c; bl ReadEndian` dereferences
  nothing locally).

An `addi` that is neither is an **intermediate anchor**, not an access.
`Flow::PreSave` computes `subi r11,r3,0x180; addi r3,r11,0x68` where we compute
`subi r31,r3,0x118` — the same address, two spellings.

It reports only the **substitution** shape: same count, same kind, a coordinate
we touch that the image never touches.

## What it cannot see

* **A byte-identical function cannot carry an offset-visible wrong field.**
  Identical words have identical displacements. 14,618 of 20,432 examined pairs
  (71.5%) are in that state — that is a *proof of absence for this class*, not a
  blind spot, and it is why the "100% with zero mismatch rows" slice is empty by
  construction rather than by judgement.
* **A mislabelled LAYOUT.** If our header names offset 0x5c `mOffset` and the
  real class has `mSteps` there, both sides emit the same displacement and this
  scanner — which only ever compares our object against the target's — is
  structurally blind. That needs independent layout truth (RB2 DWARF, Ghidra),
  not a target diff. **This is the residue of class 1 and it is unhunted.**
* **TUs that do not build.** 979 of 2,223 target objects have a counterpart.
* **Templates and adjustor thunks**: 2,733 + 3,027 drops, counted on every run.
* **Loops.** The register scan is linear, so a register reassigned around a
  backward edge reads as its fall-through value. Symmetric, so it can mis-*name*
  a coordinate but cannot invent a difference between identical streams.

## The four artifact classes — each has a worked example, each is counted

| bucket | signature | worked example |
|---|---|---|
| `uniform-bias` | every coordinate moves by ONE delta | our build picked a different `this` bias |
| `multiplicity-swap` | both sides touch both coordinates, counts redistributed | `PartyModeMgr::ClearTeam` — `switch` case blocks emitted in opposite order, so objdiff pairs case 1 against case 2 and every row reads ±12 |
| `multi-delta-block-swap` | more than one distinct delta among the pairs | `PartyModeMgr::FinalizeTeam` — deltas −12 (team vectors) and −20 (pickers) at once, objdiff rows perfectly symmetric |
| `contradicted-by-100pct` | `report.json` says exactly 100.0 | `HamNavList::NumItems` — a pure r30↔r31 permutation; at 100.0 normalized **no displacement can differ**, so the row is the instrument's, not the decomp's |

That last one is worth generalising: **when you have two instruments, make the
cheaper one contradict the other rather than trusting either alone.**

## Two instrument defects this hunt produced (both fixed, both measured)

1. **`bl __savegprlr_25` is not a clobber of r3.** Treating the second
   instruction of every EH-bearing prologue as an ordinary call kills the `this`
   proof before `mr r31,r3` runs, and the scanner then reports *zero*
   this-relative accesses for most of the corpus while looking perfectly
   healthy. (The same defect in the unicorn harness overstated real bugs ~8×.)
2. **A flow-insensitive register proof is ASYMMETRIC, and asymmetry invents
   findings.** Proving `reg == this + K` only when *every* definition agrees is
   sound in isolation and wrong for a comparison: whether a register is reused
   later is a register-allocation choice. `HamNavList::NumItems` — **100.0000**
   normalized, a pure r30↔r31 permutation — was reported as a wrong-field
   candidate purely because *our* r31 had two disagreeing definitions and the
   image's r30 had one. A rule whose verdict depends on which side you stand on
   is not a measurement.

## The manual recognizer

For the part the tool cannot reach, and for adjudicating any row it produces:

1. `this_offset_scan.py --explain '<mangled>'` — both sides' coordinates with counts.
2. `run_diff_inspect(mode="mismatches", diff_mode="name_check")` — look at the
   actual rows. **Branch-target diffs (`[br]`) next to symmetric ±N offset rows
   mean swapped blocks, not a wrong field.**
3. Ask the discriminating question: **is the coordinate we use one the image
   never touches anywhere in the function?** If the image touches it too, the
   difference is *where*, not *what*.
4. Only then resolve the anchor (`subi rX, rThis, K` feeding a same-class call,
   or the vptr load) and name the field.

## Provenance

Whole binary, 2026-09-16, worktree `det-b-wrongfield`: universe **30,832**
function bodies defined in both the target object and ours across 979 paired
units; **20,432 examined (66.27%)**; 14,618 byte-identical; 5,747 agree after
decode; 59 shape-differs; 5 multiplicity-swap; 1 multi-delta; 1 contradicted at
100%; **1 finding** (`CharMirror::Poll`).

Two-sided sabotage control, re-injecting the documented `RndFlare::Load` bug as
`bs >> mRange >> mStep` (0x130) where the image reads `mSteps` (0x12c):
clean **1** → sabotaged **2** → reverted **1**, the extra row being exactly
`?Load@RndFlare@@UAAXAAVBinStream@@@Z`, kind `arg`, target `this-0x5c` vs ours
`this-0x58`, delta +4, at norm 99.9929 — which also exercises the
`contradicted-by-100pct` gate from the passing side.
