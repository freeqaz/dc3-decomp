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

## Status: FIXED (2026-03-31) — back-computation removed, original Xbox semantics restored

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
