# Remaining Work — How To Find It

**This document contains no worklist. That is deliberate.**

Every hardcoded list of targets this project has produced rotted within weeks:
`BATCH_TARGETS.md` (2026-02-25), `LOW_HANGING_FRUIT.md` (2026-02-27),
`STUB_BURNDOWN.md` (2026-02-21), `STUB_ROADMAP.md`, `DIVERGENCE_BURNDOWN.md`,
`TO_100_TRACKING.md`, `GAP_ANALYSIS.md` — all of them named specific functions
and specific percentages, all of them were being cited months after their
contents stopped being true, and all of them are now in
[`../archive/2026-08-17-doc-audit/`](../archive/2026-08-17-doc-audit/MANIFEST.md).
One of them ("Wave 24", 2026-06) was measured as *actively misleading* three
months after it was written.

**Standing rule: ship the query, not the answer.** If you are tempted to paste a
table of function names into a document, put the query that produced it there
instead. If a future reader needs the table, they can run it and get a true one.

For where the project actually stands, see
[`../STATE_OF_THE_DECOMP.md`](../STATE_OF_THE_DECOMP.md).

---

## Before anything: re-measure

`decomp.db` is a **work-selection index, not a measurement**. The ninja DB sync
deliberately does not write `current_percent`, so it drifts continuously.

```
mcp__orchestrator__run_objdiff(symbol="...", project_dir="/abs/path/to/your/worktree")
```

**Always pass `project_dir` when you are in a worktree.** Omitting it silently
measures the main repo, and your edits appear to have done nothing.

The rule: query the DB to *choose* a target, run `run_objdiff` to *know* where it
is. Never quote a `current_percent` as a match percentage.

**`ninja <single>.obj` does not run the obj patchers, so it measures an
UNPATCHED object.** The patcher stamps (`anon_ns_patched`, `guard_patched`,
`atexit_scope_patched`, `bool_mangle_patched`, `dynamic_init_patched`) are
separate ninja targets; a targeted single-object build leaves the fresh `.obj`
without them and the row can read roughly half a point low — `?Init@JoypadClient@@AAAXXZ`
measured **99.10 %** that way, *worse* than the 99.68 % it started at, while a
full `ninja` put it at **100.0 %**, because the un-run anonymous-namespace hash
patcher left our `?A0xf41ae7e0` against the target's `?A0x831dd776`. A
single-object build is fine for "did it compile"; **always take the number from
a full `ninja`**, and never conclude a fix regressed from one.

### Columns you cannot trust

| Column | Problem |
|---|---|
| `current_percent` | Not written by the ninja sync; drifts from the moment it is set. |
| `verdict` | `COMPLETE` is wrong on **875 rows** that are not 100 % in a fresh report (540 in the 90–99.99 band, 335 at 0 %). `AT_LIMIT` is a prior, not a proof — a 2026-08-04 blind audit of regswap AT_LIMIT certificates scored **3/10**, and 640 AT_LIMIT rows carry a real-bug unicorn divergence class. |
| `verdict_reason` | Free text, stale, inconsistently populated. |
| `has_prologue_mismatch` | **Identically 0 for every row.** It measures nothing. |
| `reachable_100`, `primary_pattern`, `priority_score`, `ease_score`, `fan_in` | Products of a 2026-02 scoring experiment that was never re-run. Present, unmaintained. |

Columns that *are* reliable: `symbol`, `demangled`, `unit`, `size`, `excluded`,
`is_stub`, `attempt_count`, `unicorn_verdict`, `unicorn_class`,
`unicorn_confidence`.

**DB hygiene — specifically the 875 rotted COMPLETE certificates — is a lane
already in flight (coordinator task #101). Do not repair the database from a
decomp task.**

See [`../reference/DATABASE_SCHEMA.md`](../reference/DATABASE_SCHEMA.md) for the
full schema.

---

## The canonical queries

`decomp.db` lives at the repo root. Every query must carry `excluded = 0` — that
filter is what removes XDK and vendor code that has no source here.

### 1. Unverdicted — nobody has looked at these

The cheapest triage in the project: 277 rows, 109,676 bytes, zero adjudication.

```sql
SELECT symbol, demangled, unit, size
FROM functions
WHERE excluded = 0 AND verdict IS NULL
ORDER BY size DESC;
```

Via the orchestrator, which also filters ICF placeholders and boilerplate
thunks:

```
mcp__orchestrator__query_functions(status="workable", skip_boilerplate=True, limit=50)
```

### 2. Stubs — bodies we never wrote

494 functions, 117,920 bytes. `is_stub` is trustworthy.

```sql
-- by file, biggest first
SELECT unit, COUNT(*) AS stubs, SUM(size) AS bytes
FROM functions
WHERE excluded = 0 AND is_stub = 1
GROUP BY unit ORDER BY stubs DESC LIMIT 20;

-- the individual stubs in one unit
SELECT symbol, demangled, size
FROM functions
WHERE excluded = 0 AND is_stub = 1 AND unit = 'default/system/synth_xbox/Synth'
ORDER BY size DESC;
```

```
mcp__orchestrator__query_functions(is_stub=True, unit_pattern="*/synth_xbox/*", limit=50)
```

### 3. Unicorn DIVERGENT by class (the real-bug oracle)

Unicorn executes both the target and our object under emulation and compares
behaviour. It is the best bug oracle the project has after the match-percentage
plateau, because it fires on functions that are *wrong* independently of whether
they are *unmatched*.

Split the classes before you use them:

| Real bugs (work on these) | Unfixable artefacts (ignore) |
|---|---|
| `logic`, `call_count`, `call_arg`, `return_value`, `object_memory`, `error`, `wild_jump_match`, `cap_exhausted`, `cap_exhausted_decomp` | `build_env`, `regalloc`, `merged_call`, `merged_arg`, `stack_layout`, `fpr_precision`, `orig_error`, `cap_exhausted_orig` |

```sql
-- the real-bug population: 1,145 fns / 650,972 bytes DB-wide
SELECT symbol, demangled, unit, size, unicorn_class, unicorn_confidence
FROM functions
WHERE excluded = 0
  AND unicorn_verdict = 'DIVERGENT'
  AND unicorn_class IN ('logic','call_count','call_arg','return_value',
                        'object_memory','error','wild_jump_match',
                        'cap_exhausted','cap_exhausted_decomp')
ORDER BY size DESC;

-- class histogram, to see what the mix currently is
SELECT unicorn_class, COUNT(*), SUM(size)
FROM functions
WHERE excluded = 0 AND unicorn_verdict = 'DIVERGENT'
GROUP BY 1 ORDER BY 2 DESC;
```

```
mcp__orchestrator__query_functions(unicorn_verdict="DIVERGENT", unicorn_class="call_arg",
                                   unicorn_confidence="high", limit=30)
```

Prefer `unicorn_confidence='high'` (every probe run agreed) or
`'stable_divergent'`. `'input_sensitive'` means the runs disagreed and the
signal is weaker.

**The most valuable slice**: rows that are DIVERGENT with a real class *and*
already certified AT_LIMIT — 640 functions / 466,036 bytes, 41 % of all AT_LIMIT
bytes. A certificate on a function that demonstrably misbehaves is a certificate
worth re-opening.

```sql
SELECT symbol, demangled, unit, size, unicorn_class
FROM functions
WHERE excluded = 0 AND verdict = 'AT_LIMIT' AND unicorn_verdict = 'DIVERGENT'
  AND unicorn_class IN ('logic','call_count','call_arg','return_value',
                        'object_memory','error','wild_jump_match',
                        'cap_exhausted','cap_exhausted_decomp')
ORDER BY size DESC;
```

### 4. The near-miss band

Functions close enough that one structural change may finish them. **The DB
cannot answer this question** — `current_percent` is stale. Read the band out of
the fresh report and join to the DB only for metadata:

```bash
ninja   # report must be fresh
```

```python
# scripts/ has `authorable.py`; run from the repo (or worktree) root
import json, sqlite3, sys
sys.path.insert(0, "scripts")
from authorable import is_authorable

report = json.load(open("build/373307D9/report.json"))
db = {r[0]: r[1:] for r in sqlite3.connect("decomp.db").execute(
    "SELECT symbol, unit, verdict, unicorn_class FROM functions WHERE excluded = 0")}

rows = []
for unit in report["units"]:
    if not is_authorable(unit["name"]):
        continue
    for fn in unit.get("functions") or []:
        pct = fn.get("match_percent_normalized")
        pct = fn.get("fuzzy_match_percent", 0.0) if pct is None else pct
        if 90.0 <= pct < 100.0:
            rows.append((pct, int(fn.get("size") or 0), unit["name"], fn["name"],
                         db.get(fn["name"], ("", "", ""))))
for r in sorted(rows, reverse=True)[:40]:
    print(r)
```

Two shapes hide in this band and want different treatment:

- rows the DB still calls **COMPLETE** (540 of them, 143,888 bytes) — a stale
  certificate over a genuine near-miss;
- rows the DB calls **AT_LIMIT** — check the unicorn class before believing it.

### 5. `name_check` adjudication

Since 2026-08 the report is built with `functionRelocDiffs=name_check`, which
charges a relocation whose *target symbol name* differs even when the
instruction bytes are identical. This exposed **272 functions / 66,828 bytes
gated only by symbol-name mismatches** — 96 adjudicated as real wrong-symbol
bugs, 8 as missing data-fold aliases, 115 still unadjudicated.

This work is not codegen. It is: *which symbol should this relocation point at?*
Often the answer is an ICF alias we are missing, or an anonymous-namespace hash,
or a local-static guard variable.

The instrument is the **shipped linker map**, not the symbol map:

```bash
# orig/373307D9/ham_xbox_r.map — 118,000 names, one line per symbol.
# Every member of an ICF fold set prints the SAME address, so folds are stated
# outright. scripts/target_symbol_map.json is a VA->name function over an
# already-folded link and silently omits every spelling that lost a fold vote.
grep ' <mangled-name>' orig/373307D9/ham_xbox_r.map
```

**Start with the adjudication, not with the alias file.** A name charge has two
explanations that demand opposite actions — a legitimate `/OPT:ICF` fold (install
an alias) or a wrong callee in our source (fix the source) — and getting it wrong
in the fold direction is fail-open: an alias does not close a gap, it stops the
gap from ever being measured again.
[`../analysis/2026-08-17-comdat-fold-adjudication.md`](../analysis/2026-08-17-comdat-fold-adjudication.md)
adjudicates the whole population with three instruments and finds that the
bucket the map could not settle **is not a bucket of hidden folds**: of 119 rows
/ 44,760 B, 3 rows / 504 B are provable folds and 108 rows / 42,024 B are refuted
— 94 % of the bytes were real source differences the charges were reporting
faithfully. It also lists every refuted pair by sub-class with an owner, which is
where the remaining work in this section lives.

Prior work and method: `docs/analysis/namecheck-residency-split-20260812/`,
`docs/analysis/namecheck-source-lanes-20260812.md`,
`docs/analysis/anon-namespace-hash-lane-20260812/`,
`docs/analysis/dc3-data-comdat-fold-20260812/`.

### 6. Unit-level sweeps

```sql
-- units with the most remaining rows, ignoring vendor and asm units
SELECT unit, COUNT(*) AS remaining, SUM(size) AS bytes
FROM functions
WHERE excluded = 0
  AND (verdict IS NULL OR verdict != 'COMPLETE')
  AND unit NOT LIKE '%/lib/%'
  AND unit NOT LIKE '%asm/%'
GROUP BY unit ORDER BY bytes DESC LIMIT 30;
```

```
mcp__orchestrator__query_functions(unit_pattern="src/system/rndobj/*", status="workable")
```

When a whole unit looks untouched, `batch-check` (skill) runs objdiff over every
function in it and auto-reports the 100 % ones, which is far cheaper than a
manual query/diff/report loop.

---

## Triage routing

Once you have a target and a *measured* percentage, the shape of the diff
decides the tool. Run `run_diff_inspect(mode="diagnose")` first; it is usually
right about which of these you are in.

| Signal | Route | Why |
|---|---|---|
| 90–99.9 %, small residual, no offset or constant errors | **permuter** (`permute` skill / decomp-synth) | Beam search over behaviour-neutral source transforms. Wins concentrate in structural diffs in the 90–95 band; the ≥99 % near-miss band is largely exhausted in this repo. |
| Function exists at 100 % in a sibling decomp tree (`../rb3`, `../og-dc3-decomp`) | **reference port** | Port the source verbatim, then re-guard. See [`UPSTREAM_PORT_WORKFLOW.md`](UPSTREAM_PORT_WORKFLOW.md). `lookup_rb3` finds candidates. og-ports arrive with `HX_NATIVE` guards stripped — restore them. |
| 0 %, or `is_stub = 1`, no sibling reference | **asm archaeology** | Reconstruct from the target assembly (Ghidra + m2c + `recon`). This is how `synth_xbox`/`rnddx9` get built. Slow, but it is the only route there. |
| Zero instruction residual, relocation-name charge only | **`name_check` adjudication** | Section 5 above. Not a codegen problem. Adjudicate it *before* routing: [`../analysis/2026-08-17-comdat-fold-adjudication.md`](../analysis/2026-08-17-comdat-fold-adjudication.md) classifies the whole population and the REFUTED classes are **routed work, not noise** — `LOCAL_STATIC_SCOPE_SKEW` (26 rows / 19,052 B, the prize), `STRING_LITERAL` (21 / 6,516, probably one build-config root cause), `STORAGE_CLASS_SKEW` (10 / 2,564). Installing an alias for any of them would hide a real source difference permanently. |
| Same `off:+N` shift across several functions in one unit | **struct field order** | A header has fields in the wrong order. Fix the header, re-measure the whole TU — and read the header-inlining warning below. |
| Unicorn DIVERGENT, real class, any percentage | **behavioural fix** | Fix the logic. The percentage may not move at all; the bug still gets fixed. |
| Diff is a pure register permutation with no other residual | **probably a floor** | Plain two-term same-register commutative swaps are a backend floor. Do not permute them. Check `patterns/INDEX.md` before spending time. |

**Header edits are TU-wide events.** Any fix that touches a header changes MSVC's
inlining decisions across every translation unit that includes it, and the blast
radius is invisible to inspection. Measure the whole affected set, do not reason
about it. See
[`TECHNICAL_NOTES.md`](TECHNICAL_NOTES.md#header-edits-shift-inlining-tu-wide-2026-03-10-salvaged-2026-08-17).

---

## The metric-invisible work class

Some of the most valuable work in this repo **does not move the match
percentage at all**, and a worklist sorted by percentage will never surface it:

- **Stub reconstruction in `synth_xbox` and `rnddx9`** — 271 stubs / 76,236
  bytes with no sibling reference. Each one is a real missing behaviour; a 3 %
  match on a reconstructed 400-byte function is a huge functional gain and a
  rounding error in the headline.
- **Unicorn-DIVERGENT logic fixes** — the function was already counted as
  matched-or-certified; fixing it changes runtime behaviour and nothing else.
- **Bugs the native port exposes.** Several of the worst mis-decompilations
  found in this project were metric-invisible by construction: `ObjectDir::Iterate`
  compiled to a no-op, so every shipped DTA `iterate` body ran zero times;
  `DataNode::Equal`'s string comparison mis-decompiled; a `qsort` with a
  hardcoded 8-byte `DataNode` stride corrupted every sort under LP64. None of
  these showed up as a percentage.
- **`name_check` alias gaps** — the code is already byte-correct.

Budget for this work explicitly. If the only thing being optimised is the
headline, none of it ever gets done.

---

## See also

- [`../STATE_OF_THE_DECOMP.md`](../STATE_OF_THE_DECOMP.md) — the numbers and the ruler
- [`patterns/INDEX.md`](patterns/INDEX.md) — fixable/unfixable catalogue (read its **Corrections** section first)
- [`../tools/REFERENCE.md`](../tools/REFERENCE.md) — scripts, symbol lookup, measurement
- [`../tools/WORKFLOW.md`](../tools/WORKFLOW.md) — workflow narratives and `diff_inspect` reference
- [`../reference/DATABASE_SCHEMA.md`](../reference/DATABASE_SCHEMA.md) — `decomp.db` schema
- [`../archive/2026-08-17-doc-audit/MANIFEST.md`](../archive/2026-08-17-doc-audit/MANIFEST.md) — the worklists this document replaces, and what went stale in each
