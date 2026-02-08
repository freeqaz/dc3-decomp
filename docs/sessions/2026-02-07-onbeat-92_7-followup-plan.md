# Session Plan: RhythmBattle::OnBeat Follow-up from 92.7%

**Date**: 2026-02-07 (updated 2026-02-08)
**Function**: `RhythmBattle::OnBeat`
**Symbol**: `?OnBeat@RhythmBattle@@AAAXXZ`
**Starting baseline**: **92.7%**
**Current baseline**: **93.34%**

## Objective

Push `RhythmBattle::OnBeat` above 92.7% with tightly scoped source-shape experiments, while preserving known-good edits and minimizing churn.

## Hard Problem Framing (Read First)

This is a hard compiler-shape problem, not a normal logic bug. Most remaining deltas are second-order codegen effects, so "reasonable cleanup rewrites" often make things worse.

Treat this as a deep optimization campaign:

1. Prefer high-quality hypotheses over high experiment count.
2. Expect non-local effects: a small local rewrite can perturb register allocation across wide ranges.
3. Spend more time tracing exact producer/consumer chains before editing.
4. Keep edits tiny, but keep analysis depth high.

Default posture: trace harder before changing code.

## Deep-Trace Playbook (When Progress Stalls)

Run this playbook before trying another syntax variant in the same area:

1. Pick one mismatch family only (for example `idx ~1252` branch inversion, or `idx ~1377-1385` bool materialization).
2. Trace backward to the first divergence:
   - Use `--range` around the cluster and expand by +/- 40 to 80 instructions.
   - Find the earliest instruction where target/src stop sharing shape.
3. Map divergence to exact source statements:
   - Identify the C++ expression, temporary lifetime, and branch structure that emits that instruction.
   - Record which source variable maps to which register sequence.
4. Trace upward to controlling inputs:
   - What exact values and conditions feed this block?
   - Which earlier assignments/branches force this shape?
5. Trace downward to side effects:
   - Which later instructions depend on this value?
   - Which register/stack cascades are caused by this local choice?
6. Write a prediction before editing:
   - "This change should remove/convert instruction family X and not touch family Y."
7. Apply one micro-change and validate prediction with `--compare`.

If prediction fails twice in a row for the same lever, mark that lever exhausted and move to a different structural anchor.

## High-Effort Reasoning Strategies

Use these deliberately to think harder, not broader:

1. **Anchor on first divergence**, not on the most visible mismatch.
2. **Separate semantic equivalence from codegen equivalence**; only the latter matters here.
3. **Track lifetimes explicitly** for bool/int temps in hot windows (`b36`, `i27`, `i35`, `b43`, `outOfRange`).
4. **Use paired comparisons**:
   - baseline vs candidate
   - candidate vs last-best
   This prevents false wins from noisy global movement.
5. **Promote local dominance**:
   - Prefer changes that reduce mismatch budget in the target window even before global gain.
6. **Escalate analysis depth before declaring exhausted**:
   - two full deep-trace passes on each unresolved diff_op family.

## Locked Baseline (Do Not Regress)

### Keep these edits (original)

1. `#line 682 "RhythmBattle.cpp"` at `src/system/hamobj/RhythmBattle.cpp:682`
2. `for (int i = 0; i < unk134.size(); i++)` at `src/system/hamobj/RhythmBattle.cpp:842`
3. `i27 = b36;` at `src/system/hamobj/RhythmBattle.cpp:949`
4. Hotspot A aliases (improving edit):
   - `ArchiveSkeleton &current = unk134[i];` at `src/system/hamobj/RhythmBattle.cpp:844`
   - `ArchiveSkeleton &previous = unk134[iPrev];` at `src/system/hamobj/RhythmBattle.cpp:845`

### Keep these edits (session results, 92.7% → 93.34%)

5. **E01**: `b43 || outOfRange` evaluation order swap at line 960 (diff_op -2)
6. **E05**: Scorer loop call reorder: `current.JointPos → previous.JointPos → SetCamJointPos` at lines 851-856 (match +0.31%)
7. **E07**: `Min → Max` for `mPlayerOne/Two->Unk284()` at line 1238 (fsel args corrected)
8. **E08**: `static bool s14bc; s14bc = false;` runtime assignment at lines 1228-1229 (match +0.12%)
9. **E10**: Split nextUnk148: `int nextUnk148 = ... + 8;` at line 1258, `unk148 = nextUnk148 + i6cc;` at line 1424 (match +0.09%, OFFSET_SWAP 11→2)
10. **E11**: `(mFinale ? 16 : 0) + 8` ternary form at line 1258 (match +0.12%)

### Current metrics (93.34%)

- Match: **93.34%**
- Total instructions: **4171**
- `insert`: **44**
- `delete`: **64**
- `replace`: **185**
- `diff_op`: **3**
- `BOOL_MASK`: **3** (from concise `run_objdiff`)
- `REGISTER_SWAP`: **117** across 15 pairs
- `OFFSET_SWAP`: **2**
- `LINKER_MERGED`: **43** calls (unfixable)

## Before/After Mismatch Budget

| Metric         | Before (92.7%) | After (93.34%) | Delta  |
|----------------|----------------|----------------|--------|
| Match %        | 92.7%          | 93.34%         | +0.64% |
| Total instrs   | 4181           | 4171           | -10    |
| insert         | 54             | 44             | -10    |
| delete         | 81             | 64             | -17    |
| replace        | 184            | 185            | +1     |
| diff_op        | 5              | 3              | -2     |
| BOOL_MASK      | 2              | 3              | +1     |
| OFFSET_SWAP    | 11 (dom 0x80,0x84) | 2 (dom 0x4,0x8) | -9 |

## Remaining diff_ops (3)

1. **idx 1243**: `ble cr6` vs `bgt cr6` - branch direction for i6b4 max comparison. Tried 3 approaches (E02, E03, E04), all failed. Exhausted.
2. **idx 1369**: `blt cr6` vs `bne cr6` - condition form difference in outOfRange/endBeat area. Caused by TGT interleaving float check with bool computation. Deeply tied to scheduling.
3. **idx 3622**: `b` vs `bl Release@DataArray` - tail call optimization. Unfixable.

## Remaining Structural Issues

- **Swag jack section (1235-1415)**: Deep structural divergence in InTheZone call ordering and player pointer caching. TGT keeps player ptrs in registers, SRC reloads from struct. 20+ real replaces and I/D clusters here.
- **iPrev computation (958-970)**: Different bool materialization pattern. Exhausted per plan.
- **Extra ElapsedMs call (974-978)**: TGT has 5 extra instructions for a vtable call SRC doesn't emit.
- **Static init scheduling (1650-1800)**: Static Symbol address loading happens at different points relative to guard checks. Compiler scheduling, unfixable.
- **Register spills (2644-2657)**: SRC spills player pointers to stack, TGT keeps in registers. Register pressure difference.
- **Store ordering (3813-3819)**: DataNode initialization before vs after FocusPanel() call. Compiler scheduling.
- **lbz vs lwz for inMindControl (3089-3091)**: Stack layout treats bool as byte (TGT) vs word (SRC).

## Experiment Ledger Summary

| ID  | Region    | Change                                  | Result | Decision |
|-----|-----------|-----------------------------------------|--------|----------|
| E01 | 1235-1415 | Swap b43\|\|outOfRange order            | 92.7%, diff_op -2 | KEEP |
| E02 | 1235-1415 | Simplify i6b4 ternary                   | 92.42% | REVERT |
| E03 | 1235-1415 | if/else form for i6b4                   | 92.69% (neutral) | REVERT |
| E04 | 1235-1415 | unk124=(int)b36+5                       | 92.66% | REVERT |
| E05 | 930-1075  | JointPos call reorder                   | 93.0%  | KEEP |
| E06 | 3088-3290 | Remove (int) cast mFinale<<4            | 93.0% (neutral) | REVERT |
| E07 | 3088-3290 | Min→Max for Unk284                      | 93.0%, f0↔f13 -2 | KEEP |
| E08 | 3088-3290 | s14bc runtime assign                    | 93.12% | KEEP |
| E09 | 3628-3642 | Remove unk148=0x1c                      | 93.1%, diff_op +1 | REVERT |
| E10 | 3088+4160 | Split nextUnk148 (defer +i6cc)          | 93.22% | KEEP |
| E11 | 3088-3290 | mFinale ternary (mFinale?16:0)+8        | 93.34% | KEEP |

## Exhausted Levers (Do Not Retry)

### Phase 1 (Hotspot B 1235-1415)
- `unk124 = b36 ? 6 : 5` (no codegen effect, E03)
- `unk124 = (int)b36 + 5` (regression, E04)
- i6b4 ternary simplification (regression, E02)
- Hoisted `p2Leads` temporary bool (regression, prior session)
- Branch direction `ble/bgt` at idx 1243 (3 attempts, none fixed it)

### Phase 2 (Hotspot A 930-1075)
- `iPrev` ternary rewrite (exhausted per plan)
- Call order: SOLVED by E05 (JointPos reorder)

### Phase 3 (Late windows)
- `nextUnk148` full ternary rewrite (regression, prior session)
- Remove `(int)` cast on mFinale (no effect, E06)
- Remove `unk148 = 0x1c` (diff_op regression, E09)
- nextUnk148 expression split: SOLVED by E10/E11

## Final Recommendation

**Status: soft at-limit at 93.34%**

Significant progress from 92.7% → 93.34% (+0.64%), with 6 kept edits. The remaining mismatches are dominated by:

1. Register allocation differences (117 register swaps across 15 pairs)
2. Swag jack section structural divergence (deeply intertwined with register/scheduling decisions)
3. Two unfixable tail call optimizations
4. Compiler scheduling differences for static init and store ordering

A future session could explore:
- Restructuring the swag jack InTheZone call pattern to cache player pointers in local variables
- Investigating whether `int zone1/zone2` should use different types
- Trying different forms for the `outOfRange` bool computation
- Re-attempting `unk124` lever if stack layout changes enough to alter codegen context

However, diminishing returns are strong. Each remaining cluster involves deeply intertwined register allocation effects that are hard to predict or control from source.
