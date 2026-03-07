# AI-Guided Permuter — Search Space Analysis

Why can't we just brute-force more? Where does AI actually add value that exhaustive search can't? This document answers those questions by analyzing the actual search space of the current permuter.

## The Current Permuter Search Space

### Simple patterns: exhaustible

Most existing patterns have **small, bounded search spaces** per function:

| Pattern | Variants per function | Search space |
|---------|----------------------|--------------|
| `signed_unsigned` | 3 casts × 2 sides × N comparisons + N swaps | ~10-30 |
| `comparison_flip` | N comparisons | ~3-10 |
| `comparison_equivalence` | N zero comparisons | ~3-8 |
| `float_double_literal` | N float literals | ~5-15 |
| `branch_polarity` | N if-statements | ~3-8 |
| `and_split` | N `&&` conditions | ~2-5 |
| `bool_cast` | N boolean expressions | ~3-8 |
| `alloca_intrinsic` | 1 (swap alloca/_alloca) | 1 |
| `fsel_template` | N float conditionals × 3 forms | ~3-9 |

These are all **linear in the number of matching AST nodes**. A typical function has 5-15 comparison sites, so the total search space for all simple patterns combined is ~50-100 variants. The permuter already exhausts this with `--max-variants 100`.

**You're right**: for these patterns, just trying everything is the optimal strategy. AI adds nothing here. The search space is small, compilation is cheap, and brute force is exhaustive.

### Medium patterns: still exhaustible

| Pattern | Variants per function | Search space |
|---------|----------------------|--------------|
| `variable_extraction` | N extractable call sites | ~5-20 |
| `temp_elimination` | N single-use temps | ~3-10 |
| `reference_elimination` | N multi-use refs | ~3-8 |
| `declaration_reorder` | N! permutations (but BSF-guided → ~3-5) | ~3-5 |
| `statement_reorder` | Pairwise swaps in runs of 2-4 | ~4-8 (capped) |
| `assignment_reorder` | Adjacent swaps | ~3-6 |

Even `declaration_reorder` — which is factorial in theory — is tamed by BSF guidance to just a few candidates. These are all tractable.

### Composition: still manageable

The composer pairs two patterns (e.g., `signed_unsigned` + `variable_extraction`). With ~50 Tier 1 variants and ~20 Tier 2 variants, that's ~1,000 composed variants. The permuter caps this at 30% of the budget and uses beam search for chains. Still tractable.

**Total exhaustible search space: ~100-200 variants per function, fully searched in 2-4 minutes.**

### Where the current permuter IS the right tool

For functions at 90-99% match where the remaining gap is:
- A sign mismatch (`cmpw` vs `cmplw`)
- A missing cast
- A float literal type
- A comparison direction flip
- A declaration reorder

The permuter already finds these. AI would be slower and more expensive for the same result.

## Where Brute Force Breaks Down

The interesting question isn't "can we try more simple patterns?" It's "what patterns can't be expressed as bounded AST transforms?"

### 1. Multi-site edits with dependencies

The current patterns are **single-site**: change one comparison, extract one variable, swap two statements. But many fixes require coordinated changes at multiple sites:

**Example: boolean materialization** (proven 2026-03-06)
```cpp
// Before: two independent-looking edits that must be applied TOGETHER
a && x > 1          // site A: condition
// ...
bool b = x > 1;     // site B: doesn't exist yet, needs to be created

// Fix: a && (bool)(x > 1)  — a SINGLE edit, but knowing to add (bool)
// requires understanding the target's branchless codegen pattern
```

The `bool_cast` pattern tries `(bool)` wrapping on all boolean expressions. It works sometimes. But it doesn't know to apply it specifically to the comparison operand of a short-circuit `&&` — that requires understanding the `subfc/eqv/srwi` instruction pattern in the diagnosis.

**Search space for "apply N edits from different patterns at the right sites":**
- 5 patterns × 10 sites × 10 sites = 500 two-site combinations
- Most won't compile. Many are semantically identical.
- But the right combination is needle-in-haystack.

**This is where AI helps**: Not by searching more, but by saying "the diagnosis shows `subfc/eqv/srwi` which is the branchless boolean materialization pattern, so apply `(bool)` to the comparison inside the `&&` on line 47."

### 2. Structural transforms with large search spaces

Some transforms have combinatorial search spaces that can't be exhausted:

**Statement reorder across regions:**
- Current `statement_reorder`: pairwise adjacent swaps in runs of 2-4 statements. Cap: 8 variants.
- Real problem: a function with 20 top-level statements has 20! ≈ 2.4 × 10^18 orderings.
- Even with dependency constraints reducing legal orderings to ~1000, that's 10x the current budget.

**Variable scope manipulation:**
- Introduce inner braces to shorten lifetime: where? how deep? which variables?
- Split a temp into two shorter-lived temps: which temp? at what point?
- These aren't bounded AST transforms — they require understanding register pressure and scheduling intent.

**Call-gate restructuring:**
- `if (a && b) call()` vs `if (a) { if (b) call() }` vs `if (a) { x = b ? call() : false }`
- 3 forms × N call sites × N conditions = moderate space
- But: the right form depends on the **target's branch structure**, not just the source's AST

### 3. Transforms that require semantic understanding

These can't be expressed as pattern rules at all:

**"Move this setup block after that loop":**
- Requires understanding that two code regions are semantically independent
- Requires knowing which ordering the target prefers (from Ghidra)
- The `statement_reorder` pattern does pairwise swaps on adjacent statements — it can't move a 5-line block past a 20-line loop

**"This function's prologue saves one too many registers because variable X lives too long":**
- Requires understanding that X is declared at the top but first used at line 40
- Fix: move declaration of X down, or add braces to limit scope
- The `prologue_pressure` pattern exists but has very limited scope
- The search space is "every possible scoping arrangement of every variable" — unbounded

**"The target inlines this getter but we're calling it as a function":**
- Requires understanding the inlining boundary
- Fix might be: add `__forceinline`, or manually inline, or restructure to let the compiler decide
- No pattern can enumerate "all possible manual inlinings"

## The Spectrum

```
Search Space Size:
Small ←─────────────────────────────────────────→ Large

BRUTE FORCE WINS                              AI WINS
│                                                    │
│  sign casts                                        │
│  comparison flips                                  │
│  literal types          ┌─ TRANSITION ZONE ──┐    │
│  alloca swap            │                     │    │
│  fsel template          │  multi-pattern      │    │
│                         │  composition        │    │
│                         │  declaration order   │    │
│                         │  statement reorder   │    │
│                         │  scope manipulation  │    │
│                         └─────────────────────┘    │
│                                                    │
│                              block reorder         │
│                              call-gate restructure │
│                              lifetime management   │
│                              novel compiler quirks │
│                              prologue optimization │
│                                                    │
Exhaustible in 100 variants          Combinatorial / requires reasoning
```

## Reframing: What Does AI Actually Do?

AI doesn't search faster. It **reduces the search space** by understanding the problem.

### Level 1: Pattern Selection (low value-add for this project)

"Which of the 55 patterns should I try?" — The permuter already does this with `relevant()` and `priority()` filtering. AI would be marginally better at selection, but since we can afford to try all relevant patterns anyway, selection isn't the bottleneck.

**Verdict**: Not worth an API call. The permuter already handles this.

### Level 2: Pattern Application with Context (moderate value-add)

"I see `subfc/eqv/srwi` in the diagnosis, which means the target uses branchless boolean materialization. The source has `a && x > 1` on line 47. The fix is `a && (bool)(x > 1)`."

This is pattern recognition + context-aware placement. The pattern library documents the technique. The AI knows which AST node to apply it to because it understands the instruction-level diagnosis.

The existing `bool_cast` pattern generates `(bool)` wrappings on every boolean expression. It might stumble onto the right one. But it also generates 8 other variants that are guaranteed to not help. The AI generates 1 variant that's likely to help.

**Verdict**: Marginal for single-pattern cases (brute force already tries everything). But valuable when the right fix is a **specific combination** of pattern + location that the composition system won't enumerate.

### Level 3: Structural Transforms (high value-add)

"The target's Ghidra output shows the markup-stripping loop followed by constant materialization before the main loop. Your source has constant materialization inside the main loop. Move lines 12-18 (the float constant declarations) to after line 45 (end of the markup strip block)."

No existing pattern can express this. `statement_reorder` does adjacent swaps in small windows. This is a **region-level move** guided by Ghidra's structural analysis. The search space of "where to move N lines to" is O(N × M) where M is the number of possible insertion points — hundreds of possibilities for a 100-line function.

The AI doesn't search hundreds of possibilities. It reads the target structure and says "move it here." One suggestion. One compile. Done (or not — but the hypothesis is grounded in evidence, not random).

**Verdict**: This is where AI adds genuine new capability. No amount of brute force covers this.

### Level 4: Novel Diagnosis (highest value-add, lowest hit rate)

"This function has a 16-byte stack frame difference. The target uses 5 separate stack slots for `Symbol` locals across switch cases, but the compiler is aliasing them to one slot. There's no known source-level fix — the compiler's stack layout algorithm doesn't have a knob for this."

This isn't an edit suggestion — it's a **diagnosis** that saves you from wasting hours on an unfixable function. The pattern library has `at-limit-systemic.md` and `unfixable-compiler.md`, but there are always new unfixable patterns that haven't been documented yet.

**Verdict**: Expensive per call, low hit rate for fixes, but saves significant human time by triaging unfixable functions out of the queue.

## Revised Architecture

Given this analysis, the design should shift:

### Don't use AI for what brute force already handles

The 55 existing patterns with bounded search spaces should stay purely deterministic. No API calls needed. The permuter's `relevant()` + `priority()` filtering is already good enough for pattern selection in the small-search-space regime.

### Use AI for the structural transform tier

The real gap is **Level 3**: transforms that require understanding the target's structure and making region-level decisions. These are:

1. **Block reorder**: "move this setup block to after that loop" (guided by Ghidra region comparison)
2. **Scope manipulation**: "add braces here to shorten this variable's lifetime" (guided by prologue analysis)
3. **Call-gate restructuring**: "convert this `&&` chain to nested `if`s at this specific call site" (guided by Ghidra branch structure)
4. **Multi-pattern composition**: "apply unsigned cast to the loop counter AND extract the `.size()` call AND swap declarations 3 and 5" (3 edits that individually may not help but together fix the match)

### Use AI for triage at scale

For the ~2,469 AT_LIMIT functions, the most valuable AI action might not be "suggest a fix" but "classify this function":

- **Fixable by existing permuter** → run permuter (no AI needed)
- **Fixable by structural transform** → generate AI-guided edits
- **Unfixable (compiler/linker)** → skip, document why
- **Needs manual investigation** → flag for human review

A classification pass over 2,469 functions with Haiku costs ~$12. If it correctly triages even 50% of them, it saves enormous human time spent on unfixable functions.

## Updated Tier Model

| Tier | What | AI Role | Search Space | Cost |
|------|------|---------|-------------|------|
| 0 | Simple patterns | None (brute force) | Exhaustible (~100 variants) | $0 |
| 1 | Triage + classification | Classify fixability | N/A | ~$0.005/fn |
| 2 | Structural transforms | Generate specific region-level edits | Combinatorial → 1-5 targeted | ~$0.02/fn |
| 3 | Novel diagnosis | Identify new patterns or confirm unfixable | N/A | ~$0.15/fn |

The key insight from your question: **Tier 0 is already solved.** The existing permuter exhausts the simple pattern search space. AI's value starts at the boundary where search spaces become too large for brute force or where transforms require reasoning about target structure.

## What Kind of Actions Does AI Take?

Concretely, here's what AI-generated edits look like at each level:

### Tier 1 (triage) — no edits, just classification

```json
{
    "function": "SaveLoadManager::Poll",
    "classification": "unfixable_compiler",
    "reasoning": "Branch polarity inversions across all switch cases. ~40 inversions from positive/negative branching preference. No source-level control over branch direction selection.",
    "confidence": 0.9
}
```

### Tier 2 (structural) — specific, multi-line, location-aware edits

```json
{
    "function": "RndText::WrapText",
    "edits": [
        {
            "type": "block_move",
            "description": "Move constant materialization after markup strip loop",
            "move_lines": [12, 18],
            "insert_after_line": 45,
            "reasoning": "Ghidra shows dVar38=30.0, dVar39=60.0 after markup strip. Source has these at function entry. Target order: strip → constants → main loop."
        },
        {
            "type": "call_gate_restructure",
            "description": "Split && into nested if at CanBreakLineAt call site",
            "line": 67,
            "original": "if (temp_r23 != 0 && WordWrap_CanBreakLineAt(...))",
            "replacement": "if (temp_r23 != 0) {\n    if (var_r27 <= 0) {\n        var_r11_2 = 0;\n    } else {\n        var_r11_2 = WordWrap_CanBreakLineAt(...);\n    }\n    ...\n}",
            "reasoning": "Ghidra shows nested guard → call → test pattern, not flat &&."
        }
    ]
}
```

### Tier 3 (novel) — diagnosis + speculative edit

```json
{
    "function": "SomeNewFunction",
    "diagnosis": "Target caches float literal address in callee-saved GPR (r28), reloads value via lfs on each use. Source uses inline 100.0f which caches VALUE in FPR (f31). This causes GPR/FPR prologue type conflict.",
    "edits": [
        {
            "type": "novel",
            "description": "Convert inline float literal to static const",
            "line": 23,
            "original": "float threshold = 100.0f;",
            "replacement": "static const float kThreshold = 100.0f;\nfloat threshold = kThreshold;",
            "reasoning": "Static const forces address-based access (GPR caches address), matching target's lfs-from-address pattern. See float literal GPR caching pattern in MEMORY.md.",
            "confidence": 0.4
        }
    ]
}
```

The difference from brute-force patterns:
- **Location-specific**: "line 67", not "all && expressions"
- **Multi-line**: moves blocks, not just swaps tokens
- **Structurally justified**: references Ghidra output, not just AST shape
- **Can say "unfixable"**: the most valuable output is sometimes "stop trying"
