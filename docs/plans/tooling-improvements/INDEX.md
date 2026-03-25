# Tooling Improvements — Remaining Work

From the March 24 retrospective. Phase 1 (1A-1D) and Phase 2 partial (2A, 2B, 2D) are complete.

## Implementation Preference

**Skills over MCP tools** — new functionality should be implemented as Claude Code skills (`.claude/skills/<name>/SKILL.md`) unless it requires server-side state or complex async orchestration. Skills are lighter, easier to iterate, and directly invocable by the user.

## Remaining Items

### Phase 2

| ID | Name | Spec | Effort | Implementation |
|----|------|------|--------|----------------|
| 2C | RB2 local variable lookup | [2C-rb2-locals.md](2C-rb2-locals.md) | Half-day | Skill: `/rb2-locals` + parser module |

### Phase 3

| ID | Name | Spec | Effort | Implementation |
|----|------|------|--------|----------------|
| 3A | Side-by-side ASM comparison | [3A-compare-asm.md](3A-compare-asm.md) | 1-2 days | `diff_inspect.py` mode + skill |
| 3B | Virtual call resolver | [3B-vcall-resolver.md](3B-vcall-resolver.md) | 1 day | Skill: `/resolve-vcall` + vtable extension |
| 3C | Stack layout diff | [3C-stack-layout-diff.md](3C-stack-layout-diff.md) | 1 day | `diff_inspect.py` mode + skill |

## Completed Items (this session)

- **1A** Symbol disambiguation in orchestrator (`mcp_server.py`)
- **1B** Struct offset inheritance traversal + range-based lookup (`mcp_server.py`)
- **1C** ObjPtr/ObjOwnerPtr/ObjPtrVec sub-offset reporting (merged into 1B)
- **1D** Workflow feedback memories (`feedback_decomp_workflow.md`)
- **2A** Permuter `variable_inline` pattern (new file)
- **2B** Declaration movement across control flow (extended `declaration_movement.py`)
- **2D** Cluster pattern suggestions in `diff_inspect.py`

## Priority Order for Next Session

1. **2C** — highest value, proven time-saver on ClipCollide
2. **3B** — virtual call resolution, rare but extremely painful when needed
3. **3C** — stack layout diff, confirms AT_LIMIT faster
4. **3A** — side-by-side ASM, most effort but broadest applicability
