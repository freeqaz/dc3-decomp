# AI-Guided Permuter — Plan Index

Integration of AI (LLM) into the decomp permuter pipeline to improve effectiveness of source matching.

## Documents

| File | Description |
|------|-------------|
| [DESIGN.md](DESIGN.md) | Core architecture, MVP definition, integration points |
| [OFFLINE_PATTERN_COMPILER.md](OFFLINE_PATTERN_COMPILER.md) | Offline AI system that mines git history and proposes new deterministic permuter patterns |
| [SEARCH_SPACE_ANALYSIS.md](SEARCH_SPACE_ANALYSIS.md) | Why brute force works for simple patterns, where AI adds genuine value |
| [PROMPT_DESIGN.md](PROMPT_DESIGN.md) | Prompt engineering: context assembly, structured output, few-shot strategy |
| [VALIDATION.md](VALIDATION.md) | Validation plan: retroactive testing against known fixes, pilot criteria |
| [COST_MODEL.md](COST_MODEL.md) | Cost analysis, model selection, batch economics |

## Status

- **Phase**: Design validation (pre-implementation)
- **Next step**: Validate prompt design against 5 known-good fixes (see VALIDATION.md)
