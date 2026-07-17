#!/usr/bin/env python3
"""
DC3 Native Port — YOLO Pose Estimation Server

Runs YOLO26-pose with BOTSORT tracking on webcam input, sends skeleton data
to the C++ engine over a Unix domain socket as packed binary frames.

Protocol (little-endian):
  Header:  [uint32 frame_id] [uint32 num_persons] [float64 timestamp]
  Per person: [int32 track_id] [17 × (float32 x, float32 y, float32 conf)]

Usage:
  python pose_server.py [--socket /tmp/dc3_pose.sock] [--camera 0] [--model yolo11n-pose.pt]
  python pose_server.py --video /path/to/clip.mp4 [--loop] [--fps 30] [--socket /tmp/dc3_pose.sock] [--model yolo11n-pose.pt]
"""

import argparse
import os
import socket
import struct
import sys
import time

import cv2
import numpy as np


def main():
    parser = argparse.ArgumentParser(description="DC3 Pose Estimation Server")
    parser.add_argument("--socket", default="/tmp/dc3_pose.sock", help="Unix socket path")
    input_group = parser.add_mutually_exclusive_group()
    input_group.add_argument("--camera", type=int, default=0, help="Camera index")
    input_group.add_argument("--video", default=None, help="Path to a video file to use as input instead of a camera")
    parser.add_argument("--loop", action="store_true", help="Loop the video at EOF (video mode only)")
    parser.add_argument("--fps", type=float, default=None,
                         help="Wall-clock pacing override for video mode (default: video's own CAP_PROP_FPS, "
                              "fallback 20.0 if unreadable). Ignored in camera mode.")
    parser.add_argument("--width", type=int, default=640, help="Capture width")
    parser.add_argument("--height", type=int, default=480, help="Capture height")
    parser.add_argument("--model", default="yolo11n-pose.pt", help="YOLO pose model path")
    parser.add_argument("--conf", type=float, default=0.5, help="Confidence threshold")
    parser.add_argument("--max-persons", type=int, default=6, help="Max tracked persons")
    parser.add_argument("--show", action="store_true", help="Show debug visualization window")
    args = parser.parse_args()

    video_mode = args.video is not None

    # Late import so --help works without ultralytics installed
    from ultralytics import YOLO

    print(f"Loading model: {args.model}", file=sys.stderr)
    model = YOLO(args.model)

    frame_period = None  # video-mode wall-clock pacing interval (seconds); None = camera (paces itself)

    if video_mode:
        print(f"Opening video file {args.video}", file=sys.stderr)
        cap = cv2.VideoCapture(args.video)
        if not cap.isOpened():
            print("ERROR: Could not open video file", file=sys.stderr)
            sys.exit(1)

        video_fps = args.fps
        if video_fps is None:
            video_fps = cap.get(cv2.CAP_PROP_FPS)
            if not video_fps or video_fps <= 0:
                video_fps = 20.0
        frame_period = 1.0 / video_fps
        print(f"Video pacing at {video_fps:.3f} fps ({frame_period * 1000:.1f} ms/frame), loop={args.loop}",
              file=sys.stderr)
    else:
        print(f"Opening camera {args.camera} at {args.width}x{args.height}", file=sys.stderr)
        cap = cv2.VideoCapture(args.camera)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
        if not cap.isOpened():
            print("ERROR: Could not open camera", file=sys.stderr)
            sys.exit(1)

    # Set up Unix socket server
    sock_path = args.socket
    if os.path.exists(sock_path):
        os.unlink(sock_path)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock_path)
    server.listen(1)
    server.settimeout(0.1)  # Non-blocking accept

    print(f"Listening on {sock_path}", file=sys.stderr)

    client = None
    frame_id = 0
    next_frame_time = time.monotonic()  # video-mode pacing anchor
    eof_drain_deadline = None  # video-mode, --loop off: send ~2s of zero-person frames after EOF, then exit

    try:
        while True:
            # Accept new connections
            if client is None:
                try:
                    client, _ = server.accept()
                    client.setblocking(False)
                    print("Client connected", file=sys.stderr)
                except socket.timeout:
                    pass

            if video_mode and eof_drain_deadline is not None:
                # Past EOF, not looping: emit zero-person frames for ~2s then exit cleanly.
                frame = None
                ret = True
                if time.monotonic() >= eof_drain_deadline:
                    print("Video EOF drain complete, exiting", file=sys.stderr)
                    break
            else:
                ret, frame = cap.read()
                if not ret:
                    if video_mode:
                        if args.loop:
                            # Restart from the beginning. NOTE: BOTSORT track IDs may or may not
                            # persist across this seam — that's acceptable, we don't try to fix
                            # tracker state across a loop restart.
                            cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                            continue
                        else:
                            print("Video EOF, draining ~2s of zero-person frames", file=sys.stderr)
                            eof_drain_deadline = time.monotonic() + 2.0
                            frame = None
                            ret = True
                    else:
                        time.sleep(0.01)
                        continue

            if video_mode:
                # Wall-clock pacing: camera mode paces itself via the device, so this branch
                # only applies to file-backed video input.
                now = time.monotonic()
                sleep_for = next_frame_time - now
                if sleep_for > 0:
                    time.sleep(sleep_for)
                    next_frame_time += frame_period
                else:
                    # Fell behind (e.g. slow inference) — resync instead of busy-catching-up.
                    next_frame_time = time.monotonic() + frame_period

            if frame is None:
                # EOF-drain synthetic frame: skip inference, send an empty person list.
                results = None
            else:
                # Run tracking
                results = model.track(
                    frame,
                    persist=True,
                    tracker="botsort.yaml",
                    conf=args.conf,
                    verbose=False,
                )

            # Build binary packet
            timestamp = time.monotonic()
            persons = []

            if results and results[0].keypoints is not None:
                kpts = results[0].keypoints
                boxes = results[0].boxes

                if kpts.xy is not None and len(kpts.xy) > 0:
                    xy = kpts.xy.cpu().numpy()       # (N, 17, 2)
                    conf = kpts.conf.cpu().numpy() if kpts.conf is not None else np.ones((*xy.shape[:2], 1))  # (N, 17)

                    # Track IDs from BOTSORT
                    if boxes.id is not None:
                        track_ids = boxes.id.cpu().numpy().astype(int)
                    else:
                        track_ids = np.arange(len(xy))

                    h, w = frame.shape[:2]
                    for i in range(min(len(xy), args.max_persons)):
                        tid = int(track_ids[i]) if i < len(track_ids) else i
                        keypoints = []
                        for j in range(17):
                            # Normalize to [0, 1]
                            nx = float(xy[i, j, 0]) / w
                            ny = float(xy[i, j, 1]) / h
                            c = float(conf[i, j]) if conf.ndim == 2 else float(conf[i, j, 0])
                            keypoints.append((nx, ny, c))
                        persons.append((tid, keypoints))

            # Pack and send
            if client is not None:
                # Header: frame_id (u32), num_persons (u32), timestamp (f64)
                packet = struct.pack("<IId", frame_id, len(persons), timestamp)
                for tid, kpts in persons:
                    packet += struct.pack("<i", tid)
                    for x, y, c in kpts:
                        packet += struct.pack("<fff", x, y, c)

                # Prefix with packet length
                msg = struct.pack("<I", len(packet)) + packet

                try:
                    client.sendall(msg)
                except (BrokenPipeError, ConnectionResetError, BlockingIOError):
                    print("Client disconnected", file=sys.stderr)
                    client.close()
                    client = None

            if args.show and results:
                annotated = results[0].plot()
                cv2.imshow("DC3 Pose Server", annotated)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break

            frame_id += 1

    except KeyboardInterrupt:
        print("\nShutting down", file=sys.stderr)
    finally:
        cap.release()
        if client:
            client.close()
        server.close()
        if os.path.exists(sock_path):
            os.unlink(sock_path)
        if args.show:
            cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
