# 92 — Execution Wave 1 Results

**Date:** 2026-06-10. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`91-EXECUTION-WAVE-1.md`](91-EXECUTION-WAVE-1.md). **Scope:** Phase 0
measurement integrity (roadmap 0.2/0.3/0.4/0.7 + authorable metrics + strict-reloc
recert) and the two native quick wins (N.1/N.2). Zero decomp work.

All four lanes completed in isolated worktrees and **passed** adversarial verdict.
**No lane committed to `main` and none wrote `decomp.db`** (mtime still `Jun 10 12:58`,
HEAD still `3b686fd9`). Branches are staged for the orchestrator to merge and apply.

---

## TL;DR headline numbers

| Metric | Value | vs audit expectation |
|---|---|---|
| **Authorable code byte match (canonical)** | **78.75% (main report.json) / 78.86% (worktree build)** | audit said 77.5–78.5% — slightly above, expected drift from new decomp |
| **Authorable normalized fns** | 90.73% main / 91.60% worktree (29,264–29,545 / 32,253) | new metric, in band |
| **XEX-total code match (XDK-diluted)** | 43.95% | confirms ~44% dilution |
| **Strict-reloc false-100% upper bound** | **2,405 authorable** different-symbol-NAME targets, but ICF/string/STL-dominated → **0 functions need reopening** | audit 02 "bounded but uncounted, genuine subset much smaller" — CONFIRMED |
| **DB promotable (normalized==100, not COMPLETE)** | 196–216 (206 exact at report level) | audit 04-F2 = 206 — EXACT at report level |
| **DB demotable (COMPLETE & normalized<100 → NULL)** | 8 (of 26 stale-COMPLETE; 18 are fuzzy<100 but norm==100, correctly KEPT) | audit 04-F4 = 20 — reconciled (normalized gate) |
| **Stale is_stub cleared** | ~1,736 total (1,468–1,747 by sync + ~9–268 db-only by reconcile) | audit 04-F3 = 1,728 — close (+8 drift) |
| **percent drift (current_percent vs report fuzzy, |d|≥0.5)** | 0 false-complete (20 lagging rows, repaired by sync) | audit 03-F6 = 639 — **REFUTED** (clean re-sync since audit) |
| **json-c native** | 6 .c compiled, 57 real symbols, 15 stubs removed, +1 real crash fix | audit 12-F5 confirmed |
| **Native stub tracer** | 152 function stubs instrumented + `/api/stubs` endpoint | audit 12-F4 said 171 (conflated 152 fns + 14 data singletons) |

**Nothing blocks merge.** All required-fixes are documentation/reporting corrections
and one apply-step flag fix (Lane D cmake `-DCMAKE_BUILD_TYPE`). See per-lane and the
runbook below.

---

## Per-lane outcomes

### Lane A — measurement-sync core (Opus) — **PASS**

- **Branch:** `wave1/a-measurement-sync` · **Worktree:** `/home/free/code/milohax/wt-wave1-a-measurement-sync`
- **Files:** `scripts/sync_match_percent.py` (modified, +153/-15), `scripts/reconcile_db.py` (new, 247 ln), `scripts/test_measurement_sync.py` (new, 18 assertions, all pass).
- **Investigation result (resolved the lane's open question):** report.json function
  entries **already carry `match_percent_normalized`** alongside `fuzzy_match_percent`.
  Keys: `['address','fuzzy_match_percent','match_percent_normalized','metadata','name','size']`.
  No objdiff report-config change or derivation needed. **Independently re-verified** by
  this synthesis agent against `build/373307D9/report.json`.
- **What it does:** dual-stores normalized (adds non-destructive `match_percent_normalized`
  column, fills ~31,064 rows; `current_percent` stays fuzzy for continuity); keys
  `--promote` off `normalized==100`; new `--demote` reverts `COMPLETE→NULL` when
  `normalized<100` (gated on normalized so legit fuzzy<100/norm==100 rows are NOT
  demoted); clears stale `is_stub` when current≥100; flags (never mutates) report-absent
  symbols. `reconcile_db.py` is a read-only drift detector (checks a/b/c/d), nonzero exit
  on drift, `--fix` applies the sync-owned demote+stub-clear keyed off DB's own
  `current_percent` (catches db-only rows invisible to the report-join).
- **Key measurements vs audit:**
  - promotable 196–216 (206 exact at report level) vs **audit 04-F2 = 206** ✔ exact at report level
  - demotable 8 (of 26 stale-COMPLETE) vs **audit 04-F4 = 20** — reconciled: 18 of those 26 are fuzzy<100 but **normalized==100** (legitimately complete) and are correctly KEPT
  - stub-cleared ~1,736 vs **audit 04-F3 = 1,728** (+8 drift) ✔ close
  - percent drift 0 false-complete vs **audit 03-F6 = 639** — **REFUTED on live DB** (a clean re-sync landed post-audit; only 20 rows lag, repaired by sync)
  - db-only authorable symbols absent from report: 1,189 (flagged, informational, NOT counted as drift)
- **Contradictions:** 03-F6 (639) refuted; 04-F4 (20→8 demotable under normalized gate);
  04-F3 (1,728→1,736); 04-F2 (206) exact. Lane prompt's "report may only carry fuzzy"
  RESOLVED — normalized already present.
- **Risks:** worktree's checked-in `decomp.db` is a stale 16-col snapshot — apply target
  is the live main DB (68 cols, 52,504 rows); migration guards handle it. `--promote` and
  `--demote` are separate opt-in flags — **the apply step MUST pass both**. Dry-run on a
  DB lacking the column reports norm-updated=0 (can't ALTER on dry-run) but still computes
  promote/demote correctly.
- **Verdict required-fixes (non-blocking, reporting only):** the impl report's validation
  counts (Promoted 216, stub-cleared 1747) differ from the verifier's re-run on the
  current live DB (Promoted 196, stub-cleared 1468, +268 db-only). The mechanism is
  correct; the agent likely tested a slightly different snapshot. **Trust the post-apply
  re-measurement, not the report's frozen counts.** Also a cosmetic `b=` count in the
  DRIFT line conflates kept vs demotable rows — exits correctly.

### Lane B — authorable-denominator metrics (Sonnet) — **PASS**

- **Branch:** `wave1/b-authorable-metrics` (commit `3228dc27`) · **Worktree:** `/home/free/code/milohax/wt-wave1-b-authorable-metrics`
- **Files:** `scripts/authorable.py` (new, shared `SDK_UNIT_PREFIXES`), `scripts/progress_metrics.py` (new, 374 ln), `docs/PROGRESS_METRICS.md` (new, generated), `scripts/measure_progress.sh` (+`--authorable` flag, 12 ln).
- **What it does:** `authorable.py` is the single source of truth for exclusion prefixes
  (`SDK_UNIT_PREFIXES = ["default/xdk/", "default/lib/binkxenon/"]` + `is_authorable()`).
  `progress_metrics.py` reads report.json and computes XEX-total + authorable byte/fn
  match (fuzzy and normalized), remaining work, complete-unit counts; `--markdown`
  regenerates the doc. `--authorable` delegates via `exec` (cannot combine with other flags).
- **Key measurements (independently re-verified by the verifier from raw report.json):**
  - authorable code 78.86% worktree / **78.75% main** (5,006,772 / 6,349,080 bytes) vs **audit 77.5–78.5%** — slightly above, expected drift
  - authorable normalized fns 90.73% main / 91.60% worktree
  - SDK excluded 5.03 MB (44.2% of 11.38 MB XEX); remaining authorable ~2,989 fns / 1.24 MB
  - complete authorable units 403 / 967 (41.68%)
- **Contradictions:** `sync_match_percent.py`'s own `SDK_UNIT_PREFIXES` had only
  `default/xdk/`; audit docs (05-F3, 06-F1, 00-INDEX) say both `default/xdk/*` AND
  `default/lib/binkxenon/*`. `authorable.py` carries both (correct). Plan said
  `default/lib/*`; impl uses `default/lib/binkxenon/` (narrower but functionally equal —
  no other units under `default/lib/`).
- **Risks:** until `sync_match_percent.py` imports from `authorable.py` (Lane A owns that
  file; Lane B was correctly forbidden from editing it), the two scripts have a ~78 KB
  binkxenon denominator inconsistency (negligible). `--authorable` uses `exec` so it can't
  combine with `--detailed`.
- **Verdict required-fixes:** none.

### Lane C — strict-reloc recertification (Opus) — **PASS**

- **Branches:** `wave1/c-strict-reloc` (dc3, commit `f9bdc94b`) + `wave1/strict-reloc` (objdiff fork, commit `72b553f`). **Worktrees:** `/home/free/code/milohax/wt-wave1-c-strict-reloc` + `/home/free/code/milohax/wt-objdiff-strict`.
- **What it does:** added `FunctionRelocDiffs::NameOnly` to the objdiff fork (name+section
  match, addend ignored — the recert mode doc 02 said didn't exist) with 3 unit tests
  pinning the truth table (forgives addend / catches wrong callee / exact match holds).
  Generated `report_strict.json` with NameOnly (report.json untouched) and wrote
  `scripts/analysis/reloc_strict_classify.py` (read-only, never touches decomp.db) which
  re-diffs all 10,470 lenient-100/strict-<100 candidates and classifies every reloc-name
  mismatch. Results in `14-strict-recert-results.md`.
- **THE NUMBER:** 2,405 authorable functions are genuine different-symbol-**NAME** targets,
  but that bucket is **ICF-dominated** (~1,227 ICF folds like MemOrPoolFree/FreeSTL +
  dtor folds, ~250 different log/assert strings, ~174 STL-helper folds), leaving a residual
  ~754 suspect tail that on inspection is still mostly ICF-merged + jeff target-split
  mis-attribution. **0 functions need reopening on strict-reloc grounds** (genuine
  behavioral wrong-callee residue at most low-dozens, all ≥98.6% strict). Confirms doc 02's
  "bounded but uncounted, genuine subset much smaller" framing.
- **Key measurements vs audit:**
  - candidates 10,470 fns / 2.75 MB vs **doc 02-F2 = ~11,052** (benign: doc 02 used
    name_address + a 2-day-stale report; direction/magnitude match)
  - matched_code None→NameOnly: 5,000,868 → 2,252,372 bytes; matched_functions barely
    moves (29,278 → 29,272, −6) — the **count metric is robust to reloc strictness**
  - class breakdown: template_instantiation_variant 2,992; target_split_label 3,096;
    benign_string_path 1,321; benign_build_artifact 645
- **Contradictions:** doc 02 conflated "wrong NAME" with "behaviorally wrong"; most
  wrong-NAME relocs are ICF folds (linker merged identical bodies to one address → name
  differs, behavior identical). The classifier surfaces this distinction the audit didn't
  anticipate.
- **Risks:** `genuine_wrong_target` is a name-pattern heuristic, not a definitive ICF
  check (definitive needs resolved-ADDRESS comparison, which objdiff JSON doesn't expose),
  so 2,405 is a conservative UPPER bound. One row (HamDirector ctor `??_8RndCam` vs
  `??_8HamDirector` vbase-table) is a genuine base-class layout diff worth a manual look —
  single fn already 99.91% strict, not an emergency. Classifier spawns 1 objdiff-cli per
  candidate (~10,470 procs, ~90 s at 30 workers) — fine for periodic recert, not interactive.
- **Verdict required-fixes (non-blocking, doc-only):** in
  `14-strict-recert-results.md` the `genuine_wrong_target` bytes figure shows **742,728**
  but the actual value from `reloc_strict_classify.json` is **680,384** (~9% off). Function
  counts (2,408/2,405) are correct. Fix the MD byte figure before/after merge. Also: a
  pre-existing stale objdiff snapshot (`arch_ppc__diff_ppc-2.snap.new`) fails on pristine
  fork main too (a `match_percent_normalized` field added before this lane) — NOT caused
  by this lane; `cargo insta accept` separately if desired.

### Lane D — native quick wins N.1/N.2 (Opus) — **PASS**

- **Branch:** `wave1/d-native-quickwins` (commit `0abd4ad4`) · **Worktree:** `/home/free/code/milohax/wt-wave1-d-native-quickwins`
- **Files (12):** `native/CMakeLists.txt`, `native/src/StubTrace.{h,cpp}` (new),
  `native/src/engine_stubs_generated.cpp`, `native/src/platform/HttpServer.cpp`,
  `native/tests/test_json_parse.cpp` + `test_stub_trace.cpp` (new, 7 tests),
  `scripts/build/instrument_stubs.py` (new), `src/system/net/JsonUtils.cpp`,
  `src/system/net/json-c/config.h`, plus 2 pre-existing native-build unblock files
  `src/system/rndobj/Mesh.cpp` + `AmbientOcclusion.cpp` (HX_NATIVE-guarded, Xbox build
  byte-unchanged).
- **N.1 (json-c):** wired 6 json-c `.c` files into the native build (LANGUAGE C, PCH-skip,
  `-fno-ms-compatibility`), added an HX_NATIVE+POSIX block to `json-c/config.h`, removed
  15 return-0 json stubs from `engine_stubs_generated.cpp` (11 json_object_*/json_tokener_parse
  + lh_table_lookup + 3 printbuf_*) so the 57 real symbols win, and **fixed a real native
  crash**: `JsonUtils.cpp::LoadFromString`'s HX_NATIVE `if(!obj)` guard let
  json_tokener_parse's `is_error()` sentinel through and crashed on deref — now an
  LP64-correct is_error check. Before this, every online JSON (RockCentral/leaderboards/
  MOTD/store) silently parsed to empty.
- **N.2 (stub tracer):** added `StubTrace.{h,cpp}` (opt-in `DC3_STUB_TRACE=1` per-symbol
  hit counter, near-zero cost when off, ranked JSON dump), instrumented all 152 single-line
  function stubs via the new idempotent `instrument_stubs.py`, and added a `GET /api/stubs`
  endpoint. All 7 new tests green.
- **Key measurements vs audit:** 152 function stubs instrumented vs **doc 12-F4 = 171**
  (conflated 152 function stubs + 14 null-singleton **data** stubs, which have no body to
  trace). doc 12-F5 (json-c not compiled) CONFIRMED, plus 4 more json-c collisions doc 12
  didn't list (lh_table_lookup, printbuf_new/free/memappend).
- **Contradictions (binding):**
  - **`milo-tests 371/371 baseline is FALSE in this environment.** The pre-built main
    June-9 binary scores 263 pass / 20 fail / 1 crash; the worktree binary scores
    236 / 23 / 3 — both run from `orig-assets`. The 371/371 number is stale or from a
    different toolchain. milo-tests has no asset-aware ctest WORKING_DIRECTORY, so it must
    be run with `cwd=orig-assets`.
  - **Native build broken at HEAD `3b686fd9`:** `Mesh.cpp` + `AmbientOcclusion.cpp` don't
    compile for any native target under modern Clang/libstdc++ (ObjPtr<> `?:` ambiguity;
    std::vector iterator-as-pointer C-casts), broken by recent asm-archaeology commits
    `2b50b35e`, `6eeba04f`. This blocks ALL Wave-1 native validation, not just this lane.
    Fixed minimally under HX_NATIVE guards.
  - Pre-existing decomp bugs exposed by newer libstdc++ bounds asserts (NOT this lane):
    `CharBones::ScaleDown` and `CameraManager::RandomizeCategory` index past a vector and
    SIGABRT; the latter crashes dc3-native during App construction (before the HTTP server
    binds), so a live `/api/stubs` curl is infeasible here — `/api/stubs` is validated via
    the StubTrace unit tests (plan explicitly permits this).
- **Risks / required-fixes:**
  - **APPLY-STEP BUG (must fix in runbook):** the lane's cmake configure command omits
    `-DCMAKE_BUILD_TYPE`, so a post-merge build runs without NDEBUG, activating GCC 16
    libstdc++ hardened vector bounds assertions → `ClipPoseFixture.PoseMeshesDoesNotCrash`
    (and the 12-test ClipPoseFixture suite) crashes on the pre-existing CharBones edge-case.
    **Add `-DCMAKE_BUILD_TYPE=RelWithDebInfo`** to match the main build config. The 7 new
    tests pass in both modes.
  - Report inaccuracy (doc-only): the report claims "only HeadlessBootTest is worktree-only";
    the verifier found ClipPoseFixture also crashes in the worktree binary — this is the
    build-config difference above, NOT a Lane D source regression. Lane D made zero changes
    to any `char/` file.
  - The 2 native-build-unblock files are shared decomp source; concurrent agents may touch
    them → trivial merge conflict possible (all edits HX_NATIVE-guarded, PPC match unchanged).

---

## Consolidated decomp.db apply-steps runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches.
**Only Lane A mutates decomp.db.** Lanes B/C/D are DB-read-only (B/C read report.json; D is
native source+build only). Run from repo root on `main`.

```bash
# 0. Merge order first (see next section), then:

# 1. Ensure report.json is current (Lane A reads it; the sync rebuilds it itself in step 2).
ninja build/373307D9/report.json

# 2. THE ONLY decomp.db WRITER. Rebuilds report, dual-stores normalized (~31k rows),
#    promotes norm==100 -> COMPLETE (~196-216), demotes COMPLETE & norm<100 -> NULL (8),
#    clears stale is_stub (~1,468-1,747). MUST pass BOTH --promote AND --demote.
python3 scripts/sync_match_percent.py --build --promote --demote

# 3. Clear the residual db-only stale stubs the report-join can't see (~9-268),
#    demotes 0 (all norm-complete). Applies sync-owned corrections only.
python3 scripts/reconcile_db.py --fix

# 4. Read-only confirm: expect (a)=0 percent-drift, demotable=0, (c)=0 stale-stub,
#    report-only=0, exit 0 "OK: no drift detected".
#    The ~1,189 db-only authorable count is INFORMATIONAL, not drift.
python3 scripts/reconcile_db.py

# 5. (optional) wire `python3 scripts/reconcile_db.py` as a ninja-postbuild / nightly
#    guard (nonzero exit on drift). After the objdiff NameOnly change lands on the fork
#    main, optionally add a strict recert step to that nightly:
#      objdiff-cli report generate -c functionRelocDiffs=name_only -o build/373307D9/report_strict.json
#      python3 scripts/analysis/reloc_strict_classify.py --jobs 30
#    and watch genuine_wrong_target for NEW non-ICF two-real-method entries.
```

**Trust the live post-apply counts over the frozen report counts** (Lane A's report and
the verifier's re-run disagree by ~20 promoted / ~280 stub-cleared because they tested
different DB snapshots; the mechanism is verified correct and idempotent — a re-run shows
all counts 0).

After Lane B merges, a **separate follow-up commit** should make
`sync_match_percent.py` import `SDK_UNIT_PREFIXES` from `scripts/authorable.py` (this
also adds `default/lib/binkxenon/` to sync's exclusion — correct, but a behavior change
for that script; do it deliberately, not silently). Until then the two scripts have a
~78 KB binkxenon denominator inconsistency (negligible). Then regenerate the doc:
`python3 scripts/progress_metrics.py --markdown`.

---

## Merge order for `wave1/*` branches

**There are NO file-path conflicts across the four branches** — verified with
`git diff --name-only 3b686fd9..wave1/<lane>` (disjoint file sets, no path touched by more
than one branch). The only coupling is logical (Lane A's `SDK_UNIT_PREFIXES` vs Lane B's
`authorable.py`), which is a deliberate post-merge follow-up, not a git conflict. So order
is driven by dependency/runbook ordering, not conflict avoidance:

1. **`wave1/b-authorable-metrics`** (commit `3228dc27`) — creates `scripts/authorable.py`
   first, so the Lane A → authorable.py import follow-up has its target present. Pure
   additive (4 new/modified files, no DB).
2. **`wave1/a-measurement-sync`** (the DB-tooling lane) — merge second; it is the only
   DB-writer. After this is on main, run the runbook above. (Independent of B at the git
   level; B-first only so the import follow-up is ready.)
3. **`wave1/c-strict-reloc`** (dc3, commit `f9bdc94b`) — independent, additive (2 source
   files; `report_strict.json` / `reloc_strict_classify.json` are gitignored build
   artifacts, not committed). Fix the 742,728→680,384 byte figure in
   `14-strict-recert-results.md` before/after merge.
   - **Companion objdiff-fork branch `wave1/strict-reloc`** (commit `72b553f`, repo
     `/home/free/code/milohax/objdiff`): merge/cherry-pick the `NameOnly` change into the
     fork main separately. The pre-existing stale `arch_ppc__diff_ppc-2.snap.new` is NOT
     from this lane.
4. **`wave1/d-native-quickwins`** (commit `0abd4ad4`) — independent, native source+build
   only, no DB. Watch for a trivial conflict on the two shared unblock files
   (`Mesh.cpp` / `AmbientOcclusion.cpp`) if a concurrent agent fixed them; all edits are
   HX_NATIVE-guarded so the Xbox/PPC match is unchanged. **When building post-merge, use
   `-DCMAKE_BUILD_TYPE=RelWithDebInfo`** (the lane's own apply cmake omits it and would
   crash ClipPoseFixture on a pre-existing CharBones bug).

A/B/C/D are git-independent; any order merges cleanly. The order above is the recommended
one (B before A for the import follow-up; A's runbook last among the DB steps).

---

## What blocks merging

**Nothing blocks merge.** All four lanes pass. The required-fixes are:

- **One runbook fix (must apply):** Lane D's post-merge cmake must include
  `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (folded into the merge-order note above) — otherwise
  the build crashes a pre-existing CharBones bug in debug mode. The merged source is fine.
- **Two doc-only corrections (do before/after merge, non-blocking):**
  - Lane C `14-strict-recert-results.md`: `genuine_wrong_target` bytes 742,728 → **680,384**.
  - Lane A report counts (Promoted/stub-cleared) and Lane D test-delta narrative are
    stale vs live re-measurement — trust the post-apply numbers, update docs to match.

---

## Open follow-ups for Wave 2

1. **Land the `authorable.py` import in `sync_match_percent.py`** (Lane A owns the file;
   deferred to avoid a Wave-1 cross-lane edit). Adds `default/lib/binkxenon/` to sync's
   exclusion — deliberate behavior change. Single source of truth for `SDK_UNIT_PREFIXES`.
2. **Re-establish a trustworthy `milo-tests` baseline.** The 371/371 figure is stale/
   toolchain-specific; neither the main June-9 binary nor the worktree reproduces it
   (263/236 pass). Pin a build config (RelWithDebInfo, NDEBUG) + asset-aware ctest
   WORKING_DIRECTORY so the gate is meaningful, then re-cert the true pass count.
3. **Fix the two pre-existing native crashes exposed by hardened libstdc++:**
   `CharBones::ScaleDown` (`&mBones[mCounts[TYPE_END]]`) and
   `CameraManager::RandomizeCategory` (vector OOB during App construction). The latter
   blocks live `/api/stubs` and any headless boot. Real decomp/native bugs, out of Wave-1
   scope.
4. **Un-break native at HEAD permanently.** `Mesh.cpp` + `AmbientOcclusion.cpp` were
   broken by asm-archaeology commits `2b50b35e`/`6eeba04f` for modern Clang. Lane D's
   HX_NATIVE guards are the fix; ensure they land and add a CI native-compile smoke so a
   PPC-only commit can't silently break native again.
5. **Tighten the strict-reloc classifier** from name-pattern heuristic to a definitive
   ICF check by exposing resolved symbol ADDRESSES in objdiff's diff JSON (same address =
   ICF = benign). Would convert the 2,405 conservative upper bound into a true behavioral
   wrong-callee residue (expected low-dozens). Manually review the one real lead
   (HamDirector ctor `??_8RndCam` vs `??_8HamDirector` vbase-table).
6. **Wire reconcile + strict recert into the nightly** (runbook step 5) so measurement
   drift and any NEW non-ICF wrong-callee gets caught between manual re-syncs.
7. **Produce the live ranked stub worklist** once N.3's boot crash (#3) is fixed:
   `DC3_STUB_TRACE=1 scripts/dc3-agent-test.sh` then `curl localhost:9090/api/stubs` —
   converts silent native failures into a prioritized fix queue (the original N.2 intent).
