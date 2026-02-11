#!/usr/bin/env python3
"""
Fix COFF objects with .pdata sections that cause LNK1223 errors.

The X360 MSVC linker strictly validates .pdata contributions. Split objects
from dtk can have invalid .pdata entries (bad function boundaries, multiple
sections). This script renames ALL .pdata sections to .pdatX to bypass
the linker's validation while preserving the data in the output.

Usage:
    python3 scripts/fix_pdata.py [--dry-run] [--restore]
"""

import argparse
import json
import struct
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def fix_object(obj_path, restore=False):
    """Rename .pdata sections. Returns (path, fixed, error)."""
    try:
        with open(obj_path, 'rb') as f:
            data = bytearray(f.read())

        if len(data) < 20:
            return obj_path, False, None

        _, num_sections = struct.unpack_from('<HH', data, 0)
        opt_hdr_size = struct.unpack_from('<H', data, 16)[0]

        modified = False
        offset = 20 + opt_hdr_size
        pdata_idx = 0
        for i in range(num_sections):
            if offset + 40 > len(data):
                break
            name = data[offset:offset+8]

            if restore:
                # Restore .pdatX back to .pdata
                stripped = name.rstrip(b'\x00')
                if stripped.startswith(b'.pdat') and stripped != b'.pdata':
                    data[offset:offset+8] = b'.pdata\x00\x00'
                    modified = True
            else:
                # Rename .pdata to .pdatN
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
        # Count how many have pdata
        count = 0
        for p in obj_paths:
            with open(p, 'rb') as f:
                data = f.read()
            if b'.pdata' in data[:2000]:  # Quick scan of headers
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
