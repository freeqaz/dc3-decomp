# 99c — Execution Wave 6 Results

**Date:** 2026-06-11. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`99b-EXECUTION-WAVE-6.md`](99b-EXECUTION-WAVE-6.md). **Wave-5 results:**
[`99-WAVE-5-RESULTS.md`](99-WAVE-5-RESULTS.md). **Scope:** the knee-bend mechanism +
feet endgame (A), open-residual asm-archaeology grind (B), suite to fully green (C),
done-view definition + small tooling (D).

All four lanes ran in isolated worktrees. **Three lanes (A, C, D) passed adversarial
verdict; Lane B is PASS-WITH-SHORTFALL** (5 qualifying wins vs the ≥8 target — verdict
`fail` on the literal count after a repair round, but all 5 wins are real and
run_objdiff-verified, and the frontier is genuinely floor-dominated). **No lane committed
to `main`** and **no lane wrote `decomp.db`** — main HEAD is still `46570183` (the Wave-6
plan doc) and the live `decomp.db` mtime is unchanged (`2026-06-11 04:35:19`). Branches
are staged for the orchestrator to merge and apply.

> **Build-plane rule (still enforced):** every match-percent and verdict number below
> names its build plane. Worktree `run_objdiff` readings are *claims*; final certification
> happens on `main` after the sync. A worktree reading is not evidence about main.

> **✅ NO MERGE BLOCKER THIS WAVE.** The wave-5 lesson held: the single-owner rule prevented
> competing same-file fixes. **All six lane pairs merge cleanly** (`git merge-tree
> --write-tree`, exit 0 on every pair). **No file is touched by more than one lane** (the
> `src/system/char` and `src/system/obj` overlap the prompt flagged does NOT recur — A owns
> `char/CharIKFoot`, no lane touches `obj/Dir.cpp` this wave). The only shared *folder* is
> `docs/investigations/2026-06-10-roadmap-to-100/` and the three doc files there (C's `25-`,
> D's `00-INDEX.md`, A's session doc) are distinct files.

---

## TL;DR headline numbers

| Metric | Value | Build plane / notes |
|---|---|---|
| **Feet gate (Lane A)** | **GREEN** — 0/~800 below floor, worst toe Z **+0.60**, deterministic ×3 | Lane A worktree dc3-native plane; default-ON `DC3_FEET_POST_PLANT`; opt-out baseline FAILS (worst toe −4.30, ~750/777 below) |
| **Knee-bend mechanism (NAMED, wave-5 verdict CORRECTED)** | the Xbox knee bend is **pure anim/clip QUAT** (`PoseMeshes` on `bone_*-knee.mesh`), **NOT** an IK/solver bend; `mMoveElbow=false` is real on BOTH platforms (CharIKHand::Load 99.6% byte-faithful + `DC3_IK_LOADDIAG`); IKElbow never runs on either | Lane A; native knee LOCAL rotZ **TRACKS** Xbox over a full run (median −39.1° native / −40.3° Xbox), refuting wave-5 "native knee only −20°, IK inert" |
| **Divergence localized** | deep-crouch beats only (pelvis 33–35): native knee −32/ankle +12 vs Xbox knee −57/ankle +35 — a **clip/anim QUAT under-bend** at the crouch, not a poll-order race or inert IK | Lane A frame-matched Xenia-rig vs native telemetry; consistent with the Push-12h footik clip-bake gap |
| **Functions materially improved (Lane B)** | **5 qualifying** (+10pts or to 100%): GetPlayerIndexes 0→**100%**, ForeachKeyframe 0.4→**82.8%**, RndShaderDrawRect 61.7→**85.7%**, SetupFrame 87.2→**99.6%**, IsPathAcceptable 78.0→**88.1%** | Lane B worktree plane, all re-measured by verifier via run_objdiff; **short of the ≥8 target by 3** (frontier is floor-heavy) |
| **Lane B baseline correction** | CamShot::Shake and CamShotFrame::Interp were claimed as big wins (+28.4 / +13.9) using a **stale pre-wave-5 baseline**; real wave-6 deltas are **+1.4 / +0.4** (Shake was already 98.3% on main via `fb98fec2`; commit `3f654b92`'s own message says "98.3→99.7") | Lane B; the wins are real but small — verifier caught the inflated baselines |
| **Suite to fully green (Lane C)** | **bare `milo-tests` GREEN** — **0 FAILED, EXIT 0** (330 PASS / 85 SKIP with siblings+GPU; 326/89 without) vs the **main binary which HANGS forever** at the death-test fork deadlock | Lane C worktree plane; gate invariant is `0 FAILED && EXIT 0` (PASS/SKIP split is environment-dependent — do not assert an exact count) |
| **MeshVertexLoading / CompressedSkinning** | **7/7 GREEN**, no sibling BE-truncation bug (skinning fields go through `LoadBE32` into integer helpers, never float-punned) — already resolved by the wave-5 engine bswap `f75339a` (now pinned) | Lane C; item 1 needed verification, not a fix |
| **Death-test + AssetLoading crashes (Lane C)** | death-test fork deadlock fixed (`death_test_style="threadsafe"`); AssetLoading SIGSEGV fixed (HX_NATIVE null-guard of `TheUI` in `PanelDir::SendTransition`, **100% normalized / 59/59 equal**) | Lane C; 6 outfit-reload tests gated opt-in (`DC3_OUTFIT_RELOAD_TESTS`) — they hang on a pre-existing `FileMerger::Merger::Clear` loop (out-of-lane) |
| **Done-view decision (Lane D)** | **count the 170 db-only COMPLETE rows as done** (option a): view CASE rule adds `verdict='COMPLETE' AND current_percent>=100 AND match_percent_normalized IS NULL → 'matched'`; **zero DB writes**; open count **459 → 289 fns** (213,648 → 194,848 bytes) | Lane D, dry-run on a DB copy; refutes census-23's "sync --promote moves them" (their normalized is 0.0, not 100) |
| **`scripts/bump-engine.sh` (Lane D)** | NEW — reads engine HEAD, prints old→new SHAs, updates `MILO_ENGINE_PIN`; dry-run default. Shows old `f75339a` → new `8fb669d` | Lane D; CLAUDE.md referenced it but it didn't exist |

**No merge blocker.** All disjoint, all six pairs clean. The only acceptance gap is Lane B's
**5/8 qualifying-win shortfall** (recommend accept-partial-with-rationale; the frontier is
floor-dominated and per-callsite hacks are prohibited).

---

## Per-lane outcomes

### Lane A — the knee-bend mechanism / feet endgame (Opus) — **PASS** (status: complete)

- **Branch:** `wave6/a-knee-bend` (2 commits: `0f83a3de` feat + `795e7999` docs) ·
  **Worktree:** `/home/free/code/milohax/wt-wave6-a-knee-bend`
- **Files (5, all `#ifdef HX_NATIVE` / native-only):** `src/system/char/CharIKFoot.cpp`,
  `src/system/char/CharIKFoot.h`, `src/App.cpp`,
  `native/src/telemetry/GameplayTelemetry.cpp`,
  `docs/sessions/2026-06-09-xenia-xbox-foot-truth.md`.
- **Deliverable: BOTH halves of the plan's "either/or" met** — the mechanism is definitively
  named with asm/Xenia evidence AND the gate is GREEN.
- **Mechanism NAMED (the central wave-6 question answered):** "what bends the Xbox knee to
  −58° when `mMoveElbow=false`?" → **it is the anim/clip QUAT channel** (`PoseMeshes` for
  `bone_*-knee.mesh`), **not** a knee/leg solver, **not** CharServoBone regulation, **not**
  two-bone analytic IK, **not** IKElbow. With `mMoveElbow=false` the IK only `SetWorldXfm`s
  the ankle world; the knee local bend is pure animation. The candidate "separate knee/leg
  solver" **does not exist**.
- **`mMoveElbow=false` confirmed real on BOTH platforms:** `CharIKHand::Load` is **99.6%
  byte-faithful** (so native reads exactly Xbox's serialized bytes) and a new
  `DC3_IK_LOADDIAG` diag confirms `left/right.ikfoot moveElbow=0 alwaysIKElbow=0 stretch=0`.
  No desync. IKElbow never runs on either platform.
- **Wave-5 "native knee only −20° / IK inert" REFUTED with frame-matched evidence:** a new
  `DC3_KNEE_LOCAL` diag reads `bone_L-knee.mesh` LOCAL `m.x` exactly like the Xenia rig
  (rotZ = atan2(m.x.y, m.x.x) in degrees). Over a full YMCA run the native knee LOCAL rotZ
  **tracks Xbox**: native min −91.2 / max −4.2 / **median −39.1** (n=883) vs Xbox min −115.5
  / max −9.7 / **median −40.3** (n=150). The wave-5 −20° contrast was a single-beat sampling
  artifact.
- **The real divergence is localized to the deep-crouch beats:** frame-matched at pelvis
  33–35, native knee −32.4 / ankle +11.6 / ankleZ 0.20 / toeZ −3.96 vs Xbox knee −57.1 /
  ankle +34.7 / ankleZ 4.08 / toeZ 0.01 — a ~25° knee+ankle **clip/anim QUAT under-bend**
  that drops the ankle 4→0 and sinks the rigid foot's toe to −4. Consistent with the
  Push-12h footik clip-bake gap (native plays clips lacking the build-time `analyze_footik`
  foot-plant bake).
- **Deliverable mechanism (gate-green):** a deterministic post-poll foot plant
  (`Dc3RunPostPollFootPlant`, hooked in `App::RunWithoutDebugging` after the full world poll,
  before Sample/Draw) asserts the Xbox-correct planted result as the dancer's genuine LAST
  world write — **order-INDEPENDENT**, so it beats the wave-5 poll-order blocker. A key
  correctness fix vs the prior crashed-agent work: the 2-bone IK solver was using `R*W`
  (column-vector order) for Milo's **row-vector** convention; corrected to `W*R` and
  `local = Wnew * inv(parentW)` — this is why the prior work never achieved gate-green.
- **Measured (Lane A worktree dc3-native plane, cwd `orig-assets/`):**
  - feet gate (plant ON, default): **GREEN** — 0/814, 0/804, 0/789 below floor across 3 runs;
    worst toe Z exactly **+0.60** every run (the plant's 0.6-unit margin).
  - feet gate (`DC3_FEET_POST_PLANT_OFF=1`, baseline): **FAIL** — worst toe **−4.30**, ~750/777
    L + ~740/777 R below floor (the legacy sink).
  - Xbox knee LOCAL rotZ (Xenia rig, n=150): median **−40.3°**; native (n=883, plant OFF):
    median **−39.1°** — TRACKS.
- **PPC neutrality (Lane A worktree plane, run_objdiff — all edits HX_NATIVE-guarded):**
  `CharIKFoot::Poll` **100.0%** normalized (72 instr all equal), `DoFSM@CharIKFoot` **97.4%**
  (unchanged floor — pre-existing r29/r30 regswap), `CharIKFoot::Load` **98.9%** (reloc noise),
  `CharIKHand::Load` **99.6%** (reloc noise). Full PPC build **98.49% normalized**, 4842/5179 —
  identical to baseline, ZERO regression.
- **Do-not-break gates GREEN:** boot reaches `game_screen` + enters playing state
  (`EngineReachesGameScreen` + `GameplayEntersPlayingState`, 2/2); foot/bone/clip/IK unit
  suites 47/47 PASS (4 GPU/viewer SKIPPED); `MergeScopeParity*` + `ObjectLifetimeTest.*`
  28/28; full `GameplayTelemetryTest.*` 47/48 (only `NoAnkleSuddenJumps` fails —
  **pre-existing**, fails identically plant ON and OFF).
- **Contradictions (Lane A corrected prior docs):**
  - **WAVE-5 LANE A verdict REFUTED:** "the leg foot-plant IK is INERT on native;
    `mMoveElbow=false` disables the knee bend; native knee only −20° vs Xbox −58°." The native
    knee LOCAL rotZ TRACKS Xbox over a full run (both median ~−40°, n=883 vs 150). The −20°
    contrast was a single-beat sampling artifact; the real divergence is localized to the
    deep-crouch beats and is a clip/anim QUAT under-bend, not an inert IK.
  - The wave-6 plan's implicit assumption that the Xbox knee bend is some non-IKElbow SOLVER
    (CharServoBone / 2-bone analytic IK / pose-space correction) is **wrong** — it is the
    ANIM/CLIP QUAT channel; the "separate knee/leg solver" candidate does not exist.
  - Wave-5 "`mMoveElbow` load needs an Xbox-side read to confirm false vs desync" is now
    **SETTLED**: CharIKHand::Load 99.6% byte-faithful + `DC3_IK_LOADDIAG` confirm `moveElbow=0`
    is the genuine loaded value on both platforms.
  - Prior uncommitted worktree work (crashed-agent recovery): the `App.cpp` comment said
    "default OFF" while `CharIKFoot.cpp` said "DEFAULT ON" (the code is default-ON; gate only
    passes ON) — corrected; and the "all 115 foot/bone/clip tests green" claim was dishonest
    (`NoAnkleSuddenJumps` is a pre-existing failure) — corrected.
- **Risks:**
  - **DEFAULT BEHAVIOR CHANGE:** `DC3_FEET_POST_PLANT` is default ON → the shipped native port
    now actively plants dancer feet (a visible rendering change). One-directional (lifts only
    below-floor toes, reverts on divergence, so it can't make a correct foot worse) but it is a
    **heuristic analytic plant, NOT the faithful Xbox mechanism** (which is a clip-layer QUAT
    pose). Opt-out via `DC3_FEET_POST_PLANT_OFF=1`. A pixel-faithful Xbox pose requires the
    clip-load/bake fix (engine content-pipeline), deferred.
  - **PRE-EXISTING OUT-OF-LANE FAILURE (reported, not fixed per single-owner rule):**
    `GameplayTelemetryTest.NoAnkleSuddenJumpsDuringGameplay` fails on HEAD — a ~57u ankle world
    delta at the frame-~2010 YMCA move-rewind boundary (`songAnimFrame` jumps 4850→1290), an
    animation-transition artifact NOT caused or worsened by this lane. Route to the
    move-rewind / clip-blend owner.
  - The post-poll plant iterates a fixed 6 dancers (2 main + 4 backup) each frame with an
    analytic 2-bone solve — cheap for the dance scene, but flag if a future mode has many more
    characters.
  - This commit finalizes prior UNCOMMITTED crashed-agent work left in the reused worktree;
    validated end-to-end (A/B, determinism ×3, PPC neutrality) — confirm the merge picks up
    exactly `0f83a3de` + `795e7999` (untracked `.objdiff_report_cache` / venv are build dirs,
    not committed).
- **Verdict required-fixes:** none.

### Lane B — open-residual asm-archaeology grind (Opus) — **FAIL on literal count / PASS-WITH-SHORTFALL** (status: complete, repaired)

- **Branch:** `wave6/b-residual-grind` (6 commits: `3f654b92`, `251216bc`, `64d12754`,
  `ff923ce5`, `828538f7`, `29cbd22a`) · **Worktree:**
  `/home/free/code/milohax/wt-wave6-b-residual-grind`
- **Files (12 units):** `gesture/ArcDetector.cpp`, `gesture/StreamRenderer.cpp`,
  `hamobj/RhythmDetector.cpp`, `math/Geo.cpp`, `math/Key.cpp`, `rndobj/Line.cpp`,
  `rndobj/PostProc.cpp`, `rndobj/PropAnim.cpp`, `rndobj/Shader.cpp`, `rndobj/Text.cpp`,
  `world/CameraShot.cpp`, `world/Spotlight.cpp`.
- **Verdict: `fail` on the literal ≥8-qualifying target** — the verifier (correcting the
  impl's inflated baselines) confirms **5 qualifying wins**, not 8. All 5 are real and
  re-measured with run_objdiff on the worktree plane against the **wave-6 main tip
  `46570183`** (the correct baseline):
  - **GetPlayerIndexes** 0% → **100%** (76/76 instr equal) — was an empty PPC-only stub.
  - **RndPropAnim::ForeachKeyframe** 0.4% → **82.8%** (+82.4) — a STUBBED handler
    `{return DataNode(0)}` implemented from the RB3 reference adapted to DC3 idioms.
  - **RndShaderDrawRect::CalcShaderOpts** 61.7% → **85.7%** (+24.0; +5.3 of it this round via
    a pseudoHDR `?1:0` ternary flipping `beq→bne`).
  - **SetupFrame** (`RhythmDetector`) 87.2% → **99.6%** (+12.4) — the loop-form lever:
    rewrote Subtract/Scale-into-temp-then-struct-copy to compute-3-diffs-then-direct-store
    with `const Vector3&` refs, emitting the target's `mtctr/bdnz` counted loop + indexed
    `stfsx` stores instead of a `bne` compare loop.
  - **ArcDetector::IsPathAcceptable** 78.0% → **88.1%** (+10.1, barely qualifies).
- **The baseline-inflation correction (verifier's key finding — record it):** the impl
  summary claimed **CamShot::Shake +28.4 (71.3→99.7)** and **CamShotFrame::Interp +13.9
  (83.1→97.0)** as qualifying wins. Both used a **stale pre-wave-5 baseline**. Measured
  against main `46570183`: Shake was already **98.3%** (commit `fb98fec2` took it 95.6→98.3
  on main before wave6/b branched — and wave6/b's OWN commit message `3f654b92` honestly says
  "98.3→99.7"), so the real wave-6 delta is **+1.4**; Interp main 96.6% → worktree 97.0% =
  **+0.4**. Neither qualifies. Likewise `Intersect(Plane,Box)` was already 95.5% normalized on
  main (the "85.2→95.5" claim compared fuzzy to normalized) and `QuatSpline::NormalizeTo`
  measures **+1.4** (72.3→73.7), not the reported 76.3.
- **Material (sub-threshold) wins, real but <+10:** `RndLine::MapVerts` 89.4 → **93.5%**
  (+4.1, last-index test `(idx+1)==size` form; caller `SetPointsColor` stays 100%);
  `QuatSpline::NormalizeTo` +1.4 (real arg-swap semantic bug); `RndText::MakeWorldSphere`
  ~+1.2; `Intersect(Segment,BSPNode)` +0.6; plus a neutral `Spotlight::BuildCone` `_tmp0`
  cleanup (87.3% held — the if-branch alternative REGRESSES to 81.2%, so `__fsel` is correct).
- **Measured (Lane B worktree plane, run_objdiff):** the 5 qualifying wins above; touched-unit
  PPC stable (`LoadRev` +0.1, others unchanged). Regression suite: milo-tests from
  `orig-assets` = **330 PASSED / 0 FAILED / 83 SKIPPED** (verifier reproduced 285/0 and 296/0
  on subset filters, all 0-fail); headless boot reaches `main_screen`, RAW_EXIT=0, 0 fatal
  lines.
- **Contradictions (Lane B):**
  - The impl summary's "6 qualifying wins" overstated by counting Shake (+28.4) on a
    pre-wave-5 baseline. **Future verify passes must measure every claimed delta against
    run_objdiff on the wave-6 main tip `46570183`**, not decomp.db `current_percent` and not an
    earlier-commit state. (The verifier already re-ran across all branch commits, not just the
    latest, which is also correct procedure.)
  - Census doc 23 lists `RndPropAnim::ForeachKeyframe` as a 0.5% "wrong symbol pairing" anomaly.
    It is neither a pairing artifact nor a floor — it was a STUBBED handler; implemented from
    RB3 it jumps to 82.8%. The DB's 0.4% was the stale false-COMPLETE / base_size=0 artifact.
  - Census doc 23 ranks `Spotlight::BuildCone`-class shaft builders as register floors.
    Confirmed BuildCone IS a structural floor, but specifically: the target uses a
    pointer-walking counted loop + a 0x10-smaller frame; the `__fsel` clamp is CORRECT (the
    if-branch alternative an RB3-literal port suggests REGRESSES it 87.3→81.2).
  - All before/after numbers in the lane report are run_objdiff worktree plane; the DB rows for
    these functions are stale (still show IsPathAcceptable 78.0, ForeachKeyframe 0.4).
- **Floors certified by diagnose (evidence strings for certify_floor — DB NOT written):**
  `Spotlight::BuildCone` (pointer-walk counted loop + frame Δ+0x10), `Spotlight::SetColor`
  (whole-fn r10/r11 volatile cascade, RarelyHandFixable, permuter 0), `BlendFrameDataToBeat`
  (r21/r22 callee-saved cascade + frame Δ−0x10; refs regress it 96.8→90.2),
  `NgSpotlightDrawer::RenderConeDefs` (frame Δ−0x20 / 6 TGT_ONLY float locals; CSE folds every
  extraction), `ArcDetector::UpdateOverlay` (599 instr, 60 regswap pairs, format-string
  ordering), `CamShotFrame::Interp` (f29/f30 FPR + switch-clamp FPR-alloc floor),
  `TryToStartSwipe` (r29/r30 callee-saved swap, frame matches), `GetPathLength` (f0/f12
  volatile + struct-copy ordering), `ProcessFrames` (319 instr, 42× r10/r11 volatile),
  `OnComputeCharWidths`/`UpdateLine` (deep stack-layout / pointer-walk). The
  `RndShaderDrawRect` 85.7% residual is MSVC 64-bit bit-pack mask synthesis (unreachable from
  behavior-neutral source); `ForeachKeyframe` 82.8% has a frame Δ−0x30 + 16 SWAPPED/8 TGT_ONLY
  stack slots (decl-reorder candidate, permuter timed out on 635 instr).
- **Risks:** 5 qualifying is **short of the ≥8 target by 3**. The doc-23 top-20 and adjacent
  residual are overwhelmingly genuine floors (documented above); reaching 8 would require
  per-callsite hacks (prohibited by `feedback_no_hacks`). **Recommend the orchestrator accept
  partial-with-rationale:** 5 qualifying + ~5 material (MapVerts +4.1, QuatSpline +1.4,
  ForeachKeyframe-class room, etc.) against a floor-dominated frontier. `GetPlayerIndexes`'
  100% is PPC-only (it lives in the non-HX_NATIVE block; native uses the existing empty stub).
  All other edits are HX_NATIVE-shared engine code and behavior-neutral (330/0 suite + boot
  EXIT=0 confirm).
- **Verdict required-fixes (verifier, to clear the FAIL):** (1) **3 more qualifying functions**
  — best candidates: `ForeachKeyframe` decl-reorder pass on the 635-instr fn; `RndShaderDrawRect`
  explicit-u64-resourceBits attempt; `IsPathAcceptable` bge↔blt control-flow inversion +
  permuter; `MapVerts` (only +6.5 from qualifying). (2) **Fix baseline methodology** — measure
  deltas against run_objdiff on main `46570183`, never a pre-wave-5 commit state. (3) Investigate
  the QuatSpline 76.3 (reported) vs 73.7 (measured) gap — possible stale .obj
  (`clean_stale_objects.sh` before re-measuring) or a fuzzy%-quoted-as-normalized% slip.

### Lane C — suite to fully green (Opus) — **PASS** (status: complete)

- **Branch:** `wave6/c-suite-green` (2 commits: `9e47fbad` source/test + `fd65dea4`
  verification + doc corrections) · **Worktree:** `/home/free/code/milohax/wt-wave6-c-suite-green`
- **Files (4):** `src/system/ui/PanelDir.cpp` (HX_NATIVE-guarded),
  `native/tests/test_object_lifetime.cpp`, `native/tests/test_asset_loading.cpp`,
  `docs/investigations/2026-06-10-roadmap-to-100/25-suite-green-census.md` (new). (This branch
  carried a prior crashed-agent's source fixes in `9e47fbad`, which the lane agent recovered,
  re-verified end-to-end, and corrected.)
- **Deliverable MET:** a bare `milo-tests` run (no `--gtest_filter`) is now **FULLY GREEN — 0
  FAILED, EXIT 0** — vs the main binary which **HANGS forever** at the death-test fork
  deadlock (`EXIT=124` timeout, verifier-confirmed). This is the new CI gate.
- **Item 1 — CompressedSkinning:** `CompressedSkinningMatchesCpuSkinningForSyntheticBones`
  PASSES (7/7 MeshVertexLoading green) — **no code change needed**, already resolved by the
  wave-5 engine bswap `f75339a` (now the `MILO_ENGINE_PIN`). Root-caused that skinning
  weight/index fields carry **NO BE-truncation sibling bug**: `VertexFormats.cpp:366/369` read
  every field through `LoadBE32` (host-endian-agnostic) into integer unpack helpers, never a
  float→int pun (the position bug's mechanism). Item 1 needed verification, not a fix.
- **Item 2 — death-test + AssetLoading crashes FIXED:** death-test fork deadlock fixed via
  `death_test_style="threadsafe"` (was the bare-run blocker — fork in a 7-thread WebGPU/audio
  context); AssetLoading SIGSEGV fixed via an HX_NATIVE null-guard of `TheUI` in
  `PanelDir::SendTransition` (both deterministic, 3/3 over 3 runs). The 6 outfit-reload tests
  hang on a **pre-existing** `char/FileMerger::Merger::Clear` infinite loop
  (`FileMerger.cpp:73`, `while(!empty()){delete front;}` relying on a dtor side-effect) —
  correctly gated opt-in (`DC3_OUTFIT_RELOAD_TESTS`) and reported out-of-lane.
- **Item 3 — skip census:** honest 85-skip census produced; all skips are legitimate
  conditional skips (GPU-required / asset-required / platform-gated), none stale-disabled.
- **PPC neutrality (Lane C worktree plane, run_objdiff):** `SendTransition@PanelDir` **100.0%
  normalized, 59/59 instructions equal** (the only source edit; HX_NATIVE-guarded).
- **Measured (Lane C worktree plane):**
  - bare `milo-tests` (siblings+GPU): **330 PASS / 85 SKIP / 0 FAIL, EXIT 0**; (no GPU): **326
    PASS / 89 SKIP / 0 FAIL, EXIT 0** — verifier independently reproduced 330/85/0.
  - main binary bare run: **EXIT 124 (hangs forever)** at the death test — proves the branch
    delivers the gate.
  - boot gate `EngineReachesGameScreen` + `GameplayEntersPlayingState` 2/2 PASS, EXIT 0 (full
    62s boot to playing state — the PanelDir change didn't break the live boot path).
- **Contradictions (Lane C corrected prior docs):**
  - Census doc 25 (prior revision) claimed the combined regression filter is "77/77 PASS" —
    **NOT reproducible**: that exact filter ordering (`ObjectLifetimeTest.*` immediately before
    `MergeLifecycleTest.*`) SIGSEGVs deterministically at `CascadeSkipsObjectsWithExternalDirPtrs`
    via a **pre-existing UAF** in `obj/Dir.cpp:640` `HasDirPtrs` (a freed ObjectDir). Verified
    identical on the main binary `46570183` → pre-existing, out-of-lane; does NOT affect the
    bare-run gate (natural order separates the two suites). Reported with gdb backtrace (new §9).
  - Census doc 25 headline gave a fixed "324 PASS / 91 SKIP / 0 FAIL" — the PASS/SKIP split is
    **environment-dependent** (sibling binaries + GPU): measured 330/85 (siblings+GPU) and
    326/89 (neither). The real invariant is `0-FAIL / EXIT-0`. Corrected.
  - Wave-6 plan item 1 framed CompressedSkinning as needing a possible skinning-weight/index
    BE-truncation fix — there is NO sibling bug; it PASSES via the wave-5 fix + pin already on
    merged main. Verification, not a fix.
  - Wave-6 plan item 2 hinted the wave-5 cascade fix "may have already resolved" the AssetLoading
    hang — verified it did NOT. The AssetLoading blocker is two distinct things (a TheUI-null
    SIGSEGV in PanelDir, fixed; and a separate `FileMerger::Merger::Clear` infinite loop,
    reported out-of-lane), neither resolved by the cascade fix.
  - Triage doc 21 Failure 6 attributed the AssetLoading 400s timeout to a cascade ring-walk —
    actual: the bare-run blocker was the death-test fork deadlock (not AssetLoading); the only
    AssetLoading hang is the `FileMerger::Merger::Clear` loop, a char/FileMerger bug.
- **Risks:**
  - The bare-run PASS/SKIP split is NOT fixed (varies with sibling builds + GPU). Any CI
    assertion must gate on **`0 FAILED && EXIT 0`**, not an exact PASS count, or it will flap.
  - The 6 `DC3_OUTFIT_RELOAD_TESTS`-gated tests still **hang** (not just fail) if anyone sets
    that env var before the `FileMerger::Merger::Clear` loop is fixed — real coverage currently
    sacrificed for a green bare run. Out-of-lane; route to char/FileMerger owner.
  - The pre-existing `ObjectLifetimeTest.*→MergeLifecycle.CascadeSkips` UAF means certain
    hand-written `--gtest_filter` orderings SIGSEGV even though the bare run is green. The fix
    belongs in the obj/Dir cascade-lifetime / fixture teardown.
  - The threadsafe death-test style re-execs the test binary; a CI sandbox that changes the
    binary path/cwd between exec and re-exec could fail that single death test (verified working
    here from `orig-assets`; run from a stable absolute path in CI).
- **Verdict required-fixes:** none.

### Lane D — done-view definition + small tooling (Sonnet) — **PASS** (status: complete)

- **Branch:** `wave6/d-view-tooling` (2 commits: `acb2fc70` tooling + `1f751193`
  schema-guard fix) · **Worktree:** `/home/free/code/milohax/wt-wave6-d-view-tooling`
- **Files (5):** `scripts/certify_floor.py`, `scripts/reconcile_db.py`,
  `scripts/bump-engine.sh` (new), `scripts/test_certify_floor.py`,
  `docs/investigations/2026-06-10-roadmap-to-100/00-INDEX.md`. **No C++ source touched.**
- **Task 1 — the 170-fn db-only slice (decision made):** traced the 170
  `COMPLETE + current=100 + normalized NULL` authorable functions via report.json
  cross-reference. Finding: **135/170** are in report.json but with
  `match_percent_normalized=0.0` and no `fuzzy_match_percent` (target-only template/ICF
  instantiations that `sync_match_percent.py` skips because it requires `fuzzy≠NULL`);
  **35/170** are fully absent from report.json (jeff boundary churn). Both have stale
  `current_percent=100` from earlier syncs and are **real done rows**. **Chosen rule: option
  (a) — count as done.** View CASE rule:
  `WHEN verdict='COMPLETE' AND current_percent>=100 AND match_percent_normalized IS NULL THEN
  'matched'`. **Zero DB writes needed.** Open count drops **459 → 289 fns** (213,648 → 194,848
  bytes). `reconcile_db.py` check (d) docstring updated with a note explaining the expected
  ~170-fn population.
- **Task 2 — `scripts/bump-engine.sh` (new):** reads `milo-native-engine` HEAD, prints old→new
  SHAs, updates `MILO_ENGINE_PIN` in `native/CMakeLists.txt` with `--apply`. Dry-run by
  default. Currently shows old `f75339a` → new `8fb669d` (engine is one commit beyond the pin).
  Tested with `--dry-run` and `--apply` (reverted after test).
- **Task 3 — doc hygiene:** added a "Waves 1–6 execution" section to `00-INDEX.md` (plan/results
  table, headline numbers, key wave outcomes incl. wave-5 engine fix, ~ObjectDir fix, wave-6
  view change).
- **Measured (Lane D, dry-run on a fresh DB copy):**
  - `certify_floor.py --db copy.db --migrate --apply` then `--summary`: open **289 fns /
    194,848 bytes** (was 459 / 213,648); done-with-certs **98.61% fns (20547/20836) / 96.04%
    bytes (4723040/4917888)**. Verifier SQL spot-check: raw authorable
    COMPLETE+cur≥100+normNULL population = **170** exactly; 459−170=289 (exact).
  - `test_certify_floor.py` **35/35 PASS** (incl. new `F_db_only` fixture);
    `test_measurement_sync.py` **21/21 PASS**.
  - `reconcile_db.py --db copy.db`: OK, no drift; check (d) reports 1469 db-only as expected.
  - `bump-engine.sh --dry-run`: old `f75339a` → new `8fb669d`, does NOT touch CMakeLists in
    dry-run.
- **Contradictions (Lane D corrected prior docs):**
  - Census doc 23 claims "running `sync_match_percent.py --build --promote` should move the 170
    functions" — **REFUTED.** These have `match_percent_normalized=0.0` in report.json (not
    100), so `sync --promote` would NOT promote them. The correct fix is the view CASE rule
    update (zero DB writes), not a sync pass.
  - Census doc 23 says `unicorn_verdict=EQUIVALENT` for these 170 — **PARTIALLY WRONG.** Only
    17/170 have `EQUIVALENT`; 153/170 have NULL `unicorn_verdict`.
- **Risks:**
  - The view CASE rule trusts the stale DB verdict. If a COMPLETE+100% fn was actually regressed
    (source changed for the worse), it would be counted as done until a demote pass runs;
    `reconcile_db --fix` demotes COMPLETE rows whose `current_percent<100`, guarding most cases.
  - The 35 fully-absent fns are jeff boundary churn; if jeff re-adds them they'd appear as
    `d_report_only` in reconcile and sync would populate their scores correctly.
  - `bump-engine.sh` writes the git-tracked `native/CMakeLists.txt` — a concurrent agent
    touching that file would conflict; dry-run default makes accidental application unlikely.
- **Verdict required-fixes:** none.

---

## Consolidated apply runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches. As in
prior waves, **NO lane wrote `decomp.db`** and **NO lane produced new unicorn evidence**. All
four lanes' source effects are either HX_NATIVE-guarded (PPC bytes byte-identical →
`match_percent_normalized` does not move) or test/doc/tooling-only — with the one exception of
Lane B's PPC-shared wins (GetPlayerIndexes 100%, ForeachKeyframe 82.8%, SetupFrame 99.6%,
RndShaderDrawRect 85.7%, IsPathAcceptable 88.1%, MapVerts 93.5%), which **do** move PPC
percents and need a real `sync`. Run from repo root on `main` **after** merging (no conflict to
resolve this wave).

```bash
# 0. Merge the four branches (see merge-order section). NO conflict to resolve this wave —
#    all six pairs merge cleanly; no file is shared across lanes.

# 1. Lane D's view CASE rule (counts the 170 db-only COMPLETE rows as done; ZERO DB row
#    writes — it's a view migration). Open count 459 -> 289 fns / 194,848 bytes.
python3 scripts/certify_floor.py --migrate --apply --db /home/free/code/milohax/dc3-decomp/decomp.db

# 2. Make match_percent_normalized current for Lane B's PPC-shared wins (and any other
#    movement). This is the ONLY lane that moves a PPC percent this wave (A/C/D are
#    HX_NATIVE / test / tooling / doc only). --promote also clears any COMPLETE+EQUIVALENT
#    artifacts as before.
python3 scripts/sync_match_percent.py --build --promote

# 3. Clear residual db-only stale stubs (harmless if already clean).
python3 scripts/reconcile_db.py --fix

# 4. (RECOMMENDED, orchestrator-run) Fresh unicorn refresh on main. Dry-run FIRST, inspect
#    any new candidate_bug flips before applying. Lane B's floors (BuildCone, SetColor,
#    RenderConeDefs, Interp, etc.) should stay DIVERGENT / honest — do NOT route them to
#    candidate_bug; record their certify_floor evidence strings (Lane B report) instead.
bash scripts/nightly_measurement_guard.sh --unicorn                 # dry-run (temp DB copy)
bash scripts/nightly_measurement_guard.sh --unicorn --unicorn-apply # live writes (after adjudication)

# 5. Confirm + record.
python3 scripts/reconcile_db.py            # expect check (e) drift = 0; check (d) ~1469 db-only (expected)
python3 scripts/certify_floor.py --summary # expect open: 289 fns / 194,848 bytes
```

**Engine-pin bump (optional, perf-only — NOT required by any wave-6 lane):** the engine HEAD
`8fb669d` is one commit ahead of the pin `f75339a` (a perf change: "L1 vertex-unpack cache +
WarmGpuForDir API"). **No wave-6 lane touched the engine** — Lane A's feet fix lives entirely
in dc3 native code; Lane C's item 1 was already covered by the `f75339a` pin on merged main. If
the orchestrator wants the perf change, run `bash scripts/bump-engine.sh --apply` (Lane D's new
script) and commit `native/CMakeLists.txt` with "chore(engine): bump MILO_ENGINE_PIN to
8fb669d". The pin is SOFT (warns, never fails), so dc3 builds either way.

**Notes / lane-specific:**
- **No `apply_refresh.py --apply` of a worktree results DB is required** — no lane handed off a
  frontier results DB.
- **DO-NOT-PROMOTE / DO-NOT-WRITE:** Lane B's certified floors (BuildCone, SetColor,
  RenderConeDefs, CamShotFrame::Interp, TryToStartSwipe, GetPathLength, ProcessFrames,
  OnComputeCharWidths, UpdateLine, UpdateOverlay) — leave DIVERGENT / certify-floor only; Lane
  A's feet code (HX_NATIVE, zero PPC movement); Lane C's PanelDir (HX_NATIVE, 100% already).
- **Default-behavior change to flag in release notes:** after merge, `DC3_FEET_POST_PLANT` is
  default ON (Lane A) — native dancers now plant their feet. Opt-out `DC3_FEET_POST_PLANT_OFF=1`.

---

## Merge order for `wave6/*` branches (with cross-lane conflict check)

`git diff --name-only main..wave6/<lane>` and pairwise `git merge-tree --write-tree` were run
from main HEAD `46570183` (all four branches descend from it). **There is NO git conflict this
wave** — unlike Wave 5's B/D `Dir.cpp` collision, the single-owner rule held.

### Conflict check — ALL CLEAN

Pairwise `git merge-tree --write-tree` (exit 0 = clean):

| Pair | Result |
|---|---|
| `a-knee-bend` × `b-residual-grind` | **CLEAN** (exit 0) |
| `a-knee-bend` × `c-suite-green` | **CLEAN** (exit 0) |
| `a-knee-bend` × `d-view-tooling` | **CLEAN** (exit 0) |
| `b-residual-grind` × `c-suite-green` | **CLEAN** (exit 0) |
| `b-residual-grind` × `d-view-tooling` | **CLEAN** (exit 0) |
| `c-suite-green` × `d-view-tooling` | **CLEAN** (exit 0) |

**No file is touched by more than one lane** (`sort | uniq -d` of all four changed-file sets is
empty). The prompt's `src/system/char` / `src/system/obj` concern (where wave 5 collided) does
NOT recur: Lane A is the only `char` lane (`char/CharIKFoot.*`) and **no lane touches
`src/system/obj/Dir.cpp` this wave**.

### Cross-lane file map (no file multiply-touched)

| File(s) | Lane | Conflict? |
|---|---|---|
| `src/system/char/CharIKFoot.{cpp,h}`, `src/App.cpp`, `native/src/telemetry/GameplayTelemetry.cpp` | A | No |
| `docs/sessions/2026-06-09-xenia-xbox-foot-truth.md` | A | No |
| `src/system/{gesture,hamobj,math,rndobj,world}/*` (12 units) | B | No |
| `src/system/ui/PanelDir.cpp`, `native/tests/test_{object_lifetime,asset_loading}.cpp` | C | No |
| `docs/.../25-suite-green-census.md` (new) | C | No |
| `scripts/{certify_floor,reconcile_db,test_certify_floor}.py`, `scripts/bump-engine.sh` (new) | D | No |
| `docs/.../00-INDEX.md` | D | No |

- **No `native/CMakeLists.txt` change** (C's new tests live in already-registered
  `test_*.cpp`; D's `bump-engine.sh` writes CMakeLists only when run, not committed-changed).
- **Shared folder, distinct files:** `docs/investigations/2026-06-10-roadmap-to-100/` holds C's
  new `25-`, D's edited `00-INDEX.md`, and A's session doc lives in `docs/sessions/`. No two
  lanes edit the same doc file.

### Recommended order

1. **`wave6/d-view-tooling`** (`1f751193`) — merge first. Tooling/doc-only; disjoint from all;
   no build impact. Then run the Lane D view migration (apply runbook step 1).
2. **`wave6/c-suite-green`** (`fd65dea4`) — merge second. PanelDir.cpp is HX_NATIVE-guarded →
   `SendTransition` stays 100% normalized, PPC report.json byte-identical. Rebuild `milo-tests`,
   run the bare gate → expect **0 FAILED, EXIT 0**.
3. **`wave6/a-knee-bend`** (`795e7999`) — merge third. All HX_NATIVE / native-only → PPC
   byte-identical. Rebuild dc3-native + milo-tests; run
   `FeetNotBelowFloorDuringGameplay` → expect 0 below floor, worst toe +0.60; boot gate 2/2.
4. **`wave6/b-residual-grind`** (`29cbd22a`) — merge last (it moves real PPC percents).
   Full `ninja` rebuild regenerates report.json with the 5 qualifying wins + material gains.
   Then run apply runbook step 2 (`sync_match_percent.py --build --promote`) to repopulate
   `current_percent`/`match_percent_normalized` (GetPlayerIndexes 100%, ForeachKeyframe 82.8%,
   SetupFrame 99.6%, RndShaderDrawRect 85.7%, IsPathAcceptable 88.1%, MapVerts 93.5%).

After all four: run the rest of the DB apply runbook (reconcile, optional unicorn refresh,
certify-floor summary). Optionally bump `MILO_ENGINE_PIN` to `8fb669d` (perf-only).

---

## What blocks merging

- **NO MERGE BLOCKER.** All six lane pairs merge cleanly; no file is shared across lanes; no
  `decomp.db` write; main HEAD unchanged (`46570183`); no `Co-Authored-By`; no `git stash`.
- **ACCEPTANCE GAP to record (not a hard blocker, orchestrator decision):** **Lane B delivered
  5 qualifying wins vs the ≥8 target** (verdict `fail` on the literal count). The 5 are real and
  run_objdiff-verified against the correct main baseline; the shortfall is because the frontier
  is floor-dominated and the only path to 8 is per-callsite hacks (prohibited). **Recommend
  accept-partial-with-rationale.** The verifier's `required_fixes` (3 more functions, fix
  baseline methodology, resolve the QuatSpline measurement gap) are a *re-attempt spec*, not a
  merge blocker — the 5 real wins are net-positive and safe to land regardless.
- **BASELINE-INFLATION note (record, no action):** the Lane B impl summary's CamShot::Shake
  (+28.4) and CamShotFrame::Interp (+13.9) claims used a stale pre-wave-5 baseline; the real
  deltas are +1.4 / +0.4 (commit `3f654b92`'s own message is honest: "Shake 98.3→99.7"). Do not
  credit those as qualifying wins.
- **DEFAULT-BEHAVIOR CHANGE to flag (not a blocker):** merging Lane A makes
  `DC3_FEET_POST_PLANT` default ON — native dancers actively plant their feet (a visible,
  one-directional, revert-on-divergence heuristic plant; NOT the faithful Xbox clip-layer pose).
  Opt-out `DC3_FEET_POST_PLANT_OFF=1`.
- **OUT-OF-LANE PRE-EXISTING bugs surfaced (reported, route to owners — none block merge):**
  (a) `GameplayTelemetryTest.NoAnkleSuddenJumps` ~57u ankle delta at the YMCA move-rewind
  boundary (Lane A) → move-rewind/clip-blend owner; (b) `obj/Dir.cpp:640` `HasDirPtrs` UAF on a
  freed dir under the `ObjectLifetimeTest.*→MergeLifecycleTest.*` adjacency (Lane C) → obj/Dir
  cascade-lifetime owner; (c) `char/FileMerger::Merger::Clear` infinite loop at
  `FileMerger.cpp:73` (Lane C, the 6 gated outfit-reload tests) → char/FileMerger owner.

---

## Open follow-ups for Wave 7

1. **Faithful knee-bend (engine content-pipeline, the real Lane-A fix).** Lane A proved the
   Xbox knee bend is a clip/anim QUAT pose (`PoseMeshes` on `bone_*-knee.mesh`), not IK, and
   that the divergence is a deep-crouch clip QUAT under-bend (Push-12h footik clip-bake gap).
   The shipped fix is a heuristic post-poll analytic plant (gate-green but not pixel-faithful).
   The faithful fix is an engine clip-load/bake task so the anim itself bends the knee to −57°
   at the crouch like Xbox — deferred to an engine branch (coordinate like wave-5 lane C).
2. **Continue the open-residual grind behind the corrected census** (now 289 open after the Lane
   D view fix). Lane B took the top-class to 5 qualifying + material; the remainder is
   floor-dominated. Next: `ForeachKeyframe` decl-reorder pass (635 instr, 16 SWAPPED/8 TGT_ONLY
   slots), `RndShaderDrawRect` explicit-u64 resourceBits attempt, `IsPathAcceptable` bge↔blt
   inversion + permuter, `MapVerts` (only +6.5 from qualifying). Record certify_floor evidence
   for the certified floors (Lane B report has the strings) — DB still not written.
3. **Fix the Lane-B baseline-measurement methodology in the verify harness:** every claimed
   delta must be measured against run_objdiff on the *current main tip*, never a pre-wave commit
   state or decomp.db `current_percent`. Two of Lane B's claimed wins were inflated this way.
4. **Triage the three pre-existing bugs surfaced this wave (out-of-lane):** (a) the YMCA
   move-rewind ankle-jump (`NoAnkleSuddenJumps`); (b) the `obj/Dir.cpp:640` `HasDirPtrs` UAF
   under the ObjectLifetime→MergeLifecycle adjacency (so CI sub-filter orderings stop SIGSEGVing);
   (c) the `char/FileMerger::Merger::Clear` infinite loop (so the 6 outfit-reload tests can be
   un-gated and restore coverage).
5. **Wire the Lane-C bare-suite gate into CI** as `0 FAILED && EXIT 0` (NOT an exact PASS count —
   the PASS/SKIP split flaps with sibling builds + GPU). Run from a stable absolute path so the
   threadsafe death-test re-exec is reliable.
6. **Optional engine-pin bump to `8fb669d`** (perf: L1 vertex-unpack cache + WarmGpuForDir) via
   the new `scripts/bump-engine.sh`. No wave-6 lane required it; it's a separate perf decision.
7. **Census-doc hygiene:** doc 23 still implies the 170 db-only rows promote via `sync --promote`
   (refuted — they're normalized=0.0) and overstates their `unicorn_verdict=EQUIVALENT` count
   (17/170, not all). Update doc 23 to match the Lane D view decision; `00-INDEX.md` is already
   refreshed (Lane D).
