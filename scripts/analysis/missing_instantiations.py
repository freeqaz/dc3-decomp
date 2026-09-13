import re,sys,subprocess
unit=sys.argv[1]  # e.g. system/rndobj/Utl
tgt=[]
for l in open(f'build/373307D9/asm/{unit}.s'):
    m=re.match(r'\.fn "(.*)", global',l)
    if m: tgt.append(m.group(1))
ours=set(subprocess.run(['python3','scripts/analysis/coff_defined_symbols.py',f'build/373307D9/src/{unit}.obj'],capture_output=True,text=True).stdout.split('\n'))
miss=[t for t in tgt if t not in ours and not t.startswith('__unwind')]
print(f'{unit}: target={len(tgt)} missing={len(miss)}')
for m in miss: print('  ',m)
