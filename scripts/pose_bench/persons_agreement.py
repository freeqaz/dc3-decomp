#!/usr/bin/env python3
"""Does num_poses 1 track one dancer the same as num_poses 2? (PLAN L-3)

Runs pose_mediapipe.MediaPipeBackend twice over the same frames of a clip --
num_poses A and num_poses B, same model, same timestamps -- and compares
what the game would receive: persons per frame, and for frames where both
see exactly one person, that person's 20 DC3 joints (camera-space metres,
after root recovery and filtering).

  .venv/bin/python scripts/pose_bench/persons_agreement.py --video clip.mp4 \\
      [--model full] [--a 1] [--b 2] [--n 600] [--json out.jsonl]
      [--blank left|right --join S]

--a/--b: a num_poses, or probe:S for pose_server --probe-second-person S.
--blank SIDE --join S: black out that half of every frame before S seconds,
so a second dancer "walks in" at S; prints how many frames after S each side
first reports two people (the probe's cost in join latency).

Prints: frames where the person counts differ, and over the one-person frames
the joint distance |A - B| (mm): median, p95, max, and the same for the
depth-free image-plane part (x, y only), which is what most of the scoring
sees. A control pair (--a 2 --b 2) shows the run-to-run floor.
"""

import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, os.pardir, "native", "scripts"))
sys.path.insert(0, HERE)
from stage_bench import model_path  # noqa: E402


def pct(xs, p):
    s = sorted(xs)
    return s[min(len(s) - 1, int(p * len(s)))] if s else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video", required=True)
    ap.add_argument("--model", default="full")
    ap.add_argument("--a", default="1")
    ap.add_argument("--b", default="2")
    ap.add_argument("--blank", choices=("left", "right"), default=None)
    ap.add_argument("--join", type=float, default=0.0)
    ap.add_argument("--n", type=int, default=600)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    import cv2
    from pose_mediapipe import MediaPipeBackend

    def make(spec):
        if spec.startswith("probe:"):
            return MediaPipeBackend(model_path(args.model), num_poses=2,
                                    probe_s=float(spec.split(":")[1]))
        return MediaPipeBackend(model_path(args.model), num_poses=int(spec))
    mp_a, mp_b = make(args.a), make(args.b)
    cap = cv2.VideoCapture(args.video)
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    join_frame = int(round(args.join * fps)) if args.blank else None
    first_two = {"a": None, "b": None}
    count_diff = 0
    one = 0
    d3, d2 = [], []
    for i in range(args.n):
        ok, frame = cap.read()
        if not ok:
            break
        if join_frame is not None and i < join_frame:
            w = frame.shape[1]
            if args.blank == "left":
                frame[:, : w // 2] = 0
            else:
                frame[:, w // 2:] = 0
        ts = i * 1000.0 / fps
        pa = mp_a.process(frame, ts)
        pb = mp_b.process(frame, ts)
        if join_frame is not None and i >= join_frame:
            for k, p in (("a", pa), ("b", pb)):
                if first_two[k] is None and len(p) >= 2:
                    first_two[k] = i - join_frame
        if len(pa) != len(pb):
            count_diff += 1
            continue
        if len(pa) != 1:
            continue
        one += 1
        for (xa, ya, za, _), (xb, yb, zb, _) in zip(pa[0][1], pb[0][1]):
            d3.append(1e3 * math.sqrt((xa - xb) ** 2 + (ya - yb) ** 2 + (za - zb) ** 2))
            d2.append(1e3 * math.sqrt((xa - xb) ** 2 + (ya - yb) ** 2))
    mp_a.close()
    mp_b.close()
    res = {"video": os.path.basename(args.video), "model": args.model, "a": args.a, "b": args.b,
           "frames": i + 1, "count_differs": count_diff, "one_person_frames": one,
           "mm3d_p50": pct(d3, 0.5), "mm3d_p95": pct(d3, 0.95), "mm3d_max": max(d3) if d3 else None,
           "mm_xy_p50": pct(d2, 0.5), "mm_xy_p95": pct(d2, 0.95),
           "join_frames_a": first_two["a"], "join_frames_b": first_two["b"],
           "probes_a": mp_a.probe.probes if mp_a.probe else None,
           "probes_b": mp_b.probe.probes if mp_b.probe else None}
    print(json.dumps({k: (round(v, 2) if isinstance(v, float) else v) for k, v in res.items()}))
    if args.json:
        with open(args.json, "a") as f:
            f.write(json.dumps(res) + "\n")


if __name__ == "__main__":
    main()
