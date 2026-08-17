#!/usr/bin/env python3
"""Census of MSVC local-static LEXICAL SCOPE INDICES: target vs our objects.

dc3-decomp (title 373307D9).  A function-local static mangles as

    ?<name>@?<scope>??<enclosing function>@4<type>A

where <scope> is an MSVC number (digit d encodes d+1; otherwise a base-16
string over A..P terminated by '@', so '?BA@' is 0x10 = 16).  That number is a
per-function counter of SCOPES OPENED so far at the point of declaration, so it
is a fingerprint of the enclosing function's block structure.  When ours differs
from the target's, our source has a different number of lexical scopes before
that declaration -- and under the graded ruler
(functionRelocDiffs=name_check) every relocation naming that static is charged.

Cost of each construct, measured against the shipping compiler
(build/compilers/X360/16.00.11886.00/cl.exe, /O1 /Oi /EHsc):

    if (c) stmt;                 2      switch (x) { ... }       2
    if (c) { stmt; }             3      while/for/do + braces    2
    else stmt;                   +1     bare block { ... }       1
    else { stmt; }               +2     ternary, &&, ||          0
    MILO_ASSERT(c, line)         5      (do 1 + block 1 + if 2 + block 1)

A function's first construct starts at 2.  Statics declared in the same scope
share one index, and a static declared after an inner block keeps that block's
number (the counter never goes back down).

Usage:
    python3 scripts/analysis/scope_index_census.py            # whole build
    python3 scripts/analysis/scope_index_census.py --json /tmp/skew.json
"""
import argparse
import collections
import glob
import json
import os
import re
import subprocess
import sys

NAME = re.compile(r'^\?([^@?]+)@\?([0-9]|[A-P]+@)(\?\?.*)$')


def decode(tok):
    """MSVC number: '4' -> 5, 'BA@' -> 16."""
    if len(tok) == 1 and tok.isdigit():
        return int(tok) + 1
    v = 0
    for ch in tok[:-1]:
        v = v * 16 + (ord(ch) - 65)
    return v


def parse(sym):
    m = NAME.match(sym)
    if not m:
        return None
    return m.group(1), decode(m.group(2)), m.group(3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default=os.getcwd())
    ap.add_argument('--symbols', default=None)
    ap.add_argument('--objects', default=None)
    ap.add_argument('--json', default=None)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    syms = a.symbols or os.path.join(a.project, 'config/373307D9/symbols.txt')
    objdir = a.objects or os.path.join(a.project, 'build/373307D9/src')

    tgt = collections.defaultdict(dict)
    for ln in open(syms):
        r = parse(ln.split(' =')[0])
        if r:
            tgt[r[2]][r[0]] = r[1]

    our = collections.defaultdict(dict)
    for o in glob.glob(os.path.join(objdir, '**', '*.obj'), recursive=True):
        out = subprocess.run(['strings', '-a', o], capture_output=True, text=True).stdout
        for ln in out.splitlines():
            r = parse(ln.strip())
            if r:
                our[r[2]][r[0]] = r[1]

    match = missing = 0
    rows = []
    for fn, names in tgt.items():
        if fn not in our:
            missing += 1
            continue
        on = our[fn]
        bad = [(n, v, on.get(n)) for n, v in sorted(names.items()) if on.get(n) != v]
        if bad:
            rows.append((fn, bad))
        else:
            match += 1
    print(f"enclosing functions: match={match} diff={len(rows)} target-only={missing}")
    if not a.quiet:
        for fn, bad in rows:
            deltas = sorted({(o - t) for _, t, o in bad if o is not None})
            print(f"\n{fn}   delta={deltas}")
            for n, t, o in bad:
                print(f"   {n:44} tgt={t} ours={o}")
    if a.json:
        json.dump({'tgt': tgt, 'our': our}, open(a.json, 'w'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
