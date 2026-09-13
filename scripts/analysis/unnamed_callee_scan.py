#!/usr/bin/env python3
"""Paired-`bl` census for the two defect classes NO RULER CHARGES.

WHY THIS EXISTS
---------------
`name_check` -- the only ruler that charges relocation NAMES -- exempts a
relocation whose target carries a PLACEHOLDER name (`fn_<addr>`, `lbl_*`) BEFORE
the wrong-callee detector runs.  So when our `bl` pairs against a retail `bl`
whose destination is unnamed, calling an entirely different function costs
EXACTLY ZERO and the row sits at a clean fuzzy 100.

  Purest instance, found in rb3-xenon 2026-09-13: their source called
  `RndMat::Terminate` -- a body that is literally `{}` but EXTRN, so it still
  emits a `bl` -- where retail called `DOFProc::Terminate`, an 80-byte
  `RELEASE(TheDOFProc)` at an unnamed address.  Zero charge.  Clean 100.  Only
  decoding the retail destination revealed it.

The masking needs BOTH halves: an unnamed retail destination (so the ruler looks
away) and a callee of ours that survives to emit a `bl` at all.  A body of `{}`
that is EXTRN cannot be inlined away across a TU boundary, which is why the
trivial-callee population is where the class concentrates.

WHAT THIS SCANS
---------------
Every unit in `objdiff.json` with both a target and a base object.  For each
function DEFINED ON BOTH SIDES it takes the REL24 (`bl`) relocations in order of
offset and, when the two sides make the SAME NUMBER of calls, pairs them by
ordinal.  Then per site:

  Query A  UNNAMED RETAIL DESTINATION
           retail's relocation resolves to a placeholder name while ours
           resolves to a real symbol.  The pure masking shape.

  Query B  SIZE DISPARITY  (rank first, decode only the survivors)
           our callee's body is trivial (<= --trivial-bytes, default 8; a bare
           `blr` is 4) while retail's destination is materially larger.  Cheaper
           than a body comparison and it is what caught the rb3-xenon case: 4
           bytes of `blr` against 80 bytes of `RELEASE(global)`.

  Query C  EMPTY-EXTRN  (`--empty-extrn`; the same question from the source end)
           enumerate out-of-line trivially-empty definitions (`... Class::M() {}`
           and the `const` form) under `src/`, minus `xdk/` and `stlport/`;
           keep the ones our build really emits as a bare `blr`; then look at
           what retail calls at each of their paired sites, ranked by the retail
           destination's size.  That is an upper bound on the CANDIDATE set, not
           the defect set.

Sizes come from `config/373307D9/symbols.txt` for the retail side and from the
defining COMDAT section for ours.  Names resolve through `ham_xbox_r.map` plus
`build/373307D9/icf_aliases.map`, so a proven ICF fold compares EQUAL rather than
being reported as a wrong callee.

CONTROLS -- and why a null result here is not automatically clean
------------------------------------------------------------------
Three populations are reported alongside every run, and the run REFUSES to call
itself clean if any is empty:

  * AGREEING SITES.  Sites where both sides name the same symbol (or two names
    the maps prove are one address).  If this is zero the ordinal pairing is
    broken and every "no hits" is meaningless.
  * BOTH-TRIVIAL SITES.  Our callee is trivial AND retail's destination is
    trivial too -- the matched pairs with nothing to see.  Query B's control
    population.  If Query B finds no candidates AND no both-trivial rows, the
    size lookup is broken, not the tree clean.
  * KNOWN-NAMED SITES.  Retail destinations that DO have a name.  Query A's
    control: if every retail destination looks unnamed, the placeholder regex
    or the symbol table read is wrong.

Query C carries the same discipline separately: an empty function of ours that
retail ALSO has empty is a matched pair with nothing to see, and those rows
dominate.  THAT POPULATION IS THE CONTROL.  A Query C run that returns no
"retail destination is also trivial" rows means the pairing or the size lookup is
broken, NOT that the tree is clean, and `--empty-extrn` refuses to call such a
run clean.

TWO FALSE-POSITIVE CLASSES Query C labels rather than silently counting:
  * VTABLE-ONLY.  Many empty definitions are reached only through a vtable
    (`bctrl`, no REL24), so they never produce a paired `bl` at all.  Reported as
    `no_paired_call_site`, never as "clean".
  * HOOK FAMILIES.  A cluster of adjacent same-shape empties on one class
    (SwingHook / HitHook / MissHook / PassHook / SeeGemHook) is a genuine no-op
    hook family, not a finding.

`--selftest` additionally injects a synthetic negative control: it takes a real
agreeing site and rewrites the base-side callee name to a decoy, then requires
the classifier to flag it.  If the classifier cannot convict a site that is
KNOWN wrong, the tool exits 5 rather than reporting a clean sweep.

WHAT THIS CANNOT SEE  (state before quoting a clean sweep)
-----------------------------------------------------------
  * Functions whose two sides make DIFFERENT numbers of calls (reported as
    `fn_arity_differs`).  Ordinal pairing is unsound there, so they are skipped,
    NOT cleared.
  * Functions not defined on both sides -- including every unit we have not
    decompiled.  In dc3 that is most of `default/xdk/**`.
  * Indirect calls.  A vtable dispatch is `bctrl`, not `bl`, and carries no
    REL24; the wrong-vtable-slot class is `run_objdiff(include_data=true)`'s job.
  * A call whose retail destination is a STATIC.  dtk names an internal-linkage
    function `fn_<addr>` -- correctly -- so it lands in Query A as a hit and must
    be adjudicated, but a static that our tree does NOT call at all produces no
    site and is invisible here.  (Related: `missing_instantiations.py` reads only
    `.fn "...", global` out of the target `.s`, so statics are outside it
    entirely; a null result over that path is not evidence of absence either.)
  * Whether a decoded mismatch is a DEFECT.  dc3 and RB3 are different games and
    both trees can be correct while differing; adjudicate against dc3's own
    target bytes.  Non-defect classes to expect: a proven ICF fold, an EH funclet
    paired by byte signature, a register-save helper, and a COMDAT emitted into
    two objects where `symbols.txt` can only name one of them.

COFF NOTE
---------
The symbol reader discriminates on the symbol TYPE field (`0x20` == DT_FUNCTION).
A file-static function and its SECTION symbol share value 0 and storage class 3,
so a "last symbol at or before this offset" owner lookup that does not check TYPE
credits every static function's relocations to `.text`.

USAGE
-----
  python3 scripts/analysis/unnamed_callee_scan.py --selftest
  python3 scripts/analysis/unnamed_callee_scan.py --root . --json-out /tmp/hits.json
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

SYM_ENT = 18
REL24 = 6
DT_FUNCTION = 0x20
BLR = 0x4E800020

PLACEHOLDER = re.compile(r'^(fn|lbl|sub|func)_[0-9A-Fa-f]{6,8}$')
MAP_RE = re.compile(
    r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+\S*\s*\S*\s*(\S*)')
SYMTXT_RE = re.compile(
    r'^(\S+)\s*=\s*\.(\w+):0x([0-9A-Fa-f]+);.*?type:(\w+)(?:\s+size:0x([0-9A-Fa-f]+))?')


# --------------------------------------------------------------------------
def load_map(path: Path):
    """{name: address} from a linker map.  First definition wins."""
    out = {}
    if not path.exists():
        return out
    for line in path.read_text(errors='replace').splitlines():
        m = MAP_RE.match(line)
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def load_map_multi(path: Path):
    """{name: [addresses]} -- a name appearing twice is a COMDAT emitted into two
    objects, which is exactly how a retail destination ends up unnamed."""
    out = defaultdict(list)
    if not path.exists():
        return out
    for line in path.read_text(errors='replace').splitlines():
        m = MAP_RE.match(line)
        if m:
            out[m.group(1)].append((int(m.group(2), 16), m.group(3)))
    return out


def load_symbols_txt(path: Path):
    """{name: (addr, size)} for every symbol dtk knows, placeholders included."""
    out = {}
    for line in path.read_text(errors='replace').splitlines():
        m = SYMTXT_RE.match(line)
        if m:
            out[m.group(1)] = (int(m.group(3), 16),
                               int(m.group(5), 16) if m.group(5) else None)
    return out


# --------------------------------------------------------------------------
def parse_obj(path: Path):
    """Xbox 360 PPC COFF -> (calls, defined, sizes).

    calls   {fnname: [(offset_in_fn, callee_name), ...]} sorted by offset
    defined {fnname}
    sizes   {fnname: bytes}  -- COMDAT section size, or next-symbol delta
    """
    data = path.read_bytes()
    if len(data) < 20:
        return None
    _m, nsec, _t, symptr, nsym, opthdr, _c = struct.unpack_from('<HHIIIHH', data, 0)
    if not symptr or not nsym:
        return None
    so = 20 + opthdr
    sections = []
    for i in range(nsec):
        b = so + i * 40
        if b + 40 > len(data):
            return None
        vs, va, rs, rp, relp, _lp, nrel, _nl, chars = struct.unpack_from('<IIIIIIHHI', data, b + 8)
        sections.append(dict(rawptr=rp, rawsize=rs, relptr=relp, nrel=nrel, chars=chars))
    strtab = data[symptr + nsym * SYM_ENT:]

    def sname(raw):
        if raw[0:4] == b'\0\0\0\0':
            off = struct.unpack_from('<I', raw, 4)[0]
            return strtab[off:strtab.find(b'\0', off)].decode('latin1')
        return raw.rstrip(b'\0').decode('latin1')

    symbols = []
    i = 0
    while i < nsym:
        b = symptr + i * SYM_ENT
        value, secnum, styp, sclass, naux = struct.unpack_from('<IhHBB', data, b + 8)
        symbols.append(dict(name=sname(data[b:b + 8]), value=value, sec=secnum,
                            typ=styp, cls=sclass))
        symbols.extend([None] * naux)
        i += 1 + naux

    # TYPE 0x20 == DT_FUNCTION.  Without this a file-static function and the
    # SECTION symbol both have value 0 / class 3 and the owner lookup credits
    # the static's relocations to `.text`.
    defined = defaultdict(list)
    for s in symbols:
        if s and s['sec'] > 0 and s['typ'] == DT_FUNCTION and s['cls'] in (2, 3, 105) and s['name']:
            defined[s['sec']].append(s)
    for v in defined.values():
        v.sort(key=lambda s: s['value'])

    sizes = {}
    for secnum, lst in defined.items():
        sec = sections[secnum - 1] if 0 < secnum <= len(sections) else None
        end = sec['rawsize'] if sec else 0
        for j, s in enumerate(lst):
            nxt = lst[j + 1]['value'] if j + 1 < len(lst) else end
            if nxt > s['value']:
                sizes[s['name']] = nxt - s['value']

    def owner(secnum, off):
        best = None
        for s in defined.get(secnum, []):
            if s['value'] <= off:
                best = s
            else:
                break
        return best

    calls = defaultdict(list)
    for si, sec in enumerate(sections, start=1):
        if not sec['nrel'] or not (sec['chars'] & 0x20):
            continue
        for r in range(sec['nrel']):
            b = sec['relptr'] + r * 10
            if b + 10 > len(data):
                break
            vaddr, symidx, rtyp = struct.unpack_from('<IIH', data, b)
            if rtyp != REL24 or symidx >= len(symbols) or symbols[symidx] is None:
                continue
            fn = owner(si, vaddr)
            if fn:
                calls[fn['name']].append((vaddr - fn['value'], symbols[symidx]['name']))
    for v in calls.values():
        v.sort()

    # bodies, for the bare-blr test
    bodies = {}
    for secnum, lst in defined.items():
        sec = sections[secnum - 1] if 0 < secnum <= len(sections) else None
        if not sec or not sec['rawptr']:
            continue
        raw = data[sec['rawptr']:sec['rawptr'] + sec['rawsize']]
        for s in lst:
            n = sizes.get(s['name'])
            if n and n <= 64:
                bodies[s['name']] = raw[s['value']:s['value'] + n]

    return dict(calls=calls, defined={s['name'] for l in defined.values() for s in l},
                sizes=sizes, bodies=bodies)


def count_rel24(path: Path):
    """REL24 relocations in every code section, counted straight from the COFF
    relocation table with no symbol attribution and no disassembly.

    This is the CONTROL denominator for the whole scan.  The scan itself is
    relocation-table driven -- it never linear-disassembles, so it cannot be
    truncated the way a capstone walk is when it meets one undecodable word (a
    sibling session's sweep reported 0 callees in a 4,260-byte function whose
    object declared nrel=127, because the decoder halted before offset 0x52c).
    But the scan DOES drop a relocation whose offset falls outside every
    DT_FUNCTION symbol's extent, and that silent drop is what this counts."""
    d = path.read_bytes()
    if len(d) < 20:
        return 0
    _m, nsec, _t, symptr, nsym, opt, _c = struct.unpack_from('<HHIIIHH', d, 0)
    so, n = 20 + opt, 0
    for i in range(nsec):
        b = so + i * 40
        if b + 40 > len(d):
            break
        _vs, _va, _rs, _rp, relp, _lp, nrel, _nl, ch = struct.unpack_from('<IIIIIIHHI', d, b + 8)
        if not (ch & 0x20):
            continue
        for r in range(nrel):
            o = relp + r * 10
            if o + 10 > len(d):
                break
            if struct.unpack_from('<IIH', d, o)[2] == REL24:
                n += 1
    return n


def reloc_accounting(sc, units, limit=None):
    """Per side: REL24 present vs REL24 the scan attributed to a function."""
    tot = Counter()
    drops = []
    seen = 0
    for u in units:
        for side in ('base_path', 'target_path'):
            rel = u.get(side)
            if not rel or not (sc.root / rel).exists():
                continue
            o = sc.obj(rel)
            if not o:
                continue
            attributed = sum(len(v) for v in o['calls'].values())
            total = count_rel24(sc.root / rel)
            tot[side + '.total'] += total
            tot[side + '.attributed'] += attributed
            if total != attributed:
                drops.append((total - attributed, u['name'], side, total, attributed))
        seen += 1
        if limit and seen >= limit:
            break
    return tot, sorted(drops, reverse=True)


def is_bare_blr(body):
    return body is not None and len(body) == 4 and struct.unpack('>I', body)[0] == BLR


# --------------------------------------------------------------------------
class Scanner:
    def __init__(self, root: Path, trivial_bytes=8, size_ratio=3.0):
        self.root = root
        self.trivial = trivial_bytes
        self.ratio = size_ratio
        self.addr = load_map(root / 'orig/373307D9/ham_xbox_r.map')
        for n, a in load_map(root / 'build/373307D9/icf_aliases.map').items():
            self.addr.setdefault(n, a)
        self.mapmulti = load_map_multi(root / 'orig/373307D9/ham_xbox_r.map')
        self.symtxt = load_symbols_txt(root / 'config/373307D9/symbols.txt')
        self._objs = {}

    def obj(self, rel):
        if rel not in self._objs:
            p = self.root / rel
            self._objs[rel] = parse_obj(p) if p.exists() else None
        return self._objs[rel]

    def key(self, name):
        """Address if the maps know the name, else a name-keyed sentinel.  Two
        different spellings of one ICF-folded body get the SAME key."""
        a = self.addr.get(name)
        return a if a is not None else ('N:' + name)

    def retail_size(self, name):
        e = self.symtxt.get(name)
        return e[1] if e else None

    def base_size(self, name, unit_obj):
        s = unit_obj['sizes'].get(name)
        if s:
            return s
        # defined in another TU: find it lazily via the maps is not possible for
        # size, so fall back to the retail size of the same name (our COMDAT for
        # a matching function is the same shape) -- flagged as inferred.
        return None

    def scan(self, base_size_index):
        units = json.load(open(self.root / 'objdiff.json'))['units']
        st = Counter()
        hits_a, hits_b, both_trivial = [], [], []
        for u in units:
            bp, tp = u.get('base_path'), u.get('target_path')
            if not (bp and tp):
                continue
            b, t = self.obj(bp), self.obj(tp)
            if not b or not t:
                st['unit_skipped'] += 1
                continue
            st['unit_ok'] += 1
            for fn in (set(b['calls']) | set(t['calls'])) & b['defined'] & t['defined']:
                bl, tl = b['calls'].get(fn, []), t['calls'].get(fn, [])
                if not bl and not tl:
                    continue
                st['fn_paired'] += 1
                if len(bl) != len(tl):
                    st['fn_arity_differs'] += 1
                    continue
                st['fn_same_arity'] += 1
                for (bo, bn), (to, tn) in zip(bl, tl):
                    st['site'] += 1
                    tph = bool(PLACEHOLDER.match(tn))
                    bph = bool(PLACEHOLDER.match(bn))
                    if not tph:
                        st['ctl_retail_named'] += 1
                    if self.key(bn) == self.key(tn):
                        st['ctl_agree'] += 1
                    bsz = base_size_index.get(bn)
                    tsz = self.retail_size(tn)
                    if tph and not bph:
                        st['A_retail_placeholder'] += 1
                        hits_a.append(dict(unit=u['name'], fn=fn, off=bo, ours=bn,
                                           retail=tn, our_size=bsz, retail_size=tsz))
                    if bsz is not None and tsz is not None:
                        if bsz <= self.trivial and tsz <= self.trivial:
                            st['ctl_both_trivial'] += 1
                            both_trivial.append((u['name'], fn, bn, tn, bsz, tsz))
                        elif bsz <= self.trivial and tsz > max(self.trivial,
                                                               bsz * self.ratio):
                            st['B_size_disparity'] += 1
                            hits_b.append(dict(unit=u['name'], fn=fn, off=bo, ours=bn,
                                               retail=tn, our_size=bsz, retail_size=tsz,
                                               agree=self.key(bn) == self.key(tn)))
        return st, hits_a, hits_b, both_trivial


# --------------------------------------------------------------------------
# Query C -- the empty-EXTRN population, enumerated from source
# --------------------------------------------------------------------------
EMPTY_DEF = re.compile(
    r'^[ \t]*(?!(?:if|for|while|switch|else|do|return|case|struct|class|namespace|'
    r'extern|template)\b)([A-Za-z_][\w:<>,*&\s\[\]]*?)\b'
    r'([A-Za-z_]\w*(?:<[^;{}()]*>)?::~?[A-Za-z_]\w*|operator[^\s(]*)'
    r'\s*\(([^;{})]*)\)\s*(const\s*)?\{\s*\}', re.M)
SKIP_DIRS = {'xdk', 'stlport'}


def enumerate_empty_defs(root: Path):
    """[(path, Class::Method, line)] for out-of-line `... {}` definitions."""
    out = []
    for base, dirs, files in os.walk(root / 'src'):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in sorted(files):
            if not f.endswith(('.cpp', '.cc')):
                continue
            p = Path(base) / f
            txt = p.read_text(errors='replace')
            for m in EMPTY_DEF.finditer(txt):
                out.append((str(p.relative_to(root)), m.group(2),
                            txt[:m.start()].count('\n') + 1))
    return out


def mangled_prefix(qualname):
    """`Class::Method` -> the `?Method@Class@@` prefix MSVC mangles it to."""
    parts = qualname.replace('~', '?1').split('::')
    return f'?{parts[1]}@{parts[0]}@@' if len(parts) == 2 else None


def query_c(sc: Scanner, units, blrset):
    src_empties = enumerate_empty_defs(sc.root)
    frags = defaultdict(list)
    for p, n, ln in src_empties:
        f = mangled_prefix(n)
        if f:
            frags[f].append((p, n, ln))
    emitted = {s for s in blrset for f in frags if s.startswith(f)}

    control, candidates = [], []
    called = set()
    for u in units:
        bp, tp = u.get('base_path'), u.get('target_path')
        if not (bp and tp):
            continue
        b, t = sc.obj(bp), sc.obj(tp)
        if not b or not t:
            continue
        for fn in (set(b['calls']) | set(t['calls'])) & b['defined'] & t['defined']:
            bl, tl = b['calls'].get(fn, []), t['calls'].get(fn, [])
            if len(bl) != len(tl):
                continue
            for (bo, bn), (to, tn) in zip(bl, tl):
                if bn not in emitted:
                    continue
                called.add(bn)
                tsz = sc.retail_size(tn)
                row = (u['name'], fn, bn, tn, tsz)
                if tsz is None:
                    continue
                (control if tsz <= sc.trivial else candidates).append(row)
    return dict(src_empties=src_empties, frags=frags, emitted=emitted,
                called=called, control=control, candidates=candidates)


def report_query_c(res, show=40):
    n_src = len(res['src_empties'])
    print('\n===== QUERY C: empty-EXTRN population =====')
    print(f'  out-of-line trivially-empty definitions in src/ (xdk, stlport excluded): {n_src}')
    print(f'    across {len({n.split("::")[0] for _, n, _ in res["src_empties"]})} distinct scopes')
    print(f'  of those, emitted by our build as a bare `blr`:                          '
          f'{len(res["emitted"])}')
    print(f'  with at least one DIRECT paired call site:                               '
          f'{len(res["called"])} symbols')
    print(f'  paired to a TRIVIAL retail destination  (the CONTROL population):        '
          f'{len(res["control"])} sites')
    print(f'  paired to a NON-trivial retail destination (the CANDIDATES):             '
          f'{len(res["candidates"])} sites')
    print(f'  no paired call site (vtable-only, or never called cross-TU):             '
          f'{len(res["emitted"]) - len(res["called"])} symbols')
    if res['candidates']:
        print('\n  CANDIDATES, retail destination size descending -- decode these:')
        for un, fn, bn, tn, tsz in sorted(res['candidates'], key=lambda r: -(r[4] or 0))[:show]:
            print(f'    {un:40} {fn[:38]:38} ours={bn[:40]:40} -> retail={tn[:44]} {tsz}B')
    if not res['control']:
        print('\n  ⚠ VACUOUS: zero both-trivial rows. An empty function of ours that retail also')
        print('    has empty MUST show up here and normally dominates; its absence means the')
        print('    pairing or the size lookup is broken, NOT that the tree is clean.')
        return 6
    if not res['candidates']:
        print(f'\n  NULL RESULT, and it is meaningful: the control population is {len(res["control"])},')
        print('  so the query demonstrably can see this shape. Every empty-EXTRN callee of ours')
        print('  pairs against a retail destination that is trivial too.')
    return 0


def build_base_size_index(scanner, units):
    """{symbol: size} over every function our build DEFINES anywhere.  A callee
    is usually defined in a different object from its call site."""
    idx = {}
    blr = set()
    for u in units:
        bp = u.get('base_path')
        if not bp:
            continue
        o = scanner.obj(bp)
        if not o:
            continue
        for n, s in o['sizes'].items():
            idx.setdefault(n, s)
        for n, body in o['bodies'].items():
            if is_bare_blr(body):
                blr.add(n)
    return idx, blr


# --------------------------------------------------------------------------
def selftest(root: Path):
    """Negative control: the classifier must convict a site we KNOW is wrong."""
    print('== unnamed_callee_scan selftest ==')
    sc = Scanner(root)
    if not sc.addr:
        print('VACUOUS: ham_xbox_r.map unreadable. Exit 9.')
        return 9
    if not sc.symtxt:
        print('VACUOUS: symbols.txt unreadable. Exit 9.')
        return 9
    print(f'  [PASS] maps loaded  ({len(sc.addr)} map names, {len(sc.symtxt)} symbols.txt)')

    # placeholder regex must accept dtk placeholders and reject real names
    pos = ['fn_8263A360', 'lbl_830A1AF4', 'fn_82EB0C70']
    neg = ['?SaveVertices@RndMesh@@UAAXAAVBinStream@@@Z', 'memcpy', '__savegprlr_25',
           'fn_helper', 'lbl_notahex']
    ok = all(PLACEHOLDER.match(x) for x in pos) and not any(PLACEHOLDER.match(x) for x in neg)
    print(f'  [{"PASS" if ok else "FAIL"}] placeholder regex: '
          f'{sum(bool(PLACEHOLDER.match(x)) for x in pos)}/{len(pos)} accepted, '
          f'{sum(bool(PLACEHOLDER.match(x)) for x in neg)}/{len(neg)} wrongly accepted')
    if not ok:
        return 5

    # bare-blr detector
    if not (is_bare_blr(struct.pack('>I', BLR)) and not is_bare_blr(struct.pack('>I', 0x60000000))
            and not is_bare_blr(struct.pack('>I', BLR) * 2)):
        print('  [FAIL] bare-blr detector')
        return 5
    print('  [PASS] bare-blr detector: blr yes, nop no, 8 bytes no')

    units = json.load(open(root / 'objdiff.json'))['units']
    bidx, blrset = build_base_size_index(sc, units)
    if not bidx:
        print('VACUOUS: no base objects. The tree is not built; every query would')
        print('return nothing for the wrong reason. Exit 9.')
        return 9
    print(f'  [PASS] base size index: {len(bidx)} defined functions, '
          f'{len(blrset)} of them a bare blr')

    # ---- relocation accounting: the scan must see EVERY call relocation -----
    tot, drops = reloc_accounting(sc, units, limit=120)
    bt_, ba_ = tot['base_path.total'], tot['base_path.attributed']
    okr = bt_ > 0 and bt_ == ba_
    print(f'  [{"PASS" if okr else "FAIL"}] reloc accounting (120-unit sample): base '
          f'{ba_}/{bt_} REL24 attributed, target '
          f'{tot["target_path.attributed"]}/{tot["target_path.total"]}')

    st, ha, hb, bt = sc.scan(bidx)

    # ---- the three control populations -------------------------------------
    fails = []
    if not okr:
        fails.append('the scan did not attribute every base-side REL24 to a function; call '
                     'sites are being dropped silently and any null result understates')
    for label, n, why in (
            ('agreeing sites', st['ctl_agree'],
             'ordinal pairing is broken; no null result from this run means anything'),
            ('retail-named sites', st['ctl_retail_named'],
             'every retail destination reads unnamed: the placeholder test or the '
             'target symbol read is wrong'),
            ('both-trivial sites', st['ctl_both_trivial'],
             'the size lookup found no matched trivial pair, so Query B cannot '
             'distinguish "no candidates" from "no sizes"')):
        okc = n > 0
        print(f'  [{"PASS" if okc else "FAIL"}] control population {label:22} = {n}')
        if not okc:
            fails.append(f'{label} is EMPTY -- {why}')

    # ---- synthetic negative control ----------------------------------------
    # take a real agreeing site and repoint our side at a decoy the maps do not
    # know; the classifier must stop calling it agreeing.
    decoy = '?ThisSymbolDoesNotExist@Decoy@@QAAXXZ'
    real = None
    for u in units:
        bp, tp = u.get('base_path'), u.get('target_path')
        if not (bp and tp):
            continue
        b, t = sc.obj(bp), sc.obj(tp)
        if not b or not t:
            continue
        for fn in (set(b['calls']) & set(t['calls'])) & b['defined'] & t['defined']:
            bl, tl = b['calls'][fn], t['calls'][fn]
            if len(bl) != len(tl):
                continue
            for (bo, bn), (to, tn) in zip(bl, tl):
                if sc.key(bn) == sc.key(tn) and not PLACEHOLDER.match(tn):
                    real = (u['name'], fn, bn, tn)
                    break
            if real:
                break
        if real:
            break
    if real is None:
        fails.append('no agreeing named site found to build the negative control from')
    else:
        un, fn, bn, tn = real
        convicted = sc.key(decoy) != sc.key(tn)
        still_ok = sc.key(bn) == sc.key(tn)
        print(f'  [{"PASS" if convicted and still_ok else "FAIL"}] negative control: '
              f'{un} :: {fn[:40]} -- real callee agrees, decoy convicted')
        if not (convicted and still_ok):
            fails.append('the classifier cannot tell a known-wrong callee from a known-right '
                         'one; a clean sweep from it would be meaningless')

    if fails:
        print('\nSELFTEST FAILED -- refusing to present this scan as a measurement.')
        for f in fails:
            print('  ' + f)
        return 5
    print(f'\nSELFTEST OK. Scan reached {st["unit_ok"]} unit pairs / {st["site"]} paired sites.')
    return 0


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', default='.')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--trivial-bytes', type=int, default=8)
    ap.add_argument('--size-ratio', type=float, default=3.0)
    ap.add_argument('--json-out')
    ap.add_argument('--show', type=int, default=40)
    ap.add_argument('--reloc-accounting', action='store_true',
                    help='whole-tree REL24 present-vs-attributed audit and exit')
    ap.add_argument('--empty-extrn', action='store_true',
                    help='Query C only: the empty-EXTRN population with its control')
    args = ap.parse_args()
    root = Path(args.root).resolve()

    if args.selftest:
        return selftest(root)
    rc = selftest(root)
    if rc:
        return rc

    sc = Scanner(root, args.trivial_bytes, args.size_ratio)
    units = json.load(open(root / 'objdiff.json'))['units']
    bidx, blrset = build_base_size_index(sc, units)
    if args.reloc_accounting:
        tot, drops = reloc_accounting(sc, units)
        print(json.dumps(dict(tot), indent=1))
        print(f'objects with unattributed REL24: {len(drops)}')
        for d in drops[:args.show]:
            print(f'  -{d[0]:4} {d[1]:46} {d[2]:12} {d[4]}/{d[3]}')
        return 0 if tot['base_path.total'] == tot['base_path.attributed'] else 1
    if args.empty_extrn:
        return report_query_c(query_c(sc, units, blrset), args.show)
    st, ha, hb, bt = sc.scan(bidx)

    print('\n===== DENOMINATOR =====')
    for k in sorted(st):
        print(f'  {k:28} {st[k]}')
    print(f'\n===== QUERY A: retail destination UNNAMED, ours named  ({len(ha)}) =====')
    ha.sort(key=lambda h: -(h['retail_size'] or 0))
    for h in ha[:args.show]:
        print(f"  {h['unit']:40} {h['fn'][:46]:46} +0x{h['off']:<5x} "
              f"ours={h['ours'][:44]} ({h['our_size']}B)  retail={h['retail']} "
              f"({h['retail_size']}B)")
    print(f'\n===== QUERY B: our callee trivial, retail destination larger  ({len(hb)}) =====')
    print('  AGREE rows are the SAME symbol: our body is a stub (mostly `default/link_glue`,')
    print('  the unimplemented-CRT placeholders) where retail has the real implementation.')
    print('  That is a missing-body class, not a wrong callee. DIFFER rows are the leads --')
    print('  and read them carefully, because ordinal pairing mis-aligns when MSVC places')
    print('  the two sides\' blocks in a different order.')
    hb.sort(key=lambda h: -(h['retail_size'] or 0))
    for h in hb[:args.show]:
        print(f"  {h['unit']:40} {h['fn'][:40]:40} ours={h['ours'][:40]:40} "
              f"{h['our_size']}B -> retail={h['retail'][:40]} {h['retail_size']}B "
              f"{'AGREE' if h['agree'] else 'DIFFER'}")
    print(f'\ncontrol: {st["ctl_both_trivial"]} both-trivial matched pairs '
          f'(the population with nothing to see)')
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(
            dict(stats=dict(st), query_a=ha, query_b=hb,
                 both_trivial=[list(x) for x in bt[:2000]],
                 bare_blr_callees=sorted(blrset)), indent=1))
        print(f'wrote {args.json_out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
