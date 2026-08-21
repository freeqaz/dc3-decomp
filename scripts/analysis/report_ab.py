#!/usr/bin/env python3
"""report_ab.py -- whole-build A/B of two report.json files, BY ABSOLUTE PATH.

Both paths are read in ONE process.  The failure this guards against is real:
`cd`-ing into a worktree and reading a relative `build/373307D9/report.json`
twice compares a tree against itself and always prints "no change".

Keys on `match_percent_normalized` (canonical, does not round) and reports
`fuzzy_match_percent` alongside.  There is no `match_percent` field.

    python3 scripts/analysis/report_ab.py /abs/base/report.json /abs/new/report.json
"""
import argparse
import json
import os
import sys


def load(path):
    p = os.path.abspath(path)
    if not os.path.isabs(path):
        print('NOTE: %s resolved to %s' % (path, p))
    rep = json.load(open(p))
    fns = {}
    for u in rep['units']:
        for f in (u.get('functions') or []):
            fns[(u['name'], f['name'])] = (
                f.get('match_percent_normalized'), f.get('fuzzy_match_percent'),
                int(f['size']))
    return p, rep.get('measures', rep), fns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base')
    ap.add_argument('new')
    ap.add_argument('--max-list', type=int, default=40)
    a = ap.parse_args()
    if os.path.abspath(a.base) == os.path.abspath(a.new):
        print('REFUSING: both paths resolve to the same file')
        return 2
    pb, mb, fb = load(a.base)
    pn, mn, fn = load(a.new)
    print('base %s' % pb)
    print('new  %s' % pn)
    print()
    for k in ('total_functions', 'matched_functions', 'matched_code',
              'matched_functions_percent', 'matched_code_percent',
              'fuzzy_match_percent', 'complete_code'):
        vb, vn = mb.get(k), mn.get(k)
        try:
            d = float(vn) - float(vb)
        except (TypeError, ValueError):
            d = None
        print('  %-28s %14s -> %-14s  %s' % (k, vb, vn,
                                             ('%+g' % d) if d is not None else ''))
    print()
    gone = sorted(set(fb) - set(fn))
    new = sorted(set(fn) - set(fb))
    moved = [k for k in set(fb) & set(fn) if fb[k][0] != fn[k][0]]
    up = sorted([k for k in moved if fn[k][0] > fb[k][0]])
    down = sorted([k for k in moved if fn[k][0] < fb[k][0]])
    print('rows only in base (vanished): %d  (%d B)'
          % (len(gone), sum(fb[k][2] for k in gone)))
    print('rows only in new  (appeared): %d  (%d B)'
          % (len(new), sum(fn[k][2] for k in new)))
    print('rows present in both that MOVED on normalized: %d  (up %d, DOWN %d)'
          % (len(moved), len(up), len(down)))
    print()
    if down:
        print('REGRESSIONS on match_percent_normalized:')
        for k in down[:a.max_list]:
            print('  %-52s %-60s %8.4f -> %8.4f  (%d B)'
                  % (k[0], k[1][:60], fb[k][0], fn[k][0], fb[k][2]))
        if len(down) > a.max_list:
            print('  ... %d more' % (len(down) - a.max_list))
    else:
        print('REGRESSIONS on match_percent_normalized: NONE')
    print()
    if up:
        print('improvements in place: %d' % len(up))
        for k in up[:a.max_list]:
            print('  %-52s %-60s %8.4f -> %8.4f' % (k[0], k[1][:60], fb[k][0], fn[k][0]))
    # fuzzy is a second axis; a rename can move it without moving normalized
    fmoved = [k for k in set(fb) & set(fn) if fb[k][1] != fn[k][1]]
    fdown = [k for k in fmoved if (fn[k][1] or 0) < (fb[k][1] or 0)]
    print()
    print('rows present in both that moved on fuzzy_match_percent: %d (down %d)'
          % (len(fmoved), len(fdown)))
    for k in sorted(fdown)[:a.max_list]:
        # fuzzy_match_percent is absent (null) on some rows; do not format None
        print('  FUZZY DOWN %-46s %-56s %8s -> %-8s'
              % (k[0], k[1][:56], fb[k][1], fn[k][1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
