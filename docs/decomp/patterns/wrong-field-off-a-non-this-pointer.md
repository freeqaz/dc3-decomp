# A wrong field off a pointer that is not `this` — and why "both sides touch both addresses" is not an excuse

**Written 2026-09-30** (lane det-disp). Detector: `scripts/analysis/pointer_disp_scan.py`.
Companion to [wrong-field-at-100-percent.md](wrong-field-at-100-percent.md) (the
`this`-relative half, `this_offset_scan.py`) and
[missing-container-operator-forces-wrong-spelling.md](missing-container-operator-forces-wrong-spelling.md)
(the motivating bug).

## The mechanism

Taxonomy class 1 (wrong struct field, 121 historical bugs) is only partly
`this`-relative. One pointer hop further out — `mNextShotIt->prev`,
`node->next`, `arg.mFoo`, `GetBar()->mBaz`, a Vector3 returned by value — a
wrong field is the same single row: same opcode, same registers, a different
16-bit displacement. `HamCamShot::SetPreFrame` stepped an `ObjPtrList` iterator
through `Node::next` (`lwz r11,0x14(r11)`) where the image uses `Node::prev`
(`0x18`), at 97.34%, for months.

A displacement diff off a non-`this` base is *usually* not a bug. On the whole
binary (below), 5,536 such rows exist and 3 of them are strong candidates. The
work is in the artifacts, and every one of them has a worked example:

| artifact | why it is not a wrong field | example |
|---|---|---|
| stack frame | an `r1`-derived slot; frame layouts differ | 3,842 rows |
| anchor | `addi rX,rY,K; lwz D(rX)` vs `lwz (K+D)(rY)` — one address | `DxTex::StartCompress` (`+0x40` anchor vs `+0x14`) |
| global anchor | `lis gA; lwz -0x285(rX)` names whatever **that build's** `.data` put there | `MemInit` (our `gCheckConsistency` vs the image's `lbl_830E56F0`) |
| scheduling reorder | the two loads swapped places; each value still goes where it went | `DecompressChunk` (`(size-p)+buf` vs `(buf-p)+size`) |
| fp:fast reassociation | a·b + c·d summed in another order | `MakeScale`, the `Multiply`/`Invert` family |
| scratch | a leftover register at a call that does not take it | `FlowDistance::Activate` (a junk `f13` at a virtual call) |

## Why the obvious artifact rule is unsafe

`this_offset_scan` excuses a row when both sides touch both coordinates
(`multiplicity-swap`). **That rule hides exactly the bug it resembles.** A real
field swap — `CharUpperTwist::Load`'s shipped 3-cycle, or the Plane::Set bug
below — touches every address on both sides too; only *where each value goes*
differs. This scanner's first cut used the rule and it would have hidden both
real bugs this lane fixed.

What decides it is **value flow**. Each side is evaluated symbolically into
hash-consed expressions (loads as `ld(epoch, kind, address)`, integer `+`/`-` as
a canonical linear form, `fadd`/`fsub`/`fmul`/`fmadd` family as a polynomial so
an fp:fast reassociation is invisible and a swapped leaf is not), and every
**effect** is collected: a store to non-stack memory, a call with its argument
registers (exact, from the callee's signature — a float argument consumes a GPR
slot: `ComputeScore(V3&,V3&,V3&,float,int,bool)` reads its int from r8), a
compare (including record forms), and the return register the signature names.
A row is excused only if every effect its value reaches has an identical partner
on the other side **and** it reaches at least one (a value the linear pass never
sees used is not thereby proven dead).

## Two real bugs this found (both fixed on branch det-disp)

* **`Plane::Set(v1,v2,v3)`** — `Cross(diff31, diff21)` where the image computes
  `diff21 × diff31`. Every coefficient of the plane (a, b, c **and** d) was the
  exact negation of the image's: same plane, opposite orientation. The effect
  listing showed four stores whose polynomials differed only in sign. Introduced
  by a match-chasing commit (`9978dff23`) that swapped the operands to fix a
  scheduling row. Canonical match unchanged by the fix (99.886795).
* **`RndParticleSys::InitParticle`** — with `mShrinkRatio == 1` the image stores
  `deathFrame` into `shrinkFrame` (`lfs f11,0x40(r31)` … `stfs f11,0x70(r31)`);
  ours stored `birthFrame`, copy-pasted from the grow arm. MoveParticles' sustain
  phase (`dt < shrinkFrame`, the one that applies `sizeVel`) could never run.

Both sat in the **weak** bucket (`cand-permuted`), not the strong one. Read that
bucket.

## What the tool cannot see

* **Byte-identical functions** (22,455 of 30,832 examined): proof of absence,
  not a blind spot.
* **A mislabelled layout** — both sides emit the same wrong displacement. Needs
  RB2 DWARF / Ghidra.
* **Across a back edge or a join.** The pass is linear; volatile registers reset
  after an unconditional branch. A value used only across a loop edge is
  `permuted-unobserved` (a LEAD bucket), never excused.
* **Memory the callee writes through a pointer we passed** is modelled only for
  the stack (`postcall(n, value)`); heap memory is forwarded store→load only
  under a same-base, no-intervening-call rule.
* **Indirect / by-value-return / varargs calls** have no signature: all fresh
  volatiles are treated as arguments (strict), so a scratch difference keeps a
  row a candidate. Most of the remaining weak bucket is this.
* **Bit algebra.** Two spellings of one `rlwimi` insert (`FindSplit_Mean`) are
  not proven equal.
* **An upstream wrong pointer** puts every row off it in
  `base-provenance-differs` (LEAD); the wrong load itself is `this_offset_scan`'s.
* **TUs that do not build**: 16,000 target functions, counted.

## Manual recognizer

1. `pointer_disp_scan.py --explain '<mangled>'` prints each in-scope row with
   both sides' base values **and the effects present on only one side**. That
   second list is the adjudication surface.
2. An effect pair that differs only in **sign** or in **which leaf** sits in a
   product/sum is a real semantic difference. One that differs only in a
   scratch argument register of an unsignatured call is noise.
3. For a store, compare the stored expression; for a load, find the effect that
   consumes it.
4. Two adjacent links of one node (`next`/`prev`, `left`/`right`) differing is a
   direction bug until proven otherwise — then read the loop.

## Instrument defects found on the way (all fixed in this tool; two are in siblings)

* `report.json` staleness: the `contradicted-by-100pct` gate trusted a score
  older than the object; the sabotage control's injected row hid there until the
  report was regenerated. The gate is now off for any unit whose object is newer
  than `report.json`. ⚠ `this_offset_scan.triage` has the same gate and the same
  exposure after a per-target rebuild. *(Fixed there 2026-09-30, lane
  fix-thisoff; see wrong-field-at-100-percent.md, defect 3.)*
* `this_offset_scan.classify_symbol` reads a free special-name operator
  (`??6@YAAAVBinStream@@…`, operator<<) as a MEMBER of class `YAAAVBinStream`, so
  it treats r3 — the BinStream — as `this`. Worked around here; not fixed there.
  *(Fixed there 2026-09-30 via `access_specifier_scan.code_index`; the
  `FREE_SPECIAL_RE` workaround here is now redundant, and this scanner's
  output was byte-identical under both classifiers.)*

## Provenance

Whole binary, 2026-09-30, worktree `det-disp` at the tool's final version:
universe **69,537** target function bodies; **30,832 examined (44.34%)**;
dropped 22,705 not-defined-in-our-object + 16,000 unit-has-no-built-object.
**5,536** displacement-only rows, every one in exactly one bucket: relocated 112,
stack 3,842, this-relative 297, anchor-normalised 55, addi-not-an-address 9,
reordered 368, contradicted 6; LEADs base-unknown 481, provenance-differs 109,
permuted-unobserved 22, global-layout 16; CANDIDATES adjacent-links 1
(SetPreFrame), novel-address 2 (both refuted), permuted 216 in 19 functions.

Two-sided control (sabotage `operator<<(BinStream&, const RndParticle&)`:
`p.size` → `p.sizeVel`): clean 219 → sabotaged 220, the extra row
`cand-wrong-field-typed [RndParticle: size vs sizeVel]` → reverted 219, text and
JSON byte-identical. Selftest sabotage (value flow disabled, run from a fresh
empty directory) fails the reassociation check.
