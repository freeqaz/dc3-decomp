# Constraint-Directed Synthesis for Decomp Matching

**Date**: 2026-03-05
**Context**: Theoretical design for next-gen permuter that uses constraint solving to handle composability without combinatorial explosion

## The Problem

Functions often need multiple patterns applied together. CharLipSyncDriver::Poll needed 14 changes across 6 pattern categories. Blind composition:

- 11! declaration orderings x 2^10 comparison flips x 2^15 control flow choices = intractable

Hill climbing helps but gets stuck in local optima -- some changes only improve match% when applied *together*.

## Why Z3 Doesn't Quite Fit

The instinct is right but the problem shape is wrong for pure SAT/SMT. Z3 solves `exists x: constraints(x)` over boolean/integer domains. Our problem is: source changes -> **black box compiler** -> binary. We can't express the compiler as Z3 constraints.

But there's a reformulation that works.

## Key Insight: Ghidra Makes Most Choices Deterministic

The search space explodes because each pattern has multiple *directions* to try. Ghidra tells you which direction is correct for most categories:

| Category | Blind choices | With Ghidra |
|----------|--------------|-------------|
| Declaration order | N! (up to 5040) | **1** (var first-use order) |
| Comparison direction | 2^N | **1 each** (Ghidra shows form) |
| Control flow (&&/guard) | 2^M | **1 each** (CF skeleton) |
| Expression shape | K variants | **1** (expression structure) |
| Null check presence | 2^P | **1 each** (guard absent = remove) |
| Temp extraction | combinatorial | **~2** (var count delta) |

With Ghidra, most dimensions collapse from "search" to "lookup". The composition of 6 lookups is 1 candidate.

## Architecture: Four-Phase Synthesis

```
Phase 1: Constraint Extraction
  Ghidra cache --> Target specification:
    - Variable first-use order
    - Control flow skeleton
    - Expression shapes
    - Null check locations
    - savegpr/savefpr counts

  objdiff diagnosis --> Diff constraints:
    - cmpw vs cmplw (signed/unsigned)
    - beq vs bne (branch polarity)
    - Register swap pairs
    - Prologue delta

Phase 2: Constraint Resolution
  For each source "choice point":
    If Ghidra provides signal --> resolve to 1
    If objdiff provides signal --> resolve to 1-2
    If neither --> leave as free variable (blind)
  Result: resolved_choices + free_vars

Phase 3: Composite Generation
  Apply all resolved choices simultaneously
  Cross-product only free variables
  Typically: 1 resolved x 4-8 free = 4-8 total candidates

Phase 4: CDCL-style Refinement
  Score best candidate
  If not 100%: re-diagnose remaining diff
  Derive NEW constraints from residual
  Repeat Phase 2-3 (conflict-driven learning)
```

### Phase 4: The SAT Solver Analogy

Phase 4 is where the SAT solver analogy actually applies. CDCL (Conflict-Driven Clause Learning) works by:

1. Make assignments (apply transforms)
2. Propagate (build, score)
3. If conflict (match% didn't improve), learn a clause ("this combination doesn't work")
4. Backtrack and try different free variable assignments

For decomp, "learning a clause" means: if applying transforms A+B together made things worse, record that as a negative constraint and don't try that combination again.

## Where Z3 Actually Helps: Declaration Order

The one place real constraint solving pays off is register allocation. This is a proper assignment problem:

```python
from z3 import *

# N source variables need N callee-saved registers
# Ghidra tells us the target assignment (approximately)
# Declaration order determines our assignment (first decl -> r31)

n = 5  # variables
order = [Int(f'pos_{i}') for i in range(n)]
solver = Solver()

# Permutation constraints
solver.add(Distinct(*order))
for v in order:
    solver.add(v >= 0, v < n)

# Ghidra-derived: var_A should be in r31 (position 0)
solver.add(order[var_A_idx] == 0)

# Ghidra-derived: var_B should be in r30 (position 1)
solver.add(order[var_B_idx] == 1)

# Dependency: var_C must be declared before var_D (use-before-decl)
solver.add(order[var_C_idx] < order[var_D_idx])

# Solve -> exactly 1 valid ordering (or UNSAT = unfixable)
if solver.check() == sat:
    model = solver.model()
    # Generate single reordering
```

This takes N! (5040 for 7 vars) down to 1 via constraint solving. And it tells you when a regswap is **provably unfixable** (UNSAT) -- no more wasting 30 variants on a dead end.

## Data Model

```python
@dataclass
class ConstraintSet:
    # Deterministic (from Ghidra)
    decl_order: list[str] | None        # target variable ordering
    cf_direction: dict[int, str]        # line -> "conjunction" | "nested_if" | "guard"
    expr_shapes: dict[int, str]         # line -> target expression structure
    null_checks: dict[int, bool]        # line -> should guard exist?

    # Probabilistic (from objdiff)
    sign_choices: dict[int, str]        # line -> "signed" | "unsigned"
    branch_polarity: dict[int, str]     # line -> "beq" | "bne"

    # Derived
    target_gpr_saves: int | None        # from Ghidra __savegprlr_N
    target_fpr_saves: int | None        # from Ghidra __savefpr_N

    def free_variable_count(self) -> int:
        """Dimensions still unresolved -- determines search space size."""
        ...

    def is_provably_unfixable(self) -> bool:
        """UNSAT check -- can constraints be satisfied simultaneously?"""
        ...
```

## Synthesis Loop

```python
def synthesize(func, source):
    constraints = extract_constraints(func)  # Phase 1

    if constraints.is_provably_unfixable():
        return "UNFIXABLE", reason

    # Phase 2: resolve all deterministic constraints into edits
    edits = resolve_deterministic(constraints, source)

    # Phase 3: enumerate only free variables
    for free_assignment in enumerate_free(constraints):
        candidate = apply_edits(source, edits + free_assignment)
        score = build_and_score(candidate)
        if score == 100:
            return "COMPLETE", candidate

    # Phase 4: CDCL refinement on best candidate
    best = get_best_candidate()
    new_constraints = diagnose_residual(best)
    constraints.update(new_constraints)
    # ... repeat
```

## Expected Search Space Reduction

- Blind permuter: 100-10000 variants per function
- Constraint-directed: 1-8 variants per function
- With Z3 for decl order: often exactly 1

## What This Unlocks

1. **Batch viability** -- at 1-8 builds per function instead of 100+, sweep thousands of functions overnight
2. **Unfixable detection** -- UNSAT tells you to stop immediately, not after 50 failed variants
3. **Composability** -- resolved constraints compose trivially (independent edits). No explosion
4. **Learning** -- when a constrained candidate fails, learn which Ghidra signals are unreliable and weight them accordingly

## Limitations

### Ghidra noise
Ghidra normalizes comparisons (breaking comparison flip guidance), inserts its own casts (breaking cast guidance), and sometimes restructures control flow differently than the original. Approximately 70% of pattern categories get reliable signal; the remaining 30% (casts, codegen-specific transforms) stay as free variables.

### Compiler as black box
We can't model the MSVC PPC compiler's register allocator, instruction scheduler, or optimization passes as logical constraints. The constraint-directed approach works at the *source structure* level, not the *codegen* level. Some mismatches (stack spill scheduling, dead register allocation) are invisible to source-level analysis.

### Ghidra variable matching
Ghidra calls things `local_38` while our source uses `mParams`. Matching requires positional/contextual heuristics (Nth declaration, argument to specific call, accessed at specific struct offset). `ghidra_var_match.py` has positional matching; call-return and struct-offset matching are not yet implemented.

## Implementation Path

1. Add `ConstraintSet` dataclass to `types.py`
2. Add `extract_constraints()` combining `ghidra_ast` extractors + objdiff diagnosis
3. Add `resolve_deterministic()` mapping resolved constraints to source edits
4. Add `apply_edits()` for multiple non-overlapping edits (SourceEditor already supports this)
5. Wire into `hill_climber.py` as `--constrained` mode that runs before blind patterns
6. Optional: Z3 for declaration order specifically (pip install z3-solver)

## Relationship to Existing Tools

- **ghidra_cache.py**: provides 18,929 cached decompilations (Phase 1 data source)
- **ghidra_ast.py**: expression structure, control flow skeleton, variable order extraction (Phase 1 parsers)
- **ghidra_var_match.py**: variable-to-register inference (Phase 2 for decl order)
- **ghidra_expr_match.py**: expression tree comparison (Phase 2 for expression shapes)
- **scorer.py**: build + objdiff scoring (Phase 3 evaluation)
- **hill_climber.py**: iterative improvement loop (Phase 4 host)

The constraint-directed approach doesn't replace the existing permuter -- it adds a fast pre-pass that resolves what it can deterministically, then falls back to blind patterns for the remaining free variables.
