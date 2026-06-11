# 23 — Open-Residual Census (Wave 5 Lane D)

**Date:** 2026-06-11. **Lane:** D (Wave-5 suite burndown + residual probe).
**Source:** Wave-5 plan §Lane D item 2.

This document catalogues the 396 functions in the authorable-done view that are
open (floor_certificate IS NULL, match_percent_normalized < 100, is_stub = 0,
not XDK/lib/zlib/json-c). Total: **396 functions / 266,136 bytes**. This is the
Wave 6 grind worklist.

---

## Summary

The wave-5 plan estimated 459 functions. The DB after wave-4 merges shows
**396 functions / 266,136 bytes** as the actual open residual (the delta is
from wave-4 wins + the wave-2/3 sync that landed before this measurement).

---

## Class Histogram

| Verdict   | Unicorn class            | Count | Bytes   | Notes |
|-----------|--------------------------|------:|--------:|-------|
| AT_LIMIT  | (null — unmeasured)      | 121   | 53,056  | Not yet unicorn-run; may be routable or floor |
| AT_LIMIT  | cap_exhausted            | 102   | 82,724  | Permuter-confirmed floor (regalloc/scheduling) |
| AT_LIMIT  | cap_exhausted_decomp     | 46    | 35,492  | Floor in our source; structurally complex |
| AT_LIMIT  | wild_jump_match          | 27    | 21,896  | vtable/switch dispatch mismatch — review per fn |
| AT_LIMIT  | call_count               | 16    | 7,608   | Behavioral divergence in call count |
| AT_LIMIT  | cap_exhausted_orig       | 15    | 12,804  | Floor in original binary (orig bug or noise) |
| (null)    | cap_exhausted            | 12    | 10,708  | Permuter-confirmed floor, not AT_LIMIT yet |
| (null)    | (null)                   | 10    | 8,520   | Needs unicorn measurement |
| AT_LIMIT  | orig_error               | 10    | 8,004   | Original binary has an error/exception in it |
| AT_LIMIT  | call_arg                 | 11    | 4,860   | Wrong argument in call site |
| (null)    | cap_exhausted_decomp     | 4     | 4,172   | Source-side floor not yet AT_LIMIT |
| AT_LIMIT  | stack_layout             | 2     | 4,016   | Stack layout divergence (vbase sinking) |
| (null)    | wild_jump_match          | 5     | 3,748   | vtable/switch divergence, not AT_LIMIT |
| (null)    | build_env                | 1     | 2,496   | Build environment difference (curl) |
| AT_LIMIT  | return_value             | 3     | 2,036   | Wrong return value |
| AT_LIMIT  | error                    | 2     | 1,272   | Exception path divergence |
| AT_LIMIT  | build_env                | 2     | 904     | Build environment floor |
| AT_LIMIT  | object_memory            | 4     | 648     | Object state divergence |
| (null)    | call_arg                 | 1     | 464     | Wrong argument, not yet AT_LIMIT |
| (null)    | stack_layout             | 1     | 372     | Stack layout divergence |
| AT_LIMIT  | unmapped_access_mismatch | 1     | 336     | Memory access outside tracked region |

**Key insight**: 102+46+15+12+4 = 179 fns / ~147K bytes are permuter-confirmed
floors (cap_exhausted*). The 121+10 = 131 fns / ~61K bytes without unicorn
measurements are the highest-priority investigation targets for Wave 6
(some will be routable once diagnosed). The 27+5 = 32 wild_jump_match fns
(25.6K bytes) warrant vtable/switch audits per the triage heuristics in
`feedback_audit_triage_heuristics.md`.

---

## Top 20 Functions Ranked by Bytes

| Rank | Symbol (demangled short) | Unit | Bytes | Match% | Verdict | Unicorn | Routability call |
|-----:|--------------------------|------|------:|-------:|---------|---------|-----------------|
| 1 | `Curl_http_readwrite_headers` | net/curl/http | 4,160 | 100.0% | (null) | EQUIVALENT | **Promote** — 100% normalized, EQUIVALENT; DB stale. Sync will fix. |
| 2 | `GameEndedDataPointJob::ctor` | lazer/net_ham/DataMinerJobs | 3,700 | 92.1% | AT_LIMIT | DIVERGENT/stack_layout | Stack layout divergence — try decl reorder or vbase sinking fix |
| 3 | `StreamRenderer::DrawToTexture` | gesture/StreamRenderer | 3,400 | 87.5% | AT_LIMIT | DIVERGENT/cap_exhausted | Permuter floor; would need asm-archaeology for further gain |
| 4 | `SaveLoadManager::Poll` | meta_ham/SaveLoadManager | 3,108 | 98.8% | AT_LIMIT | DIVERGENT/cap_exhausted | Very close; likely 1-2 small structural issues before cap |
| 5 | `RndParticleSys::Load` | rndobj/Part | 3,100 | 99.0% | AT_LIMIT | DIVERGENT/cap_exhausted | Very close; permuter floor |
| 6 | `RndScaleObject` | rndobj/Utl | 2,928 | 94.4% | (null) | DIVERGENT/cap_exhausted | Permuter floor, no AT_LIMIT; diagnose to confirm |
| 7 | `Invert(Matrix4)` | math/mtx | 2,800 | 70.7% | AT_LIMIT | DIVERGENT/cap_exhausted_decomp | Significant source divergence; worth asm-archaeology |
| 8 | `RndParticleSys::InitParticle` | rndobj/Part | 2,732 | 99.4% | AT_LIMIT | DIVERGENT/cap_exhausted_orig | Floor in original; not fixable |
| 9 | `RndText::WrapText` | rndobj/Text | 2,700 | 83.8% | AT_LIMIT | DIVERGENT/cap_exhausted | Permuter floor; complex string logic |
| 10 | `dprintf_formatf` | net/curl/mprintf | 2,676 | 84.4% | AT_LIMIT | DIVERGENT/orig_error | Original binary has error (likely exception handling) |
| 11 | `RndTexBlender::DrawShowing` | rndobj/TexBlender | 2,636 | 92.7% | AT_LIMIT | DIVERGENT/cap_exhausted_decomp | Source divergence; would benefit from Ghidra+m2c |
| 12 | `Curl_proxyCONNECT` | net/curl/http_proxy | 2,496 | 100.0% | (null) | DIVERGENT/build_env | 100% match, build_env noise; **promote** |
| 13 | `RndPropAnim::ForeachKeyframe` | rndobj/PropAnim | 2,468 | 0.5% | AT_LIMIT | (null) | Near-zero match — likely wrong symbol pairing or badly wrong structure |
| 14 | `SuperFormatString::ctor` | utl/SuperFormatString | 2,292 | 94.1% | AT_LIMIT | EQUIVALENT | EQUIVALENT at 94.1% — looks like fixtures match but codegen still diverges |
| 15 | `ArcDetector::UpdateOverlay` | gesture/ArcDetector | 2,160 | 72.6% | AT_LIMIT | DIVERGENT/cap_exhausted | Asm-archaeology previously tried (+8.4pp Wave 3); deeper analysis needed |
| 16 | `RndPostProc::LoadRev` | rndobj/PostProc | 2,124 | 94.3% | AT_LIMIT | DIVERGENT/cap_exhausted | Permuter floor; might have small structural gain |
| 17 | `RndText::OnComputeCharWidths` | rndobj/Text | 2,116 | 79.8% | (null) | DIVERGENT/cap_exhausted | Asm-archaeology previously tried; check remaining divergence |
| 18 | `NgSpotlightDrawer::SetupXSection` | world/SpotlightDrawer_NG | 2,104 | 35.3% | AT_LIMIT | DIVERGENT/cap_exhausted_decomp | Major source divergence; requires full rewrite from Ghidra |
| 19 | `RndShaderStandard::CalcShaderOpts` | rndobj/Shader | 2,100 | 38.5% | AT_LIMIT | DIVERGENT/wild_jump_match | Switch dispatch mismatch; vtable audit recommended |
| 20 | `WorldInstance::SyncDir` | world/Instance | 2,096 | 99.4% | AT_LIMIT | DIVERGENT/cap_exhausted_orig | Floor in orig binary; not fixable from source |

---

## Top Units by Remaining Bytes (Top 20)

| Rank | Unit | Bytes | Fns | Avg match% |
|-----:|------|------:|----:|-----------:|
| 1  | rndobj/Text          | 13,788 | 14 | 86.4% |
| 2  | rndobj/Shader        | 9,480  | 11 | 40.6% |
| 3  | world/Spotlight      | 7,920  | 6  | 84.6% |
| 4  | rndobj/Part          | 5,832  | 2  | 99.2% |
| 5  | math/Geo             | 5,428  | 8  | 92.9% |
| 6  | world/CameraShot     | 5,232  | 4  | 97.7% |
| 7  | hamobj/RhythmDetector| 4,684  | 5  | 93.9% |
| 8  | rndobj/Utl           | 4,336  | 4  | 89.0% |
| 9  | net/curl/http        | 4,160  | 1  | 100.0% (promote!) |
| 10 | gesture/StreamRenderer | 4,080 | 3 | 74.5% |
| 11 | utl/MemTracker       | 3,784  | 12 | 68.4% |
| 12 | world/SpotlightDrawer_NG | 3,732 | 3 | 72.0% |
| 13 | lazer/net_ham/DataMinerJobs | 3,700 | 1 | 92.1% |
| 14 | gesture/ArcDetector  | 3,400  | 5  | 84.7% |
| 15 | world/SpotlightDrawer| 3,380  | 6  | 96.6% |
| 16 | rndobj/Lit_NG        | 3,304  | 4  | 77.1% |
| 17 | gesture/LiveCameraInput | 3,268 | 6 | 87.8% |
| 18 | lazer/meta_ham/SaveLoadManager | 3,108 | 1 | 98.8% |
| 19 | rndobj/Line          | 3,108  | 3  | 78.6% |
| 20 | hamobj/HamNavList    | 3,040  | 5  | 92.8% |

---

## Routable Candidates (Wave 6 Priority)

Functions that are NOT AT_LIMIT and NOT cap_exhausted — most likely to benefit
from targeted asm-archaeology or structural fixes:

**Behavioral divergences (call_count/call_arg/return_value — real bugs):**
- `RndText::ReplaceMissingCharacters` (1,384 bytes, 93.0%, AT_LIMIT/return_value) — wrong return value; fixable
- `BeatClock::OnSyncState` (1,144 bytes, 95.7%, AT_LIMIT/call_arg) — wrong call argument
- `RndLight::Projection` (764 bytes, 72.9%, AT_LIMIT/call_count) — call count divergence
- `MemAlloc` (1,172 bytes, 1.5%, AT_LIMIT/call_count) — near-zero; likely a wrapping/delegation issue
- `NgDOFProc::Set` (464 bytes, 94.7%, null/call_arg) — call argument bug, not AT_LIMIT

**Wild_jump_match (vtable/switch divergence, not AT_LIMIT):**
- `Spotlight::BuildCone` (1,140 bytes, 89.8%) — wave-3 target; vtable audit may close it
- `Spotlight::BuildNGQuad` (964 bytes, 88.0%) — similar to BuildCone
- `SetVHBlurWeights` (740 bytes, 70.3%) — switch mismatch
- `RndFont::SetCharInfo` (600 bytes, 86.4%) — vtable slot issue
- `RndShaderSimple::CalcShaderOpts` (304 bytes, 95.3%) — switch divergence

**Near-promotable (99%+, no unicorn block):**
- `Curl_http_readwrite_headers` (4,160 bytes, 100.0%, EQUIVALENT) — **promote now**
- `Curl_proxyCONNECT` (2,496 bytes, 100.0%, build_env) — **promote** (build_env noise is unfixable floor)
- `RndParticleSys::InitParticle` (2,732 bytes, 99.4%, cap_exhausted_orig) — orig floor, **certify**
- `WorldInstance::SyncDir` (2,096 bytes, 99.4%, cap_exhausted_orig) — orig floor, **certify**

---

## Anomalies / High-Priority Investigation Targets

1. **`RndPropAnim::ForeachKeyframe` (0.5%)** — Near-zero normalized match with AT_LIMIT verdict
   is suspicious. This is either a wrong symbol pairing in the DB or a function whose
   source is completely wrong structurally. Run `run_objdiff` to diagnose.

2. **`math/mtx.Invert` (70.7%, cap_exhausted_decomp)** — 2,800 bytes with significant source
   divergence. The `cap_exhausted_decomp` class means our source causes the divergence.
   Ghidra decompile + full rewrite from asm is the approach.

3. **`utl/MemTracker` (12 fns, avg 68.4%)** — 12 functions at AT_LIMIT with no unicorn
   class (unmeasured). This unit alone has 3,784 bytes of unmeasured residual. Run
   batch unicorn measurement before investing in fixes.

4. **`rndobj/Shader` (11 fns, avg 40.6%)** — Wildly below average match percentage.
   4 of 11 are wild_jump_match (CalcShaderOpts variants). The switch dispatch table
   is likely mismatched. Per the vtable audit heuristic from triage doc, run vtable
   dump on each shader class to identify the slot ordering.

5. **`SuperFormatString::ctor` (94.1%, EQUIVALENT)** — EQUIVALENT at 94.1% is unusual.
   The function behavior matches (good), but codegen still diverges. This is a
   style-only fixable divergence; permuter should handle it.

---

## Notes on Query

- **Authorable filter**: excludes `default/xdk/*`, `default/lib/*`,
  `default/system/zlib/*`, `default/system/json-c/*`.
- **floor_certificate IS NULL**: excludes already-certified floors.
- **match_percent_normalized < 100**: uses normalized scoring (the canonical gate).
- **Total from query**: 396 functions / 266,136 bytes (wave-5 plan estimated 459 —
  the delta is ~63 functions promoted/certified since the wave-5 plan was written
  at 97.80% authorable).
- **DB read from**: `/home/free/code/milohax/dc3-decomp/decomp.db` (main, read-only).
- **Build plane**: main HEAD `00e5895b` (wave-5 plan commit).
