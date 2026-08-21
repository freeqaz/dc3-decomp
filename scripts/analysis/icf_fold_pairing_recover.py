#!/usr/bin/env python3
"""icf_fold_pairing_recover.py -- the /OPT:ICF pairing-artifact class, enumerated
from a QUERY rather than a worklist, adjudicated on the TARGET's own bytes, and
recovered only where the map's contributing-.obj column agrees.

Task #114 (2026-08-21), dc3-decomp (title 373307D9).  Sibling repos ../rb3 and
../rb3-xenon share symbol names and address ranges; every number here is dc3's.

WHAT THIS IS FOR
----------------
`/OPT:ICF` folds byte-identical COMDATs to one address.  `ham_xbox_r.map`
co-lists every folded member there together with the .obj that contributed it.
dtk's splitter writes ONE of those names into `config/373307D9/symbols.txt`.
objdiff pairs target->base BY NAME within a unit, so if our object emits a
DIFFERENT member of the same fold class, the row reads 0% while our bytes are
already correct and already present at that address.

Task #112 recovered the sub-class where dtk had NO name at all and wrote a
synthesised `merged_<addr>` placeholder (48 rows).  That sub-class is now
essentially empty.  This tool addresses the REMAINDER: rows where dtk DID write
a real mangled name, but the wrong member of the fold class.

A SECOND, DIFFERENT RELATIONSHIP LIVES IN THE SAME POPULATION
-------------------------------------------------------------
`??_E<T>` (vector deleting dtor) is emitted by MSVC as an UNDEFINED WEAK
EXTERNAL whose aux record's TagIndex names `??_G<T>`.  It is an ALIAS, not a
fold: the address holds `??_G`'s COMDAT and `??_E` resolves onto it.  Those rows
are tiered separately (`ALIAS_E_TO_G`) and the aux record is READ, not assumed.
(Correction to `scripts/symbol_aliases.json`'s `_comment`, which calls this
`SEARCH_ALIAS`: MSVC emits Characteristics **2** = IMAGE_WEAK_EXTERN_SEARCH_
LIBRARY here, not 3.  The load-bearing field is TagIndex, which does name
`??_G<T>`.)

THE GATES (all must hold before a name is installed)
----------------------------------------------------
1. the row scores 0.0 `match_percent_normalized` in `build/373307D9/report.json`;
2. `symbols.txt` places its name at address A and the shipped map co-lists >= 2
   names at A;
3. our unit's built object does NOT define the split name but DOES define some
   other member M of that class;
4. M passes the STRICT body test of `icf_survivor_names.py` against the
   target's own bytes at A -- equal length, equal relocated-offset set, equal
   relocation TARGET NAMES modulo fold-equivalence, and every non-relocated word
   byte-equal (internal branch displacements included);
5. the map attributes M to THIS unit's own .obj (the contributing-.obj tiebreak
   from docs/analysis/comdat-tier2-triage-20260819.md -- the only choice stable
   under a re-split);
6. M is absent from `symbols.txt` and from `report.json`, so installing it
   cannot collide with a name something else already scores.

Gate 5 is the one that refuses "rename anything that happens to match".  Gate 6
is the one that refuses moving a name onto a row another unit already owns.

Read-only unless --apply.
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, 'scripts'))
from analysis.coffx import read_coff  # noqa: E402
from analysis.icf_survivor_names import (  # noqa: E402
    read_map, name_addresses, target_body, our_body, defined_names, body_test,
    SYMLINE_RE)

IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105


# ------------------------------------------------------- COFF weak externals

def weak_externals(path, _cache={}):
    """name -> (aux TagIndex's symbol name, Characteristics) for weak externals.

    MSVC emits `??_E<T>` as an undefined weak external aliasing `??_G<T>`.  The
    relationship is stated by the aux record, so it is READ here rather than
    inferred from the `??_E`/`??_G` name shape -- a name shape is an argument,
    not a witness (scripts/symbol_aliases.json's _comment).
    """
    if path in _cache:
        return _cache[path]
    out = {}
    try:
        data = open(path, 'rb').read()
        _m, _n, _t, symoff, nsym, _o, _c = struct.unpack_from('<HHIIIHH', data, 0)
        strtab = symoff + nsym * 18
        names, recs, i = {}, [], 0
        while i < nsym:
            off = symoff + i * 18
            nb = data[off:off + 8]
            if nb[:4] == b'\x00\x00\x00\x00':
                a = strtab + struct.unpack_from('<I', nb, 4)[0]
                e = data.find(b'\0', a)
                name = data[a:e].decode('ascii', 'replace')
            else:
                name = nb.split(b'\x00')[0].decode('ascii', 'replace')
            naux = data[off + 17]
            names[i] = name
            recs.append((i, name, data[off + 16], naux))
            i += 1 + naux
        for (idx, name, cls, naux) in recs:
            if cls == IMAGE_SYM_CLASS_WEAK_EXTERNAL and naux >= 1:
                tag, chars = struct.unpack_from('<II', data, symoff + (idx + 1) * 18)
                out[name] = (names.get(tag, '?'), chars)
    except Exception:
        out = {}
    _cache[path] = out
    return out


# ------------------------------------------------------------- the population

def load_symbols_txt(project):
    sym2addr, dup = {}, collections.Counter()
    p = os.path.join(project, 'config', '373307D9', 'symbols.txt')
    for line in open(p, errors='replace'):
        m = SYMLINE_RE.match(line)
        if m:
            sym2addr.setdefault(m.group(1), int(m.group(2), 16))
            dup[m.group(1)] += 1
    return sym2addr, dup


def enumerate_population(project, addr2, sym2addr, cov):
    """Every 0%-scoring report.json row, filtered down to the ICF pairing class.

    Every drop is counted and named; the caller prints the denominator.
    """
    rep = json.load(open(os.path.join(project, 'build/373307D9/report.json')))
    repnames = collections.Counter()
    rows = []
    for u in rep['units']:
        for f in (u.get('functions') or []):
            repnames[f['name']] += 1
            if f.get('match_percent_normalized') == 0.0:
                rows.append((u['name'], f['name'], int(f['size'])))
    cov['universe: report.json functions'] = (
        sum(len(u.get('functions') or []) for u in rep['units']), 0)
    cov['examined: rows at 0.0 normalized'] = (len(rows), sum(r[2] for r in rows))

    out = []
    for unit, name, size in rows:
        rel = unit.replace('default/', '', 1)
        objp = os.path.join(project, 'build/373307D9/src', rel + '.obj')
        addr = sym2addr.get(name)
        if addr is None:
            cov.drop('not in symbols.txt (lib/xdk row named by the splitter only)',
                     size)
            continue
        members = addr2.get(addr)
        if not members:
            cov.drop('address absent from the shipped linker map', size)
            continue
        if len(members) < 2:
            cov.drop('not a fold class: the map lists ONE name at this address',
                     size)
            continue
        if not os.path.exists(objp):
            cov.drop('no built object for the unit (xdk/ or lib/, not decompiled)',
                     size)
            continue
        ours = defined_names(objp)
        if name in ours:
            cov.drop('our object DOES define this name: a real 0% code gap, '
                     'not a pairing artifact', size)
            continue
        if not any(m[0] in ours for m in members if m[0] != name):
            cov.drop('our object defines no other member of the fold class', size)
            continue
        out.append(dict(unit=unit, split_name=name, size=size,
                        address='0x%08X' % addr))
    return out, repnames


class Coverage:
    def __init__(self):
        self.stages = collections.OrderedDict()
        self.drops = collections.Counter()
        self.dropb = collections.Counter()

    def __setitem__(self, k, v):
        self.stages[k] = v

    def drop(self, why, size):
        self.drops[why] += 1
        self.dropb[why] += size

    def report(self):
        print('DENOMINATOR')
        for k, (n, b) in self.stages.items():
            print('  %-58s %6d rows %9d B' % (k, n, b))
        print('  dropped:')
        for k, n in self.drops.most_common():
            print('    %-56s %6d rows %9d B' % (k, n, self.dropb[k]))


# ---------------------------------------------------------------- adjudicate

def owns(mapobj, unit):
    own = os.path.basename(unit.replace('default/', '', 1))
    return mapobj.rsplit(':', 1)[-1].rsplit('.obj', 1)[0] == own


def adjudicate(project, row, addr2, naddr, sym2addr, repnames):
    addr = int(row['address'], 16)
    rel = row['unit'].replace('default/', '', 1)
    objp = os.path.join(project, 'build/373307D9/src', rel + '.obj')
    asmp = os.path.join(project, 'build/373307D9/asm', rel + '.s')
    out = dict(row)
    members = addr2[addr]
    out['map_members'] = [{'sym': s, 'flags': f, 'obj': o} for s, f, o in members]
    mm = {s: o for s, _f, o in members}
    out['split_name_map_obj'] = mm.get(row['split_name'])
    out['split_name_unit_owned'] = owns(mm.get(row['split_name'], '<none>'),
                                        row['unit'])

    tbody, trefs = target_body(asmp, row['split_name'])
    if tbody is None:
        out['verdict'] = 'REFUSE_NO_TARGET_ASM'
        out['why'] = 'no %s block in build/373307D9/asm/%s.s' % (row['split_name'], rel)
        return out
    out['target_size'] = len(tbody)

    ours = defined_names(objp)
    cands = [(s, f, o) for (s, f, o) in members if s != row['split_name'] and s in ours]
    # gate 5 first: the contributing-.obj tiebreak decides the ORDER, and a
    # candidate the map attributes elsewhere can never be installed.
    cands.sort(key=lambda c: (not owns(c[2], row['unit']), c[0]))

    tried, passers = [], []
    for (s, _f, o) in cands:
        obody, orefs = our_body(objp, s)
        if obody is None:
            tried.append({'sym': s, 'result': 'no COMDAT body in our object'})
            continue
        ok, why, nrel = body_test(tbody, trefs, obody, orefs, naddr)
        tried.append({'sym': s, 'map_obj': o, 'unit_owned': owns(o, row['unit']),
                      'result': why, 'nreloc': nrel})
        if ok:
            passers.append((s, o, nrel))
    out['tried'] = tried
    out['passers'] = [{'sym': s, 'map_obj': o, 'nreloc': n,
                       'unit_owned': owns(o, row['unit'])} for s, o, n in passers]

    if not passers:
        out['verdict'] = 'REFUSE_BODY_DIFFERS'
        out['why'] = ('every fold member our object defines fails the body test '
                      'against the target bytes at this address')
        return out

    owned = [p for p in passers if owns(p[1], row['unit'])]
    if not owned:
        out['verdict'] = 'REFUSE_TIEBREAK_FAILS'
        out['why'] = ('gate 5: the body test passes, but the map attributes every '
                      'passing member to another .obj (%s), so this address is not '
                      'this unit\'s to name' % ', '.join(sorted({p[1] for p in passers})))
        return out

    name, mapobj, nrel = owned[0]
    out['name'] = name
    out['map_obj'] = mapobj
    out['nreloc'] = nrel
    out['unit_owned'] = True
    out['fold_twins_in_unit'] = [p[0] for p in owned[1:]]
    out['arbitrary_among_equals'] = len(owned) > 1

    if name in sym2addr:
        out['verdict'] = 'REFUSE_NAME_CLASH_SYMBOLS_TXT'
        out['why'] = ('gate 6: symbols.txt already places %s at 0x%08X'
                      % (name, sym2addr[name]))
        return out
    if repnames.get(name):
        out['verdict'] = 'REFUSE_NAME_CLASH_REPORT'
        out['why'] = 'gate 6: report.json already carries a row named %s' % name
        return out

    we = weak_externals(objp).get(row['split_name'])
    out['weak_external_aux'] = list(we) if we else None
    alias = bool(we and we[0] == name)
    out['alias_of_chosen'] = alias

    if nrel > 0:
        out['verdict'] = 'ALIAS_E_TO_G' if alias else 'PROVEN_BODY'
    else:
        out['verdict'] = 'ALIAS_E_TO_G_WEAK' if alias else 'WEAK_NO_RELOC'
    out['why'] = (
        ('the COFF aux record of the weak external %s names %s (Characteristics '
         '%d): the address holds %s\'s COMDAT and the split name is its alias; '
         % (row['split_name'], we[0], we[1], name) if alias else '') +
        ('body test: %d B identical, %d relocations agreeing by target name'
         % (len(tbody), nrel) if nrel else
         'body test passes but the body has ZERO relocations, so byte-identity '
         'does not discriminate between fold members; installed on the map alone')
    )
    return out


INSTALL = ('PROVEN_BODY', 'ALIAS_E_TO_G')
INSTALL_WEAK = ('WEAK_NO_RELOC', 'ALIAS_E_TO_G_WEAK')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default=REPO)
    ap.add_argument('--json')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--include-weak', action='store_true')
    args = ap.parse_args()

    project = os.path.abspath(args.project)
    addr2 = read_map(project)
    naddr = name_addresses(project, addr2)
    sym2addr, dup = load_symbols_txt(project)
    cov = Coverage()
    pop, repnames = enumerate_population(project, addr2, sym2addr, cov)
    cov['CANDIDATE population (the ICF pairing class)'] = (
        len(pop), sum(r['size'] for r in pop))
    cov.report()
    print()

    res = [adjudicate(project, r, addr2, naddr, sym2addr, repnames) for r in pop]
    tally = collections.Counter(r['verdict'] for r in res)
    print('ADJUDICATION')
    for k, n in tally.most_common():
        b = sum(r['size'] for r in res if r['verdict'] == k)
        print('  %-32s %4d rows %8d B' % (k, n, b))
    arb = [r for r in res if r.get('arbitrary_among_equals')]
    print('  (%d of the installable rows have >1 unit-owned passer -- '
          'arbitrary among equals, resolved by sorting the names)' % len(arb))
    print()

    if args.verbose:
        for r in res:
            mark = {'PROVEN_BODY': '++', 'ALIAS_E_TO_G': '=>',
                    'WEAK_NO_RELOC': ' ~', 'ALIAS_E_TO_G_WEAK': ' ='}.get(
                        r['verdict'], '!!')
            print('%s %s %5dB %-46s' % (mark, r['address'], r['size'], r['unit']))
            print('      split  %s   [%s]' % (r['split_name'], r.get('split_name_map_obj')))
            if r.get('name'):
                print('      name   %s   [%s]' % (r['name'], r.get('map_obj')))
            print('      %s: %s' % (r['verdict'], r['why']))

    if args.json:
        json.dump(res, open(args.json, 'w'), indent=1)
        print('wrote %s' % args.json)

    if args.apply:
        want = {}
        for r in res:
            if r['verdict'] in INSTALL or (args.include_weak
                                           and r['verdict'] in INSTALL_WEAK):
                if dup[r['split_name']] != 1:
                    print('REFUSING %s: %d symbols.txt lines carry this name'
                          % (r['split_name'], dup[r['split_name']]))
                    return 1
                want[r['split_name']] = r['name']
        if len(set(want.values())) != len(want):
            print('REFUSING to write: two rows chose the same replacement name')
            return 1
        p = os.path.join(project, 'config', '373307D9', 'symbols.txt')
        lines = open(p, errors='replace').read().splitlines(True)
        done = 0
        for i, line in enumerate(lines):
            m = SYMLINE_RE.match(line)
            if m and m.group(1) in want:
                lines[i] = line.replace(m.group(1), want[m.group(1)], 1)
                done += 1
        if done != len(want):
            print('REFUSING to write: matched %d lines, wanted %d' % (done, len(want)))
            return 1
        open(p, 'w').write(''.join(lines))
        print('rewrote %d names in %s' % (done, p))
        print('PREDICTED whole-build effect: +%d matched functions, +%d bytes'
              % (done, sum(r['size'] for r in res
                           if r['verdict'] in INSTALL
                           or (args.include_weak and r['verdict'] in INSTALL_WEAK))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
