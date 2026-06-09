# 2026-06-08 — Feet-in-floor RE-VERIFICATION (ultracode session)

Goal of this session: independently verify the previous session's conclusion that
the DC3 native feet-in-floor bug is an "ANIMATION-pose channel-decode bug, NOT IK"
(commits `5f05afb4` + `c54598e7`). Re-running the diagnostics surfaced data the
prior conclusion did not account for.

## Gate (still FAILING)

`GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay`: L-toe worst Z **-4.20**,
R-toe **-4.10**; 705/729 (L) + 675/729 (R) below the -2.0 floor threshold.
Bug is UNFIXED. (47/48 of the suite otherwise green.)

Telemetry measures, on player0's char dir:
- `lAnkleZ` = `bone_L-ankle.mesh`->WorldXfm().v.z
- `lToeZ`   = `bone_L-toe.mesh`->WorldXfm().v.z
(GameplayTelemetry.cpp:266-310)

## Foot is RIGID + intact — uniform ~4.5u downward shift

| state    | ankle Z | toe Z | ankle→toe drop | toeLocal Z |
|----------|---------|-------|----------------|------------|
| rest     |  4.39   | 0.01  | 4.38           | —          |
| gameplay | -0.13   | -4.02 | 3.89           | **0.00**   |

`FootGeom`: `toeLocal=(3.89,5.03,0.00)` — in the ankle's LOCAL frame the toe has
**zero** vertical offset; its 4u world-drop is the ankle's normal rotation
(`ankleM.x=(0,0,-1)`, foot-forward points down, same as rest). So this is NOT a
foot-rotation bug. The entire rigid foot is shifted down because the **ankle
itself sits ~4.5u too low** (-0.13 vs rest 4.39).

## THE SMOKING GUN: IK effector is planted, but applied with ZERO weight

`IkSnap f=3001` (bone_L-ankle.ikf, char/main/main.milo):
```
fingerW.v=(32.12,-31.07,-3.93)   <- toe-target (spot_L-toe.trans), SUNK to -3.93
effW.v   =(33.06,-26.56, 0.45)   <- IK effector, PLANTED at the floor
neutral.v=(30.80,-30.62,-0.01)   <- neutral toe at floor (~0)
totalWeight=0.000  constraintCount=0   <-- ZERO CONSTRAINTS / ZERO WEIGHT
fingerDirty=0  effDirty=0
```

`ChainZ f=3001`: pelvis 33.73 -> knee 16.85 -> ankle(.mesh) 0.45 -> toe(.mesh)
-3.93. FINGER=spot_L-toe.trans, fingerW.z=-3.93, neutralZ=-0.007, clampF=0.000.

`AnkleClamp`: neutralZ==effZ (identical), clampFactor=0.0000 always (the clamp
never engages: (neutralZ - groundH - 5.0)*0.0909 < 0 -> clamped to 0).

### Interpretation (corrects the prior "it's an anim bug" claim)

The IK effector `bone_L-ankle.ikf` HAS a correct floor-planted answer (Z=0.45),
but `constraintCount=0 / totalWeight=0` means it is **never applied** to the
rendered skeleton bones. The skeleton therefore keeps the raw anim pose, in which
the foot sinks during the crouch.

- "Toe Z invariant under IK on/off" (prior session's airtight point #1) is TRIVIAL
  when IK already applies zero weight: skipping a no-op is a no-op. It does NOT
  show IK is irrelevant; it shows IK is already inert — which is the bug.
- "Matched engine -> discarded on Xbox too" (prior point #3) is INVALID: the
  native port (HX_NATIVE/LP64/clang) is NOT the byte-matched build. A port-only
  divergence (empty constraints, dirty/poll order, world-vs-local apply) can make
  native discard IK that Xbox applies.

## Three live hypotheses (to be adversarially resolved)

- **H2a (LEADING): empty IK constraints.** constraintCount=0/totalWeight=0 is
  itself the bug — on Xbox the foot-plant constraint has weight>0 and pulls the
  sunk ankle/toe up to the planted effector (Z~0). This is the ORIGINAL "empty
  constraints" theory the prior session believed it had refuted. The refutation
  targeted the *secondary* "dirty cascade explosion" mechanism; the *primary*
  observation (constraints empty) is corroborated by this run.
- **H2b: IK computed but discarded.** effector is planted but written via
  SetWorldXfm (world) and discarded by a later local recompute. Weakened by
  totalWeight=0 (nothing to discard), but the apply path must be verified.
- **H1: anim channel decode wrong.** the toe-target genuinely should decode to ~0
  and decodes to -3.93 (LP64 channel-stride). The prior session's pick. Note the
  toe-target is `spot_L-toe.trans`, an IK-system bone — verify what actually poses
  it before assuming a raw skinning channel.

## Repro

Build: `cmake --build native/build --target dc3-native milo-tests -j"$(nproc)"`
Gate:  `DC3_GAMEPLAY_TESTS=1 native/build/milo-tests --gtest_filter='GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay'`
Diag:  `MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_TEL=1 DC3_FAST_BOOT=1 DC3_IK_DIAG=1 MILO_MAX_FRAMES=9050 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt timeout 180 native/build/dc3-native`
  (FootGeom/RestGeom need DC3_TEL=1; ChainZ needs the director frame >3000, reached by frame ~9050.)
Full captured log this session: /tmp/dc3_tel.log

---

## DECISIVE EXPERIMENT — the sink is in the RAW ANIM POSE, IK-independent

Ran the same diag capture under three configs, comparing the first `FootGeom`:

| config                          | ankleW.z | toeW.z |
|---------------------------------|----------|--------|
| baseline                        | -0.13    | -4.02  |
| `DC3_IK_FOOT_SKIP=1` (skip ankle effector write) | -0.13 | -4.02 |
| `DC3_IK_CHARFOOT_SKIP=1` (skip foot FSM)         | -0.13 | -4.02 |

**Byte-identical.** Skipping the IK changes NOTHING -> the rendered ankle/toe equal the
raw `PoseMeshes` (animation) output before any IK. So:
- The ankle is sunk to ~0 (vs player0's OWN rest height 7.40) in the RAW ANIM POSE.
- H2b (discarded IK) is refuted as the SINKER: IK off == IK on.
- The prior "toe-channel LP64 decode" claim is refuted: the toe tracks the ankle by the
  correct rest offset (gameplay ankle->toe = 3.89; rest = 4.38); it is the LEG/ankle that
  is lowered, not the toe channel.

### player0's OWN rest (neutral) chain — the right reference (not iconman)
`NeutralWXfm` NEUTRALCHAIN (char/main/main.milo, player0):
```
spot_L-toe.trans  W=(30.74,-29.53, 3.95)
bone_L-toe.mesh   W=(30.74,-29.53, 3.95)  L=(3.73,3.76,0.00)
bone_L-ankle.mesh W=(30.18,-25.56, 7.40)  L=(15.39,0.00,0.00)
bone_L-knee.mesh  W=(29.65,-25.72,22.78)  L=(16.64,0.00,0.00)
bone_L-thigh.mesh W=(29.05,-24.81,39.38)  L=(0.00,0.00,3.14)
bone_pelvis.mesh  W=(25.92,-24.59,39.42)  L=(0.00,-0.50,90.64)
```
Live gameplay (ChainZ): pelvis 33.73 -> knee 16.85 -> ankle 0.45 -> toe -3.93.
Drop vs player0 rest: pelvis -5.7, knee -5.9, ankle -6.95, toe -7.88. The leg drops MORE
than the pelvis -> a deep crouch (or an over-extending leg channel on the port).

### NEW LEAD [REFUTED in Push 2 below — red herring]: bone LOCAL lengths "change"
> ⚠️ This lead is REFUTED by the Push-2 decode test. The "neutral L" column is the
> `neutral.iks` IK-reference skeleton; the "live L" is the *rendered char* skeleton —
> two DIFFERENT skeleton instances, not one bone changing length. The decode never
> changes a rigid bone's local translation (proven: drift 0.000). See Push 2.

| bone (local len)        | neutral L | live L  |
|-------------------------|-----------|---------|
| bone_L-ankle.mesh (shin, X) | 15.39 | 18.48 |
| bone_L-knee.mesh (femur, X) | 16.64 | 17.56 |
| bone_L-thigh.mesh (Z)       | 3.14  | 3.60  |
| bone_pelvis.mesh (Z)        | 90.64 | 34.49 |
Rigid bone lengths must not change with pose. This is either (a) animated TRANSLATION
channels on the leg/pelvis (legit for DC3 rigs) or (b) a skeleton-bind / channel-decode
divergence on LP64 — the SAME class as the rb3 sibling's skeleton-rebind fix
([[char-skinning-deform]]). It is the LEG + PELVIS bones, never the toe.

## Corrected root cause (supersedes 2026-06-03 doc's "2026-06-08" sections)

The rendered leg pose places the ankle ~4u below where it should be (relative to player0's
own rest height ~7.4), IK-independent, dragging the rigid foot's toe to ~-4. The foot-plant
IK computes a planted EFFECTOR (effZ~0.45 ~= groundHeight, HamIKEffector.cpp:686) but it is
NOT applied to the rendered skeleton (constraintCount=0 universally; IKElbow world-writes
are recomputed away; nothing writes the finger back).

REFUTED this session: (1) "toe channel decode" (toe offset is correct); (2) "discarded IK
is the sinker" (IK-skip is a no-op); (3) "IK is irrelevant" framing (the foot-plant clamp
exists precisely to catch this, but applies zero weight).

## THE ONE REMAINING FORK — needs Xbox ground truth
Skipping IK "exonerating" it is the same trap as the prior session: the foot-plant IK is
SUPPOSED to plant the foot, computes a planted effector, but never applies it. So either:
- **(iii) channel/skeleton decode divergence**: Xbox's raw anim already plants the foot
  (toe ~0); native's leg/pelvis channels (or bone-length decode, see NEW LEAD) over-extend
  on LP64. Fix = faithful leg/pelvis/skeleton decode (NOT the toe). Most likely given the
  rb3 sibling precedent + the bone-length anomaly.
- **(ii) IK-application divergence**: Xbox's raw anim ALSO sinks the foot, but Xbox's
  foot-plant IK lifts it (constraint weight>0 and/or a non-discarded apply path) while
  native discards it. Fix = the IK apply path / constraint load.

The decisive measurement: **Xbox's rendered `bone_L-ankle.mesh` world Z (and whether it = raw
anim or IK-lifted) at a beat-matched dance frame.** Prior Xenia capture was offset-bugged
(1/144 valid records). Reliable capture is the genuine blocker (P0). Infra exists:
docs/plans/custom-graphics-engine/BONE_GROUND_TRUTH_AND_CLIP_VALIDATION_PLAN.md.

Do NOT relax the gate or ship a native-only foot clamp until the fork is resolved — a
"survivable plant" that diverges from Xbox is the wrong fix.

---

# PUSH 2 (2026-06-08, ultracode) — decode EXONERATED; bug is in the IK foot-plant pathway

Goal: get firmer evidence + try Xenia. Built a Xenia-free decode test and ran a Xenia
capture. Result: the pose-channel DECODE is faithful; the leading suspect flips to the
IK foot-plant.

## FIRM #1 — pose-channel decode is FAITHFUL (new test, Xenia-free)
New test `ClipPoseFixture.LegBoneDecodeChannelTypesAndLocalStability`
(native/tests/test_bone_ground_truth.cpp) loads the `crouching_great_01` crouch clip on
the shared skeleton and checks every leg bone across beats. **PASSES**, `max LOCAL-
translation drift on a ROTATION-ONLY leg bone = 0.000`. Output:
- Channel types: `bone_pelvis` = `.pos`+`.quat` (hasPos=1); `bone_L-thigh/ankle` = `.quat`;
  `bone_L-knee/toe` = `.rotz`. **The leg bones are ROTATION-ONLY; only the pelvis translates.**
- Rotation-only leg bones keep their bind local translation EXACTLY across all beats
  (shin X=18.034, femur X=20.273, … constant) — no stride/LP64 corruption.
- The crouch clip decodes to a SANE, non-sunk foot on the shared skeleton (ankle worldZ
  +2.45, toe +2.7 — ABOVE floor) even at a deep crouch (pelvis local Z 12.6–18.9).
- The existing green test `WeightedPosAndQuatChannelsMatchLocalPose` already proves the
  POS *and* QUAT channel→local-pose decode match (within tol) — so rotation decode is
  faithful too, not just translation.

## FIRM #2 — the "bone-length-change" NEW LEAD is a RED HERRING
The rest "neutral L" values (shin 15.39, pelvis-Z 90.64) came from the `neutral.iks`
IK-reference skeleton; the live values (18.48 / 34.49) from the *rendered char* skeleton.
Different skeleton instances. The decode never changes a rigid bone's length (FIRM #1).

## FIRM #3 — the sink does NOT correlate with crouch depth
Isolated crowd crouch: pelvis drops ~30u → feet stay ABOVE floor (+2.7).
Gameplay song frame: pelvis drops only ~5.7u → feet SINK to -4. A *shallower* pelvis
crouch sinks the feet MORE. So the gameplay sink is not produced by the leg FK of the
crouch; it is added by the gameplay-specific IK/compositing.

## FIRM #4 — Xenia live capture is BLOCKED (confirmed empirically)
Ran `xenia-headless` (debug.xex, vulkan, fake_kinect, dc3_ik_telemetry, ymca script,
120s). Reached game_screen (stable, no crash), captured **516 bone records — ALL
`world=(0,0,~5.0)`** = the dancer skeleton is collapsed/unposed (async stall: song.anim
never loads). The capture mechanism is correct (per-bone, byte-swapped, valid=1) but there
is no real Xbox pose to read. Live-dance ground truth needs the async-stall fix (spin-poll
on unresolved thunk 0x83A00964) — weeks of work, out of scope.

## SHARPENED ROOT CAUSE — the IK foot-plant pathway, not the decode
Decode is exonerated. The gameplay sink is the leg FK NOT bending the knee enough to keep
the foot planted during the (shallow) crouch, and the foot-plant IK that should catch it
is INERT on native:
- The foot-plant CLAMP never engages: `clampFactor = (neutralZ - groundH - 5.0)*0.0909`,
  and gameplay `AnkleClamp` shows **neutralZ = 0.111** (≈ ground) so clampFactor < 0 → 0.
  With a CORRECT neutral ankle height (rest ankle ≈ 7.4) it would be ≈ 0.2 → the clamp
  WOULD engage and Interp toward the floor-planted eff. So the IK NEUTRAL ankle is
  collapsed to ground (0.111) instead of rest (~7.4) — this re-opens the "neutral skeleton"
  thread the prior session dismissed, now as the clamp-disabler (HamIKEffector.cpp:600-602).
- AND/OR constraints are empty (constraintCount=0) — the other foot-plant path.
- AND the IK world-write is discarded by the recompute (the apply path).

This is the family of hypothesis (ii) (IK-apply), with the decode (iii) now refuted.

## NEXT PROBE (Xenia-free, testable via the gate)
1. Find WHY the IK neutral ankle is 0.111 not ~7.4 — dump `mSkeleton->NeutralWorldXfm`
   for bone_L-ankle vs its rest. Is the neutral skeleton collapsed on native?
2. HX_NATIVE-gated experiment: feed the clamp the correct rest-relative neutral so
   clampFactor engages, and/or apply the planted eff to the rendered ankle as a LOCAL
   write (survives recompute). Measure the gate (FeetNotBelowFloorDuringGameplay).
   If the foot plants and the gate passes → confirms (ii). Keep it OFF by default until
   matched-safe; this would be the first candidate FIX, not just a diagnostic.
3. (Optional) confirm char/main decode parity by running the FIRM-#1 test against
   char/main + a song clip, to fully close (iii'').

---

# PUSH 3 (2026-06-09, ultracode) — DECOMP-BUG hypothesis tested vs Xbox asm: REFUTED

User hypothesis: maybe the IK clamp/apply C++ "is just writing to the wrong place" — a
DECOMPILATION accuracy bug (C++ diverges from the Xbox-360 asm) that would manifest in BOTH
the matched build and the native port. Two independent subagents diffed the suspect code
instruction-by-instruction against the real Xbox obj (`build/373307D9/obj/system/...`) via
DC3's objdiff fork.

## Audit A — IK math (HamIKEffector) = 100% FAITHFUL
| fn | match% | verdict |
|----|--------|---------|
| Poll | 99.7% | faithful (addr-reloc + stack-shift + 2 commutative fmuls) |
| ApplyConstraints / ApplyPosConstraints | 99.7-99.8% | faithful (label-reloc only) |
| IKElbow | 100.0% | byte-identical |
| GetGroundHeight | 100.0% | byte-identical |
| ComputeHandPullAndQuat / DoFancyElbow | 93-99% | faithful (regswap/commutative) |
- The `q.v += remaining*effQ.v` "doubling" → asm `fmadds` (faithful; Xbox adds-on-top too).
- The ground clamp `if (effQ.v.z<groundHeight) effQ.v.z=groundHeight` → correct dir/field/cmp.
- Back-transform `Multiply(inv, finalXfm, finalXfm)` → faithful operand order; FastInvert targets correct.
- IKElbow `grandparent/parent->SetWorldXfm` bone targets → byte-identical.

## Audit B — transform persistence (Trans.cpp) = FAITHFUL
| fn | match% | verdict |
|----|--------|---------|
| SetWorldXfm (408) | 93.87% | faithful (vtable-load/mDirty-store schedule reorder; NO behavioral diff) |
| WorldXfm_Force (655) | 99.65% | faithful (recomputes world from mLocalXfm every branch) |
| ComputeLocalXfm (506) | 100% | byte-identical |
| SetWorldPos / SetDirty_Force / SetTransParent | 100/100/99.6% | faithful |
- **KEY ANSWER:** Xbox `SetWorldXfm` writes ONLY mWorldXfm (0x48) + clears mDirty (0xbd) +
  vcall + dirty-children. It does **NOT** back-compute mLocalXfm — and the decomp reproduces
  this exactly (asm has no store to 0x8). So the IK's SetWorldXfm IS discarded on Xbox too,
  by design. The hypothesis "a dropped local-writeback" is REFUTED by the asm.
- The only world→local persist path is `ComputeLocalXfm` (via the `world_xfm` SYNC_PROP),
  NOT plain SetWorldXfm. CharForeTwist/CharUpperTwist already back-compute the local on the
  IK side to survive (docs/sessions/2026-03-24-forearm-twist-fix.md); the foot IK does not.
  (That session also notes back-computing inside SetWorldXfm caused stretched geometry — so
  the global fix is off the table; any survive-fix must be IK-side and local.)

## What this means (4 independent verifications now agree the engine is faithful)
decode (PUSH 2 test) + IK math (Audit A) + transform persistence (Audit B) are ALL faithful
to Xbox. So the native ENGINE MATH is not the bug. The feet-sink must come from one of:
- **INPUTS / SPACE** — native bakes the venue-world placement into the bone worlds BEFORE the
  IK Poll, whereas Xbox runs the IK CHARACTER-LOCAL and composites venue placement at render
  (see the `CharLocalIKScope` comment, HamIKEffector.cpp:23-60, and the HX_NATIVE scope at
  :432 — an attempted-but-maybe-incomplete fix). BOTH audit agents independently flagged this.
- **PLACEMENT** — the character ROOT Z. Gameplay player0 root W.z=0.11 (on floor) but the pose
  puts feet ~4u BELOW the root; an isolated crouch on the shared skeleton puts feet ABOVE the
  (origin) root. So ~7u of "feet vs root" is added by the gameplay placement/servo, not the pose.
- (Or the premise: does Xbox actually plant here? Only confirmable with the blocked live capture.)

## NEXT PROBE (Push 4)
Audit the INPUTS, not the math: (1) does `CharLocalIKScope` actually re-root the character to
origin for the IK, or does the venue offset leak in? Dump the bone worlds with/without the
scope. (2) Is the character ROOT/servo Z (0.11) correct, or should it lift the body ~4u so the
feet reach the floor? Dump the CharServoBone/placement transform vs the pose's lowest foot.
The fix is an input/space/placement fix (or an IK-side local back-compute like CharForeTwist),
NOT a correction to the audited IK/transform math.

---

# PUSH 4 (2026-06-09) — CharLocalIKScope re-enable: EMPIRICALLY a NO-OP

Recon found `CharLocalIKScope` (the character-local re-root that should match Xbox's IK space)
was DISABLED by a `return;` at HamIKEffector.cpp:67 (a prior session "forensically neutralized"
it). A read-only agent's PRIMARY hypothesis: re-enabling it fixes the sink (the venue offset
leaks into the IK and "doubles"). Its dtor (lines 103-110) directly re-composites every bone's
mWorldXfm — a persistence path that bypasses the SetWorldXfm-discard, so it was worth testing.

EXPERIMENT: env-gated the re-enable (`DC3_IK_LOCALSCOPE=1`, default still no-op,
HamIKEffector.cpp:63-72), rebuilt dc3-native, A/B'd the FootGeom toe Z:
| config | toe Z |
|--------|-------|
| baseline | -4.02 |
| DC3_IK_LOCALSCOPE=1 | **-4.02** (identical; X/Y wobble ~0.05 only) |

**REFUTED.** Re-enabling the character-local re-root does NOT move the rendered foot. This
empirically confirms (3rd independent way now) that the IK — in ANY space — does not affect the
rendered foot: it is discarded, and even the dtor's direct bone re-composite doesn't change the
result (the bones re-dirty/recompute from anim local before render). The recon agent's H1/H2
(IK-space, neutral-stamping) are IK-internal and therefore moot. **The rendered foot = the raw
anim pose, full stop.**

## Where this leaves it (the paradox, stated honestly)
Five independent verifications now agree the native engine is faithful/irrelevant to the rendered
foot: (1) decode faithful, (2) IK math faithful to Xbox asm, (3) transform persistence faithful,
(4) IK is a no-op (FOOT_SKIP), (5) IK-space re-root is a no-op (LOCALSCOPE). So the raw anim
places the toe at -4.02 AND the IK can't/doesn't lift it AND that IK behavior is matched-to-Xbox.
If all that is faithful, Xbox would sink too — yet the user reports Xbox feet are planted. One of
these must give:
- **(a) UNAUDITED placement** — HamRegulate::Poll adds `xfm.v.z += posDelta.z` during gameplay
  (HamRegulate.cpp:205-214); Character::Teleport/waypoint + CharServoBone set the root. The root
  is at venue Z=0.11 (on floor) and the pose puts feet ~4u below it — is a body-lift / regulation
  Z-delta a native divergence? NOT yet asm-audited. (facing channel already ruled out: facingPos.z=0.)
- **(b) DATA difference** — native loads a different clip/skeleton/waypoint than Xbox for this dance.
- **(c) PREMISE** — does Xbox actually plant for THIS specific YMCA dance/frame, or was the user's
  "planted" inspection a different song/character? Only the (blocked) live capture settles this.

## NEXT PROBE (Push 5)
Asm-audit the PLACEMENT/REGULATION chain (HamRegulate::Poll, CharServoBone, Character::Teleport)
the same way IK+transforms were audited — that's the only unaudited engine path that reaches the
raw foot. In parallel, sanity-check (b): does the native dance clip selection match Xbox's.

---

# PUSH 5 (2026-06-09) — placement faithful + native plays REAL choreography; bug cornered to the char/main pose pipeline

Asm-audit + telemetry audit of the two remaining suspects:

## (1) Native plays the REAL YMCA choreography — NOT a placeholder [REFUTED as cause]
Telemetry (state=playing): mergeMoves=1, routineLoaded=1, activeMoveCount=2, p0SongAnim=12,
doSongAnim=1, songAnimFrame advancing, lToeZ varying −4.0…+2.3. Pipeline is live:
HamDirector::Poll → ClipPlayer::PlayAnims (HamDirector.cpp:3143) → HamDriver eval → PoseMeshes.
The MoveMgr remixer builds the routine (MoveMgr.cpp:146-183,521). At worst a 3×-at-boot
expert-anim fallback (HamDirector.cpp:666-687) — still real DC3 data. So "wrong/placeholder
anim" is refuted.

## (2) Placement/regulation chain is FAITHFUL [REFUTED as cause]
Asm vs Xbox 373307D9: CharServoBone::MoveToFacing/Poll/RegulateInternal 100%, Character::Teleport
100%, Waypoint::Constrain 100%, HamRegulate::Poll 85.86% / Regulate 85.88% (sub-100% = pure
r30↔r31 regalloc cascade + commutative fmuls; the `xfm.v += posDelta` write is identical:
offsets 0x30/0x34/0x38, operands f28/f29/f30). HamRegulate moves only the ROOT, which is
correctly on the floor (player0 W.z=0.11). The sink is in the bones below the root — untouched
by regulation. Faithful + can't-be-the-cause.

## NEW concrete native-divergence candidate (B): HamDriver Layer::mWeight uninitialized
HamDriver.cpp:64-114 — `Layer::mWeight` is NOT initialized in the ctor. On Xbox it gets non-zero
garbage; native zero-init heap leaves it 0, which would gate `Eval()` OFF. There is a native
force-eval workaround so layers DO evaluate (the dancer animates) — BUT the blend WEIGHT applied
to each pose layer may be wrong. A wrong layer-blend weight → wrong blended pose → wrong foot,
WITHOUT any IK/transform/decode bug. This is a genuine LP64 zero-init divergence in the live
pose-blend path and is UNVERIFIED for correctness.

## Status: 6 verifications faithful; bug cornered to the char/main pose INPUT
decode (crowd path) + IK math + transforms + IK no-op + IK-space no-op + placement/regulation are
ALL faithful. The rendered foot = the raw blended pose. The gap is now ONLY in the gameplay pose
INPUT, which FIRM-#1 did NOT cover (it tested the crowd skeleton + a single crouch clip, not
char/main + the multi-layer song-move blend). Two concrete candidates:
- (A) a char/main- or song-clip-specific pose-channel decode divergence (re-run the FIRM-#1
  stability test against char/main + a song move, not the crowd clip).
- (B) the HamDriver Layer::mWeight zero-init blend divergence (verify the force-eval workaround
  produces the CORRECT per-layer weight vs Xbox).

NOTE: the "pelvis ~8u low + limb local-X lengths differ" observation keeps resurfacing but the
local-X *length* part is the PUSH-2 RED HERRING (neutral.iks vs rendered skeleton = different
instances). The pelvis world-height (34.5 live vs 42.5 rest) is real but expected for a crouch;
do not re-derive a "decode bug" from the length comparison.

## NEXT PROBE (Push 6)
(B) first (it's a found, concrete bug): verify HamDriver Layer::mWeight — does the native
force-eval apply the Xbox-correct weight per layer, or a wrong/forced value that skews the blend?
Then (A): extend the FIRM-#1 stability/decode test to char/main + a real song move.

---

# PUSH 6 (2026-06-09) — (B) ruled out; char/main+normal-clip PLANTS; bug isolated to the SONG-MOVE path

## (B) HamDriver Layer::mWeight blend — RULED OUT
Full lifecycle trace: mWeight is uninit in the ctor (HamDriver.h:16-29) but legitimately SET
before use — `LayerArray::Eval` (HamDriver.cpp:305) zeroes then accumulates; `LayerClip::Eval`
(:275) = `EaseSigmoid(...)*parentWeight`. The native force-eval bootstrap (Poll :65-73,
PreEvalClipWeights :84-118) seeds it with parentWeight=1.0 (NOT a forced final weight), then a
2nd Eval applies the true root weight; `SetClipWeightMap` (:232) runs from GetNeutralSkeleton
(HamCharacter.cpp:669,684) and normalizes. Verified faithful to Xbox asm (HamDriver.s). The
per-layer blend weights are correct → (B) is not the bug. (Side note: PreEvalClipWeights exists
because native polls IK effectors BEFORE song.hdrv — a poll-order divergence — but it only feeds
the IK neutral, which is discarded; moot for the rendered foot.)

## (A) char/main skeleton — POSES CORRECTLY with a normal clip [bind/decode REFUTED]
Ran the FIRM-#1 stability test with `MILO_TEST_CHAR=char/main/gen/main.milo_xbox` + the crouch
clip. Result: leg bones stable (drift 0.000) AND **feet PLANTED** — toe worldZ = +4.17/+4.05/
+3.84 (above floor), ankle +2.9…+3.8. So char/main's skeleton bind + the shared decode produce a
sane, planted foot for a normal dance clip. char/main bind/decode is NOT the bug.

## THE NARROWING: the sink is specific to the gameplay SONG-MOVE
char/main + crouch clip → toe +4 (planted). Gameplay char/main + SONG move → toe -4 (sunk).
Relative to the root the gap is ~8u, and it is NOT root placement (both roots ≈0). So the sink is
in the pose the GAMEPLAY SONG MOVE produces — either:
- (A1) the song-move POSE DATA genuinely puts the feet low (choreography authored to be caught by
  foot-plant IK — but IK is faithful-and-discarded on native AND Xbox ⇒ would sink on Xbox too ⇒
  premise), OR
- (A2) the SONG-ANIM APPLICATION PATH (HamDirector song-anim → RndPropAnim → ClipPlayer::PlayAnims
  multi-layer / additive base-pose) poses differently than a plain `CharClip::PoseMeshes(clip)` —
  e.g. an additive/base-pose offset, a wrong base frame, or a per-move root/pelvis offset that
  drops the legs ~8u. This is the last untested application difference.

## State: 8 verifications faithful; bug is the song-move pose, not the engine
decode(crowd) + decode(char/main) + IK math + transforms + IK no-op + IK-space + placement +
blend-weights are ALL faithful. A normal clip on char/main plants the feet. The native ENGINE is
faithful end-to-end; the feet-in-floor is produced by the gameplay SONG-MOVE path specifically.

## NEXT PROBE (Push 7)
Isolate an actual SONG MOVE (e.g. orig-assets/extracted/songs/<song>/gen/moves.milo_xbox or
modular_song_data/gen/era0N_moves.milo_xbox — these hold the .move/RndPropAnim choreography) and
pose char/main with it the SAME way (or via the song-anim path), checking the foot Z. If a song
move sinks the foot in isolation → (A1) song data (⇒ likely premise: Xbox sinks too / relies on
a foot-plant that's discarded on both). If it stays planted in isolation but sinks via the
HamDirector song-anim path → (A2) the application path adds the drop (a real native-fixable bug).
