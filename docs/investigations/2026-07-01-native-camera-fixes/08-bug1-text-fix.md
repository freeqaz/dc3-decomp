# Bug 1 (all UI text invisible) — ROOT CAUSE + FIX

Lane: text-fix. Worktree `/home/free/code/milohax/dc3-textfix` (branch `textfix-native`,
from main `3f68d5cf`). 2026-07-01. Both sub-bugs fixed; changes uncommitted.

## Summary

Bug 1 was two independent defects (per `06-decisive-experiments.md`):

- **1B (dominant, root cause):** EVERY glyph quad had zero vertical extent
  (`bbox=(width, 0, 0)`) → no text pixels under ANY camera. Menus, help bar,
  button icons, HUD score — all invisible while textured meshes rendered fine.
- **1A (framing):** with 1B fixed, the `[ui.cam]` prompt plane (help bar
  `A`/`Select`, `Exit Controller Mode`) projected off the bottom of the frustum.

Both are now fixed and runtime-verified.

## 1B — the decisive mechanism

Glyph quads are built in `RndText::FontMap::SetupCharacter`
(`src/system/rndobj/Text.cpp` ~2635). Text lies in the **XZ plane** (all verts
`y = 0.0`); the vertical extent of a glyph is its **Z** span `z0..z1`:

```
z0 = yPos + zOffset*size
z1 = <formula>
verts: (x0, 0, z0) (x1, 0, z1) (x2, 0, z1) (x3, 0, z0)
```

`getenv`-gated `DC3_SETUPCHAR` instrumentation (temporary) showed, for centered
menu text:

```
aspect=1.0719 size=83.500 yPos=44.750 zOff=0.000  z0=44.750  z1=44.750   ← COLLAPSED
```

`z0 == z1` ⇒ the quad is a horizontal line ⇒ zero height ⇒ invisible.

Why: the layout centers text vertically (`mAlignment` default `kMiddleCenter`),
so `WrapText`/`ComputeHeight` give `yPos = topY = th/2`, and for a single line
`th = AspectRatio*size` (see `ComputeHeight`, 100% matched). Thus
`yPos = aspect*size/2`.

The **offending commit** is the permuter sweep **`f5f704d6`** ("permuter: improve
RndText and WorldCrowd via sweep", 2026-05-27), which flipped the subtraction
operand order:

```diff
- float z1 = z0 - _tmp1 * state.mSize;   // og / correct
+ float z1 = _tmp1 * state.mSize - z0;   // permuter — behaviorally WRONG
```

- **Correct form** `z0 - aspect*size`: glyph height `z0 - z1 = aspect*size`,
  **constant regardless of yPos** — always renders.
- **Permuter form** `aspect*size - z0`: reflects the quad about `aspect*size/2`.
  For centered text `z0 = aspect*size/2` exactly ⇒ `z1 = aspect*size/2 = z0` ⇒
  collapse. (Top-align `yPos=0` and bottom-align `yPos=th` happen to still work,
  which is why the corruption slipped through.)

Subtraction is **not commutative** — the permuter's operand-order transform (a
"commutative operand-order" style move) should never have been applied here. It
was a behavioral corruption of exactly the class flagged in memory
(`comparison_operator_fix`, `stream3_fmuls_operand_order_floor`).

### The fix (PPC-neutral)

Reverted `z1` to the og form `z0 - _tmp1 * state.mSize` in `SetupCharacter`.

`RndText::FontMap::SetupCharacter` is decomp-matched shared code (not
`HX_NATIVE`), so neutrality was verified with `run_objdiff`
(`project_dir=/home/free/code/milohax/dc3-textfix`):

| form | match% normalized |
|---|---|
| permuter `aspect*size - z0` (before) | **83.8%** |
| og `z0 - aspect*size` (after fix) | **83.8%** |

**Identical** — the operand swap was match-neutral noise (both lower to `fsubs`
with the same registers, differing only in operand order which objdiff scores as
`diff_arg`). The permuter's real +match came from a co-located `italics` decl
reorder, not the `z1` flip. So the revert restores correct rendering at **zero
match cost**.

## 1A — ui.cam framing (entirely inside `#ifdef HX_NATIVE`)

`UIManager::Draw`, `case kNativeUICamDefault` (`src/system/ui/UI.cpp` ~219) was an
empty `break;` → `ui.cam` stayed at Init `pos=(0,-768,0)`, `yFov≈34.5°`, framing
only world-Z ~[-238,+238]; the help-bar/prompt plane at world-Z ~+370..+520 fell
below the viewport. Applied a FOV-widen + Z-recenter:

```cpp
const float kUiCenterZ = 160.0f;
const float kUiCamDist = 850.0f;
const float kUiYFov    = 0.919f; // 52.7deg
mCam->SetFrustum(mCam->NearPlane(), mCam->FarPlane(), kUiYFov, 1.0f);
mCam->SetLocalPos(Vector3(0, -kUiCamDist, kUiCenterZ));
```

vs the 05-plan starting point (`center=180, dist=768`): I bumped `dist 768→850`
and lowered `center 180→160` to widen the framed Z-span without increasing the
FOV (52.7° kept — no extra distortion, and far/near untouched so the
`far<=1000*near` clamp never trips). This gives **both** Z extremes margin so the
low-Z nav widgets that regression `311e3b75` worried about stay on-screen.

`WorldToScreen` screen-y (0=bottom, 1=top) before vs after, from `DC3_TEXT_DIAG`:

| text | plane Z | before (y) | after 1A (y) |
|---|---|---|---|
| `Select` / `Exit Controller Mode` (help bar) | high | **1.30 (off top)** | 0.76 ✓ |
| `This game uses an autosa…` (autosave) | mid | 0.51 | 0.32 ✓ |
| `skip` (cutscene prompt) | low | 0.077 | 0.070 ✓ |
| `Left Hand` | low | (visible) | 0.083 ✓ |

All ui.cam text now falls in `[0,1]` with margin; nothing that was visible before
is now clipped.

## Validation

- **bboxFixed:** 0 of 500 gameplay text draws report a zero-depth bbox (was
  100%). Menu/help/HUD glyphs all have proper Z extent (`skip`=21.58,
  `Select`=18.88, `Exit Controller Mode`=17.75, autosave=64.73, score `0` font
  renders).
- **menuLabelsVisible:** choose_mode screenshot (f400/f840) shows
  `Jump right in and Perform!`, `PLAYERS: 1 - 2`,
  PERFORM/REHEARSE/BATTLE/PARTY TIME/CREW THROWDOWN, `LEFT HAND`.
- **helpBarVisible:** `EXIT CONTROLLER MODE` + `SELECT` now render on-screen.
- **gameplayHudOk:** `betteroffalone.txt` (1800 frames, FAST_TIME) — score `0`
  digits and prompts render in-venue; no zero-height meshes.
- **suitePassed:** `ninja milo-tests` + `ctest -j4` → **419/419 passed, 0
  failed**.
- **objdiffChecks:** `SetupCharacter` 83.8% → 83.8% (no regression). 1A is
  `HX_NATIVE`-only (zero PPC impact).

## Files changed

- `src/system/rndobj/Text.cpp` — revert `z1` to `z0 - aspect*size` (1B root fix,
  PPC-neutral) + add the required `DC3_TEXT_DIAG` draw-loop instrumentation
  (`HX_NATIVE`/getenv-gated).
- `src/system/ui/UI.cpp` — fill in `kNativeUICamDefault` FOV-widen/Z-recenter (1A,
  entirely `HX_NATIVE`).

## Residual risks

- `SetupCharacter` sits at 83.8% for unrelated reasons (register-alloc cascade,
  offset swaps) — the `z1` line is not on the critical path to 100%, so a future
  permuter run must be prevented from re-applying the same non-commutative
  operand swap (it is match-neutral, so a naive sweep would happily re-flip it and
  re-break text). Consider a guard/comment (added) or excluding this line.
- 1A `ui.cam` framing is a native-only visual compromise: the help bar lands in
  the upper region rather than the console's bottom bar (the plane's high-Z
  prompts map to screen-top under this cam). Constants tuned by screenshot; if a
  screen with even higher/lower Z ui.cam content appears, re-tune
  `kUiCenterZ`/`kUiCamDist`.
- Bug 2 (camera roll/flip, upside-down dancer) is unrelated and untouched here.

---

# ADVERSARIAL VERIFICATION (round 1) — VERDICT: CONFIRMED

Independent verifier, 2026-07-01, worktree `/home/free/code/milohax/dc3-textfix`. All
evidence below is the verifier's own runs.

## Diff review
- `Text.cpp` DrawShowing: `DC3_TEXT_DIAG` block — inside `#ifdef HX_NATIVE`, getenv-gated → exempt.
- `Text.cpp` SetupCharacter: `z1` revert `_tmp1*mSize - z0` → `z0 - _tmp1*mSize` — SHARED decomp-matched code; neutrality checked below.
- `UI.cpp` kNativeUICamDefault: FOV/Z fill — inside the `GetNativeUICamMode()` switch, which is native-only (`#ifdef HX_NATIVE`). Zero PPC impact.

## Neutrality (run_objdiff, project_dir=worktree vs main)
Symbol `?SetupCharacter@FontMap@RndText@@UAAX...` — worktree **83.8% normalized** (83.7% raw);
main **83.8% normalized** (83.8% raw). Identical instruction breakdown (265|59/8/11/22). The
canonical/tracked metric (normalized) is UNCHANGED → gate passes. The 0.1% raw delta is the
real `fsubs` operand-order byte change and does not affect the normalized gate. Math confirms
the revert is behaviorally correct (centered single line: main form collapses z0==z1 → zero height).

## Runtime (choose_mode, 900 frames, DC3_TEXT_DIAG)
Trigger reached (main_screen@f42 → choose_mode). 500 samples: 484 nonzero-Z, 16 zero — all 16
are empty/whitespace strings (no nonempty string has zero Z). Help bar Select/Exit Controller
Mode now y≈0.76 (on-screen; was ~1.30). Screenshots f400/f840 render the description, PLAYERS,
the PERFORM/REHEARSE/BATTLE/PARTY TIME/CREW THROWDOWN list, and the help bar. (NOTE: orig-assets
lives only in the main repo — DC3_DATA must point there, not the worktree.)

## Suite
`ninja milo-tests` + `ctest -j4`: **419/419 passed, 0 failed**.

## Gameplay collateral (betteroffalone.txt, 1800 frames, FAST_TIME+TEL)
Reached game_screen, clean exit 0. 500 samples, 15 zero-Z (all empty strings). Screenshot f1700:
venue + upright dancer + score "0" + help bar all render. No nonempty zero-height meshes.

## Residuals (non-blocking)
- main_screen overshell items still off-bottom under ui.cam (Main Menu/Title/Kinect y≈1.03,
  Options/About y≈1.53) — flagged in 06-doc as likely intentionally-offscreen; not a regression.
- z1 line is normalized-match-neutral → a future naive operand-order permuter could re-break it;
  in-source comment added, exclusion recommended.
- Bug 2 (camera roll) unrelated/untouched.
