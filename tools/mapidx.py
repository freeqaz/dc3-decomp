#!/usr/bin/env python3
"""Index the shipped MSVC linker map: name -> [(addr, obj)] and addr -> [names].

The linker map is the only oracle here that lists FOLDED-AWAY spellings.
target_symbol_map.json is a VA->name FUNCTION -- one name per address -- so a
name that lost the fold vote is simply absent from it, and "both names mapped at
different addresses" is not evidence of anything.  ham_xbox_r.map has one line
per symbol, with the shared address printed for every member of a fold set.
"""
import re, sys, json
from collections import defaultdict

LINE = re.compile(r"^\s([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+(\S+)\s+([0-9A-Fa-f]{8})\s+(\S.*)?$")

def load(path):
    n2a, a2n = defaultdict(list), defaultdict(list)
    for ln in open(path, errors="replace"):
        m = LINE.match(ln.rstrip("\n"))
        if not m:
            continue
        sec, off, name, va, rest = m.groups()
        va = int(va, 16)
        obj = (rest or "").split()[-1] if rest else ""
        n2a[name].append((va, obj))
        a2n[va].append(name)
    return n2a, a2n

if __name__ == "__main__":
    n2a, a2n = load(sys.argv[1])
    print(f"{len(n2a)} names, {len(a2n)} addresses, "
          f"{sum(1 for v in a2n.values() if len(v) > 1)} shared addresses")
