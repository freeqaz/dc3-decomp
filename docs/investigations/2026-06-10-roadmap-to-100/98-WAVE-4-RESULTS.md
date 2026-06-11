# 98 — Execution Wave 4 Results

**Date:** 2026-06-11. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`97-EXECUTION-WAVE-4.md`](97-EXECUTION-WAVE-4.md). **Wave-3 results:**
[`96-WAVE-3-RESULTS.md`](96-WAVE-3-RESULTS.md). **Scope:** feet/IK live-gate attack (A),
flip-list adjudication → fixes-with-tests (B), near-miss cohort + live-bug continuation (C),
suite hygiene + unicorn cadence wiring (D).

All four lanes completed in isolated worktrees. **All four passed** adversarial verdict
(each with only MINOR / documentation required-fixes — no blockers, no repair rounds needed).
**No lane committed to `main`** and **no lane wrote `decomp.db`** — the live DB mtime is
unchanged (`2026-06-11 00:53:21`) and main HEAD is still `83014e83` (the Wave-4 plan doc).
Branches are staged for the orchestrator to merge and apply.

> **Build-plane rule (still enforced):** every match-percent and verdict number below names its
> build plane. Worktree `run_objdiff` readings are claims; final certification happens on `main`
> after the sync. A worktree reading is *not* evidence about main.

---

## TL;DR headline numbers

| Metric | Value | Build plane / notes |
|---|---|---|
| **Feet gate** | **STILL FAILS by design — NOT green** | Lane A; worst toe Z **−4.20** default (L 675/696, R 648/696 below floor); divergence now PINNED + poll-order CONFIRMED |
| **Feet divergence pinned** | live toe-target **eff.z = −3.71 native vs +0.88 Xbox** at the ankle clamp | Lane A; the neutral skeleton AGREES with Xbox (−0.04 vs +0.017) — it is the animated **leg/ankle over-extension**, not a toe-channel bug |
| **Poll-order claim** | **CONFIRMED** (was UNCONFIRMED in Wave-3) | Lane A; per-frame probe: IK(ankle) runs BEFORE POSE(song.hdrv) every frame → IK write discarded |
| **Clean-plant (off by default)** | best run **L 0/692, worst toe −0.30** (~84–90% floor-penetration cut) | Lane A; nondeterministic (LP64 poll order), right-leg spikes → stays OFF; default behavior byte-identical |
| **Bugs fixed-with-tests (B+C)** | **2 host-behavior fixes with dedicated GTests** (`Rand::Seed` 6 cases, `ThreadTask::Replace` 2 cases) | + 2 PPC-match-win refactors (B) validated by objdiff, no dedicated GTest |
| **Rand::Seed** | host fix under HX_NATIVE; **PPC 91.8% unchanged** (srawi/srwi lowering floor) | Lane B worktree plane; 4/6 GTests fail pre-fix (test-first proven) |
| **PPC-match wins (B)** | `CharFeedback::Poll` **98.4% → 100.0%**; `SkeletonUpdate::UpdateFakeArmPos` **96.8% → 99.7%** | Lane B worktree plane; intermediate-store restoration |
| **Live-bug (C)** | `ThreadTask::Replace` `remove(to)` → `erase(from)` — real native no-op bug fixed | 19/32 → **25/33** equal; normalized 82.7% → 82.0% (dip is size ratio; function materially more correct) |
| **Single-blocker cohort → 100%** | **0 of 6** — target (≥4) NOT met; cohort is **genuinely exhausted (all backend floors)** | Lane C worktree plane; each diagnosed w/ asm + permuter, all confirmed floors |
| **53-set dispositioned (C)** | **10** with asm evidence (target ≥5 MET) | 6 cohort + ThreadTask + ClipCollide + RndLight::Load + CharClipDisplay + RndWind |
| **Unicorn cadence wired (D)** | `nightly_measurement_guard.sh --unicorn` proven end-to-end on a DB COPY (33.3s, 1312 fns) | Lane D; dry-run by default, `--unicorn-apply` gates writes; no decomp.db writes |
| **doc-16 re-measure (D)** | 12 non-COMPLETE rows re-measured on main; **0 movement, 0 newly promotable** | Lane D main plane; the `.c`-rebuild fix was already in main — no parsedate-style surprise |
| **milo-tests triage (D)** | **8 failures / 6 classes** triaged; 4/8 are the NullifyAllRefs cascade, all routed to dc3 Wave 5 | Lane D; doc 21 written, investigate-only (no fixes) |

**Nothing hard-blocks merge.** The one item requiring care: lanes **B and C both edit
`native/CMakeLists.txt` at the same insertion line** (after `tests/test_sha1.cpp`) → a real git
conflict, trivially resolved by keeping both test-registration lines. Confirmed by real sequential
merge (see merge-order section). Everything else merges cleanly.

---

## Per-lane outcomes

### Lane A — feet/IK toe-vs-ankle, against the live gate (Opus) — **PASS** (status: partial)

- **Branch:** `wave4/a-feet-ik-live` (commit `7d061b19`) · **Worktree:** `/home/free/code/milohax/wt-wave4-a-feet-ik-live`
- **Files (3, all HX_NATIVE-only):** `src/system/char/CharIKFoot.cpp`,
  `src/system/hamobj/HamDriver.cpp`, `src/system/hamobj/HamIKEffector.cpp`.
- **Deliverable:** the gate is **NOT green** — the bug is unfixed by design (a true fix needs Xbox/Xenia
  ground truth on the *live animated* leg pose, a months-old P0 blocker). The plan's fallback deliverable
  IS met: the divergence is PINNED with empirical evidence, the poll-order claim is CONFIRMED, and a
  measured improvement is recorded.
- **Poll-order CONFIRMED (was UNCONFIRMED per Wave-3 Push 7b):** a new per-character frame-local
  poll-sequence probe shows, for `char/main/main.milo`, the foot-plant **IK(ankle) runs BEFORE
  POSE(song.hdrv) every frame** (seq: IK,IK,POSE,IK,IK,POSE…), so the pose re-dirties the leg and the
  IK world-write is discarded — matching `HamDriver.cpp:95-101`. *But* the IK is near-inert
  (clampFactor=0); even surviving it would not plant, because `q.v = neutral(−0.04) + eff(−3.71)` is
  already sunk by the live toe-target.
- **Divergence PINNED (Lane A, live gate telemetry):** at the ankle clamp, native **neutral.z ≈ −0.04
  AGREES with Xbox (+0.017)** — the neutral skeleton, clamp formula, and `q.v=neutral+eff` blend are all
  identical to Xbox. The **single divergence is the animated toe-target `spot_L-toe.trans` world Z:
  eff/finger.z = −3.71 native vs +0.88 Xbox.** The toe is RIGID relative to the ankle (`toeLocal.z=0.00`,
  same as rest); the foot points straight down (`ankleM.x=(0,0,−1)`) and the LEG over-extends (pelvis
  42.5→32.3, ankle 4.39→0.26). So it is a **leg/ankle over-extension, NOT a toe-channel bug.**
- **Measured improvement (clean-plant, OFF by default):** hardened the existing gated-off
  `Dc3CleanPlant` 2-bone solver with a revert-on-divergence guard (snapshot leg LOCAL matrices; revert
  to anim pose on NaN or >0.25 below baseline → strict improve-or-no-op). With clean-plant ON it cuts
  below-floor samples ~84–90% (best run **L 0/692, worst toe −0.30**; worst run L 63–106/~690,
  R 113–168/~690) but is **nondeterministic (LP64 poll order)** with residual right-leg move-boundary
  spikes to ~−12 → stays **OFF**. Default native behavior is byte-identical to baseline.
- **PPC neutrality (Lane A worktree plane, run_objdiff — verifier-confirmed):** `DoFSM@CharIKFoot`
  **97.4%** (r29↔r30 regalloc floor), `Poll@HamDriver` **97.4%** (address-relocation floor),
  `Poll@HamIKEffector` **99.9%** (stack-shift + 2 commutative fmuls floor) — all **unchanged** at
  documented floors. All edits are inside `#ifdef HX_NATIVE` (verifier confirmed by grep) → PPC
  neutrality is structural, not just claimed.
- **Do-not-break gates GREEN:** 18/18 wave-3 regression suite (Sha1/Dxt5Alpha/HamAudioCrossfade/
  EnableDetector/SoundSynthPoll) before AND after; gameplay boot EXIT=0, screen=game_screen,
  state=playing, worldLoaded=1, venuePresent=1 over 5000 frames.
- **Contradictions (Lane A corrected prior docs):**
  - The Wave-3/4 "toe-vs-ankle (the toe sinks)" framing is **imprecise** — the toe is RIGID relative
    to the ankle (`toeLocal.z=0.00`); it is the **ankle/leg that sits ~4.6u too low** and the rigid
    down-pointing foot drags the toe below floor. The reverify doc's "leg/ankle over-extension, not the
    toe channel" conclusion is the correct one.
  - The `HamDriver.cpp:95-101` poll-order claim was UNCONFIRMED (Push 7b walked it back); now CONFIRMED
    empirically — **but it is not the sinker on its own** (IK near-inert; eff already at −3.71).
  - The "neutral skeleton collapses onto the live sunk pose" framing is half the story: the native
    neutral AGREES with Xbox; the lone divergence is the **live animated foot** (eff), not the neutral.
- **Risks:** the gate is unfixed; a true fix requires live-leg Xbox/Xenia ground truth (P0 blocker —
  Xenia bone-world read offset-bugged + async-stall). The clean-plant improvement is nondeterministic
  with residual right-leg feedback spikes (the plant's modified locals persist and bias the next frame)
  → do **NOT** enable by default (it would make worst-case worse and is non-reproducible).
- **Verdict required-fixes:** MINOR only — (1) commit message says "46/48 GameplayTelemetryTest pass"
  but the standalone count is **47/48** (`AutomationReachesMultiUserScreen` fails only in chained runs
  due to a single-boot-per-process artifact); (2) one stale `ZlibLicense.obj` in the worktree should be
  cleaned before merge objdiff re-checks (unrelated to the measured fns); (3) INFORMATIONAL — "asm
  evidence" was delivered at the runtime-telemetry level (IK-chain trace at the clamp), not a raw PPC
  diff of CharIKFoot. None block merge.

### Lane B — flip-list adjudication → fixes with tests (Opus) — **PASS** (status: complete)

- **Branch:** `wave4/b-fliplist-fixes` (commit `bae0b574`) · **Worktree:** `/home/free/code/milohax/wt-wave4-b-fliplist-fixes`
- **Files (7):** `src/system/math/Rand.cpp`, `src/system/hamobj/CharFeedback.cpp`,
  `src/system/gesture/SkeletonUpdate.cpp`, `native/tests/test_rand_seed.cpp` (new, 6 cases),
  `native/CMakeLists.txt`, `scripts/unicorn/refresh_frontier.py`,
  `docs/.../22-fliplist-adjudication.md` (new).
- **Acceptance met (with one methodology caveat):** 10 rows adjudicated with asm-grounded evidence
  (3 real-fixed, 3 false/floor, 3 call_arg-noise samples, 1 deferred `CharEyes::Enter`); 3 real bugs
  fixed including `Rand::Seed`; adjudication table committed (doc 22).
- **`Rand::Seed` (host fix under HX_NATIVE):** logical-shift mask `((j>>16)&0xFFFF)` fixes signed-srawi
  MT-state high-word poison on the native host; the PPC path keeps the original `(j>>16)`. **PPC
  byte-identical at 91.8% (worktree plane)** — the srawi/srwi divergence is a genuine compiler-lowering
  floor, unreachable from any C++ spelling (verified by 7 hand forms + permuter). **6 GTest cases**
  (canonical Int() draw sequences for seeds {0x29A,1,12345,−1} + no-0xFFFF-high-word invariant +
  determinism); **4/6 FAIL on the pre-fix buggy form** (test-first proven; verifier re-ran 6/6 PASS).
- **`CharFeedback::Poll` — real PPC-match win (worktree plane):** `Clamp<float>(0,1,x)` assignment →
  in-place `ClampEq(limb.unk8,0,1)` restores the target's intermediate store; **98.4% → 100.0%
  normalized** (67 instructions all equal, verifier-confirmed). Behaviorally identical Min(Max()).
- **`SkeletonUpdate::UpdateFakeArmPos` — real PPC-match win (worktree plane):** fold the negate result
  directly into `unk5398` + clamp reads the member, restoring the missing intermediate store; **96.8%
  → 99.7% normalized** (1 residual commutative-fmuls backend floor, verifier-confirmed).
- **Tooling: degenerate-fixture auto-classifier (`refresh_frontier.py`):** added
  `is_degenerate_fixture_diff()` + a `fixture_artifact_degenerate` re-tag so zero-fill NaN/−0.0/inf
  object_memory flips (vs orig 0) classify as artifact rather than candidate_bug — fires on
  CharFeedback/SkeletonUpdate, correctly SPARES `Rand::Seed` (orig value ≠ 0).
- **Contradictions (Lane B — the structurally important finding):**
  - The flip-list framing treats **object_memory as the strongest candidate-bug signal** — but **5 of
    7** object_memory/unmapped rows examined are **zero-fill FIXTURE ARTIFACTS** (div-by-zero NaN,
    signed-zero negation, null deref), not behavioral bugs. The asm, not the class, finds the bug — the
    same CSHA1 lesson from Wave 3.
  - The acceptance criterion **"confirm verdict flips to EQUIVALENT" is structurally unmeetable** for
    this class of fix and was met **0/3**, which is the HONEST finding, not a miss: (1) the unicorn
    runner emulates PPC `.text`, so **HX_NATIVE fixes (Rand::Seed) are invisible** — PPC bytes +
    source-hash unchanged → verdict cannot flip by construction; (2) **fixture-artifact rows diverge
    identically before and after a 100%-match fix** (CharFeedback at 100% normalized still tests
    DIVERGENT because `delta/mFadeSecs=NaN` under the zero-filled fixture). A 100%-matching function
    whose verdict won't flip is definitive proof the verdict is a fixture artifact.
  - The lane note "prefer a shared-source fix when it also improves PPC match" for `Rand::Seed` does
    **not apply** — every behaviorally-correct unsigned spelling DROPS the match to 86.5–89.2% (compiler
    fuses to rlwimi or emits srawi); only the HX_NATIVE guard keeps 91.8%. The wave-2 `Rand::Int`
    HX_NATIVE pattern is the correct choice, as the lane note's fallback anticipated.
- **Risks:** `Rand::Seed` stays DIVERGENT in unicorn (real PPC-side srawi bug that is a genuine
  compiler-lowering floor) — it will keep appearing in the flip-list as a real (non-artifact)
  divergence; this is correct/honest, do NOT "fix" it to 100% PPC (proven unreachable). The CharFeedback
  / SkeletonUpdate fixes are behaviorally-identical refactors validated by objdiff + unicorn final-value
  equivalence, **not by a dedicated GTest** (accessing internals needs the private-public hack). The
  degenerate-fixture rule is conservative (fires only when ALL object_diffs are NaN/−0.0/inf vs orig 0);
  a real bug producing only degenerate-FP outputs would be mis-tagged, but is indistinguishable under
  the zero-fill probe anyway. `CharEyes::Enter` (object_memory, 92.6%) was adjudicated as likely-same
  intermediate-store cascade but NOT fixed (89-instruction store-scheduling function, high cost) — a
  follow-up that may hold a real bug of the CharFeedback/SkeletonUpdate family.
- **Verdict required-fixes:** MINOR/doc only — (1) report says "1 pre-existing FAIL
  (MeshVertexLoading.CompressedSkinning)" but there are **2** pre-existing MeshVertexLoading failures
  (both pre-existing on main, no regression introduced); (2) record in this results doc that the
  "verdicts confirmed flipped" criterion required a methodology clarification (0/3 flips is structurally
  valid — see contradictions above); (3) only `Rand::Seed` has dedicated GTests — note that
  CharFeedback/SkeletonUpdate are objdiff-validated only. None block merge.

### Lane C — near-miss cohort + live-bug continuation (Opus) — **PASS** (status: partial)

- **Branch:** `wave4/c-nearmiss-livebugs` (commit `e72de2ae`) · **Worktree:** `/home/free/code/milohax/wt-wave4-c-nearmiss-livebugs`
- **Files (4):** `src/system/obj/Task.cpp`, `src/system/obj/Object.h`,
  `native/tests/test_threadtask_replace.cpp` (new, 2 cases), `native/CMakeLists.txt`.
- **Live-bug RESOLVED — `ThreadTask::Replace` (obj/Task):** the executing-task branch called
  `mObjects.remove(to)` where the Xbox target calls `ObjPtrList::erase` on the **`from`** node. Fixed
  to erase the iterator wrapping `from` (befriended `ThreadTask` to `ObjPtrList` so its private `Node`
  is nameable — no layout/API change). This is a **real native bug**: `remove(to)` was a no-op (`to` is
  the new object, not in the list), leaving the stale `from` ref dangling. **2 GTest cases** (both FAIL
  against the old `remove(to)`, PASS after — test-first verified; verifier re-ran 2/2 PASS). Match rose
  **19/32 → 25/33** equal; normalized **82.7% → 82.0%** (worktree plane; the small dip is the
  diff_score/size ratio — the function is materially more correct; residual is the condition
  mask-fusion-vs-branch backend floor the permuter could not reach). Intent resolved purely from the
  Xbox asm (Ghidra/pyghidra-mcp unavailable; RB2 DWARF empty; RB3 uses a different signature; og-dc3
  copied the same buggy `remove(to)`). Milo Engine match% unchanged at **76.92%** — the Object.h friend
  change has zero regression (full PPC rebuild clean).
- **Single-blocker cohort — target NOT met (0 of 6 to 100%), cohort genuinely EXHAUSTED:** all six were
  re-measured on a fresh worktree (ninja warmup + `clean_stale_objects.sh`) and are **genuine backend
  floors, none reachable** (Lane C worktree plane): `HttpReqCurl::WriteMemoryCallback` **99.8%** (load
  scheduling + commutative add), `CharIKRod::Copy` **99.3%** (commutative addi/subi), `IdentityInfo::Identified`
  **99.6%** (switch range-fusion, 3 forms tried), `CharIKHead::Poll` **99.7%** (volatile-FPR + commutative
  fmuls), `CheatsManager::CallCheatScript` **99.5%** (stack-temp-slot allocation), `PropSync` **99.8%**
  (FPR; permuter = 0 improvement). Each diagnosed with asm evidence + hand-edits + permuter, all
  confirming the floor.
- **53-set dispositioned (target ≥5 MET — 10 with evidence):** the 6 cohort + ThreadTask::Replace +
  4 more: `ClipCollide::Collide` **99.9%** (stack-layout), `RndLight::Load` **99.7%** (redundant-branch
  codegen + normalized-invisible `SetObjConcrete<RndLight>` vs `<RndDrawable>` ICF template-type diff),
  `CharClipDisplay::SetStartEnd` **98.3%** (f12↔f13 volatile-FPR), `RndWind::SelfGetWind` **84.6%**
  (FPR+stack cascade). All `diff_op:none` (no logic/branch/compare divergence) — cosmetic, not behavioral.
- **Contradictions (Lane C):**
  - Doc-16/plan listed the 6-fn cohort as LikelyFixable/MaybeFixable **implying reachability to 100%**;
    on a fresh worktree **all six are genuine backend floors** — the labels overstate routability. The
    diagnoses also correct the doc's *guessed* causes (CheatsManager is stack-temp allocation, not the
    "extra temp variable" guess). Recommend **re-tiering doc 16 to floor-cert** for this cohort.
  - Doc-11 named `CharClipDisplay::SetStartEnd` as object_memory and `RndLight::Load` as error
    (real-bug classes); per-function asm shows **`diff_op:none` for both** — neither is a behavioral bug
    in the executed path; the unicorn real-bug class labels are fixture artifacts for these two
    (consistent with doc-11 F2's "real-bug classes are noisy" caveat).
  - Wave-3 deferred ThreadTask::Replace pending DWARF/Ghidra intent — **DWARF was not needed**; the
    erase-vs-remove intent was resolved purely from the Xbox asm (target calls `erase` on the `from`
    node masked by `(Parent==&mObjects)&&from`).
- **Risks:** ThreadTask::Replace residual at 82.0% is the condition mask-fusion (target produces the
  iterator value branchlessly) vs our cmplw/bne branch — a backend codegen choice the permuter could not
  reach; the fix is behaviorally correct and the erase call site matches; **do NOT promote to COMPLETE.**
  `Object.h friend class ThreadTask;` is a wide-blast-radius header change (Object.h is included nearly
  everywhere) — verified clean (full rebuild, Milo Engine 76.92% unchanged; a friend decl grants access
  only, no layout impact), but the orchestrator should re-confirm the full report regenerates identically
  post-merge. `test_threadtask_replace.cpp` builds a ThreadTask shell via placement-new and calls
  `Replace` NON-virtually — setup-fragile if member offsets change (assertions are
  representation-independent). The "≥4 units to 100%" target is unachievable from this cohort — if
  hard-required it must be re-sourced from a different (non-floor) band.
- **Verdict required-fixes:** MINOR/doc only — (1) the "95 PASSED, 2 FAILED" core-suite figure is from a
  narrower filter than the 10 suites listed (the broader run is 123 PASSED, 2 FAILED — same 2
  pre-existing failures; conclusion unchanged); (2) the second pre-existing failure is in
  `MergeScopeParityUnitTest` (report wrote `MergeScopeParityTest`). No code fix required.

### Lane D — suite hygiene + cadence wiring (Sonnet) — **PASS** (status: complete)

- **Branch:** `wave4/d-hygiene-cadence` (commit `5887693f`) · **Worktree:** `/home/free/code/milohax/wt-wave4-d-hygiene-cadence`
- **Files (3, no source edits):** `scripts/nightly_measurement_guard.sh`,
  `docs/.../16-single-blocker-recert.md` (appended WAVE-4 RECONCILIATION), `docs/.../21-milo-tests-triage.md` (new).
- **Item 1 — unicorn cadence wired:** `nightly_measurement_guard.sh` gains a `--unicorn` stage (layer c)
  with the 4-step cadence: `refresh_frontier.py --run` → `apply_refresh.py --only-fresh-source` →
  `reconcile_db.py --fix` → `certify_floor.py --apply`. **Dry-run by default** (temp DB copy via mktemp,
  cleaned up); `--unicorn-apply` gates live writes. Proven **end-to-end against a DB COPY: 33.3s, 1312
  fns / 414 units**; exit 1 (correct — 1 candidate_bug flip detected). `--only-fresh-source` already
  existed in `apply_refresh.py` (skips rows whose unicorn_source_hash matches) — no addition needed.
  Verifier confirmed `git --git-common-dir` resolution so worktrees use the canonical main decomp.db.
- **Item 2 — doc-16 re-measure (main plane, sequential run_objdiff):** all 12 non-COMPLETE single-blocker
  rows re-measured, **all unchanged vs Wave-3, 0 newly promotable units** (parsedate 99.8%, HttpReqCurl
  99.8%, PropSync 99.8%, Rnd_NG 99.6%, FxSendChorus 99.9%, DataEventList 99.9%, HamSongMgr 99.9%,
  CharIKRod 99.3%, IdentityInfo 99.6%, CharIKHead 99.7%, CharBone 99.7%, CallCheatScript 99.5%). The
  `.c`-rebuild fix was already in main before this measurement → **no parsedate-style surprise**; COMPLETE
  total stays 8/20.
- **Item 3 — milo-tests triage (doc 21, investigate-only):** all 8 pre-existing failures (6 distinct
  classes) triaged with root-cause hypotheses, evidence pointers, route, and wave: MergeLifecycleTest
  cascade ×2, ObjectLifetime MergeDirs, MergeScopeParityUnitTest flatten, MeshVertexLoading skinning bswap,
  RndCamProjection YRatio/GPU, AssetLoading 400s timeout. **4/8 share the NullifyAllRefs cascade root
  cause; all 8 route to the dc3 repo and are assigned Wave 5.**
- **Contradictions (Lane D):**
  - Wave-3's "8 pre-existing failures" is correct **counting individual GTest cases**; the triage finds
    **6 distinct failure classes** covering them (MergeLifecycleTest = 2 tests / 1 class, etc.).
  - The Wave-4 plan's "likely the NullifyAllRefs cascade" attribution is **partially correct — 4 of 8 are
    cascade-caused.** Failure 3 (MergeScopeParityUnitTest) has a DIFFERENT root cause (flatten-pass
    over-flattening `kMergeReplace` subdirs), Failure 4 (MeshVertexLoading) is LP64/LE bswap missing in
    vertex unpack, Failure 5 (RndCamProjection) is GPU-init/YRatio aspect setup — none of those three is
    the cascade. The blanket "likely the cascade" was overstated.
  - The Wave-4 plan speculated some doc-16 rows "may now be 100" (the parsedate lesson) — **confirmed NO
    movement**; the `.c`-rebuild fix was already incorporated into main before the measurement.
- **Risks:** the `--unicorn` dry-run detected **1 candidate_bug flip** (different from the 57 in the
  main-plane flip-list at 83014e83) — should be adjudicated (Lane-B style) **before** `--unicorn-apply`
  writes it; it may be a new divergence introduced since the Wave-3 merge or a signal-version artifact.
  The guard's refresh step has **no timeout** and uses default `--jobs` (cpu count) → 33s may be longer
  on CI/low-CPU. The **`--unicorn-apply` write path is untested in this lane** (dry-run only per global
  rules) — the orchestrator must test it on main with `--apply` after `sync_match_percent.py`.
  MergeLifecycle/ObjectLifetime remain pre-existing failures (cascade, assigned Wave 5; no fix applied).
- **Verdict required-fixes:** none.

---

## Consolidated decomp.db apply runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches. In Wave 4,
**NO lane wrote `decomp.db` and NO lane produced new unicorn evidence to apply** (Lane B/C source fixes
move `match_percent_normalized` only; Lane D only *wired* the cadence and ran it against a DB copy). So
the DB apply is the **sync + reconcile** path plus an **optional fresh unicorn refresh** the orchestrator
runs itself to pick up the moved percents and Lane B's new degenerate-fixture classifier. Run from repo
root on `main`.

```bash
# 0. Merge the branches first (see merge-order section), resolving the B/C
#    native/CMakeLists.txt conflict (keep BOTH test-registration lines). Then:

# 1. Make match_percent_normalized current AND certify the new wins.
#    Promotes Lane B's CharFeedback::Poll (98.4 -> 100 normalized) to COMPLETE; UpdateFakeArmPos
#    stays partial at 99.7. ThreadTask::Replace is a host/behavioral win at 82.0% — do NOT promote.
#    Rand::Seed PPC is byte-identical (host fix under HX_NATIVE) — no percent movement.
#    reconcile check (a) may flag CharFeedback drift on the next run — EXPECTED and benign.
python3 scripts/sync_match_percent.py --build --promote --demote

# 2. Clear residual db-only stale stubs (harmless if already clean).
python3 scripts/reconcile_db.py --fix

# 3. (RECOMMENDED, orchestrator-run) Fresh unicorn refresh on main to pick up the moved percents
#    AND Lane B's new fixture_artifact_degenerate classifier. This is exactly the cadence Lane D
#    wired; run it via the guard or directly. Dry-run FIRST, inspect any new candidate_bug flips.
bash scripts/nightly_measurement_guard.sh --unicorn                 # dry-run (temp DB copy, ~33s)
#    Lane D's dry-run already surfaced 1 NEW candidate_bug flip not in the 57-row main flip-list.
#    ADJUDICATE that flip (Lane-B method) BEFORE applying — it may be a real new divergence or a
#    signal-version artifact. Do NOT blind-apply.
bash scripts/nightly_measurement_guard.sh --unicorn --unicorn-apply # live writes (after adjudication)

# 4. Confirm + record.
python3 scripts/reconcile_db.py            # expect check (e) drift = 0
python3 scripts/certify_floor.py --summary
```

**Notes / lane-specific steps:**
- **No `apply_refresh.py --apply` of a worktree results DB is required this wave** (unlike Wave 3) — no
  lane handed off a frontier results DB; Lane D's run was a dry-run against a copy and Lane B's fixes
  flow through `sync` (percent) + the orchestrator's own refresh (verdicts).
- **Promote-list:** `CharFeedback::Poll` → COMPLETE (normalized 100). **No-promote list:**
  `SkeletonUpdate::UpdateFakeArmPos` (99.7), `ThreadTask::Replace` (82.0, behavioral win not a PPC-100
  win), `Rand::Seed` (host-only; PPC stays a documented 91.8% srawi lowering floor — leave DIVERGENT in
  unicorn, it is honest), and Lane C's 6 cohort + 4 extra dispositions (all backend floors — certify-floor
  candidates, NOT routable to 100; do NOT write the DB for them from any lane).
- **Cadence is now self-documenting:** `nightly_measurement_guard.sh --unicorn` is the wired re-run
  (refresh → apply_refresh --only-fresh-source → reconcile --fix → certify --apply). Use it after any sync
  that moves percents. Lane B's classifier means CharFeedback/SkeletonUpdate reclassify candidate_bug →
  `fixture_artifact_degenerate` on the next refresh (expected, not a regression); `Rand::Seed` stays a
  real `memory_mismatch` DIVERGENT.

---

## Merge order for `wave4/*` branches (with cross-lane conflict check)

`git diff --name-only main..wave4/<lane>` and a **real sequential merge simulation** (throwaway
worktree, A→B→C→D) were run. **There is exactly ONE real git conflict** — lanes **B and C both insert
at the same line of `native/CMakeLists.txt`**, immediately after `tests/test_sha1.cpp` (B adds
`tests/test_rand_seed.cpp`; C adds `tests/test_threadtask_replace.cpp`). `git merge-tree` on B vs C
reports `CONFLICT (content) in native/CMakeLists.txt`, and the sequential merge confirms it fires when
**C is merged after B**. **Resolution is trivial — keep BOTH lines** (union the inserts). All other paths
are disjoint.

Cross-lane file map (only `native/CMakeLists.txt` is multiply-touched):

| File | Lanes | Conflict? |
|---|---|---|
| `native/CMakeLists.txt` | B, C | **YES** — adjacent test-registration inserts; keep both lines |
| `src/system/hamobj/*` | A (HamDriver, HamIKEffector), B (CharFeedback) | No — distinct files |
| `src/system/char/CharIKFoot.cpp` | A | No |
| `src/system/math/Rand.cpp`, `gesture/SkeletonUpdate.cpp` | B | No |
| `src/system/obj/{Task.cpp,Object.h}` | C | No |
| `scripts/unicorn/refresh_frontier.py` | B | No (D wires it but does not edit it) |
| `scripts/nightly_measurement_guard.sh` | D | No |
| `docs/.../16-…md` (append), `21-…md` (new) | D | No |
| `docs/.../22-…md` (new) | B | No |

**Semantic interaction (NOT a git conflict, but order-relevant):** Lane D's `--unicorn` guard stage
*calls* Lane B's modified `refresh_frontier.py`. **Merge B before D** so the cadence runs with the new
`fixture_artifact_degenerate` classifier in place (otherwise the first post-merge refresh would surface
CharFeedback/SkeletonUpdate as candidate_bug again). No code conflict either way; this is purely so the
orchestrator's first refresh behaves as Lane B intended.

**Doc numbering:** no collision — D writes `16-` (append) + `21-` (new), B writes `22-` (new). All
distinct, monotonic. (Wave-3's `19-`/`20-` collision is already resolved on main.)

Recommended order:

1. **`wave4/a-feet-ik-live`** (`7d061b19`) — merge first. HX_NATIVE-only IK diagnostics + the off-by-default
   clean-plant guard. Disjoint from all others. Build post-merge with
   `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DDawn_DIR=/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn
   -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++`. Clean `ZlibLicense.obj`
   staleness note from the verdict is worktree-local — no action needed on main.
2. **`wave4/b-fliplist-fixes`** (`bae0b574`) — merge second (before D, see semantic note). Lands the
   Rand::Seed host fix + the two PPC-match wins + the `refresh_frontier.py` classifier. `ninja` rebuilds
   Rand/CharFeedback/SkeletonUpdate objects; run `milo-tests --gtest_filter='RandSeed.*'` from
   `orig-assets` → expect 6/6.
3. **`wave4/c-nearmiss-livebugs`** (`e72de2ae`) — merge after B. **Resolve the `native/CMakeLists.txt`
   conflict here** (keep both B's `test_rand_seed.cpp` and C's `test_threadtask_replace.cpp` lines). Then
   full `ninja` (Object.h friend has wide blast-radius — verify Milo Engine 76.92% unchanged) +
   `milo-tests --gtest_filter='ThreadTaskReplaceTest.*'` from `orig-assets` → expect 2/2.
4. **`wave4/d-hygiene-cadence`** (`5887693f`) — merge last (doc + script only, disjoint). Then validate the
   wired cadence: `bash scripts/nightly_measurement_guard.sh --unicorn` (dry-run) — confirm it runs and
   note the 1 new candidate_bug flip for adjudication before any `--unicorn-apply`.

A/B/C/D are otherwise git-independent. After all four: run the DB apply runbook (sync first, then the
orchestrator's own unicorn refresh).

---

## What blocks merging

**Nothing hard-blocks merge.** All four lanes pass. The care items:

- **CROSS-LANE CONFLICT (must resolve, trivial):** lanes **B and C** both edit `native/CMakeLists.txt`
  at the same insertion line. Resolve by keeping both test-registration lines when merging C after B.
  Confirmed by real sequential merge — this is the one item that cannot be a clean auto-merge.
- **ORDER RULE (recommended):** merge **B before D** so Lane D's `--unicorn` cadence runs with Lane B's
  `fixture_artifact_degenerate` classifier present (semantic, not a git conflict).
- **DB-apply ordering (must follow):** `sync_match_percent.py` runs first; the optional unicorn refresh
  runs after. **The 1 new candidate_bug flip surfaced by Lane D's dry-run must be adjudicated BEFORE any
  `--unicorn-apply`** — do not blind-write it to decomp.db.
- **DO-NOT-PROMOTE (must respect):** `ThreadTask::Replace` (82.0%, behavioral win not PPC-100),
  `Rand::Seed` (host-only; PPC stays the documented 91.8% srawi lowering floor — leave DIVERGENT), and
  Lane C's 6-fn cohort + 4 extra dispositions (all backend floors). Only `CharFeedback::Poll` (→100) is
  promotable from this wave.
- **Acceptance gaps to record (not blockers):** Lane C's "≥4 single-blocker units to 100%" was **0/6**
  (cohort genuinely exhausted — all backend floors); Lane B's "confirm verdict flips to EQUIVALENT" was
  **0/3** (structurally unmeetable for HX_NATIVE + fixture-artifact rows — see Lane B contradictions).
  Both are honest findings backed by independent verification, but the criteria as written are unmet.
- **MINOR doc/text fixes (apply with the merge, non-blocking):** Lane A commit-message test count
  (46/48 → 47/48 in isolation); Lane B "1 pre-existing MeshVertexLoading FAIL" → 2; Lane C
  `MergeScopeParityTest` → `MergeScopeParityUnitTest` and the "95 PASSED" narrower-filter figure.

---

## Open follow-ups for Wave 5

1. **Feet/IK still unfixed — needs LIVE-leg Xbox/Xenia ground truth.** Lane A pinned the divergence to
   the **live animated toe-target/leg over-extension** (eff.z −3.71 native vs +0.88 Xbox; neutral AGREES
   with Xbox) and CONFIRMED the poll-order (IK before pose → write discarded). The open question is
   whether **Xbox's leg over-extends the same way** (relying on a surviving plant) **or its anim already
   plants the foot** — answerable only with Xenia bone-world ground truth on the live pose (the months-old
   P0 blocker: Xenia bone-world read offset-bugged + async-stall). The off-by-default clean-plant
   (`DC3_FEET_PLANT_FIX` + `DC3_FEET_CLEAN_PLANT`) gets to L 0/692 best-case but is nondeterministic with
   right-leg feedback spikes — a full fix likely needs the plant to re-pose from a guaranteed-clean anim
   pose each frame before solving.
2. **Adjudicate the 1 NEW unicorn candidate_bug flip** surfaced by Lane D's `--unicorn` dry-run (not in
   the 57-row main flip-list at 83014e83). Determine if it is a real new divergence introduced since the
   Wave-3 merge or a signal-version artifact, BEFORE `--unicorn-apply` writes it.
3. **`CharEyes::Enter` (object_memory, 92.6%)** — Lane B adjudicated it as a likely intermediate-store
   cascade of the CharFeedback/SkeletonUpdate family but deferred it (89-instruction store-scheduling
   function, high cost). It may hold a real intermediate-store bug.
4. **Re-tier doc 16's single-blocker cohort to floor-cert.** Lane C proved all 6 LikelyFixable/MaybeFixable
   rows (`HttpReqCurl::WriteMemoryCallback`, `CharIKRod::Copy`, `IdentityInfo::Identified`,
   `CharIKHead::Poll`, `CheatsManager::CallCheatScript`, `PropSync`) are genuine backend floors (none
   reachable to 100). The "≥4 units to 100%" lever from this cohort is exhausted — the doc-16 labels
   overstate routability and should be re-tiered; any future "units to 100%" target must be re-sourced
   from a different (non-floor) band.
5. **Triage Wave 5 (dc3 repo) — the 8 milo-tests failures (doc 21).** 4/8 are the **~ObjectDir
   NullifyAllRefs cascade** (MergeLifecycle ×2, ObjectLifetime MergeDirs, the 400s AssetLoading timeout —
   the cascade hang). The other classes are distinct: MergeScopeParityUnitTest = flatten-pass
   over-flattening `kMergeReplace` subdirs; MeshVertexLoading = LP64/LE bswap missing in vertex unpack;
   RndCamProjection = GPU-init/YRatio aspect setup. All route to dc3, all assigned Wave 5. Clear these
   before the suite is a trustworthy full-ctest gate.
6. **Continue the 53-set live-bug burndown (Lane C method).** Per-function asm + native-semantics
   diagnosis (not the name list) keeps finding real isolatable bugs (`ThreadTask::Replace` this wave,
   `CSHA1::Transform` in Wave 3). The unexamined remainder of the doc-11 53-fn set likely holds a few
   more; ~10 are now dispositioned with evidence.
7. **Test gaps for the two Lane-B PPC-match refactors.** `CharFeedback::Poll` and
   `SkeletonUpdate::UpdateFakeArmPos` are validated by objdiff (100 / 99.7) + unicorn final-value
   equivalence but lack dedicated GTests (internals need the private-public hack). Add behavior-pinning
   tests if these paths regress.
8. **Add a timeout + bounded `--jobs` to the wired unicorn refresh** in `nightly_measurement_guard.sh`
   (Lane D risk): the 33s refresh has no timeout and uses cpu-count jobs — fragile on CI/low-CPU. Also
   **test the `--unicorn-apply` write path on main** (Lane D only dry-ran it per global rules).
9. **Attack the genuine open residual** behind the now-fresh `authorable_done` view, prioritizing the
   routable/real-bug classes over certified cosmetic floors.
