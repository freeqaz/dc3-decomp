#!/usr/bin/env python3
"""
Minimal XEX2 packer for devkit/debug PE files.

Creates an unencrypted, uncompressed XEX container around a PPC PE executable.
Copies essential optional headers from the original XEX (entry point, image base,
execution ID, etc.) but updates the PE offset and image size for the new PE.

Also patches the PE's import thunks from PE ordinal format (0x80XXXXXX) to
XEX import format (0x00XXXXXX) so Xenia can properly resolve imports.

Usage:
    python3 scripts/build/build_xex.py                           # Default: build PE → XEX
    python3 scripts/build/build_xex.py --pe path/to/pe.exe       # Custom PE
    python3 scripts/build/build_xex.py --output path/to/out.xex  # Custom output
"""

import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


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


def find_and_convert_thunk_iat(pe_data, image_base):
    """
    Find import thunks in .text by pattern matching. For each thunk:
    1. Read the ordinal from its IAT entry in .rdata (LE 0x80XXXXXX)
    2. Write XEX thunk marker (BE 0x01XXXXXX) at the THUNK CODE address
       (overwriting the lis/lwz/mtctr/bctr instructions)
    3. Also convert the IAT entry to BE 0x00XXXXXX for variable imports

    Xenia's XEX loader reads the ordinal from the thunk code address, then
    overwrites those 16 bytes with syscall stubs (sc 2 / blr / nop / nop).
    The import_table VA must point to the thunk CODE, not the IAT data.

    Import thunks follow the pattern:
        lis r11, hi16     (3D60XXXX)
        lwz r11, lo16(r11)(816BXXXX)
        mtctr r11         (7D6903A6)
        bctr              (4E800420)

    Returns: (patched pe_data,
              ordinal_to_thunk_entries: {ordinal: [(thunk_code_va, iat_addr), ...]},
              ordinal_to_var_vas: {ordinal: [iat_addr, ...]})

    IMPORTANT: Maps use LIST values because ordinal namespaces are per-library.
    xam.xex ordinal 1 and xboxkrnl.exe ordinal 1 are different functions with
    different thunks. Each library consumes one entry from the list.
    """
    pe_data = bytearray(pe_data)
    pe_off = struct.unpack_from('<I', pe_data, 0x3C)[0]
    num_sections = struct.unpack_from('<H', pe_data, pe_off + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', pe_data, pe_off + 20)[0]

    # Find .text section
    text_vaddr = text_vsize = text_raw_off = 0
    section_off = pe_off + 24 + opt_hdr_size
    for i in range(num_sections):
        name = pe_data[section_off:section_off+8].rstrip(b'\x00')
        vsize = struct.unpack_from('<I', pe_data, section_off + 8)[0]
        vaddr = struct.unpack_from('<I', pe_data, section_off + 12)[0]
        raw_off = struct.unpack_from('<I', pe_data, section_off + 20)[0]
        if name == b'.text':
            text_vaddr = vaddr
            text_vsize = vsize
            text_raw_off = raw_off
            break
        section_off += 40

    if text_vsize == 0:
        print("  Warning: No .text section found for thunk scanning")
        return bytes(pe_data), {}, {}

    # Scan .text for thunk patterns
    # Multi-valued maps: ordinal → list of (thunk_va, iat_addr) tuples
    from collections import defaultdict
    ordinal_to_thunk_entries = defaultdict(list)
    ordinal_to_var_vas = defaultdict(list)
    converted = 0

    for off in range(0, text_vsize - 16, 4):
        foff = text_raw_off + off
        if foff + 16 > len(pe_data):
            break

        insn0 = struct.unpack_from('>I', pe_data, foff)[0]
        insn1 = struct.unpack_from('>I', pe_data, foff + 4)[0]
        insn2 = struct.unpack_from('>I', pe_data, foff + 8)[0]
        insn3 = struct.unpack_from('>I', pe_data, foff + 12)[0]

        if ((insn0 & 0xFFFF0000) == 0x3D600000 and  # lis r11, imm
            (insn1 & 0xFFFF0000) == 0x816B0000 and  # lwz r11, off(r11)
            insn2 == 0x7D6903A6 and                   # mtctr r11
            insn3 == 0x4E800420):                     # bctr

            hi = insn0 & 0xFFFF
            lo = insn1 & 0xFFFF
            if lo >= 0x8000:
                lo = lo - 0x10000
            iat_addr = (hi << 16) + lo
            iat_rva = iat_addr - image_base

            # Read ordinal from IAT entry (LE format: 0x80XXXXXX)
            iat_foff = find_pe_offset_for_rva(pe_data, iat_rva)
            if iat_foff is None or iat_foff + 4 > len(pe_data):
                continue

            val_le = struct.unpack_from('<I', pe_data, iat_foff)[0]
            if (val_le & 0xFF000000) != 0x80000000:
                continue

            ordinal = val_le & 0xFFFFFF
            if ordinal < 1 or ordinal > 0x40000:
                continue

            thunk_code_va = image_base + text_vaddr + off

            # Write XEX thunk marker (0x01XXXXXX) at the THUNK CODE address.
            xex_thunk_marker = 0x01000000 | ordinal
            struct.pack_into('>I', pe_data, foff, xex_thunk_marker)
            struct.pack_into('>I', pe_data, foff + 4, 0)
            struct.pack_into('>I', pe_data, foff + 8, 0)
            struct.pack_into('>I', pe_data, foff + 12, 0)

            ordinal_to_thunk_entries[ordinal].append((thunk_code_va, iat_addr))

            # Also convert the IAT entry to variable format (0x00XXXXXX BE)
            xex_var_marker = 0x00000000 | ordinal
            struct.pack_into('>I', pe_data, iat_foff, xex_var_marker)
            ordinal_to_var_vas[ordinal].append(iat_addr)

            converted += 1

    print(f"  Found {converted} import thunks:")
    print(f"    Thunk markers at code addresses (for syscall stubs)")
    print(f"    Variable markers at IAT addresses (for variable resolution)")
    dups = sum(1 for v in ordinal_to_thunk_entries.values() if len(v) > 1)
    if dups:
        print(f"    {dups} ordinals with multiple thunks (cross-library overlap)")
    return bytes(pe_data), dict(ordinal_to_thunk_entries), dict(ordinal_to_var_vas)


def find_and_convert_variable_iat(pe_data, image_base):
    """
    Find import variable entries in .rdata by scanning for LE 0x80XXXXXX
    ordinal markers that are NOT thunk IAT entries (not referenced by thunk code).

    The Xbox 360 linker places import variable ordinals in .rdata at the same
    addresses the game code references via lis/lwz pairs.

    Returns: (patched pe_data, ordinal_to_extra_var_vas: {ordinal: [va, ...]})

    Uses multi-valued ordinal maps (same as thunk scanner) because ordinal
    namespaces are per-library.
    """
    pe_data = bytearray(pe_data)
    pe_off = struct.unpack_from('<I', pe_data, 0x3C)[0]
    num_sections = struct.unpack_from('<H', pe_data, pe_off + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', pe_data, pe_off + 20)[0]

    # Find .rdata section
    rdata_vaddr = rdata_vsize = rdata_raw_off = 0
    section_off = pe_off + 24 + opt_hdr_size
    for i in range(num_sections):
        name = pe_data[section_off:section_off+8].rstrip(b'\x00')
        vsize = struct.unpack_from('<I', pe_data, section_off + 8)[0]
        vaddr = struct.unpack_from('<I', pe_data, section_off + 12)[0]
        raw_off = struct.unpack_from('<I', pe_data, section_off + 20)[0]
        if name == b'.rdata':
            rdata_vaddr = vaddr
            rdata_vsize = vsize
            rdata_raw_off = raw_off
            break
        section_off += 40

    if rdata_vsize == 0:
        return bytes(pe_data), {}

    # Scan .rdata for remaining LE 0x80XXXXXX markers (not yet converted)
    from collections import defaultdict
    ordinal_to_extra_var_vas = defaultdict(list)
    converted = 0

    for off in range(0, rdata_vsize - 3, 4):
        foff = rdata_raw_off + off
        if foff + 4 > len(pe_data):
            break

        val_le = struct.unpack_from('<I', pe_data, foff)[0]
        if (val_le & 0xFF000000) != 0x80000000:
            continue

        ordinal = val_le & 0xFFFFFF
        if ordinal < 1 or ordinal > 0x40000:
            continue

        # Check if this was already converted (BE 0x00XXXXXX by thunk scan)
        val_be = struct.unpack_from('>I', pe_data, foff)[0]
        if (val_be >> 24) in (0x00, 0x01):
            # Check if it's a valid converted marker (not just coincidence)
            be_ord = val_be & 0xFFFFFF
            if 1 <= be_ord <= 0x10000:
                continue  # Already converted by thunk scan

        # Convert from LE 0x80XXXXXX to BE 0x00XXXXXX (variable marker)
        xex_marker = 0x00000000 | ordinal
        struct.pack_into('>I', pe_data, foff, xex_marker)
        va = image_base + rdata_vaddr + off
        ordinal_to_extra_var_vas[ordinal].append(va)
        converted += 1

    print(f"  Found {converted} additional import variable entries")
    return bytes(pe_data), dict(ordinal_to_extra_var_vas)


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


def patch_import_library_header(header_data, import_libs_info, va_mapping):
    """
    Patch import_table VAs to point to new thunk section.

    Returns: patched header data
    """
    patched = bytearray(header_data)

    str_table_size = import_libs_info['string_table_size']
    patched_count = 0
    zeroed_count = 0

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

            entry_offset = lib_data_offset + 0x28 + i * 4
            if orig_va in va_mapping:
                new_va = va_mapping[orig_va]
                struct.pack_into('>I', patched, entry_offset, new_va)
                patched_count += 1
            else:
                # Zero out unmapped entries so the XEX loader skips them.
                # Keeping stale VAs from the original PE would cause xenia
                # to read garbage ordinal markers from wrong addresses.
                struct.pack_into('>I', patched, entry_offset, 0)
                zeroed_count += 1

    print(f"    Patched {patched_count} import VA entries, zeroed {zeroed_count} unmapped")
    return bytes(patched)


def generate_page_descriptors(pe_data, page_size=0x10000):
    """
    Generate XEX page descriptors from PE section headers.

    Maps each 64KB page to a section type:
      CODE (1) = executable sections (.text, BINK, RADCODE)
      DATA (2) = writable sections (.data, .bss, etc.)
      RODATA (3) = read-only sections (.rdata, .pdata, .reloc, etc.)

    XEX page descriptor encoding (MSVC bitfield, LSB-first):
      value = (page_count << 4) | info
    Stored as big-endian uint32 + 20 bytes SHA-1 digest (zeros for unsigned).

    Returns: list of (info, page_count) tuples
    """
    pe_off = struct.unpack_from('<I', pe_data, 0x3C)[0]
    num_sections = struct.unpack_from('<H', pe_data, pe_off + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', pe_data, pe_off + 20)[0]
    size_of_image = struct.unpack_from('<I', pe_data, pe_off + 24 + 56)[0]

    total_pages = (size_of_image + page_size - 1) // page_size

    # Default all pages to RODATA (3) — covers PE headers and gaps
    page_types = [3] * total_pages

    # Parse sections and assign types
    section_off = pe_off + 24 + opt_hdr_size
    for i in range(num_sections):
        vaddr = struct.unpack_from('<I', pe_data, section_off + 12)[0]
        vsize = struct.unpack_from('<I', pe_data, section_off + 8)[0]
        chars = struct.unpack_from('<I', pe_data, section_off + 36)[0]

        if vsize == 0:
            section_off += 40
            continue

        # Determine type from characteristics
        is_exec = bool(chars & 0x20000000)   # IMAGE_SCN_MEM_EXECUTE
        is_write = bool(chars & 0x80000000)  # IMAGE_SCN_MEM_WRITE

        if is_exec:
            stype = 1  # CODE
        elif is_write:
            stype = 2  # DATA
        else:
            stype = 3  # RODATA

        # Mark pages covered by this section
        start_page = vaddr // page_size
        end_page = (vaddr + vsize + page_size - 1) // page_size
        for p in range(start_page, min(end_page, total_pages)):
            # CODE takes priority over other types on shared pages
            if stype == 1 or page_types[p] == 3:
                page_types[p] = stype

        section_off += 40

    # Consolidate into contiguous ranges of same type
    descriptors = []
    i = 0
    while i < total_pages:
        cur_type = page_types[i]
        count = 1
        while i + count < total_pages and page_types[i + count] == cur_type:
            count += 1
        descriptors.append((cur_type, count))
        i += count

    return descriptors


def build_security_info(original_si, size_of_image, page_descriptors):
    """
    Build updated security info with new image size and page descriptors.

    Keeps the first 0x184 bytes from the original (RSA sig, keys, flags, etc.)
    and replaces the page_descriptor_count and page_descriptors.

    Args:
        original_si: Original security info bytes
        size_of_image: New PE SizeOfImage
        page_descriptors: List of (info, page_count) tuples

    Returns: New security info bytes
    """
    # Take the header portion (up to and including page_descriptor_count field)
    si = bytearray(original_si[:0x184])

    # Update image size at offset 4
    struct.pack_into('>I', si, 4, size_of_image)

    # Update page descriptor count at offset 0x180
    struct.pack_into('>I', si, 0x180, len(page_descriptors))

    # Append page descriptors
    for info, page_count in page_descriptors:
        # Encode: value = (page_count << 4) | info, as BE uint32
        value = (page_count << 4) | info
        si.extend(struct.pack('>I', value))
        # 20 bytes SHA-1 digest (zeros for unsigned/debug)
        si.extend(b'\x00' * 20)

    return bytes(si)


def pe_file_to_virtual(pe_data):
    """
    Convert a PE file from disk layout to virtual memory layout.

    On disk, sections are at PointerToRawData (file alignment).
    In memory, sections are at VirtualAddress (section alignment).
    XEX decompression maps the blob contiguously into guest memory,
    so the PE must be in virtual layout for sections to be at correct VAs.

    Returns: bytes of SizeOfImage length with sections at VirtualAddress offsets
    """
    pe_off = struct.unpack_from('<I', pe_data, 0x3C)[0]
    num_sections = struct.unpack_from('<H', pe_data, pe_off + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', pe_data, pe_off + 20)[0]
    size_of_headers = struct.unpack_from('<I', pe_data, pe_off + 24 + 60)[0]
    size_of_image = struct.unpack_from('<I', pe_data, pe_off + 24 + 56)[0]

    # Start with zero-filled buffer of SizeOfImage
    virtual = bytearray(size_of_image)

    # Copy PE headers (DOS header, PE header, section table)
    virtual[:size_of_headers] = pe_data[:size_of_headers]

    # Copy each section from file offset to virtual address
    section_off = pe_off + 24 + opt_hdr_size
    for i in range(num_sections):
        name = pe_data[section_off:section_off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        vaddr = struct.unpack_from('<I', pe_data, section_off + 12)[0]
        raw_size = struct.unpack_from('<I', pe_data, section_off + 16)[0]
        raw_off = struct.unpack_from('<I', pe_data, section_off + 20)[0]

        if raw_size > 0 and raw_off < len(pe_data):
            copy_size = min(raw_size, len(pe_data) - raw_off)
            if vaddr + copy_size <= size_of_image:
                virtual[vaddr:vaddr + copy_size] = pe_data[raw_off:raw_off + copy_size]

        section_off += 40

    return bytes(virtual)


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


def build_xex(pe_data, original_xex_info, pe_info, orig_pe_data=None,
              ordinal_to_thunk_entries=None, ordinal_to_var_vas=None):
    """Build a minimal XEX2 container around the PE data.

    ordinal_to_thunk_entries: {ordinal: [(thunk_code_va, iat_addr), ...]}
    ordinal_to_var_vas: {ordinal: [iat_addr, ...]} - multi-valued per-library
    """
    if ordinal_to_thunk_entries is None:
        ordinal_to_thunk_entries = {}
    if ordinal_to_var_vas is None:
        ordinal_to_var_vas = {}
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

    # Import Libraries (0x103FF) - Map original import_table VAs to our PE's addresses.
    # Each ordinal has TWO entries in the import_table:
    # - Variable entry (record_type=0x00): points to IAT DATA address
    # - Thunk entry (record_type=0x01): points to thunk CODE address
    #
    # Ordinal namespaces are per-library (xam ordinal 1 != xboxkrnl ordinal 1),
    # so we process each library separately, consuming from multi-valued ordinal
    # maps to handle cross-library ordinal overlap.
    import_libs_info = original_xex_info.get('import_libs_info')
    import_header_blob = None
    if 0x000103FF in orig and import_libs_info and orig_pe_data and \
       (ordinal_to_thunk_entries or ordinal_to_var_vas):
        print("  Processing import library header...")
        orig_image_base = original_xex_info['orig_image_base']

        # Decompress original XEX to read ordinal markers at import_table VAs
        try:
            decomp_pe = decompress_xex_pe(original_xex_info['original_data'])
        except ValueError:
            decomp_pe = None

        va_mapping = {}
        mapped_vars = 0
        mapped_thunks = 0
        unmapped_entries = []  # (orig_va, ordinal, record_type) for stub allocation

        if decomp_pe:
            # Build IAT group → library assignment.
            # Each library's IAT entries are contiguous in .rdata. We group all
            # thunk IAT addresses by proximity, then assign each group to a
            # library by checking which library's unique ordinals appear in it.
            all_thunks = []  # (iat_addr, ordinal, thunk_va)
            for ordinal, entries in ordinal_to_thunk_entries.items():
                for thunk_va, iat_addr in entries:
                    all_thunks.append((iat_addr, ordinal, thunk_va))
            all_thunks.sort(key=lambda x: x[0])

            # Find IAT groups by address gap (> 8 entries = 32 bytes)
            iat_groups = [[all_thunks[0]]] if all_thunks else []
            for t in all_thunks[1:]:
                if t[0] - iat_groups[-1][-1][0] > 32:
                    iat_groups.append([])
                iat_groups[-1].append(t)

            # Record address ranges for each group (for range-based lookup)
            group_ranges = []  # [(min_addr, max_addr)]
            for group in iat_groups:
                addrs = [t[0] for t in group]
                group_ranges.append((min(addrs), max(addrs)))

            # Build per-library real ordinal sets for library identification
            lib_real_ords = {}
            for lib_idx, lib in enumerate(import_libs_info['libraries']):
                ords = set()
                for va in lib['import_table']:
                    if va == 0:
                        continue
                    rva = va - orig_image_base
                    if rva < 0 or rva + 4 > len(decomp_pe):
                        continue
                    val = struct.unpack_from('>I', decomp_pe, rva)[0]
                    ordinal = val & 0xFFFFFF
                    if ordinal > 0:
                        ords.add(ordinal & 0xFFFF)
                lib_real_ords[lib_idx] = ords

            # Assign each IAT group to a library by scoring unique ordinals
            group_to_lib = {}
            for g_idx, group in enumerate(iat_groups):
                group_ords = set(t[1] for t in group)
                best_score = -1
                best_lib = -1
                for lib_idx, lib_ords in lib_real_ords.items():
                    # Score = ordinals unique to this library found in this group
                    other_ords = set()
                    for other_idx, other in lib_real_ords.items():
                        if other_idx != lib_idx:
                            other_ords |= other
                    unique = lib_ords - other_ords
                    score = len(group_ords & unique)
                    if score > best_score:
                        best_score = score
                        best_lib = lib_idx
                group_to_lib[g_idx] = best_lib

            def addr_to_lib_idx(addr):
                """Assign an IAT address to a library via group range membership."""
                for g_idx, (lo, hi) in enumerate(group_ranges):
                    if lo - 64 <= addr <= hi + 64:  # small margin
                        return group_to_lib[g_idx]
                return -1

            print(f"    IAT groups: {len(iat_groups)}, "
                  f"assignments: {[import_libs_info['libraries'][group_to_lib[i]]['name'] for i in range(len(iat_groups))]}")
            for g_idx, (lo, hi) in enumerate(group_ranges):
                lib_name = import_libs_info['libraries'][group_to_lib[g_idx]]['name']
                print(f"      Group {g_idx}: {lo:#x}-{hi:#x} ({len(iat_groups[g_idx])} entries) → {lib_name}")

            from collections import defaultdict

            # Build ordinal-only unique sets for fallback matching
            # An ordinal is "unique" if it appears in only one library
            all_lib_ords = set()
            for ords in lib_real_ords.values():
                all_lib_ords |= ords
            ordinal_to_unique_lib = {}  # ordinal → lib_idx (only for unique ordinals)
            for lib_idx, ords in lib_real_ords.items():
                other_ords = set()
                for other_idx, other in lib_real_ords.items():
                    if other_idx != lib_idx:
                        other_ords |= other
                for o in ords - other_ords:
                    ordinal_to_unique_lib[o] = lib_idx

            # Build per-library thunk and variable maps using group ranges
            # key = (lib_index, ordinal), value = thunk_va or iat_addr
            lib_thunk_map = {}  # (lib_idx, ordinal) → thunk_va
            lib_var_map = {}    # (lib_idx, ordinal) → iat_addr
            # Also build ordinal-only maps for fallback
            ord_thunk_map = defaultdict(list)  # ordinal → [thunk_va, ...]
            ord_var_map = defaultdict(list)    # ordinal → [iat_addr, ...]

            for ordinal, entries in ordinal_to_thunk_entries.items():
                for thunk_va, iat_addr in entries:
                    lib_idx = addr_to_lib_idx(iat_addr)
                    key = (lib_idx, ordinal)
                    lib_thunk_map[key] = thunk_va
                    ord_thunk_map[ordinal].append(thunk_va)

            for ordinal, iat_addrs in ordinal_to_var_vas.items():
                for iat_addr in iat_addrs:
                    lib_idx = addr_to_lib_idx(iat_addr)
                    key = (lib_idx, ordinal)
                    lib_var_map[key] = iat_addr
                    ord_var_map[ordinal].append(iat_addr)

            # Track consumed VAs to prevent double-mapping.
            # Each thunk/var VA can only be assigned to ONE import table entry.
            # If two libraries share the same VA (overlapping ordinals), xenia
            # would overwrite the first library's marker with a syscall stub
            # before reading it for the second library, causing a crash.
            consumed_vas = set()

            # Process each library's import table
            for lib_idx, lib in enumerate(import_libs_info['libraries']):
                lib_mapped_v = 0
                lib_mapped_t = 0
                lib_unmapped = 0

                for va in lib['import_table']:
                    if va == 0:
                        continue

                    rva = va - orig_image_base
                    if rva < 0 or rva + 4 > len(decomp_pe):
                        continue

                    val = struct.unpack_from('>I', decomp_pe, rva)[0]
                    record_type = (val >> 24) & 0xFF
                    prefixed_ordinal = val & 0xFFFFFF
                    if record_type not in (0x00, 0x01) or prefixed_ordinal == 0:
                        continue

                    # Strip library prefix to get the plain ordinal used in our PE
                    real_ordinal = prefixed_ordinal & 0xFFFF
                    key = (lib_idx, real_ordinal)

                    if record_type == 0x00:
                        # Variable import — try group-based, then ordinal-only
                        target_va = None
                        if key in lib_var_map:
                            candidate = lib_var_map.pop(key)
                            if candidate not in consumed_vas:
                                target_va = candidate
                        if target_va is None:
                            # Fallback: try ordinal-only, skip consumed VAs
                            while ord_var_map.get(real_ordinal):
                                candidate = ord_var_map[real_ordinal].pop(0)
                                if candidate not in consumed_vas:
                                    target_va = candidate
                                    break
                        if target_va is not None:
                            va_mapping[va] = target_va
                            consumed_vas.add(target_va)
                            mapped_vars += 1
                            lib_mapped_v += 1
                        else:
                            unmapped_entries.append(
                                (va, prefixed_ordinal, record_type))
                            lib_unmapped += 1
                    elif record_type == 0x01:
                        # Thunk import — try group-based, then ordinal-only
                        target_va = None
                        if key in lib_thunk_map:
                            candidate = lib_thunk_map.pop(key)
                            if candidate not in consumed_vas:
                                target_va = candidate
                        if target_va is None:
                            # Fallback: try ordinal-only, skip consumed VAs
                            while ord_thunk_map.get(real_ordinal):
                                candidate = ord_thunk_map[real_ordinal].pop(0)
                                if candidate not in consumed_vas:
                                    target_va = candidate
                                    break
                        if target_va is not None:
                            va_mapping[va] = target_va
                            consumed_vas.add(target_va)
                            mapped_thunks += 1
                            lib_mapped_t += 1
                        else:
                            unmapped_entries.append(
                                (va, prefixed_ordinal, record_type))
                            lib_unmapped += 1

                print(f"    {lib['name']}: {lib_mapped_v} vars, "
                      f"{lib_mapped_t} thunks, {lib_unmapped} unmapped")

        # For unmapped entries, allocate stub markers at the end of the PE.
        # Each stub is 16 bytes (xenia assumes 16-byte thunk entries).
        # Xenia needs valid ordinal markers at each VA.
        if unmapped_entries:
            pe_data = bytearray(pe_data)
            image_base = pe_info['image_base']
            # Allocate stub page at end of PE (page-aligned)
            stub_rva = (len(pe_data) + 0xFFF) & ~0xFFF
            stub_data = bytearray()

            for orig_va, ordinal, record_type in unmapped_entries:
                stub_offset = len(stub_data)
                stub_va = image_base + stub_rva + stub_offset
                # Write ordinal marker in XEX BE format
                xex_marker = ((record_type & 0xFF) << 24) | (ordinal & 0xFFFFFF)
                stub_data.extend(struct.pack('>I', xex_marker))
                # Pad to 16 bytes (xenia may overwrite 16 bytes for thunks)
                stub_data.extend(b'\x00' * 12)
                va_mapping[orig_va] = stub_va

            # Pad stub data to page boundary
            stub_data_aligned = len(stub_data)
            if stub_data_aligned % 0x1000:
                stub_data_aligned = (stub_data_aligned + 0xFFF) & ~0xFFF
            stub_data.extend(b'\x00' * (stub_data_aligned - len(stub_data)))

            # Extend PE with stub data
            pe_data.extend(b'\x00' * (stub_rva - len(pe_data)))
            pe_data.extend(stub_data)

            # Update SizeOfImage
            pe_off_local = struct.unpack_from('<I', pe_data, 0x3C)[0]
            new_size_of_image = stub_rva + stub_data_aligned
            struct.pack_into('<I', pe_data, pe_off_local + 24 + 56, new_size_of_image)
            pe_info = dict(pe_info)
            pe_info['size_of_image'] = new_size_of_image

            pe_data = bytes(pe_data)
            print(f"    Allocated {len(unmapped_entries)} stub entries "
                  f"at RVA 0x{stub_rva:X} ({len(unmapped_entries) * 16} bytes)")

        print(f"    Mapped {mapped_vars} variables, {mapped_thunks} thunks, "
              f"{len(unmapped_entries)} stubs")

        # Verify all mapped VAs have valid XEX markers
        bad_markers = []
        for orig_va, new_va in va_mapping.items():
            rva = new_va - image_base
            if rva < 0 or rva + 4 > len(pe_data):
                bad_markers.append((orig_va, new_va, "OUT_OF_RANGE", rva))
                continue
            val = struct.unpack_from('>I', pe_data, rva)[0]
            rec_type = (val >> 24) & 0xFF
            if rec_type not in (0x00, 0x01):
                bad_markers.append((orig_va, new_va, f"BAD_TYPE={rec_type:#x}", val))
        if bad_markers:
            print(f"    WARNING: {len(bad_markers)} entries with invalid markers!")
            for orig_va, new_va, reason, extra in bad_markers[:10]:
                print(f"      orig={orig_va:#010x} → new={new_va:#010x}: {reason} (val={extra:#010x})")
        else:
            print(f"    Verified: all {len(va_mapping)} markers valid")

        # Patch import library header with new VAs
        patched_header = patch_import_library_header(
            orig[0x000103FF][1],
            import_libs_info,
            va_mapping
        )
        import_header_blob = (0x000103FF, patched_header)
        print("  Including patched import library header")
    elif 0x000103FF in orig:
        print("  Skipping import library header (no ordinal map available)")
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

    # Build security info with proper page descriptors for our PE layout
    page_descs = generate_page_descriptors(pe_data)
    section_types = {1: "CODE", 2: "DATA", 3: "RODATA"}
    print(f"  Page descriptors ({len(page_descs)} entries):")
    page_offset = 0
    for info, count in page_descs:
        stype = section_types.get(info, f"?({info})")
        print(f"    {stype:8s} {count:4d} pages, "
              f"RVA 0x{page_offset * 0x10000:08X}-0x{(page_offset + count) * 0x10000:08X}")
        page_offset += count
    orig_si = build_security_info(
        original_xex_info['security_info'],
        pe_info['size_of_image'],
        page_descs
    )

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

    # Decompress original XEX for reference
    print("\nDecompressing original XEX...")
    orig_pe_data = None
    try:
        orig_pe_data = decompress_xex_pe(orig_info['original_data'])
        print(f"  Decompressed PE: {len(orig_pe_data):,} bytes")
    except ValueError as e:
        print(f"  Warning: Could not decompress original XEX: {e}")

    # Find import thunks in .text by pattern matching.
    # For each thunk:
    # - Write XEX thunk marker (0x01XXXXXX) at the thunk CODE address
    #   (xenia reads this, resolves ordinal, overwrites with syscall stubs)
    # - Write XEX variable marker (0x00XXXXXX) at the IAT DATA address
    #   (xenia reads this, resolves ordinal, writes variable value)
    print("\nScanning for import thunks...")
    pe_data, ordinal_to_thunk_entries, ordinal_to_var_vas = find_and_convert_thunk_iat(
        pe_data, pe_info['image_base'])

    # Find remaining import variable IAT entries in .rdata
    # (variables not associated with any thunk)
    print("Scanning for additional import variables...")
    pe_data, extra_var_map = find_and_convert_variable_iat(pe_data, pe_info['image_base'])
    for ordinal, vas in extra_var_map.items():
        if ordinal not in ordinal_to_var_vas:
            ordinal_to_var_vas[ordinal] = vas
        else:
            ordinal_to_var_vas[ordinal].extend(vas)

    total_thunks = sum(len(v) for v in ordinal_to_thunk_entries.values())
    total_vars = sum(len(v) for v in ordinal_to_var_vas.values())
    print(f"  Total: {total_thunks} thunks, {total_vars} variables")

    # Convert PE from file layout to virtual memory layout.
    # XEX decompression maps the PE blob contiguously into guest memory,
    # so sections must be at their VirtualAddress offsets (not file offsets).
    print("\nConverting PE to virtual memory layout...")
    pe_data = pe_file_to_virtual(pe_data)
    print(f"  Virtual image: {len(pe_data):,} bytes (SizeOfImage)")

    # Build XEX
    print(f"\nBuilding XEX...")
    xex_data = build_xex(pe_data, orig_info, pe_info, orig_pe_data=orig_pe_data,
                         ordinal_to_thunk_entries=ordinal_to_thunk_entries,
                         ordinal_to_var_vas=ordinal_to_var_vas)
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
