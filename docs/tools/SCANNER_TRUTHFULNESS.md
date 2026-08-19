# Scanner Truthfulness

> **Standing rule.** If your summary line can be printed without knowing how
> many rows you never looked at, it is not a summary — it is a sample presented
> as a total.

This document exists because a single session in August 2026 found **six**
defects in this project's own measurement tooling. Every one had been used to
declare some area "exhausted", and every fix reopened territory that contained
real bugs. A follow-up audit of all 46 scripts under `scripts/analysis/` plus
the top-level measurement scripts found the same shape in **21 more**.

---

## 1. The failure shape

All of it is one bug with many spellings:

> **A silent `continue`, cap, or filter on exactly the population you care
> about, with a summary line that reports only what was PROCESSED and never
> what was DROPPED.**

The six originals:

| instrument | defect | what it hid |
|---|---|---|
| `fake_impl_scan.py` | `if pct is None: continue` on `fuzzy_match_percent` — a key objdiff only emits for functions **we** define | the whole "we wrote no body at all" tier, ~1,024 rows. Four waves called that pool exhausted having never looked at it. |
| `data_symbol_scan.py` | truncated at `--max-symbols` default 4000; stderr printed only `scanned=` | 14,549 of 18,549 symbols. Every count it produced was a 22% sample presented as a total. |
| `data_symbol_scan.py` | thread race: a lazy global index published empty, then filled inside the worker pool | a month of candidate counts that were nondeterministic noise |
| `certify_floor.py` | SQL `symbol NOT LIKE '??_%'` — `_` is a single-char wildcard in LIKE | 6,835 functions hidden from every band query |
| `measure_progress.sh` | compared the reloc-sensitive `fuzzy_match_percent` ruler | phantom regressions from ICF/atexit-thunk churn |
| all percentage surfaces | they **round**, so 99.97 renders as `100.0` | two real bugs, protected by the otherwise-correct rule "a divergence on a 100%-matched function is by construction a harness artifact" |

### The five sub-shapes to look for

1. **Missing-field blindness.** A `.get(k)` that returns `None`, or worse a
   `.get(k, default)` whose default collides with a real value. `dict.get("fuzzy_match_percent", 100.0)`
   is the most optimistic possible default; `F(...)` coercing `None` to `0.0`
   collides with a genuine 0.0 and becomes indistinguishable from it.
2. **A cap on the analysis dressed as a cap on the display.** `xs = xs[:args.limit]`
   *before* the totals are computed rewrites the totals. After, it only shortens
   a printout — and then it must say `showing 20 of 1,751`.
3. **Missing input ⇒ clean bill of health.** `if p.exists():` with no `else`.
   Four DTA scanners load config from `orig-assets/`, which is absent from every
   git worktree, and then print *"No DTA access issues found."* to **stdout**
   while the only warning goes to **stderr**. Redirect stdout and you keep just
   the reassuring half.
4. **Error ⇒ benign zero.** `except Exception: return []`. `function_health.py`'s
   batch query named two columns that do not exist in `decomp.db`; the
   `OperationalError` was swallowed and the tool answered *"No functions found
   matching criteria."* to every query it was ever asked.
5. **Nondeterminism.** `as_completed` order used as output order; an unsorted
   `glob` feeding a last-write-wins dict; a `sort` whose key can tie.

---

## 2. The convention every scanner now follows

### 2.1 Report the denominator — `scripts/analysis/coverage.py`

```python
from scripts.analysis.coverage import CoverageReport, add_coverage_args

add_coverage_args(ap)                       # --allow-truncation / --coverage-json
cov = CoverageReport("my_scan", args=args)
cov.universe(len(rows), "function rows in report.json")   # BEFORE any filtering
for r in rows:
    if r.get("pct") is None:
        cov.drop("missing-fuzzy-percent", note="objdiff emits none for undefined fns")
        continue
    if r["pct"] > args.max_pct:
        cov.drop("above---max-pct")         # deliberate filters get counted too
        continue
    cov.examine()
    ...
results["_coverage"] = cov.as_dict()
sys.exit(cov.emit())
```

`emit()` prints a block that always states the denominator, and returns:

| code | meaning |
|---|---|
| `0` | full census: `universe == examined + sum(drops)`, nothing truncated |
| `3` | `TRUNCATED` — a cap cut rows out of the **analysis**. `--allow-truncation` downgrades to 0; the JSON still says `truncated: true` |
| `4` | `UNACCOUNTED` — `universe != examined + sum(drops)`. **Nothing downgrades this.** |
| `5` | `EXIT_NO_INPUT` — the corpus was empty, an input was missing, a sub-tool failed, **or `require_examined()` was set and nothing was examined** |
| `6` | `EXIT_NO_DENOMINATOR` — `universe()` was never called and no reason was given. See below |

Exit 4 is the part with teeth. It fires whenever some bare `continue` skipped
`drop()`, which means it catches the *next* instance of this bug without anyone
having anticipated the specific field or filter involved. An unbalanced
denominator is a scanner bug, never a user choice.

**And exit 4 had a one-line bypass.** `unaccounted` is
`universe - (examined + drops)`, so with no `universe()` call it is `None` —
falsy. **Omitting the call entirely** therefore disarmed the check that catches
every future instance of this bug class, and `emit()` returned **0** while
stdout said `out of None rows` and the banner said `NO DENOMINATOR`. One
deleted line restored the old silence, and nothing objected. Closed at both
ends:

- **runtime**: `emit()` returns `EXIT_NO_DENOMINATOR` (6). `universe_unknown(reason)`
  is the honest escape hatch — it still exits 0 and prints the stated reason,
  so *forgetting* is now distinguishable from *admitting*.
- **static**: `honesty_lint` `E3` flags a `CoverageReport` built in a file that
  mentions neither `universe(` nor `universe_unknown(`. Matched per **file**,
  not per variable, because a scanner may legitimately build the report in one
  function and declare the universe in another.

**Balanced is not the same as non-empty.** Drop every row for good reasons and
`universe == examined + drops` holds at `examined == 0` — arithmetically clean,
epistemically empty. `cov.require_examined(note)` makes that `EXIT_NO_INPUT`;
set it on a code path whose purpose is to CHECK something, not on a `--stats`
survey that legitimately checks nothing.

**What the tripwire still cannot see.** It catches an *uncounted* row. It does
not catch a **twice-counted** one — a site counted into the universe twice and
disposed twice balances perfectly (§5.1) — and it says nothing about whether a
drop's stated **reason** is true. Both of those need their own controls.

**The ratchet covers about half the surface.** Only **24 of the 54** scripts
under `scripts/analysis/` import `CoverageReport` (measured at `903be2231`;
`grep -l CoverageReport scripts/analysis/*.py | wc -l`) — and several of the
uncovered ones are scripts the audit table above *cleared*. A ✅ in §4 that rests on reading the code is a weaker
claim than one that rests on the runtime contract, and the two are not
distinguished in that table.

`coverage.py` also carries `like_escape()` / `like_prefix_clause()` so the
`'??_%'` wildcard defect has one correct implementation to reach for.

### 2.2 Six rules

1. **Declare the universe** before any filtering, or admit you cannot
   (`emit()` prints `universe: UNKNOWN` and `NO DENOMINATOR`).
2. **Every discard is counted.** Deliberate selection is still a discard; give
   it a slug (`below---min-pct`) and it costs one line.
3. **A cap must announce itself.** Default it to "no cap" if you can. If it is
   display-only, print `showing N of M` and register the site in
   `honesty_lint.ALLOW_DISPLAY_ONLY` **with a reason**. That list started at
   five entries and is down to one: two files were fixed properly, and two had
   never fired at all. A speculative exemption is its own small lie — it claims
   "we looked and it's fine" about a site the checker never examined — so
   `test_allowlist_has_no_dead_entries` fails on any entry that no longer
   excuses a real finding.
4. **A missing input is never a clean verdict.** Print `INCONCLUSIVE` on
   **stdout** (the exculpatory count must travel with the verdict) or exit
   non-zero.
5. **Sort before you print or serialise**, with a full tie-breaking key.
6. **State your ruler.** `fuzzy_match_percent` is reloc-sensitive;
   `match_percent_normalized` is the canonical one. 395 functions read
   `normalized == 100` and `fuzzy < 100`, and 16,920 of 48,344 rows carry no
   `fuzzy_match_percent` at all. `scripts/analysis/name_charge_census.py`'s
   `resolve_ruler()` + `rk.banner()` is the model.

### 2.3 Never round into a verdict

Every `%` surface rounds. `scripts/sync_match_percent.py:_round_pct` is the
reference fix: `r = round(v, 2); return 99.99 if (r >= 100.0 and v < 100.0) else r`.
Gate on the raw value; render with `.2f`, not `.1f`.

---

## 3. The instruments

| tool | what it does |
|---|---|
| `scripts/analysis/coverage.py` | the runtime contract: `CoverageReport`, `like_escape`, exit codes |
| `scripts/analysis/honesty_lint.py` | the static contract. `E1` unescaped SQL LIKE wildcard, `E2` a self-truncating slice in a file that never mentions truncation; `W1` error→empty-result, `W2` a global rebound in a module that runs a worker pool |
| `scripts/analysis/determinism_check.py` | runs each scanner twice under two `PYTHONHASHSEED` values and diffs it against itself |
| `scripts/analysis/tests/test_coverage.py` | negative controls for the runtime contract |
| `scripts/analysis/tests/test_honesty_lint.py` | negative controls for the lint, **plus the repo ratchet** — `KNOWN_OPEN` is empty, so a new silent cap or wildcard fails CI |

```bash
python3 scripts/analysis/honesty_lint.py --warnings   # 0 = clean
python3 scripts/analysis/determinism_check.py         # 0 = every scanner agreed with itself
python3 -m pytest scripts/analysis/tests/ -q
```

State at `903be2231` (merge pass, 2026-08-19): **0 lint errors, 42 W-rule
warnings** over 223 files; **9/9 scanners agree with themselves on a non-empty
output**; **293 tests collected**, 291 passing with the DTA corpus present and
289 passing + 2 corpus-gated skips without it.

The earlier line here said "**248 tests pass**", and that number was never
reproducible — the lane tip collects **249**, of which only 232 pass in a bare
worktree (17 skip for want of a built tree). A pass-count is a property of the
ENVIRONMENT, not of the suite; quoting one as a fixed fact is the same mistake
as quoting a truncated census as a total. State the collected count, then state
what each environment does with it.

### Negative controls, not tautologies

The best verification in the originating session reverted a known-good fix to
confirm the checker flagged it. The worst asserted a synthesised value against a
constant written in the same sitting — a tautology, and *that one let a real
error through*. So every check here is proven both ways: fed the verbatim
historical bug (it fires) and fed the fixed form plus a superficially-similar
non-bug (it stays quiet). The SQL-LIKE control does not compare strings at all;
it builds a real in-memory SQLite table containing a `??_G` artifact, a `??0`
ctor and a `??1` dtor, runs the historical `NOT LIKE '??_%'`, and asserts the
ctor and dtor really do vanish.

**The vacuity trap is real and it caught us here.** The first
`determinism_check.py` reported *"8/8 scanners agreed with themselves"* — and
three of the eight had produced **zero bytes** of stdout, because `--json` takes
a path argument and argparse had exited 2 before any scanning happened.
`sha256("") == sha256("")`, so two failures compared equal and the harness
called it determinism. The lesson generalises: **a comparison you can pass by
doing nothing proves nothing.** The harness now treats an empty output, an
`exit 2`, or a timeout as `INCONCLUSIVE`, which is a failure and not a pass.

---

## 4. Audit table

`scripts/analysis/` (46 files) and the top-level measurement scripts.
"Can silently drop" is the *interesting* population it hides, not every filter.

### Fixed

| script | what it could silently drop | fixed? |
|---|---|---|
| `data_symbol_scan.py` | `--max-symbols` default 4000 against an 18,549-symbol universe (verified at `903be2231`: 2,224 units, 50,160 data symbols, 18,549 non-string) | ✅ default → 0; `TRUNCATED` banner + exit 3; every drop counted; results sorted. **Findings are a strict superset** (20 → 73 candidate-bugs, 0 only-old) but it is **3.2× slower**: measured 246 s capped at 4,000 vs **790 s** uncapped. Budget ~15 min, not 4 — an agent with a 10-minute timeout will read a killed run as a broken scanner. |
| `fake_impl_scan.py` | the ~1,024-row "no body at all" tier behind `pct is None` | ✅ (2026-08-19, pre-existing) — normalized fallback + a counted skip |
| `certify_floor.py` | 6,835 functions behind `NOT LIKE '??_%'` | ✅ (pre-existing) — and it carries a two-path SQL-vs-Python `denominator_self_check` that exits non-zero on disagreement. **The control the other scripts lack; clone it.** |
| `audit_normalized_masking.py` | 16,920 of 48,344 rows behind `if n is None or raw is None: continue`; `norm_sym` stripping the class qualifier so two different classes' `Load` cancel out — fail-open in the tool's own headline category | ✅ |
| `batch_pattern_scan.py` | 16,920 rows on `pct is None`, **and** `--limit` default 200 applied *after* a descending sort, so only 99.58–99.90 was ever examined and the entire 90.0–99.58 band was invisible (1,551 of 1,751 dropped) | ✅ default → 0 (200 → 1,751 inspected, 1 → 115 hits) — but **8.9× slower**, ~2.2 min → ~19.3 min. Two breaks shipped with that fix and are corrected: `--json`'s top level flipped **list → dict** (a silent break — a dict iterates its keys rather than raising; the list is back, `--json-envelope` opts into the object), and an explicit `--limit N` began **exiting 3**, which made every doc'd recipe that passes one look broken (`docs/plans/compiler-instrumentation.md:900` passes `--limit 500`). An explicit `--limit` now exits 0 with the TRUNCATED banner and `truncated: true` intact; `--no-allow-explicit-limit` restores exit 3 for CI. |
| `findarray_receiver_scan.py` | a relevance gate counting only `FindArray` while `LOOKUP_METHODS` has six entries (39 files, 30.7% of the relevant set); a relative default path making any non-root cwd print "no patterns found"; `0 SHADOW_PARENT` printed while 14 existed | ✅ coverage + loud path error; the gate widening left as `TODO(heuristic)` |
| `vtable_dispatch_scan.py` | 16,920 rows uncounted — *substantively* defensible, but never stated | ✅ (its cap handling was already honest — `--limit` default 0, `capped` in both stderr and JSON) |
| `header_cluster.py` | 16,920 rows behind a `pct <= 0` guard meant for true zeros, of which there are none; `Loaded 2241 non-complete functions` where the true population is 19,193 | ✅ |
| `inlining_catalog.py` | `.get("fuzzy_match_percent", 100.0)` — the most optimistic default — then `if pct >= 100: continue` | ✅ |
| `function_health.py` | **batch mode**: SQL named `source_path`/`match_percent`, neither of which exists; `except Exception: return []` turned the `OperationalError` into `No functions found matching criteria.` to every query ever asked. **Single-symbol mode** never worked either: it shells out to `objdiff-cli diff --symbol <s>`, but the symbol is POSITIONAL. **Blast radius ≈ zero** — the only reference was a doc marked *Planned* whose recipe passed a flag the tool never had. | ✅ batch query repaired, failures propagate. Single-symbol left as `TODO(repair)` on purpose: fixing the call changes what the tool FINDS. `docs/tools/INDEX.md` now says the mode is broken instead of advertising it. Its `insert_delete` classification — the defect the lane fixed in `ceiling_calculator` — survived here and fed an `at_limit` verdict; now **contested**, so a hard floor still certifies and this class no longer does. |
| `compare_progress.py` | function-level comparison on `fuzzy` only (395 rows are `normalized==100, fuzzy<100`); base-only and current-only keys dropped with no count | ✅ (its `Regressions (N functions, showing top M)` line was already the right pattern) |
| `name_charge_census.py` | 16,780 rows / 5,129,540 B behind `F(None) → 0.0` colliding with a real 0.0; examined population was only 2,238 rows | ✅ |
| `scope_index_census.py` | an unsorted recursive `glob` into a last-write-wins dict: **568 of 6,675 (function, static) pairs hold conflicting scopes**, so those functions' verdicts flipped between runs | ✅ |
| `ceiling_calculator.py` | `insert_delete` (the most *fixable* class) counted as unfixable, systematically manufacturing "at limit"; 1,231 `current_percent IS NULL` rows silently outside the band | ✅ |
| `reclassify_at_limit.py` | funnel 3,796 → 1,701 → 1,517 with only `1517 candidates` printed; no `excluded = 0`; `UPDATE ... WHERE symbol = ?` **with no unit qualifier** | ✅ |
| `remaining_work.py` | headline `140 functions` where 363 fell to a hardcoded skip list and 314 to `--min-bytes 500` — 83% of ~817; plus `total = done + partial + len(stubs)` double-counting partials in 216 of 218 units | ✅ |
| `dta_access_audit.py`, `dta_hierarchy_scan.py`, `dta_trace_validator.py` | **missing input ⇒ clean verdict**: `orig-assets/` is absent from every worktree, so they print "No … issues found." having checked nothing | ✅ |
| `dta_dataflow.py` | the same defect — and this row was ✅ for a file **nobody had touched**. Byte-identical blob (`14e2135036…`) at the merge base, at the lane tip and on `main`. The lane's own commit body says "all **three**". | ✅ *(2026-08-19, in the merge pass)* — same `empty_corpus_banner`, same `INCONCLUSIVE` on stdout, same exit 5. Measured before: corpus absent → **28 B** `No DTA access issues found.` exit 0; corpus present → **50,302 B** `Total: 30 findings` exit 0 |
| `home_store_census.py`, `frame_deficit_census.py`, `report_absent_census.py` | uncounted unit/body skips (1,245 units; 65,661 of 114,857 unreadable target frames); unsorted `os.walk` + no `ORDER BY` | ✅ |
| `reset_false_complete.py` | `LIKE '%base_size=0%'` — unescaped `_`, on the predicate of an `UPDATE` | ✅ |
| `patches/apply_safe.py`, `unicorn/refresh_frontier.py`, `decomp_orchestrate.py` | `--limit` truncating a patch queue / a frontier sweep with no notice | ✅ |
| `reloc_strict_classify.py` | `--limit` truncating a candidate census with no notice. **This script was listed in BOTH tables**, and "honest already" was false: the lane modified it, and `honesty_lint` raises `E2` on the pre-lane form (`cands = cands[: args.limit]`, line 349). The first fix was also only half of one — see §4.1 | ✅ *(completed 2026-08-19)* — `candidates_total` is the PRE-truncation figure, `candidates_diffed` the post; both stdout lines carry `N of M`; the `sp is not None` silent drop is three counted slugs; exits via `cov.emit()` |
| `find_hidden_work.py`, `batch_check.py`, `sync_objdiff.py` | fuzzy-ruler writes to `verdict`; `current_percent IS NOT NULL` hiding never-measured COMPLETE rows; a substring SDK filter swallowing 4 authorable units; `elif match_pct > 0` leaving exactly-0% rows in no bucket at all | ✅ |

### Honest already — the premise was wrong for these

| script | why |
|---|---|
| `progress_metrics.py` | prints **both** rulers side by side with the canonical one marked, and *reads* `functionRelocDiffs` from the report rather than hardcoding it, with the comment "or this document will confidently state the wrong ruler" |
| `certify_floor.py` | see above — the `denominator_self_check` |
| `report_absent_census.py` | a denominator at every stage plus a recorded load-bearing **negative**, and a docstring that says "Re-run this before ever concluding otherwise" |
| `fold_proof.py` | prints every verdict class including the ones that conclude nothing; three explicit refusals-to-certify (vacuity guard, cheap-identity guard, `COMDAT_SELECTION_MISSING`); `UNDECIDABLE` is first-class, not a fallthrough |
| `grindarray_divergence.py` | full 64×256×256 population, no sampling, and it prints an explicit negative result. **Its trailing `CONCLUSION:` block was NOT "noted, not load-bearing" — see §4.1. Now gated; the row stands only for the population claim.** |
| `scan_behavioral_idioms.py` | no caps, no slices, sorted `rglob`, and a git failure printed to stderr rather than swallowed |
| `frame_deficit_census.py` (`--max-percent` default **101.0**) | defaulted *above* 100 on purpose, with the comment "so the filter never silently drops the function under test". The corrected form of this whole bug class. |
| `lp64_scanner.py` (worker pool) | uses `Pool(initializer=...)` so each process fully initialises before any work item — the **correct** pattern, not the `data_symbol_scan` race |

---

## 4.1 Corrections to THIS TABLE, found in the merge review

Three ✅ rows above were false, and a lane whose whole subject is "do not print
a clean verdict you did not earn" cannot ship those. All three are now fixed in
the tools and corrected above; recorded here because a wrong diagnosis that
goes unrecorded gets re-filed.

| row | what the table said | what was true |
|---|---|---|
| `dta_dataflow.py` | ✅, grouped with its three siblings | **Never touched.** Same sha256 at merge-base, lane tip and `main`. Still had `if p.exists():` with no `else` and a bare `print("No DTA access issues found.")`. Same script, corpus the only variable: **28 B all-clear, exit 0** vs **50,302 B / `Total: 30 findings`, exit 0**. `honesty_lint` emits nothing for that shape. The lane's own commit body says "all *three*" — the table said four. |
| `reloc_strict_classify.py` | in **both** the Fixed and the Honest-already table | Cannot be both, and the second is false: the lane modified it and `E2` fires on the pre-lane form. The fix was half a fix — stdout printed `candidates: 5` and the JSON `candidates_total: 5`, both POST-truncation, with the real denominator only on **stderr**, which is the stream a redirect drops. It also exited 0 when truncated, and carried an uncleared silent drop (`sp is not None` — a row scored lenient-100 whose strict score is *absent*, which is not the same as strict-clean). |
| `grindarray_divergence.py` | "Its trailing `CONCLUSION:` block was an unconditional `print` of a fixed string — **noted, not load-bearing**" | The population claim is true; "not load-bearing" is not. Sabotage every `x64_op` to return `(ppc + 1) & 0xFF` and the script prints `[DIVERGENCE FOUND]`, `[NO MATCH]` and `*** DIVERGE ***` — then, sixty lines later, prints the conclusion **byte-identical to the clean run** (1,296 B, empty diff), exit 0 both times, still saying *"Investigate the key derivation pipeline BEFORE GrindArray."* A hardcoded verdict that routes the next engineer is load-bearing by definition. Now gated on the computed result — specifically on the ops that **fire**, since op5 diverges and never fires, which is the conclusion's own premise. |
| `dta_trace_validator.py` | in **both** tables | The Fixed row is correct; the duplicate is deleted. |

---

## 5. Corrections the audit made to itself

Recorded because a wrong diagnosis that goes unrecorded gets re-filed. Every one
of these was found by *trying to write the negative control* and discovering the
premise did not hold.

| claim | what was actually true |
|---|---|
| `sync_objdiff.py` and `compare_progress.py` use the reloc-sensitive ruler | **Refuted.** `fuzzy_match_percent` names **two different rulers** depending on who wrote the JSON. `objdiff-cli diff` writes the *normalized* value into both `normalized_match_percent` and `fuzzy_match_percent` (`diff.rs:1262`), and `report.json`'s **unit-level** `measures.fuzzy_match_percent` is the size-weighted mean of per-function *normalized* values (`report.rs:1096`, verified 471/471 units). Both were already canonical — by coincidence of naming, now pinned deliberately. |
| `scope_index_census.py` is not byte-identical across two runs | **Refuted as stated.** Five consecutive pre-fix runs *were* identical; `readdir` order is stable for an unchanged directory. The order-dependence is real (`objs != sorted(objs)`, 989 objects) but **latent**. And the mechanism differed: 566 of the 568 conflicting pairs conflict *inside one object* (`_s` declared once per `MILO_ASSERT`), only 2 across objects. The real exposure was not "flips between runs" but "decided by an arbitrary tie-break" — last-write-wins 1289/30, min 1287/32, max 1281/38. |
| `reclassify_at_limit.py` reports 1,517 candidates | **1,368.** 1,517 was the count after the source-path filter only. And the demangler-**parse-failure** class — the accidental-blindness bucket — is **149 rows**, not zero. |
| all 16,920 missing-fuzzy rows have `normalized == 0.0` | **Off by one.** `RndShaderDepthVolume::CalcShaderOpts` has `normalized == 3.59375` and no fuzzy score. It now carries its own drop slug so the justification cannot silently over-generalise — which is how the original defects survived. |
| `inlining_catalog.py`'s regex makes nested-brace accessors invisible | **Worse than that.** It captures them *truncated at the first `}`*, so `int Clamped() const { if (mV<0) { return 0; } return mV; }` is recorded as a trivial one-statement body and filed as a move-to-`.cpp` candidate. 349 of 5,462. |
| `ceiling_calculator.py`'s `current_percent IS NOT NULL` hides 1,231 rows | True of the column, **not of this tool**: all 1,231 are also `excluded=1`, which the tool filters anyway. Still a NULL trap, now stated rather than inferred. |
| `reclassify_at_limit.py`'s unqualified `UPDATE ... WHERE symbol = ?` is live | **Latent.** The schema declares `symbol TEXT NOT NULL UNIQUE` and no symbol is in two units today. Fixed anyway; the test reconstructs a schema without the constraint, which is the only honest way to test a latent bug. |

And one found in this work's own instrument: the first `determinism_check.py`
reported **"8/8 scanners agreed with themselves"** while three of the eight had
produced zero bytes. See §3.

### 5.1 Numbers this document and its commits got wrong

Measured on tree `903be2231` unless stated. **Every census count below carries
a tree SHA**, because several drifted inside a single day — and because
`build/373307D9/report.json` is a build artifact, not a checked-in fact: the
worktree's copy (15,204,230 B, mtime 07:27) selects **395** audit targets where
the main checkout's (15,213,836 B, mtime 09:42) selects **384**.

| claim | where it came from | what is true |
|---|---|---|
| commit `547b459f3`: "35,039 + 12,424 + 3,203 = **47.3 %** of ALL mismatches" | `ceiling_calculator.py` printed the gap as a bare COUNT of three terms and left the reader to divide. The commit divided by hand. | The three-term sum is **68.4 %** (50,666 of 74,051). **47.3 % is `insert_delete` alone** (35,039). The other two are 16.8 % and 4.3 %. Cross-check from the commit's own figures: 73,600 − 22,934 = 50,666. Re-measured on a DB snapshot at `903be2231`: 12,394 / 35,000 / 3,201 of 73,874 → gap 68.5 %, insert_delete 47.4 % — same ordering, ~0.1 % drift. **Fixed at the source**: the tool now prints every share against the stated denominator, so nobody has to divide by hand again. |
| `compare_progress.py:74` and `:247`: "verified **471/471** units" | Hand-written. Nothing in this repo computes 471; nine candidate derivations from the report all miss it. | **2,055 / 2,055**, of **2,224** units — 169 skipped for `measures.total_code == 0` (empty `functions`, so the weighted mean is 0/0 and undefined). Tolerance **1e-5**; max \|Δ\| **7.27e-6** at `default/system/os/HDCache`, f32 serialisation. At 1e-6 only 1,646 pass, so the tolerance is honest rather than a fudge. **Caveat that must travel with it:** 1,085 of the 2,055 carry no `fuzzy_match_percent` key at all (serde omits the 0.0 default) and agree only under "absent ⇒ 0.0"; require the key and it covers 970. It is now `test_unit_measure_really_is_the_normalized_weighted_mean`, with a negative control — weighting the per-function *fuzzy* values agrees on only 1,570 — so the check discriminates instead of passing vacuously. |
| "56 `audit_normalized_masking` verdicts flipped BENIGN→REVIEW" | The lane. | **Correct — the merge review's challenge to it was refuted.** Both sides re-run pinned to the same `objdiff-cli` (4.2.3 `88b425bc3bad-dirty`): pre-lane **251 BENIGN / 144 REVIEW**, post-lane **195 / 200**, per-symbol join over 395 common keys giving **56 BENIGN→REVIEW and 0 REVIEW→BENIGN** — strictly one-directional. Reproduced exactly on a second run. The competing 250/145 → 185/210 (65 flips) is not reproducible here; the likely cause is a standing hazard in the tool rather than an error by either party — **`report.json` is stale relative to the build**, mtime 07:27 against 989 base `.obj` files rebuilt by 08:01, so the audit *selects* its 395 targets from one point in time and *classifies* them by diffing objects from another. |
| DTA coverage "2.2 %" and "21 %" | `CoverageReport.render()`, on site-level reports that counted site EVENTS. | Both **double-counted**. `access_audit` walks each line with `CHAINED_RE` and disposes, then `_check_key_based_accesses` re-walks the same lines with the same regex and disposes again: 1,679 events over **1,658 distinct** sites, 21 doubled, 8 examined twice → **29 / 1,658 = 1.75 %**. `hierarchy_scan`'s assign and find passes both count the same textual call: 331 events over **274 distinct**, 57 doubled → **70 / 274 = 25.5 %**. They move in **opposite directions**, so the pair was never comparable — and the denominators do not measure the same thing (access-sites includes bare positional `x->Int(N)`; call-sites is `Find*()` only). Fixed in both scanners. |
| `dta_hierarchy_scan` drops 106 sites "checked separately via assign_checks" | The skip predicate matched the bare prefix `\w+ = receiver->FindArray`; `ASSIGN_FINDARRAY_RE`, which actually feeds `assign_checks`, requires the closing paren right after the key — **one-argument `FindArray` only**. | **57** are captured by that regex; **49 are captured by nothing** (every one a two-argument call), and one more — `def = def->FindArray("editor")`, `Flow.cpp:701` — is captured but dropped as circular before reaching `assign_checks`, so **50 of 106 are re-checked by nothing at all**. A drop with a false *reason* is worse than an uncounted drop: an uncounted drop is invisible, this one asserted the population had been handled. Now two slugs, the honest one reading "Nothing checks these". |

**Double-counting is invisible to the exit-4 tripwire.** Each event bumps the
universe and lands in exactly one of examined/dropped, so a twice-counted site
balances perfectly. The contract catches an *uncounted* row; it cannot catch a
*twice-counted* one, and it says nothing about whether a drop's stated reason is
true. Both now have their own controls.

---

## 6. If you are writing the next scanner

1. Import `CoverageReport`. Call `universe()` first and `emit()` last.
2. Route every `continue` through `drop()`. If `emit()` returns 4, you missed one.
3. Default your caps to "no cap". A cap you did not ask for is a lie you did not
   notice.
4. Make a missing input `INCONCLUSIVE` on stdout, never "no issues found".
5. Sort before you print. Add your scanner to `determinism_check.CASES`.
6. Write the negative control *first*: build the input that exhibits the bug,
   watch the check fire, then fix it. A test that passes on day one has told you
   nothing.
7. Run `python3 scripts/analysis/honesty_lint.py` before you commit.

## See also

- [BUILD_SYSTEM.md](BUILD_SYSTEM.md) — the split graph and the fixed-point rule
- [REFERENCE.md](REFERENCE.md#trust-caveats--read-before-believing-a-column) — which `decomp.db` columns to distrust
- [../STATE_OF_THE_DECOMP.md](../STATE_OF_THE_DECOMP.md) — the two headline metrics and their (different) denominators
- [../decomp/patterns/rounded-100-hides-real-bugs.md](../decomp/patterns/rounded-100-hides-real-bugs.md)
