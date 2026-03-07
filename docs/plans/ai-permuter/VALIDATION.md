# AI-Guided Permuter — Validation Plan

Before building infrastructure, validate that the approach works by testing the prompt against known-good fixes.

## Retroactive Validation (Phase 0)

### Method

Take 5 functions that were recently fixed manually or via the existing permuter. For each:

1. Reconstruct the "before" state (source code before the fix)
2. Assemble the context package (diagnosis, Ghidra, patterns)
3. Send to Claude API with the Tier 1 prompt
4. Check: did the model suggest the fix (or something equivalent)?

### Candidate functions for retroactive testing

These are drawn from recent successful fixes documented in memory and commit history:

| Function | Fix | Pattern | Source |
|----------|-----|---------|--------|
| `ContentLoadingPanel::ShowIfPossible` | `a && x > 1` -> `a && (bool)(x > 1)` | boolean_materialization | 2026-03-06 session |
| `FlowAnimate::Load` | `SuperClass::Load(bs)` -> `SuperClass::Load(d.stream)` | prologue_kill_param | MEMORY.md |
| Various DataNode constructors | Remove `mValue.object = nullptr` | union_overlap | MEMORY.md (25+ functions) |
| Functions with `x != 0` -> `x > 0` | Unsigned zero comparison | unsigned_zero | Pattern library |
| Functions with `0.001` -> `0.001f` | Float literal type | float_literal | Pattern library |

### Success criteria

- **3/5 correct**: Approach is validated, proceed to implementation
- **2/5 correct**: Prompt needs refinement, iterate before building infrastructure
- **1/5 or fewer**: Fundamental approach may not work for Tier 1, investigate why

### What "correct" means

The model doesn't need to produce the exact same diff. It needs to:
1. Identify the right pattern/technique
2. Point to the right location in the source
3. Produce a replacement that would compile and improve the match

## Pilot Run (Phase 1)

After retroactive validation passes, run the advisor on 50 live AT_LIMIT functions.

### Function selection

Use `query_functions` to find 50 functions:
- Match range: 85-99% (close enough that small fixes could help)
- Exclude: ICF merged symbols, guard variables, dynamic initializers
- Diverse units: mix of rndobj, os, char, world, synth
- Mix of function types: Load, Save, Draw, Poll, getters, operators

### Metrics to track

| Metric | Target | Definition |
|--------|--------|------------|
| Parse success | > 95% | Model returned valid structured output |
| Build success | > 70% | Suggested edits compiled without error |
| Hit rate | > 10% | Of built edits, improved match% |
| Improvement magnitude | > 2% avg | Average match% improvement on hits |
| False negatives | < 30% | Functions where a known fix exists but AI missed it |
| Cost per improvement | < $1.00 | Total API cost / number of improved functions |

### Pilot procedure

```
for each function in pilot_set:
    1. Run objdiff to get current baseline
    2. Get Ghidra decompilation
    3. Call AI advisor (Tier 1)
    4. For each suggested edit:
        a. Apply edit to source
        b. Compile and score with objdiff
        c. Log result (success/failure, delta, edit details)
    5. If any edit improved: apply best, log as success
    6. If no edits improved: log as miss, save context for analysis
```

### Failure analysis

For every miss, categorize:
- **Wrong pattern**: AI identified a pattern but applied it incorrectly
- **Missed pattern**: A known pattern applied but AI didn't suggest it
- **No pattern exists**: The fix requires a technique not in the pattern library
- **Parse failure**: Model output couldn't be parsed into edits
- **Build failure**: Edit produced invalid C++ (syntax error, missing include)
- **Worse match**: Edit compiled but reduced match%

This categorization drives prompt refinement.

## Comparison Baseline (Phase 2)

Run the existing permuter (without AI) on the same 50 functions. Compare:

| Metric | Existing permuter | AI advisor | Combined |
|--------|------------------|------------|----------|
| Functions improved | ? | ? | ? |
| Total variants tried | ~2500 (50 per fn) | ~250 (5 per fn) | ? |
| Compile cost | ~2500 builds | ~250 builds | ? |
| API cost | $0 | ~$1.50 | ~$1.50 |
| Wall clock time | ~hours | ~minutes + builds | ? |

The key question: does the AI advisor find improvements that the existing permuter misses? If yes, the approaches are complementary (run both). If the AI advisor subsumes the permuter's hits, it's also more efficient.

## Tier 2 Validation (Phase 3)

For functions where Tier 1 found nothing, escalate to Tier 2 with richer context.

Run on the ~40 functions from the pilot that Tier 1 missed. Same metrics. The bar is lower (5% hit rate is still valuable for novel fixes) but the cost per call is higher.

## Go/No-Go Criteria

| Decision | Condition |
|----------|-----------|
| Ship Tier 1 to batch pipeline | Pilot hit rate > 10% AND cost per improvement < $1.00 |
| Invest in Tier 2 | Tier 1 misses include functions where richer context would help |
| Invest in learning loop | At least 10 successful examples accumulated from pilot |
| Abandon approach | Retroactive validation < 2/5 AND prompt iteration doesn't improve it |
| Pivot to deterministic region matching | AI advisor works but only for simple patterns (complex structural issues need the deterministic approach from the WrapText doc) |
