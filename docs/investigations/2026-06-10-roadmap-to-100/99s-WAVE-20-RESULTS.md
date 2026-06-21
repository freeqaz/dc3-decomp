# Wave 20 Results — synth effects + DxRnd post-process + DxMesh (16 fns)

**Date:** 2026-06-21 · all-Opus, agentRetry, native-gated. Goal-loop wave. **16 reference-less
is_stub reconstructions** (11 to 100%). Native 418/418, reconcile 0 drift. Stub pool 913→888.

- **Lane A synth_xbox (6):** PitchShiftEffect::DoProcess 0→100; ExternalMic gatherGainAttribs/
  processGain/NumConnectedMics 0→100, dataReady 0→91; SynapseAPO::OnSetParameters 0→94 (moved
  private to match EAAX mangling). xbox.h +4 additive XMic decls.
- **Lane B rnddx9 (5):** DxRnd::CopyPostProcess/DrawRectDepth 0→100 (static vertex blobs byte-
  exact via data-diff), DxParticleSys::Init/NewObject 0→100, FinishPostProcess 0→83.7 (MSVC
  per-callsite MakeColor inline-fold floor). Multiply(Vec4,Mtx4) 36.4% reverted (FMA floor).
- **Lane C rnddx9 DxMesh (5):** VertSize/VertFVF/CanDraw/CheckFurTransformCache/FurWeight 0→100.
  VertFVF public→protected (target ?VertFVF@DxMesh@@IBAIXZ) + `friend class DxMultiMesh;` — the
  verifier caught the omitted friend as a MultiMesh.cpp build break; orchestrator added it.

**Loop:** ~72 functions reconstructed across waves 16-20. ~120 synth_xbox + ~25 rnddx9 stubs
remain. Levers NOT exhausted; block-floor stable (magic-constant DSP, GPU intrinsics, per-callsite
inline-fold). Lesson reinforced: public→protected visibility changes for target-mangling need the
matching friend decls (verifier-caught). Wave 21 continues.
