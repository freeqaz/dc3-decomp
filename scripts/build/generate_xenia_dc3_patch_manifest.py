#!/usr/bin/env python3
"""
Generate a machine-readable DC3 patch manifest for Xenia's title-specific
NUI/XBC resolver.

This is a post-link artifact derived from:
  - the linked PE (for .text fingerprint / section range)
  - config/373307D9/symbols.txt (for semantic target addresses)

Output: JSON manifest (default: build/373307D9/xenia_dc3_patch_manifest.json)

Fingerprint notes:
  - pe.text.fnv1a64 is the static PE/.text hash used for artifact identity.
  - pe.text.xenia_runtime_fnv1a64 is optional and should be populated from a
    Xenia runtime log if exact runtime-layout matching is desired.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path
from typing import Dict, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_PE = ROOT / "build" / "373307D9" / "default.exe"
DEFAULT_SYMBOLS = ROOT / "config" / "373307D9" / "symbols.txt"
DEFAULT_MAP = ROOT / "build" / "373307D9" / "default.map"
DEFAULT_OUT = ROOT / "build" / "373307D9" / "xenia_dc3_patch_manifest.json"

FNV64_OFFSET = 0xCBF29CE484222325
FNV64_PRIME = 0x100000001B3

SYMBOL_RE = re.compile(
    r"^(?P<name>.+?)\s*=\s*(?P<section>\.[A-Za-z0-9_$]+):0x(?P<addr>[0-9A-Fa-f]+);"
    r"(?:\s*//\s*(?P<meta>.*))?$"
)
SIZE_RE = re.compile(r"\bsize:0x([0-9A-Fa-f]+)\b")
TYPE_RE = re.compile(r"\btype:([A-Za-z_]+)\b")
MAP_PUBLIC_RE = re.compile(
    r"^\s*(?P<seg>[0-9A-Fa-f]{4}):(?P<off>[0-9A-Fa-f]{8})\s+"
    r"(?P<name>\S+)\s+(?P<abs>[0-9A-Fa-f]{8})\b"
)

CRT_SENTINELS = {"__xc_a", "__xc_z", "__xi_a", "__xi_z"}


def fnv1a64(data: bytes) -> int:
    h = FNV64_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def parse_pe_text_info(pe_data: bytes) -> Dict[str, int]:
    if pe_data[:2] != b"MZ":
        raise ValueError("not a PE (missing MZ)")
    pe_off = struct.unpack_from("<I", pe_data, 0x3C)[0]
    if pe_data[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise ValueError("not a PE (missing PE signature)")

    num_sections = struct.unpack_from("<H", pe_data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", pe_data, pe_off + 20)[0]
    opt_off = pe_off + 24
    image_base = struct.unpack_from("<I", pe_data, opt_off + 28)[0]
    sec_off = opt_off + opt_size

    for i in range(num_sections):
        off = sec_off + i * 40
        name = pe_data[off:off + 8].split(b"\x00", 1)[0]
        if name != b".text":
            continue
        vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", pe_data, off + 8)
        if raw_ptr + raw_size > len(pe_data):
            raise ValueError(".text raw range exceeds file size")
        if raw_size < vsize:
            # Rare, but pad to virtual size so the hash matches the runtime's
            # in-memory .text hashing semantics.
            text_blob = pe_data[raw_ptr:raw_ptr + raw_size] + b"\x00" * (vsize - raw_size)
        else:
            text_blob = pe_data[raw_ptr:raw_ptr + vsize]
        return {
            "image_base": image_base,
            "rva": vaddr,
            "size": vsize,
            "address": image_base + vaddr,
            "fnv1a64": fnv1a64(text_blob),
            "raw_size": raw_size,
        }

    raise ValueError(".text section not found")


def decompress_xex_to_pe_data(xex_path: Path) -> bytes:
    data = xex_path.read_bytes()
    if data[0:4] != b"XEX2":
        raise ValueError("not a XEX2 file")
    pe_offset = struct.unpack(">I", data[8:12])[0]
    opt_count = struct.unpack(">I", data[20:24])[0]

    bff_offset = None
    off = 24
    for _ in range(opt_count):
        hdr_id = struct.unpack(">I", data[off:off + 4])[0]
        hdr_val = struct.unpack(">I", data[off + 4:off + 8])[0]
        if hdr_id == 0x000003FF:
            bff_offset = hdr_val
            break
        off += 8
    if bff_offset is None:
        raise ValueError("no Base File Format header")

    size = struct.unpack(">I", data[bff_offset:bff_offset + 4])[0]
    enc_type = struct.unpack(">H", data[bff_offset + 4:bff_offset + 6])[0]
    comp_type = struct.unpack(">H", data[bff_offset + 6:bff_offset + 8])[0]
    if enc_type != 0:
        raise ValueError(f"encrypted XEX unsupported (enc_type={enc_type})")
    if comp_type == 0:
        pe_data = data[pe_offset:]
    elif comp_type == 1:
        num_blocks = (size - 8) // 8
        out = bytearray()
        data_offset = pe_offset
        for i in range(num_blocks):
            block_off = bff_offset + 8 + i * 8
            blk_size = struct.unpack(">I", data[block_off:block_off + 4])[0]
            blk_zeros = struct.unpack(">I", data[block_off + 4:block_off + 8])[0]
            out.extend(data[data_offset:data_offset + blk_size])
            data_offset += blk_size
            out.extend(b"\x00" * blk_zeros)
        pe_data = bytes(out)
    else:
        raise ValueError(f"unsupported XEX compression type {comp_type}")
    if pe_data[:2] != b"MZ":
        raise ValueError("decompressed XEX did not contain a PE")
    return pe_data


def should_include_text_symbol(name: str) -> bool:
    return (
        name.startswith("Nui")
        or name.startswith("Nuip")
        or name.startswith("D3DDevice_Nui")
        or name.startswith("CXbcImpl::")
    )


def canonicalize_map_symbol_name(name: str) -> str:
    # Linker maps use MSVC mangled names for many C++ symbols. Map a small set
    # of known DC3/Xenia patch targets back to the semantic names Xenia uses.
    if name.startswith("?") and "@@" in name:
        first_at = name.find("@")
        method = name[1:first_at] if first_at > 1 else ""
        cls_start = first_at + 1
        cls_end = name.find("@@", cls_start)
        cls = name[cls_start:cls_end] if cls_end != -1 else ""
        if cls == "CXbcImpl" and method in {"Initialize", "DoWork", "SendJSON"}:
            return f"CXbcImpl::{method}"
        if method.startswith("D3DDevice_Nui"):
            return method
    return name


def parse_symbols(symbols_path: Path) -> Tuple[Dict[str, dict], Dict[str, dict]]:
    targets: Dict[str, dict] = {}
    crt_sentinels: Dict[str, dict] = {}

    with symbols_path.open("r", encoding="utf-8", errors="replace") as f:
      for line in f:
        line = line.rstrip("\n")
        m = SYMBOL_RE.match(line)
        if not m:
          continue
        name = m.group("name").strip()
        section = m.group("section")
        address = int(m.group("addr"), 16)
        meta = m.group("meta") or ""
        type_match = TYPE_RE.search(meta)
        size_match = SIZE_RE.search(meta)
        entry = {
            "address": address,
            "section": section,
        }
        if type_match:
            entry["type"] = type_match.group(1)
        if size_match:
            entry["size"] = int(size_match.group(1), 16)

        if name in CRT_SENTINELS:
            crt_sentinels[name] = entry
            continue

        if section == ".text" and should_include_text_symbol(name):
            targets[name] = entry

    return targets, crt_sentinels


def parse_map_public_symbols(map_path: Path) -> Dict[str, int]:
    symbols: Dict[str, int] = {}
    with map_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = MAP_PUBLIC_RE.match(line.rstrip("\n"))
            if not m:
                continue
            name = canonicalize_map_symbol_name(m.group("name"))
            abs_addr = int(m.group("abs"), 16)
            symbols[name] = abs_addr
    return symbols


def infer_build_label(pe_path: Path) -> str:
    s = str(pe_path).lower()
    if "/orig/" in s or "\\orig\\" in s:
        return "original"
    if "/build/" in s or "\\build\\" in s:
        return "decomp"
    return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Xenia DC3 patch manifest")
    parser.add_argument("--pe", default=str(DEFAULT_PE), help="Path to linked PE (default.exe)")
    parser.add_argument("--xex", default=None, help="Optional built XEX path for runtime-accurate .text hash")
    parser.add_argument("--symbols", default=str(DEFAULT_SYMBOLS), help="Path to symbols.txt")
    parser.add_argument("--map", dest="map_path", default=str(DEFAULT_MAP),
                        help="Optional linker .map file for decomp addresses (default: build/373307D9/default.map)")
    parser.add_argument("--output", "-o", default=str(DEFAULT_OUT), help="Output manifest JSON")
    parser.add_argument(
        "--xenia-runtime-fnv1a64",
        default=None,
        help="Optional Xenia runtime .text FNV1a64 (hex) to embed as "
             "pe.text.xenia_runtime_fnv1a64 for exact layout matching",
    )
    parser.add_argument("--title-id", default="373307D9", help="Title ID (hex)")
    parser.add_argument(
        "--build-label",
        choices=["original", "decomp", "unknown"],
        default=None,
        help="Override build label (default: infer from PE path)",
    )
    args = parser.parse_args()

    pe_path = Path(args.pe)
    xex_path = Path(args.xex) if args.xex else None
    symbols_path = Path(args.symbols)
    map_path = Path(args.map_path) if args.map_path else None
    out_path = Path(args.output)

    if not pe_path.exists():
        print(f"error: PE not found: {pe_path}")
        return 1
    if not symbols_path.exists():
        print(f"error: symbols.txt not found: {symbols_path}")
        return 1

    pe_data = pe_path.read_bytes()
    static_text_info = parse_pe_text_info(pe_data)
    text_info = dict(static_text_info)
    if xex_path is not None:
        if not xex_path.exists():
            print(f"error: XEX not found: {xex_path}")
            return 1
        runtime_text_info = parse_pe_text_info(decompress_xex_to_pe_data(xex_path))
        # Prefer the runtime-equivalent fingerprint/range if provided.
        text_info["address"] = runtime_text_info["address"]
        text_info["rva"] = runtime_text_info["rva"]
        text_info["size"] = runtime_text_info["size"]
        text_info["raw_size"] = runtime_text_info["raw_size"]
        text_info["fnv1a64"] = runtime_text_info["fnv1a64"]
    targets, crt_sentinels = parse_symbols(symbols_path)
    map_symbols: Dict[str, int] = {}
    if map_path and map_path.exists():
        map_symbols = parse_map_public_symbols(map_path)
        for name, address in map_symbols.items():
            if name in CRT_SENTINELS:
                crt_sentinels.setdefault(name, {})["address"] = address
                crt_sentinels[name].setdefault("section", ".data")
                continue
            if should_include_text_symbol(name):
                targets.setdefault(name, {})["address"] = address
                targets[name].setdefault("section", ".text")
        # Prefer .map addresses when present for existing entries.
        for name, entry in list(targets.items()):
            if name in map_symbols:
                entry["address"] = map_symbols[name]
        for name, entry in list(crt_sentinels.items()):
            if name in map_symbols:
                entry["address"] = map_symbols[name]

    build_label = args.build_label or infer_build_label(pe_path)

    xenia_runtime_fingerprint: Optional[int] = None
    if args.xenia_runtime_fnv1a64:
        parsed_runtime_fp = 0
        value = args.xenia_runtime_fnv1a64.strip()
        if value.lower().startswith("0x"):
            value = value[2:]
        try:
            parsed_runtime_fp = int(value, 16)
        except ValueError:
            print(f"error: invalid --xenia-runtime-fnv1a64: {args.xenia_runtime_fnv1a64}")
            return 1
        xenia_runtime_fingerprint = parsed_runtime_fp & 0xFFFFFFFFFFFFFFFF

    manifest = {
        "schema_version": 1,
        "format_version": 1,
        "schema": "xenia.dc3.nui_patch_manifest",
        "title_id": args.title_id.upper(),
        "build_label": build_label,
        "build_identity": {
            "title_id": args.title_id.upper(),
            "build_label": build_label,
        },
        "pe": {
            "image_base": text_info["image_base"],
            "text": {
                "rva": text_info["rva"],
                "address": text_info["address"],
                "size": text_info["size"],
                "raw_size": text_info["raw_size"],
                "fnv1a64": text_info["fnv1a64"],
                "fnv1a64_static_pe": static_text_info["fnv1a64"],
                "xenia_runtime_fnv1a64": xenia_runtime_fingerprint,
            },
        },
        "targets": targets,
        "crt_sentinels": crt_sentinels,
        "sources": {
            "pe": str(pe_path),
            "xex": str(xex_path) if xex_path else None,
            "symbols": str(symbols_path),
            "map": str(map_path) if map_path and map_path.exists() else None,
        },
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")

    print(f"Wrote manifest: {out_path}")
    print(f"  build_label={build_label}")
    print(f"  .text addr=0x{text_info['address']:08X} size=0x{text_info['size']:X} "
          f"fnv1a64=0x{text_info['fnv1a64']:016X}")
    print(f"  .text static_pe_fnv1a64=0x{static_text_info['fnv1a64']:016X}")
    if xenia_runtime_fingerprint is not None:
        print(f"  .text xenia_runtime_fnv1a64=0x{xenia_runtime_fingerprint:016X}")
    print(f"  targets={len(targets)} crt_sentinels={len(crt_sentinels)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
