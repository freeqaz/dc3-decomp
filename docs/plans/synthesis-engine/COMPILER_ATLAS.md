# Compiler Behavior Atlas

This document describes a systematic approach to mapping MSVC PPC's codegen
decisions through controlled micro-program compilation. The goal is to build a
queryable decision tree: given a target instruction pattern, what source
construct most likely produced it?

This extends the existing compiler trace infrastructure in
[`tools/compiler_trace/`](../../tools/compiler_trace/) and the pattern
documentation in [`docs/decomp/patterns/`](../decomp/patterns/).

## Motivation

Every proven decomp pattern was discovered manually:

- Someone stared at a target's `subf.` instruction
- Guessed that subtraction-in-condition might produce it
- Tested it, confirmed, documented it

This process has produced 50+ patterns over months of work. But the exploration
is unsystematic — we discover patterns when we happen to encounter them, not
when we need them.

A compiler behavior atlas inverts this. Instead of "encounter mismatch, guess
source," it builds "instruction pattern -> source template" mappings
proactively, so the search engine can look up answers instead of guessing.

## What Already Exists

The project already has strong foundations for this:

- **[`tools/compiler_trace/invoker.py`](../../tools/compiler_trace/invoker.py)** —
  wraps cl.exe with project flags, generates `/FAs` assembly listings
- **[`tools/compiler_trace/asm_diff.py`](../../tools/compiler_trace/asm_diff.py)** —
  compiles two source variants, normalizes listings, diffs output
- **[`tools/compiler_trace/asm_regmap.py`](../../tools/compiler_trace/asm_regmap.py)** —
  extracts variable-to-register assignments from `/FAs` output
- **[`tools/compiler_trace/bsf_trace.py`](../../tools/compiler_trace/bsf_trace.py)** —
  deep GDB+Valgrind instrumentation of c2.dll's register allocator
- **[`tools/compiler_trace/regmap_solver.py`](../../tools/compiler_trace/regmap_solver.py)** —
  graph coloring simulation from BSF traces or assembly listings
- **[`docs/decomp/MSVC_X360_REGALLOC.md`](../decomp/MSVC_X360_REGALLOC.md)** —
  reverse engineering of the BSF graph coloring algorithm
- **[`docs/decomp/TECHNICAL_NOTES.md`](../decomp/TECHNICAL_NOTES.md)** —
  38 hand-documented compiler patterns
- **[`docs/decomp/patterns/INDEX.md`](../decomp/patterns/INDEX.md)** —
  master pattern index with ROI rankings and decision tree
- **[`docs/decomp/patterns/unfixable-compiler.md`](../decomp/patterns/unfixable-compiler.md)** —
  16 hard compiler-level patterns with detection heuristics

What is missing is the systematic generation and indexing layer that turns these
tools into a searchable corpus.

## Design

### Micro-Program Corpus

A micro-program is a minimal C++ function that isolates one codegen decision.
Each micro-program has:

- a **base form** and one or more **variant forms** that differ by exactly one
  source-level feature
- identical semantics across all variants (same inputs produce same outputs)
- a controlled compilation environment (same flags, same PCH, same includes)

Example micro-program family — comparison operator codegen:

```cpp
// base: unsigned != 0
int cmp_neq(unsigned x) { return x != 0 ? 1 : 0; }

// variant: unsigned > 0
int cmp_gt(unsigned x) { return x > 0 ? 1 : 0; }

// variant: signed != 0
int cmp_neq_signed(int x) { return x != 0 ? 1 : 0; }

// variant: explicit bool cast
int cmp_bool(unsigned x) { return (bool)x ? 1 : 0; }
```

Each is compiled with project-identical flags. The assembly diff between base
and variants reveals exactly what source-level change drives which instruction
change.

### Codegen Dimensions

The atlas should systematically explore these codegen decision axes:

**Comparison & Branching**
- signed vs unsigned comparison operators
- `!=` vs `>` vs `>=` for zero checks
- `(bool)` cast vs bare comparison
- negated conditions (`!x` vs `x == 0`)
- short-circuit `&&`/`||` vs nested if
- branch polarity (if-then vs if-not-else)

**Arithmetic & Bitwise**
- add vs subtract for equivalent expressions
- multiply vs shift
- XOR constant width (u8 vs u32)
- NOR peephole conditions
- `fabs()` vs conditional negate
- FMA expression orderings (`a*b + c` vs `c + a*b` vs `-(a*b - c)`)

**Type System**
- signed vs unsigned loop counters
- int vs short vs char intermediates
- float vs double literals and intermediates
- pointer casts (`(int)p` vs `(intptr_t)p`)
- enum vs int in switch statements
- bool vs int return types

**Variable Lifetime & Pressure**
- early declaration vs declaration-at-use
- explicit temp vs inline expression
- parameter reuse vs local copy
- reference binding vs repeated member access
- static const vs inline literal (float)
- alloca vs stack array

**Control Flow Shape**
- if/else vs ternary
- if-return vs if-else
- switch vs if-chain
- for vs while vs do-while
- early return vs single return
- goto vs structured control flow

**Call & Return**
- tail call eligibility conditions
- return-value-in-expression vs stored-then-returned
- chained method calls vs intermediate temps
- virtual dispatch patterns

**Class & Memory**
- constructor initialization order
- explicit vs implicit destructor
- member access via this vs via local reference
- vtable call patterns

### Compilation Pipeline

Each micro-program family runs through:

```
source variants
    |
    v
invoker.py (cl.exe with project flags + /FAs)
    |
    v
asm_diff.py (normalize + diff)
    |
    v
structured diff record:
  - source_feature: "unsigned_gt_zero"
  - instruction_delta: [(beq -> ble)]
  - register_delta: []
  - opcode_pattern: "cmplwi + ble"
  - inverse_pattern: "cmplwi + beq"
```

The output is a structured record, not a human-readable note. Each record maps
a (source feature, instruction pattern) pair.

### Index Structure

The atlas index is a lookup table organized by **target instruction pattern**:

```
target_pattern -> [
  {
    source_feature: "unsigned_gt_zero",
    confidence: "proven",
    example_micro: "cmp_gt.cpp",
    dimensions_tested: ["signedness", "operator"],
    notes: "only for cmplwi; cmpwi uses signed path"
  },
  ...
]
```

This allows reverse lookup: given a target instruction sequence, what source
constructs are known to produce it?

Multiple entries per pattern are expected — the same instruction sequence can
sometimes be produced by different source constructs. Confidence levels
distinguish proven (compiled and verified) from inferred (extrapolated from
similar patterns).

### Interaction Effects

Single-dimension mappings are necessary but not sufficient. The compiler's
decisions interact:

- Type choice affects comparison instruction choice
- Variable lifetime affects register allocation, which affects instruction
  scheduling
- Control flow shape affects which variables are live across branches, which
  affects register pressure

The atlas should capture known interaction effects as compound entries:

```
target_pattern: "subfc + eqv + srwi"
  -> source_feature: "bool_materialization_with_short_circuit"
  -> requires: "&&" operator + "(bool)" cast on second operand
  -> interaction: type_of_second_operand must be integer, not pointer
```

These are harder to discover systematically but are the highest-value entries.
The existing pattern documentation (`docs/decomp/patterns/`) already captures
many interaction effects in prose — the atlas formalizes them.

## Implementation Approach

### Phase 1: Harvest Existing Knowledge

Convert the 50+ proven patterns from `TECHNICAL_NOTES.md` and
`docs/decomp/patterns/*.md` into structured atlas entries. This is
documentation work, not new compilation. It creates the initial index with
proven entries.

Estimated: 50-80 entries from existing documentation alone.

### Phase 2: Systematic Single-Dimension Exploration

For each codegen dimension listed above, create a micro-program family with
5-15 variants. Compile all variants, diff, and record results.

This is automatable: a script generates the micro-program source, compiles via
invoker.py, diffs via asm_diff.py, and produces structured records.

Estimated: 200-400 new entries from ~30 micro-program families.

### Phase 3: Interaction Discovery

Use the single-dimension atlas to identify dimensions that share instruction
patterns (suggesting interaction). Create compound micro-programs that combine
those dimensions and test combinations.

This is semi-automated: the atlas identifies which dimensions to cross, but the
micro-programs need manual design for non-trivial interactions.

### Phase 4: Search Integration

Connect the atlas to the permuter's proposal generation:

1. When objdiff reports a mismatched instruction cluster, extract the target's
   instruction sequence.
2. Look up the atlas for source features that produce that sequence.
3. Generate proposals that apply those source features to the current function.

This turns blind pattern search into targeted, evidence-based proposal
generation.

## Micro-Program Design Principles

### Isolation

Each micro-program should test exactly one codegen variable. Avoid testing
comparison operators in a function that also has complex control flow — the
control flow will interact with the comparison and obscure the result.

### Realism

Micro-programs should use the same types, calling conventions, and header
environment as the real project. A pattern that works in isolation but not under
the project's PCH/include environment is misleading.

Use the project's actual compiler flags and PCH. The invoker.py infrastructure
already handles this.

### Stability

Mark entries with the compiler version and flags used. MSVC PPC is frozen (no
updates), so entries should be permanently valid, but flag changes (e.g.,
optimization level) can change results.

### Negative Results

Record when a source change does NOT affect codegen. These are equally
valuable — they tell the search engine not to waste budget on that
transformation.

Example: "Commutative addition order (`a+b` vs `b+a`) produces identical code
under all tested conditions." This prevents the permuter from trying
commutative swaps.

## Relationship to Other Systems

### Permuter Patterns

The atlas does not replace patterns. Patterns are source-to-source
transformations; the atlas is instruction-to-source mappings. The atlas tells
patterns *when* to fire and *what* to try.

### Ghidra / m2c Guidance

Decompiler output shows what the compiler *did*. The atlas maps what the
compiler does *from source*. Together, they close the loop: decompiler shows
target behavior, atlas maps behavior to source, permuter applies the source
change.

### Target Facts Layer

The atlas is the empirical foundation for the target facts layer described in
the synthesis engine roadmap. Target facts are structured hypotheses about what
the target source probably looks like; the atlas provides the evidence base for
those hypotheses.

## Expected Value

### Quantitative

The 50+ known patterns were discovered over months. Systematic exploration
should discover patterns faster and more completely. Conservative estimate:
2-3x the current pattern count within the first two exploration phases.

Many of these will be minor variants of known patterns, but even minor variants
matter — `cmplwi + ble` vs `cmplwi + bge` is a different atlas entry with a
different source trigger.

### Qualitative

The atlas changes the permuter's search from "try things and see" to "look up
what to try." This is the difference between O(patterns * variants) blind
search and O(mismatches * atlas_entries) targeted search.

For functions where the mismatch is a known pattern, the atlas should produce
the fix on the first or second try, not after 50 builds.

### Ceiling Identification

The atlas also identifies ceilings faster. If a mismatch maps to an atlas entry
marked "no known source-level fix" (e.g., volatile register renaming), the
search can skip it immediately rather than wasting budget.

## Open Questions

- Should the atlas be a SQLite database, a JSON file, or a Python module? The
  lookup patterns suggest a database, but the editorial workflow suggests a file
  format.
- How should confidence decay work? An entry proven on 3 micro-programs is
  stronger than one proven on 1. Should confidence be a count or a tier?
- How much of the atlas can be auto-generated vs requiring manual curation?
  Phase 2 is mostly automatable, but interaction discovery (Phase 3) likely
  needs human guidance.
- Should the atlas include negative results from the permuter's own history?
  e.g., "commutative_swap has 0% win rate across 158 AT_LIMIT functions" is
  atlas-level knowledge.
