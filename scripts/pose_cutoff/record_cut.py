#!/usr/bin/env python3
"""record_cut.py -- run the desktop MediaPipe backend over a camera-shaped clip
with part of the picture cut away, and write a dc3-pose-fixture v1 file keyed
on song time (PLAN H-90).

The clips are scripts/playtest/make_footage.py's: 640x480, 30 fps, the song's
time 0 at --t0 (the clip's pre-roll, 8 s).  A cut is applied by ffmpeg before
MediaPipe sees the frame:

  --cut-bottom ROW   rows ROW..479 black (the frame ends at that row)
  --cut-top ROW      rows 0..ROW-1 black
  --cut-left COL     columns 0..COL-1 black
  --cut-right COL    columns COL..639 black
  --zoom F           the centre F x F of the frame scaled back to 640x480
                     (a camera with a narrower view: the dancer too close);
                     --hfov is narrowed to match, so metres stay metres

Black bars stand in for the frame edge: MediaPipe pads its person crop with
zeros past the image border, so a black region and the outside of the image
are the same input to the landmark model (INFERRED from MediaPipe's ROI
letterboxing, not measured).  Same backend, model and VIDEO-mode clock as
pose_replay_record.py / video_calib/record_segment.py, so a run is
deterministic for a given clip and cut.

  python3 -I scripts/pose_cutoff/record_cut.py --video macarena.mp4 --t0 8 \
      --start -3 --dur 165 --shift 0.137 --cut-bottom 330 --out macarena_knees.dc3pose

Output lines: <song_t> <tracked 0|1> [20 x (x y z conf)], conf = MediaPipe
visibility (min over the landmarks a DC3 joint is built from).
"""
import argparse
import math
import os
import subprocess
import sys

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))
from pose_mediapipe import MediaPipeBackend  # noqa: E402

W, H = 640, 480


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True)
    ap.add_argument("--t0", type=float, default=8.0, help="clip time of song time 0 (s)")
    ap.add_argument("--start", type=float, default=-3.0, help="segment start, song time (s)")
    ap.add_argument("--dur", type=float, required=True)
    ap.add_argument("--shift", type=float, default=0.0,
                    help="add to every song time (the dancer's per-song lead, as "
                         "fixture_transform.py --shift)")
    ap.add_argument("--cut-bottom", type=int)
    ap.add_argument("--cut-top", type=int)
    ap.add_argument("--cut-left", type=int)
    ap.add_argument("--cut-right", type=int)
    ap.add_argument("--zoom", type=float)
    ap.add_argument("--hfov", type=float, default=74.6,
                    help="horizontal FOV of the 640-px clip (make_footage: a 1056-px crop of "
                         "a 58.51-deg stream camera shown 470 px wide -> 74.6 deg)")
    ap.add_argument("--model", default=os.path.join(REPO, "native/models/pose_landmarker_full.task"))
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    vf = []
    hfov = a.hfov
    if a.zoom:
        cw, ch = int(round(W * a.zoom / 2)) * 2, int(round(H * a.zoom / 2)) * 2
        vf.append(f"crop={cw}:{ch}:{(W - cw) // 2}:{(H - ch) // 2},scale={W}:{H}")
        hfov = math.degrees(2 * math.atan(math.tan(math.radians(a.hfov) / 2) * cw / W))
    if a.cut_bottom is not None:
        vf.append(f"drawbox=x=0:y={a.cut_bottom}:w={W}:h={H - a.cut_bottom}:color=black:t=fill")
    if a.cut_top is not None:
        vf.append(f"drawbox=x=0:y=0:w={W}:h={a.cut_top}:color=black:t=fill")
    if a.cut_left is not None:
        vf.append(f"drawbox=x=0:y=0:w={a.cut_left}:h={H}:color=black:t=fill")
    if a.cut_right is not None:
        vf.append(f"drawbox=x={a.cut_right}:y=0:w={W - a.cut_right}:h={H}:color=black:t=fill")
    vf.append("fps=30")
    ss = a.t0 + a.start
    cmd = ["ffmpeg", "-nostdin", "-loglevel", "error", "-ss", f"{ss:.3f}", "-i", a.video,
           "-t", f"{a.dur:.3f}", "-vf", ",".join(vf), "-an", "-f", "rawvideo",
           "-pix_fmt", "bgr24", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    backend = MediaPipeBackend(a.model, num_poses=1, hfov_deg=hfov, min_conf=0.5)
    n = seen = 0
    size = W * H * 3
    with open(a.out, "w") as out:
        out.write("# dc3-pose-fixture v1\n")
        out.write(f"# record_cut.py video={os.path.basename(a.video)} t0={a.t0} start={a.start} "
                  f"dur={a.dur} shift={a.shift} vf={','.join(vf)} hfov={hfov:.2f} "
                  f"model={os.path.basename(a.model)}\n")
        while True:
            buf = proc.stdout.read(size)
            if len(buf) < size:
                break
            frame = np.frombuffer(buf, dtype=np.uint8).reshape(H, W, 3)
            people = backend.process(frame, n * 1000.0 / 30.0)
            t = a.start + n / 30.0 + a.shift
            if people:
                seen += 1
                joints = min(people, key=lambda p: p[0])[1]
                vals = " ".join(f"{x:.5f} {y:.5f} {z:.5f} {c:.3f}" for x, y, z, c in joints)
                out.write(f"{t:.4f} 1 {vals}\n")
            else:
                out.write(f"{t:.4f} 0\n")
            n += 1
    proc.wait()
    backend.close()
    print(f"{a.out}: {n} frames, person in {seen}", flush=True)
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
