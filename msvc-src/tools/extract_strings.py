#!/usr/bin/env python3
"""
Extract strings from MSVC compiler binaries and cross-reference them to
code locations. This builds a map of which functions reference which
diagnostic/pass-name strings, providing a roadmap into c2.dll's internals.

Usage:
    python3 msvc-src/tools/extract_strings.py [--binary PATH] [--min-len N]
"""

import argparse
import struct
import json
import sys
from pathlib import Path
from collections import defaultdict


def parse_pe_sections(data: bytes) -> list[dict]:
    """Parse PE section headers."""
    pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
    # Skip PE sig (4) + COFF header fields
    num_sections = struct.unpack_from('<H', data, pe_offset + 6)[0]
    opt_header_size = struct.unpack_from('<H', data, pe_offset + 20)[0]
    sections_offset = pe_offset + 24 + opt_header_size

    # Also get ImageBase from optional header
    image_base = struct.unpack_from('<I', data, pe_offset + 24 + 28)[0]

    sections = []
    for i in range(num_sections):
        off = sections_offset + i * 40
        name = data[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        vsize, vrva, rawsize, rawptr = struct.unpack_from('<IIII', data, off + 8)
        sections.append({
            'name': name,
            'virtual_address': vrva,
            'virtual_size': vsize,
            'raw_offset': rawptr,
            'raw_size': rawsize,
        })

    return sections, image_base


def rva_to_file_offset(sections: list[dict], rva: int) -> int | None:
    """Convert an RVA to a file offset."""
    for sec in sections:
        if sec['virtual_address'] <= rva < sec['virtual_address'] + sec['virtual_size']:
            return sec['raw_offset'] + (rva - sec['virtual_address'])
    return None


def file_offset_to_rva(sections: list[dict], offset: int) -> int | None:
    """Convert a file offset to an RVA."""
    for sec in sections:
        if sec['raw_offset'] <= offset < sec['raw_offset'] + sec['raw_size']:
            return sec['virtual_address'] + (offset - sec['raw_offset'])
    return None


def extract_strings(data: bytes, sections: list[dict], min_len: int = 4) -> dict[int, str]:
    """Extract ASCII strings with their RVAs."""
    strings = {}

    # Find data sections (typically .rdata, .data)
    for sec in sections:
        if sec['name'] in ('.rdata', '.data', '.text'):
            start = sec['raw_offset']
            end = start + sec['raw_size']
            i = start
            while i < end:
                # Find start of printable ASCII run
                run_start = i
                while i < end and 0x20 <= data[i] < 0x7f:
                    i += 1
                if i - run_start >= min_len and i < end and data[i] == 0:
                    s = data[run_start:i].decode('ascii')
                    rva = file_offset_to_rva(sections, run_start)
                    if rva is not None:
                        strings[rva] = s
                i += 1

    return strings


def find_string_references(data: bytes, sections: list[dict], image_base: int,
                           string_rvas: set[int]) -> dict[int, list[int]]:
    """Find code locations that reference string RVAs (via absolute addresses)."""
    refs = defaultdict(list)  # string_rva -> [code_rva, ...]

    # Find the .text section
    text_sec = None
    for sec in sections:
        if sec['name'] == '.text':
            text_sec = sec
            break
    if not text_sec:
        return refs

    # Scan .text for 4-byte values that match string virtual addresses
    start = text_sec['raw_offset']
    end = start + text_sec['raw_size']

    # Build set of absolute addresses to search for
    target_addrs = {}
    for rva in string_rvas:
        va = image_base + rva
        target_addrs[struct.pack('<I', va)] = rva

    for i in range(start, end - 3):
        chunk = data[i:i+4]
        if chunk in target_addrs:
            code_rva = file_offset_to_rva(sections, i)
            if code_rva is not None:
                refs[target_addrs[chunk]].append(code_rva)

    return refs


def find_functions(data: bytes, sections: list[dict]) -> list[dict]:
    """Find function boundaries using common x86 prologues."""
    functions = []

    text_sec = None
    for sec in sections:
        if sec['name'] == '.text':
            text_sec = sec
            break
    if not text_sec:
        return functions

    start = text_sec['raw_offset']
    end = start + text_sec['raw_size']

    # Look for push ebp; mov ebp, esp (55 8B EC)
    i = start
    while i < end - 3:
        if data[i:i+3] == b'\x55\x8b\xec':
            rva = file_offset_to_rva(sections, i)
            if rva is not None:
                functions.append({
                    'rva': rva,
                    'offset': i,
                    'name': f'sub_{rva:08X}',
                })
        i += 1

    # Sort by address and estimate sizes
    functions.sort(key=lambda f: f['rva'])
    for i in range(len(functions) - 1):
        functions[i]['size'] = functions[i+1]['rva'] - functions[i]['rva']
    if functions:
        functions[-1]['size'] = (text_sec['virtual_address'] + text_sec['virtual_size']) - functions[-1]['rva']

    return functions


def classify_strings(strings: dict[int, str]) -> dict[str, list[tuple[int, str]]]:
    """Classify strings into categories relevant to compiler internals."""
    categories = {
        'pass_names': [],       # Optimization pass identifiers
        'diagnostics': [],      # INL/INF/ERR/WRN messages
        'ppc_insns': [],        # PowerPC instruction mnemonics
        'd2_flags': [],         # /d2 undocumented flags
        'pogo': [],             # Profile-guided optimization
        'vmx': [],              # VMX/Altivec related
        'format_strings': [],   # printf-style format strings
        'other': [],
    }

    ppc_mnemonics = {
        'add', 'addi', 'addis', 'addic', 'addc', 'adde', 'addme', 'addze',
        'and', 'andi', 'andis', 'andc', 'or', 'ori', 'oris', 'orc',
        'xor', 'xori', 'xoris', 'nor', 'nand', 'eqv',
        'lwz', 'lwzu', 'lbz', 'lhz', 'lha', 'lmw', 'lfs', 'lfd',
        'stw', 'stwu', 'stb', 'sth', 'stmw', 'stfs', 'stfd',
        'cmpw', 'cmpwi', 'cmplw', 'cmplwi',
        'beq', 'bne', 'blt', 'bgt', 'ble', 'bge', 'bdnz',
        'bl', 'blr', 'bctr', 'bctrl',
        'rlwinm', 'rlwimi', 'rlwnm',
        'slw', 'srw', 'sraw', 'srawi',
        'mullw', 'mulhw', 'mulhwu', 'divw', 'divwu',
        'neg', 'subfic', 'subfc', 'subfe', 'subfze',
        'mflr', 'mtlr', 'mfctr', 'mtctr', 'mfcr', 'mtcrf',
        'extsb', 'extsh', 'clrlwi', 'clrrwi',
        'fmr', 'fneg', 'fabs', 'fadd', 'fsub', 'fmul', 'fdiv',
        'fmadd', 'fmsub', 'fnmadd', 'fnmsub',
        'fcmpu', 'frsp', 'fctiwz',
    }

    for rva, s in strings.items():
        s_lower = s.lower().strip()

        if s_lower.startswith(('/d2', '-d2')):
            categories['d2_flags'].append((rva, s))
        elif s.startswith(('INL:', 'INF:', 'ERR:', 'WRN:', 'OPT:')):
            categories['diagnostics'].append((rva, s))
        elif s.startswith('Pogo') or 'pogo' in s_lower:
            categories['pogo'].append((rva, s))
        elif 'vmx' in s_lower or 'altivec' in s_lower or s.startswith('__restvmx'):
            categories['vmx'].append((rva, s))
        elif s_lower in ppc_mnemonics or s_lower.rstrip('.') in ppc_mnemonics:
            categories['ppc_insns'].append((rva, s))
        elif s.isupper() and '_' in s and len(s) > 3:
            categories['pass_names'].append((rva, s))
        elif '%' in s and any(c in s for c in 'dsfxXu'):
            categories['format_strings'].append((rva, s))
        else:
            categories['other'].append((rva, s))

    return categories


def main():
    parser = argparse.ArgumentParser(description='Extract and analyze strings from MSVC compiler binaries')
    parser.add_argument('--binary', default='build/compilers/X360/16.00.11886.00/c2.dll',
                        help='Path to binary (default: c2.dll)')
    parser.add_argument('--min-len', type=int, default=4, help='Minimum string length')
    parser.add_argument('--output', help='Output JSON file')
    parser.add_argument('--xref', action='store_true', help='Cross-reference strings to code locations')
    parser.add_argument('--functions', action='store_true', help='Find and list functions')
    parser.add_argument('--category', help='Only show strings in this category')
    args = parser.parse_args()

    binary_path = Path(args.binary)
    if not binary_path.exists():
        print(f"Error: {binary_path} not found", file=sys.stderr)
        sys.exit(1)

    data = binary_path.read_bytes()
    sections, image_base = parse_pe_sections(data)

    print(f"Binary: {binary_path} ({len(data):,} bytes)")
    print(f"Image base: 0x{image_base:08X}")
    print(f"Sections:")
    for sec in sections:
        print(f"  {sec['name']:8s}  RVA=0x{sec['virtual_address']:08X}  "
              f"Size=0x{sec['virtual_size']:08X}  Raw=0x{sec['raw_offset']:08X}")
    print()

    # Extract strings
    strings = extract_strings(data, sections, args.min_len)
    print(f"Strings found: {len(strings)}")

    # Classify
    categories = classify_strings(strings)
    for cat_name, items in categories.items():
        print(f"  {cat_name}: {len(items)}")
    print()

    # Show specific category
    if args.category:
        items = categories.get(args.category, [])
        for rva, s in sorted(items):
            print(f"  0x{rva:08X}: {s}")
        return

    # Cross-reference strings to code
    if args.xref:
        print("Cross-referencing strings to code locations...")
        refs = find_string_references(data, sections, image_base, set(strings.keys()))
        referenced = {rva: locs for rva, locs in refs.items() if locs}
        print(f"Strings referenced from code: {len(referenced)}")

        # Find interesting strings (pass names, diagnostics) and their code locations
        print("\n=== Pass Name References ===")
        for rva, s in sorted(categories['pass_names']):
            if rva in referenced:
                code_locs = referenced[rva]
                print(f"  '{s}' @ 0x{rva:08X} <- referenced from {len(code_locs)} location(s):")
                for loc in code_locs[:3]:
                    print(f"    code @ 0x{loc:08X}")

        print("\n=== Diagnostic References ===")
        for rva, s in sorted(categories['diagnostics']):
            if rva in referenced:
                code_locs = referenced[rva]
                print(f"  '{s[:60]}...' @ 0x{rva:08X} <- {len(code_locs)} ref(s)")

    # Find functions
    if args.functions:
        functions = find_functions(data, sections)
        print(f"\nFunctions found: {len(functions)}")

        # If we have xrefs, annotate functions with their strings
        if args.xref:
            refs = find_string_references(data, sections, image_base, set(strings.keys()))

            # Map code addresses to containing functions
            func_strings = defaultdict(list)
            for str_rva, code_locs in refs.items():
                for code_rva in code_locs:
                    # Find which function contains this code address
                    for func in functions:
                        if func['rva'] <= code_rva < func['rva'] + func.get('size', 0):
                            func_strings[func['rva']].append(strings[str_rva])
                            break

            # Print functions with interesting strings
            print("\n=== Functions with Pass/Diagnostic Strings ===")
            for func in functions:
                strs = func_strings.get(func['rva'], [])
                interesting = [s for s in strs if
                    (s.isupper() and '_' in s) or
                    s.startswith(('INL:', 'INF:', 'ERR:', 'WRN:', 'OPT:'))]
                if interesting:
                    print(f"\n  {func['name']} (0x{func['rva']:08X}, {func.get('size', '?')} bytes):")
                    for s in interesting[:5]:
                        print(f"    - {s[:80]}")

    # Output JSON
    if args.output:
        output = {
            'binary': str(binary_path),
            'image_base': image_base,
            'sections': sections,
            'strings': {f'0x{rva:08X}': s for rva, s in strings.items()},
            'categories': {
                cat: [(f'0x{rva:08X}', s) for rva, s in items]
                for cat, items in categories.items()
            },
        }
        Path(args.output).write_text(json.dumps(output, indent=2))
        print(f"\nOutput written to {args.output}")


if __name__ == '__main__':
    main()
