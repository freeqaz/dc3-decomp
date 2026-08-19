---
name: unicorn-query
description: Query unicorn behavioral test results from the database. Filter functions by verdict (EQUIVALENT/DIVERGENT), divergence class, harness provenance, and unit. Use to find functions with real behavioral bugs to fix — and to check whether a recorded verdict is stale enough to be worthless.
argument-hint: "[--class <class>] [--verdict DIVERGENT|EQUIVALENT] [--min-harness 4] [--unit pattern] [--status workable|complete|all] [--limit N] [--summary-only]"
allowed-tools: Bash(python3 scripts/unicorn/query.py *)
---

# Unicorn Query Skill

Query the decomp database for unicorn behavioral test metadata. Surfaces functions
by their runtime behavioral equivalence status.

## Arguments

`$ARGUMENTS`

## Steps

1. **Build the command** from the arguments and run:
   ```bash
   python3 scripts/unicorn/query.py $ARGUMENTS
   ```

   If no arguments were provided, default to `--verdict DIVERGENT` to show all divergent functions.

2. **Present results.** Two things must be said every time, or the numbers mislead:

   * **Which harness measured it.** `unicorn_harness_version` (h1..hN) is printed
     in every summary and in the last column of the table. **h1 means the verdict
     predates the 2026-08-18/19 defect fixes and is worthless** — that harness
     stubbed MSVC's register-save helpers, so `bl __savegprlr_N` zeroed `this` at
     the second instruction of the prologue and 87.5% of functions spun to the
     instruction cap. It overstated real bugs by roughly 8x. Pass
     `--min-harness 4` to exclude those rows entirely. The changelog is in
     `scripts/unicorn_runner/signal_version.py`.
   * **How much of the DIVERGENT count is artifact.** The summary prints this as
     a percentage. As of the 2026-08-19 whole-DB re-ingest it is **99.4%** —
     9,815 `data_layout` + 2,096 `cap_exhausted` out of 12,419 DIVERGENT. A bare
     "12k functions diverge" is not a finding.

3. **Read the class, not the verdict.**

   | class | verdict on it |
   |---|---|
   | `data_layout` | ARTIFACT. Only differing values are addresses the harness assigns per side. Never a decomp bug. |
   | `cap_exhausted` / `_orig` / `_decomp` | ARTIFACT 72% / 98% / 97%. Emulator gave up. |
   | `build_env`, `regalloc`, `stack_layout`, `merged_call`, `merged_arg`, `fpr_precision`, `orig_error` | ARTIFACT — build/emulation/target properties, not our source. |
   | `call_count` | ARTIFACT ~83%. Often just how many stub hits fit the budget (`AdjustSaturation` logged 8,321 vs 8,322, and that off-by-one WAS the divergence). |
   | `wild_jump_match` | Both sides crashed somewhere *different*. A whole-function-rewrite signal on a low-match AT_LIMIT function, not a pinpointed defect. Deprioritise. |
   | `unmapped_access_mismatch` | SUSPECT. Trampoline stubs return 0 where a ctor returns `this`, which manufactures these. |
   | `return_value` | Check the return type first — the comparator compares `r3` without knowing it. 6 of the 10 current rows are on `void`- or `float`-returning functions and are false. |
   | `error` (decomp_error) | **The highest-precision signal in the oracle.** Our side faults where the original doesn't. Chase every one. Currently zero rows. |
   | `logic`, `call_arg`, `object_memory` | Pinpointed. Adjudicate individually. |

4. **A pinpointed class on a COMPLETE (100%) function is structurally an
   artifact.** Identical bytes plus identical inputs cannot produce different
   behaviour. 408 such rows were audited; zero were genuine. Do not re-open this.

## Common Queries

- `/unicorn-query --summary-only` — verdict counts, class breakdown with artifact
  annotations, and harness provenance. **Start here.**
- `/unicorn-query --verdict DIVERGENT --min-harness 4` — only verdicts from the fixed harness
- `/unicorn-query --class error` — decomp-side faults; the only class worth chasing blind
- `/unicorn-query --class logic --status workable` — pinpointed bugs not yet marked done
- `/unicorn-query --class data_layout --unit system/char/*` — see what the artifact floor looks like in one subsystem
- `/unicorn-query --verdict EQUIVALENT --status workable` — behaviorally correct but asm not matching yet

## Standing caveat on EQUIVALENT

The default fixture is zero-fill, so loop bounds read as 0: **411 of 660
completions log zero calls.** An EQUIVALENT can mean "both sides skipped the loop
identically." A further ~21% rest on the "both sides hit an identical error at an
identical PC" rule rather than on both sides completing. EQUIVALENT is evidence,
not proof. See `docs/analysis/2026-08-19-unicorn-reingest.md`.
