#!/usr/bin/env python3
"""Re-derive the three source lanes against the SHIPPED LINKER MAP, not residency."""
import json, sys, collections
sys.path.insert(0, "/home/free/.claude/jobs/bc1aebde/tmp/laneS")
from mapidx import load

n2a, a2n = load(sys.argv[2])
rows = [json.loads(l) for l in open(sys.argv[1])]
lanes = sys.argv[3].split(",")

def verdict(t, b):
    ta, ba = n2a.get(t), n2a.get(b)
    if ta is None and ba is None: return "NEITHER_IN_MAP"
    if ba is None: return "OURS_ABSENT_FROM_MAP"
    if ta is None: return "TARGET_ABSENT"          # cannot happen for a reloc name
    sa, sb = {a for a, _ in ta}, {a for a, _ in ba}
    if sa & sb: return "FOLDED(same addr)"
    return "DISTINCT(retail shipped both)"

for lane in lanes:
    L = [r for r in rows if r["lane"] == lane]
    c = collections.Counter((r["target"], r["base"]) for r in L)
    agg = collections.Counter()
    print(f"\n=== {lane}: {len(L)} sites, {len(c)} pairs")
    for (t, b), n in c.most_common():
        v = verdict(t, b)
        agg[v] += 1
        aT = n2a.get(t, [])
        aB = n2a.get(b, [])
        print(f"  [{v:26}] n={n:3d}")
        print(f"      T {t}  @ {[hex(a) for a,_ in aT][:3]} {[o for _,o in aT][:2]}")
        print(f"      O {b}  @ {[hex(a) for a,_ in aB][:3]} {[o for _,o in aB][:2]}")
    print(f"  --- pair verdicts: {dict(agg)}")
