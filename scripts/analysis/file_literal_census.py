#!/usr/bin/env python3
"""Census of __FILE__-literal disagreements: which TU/file the target attributes an
assert to, versus which one we do.

Mechanism this hunts (task #111).  ``MILO_ASSERT`` bakes ``__FILE__`` into a
``??_C@`` string COMDAT.  MSVC spells ``__FILE__`` exactly the way the file was
reached: a ``.cpp`` named on the command line comes out as a bare basename
(``CharBones.cpp``), while a header reached through ``/I 'e:\\lazer_build_gmc1\\
system\\src'`` comes out as the full ``e:\\lazer_build_gmc1\\system\\src\\char\\
CharBones.h``.  So the literal is a direct readout of WHICH FILE the definition
lived in -- and when the target says ``...\\char\\CharBones.h`` and we say
``CharBones.cpp``, our definition is out-of-line in the ``.cpp`` where the
original's was ``inline`` in the header.

We read both sides' COFF relocation tables (never positional pairing -- see
docs/analysis, the string-literal lane burned itself on that) and compare the
MULTISETS of file-shaped literals per function.

Usage:
    python3 scripts/analysis/file_literal_census.py [--json OUT] [--all-literals]
"""
import argparse
import collections
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from strlit import decode_strlit  # noqa: E402
from strlit_relocs import parse  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FILE_SUFFIXES = ('.cpp', '.h', '.c', '.hpp', '.inl', '.cc')


def looks_like_file(text):
    t = text.rstrip('\x00')
    return t.lower().endswith(FILE_SUFFIXES)


def build_strlit_bytes_index(roots):
    """symbol name -> full NUL-terminated text, read from wherever it is DEFINED.

    The ??_C@ name only carries the first 32 source characters, so
    ``e:\\lazer_build_gmc1\\system\\src\\c`` is as much as the mangling shows and two
    different headers under one directory decode identically.  The COMDAT's own
    bytes are the ground truth; in the target's split objects the reference and
    the definition usually live in DIFFERENT units, so index globally.
    """
    idx = {}
    for root in roots:
        for p in sorted(glob.glob(os.path.join(root, '**', '*.obj'), recursive=True)):
            try:
                if os.path.getsize(p) == 0:
                    continue
                d, secs, symnames, symrecs = parse(p)
            except Exception:
                continue
            for k, n in enumerate(symnames):
                if not n.startswith('??_C@') or n in idx:
                    continue
                value, secnum, sclass, naux = symrecs[k]
                if not (1 <= secnum <= len(secs)):
                    continue
                s = secs[secnum - 1]
                blob = d[s['ptr'] + value: s['ptr'] + value + 512]
                z = blob.find(b'\x00')
                if z >= 0:
                    blob = blob[:z]
                try:
                    idx[n] = blob.decode('latin-1')
                except Exception:
                    pass
    return idx


def obj_file_literals(path, cache, full_text=None):
    """-> {func_symbol: [file-literal text, ...]} for one COFF object."""
    if path in cache:
        return cache[path]
    out = {}
    try:
        if os.path.getsize(path) == 0:
            cache[path] = out
            return out
        d, secs, symnames, symrecs = parse(path)
    except Exception:
        cache[path] = out
        return out

    # decode every string COMDAT name once
    text_of = {}
    for n in set(symnames):
        if n.startswith('??_C@'):
            try:
                w, ln, h, t, trunc = decode_strlit(n)
            except Exception:
                continue
            t = t.rstrip('\x00')
            if full_text and n in full_text:
                t, trunc = full_text[n], False
            text_of[n] = (t, trunc, ln)

    # section index -> list of (offset, symname) string relocations
    sec_relocs = collections.defaultdict(list)
    for s in secs:
        if not s['nrel']:
            continue
        import struct
        for r in range(s['nrel']):
            va, symidx, rtype = struct.unpack_from('<IIH', d, s['ptrrel'] + r * 10)
            nm = symnames[symidx] if symidx < len(symnames) else '?'
            if nm in text_of:
                sec_relocs[s['idx']].append((va, nm))

    # function symbols: class 2 (external) or 3 (static), with a section, in a
    # code section.  We attribute a relocation to the symbol whose [value,
    # value+size) window it falls in; MSVC's function COMDATs are one symbol per
    # section, so section-scoped attribution is exact for /Gy code.
    by_sec = collections.defaultdict(list)
    for k, n in enumerate(symnames):
        value, secnum, sclass, naux = symrecs[k]
        if 1 <= secnum <= len(secs) and sclass in (2, 3) and not n.startswith('??_C@'):
            sec = secs[secnum - 1]
            if sec['flags'] & 0x20:  # IMAGE_SCN_CNT_CODE
                by_sec[secnum].append((value, n))

    for secnum, syms in by_sec.items():
        relocs = sec_relocs.get(secnum, [])
        if not relocs:
            continue
        syms = sorted(set(syms))
        for va, nm in relocs:
            # the last symbol at or before this offset
            owner = None
            for value, n in syms:
                if value <= va:
                    owner = n
                else:
                    break
            if owner is None:
                continue
            t, trunc, ln = text_of[nm]
            if looks_like_file(t) or (trunc and ('\\' in t or '/' in t)):
                out.setdefault(owner, []).append((t, trunc, ln, nm))
    cache[path] = out
    return out


def tail(text, n=3):
    """last n path components, separator-normalised, for comparing shapes"""
    return '/'.join(text.replace('\\', '/').split('/')[-n:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--json', help='write full rows here')
    ap.add_argument('--repo', default=REPO)
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args()

    cfg = json.load(open(os.path.join(args.repo, 'objdiff.json')))
    full_text = build_strlit_bytes_index([
        os.path.join(args.repo, 'build/373307D9/obj'),
        os.path.join(args.repo, 'build/373307D9/src'),
    ])
    cache = {}
    rows = []
    units_examined = 0
    units_skipped_missing = 0

    for u in cfg['units']:
        if 'target_path' not in u or 'base_path' not in u:
            units_skipped_missing += 1
            continue
        tp = os.path.join(args.repo, u['target_path'])
        bp = os.path.join(args.repo, u['base_path'])
        if not (os.path.exists(tp) and os.path.exists(bp)):
            units_skipped_missing += 1
            continue
        units_examined += 1
        tgt = obj_file_literals(tp, cache, full_text)
        base = obj_file_literals(bp, cache, full_text)
        for fn in set(tgt) | set(base):
            tl = collections.Counter(t for t, _, _, _ in tgt.get(fn, []))
            bl = collections.Counter(t for t, _, _, _ in base.get(fn, []))
            if tl == bl:
                continue
            # halve: REFHI/REFLO both charge, symmetric
            only_t = sorted((tl - bl).elements())
            only_b = sorted((bl - tl).elements())
            if not only_t and not only_b:
                continue
            rows.append(dict(unit=u['name'], func=fn,
                             target_only=sorted(set(only_t)),
                             base_only=sorted(set(only_b)),
                             target_all=sorted(set(tl)), base_all=sorted(set(bl))))

    # classify
    def classify(r):
        t = r['target_only']
        b = r['base_only']
        if t and b and len(t) == 1 and len(b) == 1:
            if tail(t[0], 2).lower() == tail(b[0], 2).lower():
                return 'SEPARATOR_OR_PREFIX'
            ts, bs = t[0].lower(), b[0].lower()
            t_is_h = ts.endswith(('.h', '.hpp', '.inl'))
            b_is_h = bs.endswith(('.h', '.hpp', '.inl'))
            if t_is_h and not b_is_h:
                return 'TARGET_HEADER_OURS_CPP'
            if b_is_h and not t_is_h:
                return 'TARGET_CPP_OURS_HEADER'
            return 'DIFFERENT_FILE'
        if t and not b:
            return 'TARGET_ONLY'
        if b and not t:
            return 'BASE_ONLY'
        return 'MULTI'

    for r in rows:
        r['klass'] = classify(r)

    counts = collections.Counter(r['klass'] for r in rows)
    if not args.quiet:
        print('units examined %d, skipped (missing obj) %d' % (units_examined, units_skipped_missing))
        print('disagreeing functions: %d' % len(rows))
        for k, v in counts.most_common():
            print('  %-24s %d' % (k, v))
        print()
        for k in ('TARGET_HEADER_OURS_CPP', 'TARGET_CPP_OURS_HEADER', 'DIFFERENT_FILE',
                  'SEPARATOR_OR_PREFIX', 'TARGET_ONLY', 'BASE_ONLY', 'MULTI'):
            sel = [r for r in rows if r['klass'] == k]
            if not sel:
                continue
            print('== %s (%d)' % (k, len(sel)))
            for r in sorted(sel, key=lambda r: (r['unit'], r['func'])):
                print('  %s :: %s' % (r['unit'], r['func']))
                print('      target: %s' % (r['target_only'] or r['target_all']))
                print('      ours:   %s' % (r['base_only'] or r['base_all']))
            print()

    if args.json:
        json.dump(rows, open(args.json, 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
