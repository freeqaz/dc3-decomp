#!/usr/bin/env python3
"""
Read bytes from the active Ghidra MCP session and pretty-print decodes.

Useful for recovering unnamed tables/string constants (e.g. lbl_820010E0).

Examples:
  scripts/ghidra_read_bytes.py 0x82000b20 -n 64 --u32
  scripts/ghidra_read_bytes.py 0x82001a40 0x82001a4c -n 64 --strings
"""

from __future__ import annotations

import argparse
import string
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.ghidra.mcp_client import MCPClient, MCPError  # noqa: E402


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Read bytes from Ghidra MCP and decode them")
    p.add_argument("addresses", nargs="+", help="Address(es), e.g. 0x82000b20")
    p.add_argument("-n", "--size", type=int, default=64, help="Bytes to read per address")
    p.add_argument(
        "--binary",
        help="Optional Ghidra binary name (defaults to active default.xex-* auto-detect)",
    )
    p.add_argument(
        "--no-hexdump", action="store_true", help="Skip hexdump view (still prints decodes)"
    )
    p.add_argument("--u32", action="store_true", help="Decode as big-endian u32 words")
    p.add_argument("--u16", action="store_true", help="Decode as big-endian u16 words")
    p.add_argument(
        "--strings", action="store_true", help="Extract printable null-terminated strings"
    )
    p.add_argument(
        "--min-string-len",
        type=int,
        default=3,
        help="Minimum length for extracted strings (default: 3)",
    )
    p.add_argument("--raw-json", action="store_true", help="Print raw MCP response too")
    return p.parse_args()


def normalize_addr(addr: str) -> str:
    s = addr.strip().lower()
    if s.startswith("0x"):
        return s
    # Treat as hex by default for this use case.
    return "0x" + s


def decode_hex_bytes(hex_data: str) -> bytes:
    if len(hex_data) % 2 != 0:
        raise ValueError(f"Odd-length hex payload ({len(hex_data)})")
    return bytes.fromhex(hex_data)


def hexdump(data: bytes, base_addr: int, width: int = 16) -> str:
    lines: list[str] = []
    for off in range(0, len(data), width):
        chunk = data[off : off + width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"0x{base_addr + off:08x}  {hex_part:<{width*3-1}}  |{ascii_part}|")
    return "\n".join(lines)


def extract_cstrings(data: bytes, min_len: int) -> list[tuple[int, str]]:
    printable = set(bytes(string.printable, "ascii")) - {0x0b, 0x0c, 0x0d, 0x0a, 0x09}
    out: list[tuple[int, str]] = []
    i = 0
    n = len(data)
    while i < n:
        if data[i] == 0:
            i += 1
            continue
        start = i
        while i < n and data[i] in printable and data[i] != 0:
            i += 1
        if i < n and data[i] == 0 and (i - start) >= min_len:
            out.append((start, data[start:i].decode("ascii", errors="replace")))
            i += 1
        elif i == start:
            i += 1
    return out


def decode_u32_be(data: bytes) -> list[int]:
    usable = len(data) - (len(data) % 4)
    return [int.from_bytes(data[i : i + 4], "big") for i in range(0, usable, 4)]


def decode_u16_be(data: bytes) -> list[int]:
    usable = len(data) - (len(data) % 2)
    return [int.from_bytes(data[i : i + 2], "big") for i in range(0, usable, 2)]


def main() -> int:
    args = parse_args()

    try:
        client = MCPClient(binary=args.binary)
        client.initialize()
    except MCPError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    for idx, addr_in in enumerate(args.addresses):
        addr = normalize_addr(addr_in)
        if idx:
            print()
        print(f"== {addr} ==")

        try:
            res = client.read_bytes(addr, args.size)
        except MCPError as e:
            print(f"read failed: {e}")
            continue

        if args.raw_json:
            print(res)

        hex_data = res.get("data", "")
        if not isinstance(hex_data, str):
            print(f"unexpected response: {res}")
            continue

        data = decode_hex_bytes(hex_data)
        base_addr = int(res.get("address", addr).replace("0x", ""), 16)

        print(f"size: {len(data)} bytes")
        if not args.no_hexdump:
            print(hexdump(data, base_addr))

        if args.strings:
            strings_found = extract_cstrings(data, args.min_string_len)
            print("strings:")
            if not strings_found:
                print("  (none)")
            else:
                for off, s in strings_found:
                    print(f"  +0x{off:x} @ 0x{base_addr + off:08x}: {s}")

        if args.u16:
            vals16 = decode_u16_be(data)
            print("u16 (big-endian):")
            print(" ", vals16)

        if args.u32:
            vals32 = decode_u32_be(data)
            print("u32 (big-endian):")
            print(" ", vals32)
            print("u32 (hex):")
            print(" ", [f"0x{v:08x}" for v in vals32])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
