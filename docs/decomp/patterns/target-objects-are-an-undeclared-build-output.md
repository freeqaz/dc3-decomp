# The target objects are an undeclared build output

**Established 2026-08-21.** Supersedes the working theory that a stale
`build/373307D9/report.cache` served pre-rename rows. It did not. The cache was
tested directly and exonerated; the real discriminator is the split.

## The shape

`report.json` is 2,223 object-pair diffs. The two sides are not equally
supervised:

| | base side | target side |
|---|---|---|
| Path | `build/<v>/src/**.obj` | `build/<v>/obj/**.obj` |
| Written by | ninja compile edges | `dtk xex split` |
| Declared as a ninja output? | yes, one edge each | **no — 0 of 2,223** |
| Asserted before measuring? | yes, `patch_guard.ensure_patched_tree()` | **nothing, until this doc** |
| Described in `report.json`'s `provenance`? | no | no |

The split's only declared output is `build/<v>/config.json`. The 2,223 target
objects are side effects.

That matters because their **content depends on `config/<v>/symbols.txt`**: dtk
writes each function under the name symbols.txt gives its address. Renaming a
symbol rewrites the COFF symbol table of every unit that mentions it. A report
taken against objects split from a different symbols.txt is a different
measurement — and a silent one, because a function whose target-side name no
longer pairs simply scores `0.0`, exactly like an unwritten function.

## The reproduction

One worktree, one objdiff-cli (`4.2.7`, `76c8da87e040`, xxh3
`182254643f310a45`), fully built and patch-verified, **report cache cold on both
runs**, identical `symbols.txt` on disk for both:

```
report started 2 s into `dtk xex split`   ->  29,497 matched functions
same command after the split finished     ->  29,838 matched functions
```

341 functions, no error, no warning. The split takes ~11.6 s; a cold report
takes ~6 s. The windows overlap, and the main repo runs several lanes at once.

A second, deterministic form of the same failure: restore `symbols.txt` with an
**older** mtime than `config.json` (`cp -a`, `tar -x`, `git checkout` of an old
tree, a reflinked worktree). Ninja then plans `REPORT` and does **not** plan
`SPLIT` — verified with `ninja -n` — so the report measures objects that
disagree with the config, forever, and nothing says so.

## What this REFUTES

The 341 gap was first read as a stale objdiff report cache. Three measurements
say otherwise, all on 4.2.7:

1. **Direct key test.** Mutate one byte of a symbol name inside a target `.obj`
   (`??_G` -> `??_E`) and re-run: `0 hits, 1 miss`. Unmutated control:
   `1 hit, 0 misses`. `ReportCache::hash_unit` hashes the whole target object's
   bytes and has since it existed.
2. **End-to-end, both directions.** Swap `symbols.txt` between `8b54d54f0` and
   `e5b1e3ce7` through `ninja` with a warm cache: `1,377 hits / 847 misses` each
   way, and the number lands on 29,497 / 29,838 correctly each way.
3. **The reproduction above is cold on both sides.** Purging the cache cannot be
   the variable when there is no cache.

Do not "fix" `objdiff-cli/src/cmd/report.rs` for this.

## The guard

`scripts/verify_split_current.py`. `--begin` / `--complete` bracket the split
inside the ninja rule and record which config produced the objects; `--check`
exits 1 on three conditions, each with its own message:

* the config inputs have drifted since the split;
* a split is recorded as **in flight** (the input hashes cannot see this — a
  re-run with an unchanged `symbols.txt` matches its own stamp the whole time it
  is rewriting);
* there is no record at all.

Wired as an `always` ninja edge gating `report.json`, `report_raw.json` and
`baseline.json`, and as `patch_guard.ensure_split_current()` for the
measurement that does not go through ninja — which, per `tools/project.py`'s own
report-cache comment, is most of it. An in-flight split is **waited out**
(bounded, 180 s), not refused: one lane's `ninja` must not be another lane's
error.

Sabotage-tested in `tests/test_split_currency.py` (10 tests, each carrying its
negative control, each red assertion pinning *which* condition fired).

## The second consumer: a pattern scan carries its own copy (2026-09-10)

The split-currency guard above answers "do the objects on disk match the config
that produced them?" — a property of the **tree right now**. A recorded
`pattern_scans` row asks a different question: "are the findings I stored still
about the objects on disk?" — a property of a **moment in the past**, which no
amount of stamp-checking on the current tree can answer.

Since schema **v18**, `pattern_scan_units` stores, per unit, a sha256 of the
target object **and** of the base object as they were when the scan ran, plus a
`raced` flag for any unit whose objects moved between the census's pre-sweep and
post-sweep reads. `callee_gate.stale_units(db, scan_id)` returns
`{unit: direction}` and `verify_pattern_scan_current.py --check` exits **4**
(target moved) or **5** (base moved), naming the units.

Two things this settles:

* **`build_rev` is not provenance for a scan.** Scan 16 was taken across a
  landing merge: it recorded `b91fc0cb5`, repaired 3 rows that were stale against
  deleted base objects, and introduced 6 new ones. On an active repo a whole-repo
  rev names *a* commit from the measurement window, never *the* tree diffed.
* **Per unit, not per tree.** A tree-wide "something moved" verdict is
  permanently red here and would be routed around inside a day; a per-unit answer
  lets a lane ask about the unit it is working on.

There is deliberately **no backfill** for scans written before v18: their
objects' hashes are not recoverable, and taking them from today's objects would
manufacture a green reading for exactly the condition the table detects.

## What it does NOT cover

* The target objects' own **bytes**, *for the split-currency guard*. That is a
  different assertion ("nobody hand-edited a target object") and has not happened
  here. (A pattern scan's *recorded* baseline does hash them — see above — but
  that vouches for one past measurement, not for the tree.)
* Any measurement taken **before** this landed. Split state and patch state
  leave no historical record, so which past number ran against which tree state
  is not reconstructible. Treat undated matched-function figures accordingly.
