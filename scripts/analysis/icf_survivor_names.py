#!/usr/bin/env python3
"""icf_survivor_names.py -- recover the real name of an /OPT:ICF fold survivor
that `config/373307D9/symbols.txt` currently spells `merged_<addr>`, and prove
the recovery with a BODY TEST rather than by lifting a name from a stale list.

The class this answers
----------------------
Retail DC3 was linked with MSVC /OPT:ICF.  Where several byte-identical COMDATs
fold to one address, `orig/373307D9/ham_xbox_r.map` co-lists every member at
that address together with the .obj that contributed it.  dtk's splitter did not
consume the map for these addresses, so `symbols.txt` carries a synthesised
`merged_<addr>` / `merged_<Shape>` placeholder.  objdiff pairs target->base BY
NAME within a unit; our object defines a real mangled name there and the target
side is called `merged_...`, so the row scores 0% while our bytes are correct
and already present.

Why the existing `recoverable_name` field is not admissible
-----------------------------------------------------------
`docs/analysis/report-absent-rows-20260818/recoverable-merged-names.json` carries
a `recoverable_name` per address.  It is a STALE decomp.db spelling, not a
map-derived choice, and it is demonstrably the WRONG fold member for several
rows -- e.g. at 0x8235E578, in unit `system/char/CharBoneOffset`, it names
`?Handle@PhotoSpotlightPositioner@@` while the map's member from
`char:CharBoneOffset.obj` is `?Handle@CharBoneOffset@@`.  This script ignores
that field entirely and re-derives every candidate from the map.

The body test (the actual witness)
----------------------------------
The split asm `build/373307D9/asm/<unit>.s` prints, for every instruction, both
the LINKED bytes and the operand's SYMBOL NAME (`bl "?Sym@DataArray@@..."`,
`lis r11, "?sActive@MessageTimer@@1_NA"@ha`).  So the target's relocation TARGET
NAMES are directly readable, and the test does not have to blind itself to
branch displacements the way `icf_pairing_bodytest.py` does.  For a candidate
name N in our object's COMDAT:

  * lengths must be equal;
  * the set of relocated offsets must be equal on both sides;
  * at every relocated offset the two sides must name the SAME callee/datum
    (modulo fold-equivalence: two names sharing an address in the retail map
    are the same thing to the linker);
  * at every NON-relocated offset the bytes must be equal EXACTLY -- including
    internal branch displacements, which are self-relative and therefore
    directly comparable.

That last clause is what `icf_pairing_bodytest.py` gives up (it masks every
b/bc), and it is what makes a pass here mean "our body IS the body at that
address" rather than "our body is the same shape".

THE CHEAPNESS GUARD
-------------------
A body with ZERO relocations is not evidence: `{ return 0; }` is 8 identical
bytes and every such stub in the image is byte-identical to every other.  Those
rows are reported as WEAK_NO_RELOC and are NOT certified on bytes; they are
adjudicated on the map alone (the linker's own statement that name N is at that
address AND that N's contributing .obj is this unit's .obj) and reported in a
separate tier so a reader can discount them independently.

Read-only.  Writes nothing.
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
from analysis.coffx import read_coff, infer_sizes  # noqa: E402

# ---------------------------------------------------------------- retail map

MAP_RE = re.compile(
    r'^\s+(\d{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(f\s*i?)\s+(\S+)\s*$')


def read_map(project):
    """addr(int) -> [(symbol, flags, contributing_obj)]"""
    addr2 = collections.defaultdict(list)
    path = os.path.join(project, 'orig', '373307D9', 'ham_xbox_r.map')
    for line in open(path, errors='replace'):
        m = MAP_RE.match(line)
        if not m:
            continue
        _, _, sym, addr, flags, obj = m.groups()
        addr2[int(addr, 16)].append((sym, flags.strip(), obj))
    return addr2


SYMLINE_RE = re.compile(r'^(\S+) = \.\w+:0x([0-9A-Fa-f]+);')


def name_addresses(project, addr2):
    """symbol -> address, from the retail map AND config/373307D9/symbols.txt.

    Two names are the same thing to the linker iff they land on one address.
    symbols.txt is needed as well as the map because the TARGET side of a
    relocation may itself be a `merged_<addr>` / `merged_<Shape>` placeholder
    (an ICF survivor dtk has not named either) -- comparing that to our real
    spelling by string would refute a pair the linker itself folded.
    """
    out = {}
    for addr, members in addr2.items():
        for s, _f, _o in members:
            out.setdefault(s, addr)
    p = os.path.join(project, 'config', '373307D9', 'symbols.txt')
    for line in open(p, errors='replace'):
        m = SYMLINE_RE.match(line)
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


# ------------------------------------------------------------- target side

# /* ADDR FILEOFF  AA BB CC DD */\tmnemonic operands
INSN_RE = re.compile(
    r'^/\* ([0-9A-F]{8}) [0-9A-F]+  ((?:[0-9A-F]{2} ){3}[0-9A-F]{2}) \*/\s*(.*?)\s*$')
# a quoted mangled name, or a bare C identifier, optionally @ha/@l/@h
REF_RE = re.compile(r'"([^"]+)"(@ha|@l|@h)?|(?<![\w.$@])([A-Za-z_][\w$@]*)(@ha|@l|@h)?')

BRANCH_MNEMONICS = ('b', 'bl', 'ba', 'bla')

REL_PPC_PAIR = 0x12   # IMAGE_REL_PPC_PAIR


def target_body(asmpath, sym):
    """(bytes, {offset: refname}) for `sym` in dtk's split listing.

    A ref is recorded only where the operand names something OUTSIDE the body
    (a quoted mangled symbol, or a bare identifier such as `__savegprlr_24`).
    `.L_xxxxxxxx` local labels are internal and deliberately NOT refs, so their
    displacements stay under exact byte comparison.
    """
    if not os.path.exists(asmpath):
        return None, None
    body, refs = [], {}
    inside = False
    base = None
    for line in open(asmpath, errors='replace'):
        st = line.strip()
        if not inside:
            # `.fn merged_X, global`  or  `.fn "?Name@@...", global`
            if st.startswith('.fn '):
                rest = st[4:].split(',')[0].strip()
                if rest.startswith('"') and rest.endswith('"'):
                    rest = rest[1:-1]
                if rest == sym:
                    inside = True
            continue
        if st.startswith('.endfn'):
            break
        m = INSN_RE.match(st)
        if not m:
            continue
        addr = int(m.group(1), 16)
        if base is None:
            base = addr
        off = addr - base
        body.append(bytes.fromhex(m.group(2).replace(' ', '')))
        text = m.group(3)
        # strip the mnemonic; look for a symbol reference in the operands
        parts = text.split(None, 1)
        if len(parts) < 2:
            continue
        mnem, ops = parts[0], parts[1]
        for mm in REF_RE.finditer(ops):
            name = mm.group(1) or mm.group(3)
            if not name:
                continue
            if name.startswith('.L_') or re.fullmatch(r'r\d+|f\d+|cr\d+|sp|rtoc', name):
                continue
            if mm.group(1) is None:
                # bare identifier: only trust it in a branch slot or with @ha/@l
                if mnem not in BRANCH_MNEMONICS and not mm.group(4):
                    continue
            refs[off] = name
            break
    if not body:
        return None, None
    return b''.join(body), refs


# ---------------------------------------------------------------- our side

def our_body(objpath, sym, _cache={}):
    """(bytes, {offset: relocated_target_name}) for `sym`'s COMDAT in our object."""
    if objpath not in _cache:
        data = open(objpath, 'rb').read()
        secs, syms = read_coff(data)
        if secs is None:
            _cache[objpath] = None
        else:
            infer_sizes(secs, syms)
            _cache[objpath] = (secs, syms)
    got = _cache[objpath]
    if got is None:
        return None, None
    secs, syms = got
    hit = None
    for s in syms:
        if s.name == sym and s.sec > 0:
            hit = s
            break
    if hit is None or hit.size == 0:
        return None, None
    sec = secs[hit.sec - 1]
    start, end = hit.value, hit.value + hit.size
    if end > len(sec.data):
        return None, None
    body = sec.data[start:end]
    byidx = {s.index: s.name for s in syms}
    relocs = {}
    for (va, symidx, typ) in sec.relocs:
        # IMAGE_REL_PPC_PAIR (0x12) carries the REFHI/REFLO displacement in the
        # SymbolTableIndex field -- it is NOT a symbol index.  Reading it as one
        # resolves to symbol 0 (`@comp.id`) and, because it shares the offset of
        # the REFHI it pairs with, silently OVERWRITES the real target name.
        # That produced 6 false "reloc names X vs ours @comp.id" refusals.
        if typ == REL_PPC_PAIR:
            continue
        if start <= va < end:
            relocs[va - start] = byidx.get(symidx, '?')
    return body, relocs


def defined_names(objpath, _cache={}):
    if objpath not in _cache:
        if not os.path.exists(objpath):
            _cache[objpath] = set()
        else:
            secs, syms = read_coff(open(objpath, 'rb').read())
            _cache[objpath] = set() if secs is None else {
                s.name for s in syms if s.sec > 0}
    return _cache[objpath]


# ------------------------------------------------------------------- verdict

def same_thing(a, b, naddr):
    """Fold-equivalence: same string, or both names land on one address."""
    if a == b:
        return True
    aa, bb = naddr.get(a), naddr.get(b)
    return aa is not None and aa == bb


def body_test(tbody, trefs, obody, orefs, naddr):
    """-> (ok, reason, nreloc)"""
    if len(tbody) != len(obody):
        return False, 'size %d vs %d' % (len(tbody), len(obody)), len(orefs)
    if set(trefs) != set(orefs):
        only_t = sorted(set(trefs) - set(orefs))
        only_o = sorted(set(orefs) - set(trefs))
        return False, 'reloc offsets differ (target-only %s, ours-only %s)' % (
            ['0x%x' % o for o in only_t[:4]], ['0x%x' % o for o in only_o[:4]]), len(orefs)
    for off, tn in trefs.items():
        on = orefs[off]
        if not same_thing(tn, on, naddr):
            return False, 'reloc at 0x%x names %s vs ours %s' % (off, tn, on), len(orefs)
    for i in range(0, len(tbody), 4):
        if i in trefs:
            continue
        if tbody[i:i + 4] != obody[i:i + 4]:
            return False, 'byte mismatch at 0x%x: %s vs %s' % (
                i, tbody[i:i + 4].hex(), obody[i:i + 4].hex()), len(orefs)
    return True, 'ok', len(orefs)


def adjudicate(project, row, addr2, naddr):
    addr = int(row['address'], 16)
    unit = row['unit']
    rel = unit.replace('default/', '', 1)
    objp = os.path.join(project, 'build/373307D9/src', rel + '.obj')
    asmp = os.path.join(project, 'build/373307D9/asm', rel + '.s')
    out = dict(row)
    out['map_members'] = [{'sym': s, 'flags': f, 'obj': o} for s, f, o in addr2.get(addr, [])]
    if not addr2.get(addr):
        out['verdict'] = 'REFUSE_NOT_IN_MAP'
        out['why'] = 'the shipped linker map names no symbol at this address'
        return out
    if not os.path.exists(objp):
        out['verdict'] = 'REFUSE_NO_OBJECT'
        out['why'] = 'no built object for %s' % unit
        return out
    ours = defined_names(objp)
    cands = [(s, f, o) for (s, f, o) in addr2[addr] if s in ours]
    if not cands:
        out['verdict'] = 'REFUSE_WE_DEFINE_NO_MEMBER'
        out['why'] = ('our %s.obj defines none of the %d fold members the map '
                      'lists here' % (rel, len(addr2[addr])))
        return out
    tbody, trefs = target_body(asmp, row['split_name'])
    if tbody is None:
        out['verdict'] = 'REFUSE_NO_TARGET_ASM'
        out['why'] = 'no %s block in %s.s' % (row['split_name'], rel)
        return out
    out['target_size'] = len(tbody)

    # ORDER THE CANDIDATES BEFORE TESTING.  Several fold sets have more than one
    # member our object defines, and they are byte-identical BY CONSTRUCTION --
    # that is what /OPT:ICF folding means -- so the body test cannot choose
    # between them and map order is not a reason.  Prefer the member the map
    # says was contributed by THIS unit's own .obj: that is the same rule
    # docs/analysis/comdat-tier2-triage-20260819.md derived for dtk's splitter,
    # and it is the only choice that is stable under a re-split.
    own = os.path.basename(rel)

    def owns(o):
        # map spells it `lib:Basename.obj` (or bare `Basename.obj` for App)
        return o.rsplit(':', 1)[-1].rsplit('.obj', 1)[0] == own

    cands.sort(key=lambda c: (not owns(c[2]), c[0]))
    out['candidates'] = [{'sym': s, 'obj': o, 'unit_owned': owns(o)} for s, _f, o in cands]

    tried = []
    for (s, f, o) in cands:
        obody, orefs = our_body(objp, s)
        if obody is None:
            tried.append({'sym': s, 'result': 'no COMDAT body in our object'})
            continue
        ok, why, nrel = body_test(tbody, trefs, obody, orefs, naddr)
        tried.append({'sym': s, 'map_obj': o, 'result': why, 'nreloc': nrel})
        if ok:
            twins = []
            for (s2, _f2, o2) in cands:
                if s2 == s:
                    continue
                b2, r2 = our_body(objp, s2)
                if b2 is not None and body_test(tbody, trefs, b2, r2, naddr)[0]:
                    twins.append(s2)
            out['name'] = s
            out['map_obj'] = o
            out['unit_owned'] = owns(o)
            out['nreloc'] = nrel
            out['fold_twins_in_unit'] = twins
            out['verdict'] = 'PROVEN_BODY' if nrel > 0 else 'WEAK_NO_RELOC'
            out['why'] = ('body test: %d B identical, %d relocations agreeing by '
                          'target name' % (len(tbody), nrel)) if nrel else (
                          'body test passes but the body has ZERO relocations, so '
                          'byte-identity does not discriminate between fold members')
            out['tried'] = tried
            return out
    out['verdict'] = 'REFUSE_BODY_DIFFERS'
    out['why'] = 'every map member our object defines fails the body test'
    out['tried'] = tried
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default=REPO)
    ap.add_argument('--rows', default=os.path.join(
        REPO, 'docs/analysis/report-absent-rows-20260818/recoverable-merged-names.json'))
    ap.add_argument('--json', help='write the full adjudication here')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()

    project = os.path.abspath(args.project)
    addr2 = read_map(project)
    naddr = name_addresses(project, addr2)
    rows = json.load(open(args.rows))

    res = [adjudicate(project, r, addr2, naddr) for r in rows]
    tally = collections.Counter(r['verdict'] for r in res)
    print('rows: %d   bytes: %d' % (len(res), sum(r['size'] for r in res)))
    for k, n in tally.most_common():
        b = sum(r['size'] for r in res if r['verdict'] == k)
        print('  %-28s %3d rows  %6d B' % (k, n, b))
    print()
    for r in res:
        mark = {'PROVEN_BODY': '++', 'WEAK_NO_RELOC': ' ~'}.get(r['verdict'], '!!')
        print('%s %-11s %5dB %-44s %s' % (mark, r['address'], r['size'], r['unit'],
                                          r['split_name']))
        if r.get('name'):
            print('      name  %s' % r['name'])
            print('      map   %s' % r.get('map_obj'))
        print('      %s: %s' % (r['verdict'], r['why']))
        if args.verbose and r.get('tried'):
            for t in r['tried']:
                print('        tried %s -> %s' % (t['sym'][:70], t['result']))
        # a stale-name cross-check, reported but never used as evidence
        if r.get('name') and r.get('recoverable_name') and r['name'] != r['recoverable_name']:
            print('      NOTE stale census name disagreed: %s' % r['recoverable_name'][:90])

    if args.json:
        json.dump(res, open(args.json, 'w'), indent=1)
        print('\nwrote %s' % args.json)


if __name__ == '__main__':
    main()
