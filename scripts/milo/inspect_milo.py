#!/usr/bin/env python3
"""Inspect object types and names inside a decompressed milo scene file.

Milo ObjectDir format (after decompression):
  - Header: revision, type string, entry count
  - Entry table: list of (type, name) pairs
  - Object data blocks

This reads the ObjectDir entry table to list all objects by type.

Usage:
  inspect_milo.py <file.milo_xbox>                    List all object types
  inspect_milo.py <file.milo_xbox> --type EventTrigger Filter by type
  inspect_milo.py <file.milo_xbox> --summary           Type counts only
  inspect_milo.py <file.milo_xbox> --search UITrigger   Search raw bytes for string
"""

import argparse
import os
import struct
import sys
from collections import defaultdict

# Add parent for inflate_milo import
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from inflate_milo import decompress_milo, read_milo_header


def read_string_be(data, offset):
    """Read a big-endian length-prefixed string from data."""
    if offset + 4 > len(data):
        return None, offset
    length = struct.unpack_from(">I", data, offset)[0]
    offset += 4
    if length == 0 or length > 4096:
        return "", offset
    if offset + length > len(data):
        return None, offset
    try:
        s = data[offset:offset + length].decode("ascii", errors="replace")
    except Exception:
        s = data[offset:offset + length].hex()
    return s, offset + length


def parse_object_dir_entries(data):
    """Parse the ObjectDir entry table from decompressed milo data.

    Returns list of (type_name, object_name) tuples.
    """
    if len(data) < 8:
        return []

    # ObjectDir starts with a revision number (big-endian u32)
    # Then the dir type string, then entry count
    offset = 0
    revision = struct.unpack_from(">I", data, offset)[0]
    offset += 4

    # Read dir type string
    dir_type, offset = read_string_be(data, offset)
    if dir_type is None:
        return []

    # Entry count
    if offset + 4 > len(data):
        return []
    num_entries = struct.unpack_from(">I", data, offset)[0]
    offset += 4

    # Sanity check
    if num_entries > 50000 or num_entries == 0:
        return []

    entries = []
    for i in range(num_entries):
        type_name, offset = read_string_be(data, offset)
        if type_name is None:
            break
        obj_name, offset = read_string_be(data, offset)
        if obj_name is None:
            break
        entries.append((type_name, obj_name))

    return entries


def search_raw_bytes(data, pattern):
    """Search for a string pattern in raw binary data. Returns offsets."""
    pattern_bytes = pattern.encode("ascii")
    results = []
    start = 0
    while True:
        idx = data.find(pattern_bytes, start)
        if idx == -1:
            break
        # Try to extract context around the match
        ctx_start = max(0, idx - 16)
        ctx_end = min(len(data), idx + len(pattern_bytes) + 32)
        context = data[ctx_start:ctx_end]
        # Show printable chars
        ctx_str = "".join(chr(b) if 32 <= b < 127 else "." for b in context)
        results.append((idx, ctx_str))
        start = idx + 1
    return results


def main():
    parser = argparse.ArgumentParser(description="Inspect milo scene object types")
    parser.add_argument("file", help="Input .milo_xbox file")
    parser.add_argument("--type", "-t", metavar="TYPE", help="Filter by object type")
    parser.add_argument("--summary", "-s", action="store_true", help="Type counts only")
    parser.add_argument("--search", metavar="STRING", help="Search raw bytes for string pattern")
    parser.add_argument("--raw", action="store_true", help="Skip decompression (file already decompressed)")

    args = parser.parse_args()

    if not os.path.isfile(args.file):
        print(f"ERROR: File not found: {args.file}")
        sys.exit(1)

    # Read/decompress the file
    try:
        if args.raw:
            with open(args.file, "rb") as f:
                data = f.read()
        else:
            # Check if it's a milo container or raw ObjectDir
            with open(args.file, "rb") as f:
                magic = f.read(4)
            if magic in (b'\xaf\xde\xbe\xca', b'\xaf\xde\xbe\xcb',
                         b'\xaf\xde\xbe\xcc', b'\xaf\xde\xbe\xcd'):
                data, info = decompress_milo(args.file)
                ver, desc = info[0], info[1]
                print(f"Milo: {os.path.basename(args.file)} (version {ver}: {desc})")
            else:
                with open(args.file, "rb") as f:
                    data = f.read()
                print(f"Raw file: {os.path.basename(args.file)}")
    except Exception as e:
        print(f"ERROR reading file: {e}")
        sys.exit(1)

    print(f"Data size: {len(data):,} bytes\n")

    # Raw byte search mode
    if args.search:
        results = search_raw_bytes(data, args.search)
        if results:
            print(f"Found {len(results)} occurrences of '{args.search}':\n")
            for offset, context in results[:50]:
                print(f"  0x{offset:08x}: {context}")
            if len(results) > 50:
                print(f"  ... and {len(results) - 50} more")
        else:
            print(f"'{args.search}' NOT FOUND in raw data")
        return

    # Parse entry table
    entries = parse_object_dir_entries(data)
    if not entries:
        print("Could not parse ObjectDir entry table.")
        print("Trying raw string search for common types...")
        for typename in ["EventTrigger", "UITrigger", "PropAnim", "RndMesh",
                         "RndMat", "RndTex", "UILabel", "UIButton", "RndTransAnim"]:
            results = search_raw_bytes(data, typename)
            if results:
                print(f"  {typename}: {len(results)} raw occurrences")
        return

    # Filter
    if args.type:
        entries = [(t, n) for t, n in entries if args.type.lower() in t.lower()]

    # Summary mode
    if args.summary:
        counts = defaultdict(int)
        for type_name, _ in entries:
            counts[type_name] += 1
        print(f"{'Type':<30} {'Count':>6}")
        print("-" * 38)
        for t in sorted(counts, key=lambda x: counts[x], reverse=True):
            print(f"{t:<30} {counts[t]:>6}")
        print("-" * 38)
        print(f"{'TOTAL':<30} {len(entries):>6}")
    else:
        # Full listing
        counts = defaultdict(int)
        for type_name, _ in entries:
            counts[type_name] += 1

        # Print summary first
        print(f"{'Type':<30} {'Count':>6}")
        print("-" * 38)
        for t in sorted(counts, key=lambda x: counts[x], reverse=True):
            print(f"{t:<30} {counts[t]:>6}")
        print("-" * 38)
        print(f"{'TOTAL':<30} {len(entries):>6}")

        # Then full list
        if len(entries) <= 500:
            print(f"\n{'Type':<25} {'Name'}")
            print("-" * 70)
            for type_name, obj_name in entries:
                print(f"{type_name:<25} {obj_name}")


if __name__ == "__main__":
    main()
