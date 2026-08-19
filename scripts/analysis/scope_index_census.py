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
# Atexit destructor helper for a function-local static:
#   ??__F<name>@?<scope>??<enclosing function>@YAXXZ
# Its scope counter is the same number the data symbol carries, and dtk names
# ALL of them, whereas it leaves most of the .data objects as bare `lbl_<addr>`.
# So on the TARGET side this is the only complete enumeration of a function's
# statics.  It is useless on OUR side: scripts/obj_atexit_scope_patcher.py
# rewrites these names in build/373307D9/src/**.obj to whatever the target says,
# precisely so objdiff can pair the bodies.  Never read our indices from them.
ATEXIT = re.compile(r'^\?\?__F([^@?]+)@\?([0-9]|[A-P]+@)(\?\?.*)@YAXXZ$')


def decode(tok):
    """MSVC number: '4' -> 5, 'BA@' -> 16."""
    if len(tok) == 1 and tok.isdigit():
        return int(tok) + 1
    v = 0
    for ch in tok[:-1]:
        v = v * 16 + (ord(ch) - 65)
    return v


def parse(sym, rx=NAME):
    m = rx.match(sym)
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

    # name -> SORTED LIST of scope indices.  A function may declare several
    # statics that share a name -- every MILO_NOTIFY_ONCE in a function declares
    # a `_dw`, every static Message a `msg`.  Keying name -> single int silently
    # kept whichever one was parsed last on each side and then compared two
    # unrelated statics, which is how RndTexBlender::DrawShowing (5 `_dw` ours,
    # 3 target) came out as a one-line "tgt=15 ours=9" row.
    tgt = collections.defaultdict(lambda: collections.defaultdict(list))
    atexits = []
    for ln in open(syms):
        sym = ln.split(' =')[0]
        r = parse(sym)
        if r:
            bucket = tgt[r[2]][r[0]]
            if r[1] not in bucket:
                bucket.append(r[1])
            continue
        r = parse(sym, ATEXIT)
        if r:
            atexits.append(r)

    # Fold the atexit helpers into the buckets their data symbol already opened.
    # The data key carries the static's type suffix (`...@Z@4VDebugNotifyOncer@@A`)
    # and the atexit key does not, so match on prefix.  Deliberately do NOT open
    # a new function key from an atexit alone: our side has no atexit evidence to
    # compare it against (the patcher rewrote those names), so it would only
    # inflate `target-only`.
    for name, idx, fn in atexits:
        for k in tgt:
            if k == fn or k.startswith(fn + '@'):
                bucket = tgt[k].get(name)
                if bucket is not None and idx not in bucket:
                    bucket.append(idx)

    our = collections.defaultdict(lambda: collections.defaultdict(list))
    for o in glob.glob(os.path.join(objdir, '**', '*.obj'), recursive=True):
        out = subprocess.run(['strings', '-a', o], capture_output=True, text=True).stdout
        for ln in out.splitlines():
            r = parse(ln.strip())  # data symbols ONLY -- see ATEXIT comment
            if r:
                bucket = our[r[2]][r[0]]
                if r[1] not in bucket:
                    bucket.append(r[1])

    for side in (tgt, our):
        for names in side.values():
            for k in names:
                names[k].sort()

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
            # Positional deltas are only meaningful when both sides declare the
            # same number of statics under that name; otherwise the count itself
            # is the finding (we invented or dropped a declaration).
            deltas = sorted({o - t for _, tl, ol in bad
                             if ol and len(ol) == len(tl)
                             for t, o in zip(tl, ol)})
            counts = sorted({(len(tl), len(ol or [])) for _, tl, ol in bad
                             if len(tl) != len(ol or [])})
            note = f"   COUNT tgt/ours={counts}" if counts else ""
            print(f"\n{fn}   delta={deltas}{note}")
            for n, t, o in bad:
                print(f"   {n:44} tgt={t} ours={o}")
    if a.json:
        json.dump({'tgt': tgt, 'our': our}, open(a.json, 'w'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
