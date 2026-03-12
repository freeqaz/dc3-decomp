# Session 53 — Step 2: PanelDir Flow/PropAnim Refactor (2026-03-12)

Step 2 of the [UI Animation Unwind Plan](../native/UI_ANIMATION_STATUS.md).

## Summary

Removed the PropAnim end-frame forcing entirely and narrowed Flow activation to only game-code-triggered flows. The normal `Flow::Enter()` auto-start path handles `startMode>0` flows correctly.

## A/B Test Results

| Variant | Draw calls | G mean | B mean | Visual |
|---------|-----------|--------|--------|--------|
| Baseline (all hacks) | 439 | 43.03 | 46.42 | Full teal bars |
| Skip flow activation | 451 | 17.63 | 21.15 | Dimmer teal |
| Skip PropAnim forcing | 439 | 43.03 | 46.43 | **Identical to baseline** |
| Skip both | 451 | 17.62 | 21.14 | Matches no-flow |
| Only activate startMode=0 | 439 | — | — | **Identical to baseline** |
| Skip startMode=0 only | 451 | — | — | Matches no-flow |

## Key Finding: Flow::Enter() Auto-Start

`Flow::Enter()` (called via `RndDir::Enter()` → `mEnters`/`mPolls` traversal) already auto-starts flows with `mStartMode > 0`:
- `mStartMode == 1`: immediate execution
- `mStartMode == 2`: queued via `TheFlowMgr->QueueCommand()`

Our blanket `Activate()` call was redundantly double-activating these flows. The startMode>0 flows handle themselves.

The visual difference comes entirely from `startMode=0` (game-code-triggered) flows. These are normally fired by DTA enter scripts on Xbox. Our blanket activation of these is still necessary.

## Changes

### 1. Removed PropAnim End-Frame Forcing (`PanelDir.cpp`)

Deleted the entire block that iterated `RndPropAnim` objects (both top-level and nested `RndDir`s) and called `SetFrame(endFrame, 1.0f)` on anything named "enter". This was a no-op after Session 52's timer-based animation fix — the enter animations now play through and reach their end frames naturally.

### 2. Narrowed Flow Activation to startMode=0 Only (`PanelDir.cpp`)

Changed the `ObjDirItr<Flow>` loop to skip flows with `GetStartMode() > 0` (auto-starting flows). Only `startMode == 0` (game-code-triggered) flows still get blanket-activated.

### 3. Added `GetStartMode()` Getter (`Flow.h`)

Added `int GetStartMode() const` to expose the protected `mStartMode` member for the startMode check in `PanelDir::Enter()`.

## What's Left of the Native Flow Hack

The `ShouldActivateNativeFlow()` filter + blanket activation loop remain, but now only fire for `startMode=0` flows. These are flows that Xbox's DTA enter scripts trigger — `show_game_mode_icon.flow`, `update_rank_number.flow`, `activate_letterbox.flow`, etc.

To fully remove this hack, we'd need to either:
1. Implement the DTA `ui_enter`/`ui_enter_forward` handlers that trigger these flows on Xbox
2. Or find another authored mechanism that starts them

## Verification

- Frame 220: 439 draw calls, pixel-identical to baseline
- Enter animation frames 3→10→50 all render correctly
- PPC build clean (all changes inside `#ifdef HX_NATIVE`)

## Screenshots

Archived: `archive/screenshots/session53/`
