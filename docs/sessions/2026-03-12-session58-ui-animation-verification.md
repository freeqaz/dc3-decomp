# Session 58: UI Animation Pipeline Verification

**Date**: 2026-03-12
**Goal**: Investigate why menu animations weren't loading; verify the full Flow→AnimTask pipeline.

## Background

After sessions 52–56 removed rendering hacks (auto-prelit, zero-alpha floor `#if 0`'d), the question remained: are the Flow-driven animations actually running? The previous investigation (session 56) traced the chain partway but context ran out before finding the exact break point.

## Investigation Method

Added unconditional `printf` diagnostics at each stage of the animation chain, building from the outer layer inward:

1. **Flow::Execute** — already had logging; confirmed entry (e.g., `enter.flow` with 13 children)
2. **Flow::Activate** — added logging to both ProxyFile branches
3. **FlowQueueable::Activate** — logged mRunningNodes count, interrupt mode, ActivateTrigger return
4. **Flow::ActivateTrigger** — made logging unconditional (was gated behind env var)
5. **FlowNode::ActivateChild** — logged each child name/class, activation result
6. **FlowAnimate::Activate** — logged anim pointer, enable, period, immediateRelease, runningNodes
7. **FlowAnimate::Execute** — logged state, isRunning, anim/task pointers
8. **RndAnimatable::Animate** — logged rate, units, start/end frames, delay, returned task pointer
9. **TaskMgr::Poll** — logged all four timeline times (seconds, beats, uiSeconds, tutorialSeconds)

Each layer was verified before adding the next, confirming the chain was unbroken.

## Findings

### The full pipeline works end-to-end

```
PanelDir::Enter()
  → Flow::Enter() (startMode>0 auto-start) or ShouldActivateNativeFlow() (startMode=0)
    → Flow::Execute()
      → Flow::Activate()
        → FlowQueueable::Activate(listener)
          → Flow::ActivateTrigger()
            → FlowNode::ActivateChild(child)  [for each mChildNode]
              → FlowAnimate::Activate()
                → TheFlowMgr->QueueCommand(this, kQueue)  [queues for next poll]
                  → FlowManager::Poll() [called every frame from App.cpp:779]
                    → FlowAnimate::Execute(kQueue)
                      → mAnim->Animate(blend, wait, delay, rate, start, end, ...)
                        → AnimTask created
                        → TheTaskMgr.Start(task, kTaskUISeconds, delay)
                          → TaskMgr::Poll() [called every frame from App.cpp:776]
                            → TaskTimeline::Poll() → AnimTask::Poll(time)
                              → anim->SetFrame(frame, blend)
                                → PropAnim drives material alpha/color
```

### Key data points from runtime

- **All UI animations use `rate=2` (`k30_fps_ui`) → `kTaskUISeconds` timeline**
- **kTaskUISeconds IS advancing**: 0.000 at frame 0 → 0.643 at frame 300 → 1.169 at frame 1800
  - Set by `UIManager::Poll()` → `TheTaskMgr.SetUISeconds(mTimer.SplitMs() * 0.001f, false)` at UI.cpp:537
- **AnimTasks are created with valid parameters**: e.g., `helpbar.anim` start=0 end=15 fpu=30, `beam.anim` start=0 end=150 fpu=30
- **FlowAnimate nodes**: Most have `enable=1`, `immRelease=0`, `mRunningNodes=0` → take the queue path (return true, stay running)
- **`mAnim->Animate()` returns non-null tasks** for all enabled animations

### Visual confirmation

Screenshots at frames 200/300/400/500 show the enter animation working:

| Frame | Screen | Visual State |
|-------|--------|-------------|
| 200 | `title_screen` | DC3 logo, "HOW TO NAVIGATE" overlay |
| 300 | transition | Menu elements fading in, "BROWSE SONGS" appearing |
| 400 | `main_screen` | Full menu rendered: DANCE, STORY, FITNESS, etc. |
| 500 | `main_screen` | Steady state, all items visible |

Screenshots: `archive/screenshots/2026-03-12-ui-animations/`

## What Changed vs Previous Sessions

The animation pipeline was already working by the time this investigation started. The key enabling changes were spread across sessions 52–55:

1. **Session 52**: Timer-based enter animation, transition waits re-enabled
2. **Session 53**: PropAnim forcing removed, Flow activation narrowed to `startMode=0` only
3. **Session 54**: Overlay filter removed (flows now animate overlays correctly)
4. **Session 55**: PanelDir dir-hide removed (redundant with MeshFilter)

The `ShouldActivateNativeFlow()` filter in `PanelDir::Enter()` is the critical mechanism — it activates `startMode=0` flows that would normally be triggered by DTA enter scripts on Xbox.

## Cleanup Done

Removed all diagnostic `printf` statements added during the investigation:
- `Flow.cpp`: Removed logging from Activate(), Execute(), ActivateTrigger()
- `FlowQueueable.cpp`: Removed logging from Activate()
- `FlowNode.cpp`: Removed logging from ActivateChild()
- `FlowAnimate.cpp`: Removed logging from Activate(), Execute()
- `Anim.cpp`: Removed logging from Animate() overloads
- `Task.cpp`: Removed logging from TaskMgr::Poll()
- `PanelDir.cpp`: Stripped diagnostic iteration (UITrigger/EventTrigger/MatAnim/TransAnim/PropAnim counting), kept functional Flow activation logic
- `DirLoader.cpp`: Removed CreateObj type dump
- `EventTrigger.cpp`: Removed OnTrigger() and TriggerSelf() logging

Also removed diagnostic-only includes from PanelDir.cpp (`rndobj/MatAnim.h`, `rndobj/PropAnim.h`, `rndobj/TransAnim.h`).

## Architecture Summary

DC3 UI panels use **Flow-driven PropAnims** for all visual animations (not UITriggers/EventTriggers — zero of those exist in .milo files). The Flow system is a visual scripting graph where:

- **Flow** objects contain child **FlowNode** trees (FlowAnimate, FlowSequence, FlowRun, FlowSetProperty, etc.)
- **FlowAnimate** nodes reference **RndAnimatable** objects (PropAnims) and create **AnimTasks**
- **AnimTasks** run on the **kTaskUISeconds** timeline, advanced by **UIManager::Poll()**
- **PropAnims** drive material properties (color, alpha, transform) via keyframe interpolation

Two activation paths:
1. `startMode > 0` (auto-start): `Flow::Enter()` → `FlowQueueable::Execute(kQueue)` or `TheFlowMgr->QueueCommand()`
2. `startMode == 0` (game-code triggered): `ShouldActivateNativeFlow()` in `PanelDir::Enter()` activates on native; on Xbox, DTA enter scripts call `{flow.flow activate}`
