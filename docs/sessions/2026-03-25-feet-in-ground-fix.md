# Feet In Ground Fix — 2026-03-25

## Problem
Characters' feet clip through the floor during gameplay on the native port.

## Investigation

### Phase 1: IK System Verification
Confirmed the HamIKEffector IK system IS active during gameplay:
- Weight = 1.0, type = ankle, skeleton connected
- Ankle bone at Z=4.4 (rest), drops to Z=0.1 during dance animations
- `mGround` is null on ALL effectors — by design (never set in .milo data or C++ code, same on Xbox)
- Ground height falls back to `character->WorldXfm().v.z` = 0.0

### Phase 2: Decomp Chain Audit
Ran objdiff on 25 functions in the bone/IK/character transform chain. Results:

| Function | Before | After | Fix |
|----------|--------|-------|-----|
| CharIKHand::IKElbow | 86.5% | 93.0% | Wrong bone (shoulder→elbow), math bug |
| CharIKFoot::DoFSM | 91.1% | 97.4% | Missing mFootTransform cache, state 2 ref |
| CharIKHand::Poll | 85.9% | 91.4% | Vector3 member cast, merged conditions |
| HamRegulate::Regulate | 82.2% | 85.9% | Inlined dt, shared ref binding |
| ComputeHandPullAndQuat | 86.4% | 86.3% | Semantic fix: dz sign inverted |
| HamRegulate::Poll | 85.5% | 85.9% | Declaration reorder |
| GetGroundHeight | 77.8% | 78.4% | do-while restructure |
| NeutralWorldXfm | 96.4% | 96.4% | AT_LIMIT (volatile reg swap) |
| DoFancyElbow | 84.5% | 84.5% | AT_LIMIT (FPR spill strategy) |

Real decomp bugs fixed:
1. **CharIKHand::IKElbow**: `elbowDir` used `shoulder->WorldXfm()` instead of `elbow->WorldXfm()`
2. **CharIKHand::IKElbow**: Math bug in sphere collision `d` calculation (wrong formula)
3. **ComputeHandPullAndQuat**: `dz` pull direction sign inverted vs dx/dy
4. **CharIKFoot::DoFSM**: Missing `mFootTransform = mFinger->WorldXfm()` at function start
5. **CharIKFoot::DoFSM**: State 2 Subtract used `tf.v` instead of `mFootPosition`

### Phase 3: Floor Position Analysis
Used vertex data logging during GPU upload to determine actual floor surface height:
- `r_main_floor.mesh` near character position (X=68, Y=51): **Z=0.00**
- `r_raised_floor.mesh`: Z=17.8-19.9 (distant stage area, not where dancers stand)
- Character root position: Z=0.0

**Finding**: Floor IS at Z=0 where characters dance. Character root is correctly at Z=0.

### Root Cause
The IK ground clamp prevents the **ankle bone** from going below Z=0, but the **foot mesh** extends ~3-4 units below the ankle joint. During low dance moves, the ankle drops to Z=0.1 (barely above ground), putting the foot sole at Z≈-3 (below the floor).

Verified with `DC3_FOOT_OFFSET=3` env var test — shifting characters up 3 units made feet sit perfectly on the floor.

## Fix

### Native-only foot-sole clamp (HamIKEffector.cpp)
Instead of clamping the ankle to `groundHeight`, clamp it to `groundHeight + footClearance` where footClearance = 70% of the neutral ankle rest height. This keeps the foot sole at ground level while still allowing natural ankle movement.

```cpp
#ifdef HX_NATIVE
float footClearance = (neutralQ.v.z - groundHeight) * 0.7f;
float soleGround = groundHeight + footClearance;
if (effQ.v.z < soleGround) {
    effQ.v.z = soleGround;
}
#endif
```

With neutralQ.v.z ≈ 4.4, footClearance ≈ 3.1 — matches the empirical DC3_FOOT_OFFSET=3 test.

Marked with `TODO HACK`. **This fix is insufficient** — feet still clip through the floor on the web build despite the native-only clamp. The `#ifdef HX_NATIVE` guard means it applies to both desktop and web native builds, but the issue persists, suggesting the clamp alone doesn't fully solve the problem or there's an additional contributing factor.

### Other fixes applied
- **ImGui headless crash**: Guard GLFW callbacks with null window check (ImGuiBackend.cpp)
- **Diagnostic cleanup**: Removed all temporary fprintf diagnostics from CharServoBone, HamRegulate, DirLoader, Object, BoneSetup, MeshGpuCache, Mesh_Wgpu

## Tools Used
- HTTP debug server (`DC3_HTTP=1`) for live scene queries during gameplay
- `scripts/dc3-agent-test.sh` for quick headless launches with HTTP + telemetry
- `MILO_INPUT_SCRIPT` with ymca.txt for automated menu navigation to gameplay
- 5 parallel subagents (3 Opus, 2 Sonnet) for batch decomp work across isolated worktrees
- Permuter (`--beam` mode) for automated source-level optimization

## Status: IN PROGRESS (2026-03-31) — dirty cascade root cause confirmed, fix needs Xbox ground truth

### Phase 9: Dirty cascade root cause confirmed (2026-03-31)

Telemetry instrumentation revealed the **mechanism**: IK correctly sets ankle WorldXfm (Z=5.84) during Poll, but by the time the renderer reads it, ankle is at Z=-2.5. The ankle bone gets **re-dirtied by a dirty cascade from the pelvis IK effector**.

**Root cause chain**:
1. Ankle IK effector runs → `SetWorldXfm(ankle, Z=5.84)` → `mDirty=false` ✓
2. Pelvis IK effector runs AFTER (CharPollableSorter doesn't see transitive bone dependency)
3. `pelvis->SetWorldXfm()` → cascade: `thigh.SetDirty()` → `shin.SetDirty()` → `ankle.SetDirty()` → mDirty=true
4. Renderer calls `ankle->WorldXfm()` → `WorldXfm_Force()` → recomputes from stale `mLocalXfm` → Z=-2.5
5. IK correction permanently lost for this frame

**Confirmed by dirty-flag telemetry** (checking Dirty() BEFORE WorldXfm()): `lAnkleDirty=1 rAnkleDirty=1 lKneeDirty=1 rKneeDirty=1 pelvisDirty=0` — pelvis is clean (its own SetWorldXfm cleared it) but cascade dirtied the entire leg chain. Thigh also dirty=1.

**Render-time confirmation**: Added diagnostic to FillBoneUniforms showing bones are dirty (wasDirty=1) and ankle Z is negative at actual GPU upload time. The IK corrections do NOT survive to rendering.

**Why PollDeps doesn't prevent this**: The pelvis effector declares `change = {pelvis_bone}`. The ankle effector declares `changedBy = {shin, thigh}`. The sorter only checks direct object matches, not transitive bone parent chains.

**Attempted native-only fixes** (all reverted — bandaging without understanding Xbox behavior):
1. **Save/restore via SetWorldXfm** — worked during Poll but pelvis entry (saved last, restored last) re-cascaded dirty to all descendants
2. **Depth-sorted restore** — NaN propagation across frames from stale parent transforms
3. **Direct mWorldXfm write** — broke WorldXfm_Force on subsequent frames (parent stays clean with stale data)
4. **Render-time override via LookupIKOverride** — frame clearing logic caused entries to be erased before Draw could use them

**Critical open question**: This same code runs on Xbox. The same dirty cascade SHOULD happen there too. Either:
- The Xbox bone upload path works differently (reads during Poll, not Draw)
- The Xbox has different timing for when bones are read
- The Xbox game also has feet below floor (less visible at 720p)
- Something else we're missing

**Blocked on**: Xenia IK telemetry — need Xbox bone positions at render time to compare. Xenia reaches game_screen but `__mftb()` (PPC timebase register) is frozen, freezing all game timers and preventing animation/IK from running.

### Phase 8: Feet-in-floor during gameplay (2026-03-31) [SUPERSEDED by Phase 9]

Phase 8's analysis was correct that the ground clamp's `footDepth` was unreliable, but this was a SYMPTOM of the dirty cascade, not the root cause. The "wild pre-clamp ankle Z values (-471 to +214)" were actually from different character instances cycling through the throttled diagnostic counter.

### Phase 7: Back-computation was the bug, not the fix (2026-03-31)

Deep investigation revealed the `#ifdef HX_NATIVE` mLocalXfm back-computation hacks in IKElbow/DoFancyElbow/Poll were themselves causing "flying feet" teleportation:

1. IKElbow displaces parent bone (shin) via `SetWorldXfm(parent, correctedXfm)`
2. Back-computation writes `mLocalXfm = MultiplyInverse(effector, displaced_parent)`
3. Next frame: animation restores the UN-displaced parent mLocalXfm from clips
4. But the effector's mLocalXfm was computed relative to the DISPLACED parent
5. Dirty cascade: `ankle.mWorldXfm = stale_backcomputed_local * un-displaced_parent` → **WRONG POSITION → teleportation**

The original Xbox code NEVER writes mLocalXfm after IK — it corrects mWorldXfm each frame via `SetWorldXfm()`, and nothing re-dirties the bone between IK Poll and Draw.

**Investigation confirmed**:
- Poll order is correct (same `CharPollableSorter` on both platforms)
- No render thread race (native port is single-threaded)
- `SetWorldXfm()` clears dirty flag — IK-corrected bones stay clean until next frame's animation
- The back-computation hack itself introduced the frame-delayed cross-reference between displaced/un-displaced parent states

**Current approach**: Strip ALL back-computation hacks and test whether the original `SetWorldXfm()` semantics work correctly. If something on the native port re-dirties IK-corrected bones between Poll and Draw, find and fix THAT specific source.

**Telemetry tests added** (10 new "Tier 7: Flying Feet detection"):
- `NoAnkleSuddenJumpsDuringGameplay` — catches ankle teleportation > 20 units
- `NoAnkleNaN/LocalXfmNaN` — catches NaN in ankle transforms
- `NoHandNaN`, `HandBonesNotFlying`, `KneeLocalXfmNotCorrupt` — catches arm/knee issues
- `AnkleRotationMatrixValid`, `AnkleSeparationNotExploding`, `AnklePositionSmoothness`
- Full suite: `DC3_GAMEPLAY_TESTS=1 ctest -R "Foot|Ankle|Leg|Bone|Inverted|Hand|Knee|Flying|Smooth|Explod|Rotation|Garbage|Collapsed"` (61 tests)

**Resolution**: All 6 mLocalXfm back-computation sites removed (2 in IKElbow, 3 in DoFancyElbow, 1 in Poll). The original Xbox `SetWorldXfm()` semantics — correct WorldXfm each frame without touching mLocalXfm — work correctly on the native port. Nothing re-dirties IK-corrected bones between Poll and Draw. 60/61 telemetry tests pass. Ankle smoothness median delta 0.10 units (was inf with the hack).

The ground clamp and foot inversion diagnostic are preserved (separate, valid fixes).

**Phases 4-6 below are historical** — the back-computation approach described there was well-intentioned but fundamentally wrong. It introduced a frame-delayed cross-reference between displaced and un-displaced parent states that caused the very teleportation it aimed to prevent.

### Phase 4: Root Cause — Missing mLocalXfm Back-Computation

The actual symptom was **disfigured characters with merged limbs** — bones collapsing to shared points, stretched mesh triangles, feet centered at origin. This was the same bug pattern as the forearm twist fix (see `docs/sessions/2026-03-24-forearm-twist-fix.md`).

**Root cause**: `HamIKEffector::Poll()` calls `SetWorldXfm()` on bones to apply IK corrections. `SetWorldXfm()` sets `mDirty = false` on the bone itself but cascades `SetDirty()` to children. If a later pollable dirties a parent bone, `WorldXfm_Force()` recomputes from the stale `mLocalXfm`, discarding the IK correction.

**Fix**: Back-compute `mLocalXfm` after `SetWorldXfm()` on ankle/hand effectors only:
```cpp
mEffector->SetWorldXfm(finalXfm);
#ifdef HX_NATIVE
if ((t == kEffectorTypeAnkle || t == kEffectorTypeHand)
    && mEffector->TransParent()) {
    Transform invParent;
    Invert(mEffector->TransParent()->WorldXfm(), invParent);
    Multiply(finalXfm, invParent, mEffector->mLocalXfm);
}
#endif
```

### Phase 5: Regression — Pelvis + IKElbow Back-Computation Corruption

The initial fix (Phase 4) applied mLocalXfm back-computation to **all** effector types and to IKElbow's thigh/shin bones. This caused a severe regression:

1. **Pelvis corruption**: Writing `mLocalXfm` on the pelvis (skeleton root) corrupted every child bone on dirty cascades, causing characters to appear disfigured with limbs merged at a central point.

2. **IKElbow thigh/shin corruption**: Writing `mLocalXfm` on structural bones (thigh, shin) via `Invert()` of their parent's WorldXfm produced garbage values (3.71e+05) when the parent transform was stale or near-singular. `FillBoneUniforms` detected these as garbage (>100000) and fell back to identity matrices, collapsing vertices to the mesh origin.

**Resolution**:
- **Removed** IKElbow back-computation entirely (thigh/shin `mLocalXfm` writes)
- **Gated** Poll back-computation to `kEffectorTypeAnkle || kEffectorTypeHand` only
- **Changed** `FastInvert()` to `Invert()` for correctness with non-orthogonal matrices

### Phase 6: Regression Tests

Added runtime assertions and telemetry-based gameplay tests to prevent future regressions:

**Runtime assertions** (`HamIKEffector::Poll()`):
- `FOOT INVERTED` — fires if toe Z > ankle Z + 2 (foot flipped through shin)
- `FOOT FLIPPED` — fires if ankle Z-axis points strongly upward (rotation 180°)

**Gameplay telemetry tests** (`test_gameplay_telemetry.cpp`, require `DC3_GAMEPLAY_TESTS=1`):
- `FootBonesFoundDuringGameplay` — ankle/toe bones discoverable during gameplay
- `NoInvertedFeetDuringGameplay` — toe never above ankle across all samples
- `FootZAxisNotFlippedDuringGameplay` — ankle rotation never flipped
- `NoFootInversionWarningsInOutput` — no runtime warnings in stderr
- `AnklesNotCollapsedDuringGameplay` — L/R ankles separated (>3 units) — catches merged characters
- `LegsNotCollapsedDuringGameplay` — pelvis-to-ankle distance >10 — catches collapsed legs
- `NoBoneGarbageDuringGameplay` — no ankle coordinate >10000 or NaN

**Static bone tests** (`test_foot_bone_invariants.cpp`):
- 10 tests validating rest-pose skeleton: bilateral separation, toes below ankles, no garbage, no collapsed legs, distinct bone positions, correct rotation orientation

All 7 gameplay tests and 10 static tests pass. Run with:
```bash
DC3_GAMEPLAY_TESTS=1 ctest -R "Foot|Ankle|Leg|Bone|Inverted" --output-on-failure
```

### Note on foot-sole clamp hack
The `TODO HACK` foot-sole clamp in the ankle case is still present. It was originally added to address perceived floor clipping. With the mLocalXfm fix, the original IK ground clamp should work correctly. The hack may now be unnecessary — consider removing it in a future pass.

## Next Steps
- Test on web build to confirm the fix applies there too (HX_NATIVE guards are shared)
- Consider removing the foot-sole clamp hack now that the root cause is fixed
- Compare with Xbox footage to verify feet match original game behavior

## Files Modified
- `src/system/hamobj/HamIKEffector.cpp` — mLocalXfm back-computation (gated to ankle/hand), foot inversion runtime assertions, removed IKElbow back-computation
- `src/system/rndobj/Trans.h` — added `friend class HamIKEffector`
- `native/src/telemetry/GameplayTelemetry.h` — foot bone telemetry fields (ankle/toe Z, separation, inversion flags)
- `native/src/telemetry/GameplayTelemetry.cpp` — bone position sampling during gameplay
- `native/tests/test_gameplay_telemetry.cpp` — 7 foot/bone gameplay regression tests
- `native/tests/test_foot_bone_invariants.cpp` — 10 static bone invariant tests
- `src/system/hamobj/HamRegulate.cpp` — Regulate, Poll improvements
- `src/system/char/CharIKHand.cpp` — Poll, IKElbow bug fixes
- `src/system/char/CharIKFoot.cpp` — DoFSM bug fixes
- `src/system/char/CharServoBone.cpp` — diagnostic cleanup
- `native/src/gfx/ImGuiBackend.cpp` — headless null window guard
- `native/src/platform/BoneSetup.cpp` — garbage WorldXfm detection + identity fallback

---

## Phase 7 — 2026-05-05: Empirical Bone-Position Data

After two incorrect AI agent investigations (one falsely concluded "platform divergence" without
data, one fabricated a render-time Z offset), the user requested actual measurements.

### Test relaxation reverted

`FeetNotBelowFloorDuringGameplay` was relaxed by an agent to threshold `-4.0` with a comment
claiming "matches Xbox behavior". This was unverified and wrong — the real-Xbox visual inspection
shows feet are NOT in floor. Test restored to `-2.0` with a comment warning future agents not to
relax it.

Result: test fails as expected — 608/609 gameplay samples have toe Z < -2.0 (worst -3.30).

### Real bone telemetry captured

Added one-shot `DC3_IK_DIAG FootGeom` log in `native/src/telemetry/GameplayTelemetry.cpp` that
dumps the ankle/toe/parent-chain world positions during gameplay (gated to first 5 samples on
`game_screen` + `state="playing"`).

**Rest pose** (initial frames before gameplay):
```
ankleW = (38.24, -15.00,  4.39)
toeW   = (38.33, -18.99,  0.01)   ← toe correctly on floor
ankleM.x = (-0.00, -0.00, -1.00)  ← ankle local X axis points straight DOWN in world
toeLocal = (4.37, 3.99, 0.00)     ← toe offset along ankle X by 4.37
```

**Gameplay** (frame ~9000, choose_mode → game_screen):
```
ankleW   = (30.91, -24.05,  0.84)   ← ankle 3.5 units LOWER than rest
toeW     = (32.45, -28.23, -3.12)   ← toe deep below floor
shinWZ   = 17.76
thighWZ  = 34.32
pelvisWZ = 33.74
abovePWZ = 0.11   parent name: "player0"   ← character root at floor level
```

### Key findings

1. **Toe-relative-to-ankle offset is preserved** between rest and gameplay (both ~4 units below).
   The skeleton structure is intact.
2. **Ankle is ~3.5 units lower** in gameplay than in rest pose. The same offset applies to the
   pelvis (rest ~37, gameplay ~33.7), so the *entire* skeleton is shifted down by ~3.5 units.
3. **Character root `player0` is at Z=0.11** during gameplay (effectively on floor). For the toe
   to land on floor, the skeleton would need to be ~3.5 units higher relative to player0.
4. **HamIKEffector::Poll matches Xbox at 99.9%** (only stack offset diffs, no logic divergence).
   `ApplyConstraints` is 100% normalized. The IK math is identical.
5. The bug source is therefore in the **input data** to IK (animation move target, neutral pose,
   constraint targets) or in the **character placement** — *not* in the IK code itself.

### Hypotheses to test next

- (A) The animation system writes the wrong Z to bone mLocalXfm (does animation update local
  pose? In rest pose, was player0 at Z \!= 0.11?).
- (B) The character's spawn Z is wrong (player0 should be ~3.5 higher to put feet on floor with
  the current bone offsets).
- (C) totalWeight in the IK constraint loop differs between native and Xbox (still unverified
  without real Xbox data).
- (D) A render-time bone matrix transform exists somewhere (audited `BoneSetup.cpp` — no Z
  offset there; the prior agent's claim was fabricated).

### Files modified in this phase

- `native/tests/test_gameplay_telemetry.cpp` — restored `FeetNotBelowFloorDuringGameplay`
  threshold to -2.0 with anti-relaxation warning.
- `native/src/telemetry/GameplayTelemetry.cpp` — added `DC3_IK_DIAG FootGeom` one-shot dump.

### Next steps

- Capture player0 Z and pelvis local Z **in rest pose** (currently only have gameplay).
  This will pinpoint whether (A) animation moves the pelvis down, or (B) the spawn is wrong.
- Continue Xenia investigation in parallel for ground truth, but local empirical work is more
  productive than chasing the loading hang.

### Phase 7 update: rest-pose vs gameplay confirms IK rotation bug

Captured both rest-pose and gameplay samples:

| metric | rest pose (loading) | gameplay |
|---|---|---|
| `player0.WorldXfm.v.z` | 0.00 | 0.11 |
| `bone_pelvis.WorldXfm.v.z` | 42.51 | 33.62 |
| `lAnkle.WorldXfm.v.z` | 4.39 | 0.84 |
| `lToe.WorldXfm.v.z` | 0.01 (on floor) | -3.12 (below floor) |
| `lAnkle.WorldXfm.m.x` | (0, 0, -1) | (0, 0, -1) |
| `lToe.LocalXfm.v` (relative to ankle) | (4.37, 3.99, 0) | (3.96, 4.45, 0) |

**Critical observation:** The ankle's local X axis points STRAIGHT DOWN in *both* rest pose and
gameplay. The toe is offset along that axis, so the toe is always ~4 units below the ankle.

This is the T-pose bone orientation (feet dangling, toe below ankle). In a real standing /
dancing pose, the ankle should be **rotated** so the foot is flat on the floor — toe forward,
not down. **The IK is failing to rotate the ankle.**

Hypothesis: `totalWeight` in `HamIKEffector::ApplyConstraints` returns 0 (or near 0) during
gameplay, so the IK-computed rotation never gets applied — the bone keeps its animation/T-pose
rotation. The foot stays pointed down, and the toe ends up below the floor when the ankle is
positioned near floor level.

### Pending action

Add `totalWeight` telemetry to `HamIKEffector::Poll` so we can observe it during gameplay.
This is the next concrete data point needed.

### Phase 7 update 2: ROOT CAUSE — `mConstraints` is empty for all polls

Added telemetry to `HamIKEffector::Poll()` capturing `totalWeight`, `mConstraints.size()`,
finger/effector names, and pre-IK world positions. During gameplay (filtered to
`main.milo` player character with ankle Z < 1.5):

```
DC3_IK_DIAG IkSnap[1]: effPath=bone_L-ankle.ikf (char/main/main.milo)
  fingerW.v=(32.44,-28.22,-3.12)    ← toe spot at Z=-3.12 (below floor)
  effW.v=(30.91,-24.05,0.84)        ← ankle at Z=0.84 (matches FootGeom)
  neutral.v=(32.44,-28.22,-3.12)    ← rest-pose pose-applied position
  totalWeight=0.000  constraintCount=0
```

**`mConstraints.size() == 0` for every poll** — ankle, pelvis, hand, all effectors. With no
constraint contributions, `ApplyConstraints` returns 0, and the IK falls through to:

```cpp
float remaining = 1.0f - totalWeight;   // = 1.0
q.v += remaining * effQ.v;               // q = current finger (toe spot) world position
```

The IK solver treats the *current animated toe spot position* as the target. So the foot
follows whatever the animation puts it at. There's no constraint pulling the foot toward
the floor.

In a **crouch dance pose**:
- Animation drops pelvis ~8 units (rest 42.51 → gameplay 33.62)
- Real-Xbox IK constraints pull the foot *back up* to floor → toe stays at Z≈0
- Native (empty constraints) → foot follows the pelvis drop → toe at Z=-3.12

This explains the entire bug: Xbox's IK has working `mConstraints` that anchor feet to
floor targets. Ours doesn't load any.

### `Load()` vs `Save()` asymmetry

```cpp
BEGIN_SAVES(HamIKEffector)
    SAVE_SUPERCLASS(Hmx::Object)        // Hmx::Object
    SAVE_SUPERCLASS(CharWeightable)
    bs << mEffector;
    bs << mMore;
    bs << mElbow;
    bs << mConstraints;
    ...

BEGIN_LOADS(HamIKEffector)
    LOAD_SUPERCLASS(CharPollable)       // CharPollable \!
    LOAD_SUPERCLASS(CharWeightable)
    d >> mEffector;
    d >> mMore;
    if (d.rev > 1) d >> mElbow;
    ...
    d >> mConstraints;
```

**Note:** `HamIKEffector::Load` and `Save` both match Xbox at 100%, so this asymmetry is
present in the original game too. Either:
- Both `Hmx::Object::Save` and `CharPollable::Load` are no-ops (just vtable, no bytes), so
  the asymmetry doesn't shift the byte stream.
- Or the .ikf files in DC3 simply don't define constraints in the static data, and the
  game populates them from another source we haven't found yet (move/animation system?).

### Next concrete steps

1. **Confirm whether the .ikf binary data contains constraint definitions.** Dump the
   binary bytes of a HamIKEffector saved object from a .milo file and check if the
   `mConstraints` array has entries.
2. **If yes:** find where our Load drops them. Check `ObjVector<Constraint>::operator>>`
   and walk the byte offsets.
3. **If no:** find where the game populates `mConstraints` at runtime. Search for
   `BustAMoveData`, `ClipPlayer`, `HamRegulate`, or move-system code that may
   inject constraint targets per move.

This is the true root cause — not "IK clamp threshold" or "render-time Z offset". The
empty `mConstraints` array deprives the IK solver of any target to pull toward.
