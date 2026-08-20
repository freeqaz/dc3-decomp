# Tool-gap inventory, verified and attributed — 2026-08-20

A day of intensive tooling work surfaced a long list of instrument defects. Some
were fixed; several were only written up, and — as it turns out — several
write-ups were wrong about *which repo* the defect lived in, or *whether it was
still there*, or *what it caused*.

This document re-checks every candidate **against the code as it stands on
2026-08-20**, attributes it to a repo, and says whether it is still real. Items
that turned out to be already fixed are kept and marked, deliberately: those are
the claims most likely to still be repeated in conversation, and retracting them
is the point of the exercise.

**Every measurement below names the repo it came from.** dc3-decomp, rb3 and
rb3-xenon share symbol names and address ranges, and an unattributed number gets
refuted against the wrong binary.

## Summary

| # | Gap | Repo | Still real? | Severity |
|---|-----|------|-------------|----------|
| 1 | `MAKE_STRING` / `MAKESTRING` serde spelling split | `objdiff` | **Yes → fixed on a branch** | Med |
| 2 | `detect_linker_merged` over-broad; 3 flags are 1 flag | `objdiff` | **Yes** | **High** |
| 3 | Canonical ruler blind to reloc-name divergence | `objdiff` | **No — fixed AND deployed** | (was High) |
| 4 | `check_doc_links.py` slugify blind to `--` anchors | `objdiff` | **Yes → fixed on a branch** | Low |
| 5 | Funclet `fn_<addr>` byte-signature pairing false rows | `objdiff` | **No — disclosed + carved out** | Low residual |
| 6 | Jump-table split residue (task #74) | `jeff` | **No — closed, no regression** | — |
| 7 | `dtk xex split` fixed-point rule | `jeff` | **No — holds today** | — |
| 8 | Split `.obj`s carry cross-unit globals as UNDEF externals | **`dc3`, not `jeff`** | **No — fixed 2026-08-18** | (misattributed) |
| 9 | `backfill_reloc_patterns.py` measured a moving tree | `dc3` | **Yes → FIXED** | **High** |
| 9b | `updated_at` does not date the pattern flags | `dc3` | **Yes** (new finding) | Med |
| 10 | `mode=attributed` crashes (`/FAs` exit −11) | `dc3` | **Yes → FIXED** (far worse than reported) | **High** |
| 11 | `backup-db.sh` overwrites its dated archive | `dc3` | **Yes → FIXED** | **High** |
| 12 | `ninja <single>.obj` skips the obj patchers | `dc3` | **Yes — worse: `run_objdiff` is the vector** | **High** |
| 13 | `fold_proof.py` cannot canonicalise transitively | `dc3` | **Premise yes, consequence REFUTED** | Low (misleading) |
| 14a | Unicorn ctor-returns-`this` trampoline stub | `dc3` | **Yes** (known, declined) | Med |
| 14b | `EQUIVALENT` is a weak verdict | `dc3` | **Yes, all three blind spots** | **High** |
| 14c | 411/660 completions log zero calls | `dc3` | **Reproduces; not auditable from the DB** | Med |
| 15 | `MILO_ENGINE_PATH` default broken in a worktree | `dc3` | **No — fix works; stale CMake cache** | Low |
| 16 | ~2,900 `.permuter_work_*` scratch files | `dc3` | **Yes, but mis-sized 120×** | Low disk / Med tooling |
| 16b | 75,121 `_CL_*` files are the actual 7.4 GB | `dc3` | **Yes** (new finding) | Low |
| **A** | `cargo test -p objdiff-cli` does not compile | `objdiff` | **Yes → FIXED** (new finding) | **High** |
| **B** | *(suspicion)* `report.json` predates the deployed ruler | `dc3` | **No — refuted by me, see §B** | — |

Nothing here touches `milo-native-engine`.

---

# Part 1 — `../objdiff`

`bin/objdiff-cli` is a **symlink shared by dc3-decomp, rb3 and rb3-xenon**:

```
/home/free/code/milohax/{dc3-decomp,rb3,rb3-xenon}/bin/objdiff-cli
    -> /home/free/code/milohax/objdiff/target/release/objdiff-cli
```

There is **no cargo edge** in any of the three ninja graphs, so a source change
in `../objdiff` has *zero* effect until someone runs `cargo build --release`
there by hand — and when they do, **the instrument changes for all three
projects simultaneously**, while their `report.json` and `decomp.db` still hold
numbers measured with the previous one. Every objdiff fix below is therefore
committed **as source on a branch and deliberately not built**.

Branch: `fix/tool-gap-inventory-20260820` in `/home/free/code/milohax/objdiff`
(2 commits). `target/release/objdiff-cli` is untouched — still
`4.2.4 (39144b470916, xxh3 b163150cbf5cfa90)`, mtime `2026-08-20 21:34:08`.

## §1 — The `MAKE_STRING` / `MAKESTRING` spelling split — REAL

**File:line.** `/home/free/code/milohax/objdiff/objdiff-cli/src/cmd/analysis.rs`

- `:108-110` — `#[derive(..., Serialize)]` + `#[serde(rename_all = "SCREAMING_SNAKE_CASE")]` on `PatternType`
- `:144` — variant `MakeStringTemplateMismatch`
- `:180` — `PatternType::as_str()` returns `"MAKESTRING_TEMPLATE_MISMATCH"`
- `:410` — `pub struct Pattern { pub pattern: PatternType, ... }`, serialized by derive

SCREAMING_SNAKE_CASE splits the internal capital in `String`, so
`patterns[].pattern` emits **`MAKE_STRING_TEMPLATE_MISMATCH`** while
`patterns_checked[]` (built from `as_str`) and every human-facing string say
**`MAKESTRING_TEMPLATE_MISMATCH`**. One JSON document, both spellings.

I enumerated all 21 variants: **exactly one diverges.** The other 20 agree by
coincidence of naming, not by construction — which is why this survived.

**Who is affected (measured 2026-08-20):**

| repo | consumer | effect |
|---|---|---|
| dc3-decomp | `scripts/sync_objdiff.py:246-253` and `scripts/analysis/reloc_flag_triage.py:65-67` both accept **both** spellings | unaffected — 63 rows flagged in `decomp.db` |
| rb3 | `scripts/sync_objdiff.py:188` keys the `as_str` spelling against `p["pattern"]` | **`has_makestring_mismatch` = 0 across all 41,233 rows** and could never be anything else |
| rb3-xenon | column exists (`scripts/orchestrator/database.py:452`), **no writer references the pattern name at all** | 0 of 86,675 — a separate, more basic gap |

So the claim as briefed is right for rb3 and *understates* rb3-xenon, which
doesn't even have the wiring. Confirmed all three set
`"functionRelocDiffs": "name_check"` in their `objdiff.json`, so they are on the
same ruler and the bucket would mean the same thing in each.

**Severity.** MakeString is the pattern bucket that actually contained real
wrong-callee bugs on dc3 (14 of them, e.g. `DingoJob::Start` 95.3 → 100.0).
rb3 currently cannot see into it.

**Fix (implemented, commit `38669b5`).** Replaced the derive with a hand-written
`Serialize` delegating to `as_str()`, so the JSON spelling and the printed
spelling are the same string by construction and cannot drift again for *any*
variant. Added `PatternType::ALL` plus two tests: one walks every variant
asserting `serde == as_str`, one pins the variant count at 21 so a new variant
cannot be added to the enum and quietly escape the check.

**Cost:** done. **Blast radius:** changes exactly one JSON key,
`MAKE_STRING_TEMPLATE_MISMATCH` → `MAKESTRING_TEMPLATE_MISMATCH`. Both dc3
consumers already accept both spellings, so dc3 is forward-compatible; rb3
starts working; rb3-xenon is unaffected. **Requires `cargo build --release`
in `../objdiff` to take effect, which re-instruments all three projects.**

## §2 — `detect_linker_merged` is over-broad, and three flags are one flag — REAL

**File:line.** `/home/free/code/milohax/objdiff/objdiff-cli/src/cmd/analysis.rs:2061-2143`

The function has three branches, and only the **first** actually detects a
linker fold:

- `:2081` — `MERGED_FUNC_RE.is_match(t_args)`, matching `^(merged_|OnlyReturns|\?\?_[EG].*PAXI@Z$)` (regex at `:97-98`). This is the real ICF signal: a dtk synthetic fold name.
- `:2089-2096` — same MSVC template base with different type args → `"ICF:… (template merge)"`.
- `:2098-2113` — **anything else**: two `bl`/`b` instructions whose targets are different non-empty strings, where `t_is_func`/`b_is_func` is satisfied by *starting with `?`, `_`, or any ASCII letter*. Labelled `"ICF:… (cross-function merge)"`, and the doc comment at `:2099-2101` asserts it is "likely ICF merging of unrelated functions with identical machine code."

That third branch has no ICF evidence behind it whatsoever. `bl __savegprlr_28`
vs `bl __savegprlr_29` both start with `_`, so both pass, and the pair is
reported as linker-merged. **Two `bl`s to different names is the wrong-callee
signal** — the branch is finding real bugs and labelling them
`RarelyHandFixable` (`:2139`).

**Independent corroboration of the composition, from dc3-decomp's `decomp.db`**
(I did not re-run the 1,052-row census; I checked it against a different
instrument):

```
sqlite3 'file:decomp.db?mode=ro'
  has_linker_merged = 1                                 -> 1310
  has_prologue_mismatch = 1 AND has_linker_merged = 0   ->    0
  has_makestring_mismatch = 1 AND has_linker_merged = 0 ->    0
  totals: linker_merged 1310 | prologue 221 | makestring 63
```

- **The subset claim is exact.** Both set differences are empty: every
  `PROLOGUE_MISMATCH` row and every `MAKESTRING_TEMPLATE_MISMATCH` row is also a
  `LINKER_MERGED` row. Three flags, one flag.
- `detect_prologue_mismatch` (`analysis.rs:3020+`) *is* the `__savegprlr_N` vs
  `__savegprlr_M` detector. So 221 of the merged rows are provably
  save/restore-helper noise — **221/1052 = 21.0%** against the reported 20.7%.
- `63/1052 = 6.0%` against the reported 6.3% MakeString share.

Two of the five reported buckets reproduce from an independent source. I did not
re-derive the template-spelling (37.5%) / cross-function (38.1%) / true-fold
(1.9%) split, but the mechanism above makes the shape unambiguous: **the only
branch that proves a fold is `:2081`, and it is the smallest one.**

**Severity: high.** `LINKER_MERGED` + `RarelyHandFixable` is read across this
project as "source-immune, stop working on it." That verdict is being applied to
register-allocation noise (21%) and to wrong-callee bugs, which are the most
fixable class there is.

**Fix (NOT implemented — needs design, not a patch).** Split the pattern into
three: `LINKER_MERGED` for the `:2081` branch only; a new
`SAVE_RESTORE_HELPER_MISMATCH` (or fold into the existing `PROLOGUE_MISMATCH`)
for the `__savegprlr`/`__savefpr` family; and a new `WRONG_CALLEE` /
`CALL_TARGET_MISMATCH` for `:2098-2113`, with fixability `Fixable`, not
`RarelyHandFixable`. Note `b14ba45` already established the correct carve-out
list for exactly these categories in the *scoring* path — the *pattern* path
should reuse it rather than re-derive it. **Cost:** ~half a day plus a re-census.
**Blast radius:** changes pattern names and counts in all three repos'
`decomp.db`; every "at_limit / LINKER_MERGED" certificate in dc3 should be
re-adjudicated afterwards, which is the point.

## §3 — The canonical ruler was blind to wrong callees — **ALREADY FIXED AND DEPLOYED**

This one should be retracted, not repeated.

`match_percent_normalized = diff_score - arg_diff_score`, and a failing `Reloc`
arg is non-immediate, so its penalty landed in *both* terms and cancelled to
exactly zero. That was true — and it was fixed today, upstream, before this
inventory started:

- `/home/free/code/milohax/objdiff/objdiff-core/src/diff/code.rs:1688-1761` — under `NameCheck`, a relocation-name disagreement that survives `reloc_eq`'s full exemption machinery now stays in `diff_score` only.
- commit `b14ba45` → merge `7243bdd` → release `39144b4` (v4.2.4).
- **The deployed binary is built from it**: `bin/objdiff-cli --version` reports `4.2.4 (39144b470916, …)`, and `target/release/objdiff-cli` has mtime `21:34:08`, nine seconds after the `39144b4` commit timestamp `21:33:59`.

Its own whole-binary A/B on **dc3-decomp**: 328 functions drop, 0 rise, 54 leave
the matched set (19,288 bytes), headline −0.1117 pp, with `fuzzy_match_percent`
byte-identical on all 48,344 functions as the built-in control. Of the 54, 52
carry a real charged site and 0 are charged on noise alone.

**On the open question — "should there be a separate surfaced metric for
reloc-name divergence?"** It already exists, and has since before this
inventory. `objdiff-core/src/diff/mod.rs:55-62` defines `masked_equal_rows` and
`reloc_ignored_rows` ("the #1 masking channel (wrong callee via `bl`)"), and
they reach the CLI JSON at `objdiff-cli/src/cmd/diff.rs:1294-1295, 2249-2250,
2330-2331`, alongside the new `canonical_match_percent`. So the answer is: the
class is now visible **twice** — in the score (because it is no longer folded)
and as a separate disclosed row count — without abandoning the normalized ruler.
No new metric is needed.

**What *is* still true, and is by design:** the normalized ruler still forgives
register permutation. Measured by me on **dc3-decomp**, joining
`build/373307D9/report.json` (29,496 functions at `match_percent_normalized`
= 100) against `decomp.db`'s pattern flags:

```
matched AND has_register_swap        : 286 fns,  174,344 bytes
matched AND has_linker_merged        : 713 fns,  197,904 bytes
matched AND has_makestring_mismatch  :  33 fns,   16,060 bytes
```

⚠ **The briefed figure of "395 functions / 150,108 bytes" does not reproduce.**
I get 286 / 174,344 — fewer functions, more bytes. I would not trust either
number: the join mixes a post-fix `report.json` with pattern flags produced by
the contaminated backfill run described in §9. The *class* is real and is
inherent to what "normalized" means; the specific figure should be re-derived
after §9's fix and a flag re-scan, and until then not quoted.

## §4 — `check_doc_links.py` slugify is blind to `--` anchors — REAL, FIXED

**File:line.** `/home/free/code/milohax/objdiff/scripts/check_doc_links.py:44-49` (pre-fix):

```python
return re.sub(r"\s+", "-", s)
```

`github-slugger` replaces whitespace **one character at a time**. A heading like
`## Dead Store Elimination / Destructor Merging` loses the `/` to the
punctuation strip, which leaves *two adjacent spaces*, which become *two
hyphens*. Collapsing the run with `\s+` computes the same wrong slug the URLs
were written against — **so the checker reproduced the bug it was checking for
and could not fail.** It printed `OK` while GitHub served 404s.

**Reproduced exactly the reported 4 URLs**, by recomputing both rules over the
binary's own emitted link set (`objdiff-cli doc-links -P <project> -f json`):

| repo of the doc | broken URL (missing the doubled hyphen) |
|---|---|
| dc3-decomp | `at-limit-systemic.md#2-linker_merged-icf-identical-comdat-folding` |
| dc3-decomp | `fixable-liveness.md#lever-4-scope-a-declaration-into-…-register-lever` |
| dc3-decomp | `unfixable-compiler.md#dead-store-elimination-destructor-merging` |
| rb3 | `at-limit-mwcc.md#stack-slot-inversion-offset_swap-on-r1` |

In each case the source heading contains a `/` or an em dash between two spaces.

**Fix (implemented, commit `38669b5`).** `\s+` → `\s` in `slugify`, with the
reasoning recorded in the docstring, **and** the four emitted anchor strings
corrected in `analysis.rs:1624, 1666, 1669, 1685`. Verified both directions:
against the deployed v4.2.4 the fixed checker now correctly reports **4
failures**, which is the honest current state; against a debug build carrying
the corrected strings it reports **55/55 green**.

**Cost:** done. **Blast radius:** `check_doc_links.py` is a standalone script —
zero effect on the shared binary. The four anchor strings *are* in the binary,
so they only take effect on `cargo build --release`. Closes project task **#90**.

## §5 — Funclet `fn_<addr>` byte-signature pairing — LARGELY FIXED

The mechanism is real and still present — `pair_funclets_by_bytes`, at
`/home/free/code/milohax/objdiff/objdiff-core/src/diff/mod.rs:816-823` and
`:858-865` — and it genuinely can pair many byte-identical target funclets
many-to-one onto one base funclet. `b14ba45`'s own account documents a case
where RndRibbon's static guard was paired against RndFont's purely because their
code shape coincided.

But it is no longer a silent source of false rows, on three counts:

1. **It is disclosed.** `SymbolDiff::masked_equal_symbol` (`mod.rs:63-72`) is set on exactly these pairs, is documented as disclosure-only ("it never changes `match_percent` / `diff_score`"), deliberately over-discloses as the safe direction, and reaches the CLI JSON (`diff.rs:1296, 2251, 2332`).
2. **It is carved out of scoring.** `b14ba45` carve-out #2 exempts name charges on placeholder-named enclosing symbols precisely because "charging names there measures the pairing heuristic, not the source" — 204 of 226 static-guard sites and 89 of 213 wrong-symbol sites.
3. **dc3 already handles it downstream.** `/home/free/code/milohax/dc3-decomp/scripts/get_progress.py:7-23, 42-76` excludes `fn_<hexaddr>` funclets from "remaining" *and* warns if the paired fraction regresses.

**Verdict:** the "86 of 96 rows were this artifact" observation was about an
earlier analysis pass and does not describe today's tooling. Residual risk is
low and is now measurable rather than invisible. **No action recommended** beyond
making sure any new consumer reads `masked_equal_symbol` instead of assuming a
pair is name-based.

## §A — `cargo test -p objdiff-cli` does not compile — NEW FINDING, FIXED

Not on the candidate list; found while trying to test §1.

```
error[E0063]: missing field `canonical_match_percent` in initializer of `cmd::diff::DiffOutput`
    --> objdiff-cli/src/cmd/diff.rs:4611
```

`ae19080` added `canonical_match_percent` to `DiffOutput` at its three
*production* construction sites and missed the fourth: `make_test_output`, the
helper behind every markdown-rendering test. **v4.2.4 was cut and released on
top of that**, so 153 tests have been dark across the release. Nothing caught it
because a test binary that does not compile produces no failures — only an error
on a command nobody had to run.

Repairing it immediately exposed what it was hiding:
`test_markdown_concise_mode` asserted `Match: 90.0%` — the *fuzzy* score, i.e.
precisely the mislabelling `ae19080` set out to fix. The commit could not have
updated that assertion even had it noticed, because it broke compilation in the
same change. **The one test that would have documented the behaviour change was
silenced by the behaviour change.**

**Fix (implemented, commit `b884b45`).** Added the field with a value
deliberately *different* from the fuzzy one (94.0 vs 90.0) so the two rulers can
be told apart, and rewrote the assertion to require that the canonical number is
rendered and that the fuzzy number is not presented as "the match".
`cargo test -p objdiff-cli`: **153 passed, 0 failed** (2 of them new, from §1).

**Severity: high** — a release shipped with its own test suite un-runnable, on
the very commit that changed the meaning of the project's headline metric.
**Cost:** done. **Blast radius:** test code only; production untouched.

---

# Part 2 — `../jeff` (dtk)

Same no-cargo-edge caveat: `target/release/dtk` is the ninja dependency for
every dc3 worktree's SPLIT edge, and nothing rebuilds it for you.

**Nothing in this inventory requires a change to `jeff`.** All three candidates
resolve as closed or misattributed.

## §6 — Jump-table split residue (task #74) — CLOSED, no regression

jeff HEAD is `614331e`. The relevant work is present:
`git merge-base --is-ancestor` confirms `dde965c` (the fix), `b381932` (its
merge) and `70581ef` (extra regression tests) are all ancestors.

- Guard: `/home/free/code/milohax/jeff/src/cmd/xex.rs:693-695` excludes `jumptable_` / `except_data_` / `except_record_` as relocation targets, inside `synthesize_reloc_targeted_leaf_functions_once` (`:667`); fixed-point iterator at `:1019-1031`.
- Jump-table discovery/validation: `/home/free/code/milohax/jeff/src/analysis/cfa.rs:148, 326-356, 667-726`.
- Tests: `cargo test` (debug profile, deliberately not `--release`, to avoid relinking the shared `target/release/dtk`) → **165 passed, 0 failed**, up from 135 before the fix. The three named regression tests are at `src/cmd/xex.rs:3767, 3787, 3812`, including a negative control proving the gate did not disable the pass's real job.

Output checked against **dc3-decomp** after a forced SPLIT with dtk-at-HEAD:
`config/373307D9/symbols.txt:181929-181930` shows `jumptable_82B7291C` typed
`object`, and binary-wide `jumptable_*` typed `function` = **0** (of 185 jump
tables), `except_data_|except_record_` typed function = **0**.

## §7 — The `dtk xex split` fixed-point rule — HOLDS

Rule documented at
`/home/free/code/milohax/dc3-decomp/docs/tools/BUILD_SYSTEM.md:52-77`. Tested in
the worktree `/home/free/code/milohax/wt/toolgaps`, not the shared main repo.

The SPLIT edge's declared input is `config/373307D9/config.yml`; the other three
reach ninja by depfile:

```
$ ninja -t deps build/373307D9/config.json
build/373307D9/config.json: #deps 3, deps mtime … (VALID)
    orig/373307D9/default.xex
    config/373307D9/splits.txt
    config/373307D9/symbols.txt
```

A plain `ninja` ran 0 SPLIT edges (the reflink build dir already held a valid
split), so it did not exercise the splitter at all — a genuine SPLIT had to be
forced via the *other* depfile input, per the doc's own procedure:

```
$ touch config/373307D9/splits.txt
$ ninja build/373307D9/config.json     # ran a real dtk-at-614331e split, exit 0
```

Afterwards: `symbols.txt` sha256 `0cdde38a…cacddd` → `0cdde38a…cacddd`, size
19,098,950 unchanged, **mtime still `2020-01-01 00:00:00`**; `splits.txt` sha256
unchanged; all 8 files under `config/373307D9/` md5-identical;
`git status config/` clean. dtk never opened `symbols.txt` for writing. Two
further `ninja` runs settled to the always-run edges only
(`CHECK ICF-ALIAS MAP`, `PROGRESS`) — no self-refire.

**Fixed point confirmed empirically against jeff HEAD, 2026-08-20.** One doc
nit: `BUILD_SYSTEM.md` says "exactly one always-run edge `[1/1] PROGRESS`"; a
`CHECK ICF-ALIAS MAP` edge has since been added. Both are always-run by design;
neither is SPLIT.

## §8 — Cross-unit globals as UNDEFINED externals — **MISATTRIBUTED; the real gap is dc3's, and it was fixed 2026-08-18**

This is the item the brief got wrong, and it is worth being explicit about.

**It cannot be a jeff defect.** The split objects are not standalone artifacts —
they are *linked*. `build/373307D9/default.exe.rsp` feeds **3,191 `.obj` files**
into a real link (rule at `build.ninja:45`). If jeff emitted a per-unit `.obj`
that *defined* a global owned by another unit, every such symbol would collide
at link time. Undefined externals are the only correct COFF encoding, and jeff
pins it with tests: `test_write_coff_global_unknown_symbol_is_external`
(`/home/free/code/milohax/jeff/src/util/xex.rs:2978`) and
`test_write_coff_local_unknown_symbol_is_label`, both passing.

**The phenomenon is real** — an independent 80-unit sample over dc3-decomp's
`build/373307D9/obj/` (2,223 split objects) found **6,093 undefined data
relocations (692 unique symbols), 3,085 with nonzero image content (470
unique)** — same order as the briefed 4,977 / 2,474; the delta is sample choice.

**But dc3 already resolves them, and has since 2026-08-18.**
`/home/free/code/milohax/dc3-decomp/scripts/unicorn_runner/image.py` parses the
decompressed PE (`orig/373307D9/ham_xbox_r.exe`, present, 17,283,584 bytes) plus
`config/373307D9/symbols.txt` and serves symbol content by name;
`patcher.seed_image_globals` (`scripts/unicorn_runner/patcher.py:305-356`) seeds
any REFHI/REFLO/ADDR32 target the `.obj` leaves undefined, wired into both the
single-function and co-load paths (`builder.py:60` and `:125`).
`pytest scripts/unicorn_runner/tests/test_image.py -q` → **27 passed**.

**Action: none in jeff.** The only residual is documentation: the note at
`/home/free/code/milohax/dc3-decomp/scripts/unicorn_runner/signal_version.py:60-63`
— which is where the 4,977/2,474 figures actually come from, as a self-citation,
not an external survey — reads as though the splitter is at fault. It should say
this is correct COFF that the harness must resolve, so the next reader does not
file a jeff bug. One comment edit, zero behavioural blast radius.

---

# Part 3 — dc3-decomp's own scripts

## §9 — `backfill_reloc_patterns.py` measured a tree being rewritten under it — REAL, FIXED

**File:line (pre-fix).**
`/home/free/code/milohax/dc3-decomp/scripts/backfill_reloc_patterns.py` had
**zero** staleness guard: `main()` at `:86` went straight from `parse_args()`
(`:103`) to `sqlite3.connect()` (`:105`) to `run_batch()` (`:126`). It did not
even import `subprocess`; nothing in its 189 lines mentioned the build tree's
patch state.

The verifier it should have called already existed —
`scripts/verify_objs_patched.py`, with `--verify-manifest` (`:205`), `--repo`
(`:200`), `--quiet` (`:207`), returning **0** settled / **1** drifted / **2**
manifest absent. Only `configure.py:472` called it (`--check --emit`); **no
measurement tool called `--verify-manifest` at all.** The guard had been
*recommended* and never implemented
(`docs/analysis/2026-08-19-reloc-pattern-flag-triage.md:155-157`, action item 2
at `:424`).

**Consequence, still sitting in the DB.** dc3-decomp's `decomp.db` records
`has_linker_merged = 1310` — the contaminated number, against 1,052 from a clean
worktree and 1,069 from main's tree, with the two settled trees agreeing on
1,051.

**Fix (implemented, commit `b566a6402` in `wt/toolgaps`).** New
`require_settled_tree(project_dir, skip)` runs
`verify_objs_patched.py --repo <dir> --verify-manifest --quiet` before any DB
open or measurement and `sys.exit(4)` naming the tree, distinguishing *drifted*
(rc 1) from *never verified* (rc 2), with a loud `--skip-verify` / `--force`
escape hatch. Tested: missing-manifest tree → rc 4; synthetic drifted tree →
rc 4; `--skip-verify` on that tree → warns and proceeds; settled worktree →
guard silent, real results. The real `decomp.db` was only ever read.

**Severity: high** — it poisoned the pattern flags that §2's whole analysis rests
on, and the poisoning is invisible in the data.

**Follow-up still owed:** re-run the backfill against a settled tree and
re-derive §2's and §3's overlap figures. Until then, every `has_*` count in
dc3's `decomp.db` — including the 1310, the 221 and the 63 I quote in §2 —
should be read as approximate.

## §9b — `updated_at` does not date the pattern flags — NEW FINDING, REAL

Found while trying to date §9's damage. `scripts/sync_match_percent.py` bumps
`updated_at = CURRENT_TIMESTAMP` at six sites (`:466, 474, 482, 490, 498, 506`)
but writes **no `has_*` flag at all** — its only three `has_` occurrences
(`:318, 323, 395`) are a local variable, `has_norm_col`, about a schema column.

So the flags and their apparent timestamp have different authors. dc3's
`decomp.db` currently shows all 1,310 `has_linker_merged` rows with
`updated_at` in a one-second window at **10:01:20–10:01:21** — which is when the
percent sync ran, not when the flags were written (the contaminated backfill was
at 09:11:03–09:11:36).

**Severity: medium.** It silently defeats exactly the forensic method §9 was
diagnosed with: the timestamps that proved the contamination have since been
overwritten by an unrelated writer, so the same argument can no longer be made
from the DB as it stands.

**Fix (not implemented):** give the pattern flags their own
`patterns_updated_at` column, written only by the flag writer. One migration,
~5 lines in `backfill_reloc_patterns.py` / `sync_objdiff.py`. ~1 h.

## §10 — `run_diff_inspect mode=attributed` crashes — REAL, FIXED, and much worse than reported

Reproduced on **dc3-decomp**, but the briefed diagnosis ("`/FAs` exits −11 on
`LoopVizCallback::UpdateOverlay`") is wrong on both counts: it is not `/FAs`, and
it is not that function.

Running the tool's own reconstructed command by hand:

| command | exit |
|---|---|
| `build/tools/wibo` + cl.exe + `/FAs`, GamePanel.cpp | **139 (SIGSEGV)** |
| same, **no listing flag at all** | **139** |
| same `/FAs` command on Rand.cpp, Flare.cpp | **139, 139** |
| `/home/free/code/milohax/wibo/build/release/wibo`, identical `/FAs` command | **0** (771 KB listing) |

**Root cause:** `/home/free/code/milohax/dc3-decomp/tools/compiler_trace/invoker.py:13`
hardcoded `WIBO = PROJECT_ROOT / "build" / "tools" / "wibo"` — a legacy
`download_tool.py` artifact dated **2025-05-28** that segfaults on *every*
translation unit. The build itself uses a different binary
(`configure.py:174` defaults `config.wrapper` to `../wibo/build/release/wibo`,
dated 2026-08-05), which is what every rule in `build.ninja` names.

So `mode=attributed` was **dead binary-wide**, not for one function — confirmed
on an unrelated symbol, `?CalcScale@RndFlare@@IAAXXZ`, same exit −11. It was
mis-triaged as a per-TU compiler quirk because the one function anyone happened
to try it on was the one in the ticket. The exit code *was* checked correctly
(`scripts/analysis/diff_inspect.py:993-996`, `scripts/orchestrator/mcp_server.py:3155-3157`);
the message was accurate and simply misread.

**Fix (implemented, commit `598f6a964`, branch `fix/invoker-wibo-path-20260820`,
worktree `/home/free/code/milohax/wt/wibofix`).** `_resolve_wibo()` replaces the
constant: `$DC3_WIBO` → `<repo>/../wibo/build/release/wibo` → `<main checkout>/../wibo/…`
resolved via `git rev-parse --path-format=absolute --git-common-dir` (needed
because inside a worktree `<repo>/..` is the `wt/` pool — the same trap as §15)
→ legacy path last. Verified: both `?CalcScale@RndFlare@@IAAXXZ` and
`?UpdateOverlay@LoopVizCallback@@UAAMPAVRndOverlay@@M@Z` now return full reports
at exit 0.

**Severity: high** — an entire MCP analysis mode silently unusable. **Cost:**
paid, one file. **Blast radius:** tooling only, no `src/`. Project task **#91**
should be reworded from "compiler segfaults on this TU" to "stale wibo killed
`attributed` everywhere" before closing.

Two smaller unfixed items noted in passing: `attributed` rejects demangled names
(`cannot resolve source file for symbol`), and `mode=asm_listing` builds the
wrong `.obj` path (unit name instead of `src/…`).

## §11 — `backup-db.sh` overwrites its dated archive — REAL, FIXED

**File:line (pre-fix).** `/home/free/code/milohax/dc3-decomp/scripts/backup-db.sh:34-36`

```sh
date="$(date +%Y-%m-%d)"
base="$(basename "$db_path")"
backup_file="$archive_dir/$base.$date.xz"
```

Date-only, no time component, no collision check — and `:39-41` did not prevent
the clobber, it *announced* it:
`echo "Overwriting today's existing backup: $backup_file"`.

Disk confirms it. `~/code/db-backups/` holds exactly **one dated pair per day**,
plus a hand-made `decomp.db.2026-08-19.pre-unicorn-reingest.xz` at the *identical
byte size* (22,510,160) as `decomp.db.2026-08-19.xz` — someone copied the
archive aside to survive the next run. That workaround is documented in two
places (`docs/analysis/2026-08-19-unicorn-reingest.md:257-261`,
`docs/analysis/unicorn-full-resweep-20260819.md:175-178`), and three further
`decomp.db.pre-task120-*-20260819T*` copies show other tools already hand-rolling
second-resolution stamps.

**Severity: high**, because of what it interacts with: the standing "back up
first" rule means the *second* risky operation of a day destroys the undo the
*first* one created.

**Fix (implemented, commit `ac30e4074` in `wt/toolgaps`).** Stamp is now
`%Y-%m-%dT%H%M%S`, name is `<db>.<stamp>[.<DB_BACKUP_LABEL>].xz`, and an
existing path is **refused** (`return 1`) rather than overwritten.
`DB_BACKUP_LABEL` gives the pre-ingest naming a supported spelling. Unchanged:
the sqlite3 `.backup` consistent snapshot, the out-of-repo default directory,
`DB_BACKUP_DIR`, positional single-DB mode. Tested against a throwaway DB in a
temp archive dir: three runs seconds apart → three distinct archives, `xz -dc`
round-trips, and with a frozen-clock `date` stub the second run exits 1 and the
first archive survives.

## §12 — `ninja <single>.obj` skips the obj patchers — REAL, and the vector is `run_objdiff` itself

The claim is right and understated. The problem is not an agent typing
`ninja Foo.obj` by hand; it is **every `mcp__orchestrator__run_objdiff` call
that follows a source edit.**

**Where the patchers live.** `/home/free/code/milohax/dc3-decomp/configure.py:399-477`
defines a `post-compile` chain of six `run_script` edges — `create_data_stubs.py`,
`obj_anon_ns_patcher.py`, `obj_dynamic_init_patcher.py`, `obj_guard_patcher.py`,
`obj_bool_mangle_patcher.py`, `obj_atexit_scope_patcher.py` — then
`scripts/verify_objs_patched.py --check --emit`.

**They are separate downstream edges, not part of the `.obj` rule.** The compile
edge has no patch dependency (`build.ninja:14422-14424`), and the patch stamps
sit downstream taking the `all_source` phony as an implicit input
(`build.ninja:29215-29249`, phony at `:32451`). Ninja builds only a named
target's *ancestors*, so naming the `.obj` stops exactly one edge short of every
patcher — and the fresh compile **overwrites the previously-patched bytes**.
This is already documented verbatim at
`/home/free/code/milohax/dc3-decomp/scripts/verify_objs_patched.py:37-40`.

**Why `run_objdiff` triggers it.** `scripts/orchestrator/mcp_server.py:2367-2371`
passes `--build` unconditionally, and `--build` without `--full-build` is
implemented in `/home/free/code/milohax/objdiff/objdiff-cli/src/cmd/diff.rs:838-880`
as **`ninja <base_obj_path>`** — a single-object target.
`grep -rn "verify_objs_patched\|patch_state" scripts/orchestrator/` → **0 hits**:
no measurement path checks the guard.

**Measured** in `wt/toolgaps` on `default/lazer/game/BustAMovePanel`:

| state | obj sha256 | `--check` | unit `matched_code_%` | `?SetUpMoveNames@BustAMovePanel@@AAAXXZ` |
|---|---|---|---|---|
| baseline (full-ninja tree) | `3069aa33` | exit 0 | **44.18098** | **100.0** |
| after `ninja …/BustAMovePanel.obj` | `f7ba6649` | **FAILS** | **43.69392** | **99.868** |
| after plain full `ninja` | `4637a9e9` | exit 0 | **44.18098** | **100.0** |

**Δ = −0.487 pp** of `matched_code_percent` from unpatching one object — the
briefed "about 0.5 pp" is accurate to three digits. `matched_functions_percent`
fell 1.22 pp and one function flipped out of COMPLETE. The specific losses were
the anon-namespace hash `08878e05 -> c73cd9f6`, one static guard's storage class
(3→2), and 13 `??__F…` atexit scope-counter renames.

**The nuance that manufactured the inter-lane contradiction.** The drop shows on
the *report* ruler (`report.json`, and therefore `measure_progress.sh` and
`query_functions.current_percent`) but **not** on `run_objdiff`'s canonical
`name_check` headline — `SetUpMoveNames` reads `100.0% canonical` in *both*
states. So the gap silently desynchronises the two rulers: `run_objdiff` says
100.0, the report says 99.868, and neither is obviously wrong.

Both halves of the reported incident reproduce. A `run_objdiff` call printed
`Building incremental: …BustAMovePanel.obj` while the sha256 did not change —
ninja had nothing to do (the patchers preserve mtime by design,
`scripts/obj_patch_io.py`), so **the tool announced a build it did not perform
and read patched bytes**. Conversely, `touch`ing the `.cpp` and then making one
ordinary `run_objdiff` call left `verify_objs_patched.py --check` at exit 1 while
the call reported a clean `Complete (High)` verdict — degrading the tree for the
next reader.

**Severity: high.** The entire decomp inner loop reads unpatched objects after
an edit and leaves the tree unpatched behind it. The bias is one-directional —
it measures **low**, so it manufactures phantom regressions and phantom bugs,
never phantom wins.

**Fix (not implemented — one small change, but it is on the hot path of every
lane, so it wants a deliberate landing).** In `mcp_server.py::_run_objdiff` and
the sibling `run_diff_inspect` / `run_analyze_function` paths, drop `--build` in
favour of an explicit `ninja post-compile` in `project_dir` before diffing, then
assert `verify_objs_patched.py --verify-manifest`. `post-compile` reaches the
object through `all_source`, so the specific `.obj` is still compiled first.
Measured cost:

```
single-obj ninja after touching one .cpp  ->  2.3s   (leaves tree UNPATCHED)
ninja post-compile (proposed)             ->  9.7s   -> "989 objects verified patched"
ninja post-compile, nothing changed       ->  0.0s   "ninja: no work to do."
verify_objs_patched.py --verify-manifest  ->  0.1s
```

**+7.4 s per measurement that follows an edit, +0 s otherwise.** Blast radius:
the three orchestrator measurement tools. No change to `configure.py`, none to
the build graph, none to `../objdiff`.

Rejected alternatives: a *wrapper script* leaves `run_objdiff` — where the damage
happens — untouched; a *ninja phony that always patches* already exists
(`post-compile`), the problem is that objdiff-cli names the `.obj`; *folding the
patchers into the `msvc` rule* costs 5 Python startups per TU (~15 min on a cold
980-object build) and `configure.py:391-394` documents that a patcher perturbing
the object's mtime creates a non-terminating recompile/repatch oscillation;
*guard-only* refusal already exists and costs 0.1 s but only reports the broken
tree, leaving the agent stuck — worth adding **in addition**, as the assertion,
not instead.

## §13 — `fold_proof.py` transitive canonicalisation — **PREMISE TRUE, CONSEQUENCE REFUTED**

This is the other item to stop repeating.

**The code fact is exactly as reported.**
`/home/free/code/milohax/dc3-decomp/scripts/analysis/fold_proof.py:248-252`,
`canonicalise()`, is one dict lookup — `canon.get(tn, tn)` — with no chasing, no
union-find, no fixed point. `load_map()` (`:217-223`) and
`load_equivalences()` (`:242-244`) each build a flat table, and `_same_mod()`
(`:392-393`) is called with one table or the other, **never with a merged one**
(`:400/410` vs `:401/411`) — a second closure gap the original report did not
even name.

**But it explains zero rows, including zero of the five.** Both tables are
*already flat* (`canon[canon[x]] == canon[x]` for all x): map 13,463 entries,
0 needing more than one hop; alias 8,719 entries, 0 needing more than one hop;
2,062 alias groups with 0 names in more than one group. The alias installer
closes them upstream — the group records literally carry
`"evidence": "ICF fold class (transitive closure over witnessed pairs)"`.

Re-running `prove_pair` over **all 6,657 memberships** three ways gives byte-identical verdicts:

| table | PROVEN_FOLD | PROVEN_MOD_MAP | REFUTED | UNDECIDABLE |
|---|--:|--:|--:|--:|
| as shipped (single hop, separate tables) | 2790 | 512 | 12 | 3343 |
| fixed-point-closed tables (= union-find) | 2790 | 512 | 12 | 3343 |
| merged union-find over map ∪ alias | 2790 | 512 | 12 | 3343 |

**0 verdict changes**, even though the merged union-find genuinely merges (3,069
classes over 13,550 names).

**And the blocked rows are blocked by something else.** It is **2 rows, not 3** —
`??_DAppLabel` / `??_DHamLabel` now proves `PROVEN_FOLD` (108 B, 8 relocations,
byte- and reloc-identical), so
`docs/analysis/2026-08-19-refuted-fold-memberships.md:57-64` is one commit stale.
The remaining two (`??_GFitnessCalorieSort*`, `??_GHamNavProvider`/`??_GAppNavProvider`)
differ on inner destructor names that are **simply absent from
`ham_xbox_r.map`** — there is no A~B, B~C chain to close. The honest reading is
that retail folded the inner destructors and *our two bodies differ*
(`??1HamNavProvider` is 240 B / 10 relocs; `??1AppNavProvider` is a 28 B / 5
reloc forwarder that `bl`s it). That is a source question, not a tooling one.

**Recommendation: do NOT implement union-find.** Beyond measuring zero, it would
be **fail-open**: 1,393 names appear at more than one address in
`ham_xbox_r.map` (`??3@YAXPAX@Z` at three, plus ~1,300 `__unwind$*`), and
unioning those distinct definitions into one class would *manufacture* aliases —
the exact failure the module docstring (`:49-66`) exists to prevent. If anyone
wants the residual robustness anyway, the correct shape is a fixed-point loop
plus one merged table for `_same_mod`, keyed **per address bucket only**; ~15
lines, ~30 min, measured value on today's corpus = 0 rows.

**Severity of believing the claim: low blast radius, actively misleading.** It
sends a lane to write code that changes nothing, and it labels 2 rows "tool
limitation" when the evidence points at our own destructor bodies.

## §14 — Unicorn harness residue — ALL THREE SUB-CLAIMS REAL

### §14a — trampoline stubs break the ctor-returns-`this` contract

Real, unfixed — but already known and **deliberately declined**
(`docs/analysis/2026-08-19-unicorn-reingest.md`, "Defect 10 … evaluated, not
worth it").

`scripts/unicorn_runner/memory_map.py:35-36`:

```python
# Trampoline stub: li r3, 0; blr
TRAMPOLINE_STUB = bytes([0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20])
```

Written into every slot at `scripts/unicorn_runner/engine.py:483` (and `:101`
for vtable slots). `patcher.py:59-70` applies it to **every** REL24/REL14 target
with one exception, the `__savegprlr_N` family fixed in `4e4562478`. There is no
constructor predicate anywhere in `scripts/unicorn_runner/`. `comparator.py:366`
even acknowledges the problem in a comment — *"a ctor really does return `this`
in r3"* — while the emulator hands back 0.

The briefed "1 surviving row" checks out: exactly **1**
`unmapped_access_mismatch` row in 29,408 (`?FaceCenter@@YAXPAVRndMesh@@…`,
94.74%, `default/system/rndobj/Mesh`).

**But the row count understates it.** `r3 = 0` from a ctor makes the caller
null-deref; both sides do it identically, and `comparator.py:296-309` converts a
matching error at a matching PC into EQUIVALENT. So the artifact mostly produces
**vacuous passes, not visible rows** — measured on the frontier, **233 of 870
EQUIVALENT rows (26.8%) rest on the matching-error rule**. That is also §14b(iii)
by another route.

**Fix:** a second stub — bare `blr`, r3 untouched — for targets whose mangled
name starts `??0`, using the mechanism `save_helpers.py` already established.
~10 lines plus a regression test, ~1–2 h with a re-sweep. Worth doing as a
*vacuity* fix for the 233-row channel, not for the single visible row.

### §14b — `EQUIVALENT` is a weak verdict

Real on all three blind spots. **There is no committed sabotage test** — the
"3 of 6 survived" result exists only as prose in
`docs/analysis/2026-08-19-toolchain-audit.md` §E; grep for `sabotage`/`mutant`
across `scripts/` finds only a comment. Nothing re-runs it, so it will rot.

**(i) Call targets are compared, then discarded.** `comparator.py:73-108`
(`compare_call_logs`) compares **length + r3/r4/r5/r6 only**; `CL_TRAMP_ADDR` is
in the tuple (`call_log.py:26-32`) and never read. `check_call_targets`
(`:190-207`) *does* compare symbol names — but is called at `:438`, **after the
verdict is already EQUIVALENT**, and returns `warnings`, which
`batch_to_db.py:167` and `apply_refresh.py:147` then drop on write:

```sql
SELECT unicorn_verdict, COUNT(*), SUM(unicorn_reason IS NULL)
FROM functions WHERE unicorn_verdict IS NOT NULL GROUP BY 1;
-- DIVERGENT  |12419|    0
-- EQUIVALENT |16989|16989
```

An independent re-sweep of the frontier capturing the discarded warnings found
**33 EQUIVALENT rows carrying a wrong-callee warning, 23 of them not explained
by a known linker fold** — including `?ForeachKeyframe@RndPropAnim@@` calling
`DataArray::Node() const` where the target calls the **non-const** `Node()`, and
`_M_range_initialize<wchar_t>` calling `wmemcpy` where the target calls
`memcpy`.

**(ii) The fixture really is one repeated byte.** `engine.py:366-372`:
`self._fill_cache[key] = bytes([key]) * REGION_SIZE`, and the DB-producing
schedule (`scripts/unicorn/refresh_frontier.py:76-80`) is zero-fill then
0xCD-fill and nothing else. So `confidence="high"` means *two uniform fixtures
agreed*, `obj->a == obj->b` for every persisted verdict, and field-swap bugs
(the `RndFlare::Load` class) are structurally undetectable. A typed fixture
exists (`typed_fixture.py:204-269`) but is reachable only from the
single-function CLI, not from `batch_to_db` or `refresh_frontier`.

**(iii) Symmetric faults** — `comparator.py:296-309`, quantified above at 233/870.

**Severity: high.** `EQUIVALENT` is being consumed as "behaviourally proven". It
actually means: *no difference in r3/f1, in r3–r6 at each logged call, or in the
object/global regions, under one uniform-byte fixture, possibly because both
sides crashed at the same PC.*

**Fix, cheapest first:** persist `warnings` (or promote `check_call_targets` to
`DIVERGENT/call_target` when `has_icf_folded_callsites()` is false) — ~20 lines
plus a DB column, and it converts 23 already-computed, currently-discarded
findings into rows. Then commit the six sabotages as a test (~2–4 h). Wiring the
typed fixture into `_SCHEDULE` is a bigger job (~1 day plus a full re-sweep).

### §14c — "411 of 660 completions log zero calls"

**Reproduces, and is not auditable from the DB.** No writer persists call counts:
`compare()` puts `call_count` in the EQUIVALENT details
(`comparator.py:442-449`) and `batch_to_db.py:167` / `apply_refresh.py:147`
persist only `reason`, which is absent for EQUIVALENT (see the query above —
16,989 of 16,989 null). The `unicorn_refresh` schema
(`refresh_frontier.py:253-272`) has no call-count column either.

Re-derived by re-running the harness over the identical scope and probe
(1,781 fns / 442 units):

```
EQUIVALENT 870 · DIVERGENT 909 · SKIPPED 2
EQUIVALENT resting on the matching-error rule:          233 (26.8%)
EQUIVALENT that actually completed:                     637
  of which log ZERO calls on both sides:                398 (62.5%)
```

**398/637 = 62.5%** against the documented **411/660 = 62.3%** — the absolute
numbers moved because the frontier moved, but the ratio reproduces to 0.2 pp.

**Severity: medium.** The phenomenon is honest and already documented, so it
causes no *new* wrong conclusion. The provenance hole does: two documents
headline a number no query can reproduce, and anyone spot-checking it from the
DB gets an empty result — which, by this project's own standing lesson, reads as
"not a problem."

**Fix:** add `unicorn_call_count` (and ideally `unicorn_completion` =
clean / matching-error / cap) to `functions` and thread `details["call_count"]`
through the two writers. One migration, ~6 lines, plus a re-sweep to backfill.
~1 h — and it gives §14b its filter: `EQUIVALENT AND call_count = 0` is
"proved nothing".

## §15 — `MILO_ENGINE_PATH` in a worktree — **NOT REAL; the later lane hit a stale cache**

The fix from `07fdaeea7` is present and **works**.
`/home/free/code/milohax/dc3-decomp/native/CMakeLists.txt:229-244`: line `:229`
alone would give `/home/free/code/milohax/wt/milo-native-engine` inside a
worktree (`CMAKE_SOURCE_DIR` is `<wt>/native`, so `../..` is the `wt/` pool),
and `:231-240` is exactly the fallback for that — `git rev-parse
--path-format=absolute --git-common-dir` → `/home/free/code/milohax/dc3-decomp/.git`
→ parent → parent → `/home/free/code/milohax`. Local git is 2.55.0, so
`--path-format` is supported.

Tested from a genuinely fresh worktree created with `scripts/setup_worktree.sh`,
configuring with **no** `-DMILO_ENGINE_PATH`:

```
-- Configuring done (5.6s) / -- Generating done (0.3s)      CMAKE_EXIT=0
CMakeCache.txt:300  MILO_ENGINE_PATH:PATH=/home/free/code/milohax/milo-native-engine
```

**Why the later lane still needed `-D`:** `MILO_ENGINE_PATH` is `CACHE PATH`
**without `FORCE`** — correctly, so an explicit `-D` override keeps working —
which means a build directory whose cache already holds the old bad value never
picks up the corrected default. Proven by poisoning a cache and reconfiguring
without `-D`: the bad value survives. Any lane reusing a build dir configured
before `07fdaeea7` (2026-08-19 09:04) sees the old path forever.
`scripts/setup_worktree.sh` does not symlink the engine; the CMake fallback is
the entire mechanism.

**Severity: low. No code fix needed.** Remedy is `rm -rf <wt>/native/build`.
Worth one line of documentation next to `CLAUDE.md`'s existing note about the
deliberate absence of `FORCE`, spelling out the stale-cache consequence.

Unrelated observation from the same test: `native/CMakeLists.txt:109`
`find_package(Dawn REQUIRED)` does not auto-find; `-DDawn_DIR=…` must be passed,
as the main repo's own cache shows it is.

## §16 — `.permuter_work_*` scratch files — REAL but mis-sized by 120×; NOT deleted

**Counts, main repo `/home/free/code/milohax/dc3-decomp`:**

```
find src -name '.permuter_work_*' | wc -l   -> 2917   (all regular files)
du -sch (those 2917)                        -> 61M
du -sh src                                  -> 7.4G
```

**The "they contribute to a 7.4 GB `src/`" framing is wrong by two orders of
magnitude.** They are 61 MB — 0.8% of `src/`.

All 2,917 are gitignored (`.gitignore:36:.permuter_work_*`, 2917/2917 confirmed
via `git check-ignore`) and `git status --porcelain` is empty.

**Reference sweep — nothing reads them.** Zero references to `permuter_work`
anywhere in dc3 outside `src/` itself (the only `permuter_` hits are
`permuter_cache`, a DB, and the `permuter_exhausted` certificate string in
`scripts/certify_floor.py`). `../decomp-synth` is the **writer, never a reader**:
`decomp_synth/scorer.py:288,323` construct the name and `:362,381`
`unlink(missing_ok=True)` on exit; `file_util.py:48-65` treats them as ephemeral;
`reloc_audit.py:282,497` actively *excludes* paths containing `permuter_work` as
`__FILE__` pollution. No resume path, no cache path, no harvest consumer — the
2,917 on disk are orphans of killed runs.

**Not deleted, because 19 are recent.** 19 files date from 2026-08-19 06:42-06:43
(two permuter tokens), inside the briefed 2-day window. No `decomp_synth` process
is alive, so they are almost certainly orphans too — but the gate was explicit,
so nothing was removed. The age-filtered form is safe on the evidence above:

```bash
find /home/free/code/milohax/dc3-decomp/src -name '.permuter_work_*' \
     ! -newermt '2026-08-18' -delete     # 2898 files, 61M
```

**Severity: low for disk, medium for tooling correctness.** `pathlib.rglob`
matches dotfiles, so every Python scanner walking `src/**/*.cpp` ingests these
stale duplicates as real source:

```
src/system/utl:  rglob('*.cpp') -> 314 files, 243 of them .permuter_work_* (77%)
```

Affected: `scripts/scan_behavioral_idioms.py:95`,
`scripts/analysis/dta_dataflow.py:850`,
`scripts/analysis/dta_access_audit.py:711,755`,
`scripts/analysis/dta_hierarchy_scan.py:557`,
`scripts/analysis/findarray_receiver_scan.py:552`. These double-count findings
and report them against filenames that do not exist. **A `.permuter_work_`
filter should be added to those five scanners regardless of whether the files are
deleted** — that is the durable half of this item.

## §16b — the actual 7.4 GB is 75,121 `_CL_*` files — NEW FINDING

```
find src -type f -name '_CL_*' | wc -l   -> 75121
du -sch (those)                          -> 7.4G     <- essentially all of src/
```

`cl.exe` temp files (`_CL_15b5e478db` etc.), gitignored at `.gitignore:64`, all
dated **2026-04-03 … 2026-06-01** — nothing newer than ~11 weeks. Nothing
references them. This, not §16, is the reclaim target. Same evidence standard,
~120× the payoff. Not deleted here either; flagged for a decision.

## §B — *(my own suspicion, refuted)* — `report.json` predates the deployed ruler

I raised this and then disproved it, so it is recorded rather than dropped.

`build/373307D9/report.json` has mtime `10:01:20` while the deployed
`objdiff-cli` was rebuilt at `21:34:08`, which looked like every current number
predating `b14ba45`'s ruler change. It does not. `report.json`'s own provenance
block is authoritative:

```
tool_version     = 4.2.3
tool_commit      = ae19080447e5
diff_config      = ['functionRelocDiffs=name_check', … 22 keys]
```

and `git merge-base --is-ancestor b14ba45 ae19080` → **yes**. The only commits in
the deployed `39144b4` but not in `ae19080` are the merge commit `7243bdd` and
the release commit `39144b4` itself, and `git show --stat 39144b4` is
`Cargo.lock` + `Cargo.toml` only. **`report.json` is functionally current with
the deployed instrument.** No action.

(The DB's `has_*` pattern flags are a different matter — those *are* stale, per
§9 and §9b.)

---

# Fix plan, ordered

**Landed already** (branches, not pushed):

| what | where | commit |
|---|---|---|
| backfill refuses an unsettled tree | `wt/toolgaps` | `b566a6402` |
| backup-db one archive per run | `wt/toolgaps` | `ac30e4074` |
| stale-wibo fix, revives `attributed` | `wt/wibofix`, branch `fix/invoker-wibo-path-20260820` | `598f6a964` |
| objdiff test binary compiles again | `objdiff`, branch `fix/tool-gap-inventory-20260820` | `b884b45` |
| objdiff spelling split + 4 doc anchors | same branch | `38669b5` |

**Next, cheapest-first, all dc3-only and all low-risk:**

0. **Make `run_objdiff` build `post-compile`, not the bare `.obj`** (§12).
   The single highest-value fix on this list, because it corrupts the tree that
   every *other* measurement then reads, and it biases one way (low). ~10 lines
   in `mcp_server.py`, +7.4 s per post-edit measurement, +0 s otherwise.
1. **Persist the unicorn wrong-callee warnings** (§14b). ~20 lines + a column.
   Converts 23 already-computed findings into rows. Best value here.
2. **Persist `unicorn_call_count`** (§14c). ~6 lines + a migration + a re-sweep.
   Makes every EQUIVALENT row's weakness queryable.
3. **Re-run the pattern backfill against a settled tree** (§9 follow-up), then
   re-derive §2's and §3's figures. Cheap now that the guard exists.
4. **`patterns_updated_at` column** (§9b). One migration.
5. **ctor `blr` stub** (§14a). ~10 lines + a test + a re-sweep.
6. **Add a `.permuter_work_` filter to the five `rglob` scanners** (§16) — this
   is the durable half of that item, independent of any deletion.
7. Documentation nits: `signal_version.py:60-63` (§8), `BUILD_SYSTEM.md`'s
   always-run edge count (§7), `CLAUDE.md` stale-cache note (§15). Reword task
   **#91** (§10).

**Housekeeping, awaiting a decision (nothing deleted here):**

- 2,898 `.permuter_work_*` older than 2026-08-18 — 61 MB (§16).
- 75,121 `_CL_*` cl.exe temps, newest 2026-06-01 — **7.4 GB** (§16b). This is
  the one that matters.

**Requires a deliberate, coordinated `cargo build --release` in `../objdiff`:**

8. §1 and §4's binary-side strings. **This re-instruments dc3-decomp, rb3 and
   rb3-xenon at once** — the symlink is shared and none of the three has a cargo
   edge. Do it with all three repos' `report.json` re-synced in the same
   session, or the three trees disagree about what their own numbers mean.
   Closes task **#90**.

**Needs design, not a patch:**

9. **§2 — split `LINKER_MERGED` into three patterns.** The only item here that
   changes decomp *conclusions* rather than tooling hygiene: it currently stamps
   register-allocation noise and wrong-callee bugs alike as
   `RarelyHandFixable`. ~half a day plus a re-census, after which every
   `at_limit` / `LINKER_MERGED` certificate should be re-adjudicated.

**Explicitly do not do:**

10. **Union-find in `fold_proof.py`** (§13) — measures zero, and would be
   fail-open on 1,393 multiply-addressed names.
11. **Anything in `../jeff`** (§6, §7, §8) — all three candidates are closed or
    misattributed.
