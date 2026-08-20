#!/usr/bin/env python3
"""Per-function ordered list of string-literal relocation targets, read straight from COFF."""
import struct, sys, os

def parse(path):
    d = open(path,'rb').read()
    machine, nsec, ts, symptr, nsym, optsz, chars = struct.unpack_from('<HHIIIHH', d, 0)
    secs=[]; off=20+optsz
    for k in range(nsec):
        raw=d[off:off+40]
        vsz,va,szraw,ptrraw,ptrrel,ptrln,nrel,nln,flags = struct.unpack_from('<IIIIIIHHI', raw, 8)
        secs.append(dict(idx=k+1,name=raw[0:8].rstrip(b'\0').decode('latin-1'),size=szraw,ptr=ptrraw,
                         ptrrel=ptrrel,nrel=nrel,flags=flags))
        off+=40
    strtab_off=symptr+nsym*18
    stl=struct.unpack_from('<I', d, strtab_off)[0]
    strtab=d[strtab_off:strtab_off+stl]
    symnames=[]; symrecs=[]
    i=0
    while i < nsym:
        rec=d[symptr+i*18:symptr+i*18+18]
        if rec[0:4]==b'\0\0\0\0':
            so=struct.unpack_from('<I',rec,4)[0]; e=strtab.index(b'\0',so)
            name=strtab[so:e].decode('latin-1')
        else:
            name=rec[0:8].rstrip(b'\0').decode('latin-1')
        value,secnum,typ,sclass,naux=struct.unpack_from('<IhHBB',rec,8)
        for k in range(1+naux):
            symnames.append(name); symrecs.append((value,secnum,sclass,naux))
        i+=1+naux
    return d,secs,symnames,symrecs

def func_string_relocs(path, func):
    d,secs,symnames,symrecs = parse(path)
    # find function symbol
    tgt=None
    for k,n in enumerate(symnames):
        if n==func:
            value,secnum,sclass,naux=symrecs[k]
            if 1<=secnum<=len(secs):
                tgt=(secs[secnum-1], value); break
    if tgt is None: return None
    s,val = tgt
    out=[]
    for r in range(s['nrel']):
        va,symidx,rtype = struct.unpack_from('<IIH', d, s['ptrrel']+r*10)
        nm = symnames[symidx] if symidx < len(symnames) else '?'
        if nm.startswith('??_C@'):
            out.append((va,nm))
    out.sort()
    # NOTE: do NOT dedupe consecutive duplicates.  A REFHI/REFLO pair for the
    # same literal is usually adjacent, but MSVC's scheduler can separate them
    # and can interleave two literals' pairs differently on the two sides.
    # Collapsing runs therefore invents count differences that are not there --
    # it manufactured a phantom "extra POPPING literal" in
    # App::RunWithoutDebugging where the raw counts are 2 and 2 on both sides.
    # Compare raw multisets; the doubling is harmless because it is symmetric.
    return [nm for va, nm in out]

if __name__=='__main__':
    print(func_string_relocs(sys.argv[1], sys.argv[2]))
