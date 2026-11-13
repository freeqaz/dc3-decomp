#!/usr/bin/env python3
"""Compare a recorded player fixture with the song's choreography reference.

Inputs: a dc3-pose-fixture v1 file keyed on song time (record_segment.py,
then fixture_transform.py --hold --mirror) and the reference the scorer uses,
recorded by dump_target.py.  Lead L follows DC3_POSE_PERFORM_LEAD_MS: the
reference at `ref` is compared with the player at `ref + L`.

Both skeletons are taken hip-centred (minus HipCenter) and divided by their
own torso length (HipCenter-ShoulderCenter), a crude stand-in for the
scorer's NormBoneLengths, so body size and camera distance drop out.

Reports
  * per lead: mean limb-direction error in degrees (upper arms, forearms,
    thighs, shins: the bones the scorer's limb errors are built on) and the
    mean normalised joint distance -- the lead that minimises them is the
    footage's timing offset seen by geometry alone, independent of scoring;
  * at the best lead: per joint, the mean signed offset player - reference
    per axis (x, y, z) in torso units and the RMS -- a systematic error shows
    as a mean that is large against its RMS (e.g. depth from monocular video).

  python3 -I scripts/video_calib/pose_vs_reference.py --fixture F --target T [--leads -400:400:25] [--json out]
"""
import argparse
import json
import sys

import numpy as np

NAMES = ["HipCenter", "Spine", "ShoulderCenter", "Head", "ShoulderLeft", "ElbowLeft", "WristLeft",
         "HandLeft", "ShoulderRight", "ElbowRight", "WristRight", "HandRight", "HipLeft", "KneeLeft",
         "AnkleLeft", "HipRight", "KneeRight", "AnkleRight", "FootLeft", "FootRight"]
LIMBS = {"upper_arm_L": (4, 5), "forearm_L": (5, 6), "upper_arm_R": (8, 9), "forearm_R": (9, 10),
         "thigh_L": (12, 13), "shin_L": (13, 14), "thigh_R": (15, 16), "shin_R": (16, 17)}


def load_fixture(path):
    ts, js = [], []
    for line in open(path):
        if line.startswith("#"):
            continue
        p = line.split()
        if len(p) < 2 or p[1] != "1":
            continue
        ts.append(float(p[0]))
        js.append(np.array(p[2:], dtype=float).reshape(20, 4)[:, :3])
    return np.array(ts), np.array(js)


def load_target(path):
    ts, js, moves = [], [], []
    for line in open(path):
        d = json.loads(line)
        ts.append(d["ref"])
        js.append(np.array(d["joints"], dtype=float))
        moves.append(d.get("move") or "")
    o = np.argsort(ts)
    return np.array(ts)[o], np.array(js)[o], [moves[i] for i in o]


def normalise(j):
    c = j - j[:, :1, :]
    torso = np.linalg.norm(j[:, 2] - j[:, 0], axis=1)
    return c / torso[:, None, None]


def at(ts, js, t):
    """Latest frame at or before each t (a camera shows what it last captured)."""
    i = np.clip(np.searchsorted(ts, t, side="right") - 1, 0, len(ts) - 1)
    ok = (t >= ts[0]) & (t <= ts[-1] + 0.1)
    return js[i], ok


def limb_angles(a, b):
    out = {}
    for name, (p, q) in LIMBS.items():
        u = a[:, q] - a[:, p]
        v = b[:, q] - b[:, p]
        cos = (u * v).sum(1) / (np.linalg.norm(u, axis=1) * np.linalg.norm(v, axis=1) + 1e-9)
        out[name] = np.degrees(np.arccos(np.clip(cos, -1, 1)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--target", required=True)
    ap.add_argument("--leads", default="-400:400:25", help="ms, start:stop:step")
    ap.add_argument("--json")
    a = ap.parse_args()
    fts, fjs = load_fixture(a.fixture)
    rts, rjs, moves = load_target(a.target)
    keep = np.array([m not in ("", "Rest.move", "Freestyle.move") for m in moves])
    rts, rjs = rts[keep], rjs[keep]
    rn = normalise(rjs)
    lo, hi, st = (int(v) for v in a.leads.split(":"))
    curve = []
    for lead in range(lo, hi + 1, st):
        f, ok = at(fts, fjs, rts + lead / 1000.0)
        fn = normalise(f)
        ang = limb_angles(fn[ok], rn[ok])
        dist = np.linalg.norm(fn[ok] - rn[ok], axis=2).mean()
        curve.append({"lead_ms": lead, "limb_deg": round(float(np.mean([v.mean() for v in ang.values()])), 2),
                      "dist": round(float(dist), 4), "n": int(ok.sum())})
    best = min(curve, key=lambda c: c["limb_deg"])
    f, ok = at(fts, fjs, rts + best["lead_ms"] / 1000.0)
    d = normalise(f)[ok] - rn[ok]
    ang = limb_angles(normalise(f)[ok], rn[ok])
    per_joint = {NAMES[j]: {"mean_xyz": [round(float(d[:, j, k].mean()), 3) for k in range(3)],
                            "rms_xyz": [round(float(np.sqrt((d[:, j, k] ** 2).mean())), 3) for k in range(3)]}
                 for j in range(20)}
    res = {"fixture": a.fixture, "target": a.target, "samples": int(ok.sum()), "curve": curve,
           "best": best, "limb_deg_at_best": {k: round(float(v.mean()), 1) for k, v in ang.items()},
           "per_joint_torso_units": per_joint,
           "torso_m": {"player": round(float(np.median(np.linalg.norm(f[ok][:, 2] - f[ok][:, 0], axis=1))), 3),
                       "reference": round(float(np.median(np.linalg.norm(rjs[ok][:, 2] - rjs[ok][:, 0], axis=1))), 3)}}
    print(f"{a.fixture.split('/')[-1]}: best lead {best['lead_ms']} ms, limb error {best['limb_deg']} deg "
          f"(curve: " + " ".join(f"{c['lead_ms']}:{c['limb_deg']}" for c in curve[::2]) + ")")
    if a.json:
        with open(a.json, "w") as fo:
            json.dump(res, fo, indent=1)


if __name__ == "__main__":
    sys.exit(main())
