# A rendered "100.0%" is not byte-identity — three surfaces round

**Established 2026-08-19.** Every percentage this project renders is rounded, so **99.97% displays
as `100.0`**. Three independent surfaces do it:

| surface | what it showed | truth |
|---|---|---|
| `decomp.db.current_percent` | `100.0` | `99.971695` |
| `run_objdiff` headline | `Match: 100.0% normalized (99.8% raw)` | 3 `diff_arg` rows in its own table below |
| `report.json.match_percent_normalized` | `100.00` | `run_objdiff` 99.9%, 4 real mismatches |

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

> **"100% ⇒ artifact" may only be applied to an instruction count of ZERO MISMATCHES — never to any
> rendered percentage.**

Use `run_objdiff`'s instruction summary (`all equal` vs a mismatch table), not its headline number,
and never the DB's or `report.json`'s. If you are about to dismiss a divergence because "it's at
100%", re-measure and look at the table first.

## The fingerprint this exposes

A serializer whose diff is **one `this`-relative address** (`subi rN,r31,<offset>`) is a
Save/Load field disagreement until proven otherwise. Sweeping that fingerprint across the 90-100%
band is how `RndFlare::Load` was found after `CharUpperTwist::Load`.

A wrong field in a serializer silently corrupts every saved or loaded object of that type, and costs
almost no match% — which is exactly why this class survives to the 99% band.

## 2026-08-19 sweep: three corrections to the above

A full sweep of this fingerprint was run over every serializer in the binary. It found real bugs
(below), but three things in the earlier text are wrong and cost time:

**1. `r31` is not `this`.** In any function with a large frame MSVC uses `r31` as the *frame
pointer* (`subi r31, r1, <framesize>` in the prologue), so `<off>(r31)` is a stack slot. Three of
the loudest apparent "field disagreements" were pure stack-layout diffs: `FxSendChorus::Load`
(r31 = r1-0x?? , 10 rows), `CamShot::Load` (**119** rows, frame 0x470 vs 0x490) and `RndText::Load`
(frame 0x1c0 vs 0x1b0). Always read the prologue before believing an `r31` offset.

**2. `run_objdiff`'s "Offset Mismatches (resolved)" enrichment does not check the base register.**
It resolves *any* offset against the class struct, so it will invent a field-disagreement story out
of a stack diff. The named lead in the earlier text —
`FxSendChorus::Load`, *"Source accesses 'mInputGain' but target accesses 'mReverbMixDb'"* — is
exactly this false positive; `FxSend`'s layout has no hole (`FxSend::Save` 74/74 and
`FxSend::Load` 151/151 are byte-identical). Treat that enrichment as a lead, never a finding.

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
