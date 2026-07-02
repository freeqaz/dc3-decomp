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

## Rank 4 (reward/unlock) — LANDED, real earning works on native (`9c98fbef`, `b2e80335`, `65045f6b`)
A design workflow (`wf_11ff7e2c`) CORRECTED the audit's premise: `TheProfileMgr.Init()` is
`#ifndef HX_NATIVE` (MetaPanel.cpp:185), so native `mProfiles` is **empty** — there was nothing to
"earn" into, and flipping `HasValidSaveData()` would have been a no-op + risked null-profile asserts.
User chose "#1 then #2 then push through (#3)". Landed in three PPC-neutral (all `#ifdef HX_NATIVE`),
runtime-verified stages:
1. **sUnlockAll (`9c98fbef`)** — `MetaPanel::sUnlockAll=true` surfaces all content unlocked on the dev
   port + short-circuits the null-profile `MILO_ASSERT(pProfile)` surface. (MetaPanel::Init 100%.)
2. **Profiles + scoped signin (`b2e80335`)** — `ProfileMgr::InitNative()` (side-effect-free subset of
   the Xbox Init: 4 HamProfiles, SetName, AddSink, InitSliders, pad-0 `kMetaProfileLoaded`);
   `GetSignedInProfiles` HX_NATIVE branch treats pad-0 as signed in (NOT global mSigninMask, to avoid
   ShellInput ripple); `UploadDeferredFitnessGoal`/`UpdateFriendsList` HX_NATIVE early-returns (null
   `TheFitnessGoalMgr` vtable-deref + job leak). All 5 touched fns run_objdiff 100% unchanged.
3. **Pad enrollment (`65045f6b`)** — the real blocker, found by runtime tracing: the local player kept
   `HamPlayerData::PadNum()==-1` (Xbox assigns pads in Kinect `SkeletonIdentifier` enrollment, which
   native bypasses) → `GetProfileFromPad(-1)==null` → every grant no-op'd. Fix: `MultiUserGesturePanel::
   Poll` assigns the local player pad 0 in the native `enter_gameplay` block (the native enrollment
   equivalent); `HamGameData::SetAssociatedPadNum` gets an HX_NATIVE branch because the Xbox path gates
   the pad on `IsSignedIn` (false on native) and would force -1.

**Runtime proof (headless betteroffalone→macarena):** `*** ACCOMPLISHMENT COMPLETED: acc_synchronicity
pad=0 ***` — a real accomplishment earned to the pad-0 profile. All `HandleSongCompleted` gates pass
(pad=0, profile valid, `update_leaderboards`=1); player 1 correctly stays unassigned (single-player);
exit 0, no crashes; suite 419/419. In-session earning; persistence stays deferred (native save machine
dormant by design — `unk2c`/`Activate` never fired). Method note: each blocker (empty profiles → signin
gate → pad=-1 → grant) was found by an env-gated `DC3_EARN_PROBE` trace, fixed at root, re-run — five
build/trace iterations, all PPC-neutral.

## Rank 6 / #37 (native move-scoring) — WIRED + runtime-proven (`68d404dd`)
The pipeline was inert — `DetectFrac≡0`, every dance move a miss. An Opus subagent wired STEP 1-4 in a
worktree (native SkeletonCallback registry + driver fan-out + self-test inject, PPC-verified); the
orchestrator drove 9 build/trace/fix iterations (env-gated probes down `GestureMgr::Poll` → driver →
`FilterQueue::Poll` → `DetectFrac`) to find + fix the blockers GPU runtime revealed:
1. **The scoring callbacks never registered** — `SkeletonUpdate::sInstance` is never created on native
   (ctor needs Xbox `LiveCameraInput`). Added an HX_NATIVE static native-callback registry; MoveDir/Game
   register into it; `GestureMgr_NativePoll` fans out `Update()`→`PostUpdate()` (mirrors Xbox).
2. **The driver itself was NEVER CALLED** — the native App loops (`RunWithoutDebugging` headless loop +
   `RunOneFrame`) both omitted the per-frame `TheGestureMgr->Poll()` that `App::Run` drives on Xbox. So
   the *entire* native pose→skeleton→scoring pipeline was dead. Added it (before `TheUI->Poll`).
3. Enabling the driver activated dormant native skeleton paths; guarded each GPU-exposed crash at root:
   `SkeletonChooser::DoesRequireHandRaise` (null DataNode — no Kinect properties off the perform path),
   `MoveDir::PostUpdateFilters` async-detector write side (null `DetectFrame::mMoveFrame`), and HamVisDir
   NOT registering (its `FreestyleMotionFilter` reads skeleton velocity from the NUI history archive,
   unfed on native → `SkeletonHistory::PrevFromArchive` null-deref).

**Runtime proof:** camera-free self-test (`DC3_POSE_SELFTEST` feeds the choreography's own reference
DancerSkeleton as the player at `FilterQueue::Poll` → perfect mimicry → PSNR max → `DetectFrac`=1.0).
betteroffalone→macarena scores **27990 non-zero move fracs** (was identically 0), boot→gameplay→results
clean, exit 0. All edits `#ifdef HX_NATIVE` → PPC byte-identical (run_objdiff unchanged on
DoesRequireHandRaise 100%, PostUpdateFilters 97.2%, ResetDetection/HamVisDir ctor+dtor 100%,
FilterQueue::Poll 93.4%, RunWithoutDebugging 99.5% — all pre-existing baselines).

**Gated behind `DC3_NATIVE_SCORING`/`DC3_POSE_SELFTEST` (opt-in)** so DEFAULT gameplay is unchanged
(driver dormant) — verified crash-free, suite 419/419. **Terminal step (task #41, deferred):** real
live-pose scoring needs the native skeleton-history displacement path finished (the real/dummy skeleton
hits the `PrevFromArchive` null-deref the self-test's reference avoids) + real pose input, then flip the
default. The wiring + the self-test proof are the hard part; those are done.

## #41 step 1 LANDED (2026-07-02, main `5a02adb8`): live-pose displacement path fixed

The "PrevFromArchive null-deref" was diagnosed by a 5-agent research workflow + live repro: **not a
missing archive** — `NativeSkeletonHistory` existed and was populated. The real defects: native skeleton
fills (`FillSkeleton`/`FillDummySkeleton`) never did `Skeleton::Poll`'s bookkeeping, so `mSkeletonIdx`
stayed **-1** (→ `mHistories[-1]` wild OOB read; the range MILO_ASSERT is non-fatal on native),
`mElapsedMs` was garbage (poisoning the archive time-walk), and the `mCamDisplacements` memo cache was
never cleared (would have frozen first-frame displacements forever). Fix (Opus impl + Sonnet adversarial
review, approve/0-confirmed-issues): `FinalizeSkeletonFrame`/`MarkUntracked` helpers mirroring Poll's
tracked/untracked halves; archive loop moved BEFORE fills (Xbox `UpdateCallbacks` order); real
steady_clock frame delta; finalize keyed to actual-fill per branch; dummy fallback only when NO provider
runs (transient 0-person frames mark untracked instead of poisoning history); HX_NATIVE bounds guards in
`PrevFromArchive`/`GetArchive` (asserts untouched; PPC 93.7%/100.0% baselines unchanged).

**Runtime gates (all headless betteroffalone, 12-18k frames):** scoring run EXIT=0 + zero
SkeletonHistory FAILs (previously SIGSEGV on first move frame); **archive lookups ~98% hit rate**
(`DC3_SCORING_DEBUG` counters: 7129 calls/6977 hits worktree, 5211/5128 on main — misses = authentic
cold-start); self-test clean; default gameplay unchanged; gesture/skeleton suite 31/31 (3 suite
failures are pre-existing missing milo-rnd-library assets, identical on main). **Step 2 in flight:**
stable trackId→slot mapping + provider new-frame gating, then the live-pose run + default-on decision.

## Process
Subagents find + root-cause + rank (GPU-blocked); orchestrator runtime-verifies + merges. This wave:
3 fixes runtime/suite-verified before merge; 1 speculative finding (rank 5) runtime-DISPROVEN before
any code, preventing a regression.
