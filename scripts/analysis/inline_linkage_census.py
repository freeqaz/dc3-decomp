#!/usr/bin/env python3
"""Whole-binary census of "was this function defined `inline`?" -- ours vs the image.

Two independent ground truths meet here, and neither is a guess:

1. **The shipped linker map, `orig/373307D9/ham_xbox_r.map`.**  MSVC's map marks
   every code symbol `f`, and adds a second flag `i` when the symbol came from a
   *pick-any* COMDAT -- which is what the compiler emits for an `inline`
   (or template, or in-class) definition.  A function defined out-of-line in a
   `.cpp` and merely made COMDAT by `/Gy` is `f` with no `i`.  In `char:CharBones.obj`
   the split is visible in five consecutive lines: `ByteQuat::ToQuat`,
   `ByteQuat::Set`, `ShortVector3::ToShort`, `ShortVector3::Set` and
   `MakeShortAng` are all `f i`, while `CharBones::TypeOf` immediately below them
   is bare `f`.

2. **Our own COFF objects.**  The COMDAT selection byte lives in the aux record of
   the *section* symbol: `IMAGE_COMDAT_SELECT_ANY` (2) for an inline/template
   definition, `IMAGE_COMDAT_SELECT_NODUPLICATES` (1) for a `/Gy` out-of-line one.
   Same distinction, read from the compiler rather than the linker.

So `map=i, ours=NODUPLICATES` means *retail defined this inline (in a header) and
we define it out-of-line in a .cpp*, and the reverse means the opposite.  That is
task #111's family, enumerated rather than sampled.

Usage:
    python3 scripts/analysis/inline_linkage_census.py [--json OUT] [--only-charged]
"""
import argparse
import collections
import glob
import json
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SEL_NODUPLICATES = 1
SEL_ANY = 2

# " 0005:00094d08       ?MakeShortAng@@YAFM@Z      823c4d08 f i char:CharBones.obj"
MAP_RE = re.compile(
    r'^\s+[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+(f|f i|\s*)\s+(\S+)\s*$')


def read_map(path):
    """-> {symbol: (address, is_inline_comdat, contributing_obj)} for code symbols."""
    out = {}
    for line in open(path, 'r', errors='replace'):
        m = MAP_RE.match(line.rstrip('\n'))
        if not m:
            continue
        name, addr, flags, obj = m.groups()
        flags = flags.strip()
        if not flags.startswith('f'):
            continue  # data symbol
        out[name] = (int(addr, 16), flags == 'f i', obj)
    return out


def obj_comdat_selection(path):
    """-> {func_symbol: selection byte} for symbols in code sections of one object."""
    d = open(path, 'rb').read()
    machine, nsec, ts, symptr, nsym, optsz, chars = struct.unpack_from('<HHIIIHH', d, 0)
    secs = []
    off = 20 + optsz
    for k in range(nsec):
        raw = d[off:off + 40]
        vsz, va, szraw, ptrraw, ptrrel, ptrln, nrel, nln, flags = struct.unpack_from('<IIIIIIHHI', raw, 8)
        secs.append(dict(idx=k + 1, name=raw[0:8].rstrip(b'\0').decode('latin-1'), flags=flags))
        off += 40
    strtab_off = symptr + nsym * 18
    stl = struct.unpack_from('<I', d, strtab_off)[0]
    strtab = d[strtab_off:strtab_off + stl]
    sel = {}
    funcs = []
    i = 0
    while i < nsym:
        rec = d[symptr + i * 18:symptr + i * 18 + 18]
        if rec[0:4] == b'\0\0\0\0':
            so = struct.unpack_from('<I', rec, 4)[0]
            e = strtab.index(b'\0', so)
            name = strtab[so:e].decode('latin-1')
        else:
            name = rec[0:8].rstrip(b'\0').decode('latin-1')
        value, secnum, typ, sclass, naux = struct.unpack_from('<IhHBB', rec, 8)
        if sclass == 3 and naux >= 1 and 1 <= secnum <= len(secs) and name == secs[secnum - 1]['name']:
            aux = d[symptr + (i + 1) * 18:symptr + (i + 1) * 18 + 18]
            sel[secnum] = aux[14]
        elif sclass in (2, 3) and 1 <= secnum <= len(secs) and (secs[secnum - 1]['flags'] & 0x20):
            if not name.startswith('??_C@'):
                funcs.append((name, secnum))
        i += 1 + naux
    return {n: sel.get(s) for n, s in funcs}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=REPO)
    ap.add_argument('--json')
    ap.add_argument('--limit', type=int, default=60)
    args = ap.parse_args()

    mp = read_map(os.path.join(args.repo, 'orig/373307D9/ham_xbox_r.map'))
    sys.stderr.write('map: %d code symbols, %d marked inline-COMDAT\n'
                     % (len(mp), sum(1 for v in mp.values() if v[1])))

    ours = {}  # symbol -> set of selection bytes seen across our objects
    where = collections.defaultdict(set)
    nobj = 0
    for p in sorted(glob.glob(os.path.join(args.repo, 'build/373307D9/src/**/*.obj'), recursive=True)):
        try:
            if os.path.getsize(p) == 0:
                continue
            sels = obj_comdat_selection(p)
        except Exception:
            continue
        nobj += 1
        for n, s in sels.items():
            if s is None:
                continue
            ours.setdefault(n, set()).add(s)
            if s == SEL_NODUPLICATES:
                where[n].add(os.path.relpath(p, args.repo))
    sys.stderr.write('ours: %d objects, %d function symbols with a COMDAT selection\n'
                     % (nobj, len(ours)))

    # report.json scores, for cost
    rep = json.load(open(os.path.join(args.repo, 'build/373307D9/report.json')))
    score = {}
    for u in rep['units']:
        for f in u.get('functions', []):
            n = f.get('name')
            if n:
                score.setdefault(n, []).append(
                    (u["name"], f.get("match_percent_normalized"), int(f.get("size") or 0)))

    rows = []
    for n, (addr, map_inline, obj) in mp.items():
        s = ours.get(n)
        if not s:
            continue
        ours_inline = (SEL_ANY in s)
        ours_outofline = (SEL_NODUPLICATES in s)
        if map_inline and ours_outofline and not ours_inline:
            klass = 'RETAIL_INLINE_OURS_OUTOFLINE'
        elif (not map_inline) and ours_inline and not ours_outofline:
            klass = 'RETAIL_OUTOFLINE_OURS_INLINE'
        else:
            continue
        rows.append(dict(symbol=n, address='0x%08x' % addr, map_obj=obj, klass=klass,
                         defined_in=sorted(where.get(n, [])),
                         scores=score.get(n, [])))

    counts = collections.Counter(r['klass'] for r in rows)
    print('disagreements: %d' % len(rows))
    for k, v in counts.most_common():
        print('  %-32s %d' % (k, v))

    def cost(r):
        below = [t for t in r['scores'] if t[1] is not None and t[1] < 100.0]
        return sum(t[2] or 0 for t in below), below

    print()
    for k in ('RETAIL_INLINE_OURS_OUTOFLINE', 'RETAIL_OUTOFLINE_OURS_INLINE'):
        sel = [r for r in rows if r['klass'] == k]
        scored = [(cost(r)[0], r) for r in sel]
        scored.sort(key=lambda t: -t[0])
        tot = sum(c for c, _ in scored)
        print('== %s: %d symbols, %d bytes currently sub-100%%' % (k, len(sel), tot))
        for c, r in scored[:args.limit]:
            if c == 0:
                continue
            print('  %7d B  %s' % (c, r['symbol'][:100]))
            print('             map_obj=%s ours=%s' % (r['map_obj'], ','.join(r['defined_in'])))
            for un, pc, sz in cost(r)[1]:
                print('             %-44s %8.4f  %d B' % (un, pc, sz))
        print()

    if args.json:
        json.dump(rows, open(args.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
