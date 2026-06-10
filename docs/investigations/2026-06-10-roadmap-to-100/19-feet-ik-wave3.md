# 19 — Wave 3 Lane A: feet/IK gate now REACHABLE — measured residual at gameplay

**Date:** 2026-06-10. **Lane:** Wave 3 Lane A (`wave3/a-gameplay-feet`).
**Worktree:** `/home/free/code/milohax/wt-wave3-a-gameplay-feet`.
**Predecessor:** `18-feet-ik-lane-c.md` (Wave-2 Lane C — gate was BLOCKED on the boot crash).

All measurements below were computed in this worktree (native RelWithDebInfo,
`milo-tests` run from the main repo's `orig-assets/` with `DC3_DATA` set; engine boots via
`scripts/dc3-input-flows/ymca.txt`). No `decomp.db` writes; no main commits.

## The gate is no longer blocked

Wave-2 Lane C reported `FeetNotBelowFloorDuringGameplay` as **BLOCKED** — it could not
reach gameplay because `dc3-native` crashed before any gameplay frame (first in
`CameraManager::RandomizeCategory`, fixed in Wave-2; then in `Sound::SynthPoll`, the
named next blocker). **Wave-3 Lane A fixed `Sound::SynthPoll`** (see `15-native-stub-worklist.md`
§Wave 3), and the boot now runs the full UI chain to `game_screen` / `state=playing`.

The gate test now **RUNS and FAILS** (it is designed to fail until the bug is fixed — the
test header explicitly says "Do NOT relax this threshold"). This is the expected, intended
outcome: a previously-unreachable bug is now empirically observable.

## Measured ankle/toe telemetry at gameplay (the deliverable)

`DC3_GAMEPLAY_TESTS=1 ... milo-tests --gtest_filter=GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay`
(818 foot samples on `game_screen`, `state=playing`):

| Foot | Worst toe Z | Ankle Z at worst | Below floor (Z < -2) |
|------|------------:|-----------------:|---------------------:|
| Left  | **-4.30** | +2.00 | 801 / 818 samples |
| Right | **-4.30** | +1.60 | 781 / 818 samples |

Steady-state during a held gameplay frame window (HTTP-polled live run):
`lAnkleZ ≈ 1.8, lToeZ ≈ -4.2, rAnkleZ ≈ -0.0, rToeZ ≈ -3.9`.

A 1280×720 gameplay screenshot was captured via `/api/screenshot` (the dancers render;
the floor-penetration is at the feet, consistent with the telemetry).

## Residual narrowed further (with the new data)

The new ground truth sharpens the prior narrowing:

- **The ankle plants; the TOE sinks.** Both ankles sit at/above the floor
  (+1.6 … +2.0); both toes are ~4 units **below** it (−3.9 … −4.3). The divergence is
  **toe-relative-to-ankle**, NOT a whole-foot drop. So whatever clamps/places the ankle
  works; the toe is not being lifted with it.
- **This matches the in-isolation evidence.** Wave-2's `ClipPoseFixture` (venue-free)
  showed the crouch clip *plants* the foot (toe ABOVE floor) — so decode is faithful and
  the sink only appears on the **gameplay song-move + venue path**. The new gameplay
  numbers confirm the sink is introduced there, not by decode.
- **The baseline ~-4.2 toe was already documented.** `src/system/char/CharIKFoot.cpp`
  (Push 13/14 comments) records "baseline (off) is stable at toe ~-4.2" — the exact value
  measured here (-4.3). Several `Dc3CleanPlant` plant-repair experiments are present but
  **gated OFF** because each either destabilized the solver or fixed only one leg
  (one-sided). The CharIKFoot comment block (lines 57-64) pins the mechanism candidate:
  the IK solver reads a leg bone **after the move pose** at a point where the
  toe-target/ankle world position is transiently wild (z ~150 / −120) and the solver
  diverges — i.e. a **poll-order / read-stale** issue in *when* the foot-plant IK runs
  relative to the song-move pose application (`HamDriver.cpp:95-101`).

## CLOSED leads (do NOT re-litigate — refuted in Wave-2 with asm)

- **`CharIKFoot::DoFSM` int-vs-float field** (offset 0x30/0x34) — REFUTED with asm
  (it is `Transform::v.x/.y`, float; `lwz/stw` is an MSVC float-bit-copy; DoFSM is a
  97.4% regalloc floor, zero logic diff). Not a feet lever.
- **`HamIKEffector::mConstraints` empty** — FAITHFUL (serialized-data-only; Xbox capture
  also measured 0 on all five effectors). No native wiring gap.

## What is NOT done (honest status)

The feet bug is **NOT fixed** — the toe still sinks ~4 units. A fix was not attempted in
this lane because: (a) the residual is a deep IK poll-order/read-stale divergence with
multiple prior plant-repair experiments already tried and gated off as destabilizing or
one-sided (`CharIKFoot.cpp` Push 13/14); and (b) shipping a risky IK change would
jeopardize the now-working full-gameplay boot that this lane just unblocked. The
acceptance item permits delivering "ankle/toe telemetry before/after with the residual
cause narrowed further" when the gate cannot pass yet — that is what is delivered:

| Before (Wave 2) | After (Wave 3) |
|---|---|
| Gate BLOCKED — boot crashed before any gameplay frame; **zero** gameplay foot samples | Gate REACHABLE — 818 gameplay foot samples; worst toe Z **-4.30**, ankle **+1.6…+2.0** |
| Residual hypothesized (song-move/poll-order), unmeasured | Residual measured: ankle plants, **toe** sinks ~4u → toe-vs-ankle divergence on the gameplay path; `HamDriver.cpp:95-101` poll-order remains the leading candidate |

## Recommended next step (for the orchestrator / a feet-focused lane)

The boot is unblocked, so a dedicated IK lane can now iterate against the live gate. Attack
the **poll-order / read-stale** candidate directly: instrument *who writes each leg bone
last per frame* (the `DC3_IK_DIAG2` counter already exists in `CharIKFoot.cpp`) during
`game_screen` and check whether the foot-plant IK reads the toe/ankle world transform
**before** the song-move pose has finalized it. Do NOT spend effort on DoFSM int/float or
mConstraints (both closed). The `Dc3CleanPlant` experiments (gated env vars
`DC3_DRIVER_ZEROBASE`, etc.) are a starting menu but each was already found one-sided —
the fix likely needs to be symmetric and ordered, not a per-leg plant patch.
