"""Diagnosis layer — extract structured mismatch info from objdiff JSON.

Reuses pure analysis functions from scripts/analysis/diff_inspect.py to avoid
duplicating logic. Produces a Diagnosis dataclass for pattern filtering.
"""

from __future__ import annotations

from collections import Counter

from .types import Cluster, DiffOp, Diagnosis, SwapInfo

# Import pure analysis functions from diff_inspect
from scripts.analysis.diff_inspect import (
    parse_breakdowns,
    compute_reg_swap_pairs,
    compute_offset_histogram,
    find_clusters,
    categorize_replaces,
)


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

    # Diff ops (opcode mismatches)
    diff_ops: list[DiffOp] = []
    for ins in instrs:
        if ins.get("match_type") == "diff_op":
            t = ins.get("target", {})
            b = ins.get("base", {})
            diff_ops.append(DiffOp(
                index=ins["index"],
                target_opcode=t.get("opcode", ""),
                base_opcode=b.get("opcode", ""),
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

    # Noise budget: how many diff_arg instructions are fully explained
    noise_total = sum(1 for ins in instrs if ins.get("match_type") == "diff_arg")
    noise_explained = 0
    for ins in instrs:
        if ins.get("match_type") != "diff_arg":
            continue
        bd = ins.get("diff_breakdown")
        if not bd:
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
    )


def is_all_noise(diagnosis: Diagnosis) -> bool:
    """Return True if all mismatches are noise (nothing to permute).

    Noise = no diff_ops, no clusters, no unexplained diff_arg, and no GPR swaps.
    """
    if diagnosis.diff_ops:
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

    # Noise
    if diagnosis.noise_total > 0:
        parts.append(
            f"noise {diagnosis.noise_explained}/{diagnosis.noise_total}"
        )

    return "Diagnosis: " + ", ".join(parts)
