# Session: Orchestrator & Objdiff Tooling Feedback

**Date:** 2026-01-26
**Focus:** Critical evaluation of orchestrator MCP tools and objdiff-cli after attempting to fix 99%+ match functions

## Summary

Attempted to push 9 near-complete functions (98-99.9% match) to 100%. **All were at their practical limits** due to toolchain artifacts, not code logic issues. This revealed several gaps in our tooling.

## Functions Tested

| Function | Match | Verdict | Root Cause |
|----------|-------|---------|------------|
| `Curl_getconnectinfo` | 99.9% | at_limit | Struct layout (SessionHandle 136 bytes off) |
| `SpeechMgr::Enable` | 99.6% | at_limit | `__FILE__` path + LTCG merged calls |
| `BaseMaterial::Copy` | 99.6% | at_limit | LTCG merged ObjRefConcCopyRef (87%) |
| `SongInfoAudioTypeToSym` | 99.6% | at_limit | Jump table offset + LTCG |
| `RhythmDetector::SetType` | 99.7% | at_limit | Struct layout (284 bytes off) |
| `CampaignProgress::IsEraSongAvailable` | 99.5% | at_limit | `__FILE__` + LTCG |
| `op6` (ByteGrinder) | 99.2% | at_limit | LTCG + register allocation |
| `Box::Volume` | 98.8% | at_limit | Instruction scheduling |
| `CharCollide::Load` | 99.2% | at_limit | LTCG merged calls |

## Unfixable Pattern Categories Discovered

### 1. LTCG Merged Calls
The linker merged common template instantiations (`MakeString`, `DataArray::Node`, `ObjRefConcCopyRef`). We call the unmerged versions.

### 2. Struct Layout Mismatches
Base class sizes differ from original. Seen in:
- `SessionHandle.state`: 136 bytes off (0x568 vs 0x4e0)
- `RhythmDetector` hierarchy: 284 bytes off (0xc14 vs 0xaf8)

### 3. `__FILE__` Path Differences
Our build uses full paths (`src/system/gesture/SpeechMgr.cpp`) while original used short names (`SpeechMgr.cpp`).

### 4. Instruction Scheduling
Compiler reorders loads/operations for optimization. `Box::Volume` loads struct fields in different order regardless of source code order.

### 5. Register Allocation
Commutative operations get different operand order (`xor r10,r11` vs `xor r11,r10`).

---

## Tooling Issues & Recommendations

### Issue 1: Verdict System Threshold Too High

**Problem:** The `merged_call_ratio` threshold of 0.8 means functions with 40-70% merged calls still get "NEEDS_INVESTIGATION" instead of "AT_LIMIT".

**Evidence:** Only `BaseMaterial::Copy` (87% merged) got AT_LIMIT verdict. Functions like `op6` (67% merged) got NEEDS_INVESTIGATION.

**Recommendation:** Lower threshold to 0.5 or make it configurable.

**Files to investigate:**
- Wherever the verdict logic lives in objdiff-cli

---

### Issue 2: Missing Struct Offset Mismatch Detection

**Problem:** Struct layout mismatches are a major unfixable category but aren't detected by the pattern analyzer.

**Evidence:** `Curl_getconnectinfo` and `RhythmDetector::SetType` both had obvious patterns:
- Multiple `lwz`/`stw` instructions with consistent offset deltas
- Same registers, same instruction sequence, just different offsets

**Detection approach:**
```
IF multiple load/store instructions have:
  - Same opcode (lwz, stw, lfs, etc.)
  - Same base register
  - Consistent delta between target and base offsets
THEN flag as STRUCT_OFFSET_MISMATCH (unfixable)
```

**Recommendation:** Add `STRUCT_OFFSET_MISMATCH` pattern to objdiff analysis.

---

### Issue 3: Missing `__FILE__` Path Detection

**Problem:** Functions using `MILO_ASSERT` have `__FILE__` string mismatches that are unfixable.

**Evidence:** `SpeechMgr::Enable`, `CampaignProgress::IsEraSongAvailable` both had:
```
Target: lis r9, ??_C@_0O@..@SpeechMgr?4cpp
Base:   lis r9, ??_C@_0CB@..@src?1system?1gesture?1SpeechMgr?4cpp
```

**Detection approach:**
```
IF string literal mismatch AND string contains ".cpp"
THEN flag as FILE_PATH_MISMATCH (unfixable)
```

**Recommendation:** Add `FILE_PATH_MISMATCH` pattern to objdiff analysis.

---

### Issue 4: `lookup_rb3` Search Too Broad

**Problem:** Search returns too many irrelevant results, making it hard to find the actual function implementation.

**Evidence:**
- Searching "CharCollide" returned 20 matches, mostly `#include` and variable declarations
- Searching "Box::Volume" returned unrelated audio "Volume" matches

**Recommendation:**
1. Support exact function signature matching: `lookup_rb3 "Box::Volume()"`
2. Prioritize function definitions over references
3. Filter by file extension (`.cpp` only for implementations)

---

### Issue 5: Attempt History Lacks Context

**Problem:** `get_attempts` returns "unknown" status for all attempts with no useful information.

**Evidence:** `CharCollide::Load` showed 8 attempts, all with:
- Status: "unknown"
- Match went from 99.9% to 0.0% (clearly build errors)
- No record of what was tried or why it failed

**Recommendation:** Store richer attempt metadata:
```json
{
  "status": "build_error|worse|same|improved|complete",
  "error_message": "...",
  "diff_summary": "what changed",
  "code_change": "brief description of what was tried"
}
```

---

### Issue 6: No Unit-Level Pattern Tracking

**Problem:** Once I identified that curl functions have struct layout issues, I had no way to skip them or mark the whole unit.

**Evidence:** Had to test `Curl_getconnectinfo` individually even though all curl functions likely have the same `SessionHandle` offset issue.

**Recommendation:** Add unit metadata to track known-unfixable patterns:
```json
{
  "unit": "system/net/curl/*",
  "known_issues": ["STRUCT_OFFSET_MISMATCH"],
  "notes": "SessionHandle struct 136 bytes smaller than target"
}
```

---

### Issue 7: 99%+ Strategy Was Wrong

**Problem:** The plan assumed 99%+ functions would be "easy wins". In reality, they're almost all at limit due to toolchain differences.

**Evidence:** 9/9 functions tested were unfixable.

**Recommendation:**
1. Target 80-95% functions for actual code fixes
2. Functions at 99%+ should be flagged as "likely at limit" unless they have specific fixable patterns
3. Add a query filter for "functions without known unfixable patterns"

---

## Action Items

### High Priority
- [ ] Add `STRUCT_OFFSET_MISMATCH` pattern detection
- [ ] Add `FILE_PATH_MISMATCH` pattern detection
- [ ] Lower `merged_call_ratio` threshold to 0.5

### Medium Priority
- [ ] Improve `lookup_rb3` with exact match support
- [ ] Store meaningful attempt history
- [ ] Add unit-level metadata for known issues

### Low Priority
- [ ] Add query filter for "likely fixable" functions
- [ ] Consider auto-marking 99%+ functions with only LTCG issues as at_limit

---

## Test Cases for New Patterns

### STRUCT_OFFSET_MISMATCH
```
# Curl_getconnectinfo - should detect 136-byte offset delta
Target: lwz r11, 0x568, r3
Base:   lwz r11, 0x4e0, r3
# Delta: 0x88 (136 bytes) - consistent across multiple instructions
```

### FILE_PATH_MISMATCH
```
# SpeechMgr::Enable - should detect .cpp path difference
Target: ??_C@_0O@...@SpeechMgr?4cpp
Base:   ??_C@_0CB@...@src?1system?1gesture?1SpeechMgr?4cpp
```

---

## Related Files

- `./bin/objdiff-cli` - The extended objdiff binary
- `docs/tools/objdiff/USAGE.md` - Usage documentation
- Orchestrator MCP server (location TBD)

---

## Implementation Phases

### Phase 1: New Pattern Detection (High Priority)

**Goal:** Detect the two most common unfixable patterns automatically.

**1a. STRUCT_OFFSET_MISMATCH Detection**

Location: objdiff-cli pattern analyzer

Implementation:
1. Scan diff for load/store instruction pairs (lwz, stw, lfs, stfs, etc.)
2. Group by base register
3. Calculate offset deltas between target and base
4. If 3+ instructions share the same delta → flag as `STRUCT_OFFSET_MISMATCH`
5. Include delta in verdict message (e.g., "struct offset +136 bytes")

Test: `Curl_getconnectinfo` should detect 0x88 (136 byte) delta

**1b. FILE_PATH_MISMATCH Detection**

Location: objdiff-cli pattern analyzer

Implementation:
1. Scan for string literal mismatches in symbol table
2. Check if either string contains `.cpp`, `.h`, or common source extensions
3. Compare just the filename portion (after last `/` or `\`)
4. If filenames match but full paths differ → flag as `FILE_PATH_MISMATCH`

Test: `SpeechMgr::Enable` should detect path difference

**Deliverables:**
- [ ] `STRUCT_OFFSET_MISMATCH` pattern in verdict output
- [ ] `FILE_PATH_MISMATCH` pattern in verdict output
- [ ] Both patterns count toward AT_LIMIT verdict

---

### Phase 2: Verdict Threshold Tuning (High Priority)

**Goal:** Functions with significant merged call ratios should auto-classify as AT_LIMIT.

Location: objdiff-cli verdict logic

Changes:
1. Lower `merged_call_ratio` threshold from 0.8 to 0.5
2. Make threshold configurable via environment variable or config: `OBJDIFF_MERGED_THRESHOLD=0.5`
3. Document the threshold behavior

**Rationale:** 50%+ merged calls means half the function is unfixable due to LTCG. Further investigation is unlikely to help.

**Deliverables:**
- [ ] Threshold lowered to 0.5 by default
- [ ] Optional: environment variable override
- [ ] Update usage docs with threshold info

---

### Phase 3: RB3 Lookup Improvements (Medium Priority)

**Goal:** Make `lookup_rb3` return actionable results for function implementations.

Location: Orchestrator MCP `lookup_rb3` tool

**3a. Exact Match Support**
```
lookup_rb3 "Box::Volume"      # current: broad search
lookup_rb3 "Box::Volume()"    # new: exact function signature
```

Implementation:
1. Detect `()` or `(` in query → switch to exact function match mode
2. Search for `FunctionName::MethodName(` pattern
3. Return only lines containing the function definition

**3b. Result Prioritization**
1. Rank function definitions (contains `{` on same/next line) highest
2. Rank declarations (contains `;` after signature) second
3. Rank references lowest
4. Filter to `.cpp` files for implementations, `.h` for declarations

**3c. Smarter Filtering**
1. Exclude `#include` lines from results
2. Exclude forward declarations unless specifically requested
3. Limit to 10 most relevant results

**Deliverables:**
- [ ] Exact match mode with `()` syntax
- [ ] Result ranking by definition > declaration > reference
- [ ] `.cpp`-only filter option

---

### Phase 4: Attempt History Enrichment (Medium Priority)

**Goal:** Make `get_attempts` useful for understanding what was tried.

Location: Orchestrator MCP database schema + `report_result` tool

**4a. Schema Changes**
```sql
ALTER TABLE attempts ADD COLUMN error_message TEXT;
ALTER TABLE attempts ADD COLUMN change_description TEXT;
ALTER TABLE attempts ADD COLUMN patterns_detected TEXT;  -- JSON array
```

**4b. report_result Enhancement**
Add optional parameters:
```json
{
  "status": "at_limit",
  "percent": 99.2,
  "notes": "LTCG merged calls unfixable",
  "patterns": ["MERGED_CALLS", "STRUCT_OFFSET"],
  "change_tried": "Reordered member initialization"
}
```

**4c. get_attempts Output**
Include structured history:
```json
{
  "attempts": [
    {
      "date": "2026-01-26",
      "status": "at_limit",
      "percent_before": 99.1,
      "percent_after": 99.2,
      "patterns": ["MERGED_CALLS"],
      "change_tried": "Reordered member initialization"
    }
  ]
}
```

**Deliverables:**
- [ ] Database schema migration
- [ ] `report_result` accepts patterns and change_tried
- [ ] `get_attempts` returns structured history

---

### Phase 5: Unit-Level Metadata (Medium Priority)

**Goal:** Track known issues at the directory/unit level to skip entire problem areas.

Location: Orchestrator MCP + new metadata store

**5a. Unit Metadata Schema**
```json
{
  "unit_patterns": {
    "system/net/curl/*": {
      "known_issues": ["STRUCT_OFFSET_MISMATCH"],
      "notes": "SessionHandle struct 136 bytes smaller than target",
      "skip_above": 98.0
    },
    "system/gesture/*": {
      "known_issues": ["FILE_PATH_MISMATCH"],
      "notes": "All MILO_ASSERT functions have path differences"
    }
  }
}
```

**5b. query_functions Enhancement**
Add filter option:
```
query_functions --exclude-known-issues
query_functions --unit-pattern "system/ui/*" --exclude-known-issues
```

**5c. New MCP Tool: set_unit_metadata**
```json
{
  "tool": "set_unit_metadata",
  "params": {
    "unit_pattern": "system/net/curl/*",
    "known_issues": ["STRUCT_OFFSET_MISMATCH"],
    "notes": "SessionHandle offset"
  }
}
```

**Deliverables:**
- [ ] Unit metadata storage (JSON file or database table)
- [ ] `query_functions` respects unit exclusions
- [ ] `set_unit_metadata` tool for agents to record findings

---

### Phase 6: Query Strategy Improvements (Low Priority)

**Goal:** Help agents find functions worth investigating.

Location: Orchestrator MCP `query_functions` tool

**6a. "Likely Fixable" Filter**
```
query_functions --likely-fixable
```

Excludes functions with:
- 99%+ match AND no specific fixable patterns
- Known unit-level issues (from Phase 5)
- Previous attempts with AT_LIMIT status

**6b. Auto-AT_LIMIT for 99%+ LTCG**
When `run_objdiff` returns:
- Match ≥ 99%
- Only LTCG-related patterns detected (merged calls, register alloc, scheduling)
- No obvious code errors

→ Automatically suggest AT_LIMIT status in verdict

**6c. Target Range Recommendation**
Update documentation to recommend:
- **80-95%**: Best targets for code fixes
- **95-99%**: May be fixable, review patterns first
- **99%+**: Likely at limit, skip unless patterns indicate otherwise

**Deliverables:**
- [ ] `--likely-fixable` filter option
- [ ] Auto-suggest AT_LIMIT for 99%+ LTCG-only functions
- [ ] Updated strategy documentation

---

## Phase Dependencies

```
Phase 1 (Pattern Detection)
    ↓
Phase 2 (Threshold Tuning) ←── can run in parallel with Phase 1
    ↓
Phase 4 (Attempt History) ←── uses patterns from Phase 1
    ↓
Phase 5 (Unit Metadata) ←── uses patterns from Phase 1
    ↓
Phase 6 (Query Strategy) ←── depends on Phases 4 & 5

Phase 3 (RB3 Lookup) ←── independent, can run anytime
```

## Estimated Scope

| Phase | Complexity | Files Affected |
|-------|------------|----------------|
| 1a | Medium | objdiff-cli pattern analyzer |
| 1b | Low | objdiff-cli pattern analyzer |
| 2 | Low | objdiff-cli verdict logic |
| 3 | Medium | Orchestrator MCP lookup_rb3 |
| 4 | Medium | Orchestrator MCP schema + tools |
| 5 | Medium | Orchestrator MCP + new storage |
| 6 | Low | Orchestrator MCP query_functions |
