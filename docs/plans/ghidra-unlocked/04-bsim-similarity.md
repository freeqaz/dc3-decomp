# 04 — BSim Cross-Binary Similarity

Priority: **Tier 1**  
Readiness: **Ready with scope split**  
Effort: **Medium**

## Why This Matters

BSim is the best candidate here for finding renamed or structurally similar implementations across DC3, RB3, and other binaries.

## What The Review Found

BSim is present in the local Ghidra build and the built distribution includes:

- `../ghidra/build/ghidra/support/bsim`

This is important because the most promising stock scripts are not the right headless API.

### Important corrections

- `CreateH2BSimDatabaseScript.java` explicitly tells headless users to use the `bsim` command-line tool.
- `AddProgramToH2BSimDatabaseScript.java` also points headless users to `bsim`.
- `LocalBSimQueryScript.java` is GUI-only.

So the plan should not rely on:

- direct H2 access from Python for queries
- GUI-only scripts for headless automation
- `analyzeHeadless -postScript ...` as the primary BSim lifecycle path

## Recommended Scope Split

### Phase 1 — Database lifecycle and signature ingest

Use `../ghidra/build/ghidra/support/bsim` for:

- database creation
- metadata management
- signature generation
- committing signatures

Treat this as the supported infrastructure path.

### Phase 2 — Query integration

Build our own thin query wrapper for analyst workflows.

Options:

1. A custom Java/PyGhidra helper that calls BSim query APIs directly
2. A dedicated scriptable wrapper around the BSim client libraries

Do **not** query the H2 file as raw SQL. Similarity logic lives in BSim, not in a simple table lookup.

## Implementation

### CLI

Add:

- `tools/ghidra/bsim_db.py`
- `tools/ghidra/bsim_query.py`

Suggested responsibilities:

- `bsim_db.py`
  - create/open DB
  - ingest binaries
  - list indexed executables
- `bsim_query.py`
  - query one function against indexed binaries
  - batch export best matches
  - `--json`

### Skill

Add:

- `.claude/skills/ghidra-bsim/SKILL.md`

## Operational Requirements

- Use a persistent scratch Ghidra project for all indexed binaries.
- Record which binaries were ingested, with language ID and compiler spec, so results are reproducible.
- Keep RB3/DC3/XDK indexes separate enough that we can filter by source corpus.

## Suggested Milestones

### Milestone 1

Create a local file-backed DB and ingest DC3 + RB3.

### Milestone 2

Return top-N similar functions for a single DC3 function.

### Milestone 3

Batch the whole DC3 corpus against RB3 and store best matches.

## Acceptance Criteria

- A reproducible DB creation command is documented and works.
- DC3 and one reference binary are indexed.
- A single-function query returns ranked matches with similarity/confidence.
- Batch output can be consumed by other tools or skills.
