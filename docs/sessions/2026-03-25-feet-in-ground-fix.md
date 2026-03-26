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

## Status: FIXED — feet correctly oriented on native

### Phase 4: Root Cause — Missing mLocalXfm Back-Computation

The actual symptom was **inverted feet** (ankle on ground, foot mesh flipped 180° upward through shin), not floor clipping. This was the same bug pattern as the forearm twist fix (see `docs/sessions/2026-03-24-forearm-twist-fix.md`).

**Root cause**: `HamIKEffector::Poll()` calls `SetWorldXfm()` on ankle, shin, and thigh bones to apply IK corrections. `SetWorldXfm()` sets `mDirty = false` on the bone itself but cascades `SetDirty()` to children. If a **later pollable** (e.g., another HamIKEffector, CharUpperTwist) dirties a parent bone in the IK chain, `WorldXfm_Force()` recomputes the bone's world transform from the stale `mLocalXfm` (the raw animation pose), discarding the IK correction. The animation-pose local rotation composed with the IK-modified parent chain produces an inverted foot.

**Fix**: Back-compute `mLocalXfm` after every `SetWorldXfm()` call in the IK chain:
```cpp
bone->SetWorldXfm(xfm);
#ifdef HX_NATIVE
if (bone->TransParent()) {
    Transform invParent;
    Invert(bone->TransParent()->WorldXfm(), invParent);
    Multiply(xfm, invParent, bone->mLocalXfm);
}
#endif
```

Applied to three locations:
1. `IKElbow()` — grandparent (thigh) after `SetWorldXfm`
2. `IKElbow()` — parent (shin) after `SetWorldXfm`
3. `Poll()` — effector (ankle) after final `SetWorldXfm`

Also added `friend class HamIKEffector;` to `RndTransformable` for direct `mLocalXfm` access (same pattern as `CharForeTwist`/`CharUpperTwist`).

Confirmed via headless screenshots: feet correctly oriented across multiple dance frames on the throneroom venue.

### Note on foot-sole clamp hack
The `TODO HACK` foot-sole clamp in the ankle case is still present. It was originally added to address perceived floor clipping, but the real issue was the inverted feet. With the mLocalXfm fix, the original IK ground clamp should work correctly. The hack may now be unnecessary — consider removing it in a future pass.

## Next Steps
- Test on web build to confirm the fix applies there too (HX_NATIVE guards are shared)
- Consider removing the foot-sole clamp hack now that the root cause is fixed
- Compare with Xbox footage to verify feet match original game behavior

## Files Modified
- `src/system/hamobj/HamIKEffector.cpp` — GetGroundHeight, ComputeHandPullAndQuat, foot-sole clamp, **mLocalXfm back-computation** (root fix)
- `src/system/rndobj/Trans.h` — added `friend class HamIKEffector`
- `src/system/hamobj/HamRegulate.cpp` — Regulate, Poll improvements
- `src/system/char/CharIKHand.cpp` — Poll, IKElbow bug fixes
- `src/system/char/CharIKFoot.cpp` — DoFSM bug fixes
- `src/system/char/CharServoBone.cpp` — diagnostic cleanup
- `native/src/gfx/ImGuiBackend.cpp` — headless null window guard
- `native/src/platform/BoneSetup.cpp` — diagnostic cleanup
- `native/src/platform/Mesh_Wgpu.cpp` — diagnostic cleanup
- `native/src/platform/MeshGpuCache.cpp` — diagnostic cleanup
