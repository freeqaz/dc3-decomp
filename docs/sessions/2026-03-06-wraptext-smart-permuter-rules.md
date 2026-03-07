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

## Planning Decisions

These decisions came out of review and should shape the next implementation:

- optimize first for **cross-function generalization**, not a one-off `WrapText` win
- keep Ghidra vs m2c trust **configurable** and validate it experimentally rather than hard-coding one oracle as globally superior
- add field/offset anchoring early, even if that expands extraction scope
- prefer a broader, more wasteful mutation space over an overly conservative one in the first iteration
- keep region matching as a clean abstraction in `region_match.py`, not ad hoc logic inside `constraint_solver.py`
- tier call-gate shaping:
  - start with anchored helper calls
  - expand to more generic call-site shells later
- use register-pressure estimates for **ranking and telemetry only**, not pruning
- treat "not knowing what to mutate" as the primary bottleneck; search-budget collapse is secondary for now
- allow source-shape rewrites freely in the matching phase; cleanup can happen afterward

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

## m2c As A Second Oracle

m2c is valuable here precisely because it is less polished.

For hard matching work, Ghidra often improves readability while m2c preserves more of the machine-shaped structure:

- explicit temporaries
- raw stack-slot groupings
- lower-level loop shells
- branch-and-call gating that is closer to emitted code

That makes the right split:

- **Ghidra** for semantic anchors
- **m2c** for structural anchors

### What m2c is good for

- call placement and call gating
- loop-entry / loop-exit form
- temp reuse and lifetime windows
- stack-backed scratch-region grouping
- identifying where source shape is too "clean" compared to the target

### What Ghidra is still better for

- function names
- field names and type-backed interpretation
- assert strings and semantic labels
- higher-level intent
- xrefs and whole-program context

### How to use both

Do not treat m2c as a fallback. Treat it as a second target-side view.

The permuter should compare:

1. source vs Ghidra
2. source vs m2c
3. Ghidra vs m2c

Then assign confidence:

- if Ghidra and m2c agree, emit a strong structural constraint
- if m2c alone gives the better call-shell view, prefer m2c for that site
- if Ghidra alone gives the better semantic anchor, prefer Ghidra for that site
- if they disagree sharply, lower confidence and avoid aggressive automation

## The Right Next Step: Region-Aware Guided Matching

The next smart layer should not be "more patterns first". It should be a better **alignment model** between target structure and source structure.

### Proposal: build a lightweight structural IR for source, Ghidra, and m2c

For each top-level statement and important nested block, extract:

- statement kind: declaration / if / loop / call / assignment / assert
- call anchors: `ParseMarkup`, `WordWrap_CanBreakLineAt`, `SegmentLength`, `MakeString`
- literal anchors: `30.0f`, `60.0f`, `1.0f`, `0x3c`, `'\n'`
- string anchors: assert expressions, file strings
- field/offset anchors: named member accesses when available, raw offsets when names are lost
- writes / reads summary
- side-effect summary: pure read / local write / unknown call / pointer escape
- pointer/field usage summary
- control tags: guard, conjunction, nested-if, call-under-guard
- allocation tags: `_alloca`, temp scratch buffer, vector reserve/erase

This should exist for:

- source AST via Tree-Sitter C++
- Ghidra decomp AST via Tree-Sitter C
- m2c decomp AST via Tree-Sitter C

Once that exists, use weighted similarity scoring and LCS-style alignment to match **regions**, not just nodes by position.

The important refinement is that region matching should be triaged through both decompilers:

- Ghidra supplies semantic labels and type-backed anchors
- m2c supplies code-shape hints, temp reuse, and lower-level control shells
- the matcher fuses them into one target-side region map with per-region confidence

This is also where the appended "offset-aware anchoring" idea fits. It makes sense and should be part of the base alignment layer, not a separate late-stage feature. Calls and strings will not exist in every function. Field offsets and access patterns will.

The dual-oracle policy should stay configurable:

- Ghidra-preferred mode
- m2c-preferred mode for structural shells
- fused mode with confidence weighting

That lets us run experiments instead of arguing from anecdotes.

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
   - alias / side-effect risk
3. align source blocks to target blocks using anchors
4. derive a conservative partial-order graph
5. emit only legal reorder variants that move source order toward target order

**Key point**: this should be smarter than existing `statement_reorder`.

`statement_reorder` is generic adjacency swapping. `ghidra_block_schedule` should operate on **multi-statement regions with anchor-based alignment**.

The appended DDG idea also makes sense here, with one narrowing: use a **lightweight dependence graph**, not an ambitious whole-function DDG. The goal is to produce topological sorts of obviously legal schedules inside an aligned window.

For the first pass, it is acceptable to be broad:

- allow more candidate schedules
- accept more compile-waste
- record legality and success telemetry so later heuristics can become stricter from data rather than intuition

**Implementation target**:

- add `extract_block_fingerprints()` to `ghidra_ast.py`
- add the same extraction for m2c output
- add matching logic in a neutral `region_match.py`
- add a legality helper that builds reorder windows plus a dependency DAG
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

This is also the rule where m2c should often be trusted more than Ghidra, because m2c tends to preserve the compiler-shaped shell around a call.

The appended CFG idea is useful if it stays small. We do not need full subgraph isomorphism. We need anchored path-shape summaries:

- how many guards dominate this call
- whether the call is on the true branch, false branch, or both
- whether the call is followed by a boolean materialization / test sequence

That is enough to guide `and_split` and `early_return_merge` without adding a heavyweight CFG solver.

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

m2c is likely the better source for this rule, because temp naming and stack-slot reuse expose pressure and lifetime more directly than Ghidra's cleaner pseudocode.

The appended register-pressure idea is directionally right, but it should start as a **soft scoring signal**, not a hard ABI-threshold prune. A lightweight liveness estimate can help rank variants, but the compiler's real register allocation is too nonlinear for "peak live vars > N" to be a safe early reject rule.

Important: keep the telemetry. We want to know whether pressure score actually predicts match improvements, spill behavior, or compile failures before using it for pruning.

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
- resolved field names or stable field offsets: very high weight
- float constants: medium weight
- character constants: medium weight
- operator/control shape: medium weight
- read/write sets: medium weight

Then run a best-match / sequence alignment pass to pair source regions with Ghidra regions.

This is not a user-visible permuter pattern. It is infrastructure that makes all later guided rules reliable.

For this rule, m2c contributes anchor classes that Ghidra often hides:

- temp families like `var_r*`, `temp_f*`
- stack-slot runs
- compare/call/test idioms

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

- compare loop shell form in source vs Ghidra/m2c
- emit only shell-preserving conversions relevant to the matched region

Examples:

- `for (;;) { ... }` -> `do { ... } while (true);`
- pre-read `while (c != 0)` -> `for (;;) { c = *s; if (c == 0) break; ... }`
- move `cur++`/`cCount++`/`curBrk++` between tail and header forms

This should stay constrained to regions where alignment says the loop corresponds.

m2c should be the primary loop-shell classifier here, with Ghidra acting as a semantic cross-check.

### 7. `m2c_temp_pack`

**Problem it solves**: some functions are blocked not by declaration order alone, but by how source temporaries are grouped, split, and reused across calls.

**Why this is m2c-driven**:

m2c exposes temporary clusters and stack-slot families more concretely than Ghidra. That gives us a way to infer when the target likely wants:

- one reused temp instead of two
- two shorter-lived temps instead of one long-lived temp
- a literal hoisted into a temp
- a temp introduced closer to first use

**What it should do**:

1. cluster m2c temporaries by region and use distance
2. compare those clusters to source locals in the aligned region
3. emit only targeted transforms:
   - split a temp
   - merge adjacent temps
   - move temp introduction
   - inline a trivial temp
   - hoist a repeated literal or address

## Review Of The Appended Ideas

The appended ideas are mostly good, but they need to be sorted into:

- core architecture we should adopt now
- useful heuristics that should stay soft
- expensive ideas that should be deferred or narrowed

### Keep and integrate now

- offset-aware anchoring
- conservative dependence graphs for block scheduling
- region-focused pruning once region IDs are stable
- configurable Ghidra/m2c fusion with confidence scoring

These directly strengthen the region-matching plan and reduce wasted variants without changing the basic architecture.

### Keep, but narrow

- register-pressure estimation
- CFG-aware control-flow matching

Both are useful as guidance signals, but not as rigid proof systems. They should rank or suggest edits, not become mandatory gates in the first implementation.

### Defer or keep as diagnostics only

- full CFG subgraph isomorphism
- automatic inline-boundary synthesis

These are too expensive or too source-invasive for the first serious version. Inline and macro boundaries can matter, but inventing dummy inline wrappers is not a good default decomp workflow. At most, boundary detection should explain why a region is stubborn or highlight a candidate for manual cleanup.

## Design Principle: Move From Local Patterns To Targeted Region Edits

The permuter already has many useful local transforms. The missing piece is choosing **where** to apply them.

The right architecture is:

1. extract source regions
2. extract Ghidra regions
3. extract m2c regions
4. align them by anchors and region order
5. explain the delta in terms of:
   - order
   - call shell
   - loop shell
   - lifetime window
   - declaration order
   - temp packing
6. emit only transforms justified by that explanation

This is the difference between:

- "try 40 transforms because the function has branch diffs"
- "the `WordWrap_CanBreakLineAt` site in source is a compact boolean, but the aligned target site is an explicit gated call, so emit 2 variants only"

## Additional Infrastructure Beyond Individual Rules

### Conservative legality graph for reordering

Block scheduling needs a reusable legality layer:

- derive def-use and write-after-read hazards inside a candidate window
- treat unknown calls and pointer alias escapes as schedule barriers
- generate only topological sorts that respect those barriers

This is the practical version of the appended DDG idea.

### Active-region focus and progressive pinning

Combination explosion is real, but pinning should come **after** alignment and region IDs are reliable.

The useful version is:

- mark regions with strong source-target agreement as inactive
- spend variant budget on the remaining mismatched windows
- allow pinning at region granularity first, not arbitrary AST-node granularity

This is safer than immediately trying to freeze exact AST nodes based on noisy line mappings.

It is also explicitly lower priority than better mutation targeting. If the permuter still cannot identify the right region and the right edit family, pinning just helps it fail faster.

### Heuristic scoring signals

The solver should eventually score candidates using:

- alignment confidence
- estimated lifetime pressure
- branch-path similarity at anchored call sites
- spill suspicion from m2c or asm hints

These should bias search order and pruning, but only after the basic region edits are working.

## How To Integrate This With Existing Code

### Phase 1: Add richer extraction and alignment

Add to `ghidra_ast.py`:

- `extract_block_fingerprints(ast)`
- `extract_call_gate_sites(ast)`
- `extract_loop_shells(ast)`
- `extract_assert_anchors(ast)`
- `extract_field_offset_anchors(ast)`
- `extract_live_ranges(ast)` or a lighter first/last-use approximation

Add parallel extraction for m2c output.

Add new module:

- `region_match.py`

Responsibilities:

- build source-side fingerprints
- align source, Ghidra, and m2c regions
- attach confidence scores to matches
- support configurable oracle-combination policies for experiments
- provide stable region IDs for patterns and the constraint solver

### Phase 2: Add legality and structural constraints

Extend `ConstraintSet` with:

- `region_order_constraints`
- `call_gate_constraints`
- `loop_shell_constraints`
- `lifetime_constraints`
- `assert_anchor_map`
- `temp_pack_constraints`
- `oracle_confidence`

Add a legality helper used by scheduling-oriented edits:

- reorder windows
- dependency DAG
- barrier classification for calls / aliasing

This lets `constraint_solver.py` emit deterministic edits for structural moves rather than only decl-order and sign choices.

### Phase 3: Teach existing patterns to consume region constraints

Do not immediately write six new standalone patterns.

Instead:

- let `statement_reorder` accept aligned region move hints plus legality windows
- let `and_split` / `early_return_merge` accept call-gate hints
- let `declaration_reorder` / `temp_elimination` / `reference_elimination` accept lifetime hints
- let `prologue_pressure` consume scope and pressure hints
- let `fma_reorder` keep using expression guidance

Then add genuinely new patterns only where existing ones cannot express the edit.

### Phase 4: Add search control, not just more rewrites

Once region matching is stable:

- bias search toward mismatched regions only
- keep solved regions inactive unless a later edit overlaps them
- use heuristic scores to rank promising variants before full compile/test

This is where progressive pinning belongs.

But it should remain behind the mutation-targeting work in priority.

### Phase 5: Validate on a small "hard but fixable" suite

Recommended validation set:

1. `RndText::WrapText`
2. `SaveLoadManager::Poll`
3. `ContentLoadingPanel::Poll`
4. `RhythmBattle::OnBeat`
5. one known FMA case (`CalcSpline` or `InterpTangent`)
6. at least one additional large, reg-cascade-heavy function discovered during triage
7. one regswap-heavy medium function that already benefits from declaration guidance

Metrics:

- average variants generated per round
- hit rate of guided variants vs blind variants
- compile success rate of scheduled variants
- whether large insert/delete cluster pairs shrink
- whether branch/call ordering diffs disappear at aligned call sites
- whether dual-oracle guidance beats Ghidra-only guidance
- whether m2c-preferred, Ghidra-preferred, or fused mode wins on each function family
- whether pressure score correlates with better or worse outcomes

## Why `WrapText` Points To Region Matching, Not Just More Rules

The most important lesson from this function is that the next ceiling is not "we need one more clever rewrite". It is:

**the permuter needs to understand that a hard function is made of regions, and that Ghidra is useful mainly because it tells us how those regions are ordered and shaped in the target.**

`WrapText` is a good automation target because it exposes all of the missing layers at once:

- region reorder
- call-gate shaping
- loop-shell shaping
- lifetime pressure
- semantic anchors via asserts and helper calls
- field/offset anchors for less expressive functions

If we solve those well here, the same machinery should generalize to a large class of 50-90% functions that are currently too expensive to fix manually.

## Recommended Next Implementation Order

1. Build `region_match.py` as a clean abstraction and prove it can align the `WrapText` setup region.
2. Add field/offset anchors so the matcher still works on low-string, low-call functions.
3. Make oracle fusion configurable so Ghidra-preferred, m2c-preferred, and fused modes can be compared empirically.
4. Implement tier-1 call-site anchored `ghidra_call_gate_shape` for distinctive helper calls first.
5. Add legality windows plus dependency DAG support for region reordering.
6. Add `region_order_constraints` to `ConstraintSet`.
7. Teach `statement_reorder` to consume region moves.
8. Add `ghidra_scope_window` only after region alignment exists.
9. Add m2c-backed `temp_pack_constraints` after lifetime extraction is stable.
10. Add ranking-only pressure telemetry.
11. Add progressive pinning only after region IDs and mismatch localization are trustworthy.

That order matters. Without region alignment, the later rules stay brittle and positional. Without legality windows, block scheduling becomes a compile-waste machine. Without stable region IDs, pinning will freeze the wrong things.

## Bottom Line

The permuter already knows how to rewrite syntax. The next gain is teaching it **where** and **why** to rewrite.

`WrapText` shows that Ghidra + Tree-Sitter + m2c can supply that missing information if we stop treating Ghidra as a bag of tags and start treating the decompilers together as a **target-side structural map**.
