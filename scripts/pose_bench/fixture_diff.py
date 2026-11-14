#!/usr/bin/env python3
"""Compare two dc3-pose-fixture v1 files frame by frame (PLAN W-20).

Frames are matched on their timestamp (to 0.1 ms).  Reports how many frames
each side tracked, and for frames both tracked the 3D joint distance (mean,
p50, p95, max, metres) and the same with each skeleton re-centred on its own
hip centre (pose shape without the root's placement).

  python3 -I scripts/pose_bench/fixture_diff.py A.dc3pose B.dc3pose [--shift-b N]

--shift-b N compares A's frame k with B's frame k+N: the sabotage control (one
frame of lag must read clearly worse than a real comparison).
Exit 0 always; the numbers are the result.
"""
import argparse
import math
import sys


def load(path):
    frames = []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = line.split()
            t, tracked = float(p[0]), p[1] == "1"
            v = [float(x) for x in p[2:]] if tracked else None
            frames.append((round(t, 4), v))
    return frames


def q(a, p):
    if not a:
        return float("nan")
    s = sorted(a)
    return s[min(len(s) - 1, int(p * len(s)))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--shift-b", type=int, default=0)
    a = ap.parse_args()
    fa, fb = load(a.a), load(a.b)
    ib = {t: i for i, (t, _) in enumerate(fb)}
    d, drel = [], []
    both = aonly = bonly = none = 0
    for t, va in fa:
        i = ib.get(t)
        if i is None:
            continue
        i += a.shift_b
        if i < 0 or i >= len(fb):
            continue
        vb = fb[i][1]
        if va and vb:
            both += 1
        elif va:
            aonly += 1
            continue
        elif vb:
            bonly += 1
            continue
        else:
            none += 1
            continue
        for k in range(20):
            x = va[k * 4:k * 4 + 3]
            y = vb[k * 4:k * 4 + 3]
            d.append(math.dist(x, y))
            xr = [x[j] - va[j] for j in range(3)]
            yr = [y[j] - vb[j] for j in range(3)]
            drel.append(math.dist(xr, yr))
    mean = lambda v: sum(v) / len(v) if v else float("nan")
    print(f"frames: both {both}  {a.a.split('/')[-1]} only {aonly}  {a.b.split('/')[-1]} only {bonly}  neither {none}"
          f"{'  (B shifted %+d frames)' % a.shift_b if a.shift_b else ''}")
    print(f"joint 3D mm: mean {mean(d) * 1000:.2f} p50 {q(d, .5) * 1000:.2f} p95 {q(d, .95) * 1000:.2f} max {q(d, 1) * 1000:.1f}")
    print(f"hip-relative mm: mean {mean(drel) * 1000:.2f} p50 {q(drel, .5) * 1000:.2f} p95 {q(drel, .95) * 1000:.2f} max {q(drel, 1) * 1000:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
