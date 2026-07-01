# Decisive experiments (gates from 05-plan.md) — results

Run by the orchestrator on the `dc3-camerafix` worktree
(`/home/free/code/milohax/dc3-camerafix`), 2026-07-01. Both diagnostics are
`HX_NATIVE` + `getenv`-gated (zero PPC impact):

- `DC3_TEXT_DIAG` in `src/system/rndobj/Text.cpp` (~line 2347 draw loop): logs the
  text ASCII, `RndCam::Current()` path, and `WorldToScreen` of the first mesh vert.
- `DC3_ROOT_DIAG` in `native/src/telemetry/GameplayTelemetry.cpp` (~line 285):
  logs player0 pelvis + transform-root world basis rows + positions per 10 frames.
- `DC3_CAM_DIAG` (same file, next block): logs `CameraManager::CurrentShot()` name,
  its `RndCam` world basis (`upZ = m.z.z`, `fwdZ = m.y.z`), basis determinant
  (handedness), and position per 10 frames.

Env for all runs: `DC3_DATA=<repo>/orig-assets MILO_HEADLESS=1 DC3_FAST_BOOT=1
DC3_SHOW_SPLASH=0` (+ `DC3_FAST_TIME=1 DC3_TEL=1` for gameplay). **Gotcha:** without
`DC3_DATA` the run dies at `gen/main_xbox.hdr` (exit 136). The text-diag stderr log
contains binary bytes — `grep`/`fgrep` silently return nothing on it; use
`strings -a | awk`.

## GATE 1 — which cam draws the menu labels, and where do they land?

Run: `to-choose-mode.txt`, 850 frames, 500 text-draw samples.

| Text | Camera | screen (x,y) | Verdict |
|---|---|---|---|
| `<altb>Dance/Story/Fitness/PARTY TIME` | `turbo_shell.cam (ui/common.milo)` | (0.686, 0.45–0.65) | **ON-screen** under baked cam |
| `PLAYERS: 1 - 2`, `Jump right in…` | `turbo_shell.cam` | (0.205–0.1, 0.41–0.55) | ON-screen |
| `A`, `Select` (help bar), `Exit Controller Mode` | `main/[ui.cam]` | y ≈ **1.298–1.301** | **OFF-screen** (~0.3 viewport below) |
| `Main Menu`, `Title Screen`, `Kinect Tuner` | `main/[ui.cam]` | y ≈ 1.764 | OFF-screen |
| `Options`, `About` | `main/[ui.cam]` | y ≈ 2.654 | OFF-screen (likely off-screen slider items) |
| autosave notice, `Left Hand`, `skip` | `main/[ui.cam]` | y ∈ 0.08–0.51 | ON-screen (draws fine today) |

**Conclusions:**
1. Bug-1 scope confirmed: the *missing* text is the **ui.cam plane** (help bar,
   prompts, overshell list). The tight y≈1.30 cluster = one UI plane sitting exactly
   one framing-gap (~0.8·476 ≈ 380 world-Z units) outside the stock 34.5° frustum —
   matches the 05-plan Z≈+370..+520 analysis. The FOV-widen + Z-recenter fix in
   05-plan.md targets exactly this plane. Items at y≈1.76/2.65 are deeper
   (probably intentionally-offscreen slider rows — verify post-fix they don't leak in;
   if the y=1.76 row (`Main Menu` header) SHOULD be visible, the framing needs to
   reach it, i.e. prefer the wider option).
2. The DANCE/STORY **menu-item labels project on-screen under `turbo_shell.cam`**.
   Whether their pixels actually appear on the unfixed build is checked by
   screenshot (see GATE 1b below). If they render → bug 1 == ui.cam framing only.

### GATE 1b — do the baked-cam labels actually render?
Screenshots f400–f840 (choose_mode): **ZERO text renders anywhere** — description
panel blank, mode list icon-only — despite the on-screen projections above.

### GATE 1c — why: ALL glyph quads are ZERO-HEIGHT
Extended `DC3_TEXT_DIAG` with mesh bbox + material. Every single text mesh reports
`bbox=(W, 0.00, 0.00)` — correct, per-string widths (`Dance`=52.77, `CREW
THROWDOWN`=149.34, button-icon `A`=14.78) but **zero height and zero depth**, across
all fonts (`Eagle-Light(47).mat`, `icons_buttons.mat`). Degenerate quads → no
pixels. Materials resolve fine.

**Bug 1 is therefore TWO defects:**
- **1A (framing):** ui.cam plane at scr y≈1.3 off-frustum → the 05-plan FOV/Z fix.
- **1B (zero-height glyphs):** the text mesh builder writes glyph X extents
  correctly but collapses the vertical axis to a constant → every string invisible
  under every camera. This is the dominant visible symptom. Root-cause target: the
  vertex-write path in `RndText::UpdateMesh`/font mesh creation
  (`src/system/rndobj/Text.cpp`, `Font*.cpp`) — find which recent commit zeroed the
  vertical component (candidates: `e815c977` wave-15 ReplaceMissingCharacters +
  XfmOnCircleEdge, `0e6ab068` OnComputeCharWidths/FitTextScroll asm-archaeology,
  `ff923ce5`, `64d12754`; also any `Font.cpp` commits — widths being CORRECT while
  height is zero suggests a height/size variable, not per-char metrics).

## GATE 2 — character root vs camera

Run: `betteroffalone.txt`, 2600 frames, pelvis/root basis every 10 frames.
Bad/good reference frames from `_wf_dense` (prior build, deterministic timeline):
f2512 bad (upside-down dancer), f2524 good.

Result across f2480–2590 (the bad window): **player0 never inverts.**
- `rootUpZ = 0.000` constant, `rootUpY = -0.901` constant (yaw-only), root z = 0
  (on floor) the whole time.
- `pelvisPos.z ∈ [32.6, 33.8]` (normal standing height), positions continuous
  (x: 18–23, y: −27..−30), no teleports, no sign flips.

**Character-root hypothesis REFUTED.** The "upside-down dancer" pixels cannot come
from the character's world transform.

### Frame evidence re-read (orchestrator)
Re-examining `assets/bug2_gameplay_f2500_upside-down-dancer.png` vs
`..f2200_GOOD..`: the **entire f2500 frame is upside-down** — the "upright venue"
that drove the angle-B redirect is the **floor-reflection half** of the frame (the
venue has a mirror floor; the pillar/stool are near-vertically-symmetric, which
disguised the flip). The bar stool stays on the LEFT in both good and bad frames →
the flip preserves left/right → it is a **vertical flip (negated up / improper,
mirror-handed basis)**, not a 180° roll (which would also swap left/right).

So bug 2 is back to **angle A: camera transform** — specifically an
improper/mirrored camera basis on certain shots (classic `LookAt` up-degeneracy or a
cross-product sign), with the venue reflection masking it. Angle B's pixel-evidence
refutation was itself based on a misread.

## GATE 2b — camera basis per shot: THE CAMERA ROLLS CHAOTICALLY

Run: same gameplay flow with `DC3_CAM_DIAG` (+ `shotFrame` from
`RndAnimatable::GetFrame()`), logs in `/tmp/cam_diag.log` / `cam_diag2.log` /
`fek_stub.log` (3 runs).

**Findings (all three runs consistent):**
1. `upZ` of `world.cam` forms a **continuum of roll angles** within single shots:
   +1.0, 0.889, 0.265, 0.032, 0.006, ~0.000, −0.218, −0.961, −0.996 — i.e. rolls of
   0°, 27°, 74°, ~90°, ~102°, ~165°, ~180°. 20–28 of ~235 samples are upside-down.
   `det` stays ≈ +1 (proper rotation → genuine roll, not a mirror), with dips to
   0.93–0.98 (non-orthonormal intermediates → matrix-lerp between distant rotations).
2. **`shotFrame` advances perfectly smoothly** (0.00, 2.50, 5.00, 7.50… monotone at
   0.25/frame) while the POSE jumps chaotically between a small set of repeating
   poses (e.g. `area1_near03.shot`: upright@(-2.2,-180.9,72.3), rolled-90°@(±21,…,72),
   upside-down@(-1.7,-178.5,95.6), low@(-5.7,-183.3,49.2)). Clean time input +
   garbage pose output ⇒ the bug is INSIDE shot evaluation:
   `CamShot::SetFrame` → `GetKey` (keyframe bracketing) → `CamShotFrame::UpdateTarget`
   (with `mLastPrev`/`mLastNext` caching) → `Interp`/`BuildTransform`/`ApplyScreenOffset`.
3. The position jumps (~21–24 units along DIFFERENT axes correlated with the roll)
   look like a screen-space framing offset applied in the rolled camera frame — one
   root cause plausibly explains both roll and teleport.
4. **Nondeterministic across runs**: at identical `shotFrame` values, different runs
   produce different poses (run2 f2160: upright; run3 f2160: rolled-90°). Suggests
   uninitialized state / stale cache (`mLastPrev`/`mLastNext`, the function-local
   `static CamShotFrame nullFrame`) or pointer-order-dependent iteration.
5. Old evidence re-read: the f2500 "upside-down dancer" frame is the WHOLE frame
   upside-down (the "upright venue" was the mirror-floor reflection; pillar/stool
   near-symmetric). GATE 2 (pelvis diag) independently proves the character never
   inverts. The user-visible "camera chaotically flipping around" = this roll churn.

### Refuted hypotheses (do not revisit)
- **Character root/pelvis inversion** — GATE 2: root upright, z=0, yaw-only, all frames.
- **`RndPropAnim::ForeachKeyframe` (3f654b92 stub→impl) corrupting shot keys** —
  stub-test (`DC3_FEK_STUB=1` restores the no-op): flip UNCHANGED (20 upZ<0). Its only
  gameplay callers are `move_sound`/`move` props on HamMove anims, not camera anims.
  (Diag env `DC3_FEK_DIAG=1` logs callers; keep for other investigations.)
- **Anim-time/beat thrash** — `shotFrame` is smooth and monotone.
- **`CameraManager::Poll` selection churn** — shot changes are clean and infrequent
  (`intro_quick` → `venue02` → `cu04` → `area1_near01/02/03` → `area1_far03`).
- **3f654b92's `CamShotFrame::Interp` switch restructure / `Shake` fabsf** — diff
  reviewed: behaviorally neutral forms. (Not re-verified at runtime; low suspicion.)

### Suspect-commit shortlist for bug 2 (recent CameraShot.cpp)
- `05ff728c` (2026-05-31) — og-dc3 port: `SetPos` 85.8→97.4, `SetFrame` 99.5→100.
  og ports have previously dropped native-safety/zero-init (see memory:
  og-ports drop HX_NATIVE guards). PRIME suspect window.
- `a7a76809` — `SetPos` Dot operand order.
- `fb98fec2` (2026-06-02) — 80-99.99 sweep harvest (check which CameraShot fns).
- `3f654b92` (2026-06-11) — Interp/Shake (reviewed-neutral, verify last).

### Repro numbers (deterministic flow, `betteroffalone.txt` → macarena song)
Gameplay starts ~f1500; richest thrash window f2080–2260 (`area1_near02/03`);
`DC3_CAM_DIAG=1 DC3_TEL=1` logs every 10 frames. Worktree
`/home/free/code/milohax/dc3-camerafix` has all diagnostics + warm native build.
