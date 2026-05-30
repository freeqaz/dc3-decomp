# AI-Guided Permuter → moved to `decomp-synth`

The AI-advisor design suite was part of the source permuter, now extracted into
the standalone open-source tool **[`decomp-synth`](../../../../decomp-synth)**.
These docs live (cleaned) under
[`../../../../decomp-synth/docs/ai-advisor/`](../../../../decomp-synth/docs/ai-advisor/):

- `INDEX.md` — section index
- `DESIGN.md` — core architecture, MVP definition, integration points
- `OFFLINE_PATTERN_COMPILER.md` — mining git history to propose new deterministic patterns
- `SEARCH_SPACE_ANALYSIS.md` — where AI adds value over brute-force search
- `PROMPT_DESIGN.md` — prompt engineering: context assembly, structured output, few-shot
- `VALIDATION.md` — retroactive validation plan & pilot criteria
- `COST_MODEL.md` — cost analysis, model selection, batch economics

Nothing from this cluster is DC3-specific, so it all moved.
