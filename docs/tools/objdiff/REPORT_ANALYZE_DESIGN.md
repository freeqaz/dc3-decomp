# Design Document: `report analyze` Command (Phase 5.6)

## Overview

The `report analyze` command performs batch analysis on functions from a progress report, running the full diff+analysis+verdict pipeline on each function and grouping results by fixability classification.

### Command Syntax
```bash
objdiff-cli report analyze build/373307D9/report.json \
    --min-percent 90 --max-percent 99 \
    --limit 50 \
    -f json-pretty
```

---

## Data Flow

```
report.json
    |
    v
[1. Filter functions by percent range]
    |
    v
[2. For each function: find unit, load objects]
    |
    v
[3. Run diff + analysis + verdict pipeline]
    |
    v
[4. Group by verdict classification]
    |
    v
JSON Output (grouped results)
```

---

## Key Challenges

### Challenge 1: Report -> Unit -> Object Path Resolution

**Problem:** The report contains function names and unit names, but not direct paths to object files. We need to resolve from report unit name to object file paths.

**Solution:** The report unit name matches the `ProjectObject.name()` value in `objdiff.json`. We can:
1. Load project config using `try_project_config()`
2. Build `ObjectConfig` for each unit (same as `report generate` does)
3. Match report unit name to find the corresponding `ObjectConfig`
4. Extract `target_path` and `base_path` from `ObjectConfig`

**Existing code to reuse:**
- `ObjectConfig::new()` in `diff.rs` (lines 793-822) - builds paths from project config
- `try_project_config()` in `config/mod.rs` - loads project config
- `read_report()` in `report.rs` (lines 581-591) - loads and parses report

**Code reference from `report.rs` generate function (lines 221-235):**
```rust
let objects = project_units
    .iter()
    .enumerate()
    .map(|(idx, o)| {
        (
            ObjectConfig::new(
                o,
                project_dir,
                target_obj_dir.as_deref(),
                base_obj_dir.as_deref(),
            ),
            idx,
        )
    })
    .collect::<Vec<_>>();
```

This exact pattern can be reused to build a lookup map from unit name to object paths.

### Challenge 2: Running Diff Pipeline Programmatically

**Problem:** The diff+analysis+verdict pipeline is currently embedded in `run_json()` in `diff.rs`. We need to call it for each function without the CLI argument parsing overhead.

**Solution:** Extract the core diff logic from `run_json()` into reusable functions. Looking at `run_json()` (lines 415-578), the key operations are:

1. **Build diff config** (lines 429-430):
   ```rust
   let (diff_config, mapping_config) =
       build_config_from_args(&args, project_config.as_ref(), unit_options.as_ref())?;
   ```

2. **Read objects** (lines 433-448):
   ```rust
   let target_obj = target_path.as_ref().map(|p| {
       obj::read::read(p.as_ref(), &diff_config, DiffSide::Target)
   }).transpose()?;
   let base_obj = base_path.as_ref().map(|p| {
       obj::read::read(p.as_ref(), &diff_config, DiffSide::Base)
   }).transpose()?;
   ```

3. **Perform diff** (lines 451-458):
   ```rust
   let diff_result = diff_objs(
       target_obj.as_ref(),
       base_obj.as_ref(),
       None,
       &diff_config,
       &mapping_config,
   )?;
   ```

4. **Build instruction diffs** (lines 510-522):
   ```rust
   let instructions = build_instruction_diffs(
       target_obj.as_ref(),
       base_obj.as_ref(),
       left_diff,
       right_diff,
       target_symbol_idx,
       base_symbol_idx,
       &diff_config,
   )?;
   ```

5. **Run analysis and verdict** (lines 532-552):
   ```rust
   let instruction_summary = InstructionSummary::from_instructions(&instructions);
   let analysis = super::analysis::analyze_instructions(&instructions);
   let verdict = super::analysis::compute_verdict(&summary, &analysis, match_percent);
   ```

**Refactoring approach:** Create a new function that takes object paths and symbol name, returns analysis results:

```rust
pub struct AnalysisResult {
    pub symbol: String,
    pub demangled: Option<String>,
    pub unit: String,
    pub fuzzy_match_percent: Option<f32>,
    pub size: u64,
    pub instruction_summary: InstructionSummary,
    pub analysis: Analysis,
    pub verdict: Verdict,
}

pub fn analyze_function(
    target_path: Option<&Utf8PlatformPath>,
    base_path: Option<&Utf8PlatformPath>,
    symbol_name: &str,
    diff_config: &DiffObjConfig,
    mapping_config: &MappingConfig,
) -> Result<AnalysisResult> {
    // Read objects
    // Perform diff
    // Find symbol
    // Build instructions
    // Compute analysis + verdict
    // Return result
}
```

### Challenge 3: Efficient Object Loading

**Problem:** Loading objects for 50+ functions could be slow if we reload the same object files repeatedly.

**Analysis:** Looking at the report structure:
- Each unit has multiple functions
- If we process function-by-function, we'd reload the same objects repeatedly
- If we process unit-by-unit, we load each object pair once

**Solution:** Process functions grouped by unit:
1. Group report functions by unit name
2. For each unit, load objects once
3. Analyze all functions in that unit
4. Move to next unit

This means each object pair is loaded at most once, significantly reducing I/O.

### Challenge 4: Symbol Name Resolution

**Problem:** Report contains function names (mangled). Need to find the symbol index in loaded objects.

**Solution:** Use existing `symbol_by_name_or_demangled()` method on Object (from PR #1):
```rust
// In Object implementation
pub fn symbol_by_name_or_demangled(&self, name: &str) -> Option<usize> {
    // Tries exact match first, then demangled match
}
```

This is already used in `run_json()` at line 462-463.

---

## Code Reuse Summary

### Can be reused directly:
| Function/Type | Location | Purpose |
|---------------|----------|---------|
| `read_report()` | `report.rs:581-591` | Load and parse report |
| `ObjectConfig::new()` | `diff.rs:793-822` | Build object paths from project config |
| `build_config_from_args()` | `diff.rs:399-413` | Build diff config (needs simplification) |
| `obj::read::read()` | `obj/read.rs:983-997` | Read object file |
| `diff_objs()` | `diff/mod.rs` | Perform diff |
| `build_instruction_diffs()` | `diff.rs:580-651` | Build instruction diff output |
| `InstructionSummary::from_instructions()` | `diff.rs:147-166` | Summary statistics |
| `analyze_instructions()` | `analysis.rs:421-453` | Pattern detection |
| `compute_verdict()` | `analysis.rs:460-652` | Verdict computation |

### Needs refactoring/extraction:
| Code | Current Location | Change Needed |
|------|------------------|---------------|
| Core diff logic | `run_json()` lines 433-552 | Extract to standalone function |
| Diff config building | `build_config_from_args()` | Need version without Args struct |

---

## Proposed Implementation

### New Types (in `report.rs`)

```rust
#[derive(FromArgs, PartialEq, Debug)]
#[argp(subcommand, name = "analyze")]
pub struct AnalyzeArgs {
    #[argp(positional, from_str_fn(platform_path))]
    /// Report file
    report: Utf8PlatformPathBuf,

    #[argp(option, short = 'p', from_str_fn(platform_path))]
    /// Project directory (defaults to current directory)
    project: Option<Utf8PlatformPathBuf>,

    #[argp(option)]
    /// Minimum match percentage (0-100)
    min_percent: Option<f32>,

    #[argp(option)]
    /// Maximum match percentage (0-100)
    max_percent: Option<f32>,

    #[argp(option)]
    /// Maximum number of functions to analyze
    limit: Option<usize>,

    #[argp(option, short = 'o', from_str_fn(platform_path))]
    /// Output file
    output: Option<Utf8PlatformPathBuf>,

    #[argp(option, short = 'f')]
    /// Output format (json, json-pretty)
    format: Option<String>,

    #[argp(option, short = 'c')]
    /// Configuration property (key=value)
    config: Vec<String>,
}

#[derive(Serialize)]
struct AnalyzeOutput {
    query: AnalyzeQuery,
    summary: AnalyzeSummary,
    results: AnalyzeResults,
}

#[derive(Serialize)]
struct AnalyzeQuery {
    min_percent: Option<f32>,
    max_percent: Option<f32>,
    limit: Option<usize>,
}

#[derive(Serialize)]
struct AnalyzeSummary {
    total_analyzed: usize,
    by_verdict: HashMap<String, usize>,
}

#[derive(Serialize)]
struct AnalyzeResults {
    #[serde(rename = "LIKELY_FIXABLE")]
    likely_fixable: Vec<AnalyzedFunction>,
    #[serde(rename = "MAYBE_FIXABLE")]
    maybe_fixable: Vec<AnalyzedFunction>,
    #[serde(rename = "AT_LIMIT")]
    at_limit: Vec<AnalyzedFunction>,
    #[serde(rename = "NEEDS_INVESTIGATION")]
    needs_investigation: Vec<AnalyzedFunction>,
}

#[derive(Serialize)]
struct AnalyzedFunction {
    name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    demangled: Option<String>,
    unit: String,
    fuzzy_match_percent: f32,
    size: u64,
    primary_pattern: Option<String>,
    suggestion: Option<String>,
}
```

### Implementation Steps

#### Step 1: Add `Analyze` to `SubCommand` enum
```rust
pub enum SubCommand {
    Generate(GenerateArgs),
    Changes(ChangesArgs),
    Summary(SummaryArgs),
    Query(QueryArgs),
    Function(FunctionArgs),
    Analyze(AnalyzeArgs),  // NEW
}
```

#### Step 2: Create helper function to build diff config without Args
```rust
fn build_diff_config(
    project_config: Option<&ProjectConfig>,
    unit_options: Option<&ProjectOptions>,
    cli_config: &[String],
) -> Result<(DiffObjConfig, MappingConfig)> {
    let mut diff_config = DiffObjConfig::default();
    if let Some(options) = project_config.and_then(|c| c.options.as_ref()) {
        apply_project_options(&mut diff_config, options)?;
    }
    if let Some(options) = unit_options {
        apply_project_options(&mut diff_config, options)?;
    }
    apply_config_args(&mut diff_config, cli_config)?;
    Ok((diff_config, MappingConfig::default()))
}
```

#### Step 3: Create core analysis function (in `diff.rs` or new module)
```rust
pub fn analyze_symbol(
    target_obj: Option<&Object>,
    base_obj: Option<&Object>,
    diff_result: &DiffResult,  // Result from diff_objs
    symbol_name: &str,
    diff_config: &DiffObjConfig,
) -> Result<Option<AnalysisResult>> {
    // Find symbol in target or base
    let symbol_idx = target_obj
        .and_then(|o| o.symbol_by_name_or_demangled(symbol_name))
        .or_else(|| base_obj.and_then(|o| o.symbol_by_name_or_demangled(symbol_name)));

    let Some(symbol_idx) = symbol_idx else {
        return Ok(None);  // Symbol not found
    };

    // Get symbol info
    let obj = target_obj.or(base_obj).unwrap();
    let symbol = &obj.symbols[symbol_idx];

    // Get both symbol indices
    let target_symbol_idx = target_obj.and_then(|o| o.symbol_by_name_or_demangled(symbol_name));
    let base_symbol_idx = base_obj.and_then(|o| o.symbol_by_name_or_demangled(symbol_name));

    // Build instruction diffs
    let instructions = build_instruction_diffs(
        target_obj,
        base_obj,
        diff_result.left.as_ref(),
        diff_result.right.as_ref(),
        target_symbol_idx,
        base_symbol_idx,
        diff_config,
    )?;

    // Get match percent from diff
    let obj_diff = diff_result.left.as_ref().or(diff_result.right.as_ref()).unwrap();
    let symbol_diff = &obj_diff.symbols[symbol_idx];
    let match_percent = symbol_diff.match_percent;

    // Compute summary, analysis, verdict
    let instruction_summary = InstructionSummary::from_instructions(&instructions);
    let analysis = super::analysis::analyze_instructions(&instructions);
    let verdict = super::analysis::compute_verdict(&instruction_summary, &analysis, match_percent);

    Ok(Some(AnalysisResult {
        symbol: symbol.name.clone(),
        demangled: symbol.demangled_name.clone(),
        match_percent,
        size: symbol.size,
        instruction_summary,
        analysis,
        verdict,
    }))
}
```

#### Step 4: Main analyze function
```rust
fn analyze(args: AnalyzeArgs) -> Result<()> {
    let output_format = QueryOutputFormat::from_option(args.format.as_deref())?;

    // Load report
    let report = read_report(&args.report)?;

    // Load project config
    let project_dir = args.project.as_deref()
        .unwrap_or_else(|| Utf8PlatformPath::new("."));
    let (project_config, _) = objdiff_core::config::try_project_config(project_dir.as_ref())
        .ok_or_else(|| anyhow!("Project config not found"))?;
    let project_config = project_config?;

    // Build object configs (unit name -> ObjectConfig)
    let target_obj_dir = project_config.target_dir.as_ref()
        .map(|p| project_dir.join(p.with_platform_encoding()));
    let base_obj_dir = project_config.base_dir.as_ref()
        .map(|p| project_dir.join(p.with_platform_encoding()));
    let units = project_config.units.as_deref().unwrap_or_default();

    let object_configs: HashMap<&str, (ObjectConfig, usize)> = units
        .iter()
        .enumerate()
        .map(|(idx, o)| {
            let config = ObjectConfig::new(
                o,
                project_dir,
                target_obj_dir.as_deref(),
                base_obj_dir.as_deref(),
            );
            (config.name.as_str(), (config, idx))
        })
        .collect();

    // Filter functions from report
    let mut candidates: Vec<(&ReportUnit, &ReportItem)> = Vec::new();
    for unit in &report.units {
        for func in &unit.functions {
            // Apply percent filters
            if let Some(min) = args.min_percent {
                if func.fuzzy_match_percent < min { continue; }
            }
            if let Some(max) = args.max_percent {
                if func.fuzzy_match_percent > max { continue; }
            }
            // Skip 100% matches
            if func.fuzzy_match_percent >= 100.0 { continue; }

            candidates.push((unit, func));
        }
    }

    // Sort by match percent descending (analyze best candidates first)
    candidates.sort_by(|a, b| {
        b.1.fuzzy_match_percent
            .partial_cmp(&a.1.fuzzy_match_percent)
            .unwrap_or(std::cmp::Ordering::Equal)
    });

    // Apply limit
    if let Some(limit) = args.limit {
        candidates.truncate(limit);
    }

    // Group by unit for efficient loading
    let mut by_unit: HashMap<&str, Vec<&ReportItem>> = HashMap::new();
    for (unit, func) in &candidates {
        by_unit.entry(unit.name.as_str()).or_default().push(*func);
    }

    // Process each unit
    let mut results = AnalyzeResults {
        likely_fixable: Vec::new(),
        maybe_fixable: Vec::new(),
        at_limit: Vec::new(),
        needs_investigation: Vec::new(),
    };
    let mut verdict_counts: HashMap<String, usize> = HashMap::new();

    for (unit_name, functions) in by_unit {
        // Find object config
        let Some((object_config, unit_idx)) = object_configs.get(unit_name) else {
            warn!("Unit not found in project: {}", unit_name);
            continue;
        };

        // Build diff config with unit options
        let unit_options = units.get(*unit_idx).and_then(|u| u.options());
        let (diff_config, mapping_config) = build_diff_config(
            Some(&project_config),
            unit_options,
            &args.config,
        )?;

        // Load objects
        let target_obj = object_config.target_path.as_ref()
            .map(|p| obj::read::read(p.as_ref(), &diff_config, DiffSide::Target))
            .transpose()?;
        let base_obj = object_config.base_path.as_ref()
            .map(|p| obj::read::read(p.as_ref(), &diff_config, DiffSide::Base))
            .transpose()?;

        // Run diff once for the unit
        let diff_result = diff_objs(
            target_obj.as_ref(),
            base_obj.as_ref(),
            None,
            &diff_config,
            &mapping_config,
        )?;

        // Analyze each function
        for func in functions {
            let result = analyze_symbol(
                target_obj.as_ref(),
                base_obj.as_ref(),
                &diff_result,
                &func.name,
                &diff_config,
            )?;

            let Some(result) = result else { continue; };

            // Record verdict
            let classification = result.verdict.classification;
            *verdict_counts.entry(classification.to_string()).or_insert(0) += 1;

            // Build output item
            let analyzed = AnalyzedFunction {
                name: func.name.clone(),
                demangled: func.metadata.as_ref().and_then(|m| m.demangled_name.clone()),
                unit: unit_name.to_string(),
                fuzzy_match_percent: func.fuzzy_match_percent,
                size: func.size,
                primary_pattern: result.analysis.patterns.first().map(|p| p.pattern.as_str().to_string()),
                suggestion: result.verdict.suggestions.first().map(|s| s.action.clone()),
            };

            // Add to appropriate bucket
            match classification {
                VerdictClassification::LikelyFixable => results.likely_fixable.push(analyzed),
                VerdictClassification::MaybeFixable => results.maybe_fixable.push(analyzed),
                VerdictClassification::AtLimit => results.at_limit.push(analyzed),
                VerdictClassification::NeedsInvestigation => results.needs_investigation.push(analyzed),
                VerdictClassification::Complete => {} // Should not happen (filtered out)
            }
        }
    }

    // Build output
    let output = AnalyzeOutput {
        query: AnalyzeQuery {
            min_percent: args.min_percent,
            max_percent: args.max_percent,
            limit: args.limit,
        },
        summary: AnalyzeSummary {
            total_analyzed: candidates.len(),
            by_verdict: verdict_counts,
        },
        results,
    };

    // Write output
    write_analyze_output(&output, args.output.as_deref(), output_format)?;

    Ok(())
}
```

---

## Performance Considerations

### Object Loading Strategy
- Group functions by unit before processing
- Load each object pair once per unit
- Expected: ~200 unique units for DC3, ~500ms per unit cold, ~100ms warm
- 50 functions across 30 units = ~30 object loads = 3-15 seconds

### Parallelization (Optional Enhancement)
Could use rayon to process units in parallel:
```rust
by_unit.par_iter().map(|(unit_name, functions)| {
    // Load objects + analyze functions
}).collect()
```

However, the sequential approach is likely fast enough for initial implementation.

### Memory Usage
- One object pair in memory at a time (sequential processing)
- Report stays in memory throughout
- Peak: ~100MB for large objects + report

---

## Estimated Complexity

| Component | Lines | Effort |
|-----------|-------|--------|
| `AnalyzeArgs` struct + subcommand registration | ~40 | Low |
| Output types (`AnalyzeOutput`, etc.) | ~60 | Low |
| `build_diff_config()` helper | ~15 | Low |
| `analyze_symbol()` function | ~80 | Medium |
| Main `analyze()` function | ~120 | Medium |
| Output writing | ~30 | Low |
| **Total** | **~345** | Medium |

The spec estimated ~120 lines, but proper error handling and the unit-grouping optimization add complexity. Still reasonable for a single PR.

---

## Open Questions

### Q1: Should we cache loaded objects?
**Recommendation:** No, not initially. The unit-grouping strategy handles this well enough. Can add LRU cache later if needed.

### Q2: Parallel processing?
**Recommendation:** Sequential first. Profile if >30 seconds, then consider rayon.

### Q3: Include full analysis in output?
**Recommendation:** No. Only include primary pattern and first suggestion to keep output manageable. Users can run `diff --verdict` on specific functions for full details.

### Q4: What if unit not found in project?
**Recommendation:** Log warning, skip that unit's functions. This handles renamed/removed units gracefully.

### Q5: Should we make `analyze_symbol` public API?
**Recommendation:** Yes, expose in `diff.rs` for programmatic use. This enables future tooling (LSP, CI integration).

---

## Summary

The `report analyze` command is implementable with moderate refactoring:

1. **Reusable code:** Most core logic exists in `diff.rs` and `analysis.rs`
2. **Main extraction needed:** Core diff/analysis logic from `run_json()`
3. **New code:** Args struct, output types, unit-grouped processing loop
4. **Performance:** Unit-grouping ensures each object loaded once
5. **Estimated effort:** ~345 lines, single PR

The implementation follows established patterns in the codebase and reuses existing infrastructure for config loading, object reading, and analysis.
