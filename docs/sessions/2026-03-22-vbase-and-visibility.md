# Virtual Base Fix + Character Visibility — Session 2026-03-22

## Commits

```
e4d5973aa native: force characters visible + skip venue screen meshes
97a957dd5 native: wire FrameCapture into render loop + white rectangle identified
88361058f fix: guard SameObject with HX_NATIVE to restore PPC match%
3ea7c6676 fix: remove debug fprintf from SameObject (broke PPC build)
532643bf8 native: fix camera shot cycling via PropKeys virtual base comparison
049d39723 docs: vbase pointer comparison audit — 21 sites, 1 HIGH risk
b3ac93840 more working stuff (concurrent agent — includes ReplaceRefsFrom fix)
```

## Camera Shot Cycling Fix

**Root cause**: PropKeys target comparison used raw `==` on `Hmx::Object*`. On Itanium ABI, the same HamDirector object accessed through different virtual base paths yields different pointer values (offset 0x780). `GetKeys(this, "shot")` returned null because `this` didn't match the stored target.

**Fix**: `SameObject()` helper using `dynamic_cast<const void*>` to compare most-derived addresses. Applied to `GetKeys`, `FindKeys`, `GetNumKeys` in PropAnim.cpp. Guarded with `#ifdef HX_NATIVE` to preserve PPC codegen (macro expanding to raw `==`).

**Verification**: Camera shots now cycle at beats 24, 49, 71, 96, 118, 141, 163.

## Character Visibility Fix

**Root cause**: Characters loaded from .milo with `mShowing = false`. On Xbox, DTA scripts call `set_showing 1`. On native, those DTA paths don't fire. `HamCharacter::Poll()` temporarily sets `Showing(true)` for animation but restores the original false value.

**Fix**: `HamDirector::VenueEnter()` now calls `SetShowing(true)` on all 4 characters under `#ifdef HX_NATIVE`.

**Verification**: Frame capture at 6500 shows 416+ draws including all character meshes (angel05, aubrey05, dci01 — skinned, textured, lit).

## White Rectangle

**Identified source**: `screen_image_*.mesh` — venue TV screens referencing Kinect camera render targets. Additive blend on empty texture = white rectangle. Added to MeshFilter skip list.

**Remaining**: Another white rectangle persists from a different source. Frame capture shows no `tex=0` draws with white color. Likely a render target texture initialized to white. Needs further investigation with NDC position filtering.

## Vbase Comparison Audit

21 sites audited across the codebase. 1 HIGH (`ReplaceRefsFrom` — fixed), 7 MEDIUM (monitoring), 13 LOW (safe). Full results in `docs/sessions/convergence/07-vbase-comparison-audit.md`.

## Skeleton Data

Confirmed loading correctly on native desktop. `mSkeletonBones` is non-null, all 13 `sSkeletonClips` populate. The `GetNeutralSkeleton` null guard only triggers on the web build (missing MEMFS assets). Skeleton blending is cosmetic (IK/pose quality), not related to character visibility.

## State After This Session

Characters render, camera cycles, song plays, venue is lit, move cards work. The game is visually functional during gameplay. Remaining work:
- White rectangle (another source beyond screen_image_*)
- Diagnostic logging cleanup
- Hack removal (gNativeVenueDir etc.)
- Full DTA menu flow test without DC3_SCREEN bypass
