#!/usr/bin/env python3
"""List the 4/8-byte `.data` float statics our build emits, per object.

Companion to `data_float_labels.py`, which lists the ones the TARGET has.
A `.data` float in the target (dtk names it `lbl_8xxxxxxx`) is a MUTABLE
`static float`; if our object has no `.data` float at all for that unit, our
source spelled the constant as an inline literal (which lands in `.rdata` as a
`__real@<hex>` COMDAT) and we have therefore *guessed* both the storage class
and the value.  objdiff cannot see the difference: the load instruction is
byte-identical either way and `lbl_*` relocation names are exempt from the
graded ruler, so this class is invisible to match%.

Usage:
    python3 scripts/analysis/coff_data_floats.py build/373307D9/src/**/X.obj
    python3 scripts/analysis/coff_data_floats.py --unit system/hamobj/PoseFatalities
"""
from __future__ import annotations

import argparse
import glob
import os
import struct
import sys

# RTTI / vtable / EH data that lives in .data but is never a float constant.
NOISE_PREFIXES = ("??_R", "??_7", "??_8", "__unwind$", "__catch$", "$T", "$M")


def read_coff(path: str):
    b = open(path, "rb").read()
    _mach, nsec, _ts, symptr, nsym, optsz, _ch = struct.unpack_from("<HHIIIHH", b, 0)
    secs = []
    off = 20 + optsz
    for i in range(nsec):
        raw = b[off + 40 * i : off + 40 * i + 40]
        name = raw[:8].rstrip(b"\0").decode("latin1")
        _vsz, _va, sz, ptr, _pr, _pl, _nr, _nl, chars = struct.unpack_from(
            "<IIIIIIHHI", raw, 8
        )
        secs.append((name, sz, ptr, chars))
    strtab = symptr + 18 * nsym
    syms = []
    i = 0
    while i < nsym:
        rec = b[symptr + 18 * i : symptr + 18 * i + 18]
        nm = rec[:8]
        if nm[:4] == b"\0\0\0\0":
            o = struct.unpack_from("<I", nm, 4)[0]
            e = b.index(b"\0", strtab + o)
            name = b[strtab + o : e].decode("latin1")
        else:
            name = nm.rstrip(b"\0").decode("latin1")
        val, secnum, _typ, sc, naux = struct.unpack_from("<IhHBB", rec, 8)
        syms.append((name, val, secnum, sc, naux))
        i += 1 + naux
    return secs, syms, b


def data_floats(path: str):
    """Yield (symbol, section, size, value) for small .data objects."""
    secs, syms, b = read_coff(path)
    # Which sections have relocations?  A float constant never does.
    out = []
    for name, val, secnum, sc, _naux in syms:
        if not (1 <= secnum <= len(secs)):
            continue
        sname, ssz, sptr, _chars = secs[secnum - 1]
        if not sname.startswith(".data"):
            continue
        if name.startswith(NOISE_PREFIXES):
            continue
        if name == sname:
            continue  # the section symbol itself, not the datum
        if ssz not in (4, 8) or sc not in (2, 3):
            continue
        raw = b[sptr + val : sptr + val + ssz]
        if len(raw) != ssz:
            continue
        v = struct.unpack(">f" if ssz == 4 else ">d", raw)[0]
        out.append((name, sname, ssz, v))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("objs", nargs="*")
    ap.add_argument("--unit", action="append", default=[])
    ap.add_argument("--build-root", default="build/373307D9/src")
    args = ap.parse_args()

    paths = list(args.objs)
    for u in args.unit:
        paths.extend(glob.glob(os.path.join(args.build_root, u + ".obj")))
    if not paths:
        print("no objects given", file=sys.stderr)
        return 1

    for p in sorted(paths):
        fl = data_floats(p)
        print(f"{p}: {len(fl)} .data float statics")
        for name, sname, sz, v in fl:
            print(f"    {sname:10} {sz}  {v!r:24} {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
