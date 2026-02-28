#!/usr/bin/env python3
"""
Analyze .text section address drift between original and decomp MAP files.

Parses both MAP files, matches symbols by name, and reports:
- Per-function offset drift (how far each function shifted from original position)
- Drift distribution and acceleration
- Top object files contributing to growth
- Impact analysis for targeted fixes

Usage:
    python3 scripts/misc/analyze_text_drift.py
    python3 scripts/misc/analyze_text_drift.py --top 40 --decomp-only
"""
import argparse
import re
from collections import defaultdict
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
ORIG_MAP = PROJECT_ROOT / "orig/373307D9/ham_xbox_r.map"
DECOMP_MAP = PROJECT_ROOT / "build/373307D9/default.map"

# Original uses section 0005 for .text, decomp uses section 0098
ORIG_TEXT_SECTION = "0005"
DECOMP_TEXT_SECTION = "0098"

# Object file patterns that indicate our decomp code (original MAP uses lib:obj format)
DECOMP_LIB_PREFIXES = [
    "i char:", "i flow:", "i hamobj:", "i meta:", "i midi:", "i obj:",
    "i os:", "i rndobj:", "i rnddx9:", "i synth:", "i ui:", "i utl:",
    "i world:", "i math:", "i net:", "i net_ham:",
    "game:", "meta_ham:", "lazer:",
]

SDK_LIB_PREFIXES = [
    "nuispeech:", "nui:", "ST:", "d3d9", "xaudio2:", "xhv",
    "xapilibi:", "xonline:", "xgraphics:", "xboxkrnl",
    "libcpmt:", "libcmt:", "vcomp:", "msvcrt",
    "curl:", "zlib:",
]


def parse_map(path, text_section):
    """Parse MAP file, return list of (offset, name, va, obj) for .text function symbols."""
    symbols = []
    pattern = re.compile(
        rf'^\s*{text_section}:([0-9a-fA-F]+)\s+(\S+)\s+([0-9a-fA-F]+)\s+f\s+(.+)$'
    )
    with open(path) as f:
        for line in f:
            m = pattern.match(line)
            if m:
                offset = int(m.group(1), 16)
                name = m.group(2)
                va = int(m.group(3), 16)
                obj = m.group(4).strip()
                symbols.append((offset, name, va, obj))
    symbols.sort(key=lambda x: x[0])
    return symbols


def calc_sizes(symbols):
    """Calculate per-symbol sizes from offset gaps.

    NOTE: This is approximate — gaps may include padding or interleaved functions
    from other object files. Treat large deltas with suspicion.
    """
    sizes = {}
    for i, (offset, name, va, obj) in enumerate(symbols):
        if i + 1 < len(symbols):
            size = symbols[i + 1][0] - offset
        else:
            size = 0
        sizes[name] = (offset, size, obj)
    return sizes


def is_decomp_obj(obj):
    """Check if an object file is from our decomp code (not SDK)."""
    return any(obj.startswith(p) for p in DECOMP_LIB_PREFIXES)


def is_sdk_obj(obj):
    """Check if an object file is from SDK/third-party libraries."""
    return any(obj.startswith(p) for p in SDK_LIB_PREFIXES)


def main():
    parser = argparse.ArgumentParser(description="Analyze .text address drift")
    parser.add_argument("--top", type=int, default=30, help="Number of top results to show")
    parser.add_argument("--decomp-only", action="store_true", help="Only show decomp obj files")
    parser.add_argument("--sdk-only", action="store_true", help="Only show SDK obj files")
    args = parser.parse_args()

    print("Parsing MAP files...")
    orig_syms = parse_map(ORIG_MAP, ORIG_TEXT_SECTION)
    decomp_syms = parse_map(DECOMP_MAP, DECOMP_TEXT_SECTION)
    print(f"  Original: {len(orig_syms)} .text symbols")
    print(f"  Decomp:   {len(decomp_syms)} .text symbols")

    orig_base = orig_syms[0][2]   # VA of first .text symbol
    decomp_base = decomp_syms[0][2]
    print(f"  Original .text base: 0x{orig_base:08X}")
    print(f"  Decomp .text base:   0x{decomp_base:08X}")
    print(f"  Base VA delta:       0x{decomp_base - orig_base:X} ({decomp_base - orig_base:+,} bytes)")

    # Build lookup by name
    orig_by_name = {name: (off, va, obj) for off, name, va, obj in orig_syms}
    decomp_by_name = {name: (off, va, obj) for off, name, va, obj in decomp_syms}
    common = set(orig_by_name.keys()) & set(decomp_by_name.keys())
    print(f"  Matched symbols: {len(common)}")

    # ── SECTION 1: Offset Drift ──
    # For each matched symbol, drift = decomp_offset - orig_offset
    # This shows how far the function moved relative to .text start
    print(f"\n{'='*80}")
    print(f"OFFSET DRIFT (decomp offset - original offset per function)")
    print(f"{'='*80}")

    drifts = []
    for off, name, va, obj in orig_syms:
        if name in decomp_by_name:
            d_off, d_va, d_obj = decomp_by_name[name]
            drift = d_off - off
            drifts.append((off, name, drift, obj))

    if drifts:
        drift_vals = [d for _, _, d, _ in drifts]
        print(f"  Functions tracked: {len(drifts)}")
        print(f"  Min drift:  {min(drift_vals):+,} bytes")
        print(f"  Max drift:  {max(drift_vals):+,} bytes")
        print(f"  Mean drift: {sum(drift_vals)/len(drift_vals):+,.0f} bytes")

        # Distribution
        buckets = {"0": 0, "1-16": 0, "17-256": 0, "257-4K": 0, "4K-64K": 0, ">64K": 0}
        for d in drift_vals:
            ad = abs(d)
            if ad == 0: buckets["0"] += 1
            elif ad <= 16: buckets["1-16"] += 1
            elif ad <= 256: buckets["17-256"] += 1
            elif ad <= 4096: buckets["257-4K"] += 1
            elif ad <= 65536: buckets["4K-64K"] += 1
            else: buckets[">64K"] += 1

        print(f"\n  Drift distribution:")
        for label, count in buckets.items():
            pct = 100 * count / len(drift_vals) if drift_vals else 0
            print(f"    {label:>10}: {count:>6} ({pct:>5.1f}%)")

    # ── SECTION 2: Cumulative Drift Curve ──
    print(f"\n{'='*80}")
    print(f"CUMULATIVE DRIFT CURVE (sampled at intervals)")
    print(f"{'='*80}")

    if drifts:
        step = max(1, len(drifts) // 25)
        print(f"{'Orig Offset':>12} {'Drift':>10} {'Obj':>30}  {'Symbol'}")
        print(f"{'-'*12} {'-'*10} {'-'*30}  {'-'*40}")
        for i in range(0, len(drifts), step):
            off, name, drift, obj = drifts[i]
            obj_short = obj[:30]
            name_short = name[:40]
            print(f"  0x{off:08x} {drift:>+10}  {obj_short:>30}  {name_short}")
        off, name, drift, obj = drifts[-1]
        print(f"  0x{off:08x} {drift:>+10}  {obj[:30]:>30}  {name[:40]} [END]")

    # ── SECTION 3: Size Deltas (approximate) ──
    print(f"\n{'='*80}")
    print(f"SIZE DELTAS (approximate, from MAP symbol gaps)")
    print(f"{'='*80}")
    print(f"NOTE: Large deltas may be artifacts of different link ordering.")
    print(f"      Only trust deltas where both sizes are reasonable (<10KB).\n")

    orig_sizes = calc_sizes(orig_syms)
    decomp_sizes = calc_sizes(decomp_syms)

    # Calculate per-function size deltas (filtered to reasonable sizes)
    deltas = []
    for name in common:
        o_off, o_size, o_obj = orig_sizes[name]
        d_off, d_size, d_obj = decomp_sizes[name]
        if o_size > 0 and d_size > 0:
            # Filter by requested category
            if args.decomp_only and not is_decomp_obj(o_obj):
                continue
            if args.sdk_only and not is_sdk_obj(o_obj):
                continue
            delta = d_size - o_size
            deltas.append((name, o_size, d_size, delta, o_obj, o_off))

    # Trusted deltas: both sizes < 10KB (unlikely to be MAP artifacts)
    trusted = [(n, os, ds, d, o, oo) for n, os, ds, d, o, oo in deltas if os < 10000 and ds < 10000]

    print(f"All deltas: {len(deltas)} functions")
    print(f"Trusted deltas (both sizes <10KB): {len(trusted)} functions")

    if trusted:
        exact = sum(1 for _, _, _, d, _, _ in trusted if d == 0)
        print(f"  Exact size match: {exact} ({100*exact/len(trusted):.1f}%)")

        total_trusted_delta = sum(d for _, _, _, d, _, _ in trusted)
        print(f"  Net trusted delta: {total_trusted_delta:+,} bytes")

    # Top mismatches (trusted only)
    trusted.sort(key=lambda x: abs(x[3]), reverse=True)
    print(f"\nTop {args.top} trusted size mismatches:")
    print(f"{'Delta':>8} {'Orig':>6} {'Decomp':>6} {'Offset':>12}  {'Object':>30}  {'Symbol'}")
    print(f"{'-'*8} {'-'*6} {'-'*6} {'-'*12}  {'-'*30}  {'-'*40}")
    for name, o_size, d_size, delta, obj, o_off in trusted[:args.top]:
        if delta == 0:
            break
        print(f"{delta:>+8} {o_size:>6} {d_size:>6} 0x{o_off:08x}  {obj[:30]:>30}  {name[:50]}")

    # ── SECTION 4: Per-Object File Aggregation ──
    print(f"\n{'='*80}")
    print(f"TOP OBJECT FILES BY TOTAL TRUSTED SIZE DELTA")
    print(f"{'='*80}")

    obj_agg = defaultdict(lambda: {"delta": 0, "count": 0, "mismatched": 0})
    for name, o_size, d_size, delta, obj, o_off in trusted:
        obj_agg[obj]["delta"] += delta
        obj_agg[obj]["count"] += 1
        if delta != 0:
            obj_agg[obj]["mismatched"] += 1

    sorted_objs = sorted(obj_agg.items(), key=lambda x: abs(x[1]["delta"]), reverse=True)
    print(f"{'Delta':>8} {'Funcs':>6} {'Mismatch':>8}  {'Object file'}")
    print(f"{'-'*8} {'-'*6} {'-'*8}  {'-'*50}")
    for obj, info in sorted_objs[:args.top]:
        if info["delta"] == 0:
            break
        tag = ""
        if is_decomp_obj(obj): tag = " [DECOMP]"
        elif is_sdk_obj(obj): tag = " [SDK]"
        print(f"{info['delta']:>+8} {info['count']:>6} {info['mismatched']:>8}  {obj}{tag}")

    # ── SECTION 5: Decomp-only drift ──
    decomp_drifts = [(off, name, drift, obj) for off, name, drift, obj in drifts if is_decomp_obj(obj)]
    if decomp_drifts:
        print(f"\n{'='*80}")
        print(f"DECOMP-ONLY FUNCTIONS DRIFT")
        print(f"{'='*80}")
        d_vals = [d for _, _, d, _ in decomp_drifts]
        print(f"  Count: {len(decomp_drifts)}")
        print(f"  Min drift:  {min(d_vals):+,} bytes")
        print(f"  Max drift:  {max(d_vals):+,} bytes")
        print(f"  Mean drift: {sum(d_vals)/len(d_vals):+,.0f} bytes")

        # What % have <256 byte drift?
        under_256 = sum(1 for d in d_vals if abs(d) < 256)
        under_1k = sum(1 for d in d_vals if abs(d) < 1024)
        print(f"  <256B drift: {under_256} ({100*under_256/len(d_vals):.1f}%)")
        print(f"  <1KB drift:  {under_1k} ({100*under_1k/len(d_vals):.1f}%)")


if __name__ == "__main__":
    main()
