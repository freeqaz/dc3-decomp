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

## The weak bucket, adjudicated (2026-09-30, lane leads-disp-arith)

Whole binary at `731b6137f` (main), same universe and coverage as below:
**218 candidate rows in 20 functions** (cand-permuted 216/19, cand-novel-address
2/2). **Every one adjudicated; zero real bugs.** Method: both listings traced by
hand (`build/373307D9/asm/**` against objdiff's aligned listing), and for the
float-heavy leaf functions a two-sided numeric run -- the image's `.text` and
ours executed in the unicorn PPC harness on the same random inputs, output
regions compared (`/fp:fast` last-bit noise ~1e-6 relative is equality; a
one-operand sabotage of `Multiply(Transform)` built through ninja gives
max relative error **5.27**, 100/100 samples differing, and reverting it
returns **3.6e-7**).

| function (rows) | native? | refutation |
|---|---|---|
| `CharInterest::ComputeScore` (5) | yes | Vec.h component order (image z,y,x; ours x,z,y) through Subtract/LengthSquared/Dot: every component meets its own partner. Other one-sided effects: the li-0/li-1 diamond read linearly, a scratch FPR at `atexit`, and `lbl_82010450` = `.rdata` **0.25** vs `__real@3e800000` |
| `HandInvokeGestureFilter::CalcInPose` (2) | yes | `Value()->y/z` loads permuted; each meets `rightArmDir.y/.z` (0x82DFE28C-AC). All five angle tests, bends and the tilt constant re-derived from the listing: equal. **Now bucketed `reordered` by the tool** (see defects below) |
| `RndParticleSys::MoveParticles` (6) | yes | midcolVel/colVel component registers permuted, each channel lands in its own `col` field; size phases: grow/sustain/shrink arms take the same (frame, scale, vel) triple, the image just keeps one `fsubs` in the tail |
| `RndScaleObject` (3) | yes | particle-system block scheduling: every field is multiplied by the same factor on both sides (0x148/0x14c/0x158/0x15c by fov, 0x150/0x154/0x1a0-0x1ac by scale, 0x198/0x19c by 1/fov) |
| `NgSpotlightDrawer::RenderConeDefs` (4) | **no** (`{}` natively) | camUp/camPos loaded in another order; scratch GPRs/FPRs at the `SetPConstant` vcalls; the image re-reads both radii from the stack Vector4 it just passed by const reference, we keep them in registers |
| `DxRnd::DrawString` (2) | no (rnddx9) | D3DCOLOR packing traced both sides: both build `a<<24 \| r<<16 \| g<<8 \| b` |
| `Invert(Matrix4)` (10) | yes | numeric, both `.text`s, 400 inputs (half affine): max relative diff **4.1e-6** |
| `Multiply(Transform, Transform, Transform&)` (7) | PPC arm only | both arms (`&b == &out` and not) traced to the same `a.v * b.m + b.v`; numeric, both aliasing modes, 300 inputs each: **6.2e-7 / 7.2e-7**; sabotage control above |
| `Multiply(Matrix3, Matrix3, Matrix3&)` (3) | yes | numeric, 300 inputs: **9.5e-7** |
| `CSHA1::Transform` (147) | yes | already verified behaviourally (both `.text`s produce SHA-1("abc"); see `src/system/math/SHA1.cpp`) |
| `ArcDetector::Update` (4) | yes | the 16-byte node copy into the stack Vector3 is the same word-for-word map in another order; the rest is scratch at `list::insert` |
| `SkeletonQualityFilter::UpdateIsSideways` (4) | yes | joint loads permuted; the only differing effect is the compare constant: image `.data` `lbl_82F0C194` = **0.25** (read once, never written), ours `__real@3e800000` |
| `RndCam::GetViewProjectXfms` (1) | yes | image `fneg`, then `* 2.0`; ours `* -2.0` (the source comment's known residual): projYNum equal. Scratch `f12` at `ScreenRect` |
| `SpotlightDrawer::DrawWorld` (3) | yes | intensity-scaled colour: r/g/b land at 0x60/0x64/0x68 on both sides; differing FPRs are scratch at the vcall |
| `RndParticleSys::InitParticle` (2) | yes | `size + sizeVel` added in the other order. (Its one-sided `vel.w` store: the image joins the ternary before one store, we store in each arm) |
| `DxParticleSys::DrawParticles` (2) | no (rnddx9) | scratch GPRs at `D3DDevice_EndVertices` (takes r3 only) |
| `FlowDistance::Activate` (2), `kdTree::FindSplit_Mean` (6) | yes | adjudicated by det-disp (scratch f13 at a vcall; two `rlwimi` spellings of one insert) |
| `RndLine::UpdateLine` (3 + 1 novel) | yes | see the arith doc's trace-crossed-branch pass (same function, same lane) and det-disp's novel-address refutation |
| `HamDirector::CollideList` (1 novel) | yes | det-disp: refuted |

**Outside the candidate buckets** (a sample, not the bucket): 7 of the 22
`permuted-unobserved` LEAD rows were read too -- `MeasureMap::
AddTimeSignature` [2] (the image CSEs `tick - prev.tick` across the MILO_FAIL,
we recompute it; same operands), `Skeleton::Poll` [4] (the floor-clip-plane
Vector4 copied word-for-word in another order, then `w` read back from 0x6c on
both sides), `RndAmbientOcclusion::BlendVert` [1] (a dead `addi r11` -- the
typed "tex vs tangent" field pair is an address never dereferenced). All
refuted. The other 15 (`CharEyes::LidTrackAndClampingUpdate` 3,
`CharGuitarString::Poll` 7, `NgLight::SphereConeTest` 1, and 4 ⊘) are unread.

**What recurs:** 12 of the 20 functions carry a one-sided effect that is only a
SCRATCH register at a call the tool cannot type (indirect call, by-value
return, CRT/D3D external). The by-value-return part of that is now closed
(below). The rest -- indirect calls and externals -- stays strict on purpose.

## Instrument defects found by that pass (fixed, `77d3ed088`)

`arg_regs()` -- the "exact registers from the callee's signature" model --
was wrong in three measured ways:

* **A by-value aggregate parameter is not one GPR.** `Vector3DESmoother::
  Smooth(class Vector3, float, bool)` is called `ld r4; ld r5; fmr f1; li r7`:
  two 64-bit GPRs for the Vector3, so the float consumes r6's slot and the bool
  is r7. The model compared r6 and never r5 or r7 -- **a soundness hole**: a
  wrong z/w half or a wrong bool at such a call could not keep a row a
  candidate. 1,953 signatures in report.json had that shape. Now strict.
* **Constructors and destructors lost `this`** (their head is the bare
  `public:`, which never matched `"public: "`), shifting every argument down
  one register.
* **By-value aggregate returns** were refused ("do not guess"). Measured
  layout: r3 = hidden result pointer, r4 = `this`, then the arguments.

Same objects, before -> after: exactly 2 of 5,473 rows moved (CalcInPose's,
to `reordered`); nothing moved INTO a candidate bucket, i.e. the soundness
holes were not hiding a live difference in today's tree.

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
