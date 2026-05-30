# Synthesis Engine → moved to `decomp-synth`

The synthesis-engine **design and architecture** docs were part of the source
permuter, which has been extracted into the standalone open-source tool
**[`decomp-synth`](../../../../decomp-synth)**. They now live (cleaned) under
[`../../../../decomp-synth/docs/architecture/`](../../../../decomp-synth/docs/architecture/):

- `README.md`, `ROADMAP.md` — engine overview & execution roadmap
- `COMPILER_ATLAS.md`, `PATTERN_MINING.md` — compiler RE & transfer learning
- `INSTRUCTION_ATTRIBUTION.md`, `TARGET_FACTS.md` — evidence layers
- `DIFFERENTIAL_TESTING.md` — black-box compiler testing
- `IL_PERMUTER.md`, `IL_TYPE_CONTROL.md` — IL-level design
- `DEEP_ANALYSIS_PLAN.md`, `MSVC_ROADMAP.md` — compiler back-end reverse-engineering

See [`../../../../decomp-synth/docs/README.md`](../../../../decomp-synth/docs/README.md) for the full index.

## Stays here (DC3-specific)

These document concrete results against DC3's binary and remain project-side:

- [HEADER_LEVEL_CLUSTERING.md](HEADER_LEVEL_CLUSTERING.md) — catalog of DC3 header-level fixes (1,500+ function improvements)
- [OG_REGRESSION_ANALYSIS.md](OG_REGRESSION_ANALYSIS.md) — impact analysis of header changes vs the upstream fork
