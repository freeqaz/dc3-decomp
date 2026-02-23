#!/usr/bin/env python3
"""
Scan all .obj files in DC3 decomp build for specific missing symbol definitions.

Parses PE/COFF object files (little-endian) to find:
  1. ?RawAlloc@FixedSizeAlloc@@MAAPAHH@Z
  2. ?RawAlloc@ReclaimableAlloc@@MAAPAHH@Z
  3. ??_GReclaimableAlloc@@UAAPAXI@Z
  4. ??_GFixedSizeAlloc@@UAAPAXI@Z  (comparison symbol)

For each match, reports: file, defined/undefined, storage class, section,
COMDAT flag, and section raw data size.
"""

import struct
import os
import sys
from pathlib import Path
from collections import defaultdict

# Target symbols
TARGET_SYMBOLS = {
    "?RawAlloc@FixedSizeAlloc@@MAAPAHH@Z",
    "?RawAlloc@ReclaimableAlloc@@MAAPAHH@Z",
    "??_GReclaimableAlloc@@UAAPAXI@Z",
    "??_GFixedSizeAlloc@@UAAPAXI@Z",  # comparison
}

# COFF constants
IMAGE_SCN_LNK_COMDAT = 0x00001000

STORAGE_CLASS_NAMES = {
    0: "NULL",
    1: "AUTOMATIC",
    2: "EXTERNAL",
    3: "STATIC",
    4: "REGISTER",
    5: "EXTERN_DEF",
    6: "LABEL",
    7: "UNDEF_LABEL",
    8: "MEMBER_OF_STRUCT",
    9: "ARGUMENT",
    10: "STRUCT_TAG",
    11: "MEMBER_OF_UNION",
    12: "UNION_TAG",
    13: "TYPE_DEFINITION",
    14: "UNDEF_STATIC",
    15: "ENUM_TAG",
    16: "MEMBER_OF_ENUM",
    17: "REGISTER_PARAM",
    18: "BIT_FIELD",
    100: "BLOCK",
    101: "FUNCTION",
    102: "END_OF_STRUCT",
    103: "FILE",
    104: "SECTION",
    105: "WEAK_EXTERNAL",
    107: "CLR_TOKEN",
}


def parse_obj_file(filepath):
    """Parse a COFF .obj file and yield matches for target symbols."""
    try:
        with open(filepath, "rb") as f:
            data = f.read()
    except (IOError, OSError) as e:
        yield {"error": f"Cannot read file: {e}"}
        return

    if len(data) < 20:
        return  # Too small to be a valid COFF

    # Try to detect endianness from machine type
    machine_le = struct.unpack_from("<H", data, 0)[0]
    machine_be = struct.unpack_from(">H", data, 0)[0]

    endian = "<"  # default little-endian
    if machine_le == 0x01F2:
        endian = "<"
    elif machine_be == 0x01F2:
        endian = ">"
    elif machine_le in (0x014C, 0x8664, 0x0000):
        endian = "<"
    else:
        # Try both: whichever gives a sane symtab offset
        for try_endian in ("<", ">"):
            _, num_sec, _, symtab_off, num_sym, opt_hdr_sz, _ = struct.unpack_from(
                f"{try_endian}HHIIIHH", data, 0
            )
            if 0 < num_sec < 10000 and 0 < symtab_off < len(data) and num_sym < 1000000:
                endian = try_endian
                break

    # Parse COFF header
    machine, num_sections, timestamp, symtab_offset, num_symbols, opt_hdr_size, characteristics = \
        struct.unpack_from(f"{endian}HHIIIHH", data, 0)

    if num_symbols == 0 or symtab_offset == 0:
        return  # No symbol table

    if symtab_offset >= len(data):
        return  # Invalid

    # Parse section headers (start right after COFF header + optional header)
    section_hdr_offset = 20 + opt_hdr_size
    sections = {}  # 1-indexed

    for i in range(num_sections):
        off = section_hdr_offset + i * 40
        if off + 40 > len(data):
            break

        sec_name_raw = data[off:off + 8]
        sec_name = sec_name_raw.split(b'\x00')[0].decode('ascii', errors='replace')

        s_vsize, s_vaddr, s_rawsize, s_rawptr, s_relocptr, s_linenoptr, \
            s_nrelocs, s_nlinenums, s_flags = struct.unpack_from(
                f"{endian}IIIIIIHHI", data, off + 8
            )

        sections[i + 1] = {
            "name": sec_name,
            "virtual_size": s_vsize,
            "raw_data_size": s_rawsize,
            "raw_data_ptr": s_rawptr,
            "flags": s_flags,
            "is_comdat": bool(s_flags & IMAGE_SCN_LNK_COMDAT),
            "reloc_ptr": s_relocptr,
            "num_relocs": s_nrelocs,
        }

    # Parse string table
    strtab_offset = symtab_offset + num_symbols * 18
    strtab = b""
    strtab_size = 0

    if strtab_offset + 4 <= len(data):
        strtab_size = struct.unpack_from(f"{endian}I", data, strtab_offset)[0]
        if strtab_size > 4 and strtab_offset + strtab_size <= len(data):
            strtab = data[strtab_offset:strtab_offset + strtab_size]
        elif strtab_size > 4:
            strtab = data[strtab_offset:]  # truncated, take what we can

    def get_string_from_strtab(offset):
        """Get a null-terminated string from the string table."""
        if offset < 0 or offset >= len(strtab):
            return f"<invalid_strtab_offset:{offset}>"
        end = strtab.index(b'\x00', offset) if b'\x00' in strtab[offset:] else len(strtab)
        return strtab[offset:end].decode('ascii', errors='replace')

    # Parse symbol table
    idx = 0
    while idx < num_symbols:
        sym_offset = symtab_offset + idx * 18
        if sym_offset + 18 > len(data):
            break

        # Symbol entry: 18 bytes
        name_field = data[sym_offset:sym_offset + 8]
        value = struct.unpack_from(f"{endian}I", data, sym_offset + 8)[0]
        section_number = struct.unpack_from(f"{endian}h", data, sym_offset + 12)[0]  # signed short
        sym_type = struct.unpack_from(f"{endian}H", data, sym_offset + 14)[0]
        storage_class = struct.unpack_from("B", data, sym_offset + 16)[0]
        num_aux = struct.unpack_from("B", data, sym_offset + 17)[0]

        # Decode symbol name
        first_four = struct.unpack_from(f"{endian}I", name_field, 0)[0]
        if first_four == 0:
            str_offset = struct.unpack_from(f"{endian}I", name_field, 4)[0]
            sym_name = get_string_from_strtab(str_offset)
        else:
            sym_name = name_field.split(b'\x00')[0].decode('ascii', errors='replace')

        # Check if this symbol matches any target
        if sym_name in TARGET_SYMBOLS:
            # Determine defined vs undefined
            if section_number > 0:
                status = "DEFINED"
            elif section_number == 0:
                status = "UNDEFINED (EXTERN)"
            elif section_number == -1:
                status = "ABSOLUTE"
            elif section_number == -2:
                status = "DEBUG"
            else:
                status = f"SPECIAL({section_number})"

            sc_name = STORAGE_CLASS_NAMES.get(storage_class, f"UNKNOWN({storage_class})")

            result = {
                "symbol": sym_name,
                "status": status,
                "section_number": section_number,
                "value": value,
                "type": sym_type,
                "storage_class": storage_class,
                "storage_class_name": sc_name,
                "num_aux": num_aux,
                "symbol_index": idx,
            }

            # If defined, add section info
            if section_number > 0 and section_number in sections:
                sec = sections[section_number]
                result["section_name"] = sec["name"]
                result["section_raw_size"] = sec["raw_data_size"]
                result["section_flags"] = f"0x{sec['flags']:08X}"
                result["section_is_comdat"] = sec["is_comdat"]
                result["section_num_relocs"] = sec["num_relocs"]
            elif section_number > 0:
                result["section_name"] = f"<section {section_number} not found in headers>"

            # Parse aux symbol for COMDAT selection type if applicable
            if num_aux > 0 and section_number > 0:
                aux_offset = symtab_offset + (idx + 1) * 18
                if aux_offset + 18 <= len(data):
                    aux_length = struct.unpack_from(f"{endian}I", data, aux_offset)[0]
                    aux_nreloc = struct.unpack_from(f"{endian}H", data, aux_offset + 4)[0]
                    aux_nline = struct.unpack_from(f"{endian}H", data, aux_offset + 6)[0]
                    aux_checksum = struct.unpack_from(f"{endian}I", data, aux_offset + 8)[0]
                    aux_number = struct.unpack_from(f"{endian}H", data, aux_offset + 12)[0]
                    aux_selection = struct.unpack_from("B", data, aux_offset + 14)[0]

                    COMDAT_SELECTIONS = {
                        0: "IMAGE_COMDAT_SELECT_NODUPLICATES (0)",
                        1: "IMAGE_COMDAT_SELECT_NODUPLICATES",
                        2: "IMAGE_COMDAT_SELECT_ANY",
                        3: "IMAGE_COMDAT_SELECT_SAME_SIZE",
                        4: "IMAGE_COMDAT_SELECT_EXACT_MATCH",
                        5: "IMAGE_COMDAT_SELECT_ASSOCIATIVE",
                        6: "IMAGE_COMDAT_SELECT_LARGEST",
                    }
                    result["aux_comdat_selection"] = COMDAT_SELECTIONS.get(
                        aux_selection, f"UNKNOWN({aux_selection})"
                    )
                    result["aux_checksum"] = f"0x{aux_checksum:08X}"
                    result["aux_assoc_section"] = aux_number

            yield result

        idx += 1 + num_aux  # Skip auxiliary symbol entries


def main():
    obj_dir = Path(os.path.expanduser("~/code/milohax/dc3-decomp/build/373307D9/obj/"))

    if not obj_dir.exists():
        print(f"ERROR: Directory not found: {obj_dir}")
        sys.exit(1)

    obj_files = sorted(obj_dir.rglob("*.obj"))
    print(f"Scanning {len(obj_files)} .obj files in {obj_dir}")
    print(f"Looking for symbols:")
    for s in sorted(TARGET_SYMBOLS):
        print(f"  {s}")
    print()
    print("=" * 120)

    # Collect all results
    all_results = defaultdict(list)  # symbol -> list of (file, result)
    files_with_errors = []
    files_scanned = 0

    for obj_path in obj_files:
        files_scanned += 1
        rel_path = obj_path.relative_to(obj_dir)

        for result in parse_obj_file(obj_path):
            if "error" in result:
                files_with_errors.append((rel_path, result["error"]))
                continue

            sym = result["symbol"]
            all_results[sym].append((rel_path, result))

    # Print results per symbol
    for sym in sorted(TARGET_SYMBOLS):
        matches = all_results.get(sym, [])
        defined_matches = [m for m in matches if m[1]["status"] == "DEFINED"]
        undef_matches = [m for m in matches if m[1]["status"] != "DEFINED"]

        print(f"\n{'=' * 120}")
        print(f"SYMBOL: {sym}")
        print(f"  Total references: {len(matches)}")
        print(f"  DEFINED in:       {len(defined_matches)} file(s)")
        print(f"  UNDEFINED in:     {len(undef_matches)} file(s)")
        print(f"{'~' * 120}")

        # Show defined matches first
        if defined_matches:
            print(f"\n  *** DEFINED INSTANCES ***")
            for rel_path, r in sorted(defined_matches, key=lambda x: str(x[0])):
                print(f"\n  FILE: {rel_path}")
                print(f"    Status:         {r['status']}")
                print(f"    Storage class:  {r['storage_class']} ({r['storage_class_name']})")
                print(f"    Section:        {r['section_number']}")
                print(f"    Value (offset): 0x{r['value']:08X}")
                print(f"    Symbol type:    0x{r['type']:04X}")
                print(f"    Symbol index:   {r['symbol_index']}")
                if "section_name" in r:
                    print(f"    Section name:   {r['section_name']}")
                    print(f"    Section size:   {r.get('section_raw_size', 'N/A')} bytes")
                    print(f"    Section flags:  {r.get('section_flags', 'N/A')}")
                    print(f"    Is COMDAT:      {r.get('section_is_comdat', 'N/A')}")
                    print(f"    Sec relocs:     {r.get('section_num_relocs', 'N/A')}")
                if "aux_comdat_selection" in r:
                    print(f"    COMDAT select:  {r['aux_comdat_selection']}")
                    print(f"    COMDAT chksum:  {r['aux_checksum']}")
                    print(f"    Assoc section:  {r['aux_assoc_section']}")
        else:
            print(f"\n  *** NO DEFINITIONS FOUND ***")
            print(f"  This symbol is NEVER defined in any .obj file!")

        # Show undefined references
        if undef_matches:
            print(f"\n  Undefined references ({len(undef_matches)} files):")
            for rel_path, r in sorted(undef_matches, key=lambda x: str(x[0])):
                sc = r['storage_class_name']
                print(f"    {rel_path}  [storage={sc}, sec={r['section_number']}]")

    # Summary
    print(f"\n\n{'=' * 120}")
    print(f"SUMMARY")
    print(f"{'=' * 120}")
    print(f"Files scanned:  {files_scanned}")
    print(f"Files with errors: {len(files_with_errors)}")
    if files_with_errors:
        for fp, err in files_with_errors[:10]:
            print(f"  {fp}: {err}")
        if len(files_with_errors) > 10:
            print(f"  ... and {len(files_with_errors) - 10} more")

    print()
    for sym in sorted(TARGET_SYMBOLS):
        matches = all_results.get(sym, [])
        defined = [m for m in matches if m[1]["status"] == "DEFINED"]
        undef = [m for m in matches if m[1]["status"] != "DEFINED"]
        tag = "MISSING" if not defined else "FOUND"
        print(f"  [{tag:7s}] {sym}")
        print(f"           {len(defined)} definition(s), {len(undef)} external reference(s)")

    # Cross-reference: which files reference missing symbols?
    missing_syms = [s for s in TARGET_SYMBOLS if not any(
        m[1]["status"] == "DEFINED" for m in all_results.get(s, [])
    )]
    if missing_syms:
        print(f"\n{'=' * 120}")
        print(f"LINKER IMPACT: Files that reference missing symbols")
        print(f"{'=' * 120}")
        file_missing = defaultdict(set)
        for sym in missing_syms:
            for rel_path, r in all_results.get(sym, []):
                file_missing[str(rel_path)].add(sym)
        for fp in sorted(file_missing):
            print(f"\n  {fp}:")
            for s in sorted(file_missing[fp]):
                print(f"    - {s}")


if __name__ == "__main__":
    main()
