# Wave 26 Results — Native gameplay bug-hunt: 2 real bugs found + fixed

**Date:** 2026-06-23 · Orchestrator-driven headless gameplay runs (boot→gameplay→endgame, sandbox-disabled
for GPU since subagents are GPU-blocked) + Opus fix/review workflows (agentRetry, native-gated).

## Method
Drove full boot→gameplay→endgame sessions via `scripts/dc3-input-flows/betteroffalone.txt` (robust
`wait_screen` flow) + `DC3_FAST_BOOT=1`, captured the full log, triaged every WARN/FAIL/assert/anomaly.
The native gameplay path is healthy — it plays a full song to the endgame results screen cleanly. Only
**two** signals were real code bugs; the rest are missing-asset warnings (`.bik` previews, loading MIDI),
benign timing, debug printfs, or asset-version notes.

## Bug 1 — HamSupereasyData empty-`preferred` move-graph FAIL (FIXED `d03901e3`)
`FAIL: 'macarena' HamSupereasyData has move '' at index N not found in move graph`.
Root cause: `HamSupereasyData::Load` reads `preferred` only `if (d.altRev > 0)`, so altRev-0 songs
(macarena) have empty `preferred`; `SuperEasyRemixer::SaveSuperEasyMoveParents` looked up the empty
`preferred` with no fallback (the sibling `LoadAllVariants` correctly falls back `.second`→`.first`).
Confirmed via og-dc3 that this is faithful decomp (the original would also FAIL on altRev-0 data — fatal
on Xbox) → fixed `#ifdef HX_NATIVE` with a `preferred`→`first`→`second` fallback; `#else` byte-identical
(objdiff 78.2% unchanged). Runtime-verified: macarena FAIL gone, supereasy no longer wrongly degrades to
the easy track. Adversarially reviewed → CONFIRMED.

## Bug 2 — FlowSequence 2-running-nodes asserts on endgame (ROOT-CAUSED, fix reverted, DEFERRED)
`FlowSequence.cpp` `MILO_ASSERT(mRunningNodes.empty(), 0x74)` + `MILO_ASSERT(mRunningNodes.size()<2,
0xA6)` fire ~6× entering `perform_endgame_screen` (NON-FATAL — engine recovers; endgame→results
completes cleanly).

**First fix attempt reverted (`2c95ca22`).** An Opus investigation hypothesized (statically) that
`PanelDir::Enter()`'s `ObjDirItr<Flow>(this, true)` recurse independently re-activated nested flows; the
fix (`a80e1b0b`: skip nested flows + `'1'`-suffix dedup) was adversarially "CONFIRMED" on static
reasoning + suite-green — but **orchestrator runtime verification proved it had ZERO effect** (still 6
asserts at endgame). Reverted, and the real cause found via runtime instrumentation
(`FLOWDBG` dump of `mRunningNodes`):

**Actual root cause:** the owner flow is **`test_init.flow`** — the **XP-toaster init flow** (lives in
`ui/hud/gen/xptoaster*.milo_xbox`, active on the endgame `xp_overlay_panel`). Its `FlowSequence`
double-activates a `FlowRun` child named `l1` (two `FlowRun l1` in `mRunningNodes`). The asserts run
*before* the `mIsAdvancing` guard, so a synchronous re-entrant `ChildFinished` during the
`FlowIf→FlowSwitch→FlowSequence` "ran in full immediately" cascade trips them. This is an instance of
the documented **native blanket-Flow-activation hard problem** (DECOMP_GAPS "blanket Flow start"): native
`PanelDir::Enter` activates flows that Xbox drives via targeted DTA, and the synchronous activation order
differs. Confirmed native-only (og-dc3 `PanelDir::Enter` has no flow-activation loop; the asserts are
matched Xbox code — must not be silenced).

**DEFERRED.** Non-fatal, and a correct fix is the broad native flow-activation *semantic content filter*
(make native activation match Xbox's targeted DTA set) — a large, known-unresolved effort. Silencing the
matched asserts or band-aiding `mRunningNodes` is disallowed. Lesson recorded: **runtime-verify
native-behavior fixes before merging** — static review + suite-green is insufficient for flow/timing bugs.

## Non-bugs (triaged, no action)
- `EnableFacialAnimation … lipsync name:NULL` ×13 — informational; the function no-ops on null sync
  (characters without lipsync). Benign.
- `UIList::PreLoad … mListDir=(nil)` ×3 — HX_NATIVE debug `printf` showing `mListDir` before resolution;
  many lists are legitimately null at preload. Debug spam, not a bug (candidate for cleanup).
- `director.milo NOT FOUND`, `MatAnim 'blackmask.mnm' version` warn, missing `.bik` previews/intro —
  asset/version, handled gracefully.

## Also this session (waves 24-25, context)
FileMerger sync-reload infinite-loop fix (re-enabled 6 tests), honest native scoring (fake-points hack
removed + crash-safety), FileMerger backend-floor + bug-origin proof, Xbox-stub lever proven empty.

## Loop status
The native gameplay path (boot→song→endgame→results) completes cleanly and crash-free; the one real
fatal-on-Xbox code bug found (supereasy) is fixed. The remaining non-fatal native items all trace to the
**native blanket-Flow-activation hard problem** (semantic-content filter): the endgame xptoaster
`FlowSequence` asserts (this wave, deferred), the `main_screen`/`choose_mode` contradictory-sibling
over-activation, and the `DC3_REAL_MOVE_PASSED` move-graph instability — plus IK deep-fidelity
(user-deferred). These are deferred-hard, not wave-shaped. Process lesson this wave: **runtime-verify
native-behavior fixes before merging** (a statically-"confirmed" flow fix was runtime-proven ineffective
and reverted). Further gameplay bug-hunting on other songs/modes is the natural continuation.
