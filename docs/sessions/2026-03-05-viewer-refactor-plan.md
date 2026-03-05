# Milo Viewer Refactor Plan

**Date:** 2026-03-05
**Status:** COMPLETE
**File:** `native/src/viewer/milo_viewer.cpp` (2459 lines → 450 lines)

## Result

The monolithic 2459-line `milo_viewer.cpp` was broken into 7 focused modules:

```
native/src/viewer/
├── milo_viewer.cpp          # 450 lines: main(), engine init, char setup, mode dispatch
├── ViewerArgs.h/cpp         # CLI config struct + parser
├── ViewerCamera.h/cpp       # OrbitCamera, auto-frame, mouse callbacks
├── ViewerAnimation.h/cpp    # AnimState, CharAnimState, BlinkState, PoseMeshesWithFacing
├── ViewerScene.h/cpp        # Scene lifecycle, mesh vis, env lookup, drawing
├── ViewerCapture.h/cpp      # Screenshot + video + interactive mode runners
└── ViewerPoseDump.h/cpp     # JSON pose dump

native/src/char/
└── CharTwistSolver.h/cpp    # Twist bone solvers
```

Total across all viewer files: ~2600 lines (slight growth from deduplication overhead + headers).
Largest single file: ViewerScene.cpp (506 lines) — scene loading is inherently complex.

## Phases Completed

| Phase | What | Lines moved | Key decisions |
|-------|------|-------------|---------------|
| 1. ViewerArgs | CLI config | ~180 | `ViewerConfig` struct + `Parse()` |
| 2. ViewerCamera | Orbit camera | ~160 | Global `gOrbitCam` (GLFW callback constraint) |
| 3. CharTwistSolver | Twist bones | ~166 | Static methods on utility class |
| 4a. Diagnostics | Cleanup | ~150 removed | Gated behind `--verbose` / `--dump-bones` |
| 4b. ViewerAnimation | Anim state | ~250 | `BlinkState` / `AnimState` / `CharAnimState` structs |
| 5. ViewerScene | Scene mgmt | ~300 | `ViewerScene` struct with lifecycle methods |
| 6. ViewerCapture | Mode dispatch | ~490 | `std::variant<Screenshot, Video, Interactive>` |
| 7. ViewerPoseDump | Pose dump | ~120 | Free function, JSON output |
| 8. Final cleanup | Trim main() | — | 450 lines total, linear top-to-bottom flow |

## Architecture Decisions

### What worked well
- **Composition over inheritance** — `BlinkState`, `AnimState`, `CharAnimState` are plain structs with methods. No vtables, no heap allocation, easy to debug.
- **`std::variant` mode dispatch** — exhaustive compile-time checking, value semantics, no virtual overhead.
- **Deduplication via `BlinkState::Weight()`** — replaced 3 copy-pasted blink weight calculations.
- **`CharAnimState::PollFace()`** — replaced 2 duplicated face servo blocks.
- **`TrackPelvis()` shared helper** — video + interactive pelvis smoothing in one place.

### Accepted compromises
1. **`gAnim` and `gOrbitCam` as globals** — GLFW callbacks need them. Correct fix is `glfwSetWindowUserPointer`, but not worth the churn for this refactor.
2. **Char setup stays in main()** — ~150 lines of imperative wiring (clips → driver → servo → visemes → face servo → eyes). Sequential by nature; extracting to a method would just move it, not simplify it.
3. **`_exit()` instead of clean shutdown** — GPU objects in globals crash during static destructor ordering. Stays visible in main().
4. **`poseDumpBones` parsed in `SelectMode()`** — the CSV parsing happens at mode selection time, not at config parse time. Slight coupling but avoids adding a `std::vector<std::string>` to ViewerConfig for a rarely-used feature.

## Non-Goals (unchanged)
- No ECS, plugin system, or abstraction layers
- No changes to engine code called by the viewer
- No `glfwSetWindowUserPointer` migration (future cleanup)
