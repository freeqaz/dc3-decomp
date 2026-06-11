# 00 — Audit Index: DC3 Decomp Measurement-Trust & Roadmap-to-Done

12-agent audit answering: (1) is the measurement trustworthy? (2) what gaps remain?
(3) what structural fixes flip many functions? (4) what decomp bugs affect the native
port? Plus synthesis: a verification log and a roadmap to "done."

## Trust status of the measurement chain

> **TRUSTWORTHY, with 3 bounded/documented caveats.** jeff (target side) is CLEARED —
> a re-split at HEAD is byte-identical (doc 01, VERIFIED). The objdiff scorer semantics
> are sound and the immediate-diff change already TIGHTENED the metric (doc 02,
> VERIFIED). The build env is clean — no wrong-flag units, no stale objects, deterministic
> modulo COFF timestamp (doc 10).
>
> Caveats, all closed by Phase 0 of the roadmap: (a) **wrong-call-target false-100%** is
> possible under the lenient reloc mode fed to the DB — bounded but currently *uncounted*
> (doc 02 F3); (b) **decomp.db is optimistically drifted** vs report.json — sticky
> COMPLETE (639+20 rows), stale is_stub (1,728), stores fuzzy not normalized (206
> rows) (docs 03/04); (c) **the 43.8% headline is XDK-diluted** — real authorable match
> is ~77.5-78.5% (6 docs agree).

## Headline numbers (the corrected denominator)

| | Value |
|---|---|
| **True denominator** (authorable, excl XDK+Bink+SDK) | ~6.35–6.43 MB (the other ~5.03 MB / 44% is un-authorable vendor code) |
| **Matched of authorable** | **77.5–78.5%** (NOT the 43.8% XDK-diluted headline) |
| **Real remaining work** | **~1.44 MB** (both DB and report planes agree to 0.23%) |
| **Authorable partial frontier** | 1,699 fns / 1,127,844 bytes |
| **Expected floor at done** | ~1,000–1,150 fns / ~700–900 KB, almost all certifiable cosmetic |
| **True remaining *work*** | ~150–650 fns / ~100–350 KB |

## Doc-by-doc index

| Doc | Key takeaway |
|---|---|
| **01-measurement-jeff-split** | jeff CLEARED. Re-split at HEAD = byte-identical (2223 same, 0 diff). The 4 recent fixes are no-ops/already-applied for DC3 (RB3-xenon-motivated). 1536 fn_addr = MSVC EH funclets (~233 stuck at 0% inflate the denom). Do NOT re-split for match% gains. All claims VERIFIED. |
| **02-measurement-objdiff-fork** | Scorer sound. report.json uses reloc-mode None → forgives ALL reloc *targets* (matched_code 2.29x inflated vs strict, but ~benign addend noise). Genuine residual: wrong-call-target false-100%, bounded+uncounted. f62bc9c TIGHTENED the metric (no longer hides wrong constants/offsets/vtable slots). 3 coexisting headline numbers. All claims VERIFIED. |
| **03-db-vs-report-reconciliation** | The two planes reconcile to 0.23%. Real remaining ≈1.44 MB non-SDK. Bugs: 639 FALSE-COMPLETE rows (47 KB say 100%/report 0%), 20 stale COMPLETE, sync never demotes. best_percent is externally seeded — don't mine for lost wins except the [99,100) band. Arithmetic UNVERIFIED-but-cross-corroborated. |
| **04-verdict-freshness-sample** | current_percent is FRESH; the *verdict label* is stale. DB stores fuzzy not normalized (206 fns secretly complete). is_stub 64% false (1,728 at 100%). AT_LIMIT is the worst column (29% ≥85%, 44% never attempted). AT_LIMIT 40-85 band is 8/8 ROUTABLE. unicorn 3 months stale. |
| **05-zero-percent-census** | The "19,626 @ 0% / 5.44 MB" is a category error: 86% excluded=1, lives entirely under xdk/ + binkxenon/ (no source). Only ~407 authorable / 116 KB; ~100 game-relevant / ~25 KB. TRUE authorable denom = 6.35 MB → 78.5% matched. All numbers VERIFIED-aligned. |
| **06-unit-gap-census** | Real game match = 77.76% (not 43.8%). complete_units=968 is a stale objdiff.json allowlist (ignores data, treats AT_LIMIT as done). Frontier = 2,416 non-stub fns / 1.21 MB; 61% sit in ≥95% units (last-mile). 153 units are 1 fn from complete (71 recert-only). Data uncertifiable (matched_data=0.08% is jeff ICF noise). |
| **07-structural-flip-levers** | NO undiscovered struct/vtable/sizeof lever exists — clustering shows per-function regalloc, not shared layout depression. The fixable shared-cause errors are already applied; the rest are UNFIXABLE floors. Biggest "lever" = reporting the right (game-only) denominator. claim VERIFIED (direction). |
| **08-floor-vs-routable** | Defines the CEILING. Frontier 1,699 fns / 1.13 MB; ~65% certifiable cosmetic floor (unicorn EQUIVALENT), ZERO logic-class DIVERGENT. "routable" is mostly call_count emulation artifact; hard residue ~27 fns / ~13 KB. No floor_certificate column exists. Ceiling + residue claims VERIFIED. |
| **09-og-dc3-port-lane** | Same XEX, comparable 100% — but jeff partitions objects differently so only external class methods transfer (statics don't). Honest lane = ~186 net-new stub fns / ~22 KB (NOT 190 big-byte matches). Whole-unit lane = 6 DSP units / 25 fns. Our source has surpassed og on near-misses. UNVERIFIED (re-derive via og_coverage.py). |
| **10-build-env-audit** | NO per-unit flag/PCH corruption. Flags are per-library uniform + 3 sanctioned overrides. 95.4% bimodal at 0%/100% (healthy). 0 truly-stale objs. Deterministic modulo COFF timestamp. keygen_xbox (/Od) is the one banded unit — flag is correct, residual is /Od global-reload source-shaping. |
| **11-native-unicorn-divergences** | NO `logic` unicorn_class exists (scout framing wrong). 395 of 577 real-bug DIVERGENT are at 100% = false positives (trust only when <100). Funnel 182→153→53; the 53 zero-guard fns (43 KB) are the defensible live-bug set. 2 confirmed: DecodeDxt5Alpha, ResetNormals. Split VERIFIED; **MemAlloc-bypass rationale REFUTED** (see doc 13). |
| **12-native-stub-intersection** | is_stub is a decomp-TARGET (Xbox) signal, disjoint from native runtime stubs. Of 677 real 0% stubs, only 42 native-compiled (mostly STL/guarded). The REAL native stub surface = 171 silent return-0 stubs in engine_stubs_generated.cpp. json-c not compiled → all online JSON parses to 0. IK feet bug = DoFSM int/float field + mConstraints wiring, NOT a stub. UNVERIFIED (source-grep based). |
| **13-verification-log** | Adversarial pass: 15 VERIFIED, 1 REFUTED (doc 11's MemAlloc-bypass rationale — numbers hold, mechanism wrong). Net: measurement chain trustworthy with the 3 Phase-0 caveats. |
| **90-ROADMAP** | Definition of done + Phase 0 (measurement) → Phase 1 (structural levers, mostly tooling/pairing — no source lever exists) → Phase 2 (og ports, ~186 stubs) → Phase 3 (frontier grind + floor cert) + native-bug burndown + consolidated tooling backlog (20 items, 4 tiers). Near-term = tooling-only + minimal very-high-ROI decomp. |

## The one REFUTED claim

Doc 11 (native-unicorn-divergences) F5/F7: MemAlloc is "not a native bug" because its
body is bypassed by an HX_NATIVE guard. **REFUTED** — MemAlloc (MemMgr.cpp:298-303) has
no guard; the line-313 guard belongs to MemOrPoolAllocSTL. MemAlloc's body is an
*unguarded* `malloc()` stub that runs on native as written; it's still correctly
excluded from the live-bug set, but as an undecompiled-Xbox-allocator (native malloc()
is behaviorally correct), not as guard-bypassed. The 53/100/153 funnel and 43,364-byte
sum are VERIFIED. See `13-verification-log.md` #15.

## Waves 1–6 execution

The roadmap has been executed across six orchestrated waves (2026-06-10 through 2026-06-11).
Each wave ran in isolated worktrees; branches are merged to `main` by the orchestrator.

| Wave | Plan | Results | Summary |
|------|------|---------|---------|
| **Wave 1** | `91-EXECUTION-WAVE-1.md` | `92-WAVE-1-RESULTS.md` | Measurement sync core, authorable-denominator metrics, strict-reloc recert, reconcile_db, stale-object cleanup, open-residual classification |
| **Wave 2** | `93-EXECUTION-WAVE-2.md` | `94-WAVE-2-RESULTS.md` | Floor-cert tooling (certify_floor.py), unicorn evidence refresh, native-stub intersection, single-blocker recert |
| **Wave 3** | `95-EXECUTION-WAVE-3.md` | `96-WAVE-3-RESULTS.md` | IK feet investigation (CharIKFoot/CharIKLeg), asm-archaeology grind wave 1, strict-reloc promotion, suite green pass 1 |
| **Wave 4** | `97-EXECUTION-WAVE-4.md` | `98-WAVE-4-RESULTS.md` | Flip-list adjudication, unicorn refresh wave 2, asm-archaeology wave 2, suite regression set established (45 tests) |
| **Wave 5** | `99-EXECUTION-WAVE-5.md` | `99-WAVE-5-RESULTS.md` | IK root-cause named (mMoveElbow=false / IK inert), ~ObjectDir NullifyAllRefs cascade fix, vertex-unpack bswap engine bug fixed, open-residual census (459 fns / 213,648 bytes) |
| **Wave 6** | `99b-EXECUTION-WAVE-6.md` | *(in progress)* | Knee-bend mechanism (A), residual asm-archaeology grind (B), suite to fully green (C), done-view definition + small tooling (D) |

### Current headline numbers (post-wave-5, pre-wave-6 merge)

| Metric | Value |
|--------|-------|
| **authorable_done with certs** | **97.80%** fns / **95.66%** bytes |
| **Open functions** | **289** fns / **194,848** bytes (post-wave-6 Lane D view fix) |
| **Open pre-view-fix** | 459 fns / 213,648 bytes (170 were COMPLETE+100% promotion artifacts) |
| **cap_exhausted family** (certified floor) | 178 fns / 145,748 bytes |
| **Genuinely routable residual** | ~111 fns / ~49K bytes |
| **Gameplay boot** | PASSES (`game_screen`, EXIT=0) |
| **Regression suite** | 45 tests green |
| **Feet gate** | NOT GREEN — mechanism named (IK inert, knee −58° Xbox vs −20° native); engine-side fix needed |

### Key Wave outcomes

- **Wave 5 Lane C engine fix:** vertex-unpack bswap in `milo-native-engine` (compressed
  mesh positions were collapsing to origin); branch `wave5/vertex-unpack-bswap`
  (`f75339a`) pending engine-main merge + `MILO_ENGINE_PIN` bump.
- **Wave 5 Lane B:** `~ObjectDir` transitive survivor-closure fix (HX_NATIVE) — 59/59
  object-lifetime tests pass.
- **Wave 6 Lane D view fix:** `authorable_done` CASE rule updated to count
  `verdict=COMPLETE AND current_percent>=100 AND match_percent_normalized IS NULL` as
  `matched`, clearing 170 spuriously-open promotion artifacts without any DB writes.
  See `scripts/certify_floor.py` and the tracing note in `scripts/reconcile_db.py`
  check (d).

## Start here

- **What's the real state?** → `90-ROADMAP.md` "Definition of done" + this index's
  headline numbers.
- **Can I trust the percentages?** → `13-verification-log.md` net trust statement.
- **What do I do next?** → `90-ROADMAP.md` Phase 0 + Tooling backlog Tier 1.
- **Wave execution history** → the table above; results docs for each wave.


