#!/usr/bin/env python3
"""
Minimal XEX2 packer for devkit/debug PE files.

Creates an unencrypted, uncompressed XEX container around a PPC PE executable.
Copies essential optional headers from the original XEX (entry point, image base,
execution ID, etc.) but updates the PE offset and image size for the new PE.

Also patches the PE's import thunks from PE ordinal format (0x80XXXXXX) to
XEX import format (0x00XXXXXX) so Xenia can properly resolve imports.

Usage:
    python3 scripts/build_xex.py                           # Default: build PE → XEX
    python3 scripts/build_xex.py --pe path/to/pe.exe       # Custom PE
    python3 scripts/build_xex.py --output path/to/out.xex  # Custom output
"""

import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_pe_offset_for_rva(pe_data, rva):
    """Find the file offset in PE data for a given RVA. Returns None if not found."""
    if pe_data[0:2] != b'MZ':
        return None

    pe_offset = struct.unpack_from('<I', pe_data, 0x3C)[0]
    if pe_data[pe_offset:pe_offset+4] != b'PE\x00\x00':
        return None

    # COFF header
    num_sections = struct.unpack_from('<H', pe_data, pe_offset + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', pe_data, pe_offset + 20)[0]

    # Parse sections to find which one contains this RVA
    section_off = pe_offset + 24 + opt_hdr_size
    for i in range(num_sections):
        vsize = struct.unpack_from('<I', pe_data, section_off + 8)[0]
        vaddr = struct.unpack_from('<I', pe_data, section_off + 12)[0]
        raw_size = struct.unpack_from('<I', pe_data, section_off + 16)[0]
        raw_offset = struct.unpack_from('<I', pe_data, section_off + 20)[0]

        if vaddr <= rva < vaddr + max(vsize, raw_size):
            # Found the section - convert RVA to file offset
            return raw_offset + (rva - vaddr)

        section_off += 40

    return None


def find_idata_section(pe_data):
    """Find the .idata section in PE and return (rva, size, file_offset)."""
    if pe_data[0:2] != b'MZ':
        return None

    pe_offset = struct.unpack_from('<I', pe_data, 0x3C)[0]
    if pe_data[pe_offset:pe_offset+4] != b'PE\x00\x00':
        return None

    # COFF header
    num_sections = struct.unpack_from('<H', pe_data, pe_offset + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', pe_data, pe_offset + 20)[0]

    # Parse sections
    section_off = pe_offset + 24 + opt_hdr_size
    for i in range(num_sections):
        name = pe_data[section_off:section_off+8].rstrip(b'\x00')
        vsize = struct.unpack_from('<I', pe_data, section_off + 8)[0]
        vaddr = struct.unpack_from('<I', pe_data, section_off + 12)[0]
        raw_size = struct.unpack_from('<I', pe_data, section_off + 16)[0]
        raw_offset = struct.unpack_from('<I', pe_data, section_off + 20)[0]

        if name == b'.idata':
            return vaddr, vsize, raw_offset

        section_off += 40

    return None


def copy_import_data_from_original(pe_data, orig_xex_data, orig_image_base=0x82000000):
    """
    Copy import ordinal data from original XEX to the decompiled PE.

    The original XEX has import ordinal data at RVA 0x600+ in the PE.
    The decompiled PE may have different data there.
    This function copies the import ordinal data from the original.

    Returns: patched PE data
    """
    # Find PE offset in original XEX
    orig_pe_offset = struct.unpack_from('>I', orig_xex_data, 8)[0]

    # Import data is typically at RVA 0x600-0x1000
    # Find the extent of import data by looking for record_type 0x00 values
    import_rva_start = 0x600
    import_rva_end = 0x1000

    # Scan to find where import data actually ends
    for rva in range(0x600, 0x2000, 4):
        file_off = orig_pe_offset + rva
        if file_off + 4 > len(orig_xex_data):
            break
        val = struct.unpack_from('>I', orig_xex_data, file_off)[0]
        record_type = (val >> 24) & 0xFF
        ordinal = val & 0xFFFFFF

        # Check if this looks like valid import data
        if record_type == 0x00 and 0 < ordinal < 0x10000:
            import_rva_end = rva + 4

    import_size = import_rva_end - import_rva_start
    print(f"  Found import data in original XEX: RVA 0x{import_rva_start:X}-0x{import_rva_end:X} ({import_size} bytes)")

    # Copy import data from original XEX to PE
    pe_data = bytearray(pe_data)
    src_off = orig_pe_offset + import_rva_start
    dst_off = import_rva_start  # File offset = RVA for our PE

    pe_data[dst_off:dst_off + import_size] = orig_xex_data[src_off:src_off + import_size]
    print(f"  Copied import data from original XEX to RVA 0x{import_rva_start:X}")

    return bytes(pe_data)


def patch_and_relocate_imports(pe_data, image_base, expected_import_rva=0x600):
    """
    Patch PE import thunks from little-endian ordinal format to big-endian XEX format.

    The Xbox 360 linker generates import thunks with bit 31 set (0x80XXXXXX format)
    in little-endian. Xenia's XEX parser expects record_type in the high byte
    (big-endian): 0x00=variable, 0x01=thunk.

    This function:
    1. Finds .idata section
    2. Converts data entries from 0x80XXXXXX (LE) to 0x00XXXXXX (BE)
    3. Builds a mapping: ordinal -> RVA in .idata

    Returns: (patched PE data, .idata RVA, dict of ordinal -> RVA)
    """
    idata_info = find_idata_section(pe_data)
    if not idata_info:
        print("  Warning: No .idata section found, skipping import patching")
        return pe_data, 0, {}

    idata_rva, idata_vsize, idata_file_offset = idata_info
    print(f"  Found .idata at RVA 0x{idata_rva:X}, size 0x{idata_vsize:X}")

    pe_data = bytearray(pe_data)
    patched = 0
    ordinal_to_rva = {}

    # Scan and patch data entries to 0x00XXXXXX format (record_type=0x00)
    off = 0
    while off < idata_vsize - 3:
        val_le = struct.unpack_from('<I', pe_data, idata_file_offset + off)[0]

        # Check if this looks like an ordinal import (0x80XXXXXX with reasonable ordinal)
        if (val_le & 0xFF000000) == 0x80000000:
            ordinal = val_le & 0xFFFFFF  # Full 24-bit ordinal
            if 0x001 <= ordinal <= 0x1FFFF:  # Reasonable ordinal range
                # Convert to XEX data format: big-endian with record_type=0x00
                # Value is just the ordinal (0x00XXXXXX)
                struct.pack_into('>I', pe_data, idata_file_offset + off, ordinal)
                patched += 1
                ordinal_to_rva[ordinal] = idata_rva + off

        off += 4

    print(f"  Converted {patched} import entries to XEX data format (0x00XXXXXX)")
    print(f"  Mapped {len(ordinal_to_rva)} ordinals to RVAs")

    return bytes(pe_data), idata_rva, ordinal_to_rva


def parse_import_library_header(data, header_offset):
    """
    Parse xex2_opt_import_libraries structure.

    Structure:
    +0x00: total_size (4 bytes)
    +0x04: string_table_size (4 bytes)
    +0x08: string_table_count (4 bytes)
    +0x0C: string_table data
    ... library headers follow
    """
    total_size = struct.unpack_from('>I', data, header_offset)[0]
    str_table_size = struct.unpack_from('>I', data, header_offset + 4)[0]
    str_table_count = struct.unpack_from('>I', data, header_offset + 8)[0]

    # Parse string table (null-terminated strings)
    str_table_off = header_offset + 12
    strings = []
    pos = 0
    while pos < str_table_size:
        end = data.find(b'\x00', str_table_off + pos)
        if end == -1:
            break
        s = data[str_table_off + pos:end].decode('utf-8', errors='replace')
        strings.append(s)
        pos = end - str_table_off + 1

    # Parse library headers
    libs = []
    lib_off = str_table_off + str_table_size

    while lib_off < header_offset + total_size:
        lib_size = struct.unpack_from('>I', data, lib_off)[0]
        if lib_size == 0:
            break

        # Parse xex2_import_library structure
        # +0x00: size (4)
        # +0x04: digest (20)
        # +0x18: id (4)
        # +0x1C: version (4)
        # +0x20: version_min (4)
        # +0x24: name_index (2)
        # +0x26: count (2)
        # +0x28: import_table[] (4 each)

        name_index = struct.unpack_from('>H', data, lib_off + 0x24)[0]
        count = struct.unpack_from('>H', data, lib_off + 0x26)[0]

        lib_name = strings[name_index] if name_index < len(strings) else f"unknown_{name_index}"

        import_table = []
        for i in range(count):
            va = struct.unpack_from('>I', data, lib_off + 0x28 + i * 4)[0]
            import_table.append(va)

        libs.append({
            'offset': lib_off,
            'size': lib_size,
            'name': lib_name,
            'name_index': name_index,
            'count': count,
            'import_table': import_table,
        })

        lib_off += lib_size

    return {
        'total_size': total_size,
        'string_table_size': str_table_size,
        'strings': strings,
        'libraries': libs,
        'raw_data': data[header_offset:header_offset + total_size],
    }


def build_ordinal_to_rva_map(pe_data):
    """
    Build mapping from ordinal -> RVA in patched .idata section.

    The .idata has already been patched to XEX format (0x00XXXXXX).
    Returns: dict mapping ordinal -> RVA
    """
    idata_info = find_idata_section(pe_data)
    if not idata_info:
        return {}

    idata_rva, idata_vsize, idata_file_offset = idata_info
    ordinal_map = {}

    for off in range(0, idata_vsize - 3, 4):
        # Read big-endian (already converted to XEX format)
        val = struct.unpack_from('>I', pe_data, idata_file_offset + off)[0]
        record_type = (val >> 24) & 0xFF
        if record_type == 0x00:  # Data import (ordinal in low 24 bits)
            ordinal = val & 0xFFFFFF
            if ordinal > 0:  # Valid ordinal
                ordinal_map[ordinal] = idata_rva + off

    return ordinal_map


def decompress_xex_pe(xex_data):
    """
    Decompress XEX with basic compression (type 1) and return the PE data.

    For debug/devkit XEXs (encryption type 0), this reads the block
    descriptors and concatenates data blocks with zero padding.
    """
    # Parse XEX2 header
    if xex_data[0:4] != b'XEX2':
        raise ValueError(f"Not a XEX2 file: {xex_data[0:4]}")

    pe_offset = struct.unpack('>I', xex_data[8:12])[0]
    opt_count = struct.unpack('>I', xex_data[20:24])[0]

    # Find the Base File Format header (0x3FF)
    off = 24
    bff_offset = None
    for i in range(opt_count):
        hdr_id = struct.unpack('>I', xex_data[off:off+4])[0]
        hdr_val = struct.unpack('>I', xex_data[off+4:off+8])[0]
        if hdr_id == 0x000003FF:  # Base File Format
            bff_offset = hdr_val
            break
        off += 8

    if bff_offset is None:
        raise ValueError("No Base File Format header found")

    # Read XEXFileDataDescriptor
    size = struct.unpack('>I', xex_data[bff_offset:bff_offset+4])[0]
    enc_type = struct.unpack('>H', xex_data[bff_offset+4:bff_offset+6])[0]
    comp_type = struct.unpack('>H', xex_data[bff_offset+6:bff_offset+8])[0]

    if enc_type != 0:
        raise ValueError(f"Encrypted XEX not supported (enc_type={enc_type})")

    if comp_type == 0:
        # Raw - just copy PE directly
        return xex_data[pe_offset:]
    elif comp_type == 1:
        # Basic compression - block-based with zero padding
        num_blocks = (size - 8) // 8

        pe_data = bytearray()
        data_offset = pe_offset

        for i in range(num_blocks):
            block_off = bff_offset + 8 + i * 8
            blk_size = struct.unpack('>I', xex_data[block_off:block_off+4])[0]
            blk_zeros = struct.unpack('>I', xex_data[block_off+4:block_off+8])[0]

            # Read data block
            pe_data.extend(xex_data[data_offset:data_offset + blk_size])
            data_offset += blk_size

            # Add zero padding
            pe_data.extend(b'\x00' * blk_zeros)

        return bytes(pe_data)
    elif comp_type == 2:
        raise ValueError("LZX compression not supported - use xextool -cu first")
    else:
        raise ValueError(f"Unknown compression type: {comp_type}")


def build_va_to_ordinal_map_from_decompressed(original_xex_info, import_libs_info):
    """
    Build mapping from original VA -> ordinal by decompressing the XEX and reading ordinals.

    The original import_table VAs point to locations where ordinal values were stored
    in the decompressed PE (at RVA 0x600-0x1E48).

    Returns: dict mapping VA -> ordinal
    """
    xex_data = original_xex_info['original_data']
    orig_image_base = original_xex_info['orig_image_base']

    # Decompress the original XEX to get the PE data
    try:
        decomp_pe = decompress_xex_pe(xex_data)
    except ValueError as e:
        print(f"  Warning: Could not decompress original XEX: {e}")
        return {}

    va_to_ordinal = {}

    for lib in import_libs_info['libraries']:
        for va in lib['import_table']:
            if va == 0:
                continue

            # Convert VA to RVA
            rva = va - orig_image_base

            # Read ordinal from decompressed PE at this RVA
            if rva >= 0 and rva + 4 <= len(decomp_pe):
                val = struct.unpack_from('>I', decomp_pe, rva)[0]
                record_type = (val >> 24) & 0xFF
                ordinal = val & 0xFFFFFF

                if record_type in (0x00, 0x01) and ordinal > 0:
                    va_to_ordinal[va] = ordinal

    return va_to_ordinal


def build_va_to_ordinal_map(orig_xex_data, orig_image_base, import_libs_info):
    """
    Build mapping from original VA -> ordinal by reading values from original XEX.

    The original import_table VAs point to locations where ordinal values were stored.
    Returns: dict mapping VA -> ordinal
    """
    va_to_ordinal = {}

    for lib in import_libs_info['libraries']:
        for va in lib['import_table']:
            if va == 0:
                continue

            # Calculate where this VA is in the original PE data
            # Original XEX has PE at some offset, need to find it
            orig_pe_offset = struct.unpack_from('>I', orig_xex_data, 8)[0]
            rva = va - orig_image_base
            file_offset = orig_pe_offset + rva

            if file_offset < len(orig_xex_data) - 4:
                # Read the ordinal value stored at this location
                # In original XEX, this should be in big-endian format
                val = struct.unpack_from('>I', orig_xex_data, file_offset)[0]
                record_type = (val >> 24) & 0xFF
                ordinal = val & 0xFFFFFF

                if record_type == 0x00 and ordinal > 0:
                    va_to_ordinal[va] = ordinal

    return va_to_ordinal


def patch_import_library_header(orig_header_data, import_libs_info,
                                  va_to_ordinal, ordinal_to_rva, new_image_base):
    """
    Patch import_table VAs to point to decompiled PE's .idata section.

    For each import_table entry:
    1. Look up the ordinal from the original VA
    2. Find the new RVA for that ordinal in our .idata
    3. Update the VA to point to the new location
    """
    patched = bytearray(orig_header_data)
    patched_count = 0
    missing_count = 0

    for lib in import_libs_info['libraries']:
        lib_offset = lib['offset']
        # Find where this library starts in the header data
        # Libraries are after the string table
        str_table_size = import_libs_info['string_table_size']
        # Calculate offset from start of libraries section
        lib_data_offset = 12 + str_table_size  # 12 = size(4) + str_size(4) + str_count(4)
        for prev_lib in import_libs_info['libraries']:
            if prev_lib['offset'] < lib['offset']:
                lib_data_offset += prev_lib['size']

        for i, orig_va in enumerate(lib['import_table']):
            if orig_va == 0:
                continue

            # Look up ordinal for this original VA
            ordinal = va_to_ordinal.get(orig_va)
            if ordinal is None:
                missing_count += 1
                continue

            # Find new RVA for this ordinal
            new_rva = ordinal_to_rva.get(ordinal)
            if new_rva is None:
                missing_count += 1
                continue

            # Calculate new VA
            new_va = new_image_base + new_rva

            # Patch the import_table entry
            # Import table is at lib_data_offset + 0x28 + i*4 in the raw data
            entry_offset = lib_data_offset + 0x28 + i * 4
            struct.pack_into('>I', patched, entry_offset, new_va)
            patched_count += 1

    print(f"  Patched {patched_count} import table entries, {missing_count} missing")
    return bytes(patched)


def generate_thunk_data(orig_pe_data, import_libs_info, image_base=0x82000000):
    """
    Generate thunk marker data and VA mapping from decompressed original PE.

    The original PE has thunk markers at RVA 0xEE5xxx with 0x01XXXXXX format.
    We need to extract the ordinals and create new thunk markers.

    Returns: (thunk_data, old_va_to_new_va, thunk_rva_base)
    """
    thunk_count = 347  # Known from analysis
    thunk_data = bytearray(thunk_count * 16)  # 16 bytes per thunk
    va_mapping = {}  # old_va -> new_va

    # Parse PE to get SizeOfImage for thunk placement
    if orig_pe_data[0:2] != b'MZ':
        return bytes(thunk_data), va_mapping, 0

    pe_offset = struct.unpack_from('<I', orig_pe_data, 0x3C)[0]
    if orig_pe_data[pe_offset:pe_offset+4] != b'PE\x00\x00':
        return bytes(thunk_data), va_mapping, 0

    size_of_image = struct.unpack_from('<I', orig_pe_data, pe_offset + 24 + 56)[0]

    # New thunk section RVA (at end of PE image, page-aligned)
    thunk_rva_base = (size_of_image + 0xFFF) & ~0xFFF

    thunk_idx = 0
    thunks_found = 0

    for lib in import_libs_info['libraries']:
        for i, va in enumerate(lib['import_table']):
            if va == 0:
                continue

            rva = va - image_base

            # Thunks are at high RVAs (0xEE5xxx), variables at low (0x6xx)
            # Variables are at 0x600-0x1E48 (< 8KB), thunks at 0xEE5xxx (> 15MB)
            # Use 1MB as threshold to distinguish them
            if rva > 0x100000:  # Is thunk (> 1MB)
                # Read ordinal from original thunk marker in decompressed PE
                if rva + 4 <= len(orig_pe_data):
                    val = struct.unpack_from('>I', orig_pe_data, rva)[0]
                    record_type = (val >> 24) & 0xFF
                    ordinal = val & 0xFFFFFF

                    if record_type == 0x01:  # Thunk marker
                        # Generate thunk marker: 0x01XXXXXX
                        marker = 0x01000000 | ordinal

                        # Write to thunk data
                        offset = thunk_idx * 16
                        struct.pack_into('>I', thunk_data, offset, marker)

                        # Map old VA to new VA
                        new_rva = thunk_rva_base + offset
                        va_mapping[va] = image_base + new_rva

                        thunk_idx += 1
                        thunks_found += 1

    return bytes(thunk_data), va_mapping, thunk_rva_base


def extend_pe_with_thunks(pe_data, thunk_data, thunk_rva_base):
    """
    Extend PE to include thunk section at end.

    Updates:
    - SizeOfImage in optional header
    - Appends thunk data to file

    Returns: extended PE data
    """
    pe_data = bytearray(pe_data)

    # Parse PE header
    pe_offset = struct.unpack_from('<I', pe_data, 0x3C)[0]
    size_of_image_offset = pe_offset + 24 + 56

    old_size_of_image = struct.unpack_from('<I', pe_data, size_of_image_offset)[0]

    # Page-align thunk data
    thunk_size_aligned = (len(thunk_data) + 0xFFF) & ~0xFFF

    # Update SizeOfImage
    new_size_of_image = thunk_rva_base + thunk_size_aligned
    struct.pack_into('<I', pe_data, size_of_image_offset, new_size_of_image)

    # Append thunk data (padded to page boundary)
    pe_data.extend(thunk_data)
    padding = thunk_size_aligned - len(thunk_data)
    pe_data.extend(b'\x00' * padding)

    print(f"    Extended PE with thunk section:")
    print(f"      Old SizeOfImage: 0x{old_size_of_image:X}")
    print(f"      New SizeOfImage: 0x{new_size_of_image:X}")
    print(f"      Thunk RVA base: 0x{thunk_rva_base:X}")
    print(f"      Thunk size: {len(thunk_data)} bytes + {padding} padding")

    return bytes(pe_data), new_size_of_image


def patch_import_library_header(header_data, import_libs_info, va_mapping):
    """
    Patch import_table VAs to point to new thunk section.

    Returns: patched header data
    """
    patched = bytearray(header_data)

    str_table_size = import_libs_info['string_table_size']
    patched_count = 0

    for lib in import_libs_info['libraries']:
        # Calculate offset to this library's import_table in the header
        # Libraries start after string table (12 bytes for header fields)
        lib_data_offset = 12 + str_table_size

        # Add sizes of previous libraries
        for prev_lib in import_libs_info['libraries']:
            if prev_lib['offset'] < lib['offset']:
                lib_data_offset += prev_lib['size']

        # Patch each import_table entry
        for i, orig_va in enumerate(lib['import_table']):
            if orig_va == 0:
                continue

            if orig_va in va_mapping:
                new_va = va_mapping[orig_va]
                # import_table starts at offset 0x28 in library header
                entry_offset = lib_data_offset + 0x28 + i * 4
                struct.pack_into('>I', patched, entry_offset, new_va)
                patched_count += 1

    print(f"    Patched {patched_count} thunk VA entries in import header")
    return bytes(patched)


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
    import_libs_offset = None
    import_libs_info = None

    for hdr_id, hdr_val in opt_headers:
        key_type = hdr_id & 0xFF
        if key_type <= 0x01:
            # Inline value
            bff_headers[hdr_id] = ('inline', hdr_val)
        elif key_type == 0xFF:
            # Pointer to variable-length data
            size = struct.unpack('>I', data[hdr_val:hdr_val+4])[0]
            bff_headers[hdr_id] = ('blob', data[hdr_val:hdr_val+size])
            # Special handling for import libraries
            if hdr_id == 0x000103FF:
                import_libs_offset = hdr_val
                import_libs_info = parse_import_library_header(data, hdr_val)
        else:
            # Fixed-size data (key_type * 4 bytes)
            size = key_type * 4
            bff_headers[hdr_id] = ('fixed', data[hdr_val:hdr_val+size])

    # Get original image base
    orig_image_base = 0x82000000  # Default
    if 0x00010201 in bff_headers and bff_headers[0x00010201][0] == 'inline':
        orig_image_base = bff_headers[0x00010201][1]

    return {
        'mod_flags': mod_flags,
        'opt_headers': opt_headers,
        'bff_headers': bff_headers,
        'security_info': si_data,
        'original_data': data,
        'import_libs_offset': import_libs_offset,
        'import_libs_info': import_libs_info,
        'orig_image_base': orig_image_base,
    }


def build_xex(pe_data, original_xex_info, pe_info, idata_rva=0x2B0A00, ordinal_to_rva=None, orig_pe_data=None):
    """Build a minimal XEX2 container around the PE data.

    If orig_pe_data is provided, enables full import resolution by:
    1. Generating thunk markers from the decompressed original PE
    2. Extending the PE with a thunk section
    3. Patching the import library header to point to new thunks
    """
    if ordinal_to_rva is None:
        ordinal_to_rva = {}
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

    # Import Libraries (0x103FF) - Process FIRST to extend PE before Base File Format
    # We need to patch the import_table VAs to point to our new thunk section.
    # The import_table has interleaved entries:
    # - Even indices: variable imports (0x00XXXXXX) at RVA 0x600+ (we have these)
    # - Odd indices: thunk markers (0x01XXXXXX) at RVA 0xEE5xxx (we DON'T have these)
    #
    # Solution: Generate thunk markers, extend PE with thunk section, patch import header.
    import_libs_info = original_xex_info.get('import_libs_info')
    import_header_blob = None
    if 0x000103FF in orig and import_libs_info and orig_pe_data:
        print("  Processing import library header with thunk section...")
        # Generate thunk data and VA mapping
        thunk_data, va_mapping, thunk_rva_base = generate_thunk_data(
            orig_pe_data,
            import_libs_info,
            original_xex_info['orig_image_base']
        )
        print(f"    Generated {len(va_mapping)} thunk markers")

        # Extend PE with thunk section
        pe_data, new_size_of_image = extend_pe_with_thunks(pe_data, thunk_data, thunk_rva_base)

        # Update pe_info with new size
        pe_info = dict(pe_info)  # Make a copy
        pe_info['size_of_image'] = new_size_of_image

        # Patch import library header
        patched_header = patch_import_library_header(
            orig[0x000103FF][1],
            import_libs_info,
            va_mapping
        )
        import_header_blob = (0x000103FF, patched_header)
        print("  Including patched import library header")
    elif 0x000103FF in orig:
        print("  Skipping import library header (no decompressed original PE available)")
    else:
        print("  No import library header in original XEX")

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

    # Add import header if we processed it
    if import_header_blob:
        blob_headers.append(import_header_blob)

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

    # Parse original XEX first (needed for import data)
    orig_xex_path = Path(args.original_xex)
    print(f"\nParsing original XEX: {orig_xex_path}")
    orig_info = parse_original_xex(orig_xex_path)
    print(f"  {len(orig_info['opt_headers'])} optional headers")
    print(f"  Security info: {len(orig_info['security_info'])} bytes")

    # Decompress original XEX to get import data
    print("\nDecompressing original XEX for import data...")
    orig_pe_data = None  # Initialize to None in case decompression fails
    try:
        orig_pe_data = decompress_xex_pe(orig_info['original_data'])
        print(f"  Decompressed PE: {len(orig_pe_data):,} bytes")

        # Copy import data from original PE (RVA 0x600-0x1E48) to our PE
        import_start = 0x600
        import_end = 0x1E48
        import_size = import_end - import_start

        pe_data = bytearray(pe_data)
        pe_data[import_start:import_end] = orig_pe_data[import_start:import_end]
        pe_data = bytes(pe_data)
        print(f"  Copied {import_size} bytes of import data from RVA 0x{import_start:X} to 0x{import_end:X}")

    except ValueError as e:
        print(f"  Warning: Could not decompress original XEX: {e}")
        print("  Import resolution may not work correctly")

    # Patch and relocate import thunks in .idata section
    print("\nPatching import thunks...")
    pe_data, idata_rva, ordinal_to_rva = patch_and_relocate_imports(pe_data, pe_info['image_base'])
    if idata_rva == 0:
        print("  Warning: Could not find .idata section!")

    # Build XEX
    print(f"\nBuilding XEX...")
    xex_data = build_xex(pe_data, orig_info, pe_info, idata_rva, ordinal_to_rva, orig_pe_data)
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
