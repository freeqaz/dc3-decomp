#!/usr/bin/env python3
"""joint_error.py -- how far each estimate of a cut-off joint is from where
the joint really was (PLAN H-90 step 3a).

  python3 -I scripts/pose_cutoff/joint_error.py TRUTH CUT [--est NAME=FILE ...]

TRUTH is the uncut fixture (the full frame), CUT the same take with part of
the body cut away (record_cut.py, or cutoff.py cut).  For every frame where a
joint is NotTracked (conf < 0.3) in CUT but tracked (conf >= 0.6) in TRUTH,
the error is the distance between its hip-relative position in each
estimate and in TRUTH, metres.  Rows:

  off      what the scorer got before DC3_POSE_ESTIMATE: the joint frozen at
           its last position with conf >= 0.3 (FillSkeletonData's hold), or
           the tracker's guess if it was never seen;
  tracker  the tracker's guess (CUT's own position);
  NAME     each --est file (estimate_tool output over CUT).
"""
import argparse
import sys

import numpy as np

GROUPS = {"knees": (13, 16), "ankles": (14, 17), "feet": (18, 19), "head": (3,),
          "elbows": (5, 9), "wrists": (6, 10), "hands": (7, 11)}


def load(path):
    d = {}
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        f = line.split()
        if f[1] != "1":
            continue
        d[round(float(f[0]) * 30)] = np.array(f[2:2 + 80], float).reshape(20, 4)
    return d


def freeze(cut):
    out, last = {}, {}
    for k in sorted(cut):
        fr = cut[k].copy()
        for j in range(20):
            if fr[j, 3] >= 0.3:
                last[j] = fr[j, :3].copy()
            elif j in last:
                fr[j, :3] = last[j]
        out[k] = fr
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("truth")
    ap.add_argument("cut")
    ap.add_argument("--est", action="append", default=[])
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    truth, cut = load(a.truth), load(a.cut)
    rows = [("off", freeze(cut)), ("tracker", cut)]
    for e in a.est:
        n, p = e.split("=", 1)
        rows.append((n, load(p)))
    keys = sorted(set(truth) & set(cut))
    print(f"{a.label:22s} {'method':10s} " + " ".join(f"{g:>12s}" for g in GROUPS))
    for name, est in rows:
        cells = []
        for g, js in GROUPS.items():
            errs = []
            for k in keys:
                if k not in est:
                    continue
                T, C, E = truth[k], cut[k], est[k]
                for j in js:
                    if C[j, 3] < 0.3 and T[j, 3] >= 0.6:
                        errs.append(np.linalg.norm((E[j, :3] - E[0, :3]) - (T[j, :3] - T[0, :3])))
            cells.append(f"{np.median(errs):6.3f} n{len(errs):<5d}" if len(errs) >= 30 else f"{'-':>12s}")
        print(f"{a.label:22s} {name:10s} " + " ".join(cells))
    return 0


if __name__ == "__main__":
    sys.exit(main())
