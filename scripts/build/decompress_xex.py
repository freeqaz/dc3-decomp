#!/usr/bin/env python3
"""
Decompress XEX with basic compression (type 1) and extract PE.

For debug/devkit XEXs (encryption type 0), this simply reads the block
descriptors and concatenates data blocks with zero padding.
"""

import argparse
import struct
import sys
from pathlib import Path


def decompress_xex(xex_path: Path, output_path: Path) -> bytes:
    """Decompress a XEX with basic compression and return the PE data."""
    with open(xex_path, 'rb') as f:
        data = f.read()

    # Parse XEX2 header
    if data[0:4] != b'XEX2':
        raise ValueError(f"Not a XEX2 file: {data[0:4]}")

    pe_offset = struct.unpack('>I', data[8:12])[0]
    opt_count = struct.unpack('>I', data[20:24])[0]

    # Find the Base File Format header (0x3FF)
    off = 24
    bff_offset = None
    for i in range(opt_count):
        hdr_id = struct.unpack('>I', data[off:off+4])[0]
        hdr_val = struct.unpack('>I', data[off+4:off+8])[0]
        if hdr_id == 0x000003FF:  # Base File Format
            bff_offset = hdr_val
            break
        off += 8

    if bff_offset is None:
        raise ValueError("No Base File Format header found")

    # Read XEXFileDataDescriptor
    size = struct.unpack('>I', data[bff_offset:bff_offset+4])[0]
    enc_type = struct.unpack('>H', data[bff_offset+4:bff_offset+6])[0]
    comp_type = struct.unpack('>H', data[bff_offset+6:bff_offset+8])[0]

    print(f"Encryption type: {enc_type} (0=none, 1=encrypted)")
    print(f"Compression type: {comp_type} (0=raw, 1=basic, 2=lzx)")

    if enc_type != 0:
        raise ValueError(f"Encrypted XEX not supported (enc_type={enc_type})")

    if comp_type == 0:
        # Raw - just copy PE directly
        print("Raw PE, copying directly...")
        pe_data = data[pe_offset:]
    elif comp_type == 1:
        # Basic compression - block-based with zero padding
        num_blocks = (size - 8) // 8
        print(f"Basic compression: {num_blocks} blocks")

        pe_data = bytearray()
        data_offset = pe_offset

        for i in range(num_blocks):
            block_off = bff_offset + 8 + i * 8
            blk_size = struct.unpack('>I', data[block_off:block_off+4])[0]
            blk_zeros = struct.unpack('>I', data[block_off+4:block_off+8])[0]

            print(f"  Block {i}: {blk_size:#x} bytes + {blk_zeros:#x} zeros")

            # Read data block
            pe_data.extend(data[data_offset:data_offset + blk_size])
            data_offset += blk_size

            # Add zero padding
            pe_data.extend(b'\x00' * blk_zeros)

        pe_data = bytes(pe_data)
    elif comp_type == 2:
        raise ValueError("LZX compression not supported - use xextool -cu first")
    else:
        raise ValueError(f"Unknown compression type: {comp_type}")

    # Verify PE magic
    if pe_data[0:2] != b'MZ':
        raise ValueError(f"Invalid PE magic: {pe_data[0:2]}")

    pe_sig_offset = struct.unpack('<I', pe_data[0x3C:0x40])[0]
    if pe_data[pe_sig_offset:pe_sig_offset+4] != b'PE\x00\x00':
        raise ValueError(f"Invalid PE signature at offset {pe_sig_offset}")

    print(f"PE size: {len(pe_data):,} bytes")

    # Write output
    if output_path:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'wb') as f:
            f.write(pe_data)
        print(f"Wrote: {output_path}")

    return pe_data


def extract_import_data(pe_data: bytes, output_path: Path = None) -> bytes:
    """
    Extract import ordinal data from PE at RVA 0x600-0x2000.

    The import data contains ordinal values in big-endian format with
    record_type in the high byte (0x00=variable, 0x01=thunk).
    """
    # Import data is typically at RVA 0x600
    import_rva_start = 0x600
    import_rva_end = 0x2000

    # Find where valid import data ends
    actual_end = import_rva_start
    for rva in range(import_rva_start, import_rva_end, 4):
        val = struct.unpack('>I', pe_data[rva:rva+4])[0]
        record_type = (val >> 24) & 0xFF
        ordinal = val & 0xFFFFFF

        # Check if this looks like valid import data
        if record_type in (0x00, 0x01) and 0 < ordinal < 0x10000:
            actual_end = rva + 4
        elif record_type == 0x00 and ordinal == 0:
            # End marker or padding
            pass

    import_data = pe_data[import_rva_start:actual_end]
    print(f"Import data: RVA 0x{import_rva_start:X}-0x{actual_end:X} ({len(import_data)} bytes)")

    if output_path:
        with open(output_path, 'wb') as f:
            f.write(import_data)
        print(f"Wrote: {output_path}")

    return import_data


def analyze_import_data(pe_data: bytes):
    """Analyze import data in the PE."""
    print("\n=== Import Data Analysis ===")

    # Scan for import data
    imports_found = []
    for rva in range(0x600, 0x2000, 4):
        val = struct.unpack('>I', pe_data[rva:rva+4])[0]
        record_type = (val >> 24) & 0xFF
        ordinal = val & 0xFFFF

        if record_type in (0x00, 0x01) and 0 < ordinal < 0x10000:
            imports_found.append((rva, record_type, ordinal))

    print(f"Found {len(imports_found)} import entries")

    # Group by type
    variables = [x for x in imports_found if x[1] == 0x00]
    thunks = [x for x in imports_found if x[1] == 0x01]

    print(f"  Variables (type 0x00): {len(variables)}")
    print(f"  Thunks (type 0x01): {len(thunks)}")

    # Show first few
    if imports_found:
        print("\nFirst 10 imports:")
        for rva, rtype, ordinal in imports_found[:10]:
            type_str = "VAR" if rtype == 0x00 else "THK"
            print(f"  0x{rva:04X}: {type_str} ordinal {ordinal}")


def main():
    parser = argparse.ArgumentParser(description="Decompress XEX and extract PE/import data")
    parser.add_argument("xex", type=Path, help="Input XEX file")
    parser.add_argument("--pe", "-p", type=Path, help="Output PE file")
    parser.add_argument("--imports", "-i", type=Path, help="Output import data file")
    parser.add_argument("--analyze", "-a", action="store_true", help="Analyze import data")
    args = parser.parse_args()

    # Decompress
    pe_data = decompress_xex(args.xex, args.pe)

    # Analyze if requested
    if args.analyze:
        analyze_import_data(pe_data)

    # Extract imports
    if args.imports:
        extract_import_data(pe_data, args.imports)

    return 0


if __name__ == '__main__':
    sys.exit(main())
