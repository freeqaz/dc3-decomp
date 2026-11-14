#!/usr/bin/env python3
"""Per-stage cost of the desktop pose server's work, unpaced (PLAN L-3).

Runs the real code -- pose_mediapipe.MediaPipeBackend.process (its own
last_timing), pose_server.camera_block and pose_server.pack_frame -- over
the frames of a clip as fast as it can, and prints where the time goes:

  decode   cv2.VideoCapture.read (a camera's MJPG/YUYV decode is here too)
  cvt      BGR -> RGB + mp.Image
  infer    PoseLandmarker.detect_for_video (detector + landmarks + masks)
  infer_cpu  process CPU time during infer: steadier than wall time on a
           loaded box, and > infer when MediaPipe runs several threads
  geom     root recovery, 33 -> 20 remap, mask copy
  track    CentroidTracker + root-depth One Euro filter
  camera   the v3 camera block (--frames): scale, BGR->RGB, ordinal mask
  pack     protocol v2/v3 packet

and the process CPU time per frame (cpu_ms): cpu_ms / wall_ms is how many
cores the work kept busy (XNNPACK threads show up here).

  .venv/bin/python scripts/pose_bench/stage_bench.py --video clip.mp4 \\
      [--model full|lite|heavy|PATH] [--num-poses 2] [--frames 320x240|off] \\
      [--delegate cpu|gpu] [--n 300] [--json out.json]
  --slow STAGE=MS   add MS of sleep to one stage (infer, cvt, camera): the
                    instrument check -- that stage, and only it, must grow.

Footage: any clip; the numbers in docs/native/POSE_INPUT.md#performance come
from 60 s excerpts of the np-video-calib stream (one dancer) and the
playtest kit's macarena_duo (two), both 640x480 30 fps.
"""

import argparse
import json
import os
import resource
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))

STAGES = ("decode", "cvt", "infer", "infer_cpu", "geom", "track", "camera", "pack")


def model_path(name):
    if os.path.isfile(name):
        return name
    p = os.path.join(REPO, "native", "models", f"pose_landmarker_{name}.task")
    if not os.path.isfile(p):
        # A worktree has no gitignored models: use the main checkout's.
        common = os.popen(f"git -C {REPO} rev-parse --git-common-dir").read().strip()
        p = os.path.join(os.path.dirname(os.path.abspath(os.path.join(REPO, common))),
                         "native", "models", f"pose_landmarker_{name}.task")
    return p


def cpu_s():
    r = resource.getrusage(resource.RUSAGE_SELF)
    return r.ru_utime + r.ru_stime


def pct(xs, p):
    if not xs:
        return None
    s = sorted(xs)
    return s[min(len(s) - 1, int(p * len(s)))]


def summarize(rows, wall_s, cpu_total_s):
    out = {"frames": len(rows), "wall_s": round(wall_s, 3),
           "fps_unpaced": round(len(rows) / wall_s, 2) if wall_s else None,
           "cpu_ms_per_frame": round(cpu_total_s * 1e3 / max(1, len(rows)), 2),
           "cores_busy": round(cpu_total_s / wall_s, 2) if wall_s else None,
           "persons_mean": round(statistics.fmean(r["persons"] for r in rows), 3) if rows else 0}
    for st in STAGES + ("total",):
        xs = [r[st] for r in rows if st in r]
        if xs:
            out[st] = {"p50": round(pct(xs, 0.5), 2), "p95": round(pct(xs, 0.95), 2),
                       "mean": round(statistics.fmean(xs), 2)}
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video", required=True)
    ap.add_argument("--model", default="full")
    ap.add_argument("--num-poses", type=int, default=2)
    ap.add_argument("--frames", default="320x240", help="v3 camera block WxH, or off")
    ap.add_argument("--delegate", default="cpu", choices=("cpu", "gpu"))
    ap.add_argument("--probe", type=float, default=0.0,
                    help="pose_server --probe-second-person S (num_poses 1 + a probe)")
    ap.add_argument("--n", type=int, default=300, help="frames measured (after --warmup)")
    ap.add_argument("--warmup", type=int, default=30)
    ap.add_argument("--start", type=float, default=0.0, help="seconds into the clip")
    ap.add_argument("--slow", action="append", default=[], metavar="STAGE=MS")
    ap.add_argument("--json", default=None)
    ap.add_argument("--label", default=None)
    args = ap.parse_args(argv)

    import cv2
    import numpy as np
    import pose_server
    from pose_mediapipe import MediaPipeBackend

    slow = {}
    for s in args.slow:
        k, v = s.split("=")
        slow[k] = float(v) / 1e3

    frames_size = None if args.frames in ("off", "0", "") else pose_server.parse_frames(args.frames)
    backend = MediaPipeBackend(model_path(args.model), num_poses=args.num_poses,
                               delegate=args.delegate, segmentation=bool(frames_size),
                               probe_s=args.probe)
    if "infer" in slow:
        real = backend._landmarker.detect_for_video

        def slowed(img, ts, *a, _real=real, **k):
            time.sleep(slow["infer"])
            return _real(img, ts, *a, **k)
        backend._landmarker.detect_for_video = slowed
    if "cvt" in slow:
        real_img = backend._mp.Image

        class SlowImage:
            def __new__(cls, *a, **k):
                time.sleep(slow["cvt"])
                return real_img(*a, **k)
        backend._mp = type("mp", (), {"Image": SlowImage, "ImageFormat": backend._mp.ImageFormat})

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    if args.start:
        cap.set(cv2.CAP_PROP_POS_MSEC, args.start * 1e3)

    rows = []
    clock = time.perf_counter
    i = 0
    t_wall0 = cpu0 = None
    while len(rows) < args.n:
        if i == args.warmup:
            t_wall0, cpu0 = clock(), cpu_s()
        t0 = clock()
        ok, frame = cap.read()
        if not ok:
            cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
            ok, frame = cap.read()
            if not ok:
                sys.exit("clip has no frames")
        t1 = clock()
        persons = backend.process(frame, i * 1000.0 / fps)
        t2 = clock()
        image = None
        if frames_size:
            masks = list(backend.last_masks or [])[:len(persons)]
            if "camera" in slow:
                time.sleep(slow["camera"])
            image = pose_server.camera_block(frame, masks, frames_size, cv2, np)
        t3 = clock()
        h, w = frame.shape[:2]
        pose_server.pack_frame(i, 0.0, w, h, 20, 1, persons, image)
        t4 = clock()
        if i >= args.warmup:
            row = {"decode": (t1 - t0) * 1e3, "camera": (t3 - t2) * 1e3, "pack": (t4 - t3) * 1e3,
                   "total": (t4 - t0) * 1e3, "persons": len(persons)}
            row.update(backend.last_timing)
            if not frames_size:
                row.pop("camera")
            rows.append(row)
        i += 1
    wall = clock() - t_wall0
    cpu = cpu_s() - cpu0
    backend.close()

    res = {"label": args.label, "video": os.path.basename(args.video), "model": args.model,
           "num_poses": args.num_poses, "probe": args.probe,
           "probes": backend.probe.probes if backend.probe else None, "frames": args.frames, "delegate": args.delegate,
           "slow": args.slow, "loadavg": os.getloadavg()[0], **summarize(rows, wall, cpu)}
    line = (f"{args.label or ''} model={args.model} poses={args.num_poses}"
            f"{f' probe={args.probe}' if args.probe else ''} frames={args.frames} "
            f"delegate={args.delegate}: {res['fps_unpaced']} fps unpaced, "
            f"cores {res['cores_busy']}, persons {res['persons_mean']}, load {res['loadavg']:.0f}\n  "
            + "  ".join(f"{st} {res[st]['p50']}/{res[st]['p95']}" for st in STAGES + ("total",) if st in res)
            + "   (ms p50/p95)")
    print(line)
    if args.json:
        with open(args.json, "a") as f:
            f.write(json.dumps(res) + "\n")
    return res


if __name__ == "__main__":
    main()
