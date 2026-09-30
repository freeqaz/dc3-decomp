#!/usr/bin/env python3
"""Compare our objects' COMDAT SELECTION against ham_xbox_r.map's f / f i column.

Why a selection byte and not the `inline` keyword:  on this toolchain EVERY
function we compile lands in its own COMDAT .text section (function-level
linking), so "is it a COMDAT" does not discriminate and cannot be matched by
adding `inline`.  What the map's column actually reports is the COMDAT
*selection type*, which lives in the section symbol's aux record:

    map `f i`   <->  IMAGE_COMDAT_SELECT_ANY           (inline / template / in-class)
    map bare `f` <-> IMAGE_COMDAT_SELECT_NODUPLICATES  (ordinary out-of-line definition)

The dtk-carved TARGET objects record no selection at all (section `/50`,
Selection byte absent), so ham_xbox_r.map is the only carrier of the image's
linkage class -- there is nothing to diff object-to-object.

A mismatch is actionable: `inline` on the definition moves NODUPLICATES -> ANY.
It is NOT automatically a match win -- see the measured refutations in
src/system/world/SpotlightDrawer.h.

  python3 scripts/analysis/comdat_selection_audit.py                 # whole binary
  python3 scripts/analysis/comdat_selection_audit.py <obj> [<obj>..] # named objects
"""
import glob
import os
import re
import struct
import sys

SEL = {1: "NODUP", 2: "ANY", 3: "SAME_SIZE", 4: "EXACT", 5: "ASSOC", 6: "LARGEST"}
MAP = "orig/373307D9/ham_xbox_r.map"
# "<sect:off> <name> <addr> f[ i] <contributor>"; the optional `i` is group 3.
MAP_RE = re.compile(r"^\s*\S+\s+(\S+)\s+([0-9a-f]{8})\s+f(\s+i)?\s")


def load_map(path):
    """name -> expected selection.  First definition wins (ICF lists aliases)."""
    out = {}
    with open(path, errors="replace") as fh:
        for line in fh:
            m = MAP_RE.match(line)
            if m:
                out.setdefault(m.group(1), "ANY" if m.group(3) else "NODUP")
    return out


def text_function_selections(path):
    """name -> selection, for EXTERNAL symbols defined in a .text section."""
    data = open(path, "rb").read()
    if len(data) < 20:
        return {}
    _mach, nsec, _ts, psym, nsym, ohdr, _ch = struct.unpack_from("<HHIIIHH", data, 0)
    if not psym or not nsym:
        return {}
    secs = []
    base = 20 + ohdr
    for i in range(nsec):
        name, _vs, _va, _sz, _pr, _prl, _pln, _nr, _nl, chars = struct.unpack_from(
            "<8sIIIIIIHHI", data, base + i * 40
        )
        secs.append([name.rstrip(b"\0").decode("latin1"), chars, None])
    strtab = psym + nsym * 18

    def symbol_name(rec):
        if rec[0:4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", rec, 4)[0]
            end = data.index(b"\0", strtab + off)
            return data[strtab + off : end].decode("latin1")
        return rec[0:8].rstrip(b"\0").decode("latin1")

    entries = []
    i = 0
    while i < nsym:
        rec = data[psym + i * 18 : psym + i * 18 + 18]
        _val, secnum, _typ, sclass, naux = struct.unpack_from("<IhHBB", rec, 8)
        name = symbol_name(rec)
        # A section definition carries the Selection byte at aux offset 14.
        if sclass == 3 and naux >= 1 and 0 < secnum <= nsec and name == secs[secnum - 1][0]:
            secs[secnum - 1][2] = data[psym + (i + 1) * 18 + 14]
        entries.append((name, secnum, sclass))
        i += 1 + naux

    out = {}
    for name, secnum, sclass in entries:
        if sclass == 2 and 0 < secnum <= nsec and secs[secnum - 1][0].startswith(".text"):
            out[name] = SEL.get(secs[secnum - 1][2], str(secs[secnum - 1][2]))
    return out


def main(argv):
    repo = os.environ.get("REPO_ROOT") or os.getcwd()
    os.chdir(repo)
    if not os.path.exists(MAP):
        print(f"ERROR: {MAP} not found; run from the repo root", file=sys.stderr)
        return 2
    link = load_map(MAP)
    objs = argv[1:] or sorted(glob.glob("build/373307D9/src/**/*.obj", recursive=True))
    if not objs:
        print("ERROR: no objects; run a full ninja first", file=sys.stderr)
        return 2
    compared = 0
    rows = []
    unknown = 0
    for path in objs:
        try:
            sels = text_function_selections(path)
        except Exception as exc:  # a malformed object must not read as "no mismatches"
            print(f"WARN: could not parse {path}: {exc}", file=sys.stderr)
            continue
        for name, sel in sels.items():
            want = link.get(name)
            if want is None:
                unknown += 1
                continue
            compared += 1
            if want != sel:
                rows.append((os.path.basename(path), sel, want, name))
    print(f"objects examined      : {len(objs)}")
    print(f"functions compared    : {compared}")
    print(f"not in map (skipped)  : {unknown}")
    print(f"SELECTION MISMATCHES  : {len(rows)}")
    ours_nodup = sum(1 for r in rows if r[1] == "NODUP")
    print(f"  ours NODUP / image ANY : {ours_nodup}   <- add `inline` to match")
    print(f"  ours ANY / image NODUP : {len(rows) - ours_nodup}   <- we over-inlined")
    for obj, sel, want, name in sorted(rows):
        print(f"  ours={sel:6s} image={want:6s} {obj:28s} {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
