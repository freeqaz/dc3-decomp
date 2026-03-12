# UI Animation System — Native Port Status

## Current State (Session 51)

Menu text is visible and positioned via the scene camera (`turbo_shell.cam`). Background mesh rendering now works (439 draw calls, up from 0). The text is roughly centered rather than clearly on the right as in the Xbox reference — this is because `turbo_shell.cam` at x=-125 only provides a small offset from center. Investigation confirmed no Flow targets the camera position; x=-125 IS the correct loaded value from the .milo data.

**New finding (2026-03-11 late)**: the remaining native UI animation problem is not that DTA/Flow loading is broken globally. A runtime trace with `MILO_DEBUG_PANEL_FLOWS=1` and `MILO_DEBUG_FLOW_ACTIVATE=1` shows that choose-mode flows and `FlowAnimate` nodes do activate on native. The main blockers are now:

1. **Native lifecycle hacks still override the authored behavior**
   - `PanelDir::Enter()` blanket-activates flows and force-jumps `"enter"` `RndPropAnim`s to end frame
   - `HamNavList` starts ribbon/header enter animation, then native-only poll code clears `TestEntering`, calls `StopAnimation()`, and resets the frame

2. **Several choose-mode flows are stateful control-flow graphs, not simple visual enters**
   - `update_rank_number.flow`, `udpate_icon_state.flow`, and `update_tier.flow` activate, but route through `FlowSwitch` / `FlowCommand` / `FlowRun`
   - `show_game_mode_icon.flow` reaches `play_enter_anim.flow`, but that `FlowAnimate` is configured with `enable=0`

3. **The old hacks are now distorting the picture**
   - blanket flow activation still starts contradictory show/hide flows on the same panel
   - forced PropAnim end-frame logic skips real motion
   - alpha/visibility fallbacks still mask missing flow-driven state

**Xbox reference**: `archive/screenshots/references/dc3_main_menu.jpg`
**Current screenshots**: `archive/screenshots/session49/`

## Bottom Line

The remaining native UI animation issue is no longer “assets/flows failed to load.”

It is now a three-part problem:

1. authored flows do run, but many of them are state-routing graphs rather than direct visual enters
2. native lifecycle hacks still override or cancel the authored behavior before it can settle naturally
3. visibility/alpha hacks still make the frame look “mostly correct,” which hides which state transitions are genuinely missing

That means the path to 100% is not “remove all hacks and hope.” The path is:

1. fix animation lifetime / completion first
2. stop canceling real enter animations
3. stop blanket-starting contradictory flows
4. only then peel back alpha / visibility / suppression hacks one by one

If this order is reversed, native will regress into blocked transitions, stuck controller-mode overlays, or invisible text/background layers and the resulting frame will be harder to reason about.

## Root-Cause Chain

The current failure chain is:

1. `HamNavList::PlayEnterAnim()` starts a real `RndAnimatable::Animate()` task for ribbon/header enter motion.
2. On native, that task does not naturally self-delete in the same way the Xbox lifecycle expects.
3. To avoid permanent “still animating” state, native poll/input paths clear `TestEntering`, call `StopAnimation()`, and reset the frame.
4. Because animations are now being cancelled rather than completed, `UIManager` also skips transition waits.
5. Because panel enter state is now incomplete, `PanelDir::Enter()` blanket-starts flows and force-jumps `"enter"` PropAnims to their end frame.
6. Because state is still incomplete after that, text/mesh visibility hacks keep the frame readable.

This is why the hacks stack on top of each other. The deeper fix is the animation lifetime/completion path, not the renderer.

## What 100% Looks Like

For native choose-mode / main-menu parity, “working” means:

- `HamNavList` enter motion is driven by authored animation lifetime, not by `StopAnimation()` + frame reset
- panel enter/exit waits complete because animations genuinely end, not because native skips the checks
- `PanelDir::Enter()` no longer needs blanket flow activation or forced end-frame PropAnim positioning
- helpbar/controller-mode/speech overlays reach the correct state through authored flow/property changes
- text and decorative UI meshes are visible because their real alpha/state is driven, not because native forces visibility
- the current scene-camera selection stays in place, because it reflects authored panel camera usage rather than a debug crutch

### Session 49 Fixes

1. **Scene camera selection** (`HamNavList.cpp`) — HamNavList::DrawShowing now uses the PanelDir's scene camera (`turbo_shell.cam`) instead of the default UI camera (`[ui.cam]`).

2. **Scene uniform camera change detection** (`Rnd_Wgpu.cpp`) — `EnsureSceneUniformsCurrent()` now checks all 3 position components (X, Y, Z) instead of just Y. This was a general rendering bug: when cameras with different X/Z positions were swapped, the scene uniforms weren't being re-uploaded. **Fixed 0 → 439 mesh draw calls.**

3. **Removed `entering=true` hack** (`HamNavList.cpp`) — Main ribbon correctly passes `entering=false`, stopping beam/chevron overlay meshes. The widget system (DrawWidgets) renders the actual menu text.

### Session 48 Fixes (still in place)

1. **Label alpha force** (`HamListRibbon.cpp`) — Force alpha to 1.0 on native
2. **Flow activation + PropAnim forcing** (`PanelDir.cpp`) — Activate Flows and force enter PropAnims to end frame
3. **Nested RndDir PropAnim forcing** (`PanelDir.cpp`) — Iterate into nested RndDir objects (like `game_mode_icon`) to force their enter PropAnims

## Key Findings: Camera System

### Camera Inventory (choose_mode_panel)

| Camera | World Position | FOV (rad) | Purpose |
|--------|---------------|-----------|---------|
| `[ui.cam]` | (0, -768, 0) | 0.602 | Default UI camera (centered) |
| `turbo_shell.cam` | (-125, -663.5, -63) | 0.602 | Scene camera (PanelDir.mCam) |
| `camera1.cam` | (32, -111.5, -63) | 2.127 | Close-up effect camera |

### Camera Position vs Menu Position

- `turbo_shell.cam` at x=-125, HamNavList at x=-107.55 → menu appears 17.45 units right of camera center
- At distance 655 with FOV 0.602: visible half-width ≈ 361 units
- 17.45 / 361 = 4.8% right of center → screen pixel ~671 of 1280 (52.4% from left)
- Xbox reference shows menu at ~65% from left → would need ~150 more units of offset

### Flow Investigation Results

Traced all FlowSetProperty and FlowAnimate activations during boot. **Neither targets `turbo_shell.cam` or `right_hand.hnl` position.** The camera position x=-125 is the intended loaded value.

FlowSetProperty targets: toaster, alert, motd, material alphas, letterbox, say_xbox
FlowAnimate targets: buffer_fadeout, beam, letterbox, sound, shield_mic, say_xbox, silhouette, leftside_scroll, diagonal, fade

### Scene Uniform Bug (Fixed)

`EnsureSceneUniformsCurrent()` in `Rnd_Wgpu.cpp` was only checking `camPosY` to detect camera position changes. When `PanelDir::DrawShowing()` selected `turbo_shell.cam` (at x=-125, z=-63) replacing `[ui.cam]` (at x=0, z=0), the pointer identity check detected the change correctly for the FIRST switch. But any subsequent same-pointer position changes in X/Z were missed.

This bug also prevented mesh rendering in scenarios where the camera pointer was the same but the position changed between panels. Fix: check all 3 position components.

## Remaining Investigation

### Text Position (center vs right)

The menu text appears centered because `turbo_shell.cam` at x=-125 only shifts the view slightly right. On Xbox, the same camera position would produce the same result. The Xbox reference may appear "more right" because:
1. Background meshes (teal bars, logo, diagonal pattern) create visual framing that makes the text look further right in context
2. The Xbox reference might be in a different navigation state (collapsed menu vs expanded)
3. There may be an additional transform or coordinate system difference not yet found

### Missing Visual Elements

Despite 439 mesh draw calls, the rendered scene still looks similar. The meshes may be:
- Background elements behind the UI (not visible against the dark background)
- Using materials/shaders that don't render correctly yet
- Rendering to wrong depth (behind or in front of expected layer)

Need to investigate which meshes are drawing and whether they're visible on screen.

### Flow/animation state

Latest runtime trace confirms:
- `choose_mode_panel` activates `Enter.flow`, `show_game_mode_icon.flow`, `highlight.flow`, `select.flow`, and `play_enter_anim.flow`
- `play_enter_anim.flow` reaches a `FlowAnimate`, but logs `enable=0`
- `main_panel` activates `update_rank_number.flow`, `udpate_icon_state.flow`, and `update_tier.flow`, but these are control-flow/state-routing nodes rather than direct visual anims

This shifts the problem from “why don’t flows load?” to “which native hacks are still bypassing or masking the real authored state?”

## Manual Validation (2026-03-11)

The following native runs were used to validate which hacks materially affect the final frame, versus which ones are currently just code-backed hypotheses.

### Screenshot sweeps

All runs used:

```bash
MILO_RENDER=1 MILO_HEADLESS=1 MILO_FIRST_SCREEN=main_screen MILO_MAX_FRAMES=260 \
MILO_SCREENSHOT_FRAMES=220 native/build/dc3-native
```

| Variant | Env | Result | Interpretation |
|--------|-----|--------|----------------|
| Baseline | none | `613272/921600` non-black pixels | Current curated native behavior |
| Flow filter: menu only | `MILO_NATIVE_FLOW_FILTER=menu_only` | `809491/921600` non-black pixels; top help text visible | Blanket flow activation is not neutral; it changes visible shell state |
| Flow filter: all | `MILO_NATIVE_FLOW_FILTER=all` | `809759/921600` non-black pixels; extra left-hand/controller overlays reappear | Re-enabling all flows restores more authored state, but also reintroduces conflicting overlays |
| UI cam: original | `MILO_UI_CAM_MODE=original` | `613269/921600` non-black pixels; visually matches baseline | The historical camera override is not the current blocker |
| UI cam: rotate_hack | `MILO_UI_CAM_MODE=rotate_hack` | `691219/921600` non-black pixels; severe left-shift / clipped composition | Debug-only crutch; not a viable path |
| UI cam: z_hack | `MILO_UI_CAM_MODE=z_hack` | `641063/921600` non-black pixels; large horizontal black band | Known-bad debug override |

### What the screenshots prove

1. The current blocker is upstream of GPU submission.
   The renderer responds to flow/camera changes with materially different frames, so the remaining issue is not “WebGPU refuses to draw the animated UI.”

2. `PanelDir::Enter()` flow forcing is a real composition hack.
   `curated`, `menu_only`, and `all` all produce visibly different choose-mode/helpbar states. That means the filter is actively shaping authored UI state, not just compensating for startup.

3. The camera debug modes should not be treated as fixes.
   `original` is effectively equivalent to the current baseline, while `rotate_hack` and `z_hack` both make composition worse.

4. The scene camera selection in `HamNavList::DrawShowing()` looks like a real fix.
   The debug camera env modes do not recover missing native animation behavior, and the current baseline already reflects the scene camera path.

### GPU capture validation

Using `.claude/skills/gpu-capture` and `.claude/skills/gpu-inspect`, trimmed GFXReconstruct captures were taken for:

- baseline choose-mode (`frames 200-230`)
- `MILO_NATIVE_FLOW_FILTER=menu_only` choose-mode (`frames 200-230`)

High-signal results:

- both captures report `31` trimmed frames at `1280x720`
- both captures create `21` graphics pipelines
- both captures submit the same overall draw volume:
  - `11904` `vkCmdDrawIndexed`
  - `512` `vkCmdDraw`
- pipeline usage is nearly identical between the two captures; the only observed delta was a one-draw shift between two existing pipelines, not a different render path

Interpretation:

- the flow-filter variants are not swapping native onto a different renderer topology
- the visible differences are primarily authored-state differences: visibility, alpha, flow-driven show/hide, and panel/helpbar composition
- this is exactly the kind of case where screenshots + GPU traces together are useful: the screenshot changes a lot, but the GPU workload barely changes

Teardown segfaults occurred after capture completion inside the GFXReconstruct/Dawn shutdown path. The capture files were still valid, which matches the local GPU skill guidance.

### Runtime log validation

These were validated through trace/log output rather than screenshot deltas:

- `HamScreen::Enter()` does force controller mode on first screen enter, and `HelpBarPanel::EnterControllerMode()` does activate `controller_mode.flow`.
- `FlowAnimate` nodes are reachable on native (`play_enter_anim.flow`), but authored state graphs still branch through control-flow nodes and disabled anim nodes.
- `HamNavList::PlayEnterAnim()` really does start the ribbon/header enter animation path, and the native `Poll()` path immediately kills it again because the `AnimTask` never self-cleans.

### Hacks validated by code, not yet isolated by a toggle

These still need either a temporary local toggle or a one-off patch for clean A/B testing:

- text alpha force in `MaterialSetup.cpp`
- zero-alpha floor in `Mesh_Wgpu.cpp`
- speech/Kinect mesh suppression in `MeshFilter.cpp`
- helpbar voice-tip hiding in `HelpBarPanel::Draw()`
- `UIListMesh` temporary show/restore bridge

For those, the next useful step is not blanket removal. Add one temporary env gate at a time, capture a frame, and compare against both the built-in screenshot output and a GPU trace.

## PropAnim Target Analysis (choose_mode_panel)

| PropAnim | Targets | Notes |
|----------|---------|-------|
| `enter.anim` (13.6/13.6) | `tapeX.mat` | Does NOT target list position or camera |
| `camHold.anim` (0/0) | `camera1.cam` (×2 keys) | Holds camera1, not turbo_shell |
| `special_select.anim` (0/0) | Empty (0 keys) | |
| Other PropAnims | Various materials/effects | Not positioning-related |

## Graphics/UI Hack Inventory

This section is the current code-backed inventory of native graphics/UI hacks that affect rendering, visibility, camera behavior, or animation state.

### A. UI animation/state forcing

| File | Hack | Why it exists | Removal outlook |
|------|------|---------------|-----------------|
| `src/system/ui/PanelDir.cpp` | Blanket native `Flow` activation in `PanelDir::Enter()` via `ShouldActivateNativeFlow()` | Many panel enter flows were not starting naturally on native | High-priority removal target. Now known to start contradictory flows (`show` + `hide`, `enter` + `exit`) |
| `src/system/ui/PanelDir.cpp` | Force all `"enter"` `RndPropAnim`s to their end frame, including nested `RndDir`s | Used to recover final positioned state when enter animation wiring was missing | High-priority removal target. This explicitly skips real authored animation |
| `src/system/ui/PanelDir.cpp` | Hide tutorial / gesture / silhouette subdirs on enter | Prevents Kinect/tutorial overlays from drawing over controller-mode UI | Likely removable once controller-mode flow/state is reliable |
| `src/system/hamobj/HamListRibbon.cpp` | Force label style alpha to `1.0` if near-zero | Flow-driven alpha animation is not reliably restoring label visibility | Medium-priority removal target |
| `src/system/hamobj/HamNavList.cpp` | Native forcibly clears `TestEntering`, calls `StopAnimation()`, resets frame | `AnimTask` created from raw `RndAnimatable::Animate()` never self-deletes without Xbox-side cleanup | High-priority fix target. This explicitly kills the enter animation path |
| `src/system/hamobj/HamNavList.cpp` | Input path bypasses `!IsAnimating()` requirement on native | Same `AnimTask` lifetime issue would otherwise block controller navigation forever | Depends on fixing animation cleanup/lifecycle first |
| `src/system/ui/UIListMesh.cpp` | Temporarily force hidden template meshes visible during list slot draw, then restore hidden state | Shared UI list meshes are authored as hidden templates; native was dropping them at draw time | Keep for now. This is a targeted draw bridge, not the main animation blocker |

### B. Renderer/material heuristics

| File | Hack | Why it exists | Removal outlook |
|------|------|---------------|-----------------|
| `native/src/platform/MaterialSetup.cpp` | Narrow text-only alpha force | Replaces the old broad AlphaForce; text materials still often stay at alpha 0 without flow-driven updates | Medium-priority removal target |
| `native/src/platform/Mesh_Wgpu.cpp` | SrcAlpha zero-alpha floor (`0.20`) for non-text decorative meshes | Many UI/background materials remain invisible because Xbox flows normally animate alpha at runtime | Medium-priority removal target |
| `native/src/platform/MeshFilter.cpp` | Skip Kinect/speech/tutorial meshes (`grey_alpha.mesh`, `warning_*`, mic/shield/tutorial content, etc.) | Prevents unavailable Kinect UI and speech overlays from covering menu content | Remove selectively after controller-mode / speech state is driven correctly |
| `native/src/platform/MeshFilter.cpp` | Skip tiny white srcAlpha shading overlays | These default to opaque white rectangles when PropAnim color/alpha never runs | Removal depends on real overlay material animation being restored |
| `native/src/platform/MaterialSetup.cpp` | Auto-prelit heuristic for near-zero-ambient UI scenes | Compensates for native lighting mismatch in UI-like environments | Lower priority renderer heuristic |
| `native/src/platform/MaterialSetup.cpp` | Specular clamp / emissive guard / eye emissive boost / shader-variation name-detect | Compensates for missing Xbox shader conventions and environment interactions | Lower priority renderer heuristic |
| `native/src/platform/MeshGpuCache.cpp` | `FixZeroAlpha()` forces vertex alpha to 1 for meshes with all-zero vertex alpha | Prevents texture-only meshes from multiplying to black | Lower priority compatibility heuristic |

### C. Camera/composition overrides

| File | Hack | Why it exists | Removal outlook |
|------|------|---------------|-----------------|
| `src/system/ui/UI.cpp` | `MILO_UI_CAM_MODE` debug override with `z_hack` / `rotate_hack` | Historical experimentation to recover menu placement in HD/native projection | Already debug-only. Do not treat as normal path |
| `src/system/hamobj/HamNavList.cpp` | Panel scene camera selection for shell draw (`turbo_shell.cam`) | Needed because the native renderer was previously using the default UI camera for scene-authored shell content | This now looks like a real fix, not a hack |
| `src/system/ui/UI.cpp` | Single-pass native camera/environment selection around `UIManager::Draw()` | Native WebGPU path does not mirror Xbox per-panel NgRnd state changes exactly | Architectural bridge; likely remains unless draw architecture changes |
| `native/src/platform/Rnd_Wgpu.cpp` | Clear-color override when DTA config is unavailable (`MILO_CLEAR_COLOR`) | Gives native a sane background without full venue/postproc parity | Debug/bootstrap aid |

### D. Controller/Kinect bypasses with visual side effects

These are not renderer hacks in isolation, but they directly change which UI flows, panels, and overlays become visible.

| File | Hack | Why it exists | Removal outlook |
|------|------|---------------|-----------------|
| `native/src/platform/GestureMgr_Native.cpp` | Force controller mode during native gesture init and in headless mode | Prevents Kinect-gated screens from blocking boot/navigation | Remove only when native gesture/skeleton flow reaches parity |
| `src/lazer/meta_ham/HamScreen.cpp` | Force controller mode on first `HamScreen::Enter()` | Ensures helpbar/controller-mode flows fire before regular screen enter logic | Medium-priority removal target |
| `src/lazer/meta_ham/ShellInput.cpp` | Skip most Kinect infrastructure init and short-circuit `Poll()` | Native does not initialize the full Kinect gesture stack | Remove only with broader input parity |
| `src/lazer/meta_ham/ShellInput.cpp` | Never exit controller mode on native | Without Kinect input, native could fall into a state it cannot recover from | Medium-priority removal target |
| `src/lazer/meta_ham/CursorPanel.cpp` | Skip gesture-driven cursor logic entirely | Cursor panel depends on Kinect/skeleton data | Remove only with body-input parity |
| `src/lazer/meta_ham/HelpBarPanel.cpp` | Hide voice-tip drawables during controller-mode draw | Prevents speech/Kinect overlays from drawing over menu text and ribbons | Medium-priority removal target |

### E. Screen/panel lifecycle shortcuts with visual impact

These are not pure graphics hacks, but they affect panel visibility, transitions, and what reaches the renderer.

| File | Hack | Why it exists | Removal outlook |
|------|------|---------------|-----------------|
| `src/system/ui/UI.cpp` | Auto-advance stuck boot/tutorial screens in `UIManager::Poll()` | Some DTA handlers depend on unavailable Xbox globals or async systems | Remove only after screen-flow parity is restored |
| `src/system/ui/UI.cpp` | Skip transition exit/enter waits on native | UI transitions would otherwise block forever because some animation lifecycles never complete | Depends on fixing UI animation cleanup |
| `src/system/ui/UI.cpp` | Set `mSink = current screen` because `set_sink` DTA path does not fire | Required for input routing, indirectly affects interactive UI flow | Likely permanent unless full screen-level DTA side effects are restored |
| `src/system/ui/UIPanel.cpp` | Synchronous panel loading on native | Simplifies loader interaction and avoids blocked loaders during native boot | Lower priority |
| `src/system/ui/UIPanel.cpp` | Force-finish panels with `is_loaded=false` but no loader | Lets native bypass DLC/network/save-gated load conditions | Remove with broader game-state parity |
| `src/system/ui/UIPanel.cpp` | Block tutorial panel `Enter()` on native | Prevents Kinect-only panel content from coming up | Remove when controller-mode/tutorial suppression is natural |
| `src/system/ui/UIScreen.cpp` | Skip null panels in `SetTypeDef()` | Defensive against failed native object construction | Defensive, not an animation target |
| `src/system/ui/UIScreen.cpp` | Always load all panels on native | Historical workaround for lifecycle instability | Re-evaluate |
| `src/system/ui/UIScreen.cpp` | Hide previous screen instead of unloading in `Enter()` | Kept transitions moving while unload/teardown was unstable | Re-evaluate; has direct composition implications |
| `src/system/ui/UIScreen.cpp` | Skip tutorial panels on screen enter | Prevents Kinect tutorial content from appearing | Remove when tutorial/controller-mode logic is real |

## Removal Matrix

This is the high-signal decision table for what to remove, what to keep, and what must be replaced first.

| Hack / behavior | Visible effect today | Confidence | Action |
|-----------------|----------------------|------------|--------|
| `HamNavList` native self-cancel (`StopAnimation()` + reset) | Explicitly prevents ribbon/header enter motion from ever running | Confirmed by code + flow trace | Replace first |
| `UIManager` transition wait skips | Lets screens advance even though animations never complete | Confirmed by code; depends on the same lifetime bug | Replace right after animation cleanup |
| `PanelDir` blanket Flow activation | Changes choose-mode/helpbar composition; starts contradictory flows | Confirmed by screenshots + GPU capture + code | Remove after real start triggers are understood |
| `PanelDir` forced `"enter"` PropAnim end-frame | Skips real motion and jumps to settled state | Confirmed by code | Remove after real flow/lifecycle path is stable |
| `HamListRibbon` label alpha force | Keeps text visible even when authored alpha is still zero | Confirmed by code; not yet isolated by toggle | Remove after flow-driven text alpha is proven |
| `Mesh_Wgpu` zero-alpha floor | Makes decorative srcAlpha meshes show up even when authored alpha never runs | Confirmed by code; not yet isolated by toggle | Remove after flow/material alpha is proven |
| `MeshFilter` speech/tutorial/white-overlay suppression | Prevents unavailable Kinect/speech overlays from covering the UI | Confirmed by code; partially supported by screenshot deltas | Remove selectively, not all at once |
| `HelpBarPanel::Draw()` voice-tip hiding | Suppresses drawables that controller-mode flow should eventually hide | Confirmed by code | Remove after controller-mode helpbar state is natural |
| `UIListMesh` hidden-template force-show | Allows authored hidden template meshes to render as list items | Confirmed by code; targeted bridge | Keep until list draw/state path can replace it |
| `HamNavList` scene camera selection | Uses the authored panel scene camera instead of default `[ui.cam]` | Confirmed by fixed draw-call recovery and camera analysis | Keep; treat as real fix |
| `MILO_UI_CAM_MODE=rotate_hack` / `z_hack` | Distorts or breaks composition | Confirmed by screenshots | Leave debug-only; not part of fix path |
| forced controller mode / never-exit-controller-mode | Keeps native out of Kinect-gated dead ends | Confirmed by code and runtime logs | Keep until shell input / gesture parity is broader |
| screen/panel load shortcuts | Prevent blocked boot/load states from hanging native | Confirmed by code | Keep until broader game-state parity exists |

## What We Can Remove Today

Very little should be removed immediately from the live native path.

Safe conclusions:

- `MILO_UI_CAM_MODE=rotate_hack` and `MILO_UI_CAM_MODE=z_hack` are not part of the fix path and should remain debug-only.
- the scene camera selection in `HamNavList::DrawShowing()` should stay; it behaves like a real fix, not a temporary crutch.

Not safe to remove yet:

- `HamNavList` self-cancel logic, unless animation lifetime is fixed first
- `PanelDir` blanket flow activation / forced PropAnim end-frame logic, unless real entry triggers are ready
- text/alpha/mesh suppression hacks, unless authored state is already driving visibility correctly
- controller-mode and panel/screen lifecycle bridges, unless native can still boot and navigate without them

Practical takeaway:

- the next removals should be replacement-driven, not cleanup-driven
- if a hack does not yet have a proven replacement path, keep it and add a temporary env gate for A/B capture instead of deleting it outright

## Real Fixes To Preserve

These currently look like correct native behavior rather than hacks to peel back:

- scene camera selection for shell draw (`turbo_shell.cam`)
- scene uniform XYZ camera-change detection
- removal of the earlier `entering=true` ribbon workaround

## Workarounds in Place

### 1. Scene Camera Selection (HamNavList.cpp)

```cpp
PanelDir *ownerPanel = dynamic_cast<PanelDir*>(DataDir());
RndCam *drawCam = (ownerPanel && ownerPanel->Cam()) ? ownerPanel->Cam()
    : (TheUI ? TheUI->GetCam() : nullptr);
```

### 2. Label Alpha Force (HamListRibbon.cpp)

Force alpha to 1.0 since Flow-driven alpha animation doesn't run.

### 3. Flow Activation Filter (PanelDir.cpp)

`ShouldActivateNativeFlow()` controls which Flows activate on Enter().

### 4. Nested RndDir PropAnim Forcing (PanelDir.cpp)

Iterates into nested RndDir objects to force their enter PropAnims to end frame.

## Practical Unwind Order

This is the recommended order to reach native UI parity without destroying bootability.

### Step 1: restore animation lifetime

Target:

- `RndAnimatable` / `AnimTask` lifetime mismatch
- `HamNavList` native self-cancel path
- `UIManager` transition enter/exit skip logic

Success criteria:

- ribbon/header enter animation can run without being force-killed
- `IsAnimating()` eventually becomes false naturally
- transition waits can be re-enabled without hanging

### Step 2: stop synthetic panel entry behavior

Target:

- `PanelDir::Enter()` blanket flow activation
- forced end-frame `"enter"` PropAnim logic

Success criteria:

- choose-mode and helpbar enter state comes from authored triggers/state
- screenshot deltas between `curated`, `menu_only`, and `all` disappear because the hack is gone, not because the filter changed

### Step 3: remove visibility masks

Target:

- text alpha force
- zero-alpha floor
- white-overlay / Kinect mesh suppression
- helpbar voice-tip hiding

Success criteria:

- text stays readable without alpha forcing
- decorative meshes show or hide for authored reasons
- controller-mode overlays do not need draw-time suppression

### Step 4: revisit broad parity bridges

Target:

- forced controller mode
- never-exit-controller-mode
- boot auto-advance
- synchronous/force-finish panel load shortcuts

Success criteria:

- native remains navigable without these bridges
- tutorial / Kinect-gated screens are suppressed by real state rather than native bypasses
- screen transitions and panel loading are no longer hiding lifecycle bugs

## Unwind Priorities

### Priority 1: animation correctness blockers

1. `HamNavList` enter-animation self-cancel path
2. `AnimTask` / `RndAnimatable` lifetime mismatch
3. transition skip logic that assumes animations never complete
4. `PanelDir::Enter()` blanket Flow activation
5. `PanelDir::Enter()` PropAnim end-frame forcing

### Priority 2: visibility/alpha masks

1. text alpha force
2. zero-alpha floor for srcAlpha decorative meshes
3. speech/Kinect mesh suppression
4. white-overlay suppression in `MeshFilter`

### Priority 3: broader parity bridges

1. forced controller mode
2. never-exit-controller-mode behavior
3. boot screen auto-advance
4. panel load/finish shortcuts

## Recommended Validation Workflow

For the next round of peelback work:

1. Add a temporary env gate around one hack.
2. Capture a built-in frame with `MILO_SCREENSHOT_*`.
3. If the frame changes materially, capture a GPU trace with `.claude/skills/gpu-capture` and inspect it with `.claude/skills/gpu-inspect`.
4. Only remove the hack permanently after confirming the replacement path is authored-state-correct, not just visually non-black.

Useful local skill entry points:

- `.claude/skills/gpu-capture/SKILL.md`
- `.claude/skills/gpu-inspect/SKILL.md`
- `.claude/skills/gpu-debug/SKILL.md`

## Next Concrete Tasks

1. Add a temporary env gate around the `HamNavList` native self-cancel block and verify that enter anims complete only after animation lifetime is fixed.
2. Add temporary env gates around:
   - text alpha force
   - zero-alpha floor
   - `MeshFilter` suppression
   - helpbar voice-tip hiding
3. For each gate, take:
   - one built-in screenshot capture
   - one trimmed GPU capture if the screenshot changes materially
4. After animation lifetime is repaired, remove `PanelDir` flow forcing and forced PropAnim end-frame logic before touching broader controller-mode bridges.
5. Only after the above is stable, re-evaluate the remaining panel/screen lifecycle shortcuts.

## PPC Decomp Impact

All `#ifdef HX_NATIVE` guards are compiled out for PPC. No decomp regressions.

## File References

| File | Change | Purpose |
|------|--------|---------|
| `native/src/platform/Rnd_Wgpu.cpp` | XYZ camera change detection | Fix scene uniform updates |
| `native/src/platform/Rnd_Wgpu.h` | Added mLastCamPosX/Z tracking | Support XYZ detection |
| `src/system/hamobj/HamNavList.cpp` | Scene camera selection, removed hacks | Correct camera + ribbon filter |
| `src/system/hamobj/HamListRibbon.cpp` | Force label alpha to 1.0 | Fix invisible text |
| `src/system/ui/PanelDir.cpp` | Flow activation, PropAnim forcing | Native enter pipeline |
