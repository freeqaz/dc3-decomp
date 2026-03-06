# WrapText-Inspired Smart Permuter Rules

**Date**: 2026-03-06
**Primary case study**: `RndText::WrapText`
**Goal**: extract automation ideas from a hard, still-fixable function and turn them into reusable Ghidra + Tree-Sitter permuter infrastructure.

## Why This Function Matters

`WrapText` is the right kind of hard:

- the algorithm is already recovered
- the remaining gap is mostly source-shape and compiler-scheduling noise
- the mismatch is too large for blind local rewrites
- the target structure is visible in Ghidra

That makes it a good design target for the next permuter generation. The function is not blocked by missing semantics. It is blocked by the permuter not yet understanding **region order, call-gating shape, and lifetime/scheduling intent**.

## Validated Observations From `WrapText`

The last successful measured baseline on this branch was about **58.3%**. The dominant mismatch pattern is not random noise:

- a **55-insert** cluster and a **52-delete** cluster appear in the same early/mid region
- the remaining `diff_op` set is concentrated around the `WordWrap_CanBreakLineAt` gate
- there are many regswaps, but they look downstream of the structural mismatch rather than primary

The Ghidra decomp from `run_analyze_function` confirms the key source-shape gap:

```c
if (pRVar6->mMarkup != false) {
    /* strip markup loop */
}
dVar38 = 30.0;
dVar39 = 60.0;
local_158 = (uint *)0x8209bd8c;
dVar43 = 1.0;
local_164 = "numWp < wLen + 1";
local_168 = "bestWp != -1";
local_15c = "lineLen >= bestLineLen";
do {
    ...
}
```

That is the core insight: in the target, the compiler emits:

1. scratch-buffer setup
2. markup stripping
3. constant/assert materialization
4. main loop

In our source attempts, those regions tend to land in a different order.

Ghidra also confirms the branch shape around the break gate:

```c
if ((uVar1 == 0x3c) && (pRVar6->mMarkup != false)) {
    puVar7 = ParseMarkup(...);
    ...
}
...
if (temp_r23 != 0) {
    if (var_r27 <= 0) {
        var_r11_2 = 0;
    } else {
        temp_r3_3 = WordWrap_CanBreakLineAt(...);
        var_r11_2 = ...
    }
    if (var_r11_2 != 0) {
        ...
    }
}
```

That validates two more things:

- target prefers an explicit gated call shape over a compact boolean expression
- call placement and branch ordering matter as much as the final condition itself

## What The Current Guided Permuter Still Misses

The repo already has good first-generation Ghidra guidance:

- `ghidra_ast.py`
- `ghidra_expr_match.py`
- `ghidra_var_match.py`
- `ghidra_preflight.py`
- Ghidra-guided paths in `declaration_reorder`, `fma_reorder`, `early_return_merge`, `and_split`, `null_guard_elimination`

That is useful, but it is still too shallow for functions like `WrapText`.

### Current limitations

1. **Expression matching is positional**
   - `compare_arithmetic_expressions()` matches the i-th source expression against the i-th Ghidra expression.
   - This works for small math functions, but breaks when blocks reorder.

2. **Control-flow matching is tag-only**
   - `extract_condition_structure()` tells us that Ghidra contains `"conjunction"` or `"guard_return"`.
   - It does not tell us **which call site** or **which region** the tag belongs to.

3. **Declaration guidance is still first-use only**
   - useful for regswaps
   - not enough for functions where the real problem is "this whole setup block is in the wrong place"

4. **Preflight only detects red flags**
   - it can skip obviously bad functions
   - it does not yet derive a positive "do this exact structural move" recommendation

5. **The constraint solver has no region model**
   - `ConstraintSet` knows about decl order, condition tags, sign choices
   - it does not know about "block X should move after block Y"

## The Right Next Step: Region-Aware Guided Matching

The next smart layer should not be "more patterns first". It should be a better **alignment model** between target structure and source structure.

### Proposal: build a lightweight structural IR for both source and Ghidra

For each top-level statement and important nested block, extract:

- statement kind: declaration / if / loop / call / assignment / assert
- call anchors: `ParseMarkup`, `WordWrap_CanBreakLineAt`, `SegmentLength`, `MakeString`
- literal anchors: `30.0f`, `60.0f`, `1.0f`, `0x3c`, `'\n'`
- string anchors: assert expressions, file strings
- writes / reads summary
- pointer/field usage summary
- control tags: guard, conjunction, nested-if, call-under-guard
- allocation tags: `_alloca`, temp scratch buffer, vector reserve/erase

This should exist for:

- source AST via Tree-Sitter C++
- Ghidra decomp AST via Tree-Sitter C

Once that exists, use similarity scoring and LCS-style alignment to match **regions**, not just nodes by position.

## New Rule Ideas

These are the concrete rule families `WrapText` suggests.

### 1. `ghidra_block_schedule`

**Problem it solves**: same work exists on both sides, but semantically independent setup blocks are emitted in different order.

**WrapText evidence**:

- markup stripping is followed by constant/assert loads in target
- our source variants tend to interleave those regions differently
- the 55I/52D paired cluster is exactly what block movement looks like in objdiff

**What it should do**:

1. find reorderable block candidates inside a function region
2. summarize each block's dependencies:
   - reads
   - writes
   - calls
   - allocas
3. align source blocks to target blocks using anchors
4. derive a partial-order target graph
5. emit only legal reorder variants that move source order toward target order

**Key point**: this should be smarter than existing `statement_reorder`.

`statement_reorder` is generic adjacency swapping. `ghidra_block_schedule` should operate on **multi-statement regions with anchor-based alignment**.

**Implementation target**:

- add `extract_block_fingerprints()` to `ghidra_ast.py`
- add matching logic in a new `ghidra_region_match.py`
- add a pattern or constraint-solver edit category for block moves

### 2. `ghidra_call_gate_shape`

**Problem it solves**: target and source agree on the condition, but disagree on how the compiler reaches the call.

**WrapText evidence**:

- `WordWrap_CanBreakLineAt` is behind an explicit nested `if (cCount <= 0) false else call`
- `ParseMarkup` is gated by `(ch == '<') && mMarkup`
- the current mismatch includes target-branch/source-call and source-branch/target-call swaps around that site

**What it should do**:

For a specific anchored call site:

1. inspect the control shell around the call in source and Ghidra
2. classify source and target into forms:
   - direct `&&`
   - explicit `if/else` temp bool
   - nested `if`
   - guard + call + test
3. emit only the transforms that move source toward target

**General transforms**:

- `a && call()` <-> `if (a) x = call(); else x = false;`
- `if (a) if (b) call()` <-> `if (a && b) call()`
- `if (ch == '<' && flag)` <-> `if (flag) { if (ch == '<') ... }`

This is more precise than current branch-polarity or `and_split` guidance because it is **call-site anchored**.

### 3. `ghidra_scope_window`

**Problem it solves**: variable lifetime and scope shape change register pressure and scheduling, especially in medium-large functions.

**WrapText evidence**:

- scratch-buffer locals for markup stripping are natural scope-isolation candidates
- the function saves a large callee-saved set
- many regswaps are likely caused by earlier lifetime choices rather than declaration order alone

**What it should do**:

1. compute first-use and last-use windows for source locals
2. approximate first/last-use windows for Ghidra locals or anchored values
3. detect values that appear to live too long in source
4. generate edits such as:
   - introduce inner braces
   - extract a temp later
   - inline a temp earlier
   - split one temp into two shorter-lived temps

This is the missing bridge between:

- `declaration_reorder`
- `temp_elimination`
- `reference_elimination`
- `prologue_pressure`

Today those all act locally. This rule would make them target-directed.

### 4. `ghidra_anchor_alignment`

**Problem it solves**: all other guided rules need stable source-target correspondence, and positional matching is too brittle.

**WrapText evidence**:

- repeated `SegmentLength` calls
- repeated assert strings
- float literals `30.0`, `60.0`, `1.0`
- distinctive helper calls `ParseMarkup`, `WordWrap_CanBreakLineAt`
- alloca scratch regions

This function has enough anchors that alignment should be solved semantically, not by ordinal position.

**What it should do**:

Build a per-region fingerprint using weighted anchors:

- unique call names: very high weight
- unique strings: very high weight
- float constants: medium weight
- character constants: medium weight
- operator/control shape: medium weight
- read/write sets: medium weight

Then run a best-match / sequence alignment pass to pair source regions with Ghidra regions.

This is not a user-visible permuter pattern. It is infrastructure that makes all later guided rules reliable.

### 5. `ghidra_assert_anchor`

**Problem it solves**: assert-related code is often a good alignment marker even when `MakeString` names are noisy.

**WrapText evidence**:

- `bestWp != -1`
- `numWp < wLen + 1`
- `lineLen >= bestLineLen`
- `style.brk == false`

These strings are semantically unique and appear in a meaningful order near the real mismatch.

**What it should do**:

- treat assert expression strings as region anchors
- align source assert sites to Ghidra assert sites
- use them to infer region order and branch ownership

Important: this is **not** about chasing `MakeString` mangled names. It is about using asserts as semantic beacons.

### 6. `ghidra_loop_shell`

**Problem it solves**: `for (;;)`, `do { } while (true)`, pre-read `while`, and post-increment loop forms often produce the same semantics but different scheduling.

**WrapText evidence**:

- target main loop decompiles as `do { ... }`
- markup stripping loop shape is structurally important
- the function contains both a preprocessing loop and a main walk loop

**What it should do**:

At a matched loop region:

- compare loop shell form in source vs Ghidra
- emit only shell-preserving conversions relevant to the matched region

Examples:

- `for (;;) { ... }` -> `do { ... } while (true);`
- pre-read `while (c != 0)` -> `for (;;) { c = *s; if (c == 0) break; ... }`
- move `cur++`/`cCount++`/`curBrk++` between tail and header forms

This should stay constrained to regions where Ghidra alignment says the loop corresponds.

## Design Principle: Move From Local Patterns To Targeted Region Edits

The permuter already has many useful local transforms. The missing piece is choosing **where** to apply them.

The right architecture is:

1. extract source regions
2. extract Ghidra regions
3. align them by anchors
4. explain the delta in terms of:
   - order
   - call shell
   - loop shell
   - lifetime window
   - declaration order
5. emit only transforms justified by that explanation

This is the difference between:

- "try 40 transforms because the function has branch diffs"
- "the `WordWrap_CanBreakLineAt` site in source is a compact boolean, but the aligned target site is an explicit gated call, so emit 2 variants only"

## How To Integrate This With Existing Code

### Phase 1: Add richer extraction, not new patterns first

Add to `ghidra_ast.py`:

- `extract_block_fingerprints(ast)`
- `extract_call_gate_sites(ast)`
- `extract_loop_shells(ast)`
- `extract_assert_anchors(ast)`
- `extract_live_ranges(ast)` or a lighter first/last-use approximation

Add new module:

- `ghidra_region_match.py`

Responsibilities:

- build source-side fingerprints
- align source and Ghidra regions
- provide stable region IDs for patterns and the constraint solver

### Phase 2: Upgrade the constraint model

Extend `ConstraintSet` with:

- `region_order_constraints`
- `call_gate_constraints`
- `loop_shell_constraints`
- `lifetime_constraints`
- `assert_anchor_map`

This lets `constraint_solver.py` emit deterministic edits for structural moves rather than only decl-order and sign choices.

### Phase 3: Teach existing patterns to consume region constraints

Do not immediately write six new standalone patterns.

Instead:

- let `statement_reorder` accept aligned region move hints
- let `and_split` / `early_return_merge` accept call-gate hints
- let `declaration_reorder` / `temp_elimination` / `reference_elimination` accept lifetime hints
- let `fma_reorder` keep using expression guidance

Then add genuinely new patterns only where existing ones cannot express the edit.

### Phase 4: Validate on a small "hard but fixable" suite

Recommended validation set:

1. `RndText::WrapText`
2. `SaveLoadManager::Poll`
3. `ContentLoadingPanel::Poll`
4. one known FMA case (`CalcSpline` or `InterpTangent`)
5. one regswap-heavy medium function that already benefits from declaration guidance

Metrics:

- average variants generated per round
- hit rate of guided variants vs blind variants
- whether large insert/delete cluster pairs shrink
- whether branch/call ordering diffs disappear at aligned call sites

## Why `WrapText` Points To Region Matching, Not Just More Rules

The most important lesson from this function is that the next ceiling is not "we need one more clever rewrite". It is:

**the permuter needs to understand that a hard function is made of regions, and that Ghidra is useful mainly because it tells us how those regions are ordered and shaped in the target.**

`WrapText` is a good automation target because it exposes all of the missing layers at once:

- region reorder
- call-gate shaping
- loop-shell shaping
- lifetime pressure
- semantic anchors via asserts and helper calls

If we solve those well here, the same machinery should generalize to a large class of 50-90% functions that are currently too expensive to fix manually.

## Recommended Next Implementation Order

1. Build `ghidra_region_match.py` and prove it can align the `WrapText` setup region.
2. Implement `ghidra_call_gate_shape` for anchored call sites.
3. Add `region_order_constraints` to `ConstraintSet`.
4. Teach `statement_reorder` to consume region moves.
5. Add `ghidra_scope_window` only after region alignment exists.

That order matters. Without region alignment, the later rules stay brittle and positional.

## Bottom Line

The permuter already knows how to rewrite syntax. The next gain is teaching it **where** and **why** to rewrite.

`WrapText` shows that Ghidra + Tree-Sitter can supply that missing information if we stop treating Ghidra as a bag of tags and start treating it as a **target-side structural map**.

## Advanced Architectural Enhancements

Building upon the foundation of semantic, region-based alignment, here are several advanced ideas to further turbocharge the smart permuter:

### 1. Advanced Alignment: Type & Offset-Aware Anchoring
The current proposal relies on function calls, literals, and strings as anchors. In many functions (especially getters, setters, or math-heavy routines), these are scarce. 
* **The Idea:** Extend `ghidra_anchor_alignment` to use **struct offsets and memory access patterns** as anchors.
* **How it works:** 
  * Ghidra decompilation frequently exposes raw memory offsets (e.g., `*(int*)(this + 0x48)` or `this->field_48`). 
  * Using the project's DWARF data or struct headers, the permuter can resolve Tree-Sitter AST member accesses (e.g., `pRVar6->mMarkup`) to their byte offsets.
  * This creates a massive new class of semantic anchors. If both source and Ghidra perform a read at `offset 0x48` and a write at `offset 0x14`, you can confidently align those regions even if no strings or calls exist.

### 2. Hard Constraints via Data Dependency Graphs (DDG)
The proposal mentions summarizing reads/writes for `ghidra_block_schedule`. This can be formalized into a strict mathematical model to prevent generating broken code.
* **The Idea:** Build a lightweight **Data Dependency Graph (DDG)** for the variables within an aligned region.
* **How it works:** 
  * Before generating block reordering variants, map out strict Def-Use chains (e.g., Block B reads `var_x` which is written by Block A).
  * This forms a Directed Acyclic Graph (DAG) of mandatory execution order. 
  * Instead of generating permutations and filtering them, the permuter only generates valid **topological sorts** of this DAG, weighted by their similarity to Ghidra's sequence. This guarantees that 100% of generated variants are semantically legal C++, drastically reducing compilation waste.

### 3. Execution Pruning: Progressive AST Pinning ("Lock-In")
Large functions like `WrapText` suffer from combination explosion if you permute everything at once.
* **The Idea:** Introduce a stateful "Pinning" mechanism that locks down portions of the AST that are already producing perfectly matching assembly.
* **How it works:**
  * When `run_objdiff` runs, it generates a mismatch table. 
  * If a contiguous block of source lines (via DWARF line-number mapping) results in 0 mismatches, the permuter tags those Tree-Sitter AST nodes as `@Pinned`.
  * In subsequent permuter rounds, the Constraint Solver treats `@Pinned` regions as immutable scaffolding. The permuter only focuses its combination budget on the "unpinned" gaps. This acts like a binary search or progressive slice, collapsing the search space as progress is made.

### 4. Target-Specific Heuristics: Register Pressure Simulation
`ghidra_scope_window` aims to fix register swaps and scheduling by modifying variable lifetimes. However, guessing lifetimes is expensive.
* **The Idea:** Implement a fast, AST-level **Register Pressure Estimator** specifically tuned for the target architecture (PowerPC).
* **How it works:**
  * As the permuter proposes a lifetime split or block move, it runs a quick liveness pass over the proposed AST region.
  * It counts the peak concurrent live variables. If the peak exceeds the known volatile register limits of the PowerPC ABI (e.g., >8 integer registers, >13 float registers), it knows the compiler will likely be forced to spill to the stack.
  * If the target Ghidra region does *not* contain stack spills (`stw` / `lwz` to the local frame), the permuter instantly discards the variant without compiling it. This provides a quantitative score to guide `ghidra_scope_window`.

### 5. Control Flow Graph (CFG) Subgraph Isomorphism
The `ghidra_call_gate_shape` rule is excellent for localized gates, but nested `if/else` structures often get mangled in Ghidra decompilation compared to source.
* **The Idea:** Move beyond AST matching for control flow and use simplified CFG path matching.
* **How it works:**
  * Extract a basic CFG from the source Tree-Sitter and the Ghidra target.
  * Calculate the Dominator Tree or simply count the independent paths to an anchor (e.g., "There are 3 unique branches that reach `ParseMarkup`").
  * If the source has 2 paths and Ghidra has 3 paths, the permuter knows it needs to apply an `and_split` or `early_return_merge` specifically to duplicate or merge a path. This provides a mathematical target for branch restructuring.

### 6. Macro & Inline Boundary Detection
A major source of structural mismatch is when the original code used an inline function or macro, but our decompiled source manually writes out the statements (or vice versa).
* **The Idea:** Add an `inline_boundary_recognition` pass to the extraction phase.
* **How it works:**
  * If the permuter detects a recurring cluster of AST nodes (e.g., a specific math operation or validation check) that perfectly aligns with a discrete block in Ghidra, it can suggest wrapping that block in a dummy inline function to force the compiler's scheduler to isolate it.
  * Conversely, if Ghidra shows a flat block of code but our source has it inside an inline function, the permuter can temporarily "flatten" (un-inline) the Tree-Sitter AST to see if removing the inline boundary fixes the scheduling mismatch.

### Integration into Implementation Plan

These ideas map cleanly into the proposed phases:
* **Phase 1 (Extraction):** Add offset-extraction (Idea 1) and inline-boundary detection (Idea 6) to `ghidra_ast.py`.
* **Phase 2 (Constraint Model):** Add the DDG builder (Idea 2) to `ConstraintSet` to enforce strictly legal C++ variants, and add the Register Pressure Estimator (Idea 4).
* **Phase 3/4 (Execution):** Implement Progressive Pinning (Idea 3) in the core orchestrator loop, so as `WrapText` gets closer to 100%, the permuter natively speeds up by ignoring the solved regions.
