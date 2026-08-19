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
| `5` | `EXIT_NO_INPUT` — the corpus was empty, an input was missing, or a sub-tool failed. Raised by the scanner (`sys.exit(cov.emit() or EXIT_NO_INPUT)`), not by `emit()` |

Exit 4 is the part with teeth. It fires whenever some bare `continue` skipped
`drop()`, which means it catches the *next* instance of this bug without anyone
having anticipated the specific field or filter involved. An unbalanced
denominator is a scanner bug, never a user choice.

`coverage.py` also carries `like_escape()` / `like_prefix_clause()` so the
`'??_%'` wildcard defect has one correct implementation to reach for.

### 2.2 Six rules

1. **Declare the universe** before any filtering, or admit you cannot
   (`emit()` prints `universe: UNKNOWN` and `NO DENOMINATOR`).
2. **Every discard is counted.** Deliberate selection is still a discard; give
   it a slug (`below---min-pct`) and it costs one line.
3. **A cap must announce itself.** Default it to "no cap" if you can. If it is
   display-only, print `showing N of M` and register the site in
   `honesty_lint.ALLOW_DISPLAY_ONLY` **with a reason**.
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
| `data_symbol_scan.py` | `--max-symbols` default 4000 against an 18,549-symbol universe (verified: 2,224 units, 50,160 data symbols, 18,549 non-string) | ✅ default → 0; `TRUNCATED` banner + exit 3; every drop counted; results sorted |
| `fake_impl_scan.py` | the ~1,024-row "no body at all" tier behind `pct is None` | ✅ (2026-08-19, pre-existing) — normalized fallback + a counted skip |
| `certify_floor.py` | 6,835 functions behind `NOT LIKE '??_%'` | ✅ (pre-existing) — and it carries a two-path SQL-vs-Python `denominator_self_check` that exits non-zero on disagreement. **The control the other scripts lack; clone it.** |
| `audit_normalized_masking.py` | 16,920 of 48,344 rows behind `if n is None or raw is None: continue`; `norm_sym` stripping the class qualifier so two different classes' `Load` cancel out — fail-open in the tool's own headline category | ✅ |
| `batch_pattern_scan.py` | 16,920 rows on `pct is None`, **and** `--limit` default 200 applied *after* a descending sort, so only 99.58–99.90 was ever examined and the entire 90.0–99.58 band was invisible (1,551 of 1,751 dropped) | ✅ |
| `findarray_receiver_scan.py` | a relevance gate counting only `FindArray` while `LOOKUP_METHODS` has six entries (39 files, 30.7% of the relevant set); a relative default path making any non-root cwd print "no patterns found"; `0 SHADOW_PARENT` printed while 14 existed | ✅ coverage + loud path error; the gate widening left as `TODO(heuristic)` |
| `vtable_dispatch_scan.py` | 16,920 rows uncounted — *substantively* defensible, but never stated | ✅ (its cap handling was already honest — `--limit` default 0, `capped` in both stderr and JSON) |
| `header_cluster.py` | 16,920 rows behind a `pct <= 0` guard meant for true zeros, of which there are none; `Loaded 2241 non-complete functions` where the true population is 19,193 | ✅ |
| `inlining_catalog.py` | `.get("fuzzy_match_percent", 100.0)` — the most optimistic default — then `if pct >= 100: continue` | ✅ |
| `function_health.py` | **the whole tool**: batch SQL named `source_path`/`match_percent`, neither of which exists; `except Exception: return []` turned the `OperationalError` into `No functions found matching criteria.` | ✅ query repaired, failures propagate |
| `compare_progress.py` | function-level comparison on `fuzzy` only (395 rows are `normalized==100, fuzzy<100`); base-only and current-only keys dropped with no count | ✅ (its `Regressions (N functions, showing top M)` line was already the right pattern) |
| `name_charge_census.py` | 16,780 rows / 5,129,540 B behind `F(None) → 0.0` colliding with a real 0.0; examined population was only 2,238 rows | ✅ |
| `scope_index_census.py` | an unsorted recursive `glob` into a last-write-wins dict: **568 of 6,675 (function, static) pairs hold conflicting scopes**, so those functions' verdicts flipped between runs | ✅ |
| `ceiling_calculator.py` | `insert_delete` (the most *fixable* class) counted as unfixable, systematically manufacturing "at limit"; 1,231 `current_percent IS NULL` rows silently outside the band | ✅ |
| `reclassify_at_limit.py` | funnel 3,796 → 1,701 → 1,517 with only `1517 candidates` printed; no `excluded = 0`; `UPDATE ... WHERE symbol = ?` **with no unit qualifier** | ✅ |
| `remaining_work.py` | headline `140 functions` where 363 fell to a hardcoded skip list and 314 to `--min-bytes 500` — 83% of ~817; plus `total = done + partial + len(stubs)` double-counting partials in 216 of 218 units | ✅ |
| `dta_access_audit.py`, `dta_hierarchy_scan.py`, `dta_dataflow.py`, `dta_trace_validator.py` | **missing input ⇒ clean verdict**: `orig-assets/` is absent from every worktree, so all four print "No … issues found." having checked nothing | ✅ |
| `home_store_census.py`, `frame_deficit_census.py`, `report_absent_census.py` | uncounted unit/body skips (1,245 units; 65,661 of 114,857 unreadable target frames); unsorted `os.walk` + no `ORDER BY` | ✅ |
| `reset_false_complete.py` | `LIKE '%base_size=0%'` — unescaped `_`, on the predicate of an `UPDATE` | ✅ |
| `patches/apply_safe.py`, `unicorn/refresh_frontier.py`, `reloc_strict_classify.py`, `decomp_orchestrate.py` | `--limit` truncating a patch queue / a frontier sweep / a candidate census with no notice | ✅ |
| `find_hidden_work.py`, `batch_check.py`, `sync_objdiff.py` | fuzzy-ruler writes to `verdict`; `current_percent IS NOT NULL` hiding never-measured COMPLETE rows; a substring SDK filter swallowing 4 authorable units; `elif match_pct > 0` leaving exactly-0% rows in no bucket at all | ✅ |

### Honest already — the premise was wrong for these

| script | why |
|---|---|
| `reloc_strict_classify.py` | every candidate lands in exactly one labelled class including `error`; `--limit` defaults to 0 and is documented "debug"; denominators on stderr, stdout **and** in the JSON; display slices are named `_sample` |
| `progress_metrics.py` | prints **both** rulers side by side with the canonical one marked, and *reads* `functionRelocDiffs` from the report rather than hardcoding it, with the comment "or this document will confidently state the wrong ruler" |
| `certify_floor.py` | see above — the `denominator_self_check` |
| `report_absent_census.py` | a denominator at every stage plus a recorded load-bearing **negative**, and a docstring that says "Re-run this before ever concluding otherwise" |
| `fold_proof.py` | prints every verdict class including the ones that conclude nothing; three explicit refusals-to-certify (vacuity guard, cheap-identity guard, `COMDAT_SELECTION_MISSING`); `UNDECIDABLE` is first-class, not a fallthrough |
| `grindarray_divergence.py` | full 64×256×256 population, no sampling, and it prints an explicit negative result. (Its trailing `CONCLUSION:` block was an unconditional `print` of a fixed string — noted, not load-bearing.) |
| `dta_trace_validator.py` | the only DTA tool that prints a real denominator on **both** the clean and the dirty outcome, and that counts unresolvable rows instead of vanishing them |
| `scan_behavioral_idioms.py` | no caps, no slices, sorted `rglob`, and a git failure printed to stderr rather than swallowed |
| `frame_deficit_census.py` (`--max-percent` default **101.0**) | defaulted *above* 100 on purpose, with the comment "so the filter never silently drops the function under test". The corrected form of this whole bug class. |
| `lp64_scanner.py` (worker pool) | uses `Pool(initializer=...)` so each process fully initialises before any work item — the **correct** pattern, not the `data_symbol_scan` race |

---

## 5. If you are writing the next scanner

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
