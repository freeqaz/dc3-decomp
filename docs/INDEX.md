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
| [tools/GHIDRA.md](tools/GHIDRA.md) | Ghidra setup, type seeding pipeline, CLI tools (`/ghidra-search`, `/ghidra-decompile`, `/ghidra-struct`) |

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
| [plans/BUILD_ROADMAP.md](plans/BUILD_ROADMAP.md) | Path to a bootable build — phases, blockers, what's needed |
| [plans/CLEAN_LINK_PROJECT.md](plans/CLEAN_LINK_PROJECT.md) | Clean link project — eliminate `/FORCE` flags, get 1:1 XEX (spans jeff + wibo + dc3-decomp) |
| [plans/FORCE_MULTIPLE_ELIMINATION.md](plans/FORCE_MULTIPLE_ELIMINATION.md) | Eliminating `/FORCE:MULTIPLE` — link architecture, 13,400 LNK4006 duplicates, strategies |
| [plans/LINKED_BINARY_VERIFICATION.md](plans/LINKED_BINARY_VERIFICATION.md) | Re-split linked XEX for ground-truth objdiff comparison (accounts for ICF, COMDAT) |
| [plans/XENIA_HEADLESS_STATUS.md](plans/XENIA_HEADLESS_STATUS.md) | Early plan — see `runtime/XENIA_HEADLESS_STATUS.md` for current |
| [plans/THUNK_SECTION_IMPLEMENTATION.md](plans/THUNK_SECTION_IMPLEMENTATION.md) | Import resolution implementation (COMPLETED) |
| [plans/SCRIPTED_INPUT_IMPLEMENTATION.md](plans/SCRIPTED_INPUT_IMPLEMENTATION.md) | Original plan (COMPLETED) — see `runtime/SCRIPTED_INPUT_TESTING.md` |
| [plans/LBL_SYMBOL_MATCHING.md](plans/LBL_SYMBOL_MATCHING.md) | Fix `lbl_` symbol matching for function-local statics (match% accuracy) |
| [plans/compiler-instrumentation.md](plans/compiler-instrumentation.md) | Compiler introspection: register allocator, encoding patterns (DONE) |
| [plans/XENIA_BOOT_VALIDATION.md](plans/XENIA_BOOT_VALIDATION.md) | Xenia emulator build, hybrid XEX boot validation, headless mode plan |
| [plans/custom-graphics-engine/PLAN.md](plans/custom-graphics-engine/PLAN.md) | **Native port master plan** — phased roadmap, rendering/audio/input/motion |
| [plans/dc3-native/STATUS.md](plans/dc3-native/STATUS.md) | **Native port status** — boot flow, error handling, env vars, test commands |

## Runtime & Testing

| Doc | Description |
|-----|-------------|
| [runtime/XENIA_HEADLESS_STATUS.md](runtime/XENIA_HEADLESS_STATUS.md) | **Main status doc** — all xenia changes, rendering investigation, debug flags, roadmap |
| [runtime/BOOT_ANALYSIS.md](runtime/BOOT_ANALYSIS.md) | Boot progress (~70-80%), thread architecture, how to run |
| [runtime/SCRIPTED_INPUT_TESTING.md](runtime/SCRIPTED_INPUT_TESTING.md) | Xenia scripted input — `--scripted_input` usage and DC3 navigation strategy |
| [native/HEADLESS_TESTING.md](native/HEADLESS_TESTING.md) | dc3-native headless testing — scripted input + screenshots via env vars |
| [native/TESTING.md](native/TESTING.md) | Native build testing guide — GTest fixtures, ASan, debugging workflow |
| [sessions/2026-02-18-xenia-screenshot-breakthrough.md](sessions/2026-02-18-xenia-screenshot-breakthrough.md) | **Screenshot breakthrough** — full journey from black frames to rendered DC3 boot animation |
| [sessions/2026-02-18-xenia-frame-capture-attempts.md](sessions/2026-02-18-xenia-frame-capture-attempts.md) | Frame capture approaches tried — trace, deferred draws, async worker (resolved) |
| [sessions/2026-02-18-vulkan-headless-rendering.md](sessions/2026-02-18-vulkan-headless-rendering.md) | Vulkan headless rendering — async pipelines, GPU readback (resolved) |
| [sessions/2026-02-18-vulkan-performance-investigation.md](sessions/2026-02-18-vulkan-performance-investigation.md) | Vulkan perf investigation — draw path at 30fps, readback is sole bottleneck (resolved) |

## General

| Doc | Description |
|-----|-------------|
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contributing guidelines |
| [FAQ.md](FAQ.md) | Frequently asked questions |
| [codex-coordination-workflow.md](codex-coordination-workflow.md) | Coordinating with GPT-5.3-Codex via OpenRouter |

## Session Logs

Work session archives live in [sessions/](sessions/) (~145 files, Jan 2025 – Feb 2026). Named by date and topic, e.g. `2026-02-11-x360-linking-pipeline.md`.
