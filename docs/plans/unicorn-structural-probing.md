# Unicorn Runner: Structural Probing Plan

Beyond yes/no equivalence testing, the unicorn runner can extract **structural information** about functions by varying inputs and observing how behavior changes. This turns it from a validation tool into a decomp reconnaissance tool.

## Motivation

The current tool runs each function once with zeroed state and reports EQUIVALENT or DIVERGENT. But the emulation infrastructure already captures rich observable state (call logs with args, memory mutations, return values) and supports reusable engines for fast re-execution. By running functions **multiple times with different inputs**, we can build behavioral profiles that help with decomp work *before* and *during* implementation, not just after.

## What We Can Learn

| Signal | How | Helps With |
|--------|-----|------------|
| Which struct fields are read | Sentinel-pattern object memory, observe which values appear in call args or affect branching | Stubbing unknown classes, understanding function purpose |
| Which calls are load-bearing | Vary trampoline return values, observe downstream behavior changes | Knowing which mock behaviors matter |
| Which args matter | Vary r4-r6 independently, observe output changes | Understanding function signatures |
| Where divergence lives | Multi-input probing, find which input dimensions cause decomp/orig split | Narrowing down where the code bug is |
| Build-env vs real divergence | Pattern-match `__FILE__` string refs and merged symbol calls in divergence details | Filtering noise from actionable items in diagnose output |

## Phases

### DONE: Phase 0: Build-Environment Noise Filter (Feb 13, 2026)

**Goal**: Auto-classify known-unfixable divergences in `diagnose` output.

**What**: Detect two patterns in DIVERGENT results:
1. **`__FILE__` string divergences**: The decomp `__FILE__` expands to just the filename (`Foo.cpp`) while the original has a full path (`src\system\obj\Foo.cpp`). These show up as call arg mismatches where r3/r4 point to GLOBAL_BASE with different string content.
2. **Merged symbol divergences**: The decomp calls `merged_<addr>` while the original calls the real symbol. These show up as `check_call_targets` warnings already captured in `comparator.py:check_call_targets()`.

**Implementation**: Added `classify_divergence()` in `comparator.py` that post-processes a DIVERGENT `ComparisonResult`:

```python
def classify_divergence(result, decomp_result, orig_result,
                        decomp_relocs, orig_relocs):
    """Returns: 'build_env', 'regalloc', or 'logic'."""
```

**Classification rules**:
- **`build_env`**: All differing call args point to GLOBAL_BASE region (string refs), or `merged_` in call target warnings, or call count mismatch with merged warning, or return value in globals region, or only globals memory diffs
- **`regalloc`**: Same call count, <=2 calls have arg differences, differing values are small non-pointer integers (not in any mapped region)
- **`logic`**: Everything else — error mismatches, FPR mismatches, call count diffs without merged symbols, pointer-valued arg diffs

**Pipeline refactoring**: Extracted `_run_comparison_core()` from `run_comparison_inner()` to return a `ComparisonBundle` dataclass with raw `ComparisonResult`, `ExecutionResult`s, and relocs. This lets `diagnose_single()` access raw data for classification without parsing formatted text.

**Output**: `diagnose --batch` now shows `FIX(build_env)`, `FIX(regalloc)`, or `FIX` (logic). Batch summary includes breakdown: `5 FIX [4 logic, 1 build_env]` and an "Unfixable divergences" line.

**Real results** (DirLoader, 55 functions):
```
FIX(build_env)    90.4%  DirLoader::FixClassName   (known __FILE__ diff)
FIX               99.8%  MakeString<Symbol, ...>   (real logic diff)
Summary: 13 DONE, 37 SKIP [36 high, 1 sensitive, 0 basic], 5 FIX [4 logic, 1 build_env]
Unfixable divergences: 1 (1 build_env, 0 regalloc)
```

**Tests**: 12 unit tests covering all classification categories (build_env globals args, merged symbols, call count merged, return globals, globals-only memory, regalloc small values, logic errors/FPR/call count).

**Touches**: `comparator.py`, `run.py` (new `_run_comparison_core`, `ComparisonBundle`), `diagnose.py`

### DONE: Phase 1: Multi-Input Probing (Feb 13, 2026)

**Goal**: Run each function N times with varied inputs to build a divergence profile.

**What**: Generalize `--dual-fixture` (2 runs: zero + 0xCD) to N runs with varied fill patterns. Track which runs agree vs diverge.

**Implementation**: New `prober.py` module with `probe_function()`:

```python
@dataclass
class ProbeResult:
    total_runs: int = 0
    equiv_runs: int = 0
    divergent_runs: int = 0
    error_runs: int = 0
    stable_equiv: bool = False       # all runs equivalent
    stable_divergent: bool = False   # all runs divergent
    input_sensitive: bool = False    # mixed results across inputs
    divergence_classes: dict         # class -> count (uses Phase 0 classification)
    per_run: list                    # RunDetail per run
```

**Fill pattern strategy**: Run 0 = zero fill (baseline), Run 1 = 0xCD (MSVC debug fill), Run 2+ = random byte patterns from seeded RNG. Each run goes through the full `_run_comparison_core` pipeline with classification.

**Confidence labels**: `high` (all equiv), `stable_divergent` (all div), `input_sensitive` (mixed), `none` (no runs).

**CLI**: Two entry points:

```bash
# Single function probe
python3 -m scripts.unicorn_runner.probe --unit DirLoader --symbol "?FixClassName@@YA?AVSymbol@@V1@@Z" --runs 16

# Batch probe (all eligible functions in a unit)
python3 -m scripts.unicorn_runner.probe --unit DirLoader --batch --runs 8
```

**Real results** (DirLoader, 55 functions, 4 runs each):
```
Summary: 48 stable equiv, 2 stable div, 5 input-sensitive, 0 skipped (55 tested)
Divergence classes: build_env: 1, logic: 6
```

Compared to dual-fixture (2 runs): multi-input probing with 4+ runs found 5 input-sensitive functions vs dual-fixture's 1 `fixture_sensitive`. The additional runs provide finer-grained confidence.

**Batch output format**:
```
EQUIV                   4/4 equiv  FunctionA
DIV(build_env)          0/4 equiv  FunctionB
SENSITIVE               3/4 equiv  FunctionC
```

**Tests**: 12 unit tests with mocked `_run_comparison_core` — covers all-equiv, all-div, mixed, skipped, per-run details, format output.

**Future integration**: `diagnose` could optionally use probe mode instead of dual-fixture for higher confidence, showing `SKIP(8/8)` instead of `SKIP(high)`.

**Touches**: New `prober.py`, new `probe.py` (CLI), no changes to `engine.py` (fill pattern variation is sufficient without register variation for v1).

### Phase 2: Struct Field Access Probing

**Goal**: Discover which object memory offsets a function reads, and how they affect behavior.

**What**: Fill object memory with sentinel patterns where each 4-byte word has a unique value encoding its offset. Run the function and observe which sentinel values appear in:
- Call arguments (r3-r6 in call log)
- Return value (r3)
- Memory writes (written-back to object or globals region)

**Implementation** (in `prober.py`):

```python
def probe_field_access(symbol, decomp_coff, orig_coff):
    """Run with sentinel object memory, return field access map."""
    # Fill object memory: word at offset N = OBJECT_BASE + N
    # (so the value equals the address, making it traceable)
    sentinel = bytearray(REGION_SIZE)
    for i in range(0, REGION_SIZE, 4):
        struct.pack_into(">I", sentinel, i, OBJECT_BASE + i)

    # Execute and scan outputs for sentinel values
    # Any r3-r6 value in [OBJECT_BASE, OBJECT_BASE+REGION_SIZE)
    # was loaded from object memory at that offset
```

**Output**: A field access report per function:

```
Field Access: Profile::Save
  READ  offset 0x004 (via call #0 arg r3)   -- vtable ptr
  READ  offset 0x030 (via call #2 arg r4)   -- passed to FixedSizeSaveable::Save
  READ  offset 0x0A0 (via branch decision)  -- controls if-block at +0x2C
  WRITE offset 0x100 (value changed)        -- output field
```

**How branch-sensitivity is detected**: Run twice — once with sentinel pattern, once with sentinel + flip one word. If call sequence changes, that word controls a branch. Binary search narrows to specific offsets.

**Touches**: New logic in `prober.py`, new sentinel memory setup path in `engine.py` (add `object_memory` parameter to `execute()`).
**Effort**: Medium. Sentinel fill is trivial; the interesting part is scanning call logs and memory diffs for sentinel values. Branch-sensitivity detection via binary search is more involved but optional for v1.

### Phase 3: Mock Return Variation

**Goal**: Discover which external call return values affect function behavior.

**What**: Instead of all trampolines returning 0, vary individual trampoline return values and observe behavioral changes.

**Implementation**:
- Modify `TRAMPOLINE_STUB` generation to support per-trampoline return values: `li r3, <value>; blr`
- For each external call target, run the function 3 times: mock returns 0, 1, 0xFFFFFFFF
- Track which return values cause behavior changes

**Key insight**: This requires modifying `engine.py` to write different stub bytes per trampoline address. Currently `TRAMPOLINE_STUB` is a constant. Change to:

```python
def make_trampoline_stub(return_value):
    """Generate: li r3, <val>; blr (or lis+ori for values > 16-bit)."""
    if return_value <= 0x7FFF:
        li = struct.pack(">I", 0x38600000 | (return_value & 0xFFFF))
        return li + b'\x4E\x80\x00\x20'
    # For larger values: lis r3, hi; ori r3, r3, lo; blr (12 bytes)
```

This means trampolines need 12 bytes instead of 8 for large return values. Trampoline region has 64KB — plenty of room.

**Output**:
```
Call Dependencies: Profile::Save
  FixedSizeSaveable::GetSize  -- LOAD-BEARING (behavior changes with return value)
  Symbol::Find                -- FIRE-AND-FORGET (behavior unchanged)
  DataArray::FindStr          -- LOAD-BEARING (controls branch at +0x48)
```

**Touches**: `engine.py` (variable stub generation), `patcher.py` (12-byte stub support), new `prober.py` logic.
**Effort**: Medium-high. Variable-size trampolines require adjusting address assignment in `patcher.py:assign_addresses()`. The probing loop itself is straightforward.

### Phase 4: Integrated Structural Report (`recon` command)

**Goal**: Single command that runs all probes and produces a structural summary for a function before you start decomp work.

**CLI**: `python3 -m scripts.unicorn_runner.recon --unit Foo --symbol Bar`

**Output combines**:
- objdiff match % and verdict
- Unicorn equivalence (multi-input, with confidence)
- Divergence classification (build-env / regalloc / logic)
- Field access map (which struct offsets are read/written)
- Call dependency map (load-bearing vs fire-and-forget)
- Recommended action: SKIP / FIX(type) / needs investigation

**Touches**: New `recon.py` module that orchestrates Phase 0-3 outputs.
**Effort**: Small (once Phases 0-3 exist). This is orchestration and formatting.

## Priority Order

| Phase | Value | Effort | Status |
|-------|-------|--------|--------|
| **Phase 0** | High — immediately cuts noise in diagnose | Small | **DONE** (Feb 13, 2026) |
| **Phase 1** | High — better confidence than dual-fixture | Medium | **DONE** (Feb 13, 2026) |
| **Phase 2** | Medium — useful for unknown structs | Medium | Planned (reuses probing infrastructure) |
| **Phase 3** | Medium — useful for understanding call graphs | Medium-high | Planned |
| **Phase 4** | High — but only after 0-2 exist | Small | Planned (needs Phase 2) |

**Next up**: Phase 2 (struct field access probing) — the infrastructure from Phase 0-1 is in place. Phase 3 can wait — it's the most invasive change (variable-size trampolines) for moderate payoff.

## Key Design Constraint

All probing must use the **same inputs for decomp and orig**. The value comes from differential comparison — we're not fuzzing one binary, we're comparing two under varied conditions. Every probe run produces a decomp result and an orig result, and the comparison between them is the signal.
