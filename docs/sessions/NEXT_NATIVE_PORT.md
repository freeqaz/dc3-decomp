# Native Port: DTA Execution & UI Completion

**Priority**: High
**Status**: Planned
**Last Session**: 41 (UI layout fix, Transform::Multiply decomp bug)

## Context

The native port boots, enters the main loop, auto-navigates to `choose_mode_screen`, and supports interactive Up/Down/Confirm navigation. 47+ draw calls/frame, animation pipeline verified, HamUI two-pass rendering working.

**Session 39 identified DTA script execution as the critical blocker.** Menu population, animation cleanup, content population, and screen flow all depend on DTA scripts that native can't execute.

## Goals

### 1. DTA Script Execution (Critical Path)

The DTA system is the engine's scripting layer. Currently:
- `mSink` fallback dispatch doesn't fire (DTA `set_sink` action missing)
- Animation cleanup depends on DTA lifecycle events
- Content population requires DTA-driven providers
- Screen flow transitions rely on DTA handlers (`button_meanings`, `skip_selected`, etc.)

**Approach**: Audit which DTA commands are called during boot/menu flow. Categorize as:
- Already working (basic property get/set, screen transitions)
- Stubbable (Xbox-specific: Kinect, XDK services)
- Needs implementation (core engine: content providers, locale, animation lifecycle)

Focus on the minimal DTA subset needed for menu flow, not full interpreter.

### 2. Text Labels Visible

Current state: text meshes render but show token names (`mode_play`, `mode_fitness`) instead of localized strings. Two sub-problems:
- **Locale data loading**: `TheLocale.Init()` runs but locale .dta files aren't loaded from the archive
- **Font rendering**: Font meshes render correctly (Session 25 verified), just need real string content

### 3. Content Provider Population

`choose_mode_screen` shows 0 list items because no content provider returns data. The providers need:
- `HamProvider` / `PropertyEventProvider` factory stubs exist but return empty
- Content loading depends on DTA scripts configuring providers
- Minimum viable: hardcode a few mode entries for navigation testing

### 4. Visual Polish

- Remaining text clipping issues (wchar_t 2->4 byte edge cases)
- Skinned mesh rendering (bone transforms, vertex skinning shader) for character display
- Post-processing (bloom, color correction) for visual fidelity

## Decomp Bug Discovery Pattern

Each native port session has historically uncovered 1-2 real decomp bugs:
- Session 41: Transform::Multiply y/z swap (48% -> fixed)
- Session 40: ScrollDirection missing vertical mode (66.1% -> 100%)
- Session 30: ObjOwnerPtr::RefOwner() wrong member (decomp bug)
- Session 12: FlowAnimate::Load skip mAnim at rev>=3 (85.9% -> 90.7%)

This makes native port work a force multiplier for decomp quality.

## Build & Test

```bash
cd native/build && cmake --build . -j$(nproc)
MILO_RENDER=1 MILO_HEADLESS=1 MILO_SCREENSHOT_FRAMES=100,300,500 \
  MILO_SCREENSHOT_DIR=archive/screenshots/sessionNN ./dc3-native
```
