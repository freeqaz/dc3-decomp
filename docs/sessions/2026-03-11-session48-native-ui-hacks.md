# Session 48 — Initial Native UI Hacks (2026-03-11)

## Context

First pass at getting the native port's choose-mode / main-menu UI to render correctly. The authored UI animation system (Flows, PropAnims, panel enter/exit lifecycle) was not running on native, leaving text invisible, backgrounds missing, and overlays in wrong states.

## Changes

### 1. Label Alpha Force (`HamListRibbon.cpp`)

Added `#ifdef HX_NATIVE` block to force label style alpha to 1.0 when near-zero. Flow-driven alpha animation was not running on native, leaving all menu text invisible.

**Status**: Removed in Session 52 — enter animation / PropAnim forcing now drives alpha naturally.

### 2. Flow Activation + PropAnim Forcing (`PanelDir.cpp`)

Added `ShouldActivateNativeFlow()` filter to `PanelDir::Enter()`. On native, this blanket-activates matching Flows and force-jumps all `"enter"` `RndPropAnim`s to their end frame. This recovered final positioned state when enter animation wiring was missing.

**Status**: Still in place. High-priority removal target (Step 2).

### 3. Nested RndDir PropAnim Forcing (`PanelDir.cpp`)

Extended the PropAnim forcing to iterate into nested `RndDir` objects (like `game_mode_icon`) and force their enter PropAnims to end frame as well.

**Status**: Still in place. Removal depends on Step 2.

## Impact

These hacks made the choose-mode screen render recognizably — text became visible, background meshes appeared, and the general layout matched Xbox reference. However, they also start contradictory flows (show + hide, enter + exit) on the same panel, which is why they need to be unwound carefully.
