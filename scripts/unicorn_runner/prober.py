"""Multi-input probing for divergence profiling.

Runs each function N times with varied inputs (fill patterns, register values)
to build a behavioral profile that goes beyond simple dual-fixture testing.
"""

import os
import random
from dataclasses import dataclass, field

from .comparator import compare, classify_divergence
from .run import _run_comparison_core, EXIT_EQUIVALENT, EXIT_DIVERGENT, EXIT_ERROR, EXIT_SKIPPED
from .typed_fixture import extract_class_from_symbol, generate_typed_object


@dataclass
class ProbeResult:
    """Aggregated result of multi-input probing."""
    total_runs: int = 0
    equiv_runs: int = 0
    divergent_runs: int = 0
    error_runs: int = 0
    stable_equiv: bool = False       # all runs equivalent
    stable_divergent: bool = False   # all runs divergent
    input_sensitive: bool = False    # mixed results across inputs
    divergence_classes: dict = field(default_factory=dict)  # class -> count
    per_run: list = field(default_factory=list)  # per-run details
    sensitivity_dimensions: list = field(default_factory=list)  # which inputs cause flips
    early_exit: bool = False         # stopped early due to stability

    @property
    def confidence(self):
        """Return a confidence label based on probing results."""
        if self.total_runs == 0:
            return "none"
        if self.stable_equiv:
            return "high"
        if self.stable_divergent:
            return "stable_divergent"
        return "input_sensitive"


@dataclass
class RunDetail:
    """Detail for a single probe run."""
    fill_pattern: object  # None or int
    exit_code: int
    divergence_class: str = None  # None for EQUIVALENT
    fixture_type: str = "fill"    # "fill" or "typed"
    arg_r4: int = 0
    arg_r5: int = 0
    arg_r6: int = 0


def _load_struct_db():
    """Load StructDB if available, returning (db, success)."""
    try:
        from tools.struct_db import StructDB
    except ImportError:
        return None, False

    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    db_path = os.path.join(project_root, "struct_db.sqlite")
    if not os.path.exists(db_path):
        return None, False

    db = StructDB(db_path)
    db.connect()
    return db, True


def probe_function(symbol, decomp_coff, orig_coff, runs=8,
                   coload=True, coload_depth=None, timeout=5_000_000,
                   seed=None, typed=False, unit_class=None,
                   typed_mem_zero=None, typed_mem_cd=None, early_exit=True):
    """Run a function N times with varied fill patterns and aggregate results.

    Args:
        symbol: mangled symbol name
        decomp_coff: pre-parsed COFFParser for decomp
        orig_coff: pre-parsed COFFParser for original
        runs: number of probe runs (default 8)
        coload: enable intra-TU callee co-loading
        coload_depth: max co-loading recursion depth
        timeout: execution timeout in microseconds
        seed: random seed for reproducibility
        typed: use type-aware object memory from struct_db
        unit_class: override class name (from unit path) for typed fixtures
        typed_mem_zero: pre-generated typed memory (zero fill) to reuse
        typed_mem_cd: pre-generated typed memory (CD fill) to reuse
        early_exit: stop early if first 2 runs show stable behavior

    Returns:
        ProbeResult or None if symbol not found/empty
    """
    rng = random.Random(seed)

    # Generate typed object memory if requested and not pre-provided
    if typed and typed_mem_zero is None:
        class_name = unit_class or extract_class_from_symbol(symbol)
        if class_name:
            db, ok = _load_struct_db()
            if ok:
                typed_mem_zero = generate_typed_object(class_name, db, rng, fill_byte=0x00)
                typed_mem_cd = generate_typed_object(class_name, db, rng, fill_byte=0xCD)
                db.close()

    # Build run schedule with arg variation:
    # Run 0-1: r4-r6 = 0, fill varies (zero, 0xCD) — baseline
    # Run 2-3: r4-r6 = small ints (1, 2, 0), fill varies — exercise if(arg) branches
    # Run 4+: r4-r6 = random, fill varies — full randomization
    # If typed memory available, interleave typed runs
    has_typed = typed_mem_zero is not None

    schedule = []  # list of (fill_pattern, object_memory, fixture_type, arg_r4, arg_r5, arg_r6)
    # Baseline runs (args=0)
    schedule.append((None, None, "fill", 0, 0, 0))
    schedule.append((0xCD, None, "fill", 0, 0, 0))

    # Small int args (exercises conditional branches)
    if runs > 2:
        schedule.append((None, None, "fill", 1, 2, 0))
        schedule.append((0xCD, None, "fill", 1, 2, 0))

    # Typed memory runs (if available)
    if has_typed and runs > 4:
        schedule.append((None, typed_mem_zero, "typed", 0, 0, 0))
        schedule.append((0xCD, typed_mem_cd, "typed", 0, 0, 0))

    # Random fill + random args
    for _ in range(max(0, runs - len(schedule))):
        r4 = rng.randint(0, 0xFFFFFFFF) if rng.random() > 0.5 else 0
        r5 = rng.randint(0, 0xFFFFFFFF) if rng.random() > 0.5 else 0
        r6 = rng.randint(0, 0xFFFFFFFF) if rng.random() > 0.5 else 0
        schedule.append((rng.randint(0x01, 0xFE), None, "fill", r4, r5, r6))

    schedule = schedule[:runs]

    result = ProbeResult()
    result.total_runs = len(schedule)

    for run_idx, (fill, obj_mem, fixture_type, r4, r5, r6) in enumerate(schedule):
        # Build arg_registers dict if any args are non-zero
        arg_registers = None
        if r4 != 0 or r5 != 0 or r6 != 0:
            # Import here to avoid requiring unicorn at module load time
            from unicorn.ppc_const import UC_PPC_REG_4, UC_PPC_REG_5, UC_PPC_REG_6
            arg_registers = {
                UC_PPC_REG_4: r4,
                UC_PPC_REG_5: r5,
                UC_PPC_REG_6: r6,
            }

        exit_code, bundle, _, _ = _run_comparison_core(
            symbol, decomp_coff, orig_coff, timeout=timeout,
            coload=coload, coload_depth=coload_depth,
            fill_pattern=fill, object_memory=obj_mem,
            arg_registers=arg_registers)

        if exit_code == EXIT_SKIPPED:
            return None

        detail = RunDetail(fill_pattern=fill, exit_code=exit_code,
                           fixture_type=fixture_type, arg_r4=r4, arg_r5=r5, arg_r6=r6)

        if exit_code == EXIT_EQUIVALENT:
            result.equiv_runs += 1
        elif exit_code == EXIT_DIVERGENT and bundle is not None:
            result.divergent_runs += 1
            div_class = classify_divergence(
                bundle.result, bundle.decomp_result, bundle.orig_result,
                bundle.decomp_relocs, bundle.orig_relocs)
            detail.divergence_class = div_class
            result.divergence_classes[div_class] = result.divergence_classes.get(div_class, 0) + 1
        elif exit_code == EXIT_ERROR:
            result.error_runs += 1
        else:
            result.divergent_runs += 1

        result.per_run.append(detail)

        # Early exit check after first 2 runs
        if early_exit and run_idx == 1:
            if result.equiv_runs == 2:
                # Both runs equivalent — likely stable
                result.stable_equiv = True
                result.early_exit = True
                result.total_runs = 2
                break
            elif result.divergent_runs == 2:
                # Both runs divergent with same class — check if same
                classes = list(result.divergence_classes.keys())
                if len(classes) == 1:
                    result.stable_divergent = True
                    result.early_exit = True
                    result.total_runs = 2
                    break

    # Classify overall stability (if didn't early exit)
    if not result.early_exit:
        non_error = result.equiv_runs + result.divergent_runs
        if non_error > 0:
            result.stable_equiv = result.equiv_runs == non_error
            result.stable_divergent = result.divergent_runs == non_error
            result.input_sensitive = not result.stable_equiv and not result.stable_divergent

    # Analyze sensitivity dimensions
    if result.input_sensitive:
        result.sensitivity_dimensions = _analyze_sensitivity_dimensions(result.per_run)

    return result


def _analyze_sensitivity_dimensions(per_run):
    """Analyze which input dimensions cause divergence vs equivalence.

    Returns list of dimension names: "fill", "args", "typed"
    """
    dimensions = []

    # Group runs by fixture type and args
    fill_only = [r for r in per_run if r.fixture_type == "fill" and r.arg_r4 == 0 and r.arg_r5 == 0 and r.arg_r6 == 0]
    typed_runs = [r for r in per_run if r.fixture_type == "typed"]
    arg_runs = [r for r in per_run if r.arg_r4 != 0 or r.arg_r5 != 0 or r.arg_r6 != 0]

    # Check if typed causes different results than fill-only
    if typed_runs and fill_only:
        typed_equiv = sum(1 for r in typed_runs if r.exit_code == EXIT_EQUIVALENT)
        fill_equiv = sum(1 for r in fill_only if r.exit_code == EXIT_EQUIVALENT)
        typed_rate = typed_equiv / len(typed_runs) if typed_runs else 0
        fill_rate = fill_equiv / len(fill_only) if fill_only else 0
        if abs(typed_rate - fill_rate) > 0.3:  # 30% difference
            dimensions.append("typed")

    # Check if args cause different results
    if arg_runs and fill_only:
        arg_equiv = sum(1 for r in arg_runs if r.exit_code == EXIT_EQUIVALENT)
        fill_equiv = sum(1 for r in fill_only if r.exit_code == EXIT_EQUIVALENT)
        arg_rate = arg_equiv / len(arg_runs) if arg_runs else 0
        fill_rate = fill_equiv / len(fill_only) if fill_only else 0
        if abs(arg_rate - fill_rate) > 0.3:
            dimensions.append("args")

    # Check if fill patterns themselves cause different results
    if len(fill_only) > 1:
        results_by_fill = {}
        for r in fill_only:
            key = f"0x{r.fill_pattern:02X}" if r.fill_pattern is not None else "zero"
            if key not in results_by_fill:
                results_by_fill[key] = []
            results_by_fill[key].append(r.exit_code)
        # If different fill bytes give different results, it's fill-sensitive
        if len(set(tuple(v) for v in results_by_fill.values())) > 1:
            dimensions.append("fill")

    return dimensions if dimensions else ["unknown"]


def format_probe_result(probe, symbol=None):
    """Format a ProbeResult for display."""
    lines = []
    if symbol:
        lines.append(f"Probe: {symbol}")

    label = probe.confidence
    early_exit_note = ""
    if probe.early_exit:
        early_exit_note = f" (early exit after {probe.total_runs} runs)"

    lines.append(f"  Runs: {probe.total_runs} ({probe.equiv_runs} equiv, "
                 f"{probe.divergent_runs} div, {probe.error_runs} err){early_exit_note}")
    lines.append(f"  Confidence: {label}")

    if probe.divergence_classes:
        classes = ", ".join(f"{k}: {v}" for k, v in sorted(probe.divergence_classes.items()))
        lines.append(f"  Divergence classes: {classes}")

    if probe.sensitivity_dimensions:
        lines.append(f"  Sensitive to: {', '.join(probe.sensitivity_dimensions)}")

    if probe.input_sensitive:
        # Show which inputs caused divergence vs equivalence
        equiv_inputs = []
        div_inputs = []
        for d in probe.per_run:
            fill_name = f"0x{d.fill_pattern:02X}" if d.fill_pattern is not None else "zero"
            if d.fixture_type == "typed":
                fill_name += "+typed"
            if d.arg_r4 != 0 or d.arg_r5 != 0 or d.arg_r6 != 0:
                fill_name += f"+args({d.arg_r4},{d.arg_r5},{d.arg_r6})"

            if d.exit_code == EXIT_EQUIVALENT:
                equiv_inputs.append(fill_name)
            elif d.exit_code == EXIT_DIVERGENT:
                div_inputs.append(fill_name)

        if equiv_inputs:
            lines.append(f"  Equiv inputs: {', '.join(equiv_inputs)}")
        if div_inputs:
            lines.append(f"  Div inputs: {', '.join(div_inputs)}")

    return "\n".join(lines)
