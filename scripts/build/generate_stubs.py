#!/usr/bin/env python3
"""Generate a PPC big-endian COFF object with stub functions.

Reads symbol names from stdin or a file (one per line) and creates a COFF
object where each symbol is a global function containing a single `blr`
instruction (4E 80 00 20). This allows the linker to resolve references
to these symbols without needing the actual function implementations.

Usage:
    python3 generate_stubs.py < symbols.txt > stubs.obj
    python3 generate_stubs.py symbols.txt -o stubs.obj
"""
import argparse
import struct
import sys


# PPC big-endian: blr = 0x4E800020
BLR = b'\x4E\x80\x00\x20'

# COFF constants
IMAGE_FILE_MACHINE_POWERPCBE = 0x01F2
IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_ALIGN_4BYTES = 0x00300000
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
IMAGE_SYM_DTYPE_FUNCTION = 0x20


def build_coff(symbols: list[str]) -> bytes:
    """Build a minimal COFF object with stub functions for each symbol."""
    n = len(symbols)
    if n == 0:
        raise ValueError("No symbols provided")

    # Section: .text containing n * 4 bytes (one blr per function)
    section_data = BLR * n
    section_size = len(section_data)
    section_name = b'.text\x00\x00\x00'

    # Build string table (for symbol names > 8 chars)
    # String table: 4-byte size prefix + null-terminated strings
    strtab_entries = []
    strtab_offset = 4  # starts after 4-byte size
    sym_name_refs = []  # (is_short, short_bytes_or_strtab_offset)

    for sym in symbols:
        encoded = sym.encode('ascii')
        if len(encoded) <= 8:
            # Short name (inline in symbol entry)
            sym_name_refs.append((True, encoded.ljust(8, b'\x00')))
        else:
            # Long name (string table reference)
            sym_name_refs.append((False, strtab_offset))
            strtab_entries.append(encoded + b'\x00')
            strtab_offset += len(encoded) + 1

    strtab_size = strtab_offset
    strtab_data = struct.pack('<I', strtab_size)
    for entry in strtab_entries:
        strtab_data += entry

    # COFF layout:
    # [0x00] COFF header (20 bytes)
    # [0x14] Section header (40 bytes) - .text
    # [0x3C] Section data (n * 4 bytes)
    # [0x3C + section_size] Symbol table
    # [after symbols] String table

    header_size = 20
    section_header_size = 40
    section_data_offset = header_size + section_header_size
    symtab_offset = section_data_offset + section_size

    # We need n+1 symbols: 1 section symbol + n function symbols
    # Actually, just n function symbols is sufficient for linking
    num_symbols = n

    # COFF header (20 bytes, little-endian)
    coff_header = struct.pack('<HHIIIHH',
        IMAGE_FILE_MACHINE_POWERPCBE,  # Machine
        1,                              # NumberOfSections
        0,                              # TimeDateStamp
        symtab_offset,                  # PointerToSymbolTable
        num_symbols,                    # NumberOfSymbols
        0,                              # SizeOfOptionalHeader
        0,                              # Characteristics
    )

    # Section header: .text (40 bytes, little-endian)
    section_chars = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_ALIGN_4BYTES |
                     IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ)
    section_header = struct.pack('<8sIIIIIIHHI',
        section_name,                   # Name
        0,                              # VirtualSize
        0,                              # VirtualAddress
        section_size,                   # SizeOfRawData
        section_data_offset,            # PointerToRawData
        0,                              # PointerToRelocations
        0,                              # PointerToLinenumbers
        0,                              # NumberOfRelocations
        0,                              # NumberOfLinenumbers
        section_chars,                  # Characteristics
    )

    # Symbol table entries (18 bytes each, little-endian)
    symtab_data = b''
    for i, (is_short, name_ref) in enumerate(sym_name_refs):
        if is_short:
            name_bytes = name_ref  # 8 bytes already
        else:
            # Zeroes (4 bytes) + string table offset (4 bytes)
            name_bytes = struct.pack('<II', 0, name_ref)

        sym_entry = name_bytes + struct.pack('<IhHBB',
            i * 4,                          # Value (offset in section)
            1,                              # SectionNumber (1-based)
            IMAGE_SYM_DTYPE_FUNCTION,       # Type (function)
            IMAGE_SYM_CLASS_EXTERNAL,       # StorageClass (EXTERNAL = global)
            0,                              # NumberOfAuxSymbols
        )
        symtab_data += sym_entry

    return coff_header + section_header + section_data + symtab_data + strtab_data


def main():
    parser = argparse.ArgumentParser(description='Generate PPC COFF stub functions')
    parser.add_argument('input', nargs='?', help='Input file with symbol names (default: stdin)')
    parser.add_argument('-o', '--output', help='Output COFF file (default: stdout)')
    args = parser.parse_args()

    if args.input:
        with open(args.input) as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()

    symbols = [line.strip() for line in lines if line.strip()]
    if not symbols:
        print("Error: no symbols provided", file=sys.stderr)
        sys.exit(1)

    coff_data = build_coff(symbols)

    if args.output:
        with open(args.output, 'wb') as f:
            f.write(coff_data)
        print(f"Generated {args.output}: {len(symbols)} stubs, {len(coff_data)} bytes",
              file=sys.stderr)
    else:
        sys.stdout.buffer.write(coff_data)


if __name__ == '__main__':
    main()
