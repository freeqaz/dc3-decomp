#!/usr/bin/env python3
"""Cluster AT_LIMIT functions by mismatch signature to find systemic header issues.

Scans cached diff_*.json files and groups functions by:
1. Prologue delta (savegprlr N difference)
2. Insert/delete cluster opcodes (inlined function fingerprint)
3. Offset shift patterns (struct size issues)
4. Replace instruction patterns (different codegen for same construct)

Large clusters point to shared header-level root causes.

Usage:
    python3 scripts/analysis/mismatch_cluster.py [--diff-dir /tmp/claude] [--min-cluster 3]
    python3 scripts/analysis/mismatch_cluster.py --mode insert-clusters
    python3 scripts/analysis/mismatch_cluster.py --mode offset-shifts
    python3 scripts/analysis/mismatch_cluster.py --mode prologue
    python3 scripts/analysis/mismatch_cluster.py --mode call-targets
    python3 scripts/analysis/mismatch_cluster.py --mode all
"""
import json
import glob
import re
import argparse
from collections import Counter, defaultdict


def load_diffs(diff_dir, min_pct=0):
    """Load all diff JSON files."""
    results = []
    for path in glob.glob(f'{diff_dir}/diff_*.json'):
        try:
            with open(path) as f:
                data = json.load(f)
        except Exception:
            continue
        pct = data.get('normalized_match_percent', 0)
        if pct < min_pct or pct >= 100:
            continue
        results.append(data)
    return results


def extract_prologue_delta(data):
    """Extract savegprlr prologue difference."""
    instrs = data.get('instructions', [])
    for ins in instrs[:10]:  # prologue is in first few instructions
        if ins.get('match_type') != 'diff_arg':
            continue
        bd = ins.get('diff_breakdown', {})
        args = bd.get('arguments', [])
        for a in args:
            if a.get('arg_type') != 'symbol':
                continue
            tv = str(a.get('target', {}).get('value', ''))
            bv = str(a.get('base', {}).get('value', ''))
            tm = re.match(r'__savegprlr_(\d+)', tv)
            bm = re.match(r'__savegprlr_(\d+)', bv)
            if tm and bm:
                return int(tm.group(1)) - int(bm.group(1))
            tm = re.match(r'__savefpr_(\d+)', tv)
            bm = re.match(r'__savefpr_(\d+)', bv)
            if tm and bm:
                return int(tm.group(1)) - int(bm.group(1))
    return None


def extract_insert_delete_clusters(data):
    """Extract contiguous insert/delete groups as opcode fingerprints."""
    instrs = data.get('instructions', [])
    clusters = []
    current = []
    current_type = None

    for ins in instrs:
        mt = ins.get('match_type')
        if mt in ('insert', 'delete'):
            if mt != current_type and current:
                clusters.append((current_type, tuple(current)))
                current = []
            current_type = mt
            # Get opcode from whichever side exists
            if mt == 'insert':
                op = ins.get('base', {}).get('opcode', '?')
            else:
                op = ins.get('target', {}).get('opcode', '?')
            current.append(op)
        else:
            if current:
                clusters.append((current_type, tuple(current)))
                current = []
                current_type = None

    if current:
        clusters.append((current_type, tuple(current)))

    return clusters


def extract_detailed_clusters(data):
    """Extract insert/delete clusters with full instruction details."""
    instrs = data.get('instructions', [])
    clusters = []
    current = []
    current_type = None

    for idx, ins in enumerate(instrs):
        mt = ins.get('match_type')
        if mt in ('insert', 'delete'):
            if mt != current_type and current:
                clusters.append((current_type, current[:]))
                current = []
            current_type = mt
            side = ins.get('base', {}) if mt == 'insert' else ins.get('target', {})
            op = side.get('opcode', '?')
            args = [a.get('value', '') for a in side.get('typed_args', [])]
            current.append((idx, op, args))
        else:
            if current:
                clusters.append((current_type, current[:]))
                current = []
                current_type = None

    if current:
        clusters.append((current_type, current[:]))

    return clusters


def extract_offset_deltas(data):
    """Extract stack/memory offset shift deltas."""
    instrs = data.get('instructions', [])
    deltas = []
    for ins in instrs:
        bd = ins.get('diff_breakdown', {})
        args = bd.get('arguments', [])
        for a in args:
            if a.get('arg_type') != 'immediate':
                continue
            tv = a.get('target', {}).get('value', 0)
            bv = a.get('base', {}).get('value', 0)
            try:
                tv_int = int(tv)
                bv_int = int(bv)
            except (TypeError, ValueError):
                continue
            d = tv_int - bv_int
            if d != 0:
                deltas.append(d)
    return deltas


def extract_call_target_diffs(data):
    """Extract differing call targets (bl instruction symbol differences)."""
    instrs = data.get('instructions', [])
    diffs = []
    for ins in instrs:
        if ins.get('match_type') != 'diff_arg':
            continue
        t = ins.get('target', {})
        b = ins.get('base', {})
        if t.get('opcode') == 'bl' and b.get('opcode') == 'bl':
            t_args = t.get('typed_args', [])
            b_args = b.get('typed_args', [])
            if t_args and b_args:
                tv = t_args[0].get('value', '')
                bv = b_args[0].get('value', '')
                if tv != bv:
                    diffs.append((tv, bv))
    return diffs


def extract_replace_fingerprint(data):
    """Extract replace instruction pairs as fingerprints."""
    instrs = data.get('instructions', [])
    replaces = []
    for ins in instrs:
        if ins.get('match_type') != 'replace':
            continue
        t_op = ins.get('target', {}).get('opcode', '?')
        b_op = ins.get('base', {}).get('opcode', '?')
        replaces.append((t_op, b_op))
    return replaces


def get_unit(data):
    """Extract unit path from symbol or demangled name."""
    # The unit info isn't directly in the diff JSON, but we can infer from symbol
    return data.get('demangled', data.get('symbol', ''))[:60]


def analyze_prologue(diffs, min_cluster):
    """Cluster by prologue delta."""
    print("\n" + "=" * 70)
    print("PROLOGUE DELTA CLUSTERS")
    print("Functions grouped by savegprlr register count difference")
    print("=" * 70)

    by_delta = defaultdict(list)
    for d in diffs:
        delta = extract_prologue_delta(d)
        if delta is not None:
            by_delta[delta].append(d)

    for delta in sorted(by_delta.keys(), key=lambda x: -len(by_delta[x])):
        funcs = by_delta[delta]
        if len(funcs) < min_cluster:
            continue
        direction = "MORE" if delta < 0 else "FEWER"
        print(f"\n  Delta {delta:+d} ({direction} callee-saved regs in base): {len(funcs)} functions")
        # Show top functions by match %
        funcs.sort(key=lambda x: -x.get('normalized_match_percent', 0))
        for f in funcs[:8]:
            sym = f.get('demangled', f.get('symbol', ''))[:70]
            pct = f.get('normalized_match_percent', 0)
            print(f"    {pct:5.1f}%  {sym}")
        if len(funcs) > 8:
            print(f"    ... and {len(funcs) - 8} more")

    if not by_delta:
        print("  No prologue deltas found in cached diffs.")


def analyze_insert_clusters(diffs, min_cluster):
    """Cluster by insert/delete opcode fingerprints."""
    print("\n" + "=" * 70)
    print("INSERT/DELETE CLUSTER FINGERPRINTS")
    print("Functions grouped by contiguous insert/delete opcode patterns")
    print("These point to differing inlined header functions")
    print("=" * 70)

    # Collect all cluster fingerprints
    fingerprint_to_funcs = defaultdict(list)
    for d in diffs:
        clusters = extract_insert_delete_clusters(d)
        for ctype, opcodes in clusters:
            if len(opcodes) >= 2:  # Only meaningful clusters
                key = (ctype, opcodes)
                fingerprint_to_funcs[key].append(d)

    # Sort by cluster size
    sorted_fps = sorted(fingerprint_to_funcs.items(), key=lambda x: -len(x[1]))
    shown = 0
    for (ctype, opcodes), funcs in sorted_fps:
        if len(funcs) < min_cluster:
            break
        shown += 1
        if shown > 25:
            break
        print(f"\n  {ctype.upper()} cluster [{len(opcodes)} insns]: {len(funcs)} functions")
        print(f"    Opcodes: {' '.join(opcodes[:20])}")
        # Deduplicate by symbol
        seen = set()
        for f in sorted(funcs, key=lambda x: -x.get('normalized_match_percent', 0)):
            sym = f.get('symbol', '')
            if sym in seen:
                continue
            seen.add(sym)
            pct = f.get('normalized_match_percent', 0)
            dem = f.get('demangled', sym)[:70]
            print(f"    {pct:5.1f}%  {dem}")
            if len(seen) >= 5:
                break
        remaining = len(funcs) - len(seen)
        if remaining > 0:
            print(f"    ... and {remaining} more")

    if shown == 0:
        print("  No significant insert/delete clusters found.")


def analyze_offset_shifts(diffs, min_cluster):
    """Cluster by dominant offset delta."""
    print("\n" + "=" * 70)
    print("OFFSET SHIFT CLUSTERS")
    print("Functions grouped by dominant stack/memory offset delta")
    print("Consistent deltas point to struct size disagreements")
    print("=" * 70)

    by_delta = defaultdict(list)
    for d in diffs:
        deltas = extract_offset_deltas(d)
        if not deltas:
            continue
        # Find dominant delta
        c = Counter(deltas)
        dominant, count = c.most_common(1)[0]
        if count >= 2:  # At least 2 instructions with same delta
            by_delta[dominant].append((d, count))

    for delta in sorted(by_delta.keys(), key=lambda x: -len(by_delta[x])):
        entries = by_delta[delta]
        if len(entries) < min_cluster:
            continue
        print(f"\n  Offset delta {delta:+d} (0x{abs(delta):x}): {len(entries)} functions")
        entries.sort(key=lambda x: -x[0].get('normalized_match_percent', 0))
        for d, cnt in entries[:8]:
            sym = d.get('demangled', d.get('symbol', ''))[:65]
            pct = d.get('normalized_match_percent', 0)
            print(f"    {pct:5.1f}% [{cnt:2d} shifted insns]  {sym}")
        if len(entries) > 8:
            print(f"    ... and {len(entries) - 8} more")


def analyze_call_targets(diffs, min_cluster):
    """Cluster by differing call targets."""
    print("\n" + "=" * 70)
    print("CALL TARGET DIVERGENCE")
    print("Functions where target calls a different function than base")
    print("Points to ICF, wrong function calls, or vtable issues")
    print("=" * 70)

    pair_to_funcs = defaultdict(list)
    for d in diffs:
        call_diffs = extract_call_target_diffs(d)
        for tv, bv in call_diffs:
            pair_to_funcs[(tv, bv)].append(d)

    sorted_pairs = sorted(pair_to_funcs.items(), key=lambda x: -len(x[1]))
    shown = 0
    for (tv, bv), funcs in sorted_pairs:
        if len(funcs) < min_cluster:
            break
        shown += 1
        if shown > 20:
            break
        # Shorten names
        tv_short = tv[:50]
        bv_short = bv[:50]
        print(f"\n  Target calls: {tv_short}")
        print(f"  Base calls:   {bv_short}")
        print(f"  Affected: {len(funcs)} functions")
        for f in sorted(funcs, key=lambda x: -x.get('normalized_match_percent', 0))[:5]:
            pct = f.get('normalized_match_percent', 0)
            dem = f.get('demangled', f.get('symbol', ''))[:70]
            print(f"    {pct:5.1f}%  {dem}")
        if len(funcs) > 5:
            print(f"    ... and {len(funcs) - 5} more")

    if shown == 0:
        print("  No significant call target divergences found.")


def analyze_replace_patterns(diffs, min_cluster):
    """Cluster by replace instruction opcode pairs."""
    print("\n" + "=" * 70)
    print("REPLACE INSTRUCTION PATTERNS")
    print("Functions where target uses different opcode than base")
    print("Points to different code generation for same construct")
    print("=" * 70)

    pair_to_funcs = defaultdict(list)
    for d in diffs:
        replaces = extract_replace_fingerprint(d)
        for pair in replaces:
            pair_to_funcs[pair].append(d)

    sorted_pairs = sorted(pair_to_funcs.items(), key=lambda x: -len(x[1]))
    shown = 0
    for (t_op, b_op), funcs in sorted_pairs:
        if len(funcs) < min_cluster:
            break
        shown += 1
        if shown > 15:
            break
        print(f"\n  Target: {t_op:12s}  Base: {b_op:12s}  ({len(funcs)} functions)")
        seen = set()
        for f in sorted(funcs, key=lambda x: -x.get('normalized_match_percent', 0)):
            sym = f.get('symbol', '')
            if sym in seen:
                continue
            seen.add(sym)
            pct = f.get('normalized_match_percent', 0)
            dem = f.get('demangled', sym)[:70]
            print(f"    {pct:5.1f}%  {dem}")
            if len(seen) >= 5:
                break
        if len(funcs) > 5:
            print(f"    ... and {len(funcs) - 5} more")

    if shown == 0:
        print("  No significant replace patterns found.")


def analyze_detailed_inserts(diffs, min_cluster):
    """Show detailed insert/delete clusters with instruction args to identify inlined functions."""
    print("\n" + "=" * 70)
    print("DETAILED INSERT/DELETE ANALYSIS")
    print("Identifies what code is being inserted/deleted across functions")
    print("=" * 70)

    # Group by longer clusters (3+ instructions) to find meaningful patterns
    pattern_to_funcs = defaultdict(list)
    for d in diffs:
        clusters = extract_detailed_clusters(d)
        for ctype, entries in clusters:
            if len(entries) < 3:
                continue
            # Build a fingerprint from opcodes + key args
            fp_parts = []
            for idx, op, args in entries:
                # Include symbol args (function names, string literals)
                sym_args = [a for a in args if isinstance(a, str) and
                           (a.startswith('?') or a.startswith('__') or
                            a.startswith('merged_') or a.startswith('??'))]
                if sym_args:
                    fp_parts.append(f"{op}({sym_args[0][:30]})")
                else:
                    fp_parts.append(op)
            key = (ctype, tuple(fp_parts))
            pattern_to_funcs[key].append((d, entries))

    sorted_patterns = sorted(pattern_to_funcs.items(), key=lambda x: -len(x[1]))
    shown = 0
    for (ctype, fp), func_entries in sorted_patterns:
        if len(func_entries) < min_cluster:
            break
        shown += 1
        if shown > 20:
            break
        print(f"\n  {ctype.upper()} [{len(fp)} insns]: {len(func_entries)} functions")
        print(f"    Pattern: {' | '.join(fp[:15])}")
        # Show one representative's full instructions
        _, first_entries = func_entries[0]
        for idx, op, args in first_entries[:8]:
            args_str = ', '.join(str(a) for a in args)
            print(f"      [{idx:3d}] {op:12s} {args_str}")
        if len(first_entries) > 8:
            print(f"      ... ({len(first_entries) - 8} more instructions)")
        # List affected functions
        print(f"    Affected functions:")
        seen = set()
        for d, _ in sorted(func_entries, key=lambda x: -x[0].get('normalized_match_percent', 0)):
            sym = d.get('symbol', '')
            if sym in seen:
                continue
            seen.add(sym)
            pct = d.get('normalized_match_percent', 0)
            dem = d.get('demangled', sym)[:70]
            print(f"      {pct:5.1f}%  {dem}")
            if len(seen) >= 5:
                break
        if len(func_entries) > len(seen):
            print(f"      ... and {len(func_entries) - len(seen)} more")


def analyze_prologue_offset_correlation(diffs, min_cluster):
    """Cross-reference prologue deltas with offset shifts to find true struct issues."""
    print("\n" + "=" * 70)
    print("PROLOGUE vs OFFSET CORRELATION")
    print("Functions with offset shifts NOT explained by prologue differences")
    print("These likely indicate struct size disagreements")
    print("=" * 70)

    unexplained = []
    for d in diffs:
        pro_delta = extract_prologue_delta(d)
        off_deltas = extract_offset_deltas(d)
        if not off_deltas:
            continue
        c = Counter(off_deltas)
        dominant, count = c.most_common(1)[0]
        if count < 2:
            continue

        # If prologue delta explains the offset shift, skip
        # Each GPR = 4 bytes of stack; but stack frame alignment rounds to 16
        if pro_delta is not None:
            expected_shift = pro_delta * -4  # more regs saved = higher offsets
            if dominant == expected_shift:
                continue

        pct = d.get('normalized_match_percent', 0)
        sym = d.get('demangled', d.get('symbol', ''))
        unexplained.append((pct, dominant, count, pro_delta, sym))

    unexplained.sort(key=lambda x: -x[0])

    # Group by dominant offset
    by_offset = defaultdict(list)
    for entry in unexplained:
        by_offset[entry[1]].append(entry)

    for offset in sorted(by_offset.keys(), key=lambda x: -len(by_offset[x])):
        entries = by_offset[offset]
        if len(entries) < min_cluster:
            continue
        pro_deltas = Counter(e[3] for e in entries)
        print(f"\n  Offset {offset:+d} (0x{abs(offset):x}): {len(entries)} functions (no matching prologue)")
        print(f"    Prologue deltas seen: {dict(pro_deltas)}")
        for pct, off, cnt, pro, sym in entries[:6]:
            pro_s = f"pro={pro:+d}" if pro is not None else "pro=?"
            print(f"    {pct:5.1f}% [{cnt:2d} shifted] {pro_s:8s}  {sym[:60]}")
        if len(entries) > 6:
            print(f"    ... and {len(entries) - 6} more")

    if not unexplained:
        print("  All offset shifts can be explained by prologue differences.")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--diff-dir', default='/tmp/claude',
                        help='Directory containing diff_*.json files')
    parser.add_argument('--min-cluster', type=int, default=3,
                        help='Minimum cluster size to report (default: 3)')
    parser.add_argument('--min-pct', type=float, default=50,
                        help='Minimum match percentage to include (default: 50)')
    parser.add_argument('--mode', default='all',
                        choices=['all', 'prologue', 'insert-clusters',
                                 'offset-shifts', 'call-targets', 'replaces',
                                 'detailed-inserts', 'prologue-offset-corr'],
                        help='Analysis mode (default: all)')
    args = parser.parse_args()

    diffs = load_diffs(args.diff_dir, args.min_pct)
    print(f"Loaded {len(diffs)} diff files (>= {args.min_pct}% match)")

    if args.mode in ('all', 'prologue'):
        analyze_prologue(diffs, args.min_cluster)
    if args.mode in ('all', 'insert-clusters'):
        analyze_insert_clusters(diffs, args.min_cluster)
    if args.mode in ('all', 'offset-shifts'):
        analyze_offset_shifts(diffs, args.min_cluster)
    if args.mode in ('all', 'call-targets'):
        analyze_call_targets(diffs, args.min_cluster)
    if args.mode in ('all', 'replaces'):
        analyze_replace_patterns(diffs, args.min_cluster)
    if args.mode in ('all', 'detailed-inserts'):
        analyze_detailed_inserts(diffs, args.min_cluster)
    if args.mode in ('all', 'prologue-offset-corr'):
        analyze_prologue_offset_correlation(diffs, args.min_cluster)


if __name__ == '__main__':
    main()
