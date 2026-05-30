# Docs Index

Top-level sitemap for the DC3 decomp documentation.

## Tools & Workflows

| Doc | Description |
|-----|-------------|
| [tools/INDEX.md](tools/INDEX.md) | Agent tool selection guide — which tool to use when |
| [tools/REFERENCE.md](tools/REFERENCE.md) | Scripts, commands, symbol lookup, linking tools |
| [tools/WORKFLOW.md](tools/WORKFLOW.md) | Workflow narratives, common patterns, diff_inspect reference |

Tool-specific deep docs live in subdirectories:

| Subdir | Description |
|--------|-------------|
| [tools/objdiff/](tools/objdiff/) | objdiff CLI reference docs (usage, options, learnings, agent workflow) |
| [tools/orchestrator/](tools/orchestrator/) | Orchestrator incremental builds |
| [tools/GHIDRA.md](tools/GHIDRA.md) | Ghidra setup, service management, type seeding pipeline, CLI tools |

## Decomp Knowledge

| Doc | Description |
|-----|-------------|
| [decomp/TECHNICAL_NOTES.md](decomp/TECHNICAL_NOTES.md) | Compiler quirks, codegen patterns, Xbox 360 specifics |
| [decomp/RB3_REFERENCE.md](decomp/RB3_REFERENCE.md) | Rock Band 3 decomp reference (shared Milo engine) |
| [decomp/patterns/INDEX.md](decomp/patterns/INDEX.md) | Fixable/unfixable codegen patterns catalog |
| [decomp/PRAGMA_INDEX.md](decomp/PRAGMA_INDEX.md) | Xbox 360 compiler pragma documentation |
| [decomp/XBOX360_FLOATING_POINT_CODEGEN.md](decomp/XBOX360_FLOATING_POINT_CODEGEN.md) | Floating-point code generation details |
| [decomp/SUBAGENT_STRATEGY.md](decomp/SUBAGENT_STRATEGY.md) | Parallel agent strategy for batch decomp |
| [decomp/UPSTREAM_PORT_WORKFLOW.md](decomp/UPSTREAM_PORT_WORKFLOW.md) | Workflow for porting from a related decomp tree (RB3, upstream) when their function is at 100% and ours isn't |

## Reference

| Doc | Description |
|-----|-------------|
| [reference/STYLEGUIDE.md](reference/STYLEGUIDE.md) | Code style conventions |
| [reference/MACROS.md](reference/MACROS.md) | Project macros (MILO_ASSERT, OBJ_MEM_OVERLOAD, etc.) |
| [reference/DATABASE_SCHEMA.md](reference/DATABASE_SCHEMA.md) | decomp.db SQLite schema |
| [reference/FREE60_XEX_FORMAT.md](reference/FREE60_XEX_FORMAT.md) | Xbox 360 XEX executable format |

## Code Transformation

| Doc | Description |
|-----|-------------|
| [permuter/INDEX.md](permuter/INDEX.md) | C++ source permuter — **extracted to the standalone `decomp-synth` tool** ([`../../decomp-synth`](../../decomp-synth)); this page is now a pointer + how to run it in DC3 |
| [permuter/ghidra-stress-test/](permuter/ghidra-stress-test/) | DC3-specific Ghidra-guided permuter stress-test findings (project-side) |

## Dynamic Analysis

| Doc | Description |
|-----|-------------|
| [tools/UNICORN_FUNCTION_RUNNER.md](tools/UNICORN_FUNCTION_RUNNER.md) | Unicorn runner usage and design overview |

## Plans

| Doc | Description |
|-----|-------------|
| [plans/permuter/PERFORMANCE_ROADMAP.md](plans/permuter/PERFORMANCE_ROADMAP.md) | Permuter speed & power roadmap — throughput, search quality, synthesis revival (living tracker) |
| [plans/custom-graphics-engine/PLAN.md](plans/custom-graphics-engine/PLAN.md) | Native port master plan — rendering/audio/input/motion |
| [plans/dc3-native/STATUS.md](plans/dc3-native/STATUS.md) | Native port status — boot flow, error handling, env vars |
| [plans/dc3-native/PLATFORM_HACKS_ANALYSIS.md](plans/dc3-native/PLATFORM_HACKS_ANALYSIS.md) | HX_NATIVE hacks audit — 298 guards categorized, DTA handler root cause, screen flow reference |
| [plans/dc3-native/TEST_GAP_ANALYSIS.md](plans/dc3-native/TEST_GAP_ANALYSIS.md) | Test gaps — high-value missing tests for native port correctness |

## Projects

| Doc | Description |
|-----|-------------|
| [vmx128/README.md](vmx128/README.md) | VMX128 SIMD Ghidra support (Xbox 360 AltiVec extensions) |

## Debugging

| Doc | Description |
|-----|-------------|
| [debugging/native.md](debugging/native.md) | **Start here** — native port debugging, ASan, headless testing, scripted input, ObjRef rings |
| [debugging/web.md](debugging/web.md) | Web build debugging — WASM/Emscripten testing, CDP debugger |
| [tools/HTTP_DEBUG_SERVER.md](tools/HTTP_DEBUG_SERVER.md) | HTTP debug server — live DTA eval, screenshots, telemetry, settings, object introspection |

## Native Port

| Doc | Description |
|-----|-------------|
| [native/TESTING.md](native/TESTING.md) | Native build testing guide — GTest fixtures, ASan, debugging |
| [native/dta/OVERLAY_ENGINE.md](native/dta/OVERLAY_ENGINE.md) | DTA overlay engine — file overlay system design |
| [native/dta/USAGE_GUIDE.md](native/dta/USAGE_GUIDE.md) | DTA overlay usage — settings toggles, locale strings |

## General

| Doc | Description |
|-----|-------------|
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contributing guidelines |
| [FAQ.md](FAQ.md) | Frequently asked questions |

## Session Logs

Work session archives live in [sessions/](sessions/). Named by date and topic, e.g. `2026-02-11-x360-linking-pipeline.md`.
