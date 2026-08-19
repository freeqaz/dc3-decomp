#!/usr/bin/env python3
"""My own re-derivation of the icf_pairing_bodytest population buckets.
Independent of scripts/analysis/icf_pairing_bodytest.py -- written from the
report.json + linker map + our COFF objects + dtk's split .s headers."""
import json,os,re,sys,collections,struct,glob
sys.path.insert(0,"/tmp/vfy")
import foldcheck as F

ROOT=sys.argv[1]
REPORT=sys.argv[2] if len(sys.argv)>2 else ROOT+"/build/373307D9/report.json"
rep=json.load(open(REPORT))
rows=[(u["name"],f["name"],int(f.get("size",0)))
      for u in rep["units"] for f in (u.get("functions") or []) if f.get("name")]
print("report.json rows: %d   units: %d"%(len(rows),len(rep["units"])))

synth=re.compile(r"^(merged_|fn_)")
in_map=set(F.S2A)
n_synth=sum(1 for _,s,_ in rows if synth.match(s))
n_synth_notmap=sum(1 for _,s,_ in rows if synth.match(s) and s not in in_map)
n_notmap=sum(1 for _,s,_ in rows if s not in in_map)
print("rows whose symbol is NOT in ham_xbox_r.map : %d"%n_notmap)
print("  of those, merged_*/fn_* synthetic labels : %d"%n_synth_notmap)
print("  total merged_*/fn_* rows                 : %d"%n_synth)

# how many of the not-in-map rows ARE resolvable from dtk's .s header comment
HDR=re.compile(r"^#\s+\.\w+:0x[0-9A-Fa-f]+\s+\|\s+0x([0-9A-Fa-f]{8})\s+\|\s+size:")
_cache={}
def asm_addrs(unit):
    rel=unit.replace("default/","",1)
    p=ROOT+"/build/373307D9/asm/"+rel+".s"
    if p in _cache: return _cache[p]
    m={}
    if os.path.exists(p):
        pend=None
        for line in open(p,errors="replace"):
            h=HDR.match(line)
            if h: pend=int(h.group(1),16); continue
            if line.startswith(".fn ") and pend is not None:
                m[line[4:].split(", ")[0].strip().strip('"')]=pend
            pend=None
    _cache[p]=m
    return m

# quoted-vs-bare check for the .fn matcher (defect ii)
bare=quoted=0
for p in glob.glob(ROOT+"/build/373307D9/asm/**/*.s",recursive=True)[:400]:
    for line in open(p,errors="replace"):
        if line.startswith('.fn "merged_') or line.startswith('.fn "fn_'): quoted+=1
        elif line.startswith('.fn merged_') or line.startswith('.fn fn_'): bare+=1
print("dtk .fn synthetic labels in first 400 .s files:  bare=%d  quoted=%d"%(bare,quoted))

# full bucket census, my own logic
_objcache={}
def syms_of(unit):
    rel=unit.replace("default/","",1)
    p=ROOT+"/build/373307D9/src/"+rel+".obj"
    if p in _objcache: return _objcache[p]
    r=None
    if os.path.exists(p):
        try: r=F.load_obj(p)[2]
        except Exception: r="unreadable"
    _objcache[p]=r
    return r

tal=collections.Counter(); interesting=[]
resolved_only_by_asm=0
for unit,sym,size in rows:
    rec=F.S2A.get(sym)
    if rec: addr=rec[0]
    else:
        addr=asm_addrs(unit).get(sym)
        if addr: resolved_only_by_asm+=1
    if not addr: tal["no-address"]+=1; continue
    ss=syms_of(unit)
    if ss is None: tal["no-object"]+=1; continue
    if ss=="unreadable": tal["unreadable-object"]+=1; continue
    if sym in ss: tal["we-define-it"]+=1; continue
    peers=[p for p in F.A2S.get(addr,[]) if p!=sym and p in ss]
    if not peers:
        tal["no-fold-peer" if F.A2S.get(addr) else "addr-not-in-map"]+=1; continue
    tal["TESTED"]+=1
print("\nrows resolved ONLY via the .s header comment (map-blind rows): %d"%resolved_only_by_asm)
print("bucket census:")
for k,n in tal.most_common(): print("  %6d  %s"%(n,k))

# ---- verdict pass over the TESTED rows, target side = PE bytes + .pdata ----
print("\n=== verdict pass (target side = ham_xbox_r.exe + .pdata, not dtk .s) ===")
def tgtbody(addr):
    n=F.tgt_size(addr)
    return (F.read_va(addr,n),n) if n else (None,None)
def ourbody(unit,sym,length):
    rel=unit.replace("default/","",1)
    p=ROOT+"/build/373307D9/src/"+rel+".obj"
    return F.our_body(p,sym,length)
diffs=[]
for unit,sym,size in rows:
    rec=F.S2A.get(sym)
    addr=rec[0] if rec else asm_addrs(unit).get(sym)
    if not addr: continue
    ss=syms_of(unit)
    if ss is None or ss=="unreadable" or sym in ss: continue
    peers=[p for p in F.A2S.get(addr,[]) if p!=sym and p in ss]
    if not peers: continue
    tb,n=tgtbody(addr)
    if tb is None: diffs.append(("no-target-body",unit,sym,None,size)); continue
    ok=False; used=None
    for p in peers:
        got=ourbody(unit,p,n)
        if not got: continue
        body,rel_=got
        if len(body)==n and F.mask(body,rel_)==F.mask(tb,rel_): ok=True; used=p; break
    if not ok: diffs.append(("DIFFER",unit,sym,peers[0],size))
print("TESTED rows that FAIL the byte test: %d"%len([d for d in diffs if d[0]=='DIFFER']))
print("TESTED rows with no target body    : %d"%len([d for d in diffs if d[0]!='DIFFER']))
for v,unit,sym,peer,size in sorted(diffs,key=lambda x:-(x[4] or 0)):
    print("  %-14s %5s %s\n        %s\n        peer: %s"%(v,size,unit,sym,peer))
