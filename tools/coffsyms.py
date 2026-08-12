#!/usr/bin/env python3
"""List the symbol names in a COFF object.  stdlib only, no objdump needed.

    coffsyms.py <obj> [substring-filter ...]
"""
import struct
import sys


def symbols(path):
    data = open(path, "rb").read()
    psym, nsym = struct.unpack_from("<II", data, 8)
    strt = psym + nsym * 18
    out, i = [], 0
    while i < nsym:
        rec = data[psym + i * 18: psym + i * 18 + 18]
        if rec[:4] == b"\0\0\0\0":
            off, = struct.unpack_from("<I", rec, 4)
            end = data.index(b"\0", strt + off)
            name = data[strt + off:end].decode("latin1")
        else:
            name = rec[:8].rstrip(b"\0").decode("latin1")
        out.append(name)
        i += 1 + rec[17]
    return out


if __name__ == "__main__":
    pats = sys.argv[2:]
    for n in sorted(set(symbols(sys.argv[1]))):
        if not pats or any(p in n for p in pats):
            print(n)
