# Stream 3 — Binary-Oracle Phase 2 validation experiment (2026-05-31)

**Verdict: NEGATIVE. The white-box BSF allocation oracle fires perfectly but cannot
crack the "both-stuck" register-swap bucket. Declaration reorder — blind, ASM-guided,
*or* BSF-white-box-guided — improves 0 of 10 representative functions (0 of 30
function×arm runs). Do NOT productionize the white-box source-synthesis oracle for this
bucket. The bucket is a genuine floor for source-level reordering.**

This is the "do the validation experiment first, don't build blind" step from
[STREAM3-decomp-synth-binary-oracle.md](../plans/port-harvest/STREAM3-decomp-synth-binary-oracle.md).
The negative result retires the bucket honestly (the plan's stated success criterion).

## What was tested

The hypothesis (plan design direction #1): the white-box BSF-guided `declaration_reorder`
oracle "silently falls back to blind search because the patched 32-bit wibo + GDB tracer
isn't provisioned." If we provision it, guided reorder should beat blind and crack
user-variable register swaps.

### Provisioning check (precondition) — PASSES

Everything the tracer needs is already present and working:
- `gdb` 17.2; 32-bit loader `/lib/ld-linux.so.2` + `libstdc++` present.
- Patched 32-bit wibo at `wibo/build/debug/wibo` (ELF i386, the `PROT_WRITE` heap patch
  from compiler-instrumentation.md Experiment 8) — runs.
- `c2.dll` is unpatched (md5 == `c2.dll.orig`); BSF at RVA `0x026780`.
- `tools/compiler_trace/bsf_trace.py::_resolve_wibo_32()` is **already worktree-aware**
  (env override → repo-adjacent → `git rev-parse --git-common-dir` parent → `~/code/milohax`).
  The "unprovisioned in worktrees" gap the plan described is **already closed** in the
  current code.
- Smoke test: `trace_bsf(swap_a.cpp)` → 170 BSF calls in ~16 s, reproducing Experiment 8
  exactly (call #1 lo=0x4 bit=2, #2 lo=0x2 bit=1, …).

### The experiment

10 functions drawn from the **both-stuck ∩ REGISTER_SWAP-dominated** pool (39 such
functions: both_stuck candidates whose re-triage tier is `B_PERMUTER` and whose patterns
include `REGISTER_SWAP`). All 10 confirmed genuinely both-stuck (|our% − og%| < 0.5),
match range 82–94%, sizes 259–1491 bytes, mismatch counts 4–25.

For each function, `declaration_reorder` was run in three **cleanly isolated** arms
(monkeypatching the pattern's `_try_*` sub-generators so each path runs alone; Ghidra
disabled in all arms; `dry_run=True` so nothing is written):

- **blind** — random dependency-safe permutation (the historical fallback, cap 20/group).
- **asm** — `_try_asm_guided`: `/FAs` listing → var→reg map → targeted swaps (no GDB).
- **bsf** — `_try_bsf_guided` with `bsf_required=True`: GDB BSF trace → `guided_pairwise_search`
  (the white-box oracle). `bsf_required` means it errors rather than silently falling back.

Each variant is compiled and scored with objdiff (real match% deltas).

## Results

```
ROW idx size mism  blindImp asmImp bsfImp  blindTried asmTried bsfTried  err
  0  496   10    0.00   0.00   0.00      10        10        4      0
  1  259    4    0.00   0.00   0.00       6         4        4      0
  2 1491   23    0.00   0.00   0.00      19        19       19      0
  3  953   16    0.00   0.00   0.00      16        16        8      0
  4 1126   13    0.00   0.00   0.00       8         8        8      0
  5  444   11    0.00   0.00   0.00      12         9        9      0
  6  259    4    0.00   0.00   0.00       6         4        4      0
  7  690   22    0.00   0.00   0.00      22        22       22      0
  8  280   25    0.00   0.00   0.00      25        25       22      0
  9 1490   16    0.00   0.00   0.00      16        16       16      0

SUMMARY: any_improved=0  bsf_fired=10  bsf_beats_blind=0  of n=10
```

- **bsf_fired=10 / err=0** — the white-box oracle traced and generated targeted guided
  candidates (4–22 each) on every function. It is *not* falling back to blind.
- **any_improved=0** — zero improvement under any arm, 0/30 function×arm runs.
- **bsf_beats_blind=0** — guided never beats blind because both are exactly zero.

## Why it fails (mechanism — confirmed, not assumed)

Diagnosing the swap operands of the 10 functions (objdiff `--include-instructions`):

| swap type in function | count | declaration-reorderable? |
|---|---|---|
| GPR-only swap pairs | 3 funcs | No — constant/coalescing-phase (see below) |
| **FPR swap pairs** | **4 funcs** | **No — different register file from GPR decl order** |
| no clean swap pair at all | 3 funcs | No — mislabeled scheduling/coalescing divergence |

This confirms two things already in the literature here:

1. **The REGISTER_SWAP re-triage label over-counts.** It only sees GPR swaps; FPR swaps
   and scheduling get mislabeled REGISTER_SWAP/OFFSET_SWAP (memory
   `reference_prize_map_signals`, "SECOND refinement"). 4/10 of this "regswap" sample are
   actually **FPR** — GPR declaration reorder is the wrong tool by construction.

2. **The GPR swaps that *are* present are not user-variable swaps.** Per
   compiler-instrumentation.md Experiments 1–8: colors are fixed by the interference
   graph; declaration order only moves the color→register *mapping* for user variables
   colored in the **initial-coloring** phase (RVA 0x027242). The both-stuck GPR swaps are
   synthesized constants (`li rN,0` / nullptr — no source declaration) or are decided in
   the **coalescing/recoloring** phases (0x026B5E / 0x0272E8), which declaration order
   does not control. The guided oracle correctly maps a swap pair to a declaration pair
   and emits that swap — but compiling it produces the identical allocation.

This corroborates every prior data point: mtxInvert +0%, linepair +0%, matmul +0.5%,
SHA1::Transform +0.7% (1736 regswap instrs), and the exhaustive decl-reorder failures in
Experiments 1–2.

## Decision

**Do not productionize the white-box GPR allocation oracle for source synthesis.** Its
ceiling on the both-stuck bucket is ~0. The tracer itself is sound and reusable for
*analysis* (it works perfectly), but as a source-permutation guide it has nothing to bite
on here. The plan's design directions #1/#2 (read target allocation → emit exact decl
permutation) would not change this: the target allocation is already known from objdiff;
the problem is that **no source declaration order reproduces it**, because the divergence
isn't in the declaration-order-controlled phase.

### The proposed pivot (#4 FPR/fmadds) is a BUILD, not a MEASURE

The plan said "fpr_cascade_operand_hoist + pragma_fp_contract exist — measure their hit
rate." **They do not exist in decomp-synth.** The extracted repo has 11 general transforms
(arithmetic_identity, associativity, branch_polarity, commutative_reorder, comparison_flip,
declaration_movement, declaration_reorder, expression_grouping, signedness_cast,
variable_extraction, base) — the FPR/FMA patterns were dropped in the §5
"drop game-specific patterns" extraction step and never migrated. The STREAM3 inventory of
decomp-synth patterns is stale on this point.

So pursuing FPR/fmadds means **building** new patterns:
- `#pragma fp_contract(off)` injection (file-scoped; proven to suppress fmadds in
  compiler-instrumentation.md Step 7 — pure "need OFF" cases are fixable; mixed-direction
  are not).
- FPR declaration-order reorder using the *sequential* FPR rule (1st float→f31, 2nd→f30…;
  `regmap_solver.fpr_to_decl_index` already encodes it) — note `declaration_reorder` today
  filters out cross-register-file swaps and does **not** reorder by the FPR rule.

Honest scope: compiler-instrumentation.md's ceiling_calculator found only ~226 functions
with fixable encoding/FMA across **all** ~1,838 AT_LIMIT functions (FMA patterns = 171
instrs, 0.2% of mismatches; of 14 FMA functions found in a scan, only 9 were pure-direction
fixable). Most of those are not in the both-stuck regswap bucket. The FPR pivot is real but
small — it will not move the 495K-byte both-stuck bucket meaningfully.

### What actually moves the needle

The both-stuck bucket (~1,300 fns / ~495K bytes) is a genuine register-allocation /
FPR / scheduling floor. The highest-ROI legitimate lever remains **porting og-dc3's exact
source for the og-beats-us bucket** (Streams 1/2) — that carries the original's
declaration-order DNA and is proven (73 fns→100% over 3 waves). Porting does **not** help
the both-stuck bucket by definition (og-dc3 is stuck there too), and per the user's hard
constraint og-dc3 must not seed synthesis. So the both-stuck bucket should be **accepted as
the floor** and tagged at-limit, with a small optional FPR/fmadds build-out for the
~pure-direction fmadds functions if/when someone wants the marginal bytes.

## Reproduce

```bash
# worktree (already worktree-aware tracer; gdb + 32-bit wibo already provisioned)
scripts/setup_worktree.sh /home/free/code/milohax/wt/s3-oracle s3-oracle-exp
# harness monkeypatches declaration_reorder's _try_* to isolate blind / asm / bsf arms,
# dry_run=True, scores each variant with objdiff:
#   /tmp/claude/s3_experiment.py   (3 arms × 10 funcs)
#   /tmp/claude/s3_analyze.py      (summary table above)
#   /tmp/claude/s3_diagnose.py     (GPR vs FPR vs no-swap classification)
```

Artifacts: `/tmp/claude/s3_{picked,results,diag}.json`.
