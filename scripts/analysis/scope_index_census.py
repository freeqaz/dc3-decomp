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

A NAME CAN LEGITIMATELY HOLD MANY SCOPES
========================================
`_s`, `_t` and `$S<n>` are MSVC's own generated names, so ONE enclosing function
routinely declares the same name in a dozen different scopes -- every
`MILO_ASSERT` in `ObjectDir::Handle` contributes another `_s`, at scopes
3, 7, 11, 15, ... 56.  Until 2026-08-19 this tool stored `our[fn][name] = scope`
and `tgt[fn][name] = scope`, i.e. LAST WRITE WINS, so 15 of those 16 readings
were thrown away and the surviving one was whichever the enumeration order
happened to reach last.  Measured on this tree at the time of the fix:

    our side     6,675 (function, static-name) pairs, 568 of them multi-valued
    target side  1,941 pairs from 2,192 symbols.txt entries -- 251 discarded

(Those two figures were measured against `config/373307D9/symbols.txt`, which
was the target authority at the time.  It is not any more -- see `read_map`:
the ORIGINAL image's own linker map is, because 1,194 of symbols.txt's 2,192
local-static names were synthesised from OUR build and diffing against those is
a tautology.  The multi-valued defect and its fix are unaffected; only the
denominator moved.)

The census now keeps the FULL SET of scopes per name on both sides and compares
sets, which is both lossless and order-independent.  `--legacy-single-value`
reproduces the old last-write-wins reading for an A/B.

COVERAGE
========
Per scripts/analysis/coverage.py: this tool states its denominator.  The
universe is every enclosing function seen on EITHER side; functions present in
only one side are counted as drops rather than quietly skipped, which is how the
functions that exist in our objects but not in the target's map became visible.
A missing map or a missing/failing `strings` is EXIT_TOOL_FAILURE, never a
clean "no skew": an empty side reads as "everything is only on the other side",
which reads as no findings.

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
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

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

#: Deliberately LOOSER than NAME: anything shaped like a function-local static.
#: A string that matches this but not NAME is a mangling we do not understand,
#: and the point of counting it is that a future shape cannot vanish in silence.
#: (Measured 2026-08-19: 2,192 of 211,852 symbols.txt lines match the loose form
#: and all 2,192 also parse strictly, so the counter reads 0 today.  It is here
#: for the day it does not.)
LOCAL_STATIC_SHAPE = re.compile(r'^\?[^@?]+@\?[^@?]*@?\?\?')

#: `strings` missing or failing turns every function into `target-only` and the
#: tool then reports a clean "no skew".  That is a tool failure, not a result.
EXIT_TOOL_FAILURE = 5


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


def data_key(fnpart):
    """The one key both sides must agree on for a DATA symbol."""
    return loose(strip_type(fnpart))


class ScopeIndex:
    """enclosing function -> static name -> SET of scope indices.

    A set, not a scalar: see "A NAME CAN LEGITIMATELY HOLD MANY SCOPES" above.

    Every counter here is a denominator.  `considered` is what reached the
    parser, `shaped` is what looked like a local static, `parsed` is what the
    strict regex accepted, and `unparsed_shaped` is the gap between the last
    two -- the population a future mangling would otherwise vanish into.
    """

    def __init__(self, label):
        self.label = label
        self.map = collections.defaultdict(lambda: collections.defaultdict(set))
        self.considered = 0      # strings that reached the parser
        self.shaped = 0          # ...that looked like a local static
        self.parsed = 0          # ...that NAME/ATEXIT accepted
        self.unparsed_shaped = 0  # ...that neither did: the silent-loss shape
        self.readings = 0        # (fn, name, scope) triples recorded

    # -- ingestion ----------------------------------------------------------- #

    def accept(self, fnkey, name, scope):
        """Record one parsed reading."""
        self.shaped += 1
        self.parsed += 1
        self.readings += 1
        self.map[fnkey][name].add(scope)

    def reject(self, text):
        """A string the strict regexes refused.  Counted only if it LOOKED like
        a local static -- an unparsed shaped string is the silent-loss case."""
        if LOCAL_STATIC_SHAPE.match(text):
            self.shaped += 1
            self.unparsed_shaped += 1

    def feed(self, text):
        """Parse one string as a local-static DATA symbol and record it."""
        self.considered += 1
        r = parse(text)
        if r is None:
            self.reject(text)
            return False
        self.accept(data_key(r[2]), r[0], r[1])
        return True

    # -- derived counters, all denominators ---------------------------------- #

    @property
    def pairs(self):
        return sum(len(v) for v in self.map.values())

    @property
    def multivalued(self):
        return sum(1 for names in self.map.values()
                   for scopes in names.values() if len(scopes) > 1)

    @property
    def readings_beyond_first(self):
        """Readings a last-write-wins map would have DISCARDED."""
        return self.readings - self.pairs

    def stats(self):
        return {
            "side": self.label,
            "strings_considered": self.considered,
            "local_static_shaped": self.shaped,
            "parsed": self.parsed,
            "shaped_but_unparsed": self.unparsed_shaped,
            "scope_readings": self.readings,
            "function_name_pairs": self.pairs,
            "multivalued_pairs": self.multivalued,
            "readings_a_scalar_map_would_discard": self.readings_beyond_first,
        }

    def summary_line(self):
        return (f"{self.label:6s}: {self.parsed} local-static symbols parsed of "
                f"{self.shaped} shaped ({self.unparsed_shaped} shaped-but-UNPARSED) "
                f"-> {self.pairs} (function, name) pairs, {self.multivalued} of them "
                f"multi-valued; {self.readings_beyond_first} readings a scalar map "
                f"would discard")

    def as_json(self):
        return {fn: {n: sorted(s) for n, s in sorted(names.items())}
                for fn, names in sorted(self.map.items())}


def read_map(path, idx):
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

    Fills `idx` and returns `(lines_read, complete)`, where `complete` is the
    set of fn/name keys resting on an atexit helper (a COMPLETE enumeration for
    statics that have a destructor) rather than on a data symbol (a partial
    one).
    """
    complete = set()
    lines = 0
    for ln in open(path, errors='replace'):
        lines += 1
        parts = ln.split()
        if len(parts) < 2 or ':' not in parts[0]:
            continue
        sym = parts[1]
        idx.considered += 1
        r = parse(sym, ATEXIT)
        if r:
            key = loose(r[2])
            complete.add((key, r[0]))
        else:
            r = parse(sym)
            if not r:
                idx.reject(sym)
                continue
            key = data_key(r[2])
        idx.accept(key, r[0], r[1])
    return lines, complete


def load_ours(objdir, idx, strings_bin):
    """Every .obj under objdir -> ScopeIndex.  Returns (objects, failures).

    `strings` is run per object and its RETURN CODE IS CHECKED.  A missing or
    failing `strings` yields empty output, which would make every function look
    `target-only` and the census report a clean "no skew" -- so failures are
    counted and made fatal rather than absorbed.
    """
    # sorted(): glob() returns readdir order, which is neither sorted nor
    # guaranteed stable across a rebuild.  Verified 2026-08-19 on this tree:
    # `objs != sorted(objs)` (989 objects, first divergence at index 0).
    all_objs = sorted(glob.glob(os.path.join(objdir, '**', '*.obj'), recursive=True))
    # *.manual.obj is not a linked build product -- it is a hand-assembled
    # leftover that ninja neither produces nor links.  Sweeping it in gave
    # ContentLoadingPanel::SetType a phantom second `types` static at scope
    # 5 alongside the real one at 6 (which matches the map exactly).
    objs = [o for o in all_objs if not o.endswith('.manual.obj')]
    skipped_manual = len(all_objs) - len(objs)
    failures = []
    for o in objs:
        r = subprocess.run([strings_bin, '-a', o], capture_output=True, text=True)
        if r.returncode != 0:
            failures.append((o, r.returncode, (r.stderr or '').strip()[:120]))
            continue
        for ln in r.stdout.splitlines():
            idx.feed(ln.strip())   # data symbols ONLY -- see ATEXIT comment
    return objs, failures, skipped_manual


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default=os.getcwd())
    ap.add_argument('--map', default=None,
                    help='original linker map (target authority)')
    ap.add_argument('--objects', default=None)
    ap.add_argument('--json', default=None)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--strings-bin', default='strings',
                    help="the `strings` binary to read our objects with "
                         "(default: strings, resolved on PATH)")
    ap.add_argument('--legacy-single-value', action='store_true',
                    help="reproduce the pre-2026-08-19 reading, where "
                         "our[fn][name]/tgt[fn][name] held ONE scope (last write "
                         "wins) instead of the full set. For A/B only: it "
                         "discarded 5,762 of our 12,437 readings and 251 of the "
                         "target's 2,192. Which reading survived depended on "
                         "enumeration order, so this flag APPROXIMATES the old "
                         "answer (max) rather than replaying it.")
    add_coverage_args(ap)
    a = ap.parse_args()
    mapfile = a.map or os.path.join(a.project, 'orig/373307D9/ham_xbox_r.map')
    objdir = a.objects or os.path.join(a.project, 'build/373307D9/src')

    strings_bin = shutil.which(a.strings_bin) or a.strings_bin
    if shutil.which(a.strings_bin) is None:
        print(f"FATAL: `{a.strings_bin}` not found on PATH. Without it every "
              f"function reads as target-only and this census would report a "
              f"clean 'no skew' from an empty scan.", file=sys.stderr)
        return EXIT_TOOL_FAILURE
    if not os.path.exists(mapfile):
        print(f"FATAL: target linker map not found: {mapfile}. Without it the "
              f"target side is EMPTY, every function reads as ours-only, and "
              f"this census would report a clean 'no skew' from nothing.",
              file=sys.stderr)
        return EXIT_TOOL_FAILURE

    # fn -> name -> SET of scope indices.  A function may declare several
    # statics that share a name -- every MILO_NOTIFY_ONCE in a function declares
    # a `_dw`, every static Message a `msg`.  Keying name -> single int silently
    # kept whichever one was parsed last on each side and then compared two
    # unrelated statics, which is how RndTexBlender::DrawShowing (5 `_dw` ours,
    # 3 target) came out as a one-line "tgt=15 ours=9" row.
    tgt_idx = ScopeIndex('target')
    n_lines, complete = read_map(mapfile, tgt_idx)
    our_idx = ScopeIndex('ours')
    objs, failures, skipped_manual = load_ours(objdir, our_idx, strings_bin)

    tgt, our = tgt_idx.map, our_idx.map

    cov = CoverageReport("scope_index_census", args=a)
    cov.universe(len(set(tgt) | set(our)),
                 "enclosing functions declaring >=1 local static, target UNION ours")
    cov.extra("target_map", mapfile)
    cov.extra("target_map_lines", n_lines)
    cov.extra("target_atexit_backed_keys", len(complete))
    cov.extra("objects_scanned", len(objs))
    cov.extra("objects_skipped_manual", skipped_manual)
    cov.extra("objects_strings_failed", len(failures))
    cov.extra("target_side", tgt_idx.stats())
    cov.extra("our_side", our_idx.stats())
    cov.note(f"target authority: {mapfile} -- {n_lines} lines -> "
             f"{tgt_idx.parsed} local-static symbols, {len(complete)} of the "
             f"(fn, name) keys atexit-backed (a complete enumeration)")
    cov.note(f"objects: {len(objs)} .obj scanned with `{strings_bin}`, "
             f"{skipped_manual} *.manual.obj skipped, {len(failures)} failed")
    cov.note(f"scope collisions kept, not folded: {tgt_idx.multivalued} target / "
             f"{our_idx.multivalued} our (function, name) pairs hold >1 scope")
    if a.legacy_single_value:
        cov.note("--legacy-single-value: comparing ONE scope per name "
                 "(last write wins) -- lossy, kept for A/B only")

    print(tgt_idx.summary_line())
    print(our_idx.summary_line())
    if failures:
        print(f"\n!! `{strings_bin}` FAILED on {len(failures)} of {len(objs)} "
              f"objects. Every symbol they hold is missing from the 'ours' side, "
              f"which reads as 'target-only', which reads as 'no skew'. "
              f"This run is NOT a census.", file=sys.stderr)
        for o, rc, err in failures[:10]:
            print(f"   rc={rc} {o}  {err}", file=sys.stderr)

    # >>> RESULT-CHANGING (set comparison replaces last-write-wins) ----------- #
    def readings(side, fn, name):
        """Scope readings for one name, as a sorted list ([] when absent)."""
        scopes = sorted(side.get(fn, {}).get(name, ()))
        if a.legacy_single_value and scopes:
            # The historical reading kept exactly one value.  Which one was
            # enumeration-order dependent; max() is the closest deterministic
            # stand-in and is documented as an approximation, not a replay.
            return [scopes[-1]]
        return scopes
    # <<< RESULT-CHANGING ---------------------------------------------------- #

    match = missing = extra = 0
    rows = []
    for fn in sorted(set(tgt) | set(our)):
        if fn not in our:
            missing += 1
            cov.drop("target-only-no-our-object",
                     note="the linker map names it; no object of ours defines it")
            continue
        if fn not in tgt:
            extra += 1
            cov.drop("ours-only-absent-from-target-map",
                     note="our objects declare it; the target's linker map does "
                          "not -- never examined before 2026-08-19")
            continue
        cov.examine()
        names = tgt[fn]
        bad = []
        for n in sorted(names):
            t = readings(tgt, fn, n)
            o = readings(our, fn, n)
            if t != o:
                bad.append((n, t, o))
        if bad:
            rows.append((fn, bad))
        else:
            match += 1

    print(f"enclosing functions: match={match} diff={len(rows)} "
          f"target-only={missing} ours-only={extra} "
          f"universe={match + len(rows) + missing + extra}")
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
        json.dump({'tgt': tgt_idx.as_json(), 'our': our_idx.as_json(),
                   '_coverage': cov.as_dict()}, open(a.json, 'w'))
    rc = cov.emit()
    return EXIT_TOOL_FAILURE if failures else rc


if __name__ == '__main__':
    sys.exit(main())
