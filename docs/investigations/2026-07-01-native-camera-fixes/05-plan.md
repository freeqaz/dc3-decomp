# Consolidated implementation plan (Tier-1 output)

Produced by the Planning/Discovery workflow (4 agents: bug1 design, bug2 root-cause
×2 independent angles, synthesis). Both fixes are DC3-side, `HX_NATIVE`-guarded,
PPC-neutral. **Two "instrument-first" gates must be resolved before landing** (below).

## Bug 1 — ui.cam text off-screen

**Root cause (confirmed):** `src/system/ui/UI.cpp` `UIManager::Draw` switch (~line
218), case `kNativeUICamDefault` is an empty `break;` → `ui.cam` stays at Init
`pos=(0,-768,0)`, `yFov=0.602 (34.5°)`, framing only world-Z ∈ ~[−238,+238]; ui.cam
text (help-bar/prompts) at Z ≈ +370..+520 falls below the viewport. `311e3b75`
dropped the old unconditional `SetLocalPos(0,-768,387)` because Z=387 pushed low-Z
nav widgets off the *top* — leaving no framing at all.

**Fix (FOV-widen + Z-recenter, in the `kNativeUICamDefault` case):**
```cpp
case kNativeUICamDefault: {
    // Widen ui.cam FOV + recenter Z so the whole [ui.cam] world-Z span
    // (low-Z nav ~ -160 .. high-Z help-bar/prompts ~ +370..+520) fits ONE
    // frame. Stock 34.5deg frames only ~[-238,238]. near/far unchanged so
    // RndCam::SetFrustum's far<=1000*near clamp (Cam.cpp:286) never trips.
    const float kUiCenterZ = 180.0f;
    const float kUiHalfZ   = 380.0f;   // frames Z in [-200,+560]
    const float kUiCamDist = 768.0f;
    const float yFov = 0.919f;         // 2*atan(380/768) ~= 52.7deg
    mCam->SetFrustum(mCam->NearPlane(), mCam->FarPlane(), yFov, 1.0f);
    mCam->SetLocalPos(Vector3(0, -kUiCamDist, kUiCenterZ));
    break;
}
```
Why FOV-widen not distance-scale: reaching half-Z=380 by distance alone needs
D≈1224 → far would exceed 1000·near → tripped by the `Cam.cpp:286` clamp. Hybrid
fallback if 52.7° distorts: `kUiCamDist=900, yFov=0.80` (45.8°). Tune by screenshot.

**GATE 1 (scope):** discovery proved the DANCE/STORY/FITNESS **menu-item labels are
under BAKED panel cams** (`turbo_shell.cam`/`camera1.cam` in `choose_mode.milo_xbox`/
`main.milo_xbox` via `PanelDir::CamOverride`), NOT ui.cam — `z_hack`/`rotate_hack`
left them pixel-identical. So this fix may fix help-bar/prompt text but NOT the menu
labels. **Must instrument `RndText::DrawShowing` to log `RndCam::Current()->Name()` +
`WorldToScreen` for a DANCE/STORY glyph** and confirm scope before claiming bug 1
fully fixed. If labels are under a baked cam and off-screen/undrawn → separate
follow-up defect (baked-cam framing, or intro-cam anim not settling headless).

## Bug 2 — "camera flip" is actually the CHARACTER inverting

**Root cause (redirected by pixel evidence; angle B confirmed, angle A refuted):**
Across bad/good gameplay frames the **venue (bar stool, pillar, floor) is upright and
near-static**; only the **dancer** inverts/teleports. A flipped camera rotates the
whole scene — it doesn't. So the **view matrix is correct**; the defect is the
**character root/pelvis world transform driven inverted** in the move-graph/song-anim
path, NOT camera code. `CameraManager::Poll` is 99.9% matched; `cameraBlend`/
`blendFrames*` are dead knobs (no consumer); `sFlipYZ`/`GetViewProjectXfms`/
`mInvWorldXfm` proven correct. IK telemetry clean but logs no root/pelvis basis.

**GATE 2 (decisive instrumentation, do first):** in
`native/src/telemetry/GameplayTelemetry.cpp` (~line 282, pelvis handle already
resolved) log pelvis world basis under `DC3_ROOT_DIAG`:
```cpp
fprintf(stderr, "DC3_ROOT_DIAG f=%d pelvisUpZ=%.3f pelvisFwdZ=%.3f pos=(%.1f,%.1f,%.1f)\n",
        frame, pelvisD->WorldXfm().m.y.z, pelvisD->WorldXfm().m.z.z,
        pelvisD->WorldXfm().v.x, pelvisD->WorldXfm().v.y, pelvisD->WorldXfm().v.z);
```
Re-run `betteroffalone.txt`, dense window f2500–2560 (cross-ref `_wf_dense`: f2512
bad, f2524 good).
- If `pelvisUpZ` flips sign on bad frames → **character-root bug (PRIMARY)** → fix in
  char/song-anim path (`src/system/hamobj/{MoveMgr,ClipPlayer,HamDirector}`, `src/system/char/*`).
- Fallback: root stays upright but position flies out of a static frame → **camera
  not cutting/tracking** (beat-driven `pick_shot`/`ForceCameraShot` not firing) → fix
  shot advancement. (Add a per-Poll `mCurrentShot->Name()` log to confirm frozen shot.)

**Do NOT** edit `CameraManager::Poll`/`CamShotFrame::Interp`/`Cam.cpp`/`sFlipYZ` for
the flip — proven not the cause. Optional separate hardening: the native NaN guards
(CameraManager.cpp:237-264; engine Rnd_Wgpu.cpp:1286) check only camera *position*,
not rotation, and reset to identity-at-origin — a latent whole-scene-flip risk, not
this bug.

## Neutrality
Bug1 change is entirely inside `#ifdef HX_NATIVE` → zero match% impact. Bug2 fix must
be `HX_NATIVE`-guarded or `run_objdiff`-verified if it touches PPC-matched math. Do
not touch `RndCam::GetViewProjectXfms`/`projMtx.y.y` (a16912fc, load-bearing).

## Status of the two gates — RESOLVED 2026-07-01, PLAN PARTIALLY SUPERSEDED

**See `06-decisive-experiments.md` — it supersedes this doc where they conflict:**
- GATE 1: ui.cam framing fix above still stands (sub-bug 1A), but the dominant
  symptom is **sub-bug 1B: ALL text meshes have zero-HEIGHT glyph quads** (correct
  widths) → no text pixels under ANY camera. Root-cause 1B in the text/font mesh
  vertex path before/alongside landing 1A.
- GATE 2: character-root hypothesis REFUTED (pelvis diag), ForeachKeyframe
  hypothesis REFUTED (stub test). The camera genuinely ROLLS chaotically
  (upZ continuum, det≈+1) while `shotFrame` advances smoothly, nondeterministic
  across runs. **The "Do NOT edit CamShotFrame::Interp/CameraManager::Poll" guidance
  below is WITHDRAWN for the shot-evaluation path** — the defect lives in
  `CamShot::SetFrame`/`GetKey`/`UpdateTarget`-caching/`Interp`/`BuildTransform`/
  `ApplyScreenOffset` (CameraShot.cpp). `CameraManager::Poll` and `Cam.cpp`
  (`GetViewProjectXfms`/`projMtx.y.y` a16912fc) remain off-limits/proven-fine.
