import json, sys
def load(p):
    r=json.load(open(p)); d={}
    for u in r['units']:
        for f in u.get('functions',[]):
            d[(u['name'],f['name'])]=(f['match_percent_normalized'], int(f['size']))
    return d
a=load(sys.argv[1]); b=load(sys.argv[2])
th=float(sys.argv[3]) if len(sys.argv)>3 else 0.001
rows=[]
for k in set(a)|set(b):
    pa=a.get(k,(None,None)); pb=b.get(k,(None,None))
    if pa[0] is None or pb[0] is None or abs(pa[0]-pb[0])>th:
        rows.append((k,pa,pb))
rows.sort(key=lambda r:(r[2][0] or 0)-(r[1][0] or 0))
for k,pa,pb in rows:
    print(f"{str(pa[0]):>12} -> {str(pb[0]):>12}  {pb[1] or pa[1]:>6}  {k[0]}  {k[1]}")
print(f"total changed rows: {len(rows)}")
