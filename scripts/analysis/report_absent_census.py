#!/usr/bin/env python3
"""Census of decomp.db rows that join no report.json entry, and why.

Task #104 (2026-08-18), dc3-decomp.  The previous lane
(docs/analysis/cert-rot-audit-20260817.md) excluded 1,995 `merged_<hex>` /
`fn_<hex>` phantoms on a NAME SHAPE and deliberately left 140 rows alone,
because a symbol that vanished from the report may have vanished because our
build stopped emitting it -- a real defect wearing bookkeeping's clothes.

This script answers that per row by reading the COFF objects on BOTH sides
rather than inferring from the report, and classifies each row into one of:

  A_icf_survivor_renamed_merged   the split named this address with this
                                  mangled name once; it now carries
                                  merged_<addr>, and a live DB row exists
                                  under the merged name
  B_split_config_renamed_real     the split corrected the name (??_E -> ??_G,
                                  re-hashed anonymous namespace, corrected
                                  template argument / storage class); the row
                                  under the corrected name holds the cert
  C_unreferenced_inline_comdat    never in symbols.txt, defined by NO target
                                  object, present in ours only as a
                                  discardable .text COMDAT -- MSVC emits an
                                  out-of-line copy of every inline function
                                  and the linker discards the unreferenced
                                  ones
  D_retired_link_glue_stub        defined in no object on either side; a glue
                                  stub that was deliberately removed
  F_absent_both_sides             defined by no object on either side at all
  E_dtk_label                     the target object defines it with COFF
                                  storage class 6 (IMAGE_SYM_CLASS_LABEL),
                                  which objdiff maps to SymbolKind::Unknown
                                  and never reports as a function

THE LOAD-BEARING NEGATIVE
-------------------------
The class this hunt was FOR -- "both sides define it but the report omits it",
i.e. objdiff failing to score a function we emit -- came back EMPTY.  Scanning
every object under build/373307D9/obj/ (2,223 of them), 136 of the 140 symbols
are defined by no target object at all and the other 4 are labels.  Re-run this
before ever concluding otherwise; a sampled check would not have settled it.

Usage:
    python3 scripts/analysis/report_absent_census.py [--db PATH] [--json OUT]

Run from the repo root (or a worktree) AFTER a full `ninja` -- a single-object
build leaves report.json stale and the row set moves.
"""
import argparse
import json
import os
import re
import sqlite3
import subprocess
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from analysis.coffx import read_coff, infer_sizes, K_FUNC, K_OBJ  # noqa: E402

PHANTOM = re.compile(r'^(merged_|fn_)[0-9a-fA-F]+$')
SYMLINE = re.compile(r'^(\S+) = \.\w+:0x([0-9A-Fa-f]+);.*?size:0x([0-9A-Fa-f]+)')
SYMBOLS_TXT = 'config/373307D9/symbols.txt'


def load_report(path):
    rep = json.load(open(path))
    return {u['name']: set(f['name'] for f in (u.get('functions') or []))
            for u in rep['units']}


def coff_defs(path):
    """Names this COFF object DEFINES as function/object symbols."""
    if not path or not os.path.exists(path):
        return None
    secs, syms = read_coff(open(path, 'rb').read())
    if secs is None:
        return None
    infer_sizes(secs, syms)
    return {s.name for s in syms if s.sec > 0 and s.kind in (K_FUNC, K_OBJ)}


def scan_all_objs(root):
    """Every name defined by every object under `root`, kind-agnostic.

    Kind-agnostic on purpose: a name present only as a storage-class-6 LABEL is
    the E class, and a kind filter would hide it and make the row look bogus.
    """
    out = defaultdict(list)
    n = 0
    for dirpath, _, files in os.walk(root):
        for f in files:
            if not f.endswith('.obj'):
                continue
            p = os.path.join(dirpath, f)
            n += 1
            secs, syms = read_coff(open(p, 'rb').read())
            if secs is None:
                continue
            for s in syms:
                if s.sec > 0:
                    out[s.name].append((p, s.cls, s.typ))
    return out, n


def symbols_txt_history(wanted):
    """address(es) each wanted name ever held in symbols.txt, across all revs."""
    revs = subprocess.run(['git', 'log', '--format=%h', '--', SYMBOLS_TXT],
                          capture_output=True, text=True).stdout.split()
    hist = defaultdict(set)
    for c in revs:
        blob = subprocess.run(['git', 'show', f'{c}:{SYMBOLS_TXT}'],
                              capture_output=True, text=True).stdout
        for line in blob.splitlines():
            m = SYMLINE.match(line.strip())
            if m and m.group(1) in wanted:
                hist[m.group(1)].add(m.group(2).upper())
    return hist, len(revs)


def current_names_by_addr():
    cur = defaultdict(list)
    for line in open(SYMBOLS_TXT):
        m = SYMLINE.match(line.strip())
        if m:
            cur[m.group(2).upper()].append((m.group(1), int(m.group(3), 16)))
    return cur


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--db', default='decomp.db')
    ap.add_argument('--report', default='build/373307D9/report.json')
    ap.add_argument('--objdiff', default='objdiff.json')
    ap.add_argument('--json', help='write the classified rows here')
    ap.add_argument('--include-excluded', action='store_true',
                    help='also census rows already excluded=1')
    args = ap.parse_args()

    runits = load_report(args.report)
    cfg = json.load(open(args.objdiff))
    paths = {u['name']: (u.get('target_path'), u.get('base_path'))
             for u in cfg['units']}

    db = sqlite3.connect(args.db)
    where = '' if args.include_excluded else 'WHERE excluded=0'
    rows = db.execute(
        f'SELECT id,unit,symbol,size,verdict,current_percent,attempt_count,'
        f'excluded FROM functions {where}').fetchall()

    absent = [r for r in rows
              if runits.get(r[1]) is not None
              and r[2] not in runits[r[1]]
              and not PHANTOM.match(r[2])]
    print(f'non-excluded rows: {len(rows)}')
    print(f'absent from report, not name-shaped phantoms: {len(absent)} '
          f'({sum(r[3] or 0 for r in absent)} B)')
    if not absent:
        return

    wanted = {r[2] for r in absent}
    all_tgt, n_objs = scan_all_objs('build/373307D9/obj')
    print(f'scanned {n_objs} target objects; of the {len(wanted)} names, '
          f'{sum(1 for w in wanted if w in all_tgt)} appear in any of them')

    hist, n_revs = symbols_txt_history(wanted)
    cur = current_names_by_addr()
    print(f'{n_revs} revisions of {SYMBOLS_TXT}; '
          f'{len(hist)} of the names were ever in it')

    defs_cache = {}
    out = []
    for (fid, unit, sym, size, verdict, pct, ac, exc) in absent:
        tp, bp = paths.get(unit, (None, None))
        for p in (tp, bp):
            if p not in defs_cache:
                defs_cache[p] = coff_defs(p)
        td, bd = defs_cache[tp], defs_cache[bp]

        addr = cur_name = None
        for a in sorted(hist.get(sym, ())):
            if cur.get(a):
                addr, cur_name = a, cur[a][0][0]
                break
        else:
            addr = next(iter(sorted(hist.get(sym, ()))), None)

        if sym in all_tgt and all(c == 6 for _, c, _ in all_tgt[sym]):
            klass = 'E_dtk_label'
        elif cur_name and cur_name.startswith('merged_'):
            klass = 'A_icf_survivor_renamed_merged'
        elif cur_name and cur_name != sym:
            klass = 'B_split_config_renamed_real'
        elif unit == 'default/link_glue':
            klass = 'D_retired_link_glue_stub'
        elif td is not None and bd is not None and sym in td and sym in bd:
            # BOTH objects define it and the report still omits it. That is a
            # report/pairing defect -- the thing this census exists to surface
            # -- and it must be tested BEFORE class C, which is defined as
            # "defined by NO target object". C's predicate only asks about the
            # base side, so it used to swallow this case and mislabel it as a
            # benign unreferenced inline COMDAT. Only the independently
            # computed `both` list below kept it visible at all.
            klass = 'G_defined_both_sides_report_omits'
        elif bd is not None and sym in bd:
            klass = 'C_unreferenced_inline_comdat'
        elif sym not in all_tgt:
            # neither our object for this unit nor ANY target object has it
            klass = 'F_absent_both_sides'
        else:
            klass = 'Z_UNKNOWN'

        live = None
        if cur_name and cur_name != sym:
            live = db.execute(
                'SELECT id,unit,verdict,current_percent,excluded '
                'FROM functions WHERE symbol=?', (cur_name,)).fetchone()

        out.append(dict(id=fid, unit=unit, symbol=sym, size=size,
                        verdict=verdict, current_percent=pct,
                        attempt_count=ac, excluded=exc, klass=klass,
                        split_address=addr, current_split_name=cur_name,
                        live_row=live,
                        target_obj_defines=(sym in td) if td is not None else None,
                        base_obj_defines=(sym in bd) if bd is not None else None,
                        defined_in_any_target_obj=all_tgt.get(sym, [])[:3]))

    tally = defaultdict(lambda: [0, 0, Counter()])
    for x in out:
        t = tally[x['klass']]
        t[0] += 1
        t[1] += x['size'] or 0
        t[2][x['verdict']] += 1
    print(f"\n{'class':34s} {'rows':>5s} {'bytes':>7s}  verdicts")
    for k in sorted(tally):
        n, b, v = tally[k]
        print(f'{k:34s} {n:5d} {b:7d}  {dict(v)}')

    both = [x for x in out
            if x['target_obj_defines'] and x['base_obj_defines']]
    print(f'\nrows BOTH objects define (would be a real report/pairing '
          f'defect): {len(both)}')
    for x in both:
        print('   ', x['unit'], x['symbol'][:70])

    if args.json:
        json.dump(out, open(args.json, 'w'), indent=1)
        print(f'\nwrote {args.json}')


if __name__ == '__main__':
    main()
