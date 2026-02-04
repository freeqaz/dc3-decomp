# objdiff CLI Implementation Guide

> Part of the [objdiff CLI Design](./OBJDIFF_CLI_DESIGN.md) documentation.

This document covers implementation details: phases, code patterns, data structures, and testing.

---

## Implementation Phases

### Phase 1: Core Query Infrastructure (MVP) ✓ COMPLETE

**Status:** Implemented 2026-01-22

**Files modified:**
1. `objdiff-cli/src/cmd/report.rs` - Added `query`, `summary`, `function` subcommands
2. `objdiff-cli/Cargo.toml` - Added `regex = "1.12"` and `globset = "0.4"` dependencies

**Actual scope:** ~500 lines of Rust

**What was implemented:**
- `report summary` - Aggregate stats output (JSON, text formats)
- `report query` - Full filtering: `--min-percent`, `--max-percent`, `--min-size`, `--max-size`, `--unimplemented`, `--unit` (glob), `--function` (regex)
- `report query` - Sorting: `--sort-by` (name, match_percent, size), `--sort-order` (asc, desc), `--limit`
- `report query` - Output modes: `--functions`, `--units`, `--summary`
- `report function` - Direct lookup with regex or `--exact` match

**Validated against DC3:** 46,958 functions, 2,223 units

### Phase 2: Enhanced Diff Output ✓ COMPLETE

**Status:** Implemented (verified 2026-01-23)

**What was implemented:**
- `diff -f json` / `diff -f json-pretty` - JSON output format
- `--include-instructions` - Full instruction-level diff in output
- Match types: `equal`, `diff_op`, `diff_arg`, `replace`, `delete`, `insert`
- diff_score exposure in JSON output

**Usage:**
```bash
# Get JSON diff with instructions
objdiff-cli diff -p . "Symbol" -f json --include-instructions

# Get just mismatched instructions
objdiff-cli diff -p . "Symbol" -f json --include-instructions | \
  jq '[.instructions[] | select(.match_type != "equal")]'
```

See [OBJDIFF_LEARNINGS.md](OBJDIFF_LEARNINGS.md) for diagnosis patterns using this output.

### Phase 3: Additional Output Formats ✓ COMPLETE

**Status:** Implemented 2026-01-23

**Actual scope:** ~200 lines of Rust

**What was implemented:**
- `diff -f markdown` - Human-readable markdown reports with tables, pattern details, verdicts
- `report query -f csv` - CSV export for spreadsheets/scripts
- `report function -f csv` - CSV export for function lookups
- `report analyze -f csv` - CSV export for batch analysis results

**Usage:**
```bash
# Markdown diff report (ideal for agents)
objdiff-cli diff -p . "MyFunc" -f markdown --verdict --include-instructions

# CSV exports
objdiff-cli report query report.json --functions --min-percent 90 -f csv
objdiff-cli report analyze report.json --min-percent 95 --limit 50 -f csv
```

### Phase 4: Streaming & Advanced ✓ COMPLETE

**Status:** Implemented 2026-01-23

**What was implemented:**
- `report trending` command for comparing multiple reports over time
- Supports multiple report files in chronological order
- `--by-mtime` flag to order by file modification time
- `--category` filter for specific progress categories
- Output formats: `json`, `json-pretty`, `text`
- Shows deltas between consecutive reports
- Summary with trend classification (improving/declining/stable)

**Actual scope:** ~200 lines of Rust (added to `objdiff-cli/src/cmd/report.rs`)

**Note:** stdin support was already implemented in Phase 1 via `read_report("-")`

### Phase 5: Analysis & Diagnosis ✓ COMPLETE

**Status:** Implemented 2026-01-23 (PRs #1-3)

**Motivation:** Based on real-world usage (2026-01-23 session), we spent ~80% of time on manual interpretation of diff output. The tool provides raw data; users must manually:
1. Count match types with jq
2. Detect patterns (merged functions, bool masks, register swaps)
3. Apply fixability decision tree
4. Track before/after percentages

**Goal:** Automate the diagnosis workflow so the tool provides actionable insights, not just raw data.

**Estimated scope:** ~600 lines of Rust

#### 5.1: Symbol Auto-Resolution

**Problem:** `diff` requires mangled symbol names, but users think in demangled names.

**Current painful workflow:**
```bash
# Step 1: Get mangled name from report
objdiff-cli report function report.json "RndMat::GetRefractEnabled"
# Output includes: "name": "?GetRefractEnabled@RndMat@@QAA_N_N@Z"

# Step 2: Copy-paste mangled name into diff
objdiff-cli diff -p . "?GetRefractEnabled@RndMat@@QAA_N_N@Z" -f json
```

**Proposed fix:** Auto-resolve in diff command
```bash
# Just works - resolves via report.json
objdiff-cli diff -p . "RndMat::GetRefractEnabled" -f json
```

**Implementation:** When symbol not found directly, search report.json for demangled match, use mangled name.

#### 5.2: Diff Summary Statistics

**Problem:** Every diagnosis requires counting match types with jq.

**Current:**
```bash
objdiff-cli diff -p . "Symbol" -f json --include-instructions | \
  jq '.instructions | group_by(.match_type) | map({type: .[0].match_type, count: length})'
```

**Proposed:** Built-in summary
```bash
objdiff-cli diff -p . "Symbol" -f json --include-instructions --summary
```

**Output:**
```json
{
  "symbol": "...",
  "fuzzy_match_percent": 97.0,
  "instruction_summary": {
    "total": 616,
    "equal": 590,
    "diff_arg": 18,
    "diff_op": 2,
    "replace": 4,
    "delete": 2,
    "insert": 0
  },
  "instructions": [...]
}
```

#### 5.3: Pattern Detection

**Problem:** Users manually identify patterns like linker-merged functions, bool masks, register swaps.

**Proposed:** Auto-detect known patterns
```bash
objdiff-cli diff -p . "Symbol" --analyze
```

**Output:**
```
Symbol: RndMat::LoadOld (97.0%, 2464 bytes)

Detected Patterns:
  ⚠ LINKER_MERGED: 12 calls to merged functions
    - merged_Read4FloatStruct (5 calls)
    - merged_Read3FloatStruct (4 calls)
    - OnlyReturns (3 calls)

  ⚠ REGISTER_SWAP: r30 ↔ r31 (8 occurrences)
    Likely cause: Variable declaration order

  ✓ No bool mask issues detected
  ✓ No control flow mismatches detected

Match Type Breakdown:
  equal: 590 (95.8%)
  diff_arg: 24 (3.9%)  ← mostly linker-merged
  diff_op: 2 (0.3%)

Fixability: AT LIMIT
  Reason: All non-equal diffs are linker-merged function calls or register allocation.
  Recommendation: Accept current match percentage.
```

**Pattern rules (configurable):**
```rust
enum Pattern {
    LinkerMerged,      // diff_arg + bl + "merged_*|OnlyReturns"
    BoolMask,          // delete + clrlwi + ", 24"
    RegisterSwap,      // consistent r30↔r31 or similar throughout
    ComparisonStyle,   // cmpwi with adjacent value + opposite branch
    ControlFlow,       // diff_op on branch instructions
}
```

#### 5.4: Fixability Verdict

**Problem:** Users manually apply decision tree from OBJDIFF_LEARNINGS.md.

**Proposed:** Automated verdict
```bash
objdiff-cli diff -p . "Symbol" --verdict
```

**Output:**
```
VERDICT: LIKELY_FIXABLE

Reasoning:
- Found 2 diff_op instructions (fixable)
- Found 4 replace instructions (fixable)
- No linker-merged patterns detected
- 18 diff_arg are register allocation (maybe fixable)

Suggestions:
1. Check control flow at instructions 45-48 (branch mismatch)
2. Try reordering variable declarations (register swap detected)
3. Check comparison style at instruction 102 (>= vs >)
```

**Verdict categories:**
- `COMPLETE` - 100% match
- `LIKELY_FIXABLE` - Has diff_op/replace, no blockers
- `MAYBE_FIXABLE` - Only register allocation issues
- `AT_LIMIT` - All diffs are linker-merged or unfixable patterns
- `NEEDS_INVESTIGATION` - Mixed signals, unclear

#### 5.5: Watch Mode (Build Integration)

**Problem:** Users repeatedly run build + report + check loop.

**Current painful workflow:**
```bash
# Edit code...
ninja build/.../Mat.obj
ninja build/.../report.json
objdiff-cli report function report.json "RndMat::GetRefractEnabled" | jq '.matches[0].fuzzy_match_percent'
# 97.1% - no change, try again...
```

**Proposed:** Watch mode
```bash
objdiff-cli diff -p . "RndMat::GetRefractEnabled" --watch
```

**Behavior:**
1. Show current diff/analysis
2. Watch source file for changes
3. On change: rebuild object, regenerate report, show new diff
4. Highlight before/after: `97.1% → 98.2% (+1.1%)`

**Simpler alternative:** Single-shot with build
```bash
objdiff-cli diff -p . "Symbol" --build
# Runs: ninja <unit>.obj && ninja report.json && diff
```

#### 5.6: Batch Analysis

**Problem:** Finding which near-match functions are worth working on requires manual triage.

**Proposed:**
```bash
objdiff-cli report analyze report.json --min-percent 90 --max-percent 99 --limit 20
```

**Output:**
```
Near-Match Analysis (90-99%, top 20 by fixability):

LIKELY_FIXABLE (8 functions):
  RndShader::MatShaderFlagsOK  95.3%  528b   Control flow mismatch
  Box::Volume                  98.8%   48b   Single diff_op
  String::operator==           99.1%   56b   Comparison style
  ...

MAYBE_FIXABLE (5 functions):
  RndGroup::Load               98.4%  788b   Register allocation only
  ...

AT_LIMIT (7 functions):
  RndMat::LoadOld              97.0% 2464b   Linker-merged (12 calls)
  RndMat::GetRefractEnabled    97.1%  152b   Bool mask pattern
  ...
```

This is the "batch diagnosis" from WISHLIST but with actual implementation detail.

### Phase 6: Advanced Features (Partial)

**Status:** 6.3 (Instruction Context Window) COMPLETE. 6.1 and 6.2 still planned.

**Estimated scope:** ~400 lines of Rust (remaining)

**Files modified:**
- `objdiff-cli/src/cmd/diff.rs` - Added `-C`/`--context` flag, `--full-listing` flag, pattern doc links, match guidance, analysis summary, verdict factors table, markdown as default format

**Files to modify/create:**
- `objdiff-cli/src/cmd/report.rs` - Add `merged-functions` subcommand (planned)
- `objdiff-cli/src/cmd/history.rs` (new) - History tracking (planned)

#### 6.1: Merged Function Catalog

**Problem:** Understanding which merged functions affect the codebase and their overall impact.

**Command:**
```bash
objdiff-cli report merged-functions build/373307D9/report.json
```

**Output:**
```
Merged Function Analysis:

  merged_Read4FloatStruct    47 calls across 12 units
  merged_Read3FloatStruct    31 calls across 8 units
  OnlyReturns                23 calls across 15 units
  ??_G*PAXI@Z (dtors)        18 calls across 6 units

Total: 119 merged function calls
Impact: ~2.3% of instructions in near-match functions (90-99%)

Top affected units:
  system/rndobj/Mat.cpp      14 calls (merged_Read4FloatStruct: 8, merged_Read3FloatStruct: 6)
  system/rndobj/Trans.cpp    11 calls (merged_Read3FloatStruct: 7, OnlyReturns: 4)
  ...
```

**Implementation:**
1. Scan all units in report with functions in 90-99% range
2. Run pattern detection on each, collect LINKER_MERGED details
3. Aggregate counts by merged function name
4. Group by unit for "top affected" section

#### 6.2: Historical Tracking

**Problem:** Hard to know if code changes helped without manual before/after comparison.

**Commands:**
```bash
# Record current state after making changes
objdiff-cli diff -p . "MyFunc" --track
# Stores match % and timestamp in .objdiff-history/

# View history for a function
objdiff-cli history "MyFunc"
```

**History output:**
```
MyFunc match history:

  2026-01-23 14:30  97.0%
  2026-01-23 14:35  97.1%  (+0.1%)
  2026-01-23 14:40  94.5%  (-2.6%)  ← regression
  2026-01-23 14:45  97.0%  (+2.5%)  reverted
  2026-01-23 15:10  98.2%  (+1.2%)  ← current

Best: 98.2% (current)
```

**Storage format:** `.objdiff-history/<symbol_hash>.json`
```json
{
  "symbol": "MyFunc",
  "demangled": "MyFunc()",
  "entries": [
    {"timestamp": "2026-01-23T14:30:00Z", "match_percent": 97.0},
    {"timestamp": "2026-01-23T14:35:00Z", "match_percent": 97.1}
  ]
}
```

**Implementation:**
1. `--track` flag on diff command appends entry to history file
2. New `history` subcommand reads and formats history
3. Optional: `--track-note "fixed loop"` to annotate entries

#### 6.3: Instruction Context Window ✓ COMPLETE

**Status:** Implemented 2026-02-04

**Problem:** Seeing mismatched instructions without surrounding context makes diagnosis harder.

**Actual commands:**
```bash
# Show 3 instructions before/after each mismatch (like grep -C)
objdiff-cli diff -p . "MyFunc" --verdict -C 3

# Full listing of all instructions
objdiff-cli diff -p . "MyFunc" --verdict --full-listing
```

**Markdown output:** Context is shown inline with mismatches **bolded** for visibility. Non-contiguous sections are separated with `...`.

**Additional features implemented:**
- Match percentage guidance hints based on match %
- Pattern documentation links (📖 emoji) for each detected pattern
- Analysis summary showing patterns checked and unattributed mismatches
- Verdict factors table showing decision breakdown
- Markdown is now the default output format

---

## Code Patterns to Follow

Based on analysis of existing objdiff-cli code.

### Argument Parsing

```rust
#[derive(FromArgs, PartialEq, Debug)]
#[argp(subcommand, name = "query")]
pub struct QueryArgs {
    #[argp(positional, from_str_fn(platform_path))]
    /// Report file to query
    report: Utf8PlatformPathBuf,

    #[argp(option, short = 'o', from_str_fn(platform_path))]
    /// Output file
    output: Option<Utf8PlatformPathBuf>,

    #[argp(option, short = 'f', default = "OutputFormat::Json")]
    /// Output format
    format: OutputFormat,

    #[argp(option)]
    /// Filter functions by regex
    function: Option<String>,

    #[argp(option)]
    /// Minimum match percentage
    min_percent: Option<f32>,
}
```

### Error Handling

```rust
use anyhow::{bail, Context, Result};

pub fn run(args: QueryArgs) -> Result<()> {
    let report = load_report(&args.report)
        .with_context(|| format!("Failed to load report: {}", args.report))?;

    if args.min_percent.is_some() && args.max_percent.is_some() {
        if args.min_percent.unwrap() > args.max_percent.unwrap() {
            bail!("--min-percent cannot be greater than --max-percent");
        }
    }

    // ...
}
```

### Output Writing

```rust
use crate::util::output::{write_output, OutputFormat};

// Use existing write_output utility
write_output(&query_result, args.output.as_deref(), args.format)?;
```

---

## Data Structures

### QueryResult (new)

```rust
#[derive(serde::Serialize, prost::Message)]
pub struct QueryResult {
    #[prost(message, optional)]
    pub query_info: Option<QueryInfo>,
    #[prost(message, optional)]
    pub summary: Option<QuerySummary>,
    #[prost(message, repeated)]
    pub results: Vec<QueryItem>,
}

#[derive(serde::Serialize, prost::Message)]
pub struct QueryInfo {
    #[prost(string, optional)]
    pub unit_filter: Option<String>,
    #[prost(string, optional)]
    pub function_filter: Option<String>,
    #[prost(float, optional)]
    pub min_percent: Option<f32>,
    #[prost(float, optional)]
    pub max_percent: Option<f32>,
    #[prost(string, optional)]
    pub sort_by: Option<String>,
    #[prost(uint32, optional)]
    pub limit: Option<u32>,
}

#[derive(serde::Serialize, prost::Message)]
pub struct QuerySummary {
    #[prost(uint32)]
    pub matched_count: u32,
    #[prost(uint32)]
    pub filtered_count: u32,
}

#[derive(serde::Serialize, prost::Message)]
pub struct QueryItem {
    #[prost(string)]
    pub unit: String,
    #[prost(string)]
    pub name: String,
    #[prost(string, optional)]
    pub demangled_name: Option<String>,
    #[prost(uint64)]
    pub size: u64,
    #[prost(float)]
    pub fuzzy_match_percent: f32,
    #[prost(uint64, optional)]
    pub address: Option<u64>,
}
```

### DiffOutput (new)

```rust
#[derive(serde::Serialize)]
pub struct DiffOutput {
    pub symbol: String,
    pub demangled: Option<String>,
    pub unit: Option<String>,
    pub target_size: u64,
    pub base_size: u64,
    pub fuzzy_match_percent: f32,
    pub diff_score: Option<DiffScore>,
    pub build_status: Option<BuildStatusOutput>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub instructions: Option<Vec<InstructionDiff>>,
}

#[derive(serde::Serialize)]
pub struct DiffScore {
    pub matched: u64,
    pub total: u64,
}

#[derive(serde::Serialize)]
pub struct InstructionDiff {
    pub index: usize,
    pub target: Option<InstructionInfo>,
    pub base: Option<InstructionInfo>,
    pub match_type: String,  // "equal", "diff_op", "diff_arg", "insert", "delete"
}
```

---

## Agent Integration Examples

### Find Work Targets

```bash
#!/bin/bash
# find_targets.sh - Find functions that are close to matching

objdiff report query build/373307D9/report.json \
  --functions \
  --min-percent 90 --max-percent 99 \
  --sort-by size --sort-order asc \
  --limit 10 \
  | jq -r '.results[] | "\(.demangled_name // .name) (\(.fuzzy_match_percent)%)"'
```

### Check Specific Function Status

```bash
#!/bin/bash
# check_function.sh - Check if a function is done

PERCENT=$(objdiff report function build/373307D9/report.json "$1" \
  | jq -r '.matches[0].fuzzy_match_percent // 0')

if [ "$PERCENT" = "100" ]; then
  echo "DONE"
else
  echo "IN_PROGRESS: $PERCENT%"
fi
```

### Get Detailed Diff for Near-Match

```bash
#!/bin/bash
# diagnose.sh - Get detailed diff for a 99% function

objdiff diff -p . -u "$1" "$2" \
  --output-format json \
  --include-instructions \
  | jq '.instructions[] | select(.match_type != "equal")'
```

### Track Progress Over Time

```bash
#!/bin/bash
# track_progress.sh - Run after each session

DATE=$(date +%Y%m%d)
ninja build/373307D9/report.json
cp build/373307D9/report.json "reports/report_${DATE}.json"

# Show trending
objdiff report trending reports/report_*.json --format json \
  | jq '.overall | map({report, percent: .fuzzy_match_percent})'
```

---

## Testing Strategy

1. **Unit tests** for query filtering logic
2. **Integration tests** with sample report files
3. **Golden tests** comparing JSON output against expected
4. **Manual testing** with DC3 decomp project reports
