# Stream 3 — decomp-synth Binary-Oracle Phase 2 (design + build)

**Read `docs/plans/port-harvest/WORKFLOW.md`** for worktree/subagent/merge mechanics (used for
experiments here too). This stream is **design-and-build**, not a port harvest — it extends
`../decomp-synth` so its rules chase the **target binary** to crack the functions porting can't reach.

## The problem this attacks
The differ-filter splits our sub-100% functions into: og-dc3-beats-us (Streams 1+2) and
**"both stuck" ≈ 1,300 functions / ~495K bytes** where og-dc3 is *also* stuck at our level.
These are register-allocation / FPR / instruction-scheduling floors. Porting can't help (no better
source exists). The permuter currently gets ~0% on them. **This is the largest remaining bucket and
the highest ceiling — but the hardest.**

## Hard constraint from the user
**og-dc3 is NOT an oracle for decomp-synth.** Do not seed/guide synthesis from og-dc3 source — we'd
inherit its blind spots and could never surpass it. The oracle is the **target binary**
(objdiff/disassembly = ground truth) plus the **c2.dll compiler model** we've reverse-engineered.

## What already exists (inventory before building)
- **decomp-synth** (`../decomp-synth`): ~150 behaviour-neutral transforms incl. `declaration_reorder`
  (has a documented **BSF-guided mode**), `fpr_cascade_operand_hoist`, `prologue_pressure`, `slot_pad`,
  `mwcc_regorder_probe` (Metrowerks, 0 wins on our MSVC — ignore), plus beam/hill/evolutionary search,
  `target_facts`/`ppc_shape_facts`/`stack_slot_oracle`, `strategy_db`. Run: `permute` skill / `venv/bin/python -m decomp_synth.scan_and_permute`.
- **c2.dll register allocator: fully reverse-engineered** in `docs/plans/compiler-instrumentation.md`
  — the chain `declaration order → c1xx symbol ID → c2.dll interference-graph coloring → BSF (RVA 0x026780) → register`. Colors are deterministic per variable; only the color→register *mapping* shifts with allocation order.
- **White-box solver**: `tools/compiler_trace/{bsf_trace,bsf_diff,regmap_solver}.py` (BSF tracing via GDB
  + `guided_pairwise_search`, `color_to_gpr`). decomp-synth's `declaration_reorder` imports it.

## The key gap (validated 2026-05-30)
The BSF-guided oracle **is wired but runs dumb**: its tracer needs a **patched 32-bit wibo + GDB**
(PROT_WRITE hack), which is NOT provisioned in normal sweep worktrees, so it **silently falls back to
blind 20-permutation search**. That's why the Track-B permuter runs (mtxInvert/linepair/matmul) got
~0% — the white-box oracle never fired. Also: even when it fires, the prior RE proved decl-reorder only
fixes the ~12-30% of swaps that are *user-variable* pairs; **synthesized-constant swaps** (`li r29,0`
etc.) are NOT declaration-order-controllable.

## Design directions (in priority order)
1. **Productionize the white-box allocation oracle.** Provision the patched 32-bit wibo in sweep
   worktrees so BSF-guided reorder reliably fires; lift the 20-perm cap for the *guided* path (guided =
   no combinatorial blowup). Read the **target's** per-variable register assignment from the objdiff
   disasm, run `regmap_solver` against it, emit the *exact* declaration permutation. This is the
   minimal "chase the binary" win and reuses existing code.
2. **Compiler inversion as a transform.** Generalize #1: given the target's allocation/stack layout
   (from disasm) + the c2.dll model, derive the source shape (decl order, temp lifetimes, expression
   grouping) that produces it — a targeted transform, not blind search. Covers stack-slot SWAPs and
   offset shifts, not just GPR pairs.
3. **Synthesized-constant materialization model.** Model where c2.dll materializes 0/nullptr and add
   transforms that move the constant's introduction/use site (the only lever for non-user-variable
   swaps). Niche, hard; quantify how many "both-stuck" functions are constant-dominated first.
4. **FPR + fmadds levers.** Many "both-stuck" are FPR-alloc/fmadds (Geo, mtx, CharBones Multiply).
   `fpr_cascade_operand_hoist` + `pragma_fp_contract` exist — measure their real hit rate; the prior
   ceiling_calculator found ~226 functions with fixable encoding/FMA patterns.
5. **Cross-TU symbol-ID context** (symbol IDs are TU-global; siblings affect allocation) and feeding
   regalloc wins/losses to `strategy_db` — later, if 1-4 show promise.

## DO THIS FIRST — validation experiment (before building anything)
The prior RE was pessimistic; validate the premise cheaply:
1. Pick 5-8 "both-stuck" regalloc functions (query decomp.db for AT_LIMIT with verdict_reason flagging
   regalloc/FPR floors, or re-run the systematicity analysis in `reference_prize_map_signals`).
2. Provision the patched 32-bit wibo (`/home/free/code/milohax/wibo` build, see compiler-instrumentation.md
   Experiment 8) in a test worktree; confirm BSF tracing fires (not falling back).
3. Run BSF-guided `declaration_reorder` (white-box) vs blind on those functions; measure delta.
4. **Decision:** if white-box guided meaningfully beats blind (closes user-variable swaps the blind
   search missed) → productionize (#1) and roll out. If it doesn't (constant/FPR-dominated) → the
   binary-oracle source approach has a low ceiling here; document it and redirect to FPR/fmadds (#4)
   or accept these as the true floor.

## Workflow notes
- decomp-synth changes are made in `../decomp-synth` (its own repo); the DC3 side consumes the installed
  package. Test permuter changes against DC3 functions via worktrees (`project_dir=<worktree>`).
- Use subagents for parallel experiments (one function-set per agent), but this stream is more
  interactive/iterative than Streams 1-2 — expect design loops, not a mechanical sweep.
- **Success = a measured, reproducible improvement on binary-oracle-driven matching** of the "both-stuck"
  bucket, with the approach documented. Negative results are valuable (they retire the bucket honestly).
