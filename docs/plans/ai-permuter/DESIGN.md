# AI-Guided Permuter — Design

## Problem Statement

The decomp permuter generates 50-100 source variants per function using pattern rules. Most variants are wasted because the permuter doesn't understand *why* a function doesn't match — it just tries every applicable syntactic transformation.

Meanwhile, when a human (or Claude in conversation) looks at the same function with objdiff output and Ghidra decompilation, they can usually diagnose the issue and suggest 2-3 targeted fixes. This diagnosis capability is the bottleneck, not compilation throughput.

**Goal**: Use AI to perform the diagnosis step once per function, then let cheap compilation validate the suggestions.

## Core Principle

```
AI calls are expensive. Compilation is cheap.
One smart diagnosis replaces 100 blind mutations.
```

The AI never touches the build/test loop directly. It proposes edits in a structured format. The existing deterministic pipeline (scorer, objdiff, hill climber) validates them.

## Architecture

```
                         ONE API call per function
                                  |
                                  v
┌──────────────┐    ┌──────────────────┐    ┌──────────────┐
│   Context    │    │   AI Advisor     │    │   Scorer     │
│   Assembly   │───>│                  │───>│   (existing) │
│              │    │  Returns 5-15    │    │              │
│ - source     │    │  structured      │    │ - compile    │
│ - diagnosis  │    │  edit            │    │ - objdiff    │
│ - ghidra     │    │  suggestions     │    │ - score      │
│ - patterns   │    │                  │    │ - apply best │
│ - examples   │    └──────────────────┘    └──────────────┘
└──────────────┘                                   │
       ^                                           │
       │              Learning Loop                │
       └───────────── success/failure log ─────────┘
```

## Two Tiers

### Tier 1: Pattern Applicator (primary MVP)

**Model**: Haiku-class (~$0.03/function)

**What it does**: Takes the function source, objdiff diagnosis, and the full pattern library from `docs/decomp/patterns/`. Asks: "Which known patterns apply to this function, and what are the specific line-level edits?"

This is classification + template application. The pattern library already documents dozens of proven fixes with before/after examples. The AI's job is to recognize which ones apply to a specific function and produce the concrete source edit — something the hand-coded pattern rules only partially cover.

**Why this is high value**: The pattern library represents hundreds of hours of hard-won knowledge. The 30+ Python pattern files in `scripts/permuter/patterns/` implement a subset of this knowledge as syntactic rules. But many patterns are too context-dependent for simple AST matching (e.g., "boolean materialization requires a `(bool)` cast when one operand is a comparison and the other is a short-circuit `&&`"). An LLM can apply these naturally.

**Expected hit rate**: 15-25% of AT_LIMIT functions have a known-pattern fix that the existing permuter misses.

### Tier 2: Novel Fix Advisor (after Tier 1 is validated)

**Model**: Sonnet/Opus-class (~$0.10-0.30/function)

**What it does**: For functions where Tier 1 finds nothing, provides full diagnostic analysis with richer context: Ghidra decompilation, m2c output, instruction-level diff. Asks the model to diagnose the structural mismatch and suggest source-level fixes.

This handles the long tail — compiler quirks, scheduling issues, and patterns not yet documented.

**Expected hit rate**: 5-15% of remaining functions. Lower hit rate but catches genuinely novel issues.

## Integration Points

### New module: `scripts/permuter/ai_advisor.py`

```python
class AIAdvisor:
    """One API call per function -> list of suggested edits."""

    def __init__(self, tier: int = 1, model: str = "claude-haiku-4-5-20251001"):
        self.tier = tier
        self.model = model
        self.examples_db = ExamplesDB()  # successful fixes for few-shot

    def advise(self, ctx: FunctionContext) -> list[SuggestedEdit]:
        """Assemble context, call API, return structured edits."""
        prompt = self._build_prompt(ctx)
        response = self._call_api(prompt)
        return self._parse_edits(response, ctx)

    def _build_prompt(self, ctx: FunctionContext) -> dict:
        """The prompt is the product. See PROMPT_DESIGN.md."""
        ...
```

### New type: `SuggestedEdit`

```python
@dataclass
class SuggestedEdit:
    pattern: str           # which pattern/technique ("boolean_materialization", "unsigned_zero", "novel")
    description: str       # human-readable explanation
    confidence: float      # 0.0-1.0
    line_start: int        # in the function body
    line_end: int
    replacement: str       # the new source code for those lines
    reasoning: str         # why this edit should help (for logging/review)
```

### Variant generation

```python
# In generator.py or new ai_generator.py

def generate_ai_variants(ctx: FunctionContext, advisor: AIAdvisor) -> list[Variant]:
    suggestions = advisor.advise(ctx)  # ONE API call
    variants = []
    for s in suggestions:
        modified_source = apply_suggestion(ctx.file_source, ctx.func_byte_range, s)
        variants.append(Variant(
            name=f"ai_{s.pattern}_{int(s.confidence*100)}",
            pattern_name="ai_advisor",
            description=s.reasoning,
            source=modified_source,
        ))
    return variants
```

### Batch pipeline integration

The AI advisor slots into `batch_auto.py` as an additional variant source:

```python
# In batch_auto.py, alongside existing pattern-based generation

if use_ai_advisor:
    ai_variants = generate_ai_variants(ctx, advisor)
    all_variants = ai_variants + pattern_variants  # AI first, patterns as fallback
```

No changes needed to scorer, objdiff integration, or hill climbing logic.

## The Learning Loop

Every success/failure pair is logged:

```json
{
    "function": "RndText::WrapText",
    "unit": "system/rndobj/Text",
    "function_type": "method",
    "baseline": 58.3,
    "suggestion": {
        "pattern": "block_reorder",
        "description": "move constant materialization after markup strip loop",
        "confidence": 0.7
    },
    "result": {
        "match_percent": 72.1,
        "delta": 13.8,
        "build_success": true
    }
}
```

Successful fixes become few-shot examples for future calls. The system improves from its own results without code changes.

The few-shot selection is similarity-based:
- Same function type (Load, Poll, Draw, etc.)
- Same mismatch pattern (block order, regswap, sign choice)
- Same unit family (rndobj, os, char)

See [PROMPT_DESIGN.md](PROMPT_DESIGN.md) for details on few-shot strategy.

## What This Does NOT Do

- **No AI in the compile loop** — No "generate, compile, ask AI again, compile again." Too expensive, too slow. The AI speaks once; the compiler validates.
- **No replacement of existing patterns** — The 30+ pattern rules work. AI is an additional variant source, not a replacement. Run both, keep whichever produces better results.
- **No region matching infrastructure** — The `WrapText` session doc proposes a large deterministic region-matching system. That's a separate project. The AI advisor gets 80% of the value with 20% of the engineering by letting the model do the alignment in its context window.
- **No prompt-to-code generation** — The AI doesn't write arbitrary code. It identifies which known technique to apply and where, producing bounded edits within an existing function body.

## Relationship to Existing Systems

| System | Role | AI Advisor Interaction |
|--------|------|-----------------------|
| `constraint_solver.py` | Deterministic Ghidra-based edits | AI advisor is complementary — handles cases too context-dependent for deterministic rules |
| `patterns/*.py` | Syntactic transformation rules | AI advisor generates the same kind of variants, just selected more intelligently |
| `batch_auto.py` | Batch hill climbing | AI variants feed into the same pipeline |
| `diagnosis.py` | Instruction-level diff analysis | AI advisor consumes diagnosis as input context |
| `ghidra_ast.py` | Ghidra decompilation parsing | AI advisor consumes raw Ghidra text (doesn't need parsed AST) |
| Pattern library (`docs/decomp/patterns/`) | Human-readable fix documentation | AI advisor's primary reference material for Tier 1 |

## Implementation Order

1. **Validate the prompt** against 5 known-good fixes (see [VALIDATION.md](VALIDATION.md))
2. **Build `ai_advisor.py`** with Tier 1 prompt and structured output parsing
3. **Build `examples_db.py`** for few-shot example storage and retrieval
4. **Wire into `batch_auto.py`** with `--ai-advisor` flag
5. **Pilot on 50 AT_LIMIT functions**, measure hit rate
6. **If hit rate > 10%**: expand to full AT_LIMIT sweep, add Tier 2
7. **If hit rate < 10%**: refine prompt based on failure analysis, re-pilot

## Open Questions

- **Model selection**: Is Haiku sufficient for Tier 1, or does pattern application require Sonnet-level reasoning? Validation step will answer this.
- **Context window budget**: How much of the pattern library fits in a single call? May need to pre-filter patterns by diagnosis type.
- **Structured output format**: Tool use vs JSON-in-text vs XML tags? Need to test reliability of edit parsing.
- **Composition**: Should the AI be allowed to suggest combining two patterns (e.g., "unsigned zero comparison AND declaration reorder")? Or one pattern per suggestion?
- **Guardrails**: How to prevent the AI from suggesting edits that break MILO_ASSERT or OBJ_MEM_OVERLOAD macros? Include explicit constraints in prompt.
