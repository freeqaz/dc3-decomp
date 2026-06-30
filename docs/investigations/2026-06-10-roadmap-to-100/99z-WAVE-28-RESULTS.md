# Wave 28 Results — native-correctness audit → 3 crash guards landed + rank-5 disproven

**Date:** 2026-06-30 · Orchestrator-driven (GPU runtime-verified) + ultracode audit workflow.
Continues the native-port correctness lever (waves 24-27). User: "continue work on those remaining
next tasks ... delegate via subagents and ultracode."

## The audit (ultracode workflow `wf_1f186e59`)
5 static finder tracks (flow over-activation, #37 scoring-design, crash/assert/stub hunt in
party/practice/freestyle + campaign/battle/multiplayer, HX_NATIVE guard drift) → adversarial
per-finding verification → synthesis. 29 agents, ~2.2M tokens, 23 candidates → **10 confirmed real →
7 ranked worklist items**. Subagents are GPU-sandbox-blocked, so the workflow ROOT-CAUSES + ranks only;
the orchestrator drives all GPU runtime verification and merges. Full result in the wave transcript.

## Landed: 3 PPC-neutral crash guards (`6aff78ee`) — audit ranks 1-3
All three are latent null-deref / OOB crashes on un-exercised native paths where an `#ifdef HX_NATIVE`
arm was missing or fell through. Every edit lives wholly inside `#ifdef HX_NATIVE` (or the native arm
of an existing `#ifndef HX_NATIVE`), so PPC codegen is **byte-identical** — proven not just by
construction but by `run_objdiff`: `SkeletonUpdateHandle::Callbacks` stayed **100.0%** (3 instrs, all
equal — the load-bearing proof that HX_NATIVE is absent from the objdiff build), `MoveDir::UpdateOverlay`
87.2%, `HamDirector::GetClipStartAndEndBeats` 81.4% (all unchanged baselines).

1. **`SkeletonUpdateHandle::Callbacks()`** (`src/system/gesture/SkeletonUpdate.cpp:35`) — the ONLY
   sibling accessor missing the `if(!mInst) return ...` guard the other seven carry. Native never
   creates `sInstance`, so `InstanceHandle()` returns a handle wrapping null; the bare
   `return mInst->mCallbacks` deref'd NULL+0x94. Now returns a static empty fallback. **Locked in by a
   deterministic milo-tests unit test** (`test_skeleton_callbacks_guard.cpp`, no GPU/camera) — the
   clean headless gate the rank-1 verifier asked for.
2. **`MoveDir::UpdateOverlay()`** (`MoveDir.cpp:2186`) — the move-debug overlay binds
   `*handle.GetCameraInput()` (null on native) which `SkeletonViz::Visualize` immediately dereferences.
   Guarded the one live site (the verifier corrected the finding: PoseFatalities/Freestyle sites are
   dead/empty under HX_NATIVE — not fixes).
3. **`HamDirector::GetClipStartAndEndBeats()`** (`HamDirector.cpp:3098`) — the out-of-range branch
   wrapped only the Xbox throw in `#ifndef HX_NATIVE`, leaving an empty native if-body that fell
   through to index the vector with `(size_t)-1` when `KeyLessEq` returns -1 (practice frame precedes
   the first clip key, reached via `reteleport` on practice entry / scrub / restart). Native arm now
   returns nullptr like the function's other not-found paths.

**Verification:** suite **419/419** (418 baseline + new guard test, 0 fail); headless betteroffalone
boot→perform→**endgame→final_results** clean (exit 0, engine stable, no SIGSEGV/assert).

## Disproven: rank-5 NAV select/highlight flow over-activation — NOT A BUG
The audit's cosmetic, existence-UNPROVEN finding (choose_mode/main enter blanket-activating
`select.flow`/`highlight.flow` → spurious `special_select.anim` flourish). Settled by a runtime
`DC3_FLOW_PROBE` at the `Activate()` site, headless to choose_mode:
- **The hypothesized choose_mode flourish never fires** — zero probe hits for the `choose_mode` dir,
  and nothing named exactly `select.flow`/`highlight.flow` is ever activated → those flows are
  `startMode>0` (correctly `continue`-skipped by the loop's gate) or absent. The verifier's flagged
  `startMode==0` assumption is FALSE.
- What *does* activate are `sfx/common_bank.milo`'s `left_select`/`right_select`/`invalid_select`/
  `right_select_P2` (startMode=0) — **navigation sound-effect flows being armed on enter**, exactly the
  Xbox enter-handler behavior the blanket-activation exists to replicate. The proposed fix would be
  inert (exact-leaf `select`/`highlight` never activate) or, broadened, would **kill menu nav sounds**
  (audio regression). No change made; probe reverted. Probing first prevented a regression — the
  runtime-verify discipline paying off again ([[feedback_runtime_verify_native_fixes]]).

## In flight
- **Rank 4 (reward/unlock chain, `HasValidSaveData()`):** design workflow `wf_11ff7e2c` running — maps
  every save-data/autosave/profile/unlock consumer + the inverse-crash exposure of flipping save-state,
  synthesizes ONE minimal safe HX_NATIVE plan (weighing savestate-flip vs narrow HasValidSaveData
  override vs unlock-all path) + runtime-verify recipe + crash watch list. Design-only; orchestrator
  reviews + implements + GPU-gates.
- **Rank 6 / #37 (native move-scoring pipeline dead, DetectFrac≡0):** the highest-value finding but
  multi-part/hard. Concrete 3-step SkeletonCallback fan-out wiring plan + a camera-free headless
  perfect-mimicry self-test (`DC3_POSE_SELFTEST` inject at FilterQueue.cpp:85, feed the choreography's
  own DancerSkeleton as the player). Terminal `DC3_REAL_MOVE_PASSED` gate must NOT flip until the
  self-test passes. Deferred behind rank 4.

## Process
Subagents find + root-cause + rank (GPU-blocked); orchestrator runtime-verifies + merges. This wave:
3 fixes runtime/suite-verified before merge; 1 speculative finding (rank 5) runtime-DISPROVEN before
any code, preventing a regression.
