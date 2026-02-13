#!/usr/bin/env python3
"""
Rename .pdata sections in split COFF objects to bypass MSVC linker validation.

The X360 MSVC linker (link.exe 10.00.11886.00) strictly validates .pdata
(function table) contributions from each object file. Split objects from dtk
produce .pdata content that triggers LNK1223 fatal errors. Rather than trying
to detect specific failure patterns, we rename ALL .pdata sections to .pdat0
so the linker skips validation. The renamed sections still end up in the PE
but aren't recognized as function table entries (acceptable for PoC linking).

Usage:
    python3 scripts/fix_pdata.py [--dry-run] [--restore]
"""

import argparse
import json
import struct
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def has_pdata(data):
    """Check if a COFF object has a .pdata section."""
    if len(data) < 20:
        return False

    _, num_sections = struct.unpack_from('<HH', data, 0)
    opt_hdr_size = struct.unpack_from('<H', data, 16)[0]

    offset = 20 + opt_hdr_size
    for i in range(num_sections):
        if offset + 40 > len(data):
            break
        name = data[offset:offset+8].rstrip(b'\x00')
        if name == b'.pdata':
            return True
        offset += 40

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

        if not has_pdata(data):
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
            if has_pdata(data):
                count += 1
        print(f"  {count} objects have .pdata sections")
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
