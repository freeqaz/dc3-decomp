"""Negative control: does the body test DISCRIMINATE, or does it pass anything?

For every row that got a verdict, test the target body against EVERY function
symbol our unit's own object defines -- not just the map members at that
address.  If the test is doing work, only the true name (and any genuine
in-unit fold twin) passes.
"""
import json, os, sys
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
os.chdir(REPO)
sys.path.insert(0, os.path.join(REPO, 'scripts'))
sys.path.insert(0, os.path.join(REPO, 'scripts', 'analysis'))
import icf_survivor_names as M
from analysis.coffx import read_coff, infer_sizes, K_FUNC

P = os.path.abspath('.')
addr2 = M.read_map(P)
naddr = M.name_addresses(P, addr2)
ADJ = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    REPO, 'docs/analysis/icf-survivor-names-20260819/adjudication.json')
res = json.load(open(ADJ))

tot_cand = tot_pass = 0
worst = []
for r in res:
    if r['verdict'] not in ('PROVEN_BODY', 'WEAK_NO_RELOC'):
        continue
    rel = r['unit'].replace('default/', '', 1)
    objp = os.path.join(P, 'build/373307D9/src', rel + '.obj')
    asmp = os.path.join(P, 'build/373307D9/asm', rel + '.s')
    # Before --apply the split asm calls it merged_<addr>; after, it calls it
    # the installed name.  Accept either so the control is reproducible on the
    # landed tree, not only on the tree it was first written against.
    tb, tr = M.target_body(asmp, r['split_name'])
    if tb is None:
        tb, tr = M.target_body(asmp, r['name'])
    if tb is None:
        print('%-11s no target body under either spelling' % r['address'])
        continue
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
