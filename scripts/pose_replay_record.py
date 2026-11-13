#!/usr/bin/env python3
"""Run the desktop MediaPipe pose backend OFFLINE over a video and write the
DC3-20 stream it would have sent, as a replay file for the in-process
performer (DC3_POSE_PERFORM=replay:FILE, native/src/platform/PosePerformer_Native.h).

Same backend, model and geometry as pose_server.py; frames are processed in
order at timestamp i * 1000 / fps ms (pose_server.py's video clock), so the
output is deterministic for a given video and model.  The engine then replays
it keyed on song time -- no socket, no wall clock -- which is what makes a
MediaPipe-on-video scoring run repeatable.

  .venv/bin/python scripts/pose_replay_record.py --video clip.y4m --out clip.dc3pose
  .venv/bin/python scripts/pose_replay_record.py --frames DIR --fps 30 --mirror --out run.dc3pose

Output: '#' header lines, then one line per frame,
  t x0 y0 z0 c0 ... x19 y19 z19 c19
t in seconds; the person with the lowest track id (frames with nobody are
written with every confidence 0, which the engine's joint hold treats as
"keep the last good position", exactly as on the socket).

--mirror flips each frame horizontally first.  Use it on footage of the
ON-SCREEN dancer: a player mirrors the dancer, and the camera faces the
player, so the camera's view of a perfect player is the dancer's image
flipped left-right.
"""

import argparse
import glob
import os
import sys

import cv2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "native", "scripts"))
from pose_mediapipe import MediaPipeBackend  # noqa: E402


def frames_from(args):
    if args.frames:
        for path in sorted(glob.glob(os.path.join(args.frames, "*.png"))):
            yield cv2.imread(path)
        return
    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        yield frame


def main():
    ap = argparse.ArgumentParser()
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--video")
    src.add_argument("--frames", help="directory of *.png, processed in name order")
    ap.add_argument("--out", required=True)
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--t0", type=float, default=0.0, help="time of the first frame (s)")
    ap.add_argument("--mirror", action="store_true")
    ap.add_argument("--hfov", type=float, default=MediaPipeBackend.DEFAULT_HFOV_DEG)
    ap.add_argument("--model", default=os.path.join(ROOT, "native/models/pose_landmarker_full.task"))
    args = ap.parse_args()

    backend = MediaPipeBackend(args.model, num_poses=2, hfov_deg=args.hfov, min_conf=0.5)
    n = seen = 0
    with open(args.out, "w") as out:
        out.write(f"# pose_replay_record.py src={args.video or args.frames} fps={args.fps} "
                  f"mirror={int(args.mirror)} hfov={args.hfov}\n")
        for frame in frames_from(args):
            if args.mirror:
                frame = cv2.flip(frame, 1)
            t_ms = n * 1000.0 / args.fps
            people = backend.process(frame, t_ms)
            if people:
                seen += 1
                _, joints = min(people, key=lambda p: p[0])
            else:
                joints = [(0.0, 0.0, 0.0, 0.0)] * 20
            vals = " ".join(f"{x:.5f} {y:.5f} {z:.5f} {c:.3f}" for x, y, z, c in joints)
            out.write(f"{args.t0 + t_ms / 1000.0:.4f} {vals}\n")
            n += 1
    backend.close()
    print(f"{n} frames, person in {seen}, -> {args.out}")


if __name__ == "__main__":
    main()
