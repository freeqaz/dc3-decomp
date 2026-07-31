#!/usr/bin/env python3
"""Detection-robustness head-to-head: MediaPipe backend vs YOLO backend.

Answers the one question the accuracy benchmarks cannot: is the AGPL YOLO
fallback ever MORE robust at *finding* people than MediaPipe? If YOLO detects
through frames MediaPipe misses, retiring the YOLO weights has a real cost;
if not, MediaPipe dominates on both accuracy and robustness.

Both sides run exactly as the production server runs them
(native/scripts/pose_server.py):
  - MediaPipe: MediaPipeBackend(model, num_poses=2, min_conf=0.5), sequential
    frames, detect_for_video timestamps = frame_idx * frame period -- the
    exact --video-mode feeding. "Detected" = process() returned >= 1 person
    (which also requires root recovery to succeed, i.e. a usable skeleton).
  - YOLO: model.track(frame, persist=True, tracker='botsort.yaml', conf=0.5)
    (pose_server.py:214-220). "Detected" = >= 1 keypoint set returned.

Metrics per clip and per backend:
  - detection rate: fraction of frames with >= 1 usable person
  - persons/frame histogram (multi-person clip)
  - dropout runs: lengths of maximal runs of frames with 0 persons
  - track-ID churn: unique IDs over the clip, and ID-switch events (frames
    where a currently-visible person count is stable but the ID set changed --
    a churn proxy; also plain "new ID appeared" count)

Run:
  .venv/bin/python tools/pose_corpus/bench_detection.py \
      [--clips /home/free/tmp/dc3-pose-footage/solo_dancer_sattriya.mp4 ...]
"""

import argparse
import os
import sys

import cv2
import numpy as np

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO, "native", "scripts"))

MP_MODEL = os.path.join(_REPO, "native", "models", "pose_landmarker_full.task")
YOLO_MODEL = os.path.join(_REPO, "native", "models", "yolo11n-pose.pt")


def run_mediapipe(video_path):
    from pose_mediapipe import MediaPipeBackend

    be = MediaPipeBackend(MP_MODEL, num_poses=2, min_conf=0.5)  # production cfg
    cap = cv2.VideoCapture(video_path)
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    period_ms = 1000.0 / fps
    ids_per_frame = []
    i = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        people = be.process(frame, i * period_ms)
        ids_per_frame.append([tid for tid, _ in people])
        i += 1
    cap.release(); be.close()
    return ids_per_frame


def run_yolo(video_path):
    from ultralytics import YOLO

    model = YOLO(YOLO_MODEL)
    cap = cv2.VideoCapture(video_path)
    ids_per_frame = []
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        # Exactly pose_server.py:214-220.
        results = model.track(frame, persist=True, tracker="botsort.yaml",
                              conf=0.5, verbose=False)
        ids = []
        if results and results[0].keypoints is not None:
            kpts = results[0].keypoints
            boxes = results[0].boxes
            if kpts.xy is not None and len(kpts.xy) > 0:
                if boxes.id is not None:
                    ids = [int(x) for x in boxes.id.cpu().numpy()]
                else:
                    # pose_server falls back to enumeration when BOTSORT has
                    # not confirmed tracks yet; count persons, mark id None.
                    ids = [None] * len(kpts.xy)
        ids_per_frame.append(ids)
    cap.release()
    return ids_per_frame


def dropout_runs(counts):
    runs, cur = [], 0
    for c in counts:
        if c == 0:
            cur += 1
        elif cur:
            runs.append(cur); cur = 0
    if cur:
        runs.append(cur)
    return runs


def churn(ids_per_frame):
    seen = set()
    new_id_events = 0     # a previously-unseen ID appears (excluding frame 0)
    switch_events = 0     # person count stable vs prev frame but ID set changed
    prev = None
    for k, ids in enumerate(ids_per_frame):
        real = [i for i in ids if i is not None]
        for i in real:
            if i not in seen:
                seen.add(i)
                if k > 0:
                    new_id_events += 1
        if prev is not None and len(real) == len(prev) and len(real) > 0 \
                and set(real) != set(prev):
            switch_events += 1
        prev = real
    return len(seen), new_id_events, switch_events


def report(name, ids_per_frame):
    counts = [len(x) for x in ids_per_frame]
    n = len(counts)
    det = sum(1 for c in counts if c > 0)
    runs = dropout_runs(counts)
    uniq, new_ids, switches = churn(ids_per_frame)
    print(f"  {name}")
    print(f"    frames {n}, detection rate {det/n*100:.2f}% "
          f"({n-det} frames with 0 persons)")
    hist = {}
    for c in counts:
        hist[c] = hist.get(c, 0) + 1
    print(f"    persons/frame: " + ", ".join(f"{k}: {v} ({v/n*100:.1f}%)"
                                             for k, v in sorted(hist.items())))
    if runs:
        print(f"    dropout runs: {len(runs)} total, lengths "
              f"mean {np.mean(runs):.1f} p90 {np.percentile(runs,90):.0f} "
              f"max {max(runs)}")
    else:
        print("    dropout runs: none")
    print(f"    track IDs: {uniq} unique, {new_ids} late new-ID events, "
          f"{switches} same-count ID-switch frames")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clips", nargs="*", default=[
        "/home/free/tmp/dc3-pose-footage/solo_dancer_sattriya.mp4",
        "/home/free/tmp/dc3-pose-footage/multi_dancer_schottische.mp4",
    ])
    args = ap.parse_args()

    for clip in args.clips:
        print(f"\n=== {os.path.basename(clip)} ===")
        print("  running MediaPipe (production config)...", flush=True)
        mp_ids = run_mediapipe(clip)
        print("  running YOLO11n-pose + BOTSORT (production config)...", flush=True)
        yolo_ids = run_yolo(clip)
        report("MediaPipe BlazePose GHUM full (num_poses=2)", mp_ids)
        report("YOLO11n-pose + BOTSORT (conf=0.5)", yolo_ids)

        # Frame-level complementarity: does either detect where the other missed?
        n = min(len(mp_ids), len(yolo_ids))
        mp_only = sum(1 for i in range(n) if mp_ids[i] and not yolo_ids[i])
        yolo_only = sum(1 for i in range(n) if yolo_ids[i] and not mp_ids[i])
        neither = sum(1 for i in range(n) if not yolo_ids[i] and not mp_ids[i])
        print(f"  complementarity: MP-only {mp_only}, YOLO-only {yolo_only}, "
              f"neither {neither} of {n} frames")


if __name__ == "__main__":
    main()
