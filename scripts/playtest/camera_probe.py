#!/usr/bin/env python3
"""camera_probe.py -- open the camera (or a video), read frames, and say whether
a person is in view the way the game needs them: whole body, at a distance it
can score.  Run by scripts/playtest/preflight.py with the POSE venv's python
(it needs cv2 and mediapipe); prints one JSON object on stdout.

  camera_probe.py --camera 0 [--seconds 3] [--model M] [--no-pose]
  camera_probe.py --video clip.mp4 [--at 30]

Keys of the JSON: ok, opened, frames, width, height, fps (measured over the
probe), backend_fps (what the driver reports), error, and with pose: persons,
distance_m, head_in_view, feet_in_view, hands_up_room, advice (plain words).

Framing is read from the DC3 joints MediaPipeBackend emits (camera-space
metres), projected back with the backend's own pinhole model, so the probe
judges exactly what the game will receive.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))

HEAD, HAND_L, HAND_R, FOOT_L, FOOT_R, HIP_C = 3, 7, 11, 18, 19, 0


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--camera", type=int, default=0)
    g.add_argument("--video")
    ap.add_argument("--at", type=float, default=0.0, help="video: seconds into the file")
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--model", default=os.path.join(REPO, "native/models/pose_landmarker_full.task"))
    ap.add_argument("--no-pose", action="store_true")
    a = ap.parse_args()
    out = {"ok": False, "opened": False, "frames": 0}
    try:
        import cv2
    except ImportError as e:
        out["error"] = f"opencv (cv2) not importable: {e}"
        print(json.dumps(out))
        return 1

    if a.video:
        cap = cv2.VideoCapture(a.video)
        if cap.isOpened() and a.at > 0:
            cap.set(cv2.CAP_PROP_POS_MSEC, a.at * 1000.0)
    else:
        cap = cv2.VideoCapture(a.camera)
        if cap.isOpened():
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            cap.set(cv2.CAP_PROP_FPS, 30)
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    if not cap.isOpened():
        out["error"] = ("could not open " + (a.video if a.video else f"camera {a.camera}")
                        + " (another program may be using it)")
        print(json.dumps(out))
        return 1
    out["opened"] = True
    out["backend_fps"] = cap.get(cv2.CAP_PROP_FPS)
    frames = []
    t0 = time.monotonic()
    deadline = t0 + max(a.seconds, 0.5) + 3.0
    first = None
    while time.monotonic() < deadline:
        ok, fr = cap.read()
        if not ok or fr is None:
            if a.video:
                break
            time.sleep(0.01)
            continue
        if first is None:
            first = time.monotonic()
        frames.append(fr)
        if time.monotonic() - first >= a.seconds or (a.video and len(frames) >= 30):
            break
    cap.release()
    out["frames"] = len(frames)
    if not frames:
        out["error"] = "the camera opened but sent no picture (is it the right device?)"
        print(json.dumps(out))
        return 1
    h, w = frames[-1].shape[:2]
    out["width"], out["height"] = w, h
    if not a.video and len(frames) > 1:
        out["fps"] = round((len(frames) - 1) / max(time.monotonic() - first, 1e-3), 1)
    out["ok"] = True
    if a.no_pose:
        print(json.dumps(out))
        return 0

    try:
        from pose_mediapipe import MediaPipeBackend
    except ImportError as e:
        out["error"] = f"mediapipe not importable: {e}"
        out["ok"] = False
        print(json.dumps(out))
        return 1
    if not os.path.isfile(a.model):
        out["error"] = f"pose model missing: {a.model}"
        out["ok"] = False
        print(json.dumps(out))
        return 1
    be = MediaPipeBackend(a.model, num_poses=2)
    f = be._focal_px(w)
    best = None
    t_ms = 0
    for fr in frames[-15:]:
        people = be.process(fr, t_ms)
        t_ms += 33
        if people:
            best = people
    out["persons"] = len(best) if best else 0
    if not best:
        out["advice"] = ("Nobody was found in the picture. Stand in front of the camera, "
                         "whole body in view, about 2.5 m away, and run the check again.")
        print(json.dumps(out))
        return 0
    # the nearest-to-centre person
    def project(j):
        x, y, z, c = j
        z = max(z, 0.3)
        return (w / 2.0 - f * x / z, h / 2.0 - f * y / z, c)
    tid, joints = min(best, key=lambda p: abs(p[1][HIP_C][0]))
    head = project(joints[HEAD])
    feet = [project(joints[FOOT_L]), project(joints[FOOT_R])]
    dist = joints[HIP_C][2]
    out["distance_m"] = round(dist, 2)
    margin = 0.03 * h
    head_ok = head[2] >= 0.5 and head[1] > margin
    feet_ok = all(p[2] >= 0.5 and p[1] < h - margin for p in feet)
    # room above the head for raised hands: about a forearm + hand (0.45 m)
    room_px = head[1]
    need_px = f * 0.45 / max(dist, 0.3)
    out["head_in_view"] = bool(head_ok)
    out["feet_in_view"] = bool(feet_ok)
    out["hands_up_room"] = bool(room_px >= need_px)
    tips = []
    if not feet_ok:
        tips.append("your feet are not in the picture: step back, or tilt the camera down")
    if not head_ok:
        tips.append("your head is not in the picture: step back, or tilt the camera up")
    elif not out["hands_up_room"]:
        tips.append("there is no room above your head for raised hands: step back a little")
    if dist < 1.8:
        tips.append(f"you are close ({dist:.1f} m): about 2.5 m from the camera works best")
    elif dist > 3.8:
        tips.append(f"you are far ({dist:.1f} m): try 2-3 m")
    out["advice"] = ("Looks good: whole body in view at about %.1f m." % dist) if not tips \
        else ("Found you at about %.1f m, but " % dist) + "; ".join(tips) + "."
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
