# The Work Frontier

**Measured 2026-08-20** against `main` at `d505987fa`, from the pinned
`build/373307D9/report.json` written at 08:42 by that commit's build
(`md5 477391026c9c74a95b97e0673a56d9fb`), joined onto the shared
`/home/free/code/milohax/dc3-decomp/decomp.db`.

objdiff-cli `4.2.3` (`88b425bc3bad-dirty`), `functionRelocDiffs=name_check`.
**`bin/objdiff-cli`'s mtime was `2026-08-18 03:03:13` at the start of this
measurement and unchanged at the end** — so the relocation-name fix sitting on
`../objdiff` branch `fix/namecheck-reloc-normalized` (`b14ba45`) is **not** in
the binary that produced any number here. When it is built, wrong-callee bugs
begin costing normalized points and the headline is expected to fall ~0.11 pp.
Every figure below is on the pre-fix ruler.

`main` moved during this lane (`d505987fa` → `b25928dfb`; other lanes landing
near-miss and string-literal work). **`build/373307D9/report.json` did not** —
it still hashes to the value above, so every number here is current against the
tree as last built. Re-run `ninja` and re-measure if that stops being true.

**This document contains no worklist.** Per
[`REMAINING_WORK.md`](REMAINING_WORK.md), the queries are the deliverable; the
numbers are what they returned on the date above. Regenerate before quoting:

```bash
python3 scripts/analysis/frontier.py \
    --db /home/free/code/milohax/dc3-decomp/decomp.db      # from a worktree
python3 scripts/analysis/frontier.py --section near-complete --max-remaining 1
python3 scripts/analysis/frontier.py --section certs
python3 scripts/progress_metrics.py                        # the canonical headline
```

---

## 1. Headline and the three denominators

| | value | denominator |
|---|---|---|
| **CANONICAL (MATCHED)** | **91.66 %** (29,515 / 32,202 fns) | authorable rows in `report.json`, `match_percent_normalized == 100` |
| Authorable code bytes | 78.76 % (4,995,600 / 6,343,156) | same |
| Complete authorable units | 44.88 % (434 / 967) | authorable units where every function is at 100 |
| **Remaining** | **2,687 fns / 1,203,008 bytes** | the subject of this document |

Three different denominators are in daily use and they disagree. Name the one
you are quoting:

| denominator | n | what it drops |
|---|---|---|
| `report.json` authorable (`progress_metrics.py`, `frontier.py`) | **32,202** | 16,131 SDK/vendor rows (`default/xdk/*`, `default/lib/binkxenon/*`) + 11 link-glue shadow rows |
| `decomp.db` `excluded = 0` | **31,446** | 21,122 rows the DB marks unscoreable (ICF fold survivors, EH funclets, stale split spellings, unreferenced inline COMDATs) |
| `certify_floor.py`'s `authorable_done` view | **27,545** | the above **plus** every symbol starting `merged_`, `lbl_`, `fn_`, `??_` |

The gap between the first two is 756 rows. The gap between the second and third
is 3,901, and **285 of the currently-unmatched 2,687 fall into it** — see §5.3.

Reconciliation between `report.json` and the DB is otherwise excellent, and
worth stating because it is a negative result that saves a lane: **zero rows are
marked `COMPLETE` in the DB while scoring below 100 in `report.json`.** There is
no certificate rot on the canonical ruler today. Conversely 15 authorable
`report.json` rows have no DB row at all.

---

## 2. The frontier, banded

Denominator: 32,202 authorable functions, of which 29,515 are at
`match_percent_normalized == 100`. The 2,687 that are not:

| band | fns | bytes | DB `AT_LIMIT` | DB no verdict | DB `excluded=1` | no DB row |
|---|---|---|---|---|---|---|
| `[99.9, 100)` | 154 | 101,304 | 83 | 65 | 6 | 0 |
| `[99, 99.9)` | 129 | 130,624 | 117 | 10 | 2 | 0 |
| `[95, 99)` | 687 | 407,272 | 400 | 271 | 16 | 0 |
| `[90, 95)` | 550 | 191,592 | 250 | 296 | 4 | 0 |
| `[80, 90)` | 263 | 162,912 | 239 | 18 | 6 | 0 |
| `[50, 80)` | 122 | 76,036 | 109 | 8 | 5 | 0 |
| `(0, 50)` | 24 | 21,064 | 21 | 2 | 1 | 0 |
| `0` (no body written) | 758 | 112,204 | 348 | 346 | 60 | 4 |
| **TOTAL** | **2,687** | **1,203,008** | **1,567** | **1,016** | **100** | **4** |

Read the two right-hand columns as the real split: **1,567 functions carry a
live "certified unfixable" label and 1,016 have never been adjudicated at all.**
The second number is the untouched pool, and it is large.

> **Ruler caveat carried forward.** Below a blind score of ~95, objdiff is
> pairing instructions it merely lined up. Treat a per-row delta in the `[80,90)`
> and lower bands as a *shape* signal (whole clusters missing = our source is
> structurally different) and never as a per-instruction lead.

### Remaining work by port relevance

| | fns | bytes |
|---|---|---|
| **ENGINE** `default/system/**` | 2,433 | 1,071,832 |
| **GAME** `default/lazer/**` | 157 | 100,432 |
| OTHER | 54 | 5,040 |
| LOW — curl / jpeg / holmes / zlib / Bink (the port does not run these) | 43 | 25,704 |

The deprioritised subsystems are **1.6 % of remaining functions and 2.1 % of
remaining bytes**. Excluding them changes nothing material; they are not what is
standing between this project and 100 %. Do not spend a lane on the exclusion.

---

## 3. The unwritten-body tier is 758 functions, and the labels hide 46 % of it

`report.json` emits **no `fuzzy_match_percent` at all** for a function whose
body was never written; `match_percent_normalized` is then 0.0. That is
**759 / 758 authorable functions** (the one-row difference is a symbol scored
100 normalized with no fuzzy score) totalling **112,204 bytes** — 4.2 % of the
whole remaining-function count and by far the largest single homogeneous class.

**348 of them carry a live `AT_LIMIT` verdict.** None carries a floor
certificate. Their `verdict_reason` text says what happened:

| n | `verdict_reason` |
|---|---|
| 164 | `reset: stub with no source implementation` |
| 157 | *(null)* |
| 20 | `reset: was COMPLETE+is_stub (no source impl)` |
| 3 | `ICF merged address - not a real individual function` |

**An `AT_LIMIT` on a function with no body is a bookkeeping reset, not a floor.**
Any query of the form "how much AT_LIMIT is left" counts 73,832 bytes of
never-attempted work as certified-unfixable. Filter it out with
`floor_certificate IS NULL AND match_percent_normalized = 0`.

By subtree (bytes of unwritten body):

| bytes | subtree |
|---|---|
| 37,208 | `default/system/synth_xbox` |
| 17,344 | `default/system/rnddx9` |
| 16,032 | `default/system/os` |
| 11,632 | `default/system/rndobj` |
| 6,924 | `default/system/char` |
| 5,428 | `default/system/hamobj` |

`synth_xbox` + `rnddx9` + `os` is **60 %** of the unwritten tier. These are the
reference-less reconstruction targets — XAudio2/XMA, D3D9 and the Xbox platform
layer, where no RB3 or og-dc3 source exists and the body has to come from the
target assembly.

---

## 4. The AT_LIMIT population

1,567 functions / 903,704 bytes carry a live `AT_LIMIT` and are below 100 on the
canonical ruler.

| floor certificate | fns | bytes |
|---|---|---|
| `equivalent` | 726 | 476,832 |
| *(none)* | 517 | 168,972 |
| `permuter_exhausted` | 164 | 109,136 |
| `artifact:stack_layout` | 78 | 82,348 |
| `icf_merged` | 37 | 35,036 |
| `artifact:orig_error` | 20 | 18,324 |
| `artifact:merged_call` | 9 | 7,808 |
| `artifact:build_env` | 9 | 2,956 |
| `artifact:regalloc` | 6 | 1,628 |
| `artifact:merged_arg` | 1 | 664 |

**517 of the 1,567 (33 %) carry no certificate at all.** They are `AT_LIMIT` by
assertion. 348 of those 517 are the unwritten bodies from §3.

**83 sit at ≥ 99.9 %** — certified unfixable while one or two instructions from
perfect. That is the cheapest re-audit in the project and the highest-yield
place to test whether the certificates mean anything.

Blind-sample audits of this population are recorded in §6.

---

## 5. Scanners: what the repairs fixed, and what is still lying

Seven tools were re-run against their repaired code. **Three defects were still
live**, and the deepest one was *created visible* by a repair rather than fixed
by it. Summary:

| tool | after its documented repair | this lane |
|---|---|---|
| `function_health.py` | row selection fixed; **analysis half returned 2,705/2,705 error rows serialised as `ceiling 100.0 / headroom 0.0`, exit 0** | fixed the invocation, then guarded the verdict (§5.1, §5.2) |
| `ceiling_calculator.py` | clamp *disclosed*, not removed — **74.7 % of ceilings still clamped, headline field is an identity function** | root-caused: the ceiling and the measurement are on different rulers (§5.2) |
| `certify_floor.py` | wildcard escaped correctly; **`is_stub = 1` still counts as DONE, `is_stub` misses 401 of 758, `??_` filter hides 144 real functions** | quantified (§5.3); not repaired |
| `remaining_work.py` | honest, exemplary coverage block | verified (§5.4) |
| `batch_pattern_scan.py` | uncapped by default | verified — **68 of its 69 current hits were invisible under the old cap** (§5.5) |
| `fake_impl_scan.py` | NULL-skip fixed | verified; two small disclosure gaps found (§5.6) |
| `data_symbol_scan.py` | uncapped by default | verified (§5.5) |

Every tool named here was run **twice** and its output diffed; all were
byte-identical, including two full 1,568-row `ceiling_calculator` runs (identical
down to row order). `scripts/analysis/determinism_check.py` was extended from 9
cases to 13 and reports **13/13 agree** — see §5.7 for what it still does not
cover, which it now names.

### 5.1 WAS LYING, NOW REPAIRED — `function_health.py` batch mode returned 2,705 error rows and called them "ceiling 100.0, headroom 0.0"

The SQL repair worked: batch mode now selects **2,705** rows over
`--min 0 --max 99.99` instead of answering *"No functions found matching
criteria"* to every query ever put to it.

**But the analysis half had never worked.** Every row goes
through `_run_objdiff(symbol)`, which invoked
`objdiff-cli diff --symbol <sym>` — objdiff-cli takes the symbol
**positionally**, there is no `--symbol` flag, and the call exited 1 every time.
The module's own docstring documents this and says it was left unrepaired
deliberately because "fixing the call changes what this tool FINDS". The
consequence was not confined to `--symbol` mode:

```
$ python3 scripts/analysis/function_health.py --db <real> --min 0 --max 99.99 --limit 0 --json
2705 rows, verdict distribution: [('error', 2705)]
```

**2,705 of 2,705.** Zero rows have ever been analysed by this tool.

The human-readable surface is honest about it (`Errors: 251`, `Workable: 0`).
**The `--json` surface is not.** `HealthReport`'s dataclass defaults are
`ceiling_percent = 100.0` and `headroom = 0.0`, and the error path returns the
default-constructed report, so every one of those 2,705 rows serialises as:

```json
{"total_instructions": 0, "total_mismatches": 0, "fixable_mismatches": 0,
 "ceiling_percent": 100.0, "headroom": 0.0, "suggestions": [], "verdict": "error"}
```

A consumer filtering on `headroom > 0`, or `verdict == "workable"`, or
`fixable_mismatches > 0` gets **zero rows and exit code 0** — the same shape as
"this class is exhausted", which is the exact failure the honesty pass set out
to remove. It is arguably worse than the original: the original returned an
empty list, which at least looks empty. This returns 2,705 affirmatively wrong
records.

**Fixed by this lane.** Negative control, reproducible on any tree:

```
$ bin/objdiff-cli diff --symbol '?Poll@BlockMgr@@QAAXXZ' --format json
rc 1, empty stdout, "Unrecognized argument: --symbol"
$ bin/objdiff-cli diff '?Poll@BlockMgr@@QAAXXZ' --format json
rc 0, {"symbol":...,"normalized_match_percent":99.98214,...}
```

The symbol is now passed positionally and the tool produces real verdicts.
Two negative-control tests were added to
`scripts/analysis/tests/test_honesty_clusters.py` and both were sabotage-tested
(restoring `--symbol` fails one; deleting the ceiling guard fails the other).

**Read §5.2 before consuming the result.** Making the tool run exposed a second,
deeper defect that the error rows had been hiding.

### 5.2 ROOT CAUSE — the ceiling and the measurement are on different rulers, and `ceiling_calculator.py` clamps the result

**This is the most consequential finding of the pass.** Both `function_health.py`
and `ceiling_calculator.py` compute a ceiling as

```python
ceiling = 100.0 - 100.0 * unfixable_mismatches / total_instructions
```

— an **unweighted instruction-count ratio**. They then subtract it from
`match_percent`, which is objdiff's **score-weighted** `normalized_match_percent`,
a number that gives near-full credit to a partially-matching instruction (a
register-only difference costs almost nothing). **The two are not on the same
scale and the subtraction is meaningless.** It reads systematically low.

Verified by formula on **25 of 25** functions in `default/system/math/*`
(`function_health --json`, after its invocation was repaired):

| measured norm % | insns | unfixable | "ceiling" | headroom |
|---|---|---|---|---|
| 99.898 | 59 | 6 | 89.83 | **−10.07** |
| 97.688 | 64 | 22 | 65.63 | **−32.06** |
| 94.265 | 49 | 31 | 36.74 | **−57.53** |
| 84.833 | 50 | 42 | 16.00 | **−68.83** |

**21 of the 25 have a ceiling below their own measured percent** — structurally
impossible. `Multiply(Vector3 const&, Quat const&, Vector3&)` is measured at
84.8 % and assigned a ceiling of 16.0 %.

Across the whole `[99, 99.99)` band — 244 functions, every one re-measured by a
fresh objdiff run — **230 (94.3 %) come out with a ceiling below their own
measured percent.** Only 10 survive as a genuine `at_limit`. Without the guard
this lane added, those 230 would each have been emitted as
`at_limit — "Ceiling N % — no room to improve"` on a function within 1 % of
perfect. Worst case in that band: `?mash@@YAXPAE0@Z`, measured 94.17 %, ceiling
23.33 %.

**This is the mechanism behind the clamp.** `ceiling_calculator.py` reports
`ceilings_clamped_up_to_current: 1172 / 1568 = 74.7 %` and its source calls the
clamp *"the classifier disagreeing with the grader"*. It is not a disagreement,
it is two rulers — **the 74.7 % clamp rate is a property of the scale mismatch,
not of the functions**, and it will not move no matter how the classifier is
tuned. Every `at_limit` verdict either tool produced through this path is
unsupported, and re-tuning `insert_delete` will not rescue them.

`function_health.py` does not clamp, so with its invocation repaired it began
emitting `at_limit — "Ceiling 89.8 % — no room to improve"` on a function
measured at 99.898 %. That is worse than the error row it replaced, so this lane
added a guard: a ceiling below the measurement now yields verdict
`ceiling_unusable` with both numbers printed, never a certificate. Computing the
ceiling on objdiff's own weighted score is the real fix, is a change to what the
tool *finds*, and needs its own validation — it is not done here.

### 5.2b `ceiling_calculator.py` — the clamp, and its headline field

Full run over the whole AT_LIMIT population (universe 3,780; dropped 2,178
`excluded=1` and 34 `merged_`; **1,568 examined, 0 objdiff errors**):

```
ceilings_clamped_up_to_current: 1172 / 1568 = 74.7 %
```

The repair **disclosed** the clamp; it did not remove it. The rate is unchanged
from the 74.9 % on record. And the effect on the field consumers actually read
is total:

| field | rows with headroom > 0.001 (of 1,568) |
|---|---|
| `ceiling_percent` (the headline) | **43** |
| `ceiling_percent_optimistic` | **944** |

`ceiling_percent == ceiling_percent_conservative` on **1,568 of 1,568** rows —
the optimistic column is computed and never surfaced. The single input that
separates them is whether `insert_delete` counts as a floor. It is **12.1 % of
all 284,605 mismatches** in this population, and `function_health.py`'s own
source calls it *"CONTESTED: ceiling_calculator treats this as reachable, not a
floor"*.

So the tool as consumed says *1,525 of 1,568 AT_LIMIT functions have zero
headroom*; its own second opinion says 944 have headroom, 572 of them ≥ 5 pp.
**Every "you are already at your ceiling" verdict from this tool remains
unsupported.** Read `ceiling_percent_optimistic`, or nothing.

Two further live traps its own coverage block names, to its credit:
its `--min/--max` band and sort key are `current_percent` (the drifting column),
and its default ruler is `objdiff-cli diff`'s base config, **not**
`report.json`'s — pass `--use-graded-ruler` to compare against anything in §2.

### 5.3 STILL LYING — `certify_floor.py`'s `authorable_done` view counts unwritten functions as DONE

The `_` -wildcard bug is genuinely fixed (`symbol NOT LIKE '??~_%' ESCAPE '~'`),
and the two-path denominator self-check agrees with itself (27,545 fns both
ways). Two problems survive.

**(a) `is_stub = 1` is a DONE state.** The view's `is_done` CASE reads:

```sql
WHEN match_percent_normalized >= 100 ... THEN 1
WHEN is_stub = 1                         THEN 1      -- <-- unwritten counts as done
WHEN floor_certificate IS NOT NULL       THEN 1
```

`--summary` therefore prints **`DONE with certs: 26,955/27,545 (97.86 %)`** with
305 never-written functions inside the numerator. Cross-checked against
`report.json`: of those 305, **229 have no body at all** (62,920 bytes), 72 have
a partial body below 100 %, and 4 are actually at 100 %. **301 of 305 are not
done.** The natural "what is left" query — `SELECT * FROM authorable_done WHERE
is_done = 0` — returns 590 rows and hides every one of them.

**(b) `is_stub` is wrong in both directions and misses 53 % of the class.**

| | n |
|---|---|
| `report.json` says no body (norm == 0), authorable | 758 |
| `decomp.db` `is_stub = 1`, `excluded = 0` | 454 |
| both agree | 357 |
| unwritten but `is_stub = 0` — **invisible to every `is_stub` query** | **401** (37,288 B) |
| `is_stub = 1` but `report.json` scores it (90 partial, 7 at 100 %) | 97 |

**(c) the `??_` prefix filter drops 144 real unmatched functions.** Escaping the
wildcard was correct; the prefix list is not. `??_7` (vtable) and `??_R` (RTTI)
are data, but `??_E` / `??_G` are **vector and scalar deleting destructors**,
`??_H` is the vector constructor iterator, and `??__E` / `??__F` are dynamic
initialisers — all real code that must match. Of the 2,687 remaining, this
filter hides:

| n | bytes | prefix |
|---|---|---|
| 80 | 7,692 | `??_E` vector deleting destructor |
| 37 | 2,852 | `??__` dynamic init / atexit |
| 21 | 1,988 | `??_G` scalar deleting destructor |
| 6 | 352 | `??_F`, `??_D`, `??_H`, `??_V` |

**2,309 authorable `??_`-prefixed functions are already at 100 %**, so the class
is demonstrably matchable and these 144 are simply hidden work. Combined with 35
`fn_` and 6 `merged_` rows plus the 100 DB-excluded rows, `authorable_done`
cannot see **285 of the 2,687** (50,008 bytes).

### 5.4 HONEST — `remaining_work.py`

Coverage block is exemplary: universe 48,344, examined 15,440 (31.94 %), drops
named (`unit-has-no-remaining-work` 16,772, `unit-not-complete` 16,132), and it
volunteers that at `--max-percent 0` the `partial` bucket is *structurally
empty*. Reports 769 stubs / 112,364 B in complete units — consistent with §3.

One structural narrowness to know, disclosed but easy to miss: **it only looks
inside units `report.json` marks complete**, so 16,132 functions in incomplete
units are out of scope by construction. It is a near-complete-unit finder, not a
frontier tool.

### 5.5 HONEST, and the uncap recovered almost everything — `batch_pattern_scan.py`

Both this and `data_symbol_scan.py` now default to **no cap** (`--limit 0`,
`--max-symbols 0`), print a `TRUNCATED` banner and set `truncated=true` when a
cap is passed, and `batch_pattern_scan` validates `--pattern` against a literal
allowlist so a typo can no longer read as "this pattern is exhausted".

The uncapped run (universe 48,344; **1,703 examined**; drops named:
`below---min-pct` 29,769, `no-base-body-outside-band` 16,872) finds **69
functions carrying a mechanically-fixable encoding pattern**:

| pattern | hits |
|---|---|
| `bool_mask_24` | 54 |
| `fma_mismatch` | 49 |
| `extrwi_rlwinm` | 6 |
| `bool_mask_31` | 6 |
| `cmp_encoding` | 1 |

**68 of the 69 score below 99.58 %** — meaning the old `--limit 200` default,
which sorted descending *before* slicing, could see exactly **one** of them.
The truncation bug was not "a sample"; on this metric it was a ~99 % miss. The
recovered hits are ordinary engine code — `hamobj` 14, `rndobj` 10, `char` 9,
`world` 5, `gesture` 5, `utl` 5, `math` 4.

Caveat to carry: this tool bands on **`fuzzy_match_percent`**, the
relocation-sensitive ruler, not the canonical normalized one. Its own coverage
block says so.

### 5.6 HONEST — `fake_impl_scan.py`, with two small disclosure gaps

The `if pct is None: continue` is replaced by a fallback to
`match_percent_normalized` with the skipped rows counted, not dropped. Its own
docstring records that the hole sat open "from wave-14 through wave-23, across
four broad sweeps that all reported the pool EXHAUSTED".

Full run (candidate set 701 at `pct <= 70`, `target >= 80 B`; 0 errors,
0 unscannable, 0 skipped-for-no-percent):

- **625 fake implementations** — trivial body against a substantial target.
  **486 are authorable** (107,604 bytes); the other **139 are Bink**.
- **76 real-code divergences** (57,472 bytes) — bodies that are substantial and
  *wrong*. 73 `real-code-divergence`, 3 `incomplete-impl`. Concentrated in
  `rndobj` (27 fns / 22,668 B), `math` (3 / 8,904 B), `gesture` (7 / 7,088 B).

Two gaps worth closing, neither fatal:

1. **It rolls its own non-authorable prefix list** — `("xdk/", "default/xdk/",
   "thirdparty/", "default/thirdparty/")` — instead of importing
   `scripts/authorable.py`'s `is_authorable()`, which is the whole reason that
   module exists. `default/lib/binkxenon/` is missing, so **22 % of its
   headline finding count is vendor code nobody will ever write**.
2. **`divergence_count` and the `divergences` array disagree unless you pass a
   flag.** Without `--include-divergences` the JSON reads
   `{"divergence_count": 76, "divergences": []}`. A consumer reading the array
   sees zero. Smaller than §5.1's failure but the same shape.

The 76 divergences are the highest-value bug-hunting output any scanner
produced in this pass: a substantial-but-wrong body is invisible to the native
port's tests *and* looks like ordinary decomp work in the metric. Largest:

| target | norm % | missing | unit / symbol |
|---|---|---|---|
| 5,856 B | 55.69 | 19.9 % | `math/SHA1` `CSHA1::Transform` |
| 5,188 B | 64.70 | 14.0 % | `gesture/DepthBuffer3D` `DrawShowing` |
| 2,800 B | 67.27 | 17.3 % | `math/mtx` `Invert(Matrix4 const&, Matrix4&)` |
| 2,104 B | 32.72 | 53.8 % | `world/SpotlightDrawer_NG` `NgSpotlightDrawer::SetupXSection` |
| 2,100 B | 36.45 | 30.1 % | `rndobj/Shader` `RndShaderStandard::CalcShaderOpts` |
| 1,668 B | 11.76 | 41.5 % | `rndobj/Shader` `RndShaderMultimesh::CalcShaderOpts` |

### 5.7 Determinism

`scripts/analysis/determinism_check.py` was extended from 9 curated cases to 13,
adding `progress_metrics`, `frontier`, `function_health` and
`certify_floor --summary` — all four are work-selection oracles, the class whose
nondeterminism reads as "exhausted", and none had ever been checked. **13/13
agree with themselves** on non-empty output.

`ceiling_calculator` was checked by hand outside the harness: two full 1,568-row
runs produced identical output including row order
(`ceilings_clamped_up_to_current = 1172` both times). `batch_pattern_scan`,
`data_symbol_scan` and `fake_impl_scan` are too expensive to run twice inside
the harness and are now **named in its output as UNCHECKED**, so a green 13/13
cannot be read as covering them.

---

## 6. Which "exhausted" verdicts survive contact

Three blind, fixed-seed samples were drawn from the AT_LIMIT population and
handed to independent auditors with no knowledge of each other's results. The
sampling queries are in §8 so the draw can be reproduced or re-drawn.

| population | size | sampled | busted |
|---|---|---|---|
| `AT_LIMIT` at norm ≥ 99.9 | 83 | 10 | **2** (both driven to exactly 100.0 and landed) |
| `floor_certificate = 'equivalent'`, still < 100 | 726 | 10 | *audit lane still running at time of writing* |
| `floor_certificate = 'permuter_exhausted'`, still < 100 | 164 | 10 | *audit lane still running at time of writing* |

### 6.1 The ≥ 99.9 % sample: 2 of 10 busted

Branch `audit/cert-near100-20260820`.

- **`FloatKeys::SetFrame`** 99.96296 → **100.0**. Six off-by-4 stack diffs: the
  target gives `val` its own slot at 0x54; our source overlaid it on `ref` at
  0x50 because they lived in disjoint scopes and MSVC merges same-type temps
  across them. Declaring `ref, val, prev, next` together in the enclosing `if`
  reproduces the target's ascending assignment.
- **`MultiTempoTempoMap::PointForTime`** 99.90909 → **100.0**. Our source built
  a `TempoInfoPoint` to hold the float before calling `upper_bound`; DC3 passes
  the float straight through. **The RB3 reference does build one** — which is
  how the wrong shape got into our source in the first place. A standing hazard
  for every RB3-derived function.

The other eight were judged SOUND on evidence — pure FPR rotation
(`SetRegularShaderConst`: 27 of 31 mismatches are an f27→f30 rotation costing
zero normalized points), backend load-order scheduling verified by three
byte-identical source spellings (`FillCompressedVertex`), or a callee-saved GPR
count in a shared STLport header.

**Three things this sample escalated:**

1. **`floor_certificate = 'equivalent'` is not a floor proof.** It is a
   *unicorn behavioural* verdict — "the emulator saw the same behaviour" — which
   says nothing about whether a source change can reach the target's
   instructions. Seven of the ten sampled rows carried it, two carried
   `artifact:stack_layout` (also a unicorn divergence class), and **both busted
   rows came from that group**. Only one carried `permuter_exhausted`, the only
   label in the vocabulary that is actually a search-exhaustion claim. **726
   rows carry `equivalent`** — a third of the AT_LIMIT population is labelled
   with a behavioural verdict being read as a reachability verdict.
2. **`current_percent` disagreed with the canonical ruler on 8 of 10**,
   understating by up to 0.29 pp (`SetRegularShaderConst`: DB 99.698 vs
   canonical 99.992). `match_percent_normalized` in the DB agreed on all 10 to
   its rounding. Select work with the normalized column, never `current_percent`.
3. **`run_objdiff`'s `ADDRESS_RELOCATION_NOISE` verdict was wrong on both
   functions where it fired at high confidence** (`LocalizeFloat`,
   `BlockMgr::Poll`). Both are genuine static-data *layout* deltas — the
   displacement moved, which no linker artifact explains — and `BlockMgr`
   visibly responded to a declaration reorder. The detector appears to classify
   any mismatch carrying a relocation as noise without checking whether the
   displacement also moved. That is the same failure shape as the
   "Offset Mismatches (resolved)" block already documented in `CLAUDE.md`.

Independent of any sample, three "exhausted" claims are **refuted by
construction** from the data in this document:

1. **"The AT_LIMIT class is certified."** 517 of 1,567 (33 %) carry no
   certificate, and 348 of those have no body at all. A third of the class is
   `AT_LIMIT` by assertion. A further **726 carry `equivalent`, which is a
   unicorn behavioural verdict, not a reachability proof** (§6.1) — so only
   **164 rows in the whole population (10 %) carry a label that even claims a
   search was exhausted**.
2. **"You are already at your ceiling."** Unsupported for the whole population.
   The ceiling is an unweighted instruction ratio and the measurement is a
   score-weighted score; they are not comparable, 74.7 % of the comparisons come
   out negative, and the tool clamps them (§5.2). This is not a tuning problem.
3. **"The stub pool is exhausted."** 758 functions have no body; the tooling
   that queries `is_stub` can see 357 of them and the `authorable_done` view
   counts 305 as *done*.

`permuter_exhausted` deserves a standing caveat regardless of what any sample
shows: it is a statement about a **search**, not about reachability. decomp-synth
explores behaviour-neutral C++ transforms only, so it structurally cannot find a
fix that changes what the code does — a wrong field, a missing branch, a
differently-shaped loop. The label means "the permuter ran out of moves", and
should never be read as "no source change exists".

---

## 7. Ranked lanes

Ranked by *matched functions per unit of effort*, weighted toward code the
native port runs. Evidence for each ranking is the section it points at.

### 1. Close the 151 units that are ONE function from complete
**151 functions, 99,948 bytes.** Complete authorable units go **434 → 585 of 967
(44.9 % → 60.5 %)** — the largest single move available on any headline.
23 of the 151 are already ≥ 99 %; 14 are ≥ 99.9 %. 124 are engine, 27 game.
Composition: 40 no verdict, 100 `AT_LIMIT` (67 of them `equivalent`, 18
`permuter_exhausted`), 11 excluded. 37 have no body at all.
Query: `frontier.py --section near-complete --max-remaining 1`.
Extend to `--max-remaining 2` for **247 units / 343 fns / 179,512 B**.

### 2. Re-audit the 83 `AT_LIMIT` certificates at ≥ 99.9 %
**83 functions, 40,992 bytes.** One or two instructions each. 55 are certified
`equivalent`, 14 `permuter_exhausted`, 6 uncertified. Cheapest possible test of
whether the certificate vocabulary means anything, and every bust is an
immediate matched function. Precedent: the regswap AT_LIMIT blind sample busted
3 of 10.

### 3. Work the 1,016 never-adjudicated functions in the 90–99 bands
**567 functions, 148,924 bytes** (271 in `[95,99)`, 296 in `[90,95)`) plus 65 in
`[99.9,100)` worth 49,936 B. These carry **no verdict at all** — nobody has
looked. This is the single largest pool of genuinely untouched, in-range work,
and it is invisible to any query that starts from `verdict = 'AT_LIMIT'`.
Concentrated in `rndobj/Utl`, `world/LightPreset`, `rndobj/Mesh`,
`rndobj/MeshAnim`, `rndobj/PropAnim`, `obj/Dir`, `rndobj/Text`.

### 4. Reference-less body reconstruction: `synth_xbox`, `rnddx9`, `os`
**~350 functions, 70,584 bytes** of the unwritten tier (60 % of it). No RB3 or
og-dc3 reference exists; bodies come from target assembly. Metric-visible
(0 → 100 per function) *and* the class most likely to hide real native-port
bugs, because a function with no body silently does nothing at runtime. Largest
single items: `PlatformMgr::Poll` (4,844 B), `Synth360::SetGlobalReverbPreset`
(3,416 B).

### 5. Rebuild the ceiling on objdiff's own weighted score
**The single highest-leverage tooling fix left, and the reason lanes 2, 6, 10
and every AT_LIMIT re-triage are currently flying blind.** The ceiling is an
unweighted instruction ratio; the measurement is a score-weighted score; 94.3 %
of the comparisons come out negative (§5.2). This lane fixed
`function_health.py`'s invocation and guarded it from certifying nonsense, but
**the ceiling model itself is untouched** — nothing currently tells you which
of the 1,567 AT_LIMIT functions has headroom.

The fix is to compute the ceiling as *the normalized score we would reach if the
fixable mismatches were closed*, on objdiff's own scale, rather than as a raw
instruction count. Once it exists, every AT_LIMIT verdict produced through the
old path should be retired: they cannot be distinguished from the scale error.
Meanwhile `ceiling_calculator.py`'s clamp should go — it converts a visible
absurdity into an invisible one, and its `ceiling_percent` field should either
surface `ceiling_percent_optimistic` (944 rows with headroom vs the headline's
43) or be deleted.

### 7. Repair `authorable_done`: stop counting stubs as done, fix `is_stub`, narrow the `??_` filter
Three edits: drop `is_stub = 1` from the `is_done` CASE (recovers 301 rows into
the open set); rebuild `is_stub` from `report.json`'s missing
`fuzzy_match_percent` rather than whatever wrote it (recovers 401 more, corrects
97); replace the `??_` prefix with the data-only prefixes `??_7`, `??_R`, `??_8`
(recovers 144 real functions, 12,884 B). The 97.86 % DONE-WITH-CERTS headline
will fall — that is the point.

### 8. The 76 substantial-but-wrong bodies from `fake_impl_scan`
**76 functions, 57,472 bytes**, all authorable. These are the ones that look
like ordinary decomp work in the metric and are silently wrong at runtime, so
they carry native-port value on top of the percentage. `rndobj` holds 27 of
them (22,668 B), of which 3 are `CalcShaderOpts` overloads at 11–39 %.
Query: `fake_impl_scan.py --include-divergences` (**the flag is mandatory** —
without it the JSON reports 76 and hands you an empty array).

### 9. The 69 mechanical encoding-pattern hits
**69 functions**, each with a named fix (`bool_mask_24` ×54, `fma_mismatch`
×49, `extrwi_rlwinm` ×6, `bool_mask_31` ×6, `cmp_encoding` ×1). 68 of the 69
were invisible to every previous sweep because of the `--limit 200` sort-before-
slice bug, so this pool is effectively untouched despite the tool being old.
Query: `batch_pattern_scan.py --min 90 --max 99.9 --limit 0` (budget >10 min).

### 10. The `[80,90)` band, 239 of 263 already labelled `AT_LIMIT`
**263 functions, 162,912 bytes.** Structurally-different-source territory, where
`insert_delete` clusters dominate and objdiff's pairing is unreliable. High
bytes-per-function (620 B avg) and the class §5.2 most likely mis-certified. Do
these *after* the ceiling tool can tell you which have headroom.

### 11. The 285 functions `authorable_done` cannot see
**50,008 bytes.** 144 `??_E`/`??_G`/`??__` compiler-generated bodies, 35 `fn_`
funclets, 6 `merged_`, 100 DB-excluded-but-report-authorable. Not hard work —
work that no work-selection query currently emits. Needs §7 first.

### 12. `rndobj/Text` and `rndobj/Utl`
**94 functions, 49,176 bytes** across two units, the two largest single-unit
remainders. `Utl` is STL instantiation surface (56 fns, many small); `Text` is
the marquee/wrapping family with a documented bug history
(`project_rndtext_marquee_bug_family`). Both are engine code the port runs
constantly.

### Explicitly NOT worth a lane

`curl` / `jpeg` / `holmes` / `zlib` / Bink: **43 functions, 25,704 bytes, 1.6 %
of remaining.** The port does not run them. They stay in the denominator (some
name genuine unwritten work, and hiding them is the failure this project keeps
finding in its own tooling) but nothing should be staffed against them.

---

## 8. The queries

Ship these, not the tables above.

```bash
# The whole frontier, all sections, with denominators and drop reasons.
python3 scripts/analysis/frontier.py --db /home/free/code/milohax/dc3-decomp/decomp.db

# Units one function from complete -- lane 1.
python3 scripts/analysis/frontier.py --section near-complete --max-remaining 1 \
        --db /home/free/code/milohax/dc3-decomp/decomp.db

# Certificate population by class and band -- lanes 2 and 6.
python3 scripts/analysis/frontier.py --section certs \
        --db /home/free/code/milohax/dc3-decomp/decomp.db

# The unwritten tier -- lane 4.
python3 scripts/analysis/frontier.py --section stubs \
        --db /home/free/code/milohax/dc3-decomp/decomp.db

# Machine-readable join of report.json x decomp.db, one row per authorable fn.
python3 scripts/analysis/frontier.py --json \
        --db /home/free/code/milohax/dc3-decomp/decomp.db > /tmp/frontier.json
```

```sql
-- sqlite3 /home/free/code/milohax/dc3-decomp/decomp.db
-- Lane 3: never-adjudicated work in range. NOT reachable from verdict='AT_LIMIT'.
SELECT unit, symbol, match_percent_normalized, size
FROM functions
WHERE excluded = 0 AND verdict IS NULL
  AND match_percent_normalized > 0 AND match_percent_normalized < 100
ORDER BY match_percent_normalized DESC;

-- Lane 2: certificates one or two instructions from perfect.
SELECT unit, symbol, match_percent_normalized, floor_certificate, size
FROM functions
WHERE excluded = 0 AND verdict = 'AT_LIMIT'
  AND match_percent_normalized >= 99.9 AND match_percent_normalized < 100
ORDER BY match_percent_normalized DESC;

-- The AT_LIMIT rows that are AT_LIMIT by assertion (no certificate).
SELECT COUNT(*), SUM(size) FROM functions
WHERE excluded = 0 AND verdict = 'AT_LIMIT'
  AND floor_certificate IS NULL AND match_percent_normalized < 100;   -- 517

-- ... of which these have no body at all. An AT_LIMIT here is a bookkeeping
-- reset, not a floor. Never count these as certified.
SELECT COUNT(*), SUM(size) FROM functions
WHERE excluded = 0 AND verdict = 'AT_LIMIT'
  AND floor_certificate IS NULL AND match_percent_normalized = 0;     -- 348

-- Blind cert samples (§6). Reproduce the draw, or re-draw with a new seed --
-- the point is that the sample is not chosen by the person defending the certs.
SELECT unit, symbol, match_percent_normalized, size FROM functions
WHERE excluded = 0 AND verdict = 'AT_LIMIT'
  AND floor_certificate = 'equivalent' AND match_percent_normalized < 100
ORDER BY unit, symbol;                       -- 726 rows; sample with a fixed seed
```

```bash
# Lane 8: substantial-but-WRONG bodies. --include-divergences is MANDATORY;
# without it the JSON says divergence_count=76 and hands you an empty array.
python3 scripts/analysis/fake_impl_scan.py --project "$PWD" --workers 8 \
        --include-divergences --out /tmp/fake_impl.json

# Lane 9: mechanical encoding patterns. --limit 0 is the default and must stay
# that way -- the old --limit 200 sorted DESC before slicing and could see 1 of
# the 69 current hits.
python3 scripts/analysis/batch_pattern_scan.py --min 90 --max 99.9 --limit 0 --json

# Are the scanners still agreeing with themselves? Read the NOT CHECKED line too.
python3 scripts/analysis/determinism_check.py
```

```python
# The stub tier, from report.json -- the DB's is_stub flag misses 401 of these.
import json
d = json.load(open("build/373307D9/report.json"))
stubs = [(u["name"], f["name"], int(f["size"]))
         for u in d["units"] if not u["name"].startswith(
             ("default/xdk/", "default/lib/binkxenon/"))
         for f in (u.get("functions") or [])
         if f.get("fuzzy_match_percent") is None]      # 759 rows, 112,332 B
```

---

## 9. What this document deliberately does not claim

- **It does not claim the remaining 2,687 are all reachable.** Some fraction is
  a genuine compiler-backend floor. What it claims is that *nobody currently
  knows which fraction*, because the tool that was supposed to answer that
  question clamps three quarters of its own answers (§5.2).
- **It does not claim any specific certificate is wrong** outside §6's audited
  samples. It claims 517 of them were never issued.
- **A displayed 100.0 is not byte identity.** 395 authorable functions
  (150,108 bytes) counted as matched here have permuted registers, and a wrong
  callee costs zero normalized points until `../objdiff` `b14ba45` is built.
  Say "matched modulo register permutation and relocation names".
