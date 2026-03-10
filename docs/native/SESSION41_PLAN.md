# Session 41 Plan — UI Layout Fix

## Problem Statement

UI elements render but are **mispositioned and mis-scaled** compared to the Xbox original. Comparing `archive/screenshots/session40/frame_03500.png` (our native, choose_mode_screen) to `archive/screenshots/references/dc3_main_menu.jpg` (Xbox, main_screen):

- Player indicator icons are too small and shifted inward
- The navigation ribbon is centered instead of positioned correctly
- Text labels ("MAIN MENU", copyright, help bar) are not reliably legible in the full app composition even though the text renderer itself is known to work
- Background is plain gray (no venue/neon effects — expected, but layout should still match)

Reference screenshots: `archive/screenshots/references/dc3_main_menu.jpg`
Our screenshots: `archive/screenshots/session40/frame_03500.png`, `frame_03520.png`, `frame_03700.png`

## Current Baseline Before Session Start

The next session should assume the following are already true and should not be re-debugged unless they regress:

- Native `TheHamProvider` is now wired through `HamInit()` and native gets a real named `hamprovider` object instead of the old blank fallback in `App.cpp`.
- `dc3-native` currently builds again. The native build blocker from `SkeletonIdentifier.cpp` / `SkeletonQualityFilter.cpp` was a `sprintf_s` explicit-instantiation issue on HX_NATIVE and has been fixed.
- Standalone headless boot is validated on the real app binary, not just the test harness:

```bash
MILO_HEADLESS=1 MILO_MAX_FRAMES=100 ./native/build/dc3-native
```

- Current expected boot behavior:
  - camera/pose server may fail if no webcam is present
  - controller mode is forced
  - UI boots, transitions from `attract_screen` to `autosave_warning_screen`, and exits cleanly after the frame cap

This means Session 41 should treat provider wiring and app buildability as prerequisites that are already satisfied, and focus on UI layout/render correctness.

## Fresh Validation From 2026-03-10

Fresh screenshot validation tightened the scope further:

- The retail Xbox 360 screenshot remains the north-star reference for correct composition:
  - top help text (`EXIT CONTROLLER MODE`, `SELECT`)
  - right-side `MAIN MENU`
  - copyright block
  - `Say Xbox` prompt
  - logo centered-left with the right-side menu rails occupying the correct portion of the frame
  Treat this as the target layout, not as evidence of an earlier native state.
- `render-test text_menu` still renders centered, readable menu text on native. This keeps the core text path in the "working enough to debug layout" bucket.
- `render-test venue_with_ui` still looks awkward, but fresh frame capture shows its text overlays are mostly landing near the center of clip space. Treat this test as a useful smoke test for "text over a venue still renders", not as conclusive proof of the same full-app layout bug.
- A fresh standalone app capture using scripted headless input still reaches `choose_mode_screen`, and `frame_00500.png` remains visually very close to the old bad Session 40 capture. Provider/build fixes did not materially improve layout by themselves.
- Fresh archived captures under `archive/screenshots/session41/` are now the best review set:
  - `frame_00410.png` through `frame_00425.png` show the early post-tutorial state
  - `frame_00430_postfix.png` through `frame_00500_postfix.png` show the transparent-fix baseline
  - the retail north-star remains `archive/screenshots/references/dc3_main_menu.jpg`
- Intermediate standalone app frames (`frame_00220.png`, `frame_00360.png`) also show the over-wide ribbon dominating the frame during earlier UI screens, which points toward a shared placement/projection problem rather than a single broken panel.
- Standalone app logs at frame 500 explicitly reported:
  - selected camera: `[ui.cam]`
  - `RndCam::Current() == [ui.cam]`
  So "the app never switches to the UI camera" is no longer a strong primary hypothesis. The more likely issue is that the selected camera, projection, viewport mapping, or object placement is numerically wrong for UI composition.
- Extra frame probing around `main_screen` (`frame_00410.png`, `frame_00415.png`, `frame_00420.png`, `frame_00425.png`) shows the current scripted native path does not dwell on the retail-like main menu long enough to use it as a stable comparison point. By those frames the app is already in `choose_mode_screen`.

### Frame-Capture Evidence Worth Carrying Into Session 41

Fresh capture of app frame 500 (`MILO_CAPTURE_FRAME=500`) adds stronger evidence than screenshots alone:

- UI draw calls definitely exist on the bad frame. This is not a "text never instantiated" problem.
- Several unnamed `Eagle-Light` text meshes render with clearly wrong clip-space placement:
  - some text lands at roughly `ndc y = 1.56`, which is above the visible frame
  - some text lands around `ndc x = -0.68`, which is far left of the intended retail placement
- The top-edge "white block" artifacts are probably clipped helpbar label/button meshes, not the player cards themselves:
  - helpbar text/button meshes (`icons_buttons.mat`, `Eagle-Light(47).mat`) land at `ndc y = 1.56`
  - the actual player/card meshes (`buffer_container_{left,right}.mesh`, `silhouette_guy*.mesh`) sit much lower around `ndc y ~= 0.70`
  This explains why the top corners look like giant cropped glyphs even though the card meshes themselves are closer to sensible positions.
- The prominent menu ribbon also projects to suspicious positions. Repeated `mainMenuRibbon.mesh` draws cluster around:
  - `world ~= (-155, -9, -33)`
  - `ndc ~= (-0.087, 0.147, 0.993)`
  This means the ribbon's anchor is not wildly off-screen, but the resulting composition still does not match retail. That points toward wrong panel-space sizing, extents, or parent placement, not just a missing draw.
- The capture also shows the bad screen is a mixed-camera composite:
  - text/help/icon glyphs are often drawn under `[ui.cam]`
  - many of the large ribbon / overlay elements are drawn under `turbo_shell.cam`
  This is important. The next session should not assume every visible menu element ought to be under `[ui.cam]`; instead, it should verify whether the relative placement between the world/overshell camera pass and the UI camera pass matches retail.
- The frame starts under `turbo_shell.cam`, then switches into `[ui.cam]` during UI draw. That strengthens the conclusion that mid-frame camera selection exists and the remaining problem is the state or transforms used with it.

### Transparent-Queue Experiments

Two renderer experiments changed frame 500 dramatically:

- `MILO_NO_TRANSPARENT_DEFER=1`
  - removes the big mixed transparent composite
  - reveals a centered tutorial-style prompt instead of the oversized ribbon-dominated frame
- `MILO_FLUSH_TRANSPARENT_ON_CAM_SWITCH=1`
  - also changes frame 500 radically
  - again suppresses the dominant mixed ribbon composition and leaves a very different screen balance

Implication:
- the current bad layout is not only "wrong transforms"
- the global transparent queue is almost certainly crossing camera/pass boundaries in a way the original game does not
- a real fix likely needs transparent ordering scoped by camera or panel/pass, not just a better sort key

Current status:
- native now flushes deferred transparent draws before scene camera/environment changes, and deferred draws retain both queued camera and queued environment
- after this fix, frame 500 no longer shows the old oversized mixed ribbon composite
- the remaining frame is much more coherent under `[ui.cam]`, which confirms the transparent cross-camera mix was a real root cause
- residual issues remain in specific UI elements, especially top-corner help/icon blocks that are still too large / too high

Important nuance:
- frame 500 in current native runs is `choose_mode_screen`, while the retail north-star image is a main-menu retail shot
- keep screen matching honest when judging visual parity

### Controller-Mode / Helpbar Probes

Two native probes were worth trying and are now ruled out as direct fixes:

- Early `App.cpp` call to `ShellInput::EnterControllerMode(true)`
  - rejected: it aborts immediately because `ShellInput::EnterControllerMode()` asserts on `TheHamUI.GetHelpBarPanel()` before the helpbar exists
- Late replay of controller-mode visuals/state after the helpbar exists
  - probe 1: calling `HelpBarPanel::EnterControllerMode()` from `HelpBarPanel::Enter()`
  - probe 2: calling full `ShellInput::EnterControllerMode(true)` from `ShellInput::SyncToCurrentScreen()` once `in_controller_mode` was still false
  - both probes keep boot stable but move the whole helpbar/player-indicator family from roughly `ndc y ~= 0.7/1.56` to `ndc y ~= 3.2`, effectively blowing the frame off the top and turning the captured screen mostly black

Implication:
- the remaining layout bug is strongly entangled with controller-mode/helpbar state
- but the missing native parity is not solved by naively replaying `controller_mode.flow` or `EnterControllerMode()` at the first "safe-looking" point
- the next session should treat controller-mode sequencing as a state-ordering problem, not as a single missing function call

This makes the next-session question more precise:
- Why are some UI text anchors ending up above the top of the frame or too far left if `[ui.cam]` is active?
- Are panel-space transforms / anchor offsets wrong before projection?
- Is the projection / screen-rect math skewing otherwise reasonable placements?
- Or is the relative composition between `turbo_shell.cam` content and `[ui.cam]` content wrong even if each pass is internally consistent?
- Is the global transparent queue incorrectly mixing content from multiple camera passes and making the current screen look like a composite of the wrong layers?

## Relevant Recent Context (HEAD~5 and Earlier Session Work)

The recent native/rendering history narrows the likely causes of the current layout problem:

- Text rendering was already debugged to the point of "working but subtle/small", not "missing":
  - `docs/sessions/2026-03-02-text-rendering-investigation.md`
  - Text pipeline was traced end-to-end: `UILabel -> RndText -> FontMap -> mesh draw -> transparent queue -> GPU`
  - The UI camera `[ui.cam]` values were already measured and appeared sane: `(0, -768, 0)`, `yFov=0.6024`, near/far `1/1000`
- Recent native status also records explicit text fixes that should be treated as established baseline, not fresh suspects:
  - text meshes identified by empty mesh names
  - depth/cull handling for text meshes
  - `useAlphaAsRGB` font shader path
  - `mTextColor` copied into vertex colors on native
  - `FontMap` / `FontMap3d` native allocation fixed to use `sizeof(...)` instead of PPC hardcoded sizes
- A standalone render isolation tool now exists:
  - `native/build/render-test`
  - useful cases: `text_basic`, `text_menu`, `venue_with_ui`
  - this should be used to separate "renderer/text pipeline regression" from "full app UI layout/camera/wiring issue"
- Mid-frame camera-switch uniform refresh is already implemented:
  - `Mesh_Wgpu.cpp` calls `gWgpuRnd->EnsureSceneUniformsCurrent()`
  - `Rnd_Wgpu.cpp` rewrites scene uniforms when `RndCam::Current()` changes
  - because of this, "scene uniforms only written once per frame" is no longer a top-tier suspect unless traces prove the camera cache is stale anyway

## Root Cause Hypotheses

### H1: Panel-space placement / anchor transforms are wrong before or during projection (MOST LIKELY)
The strongest new evidence is that many text draws exist, but their clip-space positions are wrong. That is consistent with bad panel-space offsets, inherited transforms, or anchor math feeding the renderer.

**Evidence**:
- text draws are present on app frame 500
- some of them land off-screen (`ndc y > 1`)
- others land far left when retail expects right-side placement
- the ribbon is still visible and near the center-left of clip space, but not composed like retail
- the clipped top-edge artifacts line up with helpbar text/button meshes rather than the card meshes behind them

### H2: Camera / projection / screen-rect mismatch
The UI camera `[ui.cam]` has specific properties (position, FOV, near/far, screen rect) loaded from .milo data. If any of these are wrong or are combined incorrectly with the viewport/projection math, all UI elements shift.

**Evidence**:
- The [ui.cam] is known to sit at `(0, -768, 0)` looking at the origin.
- `render-test text_menu` looks sane.
- App frame capture still shows off-screen / misaligned text under active UI draw.
- Projection is still a serious suspect, but the new capture makes pure placement/anchor problems at least as plausible as camera math.

### H3: Camera-switch uniform path exists, but the active scene state may still be semantically wrong
Native now has an explicit camera-change refresh path:

- `Mesh_Wgpu.cpp` calls `EnsureSceneUniformsCurrent()` before immediate mesh draw
- `Rnd_Wgpu.cpp` can upload a new scene uniform block mid-frame

So the question is no longer "is there any mid-frame update path?" but "is the selected camera state, projection state, and cached scene bind group actually the right one when each pass draws?" This is still worth validating, but it is a lower-confidence hypothesis than before because app logs already show `[ui.cam]` selected during UI draw and the transparent flush restores queued cameras correctly.

### H4: Transparent ordering across cameras/panels is wrong
The strongest new renderer hypothesis is that transparent meshes are deferred globally for the whole frame and sorted together even when they were queued under different cameras. Distance sorting across different cameras is not meaningful, and end-of-frame flush can destroy the intended panel/pass ordering.

**Evidence**:
- frame capture shows mixed use of `[ui.cam]` and `turbo_shell.cam`
- disabling transparent deferral radically changes the frame
- flushing transparents on panel camera switches also radically changes the frame

Specific fix direction:
- do not use a single global transparent queue across the whole frame
- flush transparents at camera/pass boundaries, or partition the queue by camera and preserve higher-level panel order

Status:
- this hypothesis has now been validated enough to act on; native has an initial implementation that flushes transparent draws before scene state changes
- this should be treated as established baseline for the next session, not as an open question to re-prove

### H5: Text layout/visibility mismatch, not total text failure
Earlier native text investigation concluded text rendering exists, but it is subtle/small and easy to miss against the current layout/background. Treat text as a camera/layout/scale problem first, and only fall back to font loading if diagnostics prove `RndText::DrawShowing()` is not being reached or font meshes are absent.

### H6: Transform hierarchy not propagating
If `RndTransformable::WorldXfm()` returns identity for UI elements (because parent transforms aren't dirty-flagged or loaded from .milo), everything renders at the origin.

### H7: Specific panel/helpbar transforms are still wrong after the transparent fix
After fixing the mixed-camera transparent composition, the remaining visible problems are no longer "whole-screen chaos". They are more local:

- top-corner help/icon blocks are still oversized / badly placed
- some top-help text/icons were previously observed above the visible frame (`ndc y ~= 1.56`)
- the central instructional panel now reads coherently, which suggests the remaining work is in panel-specific placement, not a global renderer collapse

### H8: Controller-mode helpbar state is partially missing or is being applied at the wrong time
The top-edge issues are now localized enough that helpbar/controller-mode sequencing deserves its own hypothesis.

**Evidence**:
- native forces controller mode at the gesture layer before UI panels exist
- the obvious "replay controller mode later" probes do change the same mesh family, but they overshoot badly (`ndc y ~= 3.2`) instead of fixing the layout
- this suggests the correct native fix is likely to preserve the original event ordering or prerequisite state for controller-mode activation, not merely call the same functions later

## Session Guardrails

- Do not spend time reintroducing `TheHamProvider` null workarounds unless a new regression proves the typed provider path is broken.
- Use `dc3-native` as the primary validation target for Session 41, not only `milo-tests`.
- If a rendering/layout change only works in `milo-tests` but not `dc3-native`, treat it as incomplete.
- Preserve the current headless boot stability while iterating on layout fixes.
- Do not start from "text is broken" as the default assumption. Start from "text was previously working but dim/subtle, so the larger layout/projection path is likely still wrong."
- Do not start from "the app forgot to select `[ui.cam]`". Fresh logs already show `[ui.cam]` active at choose-mode draw time.
- Use `dc3-native` frame capture as the primary truth source for placement bugs. `render-test venue_with_ui` is still useful, but it is a weaker proxy for the full-app layout problem than the real app frame.
- Use the retail Xbox screenshot as the composition north star. Improvements should be judged against that framing, not against any ambiguous older native capture.
- Treat the current menu as a mixed-camera composition. Validate both the `[ui.cam]` text/help pass and the `turbo_shell.cam` ribbon/overshell pass, plus their relative balance.
- Do not trust a whole-frame transparent sort across mixed cameras as "probably fine". It now has direct evidence against it.
- Compare like-for-like screens. Frame 500 `choose_mode_screen` is useful for debugging, but it is not the same thing as the retail main-menu north-star shot.
- Do not regress the transparent queue fix while chasing remaining panel placement bugs.
- Do not assume the current scripted path gives a useful `main_screen` capture. It transitions through `main_screen` too quickly to serve as a clean retail comparison without extra work.
- Do not keep naïve controller-mode probes (`HelpBarPanel::EnterControllerMode()` replay or `ShellInput::EnterControllerMode()` after sync) as real fixes. They are now known-bad experiments.

## Investigation Plan

### Step 0: Reconfirm Standalone Boot Baseline (5 min)
Before touching rendering code, rerun:

```bash
MILO_HEADLESS=1 MILO_MAX_FRAMES=100 ./native/build/dc3-native
```

If this does not still reach `autosave_warning_screen` and exit cleanly, stop and fix that regression first before investigating layout.

### Step 0.5: Re-run Render Isolation Tests (10 min)
Before instrumenting the full UI, rerun the minimal renderer checks:

```bash
cd native/build
./render-test --output /tmp/text_menu.png --test text_menu
./render-test --output /tmp/venue_ui.png --test venue_with_ui --width 1280 --height 720
```

Use these to answer:
- Is text still visibly rendering in isolation?
- Does `venue_with_ui` already reproduce the same scale/placement problem as `dc3-native`?
- If `render-test` looks correct but `dc3-native` does not, the bug is likely in app/UI camera selection, panel transforms, or draw ordering rather than the core text renderer.

Current expected answer from fresh validation:
- `text_menu`: yes, readable and centered
- `venue_with_ui`: visually awkward, but frame capture suggests its text is not catastrophically misprojected
- implication: use it as a renderer smoke test, but let app frame capture drive the real hypothesis ranking

### Step 1: Dump Camera State (15 min)
Add diagnostic output when UI camera state is written during draw:

```
File: native/src/platform/Rnd_Wgpu.cpp (WriteSceneUniforms)
```

Print the camera's key properties when `WriteSceneUniforms()` runs:
- `cam->Name()` — should still confirm `[ui.cam]`
- `cam->WorldXfm().v` — camera position (expected: `(0, -768, 0)`)
- `cam->GetNearPlane()` / `cam->GetFarPlane()` — clipping planes
- `cam->GetYFov()` — field of view (expected: ~0.6 radians)
- `cam->GetScreenRect()` — viewport rect (expected: `(0, 0, 1, 1)`)
- `cam->mLocalProjectXfm` — the computed local projection

Also dump the computed viewProj matrix values to verify the pipeline. The key question here is no longer whether a UI camera exists, but whether its final projection state is the right one for screen-space UI.

### Step 2: Check Mid-Frame Camera Update (15 min)
Verify that when `PanelDir::DrawShowing()` calls `camOverride->Select()`, the scene uniforms actually switch to the selected camera used by the UI mesh draw calls.

```
File: native/src/platform/Rnd_Wgpu.cpp
Look: `EnsureSceneUniformsCurrent()` + `WriteSceneUniforms()`
```

Important: this is now a validation step, not an assumed missing feature. The update hook already exists, so focus on whether:
- `RndCam::Current()` is the expected UI camera at draw time
- the bind group offset really changes when the camera changes
- text and ribbon meshes draw under the same camera state

Fresh validation already showed `[ui.cam]` selected at frame 500 in the app, and transparent flush restores the queued camera. So this step should look for subtler state mismatches, not an outright missing camera switch.

### Step 2.5: Fix Transparent Queue Scope First (20 min)
Before deeper transform surgery, test the transparent ordering model:

- current native behavior defers transparent meshes globally until end-of-frame
- this is likely wrong once multiple camera passes are involved

Practical fix candidates:
- flush transparent draws before changing cameras in `PanelDir::DrawShowing()`
- flush transparent draws before restoring the previous camera after a panel finishes
- if that is too blunt, partition the queue by camera and flush each camera group at the right panel/pass boundary instead of sorting the whole frame together

Acceptance for this step:
- the current frame should stop looking like a mixed overlay from mismatched passes
- changes should improve compositional coherence without needing to disable all transparency

Status:
- done enough for now; native now flushes deferred transparents before scene state changes
- the next session should build on that result and move to the remaining panel-specific placement bugs

### Step 3: Dump Transform Hierarchy / Clip-Space Placement (15 min)
Add a diagnostic in `Mesh_Wgpu.cpp`'s `DrawMeshImmediate()` to print the mesh name and its WorldXfm translation for the first frame:

```
File: native/src/platform/Mesh_Wgpu.cpp (DrawMeshImmediate around line 835)
```

Check if mesh transforms are identity (everything at origin) or have meaningful values. If all transforms are identity, the .milo transform data isn't loading or propagating.

Fresh evidence already suggests a better version of this step:
- inspect a real app capture frame and compare world position plus NDC for
  - `mainMenuRibbon.mesh`
  - unnamed `Eagle-Light` text meshes
  - top-help icon/text meshes
- note which camera each family is using (`[ui.cam]` vs `turbo_shell.cam`)
- if those are already off-screen in NDC, focus on the transforms / anchors that produced them before blaming visibility or material state
- with the transparent fix in place, prioritize the top-help/icon family because they remain visibly wrong while the central instructional panel is now coherent

### Step 4: Check Text Object Visibility (15 min)
Search for RndText objects in the loaded panels:

```
File: src/system/rndobj/Text.cpp — RndText::DrawShowing()
```

Add a native-only trace to see if RndText::DrawShowing() is called at all, and if so, what text content it has and whether its font meshes exist.

Important: only treat this as a font-loading bug if the camera/uniform path checks out first. The stronger prior is that text is being projected/scaled incorrectly along with the rest of the UI.
Also keep in mind:
- native already has a `text_menu` render-test case and a `venue_with_ui` overlay case
- native text brightness was explicitly changed to use vertex color directly in WGSL for `useAlphaAsRGB`
- if text is dim again, suspect regression in vertex color propagation or text alpha handling before suspecting missing glyph generation

### Step 5: Fix the Primary Issue (30-60 min)
Based on diagnostic results from Steps 1-4, apply fixes. Most likely:

**If H1 (camera)**: Verify [ui.cam] properties match expected values. May need to trace camera loading from .milo to ensure properties aren't getting default values.

**If H2 (scene uniforms/cache)**: Fix the stale-camera path, not the absence of a path. Likely areas:
- camera selection timing before `DrawMeshImmediate()`
- last-scene-camera tracking in `EnsureSceneUniformsCurrent()`
- scene bind group replacement after ring-buffer reupload

**If H3 (text)**: Trace font .milo loading path, verify FontMap creation, check if font texture uploads succeed.
But only go this far if `render-test` and `RndText` traces indicate a real text regression.

**If H4 (transforms)**: Verify `DirLoader::LoadObjs` correctly populates LocalXfm for loaded objects. Check if `mDirty` is being set after load.

### Step 6: Compare Side-by-Side (15 min)
Take new screenshots at the same frames and compare with session 40 and Xbox reference.

## Acceptance Criteria

- `dc3-native` still boots headless for 100 frames without new regressions.
- UI screenshots show a measurable improvement in layout alignment, not just a code-path hypothesis.
- If the primary fix is camera/uniform related, screenshots should improve for both ribbon placement and text placement in the same run.
- `render-test` remains consistent with the app-level diagnosis:
  - if `render-test` text is good and app text is bad, document that the bug is above the core text renderer
  - if both are bad, document that the bug is likely in shared renderer/text code
- `dc3-native` frame capture should show previously off-screen UI text moving back into plausible clip-space positions.
- the screen should stop changing catastrophically when transparent handling is varied; once transparent scoping is correct, layout changes should become local and interpretable
- Any remaining missing elements after the camera/layout fix should be explicitly categorized as:
  - still rendering but mis-scaled/misplaced
  - not instantiated
  - instantiated but culled/hidden
  - loaded but using the wrong camera or transform

## Key Code Paths

### Camera Selection During UI Draw
```
PanelDir::DrawShowing()     — src/system/ui/PanelDir.cpp:242-267
  → CamOverride()           — returns mCam or TheUI->GetCam()
  → camOverride->Select()   — sets RndCam::sCurrent
  → [draw children]
  → curCam->Select()        — restore previous camera
```

### Projection Matrix Construction
```
RndCam::GetViewProjectXfms()  — src/system/rndobj/Cam.cpp:283-321
  → Multiply(mInvWorldXfm, sFlipYZ, viewXfm)  — view = inv(cam_world) * YZ_flip
  → Build projMtx from mYFov, mNearPlane, mFarPlane, mScreenRect, mLocalProjectXfm
```

### Scene Uniforms → GPU
```
WgpuRnd::WriteSceneUniforms()  — native/src/platform/Rnd_Wgpu.cpp:621-689
  → cam->GetViewProjectXfms(viewXfm, projMtx)
  → viewProj = view * proj (row-major 4x4 multiply)
  → write to mSceneRing uniform buffer
```

### Mesh World Transform → GPU
```
FillObjectUniforms(WorldXfm(), obj)  — native/src/platform/Mesh_Wgpu.cpp:160-189
  → converts Transform (3x3 + vec3) to 4x4 row-major model matrix
  → writes to mObjectRing uniform buffer
```

## Quick Reference

| Property | Expected Value | Source |
|----------|---------------|--------|
| UI camera position | `(0, -768, 0)` | Milo Y-forward convention |
| UI camera FOV | `0.6024178` rad (~34.6deg) | Default RndCam ctor |
| UI camera near/far | `1.0` / `1000.0` | Default RndCam ctor |
| Screen rect | `(0, 0, 1, 1)` | Full viewport |
| Xbox resolution | 1280x720 | 16:9 HD |
| Coordinate convention | Milo: X-right, Y-forward, Z-up | Flipped to D3D via sFlipYZ |

## Env Vars for Testing
```bash
MILO_RENDER=1 MILO_HEADLESS=1 \
  MILO_SCREENSHOT_DIR=archive/screenshots/session41 \
  MILO_SCREENSHOT_FRAMES=500,3500 \
  MILO_MAX_FRAMES=3600 \
  native/build/dc3-native
```

Quick sanity command before long screenshot runs:

```bash
MILO_HEADLESS=1 MILO_MAX_FRAMES=100 native/build/dc3-native
```
