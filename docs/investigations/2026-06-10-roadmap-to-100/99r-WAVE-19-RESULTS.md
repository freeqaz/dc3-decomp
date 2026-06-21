# Wave 19 Results — Mic/FilterCoeffs + DxRnd + DxTex (18 fns + 8 STL)

**Date:** 2026-06-21 · **Landed through main (DxTex commit)** · all-Opus, agentRetry, native-gated.
Goal-loop wave. **~18 reference-less is_stub reconstructions + 8 free STL instantiations.** Native
418/418, reconcile 0 drift. Stub pool: not-in-report authorable 935→913.

## Lane A — synth_xbox Mic / FilterCoeffs (4 + 8 STL)
- DSP::LowpassCoefficients 0→97.2, HighpassCoefficients 0→98.7 (RBJ biquad cookbook)
- MicManagerXbox::AddRemoteMic 0→98.8 (GainEffect XAPO + RegisterRemoteTalker; branchless
  noChain select), Poll 0→87.9 (mic/chat/buffer loops; 0xDEADBEEFFACEF0 sentinel)
- 8 vector<ChatBuffer> STL template instantiations → 100% (free harvest incl. named
  _M_insert_overflow_aux)
- Blocked: MicXbox::Poll (magic-constant pitch math), MicManagerXbox::Init (ODR refactor across
  3 XAPO headers too invasive)

## Lane B — rnddx9 DxRnd render-state (5)
- 3 at 100% incl. D3DFORMAT_BitsPerPixel 0→100; 1 at 97.4% (bool-mask floor), 1 at 69.9%
  (float-regalloc floor, algorithm asm-justified). Mtx.h additive Multiply(Vector4,Matrix4,Vector4)
  decl; d3d9.h additive D3D types.

## Lane C — rnddx9 DxTex (7)
- 4 at 100% (DebugPrintAllTextures, DxTex::Init, FinishDrawTarget, Select); StartCompress 98,
  UnlockBitmap 73.8, TexelsLock 72.8 (regalloc floors). Blocked: DoCompress + 3 GPU-tiling intrinsics.

## Loop status
The is_stub reconstruction lever (waves 16-19) has landed ~56 functions from 0%. ~135 synth_xbox
+ ~30 rnddx9 stubs remain, ~75-80% recovery. **Levers NOT exhausted.** Block-floor well-
characterized (magic-constant DSP math, GPU-tiling intrinsics). Both lanes' shared-header touches
(d3d9.h, Mtx.h) are additive (decls/types) — no blast radius. Wave 20 continues both pools.
