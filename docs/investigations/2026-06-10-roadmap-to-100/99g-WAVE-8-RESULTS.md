# 99g — Execution Wave 8 Results

**Date:** 2026-06-11. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`99f-EXECUTION-WAVE-8.md`](99f-EXECUTION-WAVE-8.md). **Wave-7 results:**
[`99e-WAVE-7-RESULTS.md`](99e-WAVE-7-RESULTS.md). **Scope:** the four open bands after
wave 7 — the `<70%` logic-gap attack (A), the `70–90%` archaeology continuation (B), the
og Phase-2.2 structural/hybrid ports (C), and the `90–100%` permuter sweep (D).

All four lanes ran in isolated worktrees off main `2b4e6cb0` (the wave-8 plan doc).
**Two lanes (B, C) carry committed source wins and pass; Lane A is an empty-branch
PASS (structural no-op, premise refuted); Lane D carries 2 clean wins but the verifier
returned `fail` on two non-source process gaps (missing sweep stats + missing
`permuter_exhausted` evidence strings) — neither blocks merge.** **No lane committed to
`main`** and **no lane wrote `decomp.db`** — main HEAD is still `2b4e6cb0` and main
`decomp.db` is untouched (mtime `2026-06-11 13:33`, no WAL/SHM). Branches are staged for
the orchestrator to merge and apply.

> **Build-plane rule (still enforced):** every match-percent and verdict number below
> names its build plane. Worktree `run_objdiff` readings are *claims*; final certification
> happens on `main` after the sync. A worktree reading is not evidence about main. The
> normalized number is canonical (relocation-noise removed); raw and `fuzzy_match_percent`
> diverge from it and several discrepancies below trace to which metric was quoted.

> **✅ NO GIT MERGE BLOCKER THIS WAVE.** Pairwise `git merge-tree --write-tree` from main
> `2b4e6cb0`: **all six lane pairs are CLEAN, no conflict markers.** `sort | uniq -d` of
> all four changed-file sets is **empty** — no file is touched by more than one lane. The
> four lanes are path-disjoint by directory: **A** = ∅ (empty branch), **B** =
> `gesture/` + `net/`, **C** = `synth/` + `synth_xbox/`, **D** = `rndobj/`. The one
> shared-header risk this wave (Lane C's additive `synth/Mic.h` enum, included by ~6
> native/web TUs) was cleared by Lane C's own native + web gates and is not touched by any
> other lane.

---

## TL;DR headline numbers

| Metric | Value | Build plane / notes |
|---|---|---|
| **Lane A — `<70` band logic gaps** | **0 functions improved / 0 zero-starts written** — premise REFUTED, empty branch | Lane A worktree `wave8/a-logic-gaps`, dc3 Xbox/PPC plane, run_objdiff normalized |
| **Lane A shortfall is structural, not effort** | The `<70` band is floor/artifact-dominated exactly as doc-08 §F4 predicted ("zero logic-class DIVERGENT fns on the partial frontier"). 27/30 "zero-starts" are decomp.db DRIFT (link_glue ObjPtrList template instantiations already removed — not pairable in report.json); the 3 real ones are REVERSE-artifacts (target lacks the symbol). 12+ fns hand-investigated, 6 permuter-exhausted, 0 source-fixable. | Lane A; verifier `pass` — the 0/10 + 0/20 is a *correct outcome*, the lane spec mis-scoped the band |
| **Lane B — `70–90` archaeology** | **2 qualifying wins, BOTH to 100%** — SHORT of the ≥6 target | Lane B worktree `wave8/b-archaeology-3`, run_objdiff normalized, re-verified by verifier |
| **Lane B wins** | `XLSPConnection::Poll` 86.1→**100.0%** (MSVC out-of-line block layout via if/else fall-through + identical-block tail-merge); `DirectionGestureFilterDoubleUser::Update` 78.3→**100.0%** (REAL behavioral bug — missing `-1` skeleton-index guard before `GetSkeleton`, which asserts `0≤idx<6`; og-dc3 has the same bug, so it had to be reverse-engineered) | Lane B; both 100% confirmed on the final full-build state |
| **Lane B shortfall is the same floor cap as waves 6/7** | ~12 further candidates probed with full archaeology; 3 permuter sweeps (AcquireFontMap 980-file/217s, EvaluateChannel) returned **0** improvements. Residue = FMA-scheduling / FPR-spill / callee-saved-register-choice / float-constant-table / register-pressure floors. Recommend accept-partial-with-rationale (wave-6 precedent: 5/8 accepted). | Lane B; per-callsite hacks prohibited (`feedback_no_hacks`) |
| **Lane C — og Phase-2.2 hybrid port** | **16 functions 0→100% + 3 material partials, 0 regressions tree-wide (48,416 fns)** — EXCEEDS the ≥15 net-new target | Lane C worktree `wave8/c-og-hybrid`, dc3 Xbox/PPC plane + report.json + native + web gates |
| **Lane C — the only real og-portable gap was synth_xbox/Mic** | VoiceControlPanel / ShellInput / HamIconMan / MicClientMapper / MicNull baselined as ALREADY ~fully matched (their `is_stub=1`/`0%` db rows are STALE report.json pairing artifacts — atexit dtors). ExternalMic's 49 stubs + Mic's big fns (Poll/OnDataReady/ProcessChatData/AddData/Init) are NOT implemented in og either → not portable. Real worklist = synth_xbox/Mic only, ~18 fns, 16 reached 100%. | Lane C; corrects the wave-7 "header-divergence-blocked" worklist |
| **Lane C — hybrid header reconciliation (evidence-backed, not blind-adopt)** | (1) `Mic::Type` enum +kDisconnected=0/kHeadset=1/kUSBMic=2 (kMicNull=2 preserved) — verified vs target `GetType` asm + RB3 semantics. (2) config block SWITCHED struct→5 separate statics — proven by target asm (`SetLowCut` stores to standalone `lbl_82F474D0`, not base+8); our ctor at 98.9% still BEATS og's 93.2%. (3) `ExternalMicClientMgr` consolidated into `ExternalMic.h`. (4) `MicManagerXbox::unk1c` int→`IXHV2Engine*` (same 4 bytes @0x1c) for `Shutdown`'s `Release()` vcall. | Lane C; the synthesis of struct (ctor) + statics (Set* helpers) was the right answer, not either alone |
| **Lane D — `90–100` permuter sweep** | **2 clean, run_objdiff-verified wins committed** (after repair round dropped a false win) | Lane D worktree `wave8/d-permuter-sweep`, run_objdiff normalized |
| **Lane D wins** | `LinearizeKeys` (rndobj/Utl) 99.96→**100.0%** (collapsed `if(f2){if(size>2)}`→`if(f2&&size>2)` ×3); `SyncPristineCtrlPoints` (rndobj/Spline) → **94.3%** (inlined `mCtrlPoints.size()` in loop init + `i==size-1`). Verifier found the SyncPristineCtrlPoints baseline is **92.24%** (per main db), so the real delta is **+2.06%**, not the commit's claimed +0.7% (stale permuter internal baseline). | Lane D; both wins genuine |
| **Lane D — false win dropped (repair round)** | `CheckBSPTree` (math/Geo.cpp) reorder committed 99.0→99.0% = **0.0% measurable** → DROPPED entirely (would have corrupted `sync_match_percent`). Residual = callee-saved FPR swap floor (f25↔f27 ×17, f26↔f28 ×16, f30↔f31 ×12 = 66 swap instrs, 0 structural). `MetaPerformer.cpp` `_cond` extraction reverted. | Lane D; the verifier confirmed the rejection is correct |
| **Lane D — verifier `fail` is process-only, not correctness** | Both committed wins verified. The `fail` is: (a) NO sweep stats (attempted/improved/curated-out) reported — plan requires it; (b) NO `permuter_exhausted` evidence strings written for exhausted floors (CheckBSPTree et al.) — plan requires it. Both are orchestrator-completable follow-ups, NOT merge blockers. Minor: `Z:tmpclaudebsf_*.obj` scratch left in worktree root. | Lane D; commit-message baseline cosmetic correctness also flagged |

**Wins-per-band roll-up:** `<70`: **0** (A, refuted). `70–90`: **2** to 100% (B). og-port:
**16** 0→100% + 3 partials (C). `90–100`: **2** curated permuter wins (D). **Total net-new
material: 20 functions** (16 og-port + 2 archaeology + 2 permuter), all run_objdiff-verified
on their worktree planes.

**No git merge blocker.** All disjoint, all six pairs clean. The acceptance gaps are: Lane A
(0 vs ≥10 — *correct refutation*, accept), Lane B (2 vs ≥6 — floor cap, accept-partial),
Lane D (verifier `fail` on missing sweep-stats + floor-cert strings — orchestrator finishes
these post-merge).

---

## Per-lane outcomes

### Lane A — the `<70` band: logic-gap attack (Opus) — **PASS (empty branch; premise refuted)** (status: blocked)

- **Branch:** `wave8/a-logic-gaps` — **0 commits, == main `2b4e6cb0`** (verified empty:
  `git diff --name-only 2b4e6cb0 wave8/a-logic-gaps` returns nothing). **Worktree:**
  `/home/free/code/milohax/wt-wave8-a-logic-gaps` — pristine, `git diff --stat HEAD` empty.
- **Outcome: 0 functions materially improved (target ≥10), 0 zero-starts written (target
  ≥20).** This is a *correct* outcome, not an agent failure — the lane's core premise that
  "the `<70` band = real missing/wrong logic, not lowering noise" is **CONTRADICTED** by the
  measured population, exactly confirming doc-08 §F4 (`unicorn_class=logic → 0 rows on the
  partial frontier`).
- **Population decomposition (decomp.db read-only `?mode=ro`, cross-checked vs report.json):**
  - **27/30 "zero-starts" are decomp.db DRIFT** — `link_glue` `ObjPtrList<T>::Owner/RefOwner`
    template instantiations the link_glue mechanism already removed; **not present as pairable
    symbols in report.json** (so not writable, nothing to match TO).
  - **3/30 that ARE in report (`EaseLinear`, `Flush@HDCache`, `~MemDoTempAllocations`) are
    REVERSE-artifacts** — the target binary has 0 bytes / no such symbol while OUR build emits
    an extra one. Unpairable, not writable.
  - Several `<70` rows are **db unit-attribution drift**: `Matrix3 Multiply` is attributed to
    `char/CharLookAt` (28.9%/0%) but is defined in `math/mtx.cpp` — building via CharLookAt.obj
    shows it as a 170-instr STUB. ThreeDSoundManager `Replace`, LightPreset `FindRef`, CharEyes
    `_Param_Construct`, `PackSongListProvider`, VirtualKeyboard `Terminate` all **NOTINREP**.
  - The named floor units (rndobj/Shader `CalcShaderOpts`, synth_xbox/Synth, FFT, Mic) were
    skipped per the warning list.
  - The genuine authored residue is deep register/FPR/stack/data-layout/codegen-thunk floors.
- **Measured (Lane A worktree plane, run_objdiff normalized; 12+ fns investigated, 6
  permuter-exhausted):** `ScaleAddEq@rnddx9/Mesh` 60.4% (frame Δ −0x10 + r3/r31 volatile
  cascade); `HttpGet` ctor 63.2% (bool-mask r10/r11 + String-ctor reorder); `CompressThread`
  30.4% (single-base-register sharing of contiguous BSS globals — build-env, not
  source-reorderable); `HolmesSetFileShare` 49.4% (`gShareName−0x40 = gMachineName` adjacent-
  global base sharing — data layout); `??_GCharDriverMidi` 58.4% (compiler-generated
  deleting-dtor inlines `~CharDriverMidi` vs our out-of-line call — codegen floor); `operator*`
  (Lit_NG Transform×Matrix4) 58.5% (wants Col3 sret read via return ptr). **All experiments
  reverted via Edit; final PPC ninja EXIT 0.**
- **Contradictions (Lane A corrected the plan + db):**
  - **Plan 99f Lane A premise CONTRADICTED:** "`<70` band = real missing/wrong logic" is FALSE
    for the measured population — it is floor/artifact-dominated (doc-08 §F4 confirmed).
  - **Plan 99f "30 zero-starts are quick kills — write them all" UNACHIEVABLE:** 27/30 are db
    DRIFT, 3/3 are reverse-artifacts. **Recommend re-deriving zero-start lists from report.json,
    not decomp.db, and dropping "not-in-base" rows.**
  - **decomp.db unit attribution is STALE for math/template symbols** (Matrix3 Multiply →
    CharLookAt), inflating the apparent `<70` logic backlog.
  - **Band-count discrepancy:** plan says 58 fns `<70`; Lane A's stricter authorable filter
    returned 47 (~11-fn gap). Verifier confirmed the db query with the *plan's* filters returns
    exactly 58; the 11-fn delta is the stricter authorable-units filter Lane A applied. Does not
    affect findings.
- **Risks:** ZERO source risk — empty branch, all do-not-break gates trivially intact. The
  acceptance shortfall is structural; **recommend the planner re-scope Lane A** for future waves
  to "floor census + cert sweep," not "logic archaeology." The real authorable work lives in
  the `90–100` (Lane D) and `70–90` (Lane B) bands.
- **Verdict (verifier `pass`):** no required fixes — branch is empty, nothing to merge. Verifier
  independently re-measured all 5 key floor percents (ScaleAddEq 60.4, HttpGet 63.2,
  CompressThread 30.4, CharDriverMidi 58.4, HolmesSetFileShare 49.4) and the 27/3 zero-start
  split, confirming all claims.

### Lane B — the `70–90` band: archaeology continuation (Opus) — **PASS (correctness) / SHORTFALL (target)** (status: partial, repaired)

- **Branch:** `wave8/b-archaeology-3` (3 commits: `c7099e89` + `faaf7a35` XLSP repair,
  `e7c82025` gesture) · **Worktree:** `/home/free/code/milohax/wt-wave8-b-archaeology-3`
- **Files (2):** `src/system/net/XLSPConnection.cpp`, `src/system/gesture/DirectionGestureFilter.cpp`.
- **Repair-round deliverable:** the original commit left `XLSPConnection::Poll` at 91.0%
  (sub-qualifying, +4.9 from the 86.1% baseline — the verifier flagged this as below the
  +10pts-or-100% bar). The repair pushed it to **100%** and added a second qualifying win.
- **2 qualifying wins (verifier re-measured via run_objdiff, worktree plane, final full-build):**
  - **`XLSPConnection::Poll`** 86.1→**100.0% normalized** (99.4% raw, 121/121 equal). Lever: the
    case 2/3 `XNetGetConnectStatus` status branches lay single-block `return` bodies out-of-line
    and tail-merge the two identical "unhandled return" `MILO_NOTIFY` blocks. Restructuring the
    if/else fall-through (group the `>=` tests, push single-statement return bodies into the cold
    path) reproduces the exact layout. **Behavior-neutral** if/else reshaping over the unsigned
    DWORD status (each case proven equivalent by hand). No HX_NATIVE block touched; pure Xbox/PPC
    net code.
  - **`DirectionGestureFilterDoubleUser::Update`** 78.3→**100.0% normalized** (99.6% raw, 56/56
    equal). **REAL behavioral bug:** `GetValidSkeletons` can return `-1` (no valid skeleton) but
    `GestureMgr::GetSkeleton(idx)` asserts `0≤idx<6`. The target binary guards each index with
    `(i == -1 ? 0 : i)` computed into locals before the two `GetSkeleton` calls. **og-dc3 has the
    same bug** (no guard) → not a reliable upstream-port source; the correct form was
    reverse-engineered from the target's masking idiom. No HX_NATIVE guard; shared engine code;
    **removes a latent native assert** (native uses the same `GestureMgr::GetSkeleton(int)`).
- **Floors confirmed this round (diagnosis + permuter 0-improvement, evidence for certify_floor —
  all run_objdiff-baselined fresh, NOT from stale db):** `RndDrawable::CollidePlane` 82.4%
  (per-callsite FMA scheduling — fmadds vs separate fmuls+fadds; shared-header Dot reorder
  rejected); `RndText::AcquireFontMap` 83.2% (font→r27 vs target r29 consistent swap, **permuter
  0/980-file scan**); `CharBonesSamples::EvaluateChannel` 84.1% (r31↔r30 28-instance callee-saved
  cascade + extsh ordering, **permuter 0**); `NgPostProc::NgPostProc` 79.3% (callee-saved f31
  across the 2nd RandomFloat); `PatchVerts::HasVert` 80.7% (callee-saved r30/r31 vs volatile);
  `TaskMgr::ResetTaskTime` 72.8% (callee-save-vs-volatile r31/f31); `CacheResource` 71.0%
  (block-layout + frame Δ +0x40 + 21-instr EH/String-dtor delta); `ResetNormals` 77.9% (519
  instr, 71 regswap pairs + 25 clusters); `DrawDetectedBar` 83.4% (Color literals from global
  const tables vs stack-materialized) + ~10 more.
- **Contradictions (Lane B):**
  - **Implementation self-label corrected:** the original XLSP win (+4.9 → 91.0%) was labeled
    "qualifying" but was sub-qualifying. RESOLVED — pushed to 100%.
  - **decomp.db `current_percent` is STALE/mislabeled** for many `70–90` rows ("reset: was false
    COMPLETE from base_size=0 objdiff bug" / "demoted: stale COMPLETE"). Fresh run_objdiff
    differs from db (DrawShowing 83.4→82.9, DrawPreClear 83.1→80.4, CacheResource 71.4→71.0).
    **Always re-baseline with run_objdiff** (held this round; verifier cross-checked 8 candidates,
    all within db-drift margin).
  - The two `ObjPtrVec<...>::erase` rows the db lists at 82.5% (LightPreset, Font) measure as
    69-instr STUBs (report.json pairing artifacts), not workable functions.
- **Risks:**
  - **Acceptance shortfall persists: 2 qualifying vs ≥6.** Same structural cap waves 6/7 lane B
    hit. The clean control-flow/block-layout/logic-gap class was exhausted (the 2 wins came from
    it); the ~12 other candidates are FMA/FPR/callee-saved/float-const/register-pressure floors
    (3 permuter sweeps returned 0). Reaching 6 would require prohibited forced edits.
    **Recommend accept-partial-with-rationale.**
  - Both wins low-risk: XLSP is behavior-neutral over an unsigned DWORD; the gesture fix is
    behavior-*correcting* (guards a `-1` index). No shared headers touched (the Plane::Dot reorder
    in Mtx.h was tested then **reverted** — it didn't help and would risk every Dot caller). No
    native code, no web rebuild needed. milo-tests / feet / boot path unaffected.
- **Verdict (verifier `fail` on quantity, pass on correctness):** "a pass on correctness, a fail
  on quantity." Both wins genuine and behavior-correct; the blocking issue is the 2-vs-6
  shortfall → **accept as partial with explicit rationale** (wave-6 precedent) OR find 4 more.
  No code fixes required.

### Lane C — og Phase-2.2 structural/hybrid port (Opus) — **PASS** (status: complete)

- **Branch:** `wave8/c-og-hybrid` (2 commits: `65fa36ba` port + `493c0325` StartPlayback) ·
  **Worktree:** `/home/free/code/milohax/wt-wave8-c-og-hybrid`
- **Files (5):** `src/system/synth_xbox/Mic.cpp`, `src/system/synth/Mic.h`,
  `src/system/synth_xbox/Mic.h`, `src/system/synth_xbox/ExternalMic.h`,
  `src/system/synth_xbox/Synth.cpp`.
- **Deliverable EXCEEDED (≥15 net-new):** **16 functions 0→100%** + **3 material partials**,
  **0 regressions** across the entire 48,416-function tree.
- **Worklist derivation:** of the lane-named files (Mic, VoiceControlPanel, ShellInput, char/*),
  **only synth_xbox/Mic held a real structural gap.** Our Mic.cpp was an earlier partial
  native-adapted decomp; og-dc3 has the fuller Xbox implementation. Method = the memory-proven
  hybrid procedure (diff vs og FIRST; reconcile headers deliberately against target asm; graft og
  bodies adapted to OUR member names; keep every matched fn byte-identical).
- **Measured (Lane C worktree plane, run_objdiff normalized):** SetGain / GetType / GetName /
  GetRecentBuf / GetContinuousBuf / StopPlayback / ChatReceiver ctor+dtor / AddMic / RemoveMic /
  GetInstance / Shutdown / SetLowCut / SetLocalGain / SetRemoteGain = **100%**; **StartPlayback
  97.1%**; **MicManagerXbox ctor 98.9%** (beats og's 93.2%); **SetNoiseGate 99.6% fuzzy** (see
  metric note below). synth_xbox/Mic functions at ≥99.5% went **46 → 62** (of 84); unit
  `system/synth_xbox` +1.54% (49.44 → 50.99%).
- **Header reconciliation (all evidence-backed, NOT blind-adopt):**
  1. `Mic::Type` enum gains kDisconnected=0/kHeadset=1/kUSBMic=2 (kMicNull=2 preserved) — verified
     vs target `GetType` asm (`li 0x2` connected / `0` disconnected) + RB3 `IsConnected =
     GetType()!=0`. **Additive.**
  2. Config block SWITCHED from one `HeadsetConfig` struct → 5 separate statics like og — proven
     by target asm: `SetLowCut` stores to standalone `lbl_82F474D0` (not `lbl_82F474C8+8`) while
     the ctor uses base+offset; both fall out of the separate-static model. Our struct lowering
     capped Set* at 94%; statics took them to 100%. The synthesis (struct-shaped ctor + separate
     statics for Set* helpers) was the right answer, not either alone.
  3. `ExternalMicClientMgr` (Associate + ConnectedForClient) consolidated from a duplicate
     fragment in Synth.cpp into `ExternalMic.h` — behavior-neutral, synth_xbox/Synth 0 regr.
  4. `MicManagerXbox::unk1c` retyped int→`IXHV2Engine*` (same 4 bytes @0x1c) for `Shutdown`'s
     `Release()` vcall (verified vs target Shutdown asm, vtable slot 1).
- **Gates ALL GREEN (verifier-reproduced):** PPC `ninja` EXIT 0; `check_native_compiles.sh`
  PASS (1702/1702 linked); **milo-tests 326 passed / 0 failed / EXIT 0** (skips are GPU/asset-
  gated); `scripts/web/build.sh --release` `[100%] Built target dc3-web`, 0 errors. **0
  HX_NATIVE/HX_WEB guards dropped** (none existed in the Xbox-only files; verifier grepped all 5).
- **Contradictions (Lane C corrected prior docs):**
  - **99e Lane A "Mic/VoiceControlPanel/ShellInput/char header-divergence-blocked, need
    structural ports" CORRECTED:** VoiceControlPanel, ShellInput, HamIconMan, MicClientMapper,
    MicNull are ALREADY ~fully matched; their `is_stub=1`/`0%` db rows are STALE report.json
    pairing artifacts (atexit dtors / CamShot mis-pairing). Only synth_xbox/Mic held a real gap.
  - **Roadmap audit "synth_xbox/Mic + ExternalMic workable stubs" CORRECTED:** the 49 ExternalMic
    stubs + Mic's big fns (Poll/OnDataReady/ProcessChatData/AddData/Init at og 35.5%) are NOT
    implemented in og source either → require ground-up Ghidra archaeology, not an og port. Only
    ~18 synth_xbox/Mic fns are og-portable.
  - "Our headers may be MORE correct than og" CONFIRMED for the config block: our ctor was
    already correct (contiguous `lbl_82F474C8` block, 98.9% vs og's 93.2%), but the Set* helpers
    needed og's separate-static model. Neither pure-struct nor pure-og was uniformly right.
- **Risks:**
  - `MicManagerXbox::unk1c` retype: same size/offset, was only ever init to 0; the only new use is
    `Shutdown`'s `Release()` vcall (matches target). synth_xbox is Xbox-only → no cross-platform
    impact. **Low.**
  - `synth/Mic.h` enum +3 values (additive): included by native/web TUs (FxSend.cpp, Synth.cpp,
    Game.cpp, MicNull.h). Native gate PASS, web green, milo-tests 0 failed. kMicNull=2 retained →
    MicNull::GetType() byte-unchanged. **Low.** Note: kUSBMic=2 == kMicNull=2 is valid C++ but
    semantically ambiguous (USB mic vs null mic share value 2) — byte-correct, informational only.
  - StartPlayback 97.1% / ctor 98.9% are not 100% — backend artifacts (two-float const-pooling
    layout / relocation-anchor register choice), byte-equivalent in behavior, both BEAT og's
    ceiling. **Material wins, not floors that block.**
  - `ExternalMicClientMgr` moved to `ExternalMic.h`: a concurrent lane re-adding a local def to a
    synth_xbox TU including it would hit C2011. No other wave-8 lane touches synth_xbox.
- **Verdict (verifier `pass`):** ≥15 net-new MET (16 per report.json, 15 confirmed at exactly
  100.0% by live run_objdiff); 0 guard drops; 0 regressions on header-sharing units; build up to
  date. **Metric note (informational, NOT a blocker):** `SetNoiseGate` scores **99.96%
  normalized** in report.json but **97.8% normalized** in live run_objdiff — a genuine
  addressing-mode difference (`stfs` register-indexed via r11 vs frame-pointer-relative) that
  report.json's relocation normalization masks. The commit's "99.6%" references
  `fuzzy_match_percent`, which is correct for that metric. The lane still clears ≥15 on the 15
  fns confirmed at true 100%, so SetNoiseGate's status does not affect acceptance.

### Lane D — permuter sweep over the `90–100` band (Sonnet) — **FAIL (process gaps; correctness OK)** (status: complete, repaired)

- **Branch:** `wave8/d-permuter-sweep` (2 clean single-purpose commits: `d9e2a8e7` LinearizeKeys,
  `80c90838` SyncPristineCtrlPoints) · **Worktree:** `/home/free/code/milohax/wt-wave8-d-permuter-sweep`
- **Files (2):** `src/system/rndobj/Utl.cpp`, `src/system/rndobj/Spline.cpp`.
  (`math/Geo.cpp` and `lazer/meta_ham/MetaPerformer.cpp` were both reverted to baseline — see
  below; 0-line diff vs `2b4e6cb0`, verifier-confirmed.)
- **Repair round** addressed all 5 verifier fixes from the first pass: rebased 2 conflated
  commits into 2 clean single-purpose commits, **dropped the false CheckBSPTree win**, reverted
  the uncommitted MetaPerformer `_cond` extraction, removed `.permuter.lock`/`.permuter_bak`
  files. Working tree clean except untracked `venv/`.
- **2 curated wins (verifier re-measured via run_objdiff, worktree plane):**
  - **`LinearizeKeys`** (rndobj/Utl) 99.96→**100.0% normalized** (99.7% raw, 324/324 equal,
    verdict Complete). Collapsed `if(f2){if(size>2)}`→`if(f2 && size>2)` for
    TransKeys/RotKeys/ScaleKeys. **Genuine.**
  - **`SyncPristineCtrlPoints`** (rndobj/Spline) → **94.3% normalized** (93.8% raw). Inlined
    `mCtrlPoints.size()` directly in the loop init + `i==size-1` (removed local `int size`).
    **Genuine.** Verifier note: the true baseline (main db + re-measured baseline code) is
    **92.24%**, not the commit's stated 93.6% (stale permuter internal baseline) → the real delta
    is **+2.06%**, larger than claimed.
- **Rejections (3, all correct — documented with hard before/after run_objdiff):**
  - **`CheckBSPTree`** (math/Geo.cpp): committed reorder 99.0% vs baseline 99.0% = **+0.0%
    measurable** → DROPPED. The original commit's "+0.02%" was a stale-db artifact; committing it
    would corrupt `sync_match_percent`. Residual = callee-saved FPR register-swap floor
    (f25↔f27 ×17, f26↔f28 ×16, f30↔f31 ×12 = 66 swap instrs, 0 structural mismatches). Verifier
    re-confirmed 99.0% and the floor.
  - **`SyncPristineCtrlPoints` int-size variant:** cached `int size` form = 92.2% vs the chosen
    full-inline win = 94.3% (−2.1%, full inline is correct).
  - **`MetaPerformer` `_cond` extraction:** 94.4% vs baseline 94.2% = +0.2% noise dominated by a
    callee-saved register floor → reverted (was an undisclosed uncommitted leftover from the first
    pass).
- **Contradictions (Lane D):**
  - The sweep's internal `current_percent` metric (from decomp.db) **overcounts vs canonical
    `match_percent_normalized`** from run_objdiff; ~70% of sweep-claimed wins were regressions or
    noise when verified. **Curation against live run_objdiff is non-optional.**
  - The `90–100` band is predominantly callee-saved register-allocation floors (post-regalloc
    artifacts), not logic-fixable; the source-routable count is far below what the 179-fn band
    size suggests.
  - main decomp.db shows `match_percent_normalized=100.0`/`verdict=COMPLETE` for CheckBSPTree but
    that is **stale 2026-05-30 triage drift**; live objdiff is 99.0%. **Do not trust db verdict
    text for this symbol.**
- **Risks:**
  - Permuter `--apply` mode applied unsafe transforms (null-deref hoists, double-negations,
    semantic rewrites, bool extractions) scored as wins by its stale internal baseline. **Future
    band sweeps must use `--no-apply` with manual per-fn run_objdiff curation.** The MetaPerformer
    leftover was a live instance — reverted.
  - Permuter workers can leave 0-byte `.obj` + `.permuter.lock`/`.permuter_bak` files when killed
    mid-build; `clean_stale_objects.sh` does not catch 0-byte objects. (The lone 0-byte
    `utl/StreamRecorder.obj` predates this work in main — May 27 leftover, not introduced here.)
    **Always sweep for lock/bak/0-byte + run a clean ninja before merging from such a worktree.**
  - Only ~2 source-routable wins remain in the swept slice; residual is overwhelmingly
    callee-saved register-assignment floor.
- **Verdict (verifier `fail` — process, not correctness):** Both committed wins verified genuine
  (LinearizeKeys 100.0%, SyncPristineCtrlPoints 94.3%); rejections confirmed correct; revert
  confirmed; rules clean. The `fail` is two unmet plan requirements:
  1. **Missing sweep statistics** — the plan requires reporting attempted/improved/curated-out
     counts; none are in any commit or doc.
  2. **Missing `permuter_exhausted` evidence strings** — the plan requires floors the sweep
     exhausts get evidence strings (at minimum CheckBSPTree); none written.
  Plus **advisory:** the SyncPristineCtrlPoints commit message baseline (93.6%) should be the
  run_objdiff-measured 92.24%; and `Z:tmpclaudebsf_*.obj` scratch should be removed before merge.
  **None of these block the merge of the 2 genuine wins** — they are orchestrator-completable
  post-merge (write the floor certs during the recert step; the missing sweep stats are a
  reporting gap, not a code defect).

---

## Consolidated apply runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches. As in
prior waves, **NO lane wrote `decomp.db`** (main mtime `2026-06-11 13:33`, no WAL/SHM) and **NO
lane produced new unicorn evidence.** Source effects: **Lane A** = none (empty branch);
**Lane B** moves PPC percents (2 new 100% in net/XLSPConnection + gesture/DirectionGestureFilter)
→ needs a real `sync`; **Lane C** moves PPC percents (16 new 100% + 3 partials in synth_xbox/Mic;
additive `synth/Mic.h` enum is native/web-compiled) → needs a real `sync` + the native + web gate;
**Lane D** moves PPC percents (2 wins in rndobj/Utl + rndobj/Spline) → needs a real `sync`. Run
from repo root on `main` **after** merging (no conflict to resolve).

```bash
# 0. Merge the three non-empty worktree branches (see merge-order section). NO conflict this
#    wave — all six pairs merge cleanly; no file is shared across lanes. Lane A is empty (skip).

# 1. (Lane C) Native + web gate is load-bearing because synth/Mic.h is native/web-compiled.
#    Re-verify on merged main BEFORE the sync:
bash scripts/check_native_compiles.sh                      # expect PASS (Lane C synth/Mic.h enum)
cmake --build native/build --target milo-tests -- -j$(nproc) # expect 326 PASS / 0 FAIL / EXIT 0
scripts/web/build.sh --release                             # expect [100%] Built target dc3-web

# 2. Full ninja on main regenerates report.json with all wave-8 source wins:
#    Lane B (XLSPConnection::Poll 100, DirectionGestureFilterDoubleUser::Update 100),
#    Lane C (16 synth_xbox/Mic reps at 100 + StartPlayback 97.1 + ctor 98.9 + SetNoiseGate),
#    Lane D (LinearizeKeys 100, SyncPristineCtrlPoints 94.3).
ninja

# 3. Repopulate current_percent / match_percent_normalized and promote the new 100% reps.
#    (Clears the stale is_stub=1 / "auto: all mismatches unfixable" / "reset: false COMPLETE"
#    labels Lanes B/C/D surfaced.)
python3 scripts/sync_match_percent.py --build --promote

# 4. Clear residual db-only stale stubs (harmless if already clean).
python3 scripts/reconcile_db.py --fix

# 5. (RECOMMENDED, orchestrator-run) Fresh unicorn refresh on main. Dry-run FIRST, inspect any
#    new candidate_bug flips before applying. The DirectionGestureFilter -1-guard fix is a REAL
#    behavioral correction — it should flip that function EQUIVALENT (or stay so); confirm it does
#    not regress any neighbor. Lane B/C/D's certified floors stay DIVERGENT/honest.
bash scripts/nightly_measurement_guard.sh --unicorn                  # dry-run (temp DB copy)
bash scripts/nightly_measurement_guard.sh --unicorn --unicorn-apply  # live (after adjudication)

# 6. Record floor certificates (the recert step — this is where Lane D's plan-required
#    permuter_exhausted strings get written, plus Lane A/B's confirmed floors). Single writer.
python3 scripts/certify_floor.py --apply ...   # see "floor-cert backlog" below for the symbol list
python3 scripts/reconcile_db.py                # expect check (e) drift = 0
python3 scripts/certify_floor.py --summary     # record new open count after wave-8 wins land
```

**Notes / lane-specific:**
- **No objdiff fork / shared-binary step this wave** (unlike wave 7) — no lane touched objdiff.
- **Lane A is an empty branch** — nothing to merge, no sync effect. Skip it in the merge order.
- **Lane C native + web gate is mandatory before the sync** because `synth/Mic.h`'s additive enum
  compiles into native + web TUs. Lane C's worktree gates were green; re-run on merged main.
- **`SetNoiseGate` (Lane C) will read ~97.8% normalized in a fresh run_objdiff but ~99.96% in
  report.json** — a relocation-normalization gap on an addressing-mode difference. Do NOT treat
  the report.json number as a regression and do NOT flag the live 97.8% as incomplete; it is a
  metric-plane disagreement (the lane delivers 15 confirmed-100% fns regardless).
- **`SyncPristineCtrlPoints` (Lane D)** baseline is 92.24% on main, not the commit's 93.6%; the
  sync will record the correct +2.06% delta automatically (it re-baselines from the build).

### Floor-cert backlog (recommended `certify_floor.py --apply`, orchestrator single-writer)

These are the wave-8 confirmed floors with run_objdiff-normalized pct + diagnose signature. **No
lane wrote them** (no-db-write rule). The plan explicitly requires Lane D's exhausted floors to
get `permuter_exhausted` strings — this is where it happens.

| Symbol | Unit | Pct | Floor class / evidence string |
|---|---|---|---|
| `?CheckBSPTree@@YA_NPBVBSPNode@@ABVBox@@@Z` | math/Geo | 99.0% | `permuter_exhausted` — callee-saved FPR swap (f25↔f27/f26↔f28/f30↔f31, 66 swaps, 0 structural) |
| `?AcquireFontMap@RndText@@...` | rndobj/Text | 83.2% | `permuter_exhausted` — font→r27 vs r29 swap (0/980-file scan) |
| `?EvaluateChannel@CharBonesSamples@@...` | char/CharBonesSamples | 84.1% | `permuter_exhausted` — r31↔r30 callee-saved cascade + extsh |
| `?CollidePlane@RndDrawable@@...` | rndobj | 82.4% | artifact — per-callsite FMA scheduling (fmadds vs fmuls+fadds) |
| `??0NgPostProc@@...` | rndobj | 79.3% | artifact — callee-saved FPR (f31 across 2nd RandomFloat) |
| `?HasVert@PatchVerts@@...` | rndobj | 80.7% | artifact — regalloc register-class (callee-saved vs volatile) |
| `?ResetTaskTime@TaskMgr@@...` | TaskMgr | 72.8% | artifact — callee-save-vs-volatile r31/f31 |
| `?CacheResource@@...` | (Lane B) | 71.0% | artifact — block-layout + frame Δ +0x40 + EH/String-dtor delta |
| `?ResetNormals@@...` | (Lane B) | 77.9% | artifact — regalloc cascade (71 regswap pairs) |
| `?DrawDetectedBar@@...` | (Lane B) | 83.4% | artifact — float-constant-table placement |
| `?ScaleAddEq@@YAXAAVTransform@@ABV1@M@Z` | rnddx9/Mesh | 60.4% | `permuter_exhausted` — frame Δ −0x10 + r3/r31 volatile cascade |
| `??0HttpGet@@QAA@IGPBDE0@Z` | net/HttpGet | 63.2% | `permuter_exhausted` — bool-mask r10/r11 + String-ctor reorder |
| `?CompressThread@@YAKPAX@Z` | rndobj/Rnd | 30.4% | build-env — single-base-reg sharing of contiguous BSS globals |
| `??_GCharDriverMidi@@UAAPAXI@Z` | char/CharDriverMidi | 58.4% | codegen — compiler deleting-dtor inlines `~CharDriverMidi` |
| `?HolmesSetFileShare@@YAXPBD0@Z` | os/HolmesClient | 49.4% | build-env — `gShareName−0x40=gMachineName` adjacent-global base sharing |

(Lane A's ScaleAddEq/HttpGet/CompressThread/CharDriverMidi/HolmesSetFileShare + Lane B's
CollidePlane/AcquireFontMap/EvaluateChannel/NgPostProc/HasVert/ResetTaskTime/CacheResource/
ResetNormals/DrawDetectedBar + Lane D's CheckBSPTree.)

---

## Merge order for `wave8/*` branches (with cross-lane conflict check)

`git diff --name-only 2b4e6cb0..wave8/<lane>` and pairwise `git merge-tree --write-tree` were run
from main HEAD `2b4e6cb0` (all four branches descend from it). **There is NO git conflict this
wave** — the single-owner rule held even though the plan flagged real overlap risk ("four
source-heavy lanes … check shared headers especially").

### Conflict check — ALL CLEAN

Pairwise `git merge-tree --write-tree` (no conflict markers). Lane A is empty, so its pairs are
trivial no-ops (merge-tree rc=1 reflects "nothing to merge from A," not a conflict):

| Pair | Result |
|---|---|
| `a-logic-gaps` × `b-archaeology-3` | **CLEAN** (A empty — no-op) |
| `a-logic-gaps` × `c-og-hybrid` | **CLEAN** (A empty — no-op) |
| `a-logic-gaps` × `d-permuter-sweep` | **CLEAN** (A empty — no-op) |
| `b-archaeology-3` × `c-og-hybrid` | **CLEAN** (rc=0) |
| `b-archaeology-3` × `d-permuter-sweep` | **CLEAN** (rc=0) |
| `c-og-hybrid` × `d-permuter-sweep` | **CLEAN** (rc=0) |

**No file is touched by more than one lane** (`sort | uniq -d` of all changed-file sets is
empty). The lanes are path-disjoint by directory.

### Cross-lane file map (no file multiply-touched)

| File(s) | Lane | Conflict? |
|---|---|---|
| *(none — empty branch)* | A | n/a |
| `src/system/net/XLSPConnection.cpp`, `src/system/gesture/DirectionGestureFilter.cpp` | B | No |
| `src/system/synth/Mic.h`, `src/system/synth_xbox/{Mic.cpp,Mic.h,ExternalMic.h,Synth.cpp}` | C | No |
| `src/system/rndobj/{Utl.cpp,Spline.cpp}` | D | No |

- **Shared-header check (the plan's flagged risk):** the only shared header touched is Lane C's
  `src/system/synth/Mic.h` (additive `Mic::Type` enum). It is `#include`d by ~6 native/web TUs
  (FxSend.cpp, Synth.cpp, Game.cpp, MicNull.h, MicClientMapper.cpp, Mic.cpp). **No other wave-8
  lane touches any synth header or any of those includers** (B is gesture/net, D is rndobj, A is
  empty), so there is no cross-lane header conflict. Lane C's own native (`check_native_compiles`
  PASS), milo-tests (326/0), and web (`Built target dc3-web`) gates cleared the enum change.
- **No other shared headers touched:** Lane B's Mtx.h Plane::Dot reorder was tested then reverted
  (`git diff … -- '*.h'` returns 0 lines on the B branch); Lane D touches only .cpp; Lane A is
  empty.

### Recommended order

1. **`wave8/c-og-hybrid`** (`493c0325`) — merge first (largest win set, touches the only shared
   header). Run `check_native_compiles.sh` + `milo-tests` (expect 326/0) + `web/build.sh
   --release` immediately after so the additive enum is gated before anything else stacks on it.
2. **`wave8/b-archaeology-3`** (`e7c82025`) — merge second (PPC percents; gesture/net only, no
   shared header). The `DirectionGestureFilter` `-1`-guard is a behavioral correction — re-run
   milo-tests after merge.
3. **`wave8/d-permuter-sweep`** (`80c90838`) — merge third (PPC percents; rndobj only).
4. **`wave8/a-logic-gaps`** — **DO NOT MERGE** (empty branch == main). Remove the worktree:
   `git worktree remove --force /home/free/code/milohax/wt-wave8-a-logic-gaps`.

After all merges: full `ninja`, then the DB apply runbook (`sync_match_percent.py --build
--promote`, `reconcile_db.py --fix`, optional unicorn refresh, the floor-cert backlog write, and
`certify_floor.py --summary`). B/C/D order is interchangeable (disjoint files); all three must
precede the sync. Merging C first is recommended only to gate the shared-header change early.

---

## What blocks merging

- **NO GIT MERGE BLOCKER.** All six lane pairs merge cleanly; no file is shared across lanes; no
  shared header is multiply-touched; no `decomp.db` write (main mtime `13:33`, no WAL/SHM); main
  HEAD unchanged (`2b4e6cb0`); no `Co-Authored-By`; no `git stash` in any lane. (The
  `CharPollGroup.cpp` modification + scratch files in the main working tree are a concurrent
  agent's, NOT from any wave-8 lane.)
- **ACCEPTANCE GAPS to record (orchestrator decisions, none a hard blocker):**
  - **Lane A: 0 improved vs ≥10, 0 zero-starts vs ≥20.** This is a **correct refutation** of the
    band's premise (doc-08 §F4), not an effort failure — accept and re-scope future Lane A.
  - **Lane B: 2 qualifying wins vs ≥6.** Floor cap (same as waves 6/7) — **recommend
    accept-partial-with-rationale** (wave-6 precedent: 5/8 accepted). Both wins are genuine and
    behavior-correct.
  - **Lane D: verifier `fail` on two PROCESS gaps, not correctness** — (1) no sweep stats
    (attempted/improved/curated-out) reported; (2) no `permuter_exhausted` evidence strings
    written for exhausted floors. Both are completable by the orchestrator post-merge (the floor
    strings during the recert step; the sweep stats are a reporting gap). The 2 committed wins are
    verified genuine and should be merged. **Do not block on the verifier `fail`.**
- **METRIC-PLANE CAVEAT to flag (not a blocker):** `SetNoiseGate` (Lane C) reads 99.96%
  normalized in report.json but 97.8% in live run_objdiff (addressing-mode masked by relocation
  normalization); `SyncPristineCtrlPoints` (Lane D) commit message understates its baseline (real
  92.24% → +2.06%). Both resolve correctly on the post-merge `sync`. Normalized run_objdiff is
  canonical.
- **DB-DRIFT cleanup surfaced (route to sync/reconcile, none block):** (a) Lane A's 27 link_glue
  ObjPtrList "zero-start" rows + Matrix3-Multiply-as-CharLookAt + several NOTINREP rows should be
  excluded/re-attributed (they inflate the authorable backlog); (b) Lane B's stale `current_percent`
  + the two `ObjPtrVec::erase` 82.5% stub rows; (c) Lane C's stale `is_stub=1` rows for
  VoiceControlPanel/ShellInput/etc. All resolve on `sync_match_percent.py --build --promote` +
  `reconcile_db.py --fix`.

---

## Open follow-ups for Wave 9 — and a candid yield assessment

**Candid assessment: the marginal authorable yield is now low and falling, and three of four
Wave-8 lanes returned at-or-below their floors.** Wave 8 netted **20 functions** (16 og-port,
2 archaeology, 2 permuter) against four lanes — and 16 of those came from a single structural
file (synth_xbox/Mic) that Lane C correctly identified as the *only* real og-portable gap left.
The signal across waves 6/7/8 is consistent and now strongly corroborated by Lane A's clean
refutation: **the remaining open set is floor/artifact-dominated, and percent alone is a useless
workability indicator.** Concretely:

1. **The `<70` band is a floor census, NOT logic archaeology (Lane A, doc-08 §F4 confirmed).**
   Re-scope it: the planner should derive zero-start lists from **report.json, not decomp.db**
   (27/30 db zero-starts are drift), drop reverse-artifact rows (target lacks the symbol), and
   re-attribute stale unit mappings (Matrix3 Multiply → mtx.cpp, not CharLookAt). Future "Lane A"
   should be "floor census + cert sweep," and its expected *match%* yield is ≈0.

2. **The `70–90` band is exhausted of the clean control-flow/logic-gap class (Lane B).** The 2
   wins this wave came from the last of it (block-layout + a real `-1`-guard bug). The residue is
   FMA-scheduling / FPR-spill / callee-saved-register-choice / float-const-table / register-
   pressure floors — confirmed by 3 permuter sweeps at 0 improvement. **Expected yield of another
   `70–90` archaeology lane is ~1–3 wins** (the occasional reverse-engineerable behavioral bug
   like the gesture `-1` guard), at high effort. Worth one more low-priority pass *only* to
   harvest behavioral correctness fixes (which also de-risk the native port), not for match%.

3. **The `90–100` band is overwhelmingly callee-saved register-allocation floor (Lane D).** ~70%
   of permuter "wins" are noise/regressions against the canonical metric; the swept slice yielded
   2 source-routable wins. **The remaining routable count is far below the 179-fn band size.**
   Future sweeps must use `--no-apply` + manual run_objdiff curation, and should **front-load
   `permuter_exhausted` cert-writing** so the band shrinks on paper and isn't re-swept. Expected
   yield of another full `90–100` sweep is a small handful of wins at high compute cost.

4. **The genuinely positive-EV work is structural og ports and behavioral correctness, not
   match-% grinding.** Lane C's synth_xbox/Mic (16 fns) is the template: find the *one* file
   where og has a fuller implementation and our tree has a real structural gap, do the hybrid
   reconciliation. Wave-7's open follow-up #1 (the coordinated structural og lane: XAUDIO2→
   xapobase + dsp/StandardEffect migration + FxSend360 named-member reconciliation + jeff
   FxSendPitchShift/Synapse partition fix) remains the **highest-EV remaining lever** — it is the
   only path to a meaningful net-new count, and it is gated on the native build, not a
   behavior-neutral graft. The ExternalMic 49-stub family (Lane C found og lacks them too)
   requires ground-up Ghidra archaeology — high effort, deferrable.

5. **Process debt to clear before the next sweep wave:** (a) write the Wave-8 floor-cert backlog
   (table above) so those ~15 functions stop reappearing as "workable"; (b) fix the decomp.db
   drift planes Lanes A/B/C surfaced (link_glue zero-starts, Matrix3 attribution, stale is_stub
   rows) so future band queries don't over-count; (c) the Lane D verifier's two required process
   fixes (sweep stats + exhausted-floor certs) are the *standing* requirements for any future
   permuter lane — bake them into the lane spec.

**Recommendation:** Wave 9 should be a **single high-EV structural lane (the coordinated og
FxSend/dsp port)** plus a **DB-hygiene + floor-cert pass** (Lane A's refutation + Lane D's
backlog), and should NOT re-run broad `<70` logic archaeology or another full `90–100` permuter
sweep — both have demonstrably ≈0 marginal yield now. The done-with-certs number (98.63% fns /
96.07% bytes pre-wave-8) will tick up modestly from the 20 wins + the floor-cert sweep, but the
remaining authorable surface is small and floor-dominated; the path to "100%" runs through
correctly *certifying* the floors and the structural-port long tail, not through more grinding.
