# Session 51 — Validation, Flow Analysis, GPU Captures (2026-03-11)

## Context

Before removing hacks, we needed to understand which ones are load-bearing. This session was pure investigation: screenshot sweeps, GPU capture comparisons, flow tracing, and runtime log validation.

## Screenshot A/B Sweeps

All runs used:

```bash
MILO_RENDER=1 MILO_HEADLESS=1 MILO_FIRST_SCREEN=main_screen MILO_MAX_FRAMES=260 \
MILO_SCREENSHOT_FRAMES=220 native/build/dc3-native
```

| Variant | Env | Non-black pixels | Interpretation |
|--------|-----|--------|----------------|
| Baseline | none | 613,272 | Current curated native behavior |
| Flow filter: menu only | `MILO_NATIVE_FLOW_FILTER=menu_only` | 809,491 | Blanket flow activation changes visible shell state |
| Flow filter: all | `MILO_NATIVE_FLOW_FILTER=all` | 809,759 | More flows = more overlays reappear |
| UI cam: original | `MILO_UI_CAM_MODE=original` | 613,269 | Camera override is not the blocker |
| UI cam: rotate_hack | `MILO_UI_CAM_MODE=rotate_hack` | 691,219 | Debug-only; severe left-shift |
| UI cam: z_hack | `MILO_UI_CAM_MODE=z_hack` | 641,063 | Debug-only; black band |

## GPU Capture Comparison

Trimmed GFXReconstruct captures for baseline vs `menu_only` flow filter:

- Both: 31 trimmed frames at 1280x720, 21 graphics pipelines
- Both: 11,904 `vkCmdDrawIndexed` + 512 `vkCmdDraw`
- Pipeline usage nearly identical — only a one-draw shift between two existing pipelines
- **Conclusion**: Flow filter variants don't change the renderer topology; visible differences are authored-state differences (visibility, alpha, flow-driven show/hide)

## Flow Tracing Results

Runtime trace of choose_mode_panel:
- Activates `Enter.flow`, `show_game_mode_icon.flow`, `highlight.flow`, `select.flow`, `play_enter_anim.flow`
- `play_enter_anim.flow` reaches a `FlowAnimate`, but `enable=0`
- `main_panel` activates `update_rank_number.flow`, `udpate_icon_state.flow`, `update_tier.flow` — these are control-flow/state-routing nodes, not direct visual anims

**Neither targets `turbo_shell.cam` or `right_hand.hnl` position.** Camera position x=-125 is the intended loaded value.

## PropAnim Target Analysis

| PropAnim | Targets | Notes |
|----------|---------|-------|
| `enter.anim` (13.6/13.6) | `tapeX.mat` | Does NOT target list position or camera |
| `camHold.anim` (0/0) | `camera1.cam` (x2 keys) | Holds camera1, not turbo_shell |
| `special_select.anim` (0/0) | Empty | |

## Runtime Log Validations

- `HamScreen::Enter()` forces controller mode on first enter; `HelpBarPanel::EnterControllerMode()` activates `controller_mode.flow`
- `FlowAnimate` nodes are reachable but authored state graphs branch through control-flow nodes and disabled anim nodes
- `HamNavList::PlayEnterAnim()` starts the ribbon/header enter animation, but native `Poll()` immediately kills it (AnimTask never self-cleans)

## Key Conclusion

The remaining native UI animation issue is not "assets/flows failed to load" — it's a three-part problem:

1. Authored flows run, but many are state-routing graphs rather than direct visual enters
2. Native lifecycle hacks override or cancel authored behavior before it settles
3. Visibility/alpha hacks make the frame look "mostly correct," hiding which transitions are genuinely missing
