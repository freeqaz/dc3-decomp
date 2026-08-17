# Meta-Strategy: Decomp Prioritization

This directory contains the strategic framework for prioritizing decomp work using an **ease x impact x confidence** scoring model, enhanced with **pattern-based fixability analysis**.

## Core Concept

Instead of working through functions linearly or by match percentage alone, we score each function on four dimensions:

- **Ease**: How likely can we get a clean match quickly?
- **Impact**: How much does matching this function improve the rest of the project?
- **Confidence**: How sure are we about what the function does?
- **Reachable100**: Can this function actually reach 100% match? (pattern-based)

Priority = Ease x Impact x Confidence x (1.5 if reachable_100 else 0.5)

## Critical Reality Check

**~80% of near-match functions have unfixable compiler/linker patterns.**

| Category | Count (est.) | Achievable | Notes |
|----------|-------------|------------|-------|
| Can reach 100% | ~4,000 | 100% | No unfixable patterns |
| LINKER_MERGED | ~16,000 | 97-99.5% | Identical COMDAT Folding |
| BOOL_MASK | ~2,000 | ~97% | Compiler bool handling |
| ASSERT_REVS | ~1,000 | ~99% | Instruction scheduling |
| XDK (excluded) | ~1,000 | N/A | External SDK |

**Implication**: A 99.5% function with LINKER_MERGED is *done*—accept it and move on.

## Documents

### Reference Documents

| Document | Purpose |
|----------|---------|
| [SCORING_MODEL.md](SCORING_MODEL.md) | Formulas for computing priority scores (pattern-aware) |
| [SQL_QUERIES.md](SQL_QUERIES.md) | Ready-to-use database queries for prioritization |
| [GOALS.md](GOALS.md) | Realistic targets and success metrics |
| [APPENDIX_RESEARCH.md](APPENDIX_RESEARCH.md) | Findings from other decomp projects |

### Related Resources

| Resource | Location |
|----------|----------|
| Database schema | [docs/reference/DATABASE_SCHEMA.md](../reference/DATABASE_SCHEMA.md) |
| Pattern reference (fixable vs unfixable) | [docs/decomp/patterns/INDEX.md](../decomp/patterns/INDEX.md) |
| Automation planning (future) | [docs/plans/PHASE3_AUTOMATION.md](../plans/PHASE3_AUTOMATION.md) |
| MCP orchestrator tools | [CLAUDE.md](../../CLAUDE.md) (Orchestrator MCP Tools section) |

## Key Insights

1. **Pattern-based fixability is the most important factor.** A function with LINKER_MERGED at 99% cannot reach 100%—don't waste time on it.

2. **The project already has strong tooling.** The gap is using existing capabilities systematically.

3. **~80% of near-matches are at their limit.** Only ~20% of 90%+ functions can reach 100%. Triage first.

4. **Parallel approach works.** Run bulk attempts AND prioritized deep work simultaneously.

## Review Decisions (2026-01-27)

Decisions made during documentation review:

| Question | Decision | Rationale |
|----------|----------|-----------|
| **Scoring weights** | Keep current, validate empirically | -40 LINKER_MERGED, -30 BOOL_MASK, -25 ASSERT_REVS are reasonable. Run `check_pattern_distribution.sh` to validate. |
| **Call graph threshold** | Validate before building | Run `validate_call_graph.sh`. If < 10 functions have 20+ callers, skip infrastructure. |
| **decomp.me integration** | Skip | DC3 is less community-driven than Nintendo decomps. Orchestrator already tracks WIP. |
| **decomp-permuter support** | Skip | PowerPC support uncertain. Only 30% success on REGISTER_SWAP. 80% of issues are LINKER_MERGED anyway. |
| **FMADDS/64BIT_EXTRACTION penalties** | Skip | These are compiler optimization choices, not source-level issues. Investigated and no fix found. |
| **GOALS metrics** | Achievable | Conservative targets. +500-1000 perfect matches is only 2-4% increase. |

### Validation Scripts

Run from project root:
```bash
./docs/meta-strategy/scripts/quick_stats.sh              # Overview
./docs/meta-strategy/scripts/validate_call_graph.sh      # Q2: Call graph value
./docs/meta-strategy/scripts/check_pattern_distribution.sh  # Validate 80% assumption
./docs/meta-strategy/scripts/find_quick_wins.sh          # Find targets
```
