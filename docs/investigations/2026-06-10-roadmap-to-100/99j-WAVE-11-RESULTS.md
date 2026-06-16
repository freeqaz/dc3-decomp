# Wave 11 Results — deep vtable-dispatch sweep + finish og audio + data-symbol divergence

**Date:** 2026-06-16 · **Landed through main `dba783fc`** · **Orchestration:** 1 Workflow
(lanes A/B/C, 7 agents, ~1.04M tokens) + orchestrator-inline harvest with **per-lane
native-suite bisection**. **Authorship:** Fable (research+orchestrator) over Opus
implementer/verifier subagents.

Wave 11 continued the "find the bugs the metric hides" theme: deepen the wave-10
vtable-dispatch hunt, finish the structural carryovers, and open a **new** blind-spot
class (vtable/data construction). Headline holds at **99.06% fns / 96.74% bytes, open
260** — wave 11's wins are behavioral (raw + data plane) and the audio lives in the
excluded `synth_xbox` unit, so little headline movement is expected; the value is
correctness + two new standing tools.

Gates: PPC ninja green, check_native_compiles PASS, **milo-tests 418/418 0 FAIL**, web
release green, reconcile 0 drift.

## The harvest caught a real native regression (the wave-9/10 lesson, in a new form)

When all three lanes were first applied to main, the native suite went **418 → 391
(27 SEGFAULTs)** in asset-loading / venue-merge / flow tests. Per-lane bisection on the
native suite (NOT the PPC sweep — the verifiers' "0 net regressions" was a PPC-plane
check) isolated it to **lane A's MoggClip::LoadNumChannels fix**: its PPC-matching form
swapped the `mLoader`/`mStream` callsites to *sibling* virtuals to match the target's
slot offsets, but those siblings are semantically wrong, so on native (different vtable
layout) MoggClip called the wrong methods and segfaulted every asset-load path that
touches audio. **MoggClip was dropped**; the correct fix is a `Loader`/`Stream` header
vtable-decl-order correction (so `IsLoaded`/`GetNumChannels` land at the target slots),
not a callsite swap — deferred to a header lane. The other 4 lane-A fixes are clean
method-name swaps to existing semantically-correct methods and pass 418/418.

**Reinforced rule:** raw-plane vtable-dispatch and data-symbol fixes MUST be gated on the
full native suite, per-lane, on the integrated tree — the PPC match% sweep does not catch
semantic dispatch errors. Bake native-suite bisection into the harvest for any
dispatch/vtable wave.

## Lane A — deep vtable-dispatch sweep (VERIFIED PASS; 4/5 landed, `8c62474c`)

Reran `vtable_dispatch_scan.py` across the FULL binary (1,456 raw<norm candidates, no
cap; 299 flagged, 28 high-confidence). Landed 4 raw-plane behavioral corrections (each
asm + dump_vtable sub-object verified, 418/418):
- **CharEyes::Poll** — dt<0 branch `Exit()` → `Enter()` (RndPollable sub-object slot
  0xc→0x8; Enter resets facing for negative delta, Exit tears down interests). The
  wave-10 CharEyes::Poll "needs-review" candidate, now resolved.
- **HamCamShot::SetPreFrame** — `mCurrentShot->SetFrame` → `SetPreFrame`.
- **HamNavList::UpdateGestures** — `mDirectionGestureFilter->ClearSwipe` →
  `ResetHoverTimer` (×2).
- **InlineHelp::DrawShowing** — `mTextLabels[i]->DrawShowing()` → `Draw()`.
- **MoggClip::LoadNumChannels** — DROPPED (native segfault; see above).
- 8 candidates skipped as needs-deeper-review (DingoJob ×2, DingoServer, ChunkStream,
  Rnd::DrawPreClear, StorePanel, RndShaderStandard::CalcShaderOpts) — ambiguous receiver
  sub-object vtables in sub-90 functions; not safe to fix blind.

## Lane B — finish structural carryovers (VERIFIED PASS, `fe19e005`)

- **FxSendReverb360 SyncEffectParams 0→100 + CreateFx 0→100** — the LAST og-audio body
  (ReverbEffect::Params resolved from target asm; new `xdk/xaudio2/xaudio2fx.h`, one
  Xbox-only includer). With EQ/Wah (wave 10), the og `synth_xbox` FxSend family is now
  **complete**. (These are in the excluded `synth_xbox` unit, so they don't move the
  authorable headline, but they are real net-new matched functions.)
- **RndShaderProgram::Cache 85.7 → 86.0** (control-flow tail); residual is the r27/r28
  callee-saved regswap floor.

## Lane C — data-symbol divergence (NEW class, VERIFIED PASS, `dba783fc`)

New tool `scripts/analysis/data_symbol_scan.py` drives `objdiff --include-data` over
vtables/RTTI/jump-tables to find slots resolving to the WRONG function (wrong
virtual-declaration order) — the construction-side analogue of lane A, invisible to
code-symbol scoring. Real fixes (data-plane, 0 net regressions, 418/418):
- **DxMesh::OnSync + DxTex::SyncBitmap** — wrong-access vtable slot corrections.
- **RndMeshAnim::AnimTarget + Synth360::PreInit** — missing virtual overrides added so
  the vtable slot resolves to the right method.
- **FlowSetProperty** base-slot divergence — investigated, BLOCKED (priority/RTTI floor),
  reverted net-zero.

## Open follow-ups (wave 12 candidates)

1. **MoggClip header vtable-order fix** — the deferred MoggClip bug needs a
   `Loader`/`Stream` virtual-declaration-order correction in the header (so `IsLoaded`
   /`GetNumChannels` compile to the target slots), validated on the native suite. This is
   the *correct* form of the wave-11 MoggClip drop.
2. **8 skipped lane-A dispatch candidates** — resolve the ambiguous receiver sub-object
   vtables (DingoJob/DingoServer/ChunkStream/Rnd::DrawPreClear/StorePanel/CalcShaderOpts)
   before fixing; several are sub-90 functions where row alignment is less certain.
3. **Wider data_symbol_scan run** — lane C prioritized known-suspect vtables; a full
   data-symbol sweep may find more wrong-decl-order bugs.
4. **FlowSetProperty** priority/RTTI floor — characterize/cert.
5. **MILO_ENGINE_PIN bump** once the concurrent engine `FxSendNative.cpp` unk0 WIP commits
   (the engine working tree is live/shared and has been mid-change across waves 10–11).
6. ShaderProgram::Cache 86.0 r27/r28 floor — cert.

## Process note

The sibling `milo-native-engine` working tree was in uncommitted flux (a concurrent
agent's `FxSendNative.cpp` WIP) throughout waves 10–11. Because the engine is consumed
via `add_subdirectory` of the LIVE working tree (not a pinned checkout), its state can
break or fix DC3's native build out from under a lane. Lane gates were instructed to flag
this as external rather than edit the engine; the harvest re-verified the native baseline
on clean main before attributing regressions.
