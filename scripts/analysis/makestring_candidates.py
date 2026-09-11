#!/usr/bin/env python3
"""Compare per-function MakeString relocation multisets, base vs target.

Pairing follows objdiff.json's unit list (base_path <-> target_path), which is
exactly what objdiff itself pairs.  Names are resolved to ADDRESSES in
ham_xbox_r.map first, so two differently spelled members of the same ICF fold
group compare equal.  A function whose address-multiset differs between the
two sides carries a candidate site.
"""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path

MAP_RE = re.compile(
    r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+\S*\s*\S*\s*(\S*)"
)


def load_map(path: Path):
    addr = {}
    group = {}
    for line in path.read_text(errors="replace").splitlines():
        m = MAP_RE.match(line)
        if not m:
            continue
        name, a, obj = m.group(1), int(m.group(2), 16), m.group(3)
        addr.setdefault(name, a)
        group.setdefault(a, []).append((name, obj))
    return addr, group


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", default="/tmp/ms.json")
    ap.add_argument("--map", default="orig/373307D9/ham_xbox_r.map")
    ap.add_argument("--only-symbol", action="store_true")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    d = json.load(open(args.scan))
    addr, _group = load_map(Path(args.map))
    icf, _g2 = load_map(Path("build/373307D9/icf_aliases.map"))
    for n, a in icf.items():
        addr.setdefault(n, a)
    units = json.load(open("objdiff.json"))["units"]

    def key(n):
        return addr.get(n, "ABSENT:" + n)

    rows = []
    paired = 0
    for u in units:
        bp, tp = u.get("base_path"), u.get("target_path")
        if not bp or not tp:
            continue
        bfns = d["base"].get(bp, {})
        tfns = d["target"].get(tp, {})
        bdef = set(d["base_def"].get(bp, []))
        tdef = set(d["target_def"].get(tp, []))
        for fn in (set(bfns) | set(tfns)) & bdef & tdef:
            bc = Counter(bfns.get(fn, []))
            tc = Counter(tfns.get(fn, []))
            if not bc and not tc:
                continue
            paired += 1
            bk = Counter()
            for n, c in bc.items():
                bk[key(n)] += c
            tk = Counter()
            for n, c in tc.items():
                tk[key(n)] += c
            if bk == tk:
                continue
            if args.only_symbol and not any(
                "Symbol" in n for n in list(bc) + list(tc)
            ):
                continue
            rows.append(
                dict(
                    unit=u["name"],
                    fn=fn,
                    base_obj=bp,
                    tgt_obj=tp,
                    base_names=dict(bc),
                    tgt_names=dict(tc),
                    only_base=[
                        (k if isinstance(k, str) else hex(k), c)
                        for k, c in (bk - tk).items()
                    ],
                    only_tgt=[
                        (k if isinstance(k, str) else hex(k), c)
                        for k, c in (tk - bk).items()
                    ],
                    in_base_only=fn in bfns and fn not in tfns,
                    in_tgt_only=fn in tfns and fn not in bfns,
                )
            )
    rows.sort(key=lambda r: (r["unit"], r["fn"]))
    print(f"unit-paired fn slots with MakeString relocs: {paired}")
    print(f"differing: {len(rows)}")
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
