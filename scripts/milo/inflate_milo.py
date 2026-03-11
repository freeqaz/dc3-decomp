#!/usr/bin/env python3
"""Decompress Harmonix .milo_xbox / .milo / .rnd files.

Supports all milo compression variants:
  A (0xCABEDEAF) - Uncompressed
  B (0xCBBEDEAF) - zlib
  C (0xCCBEDEAF) - gzip
  D (0xCDBEDEAF) - Hybrid (mixed compressed/uncompressed blocks)

Usage:
  inflate_milo.py <input> [output]          Decompress single file
  inflate_milo.py --dir <directory>         Decompress all milos in directory
  inflate_milo.py --info <input>            Show compression info without extracting
"""

import argparse
import gzip
import os
import struct
import sys
import zlib


MILO_EXTENSIONS = (
    ".milo", ".milo_xbox", ".milo_ps3", ".milo_wii", ".milo_ps2",
    ".rnd", ".rnd_xbox", ".rnd_gc", ".rnd_gz", ".rnd_ps2",
    ".gh", ".kr",
)

VERSION_NAMES = {
    b'\xaf\xde\xbe\xca': ('A', 'Uncompressed'),
    b'\xaf\xde\xbe\xcb': ('B', 'zlib'),
    b'\xaf\xde\xbe\xcc': ('C', 'gzip'),
    b'\xaf\xde\xbe\xcd': ('D', 'Hybrid (zlib + uncompressed)'),
}


def read_milo_header(f):
    """Read milo file header. Returns (version_char, description, offset, block_sizes) or None."""
    magic = f.read(4)
    info = VERSION_NAMES.get(magic)
    if info is None:
        return None

    version_char, description = info
    offset, num_blocks, max_block_size = struct.unpack("<III", f.read(12))
    block_sizes = list(struct.unpack(f"<{num_blocks}I", f.read(4 * num_blocks)))

    return version_char, description, offset, block_sizes, max_block_size


def decompress_milo(filepath):
    """Decompress a milo file. Returns (raw_bytes, version_info) or raises ValueError."""
    with open(filepath, "rb") as f:
        header = read_milo_header(f)
        if header is None:
            raise ValueError(f"Not a milo file: {filepath}")

        version, desc, data_offset, block_sizes, max_block = header
        f.seek(data_offset)

        data = bytearray()
        for block_size in block_sizes:
            if version == 'A':
                data.extend(f.read(block_size))

            elif version == 'B':
                raw = f.read(block_size)
                z = zlib.decompressobj()
                z.decompress(bytes([0x78, 0x9c]))
                data.extend(z.decompress(raw))

            elif version == 'C':
                raw = f.read(block_size)
                data.extend(gzip.decompress(raw))

            elif version == 'D':
                if block_size >= 0x1000000:  # Already uncompressed
                    actual = block_size - 0x1000000
                    data.extend(f.read(actual))
                else:
                    f.read(4)  # Skip uncompressed size field
                    raw = f.read(block_size - 4)
                    z = zlib.decompressobj()
                    z.decompress(bytes([0x78, 0x9c]))
                    data.extend(z.decompress(raw))

    return bytes(data), (version, desc, len(block_sizes), max_block)


def milo_info(filepath):
    """Print compression info for a milo file."""
    with open(filepath, "rb") as f:
        header = read_milo_header(f)
        if header is None:
            print(f"  {filepath}: Not a milo file")
            return False

        version, desc, data_offset, block_sizes, max_block = header
        file_size = os.path.getsize(filepath)
        total_blocks = len(block_sizes)
        compressed_size = sum(block_sizes)

        print(f"  {os.path.basename(filepath)}:")
        print(f"    Version:     {version} ({desc})")
        print(f"    File size:   {file_size:,} bytes")
        print(f"    Blocks:      {total_blocks}")
        print(f"    Max block:   {max_block:,} bytes")
        print(f"    Data offset: 0x{data_offset:x}")
        return True


def inflate_file(input_path, output_path=None):
    """Decompress a single milo file."""
    if output_path is None:
        base, ext = os.path.splitext(input_path)
        output_path = base + ".dec" + ext

    data, (version, desc, num_blocks, _) = decompress_milo(input_path)

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(data)

    print(f"  {os.path.basename(input_path)} -> {os.path.basename(output_path)}")
    print(f"    Compression: {version} ({desc}), {num_blocks} blocks")
    print(f"    Decompressed: {len(data):,} bytes")
    return output_path


def inflate_directory(dir_path, output_dir=None, recursive=True):
    """Decompress all milo files in a directory."""
    count = 0
    errors = 0

    for root, dirs, files in os.walk(dir_path):
        for name in files:
            if not name.lower().endswith(MILO_EXTENSIONS):
                continue
            if name.startswith("dec_") or ".dec." in name:
                continue

            input_path = os.path.join(root, name)

            if output_dir:
                rel = os.path.relpath(input_path, dir_path)
                base, ext = os.path.splitext(rel)
                out_path = os.path.join(output_dir, base + ".dec" + ext)
            else:
                out_path = None  # default: alongside original

            try:
                inflate_file(input_path, out_path)
                count += 1
            except ValueError as e:
                print(f"  SKIP: {e}")
            except Exception as e:
                print(f"  ERROR: {input_path}: {e}")
                errors += 1

        if not recursive:
            break

    print(f"\nDecompressed {count} files ({errors} errors)")


def main():
    parser = argparse.ArgumentParser(
        description="Decompress Harmonix .milo_xbox / .milo / .rnd files"
    )
    parser.add_argument("input", nargs="?", help="Input .milo file or directory")
    parser.add_argument("output", nargs="?", help="Output path (file or directory)")
    parser.add_argument("--dir", metavar="DIR", help="Decompress all milos in directory")
    parser.add_argument("--info", metavar="FILE", help="Show compression info only")
    parser.add_argument("--recursive", action="store_true", default=True,
                        help="Recurse into subdirectories (default: true)")
    parser.add_argument("--no-recursive", action="store_false", dest="recursive")

    args = parser.parse_args()

    if args.info:
        milo_info(args.info)
    elif args.dir:
        inflate_directory(args.dir, args.output, recursive=args.recursive)
    elif args.input:
        if os.path.isdir(args.input):
            inflate_directory(args.input, args.output, recursive=args.recursive)
        else:
            inflate_file(args.input, args.output)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
