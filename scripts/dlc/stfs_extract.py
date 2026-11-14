#!/usr/bin/env python3
"""Read-only Xbox 360 STFS (LIVE/PIRS/CON) package reader and extractor.

    stfs_extract.py info    <package>
    stfs_extract.py list    <package>
    stfs_extract.py verify  <package>
    stfs_extract.py extract <package> <out_dir> [--no-verify] [--lowercase]

The package is opened read-only and never modified.  `verify` checks the
whole hash tree: every allocated data block against its level-0 record,
every level-0 table against its level-1 record, every level-1 table against
the level-2 record, and the top table against the volume descriptor's top
hash.  `extract` runs the same check over the blocks it reads (unless
--no-verify) and refuses to write a file whose blocks do not hash.
--lowercase writes every path in lower case (the 360's file system ignores
case, and the native engine lowercases every path it opens) and refuses a
package whose names collide once lowercased.

Layout notes (block numbers are "data block" numbers; real blocks interleave
hash tables).  Coordinates follow Onyx's STFS.hs and Velocity's StfsPackage:
real block 0 sits at offset 0xC000 for both header sizes; a LIVE/PIRS package
(header 0xAD0E) keeps one hash table per slot ("shift 0"), a CON (0x971A)
keeps two and the active one is chosen by the record status ("shift 1").

Exit codes: 0 ok, 1 usage, 2 not an STFS package, 3 hash verification failed.
"""

from __future__ import annotations

import argparse
import hashlib
import mmap
import os
import struct
import sys
from dataclasses import dataclass

BLOCK = 0x1000
PER_TABLE = 0xAA            # records per hash table
L1_SPAN = 0xAA * 0xAA       # 0x70E4 data blocks per level-1 table
STATUS_USED = 0x80


@dataclass
class Entry:
    index: int
    name: str
    consecutive: bool
    directory: bool
    blocks: int
    first_block: int
    path_index: int
    size: int


class StfsError(Exception):
    pass


class HashError(StfsError):
    pass


class Package:
    def __init__(self, path: str):
        self.path = path
        self._f = open(path, "rb")
        self.mm = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)
        magic = self.mm[0:4]
        if magic not in (b"LIVE", b"PIRS", b"CON "):
            raise StfsError(f"{path}: magic {magic!r} is not LIVE/PIRS/CON")
        self.magic = magic.decode()
        self.header_size = struct.unpack_from(">I", self.mm, 0x340)[0]
        self.content_type = struct.unpack_from(">I", self.mm, 0x344)[0]
        self.title_id = struct.unpack_from(">I", self.mm, 0x360)[0]
        desc_size = self.mm[0x379]
        if desc_size != 0x24:
            raise StfsError(f"volume descriptor size {desc_size:#x} (not STFS)")
        self.block_separation = self.mm[0x37B]
        self.ft_block_count = struct.unpack_from("<H", self.mm, 0x37C)[0]
        self.ft_first_block = int.from_bytes(self.mm[0x37E:0x381], "little")
        self.top_hash = bytes(self.mm[0x381:0x395])
        self.allocated = struct.unpack_from(">I", self.mm, 0x395)[0]
        self.unallocated = struct.unpack_from(">I", self.mm, 0x399)[0]
        self.display_name = self._utf16(0x411, 0x80)
        self.title_name = self._utf16(0x1691, 0x80)
        first_table = ((self.header_size + 0xFFF) & 0xF000) >> 12
        self.shift = 0 if first_table == 0xB else 1
        self.spacing = (0xAB, 0x718F) if self.shift == 0 else (0xAC, 0x723A)
        if self.allocated <= PER_TABLE:
            self.top_level = 0
        elif self.allocated <= L1_SPAN:
            self.top_level = 1
        else:
            self.top_level = 2
        self._entries: list[Entry] | None = None

    def _utf16(self, off: int, n: int) -> str:
        raw = bytes(self.mm[off:off + n])
        s = raw.decode("utf-16-be", errors="replace")
        return s.split("\x00", 1)[0]

    # -- block arithmetic (Onyx coordinates: real block r at 0xC000 + r*0x1000)
    def real_data_block(self, blk: int) -> int:
        s = self.shift
        r = blk
        if blk >= PER_TABLE:
            r += ((blk // PER_TABLE) + 1) << s
        if blk >= L1_SPAN:
            r += ((blk // L1_SPAN) + 1) << s
        return r

    def l0_table_block(self, group: int) -> int:
        """Real block of the level-0 table covering data blocks group*0xAA.."""
        blk = group * PER_TABLE
        s = self.shift
        t = group * self.spacing[0]
        if blk >= PER_TABLE:
            t += ((blk // L1_SPAN) + 1) << s
        if blk >= L1_SPAN:
            t += 1 << s
        return t - (1 << s)

    def l1_table_block(self, group: int) -> int:
        if group == 0:
            return self.spacing[0] - (1 << self.shift)
        return (1 << self.shift) + group * self.spacing[1] - (1 << self.shift)

    def l2_table_block(self) -> int:
        return self.spacing[1] - (1 << self.shift)

    def offset(self, real: int) -> int:
        return 0xC000 + real * BLOCK

    def block_bytes(self, real: int) -> bytes:
        off = self.offset(real)
        data = self.mm[off:off + BLOCK]
        if len(data) < BLOCK:
            data = data + b"\x00" * (BLOCK - len(data))
        return data

    def _record_at(self, table_real: int, index: int) -> tuple[bytes, int, int]:
        """(sha1, status, next) of a record; for shift 1 pick the active table."""
        def read(t: int):
            off = self.offset(t) + index * 0x18
            rec = self.mm[off:off + 0x18]
            if len(rec) < 0x18:
                rec = rec + b"\x00" * (0x18 - len(rec))
            return bytes(rec[:20]), rec[20], int.from_bytes(rec[21:24], "big")
        rec = read(table_real)
        if self.shift == 1 and rec[1] < STATUS_USED:
            rec = read(table_real + 1)
        return rec

    def data_record(self, blk: int) -> tuple[bytes, int, int]:
        return self._record_at(self.l0_table_block(blk // PER_TABLE), blk % PER_TABLE)

    def active_table(self, table_real: int, parent_sha: bytes) -> int:
        if self.shift == 0:
            return table_real
        for t in (table_real, table_real + 1):
            if hashlib.sha1(self.block_bytes(t)).digest() == parent_sha:
                return t
        return table_real

    # -- file table
    def entries(self) -> list[Entry]:
        if self._entries is not None:
            return self._entries
        raw = bytearray()
        blk = self.ft_first_block
        for _ in range(self.ft_block_count):
            raw += self.block_bytes(self.real_data_block(blk))
            blk = self.data_record(blk)[2]
        out = []
        for i in range(len(raw) // 0x40):
            e = raw[i * 0x40:(i + 1) * 0x40]
            if e[0] == 0:
                break
            flags = e[0x28]
            name = bytes(e[: flags & 0x3F]).decode("ascii", errors="replace")
            out.append(Entry(
                index=i,
                name=name,
                consecutive=bool(flags & 0x40),
                directory=bool(flags & 0x80),
                blocks=int.from_bytes(e[0x29:0x2C], "little"),
                first_block=int.from_bytes(e[0x2F:0x32], "little"),
                path_index=struct.unpack_from(">h", e, 0x32)[0],
                size=struct.unpack_from(">I", e, 0x34)[0],
            ))
        self._entries = out
        return out

    def path_of(self, e: Entry) -> str:
        parts = [e.name]
        ents = self.entries()
        seen = set()
        p = e.path_index
        while p != -1:
            if p in seen or p >= len(ents):
                raise StfsError(f"bad path index chain at entry {e.index}")
            seen.add(p)
            parts.append(ents[p].name)
            p = ents[p].path_index
        return "/".join(reversed(parts))

    def file_blocks(self, e: Entry) -> list[int]:
        n = (e.size + BLOCK - 1) // BLOCK
        if e.consecutive:
            return list(range(e.first_block, e.first_block + n))
        blocks = []
        blk = e.first_block
        for _ in range(n):
            if blk >= self.allocated:
                raise StfsError(f"{e.name}: block chain leaves the package ({blk})")
            blocks.append(blk)
            blk = self.data_record(blk)[2]
        return blocks

    # -- verification
    def verify_tree(self) -> list[str]:
        """Verify the top/L2/L1/L0 tables. Returns a list of failures."""
        bad = []
        n_l0 = (self.allocated + PER_TABLE - 1) // PER_TABLE
        n_l1 = (self.allocated + L1_SPAN - 1) // L1_SPAN
        if self.top_level == 0:
            t = self.active_table(self.l0_table_block(0), self.top_hash)
            if hashlib.sha1(self.block_bytes(t)).digest() != self.top_hash:
                bad.append("top (L0) table hash")
            return bad
        if self.top_level == 1:
            l1 = [self.active_table(self.l1_table_block(0), self.top_hash)]
            if hashlib.sha1(self.block_bytes(l1[0])).digest() != self.top_hash:
                bad.append("top (L1) table hash")
        else:
            l2 = self.active_table(self.l2_table_block(), self.top_hash)
            if hashlib.sha1(self.block_bytes(l2)).digest() != self.top_hash:
                bad.append("top (L2) table hash")
            l1 = []
            for j in range(n_l1):
                off = self.offset(l2) + j * 0x18
                sha = bytes(self.mm[off:off + 20])
                t = self.active_table(self.l1_table_block(j), sha)
                if hashlib.sha1(self.block_bytes(t)).digest() != sha:
                    bad.append(f"L1 table {j}")
                l1.append(t)
        self._l0_tables = []
        for i in range(n_l0):
            parent = l1[i // PER_TABLE]
            off = self.offset(parent) + (i % PER_TABLE) * 0x18
            sha = bytes(self.mm[off:off + 20])
            t = self.active_table(self.l0_table_block(i), sha)
            if hashlib.sha1(self.block_bytes(t)).digest() != sha:
                bad.append(f"L0 table {i}")
        return bad

    def verify_block(self, blk: int) -> bool:
        sha = self.data_record(blk)[0]
        return hashlib.sha1(self.block_bytes(self.real_data_block(blk))).digest() == sha

    def read_file(self, e: Entry, verify: bool = True) -> bytes:
        out = bytearray()
        left = e.size
        for blk in self.file_blocks(e):
            if verify and not self.verify_block(blk):
                raise HashError(f"{self.path_of(e)}: data block {blk} fails its SHA-1")
            data = self.block_bytes(self.real_data_block(blk))
            out += data[: min(BLOCK, left)]
            left -= BLOCK
        return bytes(out)


def cmd_info(p: Package) -> int:
    ents = p.entries()
    files = [e for e in ents if not e.directory]
    print(f"path            {p.path}")
    print(f"magic           {p.magic}")
    print(f"title id        {p.title_id:08X} ({p.title_name})")
    print(f"display name    {p.display_name}")
    print(f"content type    {p.content_type:#x}")
    print(f"header size     {p.header_size:#x} (hash tables per slot: {1 << p.shift})")
    print(f"blocks          {p.allocated} allocated, {p.unallocated} unallocated, top level L{p.top_level}")
    print(f"file table      {p.ft_block_count} blocks from block {p.ft_first_block}")
    print(f"entries         {len(ents)} ({len(files)} files, {len(ents) - len(files)} dirs)")
    print(f"file bytes      {sum(e.size for e in files)}")
    return 0


def cmd_list(p: Package) -> int:
    for e in p.entries():
        kind = "d" if e.directory else "f"
        print(f"{kind} {e.size:>10} {p.path_of(e)}")
    return 0


def cmd_verify(p: Package) -> int:
    bad = p.verify_tree()
    n_bad_data = 0
    n_data = 0
    for e in p.entries():
        if e.directory:
            continue
        for blk in p.file_blocks(e):
            n_data += 1
            if not p.verify_block(blk):
                n_bad_data += 1
                if n_bad_data <= 10:
                    bad.append(f"data block {blk} of {p.path_of(e)}")
    print(f"hash tree: {'OK' if not bad else 'FAILED'}; "
          f"{n_data} file data blocks checked, {n_bad_data} bad")
    for b in bad:
        print(f"  BAD {b}")
    return 0 if not bad else 3


def cmd_extract(p: Package, out_dir: str, verify: bool, lowercase: bool = False) -> int:
    if lowercase:
        seen: dict[str, str] = {}
        for e in p.entries():
            rel = p.path_of(e)
            if seen.get(rel.lower(), rel) != rel:
                raise StfsError(f"{rel!r} and {seen[rel.lower()]!r} collide in lower case")
            seen[rel.lower()] = rel
    if verify:
        bad = p.verify_tree()
        if bad:
            print("hash tree FAILED: " + ", ".join(bad[:10]), file=sys.stderr)
            return 3
    n_files = 0
    n_bytes = 0
    for e in p.entries():
        rel = p.path_of(e)
        if lowercase:
            rel = rel.lower()
        if rel.startswith("/") or ".." in rel.split("/"):
            raise StfsError(f"refusing unsafe path {rel!r}")
        dest = os.path.join(out_dir, rel)
        if e.directory:
            os.makedirs(dest, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        data = p.read_file(e, verify=verify)
        with open(dest, "wb") as f:
            f.write(data)
        n_files += 1
        n_bytes += len(data)
    print(f"extracted {n_files} files, {n_bytes} bytes to {out_dir}"
          f" ({'every block SHA-1 verified' if verify else 'UNVERIFIED'})")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", choices=["info", "list", "verify", "extract"])
    ap.add_argument("package")
    ap.add_argument("out_dir", nargs="?")
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--lowercase", action="store_true")
    a = ap.parse_args(argv)
    try:
        p = Package(a.package)
        if a.command == "info":
            return cmd_info(p)
        if a.command == "list":
            return cmd_list(p)
        if a.command == "verify":
            return cmd_verify(p)
        if not a.out_dir:
            ap.error("extract needs an out_dir")
        return cmd_extract(p, a.out_dir, not a.no_verify, a.lowercase)
    except HashError as exc:
        print(f"stfs: {exc}", file=sys.stderr)
        return 3
    except StfsError as exc:
        print(f"stfs: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
