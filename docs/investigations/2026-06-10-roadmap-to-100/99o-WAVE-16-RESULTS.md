# Wave 16 Results — last-lever reconstructions: missing-impl decomp + ambiguous dispatch

**Date:** 2026-06-21 · **Landed through main `66cd48e5`** (+ DB runbook) · all-Opus,
agentRetry-resilient, native-gated. Goal-loop wave.

Wave 16 was framed as the "last lever" (HIGH-risk reconstruction, blocked-is-acceptable) —
but it **landed 4 more real fixes** and, critically, mapped the boundary of what's
orchestratable. Headline: done-with-certs **99.07% fns / 96.78% bytes**.
Gates: PPC green, native 418/418, reconcile 0 drift.

## Lane A — SampleInst360 missing-impl decomp (PASS, `c323cd43`)

The two virtuals wave-15 deferred, recovered from target asm + XAUDIO2/Voice/SynthSample
layouts (no speculation):
- **ElapsedTime 0 → 100%** — GetState(slot 0x64) → (float)(u64)SamplesPlayed / GetSampleRate();
  numerator-split temp matched target instruction order.
- **GetProgress 0 → 87.8%** — loop-aware sample pos → pos·1000/(LengthMs·GetNumSamples);
  residual is an open-coded signed-modulo regalloc-coalescing floor (head+tail byte-identical,
  permuter-exhausted). Honest floor.

## Lane B — ambiguous vtable-dispatch (PASS, `0514e2ca`)

2 of the 8 wave-11-skipped candidates resolved (dump_vtable slot-map + asm confirmed, native-gated):
- **DingoServer::OnMsg 88.9 → 99.9%** — Handle→Export + Disconnect→Logout wrong dispatch + explicit return.
- **DingoJob::CheckReqResult 86.2 → 86.4%** — wrong virtual dispatch.
- 4 confirmed structural floors (DingoJob::SendCallback, Rnd::DrawPreClear, StorePanel::Poll,
  RndShaderStandard::CalcShaderOpts); ChunkStream::Eof not-a-bug.

## Lane C — reference-less reconstruction BLOCKED (the boundary, `66cd48e5`)

- **DxMesh::OnSync (804B) + NgEnviron::Select (1956B) BLOCKED** — too speculative: render/GPU
  paths, no source reference (og lacks them, RB3 is Wii), m2c output unreliable. A wrong large
  render body is dangerous (MoggClip lesson). This establishes the reference-less-reconstruction
  boundary.
- compute_apres certed (artifact:build_env @ 42.3%, after clearing the stale is_stub).

## Lever-exhaustion map (the key output of this wave)

The remaining open frontier is **180 unpaired authorable virtuals, almost all `is_stub=1`** —
the Xbox render/audio layer (rnddx9 DxRnd/DxTex/DxMesh, synth_xbox). og-dc3 does NOT implement
them; RB3 is Wii (no D3D9). They split into two reconstruction categories:
- **Small/anchor-able stubs (synth_xbox/audio, ~200–800B with XAUDIO2/DSP header anchors):**
  RECOVERABLE — proven by this wave's SampleInst360 ElapsedTime/GetProgress. → **wave 17 target.**
- **Large reference-less render stubs (DxRnd 800–5188B):** BLOCKED — speculative + render-path
  dangerous. Not orchestratable; a dedicated manual-decomp effort.

All the cheap/medium classes are exhausted (const-mismatch, fake-impl, vtable dispatch/
construction, unicorn divergence). Wave 17 attacks the last tractable slice (anchor-able
synth_xbox stubs); after it, the residual is the blocked reference-less render layer + certified
compiler floors = true orchestration exhaustion.
