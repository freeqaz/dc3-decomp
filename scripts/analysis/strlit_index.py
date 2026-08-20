#!/usr/bin/env python3
"""Index every ??_C@ string COMDAT across a set of COFF objects."""
import sys, os, json, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from strlit import read_coff_symbols

def build_index(root):
    idx = {}
    for p in glob.glob(os.path.join(root,'**','*.obj'), recursive=True):
        try:
            if os.path.getsize(p) == 0: continue
            d, secs, syms = read_coff_symbols(p)
        except Exception:
            continue
        for name,(s,val) in syms.items():
            b = d[s['ptr']+val : s['ptr']+val+s['size']]
            idx.setdefault(name, []).append((p, b))
    return idx

if __name__ == '__main__':
    idx = build_index(sys.argv[1])
    print(len(idx), 'string comdats')
    json.dump({k:[(p, v.hex()) for p,v in vs] for k,vs in idx.items()}, open(sys.argv[2],'w'))
