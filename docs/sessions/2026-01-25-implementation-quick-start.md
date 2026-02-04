# Implementation Quick Start

**See full plan**: [IMPLEMENTATION_PLAN_2026-01-25.md](IMPLEMENTATION_PLAN_2026-01-25.md)

---

## The Pitch

You have **9/10 tools** and **2 critical bugs** + **1 major bottleneck**. Fix these and you have **10/10 tools** that scale to 500+ functions.

| Phase | Effort | Impact | Blocker? |
|-------|--------|--------|----------|
| **Tier 1**: Bug fixes | 1 day | Unblock workflows | YES |
| **Tier 2**: Incremental builds | 1 week | 6x faster iteration | YES for scaling |
| **Tier 3**: Integration | 2 weeks | Agent-ready | NO |

---

## What Needs to Happen (In Order)

### ✅ This Week: Tier 1 (Critical Bugs)

**1.1: Fix objdiff-cli mangled symbol handling** (2-4 hours)
- Problem: `objdiff-cli report function ... "??0Foo@@..."` fails with regex error
- Solution: Auto-escape mangled names or auto-switch to `--exact` mode
- Validation: Test piping query results into function command

**1.2: Fix objdiff-cli --build path** (2-3 hours)
- Problem: Looks for `src/system/flow/Flow.obj` instead of `obj/system/flow/Flow.obj`
- Solution: Correct path construction
- Validation: Test `objdiff-cli diff -u "..." --build` works

**1.3: Improve analyze-function unit discovery** (3-4 hours)
- Problem: Error messages show paths, users don't know to use full path with `-u`
- Solution: Add `--list-units` flag, auto-suggest correct command in errors
- Validation: Test error message suggests correct command format

**Owner**: Backend engineer
**Done when**: All three bugs fixed + regression tests passing

---

### Next Week: Tier 2 (Performance)

**2.1: Investigate & implement incremental builds** (3-5 days)
- Phase 2.1a: Investigate ninja options (2 hours)
  - Can we target `ninja build/373307D9/obj/system/flow/Flow.obj`?
  - How much faster is it vs full `ninja`?
  - How to handle edge cases (vtable misalignment)?
  - **Output**: Decision doc on approach

- Phase 2.1b-d: Implement in analyze-function, objdiff-cli, orchestrate (2-4 days)
  - Add flags: `--incremental` (default), `--full-build` (periodic)
  - Target ninja to single .obj file
  - Keep backwards compatibility

**Validation**:
- Benchmark: `ninja build/373307D9/obj/system/char/Character.obj` takes <5s
- Run analyze-function on 20 functions, compare timing vs Tier 1
- Run orchestrate batch with 50 functions, measure total build time
- Verify verdicts match between incremental and full builds

**2.2: Add decompilation caching to Ghidra MCP** (2-3 days)
- SQLite cache: (address + binary_hash) → decompilation
- Invalidate on binary change
- Expected: 200x faster on cache hits

**2.3: Harden Ghidra service reliability** (2-3 days)
- Fix port cleanup (kill stale processes)
- Add health check endpoint `/health`
- Improve startup logging
- Add `--diagnose` flag

**Owner**: Backend engineer (2.1, 2.2), DevOps (2.3)
**Done when**: Build time ≤5s per file, service uptime ≥99%, caching validated

---

### Following Week(s): Tier 3 (Integration)

Optional but recommended for agent scaling:

**3.1: Unified analyze-function + objdiff-cli** (3-4 days)
- Single command gets assembly diff + decompilation

**3.2: Watch mode** (4-5 days)
- Auto-rebuild and diff on file save

**3.3: Batch mode** (2-3 days)
- Process multiple functions from file or stdin

**Owner**: Backend engineer
**Done when**: Single commands work, orchestrate can use batch mode

---

## Key Decisions Made

✅ **Keep pyghidra-mcp service** (not direct library import)
- Cleaner for parallel agents
- Tier 2.3 fixes address reliability concerns
- Caching (Tier 2.2) addresses performance

✅ **Investigate incremental builds first** (Tier 2.1a)
- 88s → 3-5s is massive win if possible
- Edge case handling: vtable misalignment every ~10 commits
- Orchestrate tool can coordinate periodic full builds

✅ **Tier-by-tier execution**
- Deploy Tier 1 immediately (blocking bugs)
- Deploy Tier 2 after investigation (enables scaling)
- Deploy Tier 3 as needed (agent optimization)

---

## Expected ROI

### After Tier 1 (1 day effort)
- Workflows unblocked
- Can continue using tools as-is
- No performance improvement yet

### After Tier 1 + 2 (1 week effort)
- **6x faster iteration** (2 min → 20 sec per function)
- **16x faster batch** (4 hours → 15 min for 50 functions)
- Scaling to 500+ functions becomes feasible
- Parallel agents become practical

### After Tier 1 + 2 + 3 (3-4 weeks effort)
- Agent-ready architecture
- Single commands for complex workflows
- Production-quality tooling
- Ready to publish/share

---

## Next Steps

1. **Decide**: Are we proceeding with this plan?
2. **Assign**: Who owns each tier?
3. **Start Tier 1**: Begin bug fixes immediately
4. **Meanwhile**: Schedule 2-hour investigation on incremental builds (Tier 2.1a)
5. **Document**: Add findings to docs/tools/INCREMENTAL_BUILDS.md

---

## Files to Review

- **Full plan**: docs/IMPLEMENTATION_PLAN_2026-01-25.md
- **Build system**: build.ninja (understand obj file layout)
- **Orchestrate tool**: bin/orchestrate (see how parallel execution works)
- **objdiff-cli**: Check where mangled name parsing happens
- **analyze-function**: Check where unit path errors are generated

---

## Questions?

- **Incremental builds**: Is targeting `ninja build/.../obj/system/char/Character.obj` possible? Any edge cases?
- **Ghidra caching**: Should we cache at the MCP level or in analyze-function?
- **Tier 3 priority**: Which features are most important for agents? (Batch mode, unified command, watch mode?)
- **Testing**: Should we create automated benchmarks for build time regression?

