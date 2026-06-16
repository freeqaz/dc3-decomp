# Wave 12 Results — broad data-symbol vtable sweep + dispatch cleanup (native-gated)

**Date:** 2026-06-16 · **Landed through main `d6dcfaed`** · **Orchestration:** 1 Workflow
(lanes A/B, 4 agents, ~475K tokens) + native-suite-gated harvest. **Authorship:** Fable
(research+orchestrator) over Opus subagents.

Wave 12 applied the wave-11 lesson as its organizing principle: **every raw/data-plane
fix gated on the full native suite (418/418), per fix.** Headline holds at **99.06% fns
/ 96.74% bytes, open 261** — the wins are behavioral (data + logic plane); the const/
native-floor items and the excluded `synth_xbox` audio keep the headline flat, as
expected for this bug class.

Gates: PPC ninja green, check_native_compiles PASS, **milo-tests 418/418 0 FAIL**
(integrated tree), reconcile 0 drift. The only sweep "regressions" are the perennial
untouched-math report artifacts (Normalize alias + EH funclet, 36 B).

## Lane A — broad data-symbol vtable sweep (VERIFIED PASS, `1de1bc2e`)

Ran `data_symbol_scan.py` across **all 5,132 class vtables** in 860 authorable units
(wave 11 only checked known suspects). **5 fixes landed**, each native-suite 418/418
gated, PPC net +3 fns:
- **PhysicsManager/DefaultPhysicsManager::CastRays decl-order** — MSVC emits same-named
  overloaded virtuals in REVERSE source order, so our vtable had the two `CastRays`
  overloads at +0x70/+0x74 swapped vs target. Fixed the decl order in both headers.
- **SampleInst360::IsPlaying — REAL LOGIC BUG** (40→100 logic): target returns
  `mVoice->IsPlaying()`, our decomp stubbed `return false`. Fixed. The const signature
  is kept as a documented **native-floor** (the engine's `SampleInst_Native` override
  needs the const base; `SampleInst360.cpp` is Xbox-only, so no native impact, and the
  PPC symbol stays unpaired against the target's non-const name — a cross-platform
  vtable-signature divergence we can't close without breaking the engine).
- **DxShaderMgr::SetVConstant(float\*)** — added `__restrict` (the `PIBM` mangling) to
  match the target's vtable slot +0x20; a top-level qualifier that doesn't affect
  override matching, so the base and the engine's own overrides keep resolving.
- **IDataChunk::Header()** — dropped a spurious `virtual` (it's a plain accessor in the
  target), removing an extra vtable slot; also lifts derived **WaveFileData** 66→71.
- **FixedSizeSaveableStream::Finish{Write,Stream}** — dropped spurious `virtual`s (not
  virtual in the target), removing 2 extra slots; 63.9→68.5.

Recorded floors (not forced): StandardStream `SetADSR` overloaded-virtual hoisting,
BinkMovieImpl const-override (engine needs the non-const base), SampleInst360::ElapsedTime
missing override; ~60 NavList/accessor candidates triaged as benign ICF folds.

## Lane B — dispatch cleanup + MoggClip (VERIFIED PASS, `d6dcfaed`)

- **MoggClip::LoadNumChannels 91.8 → 97.4 (native-safe)** — the wave-11 DROPPED fix, done
  right: kept only the 3 native-safe method-name swaps and **reverted the segfaulting
  `Play(0)` line** (which dropped 27 native tests last wave). Native 418/418. This is the
  wave-11 lesson executed correctly.
- **The 8 skipped wave-11 dispatch candidates stay skipped** — DingoJob ×2, DingoServer,
  ChunkStream, Rnd::DrawPreClear, StorePanel, RndShaderStandard::CalcShaderOpts: all are
  sub-90 functions where the receiver sub-object vtable resolves ambiguously and the row
  alignment is uncertain; not safe to fix blind. Honest skip.

## Open follow-ups (wave 13 candidates — increasingly narrow)

1. **SampleInst360::IsPlaying const native-floor** + **BinkMovieImpl const native-floor**
   — cert these as cross-platform vtable-signature divergences (our source is
   native-correct; the PPC slot/signature can't match without breaking the engine).
2. **8 skipped dispatch candidates** — would need careful per-function receiver
   resolution; low confidence, sub-90 functions.
3. **StandardStream SetADSR overloaded-virtual hoisting** floor — characterize/cert.
4. **MILO_ENGINE_PIN bump** once the concurrent engine `FxSendNative.cpp` unk0 WIP commits.

## Candid assessment

The waves-9–12 thesis — "find the bugs the normalized metric hides" — has been
productive: it added two reusable scanners (`vtable_dispatch_scan.py`,
`data_symbol_scan.py`) and fixed ~15 real behavioral/construction bugs (wrong virtual
dispatch, wrong vtable layout, a stubbed `return false`) that all read as high-% on the
headline. **That value is real for native-port correctness but does not move the
authorable headline** (raw/data plane + excluded `synth_xbox`). The remaining candidates
are now floor-dominated and increasingly require ambiguous-receiver guesswork that the
wave-11 MoggClip segfault showed is dangerous. **Recommendation: this thread has reached
diminishing returns** — the highest-value remaining work is certifying the documented
native-floors and the occasional targeted bug, not another broad sweep.
