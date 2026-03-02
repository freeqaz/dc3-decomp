# Session: CharMeshHide Investigation & Native Viewer Mesh Visibility

**Date**: 2026-03-01
**Focus**: Character mesh overlap artifacts, CharMeshHide system analysis, native viewer fixes

## Problem

Characters rendered in the native Milo Viewer had two issues:
1. **Z-fighting artifacts** — overlapping meshes (combined body + split pieces) fighting for the same pixels
2. **Missing limbs** — incorrect visibility logic hiding arm/leg meshes

## Root Cause Analysis

### Mesh Structure in Character .milo Files

Each character outfit has multiple overlapping mesh layers:

| Mesh | Bones | Material | Default Showing |
|------|-------|----------|----------------|
| `aubrey01.mesh` (combined) | 40 | `aubrey01_skin.mat` | true |
| `aubrey01.1.mesh` (split: skin) | 30 | `aubrey01_skin.mat` | true |
| `aubrey01.2.mesh` (split: accessories) | 33 | `aubrey01_accessories.mat` | true |
| `aubrey01.3.mesh` (split: outfit) | 21 | `aubrey01_outfit.mat` | true |
| `aubrey01_lod.mesh` (combined LOD) | 40 | `aubrey01_outfit_lod.mat` | true |
| `aubrey01_lod.1.mesh` (split LOD) | 36 | `aubrey01_outfit_lod.mat` | true |
| `left_arm.mesh` | 28 | `aubrey01_skin.mat` | **false** |
| `right_arm.mesh` | 26 | `aubrey01_outfit.mat` | **false** |
| `left_leg.mesh` | 7 | `aubrey01_accessories.mat` | **false** |
| `right_leg.mesh` | 7 | `aubrey01_skin.mat` | **false** |

The combined mesh covers the full body. The splits cover specific parts (skin, outfit, accessories).
Arm/leg meshes are outfit-specific alternatives for when the combined mesh is hidden.

Both the combined mesh and all splits default to `showing=true`, causing z-fighting where they overlap.

### How DC3 vs RB3 Handle This

**RB3** has explicit C++ methods:
- `CharMeshHide::HideAll(ObjPtrList<CharMeshHide>&, int)` — ORs all flags, calls HideDraws
- `CharMeshHide::HideDraws(int)` — per-drawable: `show = (mShow != !(flags & mFlags))`
- Called from `BandCharacter::SyncObjects()` with context flags (e.g. `0x2000` for vignette)

**DC3** does NOT have these symbols in its binary:
- `HideAll` and `HideDraws` are completely absent from `config/373307D9/symbols.txt`
- `CharMeshHide::Handle` IS in the binary (as a virtual method) and is stubbed in `link_glue.cpp`
- DC3 likely evaluates CharMeshHide through the data-driven Handle/script system
- The `HamCharacter` class (DC3's BandCharacter equivalent) does not call CharMeshHide

### Pinkish Knee Patches (Not a Bug)

Systematic elimination (disabling lighting, specular, rim, rendering single mesh with texture only)
proved that faint pinkish patches at inner knees are **baked subsurface scattering (SSS) in the
diffuse texture**. The Xbox skin shader would blend this differently. Not fixable without a
dedicated skin shader.

## Viewer Fix: Combined/Split Mesh Overlap Resolver

Added heuristic in `milo_viewer.cpp` that:
1. Scans all meshes in the scene
2. For any mesh `foo.mesh`, checks if `foo.1.mesh` exists (naming convention for splits)
3. If splits exist: hides the combined mesh, force-shows arm/leg meshes
4. Skips meshes that ARE splits (contain `.N.mesh` pattern)

This correctly handles all 4 tested characters (aubrey01, aubrey04, taye01, dare04).

## Decomp Status

### CharMeshHide Files
- `src/system/char/CharMeshHide.h` — Complete (serialization, props, factory)
- `src/system/char/CharMeshHide.cpp` — Has Load/Save/Copy/Init/Handle, but Handle only
  delegates to `Hmx::Object` superclass (no custom handlers). Need to verify with Ghidra
  whether the original Handle had additional message handlers (e.g. "hide_draws").

### Definitive Answer: CharMeshHide Is Dead Code in DC3

**CharMeshHide is registered but never instantiated.** Searched all 5,399 .milo files in the
DC3 asset library — zero contain CharMeshHide objects. The class exists in the binary (factory
registration, Load/Save/PropSync/Handle), but no game data uses it.

**DC3's Handle is the trivial superclass delegator.** Ghidra decompilation at 0x827026F0 shows
`BEGIN_HANDLERS(CharMeshHide) HANDLE_SUPERCLASS(Hmx::Object) END_HANDLERS` with no custom
handlers. ICF-merged with `UIListWidget::Handle` (identical code). Our decomp is correct.

**DC3 removed the RB3 visibility system entirely:**
- RB3: `BandCharacter::SyncObjects()` → `CharMeshHide::HideAll()` → `HideDraws()` → `SetShowing()`
- DC3: `HamCharacter::SyncObjects()` → no CharMeshHide reference. No `HideAll`/`HideDraws` symbols.
- DC3 DTA `sync_objects` handler: just `{$this cache_vo_bank}`, no mesh hiding.
- DC3 does not define `CHAR_HIDE_FLAGS` (RB3 has `kHideLongCoat`, `kHideShortSleeve`, etc.)
- DC3 does not have `char_mesh_hide` script function (RB1/RB2/LRB do).

**Mesh visibility in DC3 is pre-baked in .milo data.** The `mShowing` property on each mesh is
serialized in `RndDrawable::Load()`. The gen/ outfit .milo files have the combined body mesh
AND split meshes both showing=true (overlap), which suggests these are intermediate build
artifacts. In the actual game runtime, `FileMerger` loads the outfit .milo, and the meshes
arrive with their serialized `mShowing` state. The game's camera shot system (`CamShot::DoHide`,
`HamDirector::HideBackups`) handles whole-character visibility, not per-mesh outfit control.

**No decomp bugs or missing code.** The decomp is 1:1 with the original binary. CharMeshHide
is a vestigial class carried over from the shared Milo engine (used in RB3) but stripped of
all runtime functionality in DC3.

### Cleanup Actions Taken
- Removed stale `link_glue.cpp` ALTERNATENAME for `CharMeshHide::Handle` (our `BEGIN_HANDLERS`
  macro already generates it correctly — the link_glue entry was redundant)
- Removed stale entry from `docs/link/UNIMPLEMENTED_STUBS.md`
- Removed `#ifdef HX_NATIVE` HideDraws/HideAll stubs from CharMeshHide.h/.cpp (dead code —
  zero .milo files use CharMeshHide objects, so there's nothing to evaluate)
- Removed CharMeshHide include and RB3-style HideAll iteration from viewer (was a no-op)
- Kept the name-based combined/split mesh resolver as the viewer's visibility heuristic

## Files Modified
- `native/src/viewer/milo_viewer.cpp` — Combined/split mesh resolver heuristic
- `src/system/char/CharMeshHide.h` — No changes (reverted HideDraws/HideAll — dead code)
- `src/system/char/CharMeshHide.cpp` — No changes (reverted HideDraws/HideAll — dead code)
- `src/link_glue.cpp` — Removed stale CharMeshHide::Handle ALTERNATENAME
- `docs/link/UNIMPLEMENTED_STUBS.md` — Removed CharMeshHide::Handle entry (implemented via macro)
- `native/src/platform/Mesh_Wgpu.cpp` — Cleaned up debug logging from previous session
