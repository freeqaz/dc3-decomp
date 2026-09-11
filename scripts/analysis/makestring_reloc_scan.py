#!/usr/bin/env python3
"""Enumerate MakeString<...> relocation sites per function, both sides.

The .obj files are Xbox 360 COFF (machine 0x1f2); llvm will not parse them, so
this reads the COFF structures directly.  For each object we produce, per
defining function symbol, the multiset of MakeString instantiation names it
references through a relocation.

Usage:
  python3 scripts/analysis/makestring_reloc_scan.py --json-out /tmp/ms.json
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

SYM_ENT = 18


def parse_obj(path: Path):
    data = path.read_bytes()
    if len(data) < 20:
        return None
    machine, nsec, _ts, symptr, nsym, opthdr, _chars = struct.unpack_from(
        "<HHIIIHH", data, 0
    )
    if symptr == 0 or nsym == 0:
        return None
    sec_off = 20 + opthdr
    sections = []
    for i in range(nsec):
        base = sec_off + i * 40
        if base + 40 > len(data):
            return None
        name = data[base : base + 8].rstrip(b"\0").decode("latin1")
        (vsize, vaddr, rawsize, rawptr, relptr, _lnptr, nrel, _nln, chars) = (
            struct.unpack_from("<IIIIIIHHI", data, base + 8)
        )
        sections.append(
            dict(name=name, relptr=relptr, nrel=nrel, chars=chars, rawsize=rawsize)
        )

    strtab_off = symptr + nsym * SYM_ENT
    strtab = data[strtab_off:] if strtab_off < len(data) else b""

    def sname(raw: bytes) -> str:
        if raw[0:4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", raw, 4)[0]
            end = strtab.find(b"\0", off)
            return strtab[off:end].decode("latin1")
        return raw.rstrip(b"\0").decode("latin1")

    symbols = []  # index -> dict
    i = 0
    while i < nsym:
        base = symptr + i * SYM_ENT
        raw = data[base : base + 8]
        value, secnum, styp, sclass, naux = struct.unpack_from("<IhHBB", data, base + 8)
        symbols.append(
            dict(
                name=sname(raw),
                value=value,
                sec=secnum,
                typ=styp,
                cls=sclass,
                idx=i,
            )
        )
        for k in range(naux):
            symbols.append(None)
        i += 1 + naux

    # defined function-ish symbols per section, sorted by value
    defined = defaultdict(list)
    for s in symbols:
        if s is None:
            continue
        # typ 0x20 == DT_FUNCTION.  This is the discriminator that keeps the
        # SECTION symbol (typ 0, cls 3, value 0) from shadowing a file-static
        # function, which shares its value and its storage class.
        if s["sec"] > 0 and s["typ"] == 0x20 and s["cls"] in (2, 3, 105) and s["name"]:
            defined[s["sec"]].append(s)
    for v in defined.values():
        v.sort(key=lambda s: (s["value"], 0 if s["cls"] == 2 else 1))

    def owner(secnum: int, off: int) -> str:
        cands = defined.get(secnum, [])
        best = None
        for s in cands:
            if s["value"] <= off:
                best = s
            else:
                break
        if best is not None:
            return best["name"]
        return f"<{sections[secnum-1]['name']}#{secnum}>"

    out = defaultdict(list)  # fn name -> [target sym name]
    for si, sec in enumerate(sections, start=1):
        if not sec["nrel"] or not (sec["chars"] & 0x20):  # CNT_CODE
            continue
        rp = sec["relptr"]
        for r in range(sec["nrel"]):
            base = rp + r * 10
            if base + 10 > len(data):
                break
            vaddr, symidx, rtyp = struct.unpack_from("<IIH", data, base)
            if symidx >= len(symbols) or symbols[symidx] is None:
                continue
            tgt = symbols[symidx]["name"]
            if "MakeString" not in tgt:
                continue
            out[owner(si, vaddr)].append(tgt)
    defnames = sorted(
        {
            s["name"]
            for lst in defined.values()
            for s in lst
            if sections[s["sec"] - 1]["chars"] & 0x20
        }
    )
    return out, defnames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--json-out")
    args = ap.parse_args()
    root = Path(args.root)

    result = {"base": {}, "target": {}, "base_def": {}, "target_def": {}}
    for side, globpat in (
        ("base", "build/373307D9/src/**/*.obj"),
        ("target", "build/373307D9/obj/**/*.obj"),
    ):
        for obj in sorted(root.glob(globpat)):
            try:
                r = parse_obj(obj)
            except Exception as e:  # noqa
                print(f"ERR {obj}: {e}", file=sys.stderr)
                continue
            if not r:
                continue
            relocs, defnames = r
            rel = str(obj.relative_to(root))
            result[side][rel] = {k: v for k, v in relocs.items() if v}
            result[side + "_def"][rel] = defnames
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(result, indent=1))
    nb = sum(len(v) for v in result["base"].values())
    nt = sum(len(v) for v in result["target"].values())
    print(f"base: {len(result['base'])} objs, {nb} fns with MakeString relocs")
    print(f"target: {len(result['target'])} objs, {nt} fns with MakeString relocs")


if __name__ == "__main__":
    main()
