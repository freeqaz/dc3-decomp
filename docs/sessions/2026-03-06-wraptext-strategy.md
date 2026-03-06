# RndText::WrapText Strategy Review

**Date**: 2026-03-06
**Symbol**: `?WrapText@RndText@@QAAXPBGHPAMAAV?$vector@VLine@RndText@@V?$StlNodeAlloc@VLine@RndText@@@stlpmtx_std@@@stlpmtx_std@@AAVRect@Hmx@@M@Z`
**File**: `src/system/rndobj/Text.cpp`
**Last successful measured match**: **58.3% normalized / 58.1% raw**

## Current Snapshot

- The last successful `objdiff` run on this branch still points to the same broad picture:
  - `229 diff_arg`
  - `45 replace`
  - `121 insert`
  - `118 delete`
  - `13 diff_op`
- The biggest live structural gap is still the paired insert/delete cluster:
  - `140-199`: **55 inserts**
  - `213-268`: **52 deletes**
- Those two clusters are still more important than the remaining branch flips and register swaps.

## Important Caveat

The current working tree no longer builds cleanly, so the 58.3% number is the last trustworthy baseline, not the status of the latest file contents.

Current build failure:

```text
Text.cpp(172) : error C2065: 'mActive' : undeclared identifier
```

That is consistent with an in-progress rename in `Text.cpp`: `StyleState` still exposes `brk` in `Text.h`, while newer source edits are trying to use `mActive`.

Practical consequence: **do not do more permuter or control-flow work until the function is back to a buildable baseline**. Otherwise every later conclusion is contaminated by an invalid source snapshot.

## What Still Looks Correct

### 1. The main problem is structural scheduling, not algorithm recovery

The function is already decompiled well enough to express the real algorithm. The remaining gap is mostly about **where the compiler decides to place setup work**, especially:

- `WrapPoint[0]` initialization
- `activeMarkup` / `markupCount` / `wLen` setup
- `minW` / `goodW` loading
- markup stripping
- constant/address materialization for asserts and penalty math

The large insert/delete pair strongly suggests the source and target still contain the same work, but in different block order.

### 2. Control flow around `WordWrap_CanBreakLineAt` is still a good target

The actionable `diff_op` set still includes the mid-function branch/call ordering around the break-check path:

- idx `288`: `bgt` vs `ble`
- idx `290`: target branches, source calls
- idx `293`: target calls, source branches

That is exactly the kind of mismatch caused by:

- `cCount > 0` vs `cCount >= 1`
- materializing `canBrk` with `if/else` vs `&&`
- slightly different nesting around `if (activeMarkup)`

This remains worth attacking after the build is restored.

### 3. Register noise is real, but it is downstream

The function still shows heavy register churn:

- `r10 <-> r11` dominates and is mostly volatile noise
- several callee-saved swaps exist, but they are likely consequences of earlier structure/lifetime choices

That means declaration reorder by itself is unlikely to be the best first move. Fix structure first, then re-measure whether any remaining callee-saved swaps are worth pressure tuning.

## What Does *Not* Look Like A Good Primary Bet

### 1. Writing a new `block_reorder` permuter immediately

This repo already has relevant machinery:

- `statement_reorder`
- `assignment_reorder`
- `branch_polarity`
- `comparison_flip`
- `prologue_pressure`
- `float_literal_pressure`

Before inventing a dedicated `block_reorder` pattern, it makes more sense to:

1. restore a clean baseline
2. try one or two manual source reorderings
3. use the existing reorder patterns against that stable baseline

If the same 55/52 cluster pair survives those attempts, then a custom targeted pattern is justified. Not before.

### 2. Chasing assert string lengths as a primary fix path

There are still `MakeString` template-name differences in the function-call diff, but this needs to be interpreted carefully.

- Repo docs already note that **MakeString array-size ICF noise is normalized in objdiff**
- So raw array-length/template-name differences are no longer a good reason by themselves to rewrite asserts

Assert spelling still matters when it changes real codegen, but "fix the mangled `MakeString` names" is no longer a strong standalone strategy.

### 3. Declaration reorder on its own

With this much callee-saved and volatile churn, pure declaration reshuffling is a low-ROI move unless a later diagnosis shows a small set of stable callee-saved swaps after structural fixes land.

## Refined Plan

### Phase 0: Re-establish a buildable baseline

Before anything else:

1. restore `WrapText` / `StyleState` to a compiling state
2. rerun `objdiff`
3. save that result as the new baseline

If the branch is still around ~58%, the old structural diagnosis is still valid. If it moved materially, re-diagnose before continuing.

### Phase 1: Attack the big cluster pair first

Use manual edits first, not a new pattern.

Best candidates:

- swap the order of `WrapPoint[0]` init vs `activeMarkup` / `markupCount` / `wLen`
- test whether `wLen` really belongs before or after `markupCount`
- move `minW` / `goodW` relative to the markup-strip block
- preserve semantics but try a separate inner scope for the stripping locals only if it helps scheduling

Success criterion: shrink the `140-199` and `213-268` clusters. If those do not move, the edit did not hit the real issue.

### Phase 2: Fix the break-check branch shape

Once Phase 1 stabilizes:

- prefer explicit `if/else` materialization of `canBrk` over a compact `&&`
- try `cCount > 0` instead of `cCount >= 1`
- try `do { } while (true)` if the loop entry still disagrees
- re-test the `mMarkup` gate shape only after the structural ordering is settled

Success criterion: the `288/290/293` branch-call mismatch should move or disappear.

### Phase 3: Only then touch register pressure

If the structure improves but the function is still stuck with callee-saved swaps:

- try `prologue_pressure`
- try narrower scope/lifetime changes
- consider `float_literal_pressure` only if the re-run diagnosis shows a real GPR/FPR pressure mismatch

## Recommended Working Order

1. Fix the current compile break.
2. Re-baseline with `objdiff`.
3. Do one isolated manual block-order experiment.
4. Re-run `objdiff` immediately.
5. Only after a stable baseline exists, try existing permuter reorder patterns.
6. Defer custom permuter work until the large early/mid clusters prove stubborn.

## Bottom Line

The original note was directionally right about the biggest issue: **this function still looks bottlenecked by block ordering and branch shape, not by missing logic**.

The main refinement is prioritization:

- **Yes**: structural reorder first
- **Yes**: break-check control flow second
- **Maybe later**: scope / pressure tuning
- **Not yet**: a new custom `block_reorder` pattern
- **Not primary**: assert-string / `MakeString` chasing

If this were my next pass, I would spend the next iteration on **restoring a clean 58.3% baseline and then manually reordering the pre-loop setup/markup-strip region**, because that is still the highest-leverage place to move the diff.
