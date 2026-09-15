import json, sys, re
rep = json.load(open('/home/free/tmp/w8-g/build/373307D9/report.json'))
want = [l.strip() for l in sys.stdin if l.strip()] if not sys.argv[1:] else sys.argv[1:]
idx = {}
for u in rep['units']:
    for f in u.get('functions', []):
        idx.setdefault(f['name'], []).append((u['name'], f.get('match_percent_normalized'), f.get('size')))
for w in want:
    if w in idx:
        for un, p, s in idx[w]:
            print(f"{p!s:>12}  {s:>6}  {un}  {w}")
    else:
        print(f"{'MISSING':>12}  {'':>6}  ?  {w}")
