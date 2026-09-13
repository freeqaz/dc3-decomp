import re,sys,subprocess,collections,os
byaddr=collections.defaultdict(list); byname={}
for l in open('orig/373307D9/ham_xbox_r.map',encoding='latin1'):
    m=re.match(r'\s+[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+(\S+)\s+(\S+)\s+(\S+)',l)
    if not m: continue
    byaddr[m.group(2)].append((m.group(1),m.group(5))); byname[m.group(1)]=m.group(2)
cache={}
def oursyms(u):
    if u not in cache:
        p=f'build/373307D9/src/{u}.obj'
        cache[u]=set(subprocess.run(['python3','scripts/analysis/coff_defined_symbols.py',p],capture_output=True,text=True).stdout.split('\n')) if os.path.exists(p) else set()
    return cache[u]
for line in open(sys.argv[1]):
    line=line.strip()
    if not line or line.startswith('LANE'): continue
    size,verdict,stub,unit,sym=line.split(None,4)
    addr=byname.get(sym)
    base=unit.split('/')[-1]
    if addr is None: print(f'{size:>5} {unit:<34} NOMAP {sym[:60]}'); continue
    al=byaddr[addr]
    ours=oursyms(unit)
    # aliases attributed to THIS unit's object
    mine=[n for n,o in al if o.endswith(':'+base+'.obj')]
    weemit=[n for n in mine if n in ours]
    if len(al)==1:
        print(f'{size:>5} {unit:<34} NOTEMIT {sym[:70]}')
    elif sym in [n for n,o in al if o.endswith(':'+base+'.obj')]:
        print(f'{size:>5} {unit:<34} ICF-BUT-IS-OURS(not emitted) {sym[:60]}')
    elif weemit:
        print(f'{size:>5} {unit:<34} ICF-ARTIFACT we-emit={weemit[0][:70]}')
        print(f'      target-name={sym[:100]}')
    elif mine:
        print(f'{size:>5} {unit:<34} ICF-unit-alias-NOT-emitted={mine[0][:70]}')
        print(f'      target-name={sym[:100]}')
    else:
        print(f'{size:>5} {unit:<34} ICF-no-alias-from-this-unit  {sym[:60]}')
