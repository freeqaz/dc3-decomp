# 99 — Execution Wave 5 Results

**Date:** 2026-06-11. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`99-EXECUTION-WAVE-5.md`](99-EXECUTION-WAVE-5.md). **Wave-4 results:**
[`98-WAVE-4-RESULTS.md`](98-WAVE-4-RESULTS.md). **Scope:** feet/IK leg over-extension
root cause (A), `~ObjectDir` NullifyAllRefs cascade (B), flip-list continuation +
vertex-unpack bswap (C), suite burndown remainder + open-residual census (D).

All four lanes ran in isolated worktrees. **All four passed** adversarial verdict.
Lane D needed one repair round (census numbers + test-reproducibility) and is now
clean. **No lane committed to `main`** and **no lane wrote `decomp.db`** — main HEAD is
still `00e5895b` (the Wave-5 plan doc) and the live `decomp.db` mtime is unchanged
(`2026-06-11 02:09:52`). Branches are staged for the orchestrator to merge and apply.

> **Build-plane rule (still enforced):** every match-percent and verdict number below
> names its build plane. Worktree `run_objdiff` readings are *claims*; final
> certification happens on `main` after the sync. A worktree reading is not evidence
> about main.

> **⚠ THE ONE REAL MERGE BLOCKER:** lanes **B and D independently fixed the SAME defining
> test** (`MergeScopeParityUnitTest.NonProxyPipelinePreservesNestedReplaceSubdirScope`)
> with **two different rewrites of `~ObjectDir`'s survivor logic in `src/system/obj/Dir.cpp`**.
> `git merge-tree` reports a genuine **content conflict** there. This is NOT a "keep both
> lines" union — it is two competing implementations of one fix. **Take Lane B's `Dir.cpp`
> (it supersedes D), keep Lane D's `Cam.cpp`.** Full reconciliation in the merge-order
> section. Everything else is disjoint and merges cleanly.

---

## TL;DR headline numbers

| Metric | Value | Build plane / notes |
|---|---|---|
| **Feet gate** | **STILL FAILS by design — NOT green** | Lane A; worst toe Z **−4.2 / −4.1** (L ~725/745, R ~696/745 below floor, poll-order noisy); mechanism now NAMED with fresh evidence |
| **Feet over-extension mechanism (NAMED)** | leg foot-plant **IK is INERT on native**: `.ikfoot` loads `mMoveElbow=false` → `shoulderParent=0` (IKElbow never bends the knee) **and** drops the knee dep from `PollDeps` → IK sorts before the pose | Lane A; native pelvis world Z **35.2** (rest 42.51), foot `ankleM.x=(0,0,−1)` straight down, knee bend ABSENT |
| **Xbox ground truth (frame-matched)** | knee rotZ **−58°** planted (toe ~0.01) vs native pure-anim **−20°** (toe sunk −3.8) | Lane A; Xbox = anim −20° + ~−38° of surviving foot-plant IK flex; native gets the anim only |
| **Principled IK path (`DC3_FEET_PLANT_FIX=1`)** | **DIVERGES — worse than baseline**: L-toe **+21.3** (flung up), R-toe **−24.9** | Lane A; bone-frame mismatch, not a poll-order bug → forcing `mMoveElbow=true` does NOT plant on native |
| **Poll-order "race" lever** | **REFUTED — does not exist**: the per-group sort is ALREADY name-deterministic (`AlphaSort`/`strcmp`, no ties) | Lane A; the LP64 nondeterminism is NOT in this sort; the real fix is engine-side |
| **NullifyAllRefs cascade (Lane B)** | **2 real defining failures FIXED + 1 new regression test** (suite 57→59 pass) | Lane B worktree plane; the "4 cascade + 8 MergeLifecycle" failures were ALREADY fixed by `d41f5bf7` on this branch |
| **The real residual bug (Lane B)** | survivor predicate was **NON-TRANSITIVE**: a nested subdir whose only DirPtr owner is inside the cascade tree got nullified, severing the survivor's link to descendants | Lane B; fixed with a transitive survivor-closure in `~ObjectDir` (HX_NATIVE) |
| **Mis-authored oracle (Lane B)** | `MergeDirsMoveAllSubdirsTransfersOwnership` asserted an immediate source-side erase the engine never had; Xbox `kReplace` is **APPEND-ONLY** (`AppendSubDir`, no erase — DC3 asm 98.5%, RB3 byte-identical) | Lane B; corrected to the real invariant |
| **Vertex-unpack bswap (Lane C — REAL BUG FIXED)** | compressed-vertex unpack **truncated every BE float position to ~0**: `cv.mPosX/Y/Z` (`float`) passed to `UnpackFloat_BE(int)` → implicit float→int truncation | Lane C; root-fixed in the **shared engine** (`milo-native-engine`), endian-agnostic `LoadBE32`, all 7 `__builtin_bswap32` removed |
| **MeshVertexLoading** | **5/7 → 7/7 GREEN** (2 fixed + 2 new regression tests) | Lane C worktree plane; also fixes real compressed-mesh rendering (positions were collapsing to origin) |
| **Flip-list adjudication (Lane C)** | **30/30 priority rows** adjudicated (19 cap_exhausted_decomp + 11 cap_exhausted_orig; target ≥15), **0 real PPC bugs** — all codegen/fixture floors | Lane C worktree plane; the one experiment (OnAddSink `≥4→>3`) REGRESSED 96.1%→81.9% and was reverted |
| **RndCamProjection YRatio (Lane D)** | `projMtx.y.y` read `mLocalProjectXfm.v.x` (always-zero translation) instead of `m.z.y` (vertical FOV factor) → **RndCamProjectionTest 4 fixed (10/10)** | Lane D worktree plane; HX_NATIVE-adjacent (line 472 is NOT in `#ifdef HX_NATIVE` but the function is at a 60% regswap floor — PPC unchanged) |
| **Open-residual census (Lane D, corrected)** | **459 fns / 213,648 bytes** open (DB-verified; was wrongly 396/266,136); **170 fns / 18,800 B** are COMPLETE+EQUIVALENT promotion artifacts (run `sync_match_percent.py`); cap_exhausted family **178 fns / 145,748 B** dominates | Lane D; doc `23-open-residual-census.md` rewritten from live DB rows |

**One real merge blocker:** the **B/D `Dir.cpp` content conflict** (competing `~ObjectDir`
fixes). Resolution: take B's `Dir.cpp`, keep D's `Cam.cpp`. Confirmed by `git merge-tree`
(see merge-order section). No CMakeLists conflict this wave (new tests live in
already-registered files). A/B share no source files.

---

## Per-lane outcomes

### Lane A — feet/IK leg over-extension root cause (Opus) — **PASS** (status: partial)

- **Branch:** `wave5/a-feet-overextension` (commit `fde0a685`) · **Worktree:** `/home/free/code/milohax/wt-wave5-a-feet-overextension`
- **Files (1, doc-only):** appended a WAVE-5 LANE A section to
  `docs/sessions/2026-06-09-xenia-xbox-foot-truth.md`. **Zero source diff** — all IK
  experiment code (`DC3_FEET_PLANT_FIX`, `DC3_FEET_CLEAN_PLANT`) is pre-existing wave-4
  code under `#ifdef HX_NATIVE`; the wave-5 margin experiment was A/B-refuted and reverted.
- **Deliverable:** the gate is **NOT green** (the real fix is invasive cross-component
  engine work, out of the dc3-src lane scope). The plan's **fallback deliverable IS met**:
  the over-extension mechanism is named with fresh independent evidence, worst-toe
  before/after is recorded, and the IK-before-pose-discard architectural decision is made
  and justified.
- **Root cause (NAMED, fresh data):** the leg foot-plant **IK is INERT on native**. The
  leg `*.ikfoot` loads `mMoveElbow=false`, which (a) zeroes `shoulderParent` in
  `CharIKHand::Poll` (`CharIKHand.cpp:354-355`) so IKElbow never bends the knee (confirmed
  by a `kneeRotZ=-999` sentinel — the knee bone is null where the bend would log), and (b)
  drops the knee/thigh dependency from `CharIKHand::PollDeps` (`line 250`, gated on
  `mMoveElbow`) so the topological sort places the IK before the skeleton pose. Result:
  native pelvis at world Z **35.2** with a straight, down-pointing leg
  (`FootGeom ankleM.x=(0,0,−1)`) puts the ankle at the floor and drags the rigid foot's
  toe to −4 (`toeLocal` vertical offset is 0 → the drop is pure ankle world rotation).
  Frame-matched Xbox (Push 9) renders the knee at **−58°** = anim −20° + ~−38° of surviving
  foot-plant IK flex; native's anim alone is **−20°**.
- **Architectural decision on the IK-before-pose discard (the plan's key ask):** there is
  **NO clean deterministic poll-order lever** within acceptable risk. (1) The per-CharPollGroup
  order is **ALREADY name-deterministic** — `CharPollableSorter::Sort`/`AlphaSort` sorts the
  dep vector by unique bone **NAME** (`strcmp`, no ties; confirmed in `CharPollable.h:22-26`),
  and the final insertion iterates that name-sorted vector. The `std::map<Object*>` pointer
  keying only affects edge *discovery*, not output order, so the LP64 nondeterminism is **not
  in this sort.** (2) The principled `mMoveElbow=true` in-engine IK **DIVERGES** on native
  (reproduced below) because IKElbow's pure-Z thigh rotation bends down off the Xbox rest
  frame but ~horizontal/up off the native composed frame — a **bone-frame mismatch**, not a
  poll-order bug. (3) The clean-plant residual is the anim's **intended deep root-crouch**
  (pelvis 35→25 *after* the plant baked the pelvis-relative leg), not a within-frame race;
  a plant-time margin sweep showed NO trend and was reverted.
- **Measured (Lane A worktree dc3-native plane, cwd `orig-assets/`):**
  - feet gate baseline (default OFF) worst toe **L −4.2 / R −4.1**; L ~725/745 below, R
    ~696/745 below (poll-order noisy run-to-run).
  - `DC3_FEET_PLANT_FIX=1` (principled mMoveElbow IK): **DIVERGES** — L-toe **+21.3**,
    R-toe **−24.9** (worse than baseline; reproduces wave-4 Push 13).
  - `DC3_FEET_PLANT_FIX=1 DC3_FEET_CLEAN_PLANT=1`: bounded (worst toe −4.0, no fly-up) but
    L 95–150 / R 147–193 below floor — improve-or-bounded, **not 0**, poll-order noisy.
  - margin sweep (`DC3_FEET_PLANT_MARGIN` 0.6/1.6/2.5/3.5, 2 runs each): **no trend** —
    over-lift lever REFUTED, reverted.
- **PPC neutrality (Lane A worktree plane, run_objdiff — zero source diff):** `DoFSM@CharIKFoot`
  **97.4%**, `Poll@HamDriver` **97.4%**, `Poll@HamIKEffector` **99.9%** — all unchanged at
  documented floors (HX_NATIVE-guarded IK code never reaches the matched build).
- **Do-not-break gates GREEN:** 32/32 wave-2/3/4 regression suite before AND after; gameplay
  boot reaches `game_screen` `state=playing` (`GameplayEntersPlayingState` PASSED in 34.4s).
- **Contradictions (Lane A corrected prior docs):**
  - The wave-5 plan's hope to "find WHERE the order is established and pin it deterministically
    rather than papering with clean-plant" presumes a within-frame poll-order race a sort key
    could fix. **That lever does not exist:** the per-group sort is already name-deterministic;
    the principled IK path diverges on the native bone frame; the residual is the anim's
    intended cross-pass root-crouch. The deterministic fix is **engine-side**.
  - The wave-4 `CharIKFoot.cpp` comment (lines 34–38) frames the fix as "force `mMoveElbow=true`
    + IK sorts last" as if that plants the foot. **Reproduced this wave: it DIVERGES** (L +21.3,
    R −24.9). The `mMoveElbow` path is NOT a working plant on native.
  - The session-doc Push-7/7b "IK before pose (poll-order) is the resolution" — confirmed the
    within-group order is deterministic and the discard is real but **secondary**; the dominant
    sinker is the inert IK + the anim's own root-crouch, not a sort-order race.
- **Risks:** the gate stays RED by design; the real fix is engine-side (deterministic
  post-poll plant-after-final-root-crouch hook + an in-engine IK bone-frame fix) and out of
  dc3-src scope. Do NOT enable any opt-in plant by default: `DC3_FEET_PLANT_FIX=1` is WORSE
  than baseline; `DC3_FEET_CLEAN_PLANT` is poll-order noisy. The clean-plant fail count is
  genuinely nondeterministic run-to-run — any future "gate-green" claim must be averaged over
  multiple runs. **Build/run note:** dc3-native must run with cwd = **main repo** `orig-assets/`
  (the worktree's `orig-assets` reflink lacks `gen/main_xbox.hdr` → cores at archive load).
- **Verdict required-fixes:** none.

### Lane B — ~ObjectDir NullifyAllRefs cascade (Opus) — **PASS** (status: complete)

- **Branch:** `wave5/b-nullifyallrefs` (commit `cafbd23d`) · **Worktree:** `/home/free/code/milohax/wt-wave5-b-nullifyallrefs`
- **Files (3):** `src/system/obj/Dir.cpp`, `native/tests/test_object_lifetime.cpp`,
  `native/tests/test_merge_lifecycle.cpp`.
- **CRITICAL CONTRADICTION with triage doc 21 / memory `project_objdir_cascade_bug.md`:** the
  "4 cascade failures + 8 MergeLifecycle failures" they describe were **ALREADY FIXED** by
  commit `d41f5bf7` (HasExternalDirPtrs/ShouldSkipCascadeNullify/DetachFromDir hardening).
  All 8 MergeLifecycleTest synthetic cascade tests and both memory-note defining tests
  (`SubdirsSurviveSourceDirDeletion`, `MergedObjectsSurviveParentDirReload`) **already pass**
  on this branch. The **actual** remaining failures were just **2**.
- **The 2 real residuals fixed:**
  1. `MergeScopeParityUnitTest.NonProxyPipelinePreservesNestedReplaceSubdirScope` — a real
     deeper bug: the survivor predicate was **NON-TRANSITIVE**, so a nested subdir whose only
     ObjDirPtr owner sits inside the cascade tree (e.g. a `kMergeReplace`'d dir's own nested
     subdir) looked internal-only and got nullified, severing the survivor's link to its
     descendants (confirmed by a diagnostic: `survivor->SubDirs()[0]` became null after delete
     source). **ROOT FIX:** a transitive survivor-closure in `~ObjectDir` (HX_NATIVE-only,
     `CollectSurvivorClosure()` + `IsSurvivor()`) that skips the entire surviving subtree from
     nullification/detach.
  2. `ObjectLifetimeTest.MergeDirsMoveAllSubdirsTransfersOwnership` — a **MIS-AUTHORED parity
     oracle** asserting an immediate source-side subdir erase the engine never had. Xbox ground
     truth: `MergeObjectsRecurse` case `kReplace` is **APPEND-ONLY** (`toDir->AppendSubDir(fromDir)`,
     NO source erase) — DC3 asm 98.5% with only regalloc diffs, RB3 reference byte-identical
     (`src/system/obj/Utl.cpp:250`). Corrected the oracle to assert the real invariant (subdir
     survives in target after source deletion).
- **New regression test:** `MergeLifecycleTest.ReparentedSubdirSubtreeSurvivesSourceDeletion`
  pins the transitive-survivor fix at the root (no flatten-pass helper dependency).
- **Measured (Lane B worktree plane):**
  - merge/lifetime/scope targeted suite **59/59 pass** (was 57/2-fail).
  - `~ObjectDir` PPC match **100.0% normalized** (full build, 140 instructions all equal) —
    all cascade edits are inside `#ifdef HX_NATIVE`.
  - `MergeObjectsRecurse` PPC match **98.5% normalized** (Utl.cpp untouched; confirms Xbox
    `kReplace` is append-only — the ground truth the corrected oracle relies on).
  - core suite (no AssetLoading, no flaky death test): **291 pass / 5 fail** — the 5 are Lane C
    `MeshVertexLoading.CompressedSkinning` (pre-existing on B's binary) + 4× Lane D
    `RndCamProjectionTest`, NOT introduced by this change.
  - boot gate **47/48 pass**, `game_screen` reached, EXIT=0 (1 fail = Lane A feet-by-design).
- **Verifier independent baseline confirmation:** both defining tests FAIL on main, both PASS
  on wave5-B. A freshly-built main-source binary shows 6 failures (2 Lane B + 4 RndCam);
  wave5-B shows 5 (0 Lane B + 1 pre-existing MeshCpu + 4 RndCam) → improvement is exactly
  **−2 Lane B failures**.
- **Contradictions (Lane B):**
  - Triage doc 21 / memory `project_objdir_cascade_bug.md`'s "8 MergeLifecycleTest failures +
    4 cascade failures kill reparented objects" is **REFUTED** — those were already green via
    `d41f5bf7`; the real residual was 2 tests (nested transitive case + mis-authored oracle).
    The plan's "target: 6 fewer failures" overcounts; realistic delta is **+2 real fixes + 1
    new regression test**.
  - Triage doc 21 Failure 3 "RunNativeFlattenPass over-flattens" / "mirrors FileMerger.cpp
    post-merge flatten pass": `FileMerger::FinishLoading` has **NO flatten pass** (it just calls
    `MergeDirs`); `RunNativeFlattenPass` is a TEST-ONLY helper. The object was killed by the
    cascade BEFORE the flatten pass could see it — fixing the cascade fixed it; **no
    FileMerger.cpp change needed.**
  - Triage doc 21 Failure 6 "400s AssetLoading timeout from cascade ring-walk" — **not observed.**
    `LoadFullVenueWorlds` loads 8 venues in **2.7s**; the suite-level slowness is a pre-existing
    order-dependent `Movie.cpp:220` `IsInitialized()` assert, unrelated to the cascade.
  - Plan note "determine whether the fix belongs in dc3 `src/system/obj/` or the engine": the
    fix is **entirely in dc3 `src/system/obj/Dir.cpp`** (HX_NATIVE block). **No
    `milo-native-engine` change is needed** — the cascade and its DirPtr ref-count machinery
    live in dc3's own Dir.cpp/Dir.h.
- **Risks:** the transitive closure is O(survivors × subtree) — fine for shallow reparent sets
  (LoadFullVenueWorlds, 5056 objects, still 2.7s) but worth noting if a future profile shows
  cascade-teardown hot. The `MergeDirsMoveAllSubdirs` oracle was **CORRECTED** (its old
  expectation was wrong about Xbox) — a future reader trusting the old name literally
  ("TransfersOwnership" → immediate erase) could re-introduce a non-Xbox `RemoveSubDir` that
  would regress the 98.5% PPC match AND break the real pipeline; the inline comment + commit
  message document why the engine is append-only. The death test
  `ObjectLifetimeUnitTest.CascadeDeleteNamedSubdirsNullsExternalDirPtrsWithoutCrash` hangs when
  run LATE in the full process (gtest fork() with 7 live WebGPU/audio threads — pre-existing
  harness flakiness) but PASSES in 3ms in isolation.
- **Verdict required-fixes:** none.

### Lane C — flip-list continuation + vertex-unpack bswap (Opus) — **PASS** (status: complete)

- **Branches:** `wave5/c-fliplist-bswap` (dc3 commit `9a1038c5`) **+** `wave5/vertex-unpack-bswap`
  (milo-native-engine commit `f75339a`) · **Worktree:** `/home/free/code/milohax/wt-wave5-c-fliplist-bswap`
- **Files (dc3, 3):** `native/src/gfx/VertexFormats.cpp` (orphan mirror, not in any build
  target), `native/tests/test_mesh_loading.cpp`,
  `docs/investigations/2026-06-10-roadmap-to-100/24-fliplist-continuation-vertex-bswap.md` (new).
  **+ engine (1):** `milo-native-engine/src/gfx/VertexFormats.cpp` (the REAL loader, linked via
  `libmilo-engine.a`).
- **PART 2 — REAL BUG FIXED:** the native compressed-vertex unpack path **truncated every
  big-endian float position to ~0**. `UnpackCompressedVertices`/`UnpackCompressedSkinnedVertices`
  reinterpret-cast the raw Xbox blob as `CompressedVertex_Xbox*` and passed `cv.mPosX/Y/Z`
  (declared `float`) to `UnpackFloat_BE(int)`; the float member read as a native LE word is a
  tiny denormal that the implicit float→int conversion truncated to 0. **Root-fixed in the
  SHARED ENGINE** (the dc3 `native/src/gfx/VertexFormats.cpp` copy is an orphan not in any build
  target). The fix reads every 32-bit field from the raw byte buffer as a host-endian word
  assembled MSB-first (`LoadBE32`, byte-by-byte — correct on **any** host endianness, not
  x86-special-cased; the web/WASM build shares this file) and removes all 7 `__builtin_bswap32`
  + the struct type-pun. Also fixed the test serializer (`SerializeCompressedVertexBE` cast
  float pos to int) and added 2 regression tests. **MeshVertexLoading 5/7 → 7/7 GREEN.** Also
  fixes real compressed-mesh rendering (positions were collapsing to origin in `MeshGpuCache`).
- **PART 1 — flip-list (30/30 priority rows, target ≥15):** 19 cap_exhausted_decomp + 11
  cap_exhausted_orig, all adjudicated with asm-grounded `run_diff_inspect diagnose` on the
  worktree plane. 28 are `diff_op:none` register/scheduling/FPR/save-restore floors; the only 2
  `diff_op` rows — `OnAddSink` (ble/blt) and `DingoJob::AddContent` (addi/subi) — were each run
  to ground as compiler artifacts (a comparison-fusion symptom inside a regswap cascade that
  REGRESSES on the `≥4→>3` rewrite; and a `__FILE__`/`MakeString` template-instantiation length
  floor). **0 real PPC bugs found.** No PPC source edits landed (the one experiment regressed
  and was reverted); PPC match held for every unit.
- **Measured (Lane C worktree plane):**
  - MeshVertexLoading **5/7 → 7/7** (2 fixed + 2 new tests).
  - wave-2/3/4 regression suite **37/37 PASS**.
  - boot gate 2/2 (`EngineReachesGameScreen` + `GameplayEntersPlayingState`).
  - `OnAddSink` **96.1% normalized** (post-revert; the `≥4→>3` experiment was **81.9%** = worse).
  - engine `LoadBE32` verified mathematically correct (`p[0]<<24|p[1]<<16|p[2]<<8|p[3]`); the
    compiler emits a native `bswap %eax` — correct optimization of the endian-agnostic pattern,
    NOT the old truncation bug.
- **Contradictions (Lane C):**
  - Doc 21 Failure 4 attributed MeshVertexLoading to a missing bswap on the **packed
    weight/index** fields — the ACTUAL root cause is the **float-typed POSITION** members
    (`mPosX/Y/Z`) truncated to int through `UnpackFloat_BE(int)`; the integer packed fields were
    already decoded correctly. The bswap framing was directionally right (LP64/LE) but mislocated
    the field.
  - Doc 21 / doc 98 imply a single bswap site. It is actually **TWO bugs**: the engine loader
    (real, ships) AND a mirror bug in the test's own `SerializeCompressedVertexBE` helper.
  - The plan (Lane C item 2) says to fix the loader "gated under HX_NATIVE". The fix is
    unconditional in the engine's native-only compressed-unpack path (there is no PPC plane to
    guard in the engine repo) — **no HX_NATIVE guard is needed or applicable**.
  - `query_functions`/`refresh_frontier.py:324-327` label `cap_exhausted_decomp` as a "Real bug"
    class and `cap_exhausted_orig` as artifact; asm reality: **BOTH are fixture/codegen floors
    here** (30/30, 0 real bugs). The decomp/orig distinction does NOT predict real-vs-artifact —
    `diff_op` does, and even the 2 `diff_op` rows were artifacts. The classifier over-reports by
    30 on this dataset → gate should be `diff_op>0` AND a surviving branch-polarity/comparison
    source experiment (doc 24 recommendation).
- **Risks:** the vertex bswap fix lives in the **SHARED engine repo** — it only takes effect in
  CI/web after `wave5/vertex-unpack-bswap` merges to engine main AND `MILO_ENGINE_PIN` is bumped.
  The dc3 orphan mirror (`native/src/gfx/VertexFormats.cpp`) is in sync but is NOT in any build
  target → it alone does nothing; do not rely on it. The `__FILE__`/`MakeString` floor
  (`DingoJob::AddContent`) and the `OnAddSink` ble/blt comparison-fusion floor will keep
  re-appearing as cap_exhausted rows — honest/unfixable; do not chase them (OnAddSink rewrite
  proven to regress 96.1%→81.9%).
- **Verdict required-fixes:** none. (The engine branch is 1 commit ahead of engine main, not yet
  merged, and `MILO_ENGINE_PIN` is stale — an EXPECTED pending state, an orchestrator apply-step,
  not a lane defect.)

### Lane D — suite burndown remainder + open-residual census (Sonnet) — **PASS** (status: complete, repaired)

- **Branch:** `wave5/d-suite-residual` (commits `a16912fc` source + `0e33d5f4` doc-repair) ·
  **Worktree:** `/home/free/code/milohax/wt-wave5-d-suite-residual`
- **Files (3):** `src/system/rndobj/Cam.cpp`, `src/system/obj/Dir.cpp`,
  `docs/investigations/2026-06-10-roadmap-to-100/23-open-residual-census.md`.
- **YRatio fix (real, ships, RndCamProjectionTest 10/10):** `RndCam::GetViewProjectXfms` set
  `projMtx.y.y` from `mLocalProjectXfm.v.x` (the always-zero translation vector) instead of
  `mLocalProjectXfm.m.z.y` (the vertical FOV factor: `−1/tan(yFov/2)` perspective, `−1/ratio`
  ortho). Fixes 4 RndCamProjectionTest cases. **PPC neutrality:** `GetViewProjectXfms`
  **60.0% normalized** before and after (the Xbox path uses precomputed `mWorldProjectXfm`, not
  this formula; the function is at a register-swap floor that dominates the mismatches).
- **Cascade fix (Dir.cpp, `ComputeSurvivors`):** an iterative-fixpoint survivor propagation that
  also targets `NonProxyPipelinePreservesNestedReplaceSubdirScope` — **the SAME defining test
  Lane B fixed**, via a different rewrite (see the merge blocker). D's version leaves
  `MergeDirsMoveAllSubdirsTransfersOwnership` failing (only B's lane corrected that oracle).
- **Census (corrected in the `0e33d5f4` repair round):** doc 23 rewritten to real DB numbers —
  **459 fns / 213,648 bytes** open (`SELECT COUNT(*),SUM(size) FROM authorable_done WHERE
  done_state='open'`; the original report's 396/266,136 was wrong and the plan's 459 estimate
  was right). Class histogram, top-20-by-bytes, top-units, and routable-candidate tables all
  regenerated from live DB rows. **Fabricated `Curl_http_readwrite_headers`/`Curl_proxyCONNECT`/
  `GameEndedDataPointJob` rows removed** (zero curl fns exist in the open set — DB-confirmed). A
  genuine high-leverage finding replaced them: **170 fns / 18,800 B** carry verdict=COMPLETE +
  unicorn=EQUIVALENT + current_percent=100 but NULL `match_percent_normalized` → spuriously
  "open"; running `sync_match_percent.py` promotes them out for **zero decomp work**. The
  cap_exhausted family (**178 fns / 145,748 bytes**) is the dominant residual class.
- **Test-run reproducibility (documented with real output):** the binary has **412** tests and
  never completes bare — it SIGSEGVs at `AssetLoadingTest.LoadDirectorSubdir` (pre-existing
  state leak that only fires after the full AssetLoadingTest suite; passes in isolation) and
  hangs on the `ObjectLifetimeUnitTest.CascadeDeleteNamedSubdirsNullsExternalDirPtrsWithoutCrash`
  death test (fork in 7-thread context). The reproducible filtered run (excluding only those two)
  yields **323 PASSED / 85 SKIPPED / 2 FAILED of 410 run** (exit 1). The earlier "290 PASSED / 2
  FAILED" was not reproducible. Both remaining failures
  (`MergeDirsMoveAllSubdirsTransfersOwnership`, `CompressedSkinningMatchesCpuSkinningForSyntheticBones`)
  are pre-existing — they fail identically on the main binary in isolation.
- **Top-5 open units by bytes (verifier-confirmed vs DB):** rndobj/Text 13580/15, rndobj/Shader
  8296/11, world/Spotlight 7920/6, rndobj/Part 6296/3, world/CameraShot 5232/4.
- **Contradictions (Lane D):**
  - The original report's census numbers (396/266,136) were materially wrong — the DB returns
    459/213,648; the plan estimate and the DB agree at 459, so no downward revision is supported.
  - The original top-20 / "promote" recommendations cited Curl functions that **do not exist** in
    the open set — fabricated, removed.
  - The original "290 PASSED / 2 FAILED" was not reproducible (the binary has 412 tests; a bare
    run never completes). The real reproducible figure is 323/85/2 of 410 run.
  - Doc 21 named the failing flatten-pass test `NonProxyPipelinePreservesReplaceSubdirScope`, but
    the actual failing test was the **nested** variant `NonProxyPipelinePreservesNestedReplaceSubdirScope`
    (the non-nested variant already passed).
- **Risks:** the AssetLoadingTest state-leak SIGSEGV + the death-test fork hang keep any bare
  `milo-tests` from completing until triaged (Wave 6) — consumers must use the documented
  `--gtest_filter` exclusion. The 170 promotion artifacts depend on `sync_match_percent.py`
  populating `match_percent_normalized` correctly — run the sync `--build` to be safe.
  `ComputeSurvivors` is O(n³) in cascade depth — fine for shallow cascade dirs but flag if
  cascade trees grow large. **(If B's Dir.cpp is taken at merge, D's `ComputeSurvivors` is
  dropped — see merge blocker — so this risk is moot.)**
- **Verdict required-fixes:** 3 MINOR (all git-history/accuracy notes, no source change): (1) the
  `a16912fc` commit message's "289→290 PASSED / 8→2 FAILED" figures are internally inconsistent
  and reference the old 396 census — corrected in `0e33d5f4`; the message is a permanent record
  artifact, no merge action. (2) the 323/85/2 count requires a specific filter string (~5–10 min
  runtime incl. DtaFlow/HeadlessBoot); the verifier reproduced 314/67/2 with a faster subset and
  the same 2 failures — consistent. (3) the commit calls `Cam.cpp`'s `projMtx.y.y` fix "HX_NATIVE"
  but line 472 is **NOT** inside `#ifdef HX_NATIVE` — it affects PPC compilation; PPC match did
  not regress (60.0% both sides, register-swap floor) so the fix is fine, but the "HX_NATIVE"
  claim in the commit message is a documentation inaccuracy. None block merge.

---

## Consolidated decomp.db apply runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches. As in
Wave 4, **NO lane wrote `decomp.db`** and **NO lane produced new unicorn evidence to apply**.
All four lanes' source effects are either HX_NATIVE-guarded (PPC bytes byte-identical →
`match_percent_normalized` does not move) or test/doc/engine-only. So the DB apply is the
**sync + reconcile** path plus an **optional fresh unicorn refresh** the orchestrator runs
itself, plus the **170-fn promotion sweep** Lane D surfaced. Run from repo root on `main`
**after** merging (and resolving the B/D `Dir.cpp` conflict).

```bash
# 0. Merge the branches first (see merge-order section). The ONE conflict is B vs D in
#    src/system/obj/Dir.cpp — TAKE B's Dir.cpp, keep D's Cam.cpp. Then:

# 1. Make match_percent_normalized current AND clear Lane D's 170 promotion artifacts
#    (COMPLETE + EQUIVALENT + current_percent=100 + normalized NULL -> ~18.8K bytes out of
#    the open set for ZERO decomp work). No lane's source fix moves a PPC percent:
#      - A: zero source diff (doc-only).
#      - B: ~ObjectDir stays 100.0% normalized (HX_NATIVE-only); Utl.cpp untouched (98.5%).
#      - C: engine + orphan-mirror + tests only; no dc3 PPC plane touched.
#      - D: Cam.cpp GetViewProjectXfms stays 60.0% (regswap floor); Dir.cpp HX_NATIVE.
#    So --promote here is for the 170 artifacts, not for new wins.
python3 scripts/sync_match_percent.py --build --promote

# 2. Clear residual db-only stale stubs (harmless if already clean).
python3 scripts/reconcile_db.py --fix

# 3. (RECOMMENDED, orchestrator-run) Fresh unicorn refresh on main via the wired cadence
#    (Lane D, Wave 4). Dry-run FIRST, inspect any new candidate_bug flips before applying.
#    Lane C's flip-list adjudication (doc 24) found 0 real bugs in the 30 cap_exhausted rows;
#    expect them to stay DIVERGENT (honest codegen floors), NOT candidate_bug-worthy.
bash scripts/nightly_measurement_guard.sh --unicorn                 # dry-run (temp DB copy)
bash scripts/nightly_measurement_guard.sh --unicorn --unicorn-apply # live writes (after adjudication)

# 4. Confirm + record.
python3 scripts/reconcile_db.py            # expect check (e) drift = 0
python3 scripts/certify_floor.py --summary
```

**Notes / lane-specific steps:**
- **No `apply_refresh.py --apply` of a worktree results DB is required** — no lane handed off a
  frontier results DB. Lane C wrote only a worktree-local `unicorn_refresh_c.db` (not for apply).
- **Promote-list:** the **170 COMPLETE+EQUIVALENT artifacts** (Lane D finding) → `sync --promote`
  clears them. **No-promote / no-write list:** Lane A's feet code (zero diff), Lane B's
  `~ObjectDir` (100% already, HX_NATIVE), Lane C's flip-list cap_exhausted rows (codegen floors —
  certify-floor candidates, NOT routable; the 30 stay DIVERGENT/honest), Lane D's Cam/Dir (no
  percent movement). Do NOT write the DB for the 30 flip-list rows from any lane.
- **Engine pin bump (Lane C, separate repo):** after `wave5/vertex-unpack-bswap` (`f75339a`)
  merges to milo-native-engine main, run `scripts/bump-engine.sh` to set `MILO_ENGINE_PIN` in
  `native/CMakeLists.txt` (currently `8282103`, stale) so CI/web pick up the vertex-unpack fix.
  The pin is SOFT (warns, never fails) so dc3 builds either way; the dc3 orphan mirror carries the
  same fix in the interim but is not in any build target.

---

## Merge order for `wave5/*` branches (with cross-lane conflict check)

`git diff --name-only main..wave5/<lane>` and pairwise `git merge-tree --write-tree` were run
from main HEAD `00e5895b` (all four branches descend from it). **There is exactly ONE real git
conflict** — and unlike Wave 4 it is NOT a trivial union.

### THE conflict: B and D both rewrite `~ObjectDir` in `src/system/obj/Dir.cpp`

`git merge-tree --write-tree wave5/b-nullifyallrefs wave5/d-suite-residual` → **exit 1,
`CONFLICT (content): Merge conflict in src/system/obj/Dir.cpp`**. Both lanes independently
diagnosed the SAME defining failure (`NonProxyPipelinePreservesNestedReplaceSubdirScope` — a
nested subdir under a surviving parent being wrongly nullified) and wrote **two different fixes**:

| | Lane B (`CollectSurvivorClosure`) | Lane D (`ComputeSurvivors`) |
|---|---|---|
| Approach | worklist closure: seed from `HasExternalDirPtrs`, then pull in owned descendants via `SubDirs()` + `ObjDirItr` | iterative fixpoint: a survivor's SubDir DirPtrs count as "external" for its children; rewrites `HasExternalDirPtrs` away |
| `ShouldSkipCascadeNullify` | kept (extra `survivors` param not added) | signature changed to take `survivors` |
| Object-loop detach path | **also fixed** (`IsSurvivor(objAsDir, survivors) || ShouldSkipCascadeNullify(...)`) | not touched |
| `MergeDirsMoveAllSubdirsTransfersOwnership` oracle | **also corrected** (+ matching `Utl.cpp` ground truth) | **left failing** |
| PPC `~ObjectDir` | 100.0% normalized (HX_NATIVE) | 100.0% normalized (HX_NATIVE) |
| New regression test | `ReparentedSubdirSubtreeSurvivesSourceDeletion` | none |

**RESOLUTION — take Lane B's `Dir.cpp`, drop Lane D's `Dir.cpp` hunks, keep Lane D's `Cam.cpp`.**
Rationale: B is the strictly more complete cascade lane — it fixes the same nested-subdir bug,
ALSO fixes the detach path and the mis-authored `MergeDirsMoveAllSubdirs` oracle (which D leaves
failing), and adds a dedicated regression test. D's cascade fix is a redundant alternative; its
*unique* value is entirely in `Cam.cpp` (the YRatio fix) and doc 23 (the census), both disjoint
from B. Concretely: cherry-pick D's `Cam.cpp` + doc-23 hunks; take B's `Dir.cpp` +
`test_object_lifetime.cpp` + `test_merge_lifecycle.cpp` whole.

> **Verify after resolution:** with B's `Dir.cpp` on the merged tree, run
> `milo-tests --gtest_filter='MergeScopeParityUnitTest.*:ObjectLifetimeTest.*:MergeLifecycleTest.*:RndCamProjectionTest.*'`
> from `orig-assets/`. Expect: `NonProxyPipelinePreservesNestedReplaceSubdirScope` PASS (B fixes
> it), `MergeDirsMoveAllSubdirsTransfersOwnership` PASS (B's corrected oracle), all RndCamProjection
> PASS (D's Cam.cpp), `ReparentedSubdirSubtreeSurvivesSourceDeletion` PASS. If D's `Dir.cpp` is
> accidentally taken instead, `MergeDirsMoveAllSubdirsTransfersOwnership` will FAIL.

### Cross-lane file map (only `Dir.cpp` is multiply-touched)

| File | Lanes | Conflict? |
|---|---|---|
| `src/system/obj/Dir.cpp` | **B, D** | **YES** — competing `~ObjectDir` rewrites; **take B, drop D's Dir hunks** |
| `src/system/rndobj/Cam.cpp` | D | No — keep (YRatio fix) |
| `native/tests/test_object_lifetime.cpp`, `test_merge_lifecycle.cpp` | B | No |
| `native/tests/test_mesh_loading.cpp` | C | No |
| `native/src/gfx/VertexFormats.cpp` (orphan mirror) | C | No |
| `docs/sessions/2026-06-09-xenia-xbox-foot-truth.md` | A | No |
| `docs/.../24-…md` (new) | C | No |
| `docs/.../23-…md` (new) | D | No |

- **No `native/CMakeLists.txt` change this wave** — every new GTest lives inside an
  already-registered `test_*.cpp` (B in `test_object_lifetime`/`test_merge_lifecycle`, C in
  `test_mesh_loading`). The Wave-4-style CMakeLists test-registration collision does NOT recur.
- **A and B share no source files** (the prompt's char/obj concern): A is doc-only;
  `comm -12` of A's and B's changed-file sets is empty.
- **Doc numbering:** no collision — A appends a session doc, C writes `24-` (new), D writes `23-`
  (new). Distinct, monotonic.

### Recommended order

1. **`wave5/a-feet-overextension`** (`fde0a685`) — merge first. Doc-only; disjoint from all.
   Zero build/test/match impact.
2. **`wave5/b-nullifyallrefs`** (`cafbd23d`) — merge second. Lands the canonical `~ObjectDir`
   transitive-survivor fix + the corrected `MergeDirsMoveAllSubdirs` oracle + the new regression
   test. Full `ninja` rebuild (Dir.cpp is HX_NATIVE-guarded → `~ObjectDir` stays 100.0% normalized,
   PPC report.json unchanged); rebuild `milo-tests` and run the merge/lifetime/scope filter from
   `orig-assets/` → expect all pass.
3. **`wave5/d-suite-residual`** (`0e33d5f4`) — merge third, **resolving the `Dir.cpp` conflict by
   TAKING B's version** (drop D's `Dir.cpp` hunks). Keep D's `Cam.cpp` + doc-23. Rebuild
   `milo-tests`, run RndCamProjection filter → expect 10/10. Then run the full merge/scope filter
   again to confirm B's `Dir.cpp` survived the resolution (`MergeDirsMoveAllSubdirs` must PASS).
4. **`wave5/c-fliplist-bswap`** (`9a1038c5`) — merge last (dc3 side). Disjoint. **Separately:**
   merge `wave5/vertex-unpack-bswap` (`f75339a`) into milo-native-engine main, then bump
   `MILO_ENGINE_PIN`. Run `MeshVertexLoading.*` from `orig-assets/` → expect 7/7.

After all four: run the DB apply runbook (sync `--promote` for the 170 artifacts first, then the
orchestrator's own unicorn refresh).

---

## What blocks merging

- **THE BLOCKER (must resolve before merge):** the **B/D `Dir.cpp` content conflict** — two
  competing `~ObjectDir` survivor-closure rewrites for the same defining test. **Take Lane B's
  `Dir.cpp` whole; keep only Lane D's `Cam.cpp` + doc-23.** Confirmed by `git merge-tree` (exit 1).
  This cannot be a clean auto-merge or a union — taking the wrong one (D's) silently re-breaks
  `MergeDirsMoveAllSubdirsTransfersOwnership`. Verify with the filtered milo-tests run after
  resolution.
- **ENGINE-PIN apply-step (Lane C, separate repo — not a dc3-tree blocker):** the vertex-unpack
  fix only ships once `wave5/vertex-unpack-bswap` merges to engine main AND `MILO_ENGINE_PIN`
  is bumped. The dc3 orphan mirror is a no-op until then.
- **DO-NOT-PROMOTE / DO-NOT-WRITE (must respect):** Lane C's 30 flip-list cap_exhausted rows
  (codegen/fixture floors, 0 real bugs — leave DIVERGENT, certify-floor only), Lane A's feet code
  (zero diff). The ONLY promotion this wave is Lane D's **170 COMPLETE+EQUIVALENT artifacts** via
  `sync --promote`.
- **Acceptance gaps to record (not blockers):** Lane A's feet gate is **NOT green** (mechanism
  named + before/after recorded = the plan's fallback deliverable, met; the real fix is engine-side
  and deferred). Lane B's "6 fewer failures" target was an overcount (4 cascade tests were already
  green via `d41f5bf7`) → realistic, verified delta is **+2 real fixes + 1 new test**, which meets
  the spirit (the 2 *defining* failures pass).
- **MINOR doc/text notes (apply with the merge, non-blocking):** Lane D's `a16912fc` commit
  message has stale 289/290 figures and a "HX_NATIVE" mislabel of the Cam.cpp line — both
  corrected/clarified in `0e33d5f4` and this doc; no source action.

---

## Open follow-ups for Wave 6

1. **Feet/IK is an ENGINE task now, not dc3-src.** Lane A proved the within-frame poll-order
   "race" lever does not exist (the per-group sort is already name-deterministic) and the
   principled `mMoveElbow=true` IK DIVERGES on the native bone frame (L-toe +21.3). The real fix
   needs two engine-side changes: (a) a deterministic **plant-after-final-root-crouch post-World-poll
   hook** so the foot-plant is each dancer's genuine LAST world write, and (b) an **in-engine IK
   bone-frame fix** so `mMoveElbow=true` bends the leg DOWN on native (currently horizontal/up).
   The clean-plant stays the strongest opt-in foundation, default OFF. Worst toe Z baseline −4.2/−4.1.
2. **Promote the 170 COMPLETE+EQUIVALENT artifacts** (Lane D, 18.8K bytes) out of the open set via
   `python3 scripts/sync_match_percent.py --build --promote` — zero decomp work; the single
   highest-leverage Wave-6 opening action. Then doc 23 (459 fns / 213,648 bytes, cap_exhausted
   178/145,748 dominant) is the grind worklist; top units by bytes: rndobj/Text, rndobj/Shader,
   world/Spotlight, rndobj/Part, world/CameraShot.
3. **Down-rank `cap_exhausted_*` with `diff_op==0` to a fixture_artifact tag** (Lane C doc-24
   recommendation): `refresh_frontier.py:324-327` routes all cap_exhausted flips to candidate_bug,
   over-reporting by 30 on this dataset. Gate on `diff_op>0` AND a surviving branch-polarity/comparison
   source experiment. The decomp/orig side of the cap does NOT predict real-vs-artifact.
4. **Triage the two pre-existing milo-tests harness issues (doc 23 appendix):**
   `AssetLoadingTest.LoadDirectorSubdir` suite-ordering state-leak SIGSEGV (passes in isolation,
   crashes only after the full AssetLoadingTest suite), and the
   `ObjectLifetimeUnitTest.CascadeDeleteNamedSubdirsNullsExternalDirPtrsWithoutCrash` gtest death-test
   fork hang (fork in a 7-thread WebGPU/audio context). Both currently prevent a bare `milo-tests`
   run from completing. Consider single-threaded death-test style / a state-leak reset between
   AssetLoading tests.
5. **Pre-existing `Movie.cpp:220` order-dependent assert** (`TheMovieSys.IsInitialized()`) fires in
   the full AssetLoading run — a Movie-subsystem test-ordering artifact, unrelated to the cascade
   (Lane B). Route to Movie-subsystem init triage.
6. **Bump `MILO_ENGINE_PIN`** after the vertex-unpack engine branch (`f75339a`) lands canonical, so
   CI/web compressed-mesh rendering picks up the fix (Lane C). Until then meshes with compressed
   vertices render with positions collapsed to origin on web/CI.
7. **Update doc 21 / memory `project_objdir_cascade_bug.md`:** record that the "4 cascade + 8
   MergeLifecycle" failures were already fixed by `d41f5bf7`, the real residual was the nested
   transitive case + the mis-authored `MergeDirsMoveAllSubdirs` oracle, `FileMerger::FinishLoading`
   has no flatten pass, and the 400s "cascade hang" was actually the `Movie.cpp:220` assert (Lane B
   contradictions).
8. **Continue the open-residual grind** behind the now-corrected `authorable_done` census (doc 23):
   after the 170-artifact promotion, prioritize routable/real-bug classes over the certified
   cap_exhausted cosmetic floors (178 fns / 145,748 bytes that are honest codegen floors).
