#!/usr/bin/env python3
"""Fast numpy scanner for DancerFrame runs inside decompressed milo data.

Frame record (big-endian, 488 bytes):
  s16 moveIdx, s16 moveFrameIdx, 20 x (Vector3 pos, Vector3 disp), s32 elapsedMs
"""
import sys, os, struct, numpy as np
sys.path.insert(0, "/home/free/code/milohax/dc3-decomp/scripts/milo")
from inflate_milo import decompress_milo

PER = 488

def candidates(b):
    n = len(b)
    if n < PER + 8: return np.array([], dtype=np.int64)
    a = np.frombuffer(b, dtype=np.uint8)
    lim = n - PER - 4
    hi = a[:lim]                    # moveIdx high byte
    hi2 = a[2:lim+2]                # moveFrameIdx high byte
    ms = a[484:lim+484]             # elapsedMs high byte
    m = ((hi == 0) | (hi == 1) | (hi == 0xFF)) & \
        ((hi2 == 0) | (hi2 == 1) | (hi2 == 0xFF)) & \
        ((ms == 0) | (ms == 0xFF))
    return np.flatnonzero(m).astype(np.int64)

def plausible(b, o):
    if o + PER > len(b): return False
    mi, mfi = struct.unpack_from(">hh", b, o)
    if not (-1 <= mi <= 400 and -1 <= mfi <= 400): return False
    v = np.frombuffer(b, dtype=">f4", count=120, offset=o+4)
    p = v.reshape(20, 6)
    if not np.all(np.isfinite(p)): return False
    if not (np.all(np.abs(p[:, 0]) < 3.0) and np.all(np.abs(p[:, 1]) < 3.0)): return False
    if not (np.all(p[:, 2] > 0.3) and np.all(p[:, 2] < 8.0)): return False
    if not np.all(np.abs(p[:, 3:6]) < 3.0): return False
    ms, = struct.unpack_from(">i", b, o+484)
    return -1 <= ms <= 5000

def scan(b):
    cand = set(candidates(b).tolist())
    runs = []
    consumed = set()
    for o in sorted(cand):
        if o in consumed: continue
        if not plausible(b, o): continue
        j, cnt = o, 0
        while plausible(b, j):
            consumed.add(j); j += PER; cnt += 1
        if cnt >= 4:
            ver = False
            if o >= 4:
                v, = struct.unpack_from(">I", b, o-4)
                ver = (v == cnt)
            runs.append((o, cnt, ver))
    return runs

def main(paths):
    tf = ts = tv = 0
    for p in paths:
        try:
            d, _ = decompress_milo(p); d = bytes(d)
        except Exception as e:
            print(f"SKIP {p}: {e}"); continue
        runs = scan(d)
        f = sum(c for _, c, _ in runs)
        v = sum(1 for _, _, e in runs if e)
        tf += f; ts += len(runs); tv += v
        print(f"{os.path.basename(os.path.dirname(os.path.dirname(p)))+'/'+os.path.basename(p):55s} seqs={len(runs):4d} verified={v:4d} frames={f:7d}", flush=True)
    print(f"TOTAL sequences={ts} verified={tv} frames={tf}")

if __name__ == "__main__":
    main(sys.argv[1:])
