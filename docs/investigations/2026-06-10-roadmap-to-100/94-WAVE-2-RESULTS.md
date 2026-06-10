# 94 — Execution Wave 2 Results

**Date:** 2026-06-10. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`93-EXECUTION-WAVE-2.md`](93-EXECUTION-WAVE-2.md). **Wave-1 results:**
[`92-WAVE-1-RESULTS.md`](92-WAVE-1-RESULTS.md). **Scope:** native boot unblock + live
stub worklist (A), floor certificates + canonical done view (B), the feet/IK bug (C),
measurement follow-through (D).

All four lanes completed in isolated worktrees. **A, B, D passed** adversarial verdict;
**C passed with status `partial`** (two of three acceptance items fully delivered; the
third is gated on Lane A's boot fix, which is the documented and expected dependency).
**No lane committed to `main` and only Lane B's apply step writes `decomp.db`** (live DB
mtime still `Jun 10 21:45`, untouched; main HEAD still `f8256e0a`). Branches are staged
for the orchestrator to merge and apply.

---

## TL;DR headline numbers

| Metric | Value | Notes |
|---|---|---|
| **Native boot** | **UNBLOCKED** to `main_screen` | was crashing in App ctor (`CameraManager::RandomizeCategory`); now boots headless attract→title→main_screen |
| **Root cause of the boot crash** | `Rand::Int(low,high)` **signed-modulo** (PPC `divw`) returns negative/OOB index | one root fix in `Rand.h` (HX_NATIVE) fixed BOTH crash #1 (RandomizeCategory) and #3 (FlowPickOne::Activate) |
| **Live `/api/stubs` worklist top entries** | OutputDebugStringA 94, `vorbis_synthesis_poll` 69, DmGetSystemInfo 1, DmMapDevkitDrive 1 (165 hits, 4 stubs, boot-only) | served live from a real boot; richer gameplay capture gated on next blocker (Sound::SynthPoll double-free) |
| **milo-tests pass (true baseline)** | **96% — 372 / 386** (was a broken 49% before the WORKING_DIRECTORY harness fix) | the stale "371/371" and the depressed "~263" were both cwd artifacts; net **−17 failures**, 0 new |
| **Floor certs certifiable today** | **970 / 1,314** authorable-partial frontier (equiv 600, artifact 246, permuter 108, ICF 16) | 843 rest on stale unicorn data (~98 d), 127 fresh, 344 no-evidence |
| **Canonical "done" view** | 92.67% fns / 80.61% bytes (matched+stubs) → **97.32% fns / 94.15% bytes** with certs | leaves **558 open fns / 287,908 bytes** genuine residual work |
| **Feet/IK bug** | **NOT FIXED** — doc-12 int-vs-float lever REFUTED with asm; mConstraints-empty confirmed FAITHFUL; gameplay gate blocked on Lane A | residual narrowed to the gameplay song-move/poll-order path (HamDriver.cpp:95-101) |
| **Single-blocker cohort** | **20** units at normalized ≥99.5% (doc-06 F7 "~71" REFUTED — used fuzzy) | 9/20 already 100% live (promotable), 7 LikelyFixable, 1 MaybeFixable, 3–4 NeedsInvestigation |

**Nothing blocks merge.** Lane C is `partial` only because its third item depends on Lane
A landing first — that is the intended ordering, not a defect. The required-fixes are two
non-blocking minor items in Lane D's helper scripts (Dawn path + objdiff-fork rebuild for
`--strict`). See per-lane and the runbook below.

---

## Per-lane outcomes

### Lane A — native boot unblock + live stub worklist (Opus) — **PASS**

- **Branch:** `wave2/a-native-boot` (commit `392c38fe`) · **Worktree:** `/home/free/code/milohax/wt-wave2-a-native-boot`
- **Files (9):** `src/system/math/Rand.h`, `src/system/char/CharBones.cpp`,
  `src/system/world/CameraManager.cpp` (comment-only), `native/CMakeLists.txt`,
  `native/src/StubTrace.{h,cpp}`, `native/src/main_native.cpp`,
  `native/tests/test_native_boot_crashes.cpp` (new, 7 tests),
  `docs/investigations/2026-06-10-roadmap-to-100/15-native-stub-worklist.md` (new).
- **Root cause (sharper than doc 93):** the App-ctor crash is NOT a generic vector OOB.
  `Rand::Int(low,high)` lowers `Int() % (high-low)` to a **signed** divide (`divw`) on PPC
  and reinterprets `Int()`'s unsigned table draw as signed, so a top-bit-set draw returns a
  **negative / out-of-range** index. Benign on the Xbox scratch heap; on the host it
  overwrites the adjacent `ObjPtrList` and SIGSEGVs in the ObjRef ring link.
  `CameraManager::RandomizeCategory` (Fisher-Yates shuffle) is crash #1; advancing the boot
  re-crashed in `FlowPickOne::Activate` (`mChildNodes[RandomInt(0,n)]`) — the **same root
  cause**, crash #3. **One** HX_NATIVE fix in `Rand.h` (fold the single draw into
  `[low,high)` with **unsigned** modulo) fixes both. `CharBones::ScaleDown` separately
  formed one-past-end bound pointers (`&mBones[mCounts[TYPE_END]]`) that hardened libstdc++
  aborts on → switched to `mBones.data()+index` (PPC-identical), scoped to `ScaleDown` only.
- **PPC neutrality (re-verified by the verifier via run_objdiff):** `RandomizeCategory`
  100.0%, `ScaleDown` 100.0%, `RandomInt` 100.0%, `ScaleAdd` 100.0% (= main),
  `RotateBy` 88.4% (= main), `FlowPickOne::Activate` 83.3% (= main). Both shared-code fixes
  are HX_NATIVE-guarded so the PPC `#else` path is byte-identical.
- **ctest harness fix (the big swing):** `gtest_discover_tests` lacked `WORKING_DIRECTORY`
  so every test ran in the build dir and couldn't load assets. Adding it took the census
  from **49% → 96% pass** (372/386). Net delta from the crash fixes alone vs the
  WORKING_DIRECTORY baseline: **−17 failures (31→14), 0 new**.
- **Stub worklist:** `dc3-native` now boots headless to `main_screen` and serves
  `/api/health` + `/api/stubs` live. Top hits: OutputDebugStringA 94, `vorbis_synthesis_poll`
  69, DmGetSystemInfo 1, DmMapDevkitDrive 1 (165 total, 4 distinct). Added
  `DC3_STUB_TRACE_DUMP` so the worklist is captured even on a downstream crash.
- **Contradictions:** doc 93/92's "generic vector OOB" → precise signed-modulo `divw`
  diagnosis; doc 93 named **two** crashes, there are **four** (crash #3 same root, crash #4
  = `Sound::SynthPoll` audio double-free, the new next blocker); doc 92's "milo-tests
  ~263 real baseline" → real baseline is **372/386 (96%)**, the low figure was the missing
  asset cwd. The ScaleDown `data()+index` transform had to be scoped to ScaleDown ONLY —
  applying it to ScaleAdd/RotateBy/RotateTo perturbed their regalloc (ScaleAdd 98.2→98.1,
  RotateBy 88.4→87.6).
- **Risks:** the full attract-screen boot still crashes downstream in **`Sound::SynthPoll`**
  (`src/system/synth/Sound.cpp:174` — `cur=*it; it++; erase(it)` erases the NEXT element /
  `erase(end())`); `/api/stubs` serves before this but the worklist stays boot-only until it
  is fixed (out of the two-crash lane scope, documented as the next blocker). One
  AssetLoading test still times out at 400s (genuine pre-existing slow load, not a
  regression). The `orig-assets` WORKING_DIRECTORY resolution falls back to a hard-coded
  main-repo path if the in-tree dir is absent (worktrees don't get `orig-assets` reflinked).
  The crash-handler stub dump allocates inside the signal handler — terminal-path only, gated
  on `DC3_STUB_TRACE_DUMP`. `Rand.h` is widely included but the change is purely under
  `#ifdef HX_NATIVE` (PPC build provably unaffected).
- **Verdict required-fixes:** none.

### Lane B — floor certificates + canonical done view (Opus) — **PASS** · *(only DB writer)*

- **Branch:** `wave2/b-floor-certs` (commit `dd3912df`) · **Worktree:** `/home/free/code/milohax/wt-wave2-b-floor-certs`
- **Files (4):** `scripts/certify_floor.py` (new, 569 ln), `scripts/reconcile_db.py`
  (modified, +45 ln additive), `scripts/test_certify_floor.py` (new, 35 assertions),
  `docs/investigations/2026-06-10-roadmap-to-100/17-floor-cert-apply-runbook.md` (new).
- **What it does:** `certify_floor.py` idempotently migrates 5 columns (`floor_certificate`,
  `floor_cert_pct`, `floor_cert_build`, `floor_cert_at`, `floor_cert_evidence`), creates the
  `authorable_done` SQL view, and certifies the authorable partial frontier from existing
  evidence. **Evidence precedence:** `equivalent` (unicorn EQUIVALENT) > `artifact:<class>`
  (build_env / regalloc / stack_layout / merged_call / merged_arg / fpr_precision /
  orig_error) > `icf_merged` (merged_symbol_count>0) > `permuter_exhausted` (attempts-table
  wall). Routable/real-bug classes (call_count / error / call_arg / object_memory /
  return_value) deliberately stay **open**. `primary_pattern` is never used (doc 08 F8 says
  stale). Stale-unicorn provenance is stored in the evidence JSON so certs can be
  invalidated/re-tested. `reconcile_db.py` gains an additive **check (e)** that invalidates a
  cert when its function's normalized percent moves off `floor_cert_pct`; `--fix` clears it.
  Dry-run is the default; `--apply` is the single-writer path.
- **Three headline numbers (frontier = 1,314 authorable partial fns at build `f8256e0a`):**
  **970 certifiable today** (equivalent 600, artifact 246, permuter_exhausted 108, icf 16);
  of those, **127 rest on fresh evidence**, **843 are blocked on stale unicorn**; **344 have
  no evidence at all.**
- **Canonical done view:** `authorable_done` moves "done" from **92.67% fns / 80.61% bytes**
  (matched+stubs) to **97.32% fns / 94.15% bytes** (with certs), leaving **558 open fns /
  287,908 bytes** as the genuine residual work. Certified bytes: 665,888.
- **DB safety (re-verified by the verifier):** all work was done against COPIES of the live
  DB inside the worktree. The live `/home/free/code/milohax/dc3-decomp/decomp.db` was never
  written — mtime unchanged `Jun 10 21:45:20`, **0 `floor_*` columns** confirmed pre and
  post. The wave-1 sync/reconcile test suite still passes after the additive edit.
- **Contradictions:** doc 08 §3's frontier "1,699 fns / 1,127,844 bytes" used fuzzy
  `current_percent` and did not filter `merged_`/`lbl_`/`fn_`/`??_` artifact symbols → the
  normalized + artifact-filtered frontier is **1,314**. Doc 08 §4's "~809 unicorn-EQUIVALENT"
  → measured **600** on the normalized frontier. Doc 08 §6's ambiguous `orig_error` is
  treated as a floor artifact (the original binary itself diverges — a target property),
  consistent with doc 08 line 162; this moved 53 fns into artifact certs. Added a **5th**
  column (`floor_cert_evidence`) beyond doc 08's 4-column proposal, required to store
  stale-unicorn provenance.
- **Risks:** **843 of 970 certs depend on unicorn data ~98 days old** (Feb–Mar 2026) — valid
  as floor SIGNALS (an unedited EQUIVALENT fn is still EQUIVALENT) and each stores
  `unicorn_stale=true` + test date, but a unicorn re-run is the proper refresh; only 127
  certs are stale-unicorn-independent. `pgo_block_sink` is in the enum but never auto-fires
  (the 361-fn PGO block-sink floor isn't a queryable DB flag). `permuter_exhausted` (108) is
  the weakest class (attempts-table `at_limit`/`stuck` wall); the attempts table has
  free-text contamination in `exit_status` but the cert keys only off clean values + the
  end-percent ceiling. **The apply MUST run after the sync** (certs gate off
  `match_percent_normalized`); reconcile check (e) catches resulting drift.
- **Verdict required-fixes:** none.

### Lane C — the feet/IK bug (Opus) — **PASS (status: partial)**

- **Branch:** `wave2/c-feet-ik` (commit `4fc6f6d2`) · **Worktree:** `/home/free/code/milohax/wt-wave2-c-feet-ik`
- **Files (1, additive doc only):** `docs/investigations/2026-06-10-roadmap-to-100/18-feet-ik-lane-c.md`. **Zero source edits, zero DB writes, zero PPC regressions.**
- **Item 1 — doc-12 F6 "int-vs-float field at 0x30/0x34" → REFUTED with asm.** Offsets
  0x30/0x34 are `Transform::v.x`/`.y` (float per RB2 DWARF, confirmed via
  `lookup_struct_offset`); the **same offsets are read with `lfs`/`stfs`** elsewhere in the
  same target fn (idx 121/127/128/150/157/172–176), so a declared-int field is impossible.
  The TGT `lwz`/`stw` at idx 91–95 is MSVC bit-copying the float member assignment
  (`tf.v.x=wt.v.x`) through a GPR, forwarded into the adjacent integer struct copy —
  behaviorally inert (moves identical 4 bytes). The full guided permuter exhausted **192
  candidates, 0 wins**: `CharIKFoot::DoFSM` is a **97.41% register-allocation/lowering floor**
  (r29↔r30 cascade + 2 commutative fmuls/fadds), NO logic divergence. **Not a feet-bug lever.**
- **Item 2 — `HamIKEffector::mConstraints` empty is FAITHFUL.** It is serialized-data-only
  (`d >> mConstraints` at `HamIKEffector.cpp:232`; no procedural fill anywhere). The prior
  Xenia ground-truth capture measured constraints=0 on all five Xbox effectors too → there
  is no native wiring gap; the populate-constraints theory stays **refuted**.
- **Item 3 — gameplay gate BLOCKED (the partial).** `FeetNotBelowFloorDuringGameplay` cannot
  reach gameplay because dc3-native SIGSEGVs during App construction in
  `CameraManager::RandomizeCategory` — **the Lane-A-owned boot crash** (reproduced on current
  main HEAD's binary too). Lane C did NOT touch `CameraManager.cpp`. Residual narrowed via
  the venue-boot-free `ClipPoseFixture` suite (**12/12 PASS**): leg-bone decode is
  rotation-only with **zero** local-translation drift and plants the foot in isolation (toe
  worldZ +1.56/+2.52/+2.70 **above** floor at deep crouch) — so the sink is NOT decode, NOT
  the DoFSM red herring, NOT mConstraints; it is the **gameplay song-move/poll-order path**
  (`HamDriver.cpp:95-101`, still UNCONFIRMED) which the boot crash blocks.
- **Contradictions:** doc 12 F6's two strongest feet-bug suspects (int-vs-float field;
  unpopulated mConstraints) are **both refuted** — same refutation family as doc-11's MemAlloc
  rationale. Corroborates `2026-06-08-feet-reverify-data.md` PUSH 6/7: decode is faithful;
  the sink is the gameplay/venue path.
- **Risks:** **the feet bug itself remains UNFIXED** — item 3 is blocked, not solved; the gate
  stays red until Lane A lands AND the gameplay poll-order residual is then attacked. The
  residual root-cause (`HamDriver.cpp:95-101`) is the prior session's leading hypothesis but
  was walked back in PUSH 7b — it needs the boot fix + empirical poll-order capture (possibly
  Xenia live ground truth, currently P0-blocked by an offset-bugged bone read). Lane C did NOT
  edit `CameraManager.cpp` despite diagnosing the exact root cause, to avoid colliding with
  Lane A.
- **Verdict required-fixes:** none. The verdict explicitly notes item 3's acceptance criterion
  permits "document precisely what improved and what residual remains" when the gate cannot
  pass yet — satisfied.

### Lane D — measurement follow-through (Sonnet) — **PASS**

- **Branch:** `wave2/d-measurement-followthrough` (commit `4cfead3f`) · **Worktree:** `/home/free/code/milohax/wt-wave2-d-measurement-followthrough`
- **Files (4):** `scripts/nightly_measurement_guard.sh` (new),
  `scripts/check_native_compiles.sh` (new),
  `docs/investigations/2026-06-10-roadmap-to-100/16-single-blocker-recert.md` (new),
  `scripts/analysis/baselines/.gitkeep` (new). **No DB writes, no main commits, no source edits.**
- **Item 1 — nightly guard:** `nightly_measurement_guard.sh` wraps `reconcile_db.py` (nonzero
  exit on drift) plus optional `--strict` mode that regenerates `report_strict.json` with the
  NameOnly objdiff fork and alerts if `genuine_wrong_target` grows. Base mode runs green
  end-to-end (exit 0, 0 drift: a=0, b=0-demotable/214-kept, c=0, report-only=0).
- **Item 2 — single-blocker recert:** doc 16 queried report.json for units with exactly one
  <100% normalized blocker at ≥99.5%; ran fresh `run_objdiff` on all **20**. Result:
  **9/20 already 100% live** (safe to promote), 7 LikelyFixable, 1 MaybeFixable (PropSync
  FPR regswap), **3–4 NeedsInvestigation** (Rnd_NG stale cache, FxSendChorus, HamSongMgr,
  CharBone). doc 06 F7's "~71" estimate is **REFUTED** (it used fuzzy, not normalized;
  fuzzy≥99.5% is 42, still below 71).
- **Item 3 — native compile smoke:** `check_native_compiles.sh` builds dc3-native +
  milo-tests (RelWithDebInfo); incremental ~80ms when clean; `wgpu-window-test` explicitly
  excluded (stale, headers moved to engine repo). Pre-merge gate against the
  Mesh.cpp/AmbientOcclusion.cpp breakage pattern.
- **Contradictions:** doc 06 F7 "~71" → actual **20** at normalized≥99.5% (fuzzy artifact).
  **`EstimateDraw`** in `default/system/rndobj/Rnd_NG`: report.json cached 99.94% normalized
  but live `run_objdiff` produced **97.6%** — a stale cache hit from the reflinked main-repo
  `.obj`; live score is authoritative, this unit is NOT near-complete without a fresh build.
- **Risks:** `check_native_compiles.sh` requires a pre-configured `native/build/` (Dawn at
  `/home/free/code/milohax/dc3-decomp-deps/dawn/`). `nightly_measurement_guard.sh --strict`
  spawns ~10k objdiff-cli at 30 workers (~90s) — not for interactive use. The 9 COMPLETE
  entries are eligible for `--promote` but were NOT promoted (read-only lane) — the
  orchestrator should ensure `--promote` runs after merge.
- **Verdict required-fixes (both MINOR, non-blocking):**
  - **`check_native_compiles.sh` omits `-DDawn_DIR`** in its cmake configure → the
    `--configure`/auto-configure fallback fails on a fresh worktree without Dawn on
    `CMAKE_PREFIX_PATH`. Add
    `-DDawn_DIR=${DAWN_DIR:-/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn}`.
    The incremental (no-configure) path is unaffected and works in the main repo.
  - **`--strict` mode is non-functional until the objdiff fork binary is rebuilt.** The
    checked-in `bin/objdiff-cli` (built May 30) predates the NameOnly `FunctionRelocDiffs`
    mode (fork commit `72b553f`, Jun 10). `--strict` exits 1 and writes no
    `report_strict.json`. Rebuild via `cd /home/free/code/milohax/objdiff && cargo build
    --release` and confirm `functionRelocDiffs=name_only` before exercising `--strict`. Base
    mode is fully functional.
  - **INFORMATIONAL:** the impl-report JSON field `single_blocker_needsinvestigation` says
    "3 of 20" but doc 16 correctly reports **4** NeedsInvestigation. The doc is authoritative;
    the JSON metadata is a typo. No code change.

---

## Consolidated decomp.db apply-steps runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches.
**Only Lane B mutates `decomp.db`** in Wave 2. Lanes A/C/D are DB-read-only (A is native
source+build+ctest; C is doc-only; D is read-only scripts/docs). The certs gate off
`match_percent_normalized`, so **Wave-1's sync must run first** (or have already run). Run
from repo root on `main`.

```bash
# 0. Merge order first (see next section), then:

# 1. Make match_percent_normalized current. Certs gate off it; running this first is
#    MANDATORY (Lane B risk: on a stale-normalized DB the certs are stale too).
#    This is the Wave-1 Lane-A sync — promote norm==100, demote COMPLETE & norm<100.
python3 scripts/sync_match_percent.py --build --promote --demote

# 2. Clear residual db-only stale stubs (Wave-1 reconcile --fix; harmless if already clean).
python3 scripts/reconcile_db.py --fix

# 3. Dry-run cert census (writes NOTHING — review before applying).
python3 scripts/certify_floor.py

# 4. THE ONLY Wave-2 decomp.db WRITER. Adds 5 floor_cert_* columns + the authorable_done
#    view + writes ~970 certs (equiv 600 / artifact 246 / permuter 108 / icf 16). Idempotent.
python3 scripts/certify_floor.py --migrate --apply

# 5. Confirm no stale certs immediately after apply: expect check (e) = 0.
python3 scripts/reconcile_db.py

# 6. Record the canonical done-view headline (92.67%/80.61% -> 97.32%/94.15%, 558 open fns).
python3 scripts/certify_floor.py --summary

# 7. (optional, after merge) promote the 9 single-blocker units Lane D found already 100% live.
#    They are eligible but were NOT auto-promoted (Lane D is read-only).
python3 scripts/sync_match_percent.py --build --promote
```

**Nightly / post-sync guard (wire, do not crontab):** after any future sync that moves
percents, run `python3 scripts/reconcile_db.py --fix` (clears stale certs via check (e))
then `python3 scripts/certify_floor.py --apply` (re-certifies from fresh evidence).
`scripts/nightly_measurement_guard.sh` (Lane D) wraps the reconcile drift check; its
`--strict` mode needs the rebuilt objdiff fork binary first (Lane D required-fix #2).

**843 of the 970 certs rest on ~98-day-old unicorn data.** They are valid floor signals and
carry `unicorn_stale=true`, but the orchestrator should decide whether to trust them as-is or
schedule a unicorn re-run before relying on the 94.15%-bytes "done" figure for planning.

---

## Merge order for `wave2/*` branches

**There are NO git file-path conflicts across the four branches** — verified with
`git diff --name-only main wave2/<lane>`: the pairwise file-set intersection (notably A∩C)
is **empty**, and no path is touched by more than one branch. Despite the plan's flag that
"lanes A and C may both touch `char/` or test files," they do **not** overlap: Lane A owns
`src/system/char/CharBones.cpp` + `native/tests/test_native_boot_crashes.cpp`; Lane C is a
**doc-only** branch (one file). No shared char/ or test file. Merge order is therefore driven
by the runbook dependency, not conflict avoidance:

1. **`wave2/a-native-boot`** (commit `392c38fe`) — merge first. Unblocks the native boot
   (`Rand.h` + `CharBones.cpp` ScaleDown + `CameraManager.cpp` comment-only) and lands the
   ctest WORKING_DIRECTORY harness fix that every native gate depends on. All shared-source
   edits are HX_NATIVE-guarded (PPC byte-unchanged). **Build post-merge with
   `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DDawn_DIR=/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++`** (Dawn path is required to configure).
2. **`wave2/c-feet-ik`** (commit `4fc6f6d2`) — merge after A (purely a logical dependency:
   Lane C's item-3 re-run needs A's boot fix; at the git level it is independent, one
   additive doc). Re-run `FeetNotBelowFloorDuringGameplay` only after A lands.
3. **`wave2/d-measurement-followthrough`** (commit `4cfead3f`) — independent, additive
   (2 scripts + 1 doc + a `.gitkeep`). Apply the two MINOR required-fixes (Dawn `-DDawn_DIR`
   in `check_native_compiles.sh`; rebuild the objdiff fork binary before using `--strict`).
4. **`wave2/b-floor-certs`** (commit `dd3912df`) — merge last among the functional lanes and
   run its DB apply step **after** the Wave-1 sync (runbook above). It is the only Wave-2
   DB-writer; its `reconcile_db.py` edit is **additive** (new check (e), gated on the
   `floor_certificate` column) and no other wave-2 branch touches `reconcile_db.py`, so there
   is no conflict.

A/B/C/D are git-independent; any order merges cleanly. The order above is the recommended one
(A first to unblock + harness; B's DB step last after the sync).

### Doc-numbering note (not a git conflict)

Lane B (`17-floor-cert-apply-runbook.md`) and Lane C (`18-feet-ik-lane-c.md`) both use the
`17-` prefix but are **distinct filenames** — no git conflict. Lane A uses `15-`, Lane D uses
`16-`. After merge, consider renumbering one of the two `17-*` docs (e.g. Lane C → `18-`) so
the investigation index stays monotonic. Cosmetic only.

---

## What blocks merging

**Nothing blocks merge.** A/B/D pass; C passes as `partial` by design (its third item is
gated on A, which merges first). The required-fixes are:

- **Two MINOR Lane-D helper-script fixes (non-blocking, apply with the merge):**
  - `check_native_compiles.sh`: add `-DDawn_DIR=...` to the cmake configure so a fresh
    worktree can configure.
  - Rebuild the objdiff fork binary (`cargo build --release`) before using
    `nightly_measurement_guard.sh --strict`; base mode works as-is.
- **One runbook ordering rule (must follow):** Lane B's `certify_floor.py --apply` MUST run
  **after** the Wave-1 `sync_match_percent.py` (certs gate off `match_percent_normalized`).
  Reconcile check (e) catches the drift if this is skipped.
- **One informational doc typo:** Lane D's impl-report JSON says "3 of 20" NeedsInvestigation;
  doc 16 correctly says 4. Doc is authoritative.

---

## Open follow-ups for Wave 3

1. **Fix the next native boot blocker: `Sound::SynthPoll` double-free** (`src/system/synth/Sound.cpp:174`
   — `cur=*it; it++; erase(it)` erases the next element / `erase(end())`). It ends the boot at
   `main_screen` and gates a richer **gameplay-path** stub capture and all gameplay-telemetry
   tests. Highest-priority native unblock now that the App-ctor crash is fixed.
2. **Re-run the feet/IK gate (`FeetNotBelowFloorDuringGameplay`) once boot reaches gameplay**,
   then attack the narrowed residual: the **gameplay song-move/poll-order path**
   (`HamDriver.cpp:95-101`). doc-12's int-vs-float and mConstraints leads are CLOSED (refuted
   with asm / confirmed faithful) — do NOT re-litigate them. May need empirical native
   poll-order capture and/or Xenia live ground truth (currently P0-blocked by an offset-bugged
   bone read).
3. **Promote the 9 already-100%-live single-blocker units** Lane D found (runbook step 7), and
   work the 7 LikelyFixable + 1 MaybeFixable (PropSync FPR regswap) cohort. **Re-investigate
   `EstimateDraw`** (Rnd_NG) — report.json's 99.94% is a stale reflinked-`.obj` cache; live is
   97.6%, needs a fresh build to score truthfully.
4. **Schedule a unicorn re-run to refresh the 843 stale-unicorn floor certs** (~98 days old).
   They remain valid signals, but a refresh converts them from "stale-flagged" to fresh
   evidence and lets the orchestrator trust the 94.15%-bytes done figure for planning. The
   344 no-evidence frontier fns also want unicorn coverage to be certifiable or routed.
5. **Attack the 558 open / 287,908-byte genuine residual** surfaced by the `authorable_done`
   view — this is now the canonical "real remaining work" denominator. Prioritize the
   routable/real-bug classes that Lane B deliberately left open (call_count / error / call_arg
   / object_memory / return_value) over the certified cosmetic floors.
6. **Wire `nightly_measurement_guard.sh` into the nightly/CI** (drift check + `--strict` recert
   once the objdiff fork binary is rebuilt) and add `check_native_compiles.sh` as a pre-merge
   gate so a PPC-only asm-archaeology commit can't silently break the native build again
   (the recurring Mesh.cpp/AmbientOcclusion.cpp pattern).
7. **Renumber the two colliding `17-*` investigation docs** (Lane C → `18-`) to keep the index
   monotonic. Cosmetic.
8. **`pgo_block_sink` floor certs are not auto-fired** (361-fn PGO block-sink floor isn't a
   queryable DB flag). If a curated done-view is wanted for that family, supply the 361-fn list
   manually to `certify_floor.py`.
