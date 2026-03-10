#!/usr/bin/env python3
"""Classify callee-saved register swaps from cached objdiff JSON diffs.

Scans /tmp/claude/diff_*.json files (produced by run_objdiff) and:
1. Finds functions with callee-saved register swaps
2. Classifies each swap pair by variable type (param, addr, member load, etc.)
3. Estimates fixability (both user-controllable = maybe fixable)
4. Reports most common swap pairs

Usage:
    python3 scripts/analysis/regswap_classify.py [--min-pct 90] [--verbose]
"""
import json
import glob
import re
import argparse
from collections import Counter


CALLEE_SAVED_GPR = set(f'r{i}' for i in range(14, 32))
CALLEE_SAVED_FPR = set(f'f{i}' for i in range(14, 32))

WRITE_POS0_OPS = {
    'mr', 'addi', 'subi', 'lwz', 'lbz', 'lhz', 'li', 'lis',
    'fmr', 'fmuls', 'fadds', 'fsubs', 'fdivs', 'frsp', 'fcfid',
    'lfs', 'lfd', 'extsw', 'slwi', 'clrlwi', 'rlwinm', 'add',
    'subf', 'mullw', 'neg', 'ori', 'oris', 'xori', 'andi.',
    'srwi',
}

USER_TYPES = {'param_save', 'fparam_save'}
TEMP_TYPES = {'member_load', 'addr_compute', 'global_addr', 'const', 'fcompute'}


def classify_def(opcode):
    if opcode == 'mr':
        return 'param_save'
    elif opcode in ('lwz', 'lbz', 'lhz', 'lfs', 'lfd'):
        return 'member_load'
    elif opcode in ('addi', 'subi'):
        return 'addr_compute'
    elif opcode == 'fmr':
        return 'fparam_save'
    elif opcode in ('fmuls', 'fadds', 'fsubs', 'fdivs', 'frsp', 'fcfid'):
        return 'fcompute'
    elif opcode == 'li':
        return 'const'
    elif opcode == 'lis':
        return 'global_addr'
    else:
        return opcode


def find_swap_pairs(instrs):
    """Find all callee-saved register swap pairs."""
    pairs = set()
    for instr in instrs:
        if instr.get('match_type') != 'diff_arg':
            continue
        t_args = instr.get('target', {}).get('typed_args', [])
        b_args = instr.get('base', {}).get('typed_args', [])
        for ta, ba in zip(t_args, b_args):
            if ta.get('type') != 'Register' or ba.get('type') != 'Register':
                continue
            tr, br = ta['value'], ba['value']
            if tr == br:
                continue
            pair = tuple(sorted([tr, br]))
            both_cs = (pair[0] in CALLEE_SAVED_GPR and pair[1] in CALLEE_SAVED_GPR) or \
                      (pair[0] in CALLEE_SAVED_FPR and pair[1] in CALLEE_SAVED_FPR)
            if both_cs:
                pairs.add(pair)
    return pairs


def classify_pair(instrs, lo_reg, hi_reg):
    """Classify the defining opcode for each register in the target."""
    lo_type = hi_type = None
    lo_idx = hi_idx = None

    for i, instr in enumerate(instrs):
        t_args = instr.get('target', {}).get('typed_args', [])
        opcode = instr.get('target', {}).get('opcode', '')
        if t_args and t_args[0].get('type') == 'Register' and opcode in WRITE_POS0_OPS:
            r = t_args[0]['value']
            if r == lo_reg and lo_type is None:
                lo_type = classify_def(opcode)
                lo_idx = i
            if r == hi_reg and hi_type is None:
                hi_type = classify_def(opcode)
                hi_idx = i

    return lo_type, hi_type, lo_idx, hi_idx


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--min-pct', type=float, default=90.0,
                        help='Minimum match percentage (default: 90)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show per-function details')
    parser.add_argument('--diff-dir', default='/tmp/claude',
                        help='Directory containing diff_*.json files')
    args = parser.parse_args()

    diff_files = glob.glob(f'{args.diff_dir}/diff_*.json')
    if not diff_files:
        print(f"No diff files found in {args.diff_dir}/")
        return

    all_pairs_counter = Counter()
    single_pair_results = []
    multi_pair_count = 0
    total_cs_swap_functions = 0

    for df in diff_files:
        try:
            with open(df) as f:
                data = json.load(f)
        except Exception:
            continue

        instrs = data.get('instructions', [])
        pct = data.get('normalized_match_percent', 0)
        if not instrs or pct < args.min_pct:
            continue

        pairs = find_swap_pairs(instrs)
        if not pairs:
            continue

        total_cs_swap_functions += 1
        for p in pairs:
            all_pairs_counter[p] += 1

        sym = data.get('symbol', '')

        if len(pairs) == 1:
            pair = list(pairs)[0]
            lo_type, hi_type, lo_idx, hi_idx = classify_pair(instrs, pair[0], pair[1])
            single_pair_results.append((pct, pair, lo_type, hi_type, lo_idx, hi_idx, sym))
        else:
            multi_pair_count += 1

    single_pair_results.sort(key=lambda x: -x[0])

    print(f"Callee-Saved Register Swap Analysis")
    print(f"{'=' * 60}")
    print(f"Diff files scanned:     {len(diff_files)}")
    print(f"Functions with CS swap:  {total_cs_swap_functions}")
    print(f"  Single pair:           {len(single_pair_results)}")
    print(f"  Multi pair:            {multi_pair_count}")
    print()

    print("Most common callee-saved swap pairs:")
    for pair, cnt in all_pairs_counter.most_common(15):
        print(f"  {pair[0]:>4} <-> {pair[1]:<4}: {cnt:3d} functions")
    print()

    # Classification
    type_combos = Counter()
    user_fixable = both_temp = one_temp = 0
    for _, _, lo_t, hi_t, _, _, _ in single_pair_results:
        if lo_t and hi_t:
            type_combos[(lo_t, hi_t)] += 1
            lo_user = lo_t in USER_TYPES
            hi_user = hi_t in USER_TYPES
            if lo_user and hi_user:
                user_fixable += 1
            elif not lo_user and not hi_user:
                both_temp += 1
            else:
                one_temp += 1

    classified = user_fixable + both_temp + one_temp
    if classified > 0:
        print("Variable type classification (single-pair swaps):")
        for (lo_t, hi_t), cnt in type_combos.most_common(15):
            print(f"  {lo_t:15} x {hi_t:15} = {cnt:3d}")
        print()

        print("Fixability estimate:")
        print(f"  Both user-controllable:    {user_fixable:3d} ({100*user_fixable/classified:.0f}%) - maybe fixable via decl reorder")
        print(f"  One user + one temp:       {one_temp:3d} ({100*one_temp/classified:.0f}%) - unlikely fixable")
        print(f"  Both compiler-internal:    {both_temp:3d} ({100*both_temp/classified:.0f}%) - unfixable")
    print()

    if args.verbose:
        print(f"{'%':>6} {'Pair':10} {'Lo@':>4} {'Hi@':>4} {'Lo type':15} {'Hi type':15} Symbol")
        print(f"{'─'*6} {'─'*10} {'─'*4} {'─'*4} {'─'*15} {'─'*15} {'─'*50}")
        for pct, pair, lt, ht, li, hi, sym in single_pair_results[:50]:
            lt_s = lt or '?'
            ht_s = ht or '?'
            li_s = f'{li:4d}' if li is not None else '   ?'
            hi_s = f'{hi:4d}' if hi is not None else '   ?'
            print(f"{pct:5.1f}% {pair[0]}<->{pair[1]:4} {li_s} {hi_s} {lt_s:15} {ht_s:15} {sym[:50]}")


if __name__ == '__main__':
    main()
