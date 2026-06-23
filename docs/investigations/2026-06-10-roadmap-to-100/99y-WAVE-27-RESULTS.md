# Wave 27 Results — Native flow-activation #1 + real-scoring gateway #2 (user-directed)

**Date:** 2026-06-23 · Orchestrator-driven (GPU runtime-verified; subagents GPU-blocked) + Opus
root-cause delegation. User chose "#1 (flow-activation semantic filter) then #2 (real native scoring)".

## #1 — Flow-activation semantic filter → endgame FlowSequence asserts FIXED (`28ed44cf`)
The endgame `perform_endgame_screen` tripped `FlowSequence`'s `<=1-running-node` asserts ×6
(non-fatal). Root cause (runtime-instrumented): `PanelDir::Enter`'s native blanket flow-activation
(`ObjDirItr<Flow>(this, true)` — recurse) reaches the **nested XP-toaster sub-panel**
(`ui/hud/xptoaster*.milo`) and activates its `test_init.flow`, double-activating a shared `FlowRun "l1"`
FlowSequence. The XP toaster is game-code-triggered (shown on XP award), not panel-enter-triggered.

**Two wrong turns first (both runtime-caught before merge — process lesson reinforced):**
`a80e1b0b` (skip flow-node-nested + `'1'`-dedup) — runtime-proven 0 effect, reverted. `recurse=false`
— rejected: stripped `choose_mode` menu chrome (screenshot diff vs baseline). **Correct fix:**
`ShouldActivateNativeFlow` skips flows whose path is a game-code-triggered nested sub-panel (xptoaster).
Runtime-verified: asserts **6→0** (endgame+results reached), menus **identical to baseline**,
`PanelDir::Enter` 100% normalized (PPC-neutral), suite 418/418. *Still open (non-fatal, separate
mechanism):* main_screen/choose_mode contradictory-SIBLING over-activation (top-level dup flows).

## #2 — Real native scoring: gateway SIGSEGV FIXED (`414a08b8`)
`DC3_REAL_MOVE_PASSED=1` (real `move_passed` scoring path) crashed during gameplay. Root-caused by
**disassembling the faulting PC** (the backtrace's `SymbolKeys::SetFrame+0xA` was garbage
symbolization): the real fault is `MoveDetector::Poll+0x179` = `MoveAsyncDetector.cpp:98`
(`GetMoveFrame()->GetBeat()`) dereferencing a **null `DetectFrame::mMoveFrame`**. Native has no
`SkeletonUpdate`/Kinect feed → `MoveDir::EnqueueDetectFrames` never runs → the async detector's
`mPlayerDetectFrames` keep `mMoveFrame` null; `move_passed` → `active_detector_result` DTA →
`MoveRatingFrac` → `detector->Poll` → null deref. (`activeMoveCount 2→0` from wave-25 was a red herring,
not the crash cause.)

**Fix:** gate `MoveAsyncDetector::MoveRatingFrac`'s `detector->Poll` path on
`SkeletonUpdate::HasInstance()` under HX_NATIVE — the same precondition the engine uses for detection
everywhere — returning the already-correct `0.0f` (consistent with native `DetectFrac()==0`). Root, not
a null-check band-aid (expresses the real missing precondition). Runtime-verified:
`DC3_REAL_MOVE_PASSED=1` runs 16000 frames of gameplay clean (exit 0, was SIGSEGV);
`MoveRatingFrac` 99.8% normalized (PPC-neutral baseline, HX_NATIVE-guarded); suite 418/418.

**#2 status:** the real `move_passed`→`MetaPerformer::OnMovePassed` pipeline now runs **stably**. It
stays env-gated because (a) without detection `DetectFrac`/`MoveRatingFrac` honestly return 0 → scores
0 regardless, and (b) per-beat move_passed move-graph behavior isn't yet verified. **Non-zero native
scores require the skeleton-detection wiring** (create a native `SkeletonUpdate` instance + register
`MoveDir` as its callback + feed `InternalPoseProvider`/pose_server poses → `EnqueueDetectFrames` →
`DetectFrac`, then verify the Ham2 PSNR rating math) — a large, camera-dependent, headless-unverifiable
effort; that's the remaining piece of #2.

## Process note
Three native-behavior fixes this wave were runtime-verified before merge; two plausible-but-wrong
attempts were caught and reverted/rejected by runtime evidence (assert-count + screenshot diff +
disassembly). Static review + suite-green is insufficient for flow/timing/gameplay fixes — see
[[feedback_runtime_verify_native_fixes]].
