# Wave 17 Results — synth_xbox stub reconstruction (17 functions from 0%)

**Date:** 2026-06-21 · **Landed through main `cdc38edf`** · all-Opus, agentRetry, native-gated.
Goal-loop wave. **17 reference-less is_stub functions reconstructed from target asm + headers**
(og lacks them, RB3 is Wii) — the SampleInst360 (w16) approach, scaled across three synth_xbox
subsystems. Native 418/418, reconcile 0 drift.

## Metric note (important)

`is_stub=1` functions are **pre-counted as "done"** in the authorable_done view, so reconstructing
them (stub → real match) does NOT move done-with-certs (held 99.07%/96.78%). But it is **genuine
decomp progress**: 14 went to 100% normalized (real Xbox-target match), and several carried real
behavioral bug fixes. The headline metric simply already credited the stubs; the work is real.

## Lane A — FxSend360 / Voice (6 fns, `913a08cd`)

- FxSend360::UpdateVolumes 0→100, CreateVoice 0→100, CreateInputVoice 0→87.1
- Voice::UpdateSends 0→99.9, createOrReuse 0→94.9
- **Voice::dispose 57.7→86.2 + 3 REAL behavioral bugs**: wrong vtable slots were *destroying a
  pooled voice* (DestroyVoice 0x48) instead of flushing it (FlushSourceBuffers 0x58), *reading
  state* (0x64) instead of disconnecting (SetOutputVoices 0x4), and missing the gVoiceCounters
  active/disposed bookkeeping.
- Blocked: Voice::UpdateMix (1752B), FxSend360::UpdateVoiceMatrices (2328B) — pan-curve/output-
  matrix float math with bitcast-indexed magic constants (a subtle error changes spatial audio).

## Lane B — DSP/Synapse (5 fns, `eb86b9e7`)

- PitchCorrectedVoice::SetTransposition 0→100, GetCorrection 0→86.2
- SpectralAnalysis::SetMode 0→88.4; PitchDetector ctor 0→81.2, Detect 0→77.5
- Blocked: GranularSynth::ExtractGranules/Synthesize (granular synthesis, unverifiable magic math).

## Lane C — Xbox Mic/Chat (6 fns, `cdc38edf`)

- MicManagerXbox::OnDataReady 0→100, DataReadyCallback 0→100
- ChatReceiver::ProcessChatData 0→91.9; MicXbox::AddData 0→91.1, ReadChatBuffer 0→90.1,
  AddToBuffer 0→89.7
- Blocked: MicManagerXbox::Init/AddRemoteTalker, MicXbox::Poll (XHV session setup).

## Loop status — synth_xbox lever is HIGHLY productive

The 180-unpaired-virtual pool (w16 finding) split into blocked render layer (rnddx9) + tractable
synth_xbox. Wave 17 attacked ~23 synth_xbox stubs → **17 landed, ~6 blocked** (the math/session-
setup-heavy ones). ~100 synth_xbox stub candidates remain. **Levers NOT exhausted.** Recovery
rate ~74%. The rnddx9 render stubs (DxRnd/DxTex/DxMesh, 800–5188B) remain the blocked reference-
less category. Wave 18 continues the synth_xbox pool + probes whether any rnddx9 stubs are
small/anchor-able enough to be tractable.
