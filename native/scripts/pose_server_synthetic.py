#!/usr/bin/env python3
"""Synthetic pose server for headless CI verification of the native live-pose path.

Speaks the same wire protocol as pose_server.py (see its docstring) but emits a
scripted person enter/leave scenario instead of webcam+YOLO output, so the
trackId->slot mapping, provider frame gating, and archive-history integrity in
GestureMgr_NativePoll can be exercised deterministically with no camera.

Scenario (loops):
  0-15s   person A (track 5) alone, moving      — fill/finalize/archive + gating
  @15s    A leaves and B (track 9) appears the  — same-frame slot handoff (the
          same frame                              ClearHistory-on-reclaim case)
  15-30s  B alone, moving
  30-45s  A returns -> two persons               — multi-person slot mapping
  45-50s  zero persons (provider dropout)        — slots untracked, no dummy pose
  then B returns and the cycle repeats from 15s semantics.

Usage:
  python3 pose_server_synthetic.py [--socket /tmp/dc3_pose.sock] [--fps 20]
Then run the game with:
  DC3_POSE=external DC3_POSE_NO_SPAWN=1 DC3_POSE_SOCKET=<socket> ...
"""

import argparse
import math
import os
import socket
import struct
import sys
import time

# COCO-17 keypoint order: nose, l/r eye, l/r ear, l/r shoulder, l/r elbow,
# l/r wrist, l/r hip, l/r knee, l/r ankle.
BASE_POSE = [
    (0.50, 0.20), (0.48, 0.18), (0.52, 0.18), (0.46, 0.19), (0.54, 0.19),
    (0.42, 0.32), (0.58, 0.32), (0.38, 0.45), (0.62, 0.45),
    (0.36, 0.58), (0.64, 0.58), (0.44, 0.60), (0.56, 0.60),
    (0.43, 0.75), (0.57, 0.75), (0.42, 0.90), (0.58, 0.90),
]


def make_person(track_id, t, x_offset):
    """One person's keypoints: base pose + sinusoidal arm/body sway."""
    sway = 0.04 * math.sin(t * 2.0 + track_id)
    arm = 0.08 * math.sin(t * 3.0 + track_id)
    kpts = []
    for j, (x, y) in enumerate(BASE_POSE):
        dx = sway + x_offset
        dy = 0.0
        if j in (7, 8, 9, 10):  # elbows + wrists swing
            dy = arm
        kpts.append((x + dx, y + dy, 0.9))
    return track_id, kpts


def persons_at(t):
    """Scripted enter/leave scenario, cycling every 50s."""
    phase = t % 50.0
    if phase < 15.0:
        return [make_person(5, t, -0.1)]
    if phase < 30.0:
        return [make_person(9, t, 0.1)]
    if phase < 45.0:
        return [make_person(9, t, 0.1), make_person(5, t, -0.2)]
    return []  # dropout: provider running, zero persons


def main():
    parser = argparse.ArgumentParser(description="DC3 synthetic pose server")
    parser.add_argument("--socket", default="/tmp/dc3_pose.sock")
    parser.add_argument("--fps", type=float, default=20.0)
    args = parser.parse_args()

    if os.path.exists(args.socket):
        os.unlink(args.socket)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(args.socket)
    server.listen(1)
    print(f"Synthetic pose server on {args.socket}, waiting for client...",
          flush=True)

    try:
        client, _ = server.accept()
        print("Client connected", flush=True)
        frame_id = 0
        start = time.monotonic()
        while True:
            t = time.monotonic() - start
            persons = persons_at(t)
            packet = struct.pack("<IId", frame_id, len(persons), time.monotonic())
            for tid, kpts in persons:
                packet += struct.pack("<i", tid)
                for x, y, c in kpts:
                    packet += struct.pack("<fff", x, y, c)
            try:
                client.sendall(struct.pack("<I", len(packet)) + packet)
            except (BrokenPipeError, ConnectionResetError):
                print("Client disconnected", file=sys.stderr)
                break
            frame_id += 1
            time.sleep(1.0 / args.fps)
    except KeyboardInterrupt:
        pass
    finally:
        server.close()
        if os.path.exists(args.socket):
            os.unlink(args.socket)


if __name__ == "__main__":
    main()
