# Ghidra Unlocked

Status: Design reviewed  
Created: 2026-03-24  
Updated: 2026-03-24

## Goal

Expose more of Ghidra 12.1 + our Xenon fork to the DC3 decomp workflow without bloating the MCP server or binding ourselves to GUI-only flows.

This review was checked against:

- `../ghidra` for stock Ghidra/Xenon APIs, scripts, and support tooling
- `../pyghidra-mcp` for the live server implementation started by `tools/ghidra/pyghidra-service.sh`
- the existing CLI/skill pattern in `tools/ghidra/` and `.claude/skills/`

## Architecture Decisions

### Implementation target

New work should land in:

- `tools/ghidra/*.py` for user-facing CLI wrappers
- `.claude/skills/*/SKILL.md` for skill wrappers
- `../pyghidra-mcp/src/pyghidra_mcp/tools.py` for new read-only Ghidra operations
- `../pyghidra-mcp/src/pyghidra_mcp/server.py` for thin MCP endpoints

Do not re-vendor server changes into this repo. The service manager launches the sibling `../pyghidra-mcp` repo.

### Integration modes

Use one of three patterns per feature:

1. Thin MCP endpoint
   For read-only queries against the already-loaded DC3 program.
2. Standalone headless/support-tool wrapper
   For Ghidra features that are naturally driven by `analyzeHeadless` or `support/bsim`.
3. Spike-only direct pyghidra helper
   Only when the MCP model is a poor fit and we need to prove feasibility first.

### Definition of done

Every feature that graduates from planning needs:

- a CLI entry point under `tools/ghidra/`
- a skill wrapper if it is part of normal analyst flow
- either an MCP integration test in `../pyghidra-mcp/tests/` or a documented headless smoke test
- a short operational note in `docs/tools/GHIDRA.md` once implemented

## Readiness Summary

| # | Feature | Impact | Effort | Readiness | Notes |
|---|---------|--------|--------|-----------|-------|
| 1 | [Stack Frame & Calling Convention Analysis](01-stack-frame-analysis.md) | High | Low | Ready | Query-only MCP addition |
| 2 | [Instruction-Level Queries](02-instruction-queries.md) | High | Low | Ready | Query-only MCP addition |
| 3 | [RTTI Class Recovery](03-rtti-class-recovery.md) | High | Medium-High | Spike required | Stock script does not directly support Xenon compiler setup |
| 4 | [BSim Cross-Binary Similarity](04-bsim-similarity.md) | High | Medium | Ready with scope split | Use `support/bsim` for DB lifecycle; custom query path still needed |
| 5 | [P-Code Emulation for Behavioral Testing](05-pcode-emulation.md) | High | High | Spike required | Useful, but needs strict v1 scope |
| 6 | [Data Type Manager Deep Export](06-datatype-export.md) | Medium | Low | Ready | Build on existing `list_structures`/`extract_structures` |
| 7 | [Decompiler Options Tuning](07-decompiler-tuning.md) | Medium | Low-Medium | Ready | Must be per-request, not global mutable state |
| 8 | [Version Tracking / Program Diff](08-version-tracking.md) | Medium | Medium | Spike required | Headless flow is project/session oriented, not ad hoc `.obj` diff |
| 9 | [FunctionID for Library Detection](09-function-id.md) | Medium | Medium-High | Spike required | Stock import scripts are interactive and Windows-centric |

## Recommended Order

1. Stack frame analysis
2. Instruction-level queries
3. Data type export
4. Decompiler tuning
5. BSim database + query wrapper
6. RTTI feasibility spike
7. Version tracking feasibility spike
8. FunctionID feasibility spike
9. P-code emulation

This order keeps the first wave on low-risk read-only queries, then moves into heavier Ghidra subsystems once the workflow and server extension pattern are proven.

## Common Implementation Rules

### CLI contract

All new CLI tools should:

- accept symbol or address where possible
- support `--json`
- fail with actionable guidance if the Ghidra service or required database/project is missing

### MCP contract

New server endpoints should:

- return raw or lightly normalized data
- avoid presentation logic
- support the live binary naming model already used by `MCPClient`

### Testing

For query tools:

- add a server-level integration test in `../pyghidra-mcp/tests/integration/`
- add a lightweight CLI smoke test where practical

For headless/support-tool workflows:

- document one reproducible smoke test command
- capture expected artifacts and failure modes in the plan doc

## Cross-Cutting Risks

- The live server is the sibling `../pyghidra-mcp` repo, not the vendored copy in this tree.
- Xenon-specific language support exists, but some stock Ghidra features assume a `windows` compiler spec that Xenon does not expose by default.
- Some shipped Ghidra scripts are GUI-only even when they look attractive on paper.
- Global mutation of shared decompiler state is unsafe for concurrent MCP clients.
