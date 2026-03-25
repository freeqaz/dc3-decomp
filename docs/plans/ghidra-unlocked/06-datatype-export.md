# 06 — Data Type Manager Deep Export

Priority: **Tier 2**  
Readiness: **Ready**  
Effort: **Low**

## Why This Matters

We already have useful type seeding and struct checks, but there is still no good way to bulk inspect what Ghidra currently believes about:

- structures
- unions
- enums
- typedefs

## What The Review Found

The live server already exposes:

- `list_structures`
- `extract_structures`

So this should build on existing work, not start from zero.

## Recommended Scope

### V1

Use existing `list_structures` and `extract_structures` for structure-centric workflows.

Add one new generalized endpoint only if needed:

- `list_data_types`

Supported kinds:

- `structure`
- `union`
- `enum`
- `typedef`

### Output fields

- name
- kind
- category path
- size where meaningful
- members for structures/unions
- values for enums
- target/base type for typedefs

## Implementation

### MCP server

Extend the live server in `../pyghidra-mcp`.

Implementation should use `DataTypeManager.getAllDataTypes()` and `isinstance` checks against Ghidra datatype classes, not string heuristics.

### CLI

Add:

- `tools/ghidra/datatype_export.py`

Capabilities:

- list/filter types
- deep inspect one type
- bulk JSON export
- header diff mode for the types we own

### Skill

Add:

- `.claude/skills/ghidra-types/SKILL.md`

## Design Notes

- Do not replace `struct_check.py`. This is complementary bulk export, not the same tool.
- Preserve category path in output so collisions are debuggable.
- For deep expansion, add recursion limits and cycle protection.

## Acceptance Criteria

- Existing `list_structures` data remains usable.
- New export can show at least one enum and one typedef in addition to structures.
- Bulk export produces JSON stable enough to diff between runs.
