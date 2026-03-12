# Session 56 — Alpha Floor & Panel Unload Investigation (2026-03-12)

Follow-up to [UI Animation Unwind Plan](../native/UI_ANIMATION_STATUS.md).

## Summary

Investigated two remaining native port issues: (1) what meshes hit the zero-alpha floor and why, (2) why `UnloadPanels()` hangs. Both confirmed as understood limitations with correct workarounds in place.

## Alpha Floor Tracing

Added `MILO_TRACE_ALPHA_FLOOR=1` env var gate to `Mesh_Wgpu.cpp` to log all meshes hitting the zero-alpha floor (`alpha < 0.01, blend=srcAlpha`).

**29 unique meshes** across 5 dirs:

| Dir | Meshes | Examples |
|-----|--------|---------|
| `background` (12) | Glow effects, frame backgrounds | `rt_frames_glow01-04`, `frames_glow_01-03`, `rt_frames_bg01` |
| `main_ribbon` (3) | Menu ribbon decorations | `mainMenuMiddleShading`, `arrow_shadow`, `arrow_01/02` |
| `main` (5) | XP gems, sparkles, text | `xp_gem01-03`, `text_new`, `star_sparkle_1` |
| `letterbox` (10) | Blackbars, light rays | `geo_blackbars_topBL/btmBL`, `geo_ray_vert_*` |
| `game_mode_icon` (3) | Mode selection icons | `icon`, `icon_sml`, `icon_drop_shadow` |

### Root Cause

All 29 meshes have `alpha=0` in their loaded .milo material data. On Xbox, DTA property-set scripts animate these alphas at runtime. These scripts are NOT Flow objects — they're direct DTA `set_prop` calls from screen enter/transition handlers.

Confirmed via flow activation tracing:
- **`background`, `main_ribbon`**: Zero flows exist for these dirs
- **`letterbox`**: Some flows exist but are filtered by `ShouldActivateNativeFlow()`; the flows that DO activate (`activate_letterbox.flow`) don't target material alpha
- **`game_mode_icon`**: `show_game_mode_icon.flow` is activated but doesn't animate material alpha

### Conclusion

The alpha floor at 0.20 is the correct permanent workaround. DTA script execution is architecturally infeasible on native (requires Xbox globals, save state, DLC state). The floor makes these decorative elements faintly visible instead of invisible.

## Panel Unload Investigation

Note: this section captured the working hypothesis from Session 56. Later
focused repro work superseded that diagnosis: the raw `autosave_warning`
teardown problem was reproduced below `UIPanel::Unload()` and fixed as a
two-step producer bug chain in `FlowNode::~FlowNode()` and `RndGroup::Replace()`
rather than a proven need for topological deletion.

Enabled `MILO_NATIVE_UNLOAD_PANELS=1` runtime gate (already existed in the code) to test whether `UnloadPanels()` can work.

### Findings (Session 56 Hypothesis)

The program **hangs** (not crashes) during `main_panel` unload:

```
UIPanel::Unload() → RELEASE(mDir) → ObjectDir::~ObjectDir() → DeleteObjects()
  → delete 'overlay_colorswitch.flow' (Flow)
    → delete 'p' (Flow subdir)
      → delete 'v1' (FlowSwitchCase)  ← HANGS HERE
```

The `FlowSwitchCase` destructor runs `~DataNodeObjTrack` → `~ObjPtr<Hmx::Object>` → `ObjRef::Release()`, which tries to unlink from the target object's ref ring. But the target object was already destroyed earlier in the same `DeleteObjects()` hash-table walk, leaving dangling `next`/`prev` pointers.

### Root Cause Hypothesis

`ObjectDir::DeleteObjects()` iterates via `ObjDirItr` (hash-table order) and deletes each object. Objects in the dir reference each other (e.g., FlowSwitchCase holds `ObjPtr` to another object in the same dir). Deleting A first leaves B's ref ring intact, but when B's ObjPtr to A tries to Release(), A's ring has dangling pointers.

This is the same family of issues documented in `docs/plans/dc3-native/object-lifetime-guard-root-cause.md`.

### Conclusion (Superseded)

The hide-instead-of-unload workaround is correct and should remain. A proper fix would require either:
1. **Two-pass deletion**: First null all ObjRef targets, then delete objects
2. **Topological ordering**: Delete objects in reverse dependency order
3. **Weak references**: ObjRef ring nodes check for validity before dereferencing

All three are significant architectural changes. The current workaround has zero visual impact (previous screen just stays hidden).

Later focused repro work changed the diagnosis further:

- deleting a flow child was shown to leave a null `ObjPtrVec` tombstone, and
  `FlowNode::~FlowNode()` spun on it
- after that fix, `RndGroup::Replace()` was shown to leave deleted members in
  its owner-control `ObjPtrList`, later crashing `ObjPtrList::clear()` /
  `Unlink()` during `RndGroup` teardown
- after both fixes, the raw repros `DeleteAutosaveWarningRawDir` and
  `DeleteAutosavingIconSubdirOnly` delete cleanly

## Changes

- Added `MILO_TRACE_ALPHA_FLOOR` tracing gate to `Mesh_Wgpu.cpp` (kept for future debugging)
- Added `#include <set>` and `#include <string>` to `Mesh_Wgpu.cpp` for tracing support
- Updated `UI_ANIMATION_STATUS.md` with investigation results

## Screenshots

Archived: `archive/screenshots/session56/`
