# 2026-07-02 (part 2) — Feet-in-floor FAITHFUL ROOT SOLVED: the pelvis retarget lift exists, works, and is stomped by poll order

Continues [[2026-07-02-feet-web-loop-plant-gap]] (the web-loop plant fix, same day). This
session set out to instrument the "knee .rotz under-accumulation" hypothesis and ended up
overturning the entire knee framing and finding the real mechanism end to end.

## TL;DR — the complete verified causal chain

1. **Move clips are authored on a normalized rig.** Raw clip bytes (dumped in-engine,
   `DC3_CLIP_POS`): `bone_pelvis.pos` z ≈ 31 (plain floats, comp=1 — POS channels are NOT
   short-compressed), knees bend to −43°..−83°. Played raw, this pose puts toes at −6.
   The clip also carries an animated **`bone_footik.pos`** channel (e.g. (0,1,0.5)) = per-frame
   left/right foot-plant weights for the runtime IK.
2. **Xbox retargets at runtime**: `HamIKEffector` type `kEffectorTypePelvis`
   (HamIKEffector.cpp:593-620) rescales pelvis-height-above-ground by
   `ratio = liveLegLen / neutralLegLen` (live: knee.local.x + ankle.local.x = 17.708+18.234;
   neutral from `mSkeleton->NeutralLocalPos`), blended by leg extension. For angel:
   31 × ~1.23 ≈ **38** — exactly the measured Xbox pelvis (36.3–39.9 by beat). The ankle
   effectors + CharIKFoot FSM then plant feet AT the floor (Xbox toe min = −0.00 exactly).
3. **Native computes the SAME lift correctly** — then loses it. The per-character poll order
   on native is `IK effectors → bone.servo → song.hdrv` (empirical `DC3_SEQ` trace), so the
   pelvis effector's `SetWorldXfm` lift (31.7 → **38.87**, toe +0.74 — Xbox-exact) is
   recomputed away by the servo's `PoseMeshes` in the same frame. Xbox's rendered pose
   requires `song.hdrv → bone.servo → effectors` (producers first).
4. **Why the order differs**: `CharPollableSorter::ChangedBy` polarity. Our DC3 decomp (and
   the byte-matched `Sort`, 100%) computes "a changedBy b" (consumers first); RB3's matched
   sorter computes the transpose (producers first). Flipping the polarity on native
   (`DC3_POLL_ORDER_FIX=1`) produces exactly the Xbox-required order and the lift SURVIVES
   the whole frame (pelvis mean 38.00 over the routine, = Xbox).

So: **the feet are in the floor because the whole dancer rides ~6-7 units too low — the
pelvis-height retarget is computed and then stomped.** The knee was never under-bent; if
anything native knees bend MORE (they follow the raw clip on a too-low pelvis).

## What was refuted this session (all with instrumentation, same song/rig as ground truth)

- **Knee .rotz under-accumulation: DEAD.** New probes (`DC3_KNEE_CLIP`/`DC3_KNEE_FINAL`,
  HamDriver.cpp) show the song.hdrv blend produces knee −20..−83° (median −59), weight 1.0,
  single layer, **punts=0** (TestDstComplain never fires), no missing channels, ScaleAdd
  canary 164K+ calls. The July-1 "zero-hit probe" mystery is moot.
- **Servo/PoseMeshes loss: DEAD.** Final mesh knee rotz == blend value (ratio 1.00 across
  201 joined frames).
- **Bone-length decode: DEAD.** On the matched rig (angel04) native kneeVx/ankVx =
  17.708/18.234 — identical to Xenia. (An earlier mismatch was the rasa05 rig from the
  default gate flow — see "song confound" below.)
- **Clip pos decode scale: DEAD.** Ghidra on the target confirms 0.039674062 (=1300/32767)
  and 0.00061035156 (=1/1638.4) — our constants match. And POS channels in these clips are
  uncompressed floats anyway.
- **relative-clip composition: DEAD** for these clips (`Relative()` = none in the files).

## The song confound (methodology note)

The saved Xenia ground truth (`xenia-headless.log`, repo root) is **thehustle / angel04 +
aubrey04 / rollerrink**. The gameplay gate's `ymca.txt` flow lands on **Starships /
rasa05+lima05 / streetside** (the flow comment's index map is stale). Every cross-log pose
comparison must use the same song+rig: `/tmp/thehustle-flow.txt` (= ymca.txt with 3 downs
instead of 4) reproduces the exact Xenia configuration. Beat-aligned join (both logs carry
beat markers): Xbox pelvis 36.3–39.9 tracking choreography; native flat ~31; delta 4.7–8.4.

## New instrumentation (all HX_NATIVE, env-gated, in tree)

- `DC3_KNEE_CLIP=1` — HamDriver.cpp: per-clip knee `.rotz` before/after ScaleAdd + final
  value + pelvis pos + ScaleAdd call canary + punt counter; one-shot `DC3_CLIP_POS` raw
  POS-section dump per clip (`CharBonesSamples::Dc3DumpPosChannels`, header + cpp).
- `DC3_IK_DIAG` `DC3_SEQ` trace upgraded (HamDirector.cpp `Dc3KneeLog`): gameplay-gated
  (frame>3000), 120 events, adds pelvis Z + frame; new call sites `HamDriverPoll-POST`
  (HamDriver.cpp) and `IKEffPoll-POST` (HamIKEffector.cpp, dtor-based so all return paths
  log) with PathName.
- `DC3_KNEE_LOCAL` (GameplayTelemetry.cpp) now also logs the thigh local matrix row
  (`thighMxx/Mxy/Mxz`) — directly comparable to the Xenia rig probe.
- `TestDstComplain` (CharBones.cpp) prints unconditionally on native (first 100) +
  `g_dc3DstPuntCount`; `CharBones::ScaleAdd` entry counter `g_dc3ScaleAddCalls`.

## The two switches (both native-only, PPC bytes untouched)

- **`DC3_POLL_ORDER_FIX=1`** (CharPollGroup.cpp `CharPollableSorter::ChangedBy`):
  producer-first polarity → Xbox poll order → pelvis retarget survives. **Default OFF**, see
  blocker below.
- **`DC3_IK_LOCALSCOPE=1`** (pre-existing, HamIKEffector.cpp `CharLocalIKScope`): runs the
  matched effector math character-local. This session FIXED its ctor to remap clean cached
  bone worlds (`W · R⁻¹`) instead of force-dirtying the subtree — force-dirtying erased
  earlier same-frame `SetWorldXfm` writes (the pelvis lift died at the very next effector's
  scope). `RndTransformable::SetWorldXfm` caches world + dirties children but does NOT
  back-solve the local — any dirty-recompute after an IK write discards it (this is
  Xbox-faithful; Xbox simply never re-dirties after the effectors).

## Remaining blocker (why the flip is not default): native ankle IK solve diverges

With the fixed order the effectors' output finally SURVIVES — which exposes the documented
native `CharIKHand` solve divergence full force (Push 12b/12c/13,
docs/sessions/2026-06-09-xenia-xbox-foot-truth.md):

| config (plant OFF) | result |
|---|---|
| old order (baseline) | 791/809 below floor at −4.3 (stable sunk pose) |
| flip alone | pelvis 38.0 ✓ but ankles/toes fling to ±300 (venue-offset explosion, gate-blind upward) |
| flip + old scope | sane but sunk (scope's force-dirty erased the pelvis lift) |
| flip + remapping scope | pelvis lift survives; L 138/781, R 468/781, worst −72 — solve still spirals |

Default config (no envs: old order + `Dc3RunPostPollFootPlant` clamp) re-verified GREEN
(0/741 both feet).

## NEXT — two viable paths to ship the faithful pelvis height

1. **(Recommended, small)** Add a deterministic **post-poll pelvis retarget** to the
   existing `Dc3RunPostPollFootPlant` hook (App.cpp → CharIKFoot.cpp): replicate the exact
   matched pelvis-effector math (ratio from `NeutralLocalPos`, blend bounds
   kneeLen·0.3/0.8 + ankleLen) but write the pelvis **LocalXfm** (durable), then let the
   existing 2-bone plant re-plant the feet from the lifted pelvis. Gets the Xbox pelvis
   height + planted feet without the unstable ankle IK stack. Verify vs Xenia beat table
   (pelvis 36–40, toe ≈ 0–0.8).
2. **(Faithful, big)** Stabilize the native ankle solve under `DC3_POLL_ORDER_FIX=1`:
   the June-9 doc's Push-13 items (SetWorldXfm path vs elbow path isolation; FSM lock —
   note the footik channel DOES reach the servo buffer now, but `bone_footik.mesh` local
   still reads 0 at DoFSM time on capped probes — re-check with the fixed order), plus the
   venue-offset sensitivity (scope or root-cause the `neutral + eff` identity).

Ground truth for acceptance stays: beat-aligned pelvis 36–40 at angel/thehustle, toe min
≈ 0, gate GREEN with `DC3_FEET_POST_PLANT_OFF=1`.
