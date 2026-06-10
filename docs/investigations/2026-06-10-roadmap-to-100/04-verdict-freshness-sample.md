# 04 — Verdict Freshness: Stratified Sample vs Fresh Ground Truth

## Question

How stale/wrong is `decomp.db`'s per-function view (current_percent, verdict, is_stub,
unicorn_*) when checked against fresh ground truth from `run_objdiff` / `run_diff_inspect`?
Specifically: (1) how much does `current_percent` drift; (2) is the AT_LIMIT 40-85 label
genuinely floored or routable; (3) is unicorn data fresh; (4) what re-verdict policy is warranted.

## Method (commands run)

- DB read-only census: `sqlite3 'file:decomp.db?mode=ro'` over `functions` and `attempts`
  (verdict counts, banding, is_stub, updated_at, unicorn_tested_at, attempt provenance).
- Stratified deterministic sample (`ORDER BY symbol LIMIT n OFFSET k`, never random):
  COMPLETE n=8, NULL-verdict 85-99.99 n=8, AT_LIMIT 40-85 n=10, is_stub=1 n=4+6.
- Fresh ground truth: `mcp__orchestrator__run_objdiff` (16 calls) + `run_diff_inspect mode=diagnose`
  (3 calls). objdiff `normalized` is the canonical scorer.
- report.json field analysis: streamed `build/373307D9/report.json` (15 MB, mtime 2026-06-10 00:46,
  fresh) in python3; compared `fuzzy_match_percent` vs `match_percent_normalized` per function.
- Sync semantics: read `scripts/sync_match_percent.py` (line 84 reads `fuzzy_match_percent`).
- Git provenance: `git log` on sampled units (AmbientOcclusion, Spotlight, Utl, BinkIntegration).

## Findings

### F1 — `current_percent` is NOT drifting; the **verdict label** is the stale axis.
Every sampled `current_percent` matched fresh objdiff within ~1% (raw scoring):

| Function | DB current_percent | Fresh raw | Fresh normalized |
|---|---|---|---|
| `??1?$ObjRefConcrete@VRndFont3d@@…` (COMPLETE) | 100.0 | 100.0 | 100.0 |
| `??1?$ObjRefConcrete@VRndMesh@@…` (COMPLETE) | 100.0 | 100.0 | 100.0 |
| `TransformNormal` (NULL) | 94.8 | 94.8 | 94.8 |
| `UpdateGestures` (NULL) | 94.89 | 94.5 | 94.9 |
| `BlurSurface` (AT_LIMIT) | 41.81 | 41.1 | 41.5 |
| `BuildFromBSP` (AT_LIMIT) | 70.16 | 69.6 | 70.2 |
| `BridgeGapsInMoveParents` (AT_LIMIT) | 79.47 | 79.2 | 79.5 |
| `BuildBeam` (AT_LIMIT) | 66.56 | 66.5 | 66.6 |
| `BuildNGCone` (AT_LIMIT) | 67.16 | 67.1 | 67.2 |
| `BuildVisit` (AT_LIMIT) | 66.69 | 64.9 | 65.5 |
| `BinkFileIdle` (AT_LIMIT) | 71.48 | 70.6 | 71.5 |
| `BinkFileReadFrame` (AT_LIMIT) | 84.66 | 84.4 | 84.7 |

The last sync (`updated_at` = 2026-06-08 on 48,251 rows, plus 86 on 06-09, 25 on 06-10) is recent,
and `report.json` is from 2026-06-10 00:46. So the **percentage plane is fresh.** Drift is confined
to (a) the `verdict` TEXT label and (b) the fuzzy-vs-normalized scoring choice (F2, F4).

### F2 — DB stores **fuzzy** (raw) match%, so ~206 truly-complete functions read as <100.
`report.json` carries BOTH `fuzzy_match_percent` and `match_percent_normalized` per function;
`scripts/sync_match_percent.py:84` reads `fuzzy_match_percent`. Across the 30,798 functions that
have both fields:
- `fuzzy>=100`: 29,030  vs  `normalized>=100`: 29,236 → **206 functions are 100% normalized but <100% fuzzy.**
- `fuzzy > normalized`: 0 (normalized is always >= fuzzy, as expected).

These 206 are "secretly complete" — only relocation/address noise (different .text layout) keeps the
fuzzy score under 100. Confirmed in the NULL-verdict 85-99.99 sample: **5 of 8** were normalized=100
but DB-stored <100:
- `DxRnd::Terminate` 99.98 → norm 100 (raw 99.8, 1 reloc `lwz off:-4`).
- `RndTransformable::SyncProperty` 99.96 → norm 100 (raw 99.1, single `addi vs subi` = MI sub-object subi floor).
- `CharUpperTwist::SyncProperty` 99.97 → norm 100 (raw 98.8, 3 `subi` offset diffs).
- `UIListDir::StartScroll` 99.98 → norm 100 (raw 99.5).
- (`TransformNormal`, `UpdateGestures` are genuinely <100 — real regswap/control-flow.)

The byte-level impact is small (98.26% fuzzy vs 98.41% normalized of measured code) but the
**function-count "done" line moves by 206** depending on which scorer defines "matched." Whatever
"done" means for this project must pick normalized; the DB currently does not.

### F3 — `is_stub=1` is a stale flag, not a live one: 64% of "stubs" are actually 100% complete.
`is_stub=1` on 2,686 rows, but:
- 1,728 of them have `current_percent>=100` AND `verdict='COMPLETE'`.
- breakdown: COMPLETE 1,728 (avg 100.0), AT_LIMIT 899 (avg 2.4), NULL 59 (avg 99.4).

So the flag was set when the function was a stub, the stub got implemented to 100%, and **the flag
was never cleared.** `is_stub=1` overcounts unimplemented work by ~1,728. Only the ~899 AT_LIMIT
stubs (avg 2.4%) are plausibly still real stubs. The is_stub sample also surfaced a 100%/COMPLETE
row carrying is_stub=1 (`??0?$_Vector_base@F…@VorbisReader`) — same staleness.
Most "real" (non-thunk) stubs cluster in `synth_xbox/Fx*` (StandardEffect ctors at 0%) and the
116 `??_E/??_G` rows are EH/dtor thunks, not source-authorable.

### F4 — The AT_LIMIT verdict is the most wrong axis. ~29% of AT_LIMIT rows sit ABOVE 85%.
Of 4,405 AT_LIMIT rows, banded by **current `current_percent`**:
- `<40`: 2,817 · `40-85`: 293 · `85-95`: 478 · `95-99`: 817 · `100`: 0
- **1,295 AT_LIMIT rows (29%) are now >=85%; 817 are 95-99%.** Two verified to normalized=100:
  - `AccomplishmentProgress::AddAccomplishment` 99.99 → **norm 100** (raw 99.4, 2 stack-DIFFER reloc).
  - `MetagameRank::UpdateScore` 99.98 → **norm 100** (2,149 insns, 6 reloc-noise `lwz`). Memory says
    this hit 100% in commit 5ec1e24b — the AT_LIMIT label was never retired.

These contradict the live objdiff verdict directly: the tool reports `LikelyFixable`/
`NeedsInvestigation`, the DB reports `AT_LIMIT`.

### F5 — The AT_LIMIT 40-85 band is overwhelmingly ROUTABLE, not floored. Memory CONFIRMED.
All 8 AT_LIMIT functions sampled in 40-85 carried `REGISTER_SWAP` patterns (the "floor"-smelling
label), but `diagnose` shows the swaps are **downstream cascades of real structural divergence**, not
post-regalloc artifacts. Every one had a structural frame delta plus insert/delete/replace clusters:

- `BuildFromBSP` 70%: frame Δ +0x20; 35 insert/delete in 11 clusters; **5 real replaces** (e.g. idx30
  TGT `lwz r9,0x4,r11` vs SRC `add r9,r10,r31` — different addressing). diagnose: "good" diff_op=0,
  but 11 ins/del clusters = missing/reordered code.
- `BridgeGapsInMoveParents` 79%: frame Δ -0x10; 29 ins/del in 10 clusters; **10 real replaces +
  2 diff_op** (idx136 `bne` vs `bge` — branch polarity; idx113 `subi` vs `addi`). Pure logic.
- `BlurSurface` 41%: frame Δ -0x20; **87 ins/del in 11 clusters + 9 real replaces**; decomp indexes
  a `kBlurOffsets` array where target uses inline addressing — structural data-layout divergence.
- `BuildBeam`/`BuildNGCone`/`BuildVisit`: all frame Δ ±0x10/0x20, control-flow inversions,
  commutative-op swaps, and (BuildNGCone) **16 SWAPPED stack slots = "reorder paired declarations."**
- `BinkFileIdle` 71%: prologue replace (`bl __savegprlr_29` vs `stw r12`) + 1/2 TGT/BASE-only locals.

**Verdict: 8/8 of the AT_LIMIT 40-85 sample are routable (logic/structure/decl-order), 0/8 are a true
regalloc floor.** This is fully consistent with the memory claim that the bucket is ~60%+
logic-divergence mislabeled as floor. The `REGISTER_SWAP` pattern label is the trap — trust
`diagnose`'s ins/del-cluster + real-replace + diff_op counts, which are nonzero on every one.

### F6 — unicorn_* coverage is broad but STALE by ~3 months, predating all June wins.
- Coverage: 27,343 rows have `unicorn_tested_at`; verdicts EQUIVALENT 25,466, DIVERGENT 1,877, NULL 25,161.
- **Freshness: 27,076 of 27,343 (99%) were tested 2026-03-04.** Only 43 (2026-05-14), 30 (2026-02-27),
  194 (2026-02-20) are otherwise. The bulk unicorn run is **3 months old.**
- Recent commits (the asm-archaeology wave, 6eeba04f..0e6ab068, June 9-10) moved dozens of functions
  20-50 points (BlendVert 56→84.5, Tessellate 63.8→87.3, DrawLight 58.5→98.1). Those source edits are
  **not reflected in any unicorn re-test.** A function flagged DIVERGENT/logic in March may now be
  EQUIVALENT (or vice-versa) and the DB cannot know. unicorn DIVERGENT classes (call_count 481,
  logic-class buried in build_env 781) are usable as *leads* but every DIVERGENT verdict on a
  June-touched unit must be re-run before acting.

### F7 — AT_LIMIT provenance: 44% were auto-labeled, never actually attempted.
- `attempts` table: 28,660 rows over 6,986 distinct functions, dated 2026-01-25 .. 2026-05-31.
  Activity is front-loaded: Jan 15,104 → Feb 6,788 → Mar 6,024 → Apr 505 → May 239.
- **1,946 of 4,405 AT_LIMIT rows (44%) have ZERO rows in `attempts`** — the verdict was set by bulk
  heuristic/import, not by exhausting a decompilation. `attempt_count=0` agrees: 1,905 AT_LIMIT rows.
- 40 of these never-attempted rows are already >=85% (auto-labeled AND mislabeled-high).
- `last_model` NULL on 1,950 AT_LIMIT rows; the rest split opus 1,153 / haiku 583 / unknown 395 /
  sonnet 318. The "unknown"/NULL provenance + Jan-heavy timing means most AT_LIMIT labels predate
  current tooling (the June archaeology playbook, normalized scoring conventions, the
  P1 floor-predictor). The label is episodic and never refreshed.

## Implications for the roadmap

1. **"Done" must be defined in normalized scoring, and the DB must store normalized.** Switching the
   sync to `match_percent_normalized` immediately re-classifies 206 functions as complete and removes
   the relocation-noise floor from the "remaining work" count. This is a measurement-correctness fix,
   not new decomp.
2. **AT_LIMIT is the single most unreliable column.** 1,295 of 4,405 (29%) are >=85% and at least some
   are normalized=100; 1,946 (44%) were never attempted. Any roadmap that treats AT_LIMIT count as
   "floored / unreachable" is overcounting the floor by a large margin. The real floor is much smaller
   than 4,405.
3. **The AT_LIMIT 40-85 band (283 non-stub fns, ~172 KB) is a routable work queue, not a floor.**
   8/8 sampled are logic/structure/decl-order fixable. This is exactly the band the June
   asm-archaeology agents have been clearing; it should be drained, not written off.
4. **is_stub is unusable as a "remaining stubs" metric** (64% false). Real stub work ≈ the 899
   AT_LIMIT is_stub rows, concentrated in synth_xbox effects.
5. **unicorn data cannot gate native-port bug-hunting until re-run.** It is 3 months stale and blind
   to the June source churn. Native-port-affecting bugs should be re-confirmed live, not read from the
   March snapshot.

## Tooling gaps found

- **Sync uses fuzzy, not normalized.** `scripts/sync_match_percent.py:84` should read
  `match_percent_normalized` (or store both columns), with `--promote` keyed off normalized==100.
- **No verdict-decay/refresh mechanism.** verdict + is_stub + unicorn_* are written once and never
  reconciled against current_percent. There is no trigger that, when `current_percent` moves, demotes
  a stale AT_LIMIT or clears an is_stub. A cheap nightly reconciler (pure SQL, no rebuild) could fix
  the worst contradictions (1,728 stub-clears, 1,295 AT_LIMIT-band corrections, 20 COMPLETE-but-<100).
- **AT_LIMIT carries no provenance for *why* / *which tool version*.** `verdict_reason` is known-stale;
  there is no `verdict_tool_version` or `verdict_set_at` distinct from `updated_at`. Can't tell an
  exhausted floor from an auto-import without re-running diagnose.
- **The `REGISTER_SWAP` pattern label fires on routable structural diffs** (8/8 in this sample),
  steering triage toward "floor." objdiff already knows better (diagnose shows ins/del clusters); the
  one-line pattern summary should down-rank REGISTER_SWAP when diff_op>0 or ins/del clusters exist.
- **No continuous unicorn re-test on touched units.** A post-commit hook that re-runs unicorn on the
  units changed in that commit would keep behavioral verdicts from going 3 months stale.
