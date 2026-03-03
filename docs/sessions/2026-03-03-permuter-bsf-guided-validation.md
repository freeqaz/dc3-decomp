# Permuter BSF-Guided Validation (2026-03-03)

## Goal

Verify whether the C++ permuter's BSF-guided declaration reordering is actually working as intended, and define what to change if it is not.

## Context

The declaration reorder pattern claims to support BSF-guided mode:

- `scripts/permuter/patterns/declaration_reorder.py`
- `tools/compiler_trace/bsf_trace.py`
- `tools/compiler_trace/regmap_solver.py`

Intended behavior: use BSF trace + register swap diagnosis to generate targeted declaration reorders (smaller, smarter search than blind permutations).

## Important Environment Caveat

Inside the default sandbox, BSF tracing fails because GDB needs ptrace:

- `ptrace: Operation not permitted`
- `No BSF calls captured`

So BSF validation must run with escalated permissions (outside sandbox). Without escalation, the pattern silently falls back to unguided generation.

## Validation Method

We ran dry-run declaration reorder on real symbols with and without BSF guidance:

```bash
python3 -m scripts.permuter \
  --symbol '<SYMBOL>' \
  --patterns declaration_reorder \
  --max-variants 20 \
  --dry-run --no-apply --no-compose

# compare with:
python3 -m scripts.permuter ... --no-bsf-guided
```

## Results

### 1) `?NumItems@HamNavList@@ABAHXZ`

- Diagnosis: 1 GPR swap (`r30 <-> r31`)
- Declarations in function: 2
- BSF-on: `BSF tracing... 98 calls`, `BSF guidance: 1 candidates`, generated 2 variants:
  - `bsf_declreorder_0`
  - `declreorder_1`
- BSF-off: generated 1 variant:
  - `declreorder_0`

Observation: BSF path runs and emits guided-labeled variants.

### 2) `?GetDisabledCount@HamNavList@@ABAHH@Z`

- Diagnosis: 1 GPR swap (`r27 <-> r28`)
- Declarations: 2
- BSF-on: `BSF tracing... 98 calls`, `BSF guidance: 1 candidates`, generated 2 variants
- BSF-off: generated 1 variant

Observation: same behavior as above.

### 3) `?NextSkeletonIndexToTrack@SkeletonChooser@@AAAHH@Z`

- Diagnosis: 1 GPR swap (`r28 <-> r29`)
- Declarations: 5
- BSF-on: `BSF tracing... 98 calls`, `BSF guidance: 10 candidates for 1 swap pair(s)`
- 10 equals `C(5,2)` (all pairwise swaps)

Observation: this is not targeted narrowing; it is full pairwise enumeration.

### 4) `?Poll@CharMirror@@UAAXXZ`

- Diagnosis: 9 GPR swaps
- Declarations: 8
- BSF-on: `BSF tracing... 98 calls`, `BSF guidance: 28 candidates for 9 swap pair(s)`
- 28 equals `C(8,2)` (again all pairwise swaps)

Observation: "guided" candidate count matches full pairwise set.

### 5) `?LoadSong@Game@@QAAXXZ`

- BSF-on: `BSF tracing... 2 calls`, generated same variants as BSF-off
- No `BSF guidance: ...` line (no guided candidates)

Observation: BSF can succeed but produce no guided output.

## Code-Level Root Cause

### A) `guided_pairwise_search()` is mostly unguided today

In [`tools/compiler_trace/regmap_solver.py`](/home/free/code/milohax/dc3-decomp/tools/compiler_trace/regmap_solver.py:265):

- It computes `n_vars` from BSF colorings (`line ~281`)
- Then generates **all pairwise swaps** (`lines ~291-297`)
- For multi-pair, may add broad permutations (`lines ~299-310`)
- `swap_pairs` contents are not used to target specific variable pairs, only `len(swap_pairs)` is used as a coarse filter.

So the algorithm does not yet map register swap pairs to specific declaration indices.

### B) Trace failure path silently degrades to normal permutations

In [`scripts/permuter/patterns/declaration_reorder.py`](/home/free/code/milohax/dc3-decomp/scripts/permuter/patterns/declaration_reorder.py:129):

- BSF trace errors are caught
- BSF-guided generation returns early
- Unguided `declreorder_*` generation still runs afterward

Operationally, users can think BSF mode worked when it actually fell back.

## Conclusion

BSF integration is **partially working**:

- Yes: trace capture works (with ptrace access), BSF-mode code path executes.
- No: the core "guided" search does not materially use swap-pair semantics yet; in key cases it degenerates to all-pairs.

## Fix Plan

### Phase 1: Make behavior explicit and safe (small, immediate)

1. Add explicit status labels in permuter output:
   - `BSF mode: active (guided candidates=N)`
   - `BSF mode: fallback (trace failed: <reason>)`
   - `BSF mode: active but no guided candidates`
2. Add an optional strict flag:
   - `--bsf-required` (fail instead of fallback if tracing/guidance fails)
3. Add counters in JSON output:
   - `bsf_trace_calls`, `bsf_guided_candidates`, `bsf_fallback_used`

### Phase 2: Implement actually targeted candidate generation (core fix)

Replace current `guided_pairwise_search()` with a swap-aware solver:

1. Use BSF initial-color ordering to map declaration index <-> color.
2. Build/register a color-to-GPR mapping per function phase (at least for GPR class used by swap diagnosis).
3. For each diagnosis swap pair `(rA, rB)`, infer candidate declaration index pairs likely bound to those registers.
4. Generate only those swaps (plus near-neighbor alternatives), not all `C(n,2)`.
5. Rank candidates by swap coverage (how many diagnosed register pairs they can resolve).

If mapping confidence is low, keep a bounded fallback window (for example top-K nearby pairs) rather than full pairwise expansion.

### Phase 3: Testing and acceptance criteria

Add tests that assert true narrowing:

1. Unit test: for `n=8` declarations and 1 swap pair, guided candidates must be far less than 28.
2. Unit test: different `swap_pairs` should produce different candidate sets.
3. Integration dry-run test: BSF-on vs BSF-off candidate lists should differ in content and ordering on a known fixture.
4. Regression test: with `--bsf-required`, trace failure returns non-zero and no fallback variants are generated.

## Review Notes (2026-03-03)

Code review confirms both root causes:

- **Root Cause A verified**: `regmap_solver.py:291-297` generates all `C(n,2)` pairwise swaps. `swap_pairs` param is only used at line 300 for `len()` check — actual register identities are never mapped to declaration indices. The `colorings` from `extract_initial_colorings()` are only used for `n_vars = min(len(colorings), len(decl_names))`.

- **Root Cause B verified**: `declaration_reorder.py:135-137` catches trace failure and `return`s, skipping BSF-guided gen but letting unguided variants still run. No explicit status distinction in output.

- **Phase 1 adjustment**: Move `--bsf-required` to Phase 2 — until the solver is actually targeted, requiring BSF doesn't buy much (you'd just be requiring the non-narrowing version). Status labels are the immediate win.

- **Phase 2 approach**: `extract_initial_colorings()` returns `ColorAssignment` with `alloc_order` and `color` fields. The color maps to a GPR (low colors → volatile r11-down, high colors → callee-saved r29-up). For each swap pair `(rA, rB)`, we can infer which colors map to those GPRs and then find which declarations (by alloc_order) got those colors. This gives us targeted swap indices. When confidence is low, cap fallback at `2 * len(swap_pairs)` candidates.

## Suggested Implementation Order

1. Phase 1 first (quick visibility win, avoids false confidence)
2. Phase 2 solver rewrite + `--bsf-required` flag
3. Phase 3 tests

## Files To Change

- [`tools/compiler_trace/regmap_solver.py`](/home/free/code/milohax/dc3-decomp/tools/compiler_trace/regmap_solver.py) — core solver rewrite
- [`scripts/permuter/patterns/declaration_reorder.py`](/home/free/code/milohax/dc3-decomp/scripts/permuter/patterns/declaration_reorder.py) — status labels, BSF mode reporting
- [`scripts/permuter/__main__.py`](/home/free/code/milohax/dc3-decomp/scripts/permuter/__main__.py) — `--bsf-required` flag
- [`scripts/permuter/tests/test_patterns.py`](/home/free/code/milohax/dc3-decomp/scripts/permuter/tests/test_patterns.py) — new guided search tests

## Implementation Complete (2026-03-03)

All three phases from the fix plan have been implemented and tested:

### Phase 1: Explicit status labels
- BSF mode output now reports: `active (guided candidates=N)`, `fallback (trace failed: <reason>)`, `active but no guided candidates`
- `--bsf-required` CLI flag added (fails instead of falling back)
- `--no-bsf-guided` CLI flag added (disables BSF-guided mode entirely)

### Phase 2: Targeted candidate generation
- `guided_pairwise_search()` in `regmap_solver.py` rewritten with swap-aware solver
- Color-to-GPR mapping (empirically validated by `test_bsf_engine.py`):
  - Volatile: colors 0-6 → r11-r5 (formula: `reg = 11 - color`)
  - Callee-saved: colors 7-25 → r31-r13 (formula: `reg = 38 - color`)
  - **Key fix**: color 7 maps to r31 (callee-saved), NOT r4 (volatile) as originally coded
- For each diagnosed swap pair, maps registers → colors → declaration indices
- Generates targeted swaps + +-1 neighbor search for near-miss coverage
- Multi-swap: applies all targeted swaps simultaneously when multiple pairs exist
- Bounded fallback (capped at `2 * len(swap_pairs)`) for unmapped register pairs

### Phase 3: Tests
- 65 tests pass in `test_patterns.py`
- 27 tests pass in `tools/compiler_trace/tests/test_bsf_engine.py` (integration tests)
- Tests verify true narrowing: n=8 declarations with 1 swap pair generates far fewer than C(8,2)=28 candidates
- Different swap pairs produce different candidate sets
- `bsf_required` mode correctly prevents fallback

## BSF Engine Validation Findings (2026-03-03)

Integration test suite (`test_bsf_engine.py`) empirically validated the BSF engine:

### What works
- BSF traces ARE deterministic (same source → identical trace)
- BSF traces ARE sensitive to declaration order changes (all 6 sensitivity tests pass)
- BSF traces ARE per-TU (multi-function TUs show different traces from single-function TUs)
- Declaration order directly controls register assignment: first declared → r31, second → r30, etc.

### Color-to-register mapping (corrected)
Discovered via compilation of 5-variable synthetic function + ASM/BSF correlation:

| Variable | Register | BSF Color | Formula |
|----------|----------|-----------|---------|
| a (1st)  | r31      | 7         | 38 - 7  |
| b (2nd)  | r30      | 8         | 38 - 8  |
| c (3rd)  | r29      | 9         | 38 - 9  |
| d (4th)  | r28      | 10        | 38 - 10 |
| e (5th)  | r27      | 11        | 38 - 11 |

The old mapping (colors 8-10 → r29-r31) was wrong. The boundary between volatile and callee-saved is at color 7, not color 8.

### Remaining issues
- Real project TUs have BSF noise from other functions — need per-function BSF call isolation
- `detect_register_swaps()` in `asm_diff.py` doesn't detect all swap patterns (e.g., HamNavList::NumItems shows 2 changed lines but 0 detected register swaps)
- Multi-way reordering (beyond pairwise swaps) not yet supported
