#!/usr/bin/env python3
"""Cross-validate milo file entry tables against our native loader.

Reads .milo_xbox files independently (pure Python, no engine dependency) and
extracts the directory metadata: revision, type, name, entry count, and the
list of (class, name) pairs. This serves as ground truth to compare against
what the native DirLoader produces.

Usage:
    # Validate a single file
    python3 scripts/milo/validate_milo_entries.py world/shared/gen/director.milo_xbox

    # Validate all files in a directory
    python3 scripts/milo/validate_milo_entries.py ~/code/milohax/milo-engine-libs/.../dc3/world/

    # JSON output for automated comparison
    python3 scripts/milo/validate_milo_entries.py --json director.milo_xbox
"""

import struct
import sys
import os
import json
import zlib
import io


def read_milo_container(path):
    """Read and decompress a .milo_xbox container, returning the raw scene data."""
    with open(path, 'rb') as f:
        data = f.read()

    if len(data) < 0x10:
        raise ValueError(f"File too small: {len(data)} bytes")

    magic = struct.unpack_from('<I', data, 0)[0]
    start_offset = struct.unpack_from('<I', data, 4)[0]
    num_blocks = struct.unpack_from('<I', data, 8)[0]
    max_block_size = struct.unpack_from('<I', data, 0xC)[0]

    # Read block sizes
    block_sizes = []
    for i in range(min(num_blocks, 512)):
        block_sizes.append(struct.unpack_from('<I', data, 0x10 + i * 4)[0])

    if magic == 0xCABEDEAF:
        # Uncompressed
        offset = start_offset
        chunks = []
        for sz in block_sizes:
            chunks.append(data[offset:offset + sz])
            offset += sz
        return b''.join(chunks)

    elif magic == 0xCBBEDEAF:
        # Zlib compressed
        offset = start_offset
        chunks = []
        for sz in block_sizes:
            raw_sz = sz & 0xFFFFFF
            compressed = (sz & 0xFF000000) == 0
            chunk = data[offset:offset + raw_sz]
            if compressed and raw_sz > 0:
                try:
                    chunk = zlib.decompress(chunk)
                except zlib.error:
                    pass  # Use raw data on decompression failure
            chunks.append(chunk)
            offset += raw_sz
        return b''.join(chunks)

    elif magic == 0xCDBEDEAF:
        # Zlib with uncompressed size prefix
        offset = start_offset
        chunks = []
        for sz in block_sizes:
            raw_sz = sz & 0xFFFFFF
            compressed = (sz & 0xFF000000) == 0
            chunk_data = data[offset:offset + raw_sz]
            if compressed and raw_sz > 4:
                # First 4 bytes = uncompressed size
                try:
                    chunk_data = zlib.decompress(chunk_data[4:])
                except zlib.error:
                    chunk_data = chunk_data[4:]
            chunks.append(chunk_data)
            offset += raw_sz
        return b''.join(chunks)

    else:
        raise ValueError(f"Unknown milo magic: 0x{magic:08X}")


def read_be_u32(data, offset):
    return struct.unpack_from('>I', data, offset)[0]


def read_be_string(data, offset):
    """Read a big-endian length-prefixed string."""
    length = read_be_u32(data, offset)
    if length > 512:
        raise ValueError(f"String length {length} > 512 at offset {offset}")
    s = data[offset + 4:offset + 4 + length].decode('latin-1', errors='replace')
    return s, offset + 4 + length


def parse_directory_meta(scene_data):
    """Parse the directory metadata from decompressed scene data."""
    result = {}
    off = 0

    # Revision (big-endian u32)
    revision = read_be_u32(scene_data, off)
    off += 4
    result['revision'] = revision

    if revision > 50:
        raise ValueError(f"Suspicious revision {revision}, may be little-endian")

    if revision > 10:
        # Type and name
        dir_type, off = read_be_string(scene_data, off)
        dir_name, off = read_be_string(scene_data, off)
        result['type'] = dir_type
        result['name'] = dir_name

        # String table: count + size
        hash_count = read_be_u32(scene_data, off)
        off += 4
        hash_size = read_be_u32(scene_data, off)
        off += 4
        result['hash_count'] = hash_count
        result['hash_size'] = hash_size

        if revision >= 32:
            off += 1  # Unknown bool

    # Entry count
    entry_count = read_be_u32(scene_data, off)
    off += 4
    result['entry_count'] = entry_count

    # Read entries
    entries = []
    for i in range(entry_count):
        try:
            entry_type, off = read_be_string(scene_data, off)
            entry_name, off = read_be_string(scene_data, off)
            entries.append({'type': entry_type, 'name': entry_name})
        except (ValueError, struct.error) as e:
            entries.append({'type': f'<ERROR at entry {i}>', 'name': str(e)})
            break

    result['entries'] = entries
    return result


def validate_file(path, verbose=True):
    """Validate a single .milo_xbox file."""
    try:
        scene_data = read_milo_container(path)
        meta = parse_directory_meta(scene_data)

        if verbose:
            basename = os.path.basename(path)
            print(f"  OK: {basename}")
            print(f"      rev={meta['revision']} type={meta.get('type', 'N/A')} "
                  f"name={meta.get('name', 'N/A')} entries={meta['entry_count']}")

        return {'path': path, 'status': 'ok', **meta}

    except Exception as e:
        if verbose:
            print(f"  FAIL: {os.path.basename(path)} — {e}")
        return {'path': path, 'status': 'error', 'error': str(e)}


def main():
    args = sys.argv[1:]
    json_mode = '--json' in args
    if json_mode:
        args.remove('--json')

    if not args:
        print(__doc__)
        sys.exit(1)

    paths = []
    for arg in args:
        if os.path.isdir(arg):
            for root, dirs, files in os.walk(arg):
                for f in sorted(files):
                    if f.endswith('.milo_xbox'):
                        paths.append(os.path.join(root, f))
        elif os.path.isfile(arg):
            paths.append(arg)
        else:
            print(f"Not found: {arg}", file=sys.stderr)

    results = []
    ok = 0
    fail = 0
    for p in paths:
        r = validate_file(p, verbose=not json_mode)
        results.append(r)
        if r['status'] == 'ok':
            ok += 1
        else:
            fail += 1

    if json_mode:
        json.dump(results, sys.stdout, indent=2)
        print()
    else:
        print(f"\nTotal: {len(paths)}  OK: {ok}  Failed: {fail}")


if __name__ == '__main__':
    main()
