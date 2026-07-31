#!/usr/bin/env python3
"""
DC3 Native Port — Pose Estimation Server

Streams skeleton data to the C++ engine over a Unix domain socket as packed
binary frames. Backend: MediaPipe BlazePose GHUM (Apache-2.0), which emits
DC3's own 20 joints in camera-space METRES with real depth. See
pose_mediapipe.py for the remap, axis conventions and root recovery.

(An Ultralytics YOLO11n-pose backend used to exist here — COCO-17, 2D only,
constant z substituted by the C++ side. Retired: AGPL-3.0-licensed weights,
no depth, and measured WORSE detection robustness than MediaPipe on
game-representative footage. See tools/pose_corpus/bench_model_z.py and
bench_detection.py for the ground-truth evidence behind the retirement.)

Protocol v2 (little-endian):
  [uint32 packet_len]
  [uint32 magic 0x44503302] [uint32 frame_id] [uint32 num_persons] [f64 timestamp]
  [uint16 frame_w] [uint16 frame_h] [uint8 num_landmarks] [uint8 layout] [uint16 pad]
  Per person: [int32 track_id] [num_landmarks × (f32 x, f32 y, f32 z, f32 conf)]

  layout 0 = COCO-17, normalised [0,1] image coords, z unused. Not emitted by
             this server any more, but the C++ reader keeps the path for any
             external COCO-17 source.
  layout 1 = DC3-20, camera-space metres (what this server sends)

Protocol v1 (legacy, still accepted by the C++ reader; pose_server_synthetic.py
emits it) had no magic and packed [frame_id][num_persons][ts] then per person
[track_id] + 17 × (x, y, conf). The reader distinguishes them by testing the
first word against the magic — v1's first field is a small incrementing frame_id,
so there is no ambiguity.

Usage:
  python pose_server.py [--socket /tmp/dc3_pose.sock] [--camera 0]
  python pose_server.py --video clip.mp4 [--loop] [--fps 30]
"""

import argparse
import os
import socket
import struct
import sys
import time

import cv2

# Protocol v2 magic. Chosen large so it cannot collide with a v1 packet, whose
# first field is a small incrementing frame_id.
PROTOCOL_MAGIC = 0x44503302


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
    parser.add_argument("--model", default="native/models/pose_landmarker_full.task",
                        help="MediaPipe pose landmarker .task path")
    parser.add_argument("--hfov", type=float, default=None,
                        help="Camera horizontal FOV in degrees. Default 58.51 = DC3's "
                             "own Kinect FOV, recovered from the target disassembly of "
                             "NuiTransformSkeletonToDepthImage. Absolute depth scales "
                             "with this, so calibrating it to the real camera improves "
                             "absolute-position accuracy (pose SHAPE is FOV-invariant).")
    parser.add_argument("--conf", type=float, default=0.5, help="Confidence threshold")
    parser.add_argument("--max-persons", type=int, default=6, help="Max tracked persons")
    parser.add_argument("--show", action="store_true", help="Show debug visualization window")
    args = parser.parse_args()

    video_mode = args.video is not None

    # Late import so --help works without the heavy deps installed.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from pose_mediapipe import MediaPipeBackend, NUM_DC3_JOINTS

    print(f"Loading MediaPipe model: {args.model}", file=sys.stderr)
    kwargs = {"num_poses": min(args.max_persons, 2), "min_conf": args.conf}
    if args.hfov is not None:
        kwargs["hfov_deg"] = args.hfov
    mp_backend = MediaPipeBackend(args.model, **kwargs)
    num_landmarks, layout = NUM_DC3_JOINTS, 1

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
                            # Restart from the beginning. Track IDs may or may not
                            # persist across this seam — that's acceptable, we don't
                            # try to fix tracker state across a loop restart.
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

            timestamp = time.monotonic()
            persons = []
            frame_w, frame_h = (args.width, args.height)
            if frame is not None:
                frame_h, frame_w = frame.shape[:2]

                # MediaPipe VIDEO mode requires monotonically increasing
                # timestamps; derive from frame_id so a paced video run is
                # reproducible rather than wall-clock dependent.
                ts_ms = frame_id * (frame_period * 1000.0 if frame_period else 33.0)
                for tid, joints in mp_backend.process(frame, ts_ms):
                    if len(persons) >= args.max_persons:
                        break
                    persons.append((tid, joints))

            # Pack and send (protocol v2 — see module docstring)
            if client is not None:
                packet = struct.pack(
                    "<IIIdHHBBH",
                    PROTOCOL_MAGIC, frame_id, len(persons), timestamp,
                    int(frame_w), int(frame_h), num_landmarks, layout, 0,
                )
                for tid, kpts in persons:
                    packet += struct.pack("<i", tid)
                    for x, y, z, c in kpts:
                        packet += struct.pack("<ffff", x, y, z, c)

                # Prefix with packet length
                msg = struct.pack("<I", len(packet)) + packet

                try:
                    client.sendall(msg)
                except (BrokenPipeError, ConnectionResetError, BlockingIOError):
                    print("Client disconnected", file=sys.stderr)
                    client.close()
                    client = None

            if args.show and frame is not None:
                cv2.imshow("DC3 Pose Server", frame)
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
