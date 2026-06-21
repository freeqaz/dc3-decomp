# Wave 23 Results — game/engine PPC frontier EXHAUSTION READ

**Date:** 2026-06-21 · all-Opus, native-gated. Self-scanning lanes over the non-Xbox,
non-world/rndobj (concurrent-agent) game+engine frontier to measure exhaustion. Native 418/418.

## Landed (2 marginal wins)
- **Vector3DESmoother::Smooth 92.9→97.2** (math) — 3 real structural bugs: mY-before-mX
  reference hack inverted the offset cascade; in-place Normalize vs 2 distinct stack slots;
  missing mPrevLevel write-back.
- **SkeletonExtentTracker::ApplyToMeshVerts 78.9→79.8** (gesture floor-grind).

## Exhaustion read (the wave's real output)
All three lanes independently confirm the non-Xbox game/engine PPC matchable frontier is
**exhausted of hand-authorable wins**:
- **Lane C (game / src/lazer): EMPTY PATCH** — nothing tractable left (wave 22 took the last 2).
- **Lane B (char/hamobj/gesture): "EXHAUSTED... do not relaunch broad sweeps"** — 24 workable
  fns are regalloc/split-FMA/FPR/EH-funclet floors; the 98-99.9 band (~50 fns) is uniform floor.
- **Lane A (obj/utl/flow/math/ui): FLOOR-DOMINATED** — fake_impl_scan found 0 missing-impl
  stubs; ~24 sub-100 fns all backend floors (REGISTER_SWAP/FPR/COMMUTATIVE/ADDRESS_RELOCATION),
  cross-checked against og-dc3 (identical source for TaskMgr::ResetTaskTime etc.).
- 1 deferred REAL bug: **ScriptTask::Replace 67.1%** (missing in-place mObjects replace; exact
  devirtualized SetObjConcrete form speculative + capped by an r30/r31 cascade; memory-mgmt path
  — not landed speculatively).

## Conclusion — lever map for the user's game+engine scope
| Lever | State |
|---|---|
| Xbox stub reconstruction (synth_xbox/rnddx9) | productive ~17/wave but **deprioritized by user** |
| Game/engine PPC matching (non-xbox, non-world/rndobj) | **EXHAUSTED** (this wave, 3-lane) |
| Build-flag audit (/TC vs /TP) | **EXHAUSTED** (json-c was the only victim) |
| world/rndobj PPC | covered by a **concurrent agent** |
| Native-port runtime correctness | **the remaining game/engine lever** (different mode) |

The orchestrated-PPC levers for the user's scope are spent. The remaining high-value game+engine
work is native-port runtime correctness (the game running on the engine) — the user's standing
"fix decomp gaps when the native port hits bugs" preference. Wave 24 attempts that lever.
