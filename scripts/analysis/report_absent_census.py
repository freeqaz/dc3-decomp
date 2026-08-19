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

COVERAGE (2026-08-19)
---------------------
This was already the most honest scanner of its family -- it prints a
denominator at every stage and records the load-bearing NEGATIVE above -- so
the changes are additive.  Three populations it counted but never NAMED are now
named: rows whose UNIT is absent from report.json entirely (dropped by the
`runits.get(...) is not None` guard, and a different animal from "the unit is
there but the symbol is not"), rows excluded on the `merged_`/`fn_` name shape,
and objects `scanned` includes that could not be parsed.

Usage:
    python3 scripts/analysis/report_absent_census.py [--db PATH] [--json OUT]

Run from the repo root (or a worktree) AFTER a full `ninja` -- a single-object
build leaves report.json stale and the row set moves.  `--project-dir` moves the
two remaining cwd-relative paths (symbols.txt and the target object tree).
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

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

PHANTOM = re.compile(r'^(merged_|fn_)[0-9a-fA-F]+$')
SYMLINE = re.compile(r'^(\S+) = \.\w+:0x([0-9A-Fa-f]+);.*?size:0x([0-9A-Fa-f]+)')
SYMBOLS_TXT = 'config/373307D9/symbols.txt'
TARGET_OBJ_DIR = 'build/373307D9/obj'


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

    Returns (index, n_found, n_parsed).  `n_found` counts .obj files seen and
    `n_parsed` those read_coff accepted -- the old single `n` was incremented
    BEFORE the parse check, so `scanned N objects` counted objects it had failed
    to parse.  Directory order is sorted so `all_tgt[sym][:3]` below cannot pick
    a different arbitrary 3 between runs.
    """
    out = defaultdict(list)
    n_found = n_parsed = 0
    for dirpath, dirnames, files in os.walk(root):
        dirnames.sort()
        for f in sorted(files):
            if not f.endswith('.obj'):
                continue
            p = os.path.join(dirpath, f)
            n_found += 1
            secs, syms = read_coff(open(p, 'rb').read())
            if secs is None:
                continue
            n_parsed += 1
            for s in syms:
                if s.sec > 0:
                    out[s.name].append((p, s.cls, s.typ))
    return out, n_found, n_parsed


def _git(args, cwd):
    """Read-only git call whose FAILURE is loud.

    Neither call here checked its returncode.  Run from the wrong cwd, `git log`
    returns nothing, `hist` comes back empty, and every row misclassifies -- the
    only tell being a `0 revisions of ...` line that reads like a fact about the
    repo rather than a failed subprocess.
    """
    r = subprocess.run(['git'] + args, capture_output=True, text=True, cwd=cwd)
    if r.returncode != 0:
        raise SystemExit(
            f"FATAL: `git {' '.join(args)}` failed in {cwd} (rc={r.returncode}): "
            f"{(r.stderr or '').strip()[:300]}\n"
            f"Without symbols.txt history every row is misclassified and the "
            f"only symptom is a '0 revisions' line.")
    return r.stdout


def symbols_txt_history(wanted, project_dir='.'):
    """address(es) each wanted name ever held in symbols.txt, across all revs."""
    revs = _git(['log', '--format=%h', '--', SYMBOLS_TXT], project_dir).split()
    hist = defaultdict(set)
    for c in revs:
        blob = _git(['show', f'{c}:{SYMBOLS_TXT}'], project_dir)
        for line in blob.splitlines():
            m = SYMLINE.match(line.strip())
            if m and m.group(1) in wanted:
                hist[m.group(1)].add(m.group(2).upper())
    return hist, len(revs)


def current_names_by_addr(symbols_txt):
    cur = defaultdict(list)
    for line in open(symbols_txt):
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
    ap.add_argument('--project-dir', default='.',
                    help='repo root that SYMBOLS_TXT, the target object tree and '
                         'the git history are read from (default: cwd, which is '
                         'what these three used unconditionally before '
                         '2026-08-19)')
    add_coverage_args(ap)
    args = ap.parse_args()

    pd = os.path.abspath(args.project_dir)
    symbols_txt = os.path.join(pd, SYMBOLS_TXT)
    target_obj_dir = os.path.join(pd, TARGET_OBJ_DIR)

    runits = load_report(args.report)
    cfg = json.load(open(args.objdiff))
    paths = {u['name']: (u.get('target_path'), u.get('base_path'))
             for u in cfg['units']}

    db = sqlite3.connect(args.db)
    where = '' if args.include_excluded else 'WHERE excluded=0'
    # ORDER BY: SQLite's natural order is an implementation detail, and every
    # printout below inherits it.
    rows = db.execute(
        f'SELECT id,unit,symbol,size,verdict,current_percent,attempt_count,'
        f'excluded FROM functions {where} ORDER BY unit, symbol, id').fetchall()

    cov = CoverageReport("report_absent_census", args=args)
    cov.universe(len(rows), "decomp.db function rows"
                            + ("" if args.include_excluded else " with excluded=0"))

    absent = []
    for r in rows:
        if runits.get(r[1]) is None:
            # NOT the same class as "the unit is in the report but the symbol is
            # not": the whole unit is missing, which would be a build/pairing
            # defect rather than a bookkeeping one.  It reads 0 today.
            cov.drop("unit-absent-from-report", note="report.json has no unit of "
                                                     "this name at all")
            continue
        if r[2] in runits[r[1]]:
            cov.drop("present-in-report", note="the report scores this symbol; "
                                               "nothing to explain")
            continue
        if PHANTOM.match(r[2]):
            cov.drop("phantom-name-shape", note="merged_<hex>/fn_<hex>; excluded "
                                                "deliberately, see the 1,995 in "
                                                "cert-rot-audit-20260817.md")
            continue
        cov.examine()
        absent.append(r)

    d0 = cov.as_dict()
    print(f'non-excluded rows: {len(rows)}')
    print(f'  units absent from report.json entirely : '
          f'{d0["dropped"].get("unit-absent-from-report", 0)}')
    print(f'  present in report.json                 : '
          f'{d0["dropped"].get("present-in-report", 0)}')
    print(f'  absent, excluded on the merged_/fn_ name shape : '
          f'{d0["dropped"].get("phantom-name-shape", 0)}')
    print(f'absent from report, not name-shaped phantoms: {len(absent)} '
          f'({sum(r[3] or 0 for r in absent)} B) of {len(rows)} rows')
    if not absent:
        return cov.emit()

    wanted = {r[2] for r in absent}
    all_tgt, n_objs, n_parsed = scan_all_objs(target_obj_dir)
    print(f'scanned {n_objs} target objects ({n_parsed} parsed, '
          f'{n_objs - n_parsed} UNPARSEABLE); of the {len(wanted)} names, '
          f'{sum(1 for w in wanted if w in all_tgt)} appear in any of them')
    cov.extra("target_objects_found", n_objs)
    cov.extra("target_objects_parsed", n_parsed)
    if n_objs != n_parsed:
        cov.note(f"{n_objs - n_parsed} of {n_objs} target objects failed to "
                 f"parse; a name they define reads as 'defined nowhere'")

    hist, n_revs = symbols_txt_history(wanted, pd)
    cur = current_names_by_addr(symbols_txt)
    print(f'{n_revs} revisions of {SYMBOLS_TXT}; '
          f'{len(hist)} of the names were ever in it')
    cov.extra("symbols_txt_revisions", n_revs)

    defs_cache = {}
    out = []
    for (fid, unit, sym, size, verdict, pct, ac, exc) in absent:
        tp, bp = paths.get(unit, (None, None))
        tp = os.path.join(pd, tp) if tp else tp
        bp = os.path.join(pd, bp) if bp else bp
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
                'FROM functions WHERE symbol=? ORDER BY id',
                (cur_name,)).fetchone()

        # os.walk is now sorted, so this is the FIRST 3 of a stable order rather
        # than an arbitrary 3; the count beside it says how many were elided.
        defs = sorted(all_tgt.get(sym, []))
        out.append(dict(id=fid, unit=unit, symbol=sym, size=size,
                        verdict=verdict, current_percent=pct,
                        attempt_count=ac, excluded=exc, klass=klass,
                        split_address=addr, current_split_name=cur_name,
                        live_row=live,
                        target_obj_defines=(sym in td) if td is not None else None,
                        base_obj_defines=(sym in bd) if bd is not None else None,
                        defined_in_n_target_objs=len(defs),
                        defined_in_any_target_obj=defs[:3]))

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
    print(f"{'TOTAL':34s} {len(out):5d} "
          f"{sum(x['size'] or 0 for x in out):7d}  "
          f"(of {len(rows)} db rows)")

    both = [x for x in out
            if x['target_obj_defines'] and x['base_obj_defines']]
    print(f'\nrows BOTH objects define (would be a real report/pairing '
          f'defect): {len(both)} of {len(out)}')
    for x in sorted(both, key=lambda x: (x['unit'], x['symbol'])):
        print('   ', x['unit'], x['symbol'][:70])

    cov.extra("class_tally", {k: tally[k][0] for k in sorted(tally)})
    cov.extra("both_objects_define", len(both))
    if args.json:
        json.dump(out, open(args.json, 'w'), indent=1)
        print(f'\nwrote {args.json}')
    return cov.emit()


if __name__ == '__main__':
    sys.exit(main())
