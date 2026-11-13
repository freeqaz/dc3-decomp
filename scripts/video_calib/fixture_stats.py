#!/usr/bin/env python3
"""Sanity and jitter statistics for dc3-pose-fixture v1 files.

Per file: frame count and rate, tracked fraction, median bone lengths (the
same bones as POSE_INPUT.md's "Our skeletons vs a Kinect's" table), mean
joint speed, and still-body jitter.

Jitter is measured the way POSE_INPUT.md quotes the Kinect's: the median
absolute frame-to-frame step per axis, but only over STILL windows -- runs
of >= --still-frames consecutive tracked frames whose hip-centre speed and
mean wrist speed stay under --still-speed m/s (a dancer between songs, a
held pose).  Reported for the body (all joints but hands/wrists) and for the
hands separately.  A frame-to-frame step on a still body is noise plus any
residual motion, so it is an upper bound on sensor jitter.

  python3 -I scripts/video_calib/fixture_stats.py a.dc3pose b.dc3pose [--json out.json]
"""
import argparse
import json
import sys

import numpy as np

# DC3 / Kinect joint order (native/scripts/pose_mediapipe.py)
HIP_C, SPINE, SHOULDER_C, HEAD = 0, 1, 2, 3
SHOULDER_L, ELBOW_L, WRIST_L, HAND_L = 4, 5, 6, 7
SHOULDER_R, ELBOW_R, WRIST_R, HAND_R = 8, 9, 10, 11
HANDS = (WRIST_L, HAND_L, WRIST_R, HAND_R)
BONES = {
    "upper_arm": [(SHOULDER_L, ELBOW_L), (SHOULDER_R, ELBOW_R)],
    "forearm": [(ELBOW_L, WRIST_L), (ELBOW_R, WRIST_R)],
    "shoulders": [(SHOULDER_L, SHOULDER_R)],
    "torso": [(SHOULDER_C, HIP_C)],
}


def load(path):
    ts, tracked, joints = [], [], []
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        p = line.split()
        ts.append(float(p[0]))
        tr = p[1] == "1"
        tracked.append(tr)
        if tr:
            v = np.array(p[2:], dtype=float).reshape(20, 4)
        else:
            v = np.full((20, 4), np.nan)
        joints.append(v)
    return np.array(ts), np.array(tracked), np.array(joints)


def still_mask(ts, tracked, xyz, speed, min_frames):
    dt = np.diff(ts)
    hip_v = np.linalg.norm(np.diff(xyz[:, HIP_C], axis=0), axis=1) / dt
    wr_v = (np.linalg.norm(np.diff(xyz[:, WRIST_L], axis=0), axis=1)
            + np.linalg.norm(np.diff(xyz[:, WRIST_R], axis=0), axis=1)) / 2 / dt
    ok = tracked[1:] & tracked[:-1] & (hip_v < speed) & (wr_v < speed)
    ok = np.nan_to_num(ok).astype(bool)
    mask = np.zeros_like(ok)
    run = 0
    for i, v in enumerate(ok):
        run = run + 1 if v else 0
        if run >= min_frames:
            mask[i - run + 1: i + 1] = True
    return mask  # over steps (len n-1)


def stats(path, still_speed, still_frames):
    ts, tracked, j = load(path)
    xyz = j[:, :, :3]
    out = {"file": path, "frames": int(len(ts)),
           "fps": round(float((len(ts) - 1) / (ts[-1] - ts[0])), 3) if len(ts) > 1 else 0,
           "tracked_frac": round(float(tracked.mean()), 4)}
    good = xyz[tracked]
    out["bones_cm"] = {}
    for name, pairs in BONES.items():
        ls = np.concatenate([np.linalg.norm(good[:, a] - good[:, b], axis=1) for a, b in pairs])
        out["bones_cm"][name] = round(float(np.median(ls)) * 100, 1)
    step = np.diff(xyz, axis=0)  # n-1, 20, 3
    dt = np.diff(ts)[:, None]
    both = tracked[1:] & tracked[:-1]
    sp = np.linalg.norm(step[both], axis=2) / dt[both]
    out["mean_joint_speed_mps"] = round(float(np.nanmean(sp)), 3)
    out["wrist_speed_p99_mps"] = round(float(np.nanpercentile(sp[:, [WRIST_L, WRIST_R]], 99)), 2)
    m = still_mask(ts, tracked, xyz, still_speed, still_frames)
    out["still_steps"] = int(m.sum())
    if m.sum() > 10:
        s = np.abs(step[m])  # k, 20, 3
        body = [i for i in range(20) if i not in HANDS]
        out["still_jitter_mm"] = {
            "body_xyz": [round(float(np.median(s[:, body, a])) * 1000, 1) for a in range(3)],
            "hands_xyz": [round(float(np.median(s[:, list(HANDS), a])) * 1000, 1) for a in range(3)],
        }
        # per joint z, to see which joints carry depth noise
        out["still_jitter_z_per_joint_mm"] = [round(float(np.median(s[:, i, 2])) * 1000, 1)
                                              for i in range(20)]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--still-speed", type=float, default=0.15, help="m/s, hip and wrists")
    ap.add_argument("--still-frames", type=int, default=15, help="min run length (frames)")
    ap.add_argument("--json")
    a = ap.parse_args()
    res = [stats(f, a.still_speed, a.still_frames) for f in a.files]
    for r in res:
        sj = r.get("still_jitter_mm", {})
        print(f"{r['file'].split('/')[-1]:32s} n={r['frames']:5d} fps={r['fps']:.2f} "
              f"tracked={r['tracked_frac']:.3f} bones={r['bones_cm']} speed={r['mean_joint_speed_mps']} "
              f"still={r['still_steps']} body={sj.get('body_xyz')} hands={sj.get('hands_xyz')}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(res, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
