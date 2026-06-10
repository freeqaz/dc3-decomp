# 08 — Floor vs Routable: The Project's Expected CEILING

## Question

When DC3-decomp declares "done — only cosmetic/floor mismatches remain", how many
functions and bytes will legitimately sit below 100%, and can we *certify* each one
as cosmetic rather than an un-fixed bug? This defines the project's CEILING and, by
subtraction, the true definition of "done".

## Method (commands run)

All DB queries READ-ONLY via `sqlite3 'file:decomp.db?mode=ro'`. report.json parsed
with streaming python3. Live diagnosis via `mcp__orchestrator__run_diff_inspect`
(diagnose) and `mcp__orchestrator__run_objdiff` (source of truth, normalized scoring).

- Banded `functions` by `current_percent`, split by `excluded` / `is_stub` / `exclusion_reason`.
- Split report.json `units[].measures` into vendor (xdk/d3dx9/nuispeech/ST/...) vs authorable bytes.
- Classified the partial frontier by `unicorn_verdict` / `unicorn_class`.
- Live-diagnosed 5 "claimed-floor" functions: RndMesh::Load, HollaBackMinigame::OnBeat,
  SystemMs, EstimateDraw, SkeletonViz::Visualize.
- Cross-checked DB current_percent vs run_objdiff normalized vs diagnose "match estimate".
- `.schema functions` for a `floor_certificate` column (none exists).

## Findings

### 1. The 43.8% headline is dominated by un-authorable vendor code

report.json: `total_code=11,379,348`, `matched_code=4,983,704` → **43.80%**. Splitting
units into vendor (xdk/, d3dx9, nuispeech, ST skeletal-tracking, xgraphics, xapilib,
xam, CRT) vs authorable:

```
TOTAL  code 11,379,348  matched 4,983,704 = 43.80%
VENDOR code  4,951,700  matched       888 =  0.02%   <- never authorable
AUTHOR code  6,427,648  matched 4,982,816 = 77.52%   <- the real number
author unmatched bytes 1,444,832
```

**Claim:** the project is at **77.5% of authorable code**, not 43.8%. The 4.95M vendor
bytes (XDK/SDK runtime: xgraphics/optimize, d3dx9/cprogram, nuispeech, Microsoft ST)
are XEX-resident library binaries we do not author and that are 0.02% matched by
construction. Reporting 43.8% as "progress" systematically understates the work done
and inflates the apparent remaining surface by ~3.4M bytes.

### 2. The scout's "19,626 at 0%, 5.44M bytes" is NULL+vendor, not real work

DB banding (all 52,504 rows):
```
100      31,056   5,089,908 bytes
NULL     18,089   5,161,228   <- never paired/measured
0%        1,537     277,572
95-100      948     623,744
85-95       508     319,644
70-85       240     152,172
40-70        95      57,584
0-40         31      20,816
```
NULL(18,089) + zero(1,537) = 19,626 fns / 5,438,800 bytes — exactly the scout's "0%"
figure. But **16,740 of the 18,089 NULL rows are `excluded=1`** and live in
`default/xdk/...` units (sampled symbols: XNotifyGetNext, XamShowNuiGuideUI, XGetLanguage
— all 16-byte XAM/xapilibi stubs). 18,956 rows are `excluded=1` total. The "5.44M
unmatched bytes" is overwhelmingly vendor + unpaired, **not** a backlog of authorable
functions.

### 3. The real authorable partial frontier is 1,699 functions / 1,127,844 bytes

`excluded=0 AND is_stub=0 AND 0<current_percent<100`:
```
1,699 functions, 1,127,844 bytes
  95-100   876   596,844
  85-95    493   314,428
  70-85    227   144,616
  40-70     83    53,648
  0-40      20    18,308
```
Plus **650 authorable 0% non-stub** functions (only 53,616 bytes — small unstarted
functions) and ~1,315 authorable NULL (115,520 bytes, mostly unpaired small fns).
So the *true frontier* (partial + authorable-zero) ≈ **2,349 functions / ~1.18M bytes**,
not the previously-cited 1,356. The 1,356 figure was a narrower "workable" cut; the
1,699 partial count is the population that actually needs a floor-or-fix verdict.

### 4. The behavioral oracle (unicorn) classifies the frontier as ~65% cosmetic floor

`unicorn_verdict`/`unicorn_class` on the 1,699 partial frontier (74.3% coverage,
1,263/1,699 tested):
```
FLOOR  (EQUIVALENT + artifact classes)              1,093 fns   735,752 bytes  (65%)
ROUTABLE/real-bug (logic/call_count/call_arg/...)     173 fns   118,528 bytes  (10%)
UNKNOWN (untested)                                    436 fns   274,784 bytes  (26%, avg 85.8%)
```
The single strongest signal: **809 partial functions are unicorn-EQUIVALENT (avg 94.1%)**
— behaviorally identical to the target under emulation, so the residual byte diff is
*provably* register-allocation/scheduling/relocation cosmetics. Sanity check: 24,013 of
the 100%-matched authorable functions are also EQUIVALENT, i.e. the oracle agrees with
byte-equality where both exist.

**Claim (load-bearing):** there are **zero `logic`-class DIVERGENT functions** on the
partial frontier (`unicorn_class='logic'` → 0 rows). The genuine "wrong behavior" bugs
have all been driven to 100% or 0%. The "routable" bucket is NOT a pile of logic bugs.

### 5. The "regswap floor" is tiny by name and lives inside EQUIVALENT

`unicorn_class='regalloc'` on the authorable frontier = **only 19 fns / 3,712 bytes**.
The prior "485 regswap floor" was a misnomer: register-swap functions that are
behaviorally proven cosmetic land in **EQUIVALENT**, not in a named regswap class.
Evidence — EQUIVALENT-but-low-percent functions are exactly the known cosmetic cascades:
- `FlowPtr<Sound>::operator=` @ 59.6% (the block-sinking pattern, doc §7)
- `__uninitialized_fill_n<BoneOp...>` @ 61.6% (STL template floor)
- `Rand::Gaussian` @ 55.1%, `CharClipGroup::HasClip` @ 55.4%
These are certifiable floors *at low percent*. **Percent alone is a useless floor
indicator; the EQUIVALENT verdict is the certificate.** Distribution of EQUIVALENT
partials: 269 @ 99-100, 226 @ 95-99, 141 @ 90-95, 135 @ 80-90, **42 @ <80**.

### 6. The "routable" bucket is mostly the call_count emulation artifact, not real work

`call_count` is 143 of the 173 "routable" rows (avg 88.9%). Sampling `call_count` at the
top: IsUselessLoad, NextFrame@Generator, CharClipGroup::AddClip/Sort, CharEyes::DartUpdate
are **all already at current_percent=100.0** with reason `call_count_mismatch`. The
call-count delta is a merged-call / inlining-difference artifact the emulator counts as a
"call", not a match-blocking bug. After removing the artifact-flavored call_count, the
genuinely hard real-bug residue is just **error(10) + call_arg(9) + object_memory(5) +
return_value(3) = 27 fns / ~13K bytes** plus the ~113 call_count <99% that need
per-function adjudication.

### 7. Three different "match %" numbers exist for the same function — a measurement-chain hazard

For `?SystemMs@@YAHXZ`:
- DB `current_percent` = **99.9%**
- `run_objdiff` (source of truth) = **96.1% normalized / 94.8% raw**
- `run_diff_inspect diagnose` "match estimate" = **67.7%**

For `?EstimateDraw@@YAMH@Z`: DB 99.6%, run_objdiff 99.6% norm / 98.2% raw, diagnose 69.6%.

The diagnose "match estimate" counts symbol-relocation diff_args and reloc-noise
replaces as non-equal, so it under-reports by 20-30 points and **must not be used to
judge floor status**. The DB current_percent can also drift from run_objdiff (SystemMs
99.9 vs 96.1) — the DB is synced from report.json periodically and goes stale between
syncs. **run_objdiff normalized is the only number that should gate a floor certificate.**

### 8. Live floor signatures confirm clean floors where the oracle says EQUIVALENT

- `EstimateDraw` (99.6%): residual = addi offset-shifts (.text layout reloc) + one
  f0↔f13 volatile-FPR swap inside three `fmadds` — the canonical commutative-FMA +
  FPR-allocation floor. Clean cosmetic.
- `SkeletonViz::Visualize` (99.6%): a 3-pair GPR swap daisy-chain
  (r21↔r22↔r23↔r30) with 10 downstream unexplained diff_args — textbook
  coalescing/recoloring floor, not source-fixable.
- `RndMesh::Load` (run_objdiff would show ~near-100 normalized; diagnose 95.6%): r10↔r11
  + r27↔r30 swaps + 12 symbol-relocs. Floor.

These match doc §2/§7 of `docs/decomp/patterns/at-limit-systemic.md` (ICF/merged
symbols; 361-function PGO block-sinking, RE-confirmed unfixable).

## The CEILING (definition of "done")

Best current estimate of functions/bytes that will legitimately remain <100% at
completion, partial frontier (1,699 fns / 1,127,844 bytes) + authorable-zero:

| Category | Fns | Bytes | Disposition |
|---|---|---|---|
| Certified floor — unicorn EQUIVALENT | ~809 | ~735K (with artifacts) | DONE-cosmetic |
| Artifact-class DIVERGENT (build_env/regalloc/merged_call/stack_layout/orig_error/fpr_precision) | ~284 | included above | DONE-cosmetic (verify) |
| Genuine hard real-bug residue (error/call_arg/object_memory/return_value) | ~27 | ~13K | should reach 100 |
| call_count <99% needing adjudication | ~113 | ~95K | mostly artifact, some routable |
| UNKNOWN (untested by unicorn, avg 85.8%) | 436 | 274,784 | MUST TEST before counting |
| Authorable 0% non-stub (unstarted) | 650 | 53,616 | real work, small |

**Bottom line:** if the 436 untested behave like the tested population (~65% floor),
the project's expected **CEILING ≈ 1,000–1,150 functions / ~700–900K bytes remaining
<100%, of which the vast majority is certifiable cosmetic floor.** True remaining
*work* to push everything fixable to 100% is on the order of **~150–650 functions /
~100–350K bytes** (the 650 zero-starts + ~140 routable + adjudicated unknowns). "Done"
should be declared as: *every authorable non-stub function is either 100% (run_objdiff
normalized) OR carries a floor certificate.*

## Implications for the roadmap

1. **Re-baseline the headline metric to authorable %.** Report 77.5% (and unmatched
   authorable = 1.44M bytes), not 43.8%. Vendor units should be a separate, frozen line.
2. **Floor certification is the gating activity, not more matching.** 65% of the frontier
   is already cosmetic; the win is *certifying* it so it stops being re-attempted.
3. **Close the 436-function unicorn gap first** — 26% of the frontier is unjudged and
   skews low (85.8%); some of these are the real remaining work.
4. **Adjudicate the ~113 call_count <99%** — split merged-call artifact from real
   call-arg/count bugs; this is where the genuine routable work hides.
5. The 650 authorable zeros are small (avg 82 bytes) — a cheap sweep target for
   sonnet-agents to clear the function *count*.

## Tooling gaps found

- **No `floor_certificate` column** in `functions` (`pragma_table_info` → 0 cert/floor
  columns). Floor status is inferred ad-hoc from `unicorn_verdict` + the *stale, noisy*
  `primary_pattern`/`verdict_reason`. "Done" is not auditable.
- **`primary_pattern` is stale** — rows labeled `ADDRESS_RELOCATION_NOISE` show
  `current_percent=100.0` (HamDirector::OnFileLoaded, LocalizeFloat). It is a historical
  label, not a current diagnosis.
- **diagnose "match estimate" is a different (raw-ish) metric** than report.json/DB
  normalized %, off by 20-30 points; nothing in the tooling labels which metric a number
  came from, inviting false "this is at 67% so it's broken" conclusions.
- **DB current_percent drifts from run_objdiff** between sync runs (SystemMs 99.9 vs
  96.1) — a certificate must store the run_objdiff-normalized value + a build hash so it
  can be invalidated when the function's source changes.
- **Unicorn coverage is only 74.3%** of the frontier and there's no automated "test all
  untested partials" job feeding certification.

### Proposed floor-certificate schema/tool

Add to `functions`: `floor_certificate TEXT` (NULL | enum: `equivalent` |
`artifact:<class>` | `permuter_exhausted` | `pgo_block_sink` | `icf_merged`),
`floor_cert_pct REAL` (run_objdiff normalized at cert time), `floor_cert_build TEXT`
(source hash / git rev), `floor_cert_at TIMESTAMP`. A `certify_floor.py` script marks a
function certified iff: (a) run_objdiff normalized < 100 AND (b) unicorn EQUIVALENT
**or** an artifact unicorn_class **or** ≥1 full permuter sweep with no improvement, AND
(c) the only diff classes in run_objdiff are {register swap, FPR swap, offset/reloc
shift, commutative operand order, block-sink}. Recompute/invalidate when
`floor_cert_build` != current source hash. "Done" = ∀ authorable non-stub:
`current_percent=100 OR floor_certificate IS NOT NULL`.
