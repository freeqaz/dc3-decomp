# AI-Guided Permuter — Prompt Design

The prompt is the product. Getting this right determines whether the system works.

## Prompt Structure

### Tier 1: Pattern Applicator

```
SYSTEM:
  You are a decomp matching expert for Dance Central 3 (Xbox 360, MSVC PPC compiler).
  Your job is to identify which known compiler patterns explain mismatches between
  decompiled source code and the original binary, and produce specific source edits.

  The source code compiles with MSVC for PowerPC (Xbox 360). The target binary is a
  debug build (no LTCG). Matching means producing identical assembly output.

  RULES:
  - Only suggest edits within the function body. Never modify function signatures.
  - Never modify MILO_ASSERT() calls or OBJ_MEM_OVERLOAD macros.
  - Each edit must be a concrete line replacement, not a vague suggestion.
  - Prefer minimal edits. Don't refactor — just fix the match.
  - You may suggest multiple independent edits for the same function.

USER:
  ## Function Source
  ```cpp
  {function_source}
  ```

  ## Objdiff Diagnosis
  {diagnosis_summary}
  - Total instructions: {total}
  - Matching: {matching} ({match_pct}%)
  - Diff ops: {diff_ops_summary}
  - Register swaps: {regswap_summary}
  - Clusters: {cluster_summary}

  ## Ghidra Decompilation (target)
  ```c
  {ghidra_code}
  ```

  ## Known Patterns Reference
  {pattern_docs}

  ## Recent Successful Fixes (similar functions)
  {few_shot_examples}

  ## Task
  Analyze the mismatch between source and target. Identify which known patterns
  (or novel techniques) would improve the match. Return concrete edits.

RESPONSE FORMAT (tool_use / structured output):
  [{
    "pattern": "pattern_name",
    "confidence": 0.0-1.0,
    "line_start": N,
    "line_end": M,
    "original": "the original lines",
    "replacement": "the replacement lines",
    "reasoning": "why this edit should improve the match"
  }]
```

### Tier 2: Novel Fix Advisor

Same structure, but adds:
- m2c decompilation output (for structural anchors)
- Instruction-level diff table (not just summary)
- Broader latitude: "If no known pattern applies, diagnose the structural mismatch and suggest a fix"

## Context Assembly

### What to include

| Context | Source | Size | Priority |
|---------|--------|------|----------|
| Function source | `.cpp` file, extracted by Tree-Sitter | ~50-500 lines | Required |
| Diagnosis summary | `objdiff` via orchestrator | ~20-50 lines | Required |
| Ghidra decompilation | `ghidra-decompile` skill or MCP | ~50-500 lines | Required |
| Pattern docs | `docs/decomp/patterns/*.md` | ~2000 lines total | Tier 1: required, pre-filtered |
| Few-shot examples | `examples_db` | ~100-300 lines (2-3 examples) | High value |
| m2c decompilation | `m2c` tool | ~50-500 lines | Tier 2 only |
| Instruction diff table | `run_diff_inspect mismatches` | ~50-200 lines | Tier 2 only |

### Context window budget

Haiku (200K context): Generous. Even with full pattern docs + 3 few-shot examples, a typical call uses ~5-10K tokens input.

The real constraint is output quality, not context size. Pre-filtering patterns by diagnosis type improves signal-to-noise.

### Pattern pre-filtering

Not all patterns are relevant to every function. Pre-filter based on diagnosis:

| Diagnosis signal | Relevant patterns |
|-----------------|-------------------|
| `cmpw`/`cmplw` diff ops | `fixable-comparison.md`, `fixable-casting.md` |
| Register swaps (callee-saved) | `fixable-declarations.md` |
| Branch polarity diffs | `fixable-control-flow.md`, `fixable-bool-mask.md` |
| `fsel`/`fmul`/`fadd` diffs | `fixable-fsel-fma.md` |
| Large insert/delete clusters | `fixable-control-flow.md` (block reorder) |
| Float/double instruction mismatch | `fixable-operators.md` (literal type) |

This keeps the prompt focused and reduces noise.

## Few-Shot Example Strategy

### Example format

```
### Example: ContentLoadingPanel::ShowIfPossible (87% -> 100%)
**Diagnosis**: Branch polarity mismatch at instruction 12. Target uses `subfc/eqv/srwi`
(branchless boolean materialization), source uses `cmpwi + ble` (branching).
**Pattern**: boolean_materialization
**Edit**: Line 8: `a && x > 1` -> `a && (bool)(x > 1)`
**Why**: The `(bool)` cast triggers MSVC PPC to emit branchless comparison sequence
(`subfc/eqv/srwi/addze/clrlwi`) instead of branch-based comparison.
```

### Example selection

Select 2-3 examples most similar to the current function:

1. **Same mismatch type** — If current function has sign comparison diffs, show an example where sign comparison fix worked
2. **Same function family** — Load functions have different patterns than Draw or Poll functions
3. **Recency** — More recent fixes reflect current codebase state better

### Example database

Simple JSON or SQLite, appended after each successful AI-advised fix:

```json
{
    "id": "uuid",
    "timestamp": "2026-03-07T...",
    "function": "ContentLoadingPanel::ShowIfPossible",
    "unit": "lazer/meta_ham/ContentLoadingPanel",
    "function_type": "method",
    "diagnosis_type": ["branch_polarity"],
    "baseline": 87.0,
    "result": 100.0,
    "pattern": "boolean_materialization",
    "edit_summary": "a && x > 1 -> a && (bool)(x > 1)",
    "source_before": "...",
    "source_after": "...",
    "reasoning": "..."
}
```

## Structured Output

### Option A: Tool use (preferred)

Define a tool schema that the model calls:

```json
{
    "name": "suggest_edits",
    "parameters": {
        "edits": [{
            "pattern": "string",
            "confidence": "number",
            "line_start": "integer",
            "line_end": "integer",
            "original": "string",
            "replacement": "string",
            "reasoning": "string"
        }],
        "diagnosis": "string",
        "skip_reason": "string | null"
    }
}
```

Tool use gives reliable structured output. The `skip_reason` field lets the model say "this function is blocked by ICF/regswap/unfixable compiler behavior" without wasting edits.

### Option B: JSON in text (fallback)

If tool use proves unreliable for a model tier, fall back to asking for JSON in a code block with a parsing layer.

## Prompt Iteration Strategy

The prompt will need refinement. Track these metrics per prompt version:

- **Parse success rate**: Did the response contain valid structured output?
- **Build success rate**: Did the suggested edits compile?
- **Hit rate**: Of edits that compiled, what % improved match?
- **False positive rate**: Edits that compiled but worsened match or broke execution equivalence

Store prompt version in the examples database so we can correlate prompt changes with outcome changes.

## Guardrails

### Hard constraints (in system prompt)

- Never modify `MILO_ASSERT()` or `OBJ_MEM_OVERLOAD` macro invocations
- Never change function signatures or access specifiers
- Never add `#include` directives
- Never suggest edits outside the target function body
- If the diagnosis shows only noise (offset/symbol relocations), return `skip_reason` instead of edits

### Soft constraints (in user prompt)

- Prefer the smallest edit that could explain the mismatch
- If unsure between two patterns, suggest both as separate edits (let compilation decide)
- Flag low-confidence suggestions explicitly (confidence < 0.3)

## Prompt Size Estimates

| Component | Tokens (approx) |
|-----------|-----------------|
| System prompt | ~500 |
| Function source (median) | ~300-800 |
| Diagnosis summary | ~200-400 |
| Ghidra decompilation | ~300-800 |
| Pattern docs (pre-filtered, 3-4 patterns) | ~800-1500 |
| Few-shot examples (2-3) | ~400-900 |
| **Total input** | **~2500-5000** |
| Expected output | ~200-600 |

Well within Haiku's context and speed profile. A Tier 1 call should complete in 1-3 seconds.
