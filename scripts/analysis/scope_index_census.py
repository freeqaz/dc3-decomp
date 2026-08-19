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
    else if (c) stmt;            +3     unbraced while/for body  +1
    if (a) if (b) stmt;          +4     MILO_NOTIFY_ONCE/_WARN   1
    MILO_ASSERT(c, line)         5      START_AUTO_TIMER         0
                                        MILO_NOTIFY (not ONCE)   0

THE COUNTER STARTS AT 2 BECAUSE THE FUNCTION BODY IS ITSELF SCOPE 2 -- a static
declared ahead of every construct reads 2.  Start at 2 and add the table in
source order.  A static declared after an inner block keeps that block's number;
the counter never goes back down, so two statics at the SAME lexical scope get
DIFFERENT indices when constructs sit between them.

Inlining does not feed the counter, and a destructor-bearing temporary in the
static's initialiser does not open a scope.  Both measured, both counterintuitive.
See docs/decomp/patterns/fixable-scope-index.md.

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


def strip_type(fnpart):
    """`??<fn>@4VMessage@@A` -> `??<fn>`.

    A local-static DATA symbol carries the static's type after the enclosing
    function's mangling; the ??__F atexit helper does not.  Both sides must be
    keyed the same way or two `msg` statics of DIFFERENT types in one function
    look like two competing lists for one declaration.  That is exactly what
    produced the two bogus `OptionsPanel::OnMsg` rows in the 2026-08-19 census:
    ?msg@?BA@...@4VLinkingCodeRetrievedMsg@@A and
    ?msg@?M@...@4VTokenRedeemedMsg@@A are two correct, matching statics, but
    prefix-folding both atexit helpers into both type-keyed buckets rendered
    them as `tgt=[12,16] ours=[16]` and `tgt=[12,16] ours=[12]`.
    """
    # Split at the FIRST `@4` whose head is a complete function mangling
    # (they all end in `@Z`).  rsplit is wrong: a templated static's type can
    # itself contain `@4` -- `?normalized@?P@??AnalyzeData@?A0x5c754947@@...@Z
    # @4V?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@4@A` ends in a `@4@A`
    # back-reference, so rsplit cut inside the type, left a head that did not
    # end in Z, gave up, and the data key never matched the atexit key -- which
    # rendered two statics we do have as `COUNT tgt/ours=(1,0)`.
    pos = fnpart.find('@4')
    while pos != -1:
        if fnpart[:pos].endswith('Z'):
            return fnpart[:pos]
        pos = fnpart.find('@4', pos + 1)
    return fnpart


BACKREF = re.compile(r'@(\d)@')


def loose(fnpart):
    """Function key with MSVC back-reference digits blanked.

    A `??__F` atexit helper mangles its enclosing function with FEWER preceding
    name components than the data symbol does, so MSVC numbers the SAME function's
    back-references differently on the two sides.  `FileMerger::PostMerge` is
    `PAUMerger@2@` in `?msg@?5??PostMerge@...` and `PAUMerger@1@` in
    `??__Fmsg@?5??PostMerge@...`.  Since the map supplies the target's atexit keys
    and our objects supply data keys, an unblanked key never meets its partner:
    both of PostMerge's real target helpers were invisible, and the second `msg`
    we correctly declare was reported as invented.
    """
    return BACKREF.sub('@#@', fnpart)


def read_map(path):
    """Local statics of the ORIGINAL image, from its own linker map.

    THIS, not `config/373307D9/symbols.txt`, is the target authority.
    symbols.txt names 2,192 local-static data symbols but only 998 of them
    appear in `orig/373307D9/ham_xbox_r.map`; the other 1,194 were synthesised
    from OUR build, and 97.9% of them are byte-identical to a name our own
    objects already emit (vs 85.9% for the map-backed ones).  Diffing our
    indices against those is a tautology dressed up as evidence -- and where it
    is not a tautology it is worse: a synthesised `?_dw@?2??DataIndex@
    NavListSortMgr...` sat next to the map's real `??__F_dw@?1??DataIndex@...`
    and made one static look like two, which is how the whole `COUNT
    tgt/ours=(2,1)` class was manufactured.  (NavListSortMgr::DataIndex is 100%
    with 52/52 instructions equal and contains exactly one MILO_NOTIFY_ONCE.)

    Returns fn -> name -> sorted list of indices, and the set of fn/name keys
    that rest on an atexit helper (a COMPLETE enumeration for statics that have
    a destructor) rather than on a data symbol (a partial one).
    """
    out = collections.defaultdict(lambda: collections.defaultdict(list))
    complete = set()
    for ln in open(path, errors='replace'):
        parts = ln.split()
        if len(parts) < 2 or ':' not in parts[0]:
            continue
        sym = parts[1]
        r = parse(sym, ATEXIT)
        if r:
            r = (r[0], r[1], loose(r[2]))
            complete.add((r[2], r[0]))
        else:
            r = parse(sym)
            if not r:
                continue
            r = (r[0], r[1], loose(strip_type(r[2])))
        bucket = out[r[2]][r[0]]
        if r[1] not in bucket:
            bucket.append(r[1])
    return out, complete


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default=os.getcwd())
    ap.add_argument('--map', default=None,
                    help='original linker map (target authority)')
    ap.add_argument('--objects', default=None)
    ap.add_argument('--json', default=None)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    mapfile = a.map or os.path.join(a.project, 'orig/373307D9/ham_xbox_r.map')
    objdir = a.objects or os.path.join(a.project, 'build/373307D9/src')

    # name -> SORTED LIST of scope indices.  A function may declare several
    # statics that share a name -- every MILO_NOTIFY_ONCE in a function declares
    # a `_dw`, every static Message a `msg`.  Keying name -> single int silently
    # kept whichever one was parsed last on each side and then compared two
    # unrelated statics, which is how RndTexBlender::DrawShowing (5 `_dw` ours,
    # 3 target) came out as a one-line "tgt=15 ours=9" row.
    tgt, complete = read_map(mapfile)

    our = collections.defaultdict(lambda: collections.defaultdict(list))
    for o in glob.glob(os.path.join(objdir, '**', '*.obj'), recursive=True):
        # *.manual.obj is not a linked build product -- it is a hand-assembled
        # leftover that ninja neither produces nor links.  Sweeping it in gave
        # ContentLoadingPanel::SetType a phantom second `types` static at scope
        # 5 alongside the real one at 6 (which matches the map exactly).
        if o.endswith('.manual.obj'):
            continue
        out = subprocess.run(['strings', '-a', o], capture_output=True, text=True).stdout
        for ln in out.splitlines():
            r = parse(ln.strip())  # data symbols ONLY -- see ATEXIT comment
            if r:
                bucket = our[loose(strip_type(r[2]))][r[0]]
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
            # A count row is evidence in BOTH directions only when the key is
            # atexit-backed: one ??__F helper for that fn/name proves the type
            # has a destructor, and the map names every helper in the image, so
            # the enumeration is complete.  A data-only key is not -- a
            # trivially destructible static (Symbol, DataArray*, const char*)
            # has no helper, and if the map also lacks its data name it is
            # simply invisible.  The whole `_s`/SYNC_PROP class hides here: the
            # map carries 511 SyncProperty symbols and ZERO `_s` statics, so
            # `RndRibbon::SyncProperty _s tgt=[7] ours=[7,18,30,...]` says
            # nothing about the target at all.
            counts = sorted({(len(tl), len(ol or []), (fn, n) in complete)
                             for n, tl, ol in bad if len(tl) != len(ol or [])})
            note = ''
            if counts:
                blind = all(t < o and not ax for t, o, ax in counts)
                note = ('   COUNT tgt/ours=%s%s'
                        % ([(t, o) for t, o, _ in counts],
                           '  [target-side blind spot, not evidence]' if blind else ''))
            print(f"\n{fn}   delta={deltas}{note}")
            for n, t, o in bad:
                src = 'atexit' if (fn, n) in complete else 'data-only'
                print(f"   {n:44} tgt={t} ours={o}   [{src}]")
    if a.json:
        json.dump({'tgt': tgt, 'our': our}, open(a.json, 'w'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
