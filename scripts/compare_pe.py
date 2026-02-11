#!/usr/bin/env python3
"""
Compare a linked PE against the original ham_xbox_r.exe.

Analyzes sections, .text byte differences, and uses the MAP file
for symbol-level comparison when available.

Usage:
    python3 scripts/compare_pe.py [linked_pe] [--map MAP_FILE]
"""

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


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


def compare_bytes(data1, data2, offset1=0, offset2=0):
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
                        default=str(ROOT / "build" / "373307D9" / "ham_xbox_r_test.exe"),
                        help="Path to linked PE")
    parser.add_argument("--original", default=str(ROOT / "orig" / "373307D9" / "ham_xbox_r.exe"),
                        help="Path to original PE")
    parser.add_argument("--map", default=None,
                        help="Path to MAP file for symbol-level analysis")
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

    if orig_text and link_text:
        va_shift = link_text['vaddr'] - orig_text['vaddr']
        print(f"  VA shift: {va_shift:+} (0x{va_shift & 0xFFFFFFFF:08X})")

        orig_data = section_data(orig, orig_text)
        link_data = section_data(linked, link_text)

        # Direct comparison
        result = compare_bytes(orig_data, link_data)
        print(f"  Direct: {result['match_pct']:.2f}% matching ({result['diff_bytes']:,} bytes differ)")

        # Show first N diff regions
        if result['diff_regions'] and args.show_diffs > 0:
            print(f"\n  First {min(args.show_diffs, len(result['diff_regions']))} diff regions:")
            for start, end in result['diff_regions'][:args.show_diffs]:
                size = end - start
                orig_va = orig['image_base'] + orig_text['vaddr'] + start
                link_va = linked['image_base'] + link_text['vaddr'] + start
                print(f"    Offset 0x{start:08X}  Size {size:6,}  OrigVA 0x{orig_va:08X}  LinkVA 0x{link_va:08X}")

        # Try offset-adjusted comparison (shift linked data to match original VA)
        if va_shift != 0:
            print(f"\n  Note: .text VA shifted by {va_shift:+}. This affects all absolute relocations.")
            print(f"  A relocation-aware comparison would likely show much higher match %.")

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
    text_result = None
    if orig_text and link_text:
        text_result = compare_bytes(section_data(orig, orig_text), section_data(linked, link_text))
    print(f"  File sizes: original={orig['size']:,}  linked={linked['size']:,}  diff={linked['size']-orig['size']:+,}")
    print(f"  Sections: original={orig['num_sections']}  linked={linked['num_sections']}")
    if text_result:
        print(f"  .text match: {text_result['match_pct']:.2f}%")
    print(f"  Status: {'Link succeeded' if Path(args.linked).exists() else 'Link failed'}")


if __name__ == "__main__":
    main()
