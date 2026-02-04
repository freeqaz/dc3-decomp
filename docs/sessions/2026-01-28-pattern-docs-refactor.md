# Pattern Documentation Refactor Plan

**Date:** 2026-01-28
**Goal:** Consolidate pattern research into a permanent "agentic wiki" under `docs/decomp/patterns/`, add **automatic pattern detection** to the MCP tool, and slim down the master agent prompt.

**Status:** AMENDED - Simplified approach (no `/pattern` skill, automatic detection instead)

---

## Executive Summary

**What:** Consolidate 5+ overlapping pattern docs into a single `docs/decomp/patterns/` wiki with **automatic pattern detection** in the `run_analyze_function` MCP tool.

**Why:** Pattern knowledge is scattered, wastes agent context, and can't be looked up on-demand.

**Key Changes:**
1. Create `docs/decomp/patterns/` with fixable/unfixable subdirectories
2. Write a `CHEATSHEET.md` (~50 lines) with measured success rates for prompts
3. Inline examples into pattern docs + create `examples/INDEX.md` as case study entrypoint
4. **Add automatic pattern detection to `analyze_function.py`** (replaces `/pattern` skill)
5. Deprecate `patterns.json` to `archive/`
6. Delete unimplemented permuter tool docs (DESIGN.md, USAGE.md)

**Outcome:** Agents get automatic pattern suggestions in every `run_analyze_function` call - no manual lookup needed.

**Effort:** Medium (7 phases, ~18 files to create/update)

---

## Amendment Notes (2026-01-28)

**Decision:** Replace `/pattern` skill with automatic detection in MCP tool.

**Rationale:**
- Agents already call `run_analyze_function` for every function they work on
- Adding detection there means zero extra steps - patterns are surfaced automatically
- Reduces cognitive load - agent doesn't need to know to call `/pattern`
- Detection rules embedded in `analyze_function.py` (simpler than separate YAML)

**Implementation approach:**
1. Add pattern classifier to `analyze_function.py` that post-processes objdiff output
2. Scan instructions for known symptoms (beq vs ble, fmsubs vs fnmsubs, etc.)
3. Map to fix patterns (U01, V01, etc.) with success rates and suggested fixes
4. Start with U01 (beq vs ble) as proof-of-concept - 95% success rate, easy to detect

---

## Problem Statement

Our pattern research (what source changes fix match percentages) is scattered across multiple files with significant overlap. This wastes agent context and makes pattern lookup difficult during iteration.

### Current State

| File | Lines | Type | Issues |
|------|-------|------|--------|
| `scripts/master_agent_prompt.md` | 532 | Agent context | ~54 lines of pattern content |
| `docs/permuter/PATTERNS.md` | 529 | Project doc | Comprehensive but verbose, temporary location |
| `docs/permuter/EXAMPLES.md` | 534 | Project doc | Great content, not referenced, temporary location |
| `docs/meta-strategy/APPENDIX_PATTERNS.md` | 368 | Reference | Overlaps with PATTERNS.md |
| `docs/decomp/patterns.json` | 776 | Machine-readable | Structured but not agent-friendly, outdated |
| `docs/decomp/PATTERN_DATABASE.md` | 291 | Integration docs | Describes patterns.json usage |

### Key Problems

1. **No On-Demand Lookup** - Agents get everything upfront, can't lookup specific symptoms
2. **No Single Source of Truth** - 3+ pattern catalogs with different structures
3. **Temporary vs Permanent** - `docs/permuter/` is project docs, not reference docs
4. **Examples Separated from Patterns** - EXAMPLES.md has excellent before/after but isn't colocated
5. **No Agent Discovery** - No skill to help agents find the right pattern

---

## Proposed Solution

### Target Structure

```
docs/decomp/patterns/
├── INDEX.md                    # Quick reference + navigation
├── CHEATSHEET.md               # Symptom → fix table with success rates (for prompts)
│
├── fixable/
│   ├── comparison.md           # U01, U02 - unsigned/signed comparisons + examples
│   ├── control-flow.md         # E01 - ternary, if/else, loops + examples
│   ├── declarations.md         # V01, V02, V03 - variable order + examples
│   ├── float-math.md           # F01, C03 - FMA, float/double + examples
│   ├── destructors.md          # D01 - explicit destructor + examples
│   ├── operators.md            # O01, I01 - overload selection + examples
│   └── casting.md              # C01, C02 - explicit casts, noreturn + examples
│
├── unfixable/
│   ├── linker.md               # LINKER_MERGED, LTCG_POOLING
│   ├── compiler.md             # BOOL_MASK, REGISTER_SWAP, ASSERT_REVS
│   └── thresholds.md           # Known limit thresholds by module/pattern
│
└── examples/
    └── INDEX.md                # Case study entrypoint - links to patterns by real function

tools/analyze_function.py      # ENHANCED: Automatic pattern detection
```

**Note:** No `/pattern` skill needed - detection is automatic in the MCP tool.

### File Purposes

| File | Purpose | Size Target |
|------|---------|-------------|
| `INDEX.md` | Entry point, links to all pattern docs, search guidance | ~50 lines |
| `CHEATSHEET.md` | Symptom → fix table with measured success rates | ~50 lines |
| `examples/INDEX.md` | Case study entrypoint with links to patterns | ~100 lines |
| `fixable/*.md` | Detailed pattern docs with detection + fix + **inline examples** | ~120 lines each |
| `unfixable/*.md` | Stop-sign patterns with detection + acceptance criteria | ~60 lines each |
| `analyze_function.py` | **Enhanced** with automatic pattern detection | Embedded rules |

### CHEATSHEET.md Format (Updated with Measured Success Rates)

This is what gets embedded in the master prompt:

```markdown
# Pattern Cheatsheet

## High-Success Fixes (95%+ Success Rate)

| Symptom in objdiff | Pattern | Fix | Typical Impact |
|--------------------|---------|-----|----------------|
| `beq` vs `ble` on unsigned | U01 | `x != 0` → `x > 0` | +0.4% to +1.3% |
| `cmplwi` vs `cmpwi` | U02 | Add `(int)` or `(unsigned)` cast | +1% to +50% |
| Destructor at <50% match | D01 | Add explicit `~Class() {}` | +37% to +70% |
| `fmsubs` vs `fnmsubs` | F01 | Flip: `x*y - 1.0f` → `1.0f - x*y` | +1% to +75% |
| `std::floor` vs `floor` | C01 | Use `floor()` + explicit `(float)` cast | +35% |
| `empty()` generates different code | V01 | `v.empty()` → `v.size() == 0` | +1% to +35% |

## Medium-Success Fixes (60-80% Success Rate)

| Symptom | Pattern | Fix | Notes |
|---------|---------|-----|-------|
| Register swaps (r30/r31) | V03 | Reorder variable declarations | 30% success, try 2-5 times |
| Extra branches | E01 | Try ternary vs if/else | ~75% success |
| Load order wrong | - | Swap function argument order | Depends on context |

## Stop Signs (Accept Current %)

| Symptom | Pattern | Action |
|---------|---------|--------|
| `bl merged_*` or `OnlyReturns` | LINKER_MERGED | Stop, report at_limit |
| `clrlwi r3, rX, 24` | BOOL_MASK | Stop, report at_limit |
| ASSERT_REVS at 99%+ | SCHEDULING | Stop, report at_limit |
| 10+ register swaps, no progress | REGISTER_SWAP | Stop after 5 tries |

**Full docs:** `docs/decomp/patterns/` | **Auto-detection:** Patterns surfaced in `run_analyze_function` output
```

### Category Doc Format (Updated with Inline Examples)

Each `fixable/*.md` follows this template:

```markdown
# Comparison Patterns (U01, U02)

## U01: Unsigned Zero Comparison

**Success Rate:** HIGH (95%+)
**Typical Impact:** +0.4% to +1.3%

### Detection
- objdiff shows: `beq` vs `ble` (or `bne` vs `bgt`)
- Context: unsigned variable compared to zero
- Instruction: comparison is `cmpwi` (same), branch differs

### Fix
Use `x > 0` instead of `x != 0` for unsigned types.

### Why It Works
`!= 0` generates equality test (beq/bne), `> 0` generates relational test (ble/bgt).
For unsigned types these are mathematically equivalent but generate different branches.

---

### Example: CharFaceServo::Load

**File:** `src/system/char/CharFaceServo.cpp:72`
**Before:** 98.8% | **After:** 99.5% (+0.7%)

```cpp
// Before (wrong branch)
if (d.rev != 0)
    bs >> mBlinkClipLeftName;

// After (correct branch)
if (d.rev > 0)
    bs >> mBlinkClipLeftName;
```

---

### Example: UIListArrow::Load

**File:** `src/system/ui/UIListArrow.cpp`
**Before:** 94.2% | **After:** 95.5% (+1.3%)

[example code...]

---

## U02: Signed/Unsigned Cast Forcing

[similar structure with inline examples...]
```

---

## Automatic Pattern Detection Specification

Enhance `tools/analyze_function.py` to automatically detect patterns and surface suggestions.

### Detection Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│  objdiff --verdict --include-instructions output                    │
│  ├── patterns: [REGISTER_SWAP, LINKER_MERGED, ...]                  │
│  ├── instructions: [{match_type, target, base}, ...]                │
│  └── verdict: {classification, suggestions}                         │
│                            ↓                                        │
│  Pattern Classifier (new code in analyze_function.py)               │
│  ├── detect_fix_patterns(instructions) → List[DetectedPattern]      │
│  │   - Scan for branch mismatches (beq vs ble → U01)                │
│  │   - Scan for FMA mismatches (fmsubs vs fnmsubs → F01)            │
│  │   - Scan for bool masks (clrlwi r3 → BOOL_MASK stop sign)        │
│  ├── map_objdiff_patterns(analysis) → List[DetectedPattern]         │
│  │   - REGISTER_SWAP → V03 suggestion                               │
│  │   - LINKER_MERGED → stop sign                                    │
│  └── merge and deduplicate patterns                                 │
│                            ↓                                        │
│  Enhanced Output (added to existing format)                         │
│  ├── detected_patterns: [{id, confidence, evidence, fix, rate}]     │
│  ├── stop_signs: [{id, reason}]                                     │
│  └── suggested_fixes: [{pattern, fix, success_rate, example}]       │
└─────────────────────────────────────────────────────────────────────┘
```

### Detection Rules (Embedded in Python)

| Symptom | Detection Logic | Pattern | Suggested Fix |
|---------|-----------------|---------|---------------|
| `beq` vs `ble` (or `bne` vs `bgt`) | Branch opcode diff + unsigned context | U01 | `x != 0` → `x > 0` |
| `cmplwi` vs `cmpwi` | Comparison opcode diff | U02 | Add `(int)` or `(unsigned)` cast |
| `fmsubs` vs `fnmsubs` | FMA opcode diff | F01 | Flip subtraction: `a - b` → `b - a` |
| `clrlwi r3, rX, 24` | Bool mask pattern | BOOL_MASK | Stop sign - unfixable |
| `bl merged_*` | Merged function call | LINKER_MERGED | Stop sign - unfixable |
| High register swap count | REGISTER_SWAP from objdiff | V03 | Reorder variable declarations |

### Output Enhancement

Add new section to markdown/JSON output:

```markdown
## Detected Fix Patterns

| Pattern | Confidence | Evidence | Suggested Fix | Success Rate |
|---------|------------|----------|---------------|--------------|
| U01 | HIGH | beq vs ble at index 47 | `x != 0` → `x > 0` | 95% |
| V03 | MEDIUM | 13 register swaps (r26↔r27) | Reorder variable declarations | 30% |

### Stop Signs
- **LINKER_MERGED**: 4 merged function calls detected - function is at its limit
```

### Proof of Concept: U01 Detection

Start with U01 (unsigned zero comparison) because:
1. Easy to detect: look for `beq` vs `ble` or `bne` vs `bgt` in branch instructions
2. High success rate: 95%+
3. Clear fix: `x != 0` → `x > 0`
4. Measurable impact: +0.4% to +1.3%

Detection pseudocode:
```python
def detect_u01(instructions: List[dict]) -> Optional[DetectedPattern]:
    """Detect U01: unsigned zero comparison (beq vs ble)."""
    for instr in instructions:
        if instr.get("match_type") != "diff_op":
            continue
        target_op = instr.get("target", {}).get("opcode", "")
        base_op = instr.get("base", {}).get("opcode", "")

        # Check for branch mismatch patterns
        if (target_op, base_op) in [("beq", "ble"), ("bne", "bgt"),
                                     ("ble", "beq"), ("bgt", "bne")]:
            return DetectedPattern(
                id="U01",
                name="Unsigned Zero Comparison",
                confidence="high",
                evidence=f"{target_op} vs {base_op} at index {instr.get('index')}",
                fix="Use `x > 0` instead of `x != 0` for unsigned types",
                success_rate="95%",
                typical_impact="+0.4% to +1.3%"
            )
    return None
```

---

## Migration Plan

### Phase 0: Review (COMPLETE)

- [x] Review this plan document
- [x] Validate content mapping is correct
- [x] Confirm permuter/DESIGN.md and USAGE.md can be deleted
- [x] Sign off on final structure
- [x] Decide on examples approach (inline + examples/INDEX.md)
- [x] Decide on patterns.json (deprecate to archive/)
- [x] **AMENDED:** Replace `/pattern` skill with automatic detection in MCP tool

### Phase 1: Create Structure

- [ ] Create `docs/decomp/patterns/` directory
- [ ] Create `docs/decomp/patterns/fixable/` subdirectory
- [ ] Create `docs/decomp/patterns/unfixable/` subdirectory
- [ ] Create `docs/decomp/patterns/examples/` subdirectory
- [ ] Write `INDEX.md` skeleton with navigation

### Phase 1.5: Automatic Pattern Detection (NEW - replaces skill)

Implement pattern detection in `tools/analyze_function.py`:

- [ ] Add `DetectedPattern` dataclass
- [ ] Implement `detect_u01()` - beq vs ble detection (proof of concept)
- [ ] Implement `detect_stop_signs()` - LINKER_MERGED, BOOL_MASK
- [ ] Add detection to `run_objdiff()` or `analyze_function()` flow
- [ ] Update `format_markdown()` to include detected patterns section
- [ ] Update `format_json()` to include detected patterns
- [ ] Test with known functions (CharFaceServo::Load for U01)

**Key insight:** Agent discovers patterns automatically via tool output, not manual lookup.

### Phase 2: Core Docs

- [ ] Write `CHEATSHEET.md` with measured success rates from PATTERNS.md
- [ ] Create `examples/INDEX.md` as case study entrypoint
- [ ] ~~Create `.claude/skills/pattern/SKILL.md`~~ (REMOVED - using auto-detection)

### Phase 3: Fixable Patterns (with inline examples)

- [ ] Write `fixable/comparison.md` (U01, U02) - migrate examples inline
- [ ] Write `fixable/declarations.md` (V01, V02, V03) - migrate examples inline
- [ ] Write `fixable/control-flow.md` (E01) - migrate examples inline
- [ ] Write `fixable/float-math.md` (F01, C03) - migrate examples inline
- [ ] Write `fixable/casting.md` (C01, C02) - migrate examples inline
- [ ] Write `fixable/destructors.md` (D01) - migrate examples inline
- [ ] Write `fixable/operators.md` (O01, I01) - migrate examples inline

### Phase 4: Unfixable Patterns

- [ ] Write `unfixable/linker.md` (LINKER_MERGED, LTCG)
- [ ] Write `unfixable/compiler.md` (BOOL_MASK, REGISTER_SWAP, ASSERT_REVS)
- [ ] Write `unfixable/thresholds.md` (module-specific limits)

### Phase 5: Update References

- [ ] Update `scripts/master_agent_prompt.md` to use CHEATSHEET content
- [ ] Update `docs/permuter/INDEX.md` to redirect to new location
- [ ] Update `docs/meta-strategy/APPENDIX_PATTERNS.md` to redirect
- [ ] Add links from `docs/decomp/TECHNICAL_NOTES.md`
- [ ] Update/deprecate `docs/decomp/PATTERN_DATABASE.md`

### Phase 6: Cleanup

- [ ] Delete `docs/permuter/PATTERNS.md` (content migrated)
- [ ] Delete `docs/permuter/EXAMPLES.md` (content migrated)
- [ ] Delete `docs/permuter/DESIGN.md` (unimplemented tool)
- [ ] Delete `docs/permuter/USAGE.md` (unimplemented tool)
- [ ] Move `docs/decomp/patterns.json` → `archive/patterns.json.deprecated`

### Phase 7: Validation

- [ ] Verify all pattern IDs are documented
- [ ] Verify all examples have pattern references
- [ ] Test automatic pattern detection with various functions
- [ ] Test agent workflow with new docs + auto-detection
- [ ] Verify master prompt references work
- [ ] Verify MCP tool returns detected patterns correctly

---

## Master Prompt Changes

### Current Pattern Content (Actual Lines)

~54 lines of pattern-related content:
- Lines 198-205: "Common high-impact fixes" (~8 lines)
- Lines 285-310: "Function Type Patterns" (~25 lines, keep - useful context)
- Lines 313-333: "Unfixable Patterns" table (~21 lines)

### Proposed Replacement

Replace lines 198-205 and 313-333 (~29 lines) with reference to CHEATSHEET:

```markdown
## Pattern Quick Reference

See `docs/decomp/patterns/CHEATSHEET.md` for full symptom → fix table.

**Note:** The `run_analyze_function` MCP tool now **automatically detects patterns** and suggests fixes. Check the "Detected Fix Patterns" section in its output.

**Quick fixes:**
- `beq` vs `ble` on unsigned → use `x > 0` not `x != 0`
- Destructor <50% → add explicit `~Class() {}`
- `fmsubs` vs `fnmsubs` → flip subtraction order

**Stop immediately if you see:**
- `bl merged_*` → LINKER_MERGED (unfixable)
- `clrlwi r3, rX, 24` → BOOL_MASK (unfixable)
- ASSERT_REVS at 99%+ → scheduling (unfixable)

**For detailed patterns:** read `docs/decomp/patterns/`
```

**Result:** ~15 lines instead of ~29 lines

**Note:** The main win is **automatic detection** - agents don't need to manually look up patterns. The Function Type Patterns section (lines 285-310) stays as useful context.

---

## Success Criteria

1. **Single Source of Truth:** All pattern knowledge in `docs/decomp/patterns/`
2. **Automatic Detection:** `run_analyze_function` surfaces patterns without manual lookup
3. **Examples Inline:** Each pattern has examples right in the doc
4. **Case Study Index:** `examples/INDEX.md` links real functions to patterns
5. **Clear Hierarchy:** Fixable vs unfixable, category-based organization
6. **Measured Success Rates:** CHEATSHEET includes actual statistics from decomp.db
7. **Zero Extra Steps:** Agents get pattern suggestions in every analysis call

---

## Decisions Made

| Question | Decision | Rationale |
|----------|----------|-----------|
| patterns.json integration | **Deprecate** to `archive/` | Markdown is source of truth, JSON was never maintained |
| Pattern discovery mechanism | **Automatic detection in MCP tool** | Zero extra steps - patterns surfaced in every analysis |
| Detection rules location | **Embedded in analyze_function.py** | Simpler than separate YAML, easier to maintain |
| Examples placement | **Inline + examples/INDEX.md** | More examples per pattern, case study entrypoint |
| Permuter folder fate | **Delete DESIGN.md, USAGE.md** | Unimplemented tool - never built |
| TECHNICAL_NOTES.md overlap | **Keep separate, link** | Broader compiler quirks, not just fix patterns |
| Pattern grouping | **Explore in Phase 1.5** | Now handled by automatic detection |
| First pattern to implement | **U01 (beq vs ble)** | 95% success rate, easy to detect, good proof of concept |

---

## Content Mapping

### What Goes Where

| Content Type | Source | Destination |
|-------------|--------|-------------|
| Pattern definitions (U01, V01, etc.) | `permuter/PATTERNS.md` | `decomp/patterns/fixable/*.md` |
| Pattern statistics | `permuter/PATTERNS.md` | `decomp/patterns/CHEATSHEET.md` |
| Real examples with before/after | `permuter/EXAMPLES.md` | **Inline in each pattern file** |
| Case study index | `permuter/EXAMPLES.md` | `decomp/patterns/examples/INDEX.md` |
| ROI rankings table | `permuter/PATTERNS.md` | `decomp/patterns/INDEX.md` |
| Failed pattern attempts | `permuter/PATTERNS.md` | `decomp/patterns/fixable/*.md` (as warnings) |
| Unfixable pattern definitions | `meta-strategy/APPENDIX_PATTERNS.md` | `decomp/patterns/unfixable/*.md` |
| Quick reference table | `meta-strategy/APPENDIX_PATTERNS.md` | `decomp/patterns/CHEATSHEET.md` |
| objdiff detection guidance | `meta-strategy/APPENDIX_PATTERNS.md` | `decomp/patterns/INDEX.md` |
| Compiler behavior notes | `decomp/TECHNICAL_NOTES.md` | Keep in place, link from patterns/ |
| Merged function explanations | `decomp/TECHNICAL_NOTES.md` | Keep in place, link from patterns/ |
| Machine-readable patterns | `decomp/patterns.json` | **Deprecate** → `archive/` |

### Fixable Pattern Category Assignments

| Pattern IDs | Category File | Description |
|-------------|---------------|-------------|
| U01, U02 | `comparison.md` | Unsigned/signed zero comparisons, cast forcing |
| V01, V02, V03 | `declarations.md` | Variable order, size caching, quaternion order |
| E01 | `control-flow.md` | Explicit if vs Max(), ternary vs if-else |
| F01, C03 | `float-math.md` | FMA instruction selection, float/double separation |
| C01, C02 | `casting.md` | Explicit float casts, noreturn |
| D01 | `destructors.md` | Explicit destructor declaration |
| O01, I01 | `operators.md` | Operator overload selection, inline assignment |

**Note:** C03 moved to float-math.md (more related to F01 than C01/C02).

### Unfixable Pattern Category Assignments

| Pattern | Category File | Description |
|---------|---------------|-------------|
| LINKER_MERGED, LTCG_POOLING | `linker.md` | ICF, global pooling, merged functions |
| BOOL_MASK, REGISTER_SWAP, ASSERT_REVS | `compiler.md` | ABI, register allocation, scheduling |
| Module-specific limits | `thresholds.md` | Known ceiling percentages by subsystem |

---

## Appendix A: Files to Read Before Implementation

- `scripts/master_agent_prompt.md` - current agent context
- `docs/permuter/PATTERNS.md` - source for fixable patterns
- `docs/permuter/EXAMPLES.md` - source for real examples
- `docs/meta-strategy/APPENDIX_PATTERNS.md` - source for unfixable patterns
- `docs/decomp/TECHNICAL_NOTES.md` - compiler quirks, merged functions

---

## Appendix B: File Disposition Analysis

### `docs/permuter/` Folder

| File | Content | Disposition |
|------|---------|-------------|
| `INDEX.md` | Links to PATTERNS/EXAMPLES/DESIGN/USAGE | **Update** to redirect to new location |
| `PATTERNS.md` | 529 lines of pattern definitions | **Delete** after migration |
| `EXAMPLES.md` | 534 lines of real examples | **Delete** after migration |
| `DESIGN.md` | Permuter tool architecture (TODO/unimplemented) | **Delete** - tool doesn't exist |
| `USAGE.md` | Permuter tool usage (TODO/unimplemented) | **Delete** - tool doesn't exist |

**Note:** DESIGN.md and USAGE.md describe a "C++ permuter tool" that was never implemented. The prototype exists at `scripts/test_burnxfm_variations.py` but the generalized tool was never built.

### `docs/decomp/TECHNICAL_NOTES.md`

**Content:** Compiler behavior, merged functions, common matching issues

**Disposition:** **Keep as companion doc** - This contains:
- Inlined functions list (strcpy, strlen, etc.)
- Static Symbol initialization patterns
- Merged function explanations (scalar deleters, thunks)
- Detailed compiler behavior notes

This is broader "compiler quirks" documentation, not just fix patterns. Keep it separate but cross-reference from the patterns wiki.

### `docs/decomp/patterns.json`

**Content:** Machine-readable pattern database with detection rules (776 lines)

**Disposition:** **Deprecate** → `archive/patterns.json.deprecated`
- Was never kept in sync with markdown docs
- Markdown is more readable and maintainable
- Skills can search markdown directly

---

## Appendix C: Relationship Between Docs

After refactor:

```
docs/decomp/
├── TECHNICAL_NOTES.md          # Compiler quirks (broader context)
├── PATTERN_DATABASE.md         # Update to redirect to patterns/
└── patterns/                   # NEW: Human-readable pattern wiki
    ├── INDEX.md
    ├── CHEATSHEET.md           # For embedding in prompts
    ├── fixable/                # From permuter/PATTERNS.md + EXAMPLES.md
    │   └── *.md                # Each file has inline examples
    ├── unfixable/              # From meta-strategy/APPENDIX_PATTERNS.md
    │   └── *.md
    └── examples/
        └── INDEX.md            # Case study entrypoint

tools/
└── analyze_function.py         # ENHANCED: Automatic pattern detection

archive/
└── patterns.json.deprecated    # Old machine-readable format
```

**Cross-references:**
- `TECHNICAL_NOTES.md` → links to `patterns/` for specific fixes
- `patterns/INDEX.md` → links to `TECHNICAL_NOTES.md` for compiler context
- `scripts/master_agent_prompt.md` → embeds `CHEATSHEET.md` content
- `analyze_function.py` → detects patterns automatically, references `patterns/` in suggestions

---

## Appendix D: Master Prompt Line Count Analysis (CORRECTED)

Current `scripts/master_agent_prompt.md` (532 lines):

| Section | Lines | Content | Action |
|---------|-------|---------|--------|
| Header + Assignment | 1-16 | Function info | Keep |
| MCP Worktree Warning | 17-43 | Critical warning | Keep |
| Pre-Computed Context | 44-136 | RB3 ref, Ghidra, etc. | Keep |
| Phase 1-2 (Context/Analyze) | 137-186 | Instructions | Keep |
| **Phase 3 (Edit) - Pattern hints** | **198-205** | 8 lines of fixes | **Reference CHEATSHEET** |
| Phase 4-5 (Verify/Verdict) | 210-264 | Instructions | Keep |
| Phase 6 (Stop) | 250-264 | Stop conditions | Keep |
| Phase 7 (Report) | 266-282 | Reporting | Keep |
| Function Type Patterns | 285-310 | Load/Save/Init/Poll | Keep (useful) |
| **Unfixable Patterns Table** | **313-333** | 21 lines | **Trim to stop-signs** |
| Safety Rules | 336-343 | Instructions | Keep |
| Iteration Limit | 344-350 | Instructions | Keep |
| Troubleshooting | 351-378 | Help | Keep |
| Example Session | 379-401 | Example | Keep |
| Tool Reference | 404-453 | MCP tools | Keep |
| External Resources | 455-532 | RB3/RB2 refs | Keep |

**Actual reduction targets:**
- Phase 3 pattern hints: 8 lines → 3 lines (just reference CHEATSHEET)
- Unfixable patterns table: 21 lines → 8 lines (just stop signs)

**Estimated savings:** ~18 lines (532 → ~514 lines)

**Real value:** Organization + `/pattern` skill for on-demand lookup, not line count reduction.

---

## Appendix E: Pattern Detection Implementation Notes

### Detection Priority Order

When multiple patterns are detected, prioritize by:
1. **Stop signs first** - LINKER_MERGED, BOOL_MASK (agent should stop immediately)
2. **High success rate** - U01 (95%), D01 (95%), F01 (95%)
3. **Medium success rate** - V03 (30%), E01 (75%)

### Instruction Pattern Signatures

| Pattern | Target Opcode | Base Opcode | Context |
|---------|---------------|-------------|---------|
| U01 | `beq` | `ble` | Unsigned comparison to 0 |
| U01 | `bne` | `bgt` | Unsigned comparison to 0 |
| F01 | `fmsubs` | `fnmsubs` | FMA with subtraction |
| F01 | `fnmsubs` | `fmsubs` | FMA with subtraction |
| BOOL_MASK | `clrlwi` | - | r3 as destination, immediate 24 |

### Future Enhancements

1. **LLM fallback** - For ambiguous cases, call Haiku with instruction context
2. **Pattern chaining** - Detect combinations (e.g., U01 + V03 often appear together)
3. **Confidence scoring** - Weight by instruction count, context quality
4. **Learning** - Track which suggestions led to improvements in decomp.db
