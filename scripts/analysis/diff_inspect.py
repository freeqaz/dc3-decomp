#!/usr/bin/env python3
"""Inspect objdiff JSON output for specific mismatch types with context.

Usage:
    # Generate JSON diff first:
    ./bin/objdiff-cli diff "symbol_name" --include-instructions --build --incremental -f json -o /tmp/claude/diff.json

    # Then inspect it:
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json                  # show all non-equal
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json diff_op          # only diff_op
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json replace          # only replace
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json insert,delete    # insert and delete
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json diff_op -C 8     # 8 lines context
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json all              # every instruction
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --range 950-970  # specific index range
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --summary        # count by match type

    # Analysis modes:
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --diagnose       # root cause analysis
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --clusters       # insert/delete clusters
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --regswaps       # register swap pairs
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --offsets        # offset shift analysis
    python3 scripts/analysis/diff_inspect.py /tmp/claude/diff.json --replaces       # replace categorization
    python3 scripts/analysis/diff_inspect.py --compare base.json cand.json           # delta table for two diffs

    # Direct invocation (runs objdiff internally):
    python3 scripts/analysis/diff_inspect.py --symbol "symbol_name" --diagnose
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict


# ── Formatting helpers ──────────────────────────────────────────────────────

def fmt_instr(side: dict | None) -> str:
    if not side:
        return "---"
    op = side.get("opcode", "???")
    args = side.get("args", "")
    return f"{op:8s} {args}"


def diff_annotation(ins: dict) -> str:
    """Return a short annotation describing what changed in a diff_arg instruction."""
    bd = ins.get("diff_breakdown")
    if not bd:
        return ""
    parts = []
    for arg in bd.get("arguments", []):
        at = arg.get("arg_type", "")
        tgt = arg.get("target", {})
        base = arg.get("base", {})
        tv = tgt.get("value")
        bv = base.get("value")
        if at == "register":
            parts.append(f"reg:{tv}->{bv}")
        elif at == "symbol":
            # Shorten symbol names
            ts = str(tv)[:20] if tv else "?"
            bs = str(bv)[:20] if bv else "?"
            if ts != bs:
                parts.append("sym")
        elif at == "immediate":
            if isinstance(tv, (int, float)) and isinstance(bv, (int, float)):
                delta = bv - tv
                parts.append(f"off:{delta:+d}")
            else:
                parts.append("imm")
        elif at == "branch_dest":
            parts.append("br")
    return " [" + ", ".join(parts) + "]" if parts else ""


def print_instr(ins: dict, highlight: bool = False, annotate: bool = False):
    idx = ins["index"]
    mt = ins.get("match_type", "")
    t = ins.get("target")
    b = ins.get("base")
    marker = ">>>" if highlight else "   "
    t_str = fmt_instr(t)
    b_str = fmt_instr(b)
    ann = diff_annotation(ins) if annotate and mt == "diff_arg" else ""
    print(f"{marker} {idx:4d} {mt:12s}  TGT: {t_str:40s}  SRC: {b_str}{ann}")


# ── Analysis: parse diff_breakdown ──────────────────────────────────────────

def parse_breakdowns(instrs):
    """Extract structured data from all diff_breakdown entries."""
    reg_swaps = []      # (index, target_reg, base_reg)
    offset_diffs = []   # (index, target_val, base_val, delta)
    symbol_diffs = []   # (index, target_sym, base_sym)
    branch_diffs = []   # (index,)

    for ins in instrs:
        bd = ins.get("diff_breakdown")
        if not bd:
            continue
        idx = ins["index"]
        for arg in bd.get("arguments", []):
            at = arg.get("arg_type", "")
            tgt = arg.get("target", {})
            base = arg.get("base", {})
            tv = tgt.get("value")
            bv = base.get("value")

            if at == "register":
                if tv and bv and tv != bv:
                    reg_swaps.append((idx, str(tv), str(bv)))
            elif at == "immediate":
                if isinstance(tv, (int, float)) and isinstance(bv, (int, float)) and tv != bv:
                    offset_diffs.append((idx, tv, bv, bv - tv))
                elif tv != bv:
                    # String values (symbol embedded in immediate)
                    symbol_diffs.append((idx, str(tv), str(bv)))
            elif at == "symbol":
                if tv != bv:
                    symbol_diffs.append((idx, str(tv)[:60], str(bv)[:60]))
            elif at == "branch_dest":
                branch_diffs.append((idx,))

    return reg_swaps, offset_diffs, symbol_diffs, branch_diffs


def compute_reg_swap_pairs(reg_swaps):
    """Group register swaps into pairs with counts and index ranges."""
    pair_data = defaultdict(lambda: {"count": 0, "first": 99999, "last": 0})
    for idx, tgt_reg, base_reg in reg_swaps:
        # Normalize pair order for grouping
        pair = tuple(sorted([tgt_reg, base_reg]))
        pair_data[pair]["count"] += 1
        pair_data[pair]["first"] = min(pair_data[pair]["first"], idx)
        pair_data[pair]["last"] = max(pair_data[pair]["last"], idx)
    return pair_data


def compute_offset_histogram(offset_diffs):
    """Build a histogram of offset deltas."""
    deltas = Counter()
    for idx, tv, bv, delta in offset_diffs:
        deltas[delta] += 1
    return deltas


def categorize_replaces(instrs):
    """Categorize replace instructions into symbol-reloc noise vs real structural differences."""
    static_sym = 0
    real_replace = 0
    real_examples = []
    for ins in instrs:
        if ins.get("match_type") != "replace":
            continue
        t = ins.get("target", {})
        b = ins.get("base", {})
        t_args = t.get("typed_args", [])
        b_args = b.get("typed_args", [])
        # If same opcode and base has more Symbol-type args, it's a relocation difference
        if t.get("opcode") == b.get("opcode") and len(b_args) > len(t_args):
            extra_b = [a for a in b_args if a.get("type") == "Symbol"]
            extra_t = [a for a in t_args if a.get("type") == "Symbol"]
            if len(extra_b) > len(extra_t):
                static_sym += 1
                continue
        real_replace += 1
        real_examples.append(ins)
    return static_sym, real_replace, real_examples


def find_clusters(instrs, match_types=("insert", "delete"), gap=2):
    """Group instructions of given match_types into contiguous clusters."""
    targets = [(i, ins) for i, ins in enumerate(instrs)
                if ins.get("match_type") in match_types]
    if not targets:
        return []

    clusters = []
    current = [targets[0]]
    for t in targets[1:]:
        if t[0] - current[-1][0] <= gap + 1:
            current.append(t)
        else:
            clusters.append(current)
            current = [t]
    clusters.append(current)
    return clusters


def parse_hotspot_ranges(spec: str):
    """Parse hotspot range list like '930-1075,1235-1415'."""
    ranges = []
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" not in chunk:
            raise ValueError(f"Invalid hotspot range '{chunk}' (expected N-M)")
        lo_s, hi_s = chunk.split("-", 1)
        lo, hi = int(lo_s), int(hi_s)
        if lo > hi:
            lo, hi = hi, lo
        ranges.append((lo, hi))
    return ranges


def count_indel_clusters_in_range(instrs, lo, hi, gap=2):
    """Count insert/delete clusters in an index range."""
    idxs = [
        ins["index"]
        for ins in instrs
        if lo <= ins["index"] <= hi and ins.get("match_type") in ("insert", "delete")
    ]
    if not idxs:
        return 0
    idxs.sort()
    clusters = 1
    for i in range(1, len(idxs)):
        if idxs[i] - idxs[i - 1] > gap + 1:
            clusters += 1
    return clusters


def bool_mask_proxy_count(instrs):
    """Heuristic count for bool-mask style mismatches from instruction stream only."""
    mask_roots = {"clrlwi", "rlwinm", "srwi", "extrwi", "andi", "cntlzw", "subfe", "addze"}
    bit_re = re.compile(r"(?:^|[,\s])(24|31)(?:$|[,\s])")
    count = 0
    for ins in instrs:
        if ins.get("match_type") not in ("diff_op", "replace"):
            continue
        t = ins.get("target") or {}
        b = ins.get("base") or {}
        t_op = t.get("opcode", "")
        b_op = b.get("opcode", "")
        t_root = t_op.rstrip(".")
        b_root = b_op.rstrip(".")
        text = f"{t_op} {t.get('args','')} || {b_op} {b.get('args','')}"

        has_mask_root = t_root in mask_roots or b_root in mask_roots
        dot_flip = (t_root == b_root) and (t_op.endswith(".") != b_op.endswith("."))
        has_bit = bool(bit_re.search(text))
        if has_mask_root and (dot_flip or has_bit):
            count += 1
    return count


def summarize_for_compare(instrs):
    """Collect compact metrics for compare mode."""
    counts = Counter(ins.get("match_type") for ins in instrs)
    reg_swaps, _, _, _ = parse_breakdowns(instrs)
    pair_data = compute_reg_swap_pairs(reg_swaps)
    reg_pair_counts = {pair: data["count"] for pair, data in pair_data.items()}
    return {
        "counts": counts,
        "reg_pair_counts": reg_pair_counts,
        "bool_mask_proxy": bool_mask_proxy_count(instrs),
    }


def cmd_compare(base_data, cand_data, hotspot_spec):
    """Compare two objdiff JSON payloads and print metric deltas."""
    base_instrs = base_data.get("instructions", [])
    cand_instrs = cand_data.get("instructions", [])
    if not base_instrs or not cand_instrs:
        print("Error: both baseline and candidate must contain instructions", file=sys.stderr)
        sys.exit(1)

    base = summarize_for_compare(base_instrs)
    cand = summarize_for_compare(cand_instrs)
    metrics = ["insert", "delete", "replace", "diff_op"]

    print("=" * 70)
    print("COMPARE TWO DIFFS")
    print("=" * 70)
    print()

    base_match = base_data.get("fuzzy_match_percent")
    cand_match = cand_data.get("fuzzy_match_percent")
    if isinstance(base_match, (int, float)) and isinstance(cand_match, (int, float)):
        print(f"Match %: baseline {base_match:.2f}  candidate {cand_match:.2f}  "
              f"delta {cand_match - base_match:+.2f}")
        print()

    print("Mismatch deltas (candidate - baseline):")
    print(f"{'Metric':16s} {'Baseline':>9s} {'Candidate':>10s} {'Delta':>8s}")
    print(f"{'─' * 16} {'─' * 9} {'─' * 10} {'─' * 8}")
    for m in metrics:
        b = base["counts"].get(m, 0)
        c = cand["counts"].get(m, 0)
        print(f"{m:16s} {b:9d} {c:10d} {c - b:+8d}")
    b_noneq = len(base_instrs) - base["counts"].get("equal", 0)
    c_noneq = len(cand_instrs) - cand["counts"].get("equal", 0)
    print(f"{'total_non_equal':16s} {b_noneq:9d} {c_noneq:10d} {c_noneq - b_noneq:+8d}")
    print()

    b_bool = base["bool_mask_proxy"]
    c_bool = cand["bool_mask_proxy"]
    print(f"BOOL_MASK proxy delta: baseline {b_bool}, candidate {c_bool}, delta {c_bool - b_bool:+d}")
    print("  (proxy from instruction patterns; final BOOL_MASK verdict should come from run_objdiff)")
    print()

    print("Top register-pair deltas (candidate - baseline):")
    pair_keys = set(base["reg_pair_counts"]) | set(cand["reg_pair_counts"])
    pair_deltas = []
    for pair in pair_keys:
        b = base["reg_pair_counts"].get(pair, 0)
        c = cand["reg_pair_counts"].get(pair, 0)
        d = c - b
        if d:
            pair_deltas.append((abs(d), d, pair, b, c))
    if not pair_deltas:
        print("  No register-pair count changes.")
    else:
        print(f"  {'Pair':16s} {'Baseline':>9s} {'Candidate':>10s} {'Delta':>8s}")
        print(f"  {'─' * 16} {'─' * 9} {'─' * 10} {'─' * 8}")
        for _, d, pair, b, c in sorted(pair_deltas, reverse=True)[:12]:
            p0, p1 = pair
            print(f"  {p0}<->{p1:11s} {b:9d} {c:10d} {d:+8d}")
    print()

    try:
        hotspots = parse_hotspot_ranges(hotspot_spec)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)

    print("Hotspot deltas (candidate - baseline):")
    print(f"{'Range':14s} {'I+D Δ':>8s} {'Clusters Δ':>11s} {'Replace Δ':>10s} {'diff_op Δ':>10s}")
    print(f"{'─' * 14} {'─' * 8} {'─' * 11} {'─' * 10} {'─' * 10}")
    for lo, hi in hotspots:
        def in_range(ins):
            idx = ins["index"]
            return lo <= idx <= hi

        b_region = [ins for ins in base_instrs if in_range(ins)]
        c_region = [ins for ins in cand_instrs if in_range(ins)]
        b_counts = Counter(ins.get("match_type") for ins in b_region)
        c_counts = Counter(ins.get("match_type") for ins in c_region)

        b_indel = b_counts.get("insert", 0) + b_counts.get("delete", 0)
        c_indel = c_counts.get("insert", 0) + c_counts.get("delete", 0)
        b_clusters = count_indel_clusters_in_range(base_instrs, lo, hi)
        c_clusters = count_indel_clusters_in_range(cand_instrs, lo, hi)
        b_rep = b_counts.get("replace", 0)
        c_rep = c_counts.get("replace", 0)
        b_op = b_counts.get("diff_op", 0)
        c_op = c_counts.get("diff_op", 0)

        label = f"{lo}-{hi}"
        print(f"{label:14s} {c_indel - b_indel:+8d} {c_clusters - b_clusters:+11d} "
              f"{c_rep - b_rep:+10d} {c_op - b_op:+10d}")

# ── Diagnose mode ───────────────────────────────────────────────────────────

def cmd_diagnose(instrs):
    """Root cause analysis: why doesn't this function match?"""
    counts = Counter(ins["match_type"] for ins in instrs)
    total = len(instrs)
    equal_count = counts.get("equal", 0)
    match_pct = 100.0 * equal_count / total if total else 0

    # ── 1. Match Summary ──
    print("=" * 70)
    print("DIAGNOSIS REPORT")
    print("=" * 70)
    print()
    print(f"Total instructions: {total}")
    print(f"Match estimate:     ~{match_pct:.1f}% ({equal_count}/{total} equal)")
    print()
    print("Instruction breakdown:")
    for mt, count in counts.most_common():
        pct = 100.0 * count / total
        print(f"  {mt:12s}: {count:5d} ({pct:5.1f}%)")

    # ── Parse all diff_breakdown data ──
    reg_swaps, offset_diffs, symbol_diffs, branch_diffs = parse_breakdowns(instrs)
    pair_data = compute_reg_swap_pairs(reg_swaps)
    delta_hist = compute_offset_histogram(offset_diffs)

    # ── 2. Root Causes ──
    print()
    print("-" * 70)
    print("ROOT CAUSES")
    print("-" * 70)

    # Stack frame / offset shift
    if delta_hist:
        dominant_delta, dominant_count = delta_hist.most_common(1)[0]
        print()
        print(f"  Stack/offset shift: dominant delta = {dominant_delta:+d} "
              f"({dominant_count} instructions)")
        print("  Top offset deltas:")
        for delta, count in delta_hist.most_common(8):
            print(f"    {delta:+6d}: {count:4d} instructions")
        total_offset_explained = sum(delta_hist.values())
    else:
        dominant_delta = None
        total_offset_explained = 0

    # Register swaps
    if pair_data:
        print()
        print(f"  Register swaps: {len(reg_swaps)} instructions across "
              f"{len(pair_data)} pairs")
        print("  Top swap pairs:")
        for pair, data in sorted(pair_data.items(),
                                  key=lambda x: -x[1]["count"]):
            if data["count"] < 2:
                continue
            p0, p1 = pair
            kind = "GPR" if p0.startswith("r") else "FPR" if p0.startswith("f") else "???"
            print(f"    {p0:4s} <-> {p1:4s}: {data['count']:4d} "
                  f"(idx {data['first']}-{data['last']}) [{kind}]")
        total_reg_explained = len(reg_swaps)
    else:
        total_reg_explained = 0

    # Symbol relocations
    if symbol_diffs:
        print()
        print(f"  Symbol relocations: {len(symbol_diffs)} arg differences")
        # Count how many instructions have at least one symbol diff
        sym_instrs = len(set(idx for idx, _, _ in symbol_diffs))
        print(f"    Across {sym_instrs} instructions")
    else:
        sym_instrs = 0

    # Branch dest diffs
    if branch_diffs:
        print()
        print(f"  Branch destination diffs: {len(branch_diffs)} (address relocation noise)")

    # ── 3. Actionable Mismatches ──
    print()
    print("-" * 70)
    print("ACTIONABLE MISMATCHES")
    print("-" * 70)

    # Compute what diff_arg instructions are NOT explained by root causes
    explained_indices = set()

    # An instruction is "explained" if ALL its diff_breakdown args are
    # pure register swaps, offset shifts, symbols, or branch_dests
    for ins in instrs:
        if ins.get("match_type") != "diff_arg":
            continue
        bd = ins.get("diff_breakdown")
        if not bd:
            continue
        idx = ins["index"]
        all_explained = True
        for arg in bd.get("arguments", []):
            at = arg.get("arg_type", "")
            if at in ("symbol", "branch_dest"):
                continue  # Always noise
            elif at == "register":
                continue  # Register alloc noise
            elif at == "immediate":
                tv = arg.get("target", {}).get("value")
                bv = arg.get("base", {}).get("value")
                if isinstance(tv, (int, float)) and isinstance(bv, (int, float)):
                    continue  # Offset shift noise
                elif isinstance(tv, str) or isinstance(bv, str):
                    continue  # Symbol in immediate
                else:
                    all_explained = False
            else:
                all_explained = False
        if all_explained:
            explained_indices.add(idx)

    # diff_op instructions (always actionable)
    diff_ops = [ins for ins in instrs if ins.get("match_type") == "diff_op"]
    if diff_ops:
        print()
        print(f"  diff_op (opcode mismatches): {len(diff_ops)}")
        for ins in diff_ops:
            idx = ins["index"]
            t = ins.get("target", {})
            b = ins.get("base", {})
            print(f"    idx {idx:4d}: TGT {t.get('opcode','?'):10s} {t.get('args','')}")
            print(f"             SRC {b.get('opcode','?'):10s} {b.get('args','')}")
    else:
        print()
        print("  diff_op: none (good!)")

    # insert/delete clusters
    clusters = find_clusters(instrs, ("insert", "delete"))
    if clusters:
        print()
        total_indel = counts.get("insert", 0) + counts.get("delete", 0)
        print(f"  insert/delete: {total_indel} instructions in "
              f"{len(clusters)} clusters")
        for i, cluster in enumerate(clusters):
            indices = [ins["index"] for _, ins in cluster]
            lo, hi = min(indices), max(indices)
            size = len(cluster)
            ins_count = sum(1 for _, ins in cluster if ins["match_type"] == "insert")
            del_count = size - ins_count
            print(f"    cluster {i+1}: idx {lo}-{hi} "
                  f"({size} instrs: {ins_count}I/{del_count}D)")
    else:
        print()
        print("  insert/delete: none")

    # replace instructions
    replaces = [ins for ins in instrs if ins.get("match_type") == "replace"]
    if replaces:
        sym_noise, real_count, real_examples = categorize_replaces(instrs)
        print()
        print(f"  replace: {len(replaces)} instructions "
              f"({sym_noise} symbol-reloc noise, {real_count} real)")
        if real_examples:
            show = real_examples[:8]
            for ins in show:
                idx = ins["index"]
                t = ins.get("target", {})
                b = ins.get("base", {})
                print(f"    idx {idx:4d}: TGT {fmt_instr(t)}")
                print(f"             SRC {fmt_instr(b)}")
            if len(real_examples) > 8:
                print(f"    ... and {len(real_examples) - 8} more real replaces")

    # Unexplained diff_arg
    diff_arg_instrs = [ins for ins in instrs if ins.get("match_type") == "diff_arg"]
    unexplained = [ins for ins in diff_arg_instrs
                   if ins["index"] not in explained_indices]
    if unexplained:
        print()
        print(f"  Unexplained diff_arg: {len(unexplained)} "
              f"(of {len(diff_arg_instrs)} total)")
        # These are diff_arg without breakdown data — likely still noise
        # but worth flagging
        no_breakdown = [ins for ins in unexplained if not ins.get("diff_breakdown")]
        has_breakdown = [ins for ins in unexplained if ins.get("diff_breakdown")]
        if no_breakdown:
            print(f"    {len(no_breakdown)} without diff_breakdown (no detail available)")
        if has_breakdown:
            print(f"    {len(has_breakdown)} with diff_breakdown (unusual arg types)")
            for ins in has_breakdown[:5]:
                print(f"      idx {ins['index']}: {ins.get('diff_breakdown')}")

    # ── 4. Noise Budget ──
    print()
    print("-" * 70)
    print("NOISE BUDGET")
    print("-" * 70)

    total_diff_arg = len(diff_arg_instrs)
    n_explained = len(explained_indices)
    n_unexplained = total_diff_arg - n_explained
    total_nonequal = total - equal_count

    print()
    print(f"  diff_arg instructions: {total_diff_arg}")
    print(f"    Explained by root causes: {n_explained}")
    print(f"      Offset shifts:     {total_offset_explained} arg diffs")
    print(f"      Register swaps:    {total_reg_explained} arg diffs")
    print(f"      Symbol relocs:     {len(symbol_diffs)} arg diffs")
    print(f"      Branch dests:      {len(branch_diffs)} arg diffs")
    print(f"    Unexplained:         {n_unexplained}")
    print()
    print(f"  Other non-equal: {total_nonequal - total_diff_arg}")
    print(f"    diff_op:   {counts.get('diff_op', 0)}")
    print(f"    replace:   {counts.get('replace', 0)}")
    print(f"    insert:    {counts.get('insert', 0)}")
    print(f"    delete:    {counts.get('delete', 0)}")


# ── Clusters mode ───────────────────────────────────────────────────────────

def cmd_clusters(instrs, context=2):
    """Show insert/delete clusters with context."""
    clusters = find_clusters(instrs, ("insert", "delete"))
    if not clusters:
        print("No insert/delete instructions found.")
        return

    total_indel = sum(len(c) for c in clusters)
    print(f"Found {total_indel} insert/delete instructions in "
          f"{len(clusters)} clusters (gap <= 2)")
    print()

    for i, cluster in enumerate(clusters):
        indices = [ins["index"] for _, ins in cluster]
        lo_idx, hi_idx = min(indices), max(indices)
        size = len(cluster)
        ins_count = sum(1 for _, ins in cluster if ins["match_type"] == "insert")
        del_count = size - ins_count

        # Count dominant opcodes
        opcodes = Counter()
        for _, ins in cluster:
            t = ins.get("target")
            b = ins.get("base")
            if t:
                opcodes[t.get("opcode", "?")] += 1
            if b:
                opcodes[b.get("opcode", "?")] += 1

        top_ops = ", ".join(f"{op}({c})" for op, c in opcodes.most_common(3))

        print(f"{'=' * 60}")
        print(f"Cluster {i+1}: idx {lo_idx}-{hi_idx} | "
              f"{size} instrs ({ins_count}I/{del_count}D) | ops: {top_ops}")
        print(f"{'=' * 60}")

        # Show with surrounding context
        show_lo = max(0, lo_idx - context)
        show_hi = min(len(instrs) - 1, hi_idx + context)
        cluster_indices = set(indices)
        for j in range(show_lo, show_hi + 1):
            if j < len(instrs):
                ins = instrs[j]
                highlight = ins["index"] in cluster_indices
                print_instr(ins, highlight=highlight, annotate=True)
        print()


# ── Register swaps mode ─────────────────────────────────────────────────────

def cmd_regswaps(instrs):
    """Analyze register swap pairs."""
    reg_swaps, _, _, _ = parse_breakdowns(instrs)
    if not reg_swaps:
        print("No register swaps found in diff_breakdown data.")
        return

    pair_data = compute_reg_swap_pairs(reg_swaps)

    print(f"Register swap analysis: {len(reg_swaps)} swapped args across "
          f"{len(pair_data)} pairs")
    print()

    # Separate GPR and FPR
    gpr_pairs = {}
    fpr_pairs = {}
    other_pairs = {}
    for pair, data in pair_data.items():
        p0, _ = pair
        if p0.startswith("r"):
            gpr_pairs[pair] = data
        elif p0.startswith("f"):
            fpr_pairs[pair] = data
        else:
            other_pairs[pair] = data

    def print_pair_table(pairs, label):
        if not pairs:
            return
        print(f"  {label}:")
        print(f"    {'Pair':>12s}  {'Count':>6s}  {'First':>6s}  {'Last':>6s}  {'Span':>6s}")
        print(f"    {'─' * 12}  {'─' * 6}  {'─' * 6}  {'─' * 6}  {'─' * 6}")
        for pair, data in sorted(pairs.items(), key=lambda x: -x[1]["count"]):
            p0, p1 = pair
            span = data["last"] - data["first"]
            print(f"    {p0:>4s} <-> {p1:<4s}  {data['count']:6d}  "
                  f"{data['first']:6d}  {data['last']:6d}  {span:6d}")
        total = sum(d["count"] for d in pairs.values())
        print(f"    {'Total':>12s}  {total:6d}")
        print()

    print_pair_table(gpr_pairs, "GPR (general purpose — may be fixable via declaration reorder)")
    print_pair_table(fpr_pairs, "FPR (floating point — usually unfixable)")
    print_pair_table(other_pairs, "Other")

    # Summary
    gpr_total = sum(d["count"] for d in gpr_pairs.values())
    fpr_total = sum(d["count"] for d in fpr_pairs.values())
    other_total = sum(d["count"] for d in other_pairs.values())
    print(f"Summary: {gpr_total} GPR + {fpr_total} FPR + {other_total} other "
          f"= {len(reg_swaps)} total register arg diffs")


# ── Offsets mode ────────────────────────────────────────────────────────────

def cmd_offsets(instrs):
    """Analyze offset/immediate differences."""
    _, offset_diffs, _, _ = parse_breakdowns(instrs)
    if not offset_diffs:
        print("No offset differences found in diff_breakdown data.")
        return

    delta_hist = compute_offset_histogram(offset_diffs)

    print(f"Offset analysis: {len(offset_diffs)} immediate/offset arg differences")
    print()

    # Histogram
    dominant_delta, dominant_count = delta_hist.most_common(1)[0]
    print("  Offset delta histogram (base - target):")
    print(f"    {'Delta':>8s}  {'Count':>6s}  {'Bar'}")
    print(f"    {'─' * 8}  {'─' * 6}  {'─' * 30}")
    max_count = dominant_count
    for delta, count in delta_hist.most_common(20):
        bar_len = int(30 * count / max_count) if max_count else 0
        bar = "█" * bar_len
        flag = " ◄ dominant" if delta == dominant_delta else ""
        print(f"    {delta:+8d}  {count:6d}  {bar}{flag}")

    total_explained = sum(delta_hist.values())
    print()
    print(f"  Total: {total_explained} offset arg diffs across "
          f"{len(delta_hist)} distinct deltas")
    print(f"  Dominant delta: {dominant_delta:+d} ({dominant_count} instructions, "
          f"{100.0 * dominant_count / total_explained:.1f}%)")

    # Find instructions with non-dominant deltas (interesting ones)
    print()
    print("-" * 60)
    print("OUTLIER OFFSETS (not the dominant delta)")
    print("-" * 60)

    outliers = [(idx, tv, bv, delta)
                for idx, tv, bv, delta in offset_diffs
                if delta != dominant_delta]

    if not outliers:
        print("  All offsets explained by dominant delta — likely pure stack frame shift.")
        return

    print(f"  {len(outliers)} instructions with non-dominant offset deltas:")
    print()

    # Group outliers by delta
    by_delta = defaultdict(list)
    for idx, tv, bv, delta in outliers:
        by_delta[delta].append((idx, tv, bv))

    for delta in sorted(by_delta.keys(), key=lambda d: -len(by_delta[d])):
        entries = by_delta[delta]
        print(f"  delta={delta:+d} ({len(entries)} instructions):")
        for idx, tv, bv in entries[:8]:
            ins = instrs[idx]
            t = ins.get("target", {})
            b = ins.get("base", {})
            print(f"    idx {idx:4d}: TGT {fmt_instr(t)[:50]}")
            print(f"             SRC {fmt_instr(b)[:50]}")
        if len(entries) > 8:
            print(f"    ... and {len(entries) - 8} more")
        print()


# ── Replaces mode ──────────────────────────────────────────────────────────

def cmd_replaces(instrs):
    """Categorize replace instructions into symbol-reloc noise vs real structural differences."""
    sym_noise, real_count, real_examples = categorize_replaces(instrs)
    total = sym_noise + real_count

    print(f"Replace breakdown: {total} total")
    print(f"  Symbol-reloc noise (SRC has extra sym arg): {sym_noise}")
    print(f"  Real structural replaces: {real_count}")
    print()

    if real_examples:
        print("Real replace examples:")
        for ins in real_examples:
            idx = ins["index"]
            t = ins.get("target", {})
            b = ins.get("base", {})
            print(f"  idx {idx:4d}: TGT {fmt_instr(t)}")
            print(f"           SRC {fmt_instr(b)}")


# ── Symbol invocation ───────────────────────────────────────────────────────

def run_objdiff_for_symbol(symbol, project_dir=None):
    """Run objdiff-cli diff and return path to JSON output."""
    # Deterministic filename from symbol
    h = hashlib.md5(symbol.encode()).hexdigest()[:12]
    # Also create a readable slug
    slug = re.sub(r'[^a-zA-Z0-9]+', '_', symbol)[:40].strip('_').lower()
    json_path = f"/tmp/claude/diff_{slug}_{h}.json"

    # Find project root (where objdiff.json lives)
    # objdiff binary is always resolved from the project root
    # (bin/ doesn't exist in worktrees)
    # Script is at scripts/analysis/diff_inspect.py, so go up 2 levels
    script_dir = os.path.dirname(os.path.abspath(__file__))
    script_repo_root = os.path.dirname(os.path.dirname(script_dir))
    objdiff_bin = os.path.join(script_repo_root, "bin", "objdiff-cli")

    if not os.path.exists(objdiff_bin):
        print(f"Error: objdiff-cli not found at {objdiff_bin}", file=sys.stderr)
        sys.exit(1)

    # Use project_dir for builds if specified, otherwise script's repo root
    effective_project_dir = project_dir if project_dir else script_repo_root

    print(f"Running objdiff for: {symbol}", file=sys.stderr)
    print(f"Output: {json_path}", file=sys.stderr)

    cmd = [
        objdiff_bin, "diff",
        "-p", str(effective_project_dir),
        symbol,
        "--include-instructions", "--build", "--incremental",
        "-f", "json", "-o", json_path
    ]

    result = subprocess.run(cmd, cwd=str(effective_project_dir),
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"objdiff-cli failed (exit {result.returncode}):", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        sys.exit(1)

    if result.stderr:
        # Print objdiff stderr (build progress etc) but don't fail
        print(result.stderr, file=sys.stderr, end="")

    return json_path


# ── Main ────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Inspect objdiff JSON diffs",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Analysis modes (pick one):
  --diagnose    Root cause analysis: why doesn't this match?
  --clusters    Group insert/delete into contiguous clusters
  --regswaps    Register swap pair analysis
  --offsets     Offset/immediate shift analysis
  --replaces    Categorize replaces (symbol-reloc noise vs real)
  --compare     Compare baseline and candidate JSONs (delta table)

Filter modes:
  diff_op       Only opcode mismatches
  replace       Only replaced instructions
  insert,delete Insert and delete instructions
  all           Every instruction
  --range N-M   Specific index range
  --summary     Match type counts
""")
    parser.add_argument(
        "json_file", nargs="?", default=None,
        help="Path to objdiff JSON output (optional if --symbol is used)")
    parser.add_argument(
        "match_types", nargs="?", default=None,
        help="Comma-separated match types to filter (e.g. diff_op,replace). "
        "'all' shows everything. Default: all non-equal types.")
    parser.add_argument(
        "-C", "--context", type=int, default=5,
        help="Lines of context around each match (default: 5)")
    parser.add_argument(
        "--range", type=str, default=None,
        help="Show specific index range (e.g. 950-970)")
    parser.add_argument(
        "--summary", action="store_true", help="Show match type counts only")
    parser.add_argument(
        "--diagnose", action="store_true",
        help="Root cause analysis mode")
    parser.add_argument(
        "--clusters", action="store_true",
        help="Show insert/delete clusters")
    parser.add_argument(
        "--regswaps", action="store_true",
        help="Register swap pair analysis")
    parser.add_argument(
        "--offsets", action="store_true",
        help="Offset/immediate shift analysis")
    parser.add_argument(
        "--replaces", action="store_true",
        help="Categorize replace instructions (noise vs real)")
    parser.add_argument(
        "--symbol", type=str, default=None,
        help="Run objdiff-cli diff internally for this symbol")
    parser.add_argument(
        "--project-dir", type=str, default=None,
        help="Project directory for objdiff builds (worktree support)")
    parser.add_argument(
        "--compare", nargs=2, metavar=("BASELINE_JSON", "CANDIDATE_JSON"),
        help="Compare two objdiff JSON files")
    parser.add_argument(
        "--hotspots", type=str,
        default="930-1075,1235-1415,3088-3290,3628-3642",
        help="Comma-separated hotspot ranges for --compare (default matches OnBeat campaign)")
    args = parser.parse_args()

    # Compare mode uses two JSON files directly.
    if args.compare:
        base_path, cand_path = args.compare
        with open(base_path) as f:
            base_data = json.load(f)
        with open(cand_path) as f:
            cand_data = json.load(f)
        cmd_compare(base_data, cand_data, args.hotspots)
        return

    # Resolve JSON input
    json_file = args.json_file
    if args.symbol:
        json_file = run_objdiff_for_symbol(args.symbol, project_dir=args.project_dir)
    if not json_file:
        parser.error("Either json_file or --symbol is required")

    with open(json_file) as f:
        data = json.load(f)

    instrs = data.get("instructions", [])
    if not instrs:
        print("No instructions found in JSON.", file=sys.stderr)
        sys.exit(1)

    # ── Analysis modes ──
    if args.diagnose:
        cmd_diagnose(instrs)
        return
    if args.clusters:
        cmd_clusters(instrs, context=args.context)
        return
    if args.regswaps:
        cmd_regswaps(instrs)
        return
    if args.offsets:
        cmd_offsets(instrs)
        return
    if args.replaces:
        cmd_replaces(instrs)
        return

    # ── Summary mode ──
    if args.summary:
        counts = Counter(ins["match_type"] for ins in instrs)
        total = len(instrs)
        print(f"Total instructions: {total}")
        print()
        for mt, count in counts.most_common():
            pct = 100.0 * count / total
            print(f"  {mt:12s}: {count:5d} ({pct:5.1f}%)")
        return

    # ── Range mode ──
    if args.range:
        lo, hi = args.range.split("-")
        lo, hi = int(lo), int(hi)
        for ins in instrs:
            idx = ins["index"]
            if lo <= idx <= hi:
                highlight = ins["match_type"] not in ("equal", "diff_arg")
                print_instr(ins, highlight=highlight, annotate=True)
        return

    # ── Filter mode ──
    if args.match_types == "all":
        for ins in instrs:
            highlight = ins["match_type"] not in ("equal", "diff_arg")
            print_instr(ins, highlight=highlight, annotate=True)
        return

    if args.match_types:
        wanted = set(args.match_types.split(","))
    else:
        wanted = {"diff_op", "replace", "insert", "delete"}

    # Find matching instructions and show with context
    matches = [ins for ins in instrs if ins["match_type"] in wanted]

    if not matches:
        print(f"No instructions with match type(s): {wanted}", file=sys.stderr)
        sys.exit(0)

    print(f"Found {len(matches)} instruction(s) matching {wanted}")
    print()

    # Group nearby matches to avoid redundant context
    printed = set()
    for match in matches:
        idx = match["index"]
        lo = max(0, idx - args.context)
        hi = min(len(instrs) - 1, idx + args.context)

        # Skip if already printed in a previous group's context
        if idx in printed:
            continue

        print(f"--- index {idx}: {match['match_type']} ---")
        for i in range(lo, hi + 1):
            ins = instrs[i]
            highlight = ins["index"] == idx or ins["match_type"] in wanted
            print_instr(ins, highlight=highlight, annotate=True)
            printed.add(ins["index"])
        print()


if __name__ == "__main__":
    main()
