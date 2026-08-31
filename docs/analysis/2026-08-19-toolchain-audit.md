# Toolchain audit — 2026-08-19

**Question asked:** does every measurement and analysis tool this project depends
on tell the truth?

**Method:** falsification, not confirmation. A tool that produces a number is
not verified until it has been shown to produce a *different, correct* number
when the input genuinely differs. Every GOOD verdict below is backed by a
perturbation whose correct answer was known in advance. Where no such control
could be built, the verdict is **NO-CONTROL**, not GOOD.

**Baseline:** worktree `audit/toolchain-20260819` from `eda64e956`; two
additional clean-build worktrees for the determinism test. `main` moved to
`2184a9641` during the audit (link-glue dedup, +0.03 pp) — noted where it
matters.

This audit exists because of a standing rule in this project's memory: *"Audit
the instrument before believing EXHAUSTED — every 'class exhausted' verdict is a
claim about the instrument."* Two of the findings below are instances of exactly
that failure mode.

---

## Verdicts

| # | Tool | Verdict | One-line evidence |
|---|---|---|---|
| 1 | Build determinism (`ninja`) | **GOOD — better than documented** | Two clean builds → `cmp`-identical `report.json`; 0 of 48,344 functions differ |
| 2a | `run_objdiff` percent formatter | **GOOD** | `ObjectDir::Save` renders `99.98%`, where `{:.1f}` gave `100.0%` over 2 real mismatches |
| 2b | `run_objdiff` `[STUB]` marker | **BROKEN → fixed** | Dead code: the SQL never selected `is_stub`, so it could never fire |
| 2c | `run_objdiff` headline rewrite | **FRAGILE → hardened** | `re.sub` failed open; a markdown format drift would silently restore the lying headline |
| 3 | `project_dir` cross-project guard | **GOOD** | `../rb3-xenon` raises `CrossProjectError` naming both title IDs |
| 4a | `query_functions.has_prologue_mismatch` | **CONFIRMED DEAD — and 6 more with it** | 7 `has_*` columns are identically 0 over all 52,547 rows |
| 4b | `query_functions.current_percent` | **MISDOCUMENTED** | Not stale — the **wrong ruler**. Tracks fuzzy to 0.013 pp; diverges from canonical by up to 7.6 pp |
| 5 | `measure_progress.sh` / `compare_progress.py` | **HALF-FIXED → fixed** | 213 "regressions" on a perturbation the canonical ruler cannot see |
| 6 | `progress_metrics.py` | **GOOD** (disclosure gap → fixed) | Reported ruler follows 4 forged provenance values |
| 7 | `check_doc_links.py` | **GOOD** | 30/30; falsified 3 ways; marker file intact |
| 8 | Single-object patcher gap | **AS DOCUMENTED** (magnitude NO-CONTROL) | Targeted build → drift detected, exit 1. The "~0.5 pp" figure did not reproduce |
| 9a | Native build (4 targets) | **GOOD** | 3301/3301 steps green; tree stays clean |
| 9b | `milo-tests` as an instrument | **GOOD** | Two production-code breaks → 8 failed of 11, exit 8 |
| 9c | `ctest` headline | **MISLEADING** | Prints "100% tests passed out of 441" while **79 skip**; 362 actually run |
| 9d | `RandSeed.NoSignExtensionPoison` | **VACUOUS** | Passes with the exact bug it was written to catch |
| 9e | `MILO_ENGINE_PIN` bump | **BROKEN** | `set(... CACHE)` without `FORCE`; `bump-engine.sh --apply` is a no-op |
| 10a | `fold_proof.py` (code path) | **GOOD** | Guard refuses zero-reloc bodies; disabling it flips a real pair to `PROVEN_FOLD` |
| 10b | `fold_proof.py --include-data` | **LYING → fixed** | Certified a fold the shipped map disproves |
| 10c | `report_absent_census.py` | **BROKEN classifier → fixed** | Class C swallowed the defect the tool exists to find |
| 10d | `ruler.py`, `scope_index_census.py`, `name_charge_census.py` | **GOOD** | Each falsified with a perturbed input |
| 10e | `coff_bodies.py` | **GOOD (fail-quiet)** | Correct on every bad input, silent on all of them |
| 11 | Unicorn runner | **GOOD, narrow** | Defect-8 verified 3 ways; **3 of 6 sabotages still read EQUIVALENT** |
| 12 | `STATE_OF_THE_DECOMP.md` | **DRIFTED → fixed** | Self-contradicting: two different headlines in one file |
| — | Worktree shadow `decomp.db` | **TRAP → warned** | 48,325 rows, **0 verdicts**; an empty result reads as "exhausted" |

---

## The findings that could cause a wrong conclusion

### A. `fold_proof --include-data` manufactured ICF evidence (LYING)

The worst finding, because of what a `PROVEN_FOLD` licenses. An alias installed
in `scripts/symbol_aliases.json` tells the `name_check` ruler "these two names
are the same symbol" forever. It does not *close* a gap — it stops the gap from
ever being **measured** again. A bad alias is strictly worse than the bug it
hides: the bug stays in the shipped code and the percentage goes up.

The code path's guard is real and load-bearing. `_identity_is_cheap()` returns
`True` unconditionally for code, so any zero-relocation body is downgraded to
`UNDECIDABLE`. This matters: 5,061 distinct symbols in this build compile to the
same `li r3,0; blr`, and without the guard the tool would manufacture ~12.8M
pairwise "proofs" from one 4-byte instruction. Monkeypatching the guard to
return `False` flips a real pair from `UNDECIDABLE` to `PROVEN_FOLD`, so it is
demonstrably not a no-op.

The **data** path only refused bodies under 8 bytes or all-zero. It never asked
whether the section was **writable** — and `/OPT:ICF` does not fold writable
COMDATs, because the program can write one copy without writing the other.

```
$ fold_proof.py --objects build/373307D9/src --include-data \
    --pair '?sX@Vector3@@1V1@A' '?sX@Vector4@@1V1@A'
[PROVEN_FOLD] byte- AND relocation-set-identical (16 B, 0 relocations)
              => /OPT:ICF must merge them
```

The shipped map places them at **0x82f0f720** and **0x82f0f750**. The linker did
not fold them. Both live in `.data` with `chars=0xc0300040`
(`IMAGE_SCN_MEM_WRITE`). 16 non-zero bytes clears both existing guards.

**Blast radius checked before fixing:** of 2,062 existing alias groups, none
contains a mutable-static spelling, so no bad alias has been installed from this
yet. `--include-data` is opt-in.

**Fixed.** Four controls, all passing:

| input | before | after |
|---|---|---|
| writable mutable statics, map says NOT folded | `PROVEN_FOLD` | `UNDECIDABLE` |
| read-only data fold, identical bodies | `PROVEN_FOLD` | `PROVEN_FOLD` (no over-refusal) |
| code fold with relocations | `PROVEN_FOLD` | `PROVEN_FOLD` (unregressed) |
| zero-reloc code stubs | `UNDECIDABLE` | `UNDECIDABLE` (guard intact) |

### B. `measure_progress --functions` reports regressions the canonical ruler cannot see

The recorded complaint — *"overstates regressions; compares fuzzy
(reloc-sensitive) not normalized; ICF atexit-thunk churn = phantom
regressions"* — is **half fixed**, and the broken half is the half CLAUDE.md
tells people to run (`--functions --detailed`).

`compare_progress.py:99` counts matched functions on
`match_percent_normalized`. But `compare_function_matches()` at line 290 still
reads `fuzzy_match_percent`, and that feeds `--functions`, `--regressions` and
the per-unit tables.

**Control.** Two `report.json` files differing *only* in `fuzzy_match_percent`,
and only on functions already at `match_percent_normalized == 100` — i.e. a
change the canonical ruler is structurally incapable of seeing:

```
Overall fuzzy: 53.99% -> 53.99% (+0.00%)
Subsystems changed: 0, +0 matched functions      <- canonical ruler: nothing happened

Regressions (213 functions, showing top 50):     <- same screen, same run
| int __cdecl roll(int)  | keygen_xbox | 97.6% | 94.6% | -3.0% |  48 |
| void __cdecl `public: struct ObjectDir::Entry * ...` | ... | 99.7% | 96.7% | -3.0% | 12 |
```

The tool contradicted itself on one screen, and the list is full of the 12-byte
thunks the pattern predicts. **Wrong conclusion it causes:** revert good work
chasing a `.text` layout shuffle.

**Fixed** by carrying the canonical ruler alongside rather than silently
swapping rulers (fuzzy is genuinely the finer signal and some callers want it):
a `Norm` column, a `~` marker for rows the canonical ruler did not move, and a
count printed before the table. Verified both directions — fuzzy-only
perturbation marks 213 of 213; a genuine regression shows `-7.0%` in `Norm` and
no phantom banner. A row is only called phantom when *both* sides carry a
normalized figure; a missing one prints `?`.

### C. `current_percent` is not stale — it is the wrong ruler

Documented as "drifts". That is imprecise in a way that matters. Measured across
30,647 comparable rows:

| compared against | exact | >0.5 pp | >1 pp | max |
|---|--:|--:|--:|--:|
| report `fuzzy_match_percent` | 29,290 / 30,647 | 0 | 0 | **0.013 pp** |
| report `match_percent_normalized` | 29,270 / 31,390 | 885 | 577 | **7.60 pp** |

`current_percent` is a faithful, current copy of the **fuzzy** percent. The
project's canonical ruler is **normalized**. Every disagreement runs one way
(DB understates).

**Wrong conclusion it causes:** `verdict='COMPLETE' AND current_percent < 100`
returns **374 rows**, all of which are at exactly 100 normalized. That is a
phantom cert-rot population. Real cert rot on the canonical ruler is **zero**.

Separately: `has_prologue_mismatch` is confirmed identically 0 across all 52,547
rows — **and so are six more**: `has_linker_merged`, `has_assert_revs`,
`has_ltcg_pooling`, `has_makestring_mismatch`, `has_dynamic_cast_mismatch`,
`has_alloca_mismatch`, `has_scope_counter_mismatch`. Note `has_linker_merged = 0`
everywhere while many rows' `verdict_reason` text literally says
`LINKER_MERGED` — the flag column and the text disagree. Filtering on any of the
seven selects everything or nothing.

### D. The `[STUB: no body emitted]` marker was dead code

One of the two fixes I was asked to verify for item 2 was inert.
`query_functions`' `SELECT` never listed `is_stub`, so `func.get("is_stub")` at
`mcp_server.py:1181` was always `None`. The `WHERE` clause *did* use the column,
so the filter worked and only the display was broken — the worst combination,
because it looks as though the marker is simply never warranted.

Six rows I explicitly filtered with `is_stub=true` printed `99.98%`, `99.95%`,
`99.9%` — for functions this build emits **no body for at all**. An agent reads
that as "a hair from done"; the truth is "unimplemented". **Fixed**; the marker
now appears on exactly the `is_stub=1` rows and no others.

### E. Unicorn's `EQUIVALENT` is much weaker than it sounds

The defect-8 fix is **genuine**, verified three independent ways: an independent
PE reader compared all 72 synthesised helper bodies against the shipped image
(**72 match, 0 mismatch**, 236 distinct helper addresses at `HELPER_BASE =
0x80040000`); a hand-written raw-Unicorn test confirmed `__savegprlr_14` writes
r14–r31 at −0x98…−0x10 with LR at −0x8, that `__savegprlr_29` against a poisoned
stack touches *only* r29–r31 (the staggered ladder is correct), that a tail
`b __restgprlr_14` ends with PC = the reloaded LR, and that **r3 survives** —
which the old `li r3,0; blr` stub destroyed; and a function that used to spin
16,616 times now completes in 8 logged calls.

But the negative control found the oracle's real limit. Six deliberate
sabotages, each rebuilt:

| sabotage | result |
|---|---|
| flipped a returned boolean | **DIVERGENT** ✅ |
| dropped a reciprocal on a stored float | **DIVERGENT** ✅ |
| changed a call argument | **DIVERGENT** ✅ |
| **called a different function with identical args** | EQUIVALENT ❌ |
| **swapped two object fields in a subtraction** | EQUIVALENT ❌ |
| change upstream of a fault both sides hit | EQUIVALENT ❌ |

Three channels are blind:

- **Wrong callee, identical args.** `compare_call_logs` compares only r3–r6.
  `check_call_targets` *does* compare symbol names but emits a **warning on the
  already-EQUIVALENT path**, and that warning is never persisted —
  `unicorn_reason` is NULL for all 16,989 EQUIVALENT rows.
- **Uniform fill.** The object region is one repeated byte (`FILL_BYTE=0xCD`),
  so `obj->a` and `obj->b` always hold the same value. Field-confusion bugs are
  *structurally* undetectable — and that is the `RndFlare::Load` class this
  project already found by other means. `--dual-fixture` varies the fill byte,
  not the per-field values, so its `confidence=high` means "two blind fixtures
  agreed".
- **Symmetric faults** (deliberate and documented).

**Read the verdicts asymmetrically.** DIVERGENT is informative and well
localised. EQUIVALENT means only "no difference on r3/f1, on r3–r6 at each
logged call, or in the object/global regions, under one uniform fixture".

### F. Every fresh worktree ran a degraded unicorn harness

`engine.py:175` allocated the batched register-read buffers as `ctypes.c_int`
(**signed**) while `CODE_BASE` is `0x80000000`. LR read back negative, so the
call-site offset came out as `-4294967264` where the truth is `0x20`. The C hook
does not have this bug (`_trampoline_hook.c:52` casts to `uint32_t`), so the two
implementations silently disagreed on identical input.

Consequence: `check_call_targets` looks the offset up in a dict keyed by real
offsets, so under the Python path every lookup missed and **the wrong-callee
warning was lost entirely**. Same sabotage, two outputs.

And the Python path is the one worktrees take: `_trampoline_hook*.so` is
gitignored and `setup_worktree.sh` neither built nor linked it. **Both fixed.**

### G. Worktree builds grow a shadow `decomp.db` with zero verdicts

Incidental, and a textbook instance of the standing rule. The ninja post-build
edge runs `ingest_report.py --db decomp.db` with a **relative** path, so `ninja`
in a worktree creates a fresh database *there*. Correct for writes — a worktree
build can never corrupt the shared DB. A trap for reads:

| | rows | verdicts | COMPLETE |
|---|--:|--:|--:|
| main repo `decomp.db` | 52,547 | 34,598 | 30,802 |
| a fresh worktree | 48,325 | **0** | **0** |

Any analysis script defaulting to `--db decomp.db` and run from a worktree
answers out of the shadow. `SELECT ... WHERE current_percent BETWEEN 80 AND 95`
returns five real rows against the main DB and **nothing** against the shadow —
and an empty result set reads as *"this class is exhausted"*.

`report_absent_census` was lucky: it touches `excluded`, so it crashed loudly.
Anything confined to the 16 shared columns fails silent. The MCP orchestrator
tools are **not** affected — they resolve `decomp.db` against the server's own
project root, which is why they can raise `CrossProjectError`.

Not moving where the build writes (that would reintroduce the concurrent-writer
corruption the relative path avoids). **Warned** at the moment the trap is set.

---

## The clean results, with their controls

### 1. Build determinism — better than documented

The documented "~±160-function nondeterminism between two clean builds" **did
not reproduce at all.**

Two worktrees, `clean_stale_objects.sh --all` (878 `.cpp` touched each), full
`ninja`, 980 MSVC edges each:

```
fns A: 48344   B: 48344
only in A: 0   only in B: 0
normalized differs: 0
fuzzy differs: 0
$ cmp det-a/report.json det-b/report.json   ->  IDENTICAL (15,204,214 bytes)
```

This is a *harsher* test than the documented one — different worktree paths,
different wall-clock second — and it still came out identical.

At the object level all 980 rebuilt `.obj` differ, which is why this was ever
thought to be nondeterminism. It is not. Diffing one:

```
sizes: 6349 6349        differing byte count: 2
[0x4]   COFF TimeDateStamp   1787125006 vs 1787125013   (7 s apart)
[0x20d] embedded source path 'a' vs 'b'                 (det-a vs det-b)
```

Neither reaches any measurement. **If the old ±160 figure is ever quoted again,
it should be quoted as historical.**

> ### ⚠️ CORRECTION (2026-08-31): this diff under-counted, and the under-count mattered
>
> **`differing byte count: 2` is wrong — it is at least 3, and the third field is the
> one that made the whole thing a *same-tree* problem rather than a cross-path
> curiosity.** This object carries a clock-derived **CodeView `S_OBJNAME` signature
> word** in `.debug$S`, which this diff did not report. Verified directly on
> 2026-08-31: `objname_signature_offsets()` finds it in `keygen_xbox.obj` at file
> offset **`0x1e8`** — 37 bytes *before* the `0x20d` the table above stops at, because
> `0x20d` is 33 bytes into `S_OBJNAME`'s *name* field. The differ found the path and
> did not walk back to the record header. (Second tell nobody chased: `TimeDateStamp`
> is a 4-byte field at offsets 4–7, and the companion audit at
> `docs/investigations/2026-06-10-roadmap-to-100/10-build-env-audit.md` reported it as
> "3 bytes at offsets 4-6" — a summarising differ, not an exhaustive one.)
>
> **Consequence.** The story this section tells is "the two differing fields are the
> clock and *the path*, so it only bites across worktrees." The real second field is
> also clock-derived, so **two rebuilds in the same tree at the same path differed
> too** — measured 2026-08-31 as **980 of 989 objects**, the 9 survivors being exactly
> the ones ninja did not rebuild. Fixed at the cause in `ee8902a22` by a sixth
> post-compile pass (`scripts/obj_build_metadata_patcher.py`) that zeroes both fields;
> after it, 0 of 989 differ and `tree_sha256` is stable across rebuilds.
>
> **What survives, and what does not.** The section's *headline* survives on its own
> evidence: `cmp`-identical `report.json`, 0 of 48,344 functions differing, is an
> objdiff-score comparison that neither field can reach. What does not survive is the
> licence this paragraph granted for twelve days — that object-byte churn was fully
> understood and bounded at "2 bytes, one of them the path". It was not, and in that
> window every byte-identity A/B spanning an MSVC recompile was unsound in one
> direction and unmeasurable in the other. See the population audit in `CLAUDE.md`
> ("A PCH-reached header has NO per-TU dependency record") for the corrected method.

### 2. `run_objdiff` percent honesty

The formatter is live and demonstrably in the path. The exact function named in
its own docstring:

```
# ObjectDir::Save -- Match: 99.98% normalized (99.7% raw)
**Instructions**: 603 total | 2 diff_arg
```

99.98 rounds to `100.0` under the old `{:.1f}`, so seeing `99.98` *is* the
proof. Unit-tested across the boundary: `99.95385 → 99.95%`, `99.99231 →
99.99%`, `99.999999 → <100%`, `100.0 → 100.0%`.

A true zero-mismatch function stays distinguishable:

```
# ObjectDir::Iterate -- Match: 100.0% normalized (99.6% raw)
**Instructions**: 153 total | all equal
```

### 3. Cross-project guard

```
$ run_objdiff(symbol='ObjectDir::Iterate', project_dir='../rb3-xenon')
project_dir belongs to a different decomp project -- refusing to answer.
  project_dir:  /home/free/code/milohax/rb3-xenon  (title ID 45410914)
  this server:  /home/free/code/milohax/dc3-decomp  (title ID 373307D9)
```

Names both title IDs and points at the correct orchestrator. **GOOD.**

### 6. `progress_metrics.py` reads the ruler

Falsified by forging four provenance values into scratch copies:

| forged `functionRelocDiffs=` | reported | caveat prose |
|---|---|---|
| `name_check` (real) | `name_check` | "stricter ruler" |
| `None` | `None` | forgiving-mode branch |
| `AUDIT_SENTINEL_XYZZY` | `AUDIT_SENTINEL_XYZZY` | forgiving-mode branch |
| provenance deleted | `unknown` | forgiving-mode branch |

The output tracks the input and the prose branch flips. The hardcoded-`None`
defect is genuinely fixed.

**Gap found and fixed:** the banner lived only in `--markdown`. Plain
`python3 scripts/progress_metrics.py` — the command
`STATE_OF_THE_DECOMP` tells you to run — printed `91.36% <-- CANONICAL` bare.
That caveat is load-bearing, not boilerplate: see [the ruler
note](#the-canonical-ruler-forgives-register-permutation) below.

### 7. `check_doc_links.py`

`30 ok, 0 failed` (plus rb3 25/25, 12 project-independent), re-verified after my
doc edits. Falsified three ways: renaming the marker file →
`28 ok, 2 failed`, **exit 1**; renaming a heading →
`FAIL [dc3] OFFSET_SWAP: anchor not found`; and `objdiff-cli doc-links`
auto-detection across six directories (`dc3` / `dc3` / `dc3` / **`unknown`** /
`rb3` / `unknown`).

`docs/decomp/patterns/PERMUTER_ROI_ANALYSIS.md` is present — regular file,
19,096 bytes, link count 1, git-tracked `100644`. The predicate in
`analysis.rs:1829-1841` is as described.

**Two corrections to the premise:**

- **A symlink would NOT degrade detection.** Rust's `Path::is_file()` follows
  symlinks; a scratch tree containing only a symlink still detected as `dc3`.
  Only a rename/move/delete (or the file becoming a directory) breaks it.
- `check_doc_links.py` passes `-P dc3` explicitly, so **it never exercises
  `detect_doc_project_at()`**. It caught the renamed marker only via its
  URL-existence check, which happens to cover the same file. A detection break
  that left the path occupied (e.g. it became a directory) would be invisible to
  the checker.

### 8. Single-object patcher gap

Behaviour reproduces exactly as documented:

```
$ ninja build/373307D9/src/system/os/Joypad_Xinput.obj
[1/1] MSVC build/373307D9/src/system/os/Joypad_Xinput.obj
$ python3 scripts/verify_objs_patched.py --verify-manifest ; echo $?
BUILD TREE DRIFTED SINCE IT WAS LAST VERIFIED PATCHED
  1 content differs: build/373307D9/src/system/os/Joypad_Xinput.obj
1
```

The detector's exit code is correct in both directions (0 on a clean tree). It
is documented in the right place — `docs/tools/BUILD_SYSTEM.md` names
"a **targeted** `ninja build/373307D9/src/Foo.obj`, or a tool compiling one TU".

**Operationally important and not obvious:** `run_objdiff`'s own
"Building incremental" *is* that path. After seven measurements my worktree had
seven un-patched objects. Measuring degrades the tree you are measuring.

> ### ⚠️ CORRECTION (2026-08-31): the transcript above is a vacuous positive control
>
> `--verify-manifest` is a whole-file sha256 of every `.obj` against `patch_state.json`.
> Pre-`ee8902a22`, **any** `ninja <one>.obj` changed that object's hash unconditionally —
> the COFF `TimeDateStamp` and the CodeView `S_OBJNAME` signature are clock-derived — so
> the transcript would have printed `content differs` **even if the patchers had run
> perfectly**. The demonstration could not have failed, and "seven un-patched objects" is,
> on this evidence alone, only "seven recompiled objects". The audit's own verdict row
> flagged the *magnitude* as `NO-CONTROL`; the **direction** was not flagged, and that is
> the part that made the check unfalsifiable.
>
> **The conclusion survives on independent evidence** — the single-object path really does
> skip the patchers, proven at the symbol level rather than by hashing: 224 unpromoted
> `??__E` symbols across 125 objects, a static guard's storage class left at 3 instead of
> 2, and 13 missing `??__F` atexit renames (`docs/tools/BUILD_SYSTEM.md`,
> `docs/analysis/2026-08-20-tool-gap-inventory.md`). Those are specific, named, and no
> clock can manufacture them.
>
> **The guard is also now more precise than it was**, since `ee8902a22` added
> `obj_build_metadata_patcher` as `PATCHERS[5]` and a full `ninja` leaves both fields
> zeroed: a `content differs` today is much closer to meaning what it says. It still is
> not exact — the manifest is rewritten by `--emit` on the same ninja edge — and
> `--verify-manifest` has **no test anywhere in the repo**, so by this project's own rule
> it is a guard nobody has watched fail.

**The "~0.5 pp low" magnitude is NO-CONTROL.** On the one unit I could exercise
end to end, the reading was **identical** patched vs unpatched (80.4% both
ways), including its `??__E` thunk (100.0% both ways). The patchers there rename
symbols (6 anon-ns replacements + 1 `??__E` promotion) and those renames do not
move instruction-level match. Treat "~0.5 pp" as an unverified aggregate.

⚠ One methodology note worth recording: my first attempt measured a −0.80 pp
gap, which was **a ruler artifact, not the patcher gap** — `report.json`'s
`match_percent_normalized` (81.198) against `run_objdiff`'s headline (80.4).
Two different rulers, both called "normalized". I nearly published it.

### 10. The rest of `scripts/analysis/`

| tool | control | result |
|---|---|---|
| `scope_index_census.py` | mutated one scope index in a copied `symbols.txt` | tracked exactly as predicted |
| `name_charge_census.py` | report with a row deleted; empty map; unit filter matching nothing | correct all three |
| `coff_bodies.py` | absent symbol; absent `.obj`; 0-byte `.obj`; 50/90/99 % truncated | correct, but **silent** on every bad input |
| `ruler.py` | empty project dir; corrupted provenance; ratchet with a planted offender; bogus selector | correct all four |

`ruler.py` is substantive, not decorative — the same function reads three scores
depending on the ruler (`name_check` 99.973, `none` 100.0, `data_value` 99.235),
a live instance of the "rounded 100.0 hides real bugs" pattern.

**Test coverage was the real gap.** `pytest scripts/analysis/tests` passed 80/80,
but **no test in the repo referenced any of the six tools audited**. The suite
covered `diff_inspect` frame-size fields and `inlining_catalog` only. Added
`test_fold_proof_guards.py` (80 → 90 passing), written as negative controls: each
asserts the tool *refuses*, with companion tests asserting it still accepts the
genuine article, so a guard degenerating into "always refuse" also fails.

### 9. Native build + `milo-tests`

**Build: GOOD.** All four targets green — `milo-tests`, `dc3-native`,
`milo-viewer`, `render-test`, 3301/3301 steps, no errors. `git status` clean
before *and* after (`native/build*/` is gitignored), so the native build does
not dirty the tree.

**Suite as an instrument: GOOD — it can go red, and does so correctly.**
Negative control run in a scratch worktree against **production code**, not test
assertions:

- `src/system/math/Rand.cpp:37` — dropped the `(unsigned int)` cast,
  reintroducing the documented `srawi` sign-extension bug.
- `src/system/math/SHA1.cpp:22` — removed the `Sha1Bswap32` byteswap.

→ **8 failed of 11, exit 8**, naming exactly the right tests with concrete value
diffs. Reverted → 11/11, exit 0. The suite is not lying.

**But its headline is misleading, and the control exposed a vacuous test.**

`ctest` prints **"100% tests passed out of 441"** and exits 0 — while **79 tests
skipped**. Only **362 actually executed**. So 362 is the live number and the
371/371 in CLAUDE.md is stale (the repo already knew: a 2026-06-10 investigation
records 371/371 as "FALSE in this environment").

The skips are the entire end-to-end tier, all env-gated: `GameplayTelemetryTest`
×48, `DtaFlowTest` ×7, audio/Mogg/Bink ×16, `CharClipGroupTest` ×2,
`HeadlessBootTest.LongRunStability`. **Enabling the gates surfaced real bugs the
default run cannot see:**

- **Intermittent `SIGSEGV` in `TaskMgr::Poll()`** at ~frame 50 on the
  `autosave_warning_screen → title_screen` transition — 3 crashes in 5 bare
  runs, 0 under gdb. A load-sensitive race, likely a Heisenbug. (Worth noting
  the auditor's own methodology catch: an initial 2/2 sample would have been
  called deterministic.)
- **The "flying feet" IK bug is live** — `NoAnkleSuddenJumpsDuringGameplay`:
  L-ankle teleports 130.6 units at frame 1070.
- All 7 `DtaFlowTest` **pass** when enabled — pure lost coverage.
- `CharClipGroupTest` ×2 gate on `getenv("MILO_LIB")`, but
  `test_asset_loading.cpp`'s `GetMiloLibRoot()` default already resolves on this
  box. Setting `MILO_LIB` makes both pass in 45 ms. Free coverage.

**Vacuous test found:** `RandSeed.NoSignExtensionPoison` — the test written
*specifically* to catch the `srawi` bug — **passes with that bug reintroduced.**
Its `EXPECT_NE(hi, 0xFFFFu)` probe never fires; only the sibling sequence tests
caught it. `Sha1.Deterministic` and `RandSeed.Deterministic` likewise pass on
broken code — they assert only self-consistency, so they can never fail on a
wrong-but-stable algorithm. Three tests, zero falsifying power.

**Compiler warnings carry zero information here.** All 5,712 CXX edges are
compiled with `-w` (`native/CMakeLists.txt:95`), with `-DLP64_AUDIT=ON` as the
escape hatch. "No warnings" is not evidence of health in this build.

**Engine pin: the warning mechanism works, but the source pin is inert.** Four
different values are in play:

| source | value | age |
|---|---|---|
| engine `HEAD` | `6d5dc0f` | 2026-08-18 |
| `native/CMakeLists.txt:227` | `77eb428b` | **139 commits behind** |
| `native/build/CMakeCache.txt` | `12455b0a` | 193 behind |
| CLAUDE.md (documented) | `8282103` | stale doc |

`MILO_ENGINE_PIN` is `set(... CACHE STRING ...)` **without `FORCE`**, so the
cache permanently shadows the source. Proven: setting the source pin to exactly
the engine HEAD and reconfiguring *still* warned, still quoting the cached
`77eb428b`. **Consequence: `scripts/bump-engine.sh --apply` is a no-op against
any existing build directory**, and the main build dir has been warning about a
pin nobody ever wrote. The same shadowing applies to `MILO_ENGINE_PATH`, whose
`${CMAKE_SOURCE_DIR}/../../milo-native-engine` default also resolves wrongly in
a worktree.

Also: the `native-build` skill's documented configure line fails on a *fresh*
build dir — `find_package(Dawn REQUIRED)` needs
`-DDawn_DIR=…/dc3-decomp-deps/dawn/lib/cmake/Dawn`, which exists only as an
`UNINITIALIZED` cache entry in the current build dir.

⚠ **Reproducibility caveat:** `../milo-native-engine` had an **uncommitted edit**
to `src/platform/FxSendNative.cpp` during this audit (drops
`p.mActiveBands = 0x1F` and `p.mBand5Q`). Everything built here includes it, so
these native numbers are not reproducible from any committed engine state.
Almost certainly a concurrent agent's in-progress work; left alone.

---

## The canonical ruler forgives register permutation

Not a defect — `progress_metrics.py` discloses it in its `--markdown` output and
its module docstring — but it is the single most misquotable fact in the
project, and it was **not** disclosed on the stdout path people actually run.

The canonical headline counts `match_percent_normalized == 100`.

```
$ run_objdiff '?roll@@YAHH@Z' --full-listing
Match: 94.6% normalized   |   equal 4 (33.3%), diff_arg 8 (66.7%)

| 2 | addi r11, r11, 0x13  | addi r10, r11, 0x13  | diff_arg |
| 6 | divw r10, r11, r10   | divw r7, r9, r8      | diff_arg |
| 8 | subf r11, r10, r11   | subf r5, r6, r9      | diff_arg |
```

`?roll@@YAHH@Z` has `match_percent_normalized = 100.0` and **is counted in the
91.36 % headline** with 8 of its 12 instructions differing. Every difference is
register allocation; no relocations are involved.

**395 authorable functions / 150,108 bytes** are in that state — 1.34 % of the
29,430 counted as matched. The headline is not wrong; "91.36 % byte-identical"
would be. The honest phrasing is *"91.36 % match modulo register permutation."*

---

## Changes landed on this branch

No `src/` file was touched, so PPC codegen is byte-identical by construction
(`git diff --name-only main...HEAD | grep -c '^src/'` → 0).

| commit | what |
|---|---|
| `4d7769ceb` | `[STUB]` marker dead code; `re.subn` fail-loud headline; large-output path formatter |
| *(ruler disclosure)* | `progress_metrics.py` provenance banner on the stdout path |
| *(phantom marking)* | `compare_progress.py` `Norm` column + `~` marker + pre-table count |
| *(fold guard)* | `fold_proof.py` writability guard + `coff_bodies.py --with_chars` + 10 tests |
| `693268380` | unicorn `ctypes` signedness; `setup_worktree.sh` links/builds the C hook |
| `fe43b6817` | shadow-`decomp.db` warning in `ingest_report.py` |
| `91d884590` | `report_absent_census` classifier ordering |
| `19fd2a1be` | `STATE_OF_THE_DECOMP.md` refresh |

---

## For a follow-up lane

Ordered by value, with enough detail to act without re-deriving.

1. **Promote unicorn's callee-identity mismatch from warning to verdict.**
   `comparator.check_call_targets` (~line 189) already detects it and already
   has the symbol names. Either make it DIVERGENT or at minimum persist it to
   `unicorn_reason`. Today `unicorn_reason` is NULL for all 16,989 EQUIVALENT
   rows, so the signal exists and reaches nobody.

2. **Vary the unicorn fixture per field.** `FILL_BYTE=0xCD` uniform fill makes
   every object field hold the same value, so field-confusion bugs — the
   `RndFlare::Load` class — cannot be detected even in principle. An
   address-derived fill (e.g. `byte = (offset * k) ^ seed`) would close it.
   `--dual-fixture` does not: it varies the byte, not the per-field values, so
   its `confidence=high` label currently overstates.

3. **Repoint the dead unicorn tests.** `test_integration.py:20` and
   `test_coff.py:11` hardcode `build/373307D9/system/gesture/Skeleton.obj`, a
   layout that no longer exists (real paths are `build/373307D9/src/…` and
   `build/373307D9/obj/…`). All 15 skips are dead tests, including **the only
   end-to-end test of `run_comparison()`**. Repointed by hand they are 9/10;
   the one failure (`test_bctrl_function_executes`, `??_H@YAXPAXIHP6APAX0@Z@Z`
   no longer in the original `.obj`) looks like benign staleness.

4. **Decide what `current_percent` should hold.** It currently mirrors fuzzy
   while the project's canonical ruler is normalized. Either rename it, or
   populate it from `match_percent_normalized`, or document the choice at the
   column. Any of the three beats the status quo, where the obvious cert-rot
   query returns 374 phantoms.

5. **Drop or populate the 7 dead `has_*` columns.** They are worse than absent:
   filtering on them silently selects everything or nothing, and
   `has_linker_merged = 0` actively contradicts the `verdict_reason` text on the
   same rows.

6. **Quantify the single-object patcher gap properly, or stop quoting a number.**
   It needs a full unpatched-tree `report.json` to compare against a patched
   one. Until then "~0.5 pp" is folklore.

7. **Make `coff_bodies.py` and `name_charge_census.py` complain on empty input.**
   Both currently return zero rows at exit 0 for a nonexistent path or a
   truncated map. They degrade toward the weaker answer so they cannot fail
   open, but silence is indistinguishable from a real negative.

8. **Consider a `--verify-manifest` check inside `run_objdiff`.** It un-patches
   what it measures; a one-line warning when the tree has drifted would stop the
   next person rediscovering this.

9. **Add `FORCE` to `MILO_ENGINE_PIN` (and `MILO_ENGINE_PATH`)** in
   `native/CMakeLists.txt:227`, or have `bump-engine.sh` write the cache
   directly. Until then the script silently does nothing against an existing
   build dir, and the pin warning quotes a value nobody set. Then reconcile the
   four in-flight values — engine HEAD `6d5dc0f`, source `77eb428b`, cache
   `12455b0a`, CLAUDE.md `8282103`.

10. **Fix or delete `RandSeed.NoSignExtensionPoison`.** It is the designated
    guard for the `srawi` sign-extension bug and it passes with that bug
    present. `Sha1.Deterministic` and `RandSeed.Deterministic` are
    self-consistency-only and equally unfalsifiable. A vacuous guard is worse
    than none: it makes the class look covered.

11. **Stop `ctest`'s "100% tests passed out of 441" from being the headline.**
    79 skips are counted as passes, and the skipped set is where the live bugs
    are (`TaskMgr::Poll()` SIGSEGV, flying-feet IK). At minimum default
    `MILO_LIB` on (2 tests, 45 ms, already resolvable) and report
    executed/skipped separately.

12. **Investigate the two live native bugs the gates surfaced**, both real and
    both currently invisible to CI: the intermittent `SIGSEGV` in
    `TaskMgr::Poll()` on the `autosave_warning_screen → title_screen`
    transition (3/5 bare runs, 0/1 under gdb — a load-sensitive race), and the
    130.6-unit L-ankle jump at frame 1070.
