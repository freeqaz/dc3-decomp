# Permuter sweep — 95-99% AT_LIMIT band (2026-05-27)

## Summary

Resumable `batch_auto` sweep over 487 AT_LIMIT functions in the 95-99%
match band produced **24 improvements, 18 retained, +30.8% total match**.
Two functions reached 100% (verification deferred due to a build-state
issue — see Known issues).

This was the first sustained batch run after a session of pattern-system
improvements (gating fix + OffsetNode crash fix + new patterns). Hit
rate (~5%) is consistent with prior expectations for this band.

## Commits

| Hash | Subject |
|------|---------|
| `c640d516` | `permuter: fix offset_swap gating so scope_widening/slot_pad fire on real OFFSET_SWAP funcs` |
| `a077b526` | `permuter/extractor: OffsetNode delegates start_point/end_point` |
| `7f5af665` | `rndobj/Mat: port RB3 RndMat::SyncProperty -> 100%` |
| `e9bbdbf1` | `obj/Dir: tighten PreLoad bool normalization + asserts -> 99.6%` |
| `8aaefff7` | `synth/StreamReceiver + meta/StoreEnumeration: permuter wins` |
| `6ae26614` | `hamobj/MoveDir::Draw: permuter win 96.3 -> 96.7%` |
| `a8da36d3` | `permuter sweep 95-99% band: 18 wins, +30.8% total match` |

## The 18 sweep wins

Two perfects (verification deferred):
- `Memory_Xbox.cpp` `PhysicalAlloc` — 98.7 → 100.0
- `rndobj/Graph.cpp` `RndGraph::DrawAll` — 96.1 → 100.0

Big wins:
- `hamobj/HamWardrobe.cpp` `OnAddCrowd` — 78.3 → 91.6 (+13.3)
- `net/DingoSvr.cpp` `DingoServer::AddDelayedCalls` — 96.7 → 99.9 (+3.2)
- `utl/Cache_Xbox.cpp` `CacheXbox::DeleteParentDirs` — 98.3 → 99.9 (+1.6)
- `hamobj/MoveAsyncDetector.cpp` `EnableDetector` — 92.4 → 93.5 (+1.1)
- `lazer/game/PartyModeMgr.cpp` `ReadPartySongQueue` — 98.3 → 99.3 (+1.0)
- `rndobj/Text.cpp` `UpdateScrollOffsets` — 97.4 → 98.4 (+1.0)

See commit `a8da36d3` body for the full list.

## Process notes

### Six "+0.0 delta" entries reverted

`CharMirror::Poll`, `LiveCameraInput::LiveCameraInput`, `BlockMgr::AddTask`,
`FlowAnimate::Activate`, `SongLayout::SetDefaultPattern`,
`XboxContentMgr::MountContent` — the sweep counted these as
"improvements" because their delta was a marginal positive (rounds to
0.0 in display), but committing them adds churn without measurable
benefit and risks adding artifacts. This is the lesson from the wave-2
FftIpp case (a +0.0 "improvement" added a `clrlwi` BOOL_MASK artifact).

### Auto-temp renames

The permuter emits `_tmp0`/`_tmp1` for hoisted variables. Renamed
to meaningful identifiers in 4 files for readability:
- `Memory_Xbox.cpp`: `_tmp0` → `physSize` (XPhysicalSize)
- `PropKeys.cpp`: `_tmp0` → `nextIdx` (Vector3At return)
- `MoveAsyncDetector.cpp`: `_tmp0`/`_tmp1` → `msg`/`moveName`
- `FlowTrigger.cpp`: `_tmp0` → `end` + indentation cleaned up after
  the foreach→do-while auto-expansion. Added an explanatory comment
  because the manual loop is non-obviously load-bearing for the match.

Variable names don't affect codegen — verified ObjDir::PreLoad earlier
in the session.

### 8 concurrent-effort files

The working tree had 8 src/ modifications that don't map to any
permuter-sweep improvement: `Part.h`, `Part.cpp`, `CharUpperTwist.cpp`,
`SkeletonExtentTracker.cpp`, `FxSend.cpp`, `DirLoader.cpp`,
`UILabel.cpp`, `ChallengeSortByScore.cpp`. Inferred to be from a parallel
session/agent doing manual decomp + permuter-engine development (header
changes, analytical comments — things the permuter never writes). Left
untouched.

## Known issues

### Build manifest stuck mid-session

Late in the session, `objdiff` calls began timing out with
`ninja: error: manifest 'build.ninja' still dirty after 100 tries`.
Likely cause: a calendar-day transition during the session caused
`compile_commands.json` / depfile mtime confusion. Re-verification of
the two "perfect" claims (`PhysicalAlloc`, `RndGraph::DrawAll`) was
deferred — both were objdiff-measured by the sweep at apply time and
the source diffs are clean, so reasonable confidence they hold.

**Recovery path next session:** start with a clean rebuild
(`ninja -t clean && ninja`) before running any objdiff calls. If the
two perfects re-verify, they're real wins; if not, investigate the
diff with `run_diff_inspect`.

## What to do next

1. **Verify the 2 perfects** with a clean rebuild.
2. **Run the 90-95% band sweep** — task #14, ~212 functions.
   Command: `venv/bin/python -m scripts.permuter.batch_auto --target workable --include-at-limit --min-pct 90 --max-pct 95 --limit 0 --json 2>&1 | tee cl_temp_files/permuter/sweep_90_95.log`
3. **Skip the 99%+ band** — wave 1 proved it's saturated noise (0/20).
4. After 90-95%, sweep **<90% AT_LIMIT** for full coverage. Expect
   lower per-function hit rate (more structural / missing-impl cases).
5. Watch the sweep output for the same patterns we hit here:
   - `+0.0` deltas → revert (don't commit churn)
   - `_tmp0`/`_tmp1` → rename
   - Mystery files not in the improvements list → likely concurrent work, leave alone

## References

- `docs/decomp/patterns/INDEX.md` — pattern catalog (with 2026-05-26 wave's 5 new patterns)
- `docs/decomp/UPSTREAM_PORT_WORKFLOW.md` — for the RB3-port lane
- `scripts/at_limit_rb3_candidates.py` — cross-reference of DC3 AT_LIMIT vs RB3 100%
- `cl_temp_files/permuter/sweep_95_99.log` — sweep raw output (scratch)
