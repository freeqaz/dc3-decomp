# 99e — Execution Wave 7 Results

**Date:** 2026-06-11. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`99d-EXECUTION-WAVE-7.md`](99d-EXECUTION-WAVE-7.md). **Wave-6 results:**
[`99c-WAVE-6-RESULTS.md`](99c-WAVE-6-RESULTS.md). **Scope:** the two never-run roadmap
phases — Phase 2 og-dc3 ports, native-safe half (A) and Phase 1.2 funclet pairing
reconciliation (D) — plus the open-residual grind continuation (B) and the first-ever
web/WASM build + boot of the six-wave native fix-train + engine-pin bump (C).

All four lanes ran in isolated worktrees. **Three lanes (B, C, D) passed adversarial
verdict; Lane A is PASS-WITH-SHORTFALL** — verifier verdict `pass` on correctness and
rule-compliance, but the `≥40 net-new functions` acceptance target is NOT met (28
source-level / 14 report.json-level), and the shortfall is **structural** (ICF folding +
header divergence), not effort. **No lane committed to `main`** and **no lane wrote
`decomp.db`** — main HEAD is still `f5e3e3d1` (the Wave-7 plan doc) and the engine pin on
main is still `f75339a` (the bump lives only on Lane C's branch). Branches are staged for
the orchestrator to merge and apply.

> **Build-plane rule (still enforced):** every match-percent and verdict number below
> names its build plane. Worktree `run_objdiff` readings are *claims*; final certification
> happens on `main` after the sync. A worktree reading is not evidence about main.

> **✅ NO MERGE BLOCKER THIS WAVE.** The single-owner rule held: **all six lane pairs merge
> cleanly** (`git merge-tree --write-tree`, rc=0 / no conflict on every pair, re-verified
> from main `f5e3e3d1`). **No file is touched by more than one lane** (`sort | uniq -d` of
> all four changed-file sets is empty). Lane A's own apply-step worried it might collide
> with Lane B on `os/File.cpp` — it does NOT: Lane A touches `synth_xbox/` + `oggvorbis/` +
> `link_glue.cpp`; only Lane B touches `os/File.cpp`. Lane C owns `char/CharIKFoot.cpp`;
> no other lane touches it.

---

## TL;DR headline numbers

| Metric | Value | Build plane / notes |
|---|---|---|
| **og-port net-new (Lane A)** | **28 source-level functions** (all 100% normalized) / **14 report.json representatives** (ICF-folded) — **SHORT of the ≥40 target** | Lane A worktree `wave7/a-og-port` dc3 Xbox/PPC plane, run_objdiff normalized, fresh objects |
| **og-port shortfall is structural** | ICF folds the byte-identical `Recreate`/`UpdateMix`/`OnParametersChanged` delegations across all 6 FxSend subclasses to one representative each; every remaining native-safe candidate re-verified as a header-divergence structural port (XAPO/dsp tree for `SyncEffectParams`/`CreateFx`) — NOT a clean graft | Lane A; the `≥40` figure belongs to a future coordinated **structural** lane, not a behavior-neutral graft pass |
| **Grind qualifying wins (Lane B)** | **5 qualifying** (+10pts or 100%) — MEETS the ≥5 bar: op49 98.9→**100**, op61 98.9→**100**, DingoServer::ManageJob 99.5→**100**, RndBitmap::PixelColor 97.8→**100** (NEW), FileGetPathBuf 97.4→**100 normalized** (NEW) | Lane B worktree `wave7/b-grind-2` plane, all re-measured by verifier via run_objdiff |
| **Grind: two new wins are real source fixes** | PixelColor: cache `mPalette` in a callee-saved reg across the `PixelIndex()` call (was re-loading `[this+0x14]`); FileGetPathBuf: drop the `if(iBuf!=oBuf)` guard so `strcpy` runs unconditionally (matches target + og-dc3; self-copy is idempotent) — both behavior-neutral | Lane B; the two flagged near-misses (ClearSnapshots 99.9%, Vector3DESmoother::Smooth 94.8%) were RE-DIAGNOSED and confirmed register/FPR floors — NOT forced |
| **Web/WASM build (Lane C)** | **release + debug both GREEN** — `[100%] Built target dc3-web`, 0 errors. release `.wasm` 16M (+2.4M .br / 4.2M .gz); debug `.wasm` 8.7M (+2.7M .gz) | Lane C worktree `wave7/c-web-verify`, emsdk; independently re-built by verifier |
| **Web boot (Lane C)** | **boots past prior checkpoint** — RUNTIME_INIT_OK → engine init via App → 493 assets loaded → `game_screen` UIScreens defined (panel[1..7]) → AudioDevice 44100Hz. No gMainThreadID assert flood, no WASM crash. WebGPU `requestAdapter` returns null (headless, no GPU — expected env limit, not a defect) | Lane C, headless Chromium vs DC3 web build + orig-assets |
| **Engine pin (Lane C)** | bumped `f75339a` → **`8fb669d`** (perf: L1 vertex-unpack cache + WarmGpuForDir). **Native green gate held: 324 PASSED / 0 FAILED / EXIT 0** with the new pin | Lane C; pin change is on the branch only — **main still `f75339a`** until merge |
| **WASM link breaks found+fixed (Lane C)** | **2** — `gDc3PollSeq` duplicate symbol (wave-6 CharIKFoot vs HamDirector tent-def, silently merged by ELF, fatal under wasm-ld) → `extern`; `gFrameTraceActive`/`gFetchSync*` undefined (engine `8fb669d` added FrameTrace to WebAssets.cpp; dc3-web has no Loader.cpp) → strong defs in PipelineManager.cpp under `#ifdef HX_WEB` | Lane C; both mechanical build breaks (per plan), not structural hacks |
| **Unpaired funclets (Lane D)** | **187** unpaired `fn_<addr>` in the current report.json (doc 01/05 `~232` was STALE — written before prior waves' pairings landed) | Lane D, shared `build/373307D9/report.json`; independently verified |
| **Funclet classification (Lane D)** | 187 = **8 Class A** (`??__E`/`??__F` static-init/dtor, bytes 100% — pairable) + **2 Class B** (large COMDAT in rndobj/Mesh — name lost in XEX split, unpairable) + **153 Class C** (XDK/binkxenon, non-authorable, expected) + **24 Class D** (synth_xbox/jpeg/etc. orphaned, no matching base symbol) | Lane D, evidenced via COFF symbol-byte read |
| **Funclet pairing fix (Lane D)** | objdiff fork extended `is_funclet_like()` to include `??__E`/`??__F` prefixes. New binary (worktree-local, **shared binary untouched**) measured delta: **187 → 180** unpaired (−7) and **+321 total matched** (the +321 > +7 because 314 already-named `??__E`/`??__F` symbols on both sides were also previously blocked from byte-fallback scoring) | Lane D; objdiff fork branch `wave7/funclet-pairing` @ `e5987fb`; **fork merge + shared-binary rebuild are orchestrator steps** |

**No merge blocker.** All disjoint, all six pairs clean. The only acceptance gap is Lane A's
**28-source / 14-report net-new vs the ≥40 target** — a structural cap (ICF + header
divergence), recommend accept-partial-with-rationale (see below).

---

## Per-lane outcomes

### Lane A — og-dc3 port lane, native-safe half (Opus) — **PASS (correctness) / SHORTFALL (target)** (status: partial, repaired)

- **Branch:** `wave7/a-og-port` (2 commits: `086748a0` original + `8e1ac429` repair) ·
  **Worktree:** `/home/free/code/milohax/wt-wave7-a-og-port`
- **Files (19):** 14× `src/system/synth_xbox/FxSend*` (.cpp/.h for BitCrush/Chorus/Compress/
  Distortion/Flanger/Reverb + FxSend.cpp + FxSendMeterEffect.cpp), 3× `synth_xbox/soundtouch/
  .../FIFOSample*` (original commit), `src/system/oggvorbis/VorbisMem.cpp`, `src/link_glue.cpp`.
- **Deliverable: the verifier's blocking required-fix was addressed** — the full og-dc3
  FxSend360 base-delegation family was ported (the explicit Wave-7-repair ask), plus two
  additional clean grafts found while re-deriving the worklist.
- **Net-new this repair round (23 source functions, all 100% normalized):**
  (1) **17 FxSend delegations** — `Recreate`/`UpdateMix`/`OnParametersChanged` across the six
  stub subclasses (BitCrush/Chorus/Compress/Distortion/Reverb ×3 + Flanger ×2, since Flanger's
  `OnParametersChanged` pre-existed). Each header gains the 3 virtual decls after `OBJ_SET_TYPE`
  in og's vtable order; bodies delegate to `FxSend360::Refresh`/`UpdateVolumes`/`SyncEffectParams`
  — **no `dsp/` header needed**, which is exactly why these are clean grafts while
  `SyncEffectParams`/`CreateFx` are not. (2) **VorbisMem** `OggMalloc`/`OggCalloc`/`OggRealloc`/
  `OggFree` + `sLicense` dynamic initializer = 5, behind `#ifndef HX_NATIVE` so native keeps its
  host-malloc versions; `OggRealloc` uses the in-header `MemTemp` RAII guard to match
  byte-for-byte (our `MemDoTempAllocations(bool,bool)` ctor diverges from og's no-arg form).
  (3) **FxSend360::RemoveOwnerVoice** = 1 (og FOREACH-erase graft, 47 instr).
- **Cumulative wave-7 Lane A:** 5 original (commit `086748a0`: soundtouch `ptrBegin` const +
  FxSendMeterEffect delegations) + 23 repair = **28 net-new source functions**, ICF-folded to
  **14 report.json representatives**.
- **Measured (Lane A worktree `wave7/a-og-port` dc3 Xbox/PPC plane, run_objdiff normalized):**
  - `OnParametersChanged@FxSendBitCrush360` 0%→**100.0%** (2/2 instr); `UpdateMix@FxSendChorus360`
    0%→**100.0%** (2/2); `UpdateMix@FxSendDistortion360` 0%→**100.0%** (2/2); `Recreate@FxSendReverb360`
    0%→**100.0%** (2/2); `OnParametersChanged@FxSendFlanger360` 0%→**100.0%** (2/2);
    `RemoveOwnerVoice@FxSend360` 0%→**100.0%** (47/47).
  - `OggMalloc`/`OggCalloc`/`OggRealloc` all **100.0%** (7/7, 25/25, 26/26); `OggRealloc` fixed
    via `MemTemp` from 84.6%. `OggFree` + `??__EsLicense` both **100.0%** via fuzzy hint
    (target-side ICF-merged / `fn_82EE0B00` stub-paired, not separately addressable).
  - Regression probes: `ptrBegin@FIFOSampleBuffer`, `Recreate@FxSendMeterEffect360`,
    `AddOwnerVoice@FxSend360` all **100.0%** (original wins + FxSend.cpp neighbor unaffected).
  - `measure_progress.sh --current-dir worktree --functions HEAD(086748a0)`: **+9 matched
    representatives, 0 regressions**, 460 B affected; synth_xbox +0.17%.
  - Full worktree `ninja`: **EXIT 0** — Xbox/PPC build green, report.json regenerated.
  - Native isolation: `src/link_glue.cpp` is NOT native-compiled; `synth_xbox/` excluded;
    `VorbisMem.cpp` compiles to an **empty TU under HX_NATIVE** (clang `-DHX_NATIVE -fsyntax-only`
    exit 0); native `OggMalloc`/etc come from `native_link_glue.cpp` unchanged (lines 354-361).
- **Verifier spot-checks (5 functions, independent run_objdiff on worktree plane):**
  `OnParametersChanged@FxSendBitCrush360` 2/2, `UpdateMix@FxSendChorus360` 2/2,
  `Recreate@FxSendReverb360` 2/2, `RemoveOwnerVoice@FxSend360` 47/47, `OggMalloc` 7/7 — all 100%.
  Spot-checked against og source: delegation bodies are verbatim ports (the only difference, og's
  unqualified `Refresh(sends)` vs the port's `FxSend360::Refresh()`, is semantically identical —
  `FxSend360` is the only base providing the method). `OggRealloc`'s `MemTemp{}` deviation is
  documented and achieves the byte match.
- **Guard preservation (verifier diff):** **0 HX_NATIVE guards dropped** across both commits;
  the repair commit ADDS one `#ifndef HX_NATIVE` guard in `VorbisMem.cpp` (preserves native
  behavior). FxSend files are pure Xbox-only (no guards to drop).
- **Contradictions (Lane A corrected prior docs):**
  - **Plan doc 99d Lane A acceptance `≥40 net-new`:** the native-safe clean-graft lane CANNOT
    reach 40. Source-level it tops out at **28** (FxSend delegation family + VorbisMem +
    RemoveOwnerVoice maxed); report.json-level it is **ICF-capped at ~14** (the byte-identical
    delegations across all subclasses fold to one representative each — the CLAUDE.md
    merged-symbol pattern). The 40 figure is reachable only by including header-divergence
    structural ports, which the wave plan itself partitions into the NON-native-safe half.
  - **Original wave-7 report claimed the honest native-safe lane is `~5 functions`** —
    **CORRECTED to ~28.** The original under-explored: it did not port the FxSend delegation
    family (~17 fns it wrongly dismissed as header-blocked but which need no `dsp/` header), did
    not port VorbisMem (5 fns, native-safe behind an HX_NATIVE guard it missed), and did not port
    `RemoveOwnerVoice` (1 clean fn). Doc-09's `~186` still overstates the native-safe half.
  - Verifier required-fix step suggested porting og's `dsp/` headers OR forward-declaring "only
    what the delegation body needs." Confirmed precisely: the 3-line delegation bodies need
    NOTHING from the effect type (only `FxSend360` base methods), so neither was required. The
    `dsp/` tree is needed ONLY for per-class `SyncEffectParams`/`CreateFx` (out of scope —
    XAPO/CXAPOBase/dsp::StandardEffect divergence from our cross-platform `synth/` tree).
  - `FxSendPitchShift360`/`FxSendSynapse360` `SyncEffectParams`+`CreateFx` remain blocked by a
    **jeff source-partition mismatch** (the tracked `synth_xbox/FxSendPitchShift` unit maps to
    `FxSendPitchShift.cpp` — an empty file — while the class body lives in `FxSendPitchShift360.cpp`
    compiled to an untracked obj). Confirmed via objdiff.json `source_path`. Fixing it is a jeff
    config change (out of scope, risky to shared config), not a source graft. The body itself
    would be clean (PitchShiftEffectParams/mRatio/PitchShiftEffect.h all exist in our tree).
- **Risks:**
  - **Acceptance shortfall persists (28 source / 14 report vs ≥40).** Structural, not effort —
    every remaining native-safe candidate was re-verified as a header-divergence structural port.
    Recommend orchestrator accept-partial-with-rationale, OR re-scope `≥40` to a coordinated
    structural lane (og xdk/XAUDIO2→xapobase + dsp/StandardEffect + per-effect dsp/*Effect.h
    migration; FxSend360 named-member reconciliation `unk8/unk20/unk30→mVoices/mFx`; jeff
    FxSendPitchShift/Synapse partition fix) gated on the native build.
  - **`VorbisMem.cpp` is native-compiled — the `#ifndef HX_NATIVE` guard is load-bearing.** If a
    future refactor removes it (or native stops defining HX_NATIVE for this TU), the native link
    gets duplicate `OggMalloc`/`OggFree`/etc symbols. Verified correct now; the
    `check_native_compiles.sh` gate on main catches any regression.
  - **`link_glue.cpp` OggFree stub removed** (VorbisMem.cpp now defines it for PPC). If a
    concurrent lane re-adds an OggFree stub to link_glue.cpp, the PPC link breaks. link_glue.cpp
    is PPC-only so native is unaffected.
  - `fn_82EE0B00` (sLicense dynamic initializer) may persist as 0% in report.json post-merge
    (jeff initializer-pairing limitation) despite the verified 100% byte match — do NOT treat it
    as a regression or unfinished work.
- **Verdict required-fixes:** acceptance shortfall is a structural cap, not an implementation
  defect (verifier: "No code-level required fixes — all verified implementations are correct, at
  100%, and rule-compliant"). The verifier's re-scope recommendation for `≥40` is a future-lane
  spec, not a merge blocker.

### Lane B — grind continuation (Opus) — **PASS** (status: complete, repaired)

- **Branch:** `wave7/b-grind-2` (4 commits: `5ef74399`, `4d20554a`, `dfcbb242`, `c0f507a8`) ·
  **Worktree:** `/home/free/code/milohax/wt-wave7-b-grind-2`
- **Files (6):** `synth/ByteGrinder.cpp`, `net/DingoSvr.cpp`, `gesture/LiveCameraInput.cpp`,
  `math/DoubleExponentialSmoother.cpp`, `rndobj/Bitmap.cpp` (NEW this round), `os/File.cpp`
  (NEW this round).
- **Deliverable MET — the ≥5 qualifying-win bar is satisfied.** The repair round addressed the
  verifier's sole required fix (3-of-≥5 shortfall) by adding **2 NEW qualifying wins** (both
  reach 100%), bringing the lane to 5. Crucially it did NOT force the two flagged near-misses —
  it sourced fresh wins from the open residual set instead.
- **5 qualifying wins (verifier re-measured via run_objdiff, worktree plane):**
  - **op49** (ByteGrinder) 98.9→**100.0%** (30/30 equal).
  - **op61** (ByteGrinder) 98.9→**100.0%** (30/30 equal).
  - **DingoServer::ManageJob** 99.5→**100.0% normalized** (99.2% raw = reloc noise; 117/117 equal)
    — direct `mHttpReq` access.
  - **RndBitmap::PixelColor** 97.8→**100.0%** (52/52 equal) — NEW. The target caches `mPalette`
    in a callee-saved reg (r26) across the `PixelIndex()` call while our build re-loaded
    `[this+0x14]` (the base's extra `lwz` at idx 27). Fix: cache `mPalette` into a local and
    inline the `PaletteColor` body so the compiler materializes `mPalette` once and reuses it.
    Value-identical — `PixelIndex` does not modify `mPalette`. `PaletteColor` stays defined for
    its other callers.
  - **FileGetPathBuf** 97.4→**100.0% normalized** (99.5% raw = `kAssertStr` "File.cpp"
    address-relocation noise; all 77 instr byte-equal) — NEW. Fix: removed the `if(iBuf!=oBuf)`
    guard so `strcpy(oBuf,iBuf)` runs unconditionally, matching the target's inlined byte-copy
    (base emitted an extra `cmplw cr6,r30,r31`+`beq` to skip self-copy). og-dc3 reference does
    the same unconditional strcpy; self-copy when src==dst is idempotent → behavior-neutral.
    `FileGetPath` caller independently re-verified **100.0%**, no regression.
- **Material wins (real, sub-qualifying, from predecessor commits):** ClearSnapshots 97.8→**99.9%**
  (+2.1); Vector3DESmoother::Smooth 92.9→**94.8%** (+1.9). Both correctly counted as material
  only, NOT qualifying.
- **The two verifier-flagged near-misses were RE-DIAGNOSED and confirmed floors — NOT advanced:**
  - **ClearSnapshots 99.9%:** single `lwz` r31-vs-r30 base for the SAME address (this+0x11f8) — a
    register-base coalescing choice. Every reference-binding attempt regressed it to 93%; permuter
    0 improvement.
  - **Smooth 94.8%:** f0↔f13 volatile-FPR cascade + commutative `fadds` + structural −0x10 frame
    delta. X-first reorder regressed it to 92.9%.
  - Reaching 100% on either would require the prohibited per-callsite hacks; the 2 required wins
    were instead sourced fresh (PixelColor, FileGetPathBuf).
- **Confirmed floors this round (diagnosis + permuter 0-improvement, evidence for certify_floor):**
  `RndMesh::DeleteBones` 99.3 (single MSVC count-spill `stw r5,0x54,r31`); `ConvertBonesToTranses`
  99.5 (single dead `Refs().end()`-addr precompute); `MemHeap::Free` 95.0 (0xDEADDEAD
  loop-invariant constant-hoist scheduling, permuter 0); `RndMesh::Replace` 96.9 (inner beq/bne
  branch-direction floor — all 3 source forms tried); `RndShaderSimple::CalcShaderOpts` 96.4
  (subfic-vs-subic bool→mask negation lowering); `RndLightAnim::Copy` 94.0 (callee-saved
  r28/r29/r30 cascade, permuter 0); `RndAmbientOcclusion::BuildObjectLists` 99.2 (regswap
  r24/r25 + offset-swap, decl-reorder regressed, permuter 0); `RndMesh::OnSync` 92.4 /
  `TransformNormal` 94.8 / `CharBones::ScaleAdd` 98.2 (volatile FPR / callee-save AT_LIMIT
  cascades). All experiments reverted cleanly (via Edit, not git checkout/stash).
- **Measured (Lane B worktree plane):** full PPC build GREEN (Game 160/160 linked, Milo Engine
  808/808 linked, Milo Engine matched 77.05%). Build artifacts `Bitmap.obj` + `File.obj` present.
  Predecessor wins re-verified intact: ManageJob 117/117, op49/op61 30/30.
- **Contradictions (Lane B):**
  - report.json/decomp.db labels are **stale-misleading** for the two new wins: PixelColor was
    labeled "auto: all mismatches unfixable" (97.8%) and FileGetPathBuf likewise (97.4%) — both
    were in fact source-fixable to 100% (a cached-member-reload and a redundant src!=dst guard).
    The "all mismatches unfixable" auto-label over-classifies cached-load and redundant-guard
    patterns as floors; **trust per-function diagnosis over the auto-label.**
  - **FileGetPathBuf reaches 100% NORMALIZED but stays 99.5% RAW** — the raw residual is purely
    the `kAssertStr` "File.cpp" address relocation (all 77 instructions byte-equal). Any tracker
    that gates COMPLETE on raw==100 will mis-flag this; **normalized is the canonical metric.**
  - Several functions the wave-6/wave-7 ranked open set implies are workable are confirmed floors
    (DeleteBones, ConvertBonesToTranses, MemHeap::Free, RndMesh::Replace, CalcShaderOpts,
    RndLightAnim::Copy, BuildObjectLists, OnSync, TransformNormal, ScaleAdd) — the genuinely
    source-reachable wins in this band were the cached-load / redundant-control-flow class, not
    the regswap/FPR/scheduling cascades, consistent with the wave-6 floor-dominated finding.
- **Risks:**
  - FileGetPathBuf now runs `strcpy` even when src==dst. Behavior-neutral for a byte-wise strcpy;
    matches BOTH the original debug binary AND og-dc3. Only theoretical concern is a strcpy that
    asserts on self-overlap — the inlined target byte-copy does not. Low risk.
  - PixelColor inlines the PaletteColor body locally; `PaletteColor` is still defined and used by
    other callers (source unchanged). Byte-identical output to the target. No semantic change.
  - All 6 edited files are PPC-faithful + behavior-neutral; none touch HX_NATIVE engine logic,
    feet/IK code, the boot path, or test code. Native milo-tests was NOT re-run in this worktree
    (no native build configured) — the verifier/orchestrator should re-run the green gate on the
    merged result. File.cpp retains its 4 HX_NATIVE guard blocks; the FileGetPathBuf change is in
    the unguarded Xbox path.
- **Verdict required-fixes:** none (the ≥5 bar is now met).

### Lane C — web/WASM verification + engine pin (Sonnet) — **PASS** (status: complete)

- **Branch:** `wave7/c-web-verify` (2 commits: `eeb5d08d` engine-pin bump + `f6117baa` WASM
  link-break fixes) · **Worktree:** `/home/free/code/milohax/wt-wave7-c-web-verify`
- **Files (3):** `native/CMakeLists.txt`, `native/src/gfx/PipelineManager.cpp`,
  `src/system/char/CharIKFoot.cpp`.
- **Deliverable MET (all four acceptance items):** pin bumped + native suite still green; web
  release+debug build green; boot status reported with evidence.
- **Engine pin:** `f75339a53f8a60845391a6817be07fdab08c2088` → **`8fb669d91b428bbda31e79728360aa5d56666570`**
  (perf: L1 vertex-unpack cache + WarmGpuForDir API), via `scripts/bump-engine.sh`. **Native
  green gate held with the new pin: 324 PASSED / 0 FAILED / EXIT 0** (415 run, 91 skipped, run
  from `orig-assets/`, verifier-reproduced).
- **2 WASM link breaks found + fixed (both mechanical, per plan):**
  - `gDc3PollSeq` **duplicate symbol** — wave-6's CharIKFoot.cpp added `int gDc3PollSeq = 0;`
    while HamDirector.cpp also tent-defined it; ELF silently merged both (native + PPC) but
    wasm-ld rejects duplicates. Fix: `CharIKFoot.cpp` line 61 → `extern int gDc3PollSeq;` (the
    definition stays in HamDirector.cpp at line 26 inside `#ifdef HX_NATIVE`; the only usage
    `++gDc3PollSeq` is inside an HX_NATIVE block, so the PPC build sees only an unreferenced
    extern — no linker error). **This is exactly the class of bug Lane C exists to catch** — the
    wave-6 implementer never ran a WASM build.
  - `gFrameTraceActive`/`gFetchSync*` **undefined symbols** — engine `8fb669d` added FrameTrace
    instrumentation to WebAssets.cpp; dc3-web compiles WebAssets.cpp directly without linking
    `libmilo-engine.a` (where the engine provides weak fallback defs), so dc3 needs its own
    strong defs. Fix: 18 strong definitions of all FrameTraceCounters in PipelineManager.cpp
    under `#ifdef HX_WEB` (all 18 names/types match FrameTraceCounters.h; verifier-confirmed).
- **Measured (Lane C worktree `wave7/c-web-verify`):**
  - Web build `scripts/web/build.sh --both`: **release + debug both `[100%] Built target dc3-web`,
    0 errors** (verifier independently re-ran). release `.wasm` 16M (+2.4M .br / 4.2M .gz); debug
    `.wasm` 8.7M (+2.7M .gz).
  - Headless Chromium boot: LOADING_WASM_JS → WASM_JS_LOADED → instantiateStreaming 71ms →
    **RUNTIME_INIT_OK** → "DC3 Web Port — Initializing" → 493 assets loaded → engine init via App
    → **`game_screen` UIScreens defined (panel[1..7])** → AudioDevice 44100Hz. No gMainThreadID
    assert flood, no WASM crash / pageerror. WebGPU `requestAdapter` returns null (headless, no
    GPU — expected; the full renderer path past RUNTIME_INIT_OK is not GPU-exercised here).
  - PPC build green after changes (`ninja` clean); milo-tests gate NOT needed for the PPC plane
    (all edits HX_WEB-only or HX_NATIVE-only).
- **Contradictions (Lane C corrected prior docs):**
  - Wave-7 plan "engine `8fb669d` — perf changes only" is CONFIRMED correct but **UNDERSTATES the
    impact**: the new engine commit added FrameTrace instrumentation to WebAssets.cpp that
    introduced WASM link errors absent from the prior pin. The bump requires the FrameTrace
    counter defs in PipelineManager.cpp for the WASM target (dc3 is not rb3 — no Loader.cpp
    provides them).
  - Wave-6 results "engine-pin bump (optional, perf-only — NOT required by any wave-6 lane)" is
    TRUE for native, but the **web/WASM target is broken by the bump without the two fixes in this
    lane.** The engine change is not purely transparent to dc3-web.
  - The wave-6 feet work (wave6/a-knee-bend, CharIKFoot.cpp) introduced the duplicate
    `gDc3PollSeq` definition vs HamDirector.cpp — silently accepted by ELF (native + PPC) but
    fatal under wasm-ld. The original implementer did not run a WASM build to catch it.
- **Risks:**
  - The FrameTraceCounters strong defs in PipelineManager.cpp are DC3's authoritative defs (no
    Loader.cpp). If a future wave adds a DC3-side Loader.cpp that also defines them → duplicate
    symbol at that point. They're guarded by `#ifdef HX_WEB` so only the WASM target is affected.
    All init to 0/false → functionally dead in DC3 (gFrameTraceActive=false), no behavioral risk.
  - WebGPU boot is not fully verified (requestAdapter null in headless). Full rendering pipeline
    not exercised — a GPU-requiring assertion reachable only on real GPU could theoretically
    occur. Environment limitation, not a code defect.
  - The feet gate (FeetNotBelowFloorDuringGameplay, `DC3_GAMEPLAY_TESTS=1`) not verified here
    (out of Lane C scope; was Lane A wave-6).
- **Verdict required-fixes:** none.

### Lane D — funclet pairing reconciliation (Sonnet) — **PASS** (status: complete)

- **Branch:** `wave7/d-funclet-pairing` (1 commit: `843ad951`, worktree) +
  **objdiff fork** `wave7/funclet-pairing` (`e5987fb`) · **Worktree:**
  `/home/free/code/milohax/wt-wave7-d-funclet-pairing`
- **Files:** worktree — `scripts/analyze_funclets.py` (new, 271-line analysis script, **no C++
  source touched**); objdiff fork — `objdiff-core/src/diff/mod.rs` + snapshot
  `tests/snapshots/arch_ppc__diff_ppc-2.snap`.
- **Deliverable MET (all 3 acceptance items):** population counted (187), classified with
  evidence (A/B/C/D), and a working pairing improvement measured (+321 matched, −7 unpaired) plus
  orchestrator recommendations.
- **Unpaired funclet population (independently verified):** **187** functions at
  `match_percent_normalized==0.0` with `fn_<addr>` prefix in the shared `build/373307D9/report.json`.
  Doc 01/05's `~232-233` was **STALE** (written from an earlier report.json before prior waves
  landed pairings).
- **Classification (187, evidenced via COFF symbol-byte read):**
  - **Class A = 8** — `??__E`/`??__F` static-init/dtor, bytes 100% match, pairable (across 7
    units: math/Easing, obj/DataFile ×3, os/System, synth/tomcrypt/TomCryptLicense,
    world/CameraManager, zlib/ZlibLicense). **7 actually paired by the fix** (ZlibLicense
    excluded — no base_path in objdiff.json).
  - **Class B = 2** — large COMDAT in rndobj/Mesh (`fn_8263A168` 504B, `fn_8263A360` 556B), name
    lost in the XEX split, unpairable without dtk/jeff name recovery.
  - **Class C = 153** — XDK/binkxenon (53 units), non-authorable, expected.
  - **Class D = 24** — synth_xbox/jpeg/oggvorbis/etc. orphaned (no matching base symbol; their
    source doesn't yet produce the EH flow that generates these funclets).
- **Pairing fix (objdiff fork):** extended `is_funclet_like()` to return true for names starting
  with `??__E`/`??__F` so byte-signature pairing fires for global dynamic init/dtor thunks. Added
  5 positive tests (`??__Efoo`, `??__Fbar`, `??__E`, `??__F`, mangled) + 2 negative tests
  (`??__G`, `??__R`) to prevent over-matching.
- **Measured (Lane D, NEW worktree-local binary; shared binary untouched):**
  - `cargo test` (all objdiff crates): **156 passed, 0 failed** (incl. the 7 new tests + updated
    `diff_ppc-2.snap` where one funclet transitions None/Some(98.92) → Some(100.0)).
  - report delta with the new binary: **187 → 180 unpaired** (−7); **+321 total matched**. The
    +321 > +7 because 314 already-named `??__E`/`??__F` symbols on both sides were also previously
    blocked from byte-fallback scoring (all identical thunk patterns — semantically correct).
  - **NEW binary** at `wt-wave7-d-funclet-pairing/build/objdiff-target/release/objdiff-cli`
    (Jun 11 12:29, md5 a25a36a0) differs from the **SHARED binary** at
    `objdiff/target/release/objdiff-cli` (Jun 10 23:30, md5 821cc514) — **shared binary untouched,
    rule-compliant** (verifier-confirmed by md5 + mtime).
- **Out-of-lane finding (reported, not fixed — single-owner rule):** `fn_82EDDA80` (ZlibLicense,
  Class A bytes match) stays unpaired because objdiff.json has no `base_path` for that unit —
  `config/373307D9/link_order.txt` uses the `.cpp` extension as `ZlibLicense.c` (mismatch with
  `splits.txt`'s `.cpp`), so configure.py reports "Missing configuration for
  system/zlib/ZlibLicense.cpp". Verifier confirmed the exact mismatch. A ~5-minute extension fix
  would pair it (the 8th Class A).
- **Contradictions (Lane D corrected prior docs):**
  - **doc 01 / doc 05 `~233 unpaired` / `1536 fn_addr stubs ~233 stuck at 0%`** — ACTUAL is
    **187** (or **180** post-fix). The docs were written from an earlier report.json. The `233`
    figure should be updated to 187/180 in future references.
  - **decomp.db shows 233 fn_ NULL rows vs 187 in report.json** — the known DB-drift plane
    mismatch (decomp.db not updated by this lane, per the no-DB-writes rule).
- **Risks:**
  - ZlibLicense `fn_82EDDA80` remains unpaired despite 100% byte match — needs a separate
    link_order.txt / configure.py extension fix. Risk: low (cosmetic, 1 function).
  - The +321 (vs +7 directly paired) means 314 already-named `??__E`/`??__F` symbols now receive
    byte-fallback scoring. Semantically correct (identical thunk patterns) but the delta is large
    — orchestrator should sanity-check report.json after rebuild.
  - Class B (rndobj/Mesh) cannot pair without recovering the original COMDAT name from the XEX
    split (dtk/jeff-level). Genuine orphans in the denominator.
  - 24 Class D orphans (synth_xbox etc.) have no base .obj counterpart generating equivalent EH
    thunks — incompletely decompiled units; some may pair naturally as decompilation improves.
- **Verdict required-fixes:** none.

---

## Consolidated apply runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches. As in
prior waves, **NO lane wrote `decomp.db`** and **NO lane produced new unicorn evidence**. The
source effects break down as: **Lane A** moves PPC percents (28 new 100% functions in
synth_xbox/oggvorbis, ICF-folded to 14 reps) → needs a real `sync`; **Lane B** moves PPC
percents (5 qualifying wins incl. 2 new 100% in rndobj/Bitmap + os/File) → needs a real `sync`;
**Lane C** is HX_WEB / HX_NATIVE / engine-pin only (no PPC movement) + the objdiff `??__E`/`??__F`
pairing (if the fork is merged) changes report.json funclet scores; **Lane D** is analysis-script
+ objdiff-fork only. Run from repo root on `main` **after** merging (no conflict to resolve).

```bash
# 0. Merge the four worktree branches (see merge-order section). NO conflict this wave —
#    all six pairs merge cleanly; no file is shared across lanes.

# 1. (Lane D) Merge the objdiff fork + rebuild the SHARED binary. The lane changed the fork
#    (??__E/??__F funclet pairing) but did NOT touch the shared binary — that is an
#    orchestrator step. Fork branch wave7/funclet-pairing is at e5987fb.
cd /home/free/code/milohax/objdiff && git merge --ff-only wave7/funclet-pairing   # or merge commit
cargo build --release --package objdiff-cli                                       # rebuilds target/release/objdiff-cli
cd /home/free/code/milohax/dc3-decomp

# 2. (Lane C) The engine pin is bumped to 8fb669d on the wave7/c branch (native/CMakeLists.txt).
#    Once merged it is live. Re-verify the native green gate after merge:
cmake --build native/build --target milo-tests -- -j$(nproc)                       # expect 324 PASS / 0 FAIL / EXIT 0
bash scripts/check_native_compiles.sh                                              # pre-merge native gate (Lane A VorbisMem guard)

# 3. Regenerate report.json + repopulate current_percent/match_percent_normalized for the
#    Lane A og-port wins (FxSend delegations, VorbisMem OggMalloc/etc, RemoveOwnerVoice) AND
#    the Lane B grind wins (op49/op61 100, ManageJob 100, PixelColor 100, FileGetPathBuf 100
#    normalized). Run AFTER the objdiff fork+binary rebuild so funclet pairing is reflected.
python3 scripts/sync_match_percent.py --build --promote

# 4. Clear residual db-only stale stubs (harmless if already clean).
python3 scripts/reconcile_db.py --fix

# 5. (RECOMMENDED, orchestrator-run) Fresh unicorn refresh on main. Dry-run FIRST, inspect
#    any new candidate_bug flips before applying. Lane B's certified floors (DeleteBones,
#    ConvertBonesToTranses, MemHeap::Free, RndMesh::Replace, CalcShaderOpts, RndLightAnim::Copy,
#    BuildObjectLists, ClearSnapshots, Smooth) should stay DIVERGENT / honest — record their
#    certify_floor evidence strings (Lane B report), do NOT route them to candidate_bug.
bash scripts/nightly_measurement_guard.sh --unicorn                 # dry-run (temp DB copy)
bash scripts/nightly_measurement_guard.sh --unicorn --unicorn-apply # live writes (after adjudication)

# 6. Confirm + record.
python3 scripts/reconcile_db.py            # expect check (e) drift = 0
python3 scripts/certify_floor.py --summary # record the new open count after the og-port + grind wins land
```

**Notes / lane-specific:**
- **objdiff fork merge + shared-binary rebuild is REQUIRED before step 3's sync** if you want the
  +321 funclet matches reflected in report.json. The lane's `+321 / -7` delta was measured with
  the worktree-local binary; the shared binary at `objdiff/target/release/objdiff-cli` is still
  the Jun 10 build. **Sanity-check report.json after the rebuild** (the +321 is large — 314 of it
  is already-named `??__E`/`??__F` thunks now receiving byte-fallback scores).
- **Engine pin state:** main is still `f75339a`; the bump to `8fb669d` lands ONLY when
  `wave7/c-web-verify` merges. The pin is SOFT (warns, never fails). After merge, the native
  build uses the new engine — re-run milo-tests (expect 324/0/EXIT 0) and
  `check_native_compiles.sh`.
- **Lane A native gate is load-bearing:** `VorbisMem.cpp`'s `#ifndef HX_NATIVE` guard must hold
  (empty TU under HX_NATIVE; native OggMalloc/etc come from `native_link_glue.cpp`). Run
  `check_native_compiles.sh` once on main as the pre-merge native gate.
- **`fn_82EE0B00` (Lane A sLicense init) and `fn_82EDDA80` (Lane D ZlibLicense)** may persist as
  0% in report.json post-merge despite verified 100% byte matches (jeff initializer-pairing /
  missing base_path) — do NOT treat either as a regression. The ZlibLicense base_path
  (link_order.txt `.c`→`.cpp` extension) is a separate ~5-minute orchestrator follow-up.
- **No `apply_refresh.py --apply` of a worktree results DB is required** — no lane handed off a
  frontier results DB.
- **FileGetPathBuf stays 99.5% RAW / 100% NORMALIZED** post-sync (kAssertStr "File.cpp" reloc
  noise) — normalized is canonical; do not flag it as incomplete.

---

## Merge order for `wave7/*` branches (with cross-lane conflict check)

`git diff --name-only f5e3e3d1..wave7/<lane>` and pairwise `git merge-tree --write-tree` were run
from main HEAD `f5e3e3d1` (all four branches descend from it). **There is NO git conflict this
wave** — the single-owner rule held.

### Conflict check — ALL CLEAN

Pairwise `git merge-tree --write-tree` (rc=0, no conflict markers):

| Pair | Result |
|---|---|
| `a-og-port` × `b-grind-2` | **CLEAN** (rc=0) |
| `a-og-port` × `c-web-verify` | **CLEAN** (rc=0) |
| `a-og-port` × `d-funclet-pairing` | **CLEAN** (rc=0) |
| `b-grind-2` × `c-web-verify` | **CLEAN** (rc=0) |
| `b-grind-2` × `d-funclet-pairing` | **CLEAN** (rc=0) |
| `c-web-verify` × `d-funclet-pairing` | **CLEAN** (rc=0) |

**No file is touched by more than one lane** (`sort | uniq -d` of all four changed-file sets is
empty). Lane A's own apply-step worried about a possible `os/File.cpp` collision with Lane B — it
does NOT exist: Lane A touches `synth_xbox/` + `oggvorbis/VorbisMem.cpp` + `src/link_glue.cpp`;
only Lane B touches `os/File.cpp`. Lane C owns `char/CharIKFoot.cpp` (+ `native/CMakeLists.txt`,
`native/src/gfx/PipelineManager.cpp`); no other lane touches those.

### Cross-lane file map (no file multiply-touched)

| File(s) | Lane | Conflict? |
|---|---|---|
| `src/system/synth_xbox/FxSend*.{cpp,h}` (12), `synth_xbox/soundtouch/.../FIFOSample*` (3), `oggvorbis/VorbisMem.cpp`, `src/link_glue.cpp` | A | No |
| `src/system/{synth/ByteGrinder,net/DingoSvr,gesture/LiveCameraInput,math/DoubleExponentialSmoother,rndobj/Bitmap,os/File}.cpp` (6) | B | No |
| `native/CMakeLists.txt`, `native/src/gfx/PipelineManager.cpp`, `src/system/char/CharIKFoot.cpp` (3) | C | No |
| `scripts/analyze_funclets.py` (worktree) + objdiff fork `objdiff-core/src/diff/mod.rs` + snapshot | D | No |

- **Only Lane C touches `native/CMakeLists.txt`** (the engine-pin bump). No other lane touches it.
- **Lane D's objdiff-fork changes are in a SEPARATE repo** (`/home/free/code/milohax/objdiff`),
  not the dc3-decomp tree — they cannot conflict with A/B/C dc3-decomp changes.

### Recommended order

1. **`wave7/d-funclet-pairing`** (`843ad951`) — merge first into dc3-decomp (adds only
   `scripts/analyze_funclets.py`; disjoint, no build impact). Then merge the objdiff fork branch
   `wave7/funclet-pairing` (`e5987fb`) into objdiff main and rebuild the shared
   `objdiff/target/release/objdiff-cli` (apply runbook step 1) — do this BEFORE any
   report.json regeneration so funclet pairing is reflected.
2. **`wave7/c-web-verify`** (`f6117baa`) — merge second. Edits are HX_WEB / HX_NATIVE / engine-pin
   only → PPC report.json byte-identical. Rebuild native milo-tests (expect 324/0/EXIT 0) + run
   `check_native_compiles.sh`. Engine pin goes live (`8fb669d`).
3. **`wave7/a-og-port`** (`8e1ac429`) — merge third (moves PPC percents). `VorbisMem.cpp` is
   native-compiled (empty TU under HX_NATIVE) — run `check_native_compiles.sh` as the gate. Full
   `ninja` regenerates report.json with the 14 FxSend/VorbisMem/RemoveOwnerVoice representatives.
4. **`wave7/b-grind-2`** (`c0f507a8`) — merge last (also moves PPC percents). Full `ninja`
   regenerates report.json with the 5 qualifying wins (op49/op61 100, ManageJob 100, PixelColor
   100, FileGetPathBuf 100 normalized) + 2 material (ClearSnapshots 99.9, Smooth 94.8).

After all four (+ the objdiff fork merge/rebuild): run the rest of the DB apply runbook
(`sync_match_percent.py --build --promote`, reconcile, optional unicorn refresh, certify-floor
summary). The order of A vs B is interchangeable (disjoint files); both must precede the sync.

---

## What blocks merging

- **NO MERGE BLOCKER.** All six lane pairs merge cleanly; no file is shared across lanes; no
  `decomp.db` write; main HEAD unchanged (`f5e3e3d1`); engine pin on main unchanged (`f75339a`,
  bump is branch-only); shared objdiff binary untouched; no `Co-Authored-By`; no `git stash`.
- **ACCEPTANCE GAP to record (not a hard blocker, orchestrator decision):** **Lane A delivered 28
  net-new source functions / 14 report.json representatives vs the ≥40 target.** All 28 are
  run_objdiff-verified 100% on the worktree PPC plane; the shortfall is **structural** — ICF
  folds the byte-identical FxSend delegations to one representative each, and every remaining
  native-safe candidate was re-verified as a header-divergence structural port (XAPO/dsp tree).
  **Recommend accept-partial-with-rationale** OR re-scope `≥40` to a future coordinated structural
  lane (XAUDIO2→xapobase + dsp/StandardEffect migration + FxSend360 named-member reconciliation +
  jeff FxSendPitchShift/Synapse partition fix). The verifier's verdict is `pass` on correctness
  and rule-compliance with the shortfall acknowledged.
- **ORCHESTRATOR STEPS REQUIRED before report.json reflects Lane D's gain (not a blocker, but
  sequencing matters):** the objdiff fork merge + shared-binary rebuild must happen BEFORE
  `sync_match_percent.py`, or the +321 funclet matches won't appear. The shared binary is still
  the Jun-10 build; Lane D's delta was measured with a worktree-local binary. Sanity-check
  report.json after the rebuild (the +321 includes 314 already-named `??__E`/`??__F` thunks).
- **ENGINE-PIN STATE to flag (not a blocker):** merging Lane C bumps `MILO_ENGINE_PIN` from
  `f75339a` → `8fb669d` on main. Soft pin (warns, never fails); native gate held at 324/0 with the
  new pin. Without Lane C's two WASM link-break fixes, the bump breaks the dc3-web build — so do
  NOT bump the pin independently of Lane C.
- **OUT-OF-LANE FINDINGS surfaced (reported, route to owners / orchestrator — none block merge):**
  (a) **ZlibLicense base_path gap** (Lane D) — `link_order.txt` references `ZlibLicense.c` (`.c`
  not `.cpp`), so configure.py skips it and `fn_82EDDA80` stays unpaired despite 100% bytes;
  ~5-minute extension fix would add the 8th Class A pairing. (b) **jeff FxSendPitchShift360/
  Synapse360 source-partition mismatch** (Lane A) — tracked unit maps to an empty `.cpp` while the
  class body lives in an untracked `360.cpp` obj; blocks those `SyncEffectParams`/`CreateFx` ports
  (a jeff config change, not a graft). (c) **2 Class B rndobj/Mesh COMDAT funclets** (Lane D)
  unpairable without dtk/jeff name recovery from the XEX split.

---

## Open follow-ups for Wave 8

1. **The structural og-port lane (the real path past Lane A's 28).** Lane A maxed the
   behavior-neutral clean-graft slice (FxSend delegations + VorbisMem + RemoveOwnerVoice). The
   `≥40` figure needs a coordinated **structural** lane: og xdk/XAUDIO2→xapobase + dsp/
   StandardEffect + per-effect dsp/*Effect.h migration enabling the `SyncEffectParams`/`CreateFx`
   bodies; FxSend360 named-member reconciliation (`unk8/unk20/unk30 → mVoices/mFx`) for the base
   functions; and the jeff FxSendPitchShift360/Synapse360 source-partition fix. All gated on the
   native build — not a neutral graft pass.
2. **Continue the open-residual grind.** Lane B took the next band to 5 qualifying + 2 material;
   the remainder is floor-dominated (DeleteBones, ConvertBonesToTranses, MemHeap::Free,
   RndMesh::Replace, CalcShaderOpts, RndLightAnim::Copy, BuildObjectLists, OnSync, TransformNormal,
   ScaleAdd all confirmed floors this wave). Record certify_floor evidence for them (Lane B report
   has the strings) — DB still not written. The reachable wins in this band are the cached-load /
   redundant-control-flow class, NOT the regswap/FPR/scheduling cascades.
3. **Apply the engine-pin bump on main + re-baseline the web target as a standing gate.** Lane C
   proved the six-wave native fix-train builds + boots on WASM for the first time, but only with
   two fixes for the `8fb669d` bump. Wire a web release+debug build (+ headless boot to
   `game_screen`) into the wave gate so future engine bumps / wave-N native changes can't silently
   break dc3-web (the `gDc3PollSeq` duplicate-symbol bug shipped through six waves undetected).
   Run the web build with real-GPU coverage where possible (headless requestAdapter returns null).
4. **Merge the objdiff `??__E`/`??__F` funclet pairing fork + rebuild the shared binary**, then
   regenerate report.json to bank the +321 matched (−7 unpaired). After that the real remaining
   unpaired count is **180** (153 Class C XDK non-authorable + 2 Class B rndobj/Mesh + ~24/25
   Class D orphans + 1 ZlibLicense pending base_path) — update doc 01/05's stale `~232-233` figure
   to 180.
5. **Fix the ZlibLicense base_path gap** (Lane D out-of-lane): `config/373307D9/link_order.txt`
   uses `ZlibLicense.c` (`.c`) while `splits.txt` uses `.cpp`; configure.py skips the unit so it
   has no `base_path` in objdiff.json. ~5-minute extension fix pairs `fn_82EDDA80` (the 8th Class A,
   100% bytes).
6. **Decomp.db drift cleanup for the two stale planes surfaced this wave:** (a) decomp.db shows
   233 fn_ NULL rows vs report.json's 187 (Lane D — DB-drift plane mismatch); (b) Lane B's two new
   wins (PixelColor, FileGetPathBuf) carry stale "auto: all mismatches unfixable" labels that the
   sync + recert should clear. Both resolve on main after `sync_match_percent.py --build --promote`
   + `reconcile_db.py --fix`.
7. **Census-doc hygiene:** update doc 01/05's `~232-233` unpaired-funclet figure to 187 (180
   post-fix); note the FrameTrace counter defs the `8fb669d` bump requires for the WASM target
   (dc3 has no Loader.cpp); and record that the og-port native-safe half is ~28 source functions
   (not doc-09's ~186, not the original wave-7 report's ~5).
