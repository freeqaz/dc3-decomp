# 2026-07-02 (part 3) — Feet-in-floor SHIPPED: faithful IK stack is the native default

Continues [[2026-07-02-feet-faithful-root-SOLVED-pelvis-retarget-stomped]] (part 2: the
poll-order root cause). This session ran both ship paths concurrently (two Fable agents,
orchestrator-driven GPU verification) and landed everything on main. **The dancers' feet
now plant at the floor on native via the game's own IK, Xbox-exact.**

## Commits (in order)

1. `06f1569d` — part-2 session work (poll-order flip opt-in, CharLocalIKScope fix,
   instrumentation, web-loop plant fix, session docs)
2. `7264136b` — **Path A**: post-poll pelvis retarget (durable LocalXfm write of the
   matched pelvis-effector lift in `Dc3RunPostPollFootPlant`). Now a fallback (see 4).
3. `00c9b165` — **Path B**: the two ankle-solve root causes (below)
4. `3fb97a37` — **producer-first poll order default ON native**; clamp + retarget
   auto-retire under it

## Path B — the two root causes of the ankle-solve divergence

### 1. `HamIKEffector::Poll` ankle clamp — REAL MIS-DECOMP (all platforms)

The Interp blend writes **`effQ` in place**, not `q`:

```cpp
Interp(neutralQ.v, effQ.v, clampFactor, effQ.v);  // was ...clampFactor, q.v)
Interp(neutralQ.q, effQ.q, clampFactor, effQ.q);  // was ...clampFactor, q.q)
```

Target asm decisive: the Interp out-args are the `effQ` stack slots (target allocates
effQ at 0x70; all 16 prior `off:+16` mismatches were that slot shift). As mis-decompiled,
the final target became `q.v = neutral + eff` — the SUM of two absolute world positions,
i.e. the long-documented "q.v = neutral + eff explodes with venue offsets" identity
(June-9 Push 12/13) was never Xbox's math at all. **Match improved 20 → 2 mismatches
(99.9% normalized; the 2 = same-register commutative-fmuls backend floor).** Plus
LP64 `intptr_t` casts (GetGroundHeight stays 100% — keeps the signed `cmpwi`).

### 2. Native `Multiply(Transform, Transform, Transform)` was alias-UNSAFE (mtx.cpp)

Callers routinely alias `out` with `b` — the PPC original computes the **translation
first** and even carries an explicit `&b != &out` branch (aliasing is a supported engine
idiom; 122 aliased Multiply sites engine-wide). A previous native rewrite did the matrix
product first, so aliased calls formed the translation from the already-clobbered product
matrix — corruption scaling with venue offset. The two effector back-transform calls
(`Multiply(effW, inv, inv)`, `Multiply(inv, finalXfm, finalXfm)`) and
`CharIKHand.cpp:341 Multiply(invFingerXfm, tf, tf)` (the ONLY other aliased
Transform-overload site — i.e. the June-9 CharIKHand divergence itself) were the victims.
Fix: translation into temporaries before any write. Same hazard hardened in `Rot.cpp`
`Multiply(Vector3, Quat, Vector3)`. Same bug family as the July-1 LookAt aliasing fix —
**third strike for "native rewrite dropped PPC aliasing semantics"; audit found no
further divergent sites.**

Why the symptoms matched: fling ∝ venue offset (offset dancers ±200, near-origin nearly
sane); pelvis effector immune (finger == effector skips the back-transform — why the lift
was Xbox-exact while ankles flung); L≫R asymmetry from branch-dependent alias exposure.

## Verification (all GPU-run by orchestrator; agents are GPU-blocked)

Ground truth: `/tmp/xenia-beat-truth.txt` from `xenia-headless.log` (thehustle/angel04).

| metric | native default (3fb97a37, no envs) | Xenia/Xbox |
|---|---|---|
| toe min / med / p90 | 0.00 / 0.10 / 1.1–1.3 | −0.00 / 0.04 / 1.5–2.1 |
| ankle min / med | 4.10 / 4.20 | 4.06 / 4.28 |
| pelvis med / max | 39.3 / 42.5 | 38.4 / 41.0 |
| below floor / flings | 0 / 0 | 0 / 0 |

Gate: 47/48 (only `NoAnkleSuddenJumps` — the pre-existing both-feet ~57u move-rewind
sampling artifact, magnitude unchanged vs old default; the faithful stack even passed it
48/48 on one roll, which the old default never did). Full ctest suite green. PPC:
`Poll` 99.9%, `GetGroundHeight` 100%, `Sort` 100%.

## Switch semantics after the flip (all native-only)

- **Default**: producer-first poll order; game's own IK plants feet; clamp + retarget OFF.
- `DC3_POLL_ORDER_FIX=0`: old (reversed) order + post-poll plant clamp + pelvis retarget
  fallback stack (the 7264136b behavior — toes held at +0.60, pelvis lifted post-poll).
- `DC3_FEET_POST_PLANT=1`: force the clamp back on under the faithful stack.
- `DC3_FEET_POST_PLANT_OFF=1` / `DC3_FEET_PELVIS_OFF=1`: kill the fallbacks individually.
- `DC3_IK_LOCALSCOPE` / `DC3_IK_NEUTRAL`: were compensating for the mis-decomp —
  redundant now, candidates for deletion after a soak.

## Ops notes (cost several runs this session)

- The gameplay gate's popen child has a hardcoded 180 s timeout; under heavy box load
  (concurrent worktree/RB3 builds) the child crawls or gets killed **silently** mid-run —
  mass test failures with a truncated child log and NO crash marker. Always re-check on
  a quiet box / repro the child command standalone before believing a red gate.
- Headless runs ~200 fps: 9050 frames ≈ 45–70 s wall. Wall-clock sleeps overshoot runs.
- `/api/health` never contains "playing" — poll the DC3_TEL log for `state=playing`.
- Native screenshots during a song show black + "SKIP": the song-intro movie overlay
  never ends (Bink stubbed) and covers the world plane all song. Visual checks → web build.
- `AutomationReachesMultiUserScreen` is flow-timing flaky; passes in isolation.

## Follow-ups

- Soak, then delete `DC3_IK_LOCALSCOPE` / `DC3_IK_NEUTRAL` scaffolding and consider
  removing the (now default-off) clamp/retarget fallbacks.
- Re-test hand IK visuals sometime: `CharIKHand` aliased Multiply is fixed — hands may
  have silently improved too.
- The move-rewind ~57u whole-body teleport artifact (NoAnkleSuddenJumps) is real game
  behavior at the routine loop boundary or a separate driver bug — investigate separately.
- Land the `Interp`/`intptr_t` improvements upstream knowledge: the "q.v = neutral + eff"
  identity in older docs (2026-06-09) is superseded — annotate if anyone reads it fresh.
