# Docs Index

Top-level sitemap for the DC3 decomp documentation.

## Tools & Workflows

| Doc | Description |
|-----|-------------|
| [tools/INDEX.md](tools/INDEX.md) | Agent tool selection guide — which tool to use when |
| [tools/REFERENCE.md](tools/REFERENCE.md) | Scripts, commands, symbol lookup, linking tools |
| [tools/WORKFLOW.md](tools/WORKFLOW.md) | Decision flowchart for decomp tool selection |

Tool-specific deep docs live in subdirectories:

| Subdir | Description |
|--------|-------------|
| [tools/objdiff/](tools/objdiff/) | objdiff CLI extensions: commands, options, agent workflow, escalation |
| [tools/cache/](tools/cache/) | Decompilation cache layer (SQLite-backed, ~200x speedup) |
| [tools/orchestrator/](tools/orchestrator/) | Orchestrator incremental builds, prompt updates |
| [tools/ghidra/](tools/ghidra/) | PyGhidra MCP service hardening |
| [tools/GHIDRA.md](tools/GHIDRA.md) | Ghidra setup, type seeding pipeline, troubleshooting |

## Decomp Knowledge

| Doc | Description |
|-----|-------------|
| [decomp/TECHNICAL_NOTES.md](decomp/TECHNICAL_NOTES.md) | Compiler quirks, codegen patterns, Xbox 360 specifics |
| [decomp/RB3_REFERENCE.md](decomp/RB3_REFERENCE.md) | Rock Band 3 decomp reference (shared Milo engine) |
| [decomp/patterns/INDEX.md](decomp/patterns/INDEX.md) | Fixable/unfixable codegen patterns catalog |
| [decomp/PRAGMA_INDEX.md](decomp/PRAGMA_INDEX.md) | Xbox 360 compiler pragma documentation |
| [decomp/XBOX360_FLOATING_POINT_CODEGEN.md](decomp/XBOX360_FLOATING_POINT_CODEGEN.md) | Floating-point code generation details |
| [decomp/GAP_ANALYSIS.md](decomp/GAP_ANALYSIS.md) | Decomp coverage gaps and priorities |
| [decomp/LOW_HANGING_FRUIT.md](decomp/LOW_HANGING_FRUIT.md) | Easy-win functions to target |
| [decomp/SUBAGENT_STRATEGY.md](decomp/SUBAGENT_STRATEGY.md) | Parallel agent strategy for batch decomp |

## Reference

| Doc | Description |
|-----|-------------|
| [reference/STYLEGUIDE.md](reference/STYLEGUIDE.md) | Code style conventions |
| [reference/MACROS.md](reference/MACROS.md) | Project macros (MILO_ASSERT, OBJ_MEM_OVERLOAD, etc.) |
| [reference/DATABASE_SCHEMA.md](reference/DATABASE_SCHEMA.md) | decomp.db SQLite schema |
| [reference/PRIORITIZATION.md](reference/PRIORITIZATION.md) | Function prioritization model |
| [reference/FREE60_XEX_FORMAT.md](reference/FREE60_XEX_FORMAT.md) | Xbox 360 XEX executable format |

## Strategy & Scoring

| Doc | Description |
|-----|-------------|
| [meta-strategy/INDEX.md](meta-strategy/INDEX.md) | Meta-strategy overview — prioritization framework |
| [meta-strategy/SCORING_MODEL.md](meta-strategy/SCORING_MODEL.md) | Ease x Impact x Confidence scoring formulas |
| [meta-strategy/SQL_QUERIES.md](meta-strategy/SQL_QUERIES.md) | Ready-to-use database queries for finding targets |
| [meta-strategy/GOALS.md](meta-strategy/GOALS.md) | Realistic decomp targets and success metrics |

## Context Enrichment

| Doc | Description |
|-----|-------------|
| [context-enrichment/INDEX.md](context-enrichment/INDEX.md) | Precomputed context injection pipeline and A/B testing |

## Dynamic Analysis

| Doc | Description |
|-----|-------------|
| [tools/UNICORN_FUNCTION_RUNNER.md](tools/UNICORN_FUNCTION_RUNNER.md) | Unicorn runner usage and design overview |
| [unicorn_runner/PHASE1_DESIGN.md](unicorn_runner/PHASE1_DESIGN.md) | Phase 1 design: differential function execution |

## Code Transformation

| Doc | Description |
|-----|-------------|
| [permuter/INDEX.md](permuter/INDEX.md) | C++ Permuter: tree-sitter based source permutation for register allocation |
| [permuter/evolution/OVERVIEW.md](permuter/evolution/OVERVIEW.md) | Permuter evolution: primitives, migration, composition |

## Projects

| Doc | Description |
|-----|-------------|
| [vmx128/README.md](vmx128/README.md) | VMX128 SIMD Ghidra support (Xbox 360 AltiVec extensions) |

## Plans

| Doc | Description |
|-----|-------------|
| [plans/GHIDRA_MCP_INTEGRATION.md](plans/GHIDRA_MCP_INTEGRATION.md) | Ghidra MCP integration plan |
| [plans/PYGHIDRA_MCP_XEX_SUPPORT.md](plans/PYGHIDRA_MCP_XEX_SUPPORT.md) | XEX support for pyghidra-mcp |
| [plans/PHASE3_AUTOMATION.md](plans/PHASE3_AUTOMATION.md) | Phase 3: agentic automation and orchestration |
| [plans/unicorn-roadmap.md](plans/unicorn-roadmap.md) | Unicorn runner strategic roadmap |
| [plans/unicorn-runner-performance.md](plans/unicorn-runner-performance.md) | Unicorn performance profiling |
| [plans/unicorn-runner-value.md](plans/unicorn-runner-value.md) | Unicorn value demonstration alongside objdiff/Ghidra |
| [plans/unicorn-structural-probing.md](plans/unicorn-structural-probing.md) | Structural probing beyond yes/no equivalence |

## General

| Doc | Description |
|-----|-------------|
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contributing guidelines |
| [FAQ.md](FAQ.md) | Frequently asked questions |
| [codex-coordination-workflow.md](codex-coordination-workflow.md) | Coordinating with GPT-5.3-Codex via OpenRouter |

## Session Logs

Work session archives live in [sessions/](sessions/) (~145 files, Jan 2025 – Feb 2026). Named by date and topic, e.g. `2026-02-11-x360-linking-pipeline.md`.
