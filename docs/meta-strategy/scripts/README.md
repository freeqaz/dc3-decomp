# Meta-Strategy Validation Scripts

Scripts to validate assumptions and answer open questions before implementation.

## Usage

Run from project root:

```bash
./docs/meta-strategy/scripts/SCRIPT_NAME.sh
```

## Scripts

### quick_stats.sh

Quick overview of decomp progress.

```bash
./docs/meta-strategy/scripts/quick_stats.sh
```

Shows:
- Total functions, matched count, percentage
- Progress by match range (100%, 99%+, 90%+, etc.)
- Verdict distribution
- Near-complete count

---

### validate_call_graph.sh

**Answers Q2: Is the call graph validation threshold right?**

```bash
./docs/meta-strategy/scripts/validate_call_graph.sh
```

Shows:
- Functions with 20+ callers
- Caller count distribution
- Recommendation: build call graph infrastructure or skip

**Decision criteria**:
- >= 10 functions with 20+ callers → Build infrastructure
- < 10 functions with 20+ callers → Skip, use simpler scoring

---

### check_pattern_distribution.sh

**Validates the ~80% LINKER_MERGED assumption.**

```bash
./docs/meta-strategy/scripts/check_pattern_distribution.sh
```

Shows:
- Pattern distribution for 80%+ matched functions
- How many can reach 100% vs have unfixable patterns

---

### find_quick_wins.sh

Find high-value targets for quick progress. Outputs markdown-formatted tables.

```bash
./docs/meta-strategy/scripts/find_quick_wins.sh           # Default 15 per section
./docs/meta-strategy/scripts/find_quick_wins.sh 30        # 30 per section
./docs/meta-strategy/scripts/find_quick_wins.sh > targets.md  # Save to file
```

**Sections:**

1. **High-Impact Functions (Most Dependents)** - Functions called by many others. Fixing these validates call sites across the codebase.
2. **Reachable 100%** - Functions without unfixable patterns (LINKER_MERGED, etc.)
3. **Small Functions Near Complete** - Under 200 bytes at 95%+
4. **Type Anchors** - Constructors/destructors that anchor class layout
5. **High-Impact + High-Match Combo** - Best ROI: high fan-in AND high match %
6. **Fresh Targets** - Never attempted, high match %

---

---

## Python Scripts (scripts/orchestrator/)

Core database and orchestration logic lives in Python modules:

### database.py
Database initialization, schema migrations, and query functions. Key functions:
- `ingest_report()` - Populate functions table from `report.json`
- `query_functions()` - Query by unit pattern, match range
- `query_functions_by_priority()` - Priority-ordered work selection

### rb3_pairing.py
Syncs RB3 (Rock Band 3) file pairings with DC3 units for cross-reference.
- `sync_file_pairs()` - Populate `file_pairs` table

### rb2_dwarf.py
Parses RB2 DWARF debug info dump for struct/class layouts.
- `RB2DwarfParser` / `RB2DwarfDB` - Class layout lookups

### mcp_server.py
MCP server exposing database and analysis tools to Claude agents.

### decomp_orchestrate.py (scripts/)
Main orchestrator CLI for batch decompilation work:
```bash
./bin/orchestrate query --pattern "src/system/char/*" --min-percent 80
./bin/orchestrate batch "src/system/char/*" --max-agents 3
```

---

## Prerequisites

- `decomp.db` must exist in project root
- For pattern-aware queries, run Phase 2 pattern detection first
- For call graph queries, populate `call_edges` table first

## Adding New Scripts

1. Add script to this folder
2. Make executable: `chmod +x script_name.sh`
3. Document in this README
4. Use `cd "$(git rev-parse --show-toplevel)"` to ensure project root
