#!/usr/bin/env python3
"""
Fix COFF objects with .pdata sections that cause LNK1223 errors.

The X360 MSVC linker strictly validates .pdata contributions. Split objects
from dtk can have .pdata content issues that the linker rejects:
  - Entries spanning multiple code sections (.text + .text$yc)
  - Entries referencing __unwind$ symbols instead of function symbols

Note: The original duplicate-.pdata bug (127 objects) is now fixed in dtk
via section merging in split_obj(). This workaround handles the remaining
pdata content validation issues.

Usage:
    python3 scripts/fix_pdata.py [--dry-run] [--restore]
"""

import argparse
import json
import struct
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def needs_fix(data):
    """Check if a COFF object has .pdata content the linker would reject."""
    if len(data) < 20:
        return False

    _, num_sections = struct.unpack_from('<HH', data, 0)
    sym_off_base = struct.unpack_from('<I', data, 8)[0]
    num_syms = struct.unpack_from('<I', data, 12)[0]
    opt_hdr_size = struct.unpack_from('<H', data, 16)[0]

    # Parse sections: count .text* sections and find .pdata
    offset = 20 + opt_hdr_size
    text_count = 0
    pdata_relptr = None
    pdata_nreloc = 0
    for i in range(num_sections):
        if offset + 40 > len(data):
            break
        name = data[offset:offset+8].rstrip(b'\x00')
        if name.startswith(b'.text'):
            text_count += 1
        if name == b'.pdata':
            pdata_relptr = struct.unpack_from('<I', data, offset+24)[0]
            pdata_nreloc = struct.unpack_from('<H', data, offset+32)[0]
        offset += 40

    if pdata_relptr is None:
        return False

    # Issue 1: multiple .text sections
    if text_count > 1:
        return True

    # Issue 2: pdata references __unwind$ symbols
    if sym_off_base > 0 and num_syms > 0:
        str_table_off = sym_off_base + num_syms * 18
        reloff = pdata_relptr
        for j in range(pdata_nreloc):
            if reloff + 10 > len(data):
                break
            sym_idx = struct.unpack_from('<I', data, reloff+4)[0]
            sym_off = sym_off_base + sym_idx * 18
            if sym_off + 18 > len(data):
                reloff += 10
                continue
            name_bytes = data[sym_off:sym_off+8]
            if name_bytes[:4] == b'\x00\x00\x00\x00':
                str_off = struct.unpack_from('<I', name_bytes, 4)[0]
                abs_off = str_table_off + str_off
                if abs_off < len(data):
                    end = data.find(b'\x00', abs_off, abs_off + 200)
                    if end < 0:
                        end = abs_off + 200
                    sym_name = data[abs_off:end]
                else:
                    sym_name = b''
            else:
                sym_name = name_bytes.rstrip(b'\x00')
            if b'__unwind$' in sym_name:
                return True
            reloff += 10

    return False


def fix_object(obj_path, restore=False):
    """Rename .pdata sections in objects with content issues.
    Returns (path, fixed, error)."""
    try:
        with open(obj_path, 'rb') as f:
            data = bytearray(f.read())

        if len(data) < 20:
            return obj_path, False, None

        _, num_sections = struct.unpack_from('<HH', data, 0)
        opt_hdr_size = struct.unpack_from('<H', data, 16)[0]

        if restore:
            modified = False
            offset = 20 + opt_hdr_size
            for i in range(num_sections):
                if offset + 40 > len(data):
                    break
                name = data[offset:offset+8]
                stripped = name.rstrip(b'\x00')
                if stripped.startswith(b'.pdat') and stripped != b'.pdata':
                    data[offset:offset+8] = b'.pdata\x00\x00'
                    modified = True
                offset += 40
            if modified:
                with open(obj_path, 'wb') as f:
                    f.write(data)
            return obj_path, modified, None

        if not needs_fix(data):
            return obj_path, False, None

        # Rename .pdata to .pdatN
        modified = False
        offset = 20 + opt_hdr_size
        pdata_idx = 0
        for i in range(num_sections):
            if offset + 40 > len(data):
                break
            name = data[offset:offset+8]
            if name.rstrip(b'\x00') == b'.pdata':
                new_name = f".pdat{pdata_idx}".encode('ascii')
                data[offset:offset+8] = new_name.ljust(8, b'\x00')
                pdata_idx += 1
                modified = True
            offset += 40

        if modified:
            with open(obj_path, 'wb') as f:
                f.write(data)

        return obj_path, modified, None

    except Exception as e:
        return obj_path, False, str(e)


def main():
    parser = argparse.ArgumentParser(description="Fix .pdata in COFF objects for linking")
    parser.add_argument("--dry-run", action="store_true", help="Don't modify files")
    parser.add_argument("--restore", action="store_true", help="Restore .pdatN back to .pdata")
    args = parser.parse_args()

    config_path = ROOT / "build" / "373307D9" / "config.json"
    with open(config_path) as f:
        cfg = json.load(f)

    obj_paths = [str(ROOT / u['object']) for u in cfg['units'] if u.get('object')]
    action = "Restoring" if args.restore else "Fixing"
    print(f"{action} .pdata sections in {len(obj_paths)} objects...")

    if args.dry_run:
        count = 0
        for p in obj_paths:
            with open(p, 'rb') as f:
                data = f.read()
            if needs_fix(data):
                count += 1
        print(f"  {count} objects need .pdata fix")
        return

    fixed = 0
    errors = 0
    with ThreadPoolExecutor(max_workers=32) as pool:
        futures = {pool.submit(fix_object, p, args.restore): p for p in obj_paths}
        for fut in as_completed(futures):
            path, was_fixed, err = fut.result()
            if err:
                print(f"  ERROR {Path(path).name}: {err}")
                errors += 1
            elif was_fixed:
                fixed += 1

    print(f"Modified {fixed} objects, {errors} errors")


if __name__ == "__main__":
    main()
