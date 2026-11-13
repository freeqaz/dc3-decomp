#!/usr/bin/env python3
"""Reference run of the DESKTOP pose geometry over a directory of frames, for
checking the web port (native/web/pose/pose_geometry.js) against it.

Runs native/scripts/pose_mediapipe.py's MediaPipeBackend -- exactly what
pose_server.py runs -- over frame_*.png in order, at timestamp i * 1000/fps ms
(pose_server.py's video-mode clock), and writes per frame:
  raw:    the landmarker's poses (world xyz, image xy, visibility), so the JS
          geometry can be fed IDENTICAL landmarks (geometry-only parity), and
  joints: the DC3-20 output [(trackId, [[x,y,z,conf]*20])], what the desktop
          sends over the socket.

  .venv/bin/python scripts/web/pose_parity.py --frames DIR --out ref.json
  node scripts/web/pose_parity.mjs geometry ref.json      # JS geometry on raw
  node scripts/web/pose_parity.mjs browser ref.json --port 8432   # full JS pipeline
"""

import argparse
import glob
import json
import os
import sys

import cv2

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "native", "scripts"))
from pose_mediapipe import MediaPipeBackend  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", required=True, help="directory of frame_*.png")
    ap.add_argument("--out", required=True)
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--model", default=os.path.join(ROOT, "native/models/pose_landmarker_full.task"))
    args = ap.parse_args()

    frames = sorted(glob.glob(os.path.join(args.frames, "frame_*.png")))
    if not frames:
        sys.exit(f"no frame_*.png in {args.frames}")
    backend = MediaPipeBackend(args.model, num_poses=2, min_conf=0.5)

    # Capture the raw landmarker result of each process() call.
    real = backend._landmarker
    captured = {}

    class Tap:
        def detect_for_video(self, img, ts):
            res = real.detect_for_video(img, ts)
            poses = []
            for pi, wlms in enumerate(res.pose_world_landmarks):
                ilms = res.pose_landmarks[pi] if pi < len(res.pose_landmarks) else None
                poses.append({
                    "world": [[l.x, l.y, l.z] for l in wlms],
                    "image": [[l.x, l.y] for l in ilms] if ilms is not None else None,
                    "vis": [getattr(l, "visibility", 1.0) for l in ilms] if ilms is not None else None,
                })
            captured["poses"] = poses
            return res

        def close(self):
            real.close()

    backend._landmarker = Tap()
    out = {"fps": args.fps, "model": os.path.basename(args.model), "frames": []}
    for i, path in enumerate(frames):
        img = cv2.imread(path, cv2.IMREAD_COLOR)
        h, w = img.shape[:2]
        ts_ms = i * 1000.0 / args.fps
        people = backend.process(img, ts_ms)
        out["frames"].append({
            "file": os.path.basename(path), "tsMs": ts_ms, "width": w, "height": h,
            "raw": captured.get("poses", []),
            "joints": [{"trackId": tid, "joints": [list(j) for j in js]} for tid, js in people],
        })
    backend.close()
    with open(args.out, "w") as f:
        json.dump(out, f)
    n_person = sum(1 for fr in out["frames"] if fr["joints"])
    print(f"{len(frames)} frames, {n_person} with a person -> {args.out}")


if __name__ == "__main__":
    main()
