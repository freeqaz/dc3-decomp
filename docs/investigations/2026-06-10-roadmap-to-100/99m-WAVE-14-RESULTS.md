# Wave 14 Results — fake_impl_scan.py + unicorn refresh + native_divergence cert class

**Date:** 2026-06-21 · **Landed through main `97528f9d`** · **Orchestration:** 1 Workflow
(lanes A/B/C, ALL-Opus) — **interrupted by a transient API rate-limit, then RESUMED** after
adding a graceful-retry wrapper. **Authorship:** Fable orchestrating; user directed all-Opus
+ the goal-loop ("loop until levers exhausted, build/improve tooling, commit per wave").

Wave 14 was tooling-forward: it built/improved **three pieces of tooling** and landed
**3 real bugs**. Headline holds at 99.06% fns / 96.75% bytes (the wins are in the excluded
`synth_xbox` unit + `net`, so flat headline — the value is correctness + reusable tools).

Gates: PPC ninja green, check_native_compiles PASS, **milo-tests 418/418 0 FAIL**, reconcile 0 drift.

## The rate-limit interruption + graceful-retry fix (process)

The first run hit a sustained server-side 429: lane A's implementer died on its final
StructuredOutput (after committing real work to its worktree), and both surviving verifiers
were rate-limited. **The work was not lost** — all lanes commit to worktrees as they go.
Fix: added an `agentRetry()` wrapper to the workflow script (re-attempts a null-returning
`agent()` up to 5×) + a resume-recovery preamble for lane A, then **resumed via
`resumeFromRunId`** — completed implementers cache-hit, the rate-limited agents re-ran and
succeeded. This `agentRetry` pattern is now the template for all future waves.

## Lane A — fake_impl_scan.py + 2 missing-impl recoveries (VERIFIED PASS, `1ac31362`)

**BUILT `scripts/analysis/fake_impl_scan.py`** — the third reusable blind-spot scanner
(after vtable_dispatch_scan, data_symbol_scan). It finds fake/stub bodies (trivial compiled
body vs substantial target) via read-only `objdiff --include-instructions` over already-built
`.obj` (no `--build`, fleet-safe, never writes decomp.db), side-presence classification
(target-only = missing real code), verdicts empty/trivial/partial-stub/incomplete-impl.
**Recall 3/3** on the wave-13 hand-found stubs. Harvested with it:
- **StandardStream::IsPastStreamJumpPointOfNoReturn 22.8 → 97.6%** — the body was *wrong*,
  not just short: it did an integer-sample compare; the target works in MS time (gates on
  `mState!=kInit`, reads in-song time via `GetInSongTime` vtable slot 0x40, compares
  `mJumpFromMs`).
- **FxSendMeterEffect360::InitParams 0.5 → 86.3%** — empty stub recovered (og port + target
  asm: channel `LevelData` vector build); added `LevelData(const char*)` ctor to shared
  `synth/FxSend.h` (decomposition-tested: Synth::Poll 100%, UpdateOverlay 96.5% pre-existing,
  no synth/hamobj TU regressed).

## Lane B — HttpGet::ParseHeader + unicorn refresh (VERIFIED PASS, `97528f9d`)

Ran the unicorn oracle refresh (`refresh_frontier.py`) on post-wave-13 main after fixing a
worktree-path import bug: **1403 fns re-emulated, 71 flips** (4 recovered DIV→EQ incl. wave-13's
CacheXbox, 14 candidate_bug, 44 signal-version churn, 6 artifact). Worked all 14 candidate_bugs:
- **HttpGet::ParseHeader 95 → 100%** — real 4× HTTP-header undercount: the line count was
  `((int**)p)[1]-((int**)p)[0] >> 3` = byte_diff/4 then /8 = **/32**, but `sizeof(String)==8`
  so the correct count is byte_diff/8. The parse loop ran 4× too few iterations, leaving most
  header lines unparsed. Fixed via `char**` cast. (The other 13 candidate_bugs were regalloc/
  MakeString-`__FILE__`/MSVC-lowering floors — the candidate_bug label over-fires, consistent
  with documented unreliability.)

## Lane C — native_divergence cert class (tooling landed, backlog REJECTED, `<certtool>`)

**Extended `certify_floor.py`** with a `native_divergence` manual-only cert class (+8 tests):
for genuine cross-platform floors where our source is native-correct and the PPC target can't
be matched without breaking the native/web build. Sound, gate-clean, landed.

**But the verifier rejected the backlog** — and was right: the one entry (SampleInst360::
IsPlaying as native_divergence) is **mis-classified**. Independent review showed it's
*matchable*: `synth_xbox/` is excluded from the native build (SampleInst360 isn't compiled
natively; the engine uses a separate `SampleInstNative`), the only native coupling is the
shared base `SampleInst::IsPlaying() const = 0`, and the target proves the original base was
**non-const** (all SampleInst360 virtuals are UAA). **Fix = HX_NATIVE const-split on the base
decl** (non-const for PPC to match, const for native). Backlog NOT applied; queued as a real
wave-15 win. (BinkMovieImpl::IsOpen/IsLoading + StandardStream::SetADSR, also named, reached
100% on their own between waves 13–14 — not floors.)

## What worked / loop status

Wave 14 found 3 real bugs + 3 tools/infra (down from wave-13's 9 bugs but still net-positive),
and the unicorn refresh + fake_impl_scan are now repeatable. **Levers NOT yet exhausted.**

## Open follow-ups (wave 15)

1. **SampleInst360 HX_NATIVE const-split** — fix the base `SampleInst::IsPlaying` const→split;
   likely fixes IsPlaying/ElapsedTime/GetProgress (all UAA). A potential **const-signature
   divergence CLASS** — scan for other UAA-vs-UBA mismatches.
2. **Broad fake_impl_scan harvest** — run across ALL units; harvest the reference-backed ones.
   Deferred (reference-less, large): DxMesh::OnSync (804B), NgEnviron::Select (1956B) — need
   m2c reconstruction.
3. **fake_impl_scan flagged intentional stubs** (Synth360::PreInit, MemAlloc) correctly — keep
   stubbed.
4. Voice::IsPlaying 98% CR/spill floor; compute_apres inlining floor — certs.
