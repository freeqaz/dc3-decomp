"""Diagnosis layer — extract structured mismatch info from objdiff JSON.

Reuses pure analysis functions from scripts/analysis/diff_inspect.py to avoid
duplicating logic. Produces a Diagnosis dataclass for pattern filtering.
"""

from __future__ import annotations

import re
from collections import Counter

from .types import Cluster, DiffOp, Diagnosis, SwapInfo

# Match __savegprlr_N or __savefpr_N in bl targets
_SAVE_GPR_RE = re.compile(r"__savegprlr_(\d+)")
_SAVE_FPR_RE = re.compile(r"__savefpr_(\d+)")

# Import pure analysis functions from diff_inspect
from scripts.analysis.diff_inspect import (
    parse_breakdowns,
    compute_reg_swap_pairs,
    compute_offset_histogram,
    find_clusters,
    categorize_replaces,
)


def _extract_prologue_saves(
    instrs: list[dict], side: str
) -> tuple[int | None, int | None]:
    """Extract GPR and FPR save counts from prologue instructions.

    Scans the first ~15 instructions on the given side ('target' or 'base')
    for `bl __savegprlr_N` and `bl __savefpr_N` calls. Returns (gpr_saves, fpr_saves)
    where saves = 32 - N (the number of callee-saved registers pushed).
    """
    gpr_saves: int | None = None
    fpr_saves: int | None = None

    for ins in instrs[:15]:
        side_data = ins.get(side, {})
        opcode = side_data.get("opcode", "")
        if opcode != "bl":
            continue

        # Get the branch target from typed_args (objdiff JSON format)
        # Also check "arguments" for compatibility with diff_breakdown format
        args = side_data.get("typed_args", []) or side_data.get("arguments", [])
        for arg in args:
            val = arg.get("value")
            if not isinstance(val, str):
                continue

            m = _SAVE_GPR_RE.search(val)
            if m:
                gpr_saves = 32 - int(m.group(1))
                continue

            m = _SAVE_FPR_RE.search(val)
            if m:
                fpr_saves = 32 - int(m.group(1))

        # Also check raw "args" string as fallback
        if gpr_saves is None and fpr_saves is None:
            raw_args = side_data.get("args", "")
            if isinstance(raw_args, str):
                m = _SAVE_GPR_RE.search(raw_args)
                if m:
                    gpr_saves = 32 - int(m.group(1))
                m = _SAVE_FPR_RE.search(raw_args)
                if m:
                    fpr_saves = 32 - int(m.group(1))

    return gpr_saves, fpr_saves


def diagnose_baseline(objdiff_json: dict) -> Diagnosis:
    """Analyze objdiff JSON output and produce a structured Diagnosis.

    Args:
        objdiff_json: Parsed JSON from objdiff-cli diff (must contain
            'instructions' key with --include-instructions data).

    Returns:
        Diagnosis with all mismatch categories populated.
    """
    instrs = objdiff_json.get("instructions", [])
    total = len(instrs)

    # Match type counts
    match_counts = dict(Counter(ins.get("match_type", "") for ins in instrs))

    # Parse diff_breakdown data
    reg_swaps_raw, offset_diffs, symbol_diffs, branch_diffs = parse_breakdowns(instrs)

    # Register swap pairs
    pair_data_raw = compute_reg_swap_pairs(reg_swaps_raw)
    reg_swap_pairs: dict[tuple[str, str], SwapInfo] = {}
    for pair, data in pair_data_raw.items():
        reg_swap_pairs[pair] = SwapInfo(
            count=data["count"],
            first_idx=data["first"],
            last_idx=data["last"],
        )

    # Offset delta histogram
    delta_hist_raw = compute_offset_histogram(offset_diffs)
    offset_deltas = dict(delta_hist_raw)

    # Diff ops (opcode mismatches) — includes both diff_op and replace types
    # since both represent structural code differences that patterns can fix
    diff_ops: list[DiffOp] = []
    for ins in instrs:
        match_type = ins.get("match_type")
        if match_type in ("diff_op", "replace"):
            t = ins.get("target", {})
            b = ins.get("base", {})
            t_op = t.get("opcode", "")
            b_op = b.get("opcode", "")
            # Skip if both sides are empty (pure insert/delete classified as replace)
            if not t_op and not b_op:
                continue
            diff_ops.append(DiffOp(
                index=ins["index"],
                target_opcode=t_op,
                base_opcode=b_op,
            ))

    # Insert/delete clusters
    raw_clusters = find_clusters(instrs, ("insert", "delete"))
    clusters: list[Cluster] = []
    for cluster_group in raw_clusters:
        indices = [ins["index"] for _, ins in cluster_group]
        ins_count = sum(1 for _, ins in cluster_group if ins["match_type"] == "insert")
        del_count = len(cluster_group) - ins_count
        clusters.append(Cluster(
            start_idx=min(indices),
            end_idx=max(indices),
            size=len(cluster_group),
            inserts=ins_count,
            deletes=del_count,
        ))

    # Replace categorization: symbol-reloc noise vs real structural
    replace_noise, replace_real, _ = categorize_replaces(instrs)

    # Prologue save counts
    target_gpr_saves, target_fpr_saves = _extract_prologue_saves(instrs, "target")
    base_gpr_saves, base_fpr_saves = _extract_prologue_saves(instrs, "base")

    # Boolean materialization detection: count subfc/eqv/srwi/addze sequences
    # that appear in target-only (delete) instructions, indicating branchless
    # boolean evaluation via bitwise & instead of branching &&
    bool_mat_count = _detect_bool_materialization(instrs)

    # GPR-FPR type conflict: opposite-sign deltas suggest the target uses
    # a GPR where base uses an FPR (or vice versa), e.g., caching a float
    # literal ADDRESS in GPR vs VALUE in FPR
    gpr_fpr_conflict = _detect_gpr_fpr_type_conflict(
        target_gpr_saves, base_gpr_saves, target_fpr_saves, base_fpr_saves
    )

    # Noise budget: how many diff_arg instructions are fully explained
    #
    # Address relocation heuristic: diff_arg with no diff_breakdown is almost
    # always an address relocation mismatch (lis/addi loading symbol addresses,
    # bl calling a function at a different address). objdiff doesn't provide
    # diff_breakdown for these, but the symbols match — pure noise.
    _ADDR_RELOC_OPCODES = frozenset({"lis", "addi", "bl", "bla"})

    noise_total = sum(1 for ins in instrs if ins.get("match_type") == "diff_arg")
    noise_explained = 0
    for ins in instrs:
        if ins.get("match_type") != "diff_arg":
            continue
        bd = ins.get("diff_breakdown")
        if not bd:
            # No breakdown data — check if it's a known address relocation opcode
            opcode = ins.get("target", {}).get("opcode", "")
            if opcode in _ADDR_RELOC_OPCODES:
                noise_explained += 1
            continue
        all_explained = True
        for arg in bd.get("arguments", []):
            at = arg.get("arg_type", "")
            if at in ("symbol", "branch_dest", "register"):
                continue
            elif at == "immediate":
                tv = arg.get("target", {}).get("value")
                bv = arg.get("base", {}).get("value")
                if isinstance(tv, (int, float)) and isinstance(bv, (int, float)):
                    continue
                elif isinstance(tv, str) or isinstance(bv, str):
                    continue
                else:
                    all_explained = False
            else:
                all_explained = False
        if all_explained:
            noise_explained += 1

    return Diagnosis(
        total_instructions=total,
        match_counts=match_counts,
        reg_swap_pairs=reg_swap_pairs,
        offset_deltas=offset_deltas,
        diff_ops=diff_ops,
        clusters=clusters,
        noise_explained=noise_explained,
        noise_total=noise_total,
        replace_noise=replace_noise,
        replace_real=replace_real,
        target_gpr_saves=target_gpr_saves,
        base_gpr_saves=base_gpr_saves,
        target_fpr_saves=target_fpr_saves,
        base_fpr_saves=base_fpr_saves,
        bool_materialization_sequences=bool_mat_count,
        has_gpr_fpr_type_conflict=gpr_fpr_conflict,
    )


def _detect_bool_materialization(instrs: list[dict]) -> int:
    """Count subfc/eqv/srwi/addze sequences in target-only instructions.

    This sequence is generated by MSVC PPC for branchless boolean
    materialization when using bitwise & instead of logical &&.
    We look for the sequence in delete (target-only) or diff_op rows.
    """
    # Collect target-side opcodes from delete and diff_op instructions
    target_opcodes = []
    for ins in instrs:
        match_type = ins.get("match_type", "")
        if match_type == "delete":
            op = ins.get("target", {}).get("opcode", "")
            if op:
                target_opcodes.append(op)
        elif match_type in ("diff_op", "replace"):
            op = ins.get("target", {}).get("opcode", "")
            if op:
                target_opcodes.append(op)

    # Scan for the characteristic sequence: subfc ... eqv ... addze
    # The full pattern is: subfc, eqv, srwi, addze (possibly with clrlwi)
    # We count how many subfc instructions are followed (within 4 ops) by addze
    count = 0
    for i, op in enumerate(target_opcodes):
        if op == "subfc":
            window = target_opcodes[i + 1 : i + 5]
            if "addze" in window:
                count += 1
    return count


def _detect_gpr_fpr_type_conflict(
    target_gpr: int | None,
    base_gpr: int | None,
    target_fpr: int | None,
    base_fpr: int | None,
) -> bool:
    """Detect opposite-sign GPR/FPR save deltas.

    When target has MORE GPR saves but FEWER FPR saves (or vice versa),
    it suggests a type conflict — e.g., the target caches a float literal
    address in a GPR while the base caches the float value in an FPR.
    """
    gpr_delta = 0
    fpr_delta = 0
    if target_gpr is not None and base_gpr is not None:
        gpr_delta = target_gpr - base_gpr
    if target_fpr is not None and base_fpr is not None:
        fpr_delta = target_fpr - base_fpr

    # Opposite signs and both non-zero
    if gpr_delta != 0 and fpr_delta != 0:
        return (gpr_delta > 0) != (fpr_delta > 0)
    return False


def is_all_noise(diagnosis: Diagnosis) -> bool:
    """Return True if all mismatches are noise (nothing to permute).

    Noise = no real diff_ops, no clusters, no unexplained diff_arg, and no GPR swaps.
    """
    # diff_ops now includes replaces — check if any are non-noise
    if diagnosis.replace_real > 0:
        return False
    # Check for pure diff_op type mismatches (not from replace)
    non_replace_diff_ops = len(diagnosis.diff_ops) - (diagnosis.replace_real + diagnosis.replace_noise)
    if non_replace_diff_ops > 0:
        return False
    if diagnosis.clusters:
        return False

    # Check for GPR swap pairs (potentially fixable via declaration reorder)
    for (r0, r1), info in diagnosis.reg_swap_pairs.items():
        if r0.startswith("r") or r1.startswith("r"):
            return False

    # Check for unexplained diff_arg
    unexplained = diagnosis.noise_total - diagnosis.noise_explained
    if unexplained > 0:
        return False

    # Check for real replaces (symbol-reloc noise doesn't count)
    if diagnosis.replace_real > 0:
        return False

    return True


def format_diagnosis_summary(diagnosis: Diagnosis) -> str:
    """Format a one-line diagnosis summary for stderr output."""
    parts = []

    # Diff ops
    if diagnosis.diff_ops:
        opcodes = set()
        for d in diagnosis.diff_ops[:3]:
            opcodes.add(f"{d.target_opcode}/{d.base_opcode}")
        op_str = ", ".join(opcodes)
        parts.append(f"{len(diagnosis.diff_ops)} diff_ops ({op_str})")
    else:
        parts.append("0 diff_ops")

    # GPR swap pairs
    gpr_pairs = [(p, i) for p, i in diagnosis.reg_swap_pairs.items()
                 if p[0].startswith("r")]
    if gpr_pairs:
        pair_strs = [f"{p[0]}<->{p[1]}" for p, _ in gpr_pairs[:3]]
        parts.append(f"{len(gpr_pairs)} GPR swaps ({', '.join(pair_strs)})")
    else:
        parts.append("0 GPR swaps")

    # Clusters
    parts.append(f"{len(diagnosis.clusters)} clusters")

    # Replaces
    total_replaces = diagnosis.replace_noise + diagnosis.replace_real
    if total_replaces > 0:
        parts.append(
            f"replaces {diagnosis.replace_real} real + {diagnosis.replace_noise} noise"
        )

    # Prologue mismatch
    if diagnosis.has_prologue_mismatch:
        prologue_parts = []
        if diagnosis.gpr_save_delta != 0:
            prologue_parts.append(
                f"GPR target {diagnosis.target_gpr_saves} vs base {diagnosis.base_gpr_saves} "
                f"(delta {diagnosis.gpr_save_delta:+d})"
            )
        if diagnosis.fpr_save_delta != 0:
            prologue_parts.append(
                f"FPR target {diagnosis.target_fpr_saves} vs base {diagnosis.base_fpr_saves} "
                f"(delta {diagnosis.fpr_save_delta:+d})"
            )
        parts.append(f"prologue: {'; '.join(prologue_parts)}")

    # Boolean materialization
    if diagnosis.bool_materialization_sequences > 0:
        parts.append(
            f"bool_mat {diagnosis.bool_materialization_sequences} seq"
        )

    # GPR-FPR type conflict
    if diagnosis.has_gpr_fpr_type_conflict:
        parts.append("GPR-FPR type conflict")

    # Noise
    if diagnosis.noise_total > 0:
        parts.append(
            f"noise {diagnosis.noise_explained}/{diagnosis.noise_total}"
        )

    return "Diagnosis: " + ", ".join(parts)
