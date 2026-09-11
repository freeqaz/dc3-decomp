#!/usr/bin/env python3
"""Per-function CALL-target census, base vs target, straight from COFF.

Generalisation of makestring_reloc_scan.py.  For every paired unit in
objdiff.json we take every PPC REL24 relocation (type 6 -- a `bl`) in each
function defined on BOTH sides, resolve the target symbol name to an ADDRESS
(ham_xbox_r.map, then icf_aliases.map, so ICF-folded spellings compare equal)
and compare the two multisets.

This is deliberately independent of objdiff's instruction pairing: a wrong
callee inside a large insert/delete cluster is invisible to the pattern census
but shows up here.

  --same-arity  keep only functions whose two sides make the SAME NUMBER of
                calls, so a row means a SUBSTITUTION rather than a missing or
                extra call.  Much higher precision.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

SYM_ENT = 18
REL24 = 6

MAP_RE = re.compile(
    r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+\S*\s*\S*\s*(\S*)"
)


def load_map(path: Path):
    addr = {}
    for line in path.read_text(errors="replace").splitlines():
        m = MAP_RE.match(line)
        if m:
            addr.setdefault(m.group(1), int(m.group(2), 16))
    return addr


def parse_obj(path: Path):
    data = path.read_bytes()
    if len(data) < 20:
        return None
    _mach, nsec, _ts, symptr, nsym, opthdr, _ch = struct.unpack_from("<HHIIIHH", data, 0)
    if not symptr or not nsym:
        return None
    sec_off = 20 + opthdr
    sections = []
    for i in range(nsec):
        b = sec_off + i * 40
        if b + 40 > len(data):
            return None
        nm = data[b : b + 8].rstrip(b"\0").decode("latin1")
        (_vs, _va, _rs, _rp, relp, _lp, nrel, _nl, chars) = struct.unpack_from(
            "<IIIIIIHHI", data, b + 8
        )
        sections.append(dict(name=nm, relptr=relp, nrel=nrel, chars=chars))

    strtab = data[symptr + nsym * SYM_ENT :]

    def sname(raw: bytes) -> str:
        if raw[0:4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", raw, 4)[0]
            return strtab[off : strtab.find(b"\0", off)].decode("latin1")
        return raw.rstrip(b"\0").decode("latin1")

    symbols = []
    i = 0
    while i < nsym:
        b = symptr + i * SYM_ENT
        value, secnum, styp, sclass, naux = struct.unpack_from("<IhHBB", data, b + 8)
        symbols.append(
            dict(name=sname(data[b : b + 8]), value=value, sec=secnum, typ=styp, cls=sclass)
        )
        symbols.extend([None] * naux)
        i += 1 + naux

    defined = defaultdict(list)
    for s in symbols:
        if s is None:
            continue
        # typ 0x20 == DT_FUNCTION; excludes the section symbol, which shares
        # value 0 and storage class 3 with a file-static function.
        if s["sec"] > 0 and s["typ"] == 0x20 and s["cls"] in (2, 3, 105) and s["name"]:
            defined[s["sec"]].append(s)
    for v in defined.values():
        v.sort(key=lambda s: s["value"])

    def owner(secnum, off):
        best = None
        for s in defined.get(secnum, []):
            if s["value"] <= off:
                best = s
            else:
                break
        return best["name"] if best else None

    calls = defaultdict(list)
    for si, sec in enumerate(sections, start=1):
        if not sec["nrel"] or not (sec["chars"] & 0x20):
            continue
        for r in range(sec["nrel"]):
            b = sec["relptr"] + r * 10
            if b + 10 > len(data):
                break
            vaddr, symidx, rtyp = struct.unpack_from("<IIH", data, b)
            if rtyp != REL24:
                continue
            if symidx >= len(symbols) or symbols[symidx] is None:
                continue
            fn = owner(si, vaddr)
            if fn:
                calls[fn].append(symbols[symidx]["name"])
    defnames = {
        s["name"] for lst in defined.values() for s in lst
    }
    return calls, defnames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--same-arity", action="store_true")
    ap.add_argument("--grep", help="only rows where some callee name matches this")
    ap.add_argument("--json-out")
    args = ap.parse_args()
    root = Path(args.root)

    addr = load_map(root / "orig/373307D9/ham_xbox_r.map")
    for n, a in load_map(root / "build/373307D9/icf_aliases.map").items():
        addr.setdefault(n, a)

    cache = {}

    def get(rel):
        if rel not in cache:
            p = root / rel
            cache[rel] = parse_obj(p) if p.exists() else None
        return cache[rel]

    def key(n):
        return addr.get(n, "ABSENT:" + n)

    units = json.load(open(root / "objdiff.json"))["units"]
    rows = []
    paired = 0
    for u in units:
        bp, tp = u.get("base_path"), u.get("target_path")
        if not bp or not tp:
            continue
        b, t = get(bp), get(tp)
        if not b or not t:
            continue
        bcalls, bdef = b
        tcalls, tdef = t
        for fn in (set(bcalls) | set(tcalls)) & bdef & tdef:
            bl, tl = bcalls.get(fn, []), tcalls.get(fn, [])
            paired += 1
            if args.same_arity and len(bl) != len(tl):
                continue
            bk = Counter(key(n) for n in bl)
            tk = Counter(key(n) for n in tl)
            if bk == tk:
                continue
            ob, ot = bk - tk, tk - bk
            if args.grep and not any(
                re.search(args.grep, n) for n in list(bl) + list(tl)
            ):
                continue
            rows.append(
                dict(
                    unit=u["name"],
                    fn=fn,
                    n_base_calls=len(bl),
                    n_tgt_calls=len(tl),
                    only_base=[n for n in bl if key(n) in ob],
                    only_tgt=[n for n in tl if key(n) in ot],
                )
            )
    rows.sort(key=lambda r: (len(r["only_base"]) + len(r["only_tgt"]), r["unit"]))
    print(f"paired function slots with calls: {paired}")
    print(f"differing: {len(rows)}")
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
