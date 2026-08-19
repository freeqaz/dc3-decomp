"""Negative control: does the body test DISCRIMINATE, or does it pass anything?

For every row that got a verdict, test the target body against EVERY function
symbol our unit's own object defines -- not just the map members at that
address.  If the test is doing work, only the true name (and any genuine
in-unit fold twin) passes.
"""
import json, os, sys
sys.path.insert(0, 'scripts')
sys.path.insert(0, 'scripts/analysis')
import icf_survivor_names as M
from analysis.coffx import read_coff, infer_sizes, K_FUNC

P = os.path.abspath('.')
addr2 = M.read_map(P)
naddr = M.name_addresses(P, addr2)
rows = json.load(open('docs/analysis/report-absent-rows-20260818/recoverable-merged-names.json'))
res = json.load(open('/tmp/icf48.json'))

tot_cand = tot_pass = 0
worst = []
for r in res:
    if r['verdict'] not in ('PROVEN_BODY', 'WEAK_NO_RELOC'):
        continue
    rel = r['unit'].replace('default/', '', 1)
    objp = os.path.join(P, 'build/373307D9/src', rel + '.obj')
    asmp = os.path.join(P, 'build/373307D9/asm', rel + '.s')
    tb, tr = M.target_body(asmp, r['split_name'])
    secs, syms = read_coff(open(objp, 'rb').read()); infer_sizes(secs, syms)
    names = sorted({s.name for s in syms if s.sec > 0 and s.kind == K_FUNC and s.size})
    passed = []
    for n in names:
        ob, orl = M.our_body(objp, n)
        if ob is None: continue
        ok, _why, _nr = M.body_test(tb, tr, ob, orl, naddr)
        if ok: passed.append(n)
    tot_cand += len(names); tot_pass += len(passed)
    if len(passed) != 1:
        worst.append((r['address'], r['split_name'], r['verdict'], len(names), passed))
    print('%-11s %-34s %-14s %4d candidates -> %d pass' % (
        r['address'], r['split_name'][:34], r['verdict'], len(names), len(passed)))

print('\nTOTAL: %d candidate names tried, %d passed (%.3f%%)' % (
    tot_cand, tot_pass, 100.0*tot_pass/max(tot_cand,1)))
print('\nrows where the passing set is not exactly 1:')
for w in worst:
    print('  %s %s [%s] %d names -> %s' % (w[0], w[1], w[2], w[3], [x[:60] for x in w[4]]))
