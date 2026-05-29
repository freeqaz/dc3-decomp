# Header Inline Tail-Call Reorder — Design Document

**Date**: 2026-03-07
**Status**: Design phase — not yet implemented

## Background

### The Proven Win

The `Multiply(Transform, Matrix3, Transform)` inline function in `Mtx.h` contained two independent calls:

```cpp
inline void Multiply(const Transform &t, const Hmx::Matrix3 &m, Transform &out) {
    Multiply(t.m, m, out.m);  // matrix multiply
    Multiply(t.v, m, out.v);  // vector multiply
}
```

Swapping these two lines produced a **65% to 100% match**:

```cpp
inline void Multiply(const Transform &t, const Hmx::Matrix3 &m, Transform &out) {
    Multiply(t.v, m, out.v);  // vector multiply FIRST
    Multiply(t.m, m, out.m);  // matrix multiply becomes tail call
}
```

**Why it works**: MSVC PPC can tail-call the last `bl` in a void function, emitting `b` (branch) instead of `bl` (branch-and-link) + epilogue. This eliminates the entire prologue/epilogue — callee-saved register saves, `mflr`/`mtlr`, stack frame setup — when the last call can reuse the caller's stack frame. The second call's arguments must be settable from volatile registers (r3-r12) without needing callee-saved registers that would require saving.

### Phase 1 (Completed)

`scripts/permuter/patterns/tail_call_reorder.py` handles the simple case: swapping consecutive call statements in the function's own `.cpp` body. It correctly identifies candidates via prologue mismatch diagnosis but finds few wins because most tail-call opportunities are in **inline functions defined in headers**.

### The Real Opportunity

Inline functions in headers are instantiated at every call site. A single header fix can improve **many functions across many units** simultaneously. This is both the opportunity and the danger.

## Problem Statement

Build a standalone tool that:
1. Identifies inline functions in headers where call reordering could enable tail-call optimization
2. Generates header variants with swapped call orders
3. Scores each variant against ALL affected .obj files (not just one)
4. Reports net impact: how many functions improved, how many regressed, overall delta

## Architecture

### Tool: `scripts/permuter/header_tail_call.py`

Standalone CLI, not integrated into `scan_and_permute`. Reasons:
- Header changes have cross-unit blast radius
- Must run single-threaded (one header change at a time)
- Scoring requires multi-symbol objdiff (rebuild many .obj files per variant)
- Different risk profile than single-function permutation

### Workflow

```
1. DISCOVER inline functions with swappable calls
   |
2. For each candidate inline:
   |   a. Identify all .obj files that include this header (ninja deps)
   |   b. Generate header variants (swap call orders)
   |   c. For each variant:
   |       i.   Write modified header
   |       ii.  Run `ninja` (incremental rebuild — correct dep handling)
   |       iii. Run objdiff on EVERY affected symbol
   |       iv.  Compute net delta across all affected functions
   |       v.   Restore original header
   |   d. Report: winners, losers, net impact
   |
3. Optionally apply the best variant
```

### Phase 2a: Discovery — Finding Swappable Inlines

Two approaches, both worth implementing:

#### Approach A: Bottom-up (from mismatching functions)

Start from functions with prologue mismatches (`gpr_save_delta < 0`), trace their last call to an inline definition, check if that inline has swappable calls.

```
1. Query decomp.db for functions at 90-99% with prologue mismatch signals
2. For each, parse the function body to find the last call
3. Resolve call target: is it an inline in a header?
4. If yes, parse the inline body for consecutive independent calls
5. Candidate found
```

**Pro**: Targeted — only looks at inlines that affect mismatching functions.
**Con**: Misses inlines that are already tail-calling correctly in some callers but not others.

#### Approach B: Top-down (scan all inlines)

Parse all headers for inline functions containing 2+ consecutive independent calls.

```
1. Glob src/**/*.h for inline function definitions
2. Parse with tree-sitter, find compound_statement bodies
3. For each body, find consecutive independent call_expression statements
4. Filter to void functions or functions where last stmt is a call
5. Candidate found
```

**Pro**: Complete coverage — finds ALL potential tail-call inlines.
**Con**: More candidates to test, slower.

**Recommendation**: Start with Approach A (targeted, faster feedback), add Approach B later.

### Phase 2b: Dependency Resolution — What Gets Rebuilt

Use ninja's dependency graph to find affected .obj files:

```bash
# Find all .obj files that depend on a given header
ninja -t deps | grep 'src/system/math/Mtx.h'
```

Or more precisely, use `ninja -t query` on each .obj target to check if the header is in its deps. The build system already tracks all header dependencies via `/showIncludes` + `deps = msvc`.

Alternative: `ninja -n <all_obj_targets>` after touching the header — ninja itself will report what needs rebuilding.

### Phase 2c: Scoring — Multi-Symbol Impact Assessment

This is the hardest part. After rebuilding all affected .obj files, we need to score every function in every affected unit.

#### Approach: Differential Scoring

```python
# Before variant: snapshot baseline scores for all symbols in affected units
baseline_scores = {}
for obj in affected_objs:
    for symbol in symbols_in(obj):
        baseline_scores[symbol] = objdiff_score(symbol)

# Apply variant, rebuild via ninja
apply_header_variant(variant)
subprocess.run(["ninja"] + affected_obj_targets)

# After variant: score same symbols
variant_scores = {}
for symbol in baseline_scores:
    variant_scores[symbol] = objdiff_score(symbol)

# Compute deltas
for symbol in baseline_scores:
    delta = variant_scores[symbol] - baseline_scores[symbol]
    if delta != 0:
        report(symbol, baseline_scores[symbol], variant_scores[symbol], delta)
```

#### Optimization: Only Score Relevant Symbols

Not every function in an affected .obj uses the modified inline. We can filter:
1. Parse the .cpp for calls to the modified inline (tree-sitter)
2. Only score symbols from functions that contain that call
3. Fall back to scoring all symbols if parsing is too slow

#### Optimization: Binary-Level Change Detection

After rebuilding, hash each .obj file. If the hash is unchanged from baseline, skip all symbols in that unit — the header change didn't affect the codegen for that translation unit.

```python
for obj in affected_objs:
    if md5(obj) == baseline_md5[obj]:
        continue  # No codegen change in this unit
    # Only score symbols from changed .obj files
```

### Phase 2d: Decision Framework

A header change might improve 20 functions and regress 2. How do we decide?

#### Metrics

```
Net improved:    count of functions where delta > 0
Net regressed:   count of functions where delta < 0
Total delta:     sum of all deltas (weighted by function size?)
Perfect gained:  count of functions reaching 100%
Perfect lost:    count of functions dropping from 100%
```

#### Decision Rules

1. **Never accept if it drops any function from 100%** — those are confirmed matches
2. **Accept if net positive and no 100% drops** — we can fix regressions later
3. **Flag for review** if regressions exist in functions above 95% — these might be close to matching and the regression could be significant
4. **Always report** the full impact table for human review

#### The "Closer to Real" Hypothesis

User insight: a header change that improves many functions but regresses a few may actually be **more correct** than the current header. The regressed functions might just need their `.cpp` code adjusted to match the new (correct) header behavior. This is a real possibility because:

- The target binary was compiled with ONE set of headers
- Our current headers might have wrong inline ordering that happens to match some functions but not others
- The "correct" ordering will match the most functions by default

This suggests: when evaluating a header change, **weight improvements higher than regressions**, especially when the improvement count significantly exceeds the regression count. A 20-improved / 2-regressed ratio strongly suggests the change is correct.

### Phase 2e: Post-Accept Workflow

After accepting a header change:

1. Commit the header change
2. Run the permuter on regressed functions with standard patterns
3. Many regressions may be fixable with source-level changes (the `.cpp` code just needs to adapt to the new inline ordering)
4. Report remaining regressions that need manual attention

## Data Structures

```python
@dataclass
class InlineCandidate:
    header_path: Path
    function_name: str       # e.g., "Multiply"
    qualified_name: str      # e.g., "void Multiply(const Transform&, ...)"
    line_number: int
    call_statements: list[Node]  # AST nodes of consecutive calls
    num_permutations: int    # Usually 2 for a pair swap

@dataclass
class HeaderVariant:
    candidate: InlineCandidate
    header_path: Path
    original_header: bytes
    modified_header: bytes
    description: str         # "Swap Multiply(v) and Multiply(m)"

@dataclass
class SymbolImpact:
    symbol: str
    demangled: str
    unit: str
    baseline_pct: float
    variant_pct: float
    delta: float

@dataclass
class VariantReport:
    variant: HeaderVariant
    affected_objs: list[Path]
    changed_objs: list[Path]     # Subset where .obj hash changed
    impacts: list[SymbolImpact]
    net_improved: int
    net_regressed: int
    perfect_gained: int
    perfect_lost: int
    total_delta: float
```

## CLI Interface

```bash
# Scan for candidates (no changes)
python -m decomp_synth.header_tail_call scan

# Test a specific header inline
python -m decomp_synth.header_tail_call test --header src/system/math/Mtx.h --function Multiply

# Test all candidates in a header
python -m decomp_synth.header_tail_call test --header src/system/math/Mtx.h

# Full scan + test (slow but thorough)
python -m decomp_synth.header_tail_call scan --test

# Apply a confirmed-good change
python -m decomp_synth.header_tail_call apply --header src/system/math/Mtx.h --function Multiply --variant 0
```

## Build Integration

### Why ninja, not direct cl.exe

The existing permuter scorer bypasses ninja for speed — it extracts the cl.exe command and invokes it directly. This works for `.cpp` edits because only one .obj is affected.

Header changes are different:
- A header like `Mtx.h` is included by 100+ .cpp files
- Each change may invalidate the PCH (precompiled header)
- ninja correctly handles the dependency cascade via `/showIncludes` dep tracking
- Direct cl.exe invocation would miss rebuilding dependent .obj files

### Build time considerations

- PCH rebuild: ~2-3 seconds
- Per-.obj incremental: ~0.9-1.15 seconds with `WIBO_FS_CACHE=1`
- A header included by 50 .cpp files: ~50-60 seconds per variant
- A header included by 200 .cpp files: ~3-4 minutes per variant

This means each variant test is expensive. Discovery/filtering must be aggressive to minimize the number of variants tested.

### Concurrency

**Strictly single-threaded for header variants.** Reasons:
- One header change affects many .obj files
- ninja itself parallelizes the rebuild (`-j` flag)
- Two concurrent header edits would corrupt each other's builds
- The tool must hold an exclusive lock on the header file

Internally, ninja's own parallelism handles the CPU utilization:
```bash
ninja -j$(nproc) affected_target_1.obj affected_target_2.obj ...
```

## Risk Analysis

### Risks

1. **PCH invalidation cascade**: Headers included by the PCH will cause full rebuilds. Filter these out or handle specially.
2. **Semantic correctness**: Swapping calls in an inline must preserve semantics. The independence check (no shared reads/writes) handles this, but side effects through global state are harder to verify.
3. **Build time**: Popular headers (Mtx.h, Object.h) touch hundreds of .obj files. Each variant is expensive to test.
4. **False positives in discovery**: Many inlines with consecutive calls won't benefit from reordering — only the last-call-in-void-function pattern enables tail calls.

### Mitigations

1. Classify headers by inclusion count. Start with less-popular headers for faster iteration.
2. Reuse the `_are_independent_calls` check from Phase 1, plus MILO_ASSERT guards.
3. Use .obj hash dedup: if a rebuilt .obj is identical to baseline, skip all its symbols.
4. Filter candidates: only test inlines that are the last call in at least one caller's body.

## Open Questions

1. **Should we also try reordering 3+ calls?** (e.g., permutations of A,B,C → 6 variants). Risk: combinatorial explosion. Mitigation: only test with Ghidra-guided ordering first.

2. **Can we detect tail-call eligibility statically?** Rather than build+score, analyze whether the inline's last call CAN be a tail call based on argument usage. This would filter candidates before the expensive build step.

3. **Should the tool track "header correctness confidence"?** If a header change improves 50 functions and regresses 0, that's very strong signal. We could maintain a database of "verified header orderings" to avoid retesting.

4. **Integration with the orchestrator MCP**: The `run_objdiff` tool already handles single-function scoring. Could we add a `run_header_variant` tool that wraps this entire workflow?

## Implementation Priority

1. **Inline discovery** (Approach A: bottom-up from mismatching functions)
2. **Dependency resolution** via ninja
3. **Single-variant test loop** with multi-symbol scoring
4. **Reporting** with impact table
5. **Apply mode** with safety checks
6. **Top-down discovery** (Approach B: scan all headers)
7. **Ghidra-guided ordering** (compare target's call order)
