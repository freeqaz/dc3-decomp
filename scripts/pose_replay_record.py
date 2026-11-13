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
  .venv/bin/python scripts/pose_replay_record.py --frames DIR --times T.txt --mirror --out run.dc3pose

--times gives each frame's time explicitly ("<png name> <seconds>" per line;
frames not listed are skipped) -- for screenshots of the engine itself, whose
song time per render frame comes from DC3_POSE_PERFORM_CLOCKLOG and is not a
fixed rate.  MediaPipe still sees them at a steady 1000/fps ms apart.

Output: the dc3-pose-fixture v1 format of scripts/pose_fixture_record.py,
  <t_seconds> <tracked 0|1> [20 x (x y z conf)]
so either tool's file plays in either replayer.  Unlike pose_fixture_record.py
it keeps MediaPipeBackend's own placement (no rigid re-centring: the scorer is
translation invariant, the skeleton chooser is not), takes PNG frames with
explicit times, can mirror, and can choose among several people.  The person
is the lowest track id unless --pick says otherwise.

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


def frames_from(args, times):
    if args.frames:
        for path in sorted(glob.glob(os.path.join(args.frames, "*.png"))):
            name = os.path.basename(path)
            if times is not None and name not in times:
                continue
            yield cv2.imread(path), (times[name] if times is not None else None)
        return
    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        yield frame, None


def mean_x(joints):
    xs = [x for x, _, _, c in joints if c >= 0.5]
    return sum(xs) / len(xs) if xs else 0.0


def pick(people, how):
    if how == "minx":
        return min(people, key=lambda p: mean_x(p[1]))[1]
    if how == "maxx":
        return max(people, key=lambda p: mean_x(p[1]))[1]
    return min(people, key=lambda p: p[0])[1]


def main():
    ap = argparse.ArgumentParser()
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--video")
    src.add_argument("--frames", help="directory of *.png, processed in name order")
    ap.add_argument("--out", required=True)
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--t0", type=float, default=0.0, help="time of the first frame (s)")
    ap.add_argument("--times", help="with --frames: '<png name> <seconds>' per line")
    ap.add_argument("--mirror", action="store_true")
    ap.add_argument("--pick", choices=("track", "minx", "maxx"), default="track",
                    help="which person when there are several: lowest track id (as the "
                         "socket does), or the smallest / largest mean x of confident joints")
    ap.add_argument("--hfov", type=float, default=MediaPipeBackend.DEFAULT_HFOV_DEG)
    ap.add_argument("--model", default=os.path.join(ROOT, "native/models/pose_landmarker_full.task"))
    args = ap.parse_args()

    times = None
    if args.times:
        times = {}
        with open(args.times) as f:
            for line in f:
                parts = line.split()
                if len(parts) == 2 and not line.startswith("#"):
                    times[parts[0]] = float(parts[1])
    backend = MediaPipeBackend(args.model, num_poses=2, hfov_deg=args.hfov, min_conf=0.5)
    n = seen = 0
    with open(args.out, "w") as out:
        out.write("# dc3-pose-fixture v1\n")
        out.write(f"# pose_replay_record.py src={args.video or args.frames} fps={args.fps} "
                  f"mirror={int(args.mirror)} hfov={args.hfov}\n")
        for frame, t_given in frames_from(args, times):
            if args.mirror:
                frame = cv2.flip(frame, 1)
            t_ms = n * 1000.0 / args.fps
            people = backend.process(frame, t_ms)
            t = t_given if t_given is not None else args.t0 + t_ms / 1000.0
            if people:
                seen += 1
                joints = pick(people, args.pick)
                vals = " ".join(f"{x:.5f} {y:.5f} {z:.5f} {c:.3f}" for x, y, z, c in joints)
                out.write(f"{t:.4f} 1 {vals}\n")
            else:
                out.write(f"{t:.4f} 0\n")
            n += 1
    backend.close()
    print(f"{n} frames, person in {seen}, -> {args.out}")


if __name__ == "__main__":
    main()
