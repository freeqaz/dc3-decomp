# 07 — Decompiler Options Tuning

Priority: **Tier 2**  
Readiness: **Ready**  
Effort: **Low-Medium**

## Why This Matters

The current server creates one decompiler per loaded program in `../pyghidra-mcp/src/pyghidra_mcp/context.py` and seeds it from `DecompileOptions.grabFromProgram(program)`, with only `setMaxPayloadMBytes(100)` changed.

That means there is room to improve decomp output quality for our workflow.

## What The Review Found

Relevant setters are present in `DecompileOptions.java`, including:

- `setPredicate`
- `setEliminateUnreachable`
- `setAnalyzeForLoops`
- `setSimplifyDoublePrecision`
- `setAliasBlock`
- `setMaxInstructions`
- `setMaxJumpTableEntries`

## Important Design Constraint

Do **not** add a global mutable `set_decompiler_options` MCP endpoint that changes shared server state for everyone.

The server is long-lived and shared. Global mutation creates:

- cross-request nondeterminism
- concurrency bugs
- hard-to-reproduce output changes

## Recommended Design

Use **profiles**, not global mutation.

### Option A

Add optional per-request profile support to decompilation calls.

### Option B

Create a dedicated CLI that instantiates a second decompiler with alternate options for comparison.

Either is acceptable. Option B is lower risk for the first pass.

## Suggested Profiles

### `default`

Current behavior.

### `match_friendly`

Candidate settings to test:

- disable predication simplification
- disable for-loop recovery
- disable unreachable elimination
- keep alias blocking explicit
- increase max instructions
- increase max jump-table entries

Do not assume these are globally better. Measure.

## Implementation

### CLI

Add:

- `tools/ghidra/decompile_profiles.py`

Capabilities:

- decompile with one profile
- compare two profiles side by side
- emit `--json`

### Optional server work

If we later want MCP integration, add a new endpoint that accepts a profile or raw option bundle on that one request only.

## Evaluation Plan

Use a sample set of already-matched functions and ask:

- which profile produces output structurally closer to the known good source
- which profile helps with switches and weird control flow
- which settings create more noise than value

## Acceptance Criteria

- Profile comparison is deterministic and isolated per request.
- At least one alternate profile is documented with exact option values.
- We have a short sample-based recommendation before changing any default workflow.
