# Bug 1 — Native menu/UI text is invisible (ROOT-CAUSED)

## Symptom

On native (and web), menus render their bars, icons, rank badge, and the 3D venue
backdrop, but **every glyph of text is missing** — main-menu item labels,
choose-mode labels (DANCE/STORY/FITNESS…), the bottom help-bar prompts, dialog
body text. Confirmed by headless screenshots of `main_screen` and
`choose_mode_screen`.

Reference (text working): `archive/screenshots/session37_choose_mode_text_f600.png`
(dated 2026-03-10) shows "DANCE / STORY / FITNESS / LIVE CHALLENGE" clearly.

## Root cause

**Commit `311e3b75` (2026-03-11) "CameraManager::Poll 40→85% … native UI
improvements"** broke the UI camera framing. It replaced an *unconditional* UI
camera Z placement:

```cpp
// pre-311e3b75, in UIManager::Draw (src/system/ui/UI.cpp)
// "Ribbons at Z=256-517 need camera centered at Z≈387 to fit in 34.5° FOV."
mCam->SetLocalPos(Vector3(0, -768, 387));
```

with a `switch (GetNativeUICamMode())` whose **default case does nothing**:

```cpp
switch (GetNativeUICamMode()) {           // default = kNativeUICamDefault
case kNativeUICamDefault:  break;         // <-- EMPTY. comment claims "scale distance
                                          //     for HD visible area" — never implemented
case kNativeUICamOriginal:  break;
case kNativeUICamZHack:     mCam->SetLocalPos(Vector3(0, -768, 387)); break;
case kNativeUICamRotateHack: { /* rotate + Z=387 */ } break;
}
```

`GetNativeUICamMode()` defaults to `kNativeUICamDefault` (overridable only via the
`MILO_UI_CAM_MODE` env var). So in a normal run the UI camera stays at **Z=0**,
`ui.cam` = position `(0, -768, 0)`, `yfov=0.602` (34.5°), near=1, far=1000. That
frames only world-Z ∈ ~[−238, +238]. UI text sits at world-Z ≈ 256–517, i.e.
**below the visible viewport** → invisible.

**Timeline is airtight:** last-good screenshots 2026-03-09/10; regression landed
2026-03-11.

### Exact locations
- `src/system/ui/UI.cpp` — `GetNativeUICamMode()` (~line 87), the `NativeUICamMode`
  enum (~line 80), and the `switch` in `UIManager::Draw` (~line 217). The same
  switch also appears in `UIManager::GotoFirstScreen`.

## Evidence chain (what was ruled out, and how)

All verified with headless screenshots + `HX_NATIVE`+`getenv`-gated `fprintf`
instrumentation (zero PPC/decomp impact) — see [03-diagnostic-toolkit.md](03-diagnostic-toolkit.md):

1. **Not the shared engine.** The engine advanced 32 commits past the DC3 pin, but a
   full `engine@pin` (8fb669d9) rebuild reproduced the missing text **identically**.
   Only 3 DC3-linked engine files changed (shader `standard_wgsl.inc`,
   `UniformStructs.h`, `PipelineManager.cpp`); none affect prelit text.
2. **Text meshes are 100% valid.** `RndText::FontMap` produces glyph meshes with
   correct verts/faces, empty names (so engine `isTextMesh=1`), a valid font
   material, and a real uploaded atlas texture (`Eagle-Light(47).tex`, not the
   black fallback). Font loading is fine (`CharWidthAdvanceCoords` returns valid
   UVs, 170 chars).
3. **The engine draws them.** All ~60 text meshes reach `pass.DrawIndexed(...)` in
   `Mesh_Wgpu.cpp::DrawMeshImmediate` with correct params (`prelit=1`,
   `useAlphaAsRGB=1`, `useTexture=1`, valid pipeline, both passes).
4. **Not texture/material/shader.** Forcing the shader to output **solid opaque red**
   for text produced *no red anywhere* → the triangles rasterize off-screen, not a
   color/alpha issue.
5. **Off-screen, measured.** `RndCam::WorldToScreen` put help-bar text at screen
   **Y=1.30** (screen range is [0,1]) — below the bottom edge.
6. **`a16912fc` (projMtx.y.y) is NOT the cause** — it's a *necessary* fix; reverting
   it makes the whole screen black.
7. **Confirmation.** Running with `MILO_UI_CAM_MODE=z_hack` (re-applies Z=387) moved
   that same text from Y=1.30 → **Y=0.49 (on-screen)**, and `camPos` became
   `(0,-768,387)`.

## Why it's not a clean one-line revert

`311e3b75` dropped the unconditional Z=387 **on purpose**. The pre-311e3b75 comment:

> "Once HamNavList started drawing its widgets under [ui.cam], the unconditional
> Z override pushed those items off-screen vertically."

So there is a genuine layout conflict:
- **Z=0 (current default):** HamNavList menu items are placed OK-ish but help-bar
  and other UI text fall off the bottom.
- **Z=387 (`z_hack`):** help-bar/ribbon text comes on-screen, but HamNavList menu
  items get pushed off-screen. (With `z_hack`, the choose-mode/main menu *item
  labels* still did not reappear in captures — consistent with this.)

Neither mode shows everything. The in-code intent for `kNativeUICamDefault` is
**"scale distance for HD visible area"** — i.e. move the camera *back along its view
axis* (increase |Y|) so the whole UI Z-range fits the FOV without a Z shift. That
was never implemented. There is also a TODO: *"Replace with proper camera animation
from milo PropAnims."*

## Fix design (to be finalized in Planning)

Candidate approaches, in rough order of correctness:

- **(A) Implement the HD distance-scale in `kNativeUICamDefault`.** Compute a camera
  distance/projection so world-Z ∈ [min,max] of the *whole* UI (help-bar ~372,
  ribbons ~256–517, HamNavList, dialogs) maps inside NDC. This is the option the
  comment intends and the only one that resolves the conflict. Requires enumerating
  the actual Z-extents of every UI element group and picking framing that fits all.
- **(B) Make `z_hack` the default** (restores exact March behavior). Fastest, but
  reintroduces the HamNavList-off-screen problem if HamNavList still draws under
  `ui.cam`. Only acceptable if verified that HamNavList is now fine.
- **(C) Per-group camera / the PropAnim path.** Most faithful, most work.

### Acceptance criteria (must screenshot-verify ALL)
Text must be visible and correctly placed on: `main_screen` (menu item labels),
`choose_mode_screen` (DANCE/STORY/FITNESS…), the bottom help-bar prompts, an
in-flow dialog (e.g. autosave warning), and any HamNavList ribbon list — with the
menu items themselves (icons/bars) still correctly positioned. Compare against
`archive/screenshots/session37_choose_mode_text_f600.png`.

### Neutrality
The `UI.cpp` UI-cam code is under `#ifdef HX_NATIVE`, so changes there are
PPC-neutral by construction. Do not alter `RndCam::GetViewProjectXfms` non-guarded
math (`a16912fc` proved that path is load-bearing) unless run_objdiff-verified.
