#!/usr/bin/env python3
"""Create data-stub .obj files from split .objs for Matching units.

When Config B links decomp .obj instead of split .obj for Matching units,
cross-references from non-Matching split .objs to lbl_* data symbols break
because the decomp .obj exports C++ names, not lbl_* names.

This script creates "data-stub" .obj files that contain ONLY the data
sections (.data, .rdata, .bss) from split .objs with their lbl_* symbols.
These are linked alongside the decomp .obj to provide lbl_* exports.

Usage:
    python3 scripts/create_data_stubs.py [--dry-run] [--verbose] [--unit UNIT]
"""

import argparse
import json
import os
import struct
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_ROOT / "build" / "373307D9"
SPLIT_OBJ_DIR = BUILD_DIR / "obj"
DATA_STUB_DIR = BUILD_DIR / "data"
CONFIG_DIR = PROJECT_ROOT / "config" / "373307D9"
OBJECTS_PATH = CONFIG_DIR / "objects.json"

# COFF constants
IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_LNK_COMDAT = 0x00001000

IMAGE_SYM_UNDEFINED = 0
IMAGE_SYM_ABSOLUTE = -1
IMAGE_SYM_DEBUG = -2

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3


def get_matching_units():
    """Get set of unit paths that are Matching."""
    with open(OBJECTS_PATH) as f:
        objects = json.load(f)
    matching = set()
    for lib, lib_config in objects.items():
        for path, obj_config in lib_config.get('objects', {}).items():
            status = obj_config if isinstance(obj_config, str) else obj_config.get('status', 'MISSING')
            if status == 'Matching':
                matching.add(path)
    return matching


def find_split_obj(unit_name):
    """Find the split .obj for a unit."""
    base = unit_name
    if base.endswith('.cpp') or base.endswith('.c'):
        base = base.rsplit('.', 1)[0]
    # Try standard path
    obj_path = SPLIT_OBJ_DIR / (base + '.obj')
    if obj_path.exists():
        return obj_path
    return None


def create_data_stub(split_obj_path, output_path, verbose=False):
    """Read a COFF .obj and write a new one with only data sections.

    Returns: (num_data_sections, num_data_symbols) or None on error.
    """
    with open(split_obj_path, 'rb') as f:
        data = f.read()

    if len(data) < 20:
        return None

    # Parse COFF header
    machine, num_sections, timestamp, sym_offset, num_symbols, opt_size, characteristics = struct.unpack_from(
        '<HHlIIHH', data, 0
    )

    # Parse section headers
    sections = []
    for i in range(num_sections):
        pos = 20 + opt_size + i * 40
        hdr = data[pos:pos + 40]
        name_bytes = hdr[0:8]
        vs, va, raw_size, raw_offset, reloc_offset, ln_offset, num_relocs, num_lns, chars = struct.unpack_from(
            '<IIIIIIHHI', hdr, 8
        )

        is_code = bool(chars & IMAGE_SCN_CNT_CODE)
        is_data = bool(chars & IMAGE_SCN_CNT_INITIALIZED_DATA)
        is_bss = bool(chars & IMAGE_SCN_CNT_UNINITIALIZED_DATA)

        sections.append({
            'index': i,
            'name_bytes': name_bytes,
            'virtual_size': vs,
            'virtual_addr': va,
            'raw_size': raw_size,
            'raw_offset': raw_offset,
            'reloc_offset': reloc_offset,
            'num_relocs': num_relocs,
            'chars': chars,
            'is_code': is_code,
            'is_data': is_data,
            'is_bss': is_bss,
            'keep': (is_data or is_bss) and not is_code,
        })

    # Read string table
    str_table_offset = sym_offset + num_symbols * 18
    if str_table_offset + 4 <= len(data):
        str_table_size = struct.unpack_from('<I', data, str_table_offset)[0]
        str_table_data = data[str_table_offset:str_table_offset + str_table_size]
    else:
        str_table_data = struct.pack('<I', 4)  # Empty string table

    # Build string table lookup
    strings = {}
    pos = 4
    while pos < len(str_table_data):
        end = str_table_data.find(b'\x00', pos)
        if end == -1:
            break
        strings[pos] = str_table_data[pos:end].decode('ascii', errors='replace')
        pos = end + 1

    # Read symbols
    all_symbols = []
    i = 0
    while i < num_symbols:
        pos = sym_offset + i * 18
        if pos + 18 > len(data):
            break

        sym_data = data[pos:pos + 18]
        name_bytes = sym_data[0:8]
        value, section_num, sym_type, storage_class, num_aux = struct.unpack_from(
            '<IhHBB', sym_data, 8
        )

        # Read aux symbols
        aux_data = []
        for a in range(num_aux):
            aux_pos = sym_offset + (i + 1 + a) * 18
            if aux_pos + 18 <= len(data):
                aux_data.append(data[aux_pos:aux_pos + 18])

        # Decode name
        if name_bytes[:4] == b'\x00\x00\x00\x00':
            str_off = struct.unpack_from('<I', name_bytes, 4)[0]
            name = strings.get(str_off, '<unknown>')
        else:
            name = name_bytes.rstrip(b'\x00').decode('ascii', errors='replace')

        all_symbols.append({
            'raw': sym_data,
            'name': name,
            'name_bytes': name_bytes,
            'value': value,
            'section_number': section_num,
            'sym_type': sym_type,
            'storage_class': storage_class,
            'num_aux': num_aux,
            'aux_data': aux_data,
            'original_index': i,
        })

        i += 1 + num_aux

    # Determine which sections to keep (data only, non-COMDAT)
    # Also skip .pdata, .debug sections
    kept_sections = []
    old_to_new_section = {}  # old 1-based index -> new 1-based index
    for s in sections:
        name = s['name_bytes'].rstrip(b'\x00').decode('ascii', errors='replace')
        if s['keep'] and '.pdata' not in name and '.debug' not in name and '.XBLD' not in name:
            old_to_new_section[s['index'] + 1] = len(kept_sections) + 1
            kept_sections.append(s)

    if not kept_sections:
        return None

    # Filter symbols to only those in kept sections or section 0 (UNDEFINED)
    kept_symbols = []
    for sym in all_symbols:
        sec_num = sym['section_number']
        if sec_num in old_to_new_section:
            kept_symbols.append(sym)
        elif sec_num == IMAGE_SYM_UNDEFINED and sym['value'] == 0:
            # UNDEFINED symbol (external reference) - keep if it's a data reference
            # Actually, skip UNDEFINED symbols - we only need definitions
            pass

    if verbose:
        print(f"  Sections: {num_sections} -> {len(kept_sections)} data sections")
        print(f"  Symbols: {len(all_symbols)} -> {len(kept_symbols)} data symbols")

    # Build output COFF
    out = bytearray()

    # Reserve space for header
    header_size = 20
    section_headers_size = len(kept_sections) * 40
    total_header = header_size + section_headers_size

    # Calculate section data offsets
    current_offset = total_header
    section_offsets = []
    for s in kept_sections:
        if s['raw_size'] > 0 and not s['is_bss']:
            section_offsets.append(current_offset)
            current_offset += s['raw_size']
        else:
            section_offsets.append(0)

    # Symbol table offset
    sym_table_offset = current_offset

    # Build new string table
    new_str_table = bytearray()
    new_str_table += struct.pack('<I', 0)  # Placeholder for size
    str_offset_map = {}  # symbol name -> offset in new string table

    def get_name_bytes(name):
        """Get 8-byte COFF name field, using string table for long names."""
        encoded = name.encode('ascii')
        if len(encoded) <= 8:
            return encoded.ljust(8, b'\x00')
        else:
            if name not in str_offset_map:
                offset = len(new_str_table)
                str_offset_map[name] = offset
                new_str_table.extend(encoded + b'\x00')
            return b'\x00\x00\x00\x00' + struct.pack('<I', str_offset_map[name])

    # Build symbol entries
    sym_entries = bytearray()
    num_out_symbols = 0
    for sym in kept_symbols:
        new_sec_num = old_to_new_section[sym['section_number']]
        name_field = get_name_bytes(sym['name'])

        sym_entries += name_field
        sym_entries += struct.pack('<IhHBB',
            sym['value'],
            new_sec_num,
            sym['sym_type'],
            sym['storage_class'],
            sym['num_aux'],
        )
        num_out_symbols += 1

        for aux in sym['aux_data']:
            sym_entries += aux
            num_out_symbols += 1

    # Update string table size
    struct.pack_into('<I', new_str_table, 0, len(new_str_table))

    # Write COFF header
    out += struct.pack('<HHlIIHH',
        machine,
        len(kept_sections),
        timestamp,
        sym_table_offset,
        num_out_symbols,
        0,  # optional header size
        0,  # characteristics
    )

    # Write section headers
    for i, s in enumerate(kept_sections):
        out += s['name_bytes']
        raw_off = section_offsets[i] if not s['is_bss'] else 0
        raw_sz = s['raw_size'] if not s['is_bss'] else 0
        out += struct.pack('<IIIIIIHHI',
            s['virtual_size'],
            s['virtual_addr'],
            raw_sz,
            raw_off,
            0,  # reloc offset (no relocations in data stubs)
            0,  # line number offset
            0,  # num relocations
            0,  # num line numbers
            s['chars'],
        )

    # Write section data
    for i, s in enumerate(kept_sections):
        if s['raw_size'] > 0 and not s['is_bss']:
            section_data = data[s['raw_offset']:s['raw_offset'] + s['raw_size']]
            out += section_data

    # Write symbol table
    out += sym_entries

    # Write string table
    out += new_str_table

    # Write output file
    os.makedirs(output_path.parent, exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(out)

    return (len(kept_sections), len(kept_symbols))


def main():
    parser = argparse.ArgumentParser(description='Create data-stub .obj files from split .objs')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be done')
    parser.add_argument('--verbose', action='store_true', help='Show detailed output')
    parser.add_argument('--unit', type=str, help='Process single unit')
    args = parser.parse_args()

    matching_units = get_matching_units()

    if args.unit:
        units = [args.unit]
    else:
        units = sorted(matching_units)

    total_stubs = 0
    total_sections = 0
    total_symbols = 0

    for unit in units:
        split_obj = find_split_obj(unit)
        if split_obj is None:
            if args.verbose:
                print(f"SKIP {unit}: no split .obj found")
            continue

        base = unit
        if base.endswith('.cpp') or base.endswith('.c'):
            base = base.rsplit('.', 1)[0]
        output_path = DATA_STUB_DIR / (base + '.obj')

        if args.dry_run:
            print(f"Would create: {output_path}")
            total_stubs += 1
            continue

        result = create_data_stub(split_obj, output_path, verbose=args.verbose)
        if result is not None:
            n_sections, n_symbols = result
            total_stubs += 1
            total_sections += n_sections
            total_symbols += n_symbols
            if args.verbose:
                print(f"  Created: {output_path}")
        else:
            if args.verbose:
                print(f"  SKIP {unit}: no data sections in split .obj")

    print(f"\nCreated {total_stubs} data-stub .obj files")
    print(f"Total: {total_sections} data sections, {total_symbols} data symbols")


if __name__ == '__main__':
    main()
