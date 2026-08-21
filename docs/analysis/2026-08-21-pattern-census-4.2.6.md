# Whole-binary pattern census under objdiff 4.2.6 — dc3-decomp, 2026-08-21

**Repo: dc3-decomp (title `373307D9`).** `bin/objdiff-cli` is a symlink shared with `../rb3`
and `../rb3-xenon`; every number here is dc3's. Task #127, branch
`fix/pattern-census-20260821` off `2f666acc8`, measured in
`/home/free/code/milohax/wt/pattern-census` after a full `ninja` and
`verify_objs_patched.py --verify-manifest` (989 objects, `tree_sha256=031cc652c8285391`).

    objdiff-cli 4.2.6 (bf7405e3fe07, xxh3 af689f2c8bb3be9b)

This supersedes [2026-08-19-reloc-pattern-flag-triage.md](2026-08-19-reloc-pattern-flag-triage.md).
That document's *findings* mostly survive; its *populations* do not, because they were
counting a predicate objdiff 4.2.6 has since split apart.

---

## 1. The five classes, with denominators

**Universe: 48,325 distinct function names in `build/373307D9/report.json`** (48,344 entries;
19 names appear in two units and are collapsed). **Examined: 48,290.** The 35 drops are all
`objdiff-not_found` and are counted as drops, never as zeros.

This is *not* the universe the old backfill used. It scanned `WHERE excluded = 0` — 31,446 of
52,568 DB rows — so **16,922 functions that `report.json` scores right now were never looked
at**. Every previously published pattern count has that hole in its denominator.

| pattern | `name_check` (graded) | `all` (charge-everything) | `none` (blind) | fixability |
|---|---:|---:|---:|---|
| `LINKER_MERGED` | **4** | 99 | 0 | RarelyHandFixable |
| `WRONG_CALLEE` | **127** | 303 | 0 | **LikelyFixable** |
| `TEMPLATE_INSTANTIATION_MISMATCH` | **18** | 397 | 0 | **LikelyFixable** |
| `REGISTER_SAVE_HELPER_MISMATCH` | **219** | 219 | 0 | RarelyHandFixable |
| `UNVERIFIABLE_CALLEE_NAME` | **0** | 34 | 0 | RarelyHandFixable |

*Denominator for every cell: 48,290 examined functions. `name_check` is the ruler
`objdiff.json` sets and `report.json` uses; it is the canonical column.*

`name_check` is a strict subset of `all` in all five classes (verified by set containment, not
by count). The difference is the ~2,992 `/OPT:ICF` folds already adjudicated in
`build/373307D9/icf_aliases.map`, so the `all` column's excess is the project's own
already-proven artifact population: **58.1 %** of `WRONG_CALLEE` and **95.5 %** of
`TEMPLATE_INSTANTIATION_MISMATCH` under `all` are folds `name_check` forgives.

`UNVERIFIABLE_CALLEE_NAME = 0` under `name_check` is a **structural** zero, not an empty
bucket: the graded ruler already exempts placeholder target names (`fn_<hex>`, `lbl_`,
`vftable_`) before the detector runs, so the class can only ever fire under `all`. It should
never be expected to carry rows on the canonical ruler.

Its 34 rows under `all` are still worth a look, though not as decomp work. They are dominated
by a handful of *unnamed target addresses standing where we call a CRT function* —
`fn_829A2760` ← `sprintf` (several callers), `fn_829A1AD0` ← `_snprintf`. Those are almost
certainly `sprintf`/`_snprintf` in the target, unnamed in `config/373307D9/symbols.txt`. Naming
them would either resolve the rows outright or reclassify them as real `WRONG_CALLEE`; either
answer is better than a placeholder. That is a **symbols.txt** task, not a source task.

### The full 4.2.6 vocabulary, both rulers

| pattern | `name_check` | `all` | `none` |
|---|---:|---:|---:|
| ADDRESS_RELOCATION_NOISE | 826 | 11863 | 565 |
| BOOL_MASK | 1160 | 1160 | 1160 |
| REGISTER_SWAP | 970 | 970 | 970 |
| OFFSET_SWAP | 402 | 402 | 402 |
| CONTROL_FLOW | 363 | 363 | 363 |
| REGISTER_SAVE_HELPER_MISMATCH | 219 | 219 | **0** |
| PROLOGUE_MISMATCH | 219 | 219 | **0** |
| COMMUTATIVE_OP_ORDER | 198 | 198 | 198 |
| WRONG_CALLEE | 127 | 303 | **0** |
| ANONYMOUS_NAMESPACE_HASH | 23 | 68 | 7 |
| MAKESTRING_TEMPLATE_MISMATCH | 19 | 81 | **0** |
| TEMPLATE_INSTANTIATION_MISMATCH | 18 | 397 | **0** |
| COMPARISON_STYLE | 13 | 13 | 13 |
| STATIC_GUARD_COUNTER | 13 | 13 | 4 |
| SIGNEDNESS_MISMATCH | 8 | 8 | 8 |
| DEAD_STORE_ELIMINATION | 7 | 7 | 7 |
| BOOLEAN_NEGATION / FSEL_TERNARY | 5 / 5 | 5 / 5 | 5 / 5 |
| LINKER_MERGED | 4 | 99 | **0** |
| FLOAT_PRECISION_MISMATCH | 2 | 2 | 2 |
| SCOPE_COUNTER_MISMATCH | 1 | 2 | **0** |
| FLOAT_TO_INT_TO_FLOAT | 1 | 1 | 1 |
| ALLOCA_MISMATCH / DYNAMIC_CAST_MISMATCH | 0 / 0 | 0 / 0 | 0 / 0 |

`MAKESTRING_TEMPLATE_MISMATCH` rose from a settled **63** to **81** under `all` exactly as
predicted: 4.2.6 removed a circular suppression in which the MakeString call site was itself
what made `LINKER_MERGED` fire, and `LINKER_MERGED` was what downgraded the MakeString finding
to an artifact. The finding suppressed itself.

---

## 2. What the old 1310 / 680 / 1500 actually were

| column | old value | now | what the old number was |
|---|---:|---:|---|
| `has_linker_merged` | 1310 | **4** | Three defects at once. (a) `detect_linker_merged` counted *any* `bl` naming a different symbol, so 98 % of it was not a fold — it was the four classes now split out. (b) It was measured under `all`, which charges the adjudicated folds. (c) It was measured on a tree three worktrees were rebuilding, which is why it reproduced as 1,051 on settled trees. Under the graded ruler and the corrected detector the real evidence-bearing fold population is **4 functions**. |
| `has_address_relocation` | 680 | **827** | A `functionRelocDiffs=none` number. `sync_objdiff.py` writes this column from the blind ruler, which sees 565 of the 826 the graded ruler charges. The 680 was neither: it is a stale mix of an old blind pass over the `excluded = 0` subset. |
| `detected_patterns` non-empty | 1500 | 1500 (unchanged, now labelled) | Also a `none` number, over the `excluded = 0` subset. It is not wrong so much as **incomplete by construction**: ten of 4.2.6's 25 detectors cannot fire under that ruler, so an empty or short `detected_patterns` is not evidence that nothing else is wrong. Left in place and documented; `function_patterns` carries the reloc-visible view. |

The four LINKER_MERGED survivors under `name_check`, for the record — all four were already
adjudicated **UNDECIDABLE** by the 2026-08-17 COMDAT-fold lane:
`?Copy@FxSend@@`, `??0Synapse@0DSP@@`, `?Draw@BinkMovieImpl@@`, and `MetagameRank`'s
`vector<Unlockable*>` copy ctor.

---

## 3. `REGISTER_SAVE_HELPER_MISMATCH` vs `PROLOGUE_MISMATCH` — exactly co-extensive

**Yes. Set equality, whole binary, under both rulers.**

```
name_check   RSH=219  PRO=219  identical=True  RSH-only=0  PRO-only=0
all          RSH=219  PRO=219  identical=True  RSH-only=0  PRO-only=0
```

This is structural, not luck, and the source says why:

* `detect_prologue_mismatch` scans the **first 10 instructions** for a `bl` whose two sides
  match `__(savegprlr|savefpr)_(\d+)` with different `N`.
* `detect_callee_divergences` scans **all** instructions and classifies via
  `is_regalloc_save_helper_name`, which accepts ten stems (`savegprlr_`, `restgprlr_`,
  `savefpr_`, `restgpr_`, `savevmx_`, …) with one or two leading underscores.

RSH is the strictly wider predicate, so `PRO ⊆ RSH` by construction; the measurement says the
containment is also an equality, i.e. **this binary has no save/restore-helper divergence
outside a prologue** and no stem-only disagreement (`savegprlr` vs `savefpr` at the same `N`).

**One of them is redundant and `has_prologue_mismatch` is the one kept**, because it is the one
with a documented meaning ([fixable-liveness.md](../decomp/patterns/fixable-liveness.md):
a liveness tell, not floor evidence), a live consumer in `sync_objdiff`'s
`PRACTICALLY_UNFIXABLE` set, and a richer payload (it reports both frame sizes and both first
registers; RSH reports only the two names). `REGISTER_SAVE_HELPER_MISMATCH` is **not** given a
column — it lives in `function_patterns`, where carrying it costs nothing and where the
equality above can be re-checked at any time with one query.

Note this equality holds *for this binary at this commit* and is now cheap to re-verify:

```sql
SELECT pattern, COUNT(*) FROM v_function_patterns
WHERE ruler='name_check'
  AND pattern IN ('PROLOGUE_MISMATCH','REGISTER_SAVE_HELPER_MISMATCH')
GROUP BY pattern;
```

---

## 4. Schema decision: rows with a ruler, not four more booleans

Four new `has_*` columns was the obvious move. It is the move that produced the graveyard.

**A boolean column cannot distinguish three different states, and all three read `0`:**

1. measured, and this function does not have the pattern;
2. the ruler in force could not have seen the pattern;
3. never measured at all.

That is the whole mechanism behind the six dead columns, and it is *still live*: `sync_objdiff`
runs under `none` and writes nine reloc-sensitive `has_*` columns from it, so the 2026-08-19
backfill established them and the next scheduled sync silently zeroed them again. The columns
were never dead — **they were being overwritten on a schedule.**

Schema **v17** therefore adds three tables and exactly **one** scalar column:

```sql
pattern_scans(id, ruler NOT NULL, tool_version NOT NULL, project_dir, build_rev,
              tree_verified, universe, examined, coverage_json, patterns_checked, …)
pattern_scan_examined(scan_id, function_id)          -- membership: "we looked at this row"
function_patterns(scan_id, function_id, pattern, confidence, fixability,
                  instruction_count, details)         -- details carries divergent_callees
functions.pattern_flags_scan_id                       -- provenance of the legacy has_* values
```

plus views `v_latest_pattern_scan` and `v_function_patterns` (the latter carries `ruler` in
every row, so a result set cannot be quoted without it).

The three states are now three different queries: no `pattern_scans` row for a ruler → never
measured; scan exists but no `pattern_scan_examined` row → dropped, and `coverage_json` says
why; examined with no `function_patterns` row → genuinely did not fire.

`patterns_checked` is stored per scan — the detector *vocabulary* of the binary that ran. This
is what makes the 4.2.5 → 4.2.6 rename survivable: an old scan's silence about `WRONG_CALLEE`
is now visibly "that binary did not check for it", not a negative finding.

`ruler` is `NOT NULL` on purpose. A pattern population without its ruler is not a weaker
number, it is not a number: the same objects and the same detectors give `LINKER_MERGED` = 0,
4, or 99.

### The guard, and the control that proves it

`symbol_sweep.sweep_functions(include_patterns=True)` **raises `RelocBlindPatternError`** under
`functionRelocDiffs=none` rather than reporting its zeros. To keep that from being an
unexamined assertion, `pattern_census.py --negative-control` runs the detectors under the blind
ruler *deliberately*, labels the output as a control, and refuses `--apply`. That run is the
measurement in the third column of §1: **10 of 25 detectors report a structural zero**, and
`ANONYMOUS_NAMESPACE_HASH` (23→7), `STATIC_GUARD_COUNTER` (13→4) and
`ADDRESS_RELOCATION_NOISE` (826→565) are undercounted.

### What was retired, and what was left alone

* `sync_objdiff`'s enrichment `UPDATE` now writes only the **twelve** columns whose populations
  are byte-identical under `none` and `name_check`. The nine reloc-sensitive ones are owned by
  `pattern_census.py`.
* `has_prologue_mismatch` kept; `REGISTER_SAVE_HELPER_MISMATCH` deliberately given no column.
* The legacy reloc-sensitive booleans are **refreshed from the graded scan and stamped** with
  `pattern_flags_scan_id`. Rows the scan did not examine are left alone, not zeroed — 4,278
  rows remain `NULL`-stamped and their `has_*` values must not be read as measurements.
* `has_assert_revs` / `has_ltcg_pooling` remain dropped.

---

## 5. The LikelyFixable worklist

**145 findings across 143 functions** (`WRONG_CALLEE` 127, `TEMPLATE_INSTANTIATION_MISMATCH`
18; two functions carry both), ruler `name_check`.

### Ranking

"Recoverable bytes" needs a discriminator, because a wrong callee on an otherwise-perfect
function pays that function's whole size and the same bug at 84 % pays nothing. The
discriminator is the **blind-vs-graded gap**: run the same objects under `none`, where every
relocation name is forgiven. `blind fuzzy == 100` ⇒ the name is the *only* defect.

> The old `match_percent_normalized >= 100.0` test no longer works for this. Since the
> 2026-08-20 objdiff fix a vetted relocation-name disagreement stays in `diff_score` and *does*
> reach the normalized score under `name_check`, so an otherwise-perfect row now reads 99.9-
> something. A test written against the old behaviour reports the prize slice as empty.

### Tier 3 first, because a naive rank puts it on top

**60 of the 143 rows have a splitter placeholder as their ENCLOSING symbol** (`fn_82E4DE20`,
…), 40–44 B each, all at norm 100. objdiff pairs MSVC EH funclets **by byte signature**, so the
counterpart routinely belongs to a different parent function and the differing `bl` is a
pairing artifact, not a call.

This is a **gap in the new detector**: `classify_callee_divergence` checks whether the *callee*
is a placeholder (→ `UNVERIFIABLE_CALLEE_NAME`) but never whether the *enclosing symbol* is
one. Sorted naively these 60 rows head the worklist with 2,140 "recoverable" bytes. Worth
reporting upstream.

### Tier 1 — the name is the only defect (6 functions, 3,880 B)

| norm | size | class | owner | symbol | target ← base |
|---:|---:|---|---|---|---|
| 99.987 | 3108 | WRONG_CALLEE | wrong-callee lane | `?Poll@SaveLoadManager@@QAAXXZ` | `?GetNumRotFeatures@SkeletonPCAFeatureConvert…` ← `?GetGlobalOptionsSize@ProfileMgr@@QAAHXZ` |
| 99.928 | 276 | WRONG_CALLEE | wrong-callee lane | `??0SampleInst360@@QAA@PAVSynthSample360@@_NHH@Z` | `?DrawHighlightMat@RndShaderMgr@@…` ← `?GetData@SynthSample360@@QBAPBXXZ` |
| 99.894 | 188 | TEMPLATE | **unowned** | `??$__introsort_loop@PAUCuePoint@?A0x81ddebd1@@…` | `??$__median@PAVAllocInfo@@…` ← `??$__median@UCuePoint@?A0x81ddebd1@@…` |
| 99.889 | 180 | WRONG_CALLEE | wrong-callee lane | `?CheckHueConverge@NgPostProc@@IAAXXZ` | `?IsLocal@LocalUser@@UBA_NXZ` ← `?DoHueConverge@RndPostProc@@QBA_NXZ` |
| 99.828 | 116 | TEMPLATE | **unowned** | `?push_back@?$vector@ULabel@?A0x81ddebd1@@…` | `??$_Copy_Construct@U?$pair@$$CBVString@@I@…` ← `??$_Copy_Construct@ULabel@?A0x81ddebd1@@…` |
| 0.000 | 12 | WRONG_CALLEE | **unowned** | `?SyncProperty@LockedContentPanel@@$4PPPPPPPM@A@…` | `?SyncProperty@LetterboxPanel@@UAA…` ← `?SyncProperty@LockedContentPanel@@UAA…` |

⚠ **Check the instrument on the first two.** `?GetNumRotFeatures@SkeletonPCAFeatureConvert…`
vs `?GetGlobalOptionsSize@ProfileMgr@@` appears on *both* `SaveLoadManager::Poll` and
`SetState`, and `?DrawHighlightMat@RndShaderMgr@@` vs `?GetData@SynthSample360@@` has the same
smell. Three of the loudest findings in the last relocation-name sweep were **config defects,
not source bugs** (one symbol 0xA0 off its map address; twelve `RndShader` globals named one
slot early). Run `scripts/analysis/reloc_name_gate.py` against `ham_xbox_r.map` before editing
either.

The last row is a virtual adjustor thunk (`$4PPPPPPPM@A@`), 12 bytes, at norm 0.000 — a
genuine wrong-callee finding, but a thunk/ICF question rather than a source edit.

### Tier 2 — real, does not cross the row (77 functions)

Top by size, all `unowned` unless marked. These are calls to a *different function* — a class
neither the pre-4.2.4 normalized score nor the unicorn oracle can see — on rows that have
structural mismatches too:

| norm | size | class | symbol | target ← base |
|---:|---:|---|---|---|
| 91.930 | 3700 | WRONG_CALLEE | `??0GameEndedDataPointJob@@QAA@…` | `??0String@@QAA@PBD@Z` ← `??YString@@QAAAAV0@PBD@Z` |
| 88.685 | 5072 | TEMPLATE+WRONG_CALLEE | `?UpdateOverlay@MoveDir@@UAAMPAVRndOverlay@@M@Z` | `?ClosestMoveFrame@MoveDir@@…` ← `?CurrentMoveMode@@YA?AW4MoveMode@@XZ` |
| 93.667 | 2164 | WRONG_CALLEE (lane) | `?SetParameter@EQEffect@@QAAXHM@Z` | `_blkmov` ← `memcpy` |
| 11.859 | 1956 | WRONG_CALLEE | `?Select@NgEnviron@@UAAXPBVVector3@@@Z` | `??0Matrix4@Hmx@@QAA@ABVTransform@@@Z` ← `?Select@RndEnviron@@UAAXPBVVector3@@@Z` |
| 72.642 | 1620 | TEMPLATE | `??0Synapse@0DSP@@QAA@M@Z` | `?erase@?$vector@I…` ← `?_M_fill_insert@?$vector@M…` |
| 74.308 | 1168 | WRONG_CALLEE | `?StartVoiceThreadEntry@@YAKPAX@Z` | `GetTickCount` ← `?Enter@CriticalSection@@QAAXXZ` |
| 77.227 | 1164 | WRONG_CALLEE | `?OpenFiles@HDCache@@AAAXH@Z` | `FileDelete` ← `?NewFile@@YAPAVFile@@PBDH@Z` |
| 72.385 | 1132 | WRONG_CALLEE | `?JsonToDta@?A0x8a9ffbf2@@…` | `??0Symbol@@QAA@PBD@Z` ← `??0DataNode@@QAA@ABVDataArrayPtr@@@Z` |
| 81.351 | 924 | WRONG_CALLEE | `?RecursePatternInternal@@YAXPBDP6AX00@Z_N2@Z` | `??0String@@QAA@PBD@Z` ← `?substr@String@@QBA?AV1@II@Z` |
| 84.108 | 816 | WRONG_CALLEE | `?Alloc@MemTracker@@QAAXHHPBDPAXC_NE0H@Z` | `??2AllocInfo@@SAPAXI@Z` ← `??0String@@QAA@XZ` |

Full ranked list: `/tmp/worklist.json`, or regenerate from the DB.

### Slice handed to the concurrent wrong-callee lane

⚠ **The stated boundary does not close.** That lane is described as owning the **≥ 95 %** slice
"starting with `?Set@PlayBack@CharLipSync@@…`", but that seed measures **94.421 normalized /
93.158 graded-fuzzy / 93.16 `current_percent`** here — it is ≥ 95 on *no* ruler available in
this tree. Rather than guess, the band was opened downward to `norm ≥ 93.0` **plus the seed
named explicitly**, and every row is *labelled*, never dropped. That yields **19 rows** marked
`wrong-callee-lane`; the remaining **124** are unowned. Someone should reconcile the boundary
with that lane before either side starts editing.

**No source file was edited by this lane, in that slice or anywhere else.**

---

## 6. Two defects found on the way, both closed

### `sync_objdiff` was zeroing what the backfill established

Nine of the nineteen `has_*` columns it writes are reloc-sensitive. Its `UPDATE` now writes
only the twelve that are ruler-invariant.

### `auto-AT_LIMIT` can certify a fixable wrong-callee bug as unfixable

`PRACTICALLY_UNFIXABLE` reads `detected_patterns`, which under `none` **cannot contain**
`WRONG_CALLEE` or `TEMPLATE_INSTANTIATION_MISMATCH`. So a function whose blind pattern list is
exactly `{REGISTER_SWAP, ADDRESS_RELOCATION_NOISE}` can be calling an entirely different
function and still satisfy "all mismatches unfixable".

**15 rows are in that position, and 10 already hold an auto-issued certificate** — including
`?Fail@Debug@@`, `?Trigger@UITrigger@@` and `?DrawShowing@HamNavList@@`. `auto-AT_LIMIT` now
refuses to promote a row the latest `name_check` scan says carries a LikelyFixable divergence,
and falls back to prior behaviour (reporting the count it could not check) when no scan exists.
Verified with a **positive control**: a symbol outside the fixable set still promotes, so the
gate is not simply refusing everything.

**Not acted on, deliberately:** across the whole LikelyFixable set, **67 of 143 functions
already carry an `AT_LIMIT` verdict** and 60 carry `COMPLETE`. Re-adjudicating existing
certificates is a verdict change on 67 rows and belongs to whoever owns the AT_LIMIT backlog,
not to a census. The rows are listed by the query in §7.

---

## 7. Reproduce / query

```sh
# census (read-only; ~25 s for the whole binary at -j 12)
python3 scripts/analysis/pattern_census.py --project-dir . --ruler name_check \
    --out /tmp/census-name_check.jsonl
python3 scripts/analysis/pattern_census.py --project-dir . --ruler all \
    --out /tmp/census-all.jsonl
python3 scripts/analysis/pattern_census.py --project-dir . --ruler none --no-patterns \
    --out /tmp/blind-none.jsonl
# the control that keeps the guard honest
python3 scripts/analysis/pattern_census.py --project-dir . --ruler none --negative-control \
    --out /tmp/negctl-none.jsonl

python3 scripts/analysis/pattern_worklist.py --namecheck /tmp/census-name_check.jsonl \
    --blind /tmp/blind-none.jsonl --all /tmp/census-all.jsonl --json-out /tmp/worklist.json

# record it (from a worktree, name the MAIN checkout's db explicitly)
python3 scripts/analysis/pattern_census.py --project-dir . --ruler name_check \
    --db /home/free/code/milohax/dc3-decomp/decomp.db --apply
```

```sql
-- the five classes, with the ruler in every row
SELECT ruler, pattern, COUNT(*), SUM(size) FROM v_function_patterns GROUP BY 1,2;

-- the LikelyFixable worklist, with both callee names
SELECT match_percent_normalized, size, pattern, symbol,
       json_extract(details,'$.divergent_callees[0].target_symbol') AS target,
       json_extract(details,'$.divergent_callees[0].base_symbol')   AS base
FROM v_function_patterns
WHERE ruler='name_check' AND pattern IN ('WRONG_CALLEE','TEMPLATE_INSTANTIATION_MISMATCH')
ORDER BY match_percent_normalized DESC;

-- LikelyFixable rows already certified AT_LIMIT (67)
SELECT v.symbol, v.match_percent_normalized, v.pattern
FROM v_function_patterns v JOIN functions f ON f.id = v.function_id
WHERE v.ruler='name_check' AND f.verdict='AT_LIMIT'
  AND v.pattern IN ('WRONG_CALLEE','TEMPLATE_INSTANTIATION_MISMATCH');

-- is a has_* value trustworthy on this row?
SELECT symbol, has_linker_merged, pattern_flags_scan_id FROM functions WHERE …;
-- pattern_flags_scan_id IS NULL  ->  provenance unknown, do not read as a measurement
```

Run it on a tree that passes `python3 scripts/verify_objs_patched.py --verify-manifest`, or
finding 2 of the 2026-08-19 triage will happen to you as well. `pattern_census.py` calls
`patch_guard.ensure_patched_tree()` and exits 4 rather than answering.

## Note for `../rb3` and `../rb3-xenon`

Both share `bin/objdiff-cli` by symlink and both set `functionRelocDiffs=name_check` in their
own `objdiff.json`, so both inherited the 4.2.6 vocabulary change with no edge firing anywhere.
Any stored `LINKER_MERGED` count in either tree is now counting a predicate ~50× wider than the
one the binary implements.
