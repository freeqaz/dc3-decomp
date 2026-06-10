# Audit: objdiff Fork Measurement Validity (could our match% be wrong?)

## Question
The DC3 progress numbers (report.json -> decomp.db -> "% matched") are produced by our
fork of objdiff (`/home/free/code/milohax/objdiff`, fork point = upstream v3.6.1 `66c879a`,
upstream remote = `encounter/objdiff`). Can the fork's normalizations/relaxations produce a
**false 100%** (hiding a real semantic mismatch) or a **false non-100%** (penalizing
provably-equal codegen)? Which exact number does "% matched" report, and what does 100%
actually guarantee? Do the report-generation path and the MCP/agent path use the SAME diff
config?

## Method (commands run)
- `git log --oneline 66c879a..HEAD` in the objdiff fork — enumerated 34 fork-only commits.
- Read the behavior-affecting commits in full: `b01e3ef` (funclet byte-signature pairing),
  `cd2a176` (FP-anchor frame-establisher slip), `f62bc9c` (stop normalizing immediate diffs),
  `b3f7138` (default to normalized match).
- Read the scoring core: `objdiff-core/src/diff/code.rs` lines 230-294 (match% computation),
  1019-1104 (per-arg penalty / arg_diff_score), 819-930 (`reloc_eq`/`arg_eq`), 295-405
  (`detect_fp_anchor_compensation`), and `objdiff-core/src/diff/mod.rs` 707-940 (funclet pairing).
- Read the report path: `objdiff-cli/src/cmd/report.rs` 361-372 (config), 532-690 (per-symbol
  measure accumulation). Read the diff path: `objdiff-cli/src/cmd/diff.rs` 845-853, 1230-1240.
- Read the dc3 wiring: `objdiff.json` (no diff options set), `build.ninja:33419-33442`
  (report vs report_raw rules), `scripts/sync_match_percent.py`, `scripts/ingest_report.py`.
- Quantified divergence by diffing `build/373307D9/report.json` (lenient) vs
  `build/373307D9/report_raw.json` (strict) with python3.
- Empirically ran `mcp__orchestrator__run_objdiff` + `run_diff_inspect` (raw mode) on
  `?IsLockedIn@ArcDetector@@QBA_NXZ`.
- Queried `decomp.db` read-only for `current_percent>=100` counts.

## Findings

### F1. There are THREE different "match%" numbers, and the pipeline mixes them.
`diff_code` computes two per-symbol numbers (`code.rs:263-276`):
- `match_percent` (raw): `(1 - diff_score/max_score)`.
- `match_percent_normalized`: `(1 - (diff_score - arg_diff_score)/max_score)`, i.e. raw with
  argument-only penalties subtracted back out.

`report_object` (`report.rs:637-671`) then derives the report.json fields:
- per-function `fuzzy_match_percent` = **raw** `match_percent` (line 660).
- per-function `match_percent_normalized` = normalized (line 661).
- `measures.matched_code` (bytes) counts a symbol only if **raw** `match_percent==100` (line 654).
- `measures.matched_functions` (count) counts a symbol if **normalized**==100 (line 668).
- `measures.fuzzy_match_percent` (the headline byte-weighted avg) is weighted by **normalized** (line 652).

Verified against `report.json` measures: `matched_code=4,983,704`, `matched_functions=29,236`,
`total_functions=48,413`, headline `fuzzy_match_percent=53.20`, and `matched_code_percent`
= 4,983,704/11,379,348 = **43.8%** (the scout's number). So the project quotes 43.8% (strict
bytes) and 53.2% (normalized byte-weighted) — both real, different definitions.

`sync_match_percent.py` reads per-function **`fuzzy_match_percent`** (line 84) into decomp.db
`current_percent`. In report.json there are **29,030** functions with `fuzzy_match_percent==100`
vs **29,236** with `match_percent_normalized==100` (206-fn gap, all normalized-only). decomp.db
shows **31,056** rows `current_percent>=100` of 52,504 — higher than 29,030 because decomp.db has
extra rows not in report.json (e.g. functions whose unit is `complete` with no target, scored 100
by report.rs:639-642; and rows from a wider symbol table). The headline counts are internally
consistent once you know which number is which, but **"31,056 at 100%" and "29,236 matched" and
"43.8% bytes" measure three different things.**

### F2. (LOAD-BEARING) report.json forgives ALL relocation-target differences; the MCP path does not. matched_code is inflated 2.29x vs strict.
The report-generation config sets `function_reloc_diffs = None` (`report.rs:363`). In `reloc_eq`
(`code.rs:830,841-842`) `None` => `relax_reloc_diffs=true` => **any two relocations with matching
flags are treated as equal regardless of which symbol they point to**, and a missing reloc on one
side is forgiven (line 834). The MCP/agent `diff` path sets `function_reloc_diffs = DataValue`
(`diff.rs:851,1236`); the schema default is `name_address` (`config-schema.json:6`); the ninja
`report_raw` rule uses `-c functionRelocDiffs=name_address` (`build.ninja:33429`).

Empirical impact (diff of report.json lenient vs report_raw.json strict, same 48,413 fns):
- 100% in **report.json** (None): 29,030 functions.
- 100% in **report_raw.json** (name_address): 17,978 functions.
- **11,052 functions are 100% lenient but <100% strict.**
- matched_code bytes: report.json = **4,983,704**; report_raw.json = **2,176,568**. The strict
  byte total is **2.29x smaller**; ~2.81M bytes are "matched" only because reloc targets are
  not checked. matched_functions barely moves (29,236 vs 29,224), so the byte gap is concentrated
  in large, call-heavy functions.

This is the dominant measurement-validity question. **Most of the gap is benign**, not a bug:
under `name_address`, `symbol_name_addend_matches` REQUIRES the reloc addend to match
(`code.rs:864-865`), so every `bl` whose callee merely lives at a different build address is
penalized even though the call is to the right symbol. Of 11,052 drops, **9,263 are 99-100%
raw** (one/few addend diffs) and 1,782 are 95-99%. Only **2 non-boilerplate functions** drop
below 90% (both `ArcDetector`, and both are STALE report_raw entries — see F6). 1,751 large real
`Handle`/`SyncProperty` functions sit at ~99.3% strict purely from per-call addend noise.

### F3. The lenient `None`/`DataValue` modes CAN forgive a genuinely wrong call target — but the risk is bounded and unmeasured.
`None` treats any same-flags reloc pair as equal — so a `bl wrong_function` would be scored 100%
if the decomp accidentally called a different function with a matching reloc type. `DataValue`
(`code.rs:870`) bypasses the name/address check entirely (the `||` is constant-true), requiring
only section-name equality + data-literal equality; for a `bl` there are no data literals, so
**DataValue forgives any same-section callee.** Neither `None` nor `DataValue` proves call-target
correctness. The strict guard against wrong-callee is **name match** (`name_address`), which we do
NOT feed into decomp.db. There is **no preset that does name-match-while-ignoring-addend** — the
exact mode you'd want for a clean re-cert (`name_address` couples name AND addend). This is the
real residual false-100% surface, and we currently have no count of how many of the 11,052 lenient
drops are wrong-NAME (real) vs wrong-ADDEND (benign), because report_raw can't distinguish them.

### F4. The FP-anchor normalization (`cd2a176`) is sound; very low false-100% risk.
`detect_fp_anchor_compensation` (`code.rs:295-405`) de-penalizes the `subi/addi rA,r12,K` frame
anchor row plus compensated `lwz/stw rY,off(rA)` accesses ONLY when ALL hold: (1) identical frame
size `stwu r1,-F` byte-for-byte (line 364, else abort); (2) anchor dst register equal (line ~380);
(3) at least one access through rA with `left_eff+lm.off == right_eff+rm.off` — the **effective
address is arithmetically identical** (line ~389); (4) the loaded/stored register `args[0]` is
equal (line ~384, else not suppressed); (5) any opcode replace / insert-delete / differing frame
size / second anchor / differing `bl` aborts the whole detection or stays scored. This is a true
arithmetic identity on displacements, scoped per-instruction; an unrelated mismatch keeps the
function <100%. **Cosmetic-safe.** Risk classification: low.

### F5. Funclet byte-signature pairing (`b01e3ef`) cannot inflate a function to false 100%, but inflates the function COUNT.
`pair_funclets_by_bytes` (`mod.rs:780-945`) only changes WHICH symbols are diffed against each
other; once paired the funclet is scored by the normal `diff_code`. Pass 1/2 pair only on
reloc-masked byte equality (= identical machine code, which is what ICF actually merged). Pass 3
pairs same-size funclets at >=50% byte equality (`mod.rs:225`), but those are then scored
normally and would show <100% if not identical — so pass 3 cannot create a false 100%. Net
measurement effect: **1,324 of 1,536 `fn_<addr>` funclet stubs score 100%** in report.json,
contributing to `matched_functions` (~4.5% of 29,236) but only **68,972 bytes** (~0.6% of
total_code). These are MSVC EH funclets, not source-authorable. **Cosmetic-safe for matched_code;
inflates the function-count metric.** This matches the MEMORY note "objdiff v4.2.0 funclet pairing".

### F6. (LOAD-BEARING) The immediate-diff change (`f62bc9c`) already TIGHTENED the metric — the normalized number no longer hides wrong constants/offsets/vtable slots.
Before `f62bc9c`, `arg_diff_score` subtracted EVERY same-opcode arg diff (registers, immediates,
relocs, branch-dests). The commit (and the comment at `code.rs:1050-1062`) restricts the normalized
fold to **non-immediate** args only (line 1063 `if !is_immediate`). Immediates = constants, memory
offsets, vtable slots — these now count toward the normalized score. The commit message reports an
rb3-decomp audit that found **75 functions with genuine value bugs** previously hidden as
normalized==100. So the fork has improved correctness here; the current normalized metric does NOT
forgive wrong constants/offsets. What normalization STILL forgives: register permutation, branch
destinations, and relocation diffs (the latter further gated by `function_reloc_diffs`).

### F7. report_raw.json is wired but stale; IsLockedIn empirical check confirms benign-addend dominance.
`build.ninja:33419-33436`: the `progress` target builds BOTH report.json and report_raw.json, so a
full build refreshes both. Current report_raw.json is 2 days older (Jun 8 22:45) than report.json
(Jun 10 00:46) because the last builds were incremental. `?IsLockedIn@ArcDetector@@QBA_NXZ` shows
**51.9% in stale report_raw**, but a FRESH `run_diff_inspect` (raw mode) shows **99.1%** with
exactly ONE diff_arg: a `bl ?GetSwipeAmount@ArcDetector@@QBAMXZ` whose target symbol name is
IDENTICAL on both sides (`run_diff_inspect mismatches` raw: idx 16, `addr_reloc` same name) — i.e.
a benign addend diff. `run_objdiff` reports it `100.0% normalized (99.1% raw)`. So the headline
risk number (11,052) is partly stale and overwhelmingly benign; the true wrong-symbol subset is
small but currently uncounted (F3).

### F8. Config consistency confirmed for the report->db path; the MCP path uses a third, different mode.
- report.json -> sync_match_percent.py -> decomp.db: all consistently use `None` (most lenient).
- MCP `run_objdiff`/`run_diff_inspect`: `DataValue` (default), with `raw` mode = `name_address`.
- The historical producer bug (MEMORY `e9f84f40`) was a `--noise-filter`/`functionRelocDiffs=none`
  mismatch in the *attributed* tooling; the report-generation path is internally consistent today
  (`report.rs:363` None throughout). objdiff.json sets NO diff options, so nothing overrides these.
- Consequence: a function can read 100% in decomp.db (None) and 100% via `run_objdiff` (DataValue,
  which forgives addend AND callee-name) yet be <100% under `name_address`. Agents and the DB agree;
  only the strict `name_address` view (report_raw) disagrees, and it disagrees mostly for benign
  reasons.

## Implications for the roadmap
1. **"100% matched" in decomp.db/report.json means: every instruction has the right opcode, the
   right registers up to register-allocation permutation, the right immediates/offsets/vtable
   slots, AND the right relocation TYPE — but NOT a verified-correct relocation TARGET (callee or
   data symbol).** It does NOT guarantee byte-identity. This should be the project's stated
   definition of "done"; cosmetic floor = register permutation + branch layout + benign reloc
   addend.
2. The 43.8% byte figure (matched_code_percent, strict raw==100) is the conservative, defensible
   public number. The 53.2% (normalized byte-weighted) and the function counts (29,236 / 31,056)
   are looser. Pick ONE headline and footnote the rest.
3. The genuine remaining measurement risk is **wrong-call-target hidden by reloc relaxation**
   (F3). It is bounded (only ~2 non-boilerplate fns drop >10% strict, both stale) but UNQUANTIFIED.
   A strict re-cert (below) closes this.
4. Funclet 100%s (1,324) and complete-unit-no-target 100%s inflate the function COUNT; if "done"
   is defined by function count, subtract these. By BYTES they are negligible.

## Tooling gaps found
- **No name-only (addend-ignoring) reloc mode.** `name_address` couples name+addend, so the only
  "strict" preset over-penalizes benign callee-address differences, making report_raw unusable as
  a clean wrong-target detector. The exact mode needed for re-cert does not exist.
- **No report distinguishes wrong-NAME from wrong-ADDEND reloc drops.** report_raw collapses both
  into one <100%, so we cannot count the true false-100% population from existing artifacts.
- **report_raw.json goes stale** (built only on full builds; report.json refreshed incrementally),
  so direct lenient-vs-strict diffs include stale rows (caused the spurious ArcDetector 51.9%).
- **Three coexisting headline numbers** (matched_code_percent, fuzzy_match_percent,
  matched_functions, plus decomp.db current_percent>=100) with no single source-of-truth doc.
