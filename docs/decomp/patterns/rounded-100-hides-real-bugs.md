# A displayed "100.0%" is not byte-identity — but `report.json` is NOT one of the liars

**Established 2026-08-19, corrected the same day** after an independent verification read objdiff's
scoring source and built its own COFF reader + PPC disassembler. The original version of this
document said *"every percentage surface rounds"* and named `report.json` among them. **That was
wrong**, and it was wrong in the dangerous direction: it told readers to distrust the one surface
that is exact.

| surface | rounds? | notes |
|---|---|---|
| `decomp.db.current_percent` | **yes, inconsistently** | holds `99.85558` for one row and `95.38` for another — precision depends on which writer wrote it |
| `run_objdiff` headline | **yes** | printed `Match: 100.0% normalized` above a table listing 3 mismatches |
| `report.json.match_percent_normalized` | **NO — exact f32, no rounding** | written raw by `objdiff-cli/src/cmd/report.rs` |

`match_percent_normalized` is score-weighted, not rounded: `max_score = n_instructions * 100`, and
an **immediate** arg diff costs 1 point and is deliberately *not* folded into `arg_diff_score`
(`objdiff-core/src/diff/code.rs`, carve-out added 2026-05-26). So a wrong field really does show up:

- `CamShot::Load`, 119 wrong offsets in 824 instructions → `99.85558` = `100 − 119/(824·100)·100`
- the `RndFlare::Load` bug re-injected as a control → `99.992905` = `100 − 1/(141·100)·100`

Neither renders as `100.00`. **A single-field serializer bug is fully visible in `report.json`.**

What normalization *does* forgive is register permutation, branch-target and relocation-name
differences. So `100.0` there means "no non-register mismatch", not "byte-identical" — which is a
real caveat, just a different one from rounding. Measured over 1,397 serializers scoring exactly
`100.0`: 23 have instruction differences, 274 rows in total, **every one register permutation, zero
offset diffs** — so for a *field* hunt, `report.json < 100.0` is a sound and complete filter.

## Why it matters

The project has a standing and *correct* triage rule: a behavioural divergence reported on a
100%-matched function is by construction a harness artifact, because identical bytes plus identical
inputs cannot produce different behaviour. That reasoning is sound. It becomes a bug-hiding
mechanism the moment it is applied to a **rendered** percentage rather than to byte identity.

Two real bugs were sitting under a rendered `100.0`:

- **`CharUpperTwist::Load`** — three `subi` instructions proved `Save` and `Load` disagreed on
  member order (target loads mTwist2, mUpperArm, mTwist1; we loaded declaration order). Every
  serialized `CharUpperTwist` read back with its three bone references 3-cycled. `Save` measured
  100.0% with 34/34 equal, so the Save side was target-verified — the two really did disagree.
- **`RndFlare::Load`** — read `mOffset` where the target reads `mSteps` (`subi r4,r31,0x5c` vs
  `0x60`), so every flare kept its constructor default of 1 step regardless of the authored value,
  changing the fade ramp in `DrawShowing`.

## The rule

> **"100% ⇒ artifact" may only be applied to an instruction count of ZERO MISMATCHES — never to a
> *displayed* percentage.**

Use `run_objdiff`'s instruction summary (`all equal` vs a mismatch table), not its headline number,
and not `decomp.db`'s. `report.json`'s normalized value *is* trustworthy as a filter — it is exact,
and it cannot hide a wrong field — but it still forgives register permutation, so it answers "no
non-register mismatch", not "byte-identical". If you are about to dismiss a divergence because "it's
at 100%", re-measure and look at the table first.

## The fingerprint this exposes

A serializer whose diff is **one `this`-relative address** (`subi rN,r31,<offset>`) is a
Save/Load field disagreement until proven otherwise. Sweeping that fingerprint across the 90-100%
band is how `RndFlare::Load` was found after `CharUpperTwist::Load`.

A wrong field in a serializer silently corrupts every saved or loaded object of that type, and costs
almost no match% — which is exactly why this class survives to the 99% band.

## 2026-08-19 sweep: three corrections to the above

A full sweep of this fingerprint was run over every serializer in the binary. It found real bugs
(below), but three things in the earlier text are wrong and cost time:

**1. `r31` is not reliably `this` — and it is not reliably the frame pointer either.** In a function
with a large frame MSVC parks the *frame pointer* in `r31`, so `<off>(r31)` is a stack slot and
`this` lives elsewhere (r30 in both cases below):

| function | prologue | what `r31` is |
|---|---|---|
| `CamShot::Load` | `subi r31, r1, 0x470` (base `0x490`) | frame pointer, 119 rows of stack diff |
| `RndText::Load` | `subi r31, r1, 0x1c0` (base `0x1b0`) | frame pointer |
| `FxSendChorus::Load` | `mr r31, r3` | **`this`** — frame only 0xb0, no frame pointer at all |

⚠ **The prologue spelling is `subi rN, r1, <frame>`, not `addi rN, r1, -<frame>`** (corrected
2026-08-19 — the earlier text had the `addi` form). objdiff's disassembler emits `subi` here, so a
detector matching only `addi` finds **zero** frame pointers in this binary and silently classifies
every stack slot as a field.

`FxSendChorus::Load` is the one to remember: its ten differing rows *are* stack slots, but they are
`(r1)`-relative (`0x54/0x50`, `0x58/0x54`, `0x50/0x58`, `0x50/0x5c`) and `r31` is genuinely the
receiver. So the rule is **read the prologue**, not "assume `r31` is a frame pointer" — that
substitution just trades a false positive for a false negative.

**2. `run_objdiff`'s "Offset Mismatches (resolved)" enrichment did not check the base register —
FIXED 2026-08-19.** It resolved *any* offset against the class struct, so it invented a
field-disagreement story out of a stack diff. The named lead in the earlier text —
`FxSendChorus::Load`, *"Source accesses 'mInputGain' but target accesses 'mReverbMixDb'"* — is
exactly this false positive; `FxSend`'s layout has no hole (`FxSend::Save` 74/74 and
`FxSend::Load` 151/151 are byte-identical).

The scale was larger than "will invent": measured on real objects the day it was fixed,
`CamShot::Load` emitted **29 rows, all 29 `r31`-relative** and `RndText::Load` **25, all 25** — i.e.
**54 of 54 rows were false**, naming plausible members (`RndText::mScrollOutIndex`,
`RndTransformable::mConstraint`) in a form indistinguishable from a true positive.

The enrichment now reads the base register and classifies each row —
`object-field` / `stack-slot` / `frame-slot` / `mixed-base` / `unverified` — rendering only
`object-field` as a struct finding and printing how many it excluded and why. Frame pointers are
detected **from the prologue**, only from r1-derived **non-volatile** registers, so `mr r31, r3`
(`FxSendChorus`, `CharBonesSamples::Save`) still resolves correctly. `CamShot`/`RndText` now emit
zero field rows; `CharBonesSamples::Save` still emits its two genuine `this`-relative ones.

Two adjacent defects fixed at the same time: `stwu` was in the memory-opcode set, so the frame
*allocation* (`stwu r1, -0x470(r1)`) resolved as a field; and the hint re-derived the member name by
splitting the formatted string on `::` and ` (`, so
`RndText::mAltStyle (ObjPtr<Hmx::Object>)` shipped with the field name `Object>)`.

**It is still a lead, not a finding** — a stack access through a *volatile* r1-derived register is
not detected, and when the prologue is outside the instruction window (`full_listing=false`) rows
are labelled `unverified` rather than classified. Re-run with `full_listing=true` before trusting
one. Regression tests: `scripts/orchestrator/tests/test_offset_mismatch_base_register.py`.

**3. `report.json`'s `match_percent_normalized` does not "round" — it is byte-score weighted, and
it deliberately forgives register permutation.** Each instruction is worth 100 points and a
`diff_arg` costs 1, so `100 - score/max*100` is exact: `CamShot::Load` with 119 wrong offsets out
of 824 instructions renders **99.856%**, and one wrong field in a 300-instruction serializer
renders 99.9967%. That is a real sub-100 value, not a rounded 100 — the *displays* that round to
one decimal (`decomp.db`, `get_progress`) are where the information is lost.

Separately, it forgives register-only diffs entirely, so `100.0` there means "no non-register
mismatch", not "byte-identical": 31 of the 1888 authorable serializers it calls exactly 100.0 do
have mismatches. **But all 295 of those mismatch rows are register permutation, ICF/relocation
naming, or relocation addends — zero are `this`-relative offset differences.** So for a *field*
hunt, `report.json < 100.0` is a sound and complete filter; for a byte-identity claim it is not.

The `BEGIN_SAVES`/`BEGIN_LOADS` member-ORDER permutation class was re-tested at source level
(parse both bodies, compare the member sequence). It really is empty: all 9 raw hits were artifacts
of rev-gated legacy branches, and in every case where the parse still disagreed after stripping
them, either both sides were byte-identical to the target (`FxSend`, `RndTransformable`,
`UIListArrow`, `CharWeightSetter`) or the Load-side diff was pure regalloc (`RndGenerator`,
`CharDriver`).

### What the sweep did find

- **`RndMatAnim::Load`** — `ASSERT_REVS(0, 7)` with its arguments swapped (target proves `(7, 0)`,
  and `SAVE_REVS(7,0)`/`INIT_REVS(7,0)` agree). The emitted test was `if (d.rev > 0) MILO_FAIL`,
  so *every* MatAnim ever authored tripped the assert. A whole-tree audit of `ASSERT_REVS` against
  each file's `INIT_REVS`/`SAVE_REVS` found this was the only swapped pair in 238 sites — and that
  a *more permissive* assert is legitimate (`CharHair::Load` is byte-identical with
  `ASSERT_REVS(13,0)` over `INIT_REVS(11,0)`).
- **`ObjOwnerPtr::operator=`** — the class declared only `operator=(T*)`, so `a = b` used the
  *implicit* copy-assignment and also overwrote `mOwner` with the source's owner, leaving the copy
  reporting the wrong `RefOwner()`/`Replace()` target. Found by comparing the **multiset** of
  non-frame memory offsets per side (regalloc and scheduling preserve it; a wrong field does not) —
  five `*Anim::Copy` functions returned the identical base-only `lwz <mKeysOwner+0x10>` /
  `stw 0x10` pair. Fixing it made 5 functions byte-identical, including `RndLight::Replace`
  (80.9% → 100%).

The offset-multiset comparison is the better instrument than the single-`subi` fingerprint: it is
alignment-independent, immune to the frame-pointer confusion, and it is what surfaced the
`ObjOwnerPtr` bug, which no single-instruction fingerprint would have flagged.
