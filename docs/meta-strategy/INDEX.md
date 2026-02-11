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

### Strategy Documents

| Document | Purpose |
|----------|---------|
| [SCORING_MODEL.md](SCORING_MODEL.md) | Formulas for computing scores (pattern-aware) |
| [PHASE1_QUICK_WINS.md](PHASE1_QUICK_WINS.md) | Immediate actions: triage, exclusions, SQL queries |
| [PHASE2_INFRASTRUCTURE.md](PHASE2_INFRASTRUCTURE.md) | Pattern detection, call graph, scoring infrastructure |
| [PHASE3_AUTOMATION.md](PHASE3_AUTOMATION.md) | Agentic orchestration and automated target selection |
| [SQL_QUERIES.md](SQL_QUERIES.md) | Ready-to-use database queries for prioritization |

### Reference Documents

| Document | Purpose |
|----------|---------|
| [GOALS.md](GOALS.md) | Realistic targets and success metrics |
| [Pattern Reference](../decomp/patterns/INDEX.md) | Complete pattern reference (fixable vs unfixable) |
| [APPENDIX_RESEARCH.md](APPENDIX_RESEARCH.md) | Findings from other decomp projects |

## Implementation Phases

### Phase 1: Quick Wins (Now)
Use existing tools more effectively:
- **Triage NEAR_COMPLETE functions** - Identify which 606 can actually reach 100%
- **Mark XDK as excluded** - Filter out ~1,000 SDK functions
- SQL queries against decomp.db
- objdiff-cli report analyze for pattern detection
- RB3 reference targeting

**Investment**: ~1 hour
**Payoff**: Immediate better targeting, avoid wasted effort

### Phase 2: Infrastructure (1-2 weeks)
Build scoring infrastructure:
- **Pattern detection columns** - has_linker_merged, reachable_100, etc.
- Call graph validation (validate value before full build)
- Fan-in/fan-out computation (if validated)
- Ease/impact/confidence score columns
- Priority views in database

**Investment**: Moderate
**Payoff**: Systematic prioritization with pattern awareness

### Phase 3: Automation (2-4 weeks)
Automate the agentic workflow:
- Automated target selection (pattern-aware)
- Model escalation tracking
- Parallel agent orchestration
- Continuous rescoring

**Investment**: Significant
**Payoff**: Hands-off progress acceleration

## Trade-off Guidance

| Situation | Recommendation |
|-----------|----------------|
| Fresh start, many easy targets | Phase 1 only |
| Want to find 100%-achievable targets | Phase 1 + pattern triage |
| Saturated easy targets, need systematic approach | Add Phase 2 |
| High parallelism desired, willing to invest | Add Phase 3 |
| Limited time, want immediate progress | Stay in Phase 1 |

## Key Insights

1. **Pattern-based fixability is the most important factor.** A function with LINKER_MERGED at 99% cannot reach 100%—don't waste time on it.

2. **The project already has strong tooling.** The gap is using existing capabilities systematically. Phase 1 can be done today.

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
