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
band is how `RndFlare::Load` was found after `CharUpperTwist::Load`. The related
`BEGIN_SAVES`/`BEGIN_LOADS` *permutation* sweep (same member multiset, different order) is
exhausted — zero remaining — but ~70 `Load`/`Save`/`operator>>` symbols are still sub-100 in that
band, and `run_objdiff` flags at least one outright:
`FxSendChorus::Load` — *"Source accesses 'mInputGain' but target accesses 'mReverbMixDb' — wrong
field?"*

A wrong field in a serializer silently corrupts every saved or loaded object of that type, and costs
almost no match% — which is exactly why this class survives to the 99% band.
