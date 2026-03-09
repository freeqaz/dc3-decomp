# Session: Commit History Pattern Mining

**Date**: 2026-03-09
**Goal**: Build tooling to mine commit history for decomp patterns, validate existing permuter patterns, and discover new ones.

## What We Built

### `scripts/analysis/mine_patterns.py`

A commit-history pattern mining tool that:
1. Walks consecutive pairs of cached baseline reports (`build/373307D9/baselines/`)
2. Identifies functions that improved between baselines
3. Extracts `git diff` for each changed source file
4. Classifies each diff hunk against 37 pattern classifiers
5. Reports: frequency summary, co-occurrence, validation against known ROI data, unclassified diffs

**Usage:**
```bash
python3 scripts/analysis/mine_patterns.py --summary        # Pattern frequency
python3 scripts/analysis/mine_patterns.py --validate       # Compare vs permuter ROI
python3 scripts/analysis/mine_patterns.py --unclassified   # Show unknown patterns
python3 scripts/analysis/mine_patterns.py --all            # All output modes
python3 scripts/analysis/mine_patterns.py --build-baselines 20  # Build more baselines first
```

### Infrastructure Leveraged

- `measure_progress.sh` builds + caches `report.json` per commit at `build/373307D9/baselines/<hash>.json`
- `compare_progress.py` already has function-level diffing logic
- 11 cached baselines covering 2026-03-07 through 2026-03-08 (956 function improvements)

## Key Findings

### 1. Validation: Permuter Patterns vs Commit History

| Pattern | Permuter Win% | History Frequency | Insight |
|---------|:------------:|:-----------------:|---------|
| `variable_extraction` | 42% | 28.7% | Confirmed strong — human applies it ~29% of the time |
| `signed_unsigned` | 30% | 46.7% | Higher than permuter rate — humans find more opportunities |
| `declaration_reorder` | 20% | 11.3% | Confirmed |
| `comparison_flip` | 15% | 55.5% | **Much higher** — often co-applied with other fixes |
| `branch_polarity` | 5% | 44.5% | **Much higher** — same co-application effect |
| `fma_reorder` | 2% | 1.2% | Confirmed low |
| `empty_size_swap` | **0%** | 6.9% | **Validation gap** — works in practice, permuter can't find it |
| `ternary_swap` | **0%** | 32.4% | **Validation gap** — works in practice, permuter can't find it |
| `commutative_swap` | 0% | 0% | Confirmed dead |

### 2. Why `ternary_swap` Has 0% Automated Wins

The pattern is correctly implemented with 7 variant generators (if→ternary, ternary→if, polarity flip, bare return, etc.). But:
- **`relevant()` is too broad**: fires on ANY branch opcode mismatch or cluster, which is nearly every function
- **Budget competition**: with 63 patterns all competing, ternary_swap gets small budget on functions where ternary is the real issue
- **History shows 32.4% of improvements involve ternary changes** — the pattern itself is valuable, the selection mechanism is broken

**Fix**: Tighten `relevant()` to require specific ternary-related signals (e.g., clusters that look like single-branch assignment blocks), or boost priority when Ghidra decompilation shows ternary patterns.

### 3. Why `empty_size_swap` Has 0% Automated Wins

The pattern requires `divw`/`divwu` opcode mismatch signal, which is correct but overly specific. In commit history, the 66 instances were often co-applied with other fixes that were the primary driver — the `.empty()` → `.size()` change was a secondary fix that happened to be in the same commit.

**Fix**: Also fire on `cmplw` vs `subf`+`clrrwi` mismatch patterns (the actual codegen difference for pointer-comparison vs division-based size checks).

### 4. Novel Patterns Discovered

From 23 unclassified improvements after adding 13 new classifiers:

| Pattern | Example | Count | Automatable? |
|---------|---------|:-----:|:------------:|
| **Reference elimination** | `auto& ref = m[i]; ref.foo` → `m[i].foo` | 1 | YES — inverse of `member_ref_bind` |
| **const ref swap** | `Type copy = expr` → `const Type& copy = expr` | 1 | YES — simple AST |
| **memcpy for padded struct** | `Vector3 z = m.z` → `memcpy(&z, &m.z, sizeof(Padded))` | 1 | MAYBE |
| **Static init explicitness** | Uninitialized statics → `= nullptr/false/0` | 1 | YES — trivial |
| **INIT_REVS macro** | Manual `gRevs[]` → `INIT_REVS(N, M)` | 1 | YES — pattern match |
| **bool vs int param** | `int b` → `bool b` in signature | 1 | MAYBE — needs cross-ref |
| **Condition DeMorgan** | `a && !b → !(!a \|\| b)` rewrites | 1 | Partially covered |
| **Statement relocation** | Moving statement to different position | 2 | Already partial |
| **MILO_FAIL simplification** | Shorter format strings | 7 | MAYBE |
| **sqrt→sqrtf** | `std::sqrt` → `sqrtf` | 1 | YES — already have `math_func_promotion` |
| **find() operand order** | `end() != find(x)` → `find(x) != end()` | 1 | YES — commutative swap of comparison |

### 5. Patterns That Are Really Bug Fixes (Not Codegen Optimization)

| Pattern | Count | Nature |
|---------|:-----:|--------|
| `field_rename` | 211 | Wrong member name (unk→real name, or wrong field) |
| `default_value_fix` | 20 | `true` should be `false`, wrong init values |
| `struct_type_fix` | 27 | Using correct Win32 API types |
| `body_removal` | 96 | Remove dtor/extern affecting TU inlining budget |
| `header_include_change` | 618 | Include changes affecting inlining cascade |

These are high-frequency but not permuter-automatable — they require semantic understanding of the code.

### 6. Co-occurrence Inflation Problem

Pattern co-occurrence data is inflated because classification is per-file, not per-function. When a large commit touches one file with 20 improved functions and 5 pattern types, all 20 functions get all 5 patterns. The relative ordering is still informative, but the absolute numbers overcount.

**Future fix**: Use tree-sitter to identify which function body each hunk falls within, then attribute patterns only to that function.

## Action Items

### Phase 3 Updates (PERMUTER_ROI_ANALYSIS.md)

Update Phase 3 with data-driven priorities:

1. **Fix `ternary_swap` relevance** — tighten `relevant()`, currently fires too broadly (was #4, now #1 priority based on 32.4% history frequency)
2. **Add `reference_elimination`** — inverse of `member_ref_bind`, remove ref bindings
3. **Add `const_ref_swap`** — `Type copy = expr` ↔ `const Type& ref = expr`
4. **Add `static_init_explicit`** — add explicit `= nullptr/false/0` to statics
5. **Add `find_operand_order`** — swap `end() != find()` to `find() != end()`
6. `pragma_fp_contract` — (existing #1, still relevant)
7. `hoist_sret` — (existing #2, still relevant)
8. `alloca_intrinsic` — (existing #3, still relevant)

### Future Tooling

- **Baseline accumulation**: Run `--build-baselines 50` to get more data points
- **Per-function hunk attribution**: Use tree-sitter line ranges to scope patterns
- **Permuter integration**: Feed historical success rates into budget allocation
- **Unclassified alerts**: When a new baseline is cached, auto-classify and flag unrecognized diffs
