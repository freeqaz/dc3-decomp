#!/usr/bin/env python3
"""
Compare a linked PE against the original ham_xbox_r.exe.

Analyzes sections, .text byte differences, and uses the original MAP file
for function-level comparison with anchor-based drift tracking.

The raw byte comparison is misleading (~8%) because:
1. Linker reorders sections, causing a VA shift in .text
2. Small function size differences cause cumulative positional drift
3. All relocations (ADDR32, REL24, REFHI/REFLO) differ due to drift

The anchor-based comparison:
1. Finds large functions (>256 bytes) by matching body instruction patterns
   (skips the first 12 prologue instructions, matches 32 body instructions)
2. Tracks drift sequentially with monotonicity enforcement
3. Interpolates drift between anchors for all functions
4. Reports full instruction match and opcode-level match (relocation-adjusted)

Usage:
    python3 scripts/build/compare_pe.py [linked_pe] [--map MAP_FILE]
"""

import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def parse_pe(path):
    """Parse PE headers and sections."""
    with open(path, 'rb') as f:
        data = f.read()

    if data[:2] != b'MZ':
        sys.exit(f"{path}: Not a PE file (no MZ signature)")

    pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe_offset:pe_offset+4] != b'PE\x00\x00':
        sys.exit(f"{path}: Not a PE file (no PE signature)")

    # COFF header
    machine, num_sections, timestamp, symtab_ptr, num_symbols, opt_hdr_size, chars = \
        struct.unpack_from('<HHIIIHH', data, pe_offset + 4)

    # Optional header
    opt_offset = pe_offset + 24
    magic = struct.unpack_from('<H', data, opt_offset)[0]
    image_base = 0
    entry_rva = 0

    if magic == 0x10B:  # PE32
        entry_rva = struct.unpack_from('<I', data, opt_offset + 16)[0]
        image_base = struct.unpack_from('<I', data, opt_offset + 28)[0]

    # Sections
    sec_offset = opt_offset + opt_hdr_size
    sections = []
    for i in range(num_sections):
        off = sec_offset + i * 40
        name = data[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        vsize, vaddr, raw_size, raw_ptr = struct.unpack_from('<IIII', data, off + 8)
        flags = struct.unpack_from('<I', data, off + 36)[0]
        sections.append({
            'name': name,
            'vaddr': vaddr,
            'vsize': vsize,
            'raw_ptr': raw_ptr,
            'raw_size': raw_size,
            'flags': flags,
        })

    return {
        'path': path,
        'data': data,
        'size': len(data),
        'machine': machine,
        'num_sections': num_sections,
        'timestamp': timestamp,
        'image_base': image_base,
        'entry_rva': entry_rva,
        'sections': sections,
    }


def get_section(pe, name):
    """Get section by name."""
    return next((s for s in pe['sections'] if s['name'] == name), None)


def section_data(pe, sec):
    """Get raw data for a section."""
    return pe['data'][sec['raw_ptr']:sec['raw_ptr'] + sec['raw_size']]


def compare_bytes(data1, data2):
    """Compare two byte sequences, return diff statistics."""
    min_size = min(len(data1), len(data2))
    diffs = 0
    first_diff = -1
    diff_regions = []
    in_diff = False
    diff_start = 0

    for i in range(min_size):
        if data1[i] != data2[i]:
            if first_diff == -1:
                first_diff = i
            if not in_diff:
                diff_start = i
                in_diff = True
            diffs += 1
        else:
            if in_diff:
                diff_regions.append((diff_start, i))
                in_diff = False

    if in_diff:
        diff_regions.append((diff_start, min_size))

    return {
        'total_bytes': min_size,
        'diff_bytes': diffs,
        'match_pct': (1 - diffs / min_size) * 100 if min_size > 0 else 100,
        'first_diff': first_diff,
        'diff_regions': diff_regions,
        'size_diff': len(data1) - len(data2),
    }


def parse_map_functions(map_path, section_idx=5):
    """Parse MSVC MAP file to extract .text function entries.

    Returns sorted list of (name, offset, size) tuples.
    The section_idx defaults to 5 which is .text in the original PE MAP.
    """
    prefix = f" {section_idx:04d}:"
    functions = []

    with open(map_path) as f:
        for line in f:
            if prefix not in line or ' f ' not in line:
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            sec_off = parts[0]
            try:
                offset = int(sec_off.split(':')[1], 16)
            except (ValueError, IndexError):
                continue
            name = parts[1]
            functions.append((name, offset))

    functions.sort(key=lambda x: x[1])

    # Compute sizes from consecutive offsets
    result = []
    for i, (name, off) in enumerate(functions):
        if i + 1 < len(functions):
            size = functions[i + 1][1] - off
        else:
            size = 64  # last function, use conservative estimate
        if size > 0:
            result.append((name, off, size))

    return result


def find_anchors(orig_data, link_data, functions, min_size=256):
    """Find anchor functions using sequential tracking with body fingerprints.

    Processes functions in order, using the last matched anchor's drift
    to estimate the next search position. Enforces monotonicity online
    to reject false matches.

    Uses instructions 12-44 (skipping prologue) with 32-instruction
    fingerprint for high uniqueness.

    Returns sorted list of (orig_offset, link_offset) tuples.
    """
    FP_SKIP = 12  # skip 12 prologue/setup instructions
    FP_LEN = 32   # match 32 body instructions
    SEARCH_RANGE = 2048

    anchors = []
    last_drift = 0  # start with drift=0 at beginning of .text
    last_link_off = -1  # for monotonicity enforcement

    for name, off, size in functions:
        if size < min_size:
            continue

        fp_start = off + FP_SKIP * 4
        if fp_start + FP_LEN * 4 > len(orig_data):
            continue

        # Build body fingerprint (upper halfwords of instructions 12-44)
        orig_fp = []
        for i in range(FP_LEN):
            orig_fp.append(struct.unpack_from('>H', orig_data, fp_start + i * 4)[0])

        # Search centered on last known drift
        search_center = off + last_drift
        search_start = max(0, search_center - SEARCH_RANGE)
        search_end = min(len(link_data) - (FP_SKIP + FP_LEN) * 4,
                         search_center + SEARCH_RANGE)

        best_candidate = None
        best_dist = SEARCH_RANGE + 1

        for candidate in range(search_start, search_end, 4):
            # Enforce monotonicity: must be after last anchor
            if candidate <= last_link_off:
                continue

            cand_fp_start = candidate + FP_SKIP * 4

            # Quick reject: first body instruction
            if struct.unpack_from('>H', link_data, cand_fp_start)[0] != orig_fp[0]:
                continue

            # Full body check
            match = True
            for i in range(1, FP_LEN):
                if struct.unpack_from('>H', link_data, cand_fp_start + i * 4)[0] != orig_fp[i]:
                    match = False
                    break

            if match:
                dist = abs(candidate - search_center)
                if dist < best_dist:
                    best_dist = dist
                    best_candidate = candidate

        if best_candidate is not None:
            anchors.append((off, best_candidate))
            last_drift = best_candidate - off
            last_link_off = best_candidate

    return anchors


def interpolate_drift(anchors, offset, total_orig, total_link):
    """Interpolate drift at a given offset using anchor points.

    Uses piecewise linear interpolation between the nearest anchors.
    Falls back to proportional drift if no anchors are nearby.
    """
    if not anchors:
        total_drift = total_link - total_orig
        return int(offset * total_drift / total_orig) if total_orig > 0 else 0

    # Before first anchor
    if offset <= anchors[0][0]:
        return anchors[0][1] - anchors[0][0]

    # After last anchor
    if offset >= anchors[-1][0]:
        return anchors[-1][1] - anchors[-1][0]

    # Binary search for surrounding anchors
    from bisect import bisect_right
    anchor_offs = [a[0] for a in anchors]
    idx = bisect_right(anchor_offs, offset) - 1

    if idx >= len(anchors) - 1:
        return anchors[-1][1] - anchors[-1][0]

    o1, l1 = anchors[idx]
    o2, l2 = anchors[idx + 1]
    d1 = l1 - o1
    d2 = l2 - o2

    # Linear interpolation
    t = (offset - o1) / (o2 - o1) if o2 != o1 else 0
    return int(d1 + t * (d2 - d1))


def compare_function_bytes(orig_data, link_data, orig_off, link_off, size):
    """Compare function bytes instruction-by-instruction.

    Returns (full_match, opcode_match, total_bytes) where:
    - full_match: bytes where all 4 bytes match (identical instruction)
    - opcode_match: bytes where upper 2 bytes match (same opcode, different operand)
    - total_bytes: total bytes compared
    """
    total = 0
    full_match = 0
    opcode_match = 0

    for i in range(0, size - 3, 4):
        if orig_off + i + 4 > len(orig_data) or link_off + i + 4 > len(link_data):
            break
        total += 4

        orig_insn = struct.unpack_from('>I', orig_data, orig_off + i)[0]
        link_insn = struct.unpack_from('>I', link_data, link_off + i)[0]

        if orig_insn == link_insn:
            full_match += 4
        elif (orig_insn >> 16) == (link_insn >> 16):
            opcode_match += 4

    return full_match, opcode_match, total


def anchor_based_compare(orig_pe, linked_pe, orig_text, link_text, map_path):
    """Compare .text function-by-function using anchor-based drift interpolation.

    Phase 1: Find anchor points - large functions with unique body fingerprints
             (skips prologue to avoid common mflr/stw patterns)
    Phase 2: Build piecewise linear drift model from anchors
    Phase 3: Compare all functions at interpolated positions
    """
    functions = parse_map_functions(map_path)
    if not functions:
        return None

    orig_data = section_data(orig_pe, orig_text)
    link_data = section_data(linked_pe, link_text)

    # Phase 1: Find anchors
    anchors = find_anchors(orig_data, link_data, functions)

    if not anchors:
        return None

    # Pre-compute anchor offsets for bisect
    anchor_offs = [a[0] for a in anchors]

    # Phase 2+3: Compare all functions using interpolated drift
    total_bytes = 0
    full_match_bytes = 0
    opcode_match_bytes = 0
    functions_high_match = 0  # >80% opcode match
    functions_compared = 0
    functions_out_of_range = 0

    for name, off, size in functions:
        if off + size > len(orig_data):
            continue

        drift = interpolate_drift(anchors, off, len(orig_data), len(link_data))
        link_off = off + drift

        if link_off < 0 or link_off + size > len(link_data):
            total_bytes += size
            functions_out_of_range += 1
            continue

        fm, om, t = compare_function_bytes(orig_data, link_data, off, link_off, size)
        total_bytes += t
        full_match_bytes += fm
        opcode_match_bytes += om
        functions_compared += 1

        if t > 0 and (fm + om) / t > 0.8:
            functions_high_match += 1

    return {
        'total_bytes': total_bytes,
        'full_match_bytes': full_match_bytes,
        'opcode_match_bytes': opcode_match_bytes,
        'full_match_pct': full_match_bytes / total_bytes * 100 if total_bytes > 0 else 0,
        'opcode_match_pct': (full_match_bytes + opcode_match_bytes) / total_bytes * 100 if total_bytes > 0 else 0,
        'anchors_found': len(anchors),
        'functions_total': len(functions),
        'functions_compared': functions_compared,
        'functions_high_match': functions_high_match,
        'functions_out_of_range': functions_out_of_range,
    }


def print_sections(pe, label):
    """Print section table."""
    print(f"\n{label}: {pe['path']} ({pe['size']:,} bytes)")
    print(f"  Machine: 0x{pe['machine']:04X}  Sections: {pe['num_sections']}  Timestamp: 0x{pe['timestamp']:08X}")
    print(f"  ImageBase: 0x{pe['image_base']:08X}  EntryRVA: 0x{pe['entry_rva']:08X}")
    print(f"  {'Name':12s} {'VA':>10s} {'VSize':>10s} {'RawOff':>10s} {'RawSize':>10s} {'Flags':>10s}")
    print(f"  {'-'*12} {'-'*10} {'-'*10} {'-'*10} {'-'*10} {'-'*10}")
    for s in pe['sections']:
        print(f"  {s['name']:12s} 0x{s['vaddr']:08X} 0x{s['vsize']:08X} 0x{s['raw_ptr']:08X} 0x{s['raw_size']:08X} 0x{s['flags']:08X}")


def main():
    parser = argparse.ArgumentParser(description="Compare linked PE vs original")
    parser.add_argument("linked", nargs="?",
                        default=str(ROOT / "build" / "373307D9" / "default.exe"),
                        help="Path to linked PE")
    parser.add_argument("--original", default=str(ROOT / "orig" / "373307D9" / "ham_xbox_r.exe"),
                        help="Path to original PE")
    parser.add_argument("--map", default=str(ROOT / "orig" / "373307D9" / "ham_xbox_r.map"),
                        help="Path to original MAP file for function-level analysis")
    parser.add_argument("--show-diffs", type=int, default=10,
                        help="Number of diff regions to show (default: 10)")
    args = parser.parse_args()

    orig = parse_pe(args.original)
    linked = parse_pe(args.linked)

    print_sections(orig, "Original")
    print_sections(linked, "Linked")

    # Compare common sections
    print("\n=== Section Comparison ===")
    orig_names = {s['name'] for s in orig['sections']}
    link_names = {s['name'] for s in linked['sections']}

    only_orig = orig_names - link_names
    only_link = link_names - orig_names
    common = orig_names & link_names

    if only_orig:
        print(f"  Only in original: {', '.join(sorted(only_orig))}")
    if only_link:
        print(f"  Only in linked: {', '.join(sorted(only_link))}")

    # Compare matching sections
    print(f"\n=== Section Data Comparison ===")
    for name in sorted(common):
        orig_sec = get_section(orig, name)
        link_sec = get_section(linked, name)
        if orig_sec['raw_size'] == 0 and link_sec['raw_size'] == 0:
            continue

        orig_data = section_data(orig, orig_sec)
        link_data = section_data(linked, link_sec)
        result = compare_bytes(orig_data, link_data)

        status = "MATCH" if result['diff_bytes'] == 0 and result['size_diff'] == 0 else "DIFF"
        size_str = f" (size diff: {result['size_diff']:+,})" if result['size_diff'] != 0 else ""
        print(f"  {name:12s}: {result['match_pct']:6.2f}% matching  ({result['diff_bytes']:,}/{result['total_bytes']:,} bytes differ){size_str}  [{status}]")

        va_diff = link_sec['vaddr'] - orig_sec['vaddr']
        if va_diff != 0:
            print(f"               VA offset: {va_diff:+,} (0x{va_diff:+X})")

    # Detailed .text analysis
    print(f"\n=== .text Detailed Analysis ===")
    orig_text = get_section(orig, '.text')
    link_text = get_section(linked, '.text')
    func_result = None

    if orig_text and link_text:
        va_shift = link_text['vaddr'] - orig_text['vaddr']
        print(f"  VA shift: {va_shift:+} (0x{va_shift & 0xFFFFFFFF:08X})")

        orig_tdata = section_data(orig, orig_text)
        link_tdata = section_data(linked, link_text)

        # Direct comparison
        result = compare_bytes(orig_tdata, link_tdata)
        print(f"  Raw byte match: {result['match_pct']:.2f}% ({result['diff_bytes']:,} bytes differ)")
        if va_shift != 0:
            print(f"  Note: Raw match is low due to positional drift and relocation fixups.")

        # Show first N diff regions
        if result['diff_regions'] and args.show_diffs > 0:
            print(f"\n  First {min(args.show_diffs, len(result['diff_regions']))} diff regions:")
            for start, end in result['diff_regions'][:args.show_diffs]:
                size = end - start
                orig_va = orig['image_base'] + orig_text['vaddr'] + start
                link_va = linked['image_base'] + link_text['vaddr'] + start
                print(f"    Offset 0x{start:08X}  Size {size:6,}  OrigVA 0x{orig_va:08X}  LinkVA 0x{link_va:08X}")

        # Function-level comparison using MAP file + anchor-based drift
        map_path = Path(args.map)
        if map_path.exists() and map_path.stat().st_size > 0:
            print(f"\n  Anchor-based function comparison (MAP: {map_path.name})...")
            func_result = anchor_based_compare(orig, linked, orig_text, link_text, map_path)
            if func_result:
                print(f"    Anchors found:          {func_result['anchors_found']:,}")
                print(f"    Functions in MAP:        {func_result['functions_total']:,}")
                print(f"    Functions compared:       {func_result['functions_compared']:,}")
                if func_result['functions_out_of_range'] > 0:
                    print(f"    Functions out of range:   {func_result['functions_out_of_range']:,}")
                print(f"    Functions >80%% match:    {func_result['functions_high_match']:,}")
                print(f"    Code compared:            {func_result['total_bytes']:,} bytes")
                print(f"    Full instruction match:   {func_result['full_match_pct']:.2f}%")
                print(f"    Opcode match (reloc-adj): {func_result['opcode_match_pct']:.2f}%")
            else:
                print(f"    No anchors found - cannot align functions.")
        else:
            print(f"\n  No MAP file found. Use --map to enable function-level comparison.")

    # objdiff metrics (if available)
    report_path = ROOT / "build" / "373307D9" / "report.json"
    if report_path.exists():
        with open(report_path) as f:
            report = json.load(f)
        measures = report.get('measures', {})
        if measures:
            print(f"\n=== objdiff Metrics (report.json) ===")
            print(f"  Fuzzy match:     {measures.get('fuzzy_match_percent', 0):.2f}%")
            print(f"  Matched code:    {measures.get('matched_code_percent', 0):.2f}% ({int(measures.get('matched_code', 0)):,} / {int(measures.get('total_code', 0)):,} bytes)")
            print(f"  Complete code:   {measures.get('complete_code_percent', 0):.2f}% ({int(measures.get('complete_code', 0)):,} bytes)")
            print(f"  Functions:       {measures.get('matched_functions', 0):,} / {measures.get('total_functions', 0):,} matched ({measures.get('matched_functions_percent', 0):.1f}%)")

    # Entry point comparison
    print(f"\n=== Entry Point ===")
    orig_entry_va = orig['image_base'] + orig['entry_rva']
    link_entry_va = linked['image_base'] + linked['entry_rva']
    print(f"  Original: 0x{orig_entry_va:08X}")
    print(f"  Linked:   0x{link_entry_va:08X}")
    if orig_entry_va == link_entry_va:
        print(f"  MATCH")
    else:
        print(f"  DIFFER by {link_entry_va - orig_entry_va:+,}")

    # Summary
    print(f"\n=== Summary ===")
    print(f"  File sizes: original={orig['size']:,}  linked={linked['size']:,}  diff={linked['size']-orig['size']:+,}")
    print(f"  Sections: original={orig['num_sections']}  linked={linked['num_sections']}")
    if orig_text and link_text:
        text_result = compare_bytes(section_data(orig, orig_text), section_data(linked, link_text))
        print(f"  .text raw byte match:         {text_result['match_pct']:.2f}%")
    if func_result:
        print(f"  .text anchor-aligned match:    {func_result['opcode_match_pct']:.2f}% (opcode-level, {func_result['anchors_found']:,} anchors)")
    if report_path.exists():
        with open(report_path) as f:
            measures = json.load(f).get('measures', {})
        if measures:
            print(f"  objdiff fuzzy match:           {measures.get('fuzzy_match_percent', 0):.2f}% (decomp code only)")
    print(f"  Status: {'Link succeeded' if Path(args.linked).exists() else 'Link failed'}")


if __name__ == "__main__":
    main()
