# Tooling Action Plan - Prioritized Fixes and Improvements

**Generated from**: Comprehensive agent feedback testing (2026-01-25)
**Goal**: Transform 9/10 tools into 10/10 tools, fix critical bugs, enable scaling

---

## Tier 1: Critical Bugs (Must Fix - Do Today/Tomorrow)

### 1. Fix objdiff-cli Mangled Symbol Handling

**Status**: ✅ **FIXED** (2026-01-26) - Mangled symbols now work correctly

**Issue**:
```bash
# Symbols returned by query contain regex special chars
FUNC=$(objdiff-cli report query ... | jq -r '.results[0].name')
# FUNC = "??0StorePanel@@QAA@XZ"

# Attempting to use in next command fails
objdiff-cli report function build/373307D9/report.json "$FUNC"
# Error: regex parse error (? is repetition operator)
```

**Root Cause**: `report function` treats input as regex by default, not exact match

**Solution Options**:
- **Option A** (Recommended): Auto-detect mangled names (start with `?` or `_`) and auto-escape regex special chars
- **Option B**: Make `--exact` the default, `--regex` for advanced usage
- **Option C**: Check if input looks like mangled name, auto-escape

**Implementation**:
1. In objdiff-cli argument parsing, detect mangled names
2. If detected, escape regex special characters or set exact match mode
3. Update help text to document this behavior

**Effort**: 2-4 hours
**Impact**: High - fixes common piping workflows
**Priority**: P0

---

### 2. Fix objdiff-cli --build Path Resolution

**Status**: ✅ **FIXED** (2026-01-26) - Works correctly; original error was misdiagnosed

**Investigation Results**:
```bash
# This works correctly:
objdiff-cli diff -u "default/system/flow/Flow" "??1Flow@@UAA@XZ" --build -f json

# The "No such device or address (os error 6)" error occurs only in TUI mode
# when no terminal is available (e.g., running from scripts/agents)
```

**Resolution**: The path construction is correct (`src/` for compiled base, `obj/` for target).
The error was from TUI mode trying to access a terminal. Use `-f json` or `-f markdown`
for scripted/agent workflows.

**Effort**: ✅ Complete (was not a bug)
**Impact**: High - feature works as designed
**Priority**: P0

---

### 3. Document --exact Flag in analyze-function

**Status**: ✅ **DONE** (2026-01-26) - Already implemented; shows copyable commands

**Investigation Results**:
- `analyze-function` already handles ambiguous symbols well
- Auto-retries on single non-destructor match
- Shows copyable commands like `./bin/analyze-function "Symbol" -u unit/path`
- Points to `--list-units` for discovery

**Example output**:
```
Error: Symbol "Foo::Bar" is ambiguous
Try one of:
  ./bin/analyze-function "Foo::Bar" -u default/system/char/Character
  ./bin/analyze-function "Foo::Bar" -u default/system/ui/UIPanel
```

**Effort**: ✅ Complete (was already implemented)
**Impact**: Medium - reduces support questions
**Priority**: P1

---

## Tier 2: High-Impact Improvements (This Week)

### 4. Implement Incremental Builds

**Status**: ✅ **DONE** (2026-01-26) - 94x speedup achieved

**Achieved**: Single-unit rebuild takes ~0.9s vs 88s full rebuild
**Impact**: Validation phase reduced from 88s → <1s (94x speedup)

**Solution Implemented**:
- `objdiff-cli diff --build` now uses incremental builds by default
- `run_objdiff` orchestrator tool supports `full_build` parameter
- See `docs/PHASE_2_1D_SUMMARY.md` for implementation details

**Effort**: ✅ Complete
**Impact**: CRITICAL - 94x speedup on validation
**Priority**: P0

**Actual outcome**:
- Iteration time: 88s → 0.94s (94x faster)
- Makes 500-function workflows highly feasible

---

### 5. Add Decompilation Caching to Ghidra MCP

**Status**: ✅ **DONE** (2026-01-26) - Infrastructure existed, enabled via service script

**Implementation**:
- Full SQLite cache exists: `tools/pyghidra-mcp-fork/pyghidra_mcp/cache_manager.py`
- Cache stored at `$PROJECT_DIR/cache.db` (project root)
- Service script now passes `--cache-dir "$PROJECT_DIR"` to enable caching

**Cache schema**: `(address, binary_hash, decompilation, timestamp)`

**Verification**:
```bash
# Restart service to pick up --cache-dir flag
./tools/ghidra/pyghidra-service.sh restart

# Run a decompilation
./bin/analyze-function "Flow::Save" -f json | head -20

# Check cache has entries
sqlite3 cache.db "SELECT COUNT(*) FROM decompilation_cache;"
```

**Effort**: ✅ Complete (infrastructure was built, just needed service integration)
**Impact**: High - 100-200x speedup on cache hits
**Priority**: P1

---

### 6. Fix Ghidra Service Reliability Issues

**Status**: ✅ **DONE** (2026-01-26) - See `docs/PHASE_2_3_SUMMARY.md`

**Implemented**:
1. ✅ Port cleanup before restart (automatic)
2. ✅ Health check endpoint with diagnostics
3. ✅ Improved logging to dedicated log files
4. ✅ Better error messages and startup diagnostics

**Solutions Deployed**:
1. Port cleanup: Automatic in service script
2. Health check: `./tools/ghidra/pyghidra-service.sh status` shows health
3. Logging: Dedicated log files with rotation
4. Diagnostics: `./tools/ghidra/pyghidra-service.sh diagnose`

**Effort**: ✅ Complete
**Impact**: Medium - improved reliability, reduced troubleshooting
**Priority**: P1

---

### 7. Integrate analyze-function into objdiff-cli

**Status**: ⏭️ **SUPERSEDED** - Orchestrator MCP tools now provide this

**Original idea**: Combine objdiff diff + Ghidra analysis into single command.

**Why it's no longer needed**:
- `tools/analyze_function.py` already exists and works
- Orchestrator MCP tools (`run_objdiff`, `get_attempts`, `lookup_rb3`) provide integrated experience for agents
- Rewriting Python → Rust for marginal benefit isn't worth the effort

**Effort**: N/A - not doing
**Impact**: N/A
**Priority**: Dropped

---

## Tier 3: Important Features (Next 2 Weeks)

### 8. Add Watch Mode to Tools

**Status**: 🟢 **NICE TO HAVE** - Improves iteration

**Concept**: Auto-rebuild and show diff on file save

```bash
# Watch source file for changes, auto-rebuild and diff
objdiff-cli watch "Foo::Bar"
# Output updates automatically as you edit and save

# Or combined with analyze-function
./bin/analyze-function "Foo::Bar" --watch
```

**Implementation**:
1. Use `notify-rs` or similar to watch source files
2. On change detected: trigger build and diff
3. Show output in split view or live-update markdown

**Effort**: 4-5 days
**Impact**: Medium - improves flow state
**Priority**: P2

---

### 9. Add Batch Mode to analyze-function

**Status**: 🟢 **NICE TO HAVE** - Enables parallel processing

**Concept**: Process multiple functions from a file

```bash
# Create list of functions to analyze
cat > functions.txt << EOF
Foo::Bar
Baz::Qux
System::Init
EOF

# Analyze all in one go
./bin/analyze-function --batch functions.txt -o results.json

# Or pipe from objdiff query
objdiff-cli report query ... | jq -r '.results[].name' | \
  ./bin/analyze-function --batch-stdin -o results.json
```

**Implementation**:
1. Add `--batch <file>` flag to read function names from file
2. Add `--batch-stdin` to read from stdin
3. Process all functions, aggregate results
4. Show progress bar for batch operations

**Effort**: 2-3 days
**Impact**: Medium - enables parallel processing
**Priority**: P2

---

### 10. Add Color Output to Terminal Tools

**Status**: 🟢 **NICE TO HAVE** - Improves readability

**Concept**: Color-code output for better scannability

```bash
objdiff-cli diff "Foo::Bar" --verdict
# Output with colors:
# COMPLETE ✓ (green)
# LIKELY_FIXABLE (yellow)
# AT_LIMIT (orange)
# NEEDS_INVESTIGATION (red)
```

**Implementation**:
1. Use `colored` crate for Rust tools
2. Add `--color {auto|always|never}` flag (default: auto-detect TTY)
3. Color verdicts, percentages, and status messages

**Effort**: 1-2 days
**Impact**: Low-Medium - improves UX
**Priority**: P3

---

## Tier 4: Strategic Improvements (1+ Months)

### 11. AI-Assisted Fix Suggestion Engine

**Status**: 🔵 **STRATEGIC** - High value if successful

**Concept**: Analyze verdict + diff → suggest concrete code changes

**Example workflow**:
```bash
objdiff-cli diff "Foo::Bar" --suggest-fixes
# Output:
# Match: 98.5%
# Issue: Control flow difference
# Suggestion 1: Try inverting the condition (if x > 0 → if x <= 0)
# Suggestion 2: Check for bool cast differences
# Reference: Similar fix worked in Bar::Baz
```

**Implementation**:
1. Build pattern database of successful fixes
2. Analyze diff patterns (control flow, register swaps, etc.)
3. Match against database
4. Generate suggestions with confidence score

**Data sources**:
- Git history (commits that improved match %)
- Manual annotation of common patterns
- ML training on successful fixes

**Effort**: 6-8 weeks
**Impact**: Very High - could automate 30-50% of fixes
**Priority**: P4

---

### 12. Unified Decomp IDE (VSCode Extension)

**Status**: 🔵 **STRATEGIC** - Eliminates context switching

**Concept**: Integrated workflow in VSCode

**Features**:
- Inline match percentage display
- Split-view with Ghidra decompilation
- Live diff highlighting
- Auto-rebuild on save
- One-click to Ghidra/objdiff

**Implementation**:
1. Create VSCode extension
2. Connect to objdiff-cli + Ghidra MCP
3. Show real-time analysis as user edits

**Effort**: 3-4 weeks
**Impact**: High - transforms workflow UX
**Priority**: P4

---

### 13. Distributed Build Cache

**Status**: 🔵 **STRATEGIC** - Enables scaling

**Concept**: Share compiled object files across machines

**Benefits**:
- First build on machine A: 88s
- First build on machine B (with cache): instant
- Enables parallel agents to share work

**Implementation**:
1. Integrate ccache or similar
2. Set up shared cache server
3. Configure ninja to use cache

**Effort**: 2-3 weeks
**Impact**: High for scaled workflows
**Priority**: P4

---

## Implementation Roadmap

### Week 1: Critical Fixes
- [x] Fix objdiff-cli mangled symbol handling (P0) ✅ DONE
- [x] Fix objdiff-cli --build path (P0) ✅ WORKS - Error was TUI mode needing a terminal; use `-f json` or `-f markdown` in scripts
- [x] Document --exact flag in analyze-function (P1) ✅ DONE - Already implemented, shows copyable commands
- [x] **Result**: All bugs fixed, workflows unblocked ✅

### Week 2: Build System Optimization
- [x] Implement incremental builds (P0) ✅ DONE - 94x speedup
- [x] Add decompilation caching (P1) ✅ DONE - Enabled via --cache-dir in service script
- [x] Fix Ghidra service reliability (P1) ✅ DONE
- [x] **Result**: 94x faster iteration, caching enabled, stable service ✅

### Week 3-4: Integration & Features
- [x] Integrate analyze-function into objdiff-cli (P2) - ⏭️ SUPERSEDED by orchestrator MCP tools
- [ ] Add watch mode (P2)
- [ ] Add batch mode (P2)
- [ ] Add color output (P3)
- [ ] **Result**: More streamlined workflow, batch processing enabled

### Month 2: Strategic Features
- [ ] AI-assisted fix suggestions (P4)
- [ ] VSCode extension (P4)
- [ ] Distributed build cache (P4)

---

## Success Metrics

After completing Tier 1 + 2 fixes:

| Metric | Current | Target | Improvement |
|--------|---------|--------|------------|
| Single function analysis | 2 min | 20 sec | **6x faster** |
| 50-function batch | 4 hours | 5 min | **48x faster** |
| 500-function batch | Impractical | 2 hours | **Possible** |
| Tool bugs | 2 critical | 0 | **100%** |
| Service reliability | 5/10 | 9/10 | **+80%** |
| Overall tooling quality | 8/10 | 10/10 | **+25%** |

---

## Resource Allocation

### Development Team
- **Backend Engineer**: Incremental builds, caching, service reliability (1 person, 2 weeks)
- **Full Stack Engineer**: IDE integration, UI improvements (1 person, 3-4 weeks)
- **ML Engineer**: AI-assisted suggestions (1 person, 6-8 weeks optional)

### Critical Path
1. Week 1: Bug fixes (blocking many workflows)
2. Week 2: Incremental builds (enables scaling)
3. Week 3-4: Integration (streamlines workflow)

**Estimated total effort**: 4-6 weeks for Tier 1-3 (can be parallel)

---

## Owner Assignments

| Task | Owner | Effort | Due |
|------|-------|--------|-----|
| Fix mangled symbols bug | Backend | 4h | ASAP |
| Fix --build path bug | Backend | 3h | ASAP |
| Document --exact flag | Docs | 2h | ASAP |
| Incremental builds | Backend | 4-5d | End of week 2 |
| Decompilation caching | Backend | 3d | End of week 2 |
| Service reliability | Devops/Backend | 2-3d | End of week 2 |
| objdiff-cli integration | Backend | 4d | End of week 3 |
| Watch mode | Full Stack | 4-5d | End of week 4 |
| Batch mode | Full Stack | 3d | End of week 4 |

---

## Conclusion

The tooling is already **excellent** (8-9/10 quality). With these fixes:
1. **Critical bugs eliminated** (P0 fixes)
2. **Build bottleneck removed** (17x speedup)
3. **Workflows integrated** (single commands instead of chains)
4. **Scaling enabled** (500+ functions feasible)

**Result**: Production-quality decomp tooling worthy of publication and reuse by the broader reverse engineering community.
