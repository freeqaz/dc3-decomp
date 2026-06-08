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

### NEW LEAD: bone LOCAL lengths change between rest and live (should be rigid!)
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
