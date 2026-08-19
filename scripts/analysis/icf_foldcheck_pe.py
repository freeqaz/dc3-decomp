#!/usr/bin/env python3
"""Independent ICF fold-membership byte checker (adversarial-verification instrument).

Written 2026-08-19 to check fix/objptr-icf-bodies WITHOUT reusing that lane's own
scanner.  Deliberately shares no code path with
``scripts/analysis/icf_pairing_bodytest.py``: it does not read dtk's split .s
listings, symbols.txt, report.json or objdiff at all.

TARGET side : bytes straight out of orig/373307D9/ham_xbox_r.exe (PE .text),
              with the function LENGTH taken from the PE's own .pdata
              RUNTIME_FUNCTION table -- not from dtk's split .s listings.
OUR side    : the COMDAT bytes of the symbol in our built PE-COFF .obj,
              with the object's own relocation table.

Neither side goes through dtk, objdiff, symbols.txt or report.json.

Negative controls run when this was written (all passed):
  * main's build (linear-scan ObjPtrVec::FindRef)   -> DIFFER
    the lane's build (ref->Parent() == this)        -> MATCH
  * perturbing one non-branch word of the TARGET body flips MATCH -> DIFFER
    and back when restored.
  * ObjPtrVec::erase with __declspec(noinline) on Set -> 19/19 DIFFER
    the same tree with the pin removed                -> 12/19 MATCH,
    while report.json moved by exactly 0 rows on both rulers.

Known limit: a 4-byte target body may be an /OPT:ICF BRANCH THUNK
(``b OnlyReturns``) rather than a fold member -- e.g.
?Terminate@VirtualKeyboard@@QAAXXZ at 0x825F5AD8 is ``b 0x823E3B70``.  The
.pdata table has no entry for those, so they land in the size-lookup fallback
rather than being reported as real byte differences.
"""
import struct, sys, os, re, collections, glob

DC3="/home/free/code/milohax/dc3-decomp"
EXE=DC3+"/orig/373307D9/ham_xbox_r.exe"
MAP=DC3+"/orig/373307D9/ham_xbox_r.map"

_d=open(EXE,"rb").read()
_lf=struct.unpack_from("<I",_d,0x3c)[0]; _c=_lf+4
_nsec=struct.unpack_from("<H",_d,_c+2)[0]; _osz=struct.unpack_from("<H",_d,_c+16)[0]
BASE=struct.unpack_from("<I",_d,_c+20+28)[0]
SECS=[]
for i in range(_nsec):
    r=_d[_c+20+_osz+i*40:_c+20+_osz+i*40+40]
    nm=r[0:8].rstrip(b"\0").decode(); vsz,va,rawsz,rawptr=struct.unpack_from("<IIII",r,8)
    SECS.append((nm,BASE+va,vsz,rawptr,rawsz))
def read_va(va,n):
    for nm,b,vsz,rp,rs in SECS:
        if b<=va<b+max(vsz,rs) and rs:
            return _d[rp+(va-b):rp+(va-b)+n]
    return None

# ---- .pdata: exact function extents, from the binary itself ----
PDATA={}
for nm,b,vsz,rp,rs in SECS:
    if nm==".pdata":
        n=vsz//8
        for i in range(n):
            beg,packed=struct.unpack_from(">II",_d,rp+i*8)
            if beg==0: continue
            flen=(packed>>8)&0x3FFFFF     # FunctionLength, in INSTRUCTIONS
            PDATA[beg]=flen*4
_PDKEYS=sorted(PDATA)
def _next_bound(va):
    import bisect
    c=[]
    i=bisect.bisect_right(_PDKEYS,va)
    if i<len(_PDKEYS): c.append(_PDKEYS[i])
    ks=sorted(A2S)
    j=bisect.bisect_right(ks,va)
    if j<len(ks): c.append(ks[j])
    return min(c) if c else va+0x400

def tgt_size(va):
    """Exact extent. .pdata is authoritative for functions with a prolog;
    leaf functions have no .pdata entry, so fall back to scanning to the
    terminator inside the next-symbol bound."""
    n=PDATA.get(va)
    if n: return n
    bound=min(_next_bound(va)-va,0x400)
    b=read_va(va,bound)
    if not b: return None
    for i in range(0,len(b)&~3,4):
        w=struct.unpack_from(">I",b,i)[0]
        if w==0x4E800020 or w==0x4E800420:   # blr / bctr
            return i+4
        if (w>>26)==18 and (w&3)==0 and i+4>=len(b):  # tail b
            return i+4
    return None

# ---- linker map ----
A2S=collections.defaultdict(list); S2A={}
_pat=re.compile(r"^\s+(\d{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(f\s*i?)\s+(\S+)\s*$")
for line in open(MAP,errors="replace"):
    m=_pat.match(line)
    if not m: continue
    _,_,sym,addr,fl,obj=m.groups()
    A2S[int(addr,16)].append(sym); S2A.setdefault(sym,(int(addr,16),obj))

# ---- our objects ----
def load_obj(path):
    d=open(path,"rb").read()
    nsec=struct.unpack_from("<H",d,2)[0]
    ptr,nsym=struct.unpack_from("<II",d,8)
    osz=struct.unpack_from("<H",d,16)[0]
    h=20+osz; secs=[]
    for i in range(nsec):
        r=d[h+i*40:h+i*40+40]
        vsz,va,rawsz,rawptr,relptr=struct.unpack_from("<IIIII",r,8)
        nrel=struct.unpack_from("<H",r,32)[0]
        secs.append((rawptr,rawsz,relptr,nrel))
    strt=ptr+nsym*18; syms={}; i=0
    while i<nsym:
        r=d[ptr+i*18:ptr+i*18+18]
        if r[0:4]==b"\0\0\0\0":
            o=struct.unpack_from("<I",r,4)[0]; e=d.index(b"\0",strt+o); s=d[strt+o:e].decode("latin1")
        else: s=r[0:8].rstrip(b"\0").decode("latin1")
        val,secn,typ,scl,naux=struct.unpack_from("<IhHBB",r,8)
        if secn>0 and s not in syms: syms[s]=(secn,val)
        i+=1+naux
    return d,secs,syms

def our_body(path,sym,length):
    d,secs,syms=load_obj(path)
    if sym not in syms: return None
    secn,val=syms[sym]; rawptr,rawsz,relptr,nrel=secs[secn-1]
    body=d[rawptr+val:rawptr+val+length]
    rel=set()
    for i in range(nrel):
        va=struct.unpack_from("<I",d,relptr+i*10)[0]
        if val<=va<val+length: rel.add(va-val)
    return body,rel

def mask(b,rel):
    a=bytearray(b)
    for i in range(0,len(a)&~3,4):
        w=struct.unpack_from(">I",a,i)[0]; op=w>>26
        if op in (16,18): struct.pack_into(">I",a,i,w&0xFC000003)
        elif i in rel:    struct.pack_into(">I",a,i,w&0xFFFF0000)
    return bytes(a)

# symbol -> our .obj  (built once)
_IDX=None
def index_objs(root):
    global _IDX
    if _IDX is not None: return _IDX
    _IDX={}
    for p in glob.glob(root+"/build/373307D9/src/**/*.obj",recursive=True):
        try: _,_,syms=load_obj(p)
        except Exception: continue
        for s in syms:
            _IDX.setdefault(s,[]).append(p)
    return _IDX

def check(root,sym):
    if sym not in S2A: return ("not-in-map",None,None)
    va,_=S2A[sym]
    n=tgt_size(va)
    if n is None: return ("no-pdata",va,None)
    tb=read_va(va,n)
    idx=index_objs(root)
    paths=idx.get(sym)
    if not paths: return ("we-do-not-define",va,n)
    for p in paths:
        got=our_body(p,sym,n)
        if not got: continue
        body,rel=got
        if len(body)!=n: continue
        if mask(body,rel)==mask(tb,rel): return ("MATCH",va,n)
    return ("DIFFER",va,n)

if __name__=="__main__":
    root=sys.argv[1]
    for sym in sys.argv[2:]:
        v,va,n=check(root,sym)
        print("%-18s %s  (va=%s size=%s)"%(v,sym,"%08x"%va if va else "-",n))
