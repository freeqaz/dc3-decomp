#!/usr/bin/env python3
"""
Analyze shipped metadata in an Xbox 360 XEX and its extracted PE.

This is a byte-level inventory tool aimed at decomp work. It does not rely on
`jeff`'s current parser behavior and intentionally decodes several structures
directly from the raw files:

- XEX optional headers, including fixed-size pointer headers that `jeff`
  currently truncates
- XEX security info, page descriptors, hashes, and import-library descriptors
- PE sections, data directories, CodeView debug directory, and Rich header
- Xbox-specific import variables, architecture thunks, and `.pdata`
- Embedded XDBF resource data (achievements, title metadata, string tables)
- Source/debug path strings embedded in the shipped binary
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path


OPTIONAL_HEADER_NAMES = {
    0x000002FF: "ResourceInfo",
    0x000003FF: "BaseFileFormat",
    0x00000405: "BaseReference",
    0x000005FF: "DeltaPatchDescriptor",
    0x000080FF: "BoundingPath",
    0x00008105: "DeviceID",
    0x00010001: "OriginalBaseAddress",
    0x00010100: "EntryPoint",
    0x00010201: "ImageBaseAddress",
    0x000103FF: "ImportLibraries",
    0x00018002: "ChecksumTimestamp",
    0x00018102: "EnabledForCallcap",
    0x00018200: "EnabledForFastcap",
    0x000183FF: "OriginalPEName",
    0x000200FF: "StaticLibraries",
    0x00020104: "TLSInfo",
    0x00020200: "DefaultStackSize",
    0x00020301: "DefaultFilesystemCacheSize",
    0x00020401: "DefaultHeapSize",
    0x00028002: "PageHeapSizeAndFlags",
    0x00030000: "SystemFlags",
    0x00030100: "Unknown30100",
    0x00040006: "ExecutionID",
    0x000401FF: "ServiceIDList",
    0x00040201: "TitleWorkspaceSize",
    0x00040310: "GameRatings",
    0x00040404: "LANKey",
    0x000405FF: "Xbox360Logo",
    0x000406FF: "MultidiscMediaIDs",
    0x000407FF: "AlternateTitleIDs",
    0x00040801: "AdditionalTitleMemory",
    0x00E10402: "ExportsByName",
}

DATA_DIRECTORY_NAMES = [
    "ExportTable",
    "ImportTable",
    "ResourceTable",
    "ExceptionTable",
    "CertificateTable",
    "BaseRelocationTable",
    "Debug",
    "Architecture",
    "GlobalPtr",
    "TLS",
    "LoadConfig",
    "BoundImport",
    "IAT",
    "DelayImportDescriptor",
    "CLRRuntimeHeader",
    "Reserved",
]

RICH_TOOL_NAMES = {
    0x0001: "Import records",
    0x006D: "VS2005 C (likely RAD/Bink)",
    0x006E: "VS2005 C++ (likely RAD/Bink)",
    0x007B: "VS2005 tool",
    0x009C: "Xbox 360 tool",
    0x009D: "Xbox 360 tool",
    0x009E: "Xbox 360 tool",
    0x00AA: "Xbox 360 C compiler",
    0x00AB: "Xbox 360 C++ compiler",
}

TITLE_TYPE_NAMES = {
    0: "System",
    1: "Full",
    2: "Demo",
    3: "Download",
}

SOURCE_PATH_RE = re.compile(
    rb"[A-Za-z]:\\[^\x00\r\n]{1,220}\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl)\b",
    re.IGNORECASE,
)


def be16(buf: bytes, off: int) -> int:
    return struct.unpack_from(">H", buf, off)[0]


def be32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def le16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def le32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def le64(buf: bytes, off: int) -> int:
    return struct.unpack_from("<Q", buf, off)[0]


def to_hex(data: bytes, limit: int | None = None) -> str:
    if limit is not None and len(data) > limit:
        return data[:limit].hex() + "..."
    return data.hex()


def iso_ts(timestamp: int) -> str:
    return datetime.fromtimestamp(timestamp, timezone.utc).isoformat()


def decode_ascii(data: bytes) -> str:
    return data.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def decode_text(data: bytes) -> str:
    for encoding in ("utf-8", "cp1252", "latin-1"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("latin-1", errors="replace")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def version_tuple(word: int) -> tuple[int, int, int, int]:
    return ((word >> 28) & 0xF, (word >> 24) & 0xF, (word >> 8) & 0xFFFF, word & 0xFF)


def format_version(word: int) -> str:
    major, minor, build, qfe = version_tuple(word)
    return f"{major}.{minor}.{build}.{qfe}"


def parse_xex_optional_headers(data: bytes) -> list[dict]:
    count = be32(data, 20)
    headers = []
    for i in range(count):
        off = 24 + i * 8
        key = be32(data, off)
        value = be32(data, off + 4)
        mask = key & 0xFF
        mode = "inline"
        data_off = off + 4
        total_len = 4
        if mask == 0xFF:
            mode = "counted"
            total_len = be32(data, value)
            data_off = value + 4
        elif mask < 2:
            mode = "inline"
            total_len = 4
            data_off = off + 4
        else:
            mode = "pointer"
            total_len = mask * 4
            data_off = value
        data_end = data_off + max(total_len - (4 if mode == "counted" else 0), 0)
        if mode != "counted":
            data_end = data_off + total_len
        blob = data[data_off:data_end]
        headers.append(
            {
                "index": i,
                "key": key,
                "name": OPTIONAL_HEADER_NAMES.get(key, f"Unknown_{key:08X}"),
                "mask": mask,
                "value": value,
                "mode": mode,
                "data_offset": data_off,
                "data_len": len(blob),
                "data": blob,
            }
        )
    return headers


def parse_base_file_format(blob: bytes) -> dict:
    encryption = be16(blob, 0)
    compression = be16(blob, 2)
    out = {
        "encryption": encryption,
        "encryption_name": {0: "No", 1: "Yes"}.get(encryption, f"Unknown({encryption})"),
        "compression": compression,
        "compression_name": {0: "None", 1: "Raw", 2: "Compressed", 3: "DeltaCompressed"}.get(
            compression, f"Unknown({compression})"
        ),
        "raw_blocks": [],
        "normal": None,
    }
    if compression == 1:
        for off in range(4, len(blob), 8):
            if off + 8 > len(blob):
                break
            out["raw_blocks"].append({"size": be32(blob, off), "zero_size": be32(blob, off + 4)})
    elif compression in (2, 3) and len(blob) >= 32:
        out["normal"] = {
            "window_size": be32(blob, 4),
            "block_size": be32(blob, 8),
            "block_hash": blob[12:32].hex(),
        }
    return out


def reconstruct_xex_image(xex_data: bytes, pe_offset: int, bff: dict, image_size: int) -> bytes:
    if bff["encryption"] != 0:
        raise ValueError("Encrypted XEX images are not supported by this analyzer")
    if bff["compression"] != 1:
        raise ValueError(f"Unsupported XEX compression type {bff['compression']}")
    image = bytearray(image_size)
    pos_in = pe_offset
    pos_out = 0
    for block in bff["raw_blocks"]:
        size = block["size"]
        zero_size = block["zero_size"]
        image[pos_out:pos_out + size] = xex_data[pos_in:pos_in + size]
        pos_in += size
        pos_out += size + zero_size
    return bytes(image)


def image_to_pe_file_layout(image: bytes) -> bytes:
    if image[:2] != b"MZ":
        raise ValueError("Reconstructed image does not start with MZ")
    pe_off = le32(image, 0x3C)
    section_count = le16(image, pe_off + 6)
    opt_size = le16(image, pe_off + 20)
    sec_off = pe_off + 24 + opt_size
    out = bytearray()
    first_raw = None
    for i in range(section_count):
        off = sec_off + i * 40
        virtual_addr = le32(image, off + 12)
        raw_size = le32(image, off + 16)
        raw_ptr = le32(image, off + 20)
        chars = le32(image, off + 36)
        if first_raw is None:
            first_raw = raw_ptr
            out.extend(image[:raw_ptr])
        if chars & 0x80:
            continue
        section = image[virtual_addr:virtual_addr + raw_size]
        if len(section) < raw_size:
            section = section + b"\x00" * (raw_size - len(section))
        if len(out) < raw_ptr:
            out.extend(b"\x00" * (raw_ptr - len(out)))
        out.extend(section)
    return bytes(out)


def parse_loader_info(xex_data: bytes, security_offset: int) -> dict:
    pos = security_offset
    size = be32(xex_data, pos)
    image_size = be32(xex_data, pos + 4)
    sig_off = pos + 8
    info = {
        "size": size,
        "image_size": image_size,
        "rsa_signature_sha256": sha256(xex_data[sig_off:sig_off + 0x100]),
        "info_size": be32(xex_data, sig_off + 0x100),
        "image_flags": be32(xex_data, sig_off + 0x104),
        "load_address": be32(xex_data, sig_off + 0x108),
        "image_hash": xex_data[sig_off + 0x10C:sig_off + 0x120].hex(),
        "import_table_count": be32(xex_data, sig_off + 0x120),
        "import_digest": xex_data[sig_off + 0x124:sig_off + 0x138].hex(),
        "media_id": xex_data[sig_off + 0x138:sig_off + 0x148].hex(),
        "image_key": xex_data[sig_off + 0x148:sig_off + 0x158].hex(),
        "export_table_address": be32(xex_data, sig_off + 0x158),
        "header_hash": xex_data[sig_off + 0x15C:sig_off + 0x170].hex(),
        "game_region": be32(xex_data, sig_off + 0x170),
        "allowed_media_types": be32(xex_data, sig_off + 0x174),
        "page_descriptor_count": be32(xex_data, sig_off + 0x178),
        "page_descriptors": [],
    }
    page_pos = sig_off + 0x17C
    for i in range(info["page_descriptor_count"]):
        off = page_pos + i * 0x18
        word = be32(xex_data, off)
        info["page_descriptors"].append(
            {
                "index": i,
                "info": word & 0xF,
                "size_units": word >> 4,
                "digest": xex_data[off + 4:off + 0x18].hex(),
            }
        )
    return info


def parse_resource_infos(blob: bytes) -> list[dict]:
    out = []
    for off in range(0, len(blob), 16):
        chunk = blob[off:off + 16]
        if len(chunk) < 16:
            break
        title = decode_ascii(chunk[:8])
        start = be32(chunk, 8)
        size = be32(chunk, 12)
        out.append({"title_id": title, "start": start, "size": size, "end": start + size})
    return out


def parse_static_libraries(blob: bytes) -> list[dict]:
    libs = []
    for off in range(0, len(blob), 16):
        chunk = blob[off:off + 16]
        if len(chunk) < 16:
            break
        libs.append(
            {
                "name": decode_ascii(chunk[:8]),
                "major": be16(chunk, 8),
                "minor": be16(chunk, 10),
                "build": be16(chunk, 12),
                "approval_type": chunk[14],
                "qfe": chunk[15],
            }
        )
    return libs


def parse_import_libraries(blob: bytes) -> dict:
    string_size = be32(blob, 0)
    module_count = be32(blob, 4)
    strings_blob = blob[8:8 + string_size]
    names = [s.decode("ascii", errors="replace") for s in strings_blob.split(b"\x00") if s]
    pos = 8 + string_size
    libraries = []
    for _ in range(module_count):
        table_size = be32(blob, pos)
        next_digest = blob[pos + 4:pos + 24].hex()
        module_number = be32(blob, pos + 24)
        version = be32(blob, pos + 28)
        version_min = be32(blob, pos + 32)
        unused = blob[pos + 36]
        module_index = blob[pos + 37]
        import_count = be16(blob, pos + 38)
        records = [be32(blob, pos + 40 + i * 4) for i in range(import_count)]
        name = names[module_index] if module_index < len(names) else f"<bad-name-idx:{module_index}>"
        libraries.append(
            {
                "name": name,
                "table_size": table_size,
                "next_import_digest": next_digest,
                "module_number": module_number,
                "version": format_version(version),
                "version_min": format_version(version_min),
                "unused": unused,
                "module_index": module_index,
                "import_count": import_count,
                "records": records,
            }
        )
        pos += table_size
    return {"string_size": string_size, "module_count": module_count, "names": names, "libraries": libraries}


def interpret_optional_header(header: dict) -> dict:
    name = header["name"]
    blob = header["data"]
    if name == "BaseFileFormat":
        return parse_base_file_format(blob)
    if name == "ResourceInfo":
        return {"entries": parse_resource_infos(blob)}
    if name == "EntryPoint":
        return {"entry_point": be32(blob, 0)}
    if name == "ImageBaseAddress":
        return {"image_base": be32(blob, 0)}
    if name == "ImportLibraries":
        return parse_import_libraries(blob)
    if name == "ChecksumTimestamp":
        return {"checksum": be32(blob, 0), "timestamp": be32(blob, 4), "timestamp_iso": iso_ts(be32(blob, 4))}
    if name == "EnabledForCallcap":
        return {"begin": be32(blob, 0), "end": be32(blob, 4)}
    if name == "OriginalPEName":
        return {"name": decode_ascii(blob)}
    if name == "StaticLibraries":
        return {"libraries": parse_static_libraries(blob)}
    if name == "TLSInfo":
        return {
            "tls_slot_count": be32(blob, 0),
            "address_of_raw_data": be32(blob, 4),
            "size_of_raw_data": be32(blob, 8),
            "size_of_tls_data": be32(blob, 12),
        }
    if name in {"DefaultStackSize", "DefaultFilesystemCacheSize", "DefaultHeapSize", "TitleWorkspaceSize", "AdditionalTitleMemory"}:
        key = name[0].lower() + name[1:]
        return {key: be32(blob, 0)}
    if name == "PageHeapSizeAndFlags":
        return {"heap_size": be32(blob, 0), "flags": be32(blob, 4)}
    if name in {"SystemFlags", "Unknown30100"}:
        return {"value": be32(blob, 0)}
    if name == "ExecutionID":
        return {
            "media_id": be32(blob, 0),
            "version": format_version(be32(blob, 4)),
            "base_version": format_version(be32(blob, 8)),
            "title_id": be32(blob, 12),
            "platform": blob[16],
            "executable_type": blob[17],
            "disc_number": blob[18],
            "discs_in_set": blob[19],
            "save_game_id": be32(blob, 20),
        }
    if name == "ServiceIDList":
        size = be32(blob, 0)
        ids = []
        for off in range(4, min(size, len(blob)), 4):
            ids.append(be32(blob, off))
        return {"size": size, "custom_service_ids": ids}
    if name == "GameRatings":
        nonzero = [(i, b) for i, b in enumerate(blob) if b]
        return {"size": len(blob), "nonzero": nonzero[:16], "all_zero": not nonzero}
    if name == "LANKey":
        return {"hex": blob.hex(), "all_zero": set(blob) == {0}}
    if name == "AlternateTitleIDs":
        ids = [be32(blob, off) for off in range(0, len(blob), 4)]
        return {"title_ids": [x for x in ids if x]}
    return {"raw_hex": to_hex(blob, 64)}


def parse_pe(data: bytes) -> dict:
    if data[:2] != b"MZ":
        raise ValueError("PE does not start with MZ")
    pe_off = le32(data, 0x3C)
    if data[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise ValueError("Missing PE signature")
    section_count = le16(data, pe_off + 6)
    timestamp = le32(data, pe_off + 8)
    opt_size = le16(data, pe_off + 20)
    opt_off = pe_off + 24
    image_base = le32(data, opt_off + 28)
    section_alignment = le32(data, opt_off + 32)
    file_alignment = le32(data, opt_off + 36)
    entry_rva = le32(data, opt_off + 16)
    size_of_image = le32(data, opt_off + 56)
    size_of_headers = le32(data, opt_off + 60)
    subsystem = le16(data, opt_off + 68)
    stack_reserve = le32(data, opt_off + 72)
    stack_commit = le32(data, opt_off + 76)
    heap_reserve = le32(data, opt_off + 80)
    heap_commit = le32(data, opt_off + 84)
    num_dirs = le32(data, opt_off + 92)
    dir_off = opt_off + 96
    directories = []
    for i in range(min(num_dirs, len(DATA_DIRECTORY_NAMES))):
        directories.append(
            {
                "index": i,
                "name": DATA_DIRECTORY_NAMES[i],
                "rva": le32(data, dir_off + i * 8),
                "size": le32(data, dir_off + i * 8 + 4),
            }
        )
    sec_off = opt_off + opt_size
    sections = []
    for i in range(section_count):
        off = sec_off + i * 40
        sections.append(
            {
                "index": i + 1,
                "name": decode_ascii(data[off:off + 8]),
                "virtual_size": le32(data, off + 8),
                "virtual_address": le32(data, off + 12),
                "raw_size": le32(data, off + 16),
                "raw_ptr": le32(data, off + 20),
                "characteristics": le32(data, off + 36),
            }
        )
    return {
        "pe_offset": pe_off,
        "timestamp": timestamp,
        "timestamp_iso": iso_ts(timestamp),
        "image_base": image_base,
        "entry_rva": entry_rva,
        "entry_va": image_base + entry_rva,
        "section_alignment": section_alignment,
        "file_alignment": file_alignment,
        "size_of_image": size_of_image,
        "size_of_headers": size_of_headers,
        "subsystem": subsystem,
        "stack_reserve": stack_reserve,
        "stack_commit": stack_commit,
        "heap_reserve": heap_reserve,
        "heap_commit": heap_commit,
        "directories": directories,
        "sections": sections,
        "data": data,
    }


def find_directory(pe: dict, name: str) -> dict | None:
    return next((d for d in pe["directories"] if d["name"] == name), None)


def rva_to_file_offset(pe: dict, rva: int) -> int | None:
    if rva < pe["size_of_headers"]:
        return rva
    for sec in pe["sections"]:
        start = sec["virtual_address"]
        size = max(sec["virtual_size"], sec["raw_size"])
        end = start + size
        if start <= rva < end:
            return sec["raw_ptr"] + (rva - start)
    return None


def parse_debug_directory(pe: dict) -> list[dict]:
    directory = find_directory(pe, "Debug")
    if not directory or not directory["rva"] or not directory["size"]:
        return []
    off = rva_to_file_offset(pe, directory["rva"])
    if off is None:
        return []
    out = []
    for i in range(directory["size"] // 28):
        ent_off = off + i * 28
        entry = {
            "characteristics": le32(pe["data"], ent_off),
            "timestamp": le32(pe["data"], ent_off + 4),
            "major": le16(pe["data"], ent_off + 8),
            "minor": le16(pe["data"], ent_off + 10),
            "type": le32(pe["data"], ent_off + 12),
            "size_of_data": le32(pe["data"], ent_off + 16),
            "address_of_raw_data": le32(pe["data"], ent_off + 20),
            "pointer_to_raw_data": le32(pe["data"], ent_off + 24),
        }
        if entry["type"] == 2:
            raw = pe["data"][entry["pointer_to_raw_data"]:entry["pointer_to_raw_data"] + entry["size_of_data"]]
            if raw.startswith(b"RSDS") and len(raw) >= 24:
                guid = raw[4:20]
                entry["codeview"] = {
                    "signature": "RSDS",
                    "guid_bytes": guid.hex(),
                    "guid": (
                        f"{int.from_bytes(guid[0:4], 'little'):08x}-"
                        f"{int.from_bytes(guid[4:6], 'little'):04x}-"
                        f"{int.from_bytes(guid[6:8], 'little'):04x}-"
                        f"{guid[8:10].hex()}-{guid[10:16].hex()}"
                    ),
                    "age": le32(raw, 20),
                    "path": decode_ascii(raw[24:]),
                }
        out.append(entry)
    return out


def parse_rich_header(pe_data: bytes) -> dict | None:
    try:
        rich_off = pe_data.index(b"Rich")
    except ValueError:
        return None
    key = le32(pe_data, rich_off + 4)
    dans_off = None
    for off in range(0x80, rich_off, 4):
        if (le32(pe_data, off) ^ key) == 0x536E6144:
            dans_off = off
            break
    if dans_off is None:
        return None
    records = []
    for off in range(dans_off + 16, rich_off, 8):
        compid = le32(pe_data, off) ^ key
        count = le32(pe_data, off + 4) ^ key
        if compid == 0 and count == 0:
            continue
        tool_id = (compid >> 16) & 0xFFFF
        build = compid & 0xFFFF
        records.append(
            {
                "tool_id": tool_id,
                "build": build,
                "count": count,
                "description": RICH_TOOL_NAMES.get(tool_id, "Unknown"),
            }
        )
    return {"offset": rich_off, "xor_key": key, "dans_offset": dans_off, "records": records}


def parse_iat(pe: dict) -> dict:
    directory = find_directory(pe, "IAT")
    if not directory or not directory["rva"] or not directory["size"]:
        return {"entries": []}
    off = rva_to_file_offset(pe, directory["rva"])
    if off is None:
        return {"entries": []}
    entries = []
    for i in range(directory["size"] // 4):
        value = be32(pe["data"], off + i * 4)
        if value == 0:
            continue
        entries.append(
            {
                "slot_rva": directory["rva"] + i * 4,
                "value": value,
                "record_type": (value >> 24) & 0xFF,
                "library_index": (value >> 16) & 0xFF,
                "ordinal": value & 0xFFFF,
            }
        )
    return {"rva": directory["rva"], "size": directory["size"], "entries": entries}


def parse_architecture_thunks(pe: dict) -> dict:
    directory = find_directory(pe, "Architecture")
    if not directory or not directory["rva"] or not directory["size"]:
        return {"entries": []}
    off = rva_to_file_offset(pe, directory["rva"])
    if off is None:
        return {"entries": []}
    stub_tail = bytes.fromhex("7d6903a64e800420")
    entries = []
    for i in range(directory["size"] // 16):
        ent_off = off + i * 16
        raw = pe["data"][ent_off:ent_off + 16]
        value0 = be32(raw, 0)
        value1 = be32(raw, 4)
        entry = {
            "thunk_rva": directory["rva"] + i * 16,
            "word0": value0,
            "word1": value1,
            "matches_stub_tail": raw[8:] == stub_tail,
            "record_type": (value0 >> 24) & 0xFF,
            "library_index": (value0 >> 16) & 0xFF,
            "ordinal": value0 & 0xFFFF,
        }
        entries.append(entry)
    return {"rva": directory["rva"], "size": directory["size"], "entries": entries}


def parse_pdata(pe: dict) -> dict:
    directory = find_directory(pe, "ExceptionTable")
    if not directory or not directory["rva"] or not directory["size"]:
        return {"entries": []}
    off = rva_to_file_offset(pe, directory["rva"])
    if off is None:
        return {"entries": []}
    entries = []
    prolog_hist = Counter()
    type_hist = Counter()
    exception_samples = []
    for i in range(directory["size"] // 8):
        ent_off = off + i * 8
        begin_va = be32(pe["data"], ent_off)
        begin_rva = begin_va - pe["image_base"] if begin_va >= pe["image_base"] else begin_va
        word1 = be32(pe["data"], ent_off + 4)
        prolog = word1 & 0xFF
        function_length_insns = (word1 >> 8) & 0x3FFFFF
        function_type = (word1 >> 30) & 0x3
        prolog_hist[prolog] += 1
        type_hist[function_type] += 1
        entry = {
            "begin_rva": begin_rva,
            "begin_va": begin_va if begin_va >= pe["image_base"] else pe["image_base"] + begin_rva,
            "prolog_length_insns": prolog,
            "function_length_insns": function_length_insns,
            "function_length_bytes": function_length_insns * 4,
            "function_type": function_type,
        }
        if function_type == 3 and begin_rva >= 8:
            meta_off = rva_to_file_offset(pe, begin_rva - 8)
            if meta_off is not None:
                handler = be32(pe["data"], meta_off)
                record = be32(pe["data"], meta_off + 4)
                entry["exception_handler"] = handler
                entry["exception_record"] = record
                if len(exception_samples) < 8:
                    exception_samples.append(
                        {
                            "function_va": entry["begin_va"],
                            "handler": handler,
                            "record": record,
                        }
                    )
        entries.append(entry)
    return {
        "rva": directory["rva"],
        "size": directory["size"],
        "entries": entries,
        "prolog_histogram": dict(sorted(prolog_hist.items(), key=lambda kv: (-kv[1], kv[0]))),
        "function_type_histogram": dict(sorted(type_hist.items())),
        "exception_samples": exception_samples,
    }


def inspect_directory_payload(pe: dict, name: str, resource_infos: list[dict]) -> dict:
    directory = find_directory(pe, name)
    if not directory or not directory["rva"] or not directory["size"]:
        return {"present": False}
    off = rva_to_file_offset(pe, directory["rva"])
    if off is None:
        return {"present": True, "mapped": False}
    raw = pe["data"][off:off + min(directory["size"], 64)]
    resource_overlap = False
    resource_entry = None
    if resource_infos:
        resource_entry = resource_infos[0]
        resource_start_rva = resource_entry["start"] - pe["image_base"]
        resource_end_rva = resource_start_rva + resource_entry["size"]
        resource_overlap = not (
            directory["rva"] + directory["size"] <= resource_start_rva or directory["rva"] >= resource_end_rva
        )
    return {
        "present": True,
        "mapped": True,
        "rva": directory["rva"],
        "size": directory["size"],
        "file_offset": off,
        "overlaps_resource_blob": resource_overlap,
        "resource_entry": resource_entry,
        "first_bytes_hex": raw.hex(),
        "first_bytes_ascii": "".join(chr(b) if 32 <= b < 127 else "." for b in raw[:32]),
    }


def parse_xdbf(pe: dict, resource_infos: list[dict]) -> dict | None:
    if not resource_infos:
        return None
    resource_start_va = resource_infos[0]["start"]
    resource_rva = resource_start_va - pe["image_base"]
    base_off = rva_to_file_offset(pe, resource_rva)
    if base_off is None:
        return None
    data = pe["data"]
    if data[base_off:base_off + 4] != b"XDBF":
        return None
    version = be32(data, base_off + 4)
    entry_max = be32(data, base_off + 8)
    entry_current = be32(data, base_off + 12)
    free_max = be32(data, base_off + 16)
    free_current = be32(data, base_off + 20)
    entry_table_off = base_off + 24
    free_table_off = entry_table_off + entry_max * 18
    header_len = 24 + entry_max * 18 + free_max * 8
    entries = []
    type_hist = Counter()
    for i in range(entry_current):
        off = entry_table_off + i * 18
        entry_type = be16(data, off)
        entry_id = int.from_bytes(data[off + 2:off + 10], "big")
        entry_offset = be32(data, off + 10)
        entry_size = be32(data, off + 14)
        payload_off = base_off + header_len + entry_offset
        payload = data[payload_off:payload_off + entry_size]
        entry = {
            "type": entry_type,
            "id": entry_id,
            "id_ascii": bytes.fromhex(f"{entry_id:016x}").decode("ascii", errors="replace").lstrip("\x00"),
            "offset": entry_offset,
            "size": entry_size,
            "payload_off": payload_off,
        }
        if payload[:4] in {b"XACH", b"XTHD", b"XSTC", b"XCXT", b"XPRP", b"XPBM", b"XRPT", b"XMAT", b"XVC2", b"XSRC", b"XITB"}:
            entry["section_sig"] = payload[:4].decode("ascii")
        type_hist[entry_type] += 1
        entries.append(entry)

    sections = {}
    string_table_samples = []
    for entry in entries:
        payload = data[entry["payload_off"]:entry["payload_off"] + entry["size"]]
        sig = entry.get("section_sig")
        if sig == "XACH" and len(payload) >= 14:
            samples = []
            count = be16(payload, 12)
            pos = 14
            for _ in range(min(count, 5)):
                if pos + 36 > len(payload):
                    break
                samples.append(
                    {
                        "achievement_id": be16(payload, pos),
                        "label_string_id": be16(payload, pos + 2),
                        "description_string_id": be16(payload, pos + 4),
                        "unachieved_string_id": be16(payload, pos + 6),
                        "image_id": be32(payload, pos + 8),
                        "gamer_score": be16(payload, pos + 12),
                        "flags": be32(payload, pos + 16),
                    }
                )
                pos += 36
            sections["XACH"] = {"count": count, "samples": samples}
        elif sig == "XTHD" and len(payload) >= 36:
            sections["XTHD"] = {
                "title_id": be32(payload, 12),
                "title_type": TITLE_TYPE_NAMES.get(be32(payload, 16), f"Unknown({be32(payload, 16)})"),
                "version": f"{be16(payload, 20)}.{be16(payload, 22)}.{be16(payload, 24)}.{be16(payload, 26)}",
                "flags": be32(payload, 28),
            }
        elif entry["type"] == 3:
            sample = parse_xdbf_string_table(payload)
            if sample:
                string_table_samples.append({"id": entry["id"], **sample})

    return {
        "resource_start_va": resource_start_va,
        "resource_rva": resource_rva,
        "resource_file_offset": base_off,
        "version": version,
        "entry_max": entry_max,
        "entry_current": entry_current,
        "free_max": free_max,
        "free_current": free_current,
        "type_histogram": dict(sorted(type_hist.items())),
        "metadata_ids": sorted(
            entry["id_ascii"] for entry in entries if entry["type"] == 1 and entry["id_ascii"].isprintable()
        ),
        "sections": sections,
        "string_table_samples": string_table_samples[:6],
    }


def parse_xdbf_string_table(payload: bytes) -> dict | None:
    if len(payload) < 14:
        return None
    try:
        count = be16(payload, 12)
        pos = 14
        samples = []
        for _ in range(min(count, 4)):
            if pos + 4 > len(payload):
                break
            string_id = be16(payload, pos)
            length = be16(payload, pos + 2)
            pos += 4
            raw = payload[pos:pos + length]
            pos += length
            if raw:
                text = decode_text(raw)
                samples.append({"string_id": string_id, "text": text[:80]})
        if not samples:
            return None
        return {"count": count, "samples": samples}
    except (struct.error, ValueError):
        return None


def scan_source_paths(pe_data: bytes) -> dict:
    matches = sorted({m.group().decode("utf-8", errors="replace") for m in SOURCE_PATH_RE.finditer(pe_data)})
    return {"count": len(matches), "samples": matches[:20]}


def analyze(xex_path: Path, pe_path: Path) -> dict:
    xex_data = xex_path.read_bytes()
    pe_data = pe_path.read_bytes()

    if xex_data[:4] != b"XEX2":
        raise ValueError("Only XEX2 files are supported")

    xex_header = {
        "magic": xex_data[:4].decode("ascii"),
        "module_flags": be32(xex_data, 4),
        "pe_offset": be32(xex_data, 8),
        "discardable_headers_size": be32(xex_data, 12),
        "security_info_offset": be32(xex_data, 16),
        "optional_header_count": be32(xex_data, 20),
    }
    optional_headers = parse_xex_optional_headers(xex_data)
    interpreted = {header["name"]: interpret_optional_header(header) for header in optional_headers}
    loader_info = parse_loader_info(xex_data, xex_header["security_info_offset"])

    bff = interpreted["BaseFileFormat"]
    xex_image = reconstruct_xex_image(xex_data, xex_header["pe_offset"], bff, loader_info["image_size"])
    reconstructed_pe = image_to_pe_file_layout(xex_image)
    pe = parse_pe(pe_data)

    iat = parse_iat(pe)
    arch = parse_architecture_thunks(pe)
    pdata = parse_pdata(pe)
    resources = interpreted.get("ResourceInfo", {}).get("entries", [])
    import_payload = inspect_directory_payload(pe, "ImportTable", resources)
    reloc_payload = inspect_directory_payload(pe, "BaseRelocationTable", resources)
    xdbf = parse_xdbf(pe, resources)
    rich = parse_rich_header(pe_data)
    debug_entries = parse_debug_directory(pe)
    paths = scan_source_paths(pe_data)

    iat_by_lib = Counter(entry["library_index"] for entry in iat["entries"])
    arch_by_lib = Counter(
        entry["library_index"] for entry in arch["entries"] if entry["matches_stub_tail"] and entry["record_type"] == 1
    )
    xex_imports = interpreted.get("ImportLibraries", {}).get("libraries", [])

    return {
        "xex_path": str(xex_path),
        "pe_path": str(pe_path),
        "xex_header": xex_header,
        "loader_info": loader_info,
        "optional_headers": [
            {key: value for key, value in header.items() if key != "data"}
            | {"data_preview_hex": to_hex(header["data"], 32)}
            for header in optional_headers
        ],
        "optional_header_values": interpreted,
        "reconstructed_pe_matches": reconstructed_pe == pe_data,
        "reconstructed_pe_sha256": sha256(reconstructed_pe),
        "provided_pe_sha256": sha256(pe_data),
        "pe": {k: v for k, v in pe.items() if k != "data"},
        "debug_directory": debug_entries,
        "rich_header": rich,
        "iat": {
            "rva": iat.get("rva"),
            "size": iat.get("size"),
            "entry_count": len(iat["entries"]),
            "by_library_index": dict(sorted(iat_by_lib.items())),
            "samples": iat["entries"][:12],
        },
        "architecture_thunks": {
            "rva": arch.get("rva"),
            "size": arch.get("size"),
            "entry_count": len(arch["entries"]),
            "stub_count": sum(1 for entry in arch["entries"] if entry["matches_stub_tail"]),
            "by_library_index": dict(sorted(arch_by_lib.items())),
            "samples": arch["entries"][:8],
        },
        "import_libraries": {
            "library_count": len(xex_imports),
            "libraries": [
                {
                    key: value
                    for key, value in lib.items()
                    if key != "records"
                }
                | {"record_samples": lib["records"][:8]}
                for lib in xex_imports
            ],
        },
        "pdata": {
            "rva": pdata.get("rva"),
            "size": pdata.get("size"),
            "entry_count": len(pdata["entries"]),
            "prolog_histogram": pdata["prolog_histogram"],
            "function_type_histogram": pdata["function_type_histogram"],
            "exception_samples": pdata["exception_samples"],
        },
        "directory_payloads": {
            "ImportTable": import_payload,
            "BaseRelocationTable": reloc_payload,
        },
        "xdbf": xdbf,
        "source_paths": paths,
    }


def emit_text(report: dict) -> str:
    lines = []
    xex = report["xex_header"]
    loader = report["loader_info"]
    pe = report["pe"]
    opt = report["optional_header_values"]

    lines.append("== XEX Header ==")
    lines.append(
        f"module_flags=0x{xex['module_flags']:08X} pe_offset=0x{xex['pe_offset']:X} "
        f"security_info=0x{xex['security_info_offset']:X} optional_headers={xex['optional_header_count']}"
    )
    lines.append("")

    lines.append("== XEX Security / Loader Info ==")
    lines.append(
        f"image_size=0x{loader['image_size']:X} load_address=0x{loader['load_address']:08X} "
        f"info_size=0x{loader['info_size']:X} import_tables={loader['import_table_count']} "
        f"page_descriptors={loader['page_descriptor_count']}"
    )
    lines.append(
        f"game_region=0x{loader['game_region']:08X} allowed_media=0x{loader['allowed_media_types']:08X} "
        f"image_hash={loader['image_hash']}"
    )
    lines.append(
        f"import_digest={loader['import_digest']} header_hash={loader['header_hash']} media_id={loader['media_id']}"
    )
    page_hist = Counter(desc["size_units"] for desc in loader["page_descriptors"])
    info_hist = Counter(desc["info"] for desc in loader["page_descriptors"])
    lines.append(
        f"page_descriptor_size_units={dict(sorted(page_hist.items()))} page_descriptor_info={dict(sorted(info_hist.items()))}"
    )
    lines.append("")

    lines.append("== Optional Headers ==")
    for header in report["optional_headers"]:
        lines.append(
            f"{header['index']:2d}: {header['name']:<20} mode={header['mode']:<7} "
            f"key=0x{header['key']:08X} value=0x{header['value']:08X} data_len=0x{header['data_len']:X}"
        )
    lines.append("")

    lines.append("== Key Optional Header Values ==")
    lines.append(
        f"OriginalPEName={opt['OriginalPEName']['name']} "
        f"EntryPoint=0x{opt['EntryPoint']['entry_point']:08X} "
        f"ImageBase=0x{opt['ImageBaseAddress']['image_base']:08X}"
    )
    lines.append(
        f"Checksum=0x{opt['ChecksumTimestamp']['checksum']:08X} "
        f"Timestamp={opt['ChecksumTimestamp']['timestamp_iso']}"
    )
    lines.append(
        f"Callcap=0x{opt['EnabledForCallcap']['begin']:08X}..0x{opt['EnabledForCallcap']['end']:08X} "
        f"TLS={opt['TLSInfo']} StackSize=0x{opt['DefaultStackSize']['defaultStackSize']:X}"
    )
    lines.append(
        f"SystemFlags=0x{opt['SystemFlags']['value']:08X} Unknown30100=0x{opt['Unknown30100']['value']:08X} "
        f"TitleWorkspaceSize=0x{opt['TitleWorkspaceSize']['titleWorkspaceSize']:X}"
    )
    lines.append(
        f"ExecutionID title=0x{opt['ExecutionID']['title_id']:08X} version={opt['ExecutionID']['version']} "
        f"base={opt['ExecutionID']['base_version']} disc={opt['ExecutionID']['disc_number']}/{opt['ExecutionID']['discs_in_set']}"
    )
    lines.append(
        f"GameRatings size={opt['GameRatings']['size']} all_zero={opt['GameRatings']['all_zero']} "
        f"LANKey_all_zero={opt['LANKey']['all_zero']} AlternateTitleIDs={[f'0x{x:08X}' for x in opt['AlternateTitleIDs']['title_ids']]}"
    )
    resource_entries = opt.get("ResourceInfo", {}).get("entries", [])
    if resource_entries:
        entry = resource_entries[0]
        lines.append(
            f"ResourceInfo title={entry['title_id']} start=0x{entry['start']:08X} size=0x{entry['size']:X} end=0x{entry['end']:08X}"
        )
    lines.append("")

    lines.append("== Static Libraries ==")
    for lib in opt["StaticLibraries"]["libraries"]:
        lines.append(
            f"{lib['name']}: v{lib['major']}.{lib['minor']}.{lib['build']}.{lib['qfe']} approval={lib['approval_type']}"
        )
    lines.append("")

    lines.append("== XEX Import Libraries ==")
    for lib in report["import_libraries"]["libraries"]:
        lines.append(
            f"{lib['module_index']}: {lib['name']} module={lib['module_number']} version={lib['version']} "
            f"min={lib['version_min']} imports={lib['import_count']} digest={lib['next_import_digest']}"
        )
    lines.append("")

    lines.append("== PE Header ==")
    lines.append(
        f"timestamp={pe['timestamp_iso']} image_base=0x{pe['image_base']:08X} entry_va=0x{pe['entry_va']:08X} "
        f"size_of_image=0x{pe['size_of_image']:X}"
    )
    lines.append(
        f"stack=0x{pe['stack_reserve']:X}/0x{pe['stack_commit']:X} "
        f"heap=0x{pe['heap_reserve']:X}/0x{pe['heap_commit']:X} reconstructed_match={report['reconstructed_pe_matches']}"
    )
    lines.append("")

    lines.append("== PE Sections ==")
    for sec in pe["sections"]:
        magic = ""
        if sec["raw_size"]:
            raw = Path(report["pe_path"]).read_bytes()[sec["raw_ptr"]:sec["raw_ptr"] + 4]
            if all(32 <= b < 127 for b in raw):
                magic = f" magic={raw.decode('ascii', errors='replace')}"
        lines.append(
            f"{sec['index']:2d}: {sec['name']:<8} RVA=0x{sec['virtual_address']:08X} VSZ=0x{sec['virtual_size']:X} "
            f"RAW=0x{sec['raw_ptr']:08X}+0x{sec['raw_size']:X}{magic}"
        )
    lines.append("")

    lines.append("== Data Directories ==")
    for directory in pe["directories"]:
        lines.append(f"{directory['name']:<20} RVA=0x{directory['rva']:08X} size=0x{directory['size']:X}")
    lines.append("")

    lines.append("== Directory Payload Checks ==")
    for name, payload in report["directory_payloads"].items():
        if not payload["present"]:
            continue
        lines.append(
            f"{name}: file_off=0x{payload['file_offset']:X} overlaps_resource_blob={payload['overlaps_resource_blob']} "
            f"ascii={payload['first_bytes_ascii']!r}"
        )
        lines.append(f"{name}: first_bytes={payload['first_bytes_hex'][:96]}...")
    lines.append("")

    lines.append("== Debug Directory ==")
    for entry in report["debug_directory"]:
        if "codeview" in entry:
            cv = entry["codeview"]
            lines.append(
                f"CodeView RSDS guid={cv['guid']} age={cv['age']} path={cv['path']}"
            )
    lines.append("")

    rich = report["rich_header"]
    if rich:
        lines.append("== Rich Header ==")
        lines.append(f"offset=0x{rich['offset']:X} xor_key=0x{rich['xor_key']:08X}")
        for record in rich["records"]:
            lines.append(
                f"tool=0x{record['tool_id']:04X} build={record['build']} count={record['count']} {record['description']}"
            )
        lines.append("")

    lines.append("== Import Variables / Thunks ==")
    lines.append(
        f"IAT entries={report['iat']['entry_count']} by_library={report['iat']['by_library_index']}"
    )
    lines.append(
        f"Architecture thunks={report['architecture_thunks']['entry_count']} "
        f"stub_thunks={report['architecture_thunks']['stub_count']} "
        f"by_library={report['architecture_thunks']['by_library_index']}"
    )
    lines.append("")

    lines.append("== .pdata ==")
    lines.append(
        f"entries={report['pdata']['entry_count']} prolog_histogram={report['pdata']['prolog_histogram']}"
    )
    lines.append(f"function_type_histogram={report['pdata']['function_type_histogram']}")
    for sample in report["pdata"]["exception_samples"][:6]:
        lines.append(
            f"exception_meta func=0x{sample['function_va']:08X} handler=0x{sample['handler']:08X} record=0x{sample['record']:08X}"
        )
    lines.append("")

    if report["xdbf"]:
        xdbf = report["xdbf"]
        lines.append("== Embedded XDBF ==")
        lines.append(
            f"resource_va=0x{xdbf['resource_start_va']:08X} version=0x{xdbf['version']:08X} "
            f"entries={xdbf['entry_current']}/{xdbf['entry_max']} free={xdbf['free_current']}/{xdbf['free_max']} "
            f"type_histogram={xdbf['type_histogram']}"
        )
        lines.append(f"metadata_ids={xdbf['metadata_ids']}")
        if "XTHD" in xdbf["sections"]:
            lines.append(f"XTHD={xdbf['sections']['XTHD']}")
        if "XACH" in xdbf["sections"]:
            lines.append(f"XACH count={xdbf['sections']['XACH']['count']} samples={xdbf['sections']['XACH']['samples']}")
        for table in xdbf["string_table_samples"]:
            lines.append(f"string_table id=0x{table['id']:016X} samples={table['samples']}")
        lines.append("")

    lines.append("== Embedded Source Paths ==")
    lines.append(f"count={report['source_paths']['count']}")
    for sample in report["source_paths"]["samples"][:12]:
        lines.append(sample)

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Deep raw-byte analysis for Xbox 360 XEX/PE pairs")
    parser.add_argument("xex", nargs="?", default="orig/373307D9/default.xex", help="Path to input XEX")
    parser.add_argument("pe", nargs="?", default="orig/373307D9/ham_xbox_r.exe", help="Path to extracted PE")
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of text")
    args = parser.parse_args()

    report = analyze(Path(args.xex), Path(args.pe))
    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        print(emit_text(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
