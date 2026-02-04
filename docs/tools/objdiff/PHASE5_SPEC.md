# objdiff CLI Phase 5: Analysis & Diagnosis - Implementation Specification

> **Status:** PR #1 COMPLETE, PR #2 next
> **Location:** `~/code/milohax/objdiff` (local fork)
> **Target:** Local fork only (clean up for upstream later if useful)
> **Estimated scope:** ~750 lines Rust
> **Progress:** ~160 lines implemented (5.1 + 5.2)

---

## Executive Summary

Phase 5 automates the diagnosis workflow that currently consumes 80% of decomp work time. Based on critical design review with multiple investigation threads, we've refined the approach.

**Key design decisions:**

1. **Symbol resolution:** Direct object scan with demangled matching in `diff` command (no report.json coupling)
2. **Thresholds:** Hardcoded initially, iterate based on real-world usage
3. **Watch mode (5.5):** Deferred to Phase 6; simple `--build` flag included instead
4. **Merged function patterns:** Comprehensive regex covering DC3/MSVC patterns
5. **Target:** Local fork only for now

---

## Implementation Order

| Sub-phase | Feature | Effort | Value | Risk |
|-----------|---------|--------|-------|------|
| **5.2** | `--summary` flag | ~80 lines | High | Low |
| **5.1** | Demangled symbol resolution in `diff` | ~80 lines | High | Low |
| **5.3a** | Pattern detection (3 patterns) | ~200 lines | High | Medium |
| **5.4** | `--verdict` flag | ~150 lines | High | Medium |
| **5.5** | `--build` flag (single-shot) | ~40 lines | Medium | Low |
| **5.3b** | Additional patterns (2 more) | ~100 lines | Medium | Low |
| **5.6** | `report analyze` batch | ~120 lines | High | Low |

**Deferred to Phase 6:** Watch mode (`--watch`) - requires file watching infrastructure

---

## 5.1: Symbol Resolution (Demangled Name Support)

### Design Decision

**Original proposal:** Add `--resolve` flag to `report function` with shell composition.

**Problem:** Verbose, requires subshell, awkward error handling.

**Adopted approach:** Extend `diff` command's existing object scan to match demangled names directly.

### How It Works

The `diff` command already iterates through all object files to find a symbol when no unit is specified (lines 225-251 in diff.rs). We extend this to also try demangled name matching.

### User Experience
```bash
# All of these work:
objdiff-cli diff -p . "?Volume@Box@@QBAMXZ"     # Mangled (existing)
objdiff-cli diff -p . "Box::Volume"             # Demangled (new)
objdiff-cli diff -p . "Volume"                  # Partial - prompts if ambiguous
```

### Implementation

**New function in `objdiff-core/src/obj/read.rs`:**
```rust
/// Match a symbol by exact name or demangled name
pub fn match_symbol_by_query(path: &Path, query: &str) -> Result<Vec<SymbolMatch>> {
    let data = unsafe { memmap2::Mmap::map(&File::open(path)?) }?;
    let file = object::File::parse(&*data)?;
    let mut matches = Vec::new();

    for symbol in file.symbols() {
        if symbol.kind() != object::SymbolKind::Text { continue; }
        let name = symbol.name()?;

        // Exact match on mangled name
        if name == query {
            matches.push(SymbolMatch { name: name.to_string(), exact: true });
            continue;
        }

        // Try demangling and matching
        if let Some(demangled) = Demangler::Auto.demangle(name) {
            if demangled == query {
                matches.push(SymbolMatch { name: name.to_string(), exact: true });
            } else if demangled.contains(query) {
                matches.push(SymbolMatch { name: name.to_string(), exact: false });
            }
        }
    }
    Ok(matches)
}

pub struct SymbolMatch {
    pub name: String,      // Mangled name
    pub exact: bool,       // Exact vs partial match
}
```

**Modify `objdiff-cli/src/cmd/diff.rs` (lines 225-251):**
```rust
// Replace exact name check with demangled matching
for (obj, unit_idx) in objects.iter() {
    if let Some(target_path) = &obj.target_path {
        let matches = match_symbol_by_query(target_path, symbol_name)?;

        if !matches.is_empty() {
            // Collect all matches for disambiguation
            all_matches.extend(matches.into_iter().map(|m| (m, unit_idx)));
        }
    }
}

// Handle results
match all_matches.len() {
    0 => bail!("Symbol not found: {}", symbol_name),
    1 => {
        // Single match - proceed
        let (matched, unit_idx) = &all_matches[0];
        resolved_symbol = matched.name.clone();
    }
    _ => {
        // Multiple matches - show disambiguation
        eprintln!("Multiple matches for '{}'. Did you mean:", symbol_name);
        for (m, unit_idx) in &all_matches {
            let unit_name = &objects[*unit_idx].0.name;
            eprintln!("  {} ({})", m.name, unit_name);
        }
        bail!("Ambiguous symbol. Use --unit or provide more specific name.");
    }
}
```

### Performance

| Scenario | Time |
|----------|------|
| Cold (scan 2200 objects) | ~500ms |
| Warm (OS cached) | ~150ms |

Acceptable for interactive use. No index maintenance required.

---

## 5.2: Summary Statistics (`--summary`)

### Purpose
Eliminate manual jq pipelines for match type counting.

### Command
```bash
objdiff-cli diff -p . "Symbol" -f json --include-instructions --summary
```

### Output Schema
```json
{
  "symbol": "?Volume@Box@@QBAMXZ",
  "demangled": "Box::Volume(void) const",
  "unit": "default/system/math/Geo",
  "target_size": 48,
  "base_size": 48,
  "fuzzy_match_percent": 98.83,
  "diff_score": { "matched": 118, "total": 120 },

  "instruction_summary": {
    "total": 12,
    "equal": 7,
    "diff_arg": 5,
    "diff_op": 0,
    "replace": 0,
    "delete": 0,
    "insert": 0,
    "equal_percent": 58.33,
    "mismatch_percent": 41.67
  },

  "instructions": [...]
}
```

### Implementation

**Add to `objdiff-cli/src/cmd/diff.rs`:**
```rust
#[derive(serde::Serialize, Default)]
pub struct InstructionSummary {
    pub total: usize,
    pub equal: usize,
    pub diff_arg: usize,
    pub diff_op: usize,
    pub replace: usize,
    pub delete: usize,
    pub insert: usize,
    pub equal_percent: f32,
    pub mismatch_percent: f32,
}

impl InstructionSummary {
    pub fn from_instructions(instructions: &[InstructionDiffOutput]) -> Self {
        let mut s = Self::default();
        for instr in instructions {
            s.total += 1;
            match instr.match_type.as_str() {
                "equal" => s.equal += 1,
                "diff_arg" => s.diff_arg += 1,
                "diff_op" => s.diff_op += 1,
                "replace" => s.replace += 1,
                "delete" => s.delete += 1,
                "insert" => s.insert += 1,
                _ => {}
            }
        }
        let total = s.total.max(1) as f32;
        s.equal_percent = (s.equal as f32 / total) * 100.0;
        s.mismatch_percent = 100.0 - s.equal_percent;
        s
    }
}
```

**CLI flag:**
```rust
#[argp(switch)]
/// Include instruction match type summary
summary: bool,
```

---

## 5.3: Pattern Detection (`--analyze`)

### Purpose
Auto-detect known patterns that explain mismatches and indicate fixability.

### Merged Function Detection Patterns

Based on research of DC3 codebase (2,759 merged functions identified):

**Combined detection regex:**
```regex
^(merged_|OnlyReturns|\?\?_[EG].*PAXI@Z$)
```

**Pattern breakdown:**

| Pattern | Regex | Examples | Count in DC3 |
|---------|-------|----------|--------------|
| Named merged | `^merged_[A-Za-z]` | `merged_Read4FloatStruct`, `merged_SetObjConcrete` | 43 unique |
| Address-based merged | `^merged_[0-9a-fA-F]{6,}$` | `merged_82331360` | 2,721 |
| Trivial returns | `^(OnlyReturns\|merged_Returns\d*)$` | `OnlyReturns` | 2 |
| MSVC scalar dtor | `^\?\?_G.*PAXI@Z$` | `??_GCharLipSync@@QAAPAXI@Z` | 332 |
| MSVC vector dtor | `^\?\?_E.*PAXI@Z$` | `??_ERndMat@@$4PPPPPPPM@...` | 412 |

### Patterns to Implement

#### Phase 5.3a (Initial - 3 patterns)

| Pattern | Detection Rule | Confidence | Fixability |
|---------|---------------|------------|------------|
| **LINKER_MERGED** | `diff_arg` + `bl` opcode + target matches merged regex | High | Unfixable |
| **BOOL_MASK** | `delete`/`insert` + `clrlwi`/`rlwinm` opcode + bool bit pattern | High | Usually unfixable |
| **REGISTER_SWAP** | ≥3 `diff_arg` with consistent register mapping | Medium | Sometimes fixable (30%) |

#### Phase 5.3b (Additional - 2 patterns)

| Pattern | Detection Rule | Confidence | Fixability |
|---------|---------------|------------|------------|
| **COMPARISON_STYLE** | `diff_arg` on `cmpwi`/`cmplwi` + adjacent branch differs + values differ by 1 | Medium | Sometimes fixable (50%) |
| **CONTROL_FLOW** | `diff_op`/`replace` on branch instructions | Medium | Often fixable (70%) |

### Command
```bash
objdiff-cli diff -p . "Symbol" -f json --include-instructions --analyze
```

### Output Schema
```json
{
  "symbol": "...",
  "fuzzy_match_percent": 97.0,
  "instruction_summary": { },

  "analysis": {
    "patterns": [
      {
        "pattern": "LINKER_MERGED",
        "confidence": "high",
        "instruction_count": 12,
        "mismatch_ratio": 0.857,
        "fixability": "unfixable",
        "details": {
          "merged_functions": [
            { "name": "merged_Read4FloatStruct", "count": 5 },
            { "name": "merged_Read3FloatStruct", "count": 4 },
            { "name": "OnlyReturns", "count": 3 }
          ]
        }
      }
    ],
    "patterns_checked": ["LINKER_MERGED", "BOOL_MASK", "REGISTER_SWAP"],
    "unattributed_mismatches": 0
  },

  "instructions": [...]
}
```

### Detection Algorithm: LINKER_MERGED

```rust
use regex::Regex;
use lazy_static::lazy_static;

lazy_static! {
    // Comprehensive merged function detection for DC3/MSVC
    static ref MERGED_RE: Regex = Regex::new(
        r"^(merged_|OnlyReturns|\?\?_[EG].*PAXI@Z$)"
    ).unwrap();
}

fn detect_linker_merged(instructions: &[InstructionDiffOutput]) -> Option<Pattern> {
    let mut merged_calls: HashMap<String, usize> = HashMap::new();

    for instr in instructions {
        if instr.match_type != "diff_arg" { continue; }
        let Some(target) = &instr.target else { continue; };
        if target.opcode != "bl" { continue; }
        let Some(args) = &target.args else { continue; };

        // Extract function name from call target
        let func_name = extract_call_target(args);

        if MERGED_RE.is_match(&func_name) {
            *merged_calls.entry(func_name).or_insert(0) += 1;
        }
    }

    if merged_calls.is_empty() { return None; }

    let total_count: usize = merged_calls.values().sum();
    Some(Pattern {
        pattern: PatternType::LinkerMerged,
        confidence: Confidence::High,
        instruction_count: total_count,
        fixability: Fixability::Unfixable,
        details: PatternDetails::MergedFunctions(merged_calls),
    })
}
```

### Detection Algorithm: BOOL_MASK

```rust
fn detect_bool_mask(instructions: &[InstructionDiffOutput]) -> Option<Pattern> {
    let mut mask_count = 0;

    for instr in instructions {
        if !matches!(instr.match_type.as_str(), "delete" | "insert") { continue; }

        // Check both sides for bool masking instructions
        for side in [&instr.target, &instr.base].into_iter().flatten() {
            match side.opcode.as_str() {
                "clrlwi" => {
                    // clrlwi rD, rS, 24 (mask to u8) or 31 (mask to bool)
                    if let Some(args) = &side.args {
                        if args.contains(", 24") || args.contains(", 31") {
                            mask_count += 1;
                        }
                    }
                }
                "rlwinm" => {
                    // rlwinm rD, rS, 0, 24, 31 (equivalent to clrlwi)
                    if let Some(args) = &side.args {
                        if args.contains("0, 24, 31") || args.contains("0, 31, 31") {
                            mask_count += 1;
                        }
                    }
                }
                _ => {}
            }
        }
    }

    if mask_count == 0 { return None; }

    Some(Pattern {
        pattern: PatternType::BoolMask,
        confidence: Confidence::High,
        instruction_count: mask_count,
        fixability: Fixability::UsuallyUnfixable,
        details: PatternDetails::BoolMask { bit_positions: vec![24, 31] },
    })
}
```

### Detection Algorithm: REGISTER_SWAP

```rust
fn detect_register_swap(instructions: &[InstructionDiffOutput]) -> Option<Pattern> {
    let reg_re = Regex::new(r"r(\d+)").unwrap();
    let mut mappings: HashMap<(String, String), usize> = HashMap::new();

    for instr in instructions {
        if instr.match_type != "diff_arg" { continue; }
        let (Some(target), Some(base)) = (&instr.target, &instr.base) else { continue; };

        // Skip if opcodes differ (not a pure register swap)
        if target.opcode != base.opcode { continue; }

        let target_regs: Vec<_> = reg_re.find_iter(target.args.as_deref().unwrap_or(""))
            .map(|m| m.as_str().to_string()).collect();
        let base_regs: Vec<_> = reg_re.find_iter(base.args.as_deref().unwrap_or(""))
            .map(|m| m.as_str().to_string()).collect();

        for (t, b) in target_regs.iter().zip(base_regs.iter()) {
            if t != b {
                // Normalize key ordering for consistent counting
                let key = if t < b { (t.clone(), b.clone()) } else { (b.clone(), t.clone()) };
                *mappings.entry(key).or_insert(0) += 1;
            }
        }
    }

    // Filter to swaps with >= 3 occurrences (threshold)
    let significant: Vec<_> = mappings.into_iter()
        .filter(|(_, count)| *count >= 3)
        .collect();

    if significant.is_empty() { return None; }

    let total: usize = significant.iter().map(|(_, c)| c).sum();
    let confidence = if significant.len() == 1 && total >= 5 {
        Confidence::High
    } else {
        Confidence::Medium
    };

    Some(Pattern {
        pattern: PatternType::RegisterSwap,
        confidence,
        instruction_count: total,
        fixability: Fixability::MaybeFixable,
        details: PatternDetails::RegisterSwap(significant),
    })
}
```

---

## 5.4: Fixability Verdict (`--verdict`)

### Purpose
Automated fixability classification based on detected patterns.

### Command
```bash
objdiff-cli diff -p . "Symbol" -f json --verdict
# Note: --verdict implies --analyze implies --summary
```

### Verdict Categories

| Verdict | Criteria | Action |
|---------|----------|--------|
| `COMPLETE` | 100% match | None needed |
| `LIKELY_FIXABLE` | Has `diff_op`/`replace` AND merged_ratio < 0.5 AND no BOOL_MASK | Investigate control flow |
| `MAYBE_FIXABLE` | Only REGISTER_SWAP OR only COMPARISON_STYLE | Try variable reordering |
| `AT_LIMIT` | merged_ratio ≥ 0.8 OR BOOL_MASK detected | Accept current %, move on |
| `NEEDS_INVESTIGATION` | Mixed signals, unclear pattern | Manual analysis required |

### Threshold Constants

```rust
const MERGED_RATIO_LIKELY_FIXABLE: f32 = 0.5;   // < 50% merged = might be fixable
const MERGED_RATIO_AT_LIMIT: f32 = 0.8;         // >= 80% merged = at limit
const MIN_REGISTER_SWAP_OCCURRENCES: usize = 3;
const MIN_MISMATCH_FOR_ANALYSIS: usize = 2;     // Don't analyze if only 1 mismatch
```

### Output Schema
```json
{
  "verdict": {
    "classification": "AT_LIMIT",
    "confidence": "high",
    "explanation": "85.7% of mismatched instructions are calls to linker-merged functions.",
    "factors": [
      { "name": "merged_call_ratio", "value": 0.857, "threshold": 0.8, "result": "exceeds_limit" },
      { "name": "diff_op_count", "value": 0, "threshold": 1, "result": "below_threshold" },
      { "name": "bool_mask_detected", "value": false, "result": "not_detected" }
    ],
    "recommendation": "Accept current match (97.0%). Effort better spent elsewhere.",
    "suggestions": []
  }
}
```

### Decision Algorithm

```rust
fn compute_verdict(summary: &InstructionSummary, analysis: &Analysis) -> Verdict {
    let total_mismatches = summary.total - summary.equal;

    if total_mismatches == 0 {
        return Verdict::complete();
    }

    if total_mismatches < MIN_MISMATCH_FOR_ANALYSIS {
        return Verdict::likely_fixable(vec![
            Suggestion::new("Only 1-2 mismatches - inspect manually")
        ]);
    }

    // BOOL_MASK is a hard blocker
    if analysis.has_pattern(PatternType::BoolMask) {
        return Verdict::at_limit(
            "Bool mask pattern detected - compiler optimization cannot be matched"
        );
    }

    // Calculate merged ratio
    let merged_count = analysis.pattern_instruction_count(PatternType::LinkerMerged);
    let merged_ratio = merged_count as f32 / total_mismatches as f32;

    if merged_ratio >= MERGED_RATIO_AT_LIMIT {
        return Verdict::at_limit(format!(
            "{:.1}% of mismatches are linker-merged function calls",
            merged_ratio * 100.0
        ));
    }

    // Check for fixable patterns
    let has_control_flow = summary.diff_op > 0 || summary.replace > 0;

    if has_control_flow && merged_ratio < MERGED_RATIO_LIKELY_FIXABLE {
        let mut suggestions = vec![];
        if analysis.has_pattern(PatternType::ControlFlow) {
            suggestions.push(Suggestion::check_control_flow());
        }
        if analysis.has_pattern(PatternType::ComparisonStyle) {
            suggestions.push(Suggestion::try_comparison_style());
        }
        return Verdict::likely_fixable(suggestions);
    }

    // Maybe-fixable patterns
    if analysis.has_pattern(PatternType::RegisterSwap) && merged_ratio < 0.3 {
        return Verdict::maybe_fixable("Try reordering variable declarations");
    }

    if analysis.has_pattern(PatternType::ComparisonStyle) {
        return Verdict::maybe_fixable("Try equivalent comparison operators (>= vs >)");
    }

    Verdict::needs_investigation("Mixed patterns - manual analysis recommended")
}
```

---

## 5.5: Single-Shot Build (`--build`)

### Purpose
Integrate build step into diff workflow without full watch mode complexity.

### Command
```bash
objdiff-cli diff -p . "Symbol" --build -f json --verdict
# Runs: ninja <unit>.o && ninja build/.../report.json, then diff
```

### Implementation

```rust
#[argp(switch)]
/// Rebuild object and report before diffing
build: bool,

// In run():
if args.build {
    // Get unit path from resolved symbol
    let unit = &objects[unit_idx].0;
    let obj_target = unit.target_path.as_ref()
        .ok_or_else(|| anyhow!("No target path for unit"))?;

    // Build object
    let status = Command::new("ninja")
        .arg(obj_target.as_str())
        .status()
        .context("Failed to run ninja for object")?;

    if !status.success() {
        bail!("Build failed for {}", obj_target);
    }

    // Build report (find report.json path from project config)
    if let Some(report_path) = find_report_path(&project_config) {
        Command::new("ninja")
            .arg(report_path.as_str())
            .status()
            .context("Failed to run ninja for report")?;
    }
}
```

---

## 5.6: Batch Analysis (`report analyze`)

### Purpose
Triage multiple near-match functions at once, categorized by fixability.

### Command
```bash
objdiff-cli report analyze build/.../report.json \
    --min-percent 90 --max-percent 99 \
    --limit 50
```

### Output Schema
```json
{
  "query": {
    "min_percent": 90.0,
    "max_percent": 99.0,
    "limit": 50
  },
  "summary": {
    "total_analyzed": 50,
    "by_verdict": {
      "LIKELY_FIXABLE": 8,
      "MAYBE_FIXABLE": 12,
      "AT_LIMIT": 25,
      "NEEDS_INVESTIGATION": 5
    }
  },
  "results": {
    "LIKELY_FIXABLE": [
      {
        "name": "?MatShaderFlagsOK@RndShader@@QAA...",
        "demangled": "RndShader::MatShaderFlagsOK",
        "unit": "default/system/rndobj/Shader",
        "fuzzy_match_percent": 95.3,
        "size": 528,
        "primary_pattern": "CONTROL_FLOW",
        "suggestion": "Check branch conditions"
      }
    ],
    "MAYBE_FIXABLE": [...],
    "AT_LIMIT": [...],
    "NEEDS_INVESTIGATION": [...]
  }
}
```

---

## Flag Combinations & Behavior

| Flags | Summary | Analysis | Verdict | Instructions |
|-------|---------|----------|---------|--------------|
| (none) | No | No | No | No |
| `--summary` | Yes | No | No | No |
| `--analyze` | Yes | Yes | No | Yes (implied) |
| `--verdict` | Yes | Yes | Yes | Yes (implied) |
| `--include-instructions` | No | No | No | Yes |

**Rule:** `--verdict` implies `--analyze` implies `--summary`. Analysis requires instructions.

---

## Data Structures

### New File: `objdiff-cli/src/analysis.rs`

```rust
pub enum PatternType {
    LinkerMerged,
    BoolMask,
    RegisterSwap,
    ComparisonStyle,
    ControlFlow,
}

pub enum Confidence { High, Medium, Low }

pub enum Fixability {
    Unfixable,
    UsuallyUnfixable,
    MaybeFixable,
    LikelyFixable,
}

pub struct Pattern {
    pub pattern: PatternType,
    pub confidence: Confidence,
    pub instruction_count: usize,
    pub mismatch_ratio: f32,
    pub fixability: Fixability,
    pub details: PatternDetails,
}

pub enum PatternDetails {
    MergedFunctions(HashMap<String, usize>),
    BoolMask { bit_positions: Vec<u8> },
    RegisterSwap(Vec<((String, String), usize)>),
    ComparisonStyle { instructions: Vec<usize> },
    ControlFlow { branch_instructions: Vec<usize> },
}

pub struct Analysis {
    pub patterns: Vec<Pattern>,
    pub patterns_checked: Vec<PatternType>,
    pub unattributed_mismatches: usize,
}

pub enum VerdictClassification {
    Complete,
    LikelyFixable,
    MaybeFixable,
    AtLimit,
    NeedsInvestigation,
}

pub struct Verdict {
    pub classification: VerdictClassification,
    pub confidence: Confidence,
    pub explanation: String,
    pub factors: Vec<VerdictFactor>,
    pub recommendation: String,
    pub suggestions: Vec<Suggestion>,
}
```

---

## Files to Create/Modify

| File | Action | Content |
|------|--------|---------|
| `objdiff-core/src/obj/read.rs` | Modify | Add `match_symbol_by_query()` |
| `objdiff-cli/src/analysis.rs` | Create | Pattern detection, verdict computation |
| `objdiff-cli/src/cmd/diff.rs` | Modify | Demangled matching, --summary, --analyze, --verdict, --build |
| `objdiff-cli/src/cmd/report.rs` | Modify | Add `analyze` subcommand |
| `objdiff-cli/src/main.rs` | Modify | Register analysis module |

---

## Testing Strategy

### Unit Tests
1. Pattern detection with synthetic instruction sequences
2. Verdict computation with boundary threshold cases
3. Summary calculation accuracy

### Integration Tests
1. Full diff + analyze pipeline on DC3 test fixtures
2. Batch analysis with sample report
3. Demangled symbol resolution across multiple units

### Validation Against DC3
| Function | Expected Verdict | Pattern |
|----------|-----------------|---------|
| `RndMat::LoadOld` | AT_LIMIT | LINKER_MERGED (12 calls) |
| `RndMat::GetRefractEnabled` | AT_LIMIT | BOOL_MASK |
| `MatShaderFlagsOK` (before fix) | LIKELY_FIXABLE | CONTROL_FLOW |
| `RndGroup::Load` | MAYBE_FIXABLE | REGISTER_SWAP |

---

## Success Criteria

1. **5.1 (Symbol):** `diff -p . "Box::Volume"` resolves and diffs correctly
2. **5.2 (Summary):** `--summary` matches manual jq counts
3. **5.3 (Analyze):** Detects LINKER_MERGED, BOOL_MASK, REGISTER_SWAP in known functions
4. **5.4 (Verdict):** Classifications match manual triage for validation set
5. **5.5 (Build):** `--build` runs ninja and updates diff in one command
6. **5.6 (Batch):** Triages 50 functions in <30 seconds

---

## Implementation Checklist

- [x] **PR #1:** 5.2 (--summary) + 5.1 (demangled resolution) ✅ **COMPLETE** (2026-01-23)
  - Added `--summary` flag with `InstructionSummary` struct
  - Added `match_symbol_by_query()` in read.rs for demangled lookup
  - Added `symbol_by_name_or_demangled()` in Object for contains matching
  - Supports partial demangled names (e.g., "Box::Volume")
  - Shows disambiguation when multiple matches found
- [x] **PR #2:** 5.3a (3 patterns) + 5.4 (--verdict) ✅ **COMPLETE** (2026-01-23)
  - Created `analysis.rs` with pattern detection and verdict logic
  - Added `--analyze` flag for pattern detection (LINKER_MERGED, BOOL_MASK, REGISTER_SWAP)
  - Added `--verdict` flag for automated fixability classification
  - Flag chain: --verdict implies --analyze implies --summary
  - Validated against DC3 functions (LoadOld, GetRefractEnabled, MatShaderFlagsOK, RndGroup::Load)
- [x] Validate against DC3 known functions ✅ **COMPLETE** (2026-01-23)
- [x] Update CLAUDE.md with new commands ✅ **COMPLETE** (2026-01-23)
- [x] **PR #3:** 5.5 (--build) + 5.3b (2 more patterns) + 5.6 (batch) ✅ **COMPLETE** (2026-01-23)
  - Added `--build` flag to run ninja before diffing
  - Added COMPARISON_STYLE pattern (cmpwi/cmplwi with ±1 difference)
  - Added CONTROL_FLOW pattern (diff_op/replace on branch instructions)
  - Added `report analyze` batch triage command
  - Updated OBJDIFF_CLI_USAGE.md with all new features
