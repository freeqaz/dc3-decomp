# 90 — Roadmap to Done

Synthesis of the 12-doc audit (`docs/investigations/2026-06-10-roadmap-to-100/01..12`) + the adversarial verification log
(`docs/investigations/2026-06-10-roadmap-to-100/13-verification-log.md`). This is the credible path to "done" = 100%
matched with only certified-cosmetic mismatches remaining.

**Project constraint (governs sequencing):** near-term implementation is
**tooling-only + minimal very-high-ROII decomp fixes**. The bulk frontier grind and
port lanes are queued for agent execution, not blocking. Phase 0 (measurement) and the
tooling backlog come first because they gate every "how done are we / what's left"
number below.

---

## Definition of done

Synthesized from doc 08 (floor-vs-routable), doc 05 (zero-census), doc 06 (unit-gap),
doc 02 (objdiff semantics).

> **DONE** = every *authorable* function (unit NOT under `default/xdk/*` or
> `default/lib/*`, and not a `merged_`/`lbl_`/`fn_`/`??_*` compiler artifact) is either
> **(a)** `match_percent_normalized == 100` (the objdiff normalized scorer, the canonical
> gate — NOT fuzzy, NOT byte-identity), **(b)** `is_stub = 1` (explicitly deferred), or
> **(c)** carries a **floor certificate** (run_objdiff-normalized <100 AND
> unicorn-EQUIVALENT or artifact-class or permuter-exhausted AND only cosmetic diff
> classes present).
>
> Data symbols (vtables/RTTI/static blobs) are **out of scope for v1** — they are
> currently unmeasurable (doc 06 F8: jeff ICF target-symbol resolution makes
> matched_data=0.08% meaningless). They enter a v2 bar only after the data-measurement
> chain is fixed.

**What "100% normalized" actually guarantees** (doc 02, all VERIFIED): right opcodes,
right registers up to regalloc permutation, right immediates/offsets/vtable slots, right
relocation TYPE — but NOT a verified-correct relocation TARGET (callee/data symbol), and
NOT byte-identity. The residual cosmetic floor = register permutation + branch layout +
benign reloc-addend noise.

### The true denominator (6 docs agree; arithmetic VERIFIED)

| Metric | Value | Source |
|---|---|---|
| Total XEX code | 11,379,348 bytes | report.json |
| **Non-authorable** (XDK + Bink + SDK) | ~5.03 MB (44%) | docs 05/06/08 |
| **Authorable code** | ~6.35–6.43 MB | docs 05/06/08 |
| Matched (of authorable) | **77.5–78.5%** | docs 05/06/07/08 |
| **Remaining authorable work** | **~1.44 MB** | doc 03 (both planes agree to 0.23%) |
| The XDK-diluted headline (deprecate) | 43.8% | report.json matched_code_percent |

### Expected residual floor at "done" (doc 08, VERIFIED ceiling)

- **~1,000–1,150 functions / ~700–900 KB will legitimately sit <100%**, almost all
  certifiable cosmetic floor.
- The current authorable partial frontier is **1,699 fns / 1,127,844 bytes**; of the
  tested subset ~65% are floor (unicorn EQUIVALENT or artifact class), ~10% routable,
  ~26% untested.
- **True remaining *work*** (push everything fixable to 100%): **~150–650 fns /
  ~100–350 KB** = 650 small zero-starts (53,616 bytes) + ~25-167 routable + adjudicated
  unknowns + ~27 hard real-bug residue (~13 KB).

---

## Phase 0 — Make the measurement trustworthy (GATES EVERYTHING)

The audit cleared the target side (jeff) and the scorer semantics, but found
optimistic DB drift, an uncounted false-100% risk, and an XDK-diluted headline. Close
these before counting anything as "remaining work."

| # | Item | Impact (fns/bytes) | Effort | Executor | Evidence |
|---|---|---|---|---|---|
| 0.1 | **Re-anchor the headline to authorable %** (exclude `default/xdk/*`, `default/lib/*`) in `measure_progress.sh` + report aggregator. 43.8% → ~77.5%. | Reframes entire backlog; 0 code | low | tooling | docs 05/06/07/08 (VERIFIED) |
| 0.2 | **Switch sync to normalized scoring** — `sync_match_percent.py:84` read `match_percent_normalized` (or dual-store), key `--promote` off normalized==100. Re-promotes 206 secretly-complete fns. | +206 fns to done, 0 decomp | low | tooling | doc 04 F2, doc 02 F1 (VERIFIED) |
| 0.3 | **Add reconcile.py drift detector** (nightly / ninja-postbuild / pre-commit). Fails loudly on: db.current vs report.fuzzy ≥0.5 (catches 639 FALSE-COMPLETE); verdict=COMPLETE AND <100 (catches 20); is_stub=1 AND ≥100 (catches 1,728); report-only symbols >0. | Stops 639+20+1,728 stale rows growing | low | tooling | doc 03 F4/F6/F8, doc 04 (VERIFIED arithmetic) |
| 0.4 | **Add a demote path to sync** (COMPLETE→NULL when current<100; flag report-absent rows). | Fixes 20 stale COMPLETE permanently | low | tooling | doc 03 F1/F4 |
| 0.5 | **Strict re-certification: add `FunctionRelocDiffs::NameOnly`** (name+section match, ignore addend) to objdiff-core; emit report_strict.json; diff vs report.json to enumerate the genuine false-100% (wrong-call-target) population. This is the ONLY uncounted measurement risk. | Quantifies the bounded-but-unknown false-100% set | medium | opus-agent | doc 02 F2/F3, claim #7 (VERIFIED) |
| 0.6 | **Reloc-drop classifier** (scripts/analysis) over the 11,052 lenient-100/strict-<100 fns: label each reloc name-mismatch (real) vs addend-only (benign), produce count + list. | Turns "UNQUANTIFIED" into a number | medium | sonnet-agent | doc 02 tooling-gap (VERIFIED 11,052) |
| 0.7 | **Clear stale is_stub / re-verdict AT_LIMIT** in a pure-SQL nightly reconciler: clear is_stub when current≥100 (1,728 rows); demote high-% AT_LIMIT (1,295 rows ≥85%) to NEEDS_REVERDICT. | Shrinks apparent floor by ~1,295, apparent stub backlog by ~1,728 | low | tooling | doc 04 F3/F4 (VERIFIED) |
| 0.8 | **Re-run unicorn on current src/** with a source-hash freshness gate (3-month stale, blind to all June wins). | Restores the behavioral plane; gates native-bug hunting | medium | tooling | doc 04 F6, doc 11 F6 |
| 0.9 | **PROGRESS_METRICS.md** — one doc reconciling the 4 headline numbers, naming the canonical figure (authorable normalized %), documenting None vs name_address vs DataValue. | Stops scout-number drift | low | sonnet-agent | doc 02 tooling-gap |
| 0.10 | **DO NOT re-split DC3 for match% gains** — the four recent jeff fixes are confirmed no-ops/already-applied (RB3-xenon-motivated). | Avoids wasted cycles | zero | human | doc 01 F1/F2 (VERIFIED) |

**Phase 0 net:** ~+206 fns to done from 0.2, ~1,748 false rows corrected, the false-100%
risk quantified, headline re-anchored — all with **zero new decomp work**. This is the
highest-ROI block in the roadmap.

---

## Phase 1 — Structural levers (one-fix-many-functions)

**KEY FINDING (doc 07, VERIFIED direction): there is NO undiscovered struct/vtable/sizeof
structural lever in the game frontier.** Clustering the 85-100 frontier by class shows
high within-class variance with a near-100% member in every cluster — the opposite of a
uniform layout-error depression. The shared-cause errors (`__FILE__` paths,
`_MemAllocTemp`) were already fixed; the rest (ICF, FormatString stack frame,
block-sinking) are cataloged UNFIXABLE. Build-env is clean (doc 10, VERIFIED: 0 wrong-flag
units).

So Phase 1's "levers" are **measurement/pairing levers**, not source levers:

| # | Item | Impact | Effort | Executor | Evidence |
|---|---|---|---|---|---|
| 1.1 | **The denominator re-anchor (0.1) IS the biggest lever** — reframes 43.8%→77.5% with no code. | Whole backlog | low | tooling | docs 05/06/07/08 |
| 1.2 | **Funclet pairing reconciliation** — re-run/bump objdiff funclet pairing to reconcile the ~232 unpaired `fn_` EH funclets (same mechanism that already paired 1,328). Non-authorable; cleans the denominator tail. | ~232 fns / 17 KB (count, not source) | low-medium | tooling | doc 05 F5, doc 07 gap (VERIFIED 1536/233) |
| 1.3 | **Recert ~71 single-blocker units already at rounding-100%** — pure metric correction, near-zero decomp, raises complete_units honestly. | ~71 units | low | tooling | doc 06 F7 |
| 1.4 | **/Od global-reload lever on keygen_xbox** (17 sub-100 fns) — the one banded unit; flag is confirmed correct; clean test bed for the global-reload source-shaping lever. | ~17 fns | low | opus-agent | doc 10 §3 (VERIFIED no wrong flag) |
| 1.5 | **DO NOT hunt for struct/vtable/wrong-flag levers** — falsifiable tests return zero candidates (docs 07 F3, 10 §2). | Avoids wasted budget | zero | human | docs 07/10 (VERIFIED) |

There is **no batch fix** for the 1,221 near-miss functions; they are per-function
(Phase 3).

---

## Phase 2 — Bulk lanes (port / whole-unit authoring)

Ordered by bytes-per-effort. All lanes verified small relative to prior claims — our
source has largely surpassed og.

| # | Item | Impact | Effort | Executor | Evidence |
|---|---|---|---|---|---|
| 2.1 | **og-dc3 stub port — native-safe half first** (Xbox-only files: PlatformMgr_Xbox, NetworkSocket_Win, synth_xbox/Fx*, json-c). Verbatim port, re-run_objdiff per fn. | ~half of ~186 net-new stubs / ~22 KB | medium | sonnet-agent | doc 09 §2/§4 (UNVERIFIED ~186 — re-derive via og_coverage.py) |
| 2.2 | **og-dc3 stub port — cross-platform half** (Mic, VoiceControlPanel, ShellInput, char/*): diff our source vs og FIRST, graft og bodies only under `#ifndef HX_NATIVE`, keep native path under `#ifdef HX_NATIVE`. Regression risk if guards dropped. | ~90 fns / ~21 KB | medium-high | opus-agent | doc 09 §4 (port procedure), MEMORY og-port-drops-guards |
| 2.3 | **6-unit whole-unit DSP lane** (mkfilter/complex, EnvelopeGenerator, DelayEffect, VorbisMem, CompressionEffect, Common_Xbox) — units we lack the object for; native-safe quick win. | 25 fns | low | sonnet-agent | doc 09 §3 (UNVERIFIED) |
| 2.4 | **The ~100 game-relevant authorable zero-bucket stubs** (os/PlatformMgr::Poll, gesture/DepthBuffer3D, moviebink, midi/DisplayEvents, json-c) — is_stub native placeholders; port from og or write fresh. | ~100 fns / ~25 KB | medium | sonnet-agent | doc 05 F6/F9 |
| 2.5 | **DO NOT port the 95-100 near-miss og cohort** (~159 fns) — our source has surpassed og; these are floors/permuter territory, not port targets. | Avoids regressive effort | low | human | doc 09 §4 (B), §4 conclusion |
| 2.6 | **synth_xbox (223) + rnddx9 (72) Xbox-HW backends = binary-match-only** — tag them so native effort (miniaudio/WebGPU) isn't spent here. | Prevents ~87 KB misdirection | low | tooling | doc 05 F4/F9 |

---

## Phase 3 — Frontier grind + floor certification

The bulk of genuine remaining work. The win is **certifying** the cosmetic 65%, not
re-attempting it; then grinding the routable remainder.

| # | Item | Impact | Effort | Executor | Evidence |
|---|---|---|---|---|---|
| 3.1 | **Add floor_certificate columns + certify_floor.py.** Mark certified iff run_objdiff-normalized<100 AND (unicorn EQUIVALENT OR artifact class OR permuter-exhausted) AND only cosmetic diff classes; store cert pct + build hash for invalidation. Converts ~65% of the frontier from perpetually-re-attempted into auditable done. | ~809+ fns certified done | medium | tooling | doc 08 §4/§5, schema (VERIFIED ceiling) |
| 3.2 | **Run unicorn over the 436 untested partial-frontier fns** (avg 85.8%) to close the certification gap and surface real bugs hiding among them. | 26% of frontier judged | medium | sonnet-agent | doc 08 §3 (VERIFIED 436) |
| 3.3 | **Re-diagnose + route the 283 non-stub AT_LIMIT 40-85 fns** (~172 KB). 8/8 sampled routable (logic/structure/decl-order). The June archaeology agents clear this band at +20-50 pts/fn. | 283 fns / 172 KB drainable | medium | opus-agent | doc 04 F5 (UNVERIFIED-sampled), MEMORY asm-archaeology |
| 3.4 | **Re-verdict the 1,946 never-attempted AT_LIMIT rows** (44% of pool, zero attempts-table rows) — cheap diagnose pass to separate true floor from auto-labeled-but-workable. | Corrects floor estimate | medium | sonnet-agent | doc 04 F7 (VERIFIED 1,946) |
| 3.5 | **Grind the 1,221 near-miss game fns (90-99.99%, ~821 KB)** per-function via permuter/asm-archaeology. No batch lever. | Bulk of genuine work | high | opus-agent | doc 07 F7, doc 08 §3 (VERIFIED 1,699 frontier) |
| 3.6 | **Adjudicate the ~113 call_count <99% fns** (~95 KB) — separate merged-call/inlining artifact from genuine call-arg/count bugs. Where real routable work hides. | ~113 fns / ~95 KB | medium | opus-agent | doc 08 §6, claim #12 (VERIFIED) |
| 3.7 | **Fix the ~27 hard real-bug residue** (error/call_arg/object_memory/return_value, ~13 KB) — only non-artifact behavioral mismatches on the frontier. | ~27 fns / ~13 KB | low | opus-agent | doc 08 §6, claim #12 (VERIFIED, residue=25-27) |
| 3.8 | **Clear the 650 authorable 0% non-stub fns** (avg ~82 bytes, 53,616 bytes) — batch decomp sweep to drive count toward 100%. | 650 fns / 53 KB | medium | sonnet-agent | doc 08 §2, claim #13 (VERIFIED 650) |
| 3.9 | **Diagnose-then-certify-floor the SIMD/platform partial-floor units** (synth_xbox/FFT 16%, Mic 33%, Synth 44%, rndobj/Shader 66%, os/PlatformMgr_Xbox 46%) — store floor certs with diagnose evidence rather than chasing to 100%. | ~5 units floored | medium | opus-agent | doc 06 F6, doc 08 §8 |

---

## Native-port bug burndown (parallel track)

Independent of binary matching — these execute in the live native renderer/IK/clip
paths. The funnel is **182 → 153 → 53** (sub-100 real-bug DIVERGENT → native-compiled →
zero-HX_NATIVE-guard). The 53 zero-guard set (43 KB) is the defensible live-bug list.

| # | Item | Impact | Effort | Executor | Evidence |
|---|---|---|---|---|---|
| N.1 | **Compile json-c into native** (`src/system/net/json-c/*.c` — exists, just not in native/CMakeLists). Unblocks RockCentral/leaderboard/MOTD/store JSON currently silently parsing to 0. | High — all online meta data | low | sonnet-agent | doc 12 F5 (UNVERIFIED — re-confirm compile gap) |
| N.2 | **HX_STUB_TRACE macro + /api/stubs** in the engine_stubs_generated.cpp generator; run a gameplay session to produce an evidence-ranked native-stub burndown (171 silent stubs, 1 warn today). | Converts 171 silent stubs to ranked worklist | low-medium | sonnet-agent | doc 12 F4/F8 (UNVERIFIED) |
| N.3 | **Fix CharIKFoot::DoFSM int-vs-float field at 0x30/0x34** + trace why HamIKEffector::mConstraints is never populated. Strongest lever for the feet-in-floor bug (failing test FeetNotBelowFloorDuringGameplay). | High — feet bug | medium | opus-agent | doc 12 F6, doc 11 F7 (UNVERIFIED — confirm field type live) |
| N.4 | **Fix the 53 zero-guard live native bugs**, starting DecodeDxt5Alpha (DXT5 alpha branch polarity, CONFIRMED), ClipCollide::Collide, CharClipDisplay::SetStartEnd, RndLight::Load. Each verified by run_objdiff AND a new milo-tests unit test. | High — visual/IK/clip paths; 53 fns / 43 KB | medium | opus-agent | doc 11 F5/F7 (split VERIFIED) |
| N.5 | **native_body_overridden flag** (per-fn: is source span inside an HX_NATIVE block) to exclude rewritten bodies (MemMgr) from the native-bug dashboard. NOTE: MemAlloc rationale in doc 11 F5/F7 is REFUTED (claim #15) — MemAlloc's body is an UNGUARDED malloc() stub, not bypassed by a guard; it's still excluded but as undecompiled-Xbox-allocator, not guard-bypassed. | Cleans 577→~53 dashboard | low-medium | tooling | doc 11 F5, claim #15 (REFUTED rationale) |
| N.6 | **Per-fn verification of the 100 guarded-file divergent fns** — is the divergent line under a guard (moot) or shared (live bug)? | Resolves 53↔153 | low-medium | sonnet-agent | doc 11 F5 |
| N.7 | **Regression policy:** every confirmed logic divergence (diagnose shows branch-polarity or compare-signedness, not pure regalloc) gets a milo-tests unit test pinning behavior to original. | Durable coverage | low | human | doc 11 F8 |
| N.8 | **RockCentral::ManageJob native stub** (TODO.md:266) — HX_NATIVE delete-and-return crashes on SendDropInDatapoint at game start. | Removes a known crash | medium | opus-agent | doc 12 ranked-list #5 |

---

## Tooling backlog (consolidated, deduplicated, ranked by leverage)

Near-term work is tooling-only + minimal very-high-ROI decomp. This is the consolidated
list from all 12 docs' tooling_gaps, deduped.

### Tier 1 — measurement integrity (do first; gates the numbers)

1. **reconcile.py drift detector** (Phase 0.3/0.4/0.7) — catches 639 FALSE-COMPLETE, 20
   stale COMPLETE, 1,728 stale is_stub; demote path; report-only guard. *(docs 03, 04)*
2. **Sync on normalized** + dual-store fuzzy/normalized; key promote off normalized.
   *(docs 02, 04)*
3. **FunctionRelocDiffs::NameOnly mode** in objdiff-core + report_strict.json + reloc-drop
   classifier — quantifies the false-100% wrong-call-target set. *(doc 02)*
4. **Authorable-denominator metric** (`--sourced-only`/subsystem mode excluding
   `default/xdk/*` + `default/lib/*`) in measure_progress.sh + report aggregator;
   PROGRESS_METRICS.md naming the canonical figure. *(docs 05, 06, 07, 08; the single
   most-requested gap — appears in 6 docs)*
5. **report_raw/report_strict mtime-parity assertion** — make strict refresh on the same
   incremental step as report.json (avoids the spurious-stale ArcDetector class of
   error). *(doc 02)*

### Tier 2 — DB schema & census hygiene

6. **floor_certificate columns + certify_floor.py** (TEXT enum: equivalent | artifact:<class>
   | permuter_exhausted | pgo_block_sink | icf_merged; floor_cert_pct; floor_cert_build;
   floor_cert_at). Makes "done with only cosmetic" auditable. *(docs 06, 08)*
7. **is_real_function / status-enum column** (NOT merged_/lbl_/fn_/??_/excluded; UNSCORED
   / ZERO / PARTIAL / COMPLETE) so census stops counting artifacts as work and NULL≠0 is
   explicit. *(docs 05, 07, 09 — NULL-vs-0 conflation appears in 4 docs)*
8. **Backfill exclusion_reason / verdict provenance** (verdict_set_at, verdict_tool_version,
   attempted-boolean from attempts table) so AT_LIMIT-as-floor is only honored with a
   recent diagnose behind it. *(docs 04, 05)*
9. **Canonical "done" SQL view** (excluded=0, NOT xdk/lib, NOT merged_*, normalized==100
   OR is_stub OR floor_certificate) wired into query_functions + progress skill. *(docs 03, 06)*
10. **best_percent provenance fix** — document/rename to external_reference_percent;
    exclude AT_LIMIT/is_stub from any lost-win query (only the 130-fn [99,100) band is
    real). *(doc 03)*

### Tier 3 — pairing, ports, native

11. **Funclet pairing re-run / objdiff pin bump** — reconcile ~232 unpaired EH funclets
    (Phase 1.2). *(docs 05, 07)*
12. **scripts/og_coverage.py** — continuous og cross-reference (DB join + xbox_only +
    external_method tagging; gate `?X@@YA` statics on post-port run_objdiff). Stops the
    ~190 number drifting. *(doc 09)*
13. **HX_STUB_TRACE + /api/stubs** native stub tracer (Phase N.2). *(doc 12 — "single
    highest-leverage native tooling item")*
14. **native_body_overridden flag** + native-risk dashboard query (Phase N.5). *(docs 11, 12)*
15. **Link-time warning** when a weak engine_stubs_generated.cpp stub is the final
    definition of a symbol with a real .c/.cpp in tree (the json-c failure mode). *(doc 12)*

### Tier 4 — build-env guards (standing tripwires; low urgency, build is clean today)

16. **verify_split_integrity** check (0 overlaps, 0 zero-size, 0 vftable_, 0 prune/clamp
    log lines, proposed_splits 0) wired into the split rule/CI; **content-hash target
    manifest + assert_targets_current** (mtime is unreliable); **rebaseline_targets.sh**
    that prunes orphan objs. *(doc 01)*
17. **scripts/audit_unit_flags.py** — assert each unit's flags = library-inherited + only
    the 3 sanctioned overrides. *(doc 10)*
18. **Fix clean_stale_objects.sh** — restrict PCH-mtime check to msvc_pch-rule objs; add
    obj-vs-own-cpp pass (removes 26 false positives). *(doc 10)*
19. **Build-determinism CI check** — compile a TU twice, compare after masking COFF
    offsets 4-7. *(doc 10)*
20. **Coerce report.json measures to float** at load + schema unit test (they are JSON
    strings; naive arithmetic raises). *(doc 03)*

---

## Sequencing summary

1. **Phase 0 (tooling)** — re-anchor headline, sync on normalized, reconcile.py, strict
   re-cert. Zero decomp, corrects ~1,748 stale rows + 206 promotions + quantifies the only
   false-100% risk. **Do this first.**
2. **Tier 1+2 tooling** — floor_certificate + status-enum + canonical view. Makes "done"
   queryable.
3. **Phase 1.2/1.3/1.4** — funclet pairing, recert single-blockers, keygen /Od lever
   (small, high-ROI decomp).
4. **Native track (N.1, N.2)** in parallel — json-c compile + stub tracer (low effort,
   high quality impact).
5. **Phase 3 + Phase 2** — frontier grind, floor certification, og ports (agent-driven,
   the long tail).


