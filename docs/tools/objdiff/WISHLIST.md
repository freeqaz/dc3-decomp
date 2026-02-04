# objdiff CLI Improvement Ideas

Proposed enhancements to objdiff-cli that would help with decompilation work. These features **do not currently exist** - this is a wishlist for potential future development.

> **Note:** Some items from this wishlist have been incorporated into the Phase 5 implementation plan. See [OBJDIFF_CLI_IMPLEMENTATION.md](./OBJDIFF_CLI_IMPLEMENTATION.md) for details.

---

## 1. Automatic Fixability Analysis (`--diagnose`)

> **Status:** ✅ **IMPLEMENTED** as `--analyze` and `--verdict` flags (PR #2, 2026-01-23).

**Actual command:**
```bash
objdiff-cli diff -p . "Symbol" -f json --verdict
```

**Actual output:**
```json
{
  "verdict": {
    "classification": "AT_LIMIT",
    "confidence": "high",
    "explanation": "85.7% of mismatches are calls to linker-merged functions.",
    "recommendation": "Accept current match (97.0%). Effort better spent elsewhere."
  },
  "analysis": {
    "patterns": [{"pattern": "LINKER_MERGED", "instruction_count": 14, ...}]
  }
}
```

See [OBJDIFF_CLI_USAGE.md](./OBJDIFF_CLI_USAGE.md#analysis--verdict) for full documentation.

---

## 2. Diff Summary with Grouping

> **Status:** ✅ **PARTIALLY IMPLEMENTED** via `--summary` and `--analyze` flags (PR #1-2, 2026-01-23).

**Implemented:**
- `--summary` provides instruction match type counts (equal, diff_arg, diff_op, etc.)
- `--analyze` detects and groups patterns (LINKER_MERGED, BOOL_MASK, REGISTER_SWAP)

**Still wishlist:** Grouping consecutive mismatches into "blocks" with position info:
```
Block 1 (instructions 16-20): Function call differences
  - 5 diff_arg to merged functions
  - Likely cause: Linker merging
```

---

## 3. Register Allocation Analyzer

> **Status:** ✅ **IMPLEMENTED** as REGISTER_SWAP pattern in `--analyze` (PR #2, 2026-01-23).

**Actual command:**
```bash
objdiff-cli diff -p . "Symbol" -f json --analyze | jq '.analysis.patterns[] | select(.pattern == "REGISTER_SWAP")'
```

**Actual output:**
```json
{
  "pattern": "REGISTER_SWAP",
  "confidence": "high",
  "instruction_count": 15,
  "fixability": "maybe_fixable",
  "details": {
    "swaps": [{"target_reg": "r26", "base_reg": "r27", "count": 15}]
  }
}
```

The `--verdict` flag will also suggest "Try reordering variable declarations" when this pattern is detected.

---

## 4. Merged Function Catalog

> **Status:** 📋 **PLANNED** for Phase 6 - see [OBJDIFF_CLI_IMPLEMENTATION.md](./OBJDIFF_CLI_IMPLEMENTATION.md#61-merged-function-catalog).

**Proposed command:**
```bash
objdiff-cli report merged-functions build/373307D9/report.json
```

**Proposed output:**
```
Merged Function Analysis:
  merged_Read4FloatStruct: 47 references across 12 units
    Targets: Color, Vector4 stream operators
  merged_Read3FloatStruct: 31 references across 8 units
    Targets: Vector3 stream operators
  OnlyReturns: 23 references across 15 units
    Targets: Simple getter functions

Impact: ~2.3% of total instructions affected by linker merging
```

---

## 5. Historical Tracking

> **Status:** 📋 **PLANNED** for Phase 6 - see [OBJDIFF_CLI_IMPLEMENTATION.md](./OBJDIFF_CLI_IMPLEMENTATION.md#62-historical-tracking).

**Problem:** Hard to know if a change helped without manual comparison.

**Proposed enhancement:** Track match history:
```bash
objdiff-cli diff -p . "Symbol" --track
# Stores current match in .objdiff-history

objdiff-cli history "Symbol"
# Shows:
# 2026-01-23 14:30  97.0%
# 2026-01-23 14:35  97.1%  (+0.1%)
# 2026-01-23 14:40  94.5%  (-2.6%)  ← regression
# 2026-01-23 14:45  97.0%  (reverted)
```

---

## 6. Batch Diagnosis

> **Status:** ✅ **IMPLEMENTED** as `report analyze` command (PR #3, 2026-01-23).

**Actual command:**
```bash
objdiff-cli report analyze build/373307D9/report.json --min-percent 90 --max-percent 99 --limit 50 -f json-pretty
```

**Actual output:**
```json
{
  "summary": {
    "total_analyzed": 50,
    "by_verdict": {
      "LIKELY_FIXABLE": 8,
      "MAYBE_FIXABLE": 2,
      "AT_LIMIT": 25,
      "NEEDS_INVESTIGATION": 15
    }
  },
  "results": {
    "LIKELY_FIXABLE": [...],
    "AT_LIMIT": [...],
    ...
  }
}
```

See [OBJDIFF_CLI_USAGE.md](./OBJDIFF_CLI_USAGE.md#report-analyze) for full documentation.

---

## 7. Instruction Context Window

> **Status:** 📋 **PLANNED** for Phase 6 - see [OBJDIFF_CLI_IMPLEMENTATION.md](./OBJDIFF_CLI_IMPLEMENTATION.md#63-instruction-context-window).

**Problem:** Seeing just the mismatched instruction without context makes diagnosis harder.

**Proposed enhancement:**
```bash
objdiff-cli diff -p . "Symbol" -f json --include-instructions --context 3
```

Shows 3 instructions before and after each mismatch for better pattern recognition.

---

## 8. Export to Markdown/HTML Report

> **Status:** ✅ **PARTIALLY IMPLEMENTED** - Markdown added in Phase 3. HTML remains wishlist.

**Implemented (Phase 3):**
```bash
objdiff-cli diff -p . "Symbol" -f markdown -o diff-report.md --verdict --include-instructions
```

**Still wishlist:** HTML output with syntax highlighting:
```bash
objdiff-cli diff -p . "Symbol" -f html -o diff-report.html
```
