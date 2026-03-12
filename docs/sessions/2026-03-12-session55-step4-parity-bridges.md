# Session 55 — Step 4: Parity Bridge Review (2026-03-12)

Step 4 of the [UI Animation Unwind Plan](../native/UI_ANIMATION_STATUS.md).

## Summary

Audited all 13 `#ifdef HX_NATIVE` parity bridge blocks. Removed the PanelDir Kinect/tutorial dir-hiding block (redundant with MeshFilter). All other bridges are either boot-critical infrastructure or still visually load-bearing.

## A/B Test Results

| Bridge | Visual effect when removed | Action |
|--------|---------------------------|--------|
| PanelDir dir-hide (NewSkeletonDir, tutorials, etc.) | **No regression** — MeshFilter catches meshes | **Removed** |
| Tutorial panel skip (UIScreen + UIPanel) | Tutorial panels draw, displace helpbar text | **Keep** |

## Classification of Remaining Bridges

### Boot-critical (cannot A/B test — would break boot)

| File | Hack | Why it's needed |
|------|------|-----------------|
| `UI.cpp` | Auto-advance stuck boot/tutorial screens | DTA handlers fail on native, screens would hang forever |
| `HamScreen.cpp` | Force controller mode on first enter | Without it, helpbar/flow state is wrong for controller navigation |
| `ShellInput.cpp` | Skip Kinect init, short-circuit Poll | Kinect infrastructure (SkeletonIdentifier, DepthBuffer) doesn't exist |
| `ShellInput.cpp` | Never exit controller mode | No way to re-enter after exiting (no gesture input) |
| `ShellInput.cpp` | Drive hide-mic/voice messages | Prevents voice-tip overlays from latching |
| `UIPanel.cpp` | Synchronous panel loading | LoadMgr queue backs up without UnloadPanels |
| `UIPanel.cpp` | Force-finish panels without loader | DLC/network/save state doesn't exist on native |
| `UIScreen.cpp` | Always load all panels | mLoadRefs stays >0 from skipped UnloadPanels |
| `UIScreen.cpp` | Hide previous screen instead of unload | ObjRef lifecycle SIGSEGV during bulk deletion |

### Still visually load-bearing

| File | Hack | Why it's needed |
|------|------|-----------------|
| `UIScreen.cpp` | Skip tutorial panels on enter | Kinect gesture UI displaces controller-mode content |
| `UIPanel.cpp` | Block tutorial panel Enter() | DTA bypass of UIScreen skip |

### Architectural (not a hack)

| File | Hack | Status |
|------|------|--------|
| `UI.cpp` | Set `mSink = current screen` | Input routing — likely permanent |
| `UI.cpp` | Single-pass camera/environment selection | Architectural bridge |

## Changes

### Removed: PanelDir Kinect/Tutorial Dir-Hide Block (`PanelDir.cpp`)

Deleted the block that hid `NewSkeletonDir`, `silhouette_guy_*`, and dirs matching `tutorial`/`gesture`/`nav_tut`/`spotlight`. This was redundant with MeshFilter which already catches the individual meshes by name within those dirs.

### Also cleaned up: Removed diagnostic printf in UIScreen/UIPanel tutorial skips

Stripped `printf` calls from the tutorial skip blocks — they've been stable for many sessions and the log noise isn't useful.

## Conclusion

The unwind plan is complete. What remains is infrastructure that native requires to boot and navigate:

- **Controller mode forcing** — necessary because Kinect gesture input doesn't exist
- **Boot flow auto-advance** — necessary because DTA screen handlers depend on Xbox globals
- **Panel loading shortcuts** — necessary because ObjRef lifecycle issues prevent proper unloading
- **Tutorial suppression** — necessary because gesture tutorial UI conflicts with controller mode

These are not "hacks to remove" — they're the native platform adaptation layer. They should be treated as permanent infrastructure unless the native port gains Kinect emulation, DTA script execution, or ObjRef lifecycle fixes.

## Verification

- Frame 220: 439 draw calls, matches baseline
- PPC build clean

## Screenshots

Archived: `archive/screenshots/session55/`
