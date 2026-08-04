# Permuter Pattern ROI Analysis

> Cross-reference of documented decomp patterns vs existing permuter automation.
> **Updated:** 2026-03-04

## Executive Summary

There are **22 permuter pattern implementations** covering ~72 documented fixable techniques. Cross-referencing pattern docs and session logs identified the highest-ROI gaps and implemented 8 new patterns (Phase 1+2 complete).

The highest-frequency detected patterns across all functions are address relocation (5,295), register swap (1,327), control flow (733), offset swap (445), and scope counter mismatch (430). Of these, control flow and offset swap have the best fixability prospects.

> **See [Correction (2026-08-03)](#correction-2026-08-03-register_swap--declaration_reorder-is-the-wrong-mapping)** — the register-swap bucket (1,327, the second largest) is currently routed to `declaration_reorder`, which is measured inert on that class. The productive axis is liveness/scheduling, and no permuter pattern covers it yet.

## ROI Rankings: New Patterns to Implement

### Tier 1: Trivial AST, High ROI

| Priority | Pattern | Impact | Success | Detection Signal | AST Work |
|----------|---------|--------|---------|-----------------|----------|
| **1** | **bitwise_accumulator** | +10-15% | HIGH | Short-circuit branches vs `and` | Find `result = result && expr`, try `result = result & expr` |
| **2** | **max_to_conditional** | +35% | HIGH | `bl Max` vs inline compare | Find `Max(a,b)` calls, replace with `if (a < b) a = b` |
| **3** | **sizeof_signed_cast** | +6% | HIGH | `srwi` vs `srawi+addze` | Wrap `sizeof()` with `(int)` in signed divisions |
| **4** | **initializer_literal** | +100% | HIGH | Constructor mismatch | Normalize `0.0f`/`false`/`NULL` → `0` in initializer lists |
| **5** | **alloca_intrinsic** | +5% | HIGH | ALLOCA_MISMATCH | Swap `alloca` ↔ `_alloca` |

### Tier 2: Moderate AST, High ROI

| Priority | Pattern | Impact | Success | Detection Signal | AST Work |
|----------|---------|--------|---------|-----------------|----------|
| **6** | **early_return_merge** | +15-40% | HIGH | Repeated cmp+return patterns | Combine guard returns into `||` chain |
| **7** | **bool_return_expr** | +5-15% | HIGH | Reg swap on return path | Convert `if(c) return false; return X` → `return !c && X` |
| **8** | **hoist_sret** | +6% | HIGH | Extra lwz/stw in loop on sret | Hoist sret variable declaration before loop |
| **9** | **fsel_template** | +5-44% | HIGH | Branch vs `fsel` instruction | Replace `if(x<y) x=y` with `Clamp()`/`Min()`/`Max()` template |
| **10** | **pragma_fp_contract** | +1-12% | HIGH | `fmadds` vs `fmuls`+`fadds` | Add/remove `#pragma fp_contract(on/off)` around expressions |
| **11** | **single_return** | +6% | MED | `beq` vs `bne` direction | Pre-initialize result, use single `return` at end |
| **12** | **lazy_call** | +0.4% | MED | Early getter before conditional | Move getter calls into conditional blocks where used |
| **13** | **bit_test_bool** | +5-7% | MED | `rlwinm.` vs `extrwi.` | Extract `(flags & MASK)` to `bool` before `&&` chain |
| **14** | **noreturn_attr** | variable | MED | Dead code after call | Add `__declspec(noreturn)` to never-returning functions |

### Tier 3: Already Implemented

| Existing Pattern | Win Rate | Covers Documented Patterns |
|-----------------|----------|---------------------------|
| `signed_unsigned` | 30% | Signed/Unsigned Cast, Loop Counter, String Iteration, sizeof() |
| `variable_extraction` | 42% | Variable Extraction, Local Bool Extraction |
| `comparison_equivalence` | 10% | Unsigned Zero Comparison, Comparison Style |
| `branch_polarity` | 5% | Branch Polarity Steering, Sequential If, Single Return |
| `declaration_reorder` | 20% | Variable Declaration Order (BSF-guided) |
| `declaration_movement` | — | Move declarations to change register allocation |
| `inline_assignment` | 22% | Inline Assignment |
| `fma_reorder` | 2% | FMA Expression Order |
| `comparison_flip` | 15% | Comparison Operand Order |
| `comma_split` | — | Split comma expressions into separate statements |
| `negation_split` | — | Split `-func()` into `f = func(); f = -f;` |
| `and_split` | — | Split `if (a && b)` into nested ifs |
| `bool_cast` | — | Wrap bool expressions with `bool()` or extract to local |
| `bitwise_accumulator` | — | Replace `&&` with `&` for bool accumulation |
| `max_to_conditional` | — | Replace `Max(a,b)` with `if (a < b) a = b` (and reverse) |
| `early_return_merge` | — | Combine guard returns into `\|\|` chain (and reverse) |
| `bool_return_expr` | — | Convert `if/return false/return true` → `return !cond` |
| `fsel_template` | — | Replace float conditionals with `Min()`/`Max()`/`Clamp()` |
| `ternary_swap` | 0/10 | Ternary vs If-Else |
| `argument_swap` | 5% | Argument Evaluation Order |
| `commutative_swap` | 0/143 | Regroups chains (0 wins — deprioritize) |
| `empty_size_swap` | 0/38 | `empty()` vs `size()==0` (0 wins — deprioritize) |

### Tier 4: Not Automatable (Manual Only)

These require semantic understanding, header changes, or whole-file restructuring:

| Pattern | Why Not Automatable |
|---------|-------------------|
| Explicit Destructor | Requires knowing which classes need `~T() {}` |
| ObjPtr DeferOwner | Requires template/header changes |
| Early Return for Destructor Path | Requires RAII semantic analysis |
| Struct Layout / Padding | Requires header modifications |
| Static Variable Scope | Requires understanding brace intent |
| Function Definition Order | Requires whole-file reordering with $S# tracking |
| Pre-Compute Refs Before Calls | Requires data flow analysis |
| Float/Double Separation | Requires type analysis across expression trees |
| dynamic_cast Replacement | Requires knowing GetObj() availability |

## Detected Pattern Counts (from DB)

Pattern counts are across **all non-excluded functions** (not just AT_LIMIT):

| Pattern | Count | Fixability | Notes |
|---------|------:|------------|-------|
| ADDRESS_RELOCATION | 5,295 | Unfixable | Positional drift from .text size delta |
| REGISTER_SWAP | 1,327 | Partially fixable | ~~Via `declaration_reorder`/`declaration_movement`~~ — see correction below; the productive axis is liveness/scheduling, not declaration order |
| CONTROL_FLOW | 733 | Fixable | Via `branch_polarity` + `and_split` |
| OFFSET_SWAP | 445 | Best fixability | Field reorder, `variable_extraction` |
| PROLOGUE_MISMATCH | 439 | Unfixable | Compiler prologue generation quirks |
| SCOPE_COUNTER_MISMATCH | 430 | Unfixable | `$S` guard naming/numbering |
| ANON_NAMESPACE_HASH | 337 | Unfixable | `?A0x<hash>` path-dependent (patched post-build) |
| LINKER_MERGED | 263 | Unfixable | ICF merged identical functions |
| COMMUTATIVE_OP_ORDER | 172 | Low | 0 wins from `commutative_swap` |
| BOOL_MASK | 101 | Low | Via `bool_cast` (newly added) |
| STATIC_GUARD_COUNTER | 48 | Unfixable | Needs whole-file function reorder |
| COMPARISON_STYLE | 30 | Covered | Via `comparison_equivalence` |
| MAKESTRING_MISMATCH | 27 | AT_LIMIT | Ease assert stripping, unfixable |
| ALLOCA_MISMATCH | 7 | Trivial | `alloca_intrinsic` pattern |
| DEAD_STORE_ELIMINATION | 7 | Low | Compiler optimization quirk |
| FSEL_TERNARY | 6 | Fixable | Replace branch with `Clamp()`/`Min()`/`Max()` |
| BOOLEAN_NEGATION | 2 | Covered | Via `negation_split` |
| FLOAT_PRECISION | 2 | Manual | Cast placement (`0.001` vs `0.001f`) |
| FLOAT_INT_FLOAT | 1 | Manual | Type conversion chain |

### AT_LIMIT Breakdown

| Category | Count |
|----------|-------|
| Total AT_LIMIT | 2,469 |

## Correction (2026-08-03): REGISTER_SWAP → `declaration_reorder` Is the Wrong Mapping

The `REGISTER_SWAP → declaration_reorder / declaration_movement` mapping above is
measured to under-fire, and the reason is structural rather than a tuning problem.

Evidence from three functions taken to or near 100% (see
[fixable-liveness.md](fixable-liveness.md)):

| Function | Result | Declaration-axis evidence |
|----------|--------|---------------------------|
| `ObjectDir::Iterate` | 99.4% → **100%** via a one-line liveness change | 6 reorder variants **byte-identical**, 2 regressed to ~95.8%, 65-candidate beam search **0 improvements** |
| `RndText::FitTextScroll` | 92.7% → 98.2% via call-through-the-local + block scoping | reorder variants: no movement |
| `RndText::SizeCheck` | 96.5% → 99.1% via scheduling then comparison polarity | not the declaration axis |

Why the mapping fails: MSVC's coloring assigns colors from the interference graph and
those colors are **invariant to declaration order**; order only permutes the
color→register mapping, and only where the constraints leave slack. A byte-identical
`.obj` from a reorder is therefore the expected outcome whenever the constraints are
tight — the mutation cannot change the program the allocator sees. To move a register
swap you must change the **interference graph itself**, which means changing what is
live across a call or where a value is materialized.

**Tooling implication.** `declaration_reorder` / `declaration_movement` are
*stack-and-packing* mutations that occasionally hit registers as a side effect. The
missing pattern class is liveness/scheduling mutations. Candidates, in rough order of
how mechanical they look (all unimplemented, all n=1 evidence — treat as hypotheses):

| Proposed pattern | Transform | From |
|------------------|-----------|------|
| `aggregate_projection` | `f(a, b)` ↔ `f(agg.first, agg.second)` where `agg` was just built from `a`, `b` and is unmodified | Lever 1 |
| `member_call_through_local` | `obj->mField->Method(...)` ↔ `local->Method(...)` where `local` already caches `obj->mField` | Lever 2 |
| `out_param_init_strip` | drop `= 0` / `= 0.0f` on a local whose first use is as an out-param the callee writes unconditionally | Lever 2 sub-lever |
| `product_hoist` | collapse `a = f(); b = g(); ... use(a*b)` ↔ `p = f()*g(); ... use(p)` to move the multiply's schedule slot | Lever 3 |
| `decl_scope_into_block` | move a declaration into the inner block that uses it (stack packing, not registers) | Lever 4 |

Two of these (`out_param_init_strip`, `member_call_through_local`) need a safety gate:
the first is only neutral if the callee writes the out-param on every reachable path,
the second only if the local provably still aliases the member at the call site. Neither
is a pure syntactic rewrite, so they belong in the guarded-transform tier alongside
`variable_extraction`, not the free-mutation tier.

Also note for sweep budgeting: a zero-gain sweep over declaration-axis mutations is
**not** evidence that a function is at a register floor. `ObjectDir::Iterate` produced
exactly that result and then went to 100% from one line. See
[unfixable-compiler.md: Strengthened Evidence Standard](unfixable-compiler.md#strengthened-evidence-standard-for-register-class-residuals-2026-08-03).

### Sweep budgeting, measured (2026-08-04)

Numbers for planning a pass over the AT_LIMIT + `REGISTER_SWAP` bucket:

| Quantity | Value |
|---|---|
| Population (`verdict='AT_LIMIT' AND has_register_swap=1 AND is_stub=0 AND excluded=0`) | **836** |
| Blind hit rate (stratified sample, selection rule fixed before inspection) | **3/10 = 30%** → budget **~1 win per 3 functions** |
| Post-filter rate, scoped to statement-level residuals over seven lanes | ~1 per 1.3 (31 triaged, 23 improved, 5 to byte-exact 100%) |

Do **not** budget an unfiltered pass at the post-filter rate. The filter is the
[Triage Split](fixable-liveness.md#triage-split-statement-level-vs-within-one-expression):
open statement-level residuals, skip anything confined to one flat arithmetic expression.
Two DB fields that look like they should route the sweep do not — `tier=` has no
discriminative power (mildly *anti*-correlated: 8 of 9 `A_HAND_FIXABLE` failed, the lone
`B_PERMUTER` was a win) and `current_percent` is stale by up to 12 points. Re-measure per
candidate with `run_objdiff` and `project_dir`.

The real return is not the percentage: that sweep found **12 live behavioural bugs**
(1 in the blind audit, 11 in the lanes) in a bucket labelled unfixable. Budget for that
as the deliverable.

## Instruction Scheduling

**Coverage: none.** No permuter pattern currently mutates where a value is *materialized*
relative to its consumer. This is the second half of the REGISTER_SWAP gap above, and it
is the one that shows up as **volatile**-register swaps (r0, r3-r12, f0-f13) rather than
callee-saved ones — a volatile register cannot be live across a call, so a swap between
two of them is a scheduling or operand-order question, never a live-across-call question.

Worked example, `RndText::SizeCheck` 96.5% → 99.1% (commit `0c2b0c38`): the target
computes the `FontUnit() * AspectRatio()` product *before* the `fcmpu` that consumes it,

```
fmuls  f12, f30, f1
...
fcmpu  cr6, f13, f0
bge    ...
```

while we computed it inside a later `if` condition, putting the `fmuls` in a different
slot and leaving the compare reading a different register (`fcmpu cr6, f0, f12` / `ble`).
Collapsing two separate locals into one `float fontSize = font->FontUnit() *
font->AspectRatio();` fixed the schedule, and all nine f30↔f31 / f12↔f13 swaps resolved
on their own. Only then did flipping the two float compares to the target's operand
order become productive.

**Ordering constraint for any implementation:** schedule first, polarity second. Flipping
a comparison before the producing arithmetic is in the right slot just moves the swap to
the other side of the compare, which scores as a neutral wash and will teach a beam
search that the polarity mutation is useless. A scheduling pattern must therefore be
sequenced *ahead* of `comparison_flip` / `branch_polarity` in the search, not composed
freely with them.

**Candidate transforms** (unimplemented; n=1 each — see the table in the correction
above): `product_hoist` (collapse `a = f(); b = g(); … use(a*b)` ↔ `p = f()*g(); … use(p)`),
and more generally hoisting or sinking a pure sub-expression across a statement boundary
without changing its operands. Neutrality is easy to argue for side-effect-free operands
and hard otherwise, so the gate is "both callees are known pure or are the same two
member reads" rather than a general dataflow proof.

Full pattern writeup with before/after source:
[fixable-liveness.md: Lever 3](fixable-liveness.md#lever-3--fix-the-schedule-first-then-the-comparison-polarity).

> **Anchor note.** objdiff-cli's `REGISTER_SWAP` secondary hint points at
> `docs/decomp/patterns/permuter-roi.md#instruction-scheduling`. That **filename does not
> exist in dc3-decomp** — `permuter-roi.md` is the RB3 name for this document. The
> correct DC3 path is
> `docs/decomp/patterns/PERMUTER_ROI_ANALYSIS.md#instruction-scheduling`, which this
> section provides. See the cross-repo naming note in [INDEX.md](INDEX.md#dc3--rb3-doc-filename-divergence).

## Existing Patterns with 0 Wins

These 3 patterns should be reviewed:

| Pattern | Attempts | Issue | Recommendation |
|---------|----------|-------|----------------|
| `commutative_swap` | 143 | Regroups chains but PPC compiler doesn't vary on this | Keep but deprioritize |
| `empty_size_swap` | 38 | `empty()` vs `size()==0` rarely the root cause | Keep but deprioritize |
| `ternary_swap` | 10 | Low attempt count; the pattern IS valid but rare | Keep, increase coverage |

## Objdiff Detection → Permuter Pattern Mapping

| Objdiff Signal | Current Permuter | Gap Pattern |
|---------------|-----------------|-------------|
| `CONTROL_FLOW` | `branch_polarity`, `and_split`, `bool_return_expr`, `early_return_merge` | ✓ well covered |
| `REGISTER_SWAP` | `declaration_reorder`, `declaration_movement` | **Real gap: no liveness or scheduling mutations.** See correction below |
| `OFFSET_SWAP` | (none) | Could trigger `variable_extraction` or field reorder, or scope-a-declaration-into-its-using-block ([Offset Swap fix 4](fixable-declarations.md#offset-swap)) |
| `COMPARISON_STYLE` | `comparison_equivalence` | ✓ covered |
| `COMMUTATIVE_OP_ORDER` | `commutative_swap` | ✓ covered (0 wins though) |
| `BOOL_MASK` | `bool_cast` | ✓ covered |
| `BOOLEAN_NEGATION` | `negation_split` | ✓ covered |
| `FLOAT_PRECISION_MISMATCH` | (none) | `sizeof_signed_cast`, `initializer_literal`, cast placement |
| `ALLOCA_MISMATCH` | (none) | `alloca_intrinsic` |
| `FSEL_TERNARY` | `fsel_template` | ✓ covered |
| `MAKESTRING_TEMPLATE_MISMATCH` | (none) | Needs `.Str()` insertion |
| `STATIC_GUARD_COUNTER` | (none) | Needs whole-file function reorder |
| `PROLOGUE_MISMATCH` | (skip — unfixable) | — |
| `SCOPE_COUNTER_MISMATCH` | (skip — unfixable) | — |
| `LINKER_MERGED` | (skip — unfixable) | — |
| `ADDRESS_RELOCATION_NOISE` | (skip — unfixable) | — |

## Implementation Plan

### Phase 1: Done
`negation_split`, `and_split`, `bool_cast` — implemented with tests.

### Phase 2: Done
`bitwise_accumulator`, `max_to_conditional`, `early_return_merge`, `bool_return_expr`, `fsel_template` — implemented with tests. All 112 tests pass.

### Phase 3: Data-driven priorities (from commit history mining, 2026-03-09)

Commit history analysis of 956 function improvements across 11 baselines revealed:

**Fix existing patterns (highest ROI — proven wins in history but 0% permuter rate):**
1. **Fix `ternary_swap` relevance** — 32.4% of human improvements involve ternary changes, but permuter has 0/10 wins. Root cause: `relevant()` fires on any branch opcode mismatch (too broad), wasting budget. Fix: tighten to ternary-specific signals, boost when Ghidra shows ternary patterns.
2. **Fix `empty_size_swap` relevance** — 6.9% of improvements, 0 permuter wins. Root cause: only fires on `divw`/`divwu` signal, but real codegen difference includes `cmplw` vs `subf`+`clrrwi`.

**New patterns (from unclassified improvements):**
3. `reference_elimination` — inverse of `member_ref_bind`: remove `auto& ref = m[i]; ref.foo` → use `m[i].foo` directly. Easy AST transform.
4. `const_ref_swap` — `Type copy = expr` ↔ `const Type& ref = expr`. Affects copy ctor codegen.
5. `static_init_explicit` — add explicit `= nullptr/false/0` to file-scope statics. Trivial.
6. `find_operand_order` — swap `end() != find()` to `find() != end()`. Comparison order matters.

**Existing priorities (still valid):**
7. `pragma_fp_contract` — add/remove `#pragma fp_contract(on/off)` around expressions
8. `hoist_sret` — hoist loop variable for sret register matching
9. `alloca_intrinsic` — swap `alloca` ↔ `_alloca`
10. `noreturn_attr` — `__declspec(noreturn)` insertion

### Commit History Mining Tool

`scripts/analysis/mine_patterns.py` — mines cached baselines for pattern validation and discovery. See `docs/sessions/2026-03-09-commit-history-pattern-mining.md` for full analysis.
