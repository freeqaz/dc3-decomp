#!/usr/bin/env python3
"""
Minimal XEX2 packer for devkit/debug PE files.

Creates an unencrypted, uncompressed XEX container around a PPC PE executable.
Copies essential optional headers from the original XEX (entry point, image base,
execution ID, etc.) but updates the PE offset and image size for the new PE.

Usage:
    python3 scripts/build_xex.py                           # Default: build PE → XEX
    python3 scripts/build_xex.py --pe path/to/pe.exe       # Custom PE
    python3 scripts/build_xex.py --output path/to/out.xex  # Custom output
"""

import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def parse_pe_header(pe_data):
    """Extract key fields from PE header."""
    # MZ header
    if pe_data[0:2] != b'MZ':
        raise ValueError("Not a valid PE file (no MZ magic)")

    pe_offset = struct.unpack_from('<I', pe_data, 0x3C)[0]

    # PE signature
    if pe_data[pe_offset:pe_offset+4] != b'PE\x00\x00':
        raise ValueError("Not a valid PE file (no PE signature)")

    # COFF header
    num_sections = struct.unpack_from('<H', pe_data, pe_offset + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', pe_data, pe_offset + 20)[0]

    # Optional header
    opt_off = pe_offset + 24
    # AddressOfEntryPoint at optional header + 16
    entry_rva = struct.unpack_from('<I', pe_data, opt_off + 16)[0]
    # ImageBase at optional header + 28 (PE32)
    image_base = struct.unpack_from('<I', pe_data, opt_off + 28)[0]
    # SizeOfImage at optional header + 56
    size_of_image = struct.unpack_from('<I', pe_data, opt_off + 56)[0]

    return {
        'entry_rva': entry_rva,
        'image_base': image_base,
        'size_of_image': size_of_image,
        'num_sections': num_sections,
    }


def parse_original_xex(xex_path):
    """Extract optional headers from the original XEX for reuse."""
    with open(xex_path, 'rb') as f:
        data = f.read()

    # Parse XEX2 header
    magic = data[0:4]
    if magic != b'XEX2':
        raise ValueError(f"Not a XEX2 file: {magic}")

    mod_flags = struct.unpack('>I', data[4:8])[0]
    pe_offset = struct.unpack('>I', data[8:12])[0]
    sec_info_offset = struct.unpack('>I', data[16:20])[0]
    opt_count = struct.unpack('>I', data[20:24])[0]

    # Parse optional headers
    opt_headers = []
    off = 24
    for i in range(opt_count):
        hdr_id = struct.unpack('>I', data[off:off+4])[0]
        hdr_val = struct.unpack('>I', data[off+4:off+8])[0]
        opt_headers.append((hdr_id, hdr_val))
        off += 8

    # Parse security info
    si_data = data[sec_info_offset:pe_offset]

    # Extract optional header data
    # Key type rules from XEX2 spec:
    #   0x00: inline value (no external data)
    #   0x01: inline value
    #   0xFF: variable-length (first 4 bytes at offset = total size)
    #   0x02-0xFE: fixed-size (key_type * 4 bytes at offset)
    bff_headers = {}
    for hdr_id, hdr_val in opt_headers:
        key_type = hdr_id & 0xFF
        if key_type <= 0x01:
            # Inline value
            bff_headers[hdr_id] = ('inline', hdr_val)
        elif key_type == 0xFF:
            # Pointer to variable-length data
            size = struct.unpack('>I', data[hdr_val:hdr_val+4])[0]
            bff_headers[hdr_id] = ('blob', data[hdr_val:hdr_val+size])
        else:
            # Fixed-size data (key_type * 4 bytes)
            size = key_type * 4
            bff_headers[hdr_id] = ('fixed', data[hdr_val:hdr_val+size])

    return {
        'mod_flags': mod_flags,
        'opt_headers': opt_headers,
        'bff_headers': bff_headers,
        'security_info': si_data,
        'original_data': data,
    }


def build_xex(pe_data, original_xex_info, pe_info):
    """Build a minimal XEX2 container around the PE data."""
    # We'll build the XEX in pieces:
    # 1. XEX2 header (24 bytes)
    # 2. Optional headers (8 bytes each)
    # 3. Padding to align
    # 4. Optional header data (variable-length blobs)
    # 5. Security info
    # 6. PE data (aligned to 0x1000)

    entry_point = pe_info['image_base'] + pe_info['entry_rva']
    image_base = pe_info['image_base']

    # Select which optional headers to include
    # We need at minimum: entry point, image base, base file format,
    # execution ID, system flags, default stack size
    inline_headers = []
    blob_headers = []

    orig = original_xex_info['bff_headers']

    # Entry point (0x10100) - inline
    inline_headers.append((0x00010100, entry_point))

    # Image base address (0x10201) - inline
    inline_headers.append((0x00010201, image_base))

    # Helper to get inline value from parsed headers
    def get_inline(hdr_id, default=None):
        if hdr_id in orig and orig[hdr_id][0] == 'inline':
            return orig[hdr_id][1]
        return default

    # Default stack size (0x20200) - inline
    inline_headers.append((0x00020200, get_inline(0x00020200, 0x00040000)))

    # System flags (0x30000) - inline
    inline_headers.append((0x00030000, get_inline(0x00030000, 0x00000220)))

    # Base File Format (0x3FF) - create unencrypted/raw descriptor
    # struct { uint32_t size; uint16_t enc_type; uint16_t comp_type;
    #          ... hash entries for raw blocks }
    # For raw (type 1), we need block entries: each is {data_size, zero_size}
    pe_aligned = len(pe_data)
    # Pad PE to page boundary
    if pe_aligned % 0x1000:
        pe_aligned = (pe_aligned + 0xFFF) & ~0xFFF

    # Raw compression: one block covering entire image
    # Format: size(4) + enc(2) + comp(2) + {data_size(4) + zero_size(4)} per block
    bff = struct.pack('>I', 0x18)  # size of this struct (24 bytes)
    bff += struct.pack('>HH', 0, 1)  # enc=none, comp=raw
    bff += struct.pack('>II', pe_aligned, 0)  # data block: all data, no zeros
    bff += struct.pack('>II', 0, 0)  # terminator
    blob_headers.append((0x000003FF, bff))

    # Execution ID (0x40006) - copy from original if available
    if 0x00040006 in orig:
        blob_headers.append((0x00040006, orig[0x00040006][1]))

    # Game Ratings (0x40310) - copy from original
    if 0x00040310 in orig:
        blob_headers.append((0x00040310, orig[0x00040310][1]))

    # LAN Key (0x40404) - copy from original
    if 0x00040404 in orig:
        blob_headers.append((0x00040404, orig[0x00040404][1]))

    # Original PE Name (0x183FF) - copy from original
    if 0x000183FF in orig:
        blob_headers.append((0x000183FF, orig[0x000183FF][1]))

    # Import Libraries (0x103FF) - copy from original
    if 0x000103FF in orig:
        blob_headers.append((0x000103FF, orig[0x000103FF][1]))

    # TLS Info (0x20104) - copy from original
    if 0x00020104 in orig:
        blob_headers.append((0x00020104, orig[0x00020104][1]))

    # Resource Info (0x2FF) - copy from original
    if 0x000002FF in orig:
        blob_headers.append((0x000002FF, orig[0x000002FF][1]))

    # Checksum/Timestamp (0x18002) - fixed 8-byte
    if 0x00018002 in orig:
        blob_headers.append((0x00018002, orig[0x00018002][1]))

    # Static Libraries (0x200FF) - copy from original
    if 0x000200FF in orig:
        blob_headers.append((0x000200FF, orig[0x000200FF][1]))

    # Alternate Title Memory (0x40801) - inline
    if 0x00040801 in orig and orig[0x00040801][0] == 'inline':
        inline_headers.append((0x00040801, orig[0x00040801][1]))

    # Title workspace size (0x40201) - inline
    val = get_inline(0x00040201)
    if val is not None:
        inline_headers.append((0x00040201, val))

    # Enabled for callcap (0x18102) - fixed 8-byte
    if 0x00018102 in orig:
        blob_headers.append((0x00018102, orig[0x00018102][1]))

    # Default heap size (0x20301) - inline
    val = get_inline(0x00020301)
    if val is not None:
        inline_headers.append((0x00020301, val))

    # System Flags Ext (0x30100) - inline (type 0x00)
    val = get_inline(0x00030100)
    if val is not None:
        inline_headers.append((0x00030100, val))

    # Now layout the XEX:
    total_opt = len(inline_headers) + len(blob_headers)
    header_table_end = 24 + total_opt * 8

    # Place blob data after header table
    blob_offset = header_table_end
    # Align to 4 bytes
    if blob_offset % 4:
        blob_offset = (blob_offset + 3) & ~3

    # Build blob data and record offsets
    blob_data = bytearray()
    blob_offsets = {}
    for hdr_id, bdata in blob_headers:
        # Align each blob to 4 bytes
        while len(blob_data) % 4:
            blob_data.append(0)
        blob_offsets[hdr_id] = blob_offset + len(blob_data)
        blob_data.extend(bdata)

    # Security info follows blobs
    si_offset = blob_offset + len(blob_data)
    while si_offset % 4:
        si_offset += 1

    # Build security info
    # Minimal security info: header_size(4) + image_size(4) + RSA sig(256) +
    # ... lots of fields. Let's copy from original and patch image size.
    orig_si = bytearray(original_xex_info['security_info'])
    # Update image size at offset 4
    struct.pack_into('>I', orig_si, 4, pe_info['size_of_image'])

    # PE data offset - align to 0x1000
    pe_file_offset = si_offset + len(orig_si)
    pe_file_offset = (pe_file_offset + 0xFFF) & ~0xFFF

    # Build the XEX
    xex = bytearray(pe_file_offset + pe_aligned)

    # XEX2 header
    xex[0:4] = b'XEX2'
    struct.pack_into('>I', xex, 4, 0x00000001)  # Title module
    struct.pack_into('>I', xex, 8, pe_file_offset)  # PE data offset
    struct.pack_into('>I', xex, 12, 0)  # Reserved
    struct.pack_into('>I', xex, 16, si_offset)  # Security info offset
    struct.pack_into('>I', xex, 20, total_opt)  # Optional header count

    # Write optional headers
    off = 24
    all_headers = []
    for hdr_id, hdr_val in inline_headers:
        all_headers.append((hdr_id, hdr_val))
    for hdr_id, _ in blob_headers:
        all_headers.append((hdr_id, blob_offsets[hdr_id]))

    # Sort by header ID (Xenia might expect this)
    all_headers.sort(key=lambda x: x[0])

    for hdr_id, hdr_val in all_headers:
        struct.pack_into('>I', xex, off, hdr_id)
        struct.pack_into('>I', xex, off+4, hdr_val)
        off += 8

    # Write blob data
    xex[blob_offset:blob_offset+len(blob_data)] = blob_data

    # Write security info
    xex[si_offset:si_offset+len(orig_si)] = orig_si

    # Write PE data
    xex[pe_file_offset:pe_file_offset+len(pe_data)] = pe_data
    # Zero-pad to aligned size
    # (already zeroed from bytearray initialization)

    return bytes(xex)


def main():
    parser = argparse.ArgumentParser(description="Build XEX2 from PE executable")
    parser.add_argument("--pe", default=str(ROOT / "build" / "373307D9" / "default.exe"),
                       help="Path to PE executable")
    parser.add_argument("--original-xex", default=str(ROOT / "orig" / "373307D9" / "default.xex"),
                       help="Original XEX to copy headers from")
    parser.add_argument("--output", "-o", default=str(ROOT / "build" / "373307D9" / "default.xex"),
                       help="Output XEX path")
    args = parser.parse_args()

    # Read PE
    pe_path = Path(args.pe)
    if not pe_path.exists():
        print(f"Error: PE file not found: {pe_path}")
        print("Run 'ninja link' first.")
        return 1

    print(f"Reading PE: {pe_path} ({pe_path.stat().st_size:,} bytes)")
    with open(pe_path, 'rb') as f:
        pe_data = f.read()

    pe_info = parse_pe_header(pe_data)
    print(f"  Entry RVA: {pe_info['entry_rva']:#x}")
    print(f"  Image base: {pe_info['image_base']:#x}")
    print(f"  Size of image: {pe_info['size_of_image']:#x} ({pe_info['size_of_image']:,} bytes)")
    print(f"  Entry point: {pe_info['image_base'] + pe_info['entry_rva']:#x}")

    # Parse original XEX
    orig_xex_path = Path(args.original_xex)
    print(f"\nParsing original XEX: {orig_xex_path}")
    orig_info = parse_original_xex(orig_xex_path)
    print(f"  {len(orig_info['opt_headers'])} optional headers")
    print(f"  Security info: {len(orig_info['security_info'])} bytes")

    # Build XEX
    print(f"\nBuilding XEX...")
    xex_data = build_xex(pe_data, orig_info, pe_info)
    print(f"  XEX size: {len(xex_data):,} bytes")

    # Verify
    if xex_data[0:4] != b'XEX2':
        print("ERROR: Invalid XEX magic!")
        return 1

    pe_offset = struct.unpack('>I', xex_data[8:12])[0]
    if xex_data[pe_offset:pe_offset+2] == b'MZ':
        print(f"  PE at offset {pe_offset:#x}: OK (MZ header found)")
    else:
        print(f"  WARNING: No MZ header at offset {pe_offset:#x}")

    # Write output
    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(xex_data)
    print(f"\nWrote: {out_path} ({len(xex_data):,} bytes)")

    return 0


if __name__ == '__main__':
    exit(main())
