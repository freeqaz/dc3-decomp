# DC3 Decomp Tooling Implementation Plan
## Tier-by-Tier Execution Strategy

**Date**: 2026-01-25
**Status**: Ready for implementation
**Basis**: Comprehensive agent feedback + architectural analysis
**Execution Model**: Tier-by-tier rollout with high ROI validation at each stage

---

## Overview: From 9/10 to 10/10

The tooling ecosystem (analyze-function, objdiff-cli, pyghidra-mcp) is production-quality but faces three distinct categories of issues:

1. **Tier 1 - Critical Bugs** (1 day): Blocking workflows
2. **Tier 2 - Performance Bottlenecks** (1 week): Enable scaling to 500+ functions
3. **Tier 3 - Integration & Features** (2 weeks): Streamline workflow, enable parallelization

Each tier stands alone (Tier 1 can be deployed independently) but builds on the previous one.

---

## Tier 1: Critical Bug Fixes (1 Day)

**Goal**: Unblock current workflows, fix regressions

### Bug 1.1: objdiff-cli Mangled Symbol Handling

**Status**: 🔴 BLOCKING - Breaks programmatic workflows

**Problem**:
```bash
# Symbols returned by objdiff-cli have regex special characters
FUNC=$(objdiff-cli report query ... | jq -r '.results[0].name')
# FUNC = "??0StorePanel@@QAA@XZ"

# Attempting to use in next command fails
objdiff-cli report function build/373307D9/report.json "$FUNC"
# Error: regex parse error (? is repetition operator)
```

**Root Cause**: `report function` treats input as regex by default, not exact match

**Solution**: Auto-detect and handle mangled names
- C++ mangled names start with `?` or `_Z`
- Detect these patterns and auto-escape regex special chars
- Alternatively: auto-switch to `--exact` mode for mangled names

**Implementation**:
1. Locate where objdiff-cli parses symbol argument in `report function` subcommand
2. Add regex escape function for C++ mangled names
3. Auto-detect mangled pattern and escape if needed
4. Add regression test: pipe query result directly into function command

**Effort**: 2-4 hours
**Impact**: High - fixes common automation workflows
**Priority**: P0
**Owner**: Backend engineer
**Validation**:
- Test piping: `objdiff-cli report query ... | jq '.results[].name' | while read n; do objdiff-cli report function ... "$n"; done`
- Test direct mangled: `objdiff-cli report function ... "??0Foo@@..."`

---

### Bug 1.2: objdiff-cli --build Path Resolution

**Status**: 🔴 BLOCKING - Feature doesn't work

**Problem**:
```bash
objdiff-cli diff -u "default/system/flow/Flow" "??1Flow@@UAA@XZ" --build
# Error: unknown target '...build/373307D9/src/system/flow/Flow.obj'
# Expected: '...build/373307D9/obj/system/flow/Flow.obj'
```

**Root Cause**: Path construction uses wrong base directory

**Solution**: Correct path from `src/` to `obj/`

**Implementation**:
1. Find where objdiff-cli constructs target object path in `--build` code
2. Verify ninja build layout: `build/373307D9/` contains:
   - Source: `src/` (input files only)
   - Objects: `obj/` or another directory
   - Generated files: Check actual structure
3. Update path construction to use correct directory
4. Add integration test with `--build` flag

**Effort**: 2-3 hours
**Impact**: High - feature becomes usable
**Priority**: P0
**Owner**: Backend engineer
**Validation**:
- Run with `--build` and verify object file is found
- Check objdiff-cli source for path construction
- Test: `objdiff-cli diff -u "default/system/char/Character" ... --build`

---

### Bug 1.3: Improve Unit Path Discovery in analyze-function

**Status**: 🟡 IMPORTANT - Users confused about path format

**Problem**:
- Error messages show unit paths like `(default/system/char/Character)`
- Users try `-u char/Character` (fails) instead of full path
- No way to discover valid unit paths

**Solution**: Better error messages + unit listing

**Implementation**:
1. In error for ambiguous symbols, auto-suggest correct command:
   ```
   Error: Symbol "Character::Poll" is ambiguous
   Try: ./bin/analyze-function "Character::Poll" -u default/system/char/Character
   ```
2. Add `--list-units` flag to show available units
   ```bash
   ./bin/analyze-function --list-units | grep -i character
   # Output: default/system/char/Character
   ```
3. Add `--examples` flag showing common usage patterns
4. Update help text to show full unit path format

**Effort**: 3-4 hours
**Impact**: Medium - reduces support friction
**Priority**: P1
**Owner**: Backend engineer or tools maintainer
**Validation**:
- Run with ambiguous symbol, verify suggestion is correct
- Run `--list-units`, verify format matches what works with `-u`
- Run `--examples`, verify examples are correct

---

## Tier 2: Performance & Reliability (1 Week)

**Goal**: Enable scaling to 500+ functions, remove build bottleneck

### Improvement 2.1: Incremental Build Strategy

**Status**: 🟡 HIGHEST IMPACT - Removes #1 bottleneck (88s → 3-5s)

**Problem**:
- Full rebuild takes 88 seconds per code change
- 500 functions = 12+ hours of rebuild time alone
- Validation loop: edit → build → test → verdict takes 2 minutes per function

**Solution**: Leverage ninja's incremental compilation

**Strategy A: Direct ninja targeting** (Recommended for 90% of cases)
```bash
# Instead of: ninja (full rebuild ~88s)
# Use: ninja build/373307D9/obj/system/flow/Flow.obj (~3-5s)

# objdiff-cli can already do this with --build
# analyze-function can call ninja on just the modified file's .obj
```

**Strategy B: Watch mode with intelligent rebuild**
```bash
# On source file save, rebuild only that file's .obj
objdiff-cli watch "Foo::Bar"
# - Detects source save
# - Runs: ninja build/373307D9/obj/system/foo/Bar.obj
# - Re-runs diff automatically
```

**Strategy C: Selective report generation**
```bash
# Instead of full report (scans all .obj), query single function
# objdiff-cli diff -p . "Foo::Bar" --verdict
# This is already fast (0.1s) - no rebuild of report needed
```

**Edge Case Handling**: Vtable misalignment
- When a class layout changes, vtable breaks more than just the .obj
- Mitigation: Periodic full rebuilds (e.g., after every 10 commits)
- Orchestrate tool can coordinate this: run full build every Nth batch
- Can detect vtable issues via objdiff verdict (LINKER_MERGED pattern)

**Implementation Plan**:

**Phase 2.1a: Investigate** (2 hours)
1. Check ninja documentation for target-specific builds
2. Examine build.ninja structure to understand obj path pattern
3. Test: `ninja build/373307D9/obj/system/char/Character.obj`
4. Measure: Time for single-file rebuild vs full rebuild
5. Document findings in docs/tools/INCREMENTAL_BUILDS.md

**Phase 2.1b: Implement incremental in analyze-function** (1-2 days)
1. Add `--incremental` flag (default for interactive use)
2. Add `--full-build` flag (for safety, periodic validation)
3. When `--incremental`, target just the changed .obj file
4. After building, re-run diff/verdict on that function
5. Keep existing `--build` behavior for backward compatibility

**Phase 2.1c: Extend objdiff-cli** (1 day)
1. Add `--incremental` flag to `diff` subcommand
2. When set, ninja only the specific object file
3. Document in help: "Rebuild only changed file (89% faster)"

**Phase 2.1d: Orchestrate integration** (1 day)
1. Have orchestrate tool run single agents with `--incremental`
2. Every 10th batch, run full build: `--full-build`
3. Track vtable issues and escalate to full build if detected
4. Document in orchestrate guide

**Expected Performance**:
- Single function analysis: 128s → 20s (6.4x faster)
- Iteration time: 2min → 20sec per function
- 50-function batch: 4 hours → 15 minutes
- 500-function batch: Impractical → 2-3 hours (with 10 agents)

**Effort**: 3-5 days
**Impact**: CRITICAL - 17x speedup on validation
**Priority**: P0
**Owner**: Backend engineer
**Validation**:
- Benchmark incremental vs full: `time ninja build/373307D9/obj/system/char/Character.obj` vs `time ninja`
- Run analyze-function on 20 functions, compare total time
- Test orchestrate batch mode with 50 functions, measure total build time
- Verify verdicts match between incremental and full builds
- Test edge case: vtable change, verify full build detects issue

---

### Improvement 2.2: Ghidra Decompilation Caching

**Status**: 🟡 HIGH IMPACT - Speeds up repeated analysis

**Problem**:
- Every analyze-function call decompiles from Ghidra (0.2s per call)
- Batch operations with repeated functions are slow
- Interactive workflows feel sluggish

**Solution**: SQLite cache mapping (address + binary_hash) → decompilation

**Implementation Plan**:

**Phase 2.2a: Design cache schema** (2 hours)
```sql
CREATE TABLE decompilation_cache (
  address TEXT PRIMARY KEY,
  binary_hash TEXT NOT NULL,  -- SHA256(binary), invalidates on binary change
  decompilation TEXT NOT NULL,
  timestamp INTEGER,
  source TEXT  -- 'ghidra' or other
);

CREATE INDEX idx_binary_hash ON decompilation_cache(binary_hash);
```

**Phase 2.2b: Implement cache in pyghidra-mcp** (2-3 days)
1. Add SQLite cache layer to MCP server
2. Before calling Ghidra: check cache with (address, binary_hash)
3. On miss: call Ghidra, populate cache
4. On binary change: invalidate cache entries (hash mismatch)
5. Add CLI flag: `--cache-dir /tmp/claude/ghidra_cache`
6. Add cache management commands: `--cache-stats`, `--cache-clear`

**Phase 2.2c: Test cache effectiveness** (1 day)
- Benchmark: 20 function batch with repetition
- Before: 20 × 0.2s = 4s per iteration
- After (cache hits): 20 × 0.001s = 0.02s per iteration
- Measure actual speedup on orchestrate batch runs

**Expected Performance**:
- Cache hit: 0.2s → 0.001s (200x faster)
- Batch with repeated functions: 30-50% faster
- Interactive workflows feel instant

**Effort**: 2-3 days
**Impact**: High - 100-200x speedup on repeated analysis
**Priority**: P1
**Owner**: Backend engineer
**Validation**:
- Run analyze-function on same function twice, measure time
- Run 50-function batch with 10 repetitions, measure total time
- Verify cache is populated: `ls -la /tmp/claude/ghidra_cache`
- Test cache invalidation: change binary, verify cache is cleared

---

### Improvement 2.3: Ghidra Service Reliability

**Status**: 🟡 IMPORTANT - Affects session stability

**Issues**:
1. Port conflicts prevent restart without manual cleanup
2. Startup errors not logged clearly
3. Silent crashes during long sessions
4. No health check endpoint

**Solutions**:

**Phase 2.3a: Port cleanup** (2 hours)
1. Add port cleanup to service startup script:
   ```bash
   # Check for stale processes on port 8000
   lsof -i :8000 && kill -9 <pid>
   ```
2. Add to `pyghidra-service.sh start`:
   - Kill any existing process on port 8000
   - Wait for port to be free (timeout: 5 seconds)
   - Then start service
3. Document in GHIDRA_SETUP.md

**Phase 2.3b: Health check endpoint** (4 hours)
1. Add `/health` endpoint to FastMCP server:
   ```json
   GET /health
   {
     "status": "healthy",
     "version": "0.1.6",
     "uptime_seconds": 3600,
     "requests_processed": 150,
     "cache_hits": 120
   }
   ```
2. Add monitoring: `curl http://127.0.0.1:8000/health`
3. Use in analyze-function: check health before analysis
4. Fail gracefully if service unhealthy: "Service not responding, try: ./tools/ghidra/pyghidra-service.sh restart"

**Phase 2.3c: Improve logging** (4 hours)
1. Log all startup events to separate file: `logs/pyghidra-service.log`
2. Add diagnostic mode: `pyghidra-mcp --diagnose`
   - Check Ghidra installation
   - Check port availability
   - Verify project directory permissions
   - Test MCP endpoint
3. Capture Java/Ghidra errors to log file
4. Document in troubleshooting guide

**Phase 2.3d: Auto-restart capability** (4 hours)
1. Add `--auto-restart` mode to service (optional)
2. Or use systemd/supervisor for process management
3. Document both approaches
4. Default: manual control, optional: auto-managed

**Expected Reliability**:
- Service stability: 5/10 → 9/10
- Startup success rate: ~80% → ~99%
- Troubleshooting time: 10+ minutes → 1-2 minutes

**Effort**: 2-3 days
**Impact**: Medium - improves reliability, reduces troubleshooting
**Priority**: P1
**Owner**: Backend engineer / DevOps
**Validation**:
- Test startup after port conflict: `lsof -i :8000` should be empty
- Test health check: `curl http://127.0.0.1:8000/health` returns success
- Test logging: `tail -f logs/pyghidra-service.log` shows startup
- Test --diagnose: run and verify all checks pass
- Test error handling: kill service and verify auto-restart (if enabled)

---

### Improvement 2.4: MCP Architecture Decision

**Status**: 🟡 DECISION POINT - Pyghidra MCP vs Direct Library Import

**Options**:

**Option A: Keep pyghidra-mcp** (Current)
- Pros:
  - Service is isolated from Claude Code process
  - Can restart independently
  - Supports multiple concurrent clients
  - Easier debugging (separate process)
- Cons:
  - Network latency (0.2s per call)
  - Additional process to manage
  - Port conflicts possible

**Option B: Import pyghidra as library**
- Pros:
  - Direct in-process access
  - No network/port issues
  - Faster (no HTTP overhead)
  - Simpler deployment
- Cons:
  - Heavy Java integration in Claude Code process
  - Harder to debug
  - Single-threaded (can't serve multiple clients)
  - More complex packaging

**Recommendation**: Keep Option A (MCP service) for now, with improvements
- Tier 2.3 fixes address reliability issues
- Caching (Tier 2.2) addresses performance concerns
- Service architecture is cleaner for parallel agents
- Revisit Option B if service overhead becomes critical (unlikely with caching)

**Decision**: Stick with pyghidra-mcp v0.1.6 + FastMCP

**Owner**: Architecture decision (confirmed in feedback analysis)

---

## Tier 3: Integration & Features (2 Weeks)

**Goal**: Streamline workflow, enable agent parallelization

### Feature 3.1: Unified analyze-function + objdiff-cli

**Status**: 🟢 NICE TO HAVE - Eliminates two-command workflow

**Problem**:
- Current workflow requires two commands:
  ```bash
  objdiff-cli diff -p . "Foo::Bar" --verdict
  ./bin/analyze-function "Foo::Bar"
  ```

**Solution**: Integrated command with combined output

**Implementation**:
```bash
# Single command gets both assembly and decompilation
./bin/analyze-function "Foo::Bar" --with-diff
# Or:
objdiff-cli analyze "Foo::Bar"
```

**Effort**: 3-4 days
**Impact**: Medium - reduces command overhead
**Priority**: P2
**Owner**: Backend engineer

---

### Feature 3.2: Watch Mode

**Status**: 🟢 NICE TO HAVE - Improves iteration

**Problem**: Edit → build → check verdict → repeat is slow

**Solution**: Auto-rebuild and show diff on file save

**Implementation**:
```bash
# Watch source file, auto-rebuild and diff
objdiff-cli watch "Foo::Bar"
# Output updates automatically as you save changes
```

**Effort**: 4-5 days
**Impact**: Medium - improves flow state
**Priority**: P2
**Owner**: Backend engineer

---

### Feature 3.3: Batch Mode for analyze-function

**Status**: 🟢 NICE TO HAVE - Enables parallel processing

**Problem**: Processing many functions requires looping

**Solution**: Batch input/output support

**Implementation**:
```bash
# Process functions from file
./bin/analyze-function --batch functions.txt -o results.json

# Or from stdin
objdiff-cli report query ... | jq -r '.results[].name' | \
  ./bin/analyze-function --batch-stdin -o results.json
```

**Effort**: 2-3 days
**Impact**: Medium - enables orchestrate integration
**Priority**: P2
**Owner**: Backend engineer

---

## Agent Parallelization Strategy

The orchestrate tool already handles parallel execution. These improvements enable it to scale:

### Conflict Resolution Model

**Current**: Each agent in its own worktree
```
Agent 1: Edits src/system/char/Character.cpp → build → test → verdict
Agent 2: Edits src/system/flow/Flow.cpp     → build → test → verdict
Agent 3: Edits src/system/math/Rot.cpp      → build → test → verdict
```

**Merge Phase** (Orchestrate coordinates):
1. Collect all successful changes
2. Merge into main worktree
3. Run full rebuild to detect conflicts (vtable alignment, etc.)
4. If conflicts detected:
   - Revert problematic changes
   - Agent can retry with escalated model (Sonnet → Opus)
   - Or mark as "needs human review"

**Validation**:
- Measure match percentage before/after merge
- If regression: revert and escalate
- Log all merge conflicts for human review

### Build System Optimization for Agents

1. **Incremental builds** (Tier 2.1):
   - Each agent: `ninja build/373307D9/obj/system/foo/Bar.obj` (~3-5s)
   - No conflicts on individual .obj files
   - Fast iteration per agent

2. **Shared build cache** (Optional, Tier 4):
   - Use ccache or similar for shared object files
   - Agents can skip rebuilds if another agent already built same file
   - Massive speedup for batch operations

3. **Orchestrate coordination**:
   - Run 10 agents in parallel: 10 functions simultaneously
   - Each does: analyze → code → build → test → verdict
   - Merge phase: 1 full rebuild to check for conflicts
   - Net: 100 functions in 2-3 hours (vs 40+ hours manual)

---

## Tier 4: Strategic Improvements (Optional, 1+ Months)

### Feature 4.1: AI-Assisted Fix Suggestions
- Analyze verdict + diff → suggest code changes
- Train on successful fixes from git history
- Could automate 30-50% of fixes

### Feature 4.2: VSCode IDE Extension
- Inline match percentage
- Split-view diff with Ghidra decompilation
- Auto-rebuild on save
- One-click to Ghidra/objdiff

### Feature 4.3: Distributed Build Cache
- Share compiled object files across machines
- ccache-like system for huge speedup
- Essential for scaling to 100+ agents

---

## Implementation Roadmap

### Week 1: Critical Fixes
- [ ] Fix objdiff-cli mangled symbol handling (P0)
- [ ] Fix objdiff-cli --build path (P0)
- [ ] Improve analyze-function unit discovery (P1)
- **Result**: 2-3 bugs fixed, workflows unblocked

### Week 2: Build System Optimization
- [ ] Investigate incremental builds (2.1a)
- [ ] Implement in analyze-function (2.1b)
- [ ] Extend objdiff-cli (2.1c)
- [ ] Integrate with orchestrate (2.1d)
- [ ] Add decompilation caching (2.2)
- [ ] Fix Ghidra service reliability (2.3)
- **Result**: 17x faster iteration, more stable service

### Week 3-4: Integration & Features
- [ ] Unified analyze command (3.1)
- [ ] Watch mode (3.2)
- [ ] Batch mode (3.3)
- [ ] Test end-to-end with orchestrate batch mode
- **Result**: Streamlined workflow, agent-ready

---

## Success Metrics

After Tier 1 + 2 implementation:

| Metric | Current | Target | Improvement |
|--------|---------|--------|------------|
| Single function analysis | 2 min | 20 sec | **6x faster** |
| 50-function batch | 4 hours | 15 min | **16x faster** |
| 500-function batch | Impractical | 2-3 hours | **Possible** |
| Tool bugs | 2 critical | 0 | **100%** |
| Service reliability | 5/10 | 9/10 | **+80%** |
| Build bottleneck | 88s | 3-5s | **17x faster** |
| Overall tooling quality | 8/10 | 10/10 | **+25%** |

---

## Resource Allocation

### Team Requirements
- **Backend Engineer**: Bugs (1 day) + Tier 2 optimization (1 week)
- **DevOps/SRE**: Ghidra service reliability (2-3 days)
- **Full Stack Engineer**: Tier 3 features (2 weeks, optional)

### Critical Path
1. Week 1: Bug fixes (unblocks current work)
2. Week 2: Incremental builds (enables scaling)
3. Week 3-4: Integration (enables agents)

**Estimated total effort**: 2-4 weeks for Tier 1-3 (can be parallel)

---

## Testing & Validation Strategy

### Tier 1 Testing
- Regression tests for each bug fix
- Test piping workflows (mangled symbols)
- Test --build flag with actual object files
- Test error messages and suggestions

### Tier 2 Testing
- Benchmark suite: measure build time before/after
- Automated tests: verify verdicts match incremental vs full
- Stress test: 100 parallel agents on incremental builds
- Cache validation: verify correctness with/without caching
- Service reliability: 24-hour uptime test

### Tier 3 Testing
- Integration tests: --with-diff, watch mode, batch mode
- End-to-end: orchestrate batch mode with 50 functions
- Performance: measure total time for batch operations
- Regression: verify match percentages don't degrade

### Success Criteria
- Tier 1: All tests pass, zero bugs in pipeline
- Tier 2: Build time ≤5 seconds for single file, service uptime ≥99%
- Tier 3: Single command (analyze-function --with-diff) shows both assembly and decompilation

---

## Known Risks & Mitigations

### Risk 1: Incremental builds miss edge cases
**Mitigation**: Periodic full rebuilds every 10 commits, detect vtable issues via objdiff verdict patterns

### Risk 2: Ghidra caching becomes stale
**Mitigation**: Invalidate on binary hash change, add --cache-clear flag

### Risk 3: Parallel agents cause build conflicts
**Mitigation**: Each agent has isolated worktree, orchestrate merges after validation

### Risk 4: Service port conflicts continue
**Mitigation**: Auto-cleanup stale processes on startup, health check endpoint

---

## Decision Points

### Decision 1: Incremental vs Full Build Strategy
- **Current**: Investigate options first (2.1a)
- **Then**: Choose based on ninja capabilities and vtable edge case handling
- **Owner**: Backend engineer + Architecture

### Decision 2: Pyghidra MCP vs Direct Library
- **Status**: Decided - Keep MCP service with reliability improvements
- **Rationale**: Cleaner architecture for parallel agents
- **Revisit**: If service overhead becomes critical (measure with caching)

### Decision 3: Tier 3 Features Priority
- **Current**: All marked P2 (not blocking)
- **Decision**: Implement in order of orchestrate usefulness
- **Priority**: 3.3 (Batch) > 3.1 (Unified) > 3.2 (Watch)

---

## Documentation & Communication

### During Implementation
- Update CLAUDE.md with incremental build guidance
- Add docs/tools/INCREMENTAL_BUILDS.md (detailed guide)
- Update docs/tools/WORKFLOW.md with new commands

### Before Release
- Update README with performance improvements
- Create migration guide for existing workflows
- Document all new flags and options

### After Release
- Blog post: "How we scaled from 50 to 500 functions"
- Technical deep-dive: incremental build strategy
- Agent orchestration guide

---

## Conclusion

This plan transforms the tooling from **good (9/10)** to **best-in-class (10/10)** through:

1. **Tier 1** (1 day): Fix bugs blocking workflows
2. **Tier 2** (1 week): Remove build bottleneck, enable scaling
3. **Tier 3** (2 weeks): Streamline workflow, enable agents

**Total effort**: 2-4 weeks
**ROI**: 5-10x productivity improvement + ability to scale to 500+ functions
**Impact**: Production-quality decomp tooling worthy of publication

The foundation is already excellent. These optimizations unlock the full potential.
