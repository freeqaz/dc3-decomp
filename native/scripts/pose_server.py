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

Startup order and exit codes. The game (NativeSkeletonProvider::Start in
native/src/platform/Skeleton_Native.cpp) spawns this script and waits for the
socket; it also watches for this process to exit, and turns the exit code into
an on-screen message. So the cheap checks run first and each failure has its
own code:

  1. model file exists                       else exit 5 (EXIT_NO_MODEL)
  2. cv2 importable                          else exit 6 (EXIT_NO_DEPS)
  3. camera opens AND delivers a frame       else exit 3 (EXIT_NO_CAMERA)
     (or: the --video file opens)            else exit 4 (EXIT_NO_VIDEO)
  4. bind the socket (the game connects now)
  5. import mediapipe, load the model        else exit 6 / 5
  6. stream; a camera that stops delivering frames for CAMERA_LOST_AFTER_S
     exits 7 (EXIT_CAMERA_LOST), so the game notices instead of waiting on a
     silent socket.

Camera mode asks for 640x480 at 30 fps with a one-frame driver buffer (a
deeper buffer hands MediaPipe stale frames whenever inference is slower than
the camera). The packet timestamp is the frame's capture time: the device's
own buffer timestamp when the backend reports one on this process's monotonic
clock (V4L2 does), else the moment read() returned. MediaPipe's VIDEO-mode
timestamp is derived from the same reading and forced strictly increasing (it
raises "Input timestamp must be monotonically increasing" on a repeat).

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

# Protocol v2 magic. Chosen large so it cannot collide with a v1 packet, whose
# first field is a small incrementing frame_id.
PROTOCOL_MAGIC = 0x44503302

# Exit codes the game turns into a message (NativeSkeletonProvider::ServerExit
# in native/src/platform/Skeleton_Native.h -- keep in sync).
EXIT_NO_CAMERA = 3
EXIT_NO_VIDEO = 4
EXIT_NO_MODEL = 5
EXIT_NO_DEPS = 6
EXIT_CAMERA_LOST = 7

CAMERA_FPS = 30.0
FIRST_FRAME_TIMEOUT_S = 3.0   # an opened device that never delivers is "no camera"
CAMERA_LOST_AFTER_S = 2.0     # consecutive failed reads mid-run
DEVICE_TS_MAX_AGE_S = 0.5     # a device timestamp older than this is not on our clock


class ServerExit(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def open_camera(cv2, index, width, height, fps=CAMERA_FPS,
                first_frame_timeout=FIRST_FRAME_TIMEOUT_S,
                clock=time.monotonic, sleep=time.sleep):
    """Open camera `index`, request width x height @ fps with a 1-frame buffer,
    and wait for a first frame. Returns (cap, frame). Raises ServerExit
    (EXIT_NO_CAMERA) if it will not open or opens but sends nothing -- a
    /dev/video node that is a metadata or output device opens fine and never
    delivers a picture."""
    cap = cv2.VideoCapture(index)
    if not cap.isOpened():
        raise ServerExit(EXIT_NO_CAMERA, f"could not open camera {index}")
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    deadline = clock() + first_frame_timeout
    while True:
        ok, frame = cap.read()
        if ok and frame is not None:
            return cap, frame
        if clock() >= deadline:
            cap.release()
            raise ServerExit(EXIT_NO_CAMERA,
                             f"camera {index} opened but sent no frame in "
                             f"{first_frame_timeout:.1f} s")
        sleep(0.01)


class CaptureClock:
    """Camera-mode timestamps.

    stamp(cap, read_time) -> (packet_ts_s, mediapipe_ts_ms, source).
    packet_ts_s is the capture time on time.monotonic()'s clock: the device's
    buffer timestamp (cv2.CAP_PROP_POS_MSEC) when it is plausibly on that clock
    -- V4L2 stamps buffers with CLOCK_MONOTONIC, which is what Python's
    monotonic() reads on Linux -- else the time read() returned. MediaPipe's
    timestamp is the same instant in integer ms since the first frame, forced
    strictly increasing."""

    def __init__(self, cv2):
        self._cv2 = cv2
        self._start = None
        self._last_ms = -1
        self.source = None   # "device" / "read" of the most recent frame

    def stamp(self, cap, read_time):
        t = read_time
        self.source = "read"
        try:
            dev_ms = float(cap.get(self._cv2.CAP_PROP_POS_MSEC))
        except Exception:
            dev_ms = 0.0
        if dev_ms > 0.0:
            dev = dev_ms / 1000.0
            if 0.0 <= read_time - dev <= DEVICE_TS_MAX_AGE_S:
                t = dev
                self.source = "device"
        if self._start is None:
            self._start = t
        ms = int(round((t - self._start) * 1000.0))
        if ms <= self._last_ms:
            ms = self._last_ms + 1
        self._last_ms = ms
        return t, ms, self.source


def backend_kwargs(args):
    """MediaPipeBackend options from the command line."""
    kwargs = {"num_poses": min(args.max_persons, 2), "min_conf": args.conf}
    if args.hfov is not None:
        kwargs["hfov_deg"] = args.hfov
    return kwargs


def pack_frame(frame_id, timestamp, frame_w, frame_h, num_landmarks, layout, persons):
    """One length-prefixed protocol-v2 packet (see module docstring)."""
    packet = struct.pack(
        "<IIIdHHBBH",
        PROTOCOL_MAGIC, frame_id, len(persons), timestamp,
        int(frame_w), int(frame_h), num_landmarks, layout, 0,
    )
    for tid, kpts in persons:
        packet += struct.pack("<i", tid)
        for x, y, z, c in kpts:
            packet += struct.pack("<ffff", x, y, z, c)
    return struct.pack("<I", len(packet)) + packet


def parse_args(argv=None):
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
    return parser.parse_args(argv)


def _default_backend(args):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from pose_mediapipe import MediaPipeBackend, NUM_DC3_JOINTS
        print(f"Loading MediaPipe model: {args.model}", file=sys.stderr)
        backend = MediaPipeBackend(args.model, **backend_kwargs(args))
    except ImportError as e:
        raise ServerExit(EXIT_NO_DEPS, f"mediapipe not importable: {e}")
    return backend, NUM_DC3_JOINTS, 1


def run(args, cv2=None, make_backend=_default_backend, clock=time.monotonic,
        sleep=time.sleep):
    """The server. Returns normally on a clean shutdown; raises ServerExit for
    a failure the game should report."""
    video_mode = args.video is not None

    if not os.path.isfile(args.model):
        raise ServerExit(EXIT_NO_MODEL, f"model file not found: {args.model}")
    if cv2 is None:
        try:
            import cv2  # noqa: F811 -- late import so --help works without it
        except ImportError as e:
            raise ServerExit(EXIT_NO_DEPS, f"opencv (cv2) not importable: {e}")

    frame_period = None  # video-mode wall-clock pacing interval (seconds); None = camera (paces itself)
    first_frame = None
    stamps = None

    if video_mode:
        print(f"Opening video file {args.video}", file=sys.stderr)
        cap = cv2.VideoCapture(args.video)
        if not cap.isOpened():
            raise ServerExit(EXIT_NO_VIDEO, f"could not open video file {args.video}")

        video_fps = args.fps
        if video_fps is None:
            video_fps = cap.get(cv2.CAP_PROP_FPS)
            if not video_fps or video_fps <= 0:
                video_fps = 20.0
        frame_period = 1.0 / video_fps
        print(f"Video pacing at {video_fps:.3f} fps ({frame_period * 1000:.1f} ms/frame), loop={args.loop}",
              file=sys.stderr)
    else:
        print(f"Opening camera {args.camera}, requesting {args.width}x{args.height} "
              f"@ {CAMERA_FPS:g} fps", file=sys.stderr)
        cap, first_frame = open_camera(cv2, args.camera, args.width, args.height,
                                       clock=clock, sleep=sleep)
        got_h, got_w = first_frame.shape[:2]
        print(f"Camera {args.camera} delivering {got_w}x{got_h} "
              f"(driver reports {cap.get(cv2.CAP_PROP_FPS):g} fps)", file=sys.stderr)
        stamps = CaptureClock(cv2)

    # Set up Unix socket server. Bound BEFORE the model loads: the game is
    # blocked in Start() until it can connect, and loading takes ~1 s.
    sock_path = args.socket
    if os.path.exists(sock_path):
        os.unlink(sock_path)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock_path)
    server.listen(1)
    server.settimeout(0.1)  # Non-blocking accept

    print(f"Listening on {sock_path}", file=sys.stderr)

    client = None
    try:
        backend, num_landmarks, layout = make_backend(args)

        frame_id = 0
        next_frame_time = clock()  # video-mode pacing anchor
        eof_drain_deadline = None  # video-mode, --loop off: send ~2s of zero-person frames after EOF, then exit
        failing_since = None       # camera mode: first of a run of failed reads
        logged_ts_source = None

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
                if clock() >= eof_drain_deadline:
                    print("Video EOF drain complete, exiting", file=sys.stderr)
                    break
            elif first_frame is not None:
                frame, first_frame = first_frame, None
                read_time = clock()
            else:
                ret, frame = cap.read()
                read_time = clock()
                if not ret or frame is None:
                    if video_mode:
                        if args.loop:
                            # Restart from the beginning. Track IDs may or may not
                            # persist across this seam — that's acceptable, we don't
                            # try to fix tracker state across a loop restart.
                            cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                            continue
                        print("Video EOF, draining ~2s of zero-person frames", file=sys.stderr)
                        eof_drain_deadline = clock() + 2.0
                        frame = None
                    else:
                        if failing_since is None:
                            failing_since = read_time
                        elif read_time - failing_since >= CAMERA_LOST_AFTER_S:
                            raise ServerExit(EXIT_CAMERA_LOST,
                                             f"camera {args.camera} delivered no frame for "
                                             f"{CAMERA_LOST_AFTER_S:g} s")
                        sleep(0.01)
                        continue
                failing_since = None

            if video_mode:
                # Wall-clock pacing: camera mode paces itself via the device, so this branch
                # only applies to file-backed video input.
                now = clock()
                sleep_for = next_frame_time - now
                if sleep_for > 0:
                    sleep(sleep_for)
                    next_frame_time += frame_period
                else:
                    # Fell behind (e.g. slow inference) — resync instead of busy-catching-up.
                    next_frame_time = clock() + frame_period
                timestamp = clock()
                # Video mode derives MediaPipe's timestamp from frame_id so a
                # paced run is reproducible.
                ts_ms = frame_id * frame_period * 1000.0
            else:
                timestamp, ts_ms, source = stamps.stamp(cap, read_time)
                if source != logged_ts_source:
                    logged_ts_source = source
                    print(f"Camera timestamps: {source} clock", file=sys.stderr)

            persons = []
            frame_w, frame_h = (args.width, args.height)
            if frame is not None:
                frame_h, frame_w = frame.shape[:2]
                for tid, joints in backend.process(frame, ts_ms):
                    if len(persons) >= args.max_persons:
                        break
                    persons.append((tid, joints))

            # Pack and send (protocol v2 — see module docstring)
            if client is not None:
                msg = pack_frame(frame_id, timestamp, frame_w, frame_h,
                                 num_landmarks, layout, persons)
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


def main(argv=None):
    args = parse_args(argv)
    try:
        run(args)
    except ServerExit as e:
        # One machine-greppable line; the game shows its own message for the code.
        print(f"POSE_SERVER_ERROR exit={e.code}: {e}", file=sys.stderr)
        sys.exit(e.code)


if __name__ == "__main__":
    main()
