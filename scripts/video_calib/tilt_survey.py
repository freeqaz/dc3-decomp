#!/usr/bin/env python3
"""Camera-tilt survey: the numbers behind native/src/platform/PoseLevel_Native.h.

For each song (a fixture from fixture_transform.py --hold --mirror, aligned to
its per-song lead, and the reference recorded by dump_target.py):

  ref lean     median pitch (toward the camera) of the reference's own
               ankles -> HipCenter axis, every scored frame.  The median over
               songs is NativeGravityEstimator::kPriorPitchDeg: a levelled
               Kinect dancer shows it.
  player lean  the same axis on the player's fixture (camera space).
  opt pitch    the --pitch that minimises the mean limb-direction error
               (pose_vs_reference.py's metric over upper arms, forearms,
               thighs, shins), swept -4..21 deg in 1 deg steps.
  est pitch    player lean - prior: what the estimator converges to on a
               whole-song window.

  python3 -I scripts/video_calib/tilt_survey.py --fixtures DIR --targets DIR [--songs a,b]
"""
import argparse
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pose_vs_reference as pvr  # noqa: E402

L8 = {k: v for k, v in pvr.LIMBS.items()}


def lean(j):
    v = j[:, 0] - (j[:, 14] + j[:, 17]) / 2
    return np.degrees(np.arctan2(-v[:, 2], v[:, 1]))


def pitch(j, deg):
    c, s = np.cos(np.radians(deg)), np.sin(np.radians(deg))
    o = j.copy()
    o[..., 1] = j[..., 1] * c - j[..., 2] * s
    o[..., 2] = j[..., 1] * s + j[..., 2] * c
    return o


def limb_err(p, r):
    ang = pvr.limb_angles(pvr.normalise(p), pvr.normalise(r))
    return float(np.mean([v.mean() for v in ang.values()]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--targets", required=True)
    ap.add_argument("--songs", default="")
    a = ap.parse_args()
    songs = a.songs.split(",") if a.songs else sorted(
        os.path.basename(f).split(".")[0] for f in glob.glob(f"{a.fixtures}/*.dc3pose"))
    rows = []
    for s in songs:
        fts, fjs = pvr.load_fixture(f"{a.fixtures}/{s}.dc3pose")
        rts, rjs, moves = pvr.load_target(f"{a.targets}/{s}.target.jsonl")
        keep = np.array([m not in ("", "Rest.move", "Freestyle.move") for m in moves])
        rts, rjs = rts[keep], rjs[keep]
        f, ok = pvr.at(fts, fjs, rts)
        p, r = f[ok], rjs[ok]
        sweep = [(limb_err(pitch(p, d), r), d) for d in range(-4, 22)]
        best = min(sweep)
        e0 = dict((d, e) for e, d in sweep)[0]
        rl, pl = float(np.median(lean(r))), float(np.median(lean(p)))
        rows.append((rl, pl, best[1], best[0], e0))
        print(f"{s:22s} ref lean {rl:+5.1f}  player lean {pl:+5.1f}  opt pitch {best[1]:3d} "
              f"(limb err {best[0]:.2f} vs {e0:.2f} at 0)")
    o = np.array(rows)
    prior = float(np.median(o[:, 0]))
    est = o[:, 1] - prior
    print(f"prior (median ref lean) {prior:+.2f} sd {o[:, 0].std():.2f}; player lean {np.median(o[:, 1]):+.2f} "
          f"sd {o[:, 1].std():.2f}; opt pitch median {np.median(o[:, 2]):.1f} sd {o[:, 2].std():.2f}; "
          f"est - opt mean {np.mean(est - o[:, 2]):+.2f} sd {np.std(est - o[:, 2]):.2f}")


if __name__ == "__main__":
    sys.exit(main())
