#!/usr/bin/env python3
"""Dump vtable layout from original COFF .obj files.

Reads COFF symbol and relocation tables to reconstruct vtable entries,
mapping each slot to the actual function symbol. ICF-merged symbols are
noted so you can identify which virtual function each slot corresponds to.

Usage:
    python3 scripts/dump_vtable.py <class_name> [--obj <path>]
    python3 scripts/dump_vtable.py RndFontBase
    python3 scripts/dump_vtable.py RndFont3d --obj build/373307D9/obj/system/rndobj/Font3d.obj

If --obj is not given, searches build/373307D9/obj/ for a matching .obj file.
"""

import argparse
import glob
import os
import struct
import subprocess
import sys


def read_coff_symbols(data):
    """Parse COFF symbol table and string table."""
    machine, num_sections, timestamp, symtab_offset, num_symbols, opt_hdr_size, flags = \
        struct.unpack_from('<HHIIIHH', data, 0)

    # String table immediately after symbol table
    strtab_offset = symtab_offset + num_symbols * 18
    strtab_size = struct.unpack_from('<I', data, strtab_offset)[0]
    strtab = data[strtab_offset:strtab_offset + strtab_size]

    def get_name(offset):
        if data[offset:offset + 4] == b'\x00\x00\x00\x00':
            str_offset = struct.unpack_from('<I', data, offset + 4)[0]
            end = strtab.index(b'\x00', str_offset)
            return strtab[str_offset:end].decode('ascii', errors='replace')
        else:
            return data[offset:offset + 8].rstrip(b'\x00').decode('ascii', errors='replace')

    # Read all symbols
    symbols = []
    i = 0
    while i < num_symbols:
        sym_offset = symtab_offset + i * 18
        name = get_name(sym_offset)
        value, section, type_val, storage, aux_count = \
            struct.unpack_from('<IhHBB', data, sym_offset + 8)
        symbols.append({
            'index': i,
            'name': name,
            'value': value,
            'section': section,
            'type': type_val,
            'storage': storage,
            'aux_count': aux_count,
        })
        i += 1 + aux_count

    # Read section headers
    section_hdr_offset = 20 + opt_hdr_size
    sections = []
    for s in range(num_sections):
        hdr_off = section_hdr_offset + s * 40
        sec_name_raw = data[hdr_off:hdr_off + 8].rstrip(b'\x00')
        if sec_name_raw.startswith(b'/'):
            # Long section name - offset into string table
            str_off = int(sec_name_raw[1:].decode('ascii'))
            end = strtab.index(b'\x00', str_off)
            sec_name = strtab[str_off:end].decode('ascii', errors='replace')
        else:
            sec_name = sec_name_raw.decode('ascii', errors='replace')
        vsize, vaddr, raw_size, raw_offset, reloc_offset, linenum_offset, \
            num_relocs, num_linenums, characteristics = \
            struct.unpack_from('<IIIIIIHHI', data, hdr_off + 8)
        sections.append({
            'name': sec_name,
            'vsize': vsize,
            'raw_size': raw_size,
            'raw_offset': raw_offset,
            'reloc_offset': reloc_offset,
            'num_relocs': num_relocs,
            'characteristics': characteristics,
        })

    return symbols, sections


def find_vtable(data, symbols, sections, class_name):
    """Find vtable symbol and read its relocation entries."""
    vtable_sym_name = f'??_7{class_name}@@6B@'

    # Find the vtable symbol
    vtable_sym = None
    for sym in symbols:
        if sym['name'] == vtable_sym_name:
            vtable_sym = sym
            break

    if vtable_sym is None:
        # Try partial match
        for sym in symbols:
            if f'??_7{class_name}' in sym['name'] and '6B' in sym['name']:
                vtable_sym = sym
                break

    if vtable_sym is None:
        return None, None

    # Find the section containing the vtable
    sec_idx = vtable_sym['section'] - 1  # 1-based
    if sec_idx < 0 or sec_idx >= len(sections):
        return vtable_sym, []

    section = sections[sec_idx]

    # Build symbol index lookup
    sym_by_idx = {}
    for sym in symbols:
        sym_by_idx[sym['index']] = sym

    # Read relocations for this section
    entries = []
    for r in range(section['num_relocs']):
        rel_off = section['reloc_offset'] + r * 10
        rva, sym_idx, rel_type = struct.unpack_from('<IIH', data, rel_off)
        target_sym = sym_by_idx.get(sym_idx, {'name': f'<unknown_{sym_idx}>'})
        entries.append({
            'offset': rva,
            'type': rel_type,
            'symbol': target_sym['name'],
        })

    return vtable_sym, entries


def demangle_symbol(mangled):
    """Try to demangle a MSVC mangled name."""
    try:
        result = subprocess.run(
            ['c++filt', '-n', mangled],
            capture_output=True, text=True, timeout=5
        )
        demangled = result.stdout.strip()
        if demangled != mangled:
            return demangled
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    # Basic manual demangling for common patterns
    if mangled.startswith('??_G'):
        # Scalar deleting destructor
        cls = mangled[4:].split('@@')[0]
        return f'{cls}::~{cls}() [scalar deleting]'
    if mangled.startswith('??1'):
        cls = mangled[3:].split('@@')[0]
        return f'{cls}::~{cls}()'
    if mangled.startswith('?'):
        parts = mangled[1:].split('@')
        if len(parts) >= 2:
            method = parts[0]
            cls = parts[1]
            return f'{cls}::{method}'

    return mangled


# Known ICF merge patterns - functions with identical machine code
ICF_HINTS = {
    'OnlyReturns': 'returns void/this (empty function or return this)',
}


def classify_icf(symbol, offset, all_entries):
    """Try to classify ICF-merged symbols based on context."""
    if symbol == 'OnlyReturns':
        return 'empty/returns'
    # If the symbol doesn't match the class, it's likely ICF-merged
    return None


def find_obj_file(class_name):
    """Search for the .obj file containing a class's vtable."""
    # Common name mappings
    search_names = [class_name]

    # Strip common prefixes
    if class_name.startswith('Rnd'):
        search_names.append(class_name[3:])  # RndFontBase -> FontBase
    if class_name.startswith('Ham'):
        search_names.append(class_name[3:])

    obj_dir = 'build/373307D9/obj'
    for name in search_names:
        pattern = os.path.join(obj_dir, '**', f'{name}.obj')
        matches = glob.glob(pattern, recursive=True)
        if matches:
            # Prefer the one NOT under obj/obj/ (avoid duplicate)
            for m in matches:
                if '/obj/obj/' not in m:
                    return m
            return matches[0]

    return None


def get_vtable_layout(class_name, obj_path=None, project_root=None):
    """Get vtable layout as a list of dicts with offset, slot, symbol, demangled.

    Args:
        class_name: Class name (e.g., 'RndFontBase')
        obj_path: Path to .obj file (auto-detected if None)
        project_root: Project root for auto-detection (defaults to cwd)

    Returns:
        List of dicts: [{'slot': 0, 'offset': 0, 'symbol': '...', 'demangled': '...'}, ...]
        Empty list if vtable not found.
    """
    if project_root:
        old_cwd = os.getcwd()
        os.chdir(project_root)

    try:
        if not obj_path:
            obj_path = find_obj_file(class_name)
            if not obj_path:
                return []

        with open(obj_path, 'rb') as f:
            data = f.read()

        symbols, sections = read_coff_symbols(data)
        vtable_sym, entries = find_vtable(data, symbols, sections, class_name)

        if vtable_sym is None or not entries:
            return []

        result = []
        for i, entry in enumerate(entries):
            result.append({
                'slot': i,
                'offset': entry['offset'],
                'symbol': entry['symbol'],
                'demangled': demangle_symbol(entry['symbol']),
            })
        return result
    finally:
        if project_root:
            os.chdir(old_cwd)


def lookup_vtable_offset(class_name, offset, obj_path=None, project_root=None):
    """Look up which virtual function is at a given vtable offset.

    Args:
        class_name: Class name (e.g., 'RndFontBase')
        offset: Byte offset into vtable (e.g., 0x7c)
        obj_path: Path to .obj file (auto-detected if None)
        project_root: Project root for auto-detection

    Returns:
        Dict with slot info, or None if not found.
    """
    layout = get_vtable_layout(class_name, obj_path, project_root)
    for entry in layout:
        if entry['offset'] == offset:
            return entry
    return None


def main():
    parser = argparse.ArgumentParser(description='Dump vtable layout from original COFF .obj files')
    parser.add_argument('class_name', help='Class name (e.g., RndFontBase, RndFont3d)')
    parser.add_argument('--obj', help='Path to .obj file (auto-detected if not given)')
    parser.add_argument('--demangle', '-d', action='store_true', help='Attempt to demangle symbol names')
    parser.add_argument('--raw', action='store_true', help='Show raw mangled symbol names only')
    args = parser.parse_args()

    obj_path = args.obj
    if not obj_path:
        obj_path = find_obj_file(args.class_name)
        if not obj_path:
            print(f"Error: Could not find .obj file for {args.class_name}")
            print(f"Try: python3 {sys.argv[0]} {args.class_name} --obj <path_to_obj>")
            sys.exit(1)

    print(f"Reading: {obj_path}")

    with open(obj_path, 'rb') as f:
        data = f.read()

    symbols, sections = read_coff_symbols(data)
    vtable_sym, entries = find_vtable(data, symbols, sections, args.class_name)

    if vtable_sym is None:
        print(f"Error: No vtable symbol found for {args.class_name}")
        print(f"Available ??_7 symbols:")
        for sym in symbols:
            if '??_7' in sym['name']:
                print(f"  {sym['name']}")
        sys.exit(1)

    print(f"Vtable: {vtable_sym['name']} (section {vtable_sym['section']}, {len(entries)} entries)")
    print()

    # Known Object virtual function order for annotation
    OBJECT_VIRTUALS = [
        'dtor', 'RefOwner', 'Replace', 'ClassName', 'SetType',
        'Handle', 'SyncProperty', 'InitObject', 'Save', 'Copy',
        'Load', 'PreSave', 'PostSave', 'Print', 'Export',
        'SetTypeDef', 'ObjectDef', 'SetName', 'DataDir', 'PreLoad',
        'PostLoad', 'FindPathName',
    ]

    print(f'{"Slot":>4}  {"Offset":>6}  {"Symbol":<60}  {"Annotation"}')
    print('-' * 120)

    for i, entry in enumerate(entries):
        sym = entry['symbol']
        offset_hex = f"0x{entry['offset']:04x}"

        # Annotation
        annotation = ''
        if i < len(OBJECT_VIRTUALS):
            annotation = f'[Object] {OBJECT_VIRTUALS[i]}'

        # Show demangled or raw
        if args.raw:
            display = sym
        elif args.demangle:
            display = demangle_symbol(sym)
        else:
            display = sym

        # ICF detection
        icf = classify_icf(sym, entry['offset'], entries)
        if icf:
            annotation += f' ({icf})'

        print(f'[{i:3d}]  {offset_hex}  {display:<60}  {annotation}')


if __name__ == '__main__':
    main()
