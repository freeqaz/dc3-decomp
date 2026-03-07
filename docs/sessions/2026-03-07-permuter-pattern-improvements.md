# Session: Permuter Pattern Improvements from Decomp Work

Date: 2026-03-07

## Context

This session started with MsgSinks unit decomp work (4 functions below 100%), then moved to broader function fixing across the codebase. Along the way, we identified 6 categories of source-level fixes — some automatable, some for diagnosis — that the permuter doesn't currently handle.

## Decomp Fixes Applied

| Function | File | Fix | Before | After | Pattern |
|----------|------|-----|--------|-------|---------|
| SystemInit | System.cpp | Uncommented `GlitchFinder::Init()` + added include | 99.x% | 100% | Missing function call |
| WriteChunk | ChunkStream.cpp | Fixed 4 MILO_ASSERT line numbers (+6 each) | ~95% | ~98% | Assert line delta |
| OnAnimate | Anim.cpp | `std::fabs` → `fabsf` + added `&& taskPtr` null guard | ~96% | 100% (norm) | Float promotion + missing guard |
| GetPropSyncHandler | Msg.cpp | Various attempts, all reverted | 90.0% | 90.0% (at_limit) | Prologue mismatch |
| Replace | Msg.cpp | Diagnosis only | 92.6% | 92.6% (at_limit) | Volatile regswap |
| AddPropertySink | Msg.cpp | Diagnosis only | 95.9% | 95.9% (at_limit) | sret allocation order |
| Box::Volume | Geo.cpp | Decl reorder + inline attempts, all identical | — | — (at_limit) | FPR scheduling |
| complex::operator* | complex.cpp | Operand reorder, no change | — | — (at_limit) | FPR scheduling |

## Pattern Categories

### 1. Missing Function Call Detection

**What**: Target binary has a `bl <symbol>` that our source doesn't emit. Shows as a `delete` cluster in objdiff.

**Example**: `SystemInit` was missing `GlitchFinder::Init()` — a single `delete` of `bl ?Init@GlitchFinder@@SAXXZ`.

**Detection strategy**:
1. Run `diff_inspect mode=clusters` on the function
2. Look for single-instruction `delete` clusters where opcode = `bl`
3. Extract the symbol name from the `bl` argument
4. Demangle it to get `Class::Method()`
5. Search the source for commented-out calls or missing includes

**Implementation**: New pattern `missing_call_detection`

```python
class MissingCallDetection(Pattern):
    name = "missing_call"
    opt_in = True  # Guided diagnosis, not blind permutation

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Look for delete clusters that are single `bl` instructions
        for cluster in diagnosis.clusters:
            if cluster.type == "delete" and len(cluster.ops) == 1:
                if cluster.ops[0].target_opcode == "bl":
                    return True
        return False
```

**Automation level**: Semi-automatic. Detection is easy; the fix (uncommenting a call, adding an include) requires understanding the function's structure. Best used as a diagnostic hint, not a blind permuter pattern.

**Integration point**: `Diagnosis.clusters` already contains insert/delete groups. The `DiffOp` within a cluster has `target_opcode` and `target_args` (symbol name for `bl`).

### 2. MILO_ASSERT Line Number Correction

**What**: Hardcoded `__LINE__` values in `MILO_ASSERT(cond, LINE)` are baked into the binary as `li rN, <value>`. When lines are added/removed above the assert, the number drifts.

**Example**: `ChunkStream::WriteChunk` had 4 asserts all off by +6 (778→784, 820→826, 822→828, 827→833).

**Detection strategy**:
1. Run `diff_inspect mode=mismatches` to get instruction-level differences
2. Filter for `replace` entries where both opcodes are `li` (load immediate)
3. Extract the immediate values from both target and base
4. If all `li` deltas are uniform (e.g., all +6), it's a line-number drift
5. Find `MILO_ASSERT` calls in the source, adjust the second argument by the delta

**Implementation**: New pattern `assert_line_fix`

```python
class AssertLineFix(Pattern):
    name = "assert_line_fix"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        li_deltas = []
        for d in diagnosis.diff_ops:
            if d.target_opcode == "li" and d.base_opcode == "li":
                # Extract immediate values from args
                target_val = _extract_li_immediate(d.target_args)
                base_val = _extract_li_immediate(d.base_args)
                if target_val and base_val:
                    li_deltas.append(target_val - base_val)
        # Uniform nonzero delta = line number drift
        if li_deltas and len(set(li_deltas)) == 1 and li_deltas[0] != 0:
            return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        delta = self._compute_delta(ctx.diagnosis)
        # Find MILO_ASSERT calls via tree-sitter
        for call in _find_macro_calls(ctx.body_node, b"MILO_ASSERT"):
            line_arg = call.arguments[-1]  # Last arg is __LINE__
            old_val = int(ctx.file_source[line_arg.start_byte:line_arg.end_byte])
            new_val = old_val + delta
            ed = SourceEditor(ctx.file_source)
            ed.replace_node(line_arg, str(new_val).encode())
            yield Variant(name=f"assert_line_{new_val}", source=ed.apply(), ...)
```

**Automation level**: Fully automatic. The uniform delta makes this a reliable heuristic. Can also be extended to `MILO_WARN`, `MILO_FAIL`, and other line-number macros.

**Integration point**: `diff_inspect mode=mismatches` returns `DiffOp` entries with `target_args`/`base_args` strings. Need to parse the immediate value from `li r5, 784` → `784`.

### 3. Float/Double Promotion Fix

**What**: Using `std::fabs()` (returns double) instead of `fabsf()` (returns float) causes the compiler to emit `fdiv + frsp` (double divide + round-to-single) instead of `fdivs` (single-precision divide).

**Example**: `RndAnimatable::OnAnimate` — `std::fabs(animTaskEnd - animTaskStart)` → `fabsf(...)` fixed the codegen.

**Detection strategy**:
1. Check `diagnosis.diff_ops` for `lfd↔lfs` or `fdiv↔fdivs` or presence of `frsp`
2. If found, scan source for `std::fabs`, `fabs`, `sqrt`, `sin`, `cos`, `exp`, `pow`, `log` calls
3. Try replacing with `fabsf`, `sqrtf`, `sinf`, `cosf`, `expf`, `powf`, `logf`

**Existing pattern**: `float_double_literal` already handles `0.001` ↔ `0.001f` literal suffixes. This extends it to **function calls** (`fabs` → `fabsf`).

**Implementation**: Extend `float_double_literal.py` or create `float_double_func.py`

```python
_DOUBLE_TO_FLOAT = {
    b"std::fabs": b"fabsf",
    b"fabs": b"fabsf",
    b"sqrt": b"sqrtf",
    b"sin": b"sinf",
    b"cos": b"cosf",
    b"exp": b"expf",
    b"pow": b"powf",
    b"log": b"logf",
    b"ceil": b"ceilf",
    b"floor": b"floorf",
}

class FloatDoubleFunc(Pattern):
    name = "float_double_func"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("fdivs", "fmuls", "fadds", "fsubs"):
                if d.base_opcode in ("fdiv", "fmul", "fadd", "fsub"):
                    return True
            if "frsp" in (d.target_opcode, d.base_opcode):
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        for call_node in _find_call_expressions(ctx.body_node):
            func_name = _get_callee_text(call_node, ctx.file_source)
            if func_name in _DOUBLE_TO_FLOAT:
                ed = SourceEditor(ctx.file_source)
                ed.replace_range(call_node.child_by_field_name("function"),
                                _DOUBLE_TO_FLOAT[func_name])
                yield Variant(name=f"fltfunc_{func_name.decode()}", source=ed.apply(), ...)
```

**Automation level**: Fully automatic. The `frsp` instruction is a strong signal. Can also try the reverse direction (float → double) for completeness.

### 4. Missing Null Guard Detection

**What**: Target binary has a 2-instruction `cmplwi rN, 0x0` + `beq <skip>` before a pointer dereference that our source omits.

**Example**: `OnAnimate` was missing `&& taskPtr` in `if (local_wait && taskPtr)`.

**Detection strategy**:
1. Check `diagnosis.clusters` for 2-instruction `delete` clusters containing `cmplwi` + `beq`/`bne`
2. Cross-reference with Ghidra decompilation: look for `if (ptr != (TYPE *)0x0)` guards
3. Find the corresponding pointer dereference in our source
4. Add `&& ptr` to the condition, or wrap the dereference in `if (ptr)`

**Existing pattern**: `null_guard_elimination.py` already handles the INVERSE case (removing guards present in source but absent in target). This is the complement — adding guards.

**Implementation**: New pattern `null_guard_insertion`

```python
class NullGuardInsertion(Pattern):
    name = "null_guard_insert"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for cluster in diagnosis.clusters:
            if cluster.type == "delete" and len(cluster.ops) == 2:
                opcodes = {op.target_opcode for op in cluster.ops}
                if "cmplwi" in opcodes and ("beq" in opcodes or "bne" in opcodes):
                    return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        # Strategy 1: Ghidra-guided — find guards in Ghidra output absent in source
        if ctx.ghidra_ast:
            ghidra_guards = _extract_ghidra_null_checks(ctx.ghidra_ast)
            source_guards = _extract_source_null_checks(ctx.body_node, ctx.file_source)
            missing = ghidra_guards - source_guards
            for guard_var in missing:
                # Find dereferences of this variable in source
                for deref in _find_dereferences(ctx.body_node, guard_var, ctx.file_source):
                    # Add && guard_var to enclosing condition
                    yield _insert_guard_variant(ctx, deref, guard_var)

        # Strategy 2: Blind — for each pointer deref without a guard, try adding one
        for deref in _find_unguarded_pointer_derefs(ctx.body_node, ctx.file_source):
            var_name = _get_deref_var(deref, ctx.file_source)
            yield _insert_guard_variant(ctx, deref, var_name)
```

**Ghidra integration**: The `ghidra_preflight.py` already detects call count mismatches. The null guard pattern extends this to detect specific guard patterns in Ghidra's decompiled output using `ghidra_ast.py`'s AST extraction. The key data is:
- `_extract_ghidra_null_checks(ghidra_ast)` — parse `if (ptr != (TYPE *)0x0)` patterns from Ghidra tree-sitter AST
- Compare with source's `if (ptr)` patterns
- Difference set = missing guards to try inserting

**Automation level**: Semi-automatic with Ghidra guidance (high confidence), fully automatic blind (lower confidence, more variants).

### 5. Diagnosis-First Classification (Mismatch Triage)

**What**: Before running any patterns, classify the function's mismatches into fixable vs unfixable categories. Skip functions that are blocked by unfixable patterns.

**Current state**: `ghidra_preflight.py` already does some of this (6 detection rules), but it's coarse-grained. The patterns themselves check `relevant()` but don't share classification results.

**Proposed classification taxonomy**:

| Category | Signal | Fixable? |
|----------|--------|----------|
| Volatile regswap | `r0-r12` or `f0-f13` in swap pairs | Never |
| Callee-saved regswap | `r13-r31` or `f14-f31` in swap pairs | Sometimes (decl reorder) |
| Prologue mismatch | Different `__savegprlr_N` count | Rarely (structural change) |
| Float promotion | `fdiv↔fdivs`, `frsp` present | Yes (suffix/func fix) |
| Assert line delta | Uniform `li` delta | Yes (adjust constants) |
| Missing call | Single `bl` delete | Yes (uncomment/add include) |
| Missing guard | `cmplwi+beq` delete cluster | Yes (add null check) |
| sret allocation | Stack offset swap only | Never |
| ICF / merged symbol | `merged_<addr>` in symbol | Never |
| Static guard (`??_B` vs `$S`) | Guard naming pattern | Never |
| Instruction scheduling | Same 2 instructions, reversed order | Never |
| FPR scheduling | Same FP operations, different register/order | Never |
| Branch polarity | `beq↔bne` on same condition | Sometimes (if/else invert) |

**Implementation**: Extend `Diagnosis` with a `classifications` field

```python
@dataclass
class MismatchClassification:
    category: str           # From taxonomy above
    fixable: bool           # Known fixable?
    confidence: float       # 0.0-1.0
    instructions_affected: int  # How many diff_ops this explains
    detail: str             # Human-readable explanation

def classify_mismatches(diagnosis: Diagnosis) -> list[MismatchClassification]:
    """Analyze diagnosis and classify each mismatch cluster."""
    results = []

    # Check register swap pairs
    for (r1, r2), info in diagnosis.reg_swap_pairs.items():
        is_volatile = _is_volatile_reg(r1) or _is_volatile_reg(r2)
        results.append(MismatchClassification(
            category="volatile_regswap" if is_volatile else "callee_saved_regswap",
            fixable=not is_volatile,
            confidence=0.95,
            instructions_affected=info.count,
            detail=f"{r1}↔{r2} swap ({info.count} instructions)",
        ))

    # Check prologue
    if diagnosis.target_gpr_saves and diagnosis.base_gpr_saves:
        delta = diagnosis.target_gpr_saves - diagnosis.base_gpr_saves
        if delta != 0:
            results.append(MismatchClassification(
                category="prologue_mismatch",
                fixable=False,
                confidence=0.8,
                instructions_affected=abs(delta) * 2,  # save + restore
                detail=f"Target saves {diagnosis.target_gpr_saves} GPRs, "
                       f"ours saves {diagnosis.base_gpr_saves} (delta={delta})",
            ))

    # ... more classifiers for each category
    return results
```

**Automation level**: Fully automatic. This is a diagnostic pass, not a source transformation. Its output informs pattern selection and budget allocation.

**Integration point**: Run after `scorer.get_baseline()`, before pattern generation. Feed classifications into `allocate_budgets()` to zero-budget patterns that can't help given the classification.

### 6. Ghidra Call-Graph Diff

**What**: Semantic diff between Ghidra's decompilation of the target binary and our source code. Goes beyond instruction-level mismatches to find structural differences: missing calls, wrong types, different control flow.

**Current state**: `ghidra_preflight.py` already does basic call-count comparison. `ghidra_ast.py` extracts variable order and control flow skeleton. But there's no **diffing** of the two ASTs.

**Proposed implementation**: `ghidra_source_diff.py`

```python
@dataclass
class SourceDiff:
    missing_calls: list[str]        # In Ghidra but not source
    extra_calls: list[str]          # In source but not Ghidra
    type_mismatches: list[TypeDiff] # Different types for same operation
    guard_mismatches: list[GuardDiff]  # Different null check patterns
    control_flow_diff: list[str]    # Structural differences (for/while/do-while)

def diff_ghidra_vs_source(
    ghidra_code: str,
    source_code: str,
    symbol: str
) -> SourceDiff:
    """Compare Ghidra decompilation against our C++ source."""

    # Parse both with tree-sitter
    ghidra_ast = parse_c(ghidra_code)
    source_ast = parse_cpp(source_code)

    # Extract call sites from both
    ghidra_calls = extract_call_sites(ghidra_ast)
    source_calls = extract_call_sites(source_ast)

    # Diff calls (normalize mangled names)
    missing = ghidra_calls - source_calls
    extra = source_calls - ghidra_calls

    # Extract null guards from both
    ghidra_guards = extract_null_guards(ghidra_ast)
    source_guards = extract_null_guards(source_ast)

    # Diff control flow skeleton
    ghidra_cf = extract_control_flow(ghidra_ast)
    source_cf = extract_control_flow(source_ast)

    return SourceDiff(
        missing_calls=list(missing),
        extra_calls=list(extra),
        guard_mismatches=diff_guards(ghidra_guards, source_guards),
        control_flow_diff=diff_control_flow(ghidra_cf, source_cf),
    )
```

**Ghidra MCP integration**: Uses the existing infrastructure:
- `MCPClient.decompile_function(symbol)` → pseudo-C code
- `ghidra_cache.get_or_cache_decompilation(symbol)` → cached version
- `MCPClient.get_callgraph(function, direction="called")` → callees for cross-validation
- `MCPClient.list_xrefs(symbol)` → cross-references for context

**Available data from Ghidra** (from research):
- Full decompiled pseudo-C code with types
- Function signatures
- Call graph (nodes + edges, both directions)
- Cross-references (callers/callees)
- Structure layouts (member offsets)
- Variable names (Ghidra-generated: `iVar2`, `fVar3`, etc.) with first-use order
- Control flow structure (if/while/for/switch/return)

**Key challenge**: Ghidra outputs C (not C++), so name matching requires demangling. The `ghidra_ast.py` already handles this via tree-sitter-c parsing, and `mcp_client.py` resolves symbol names to addresses.

**Automation level**: Semi-automatic. The diff provides diagnostic hints; actual fixes require understanding the semantic meaning of each difference.

## Unfixable Patterns (Skip List)

These patterns should be detected early and used to skip or deprioritize functions:

1. **Volatile register swaps** (`r0-r12`, `f0-f13`): Compiler-internal allocation, no source-level control
2. **sret buffer allocation order**: Compiler decides stack layout for unnamed return-value temporaries
3. **ICF / merged symbols**: Linker-level optimization, no source fix
4. **Static guard naming** (`??_B` vs `$S`): Compiler-internal guard variable naming
5. **FPR liveness scheduling**: MSVC PPC schedules FP ops by register liveness graph, not source order
6. **Instruction scheduling swaps**: Same instructions in reversed order for arg preparation
7. **vbase recomputation vs cached ritEnd**: Compiler chooses between volatile recompute and callee-saved cache

## Implementation Priority

### Tier 1: Quick Wins (< 1 day each)

1. **Assert line fix pattern** — Fully automatic, high confidence, clear signal (`li` delta uniformity)
   - Files: New `scripts/permuter/patterns/assert_line_fix.py`
   - Requires: Parse `li` immediates from `DiffOp.target_args`/`base_args`
   - Test: Find functions with uniform `li` delta in existing codebase

2. **Float/double function promotion** — Extend existing `float_double_literal` pattern
   - Files: Extend or sibling `scripts/permuter/patterns/float_double_literal.py`
   - Requires: Tree-sitter call expression scanning
   - Test: OnAnimate-like functions with `frsp` in diagnosis

3. **Diagnosis classification** — Wire into budget allocation
   - Files: New `scripts/permuter/classifier.py`, modify `generator.py`
   - Requires: Access to `Diagnosis.reg_swap_pairs`, `target_gpr_saves`
   - Test: Run on batch of 100 functions, verify skip accuracy

### Tier 2: Medium Effort (1-3 days each)

4. **Null guard insertion** — Complement to existing `null_guard_elimination`
   - Files: New `scripts/permuter/patterns/null_guard_insert.py`
   - Requires: Ghidra AST comparison for guided mode
   - Test: OnAnimate-like functions with `cmplwi+beq` delete clusters

5. **Missing call detection** — Diagnostic tool (not blind permuter)
   - Files: New `scripts/permuter/patterns/missing_call.py` (opt_in)
   - Requires: Symbol demangling, source scanning for commented-out calls
   - Test: SystemInit-like functions with single `bl` delete clusters

### Tier 3: Research (1 week+)

6. **Ghidra call-graph diff** — Structural comparison tool
   - Files: New `scripts/permuter/ghidra_source_diff.py`
   - Requires: tree-sitter-c + tree-sitter-cpp dual parsing, name normalization
   - Test: Compare Ghidra output against source for 10 known-fixable functions

## Key Files Reference

| File | Role |
|------|------|
| `scripts/permuter/patterns/base.py` | Pattern ABC — subclass + set `name` to auto-register |
| `scripts/permuter/patterns/float_double_literal.py` | Existing float/double literal pattern (extend for functions) |
| `scripts/permuter/patterns/null_guard_elimination.py` | Existing guard removal (complement with insertion) |
| `scripts/permuter/ghidra_preflight.py` | Unfixable pattern detection (extend with classifier) |
| `scripts/permuter/ghidra_ast.py` | Ghidra AST extraction (variable order, control flow) |
| `scripts/permuter/ghidra_cache.py` | Decompilation cache with circuit breaker |
| `scripts/permuter/ghidra_var_match.py` | Register inference from Ghidra variable order |
| `scripts/permuter/types.py` | `Diagnosis`, `DiffOp`, `Cluster`, `Variant` dataclasses |
| `scripts/permuter/generator.py` | Budget allocation, phase orchestration |
| `scripts/permuter/hill_climber.py` | Round loop, scoring, Ghidra context injection |
| `tools/ghidra/mcp_client.py` | Ghidra MCP client (decompile, callgraph, xrefs) |
| `tools/analyze_function.py` | Unified analysis (objdiff + Ghidra + m2c) |

## Implementation Status

### Implemented (this session)

**`assert_line_fix`** — `scripts/permuter/patterns/assert_line_fix.py`
- Finds `MILO_ASSERT`/`MILO_WARN`/`MILO_FAIL` calls via tree-sitter
- Generates uniform delta variants (-10..+10 applied to ALL asserts simultaneously)
- Also generates per-assert individual deltas for non-uniform cases
- `relevant()` triggers on: `offset_deltas` entries OR unexplained `diff_arg` noise
- `priority()` boosts when `offset_deltas` has small uniform deltas (1-30 range, count >= 2)
- Test scan: 58 hits across 980 files, 12 resolved with symbols
- Note: `MILO_ASSERT` line immediates show as `diff_arg` (same `li` opcode, different value) — NOT in `diff_ops`. They feed into `offset_deltas` histogram via `parse_breakdowns()`.

**`math_func_promotion`** — `scripts/permuter/patterns/math_func_promotion.py`
- Covers 15 math functions: `sqrt/sin/cos/exp/pow/log/log10/ceil/floor/atan2/asin/acos/tan/atan/hypot`
- Both directions: `sqrt` → `sqrtf` AND `sqrtf` → `sqrt`
- Handles `std::` qualified names (`std::sqrt` → `sqrtf`)
- Individual swaps + bulk swap (all-to-float or all-to-double)
- Does NOT overlap with `fabs_variant.py` (which handles `fabs`/`fabsf`/`std::fabs`)
- `relevant()` triggers on: FP single↔double opcode mismatches (`fdivs↔fdiv` etc.) or `frsp`
- Test scan: 161 hits across 980 files, 79 resolved with symbols
- Pattern correctly filters itself out for non-FP-promotion mismatches

**`classifier.py`** — `scripts/permuter/classifier.py`
- Classifies mismatches into 13 categories with fixability assessment
- Produces fixability score (0.0-1.0) for each function
- Wired into `hill_climber.py` — shows on round 1 after diagnosis
- Condensed output for functions with many register swap pairs
- Categories: volatile_regswap[-], callee_saved_regswap[~], prologue_mismatch[-], float_promotion[+], assert_line_delta[+], comparison_sign[+], branch_polarity[+/~], missing_guard[+]

**`null_guard_insert`** — `scripts/permuter/patterns/null_guard_insert.py`
- Three strategies: Ghidra-guided (diff guard sets), && condition insertion, if-wrap
- Reuses `_extract_ghidra_null_checks` and `_extract_source_null_checks` from `null_guard_elimination`
- Test scan: 6,401 hits across 980 files, 991 resolved with symbols
- High blind build failure rate expected (~70%) — scorer handles gracefully

### Not yet implemented

- `missing_call` diagnostic (Tier 2) — detect single `bl` delete clusters
- `ghidra_source_diff.py` (Tier 3) — structural Ghidra vs source comparison

## Research Findings

### Objdiff Data Pipeline

The orchestrator's instruction-level data flows through three layers:

1. **Raw JSON** (`objdiff-cli --include-instructions`): Each instruction has `match_type`, `target`/`base` sides with `opcode`/`args`/`typed_args`, and optional `diff_breakdown` with typed argument diffs
2. **`parse_breakdowns()`** in `diff_inspect.py`: Extracts `reg_swaps`, `offset_diffs` (all immediate deltas), `symbol_diffs`, `branch_diffs` from `diff_breakdown.arguments`
3. **`Diagnosis`**: Aggregates into `reg_swap_pairs` (SwapInfo), `offset_deltas` (histogram), `diff_ops` (DiffOp list for `diff_op`+`replace` types only), `clusters` (insert/delete groups)

Key insight: `DiffOp` only stores `(index, target_opcode, base_opcode)` — no argument data. Immediate values (like MILO_ASSERT line numbers) appear as `diff_arg` match type, NOT `diff_op`, so they're captured in `offset_deltas` but not in `diff_ops`. Patterns needing immediate values must use `offset_deltas` or access raw instruction data.

### Permuter Pattern System

- 57 patterns auto-registered via `__init_subclass__` in `Pattern` base class
- `generate()` yields `Variant` objects lazily (iterator-based, budget-controlled)
- `relevant(diagnosis)` returns bool to skip when diagnosis doesn't match
- `priority(diagnosis)` returns 0.0-1.0 for budget allocation weighting
- `SourceEditor` handles atomic multi-edit application (reverse byte order)
- `FunctionContext` provides: AST nodes, file source, diagnosis, Ghidra data
- Three-layer dedup: source hash → obj hash → persistent cache (symbol + source MD5)

### Ghidra Integration

- `MCPClient` at `http://127.0.0.1:8000/mcp` (JSON-RPC 2.0 + SSE)
- Decompilation cache in `decomp.db` (SQLite) with fetch-on-miss + circuit breaker (3 failures → disable)
- `ghidra_preflight.py` runs 6 red-flag checks: struct offsets, call count, dead vars, prologue, volatile swaps, merged symbols
- `ghidra_var_match.py` infers target register allocation from Ghidra variable first-use order
- `ghidra_ast.py` extracts control flow skeleton, variable order, expression structure via tree-sitter-c
- Call graph available via `MCPClient.get_callgraph(function, direction)` → JSON nodes + edges

## Relationship to Other Session Docs

- `2026-03-07-permuter-chain-improvements.md` — Chain/composition system improvements (Phase A-C). The patterns described here are **orthogonal** — they add new transformation types, while the chain doc improves the search infrastructure.
- The chain system's multi-stage composition can combine these new patterns with existing ones (e.g., `assert_line_fix` → `math_func_promotion` chain).
