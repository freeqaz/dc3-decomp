# Wave 13 Results — emulation-proven behavioral-divergence harvest + audio missing-impl recovery

**Date:** 2026-06-21 · **Landed through main `f2047ffe`** (+ DB runbook) · **Orchestration:**
1 Workflow (lanes A/B/C, 6 agents, ~810K tokens, ALL-Opus incl. verifiers) + native-suite-gated
harvest. **Authorship:** Fable orchestrating; user directed all-Opus.

Wave 13 pivoted off the (plateaued) static-blind-spot thesis to **behavioral bugs with
independent evidence**: unicorn emulation-proven divergences (Lane A) and fake/stub
implementations (Lane B). Both paid off — and unlike waves 10–12 the wins **moved the
headline**: **done-with-certs 99.06%→99.07% fns / 96.74%→96.75% bytes, open 261→258**.

Gates: PPC ninja green, check_native_compiles PASS, **milo-tests 418/418 0 FAIL**
(integrated tree), reconcile 0 drift.

## Lane A — unicorn DIVERGENT real-bug harvest (VERIFIED PASS, `87ff181a`)

Of the 17 emulation-proven divergent functions, **4 real bugs fixed** (the rest honestly
triaged as compiler floors). The value: the emulator had *already proven* these behave
wrong despite high match%; this wave found WHAT diverged.
- **StandardStream::DoJump 98.3 → 100%** — TWO bugs: (1) source guarded
  `if (mRdr) mRdr->Seek()` but the target Seeks **unconditionally**; (2) the
  `mJumpInstances.back()` field reads used the wrong offsets (read unk0/unk4, target reads
  −0xc/−0x8 = unk4/unk8). Both fixed from raw target asm.
- **NgDOFProc::Set 93.8 → 94.0** (landed-trade) — bias clamp-compare operand order.
- **CacheXbox::ThreadRead 92.2 → 91.8** (landed-trade) — real control-flow bug:
  `GetLastError() < 2` must run the `IsDeviceConnected` check, not `return 8`; also dropped
  the `_outline_DeviceID` noinline hack (target inlines it). Behavior now matches; −0.4pp
  is the licensed trade.
- 11 others triaged as floors (regalloc/FPR cascades, STL iterator materialization, ICF
  MakeString template args) or emulation artifacts — documented, not forced.

## Lane B — audio/synth missing-implementation recovery (VERIFIED PASS, `1d75f8bd`)

**The standout lane.** Wave-12's SampleInst360::IsPlaying (stubbed `return false`) pointed
at a cluster of fake/stub bodies in synth_xbox; **5 recovered from target asm + og**, all
native 418/418, 0 regressions:
- **Voice::IsPlaying 20.5 → 98.0%** — the real "is this voice playing" body that
  SampleInst360 tail-calls. (Residual is a 1-branch cr-field tail.)
- **Synth360::SetupHeadsetSubmixes 21.0 → 84.6%** + **HeadsetXferEffect ctor 0 → 86.5%**.
- **FxSendSynapse360::CreateFx 11.1 → 100%**, **SyncEffectParams 1.1 → 89.7%**.

These are genuine missing-implementation recoveries (verified: the previously-absent
instruction clusters are now present and correct), not coincidental %-matches.

## Lane C — native-floor certs (VERIFIED PASS, `f2047ffe`)

- **2 MemTracker `std::sort` STL-header templates** (`__median`, `__unguarded_linear_insert`
  on `MemDiffEntry`) certed as codegen floors (`floor_cert_backlog_w13.json`, applied).
- **The 3 named native-floors are NOT cert-routable** through the current classes
  (SampleInst360/BinkMovieImpl const signatures, StandardStream SetADSR hoisting): they are
  **cross-platform vtable-signature divergences**, not regalloc/equivalent/permuter floors.
  Recorded for a future `native_divergence` cert class — a small tooling gap to close.

## What worked (thesis confirmation)

The "behavioral bugs with independent evidence" pivot was the right call for a post-plateau
wave: **unicorn divergence class + fake-impl detection found 9 real fixable bugs** where
the broad sweeps had stalled, and several moved the headline (StandardStream::DoJump,
Voice::IsPlaying, FxSendSynapse360). The unicorn DB is a high-signal, low-noise bug oracle
that hadn't been mined this session.

## Open follow-ups

1. **Voice::IsPlaying 98% tail** (1-branch cr-field) — one more pass to 100%.
2. **More fake-impl recovery** — the synth cluster yielded 5; a broader fake-impl scan
   (trivial-body vs substantial-target across all units) likely finds more.
3. **`native_divergence` cert class** — add it to certify_floor.py so the 3 cross-platform
   signature floors (SampleInst360/BinkMovieImpl/StandardStream) become certifiable.
4. **Remaining unicorn-divergent structural floors** (RndLight::Projection, DxCam::Select,
   BeatClock::OnSyncStateChange, RndText::ReplaceMissingCharacters) — need dedicated
   structural rewrites, not single fixes.
5. **MILO_ENGINE_PIN bump** once the concurrent engine FxSendNative.cpp unk0 WIP commits.
