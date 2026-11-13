#!/usr/bin/env python3
"""Record a deterministic DC3-20 keypoint fixture from a video (PLAN P-5b).

Runs the desktop MediaPipe backend (native/scripts/pose_mediapipe.py, the same
code pose_server.py runs) over every frame of a video OFFLINE -- timestamps from
the frame index, not the wall clock -- and writes the stream of DC3's 20 joints
in camera-space metres as a small text file. Replaying that file
(scripts/synthetic_kinect.py --replay) keyed on song time is deterministic in a
way pose_server.py --video never is: no inference in the loop, no wall-clock
pacing, the same joints at the same song time on every run.

Placement: MediaPipe's absolute root comes from a pinhole fit with an assumed
FOV, which for a rendered clip is arbitrary. One rigid translation is applied to
the WHOLE clip -- mean hip centre to x = 0, z = DEPTH, lowest foot to y = 0 --
so the performer stands where a player would; the motion is untouched.

Format (text, one frame per line after the header):
    # dc3-pose-fixture v1
    # source=<video> fps=<f> frames=<n> model=<task> depth=<m> shift=<dx,dy,dz>
    <t_seconds> <tracked 0|1> [20 x (x y z conf)]
Untracked frames carry no joints (the replayer holds the previous pose).

Usage:
    .venv/bin/python scripts/pose_fixture_record.py \
        archive/screenshots-old/woot/taye01_stand_dance.mp4 \
        native/tests/data/pose/taye01_stand_dance.dc3pose
"""

import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))

HIP_C, FOOT_L, FOOT_R = 0, 18, 19
DEPTH = 2.5


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("video")
    ap.add_argument("out")
    ap.add_argument("--model", default=os.path.join(REPO, "native/models/pose_landmarker_full.task"))
    ap.add_argument("--depth", type=float, default=DEPTH)
    a = ap.parse_args()

    import cv2
    from pose_mediapipe import MediaPipeBackend

    backend = MediaPipeBackend(a.model, num_poses=1)
    cap = cv2.VideoCapture(a.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {a.video}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    frames = []
    i = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        people = backend.process(frame, int(round(i * 1000.0 / fps)))
        frames.append(people[0][1] if people else None)
        i += 1
    cap.release()

    tracked = [f for f in frames if f is not None]
    if not tracked:
        sys.exit("no person detected in any frame")
    mx = sum(f[HIP_C][0] for f in tracked) / len(tracked)
    mz = sum(f[HIP_C][2] for f in tracked) / len(tracked)
    floor = min(min(f[FOOT_L][1], f[FOOT_R][1]) for f in tracked)
    shift = (-mx, -floor, a.depth - mz)

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as out:
        out.write("# dc3-pose-fixture v1\n")
        out.write(f"# source={os.path.basename(a.video)} fps={fps:g} frames={len(frames)} "
                  f"model={os.path.basename(a.model)} depth={a.depth:g} "
                  f"shift={shift[0]:.4f},{shift[1]:.4f},{shift[2]:.4f}\n")
        for k, f in enumerate(frames):
            t = k / fps
            if f is None:
                out.write(f"{t:.4f} 0\n")
                continue
            vals = []
            for x, y, z, c in f:
                vals += [x + shift[0], y + shift[1], z + shift[2], c]
            out.write(f"{t:.4f} 1 " + " ".join(f"{v:.4f}" for v in vals) + "\n")
    print(f"{a.out}: {len(frames)} frames, {len(tracked)} tracked, "
          f"{os.path.getsize(a.out)} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
