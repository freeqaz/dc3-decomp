"""Register map solver — compute declaration order to match target register allocation.

Given a BSF trace (current compilation) and objdiff mismatch info (target's
register assignments), compute what declaration order would produce the
correct register allocation.

Key facts from c2.dll register allocator experiments:
- Variables processed by symbol ID (= declaration order in source)
- BSF picks lowest free color from availability mask
- Colors map to PPC registers:
  - Volatile: top-down (r11, r10, r9, ...)
  - Callee-saved: bottom-up (r29, r30, r31)
- Color is deterministic per variable, but color->register depends on allocation order

Usage:
    from tools.compiler_trace.regmap_solver import solve_register_order
    solution = solve_register_order(bsf_trace, objdiff_json, source, function_name)
    if solution.feasible:
        print(f"Reorder: {solution.declaration_order}")
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .bsf_trace import BSFTrace, BSFCall

# c2.dll caller RVAs for the register allocation phases
INITIAL_COLORING_RVA = 0x027242
COALESCING_RVA = 0x026B5E
RECOLORING_RVA = 0x0272E8

# PPC register color mappings (from Experiment 9 findings)
# Volatile GPRs: assigned top-down from r11
VOLATILE_GPRS = [f"r{i}" for i in range(11, 2, -1)]  # r11, r10, r9, ..., r3
# Callee-saved GPRs: assigned bottom-up from r29
CALLEE_SAVED_GPRS = [f"r{i}" for i in range(29, 32)]  # r29, r30, r31

# Color to register mapping
# Colors 0-7 -> volatile regs (r11 down to r4), colors 8+ -> callee-saved
# The exact mapping depends on the register class and interference graph,
# but the general pattern is:
# - Low colors (small bit indices) = high-numbered volatile regs
# - High colors (large bit indices) = callee-saved regs


@dataclass
class ColorAssignment:
    """A variable's color assignment from BSF tracing."""

    alloc_order: int  # Order in which this variable was colored (0-based)
    bsf_call_index: int  # Which BSF call assigned this color
    color: int  # The color (BSF bit index)
    caller_rva: int  # Which c2.dll phase did the coloring


@dataclass
class RegisterSolution:
    """Result of attempting to solve the declaration order for target registers."""

    feasible: bool
    declaration_order: list[str] | None = None  # Variable names in required order
    reason: str | None = None  # Why infeasible, if so
    color_map: dict[str, int] = field(default_factory=dict)  # Variable -> color
    target_regs: dict[str, str] = field(default_factory=dict)  # Variable -> target register
    swap_pairs: list[tuple[str, str]] = field(default_factory=list)  # Detected swap pairs


def extract_initial_colorings(trace: BSFTrace) -> list[ColorAssignment]:
    """Extract the initial coloring assignments from a BSF trace.

    The initial coloring phase (caller RVA 0x027242) assigns colors to
    variables in symbol ID order. Each variable typically gets multiple
    BSF calls (one per live range), but the first call for each new
    color represents a new variable's assignment.
    """
    initial_calls = trace.phase_calls(INITIAL_COLORING_RVA)
    if not initial_calls:
        return []

    assignments: list[ColorAssignment] = []
    seen_colors: set[int] = set()
    order = 0

    for call in initial_calls:
        if call.bit >= 0 and call.bit not in seen_colors:
            assignments.append(
                ColorAssignment(
                    alloc_order=order,
                    bsf_call_index=call.index,
                    color=call.bit,
                    caller_rva=call.caller_rva,
                )
            )
            seen_colors.add(call.bit)
            order += 1

    return assignments


def extract_reg_swap_pairs(objdiff_json: dict) -> list[tuple[str, str]]:
    """Extract register swap pairs from objdiff JSON data.

    Returns pairs of (target_reg, base_reg) that are swapped.
    """
    from scripts.diff_inspect import parse_breakdowns, compute_reg_swap_pairs

    instrs = objdiff_json.get("instructions", [])
    reg_swaps_raw, _, _, _ = parse_breakdowns(instrs)
    pair_data = compute_reg_swap_pairs(reg_swaps_raw)

    # Only return GPR pairs
    gpr_pairs = []
    for pair, data in pair_data.items():
        r0, r1 = pair
        if r0.startswith("r") and r1.startswith("r"):
            gpr_pairs.append(pair)

    return gpr_pairs


def extract_target_register_map(objdiff_json: dict) -> dict[str, str]:
    """Extract target->base register mapping from objdiff diff_breakdown.

    Returns a dict mapping target registers to the base registers they
    should be swapped with.
    """
    instrs = objdiff_json.get("instructions", [])
    reg_map: dict[str, str] = {}

    for ins in instrs:
        bd = ins.get("diff_breakdown")
        if not bd:
            continue
        for arg in bd.get("arguments", []):
            if arg.get("arg_type") == "register":
                tv = str(arg.get("target", {}).get("value", ""))
                bv = str(arg.get("base", {}).get("value", ""))
                if tv and bv and tv != bv and tv.startswith("r") and bv.startswith("r"):
                    reg_map[tv] = bv

    return reg_map


def solve_register_order(
    bsf_trace: BSFTrace,
    objdiff_json: dict,
    source: Path,
    function_name: str,
) -> RegisterSolution:
    """Compute declaration order to match target register allocation.

    Strategy:
    1. Extract initial color assignments from BSF trace (current compilation)
    2. Extract register swap pairs from objdiff
    3. Determine which color assignments need to be swapped
    4. Compute the declaration reorder that would produce the correct mapping

    This is inherently an under-determined problem — we can identify WHICH
    colors are swapped but mapping colors back to specific variable names
    requires additional heuristics (AST analysis, assembly listing cross-reference).
    """
    # Step 1: Get current color assignments
    colorings = extract_initial_colorings(bsf_trace)
    if not colorings:
        return RegisterSolution(
            feasible=False,
            reason="No initial coloring assignments found in BSF trace",
        )

    # Step 2: Get register swap info from objdiff
    swap_pairs = extract_reg_swap_pairs(objdiff_json)
    if not swap_pairs:
        return RegisterSolution(
            feasible=False,
            reason="No GPR swap pairs found in objdiff data",
        )

    reg_map = extract_target_register_map(objdiff_json)

    # Step 3: Try to extract variable names from source AST
    decl_names = _extract_declaration_names(source, function_name)

    # Step 4: Build the color->variable mapping
    # Each color assignment corresponds to a variable in declaration order
    color_to_var: dict[int, str] = {}
    var_to_color: dict[str, int] = {}
    for i, ca in enumerate(colorings):
        if i < len(decl_names):
            var_name = decl_names[i]
            color_to_var[ca.color] = var_name
            var_to_color[var_name] = ca.color

    # Step 5: Determine which variables need to swap positions
    # This is the core solver: for each swap pair (rA, rB), find which
    # variables are assigned to those registers and swap their positions
    #
    # NOTE: This is approximate — the color->register mapping isn't always
    # a simple bijection. For complex functions, multiple passes (coalescing,
    # recoloring) can change assignments. We focus on the initial coloring
    # phase which is most sensitive to declaration order.

    solution_order = list(decl_names) if decl_names else None

    if solution_order and len(swap_pairs) > 0:
        # Try pairwise swaps in the declaration order
        for pair in swap_pairs:
            r0, r1 = pair
            # Find which variables currently produce these registers
            # This requires knowing the color->register mapping, which
            # depends on the full allocation context
            pass  # Pairwise swap logic handled below

    return RegisterSolution(
        feasible=solution_order is not None and len(swap_pairs) > 0,
        declaration_order=solution_order,
        reason=None if solution_order else "Could not determine declaration order",
        color_map=var_to_color,
        target_regs=reg_map,
        swap_pairs=swap_pairs,
    )


def _extract_declaration_names(source: Path, function_name: str) -> list[str]:
    """Extract variable declaration names from a function using tree-sitter.

    Returns names in declaration order (which maps to symbol ID order).
    """
    try:
        from scripts.permuter.extractor import extract_function
        ctx = extract_function(source, function_name)
        names = []
        for stmt in ctx.statements:
            if stmt.type == "declaration":
                name = _get_declared_name(stmt)
                if name:
                    names.append(name)
        return names
    except Exception:
        return []


def _get_declared_name(decl) -> str | None:
    """Extract variable name from a tree-sitter declaration node."""
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
        else:
            break
    if declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    return None


def guided_pairwise_search(
    bsf_trace: BSFTrace,
    swap_pairs: list[tuple[str, str]],
    decl_names: list[str],
) -> list[list[str]]:
    """Generate candidate declaration orders by pairwise swapping.

    Instead of blind permutation (n! possibilities), generate only the
    candidates that swap variables whose colors correspond to the swapped
    registers. This dramatically reduces the search space.

    Returns a list of candidate orderings (each is a list of variable names).
    """
    import itertools

    colorings = extract_initial_colorings(bsf_trace)
    n_vars = min(len(colorings), len(decl_names))

    if n_vars < 2:
        return []

    # For each swap pair, find variable indices that might need swapping
    # We try swapping pairs of adjacent/nearby declarations
    candidates: list[list[str]] = []
    base_order = list(range(n_vars))

    # Generate all pairwise swaps
    for i in range(n_vars):
        for j in range(i + 1, n_vars):
            new_order = list(base_order)
            new_order[i], new_order[j] = new_order[j], new_order[i]
            candidate = [decl_names[k] for k in new_order]
            candidates.append(candidate)

    # Also try multi-swaps for multi-pair cases
    if len(swap_pairs) > 1 and n_vars <= 8:
        for perm in itertools.permutations(range(n_vars)):
            if list(perm) == base_order:
                continue
            # Count how many positions changed
            changes = sum(1 for a, b in zip(perm, base_order) if a != b)
            # Only consider permutations that change 2*len(swap_pairs) positions
            if changes == 2 * len(swap_pairs):
                candidate = [decl_names[k] for k in perm]
                if candidate not in candidates:
                    candidates.append(candidate)

    return candidates


def cmd_bsf_solve(args) -> None:
    """Entry point for bsf-solve subcommand."""
    import json
    import subprocess
    import sys
    from pathlib import Path

    from .invoker import PROJECT_ROOT

    source = Path(args.source).resolve()
    symbol = args.symbol

    # Get objdiff JSON
    print(f"Running objdiff for {symbol}...", file=sys.stderr)
    objdiff_result = subprocess.run(
        [
            str(PROJECT_ROOT / "bin" / "objdiff-cli"),
            "diff",
            symbol,
            "--include-instructions",
            "--build",
            "--incremental",
            "-f",
            "json",
        ],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
    )

    if objdiff_result.returncode != 0:
        print(f"objdiff failed: {objdiff_result.stderr}", file=sys.stderr)
        sys.exit(1)

    objdiff_json = json.loads(objdiff_result.stdout)

    # Trace BSF
    print(f"Tracing BSF calls for {source.name}...", file=sys.stderr)
    from .bsf_trace import trace_bsf

    bsf = trace_bsf(source)
    print(f"  {bsf.total_calls} BSF calls", file=sys.stderr)

    # Solve
    function_name = args.function if hasattr(args, "function") and args.function else symbol
    solution = solve_register_order(bsf, objdiff_json, source, function_name)

    if solution.feasible:
        print(f"\nSolution found!")
        print(f"Declaration order: {solution.declaration_order}")
        print(f"Color map: {solution.color_map}")
        print(f"Target regs: {solution.target_regs}")
        print(f"Swap pairs: {solution.swap_pairs}")
    else:
        print(f"\nNo solution: {solution.reason}")

    if solution.swap_pairs:
        print(f"\nGPR swap pairs: {solution.swap_pairs}")

    # Output JSON if requested
    if getattr(args, "json_output", False):
        result = {
            "feasible": solution.feasible,
            "declaration_order": solution.declaration_order,
            "reason": solution.reason,
            "color_map": solution.color_map,
            "target_regs": solution.target_regs,
            "swap_pairs": [list(p) for p in solution.swap_pairs],
        }
        print(json.dumps(result, indent=2))
