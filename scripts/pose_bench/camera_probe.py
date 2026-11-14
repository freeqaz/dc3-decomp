#!/usr/bin/env python3
"""How a real camera delivers frames to the pose server's capture loop (PLAN L-3).

Opens the camera exactly as pose_server.py does (open_camera: 640x480, 30 fps,
CAP_PROP_BUFFERSIZE 1), then stands in for the server's per-frame work with a
busy wait of W ms and reports, per W:

  fps      frames the work loop got per second
  age      capture -> the loop has the frame (ms; device stamp, V4L2 is
           CLOCK_MONOTONIC = time.monotonic): what capture adds to latency
  read     time blocked inside read()
  dup      frames whose device stamp repeated (the same buffer handed back)

for two capture shapes:
  seq      read() then work, in one loop (pose_server.py before np-pose-perf)
  thread   a grabber thread reads continuously into a newest-wins slot and
           the work loop takes the newest frame (waits only if none is new)

  python scripts/pose_bench/camera_probe.py [--camera 0] [--secs 6]
      [--work 0,20,30,40,50] [--fourcc MJPG|YUYV] [--json out.jsonl]

The busy wait holds the GIL like Python work would not (MediaPipe releases it),
so `thread` here is a lower bound.
"""

import argparse
import json
import os
import statistics
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, os.pardir, "native", "scripts"))


def busy(ms):
    end = time.perf_counter() + ms / 1e3
    while time.perf_counter() < end:
        time.sleep(0)   # yields the GIL, like a C call that releases it


def device_ts(cap, cv2, read_time):
    try:
        d = float(cap.get(cv2.CAP_PROP_POS_MSEC)) / 1e3
    except Exception:
        return None
    return d if 0.0 <= read_time - d <= 0.5 else None


def run_seq(cap, cv2, work_ms, secs):
    rows, t_end = [], time.monotonic() + secs
    last = None
    while time.monotonic() < t_end:
        t0 = time.monotonic()
        ok, _ = cap.read()
        t1 = time.monotonic()
        if not ok:
            continue
        d = device_ts(cap, cv2, t1)
        rows.append({"t": t1, "read": (t1 - t0) * 1e3, "age": (t1 - d) * 1e3 if d else None,
                     "dup": d is not None and d == last})
        last = d
        busy(work_ms)
    return rows


def run_thread(cap, cv2, work_ms, secs):
    cond = threading.Condition()
    slot = {"seq": 0, "dev": None}
    stop = threading.Event()

    def grab():
        while not stop.is_set():
            ok, f = cap.read()
            t = time.monotonic()
            if not ok:
                continue
            d = device_ts(cap, cv2, t)
            with cond:
                slot.update(seq=slot["seq"] + 1, dev=d, t=t)
                cond.notify()

    th = threading.Thread(target=grab, daemon=True)
    th.start()
    rows, t_end, seen = [], time.monotonic() + secs, 0
    while time.monotonic() < t_end:
        t0 = time.monotonic()
        with cond:
            cond.wait_for(lambda: slot["seq"] != seen, timeout=0.5)
            if slot["seq"] == seen:
                continue
            seen, d = slot["seq"], slot["dev"]
        t1 = time.monotonic()
        rows.append({"t": t1, "read": (t1 - t0) * 1e3, "age": (t1 - d) * 1e3 if d else None,
                     "dup": False})
        busy(work_ms)
    stop.set()
    th.join(1.0)
    return rows


def summary(rows):
    if len(rows) < 3:
        return {"n": len(rows)}
    span = rows[-1]["t"] - rows[0]["t"]
    ages = [r["age"] for r in rows if r["age"] is not None]
    q = lambda xs, p: sorted(xs)[min(len(xs) - 1, int(p * len(xs)))] if xs else None
    return {"n": len(rows), "fps": round((len(rows) - 1) / span, 2),
            "age_p50": q(ages, 0.5) and round(q(ages, 0.5), 1),
            "age_p95": q(ages, 0.95) and round(q(ages, 0.95), 1),
            "read_p50": round(statistics.median(r["read"] for r in rows), 1),
            "dup": sum(r["dup"] for r in rows)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--camera", type=int, default=0)
    ap.add_argument("--secs", type=float, default=6.0)
    ap.add_argument("--work", default="0,20,30,40,50")
    ap.add_argument("--fourcc", default=None)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()
    import cv2
    import pose_server
    for shape in ("seq", "thread"):
        for w in [float(x) for x in args.work.split(",")]:
            cap, _ = pose_server.open_camera(cv2, args.camera, 640, 480)
            if args.fourcc:
                cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*args.fourcc))
            fcc = int(cap.get(cv2.CAP_PROP_FOURCC))
            fcc = "".join(chr((fcc >> 8 * i) & 0xFF) for i in range(4))
            for _ in range(10):
                cap.read()   # settle exposure / stream start
            rows = (run_seq if shape == "seq" else run_thread)(cap, cv2, w, args.secs)
            cap.release()
            s = {"shape": shape, "work_ms": w, "fourcc": fcc, **summary(rows)}
            print(json.dumps(s), flush=True)
            if args.json:
                with open(args.json, "a") as f:
                    f.write(json.dumps(s) + "\n")


if __name__ == "__main__":
    main()
