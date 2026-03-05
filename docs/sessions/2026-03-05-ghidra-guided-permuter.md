# Ghidra-Guided Permuter — Design Discussion (2026-03-05)

Ideas for using Ghidra decompilation output to guide the permuter's search space, reducing blind combinatorial exploration and improving hit rates on regswaps and expression mismatches.

## Motivation

The permuter currently operates "blind" — it generates source variants based on AST patterns and opcode diagnosis, then checks each against the target via objdiff. This works for mechanical transforms (FMA reorder, guard-to-conjunction) but struggles with combinatorial problems like declaration reordering for register swaps (~30% hit rate).

The target binary's Ghidra decompilation is essentially a noisy version of the original source. If we parse it and structurally compare it to our source, we can generate *only* the variants that move toward the target's structure, dramatically reducing the search space.

## Existing Intelligence Stack

The permuter already has three "intelligence levels":

1. **Blind patterns** — AST transforms triggered by opcode diagnosis. No knowledge of the target's source structure. (All patterns in `scripts/permuter/patterns/`)
2. **BSF-guided reorder** — traces the compiler's register allocator via GDB breakpoints on the BSF function in c2.dll (`tools/compiler_trace/bsf_trace.py`). Maps variable to register, generates targeted swaps. Works for ~17% of functions (those where BSF graph coloring is used).
3. **ASM regmap** — parses `/FAs` assembly listings (`tools/compiler_trace/asm_regmap.py`) to infer variable-to-register from `mr rN, r3` patterns after function calls. Works for all functions with callee-saved regs.

Ghidra integration exists (`tools/ghidra/mcp_client.py`, `tools/ghidra/ghidra-decompile.py`) but is only used by agents for ad-hoc lookups, not by the permuter itself.

## Proposed Architecture

```
                    Permuter Pipeline (enhanced)

  1. Build + objdiff --> Diagnosis (opcodes, regswaps, clusters)
  2. Ghidra decompile --> target AST (tree-sitter parse of decompiled C)   <-- NEW
  3. Parse our source --> source AST (already exists)
  4. Structural diff --> guided transforms                                 <-- NEW
  5. Generate variants (existing blind patterns + guided patterns)
  6. Build + score each variant
```

New component: a `GhidraGuidedPattern` base class that:
1. Calls `MCPClient.decompile_function(symbol)` to get decompiled C
2. Parses it with tree-sitter (C, not C++, but tree-sitter handles both)
3. Compares structural elements against our source AST
4. Generates only the variants that move toward the target structure

## Four Approaches

### 1. Expression Structure Matching (FMA / algebraic rewrites)

**What:** Compare expression tree structure between Ghidra output and our source. When they differ (parenthesized vs flat, different term order), generate only the matching variant.

**Example — CalcSpline (proven fix):**
```c
// Ghidra output (target):
term3 = p1x3m0 - p2 * 3.0f + p3;   // flat chain

// Our source:
float term3 = p3 - (p2 * 3.0f - p1x3m0);  // parenthesized
```

Tree-sitter AST diff would immediately show: target is flat `a - b + c`, source is nested `a - (b - c)`. Instead of trying all algebraic rewrites blindly, emit only the expansion that matches.

**Difficulty:** Medium
**Impact:** High for FMA mismatches (proven on CalcSpline 96->100%, InterpTangent 98.1->99.6%)
**Challenges:** Ghidra inserts casts and may use different variable names. Need fuzzy AST matching on expression *shape* not exact text.

### 2. Control Flow Structure Matching

**What:** Compare if/else/return structure between Ghidra output and our source. Detect when target uses conjunctions vs guards, different branch polarity, etc.

**Example — MetaPanel::IsLoaded (proven fix):**
```c
// Ghidra (target):
return UIPanel::IsLoaded() && TheMetaMusic->Loaded();

// Our source:
if (!(UIPanel::IsLoaded())) return false;
return (!TheMetaMusic || TheMetaMusic->Loaded());
```

Structural diff would surface: "target uses `&&` conjunction, source uses guard returns."

**Difficulty:** Medium
**Impact:** Medium (control flow mismatches are less common than regswaps)
**Challenges:** Ghidra often restructures control flow differently than the original — its own decompiler has heuristics that may not match the original source structure. Need to validate that Ghidra's output is closer to the original than just "a different restructuring."

### 3. Declaration Order Inference (register swap targeting)

**What:** Infer variable allocation order from Ghidra's decompiled output and use it to guide `declaration_reorder`. This is the highest-impact opportunity.

**How it works:**
- Callee-saved GPR assignment follows declaration/first-use order: first declared -> r31, second -> r30, etc.
- Ghidra's decompiler assigns variable names based on when they first appear in the decompiled output
- The appearance order in Ghidra output roughly corresponds to the register allocation order in the target binary
- Cross-reference with our `/FAs` assembly listing to see our current allocation
- Generate only the declaration reordering that matches the target's allocation order

**Example:**
```c
// Ghidra decompilation (target) — variable first-use order:
local_a = GetFoo();     // first use -> likely r31 in target
local_b = GetBar();     // second use -> likely r30 in target
result = local_a + local_b;

// Our source — different order:
int b = GetBar();       // first decl -> gets r31 in our build
int a = GetFoo();       // second decl -> gets r30 in our build
return a + b;           // regswap: our r31/r30 vs target r31/r30
```

Solution: reorder declarations to match Ghidra's first-use order.

**Difficulty:** Medium-Hard
**Impact:** Very high — regswaps are the #1 blocker (~30% fixable today, could push to 60-70%+)
**Challenges:**
- Ghidra variable names are arbitrary (`local_38`, `uVar2`). Must match by *usage pattern* (which function calls produce them, which members they access), not by name.
- Need to map between C++ member access (`obj->mFoo`) and Ghidra's offset access (`*(obj + 0x24)`) using struct layout knowledge.
- Some declaration reorderings break use-before-declaration dependencies.

### 4. Live Range Analysis

**What:** Compare where variables are born/die across the two representations. If the target has a variable live across a call boundary that our source inlines (or vice versa), that tells you whether to extract or eliminate a temp.

**Difficulty:** Hard (needs cross-statement dataflow, not just AST)
**Impact:** High for prologue mismatches and temp extraction/elimination
**Challenges:** Requires building a proper use-def chain from both ASTs, not just pattern matching.

## Quantitative Impact Estimates

### Current landscape (from pattern investigation session)
- **~22K functions at AT_LIMIT** — the vast majority blocked by regswaps, addr reloc, ICF, or guard naming
- **51 functions** affected by FMA direction mismatches (13 pure NEED_OFF, 10 NEED_ON, 3 variant swaps, 18 false positives, 7 mixed)
- **~17% of near-match AT_LIMIT** functions have stack spill scheduling mismatches (unfixable)
- **~11% of near-match** functions have prologue mismatches
- **34 functions** flagged with cmplwi/cmpwi issues
- **Regswaps are the #1 source-fixable blocker** — callee-saved swaps have ~30% blind fix rate via declaration reorder

### Per-approach impact

| Approach | Difficulty | Functions affected | Current hit rate | Guided hit rate (est) | Net new fixes |
|----------|-----------|-------------------|-----------------|----------------------|---------------|
| Expression structure diff | Medium | ~51 FMA-affected | ~10% (blind permuter) | ~80% | ~35 functions |
| Control flow diff | Medium | ~100-200 branch mismatches | ~20% | ~50% | ~60 functions |
| Declaration order inference | Medium-Hard | ~2000+ regswap-blocked | ~30% (blind reorder) | ~60-70% | ~600-800 functions |
| Live range analysis | Hard | ~500 prologue mismatches | ~10% | ~40% | ~150 functions |

### Projected overall impact
Declaration-order-guided regswap alone could move **600-800 functions** from AT_LIMIT to COMPLETE or higher match%. At an average of 1-2% match improvement per function across the full decomp, this represents a meaningful push toward the project's completion target. The other three approaches add incremental but valuable wins in their niches.

**Important caveat:** The ~2000+ regswap-blocked estimate counts functions where regswap is *a* detected pattern. Many also have addr_reloc or ICF (unfixable regardless), so the true "regswap-only" population is smaller. The 600-800 net new fixes estimate accounts for this overlap — it represents functions where regswap is the *primary remaining* blocker after other patterns are excluded.

## Known Challenges

### Ghidra output is noisy
Ghidra inserts casts (`(uint)`, `(long)`), uses different variable names, sometimes restructures control flow differently than the original. Cannot do naive text comparison — need semantic/structural comparison.

### Sandbox restrictions
Ghidra MCP runs on localhost:8000, which requires sandbox bypass. The permuter currently runs inside sandbox. Solutions:
- Pre-fetch decompilations into a cache file before running the permuter
- Add a `--ghidra` flag that enables network access for guided mode
- Batch-export decompilations for all target functions offline

### Variable name matching
Ghidra calls things `local_38` while our source uses `mParams`. Need to match by position/context:
- Nth declaration in function body
- Used as argument to specific function call
- Accessed at specific struct offset
- Return value of specific call

### tree-sitter C vs C++ parsing
Ghidra and m2c output C, not C++. Most constructs parse fine but templates, `::` scope resolution, and method calls appear differently. Could use tree-sitter-c for decompiler output and tree-sitter-cpp for our source, then compare at the semantic level. Having two decompiler sources (Ghidra + m2c) provides redundancy — if one produces a noisy representation, the other may be cleaner.

## Recommended Phasing

### Phase 1: Expression-guided FMA (start here)
- Fetch Ghidra decompilation for functions with FMA/expression mismatches
- Parse target's expression structure with tree-sitter
- Compare to our source's expression structure
- If they differ structurally (parenthesized vs flat, different term order), generate only the matching variant
- **Validation:** re-run on CalcSpline and InterpTangent to confirm the guided approach would have found the fix directly

### Phase 2: Pre-fetched decompilation cache
- `tools/ghidra/batch_export.py` already exists for this — exports decompilations + xrefs to SQLite with resume support
- Extend it to cover all non-100% functions (currently focused on orchestrator needs)
- This removes the sandbox/network blocker from the permuter pipeline
- Enables all subsequent phases to work offline
- m2c decompiler output could serve as a second source — its C output is also tree-sitter parsable and may produce different (sometimes better) structural matches than Ghidra

### Phase 3: Declaration-order-guided regswap
- For functions with callee-saved regswaps, look up cached decompilation
- Extract variable first-use order from Ghidra output
- Cross-reference with `/FAs` assembly listing for our source
- Generate only the declaration reordering that matches the target's allocation order
- **Validation:** run on a sample of 20 regswap-blocked functions and measure hit rate vs blind reorder

### Phase 4: Control flow guided transforms
- Compare if/else/return structure between cached decompilation and source
- Generate guard-to-conjunction, branch polarity, and early-return transforms only when the target structure differs from ours

## Related Files

- `scripts/permuter/patterns/` — all existing pattern implementations
- `scripts/permuter/diagnosis.py` — objdiff JSON to Diagnosis extraction
- `tools/ghidra/mcp_client.py` — Ghidra MCP client (decompile, search)
- `tools/ghidra/ghidra-decompile.py` — CLI decompilation tool
- `tools/ghidra/pcode_inspect.py` — pcode/switch/cast analysis
- `tools/ghidra/code_search.py` — semantic search over decompiled code
- `tools/compiler_trace/bsf_trace.py` — BSF register allocator tracing
- `tools/compiler_trace/asm_regmap.py` — /FAs assembly register mapping
- `tools/compiler_trace/regmap_solver.py` — guided pairwise search from BSF data
- `tools/ghidra/batch_export.py` — batch decompilation export to SQLite (Phase 2 foundation)

## Concrete Phase 1 Validation Plan

Before building any infrastructure, validate the approach on the three proven fixes from this session:

1. **CalcSpline** — Fetch Ghidra decompilation, parse the `term3 =` expression, confirm tree-sitter sees a flat chain (`a - b + c`) vs our nested `a - (b - c)`. If the structural diff is clean, this proves expression-guided FMA works.

2. **InterpTangent** — Same validation. Confirm Ghidra shows `fsq3 - f4 + 1.0f` (flat) vs our `1.0f - (f4 - fsq3)` (nested).

3. **MetaPanel::IsLoaded** — Fetch Ghidra decompilation, confirm it shows a `&&` conjunction vs our guard-return structure. This validates control flow matching.

If all three show clean structural diffs between Ghidra output and our (pre-fix) source, the approach is validated. If Ghidra's output is too noisy or restructured to match, we know where to focus denoising effort before scaling up.

Note: m2c is mentioned as an alternative decompiler source but is not currently integrated — it would be a new dependency if pursued.

## Evidence from This Session

This discussion emerged from the 2026-03-05 pattern investigation and permuter improvement session. The results provide strong evidence for the guided approach:

### What the blind permuter missed
- **CalcSpline**: Permuter tried 19 variants across 7 pattern categories, including 4 FMA reorders. None improved. The winning fix (`p3 - (p2*3 - p1x3m0)` → `p1x3m0 - p2*3 + p3`) was found manually by the agent, not by the permuter.
- **InterpTangent**: Permuter tried 9 variants including 3 FMA reorders. None improved. Same algebraic expansion pattern, found manually.
- **NgFur::Shell**: Permuter tried 31 variants. Investigation doc said `#pragma fp_contract(off)` would fix it — it didn't. The MSVC PPC compiler ignores the pragma at /O1. Also has a struct offset mismatch (+28 bytes). Ghidra could have revealed both issues upfront.
- **SpotlightDrawer::Init**: Investigation recommended using global access directly. Testing showed it made match% *worse* (94.1→91.4%). Ghidra decompilation would have shown the actual instruction scheduling pattern.

### What a guided approach would have done
For CalcSpline and InterpTangent, Ghidra decompilation shows the flat expression chain directly. A structural AST diff between `a - (b - c)` (our source) and `c - b + a` (Ghidra output) would have:
1. Identified the exact mismatch type (parenthesized vs flat)
2. Generated only the correct variant (reversed expansion)
3. Skipped all 25+ other variants that were tried and failed

For NgFur::Shell, Ghidra would have shown the struct offset difference immediately, saving the agent from trying 31 fruitless variants before diagnosing it as unfixable.

### Permuter improvements made in this session
After identifying the gaps, we added:
- **Parenthesized expansion** to `fma_reorder` — `a - (b - c)` → `c - b + a` and `a + (b - c)` → `a + b - c`
- **Guard-to-conjunction** to `early_return_merge` — `if (!cond) return false; return expr;` ↔ `return cond && expr;`
- **8 new test fixtures** covering all variants
- **1 new relevance test** for fadds/fsubs triggering

These patterns now catch the CalcSpline/InterpTangent/MetaPanel cases mechanically. But they were derived from manual analysis — a guided system would discover such patterns *automatically* by comparing target structure to source structure.

## The Endgame Vision

The broader question is: what does the permuter become when it can *see* the target's source structure?

### From pattern library to structural matching engine

Today the permuter is a **pattern library** — we manually encode transforms (FMA reorder, guard-to-conjunction, signed/unsigned cast) based on observed mismatches. Each pattern is a hand-crafted rule. When we discover a new mismatch type (like parenthesized subtraction expansion), we add a new rule, write tests, and ship it. This works but scales linearly with human effort.

A Ghidra-guided permuter would be a **structural matching engine** — given "what the target looks like" (from decompilation) and "what our source looks like" (from tree-sitter), it generates transforms that close the structural gap. This inverts the scaling: instead of encoding transforms for every possible mismatch, you encode a *comparison framework* and let the structural diff drive variant generation.

In the limit, this looks like:
1. Decompile the target function
2. Parse both representations into normalized ASTs
3. Compute a tree edit distance (or structural diff)
4. For each structural difference, emit a source transform that resolves it
5. Score, keep winners

Steps 1-3 are generic infrastructure. Step 4 is where domain knowledge lives, but it's much narrower than today's per-pattern rules — it's "how do I make tree node X look like tree node Y" rather than "what are all the ways expression structure affects codegen."

### Limitations of the vision

This won't eliminate the need for pattern rules entirely:
- **Codegen-specific transforms** (like `#pragma fp_contract`, `(int)` casts for cmplwi) don't correspond to structural differences in the decompiled output — they're about *how* the compiler translates a structure, not *what* structure it sees
- **Register allocation** is not visible in decompiled output at all — it's a backend concern. Declaration-order inference from Ghidra is a heuristic, not ground truth
- **Ghidra noise** means some structural diffs will be artifacts of the decompiler, not real source differences. Need a confidence threshold

### What this means for the project

The DC3 decomp has ~22K functions at AT_LIMIT. Most are blocked by unfixable compiler artifacts (addr reloc, ICF, guard naming). But the **source-fixable** subset — regswaps, expression structure, control flow — is where a guided permuter would shine. Conservative estimate: 800-1200 additional functions could move to higher match% or COMPLETE with a well-built guided system, representing a meaningful chunk of the remaining decomp work.

## Additional Pattern Ideas (from session discussion)

### Re-parenthesization (reverse of expansion)

We added `a - (b - c)` → `c - b + a` expansion, but the reverse is also useful: collapsing a flat chain back into parenthesized form. For cases where the *target* uses parentheses and our source has a flat chain:
```cpp
// Our source (flat):
float r = c - b + a;
// Target:
float r = a - (b - c);
```
The `_collect_terms` infrastructure already handles this — we'd generate re-parenthesized variants by grouping subsets of terms back into parenthesized subexpressions. However, the combinatorics grow quickly (which terms to group?), making this a good candidate for Ghidra-guided generation rather than blind search.

### Multi-guard to conjunction chaining

The existing `_merge_guard_returns` merges guards into `||` chains, and `_guard_to_conjunction` collapses guard+return into `&&`. These could chain together:
```cpp
if (!A) return false;
if (!B) return false;
return C;
```
→ (merge guards) → `if (!A || !B) return false; return C;`
→ (guard to conjunction) → `return (A && B) && C;` → `return A && B && C;`

The hill climber already supports multi-pass composition, so these *should* chain naturally. Worth verifying on real cases whether the hill climber actually discovers this two-step path, or whether a dedicated "multi-guard-to-conjunction" pattern would be more reliable.
