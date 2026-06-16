# Wave 9 — measurement-trust fix, structural og audio port, behavioral bugs

> **Authorship note:** the wave-9 work and the first draft of this write-up were
> done by **Fable** (Claude Fable 5) acting as research + orchestrator over Opus
> implementer subagents. Continued under Opus 4.8. The wave landed on `main`
> 2026-06-11 (commits `ca13e0eb`..`5bde2f91`); this session doc was filed
> 2026-06-16. Full result tables live in
> `docs/investigations/2026-06-10-roadmap-to-100/99h-WAVE-9-RESULTS.md`; this doc
> is the *lessons* companion.

## What wave 9 was

After waves 1–8 declared the broad-sweep format exhausted (frontier
floor-dominated), wave 9 deliberately ran **narrow** lanes only, per wave-8's
recorded recommendation:

- **Lane A** — the coordinated *structural* og audio port (XAUDIO2/xapobase +
  dsp/StandardEffect), the recorded highest-EV remaining lever.
- **Lane B** — the three unlanded behavioral bugs from the 2026-06-09
  asm-archaeology session + a ShaderProgram::Cache archaeology pass.
- **Lane D** — a read-only audit of the nine accumulated native review flags.
- **Lane H** — (added mid-session) the "hidden frontier" surfaced by the
  measurement fix below.
- **Lane C** — orchestrator-inline DB hygiene (single-writer runbook).

Outcome: **+71 fns to 100% (audio), 3 behavioral bugs closed, 1 sleeper vtable
bug found, ObjPtrList::sort ×3 → 100%**, and a measurement correction that moved
the honest done-with-certs number to **99.04% fns / 96.73% bytes** (open 264 fns).
Suite 418/418, web green, PPC ninja green.

---

## Lessons worth keeping

### 1. A LIKE-prefix filter with `_`/`%` in the prefix is a silent data-loss bug

`certify_floor.py` excluded artifact symbols with `symbol NOT LIKE '??_%'`. SQL
`LIKE` treats `_` as a single-character wildcard, so that clause excluded **every**
`??`-prefixed symbol — ctors (`??0`), dtors (`??1`), operators (`??4`/`??O`),
templates (`??$`) — not just the literal `??_` vtable/RTTI prefix. **6,835
authorable functions / ~1.0 MB** were invisible to the done-view, the cert census,
and *every band query derived from them*. 112 were open frontier no wave had ever
seen.

This is the most important finding of the wave because it is a **measurement-plane
lie**, the class the 2026-06-10 audit was specifically built to hunt — and it
survived that audit. The fix (`like_prefix_clause()` with `ESCAPE '~'`) is trivial;
the lesson is the discipline:

- **Any MSVC-mangled-symbol filter must escape `_` and `%`.** Mangled names are
  *full* of underscores. A `LIKE` over them without `ESCAPE` is almost always wrong.
- **Prefer Python `startswith` / a parametrized `GLOB`** over `LIKE` for prefix
  tests on symbol columns. `reconcile_db.py` and `progress_metrics.py` used
  `startswith` and were correct; `certify_floor.py` used `LIKE` and was not.
- **Cross-check denominators against an independent counter.** The bug was found
  by noticing the cert census frontier total disagreed with a hand `COUNT(*)`.
  Two independent paths to the same headline number would have caught this waves
  earlier.

**Tooling action (open):** grep the whole `scripts/` tree for `LIKE '` over
`symbol`/`demangled` columns and convert any prefix test to escaped form or
`startswith`. (Audited in-wave: certify_floor fixed; reconcile_db, progress_metrics,
atexit_fuzzy_verify clean. Not yet swept: the orchestrator MCP query layer,
`query_functions`, `find_hidden_work.py`, any ad-hoc analysis scripts.)

### 2. A lane's own green gates do not prove a cross-cutting header edit is safe

Lane A built clean, passed `check_native_compiles`, and reported milo-tests green
*in its worktree* — while excluding "10 pre-existing char crashers." On merged
`main` the suite failed **6 tests** (DtaIncludeTest ×5 + MiloViewerPosePipeline),
and the "pre-existing crashers" were the same corruption wearing a different hat.

Root cause: the lane moved `tWAVEFORMATEX` out of `xaudio2.h`'s
`#pragma pack(push,1)` region into `xapo.h` **unpacked** — `sizeof` drifted
0x12 → 0x14, so every consumer TU disagreed on struct layout. (og's `xapo.h`
carries the identical latent bug.)

Lessons:
- **The decomposition test runs on MERGED main, not in-lane.** Transiently revert
  each suspect hunk on the integrated tree, rebuild, re-measure, attribute. This
  is already standard for match-% regressions on shared headers (it caught the
  488-regression MakeString toggle); wave 9 proves it must also gate **native/test
  correctness** for header moves, not just match%.
- **"Pre-existing failures excluded" from a lane report is a red flag**, not a
  reassurance. Reproduce the baseline yourself before accepting the exclusion.
- **When you move a struct between headers, the `#pragma pack` context moves with
  it or the size changes silently.** There is no diagnostic; only a layout-sensitive
  consumer (here, a serialized-asset parser and a pose pipeline) surfaces it.

### 3. "Match-regressing behavioral trade" estimates go stale — re-derive them

The registry said HolmesClientOpen's correct (filename-passing) `MILO_FAIL` form
cost −12.8pp (77.2 → 64.4). The actual right source form **improved** match
(77.2 → 79.3). The old number came from one naive attempt months earlier on a
different surrounding-code baseline. A "trade" recorded once is a hypothesis, not a
fact — re-measure before treating it as a constraint, especially after the
surrounding function has moved.

### 4. Some real bugs are invisible at the normalized metric — use the raw reloc plane

Splash::Suspend/Resume both read **100.0% normalized before and after** the fix.
The bug — the callsites dispatched the *opposite* DxRnd virtuals (the splash
thread owns the device during loading, so suspending the splash resumes the main
renderer) — lived entirely in the relocation plane that normalized scoring
discards. It only surfaced via `run_diff_inspect diff_mode=raw` showing
`lwz r11, 0x13c` vs `0x138` on the vtable load.

Lesson: **for virtual-dispatch correctness, normalized 100% is not proof.** A
periodic raw-mode pass over vtable-load sites (or any function whose only residual
is "address relocation noise") can catch wrong-target dispatch that the headline
metric certifies as done. This is the same wrong-call-target false-100% risk the
audit flagged for the lenient reloc mode — here it bit a *source* callsite, not the
scorer.

### 5. Fixing a measurement bug is itself decomp progress

The `??_%` fix didn't just correct a number — it **surfaced 112 workable functions
no band query had ever returned**, and lane H immediately took 4 of them to 100%
(ObjPtrList::sort ×3 via one shared-header forward-vs-backward iteration fix;
FastSort<3> +10.9). When the frontier looks exhausted, audit the lens before
concluding the work is done. "The remaining set is floor-dominated" was true *of
the set the queries could see* — and the queries were lying.

---

## Tooling improvements (landed + proposed)

**Landed this wave:**
- `certify_floor.py` `like_prefix_clause()` with `ESCAPE` (fixes the bug; applied
  to `is_authorable_sql`, the view DDL, and the `done_summary` fallback).
- `certify_floor.py --manual-file <json>` mode for orchestrator-supplied floor
  certs (worktree-permuter evidence that never reached the main attempts table);
  refuses ambiguous symbols, existing certs, and pct-drift; records *current*
  normalized pct per the reconcile check-(e) contract.
- `data/exclude_drift_rows.sql` — idempotent drift exclusion (link_glue zero-starts
  + thunks + the Matrix3-Multiply cross-unit COMDAT mis-attribution).

**Proposed (open follow-ups):**
- **SQL-`LIKE`-over-symbols audit** across `scripts/` and the orchestrator query
  layer (lesson 1). Highest priority — it's a measurement-trust hole.
- **`check_native_compiles.sh` worktree path bug**: `../../milo-native-engine`
  resolves wrong inside `wt/` worktrees; lane A worked around it with
  `-DMILO_ENGINE_PATH`. Make the script resolve the engine path absolutely.
- **Decomposition-test-on-merged-main as a harvest step**: bake a "for each
  shared-header hunk, revert/rebuild/run-suite on integrated main" gate into the
  wave harvest protocol, not just the per-lane verifier (lesson 2).
- **A raw-mode vtable-dispatch sweep tool**: enumerate functions at 100% normalized
  whose raw residual is an `lwz rN, 0xNN(r12)`-style vtable-load arg diff, and
  flag them for wrong-target dispatch review (lesson 4).
- **Two-path denominator check** in `progress_metrics.py` / `certify_floor.py`:
  compute the authorable total two independent ways and assert equality, so a
  filter bug like lesson 1 fails loudly instead of silently undercounting.

---

## State at end of wave 9

- **Done-with-certs: 99.04% fns / 96.73% bytes; open 264 fns / 193,916 bytes**
  (honest denominator, post-`??_%` fix + 31 drift exclusions).
- Suite milo-tests 418/418 0 FAIL; check_native_compiles PASS; web release green;
  PPC ninja green; reconcile_db 0 drift.
- **Wave-10 narrow candidates** (carried in 99h doc): EQ/Wah/Reverb
  SyncEffectParams (gated on EQEffect::Params 15-vs-14-field layout from target
  asm); ShaderProgram::Cache 85.7 residual; MemTracker STL template cluster;
  the SQL-LIKE audit; the check_native_compiles path fix; ~26 stale
  `wt/arch*`/`dc3-sweep*` worktrees to remove.
