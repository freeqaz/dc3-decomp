# objdiff CLI Extensions Design

## Overview

This document set outlines a plan to extend objdiff's CLI with query and real-time diff capabilities for agent-based decomp progress tracking.

**Goal:** Enable automated tools to:
1. Query progress reports to find work targets
2. Get real-time diff results during active work
3. Track progress trends over time

**Target:** Local fork of `~/code/milohax/objdiff`
**Output Format:** JSON (primary), with proto/markdown support
**Approach:** Extend existing objdiff-cli following established patterns

---

## Documentation Structure

| Document | Focus |
|----------|-------|
| **[CLI Commands](./OBJDIFF_CLI_COMMANDS.md)** | Command specs, arguments, JSON schemas |
| **[Implementation](./OBJDIFF_CLI_IMPLEMENTATION.md)** | Phases, code patterns, data structures |
| **[Agent Workflow](./OBJDIFF_CLI_AGENT_WORKFLOW.md)** | Query needs, tiering, what belongs where |
| **[Escalation System](./OBJDIFF_CLI_ESCALATION.md)** | Model retry/escalation (DC3 wrapper) |

---

## Current State

### What objdiff CLI Has (including Phase 1 extensions)

| Command | Purpose | Output | Status |
|---------|---------|--------|--------|
| `objdiff diff -1 target.o -2 base.o` | Interactive TUI diff | Terminal UI | ✓ Upstream |
| `objdiff diff -p . -u unit symbol` | Project-based diff | Terminal UI | ✓ Upstream |
| `objdiff report generate` | Generate full progress report | JSON/Proto | ✓ Upstream |
| `objdiff report changes old new` | Compare two reports | JSON/Proto | ✓ Upstream |
| `objdiff report summary` | Quick aggregate stats | JSON | ✓ **Phase 1** |
| `objdiff report query` | Filter/search reports | JSON | ✓ **Phase 1** |
| `objdiff report function` | Direct function lookup | JSON | ✓ **Phase 1** |

### What's Still Missing for Agents

1. ~~Non-interactive diff output~~ ✓ Implemented (Phase 2)
2. ~~Report querying~~ ✓ Implemented
3. ~~Function-level queries~~ ✓ Implemented
4. ~~Threshold filtering~~ ✓ Implemented
5. ~~Sorted/ranked output~~ ✓ Implemented
6. Markdown/CSV export (Phase 3)
7. Report trending over time (Phase 4)
8. **Automated diagnosis** - Pattern detection, fixability verdict (Phase 5)
9. **Symbol auto-resolution** - Use demangled names in diff command (Phase 5)
10. **Build integration** - `--watch` or `--build` flag for edit-compile-check loop (Phase 5)

---

## Proposed Commands (Summary)

| Command | Purpose | Phase |
|---------|---------|-------|
| `report query` | Filter/search reports | 1 |
| `report summary` | Quick aggregate stats | 1 |
| `report function` | Direct function lookup | 1 |
| `diff --output-format json` | Non-interactive diff | 2 |
| `report trending` | Multi-report comparison | 4 |

See **[CLI Commands](./OBJDIFF_CLI_COMMANDS.md)** for full specifications.

---

## Implementation Phases

| Phase | Scope | Features | Status |
|-------|-------|----------|--------|
| **1 (MVP)** | ~400 lines | `report query`, `report summary`, `report function`, `--unimplemented` | ✓ **Complete** |
| **2** | ~300 lines | `diff -f json`, `--include-instructions`, diff_score exposure | ✓ **Complete** |
| **3** | ~150 lines | Markdown output, CSV export | Pending |
| **4** | ~200 lines | stdin support, `report trending` | Pending |
| **5** | ~600 lines | Analysis & diagnosis automation (see below) | **Proposed** |

### Phase 5: Analysis & Diagnosis (Key Addition)

Based on real-world usage, **80% of time is spent interpreting diff output**, not running commands. Phase 5 automates this:

| Feature | Problem Solved |
|---------|----------------|
| **Symbol auto-resolution** | No more copy-pasting mangled names |
| **`--summary`** | Built-in match type counts (no jq needed) |
| **`--analyze`** | Auto-detect patterns (merged funcs, bool masks, register swaps) |
| **`--verdict`** | Automated fixability decision |
| **`--watch`/`--build`** | Integrated build+report+diff loop |
| **`report analyze`** | Batch triage of near-match functions |

See **[Implementation](./OBJDIFF_CLI_IMPLEMENTATION.md)** for full Phase 5 specification.

See **[Implementation](./OBJDIFF_CLI_IMPLEMENTATION.md)** for code patterns and data structures.

---

## Architecture: What Goes Where

### objdiff CLI (generic, reusable)
- Report querying with filters
- JSON/proto/markdown output
- Diff output in JSON format
- Summary statistics

### DC3 Wrapper (project-specific)
- RB3 cross-reference lookups
- Function claim/lock mechanism
- Model escalation tracking
- Build shortcuts

See **[Agent Workflow](./OBJDIFF_CLI_AGENT_WORKFLOW.md)** for the full breakdown.

---

## Model Escalation

When agents fail to fix a function, escalate to higher-tier models:

```
Haiku fails → Sonnet fails → Opus fails → Human review
```

This is tracked in `build/agent_state.json` by the DC3 wrapper tool.

See **[Escalation System](./OBJDIFF_CLI_ESCALATION.md)** for full details.

---

## Next Steps

1. ~~Fork objdiff repository locally~~ ✓ Done (`~/code/milohax/objdiff`)
2. ~~Implement Phase 1 (report query, summary, function)~~ ✓ Done
3. ~~Test against DC3 decomp reports~~ ✓ Done
4. ~~Implement Phase 2 (diff JSON output)~~ ✓ Done
5. **Implement Phase 5.1-5.2 (symbol resolution, summary stats)** ← High value, low effort
6. Implement Phase 5.3-5.4 (pattern detection, verdict) ← High value, medium effort
7. Implement Phase 3 (markdown, csv output)
8. Implement Phase 4 (report trending)
9. Implement Phase 5.5-5.6 (watch mode, batch analysis)
10. Consider upstream PR after validation
11. Build DC3 wrapper tool (escalation system)

**Priority rationale:** Phase 5.1-5.2 are quick wins that eliminate the most common friction points (symbol lookup, jq pipelines). Phase 5.3-5.4 provide the biggest time savings for diagnosis work.

### Phase 1 Test Results (2026-01-22)

Tested against DC3 decomp project:
- **Total functions:** 46,958
- **Matched functions:** 21,220 (45.19%)
- **Fuzzy match:** 38.96%
- **Near-matches (90-99%):** 787 functions
- **Small unimplemented (<100 bytes):** 8,986 functions
- **Nearly complete units (95-99.9%):** 171 units

Binary location: `~/code/milohax/objdiff/target/release/objdiff-cli`
