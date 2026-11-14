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

TRUTH, CUT and each --est FILE may be comma lists of the same length (one
entry per take); the errors are then pooled over the takes.
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
    truths = [load(p) for p in a.truth.split(",")]
    cuts = [load(p) for p in a.cut.split(",")]
    if len(truths) != len(cuts):
        sys.exit("TRUTH and CUT lists differ in length")
    rows = [("off", [freeze(c) for c in cuts]), ("tracker", cuts)]
    for e in a.est:
        n, p = e.split("=", 1)
        ests = [load(x) for x in p.split(",")]
        if len(ests) != len(cuts):
            sys.exit(f"--est {n}: {len(ests)} files for {len(cuts)} takes")
        rows.append((n, ests))
    print(f"{a.label:22s} {'method':10s} " + " ".join(f"{g:>12s}" for g in GROUPS))
    for name, ests in rows:
        cells = []
        for g, js in GROUPS.items():
            errs = []
            for truth, cut, est in zip(truths, cuts, ests):
                for k in sorted(set(truth) & set(cut) & set(est)):
                    T, C, E = truth[k], cut[k], est[k]
                    for j in js:
                        if C[j, 3] < 0.3 and T[j, 3] >= 0.6:
                            errs.append(np.linalg.norm((E[j, :3] - E[0, :3]) - (T[j, :3] - T[0, :3])))
            cells.append(f"{np.median(errs):6.3f} n{len(errs):<5d}" if len(errs) >= 30 else f"{'-':>12s}")
        print(f"{a.label:22s} {name:10s} " + " ".join(cells))
    return 0


if __name__ == "__main__":
    sys.exit(main())
