# Wave 9 Results — structural og audio port + behavioral trades + hidden frontier + DB hygiene

**Date:** 2026-06-11 · **Landed through main `eae4ad39`** (+ runbook DB writes) ·
**Orchestration:** 1 Workflow (lanes A/B/D, 6 agents, ~1.13M tokens) + 1 standalone Opus lane (H)
+ orchestrator-inline lane C (DB hygiene, single-writer).

Wave 9 followed wave-8's recorded recommendation exactly: ONE high-EV structural lane + a
DB-hygiene/floor-cert pass, plus the still-live items from the 2026-06-10 asm-archaeology
session's open follow-ups (three unlanded behavioral bugs, ShaderProgram::Cache, nine native
review flags). No broad band sweeps were run.

## Headline

| Metric | Pre-wave (recorded) | Post-wave (corrected denominator) |
|---|---|---|
| done-with-certs fns | 98.63% | **99.04%** |
| done-with-certs bytes | 96.08% | **96.73%** |
| open residual | 285 fns / 192.9 KB | **264 fns / 193.9 KB** |

The denominators are NOT comparable: wave 9 found and fixed a measurement bug that had hidden
6,835 authorable fns (~1.0 MB) from the done view (below), and excluded 31 drift rows. The
post-wave numbers are the first on an honest denominator.

## THE measurement finding: SQL LIKE `??_%` wildcard bug (commit `ca13e0eb`)

`certify_floor.py` used `symbol NOT LIKE '??_%'` for its artifact-prefix exclusion — but SQL
LIKE treats `_` as a single-char wildcard, so the clause excluded EVERY `??`-prefixed symbol
(ctors `??0`, dtors `??1`, operators `??4`/`??O`, templates `??$`) from `authorable_done`, the
cert census, and every band query derived from them. Impact: **6,835 authorable fns /
1,007,088 bytes invisible** (6,360 already matched, 112 open / 45.5 KB of frontier no wave ever
saw). Fix: `like_prefix_clause()` with `ESCAPE`, applied to `is_authorable_sql()`, the view DDL,
and the `done_summary` fallback; view recreated at the runbook. `progress_metrics.py` and
`reconcile_db.py` were audited clean (Python `startswith`); `atexit_fuzzy_verify.py` already
escaped correctly.

## Lane A — og XAUDIO2/dsp structural audio port (VERIFIED PASS, `70303fd7`)

**+71 functions to 100% in synth_xbox (177 → 248).** Keystone: single-definition IUnknown
(`xdk/unknwn.h`) + tWAVEFORMATEX (`xapo.h`) so the real `ATG::CSampleXAPOBase` reaches
`dsp/StandardEffect.h` (new, og-verbatim) — unblocking the whole `StandardEffect<T>` template
family per effect. Landed: FxSend360 base bodies (SyncEffectParams/Refresh/CleanChain/
OutputVoice/Reconnect), 8 per-effect SyncEffectParams, the CreateFx family,
FxSendPitchShift360/Synapse360 Synth-unit COMDATs (the "jeff partition" fns — authored in the
tracked unit; **no re-split needed**, the partition concern was resolved at source level),
MeterEffect, EnvelopeGenerator. Honestly blocked: EQ/Wah Sync (EQEffect::Params 15-field vs og
14-field layout conflict — needs target-asm sizeof resolution), Reverb (3336B, deferred).

**Orchestrator root fix on top of the lane (the wave's process lesson):** the lane moved
tWAVEFORMATEX from xaudio2.h's `#pragma pack(push,1)` region into xapo.h **unpacked** — sizeof
drifted 0x12 → 0x14 per consumer TU. The lane's own gates read green, but on merged main the
suite failed 6 tests (DtaIncludeTest ×5 + MiloViewerPosePipeline) with cross-TU layout
corruption; the lane's in-worktree "10 pre-existing char crashers" were the same contamination
wearing a different hat. Decomposition test pinned it to the single lowercase-xaudio2.h hunk;
fix = `pack(1)` around tWAVEFORMATEX in xapo.h. **og's xapo.h carries the same latent bug.**
Post-fix: ninja green, 415/415 then 418/418 suite, web release green, lane symbols re-verified
100%. Lesson reinforced: cross-cutting header edits get the decomposition test on MERGED main,
not just in-lane gates; "pre-existing failures excluded" claims from a lane are a red flag.

## Lane B — behavioral trades + ShaderProgram::Cache (VERIFIED PASS, `c579988d`)

- **HolmesClientOpen 77.2 → 79.3 raw (80.5 norm)** — the MILO_FAIL now passes the filename
  (string-pool + asm proven). The wave-4 registry's "-12.8pp trade" was wrong; the right source
  form IMPROVES match. Registry entry closed as a win, not a trade.
- **DxRnd::D3DFormatForBitmap 75.9 → 62.6 LICENSED TRADE** — target calls Debug::Fail (not
  Warn) at 'Invalid dxt format: %d'; the residual is a tail-merge floor. The one expected
  regression in the wave sweep.
- **RndVelocityBuffer::Draw && form FLOOR-CONFIRMED with counterfactual** (&& = 47.2%, single-&
  = 74.2%; behaviorally equivalent here — no RHS side effects). Registry entry closed, no edit.
- **RndShaderProgram::Cache 74.3 → 85.7** (442-instr structural archaeology) + 3 native tests
  (ShaderProgramCacheLogic) pinning cache-key logic. Residual: r27↔r28 cascade + small
  control-flow tail — plausibly one more pass someday, not certified.

## Lane H — hidden frontier (standalone Opus lane, `62b6bd82`)

First harvest of the ??-symbol population unlocked by `ca13e0eb`:
- **ObjPtrList::sort ×3 → 100%** (CharBoneDir, Rnd, CharHair) — one shared-header fix in
  `ObjPtr_p.h` (PPC branch): target iterates FORWARD (`x->next`, `last=mNodes->prev`); ours
  bubble-sorted backward. og-confirmed. Zero regressions (header edit swept).
- **LocaleChunkSort::FastSort<3> 79.7 → 90.6 raw (94.4 norm)** — decl-order; residual r8/r9
  volatile regswap floor.
- 7 floor confirmations with diagnose signatures (PropSync<Waypoint>, DrawAccessories
  <LensExtract>, Sphere>Frustum, Skeleton::operator=/ctor, EventTask ctor, FlowPtr<Object>
  ::operator=) — all certified at the runbook.

## Lane D — native review-flag audit (report-only) + the Splash bug (`eae4ad39`)

8/9 asm-archaeology native flags **verified-good** (none reverted by waves 6–8; wave-6/7/8
boot/feet/suite gates ran with all nine in-tree; three fresh runtime probes). **1 bug found and
landed:** Splash::Suspend/Resume callsites dispatched the WRONG DxRnd virtual — target asm has
the cross-dispatch (Suspend → vtable+0x13c = DxRnd::Resume; Resume → +0x138 = Suspend) because
the splash thread owns the device during loading. Invisible at the normalized metric (both fns
100.0 normalized before AND after — the diff was relocation-plane); real behavioral inversion on
Xbox. The wave-4 "vtable slots swapped" story is corrected: header order was right, the
CALLSITES were inverted. Zero native risk (threaded branch dead under HX_NATIVE).

## Lane C — DB hygiene (orchestrator-inline, single-writer runbook)

- `--manual-file` mode added to certify_floor.py (orchestrator-supplied certs from a backlog
  JSON; resolves LIKE patterns, refuses ambiguity/overwrites/pct-drift, records CURRENT norm per
  the check-(e) contract). Backlog: `data/floor_cert_backlog_w7w8.json`.
- Of the doc-listed 25 wave-7/8 backlog symbols, **19 were already auto-certified** (wave-3
  census) or done (CheckBSPTree norm=100; ??_G excluded by design); 6 manual certs written, 3 of
  them freshly re-verified by diagnose (DrawDetectedBar const-table, ScaleAddEq frame/volatile
  cascade, CompressThread BSS base-sharing) + HolmesSetFileShare re-diagnosed post-lane-B
  (mechanism unchanged at 56.0). + 8 lane-H certs = **14 manual certs written, 0 problems**.
- **31 drift rows excluded** (`data/exclude_drift_rows.sql`): 27 link_glue zero-starts absent
  from report.json, 3 link_glue thunk/branch-island rows (size 4–36, fuzzy=None), and the
  Matrix3-Multiply row (authored in `math/mtx.cpp`; target instance COMDAT-placed in
  CharLookAt.obj — unpairable per-unit).
- Runbook executed: sync (132 improvements / 1 regression = the licensed trade / 125
  promotions), reconcile --fix (119 stale is_stub, 4 stale certs cleared), view recreate + auto
  census on fixed clauses, manual certs, final reconcile **0 drift**.

## Aggregate wave sweep (main vs `f01cc201`)

88 improvements / 11.3 KB; 3 "regressions" = the documented D3DFormatForBitmap trade + 2
report-pairing artifacts on untouched math units (a 12-byte Normalize alias entry and EH funclet
`fn_82EDB280`; the real Normalize measures 99.9%). Suite gates: milo-tests **418/418 0 FAIL**,
check_native_compiles PASS, web release green, PPC ninja green.

## Open follow-ups (wave 10 candidates, all narrow)

1. **EQ/Wah/Reverb Sync** — the remaining og-audio gap; gated on resolving EQEffect::Params
   15-vs-14-field layout from target asm (`SyncEffectParams@FxSendEQ360` sizeof immediates).
2. **ShaderProgram::Cache 85.7** residual (r27↔r28 + control-flow tail) — one more targeted
   pass or certify.
3. **Hidden-frontier residue**: MemTracker STL template cluster (~8 fns, known Template/STL
   backlog class), Lit_NG/Shader `operator*` (known floors — certify-or-skip).
4. **check_native_compiles.sh path bug**: `../../milo-native-engine` resolution is wrong inside
   `wt/` worktrees (lane A worked around with -DMILO_ENGINE_PATH; fix the script).
5. Native backlog notes from lane D flag 4 (VelocityBuffer AdvanceFrame pre-existing native
   behavior note) and the optional visual checks (crown HUD screenshot series, Kinect basis).
6. Stale `wt/arch*` + `dc3-sweep*` worktrees (~26) still parked — remove when convenient
   (main build.ninja verified uncontaminated).
