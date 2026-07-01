# Bug 2 — gameplay camera chaotic roll: ROOT-CAUSED + FIXED (2026-07-01)

Worktree: `/home/free/code/milohax/dc3-camerafix`. Fix landed uncommitted there.

## TL;DR

The gameplay camera rolled chaotically (upZ continuum +1 .. −0.996, whole scene
flipping) and teleported between a small set of repeating poses. Root cause: a
**wrong decomp of `Transform::LookAt`** in `src/system/math/mtx.cpp` — the two
basis rows were written to the **wrong axes AND in the wrong order**, and because
every camera caller passes `xfm.m.z` *as the up-hint*, the wrong write-order made
the forward and up rows **identical → a degenerate (rank-deficient) matrix** whose
`Normalize()` output is dominated by floating-point noise → nondeterministic roll.

One-line fix (shared, PPC-faithful — now matches the target asm and og-dc3):
```cpp
// src/system/math/mtx.cpp  Transform::LookAt
Subtract(target, v, m.y);   // forward (target - pos) -> m.y   (was: -> m.z)
m.z = up;                   // up-hint            -> m.z   (was: m.y = up)
Normalize(m, m);
```

Result: upside-down camera samples **20–28 → 0** (2600-frame repro, 2 reruns, det
now constant 1.000); all 5 screenshots upright; native suite **419/419**.

## How it was pinned down (instrument-first)

Added three getenv-gated `HX_NATIVE` diagnostics to `CameraShot.cpp` (kept in the
worktree):
- `DC3_CAM_DIAG2` (in `CamShotFrame::Interp`) — per-eval `thisTf`/`otherTf`/`resultTf`
  upZ, target positions, `f1`/`f2`/`blendT`, target flags.
- `DC3_CAM_DIAG2_KEY` (in `CamShot::SetFrame` after `GetKey`) — `nKeys`, selected
  prev/next keyframe indices, `keyBlend`, cached `mLastPrev`/`mLastNext`.
- `DC3_CAM_DIAG3` (in `CamShotFrame::BuildTransform`) — upZ of `mWorldOffset`, after
  the `WorldXfm` multiply, and **after `ApplyScreenOffset`** (the final row), plus
  target + position.

Evidence chain (betteroffalone flow, `area1_wide01.shot`):
1. `DIAG2`: prev keyframe (`this`) is rock-solid `thisUpZ=1.000`; the **next**
   keyframe (`other`, `thT=1` has-targets) swings `otherUpZ ∈ {+1, +0.71, 0, −0.71,
   −0.99}` (a full revolution). `f1=f2=blendT=1.000`, so no camera-feedback blend —
   the roll is fully inside the target-bearing keyframe's `BuildTransform`.
2. `DIAG2_KEY`: `nKeys=1` — the shot holds on a **single keyframe**, so `prev` is
   null every frame and evaluation goes through the shared function-local
   `static CamShotFrame nullFrame` → `nullFrame.Interp(*frame54)`.
3. `DIAG3`: **decisive.** `woUpZ=0.998`, `afterWorldUpZ=0.998` (upright right up to
   the last step), but `finalUpZ` (post-`ApplyScreenOffset`) is chaotic
   (`0, −0.699, −0.989, +0.989, −0.997`) **while the target `(5.0,−24.0,58.3)` is
   perfectly stable**. So `ApplyScreenOffset` — i.e. its `LookAt` — is the sole
   source of both the roll and the position teleport.

`ApplyScreenOffset` calls `xfm.LookAt(mLastTargetPos, xfm.m.z)`. The buggy
`Transform::LookAt` body was:
```cpp
m.z.Set(target.x - v.x, target.y - v.y, target.z - v.z);  // write m.z (forward)
m.y = up;                                                  // up ALIASES m.z!
Normalize(m, m);
```
`up` is a reference to `xfm.m.z`, which the first line just overwrote with the
forward vector. So `m.y = up` copies the forward vector into `m.y` → `m.y == m.z` →
degenerate basis → `Normalize` renormalizes noise → chaotic, nondeterministic roll.
The position teleport is the follow-on `Multiply(vother, xfm, xfm.v)` using that
garbage basis.

## Ground truth (why the fix is PPC-correct, not a native band-aid)

Target asm of `?LookAt@Transform@@QAAXABVVector3@@0@Z` (Transform layout:
m.x@0x00, m.y@0x10, m.z@0x20, v@0x30):
```
fsubs ... ; stfs f12,0x10 ; stfs f13,0x14 ; stfs f0,0x18   # (target - v) -> m.y
lwz/stw x4 -> 0x20,0x24,0x28,0x2c                          # up (4 words) -> m.z
b Normalize
```
i.e. the original writes **forward → m.y, up → m.z** — exactly og-dc3's inline
`Mtx.h` form `Subtract(v1, v, m.y); m.z = v2;`. og-dc3 is a faithful decomp of the
**same** debug binary, so this is authoritative. The DC3 `mtx.cpp` version (added in
`793a8a4e`) had the rows swapped *and* the aliasing hazard — it was wrong on both
counts and, per objdiff, was an unpaired 0% stub (the target emits `LookAt` as a
COMDAT attributed to `ClipCollide.obj`; DC3 emits it out-of-line in `mtx.obj`, so
they never paired). The fix therefore cannot lower any tracked match%: the mtx-unit
matched fns (`FastInvert` 99.8%, `Det`/`Invert Matrix3` 100%) are independent of the
`LookAt` body and were re-verified unchanged.

Note this is engine-wide: **every** `Transform::LookAt` caller (camera, `ClipCollide`,
`CharEyes` head look-at, …) was getting a degenerate basis. The suite going 419/419
with the corrected convention confirms callers expect the og/target convention.

## Validation

| metric | before | after |
|---|---|---|
| upZ<0 samples (2600-frame repro) | 20–28 (nondeterministic) | **0** (2 reruns) |
| basis det | dips 0.93–0.98 | constant **1.000** |
| screenshots f2100/2200/2500/2520/2550 | whole scene upside-down on bad frames | **all upright**, stable framing |
| native suite | — | **419/419 pass** |
| objdiff FastInvert (mtx unit) | 99.8% | 99.8% (unchanged) |

Screenshots in `/tmp/lane2_shots/` (frame_02100..02550): dancer upright & dancing,
bar stool upright with correct mirror-floor reflection, venue pillar stable.

## Files changed
- `src/system/math/mtx.cpp` — `Transform::LookAt`: correct axis rows + write order
  (forward→m.y, up→m.z), matching target asm / og-dc3. Shared code; PPC-faithful.
- `src/system/world/CameraShot.cpp` — added `DC3_CAM_DIAG2` / `DC3_CAM_DIAG2_KEY`
  (in `Interp` / `SetFrame`) and `DC3_CAM_DIAG3` (in `BuildTransform`), all
  `HX_NATIVE` + getenv-gated. Diagnostic-only; kept for the verifier.

## Residual risks
- `Transform::LookAt` remains **out-of-line** in `mtx.cpp` while the target emits it
  as an inline/COMDAT (attributed to `ClipCollide.obj`), so objdiff still won't pair
  it as a standalone symbol (stays a 0% stub in the DB even though the body is now
  byte-correct). Making it inline in `Mtx.h` (as og-dc3 does) would let it pair AND
  match the target's COMDAT emission, but that changes every caller's codegen
  (inlined vs `bl LookAt`) — a separate, higher-risk decomp task. Deferred.
- The existing `HX_NATIVE` NaN/degenerate guards in `ApplyScreenOffset`/`BuildTransform`/
  `Interp` are now largely dead (the basis is no longer degenerate) but are harmless
  and left in place as defence-in-depth.
- Camera **shot selection** still has run-to-run variation (different shots picked
  across runs — `Shake`/pick-shot randomness), which is expected DC3 behavior; no
  shot inverts in any run now.

## Adversarial verification (round 1) — CONFIRMED 2026-07-01

Independently re-verified in worktree `dc3-camerafix` by the review tier. Every
number below is from the verifier's own runs, not the implementer's.

**Neutrality (shared `Transform::LookAt` change):**
- `?LookAt@Transform@@QAAXABVVector3@@0@Z` is a `Stub (High)` 0% *unpaired* symbol on
  BOTH main (`dc3-decomp`) and the worktree — objdiff never pairs it (target emits it
  as a COMDAT attributed to `ClipCollide.obj`; DC3 emits out-of-line in `mtx.obj`).
  Changing its body therefore cannot regress any tracked match%.
- `FastInvert` (the mtx-unit matched fn) = **99.8% normalized both sides**, its lone
  mismatch a pre-existing commutative `fmuls` operand swap — unchanged by the fix.
- Worktree fix body is byte-identical to og-dc3's faithful inline form
  (`Mtx.h:241`: `Subtract(v1, v, m.y); m.z = v2; Normalize(m, m);`). The old HEAD body
  was the swapped/aliasing decomp from `793a8a4e`. Confirmed the aliasing hazard is
  real: `CameraShot.cpp:297` calls `xfm.LookAt(mLastTargetPos, xfm.m.z)` (up aliases
  the row the old first statement overwrote).
- All other changed hunks (`GameplayTelemetry.cpp`, `PropAnim.cpp`, `Text.cpp`,
  `CameraShot.cpp` DIAG2/2_KEY/3) are `#ifdef HX_NATIVE` + getenv-gated → absent from
  the PPC build → zero match% impact.

**Runtime (verifier's own `betteroffalone` GPU run, `MILO_MAX_FRAMES=2600`):**
- 236 `DC3_CAM_DIAG` samples spanning f240–f2590; gameplay reached
  (`state=playing screen=game_screen`, shots `venue01`/`area1_near01/03`).
- **upZ<0 samples: 0** (was 20–28). **det<0.9 samples: 0** (was dips 0.93–0.98).
  upZ range **0.963 … 1.000**, all upright.
- Screenshots f2100/2200/2500/2520/2550: **all upright** — dancer dancing, bar stool
  and pillar vertical, correct mirror-floor reflection, sane framing. f2500 (formerly
  whole-scene-inverted) is upright. (`/tmp/lane2_verify_shots/`.)

**Suite:** `100% tests passed, 0 tests failed out of 419`.
**Menu-flow collateral:** `to-choose-mode.txt` (850f) boots clean to
`choose_mode_screen`; the only "assert/FAIL" log hits are benign pre-existing UIScreen
*names* (`campaign_outro_fail_screen` etc.), a `blackmask.mnm` version warning, and the
expected missing `intro.bik` — none new, none related to `mtx`/`LookAt`.

**Verdict: CONFIRMED.**
