#!/usr/bin/env python3
"""Summarise pose_vs_reference.py results over many songs.

Reads PVR_DIR/<song>.<A>.json and PVR_DIR/<song>.<B>.json (two fixture
variants, e.g. pose_hold_mirror and pose_hmp10 = the same plus --pitch 10)
and prints, per song in video order: the geometric best lead and the limb
error of both variants; the lead's linear trend against video time (a drift
in the recording's camera-vs-game sync would show as a slope); and, over all
songs, the median per-joint signed offset player - reference and its RMS in
torso units for variant A.

  python3 -I scripts/video_calib/pvr_summary.py --pvr-dir PVR --songs-tsv songs.tsv \\
      [--a pose_hold_mirror --b pose_hmp10]

songs.tsv: <song> <t0 video seconds> per line.
"""
import argparse
import json
import os
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pvr-dir", required=True)
    ap.add_argument("--songs-tsv", required=True)
    ap.add_argument("--a", default="pose_hold_mirror")
    ap.add_argument("--b", default="pose_hmp10")
    a = ap.parse_args()
    t0 = {}
    for line in open(a.songs_tsv):
        p = line.split()
        if len(p) >= 2:
            t0[p[0]] = float(p[1])
    rows = []
    for song, t in t0.items():
        fa = os.path.join(a.pvr_dir, f"{song}.{a.a}.json")
        fb = os.path.join(a.pvr_dir, f"{song}.{a.b}.json")
        if not (os.path.exists(fa) and os.path.exists(fb)):
            continue
        ra, rb = json.load(open(fa)), json.load(open(fb))
        rows.append((t, song, ra["best"]["lead_ms"], ra["best"]["limb_deg"], rb["best"]["limb_deg"], ra))
    if not rows:
        print("no results")
        return 1
    rows.sort()
    for t, s, lead, da, db, _ in rows:
        print(f"{t:7.1f} {s:24s} lead {lead:5d} ms  limb {da:5.2f} -> {a.b} {db:5.2f} deg")
    t = np.array([r[0] for r in rows])
    lead = np.array([r[2] for r in rows])
    if len(rows) > 2:
        fit = np.polyfit(t, lead, 1)
        print(f"lead vs video time: slope {fit[0] * 3600:.1f} ms/h, r={np.corrcoef(t, lead)[0, 1]:.3f}, "
              f"residual sd {np.std(lead - np.polyval(fit, t)):.0f} ms; median lead {np.median(lead):.0f} ms")
    print(f"median limb error {np.median([r[3] for r in rows]):.2f} deg, {a.b} {np.median([r[4] for r in rows]):.2f}; "
          f"{a.b} lower in {sum(r[4] < r[3] for r in rows)}/{len(rows)}")
    names = list(rows[0][5]["per_joint_torso_units"])
    mean = np.array([[r[5]["per_joint_torso_units"][n]["mean_xyz"] for n in names] for r in rows])
    rms = np.array([[r[5]["per_joint_torso_units"][n]["rms_xyz"] for n in names] for r in rows])
    print(f"{'joint':16s} mean x/y/z           rms x/y/z  (torso units, median over songs, {a.a})")
    for i, n in enumerate(names):
        print(f"{n:16s} " + " ".join(f"{v:+.2f}" for v in np.median(mean[:, i], 0)) + "   "
              + " ".join(f"{v:.2f}" for v in np.median(rms[:, i], 0)))
    limbs = list(rows[0][5]["limb_deg_at_best"])
    lim = np.array([[r[5]["limb_deg_at_best"][k] for k in limbs] for r in rows])
    print("limb error, median deg: " + ", ".join(f"{k} {v:.1f}" for k, v in zip(limbs, np.median(lim, 0))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
