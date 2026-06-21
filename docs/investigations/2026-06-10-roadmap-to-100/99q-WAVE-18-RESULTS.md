# Wave 18 Results — synth_xbox + FFT/DSP + rnddx9 render-stub probe (17 fns)

**Date:** 2026-06-21 · **Landed through main `df3a0a9a`** · all-Opus, agentRetry, native-gated.
Goal-loop wave. **17 reference-less is_stub functions reconstructed from 0%.** Native 418/418,
reconcile 0 drift. (Metric-invisible per the wave-17 note — is_stub pre-counted done — but the
unpaired-stub pool is measurably shrinking: not-in-report authorable 952→935.)

## Lane A — Synth360 / EnvelopeGenerator (4 fns, `8f90bb8a`)
- Synth360::Poll 0→100 (812B main audio poll), Synth360::Terminate 0→100 (teardown)
- ReverbConvertI3DL2ToNative 0→93.8 (DX reverb formula; #pragma pack(1) on XAUDIO2FX structs
  was the unaligned-access tell, 69.5→91.8)
- EnvelopeGenerator::DoProcess 0→89.7 (ADSR state machine)

## Lane B — FFT / DSP math (5 fns, `d9dd5842`)
- FftIpp::FftRealCcs 0→97.6, FftReal 0→74.9 (fixed wrong reference-arg signatures mangling the symbols)
- fft_real_forward_scalar 0→79.1, fft_scalar 0→75.9, SpectralAnalysis::Analyze 0→69.3
- Blocked: fft_real_forward_altivec (VMX intrinsics), PeakDetector::Detect/gaussian (magic-constant math)

## Lane C — small rnddx9 render-stub PROBE: TRACTABLE (8/9, `df3a0a9a`)

**Verdict: the small rnddx9 render-stub class IS tractable** — a sharp contrast with wave-16's
blocked LARGE render functions. 8/9 recovered from target asm + D3D9 headers:
- DxMesh::Copy 100, VertexBufferData ×2 100, FillCompressedVerts 100, GetMultimeshFaces partial
- DxTex::FinishCompress 100, MakeDrawTarget partial; DxMovie::SetFrame partial
- Blocked: DxTex::DoCompress (GPU texture-tiling intrinsics)
This opens the ~47 small rnddx9 stub class as a viable lever.

## Loop status — two productive lever-classes active

The is_stub reconstruction lever (waves 16-18) has now landed ~38 functions. Remaining pool:
~150 synth_xbox + ~39 small rnddx9 stubs, ~75% recovery rate, manageable block rate (vectorized
math, GPU intrinsics, large render loops). **Levers NOT exhausted.** Block boundary is now well-
characterized: pure-math-with-magic-constants (FFT-altivec, granular, peak) and GPU-intrinsic
inner loops (DoCompress) are the floor; everything structural (poll loops, teardown, param
conversion, I/O, member copies, vbuf management) is recoverable. Wave 19 continues both classes.
