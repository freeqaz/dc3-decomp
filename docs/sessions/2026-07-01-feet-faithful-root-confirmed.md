# 2026-07-01 — Faithful root CONFIRMED: native under-bends the deep-crouch knee+ankle QUAT (matched-depth vs Xbox, no new capture)

Continues the feet-in-floor investigation ([[2026-06-09-xenia-xbox-foot-truth]]). The visible bug is
already fixed + default-on via the deterministic post-poll plant (`Dc3RunPostPollFootPlant`, commit
`0f83a3de`, gate GREEN, verified again 2026-07-01: 0/804 below floor ON vs 791/803 OFF). This session
pursued the **faithful source fix** the user asked for, and pinned the root with hard evidence.

## Method — matched pelvis-depth comparison, zero new emulator run
The prior wave binned native@33–35 vs a sparse Xbox n=5 → the −25° gap was arguably a pelvis-band
artifact. Resolved it by binning BOTH sides by pelvis-world-Z into identical bands:
- **Native**: fresh Step-0 run — `DC3_GAMEPLAY_TESTS=1 DC3_FEET_POST_PLANT_OFF=1 DC3_KNEE_LOCAL=1 DC3_IK_DIAG=1 milo-tests --gtest_filter=GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay` (882 KNEE_LOCAL samples).
- **Xbox**: the SAVED `xenia-headless.log` (Jun 9, 3.9M, 1500 `DC3:IK BONE` lines, corrected `+0x78` VMX128 world read, `valid=1`). Parsed 150 frames; kneeRotZ = `atan2(mxy, mxx)` (same formula as the native `DC3_KNEE_LOCAL` diag).

## Result — the under-bend is REAL at matched depth (not a pelvis artifact)
| pelvis band | Xbox knee | Nat knee | Δknee | Xbox ankRotZ | Nat ankRotZ | Xbox ankleZ | Nat ankleZ | Xbox toeZ | Nat toeZ |
|---|---|---|---|---|---|---|---|---|---|
| 34–36 (Xbox n=15 / nat n=168) | **−53.7** | **−32.2** | **−21°** | 25.3 | 13.5 | 4.37 | **0.77** | 0.05 | **−3.60** |
| 36–38 | −52.6 | −31.9 | −21° | 14.8 | 23.0 | 5.48 | 5.72 | 0.76 | 0.75 |
| 38–40 (n=87 / 86) | −31.5 | −23.1 | −8° | 9.1 | 11.7 | 5.05 | 4.46 | 0.51 | +0.59 |
| ≥40 (standing) | −24.7 | −4.2 | — | −2.2 | 7.4 | 5.49 | 4.39 | 0.48 | +0.01 |

**At the SAME pelvis depth (34–36) native bends the knee 21° less and the ankle ~12° less**, dropping
the ankle world Z from ~4.4 → ~0.8, and the rigid foot's toe sinks to −3.6. The gap **scales with
crouch depth** (8° @ pelvis 38–40 → 21° @ 34–36), and the global medians match (native −39° / Xbox
−40°). So the divergence is specifically the **deep-crouch tail of the clip blend**.

## What is RULED OUT (this session + prior, all source-verified)
- **β (layer-weight bootstrap):** REFUTED — `DC3_IK_DIAG DriverWeight` = `mWeight=1.0000 scaleDown=0.0000` on all 40 samples. The blended QUAT is at full weight, not scaled down.
- **QUAT decode:** matched (A/B-verified prior; native globally tracks Xbox median −40°).
- **IK / footik:** not the knee source — `mMoveElbow=false` on both platforms (IKElbow never runs); `analyze_footik` authors only `bone_footik.pos` (FSM plant-lock), never a knee QUAT (verified `process_clips_func.dta`). `ScaleAddFootik` confirms nPOS=0 (footik POS absent at runtime) — a separate FSM-lock gap, not the bend.
- **Pelvis-depth artifact:** ruled out by the matched-depth binning above.

## FAITHFUL ROOT (confirmed)
A **clip selection/blend divergence localized to the deep-crouch beats**: native under-provides the
knee+ankle QUAT bend as the crouch deepens. Full driver weight (mWeight=1) but the resulting blended
knee angle caps ~21° shallower than Xbox at matched pelvis depth. This is engine clip-blend territory
(`ClipPlayer::PlayAnims` / `HamDriver::LayerArray::Eval` / `CharBones::ScaleAdd` QUAT accumulation),
`#ifdef HX_NATIVE`-fixable — NOT an asset re-bake (the `.milo_xbox` clips are shared) and NOT an IK edit.

## NEXT — pin which clip/blend layer under-provides the deep bend
Distinguish (a) native selects/weights a less-bent clip vs (b) an additive crouch layer Xbox applies
that native under-blends. Acceptance = knee reaches ~−54° at pelvis 34–36 AND gate GREEN with
`DC3_FEET_POST_PLANT_OFF=1`. Tools: native `DC3_KNEE_LOCAL` (committed); Xbox saved `xenia-headless.log`
(repo root) is a re-usable matched-depth ground truth — no fresh Xenia run needed to aim.

## UPDATE (2026-07-01, this session) — the per-clip diag attempt REFUTED the assumed blend path
Tried to instrument the per-clip knee-QUAT contribution to pin weight-split vs selection. A prior
workflow modeled the gameplay pose as `HamDriver::LayerClip::Play → CharBones::ScaleAdd`. **Both
were disproven by direct probes** (all `#ifdef HX_NATIVE`, opt-in `DC3_KNEE_CLIP`, since reverted):
- A diag in `HamDriver::LayerClip::Play` (HamDriver.cpp:334) produced **0 lines** during the gameplay
  gate — LayerClip::Play is NOT called for the gameplay dancer in the milo-tests harness.
- An inventory probe at the top of `CharBones::ScaleAdd(CharBones&,float)` (CharBones.cpp:691) gated on
  "sees a bone whose name contains `knee`" produced **0 lines** — this ScaleAdd overload is never
  called with knee-bearing bones during gameplay either.
- Yet `DC3_KNEE_LOCAL` reads `bone_L-knee.mesh` LocalXfm animating correctly (882 samples, −47..−4).

**Conclusion:** the dancer's knee `.mesh` rotation is NOT produced by the simple CharBones clip-blend
we assumed — it flows through a different **clip→servo→mesh** path (likely `CharServoBone`, or a
skeleton-bone channel whose name is not `*knee*`, then mapped onto `bone_L-knee.mesh`). Blind
instrumentation of the blend is the wrong tack. **The real next step is to trace what sets
`bone_L-knee.mesh`'s LocalXfm during gameplay** (grep the servo/skeleton pose path;
`CharServoBone::Poll`/`PoseMeshes`; how a clip channel name maps to the knee mesh bone), THEN
instrument that stage. Until that path is understood, the per-clip weight-vs-selection question is
open. The confirmed root (§ above) stands regardless; the shipped `Dc3RunPostPollFootPlant` remains
the correct pragmatic emulation while the faithful source fix is scoped.
