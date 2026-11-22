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
  [uint16 frame_w] [uint16 frame_h] [uint8 num_landmarks] [uint8 layout] [uint16 flags]
  Per person: [int32 track_id] [num_landmarks × (f32 x, f32 y, f32 z, f32 conf)]

  flags bit 0 (FLAG_UNLEVELLED) = this camera does not know its gravity: a
             webcam has no accelerometer, so the game estimates the camera's
             tilt from the player's posture and levels the skeleton itself,
             as SkeletonFrame::Create does with a Kinect's vNormalToGravity
             (native/src/platform/PoseLevel_Native.h, DC3_POSE_LEVEL). This
             server always sets it. Older servers sent 0 (pad), which reads
             as a level sensor, as synthetic_kinect.py's Kinect is.
  flags bit 1 (FLAG_NO_CAMERA) = the camera is gone and this server is looking
             for it again (see "Camera selection and loss" below): the packet
             carries no persons and no picture. The game shows a notice while
             it is set and another when it clears.
  flags bit 2 (FLAG_LATENCY_DECLARED) = this source declares the scoring
             latency the game should use, in bits 8..15 as a count of 5 ms.
             --video declares 0 (RECORDED_SOURCE_LATENCY_MS); a camera
             declares nothing, so the game keeps scoring.dta's 100 ms;
             --source-latency-ms N declares N for either (a loopback camera
             fed from a file is a recorded source). The game's
             DC3_POSE_LATENCY_MS always wins over a declaration
             (native/src/platform/Skeleton_Native.cpp, "Sensor latency").

  layout 0 = COCO-17, normalised [0,1] image coords, z unused. Not emitted by
             this server any more, but the C++ reader keeps the path for any
             external COCO-17 source.
  layout 1 = DC3-20, camera-space metres (what this server sends)

Protocol v3 (--frames WxH; PLAN P-4, the player view): v2 with magic
0x44503303 and, after the persons, a camera block:
  [uint16 w] [uint16 h] [uint8 rgb_format = 1] [uint8 mask_format = 1] [uint16 pad]
  [w*h*3 bytes RGB888, top row first] [w*h bytes person mask]
  The mask byte is 0 for nobody, k for the k-th person of THIS packet (MediaPipe's
  per-pose segmentation, the most confident person > 0.5 wins; since PLAN P-20
  steadied, filled and tidied first -- MaskRefiner below; --mask raw is the
  mask as it was. Same bytes on the wire either way). w = h = 0 means
  no picture this frame. The game asks for v3 by passing --frames; without it
  this server sends v2 exactly as before, so an older game is unaffected, and
  the C++ reader still accepts v1/v2 from older servers. Reader:
  native/src/platform/CameraStream_Native.cpp (ParseWireBlock).

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
     is LOST: the server keeps the socket, sends FLAG_NO_CAMERA packets and
     reopens the camera (re-resolving --camera) with backoff; only if it is
     still gone after CAMERA_REOPEN_BUDGET_S does it exit 7
     (EXIT_CAMERA_LOST).

Camera selection and loss. --camera takes `auto` (the default), an index, a
device path, or a /dev/v4l/by-id name. A USB camera can re-enumerate on its
own -- the GPD Pocket's integrated camera did 8 times in one afternoon, and
each time its node numbers moved (video0 -> video1 + video2, one of which is a
metadata-only node) -- so a fixed index is not a camera:
  auto      the first /dev/videoN (by N) whose V4L2 device caps include video
            capture; metadata/output nodes are skipped. Logged.
  N         /dev/videoN if it is a capture device, else as auto (logged).
  path      that node (symlinks such as /dev/v4l/by-id/... are followed on
            every open, so a by-id name survives re-enumeration).
Every open -- the first, and each reopen after a loss -- resolves again.
Without V4L2 (macOS), auto is index 0 and an index is passed to OpenCV as is.

Camera mode asks for 640x480 at 30 fps with CAMERA_BUFFERS = 2 driver buffers
and reads the camera on its own thread (NewestFrameCapture); the loop below
takes the newest frame and never an older one. One buffer is not "the newest
frame": uvcvideo fills a buffer only if it was queued when that frame began,
and OpenCV queues its buffer back at the next read(), so with one buffer every
other frame has nowhere to go. Measured on the GPD's camera (np-gpd-camfps,
docs/native/POSE_INPUT.md#capture-buffers-np-gpd-camfps-2026-11-14): 15 fps
from a sensor running at 30, 7.7 from 15. The thread is what keeps the second
buffer from handing MediaPipe a stale frame when inference is slower than the
camera. --no-capture-thread reads in the loop as before (still two buffers).
The packet timestamp is the frame's capture time: the device's
own buffer timestamp when the backend reports one on this process's monotonic
clock (V4L2 does), else the moment read() returned. MediaPipe's VIDEO-mode
timestamp is derived from the same reading and forced strictly increasing (it
raises "Input timestamp must be monotonically increasing" on a repeat).

Usage:
  python pose_server.py [--socket /tmp/dc3_pose.sock] [--camera auto|N|/dev/videoN|by-id name]
  python pose_server.py --video clip.mp4 [--loop] [--fps 30]
"""

import argparse
import os
import socket
import struct
import sys
import threading
import time

# Protocol v2 magic. Chosen large so it cannot collide with a v1 packet, whose
# first field is a small incrementing frame_id.
PROTOCOL_MAGIC = 0x44503302
PROTOCOL_MAGIC_V3 = 0x44503303   # v2 + camera block (--frames)
FLAG_UNLEVELLED = 1 << 0         # header flags: camera gravity unknown (see above)
FLAG_NO_CAMERA = 1 << 1          # header flags: camera lost, reopening (see above)
FLAG_LATENCY_DECLARED = 1 << 2   # header flags: bits 8..15 = scoring latency / 5 ms
LATENCY_UNIT_MS = 5
# A recorded file (--video) has no camera delay: frames are decoded, not
# exposed, and the dancer on it is not reacting to the game's audio. Measured
# on six songs through this server (docs/native/POSE_INPUT.md, PLAN L-4): 0
# scored 0.70 of the original's card, the Kinect's 100 ms 0.49-0.52.
RECORDED_SOURCE_LATENCY_MS = 0
WIRE_RGB888 = 1
WIRE_MASK_ORDINAL = 1
MASK_THRESHOLD = 0.5

# native/models/ next to this script's own native/scripts/, not the cwd: the
# engine runs from native/build and always passes --model, but a standalone run
# from anywhere else used to look for native/models under the cwd (PLAN P-10).
DEFAULT_MODEL = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                             "models", "pose_landmarker_full.task")

# Exit codes the game turns into a message (NativeSkeletonProvider::ServerExit
# in native/src/platform/Skeleton_Native.h -- keep in sync).
EXIT_NO_CAMERA = 3
EXIT_NO_VIDEO = 4
EXIT_NO_MODEL = 5
EXIT_NO_DEPS = 6
EXIT_CAMERA_LOST = 7

CAMERA_FPS = 30.0
CAMERA_BUFFERS = 2            # driver buffers; 1 halves the delivered rate (see above)
NEWEST_FRAME_WAIT_S = 0.25    # NewestFrameCapture.read(): no new frame this long = a failed read
FAILED_READ_PAUSE_S = 0.01    # ... and its thread's pause after a failed device read
FIRST_FRAME_TIMEOUT_S = 3.0   # an opened device that never delivers is "no camera"
CAMERA_LOST_AFTER_S = 2.0     # consecutive failed reads mid-run
CAMERA_REOPEN_DELAYS_S = (0.5, 1.0, 2.0, 4.0)  # then every 4 s
CAMERA_REOPEN_BUDGET_S = 60.0  # a camera gone this long is EXIT_CAMERA_LOST
NO_CAMERA_PACKET_S = 0.1      # FLAG_NO_CAMERA heartbeat while it is gone
DEVICE_TS_MAX_AGE_S = 0.5     # a device timestamp older than this is not on our clock


class ServerExit(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


V4L2_CAP_VIDEO_CAPTURE = 0x00000001
V4L2_CAP_VIDEO_CAPTURE_MPLANE = 0x00001000
V4L2_CAP_DEVICE_CAPS = 0x80000000
VIDIOC_QUERYCAP = 0x80685600   # _IOR('V', 0, struct v4l2_capability), 104 bytes


class V4L2Devices:
    """The box's V4L2 nodes. Tests substitute an object with the same three
    methods (tests/test_pose_server.py FakeDevices)."""

    by_id_dir = "/dev/v4l/by-id"

    def available(self):
        return sys.platform.startswith("linux")

    def nodes(self):
        """/dev/videoN paths, by N."""
        try:
            names = os.listdir("/dev")
        except OSError:
            return []
        nums = sorted(int(n[5:]) for n in names if n.startswith("video") and n[5:].isdigit())
        return [f"/dev/video{n}" for n in nums]

    def query(self, path):
        """(is_capture, description) for a node; (False, why) if it cannot be
        asked. The description is the V4L2 card name."""
        import fcntl
        try:
            fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
        except OSError as e:
            return False, f"cannot open ({e.strerror})"
        try:
            buf = bytearray(104)
            fcntl.ioctl(fd, VIDIOC_QUERYCAP, buf)
        except OSError as e:
            return False, f"not a V4L2 device ({e.strerror})"
        finally:
            os.close(fd)
        card = bytes(buf[16:48]).split(b"\0", 1)[0].decode("utf-8", "replace")
        caps, dev_caps = struct.unpack_from("<II", buf, 84)
        if caps & V4L2_CAP_DEVICE_CAPS:
            caps = dev_caps
        if caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE):
            return True, card
        return False, f"{card}: no video capture (caps 0x{caps:08x}, e.g. a metadata node)"

    def realpath(self, path):
        return os.path.realpath(path)

    def exists(self, path):
        return os.path.exists(path)

    def by_id_names(self):
        """{/dev/v4l/by-id name: the /dev/videoN it points at}."""
        try:
            names = sorted(os.listdir(self.by_id_dir))
        except OSError:
            return {}
        return {n: os.path.realpath(os.path.join(self.by_id_dir, n)) for n in names}


# ---- Which cameras there are (--list-cameras; the game's camera setting) ----
#
# A camera has three things here: an identity that stays with the device
# (`id`), a name for a person (`name`), and what to pass to --camera to get it
# (`spec`, "id:<identity>" whenever there is a real identity). An index is not
# an identity: MEASURED on a Mac with OpenCV 5.0.0, AVFoundation cameras are
# numbered in order of their uniqueID string (index 0 = "MX Brio"
# 0x220000046d0944, index 1 = "FaceTime HD Camera" 3642F2CD-...), so unplugging
# the first renumbers the second; a /dev/videoN moves the same way when a USB
# camera re-enumerates.
CAMERA_ID_PREFIX = "id:"
CAMERA_LIST_HEADER = "dc3-pose-cameras 1"   # first line of --list-cameras tsv


def _number_duplicate_names(cameras):
    """Two cameras of one model get "Name" and "Name (2)", in list order."""
    seen = {}
    for cam in cameras:
        n = seen.get(cam["name"], 0) + 1
        seen[cam["name"]] = n
        if n > 1:
            cam["name"] = f"{cam['name']} ({n})"
    return cameras


def linux_camera_list(devices):
    """The V4L2 video-capture devices, by node number -- so the first is the
    one --camera auto opens. The identity is the /dev/v4l/by-id name where
    udev made one (it names the USB device, not the node), else the node."""
    by_node = {}
    for name, node in sorted(getattr(devices, "by_id_names", dict)().items()):
        by_node.setdefault(node, name)
    cameras = []
    for node in devices.nodes():
        ok, desc = devices.query(node)
        if not ok:
            continue
        ident = by_node.get(node)
        cameras.append({
            "id": ident or node,
            "name": desc or node,
            "spec": (CAMERA_ID_PREFIX + ident) if ident else node,
            "index": int(node[len("/dev/video"):]),
            "node": node,
        })
    return _number_duplicate_names(cameras)


def parse_system_profiler_cameras(text):
    """`system_profiler SPCameraDataType -json` -> the cameras in OpenCV's
    AVFoundation order: sorted by unique id (see CAMERA_ID_PREFIX above), so
    `index` is the number cv2.VideoCapture takes NOW. Raises ValueError on
    text that is not that JSON."""
    import json
    data = json.loads(text)
    items = data.get("SPCameraDataType") if isinstance(data, dict) else None
    found = []
    for item in items or []:
        if not isinstance(item, dict):
            continue
        uid = item.get("spcamera_unique-id")
        if not uid:
            continue
        found.append((str(uid), str(item.get("_name") or uid)))
    found.sort(key=lambda pair: pair[0])
    return _number_duplicate_names([
        {"id": uid, "name": name, "spec": CAMERA_ID_PREFIX + uid, "index": i}
        for i, (uid, name) in enumerate(found)])


def _run_system_profiler():
    import subprocess
    done = subprocess.run(["system_profiler", "SPCameraDataType", "-json"],
                          capture_output=True, text=True, timeout=20)
    if done.returncode != 0:
        raise OSError(f"system_profiler exited {done.returncode}: {done.stderr.strip()[:200]}")
    return done.stdout


def list_cameras(devices=None, platform=None, profiler=_run_system_profiler):
    """This box's cameras, without opening one: [{id, name, spec, index}, ...].
    Raises OSError / ValueError when the box cannot be asked."""
    platform = sys.platform if platform is None else platform
    if platform.startswith("linux"):
        return linux_camera_list(V4L2Devices() if devices is None else devices)
    if platform == "darwin":
        return parse_system_profiler_cameras(profiler())
    return []


def find_camera(cameras, wanted):
    """The entry `wanted` names -- its spec, its identity (with or without
    "id:"), or its name (any case) -- or None."""
    wanted = str(wanted).strip()
    bare = wanted[len(CAMERA_ID_PREFIX):] if wanted.startswith(CAMERA_ID_PREFIX) else wanted
    for cam in cameras:
        if wanted == cam["spec"] or bare == cam["id"]:
            return cam
    if wanted.startswith(CAMERA_ID_PREFIX):
        return None
    for cam in cameras:
        if cam["name"].lower() == wanted.lower():
            return cam
    return None


def format_camera_list(cameras, fmt="json", platform=None, error=None):
    """--list-cameras' output. json is for people and scripts; tsv is what the
    game reads (PoseCamera_Native.cpp): the header line, then
    "camera<TAB>spec<TAB>id<TAB>name" per camera, "error<TAB>why" if the box
    could not be asked."""
    if fmt == "tsv":
        def clean(value):
            return " ".join(str(value).split())
        lines = [CAMERA_LIST_HEADER]
        if error:
            lines.append("error\t" + clean(error))
        lines += ["\t".join(("camera", clean(c["spec"]), clean(c["id"]), clean(c["name"])))
                  for c in cameras]
        return "\n".join(lines) + "\n"
    import json
    out = {"platform": sys.platform if platform is None else platform, "cameras": cameras}
    if error:
        out["error"] = error
    return json.dumps(out, indent=2) + "\n"


def resolve_camera(spec, devices, log=None, lister=None):
    """--camera -> (source for cv2.VideoCapture, description). The source is a
    /dev/videoN path, or an int index where there is no V4L2 (macOS). Raises
    ServerExit(EXIT_NO_CAMERA) when nothing matches. Called on every open, so
    an identity ("id:...", or a camera's name) follows the device to whatever
    index or node it has now. `lister` stands in for list_cameras (tests)."""
    log = log or (lambda msg: print(msg, file=sys.stderr))
    spec = "auto" if spec is None else str(spec).strip()
    if not devices.available():
        if spec in ("", "auto"):
            return 0, f"camera {spec}"
        if spec.isdigit():
            return int(spec), f"camera {spec}"
        if not spec.startswith(CAMERA_ID_PREFIX) and ("/" in spec or "://" in spec):
            return spec, f"camera {spec}"       # a path or URL: OpenCV's business
        try:
            cameras = (lister or list_cameras)()
        except (OSError, ValueError) as e:
            raise ServerExit(EXIT_NO_CAMERA, f"camera {spec}: could not list the cameras ({e})")
        cam = find_camera(cameras, spec)
        if cam is None:
            there = ", ".join(c["name"] for c in cameras) or "no cameras"
            raise ServerExit(EXIT_NO_CAMERA, f"camera {spec} is not present (found: {there})")
        return cam["index"], f"camera {cam['index']} ({cam['name']}) [{spec}]"

    def scan(why):
        skipped = []
        for node in devices.nodes():
            ok, desc = devices.query(node)
            if ok:
                extra = f"; skipped {', '.join(skipped)}" if skipped else ""
                return node, f"{node} ({desc}) [{why}{extra}]"
            skipped.append(f"{node} ({desc})")
        found = ", ".join(skipped) if skipped else "no /dev/video* nodes"
        raise ServerExit(EXIT_NO_CAMERA, f"no video-capture camera found ({why}; {found})")

    if spec in ("", "auto"):
        return scan("auto")
    if spec.isdigit():
        node = f"/dev/video{int(spec)}"
        if not devices.exists(node):
            log(f"Camera {spec}: {node} does not exist; looking for a camera instead")
            return scan(f"camera {spec} missing")
        ok, desc = devices.query(node)
        if not ok:
            log(f"Camera {spec}: {node} is not a camera ({desc}); looking for one instead")
            return scan(f"camera {spec} not a capture device")
        return node, f"{node} ({desc}) [camera {spec}]"
    path = spec[len(CAMERA_ID_PREFIX):] if spec.startswith(CAMERA_ID_PREFIX) else spec
    if not path.startswith("/"):
        for cand in (os.path.join(devices.by_id_dir, path), os.path.join("/dev", path)):
            if devices.exists(cand):
                path = cand
                break
        else:
            cam = find_camera(linux_camera_list(devices), spec)   # a camera's name
            if cam is not None:
                path = cam["node"]
    if not devices.exists(path):
        raise ServerExit(EXIT_NO_CAMERA, f"camera {spec} is not present")
    node = devices.realpath(path)
    ok, desc = devices.query(node)
    if not ok:
        raise ServerExit(EXIT_NO_CAMERA, f"camera {spec} ({node}) is not a camera: {desc}")
    return node, f"{node} ({desc}) [{spec}]"


def open_camera(cv2, index, width, height, fps=CAMERA_FPS,
                first_frame_timeout=FIRST_FRAME_TIMEOUT_S,
                clock=time.monotonic, sleep=time.sleep):
    """Open camera `index`, request width x height @ fps with CAMERA_BUFFERS
    driver buffers, and wait for a first frame. Returns (cap, frame). Raises ServerExit
    (EXIT_NO_CAMERA) if it will not open or opens but sends nothing -- a
    /dev/video node that is a metadata or output device opens fine and never
    delivers a picture."""
    v4l2 = getattr(cv2, "CAP_V4L2", None)
    if isinstance(index, str) and v4l2 is not None:
        cap = cv2.VideoCapture(index, v4l2)
    else:
        cap = cv2.VideoCapture(index)
    if not cap.isOpened():
        raise ServerExit(EXIT_NO_CAMERA, f"could not open camera {index}")
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, CAMERA_BUFFERS)
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


class NewestFrameCapture:
    """A camera read on its own thread: read() hands out the newest frame the
    device has delivered, once, and get(CAP_PROP_POS_MSEC) is that frame's own
    stamp. The slice of cv2.VideoCapture the server loop uses (read / get /
    release), so the loop is the same with or without it.

    Why a thread: the camera needs a free buffer at the start of every frame
    (CAMERA_BUFFERS), and a loop that reads only when inference is done would
    get the oldest queued frame, not the newest. The thread reads every frame
    as it completes; frames inference had no time for are replaced
    (`replaced`), never queued.

    read() returns (False, None) when the device read failed, or when no new
    frame arrived within `wait_s`; the caller's lost-camera timing is the same
    as for a cv2 read that fails. `read_time` is when the frame last returned
    came off the device (None after a failed read).

    Only the thread touches the capture once it runs, release included:
    release() asks it to stop and returns, and the thread releases the device
    when its read() comes back (cv2.VideoCapture is not safe to release under
    a read in flight; a stalled V4L2 device holds read() for up to 10 s)."""

    def __init__(self, cap, cv2, clock=time.monotonic, wait_s=NEWEST_FRAME_WAIT_S,
                 pause=time.sleep):
        self._cap, self._clock, self._wait_s, self._pause = cap, clock, wait_s, pause
        self._pos_prop = cv2.CAP_PROP_POS_MSEC
        self._cond = threading.Condition()
        self._frame = None
        self._seq = 0             # frames the thread has read
        self._taken = 0           # _seq of the frame read() last returned
        self._pos_msec = 0.0      # of the frame waiting in _frame
        self._arrived = None
        self._failed = False      # the thread's most recent device read failed
        self._stop = False
        # open_camera() has just read the first frame: its stamp is the one a
        # get() before our first read() means.
        self._taken_pos_msec = self._device_pos_msec()
        self.read_time = None
        self.replaced = 0         # frames overwritten before read() took them
        self._thread = threading.Thread(target=self._grab, name="pose-capture", daemon=True)
        self._thread.start()

    @property
    def frames_read(self):
        """Frames the device has delivered to the thread so far."""
        with self._cond:
            return self._seq

    def _device_pos_msec(self):
        try:
            return float(self._cap.get(self._pos_prop))
        except Exception:
            return 0.0

    def _grab(self):
        try:
            while True:
                with self._cond:
                    if self._stop:
                        return
                ok, frame = self._cap.read()
                ok = bool(ok) and frame is not None
                now = self._clock()
                pos = self._device_pos_msec() if ok else 0.0
                with self._cond:
                    if self._stop:
                        return
                    if ok:
                        if self._seq != self._taken:
                            self.replaced += 1
                        self._seq += 1
                        self._frame, self._pos_msec, self._arrived = frame, pos, now
                    self._failed = not ok
                    self._cond.notify_all()
                if not ok:
                    self._pause(FAILED_READ_PAUSE_S)   # a device that is gone fails at once
        finally:
            try:
                self._cap.release()
            except Exception:
                pass

    def read(self):
        with self._cond:
            if self._seq == self._taken and not self._failed:
                self._cond.wait_for(lambda: self._seq != self._taken or self._failed or self._stop,
                                    timeout=self._wait_s)
            if self._seq == self._taken:
                self.read_time = None
                return False, None
            self._taken = self._seq
            frame, self._frame = self._frame, None
            self._taken_pos_msec, self.read_time = self._pos_msec, self._arrived
            return True, frame

    def get(self, prop):
        if prop == self._pos_prop:
            with self._cond:
                return self._taken_pos_msec
        return 0.0   # the device belongs to the thread; ask it before wrapping

    def release(self, join_s=0.5):
        with self._cond:
            self._stop = True
            self._cond.notify_all()
        if self._thread is not threading.current_thread():
            self._thread.join(join_s)


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
    if getattr(args, "frames", None):
        kwargs["segmentation"] = True
    if getattr(args, "probe_second_person", 0.0):
        kwargs["probe_s"] = args.probe_second_person
    return kwargs


def parse_frames(value):
    """--frames WxH -> (w, h); raises argparse-friendly ValueError."""
    try:
        w, h = (int(v) for v in value.lower().split("x"))
    except Exception:
        raise ValueError(f"--frames wants WxH, got {value!r}")
    if not (1 <= w <= 1920 and 1 <= h <= 1920):
        raise ValueError(f"--frames {value}: each side must be 1..1920")
    return w, h


# ---- the silhouette mask, refined (PLAN P-20) -------------------------------
# MediaPipe's per-pose mask is soft, low-resolution and independent from frame
# to frame. Cut at 0.5 it flickers along the edge while the player stands
# still, opens small holes, sheds specks, and now and then loses a forearm or
# a hand the tracker still sees (measured: scripts/pose_mask_quality.py,
# docs/native/POSE_INPUT.md "The mask, measured"). MaskRefiner keeps a little
# state per track id and gives the same ordinal mask with those four fixed:
#   smooth      a per-pixel filter on the soft mask that follows a large change
#               at once and averages a small one (so it adds no trail to a
#               moving limb);
#   hysteresis  a pixel turns on above MASK_ENTER and stays on down to
#               MASK_STAY;
#   limbs       a capsule along each limb MediaPipe reports as visible and the
#               mask does not reach, so a tracked arm or leg is never missing
#               from the silhouette;
#   tidy        a 3x3 close, then pieces smaller than MASK_SPECK_SHARE of the
#               person are dropped.
# --mask raw (DC3_POSE_MASK=raw in the game) is the mask as it was.
MASK_MODES = ("refined", "raw")
MASK_ENTER = 0.5
MASK_STAY = 0.25
MASK_SMOOTH_MIN = 0.25     # filter weight of the new frame for a small change
MASK_SMOOTH_FULL = 0.5     # a change this large (0..1) is followed at once
MASK_SPECK_SHARE = 0.01
MASK_VISIBLE = 0.5         # a landmark is "tracked" from this visibility
MASK_LIMB_PROBES = (0.0, 0.25, 0.5, 0.75, 1.0)   # where a limb is checked against the mask
MASK_LIMB_MARGIN = 2       # px at 320 wide: a probe this close to the mask counts as covered
# BlazePose landmark pairs and the capsule's width as a share of the body
# scale (the larger of shoulder-to-hip and 1.2 x shoulder width, in pixels).
MASK_LIMBS = (
    (11, 13, 0.17), (13, 15, 0.13), (15, 19, 0.12),      # left arm, hand
    (12, 14, 0.17), (14, 16, 0.13), (16, 20, 0.12),      # right arm, hand
    (23, 25, 0.28), (25, 27, 0.20), (27, 31, 0.14),      # left leg, foot
    (24, 26, 0.28), (26, 28, 0.20), (28, 32, 0.14),      # right leg, foot
)


class MaskRefiner:
    """The ordinal person mask from MediaPipe's soft masks, steadied across
    frames (see the block comment above). One per stream; `ordinal()` once per
    camera frame, with the frame's persons in packet order."""

    def __init__(self, cv2, np, smooth=True, hysteresis=True, limbs=True, tidy=True,
                 enter=MASK_ENTER, stay=MASK_STAY, smooth_min=MASK_SMOOTH_MIN,
                 smooth_full=MASK_SMOOTH_FULL, limb_width=1.0, limb_margin=MASK_LIMB_MARGIN):
        self.cv2, self.np = cv2, np
        self.smooth, self.hysteresis, self.limbs, self.tidy = smooth, hysteresis, limbs, tidy
        self.enter, self.stay = enter, (stay if hysteresis else enter)
        self.smooth_min, self.smooth_full, self.limb_width = smooth_min, smooth_full, limb_width
        self.limb_margin = limb_margin
        self.state = {}       # track id -> (filtered soft mask, on)
        self._kernel = np.ones((3, 3), dtype=np.uint8)

    def _capsules(self, canvas, landmarks, w, h):
        """Draw the limbs the mask lost into `canvas` (0/1): a capsule for each
        MASK_LIMBS segment whose two landmarks are tracked and which the mask
        does not reach (within MASK_LIMB_MARGIN) at every one of
        MASK_LIMB_PROBES points along it. limbs="all" draws every tracked
        segment instead. Measured and dropped: "all" (thickens limbs the mask
        already has, adds edge flicker), no margin (a forearm resting on the
        hip blinks across the gap beside it), holding a fill for some frames
        (no gain)."""
        cv2, np = self.cv2, self.np
        xy, vis = landmarks
        px = np.asarray(xy, dtype=np.float32) * (w, h)
        scale = max(float(np.linalg.norm((px[11] + px[12]) * 0.5 - (px[23] + px[24]) * 0.5)),
                    1.2 * float(np.linalg.norm(px[11] - px[12])))
        if not (scale > 0.0) or scale > 4.0 * max(w, h):
            return
        near = None
        todo = []
        for a, b, share in MASK_LIMBS:
            if not (vis[a] >= MASK_VISIBLE and vis[b] >= MASK_VISIBLE):
                continue
            if self.limbs != "all":
                if near is None:
                    near = canvas
                    if self.limb_margin > 0:
                        r = max(1, int(round(self.limb_margin * w / 320.0)))
                        near = cv2.dilate(canvas, np.ones((2 * r + 1, 2 * r + 1), dtype=np.uint8))
                covered = True
                for t in MASK_LIMB_PROBES:
                    x = int(px[a][0] + t * (px[b][0] - px[a][0]))
                    y = int(px[a][1] + t * (px[b][1] - px[a][1]))
                    if 0 <= x < w and 0 <= y < h and not near[y, x]:
                        covered = False
                        break
                if covered:
                    continue
            todo.append((a, b, share))
        for a, b, share in todo:
            cv2.line(canvas, (int(px[a][0]), int(px[a][1])), (int(px[b][0]), int(px[b][1])),
                     1, max(1, int(round(share * self.limb_width * scale))), cv2.LINE_8)

    def ordinal(self, masks, size, ids=None, landmarks=None):
        cv2, np = self.cv2, self.np
        w, h = size
        ordinal = np.zeros((h, w), dtype=np.uint8)
        best = np.zeros((h, w), dtype=np.float32)
        state = {}
        for k, m in enumerate(masks, start=1):
            if m is None:
                continue
            tid = ids[k - 1] if ids is not None and k - 1 < len(ids) else -k
            s = cv2.resize(np.asarray(m, dtype=np.float32), (w, h), interpolation=cv2.INTER_LINEAR)
            was = self.state.get(tid)
            if was is not None and was[0].shape != s.shape:
                was = None
            if was is not None and self.smooth:
                d = s - was[0]
                gain = np.clip(np.abs(d) * (1.0 / self.smooth_full), self.smooth_min, 1.0)
                gain *= d
                gain += was[0]
                s = gain
            on = s > self.enter
            if was is not None and self.stay < self.enter:
                on |= was[1] & (s > self.stay)
            state[tid] = (s, on)
            shape = on.astype(np.uint8)
            lm = landmarks[k - 1] if landmarks is not None and k - 1 < len(landmarks) else None
            if self.limbs and lm is not None:
                self._capsules(shape, lm, w, h)
            if self.tidy:
                shape = cv2.morphologyEx(shape, cv2.MORPH_CLOSE, self._kernel)
                n, labels, stats, _ = cv2.connectedComponentsWithStats(shape, connectivity=8)
                if n > 2:
                    areas = stats[:, cv2.CC_STAT_AREA].copy()
                    areas[0] = 0
                    keep = areas >= max(1.0, MASK_SPECK_SHARE * float(areas.sum()))
                    keep[int(areas.argmax())] = True
                    keep[0] = False
                    shape = keep[labels].astype(np.uint8)
            # Between persons the more confident soft mask wins, as before; a
            # filled pixel (limb, close) counts as barely over the threshold.
            score = np.maximum(s, self.enter + 1e-3)
            win = (shape > 0) & (score > best)
            np.copyto(ordinal, k, where=win)
            np.copyto(best, score, where=win)
        self.state = state
        return ordinal


def camera_block(frame_bgr, masks, size, cv2, np, refiner=None, ids=None, landmarks=None):
    """(rgb_bytes, mask_bytes, w, h) for the v3 camera block: the frame scaled
    to `size` as RGB888, and one byte per pixel naming the most confident
    person (1-based, in `masks` order) whose mask passes MASK_THRESHOLD.
    With a `refiner` (MaskRefiner) the mask is its steadied one; `ids` are the
    persons' track ids and `landmarks` their image landmarks, in `masks`
    order."""
    w, h = size
    rgb = cv2.cvtColor(cv2.resize(frame_bgr, (w, h), interpolation=cv2.INTER_AREA),
                       cv2.COLOR_BGR2RGB)
    if refiner is not None:
        ordinal = refiner.ordinal(masks, size, ids, landmarks)
        return np.ascontiguousarray(rgb).tobytes(), ordinal.tobytes(), w, h
    ordinal = np.zeros((h, w), dtype=np.uint8)
    best = np.full((h, w), MASK_THRESHOLD, dtype=np.float32)
    for k, m in enumerate(masks, start=1):
        if m is None:
            continue
        m = cv2.resize(np.asarray(m, dtype=np.float32), (w, h),
                       interpolation=cv2.INTER_LINEAR)
        win = m > best
        ordinal[win] = k
        best[win] = m[win]
    return np.ascontiguousarray(rgb).tobytes(), ordinal.tobytes(), w, h


def mask_variant(options):
    """A camera_block with its own MaskRefiner(**options), for
    scripts/pose_mask_quality.py: f(frame, masks, size, cv2, np, ids, landmarks)."""
    box = []

    def block(frame_bgr, masks, size, cv2, np, ids, landmarks):
        if not box:
            box.append(MaskRefiner(cv2, np, **options))
        return camera_block(frame_bgr, masks, size, cv2, np, box[0], ids, landmarks)
    return block


def header_flags(latency_ms=None):
    """The v2/v3 header flags word: always FLAG_UNLEVELLED, plus a declared
    scoring latency when `latency_ms` is not None (rounded to 5 ms, 0..1000)."""
    flags = FLAG_UNLEVELLED
    if latency_ms is not None:
        units = int(round(max(0.0, min(1000.0, float(latency_ms))) / LATENCY_UNIT_MS))
        flags |= FLAG_LATENCY_DECLARED | (units << 8)
    return flags


def source_latency_ms(args):
    """What this run declares (None = nothing, the game's default stands)."""
    if args.source_latency_ms is not None:
        return args.source_latency_ms
    if args.video is not None:
        return RECORDED_SOURCE_LATENCY_MS
    return None


def pack_frame(frame_id, timestamp, frame_w, frame_h, num_landmarks, layout, persons,
               image=None, flags=FLAG_UNLEVELLED):
    """One length-prefixed packet (see module docstring): protocol v2, or v3
    when `image` is given -- (rgb_bytes, mask_bytes, w, h), or () for a v3
    packet with no picture this frame. `flags`: header_flags()."""
    packet = struct.pack(
        "<IIIdHHBBH",
        PROTOCOL_MAGIC if image is None else PROTOCOL_MAGIC_V3,
        frame_id, len(persons), timestamp,
        int(frame_w), int(frame_h), num_landmarks, layout, flags,
    )
    for tid, kpts in persons:
        packet += struct.pack("<i", tid)
        for x, y, z, c in kpts:
            packet += struct.pack("<ffff", x, y, z, c)
    if image is not None:
        if image:
            rgb, mask, w, h = image
            packet += struct.pack("<HHBBH", w, h, WIRE_RGB888, WIRE_MASK_ORDINAL, 0)
            packet += rgb + mask
        else:
            packet += struct.pack("<HHBBH", 0, 0, WIRE_RGB888, WIRE_MASK_ORDINAL, 0)
    return struct.pack("<I", len(packet)) + packet


SEND_BUFFER_BYTES = 4 << 20   # the client socket's send buffer; see widen_send_buffer


def widen_send_buffer(sock, want=SEND_BUFFER_BYTES, floor=64 << 10):
    """Ask for a send buffer that holds whole packets, and return the size the
    system reports afterwards.

    The Outbox sends once per processed frame and drops the next packet while
    one is still in flight, so a buffer smaller than a packet turns into
    dropped frames: the game gets one packet for every (packet size / buffer
    size) camera frames. macOS gives an AF_UNIX stream socket 8 KB
    (net.local.stream.sendspace); with ~300 KB packets a Mac tracked two
    dancers at 28 fps and delivered 2-3 of them a second (sent=9 dropped=140 in
    one 5 s window). Linux's default is ~208 KB, which is why it went unseen.

    A size the system refuses (above kern.ipc.maxsockbuf on macOS) is halved
    until one is taken; Linux clamps silently instead of refusing."""
    size = want
    while size >= floor:
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, size)
            break
        except OSError:
            size //= 2
    return sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)


class Outbox:
    """Whole packets to a non-blocking client. A v3 packet is ~300 KB, more
    than a socket buffer, so a send can stop part-way; the remainder goes out
    on later calls. While anything is pending a new packet is DROPPED, never
    interleaved -- the stream stays framed, and the game sees a skipped frame
    rather than a stale queue (a slow reader gets the newest frame next)."""

    def __init__(self, sock):
        self.sock = sock
        self.pending = b""
        self.sent_packets = 0
        self.dropped_packets = 0

    def flush(self):
        while self.pending:
            try:
                n = self.sock.send(self.pending)
            except BlockingIOError:
                return False
            self.pending = self.pending[n:]
        return True

    def offer(self, packet):
        """Queue `packet` unless one is still in flight; then try to send."""
        if not self.flush():
            self.dropped_packets += 1
            return False
        self.pending = packet
        self.sent_packets += 1
        self.flush()
        return True


class Stats:
    """DC3_POSE_STATS=1 (the playtest kit sets it): every 5 s, one line on
    stderr -- frames processed per second, MediaPipe's time per frame (mean,
    95th percentile), people per frame, packets sent / dropped by the outbox.
    Read with the engine's own DC3 PERF line (PLAN L-3)."""

    PERIOD_S = 5.0

    def __init__(self):
        self.t0 = time.monotonic()
        self.infer = []
        self.persons = 0
        self.last_sent = 0
        self.last_dropped = 0
        self.last_camera = None   # (capture object, its frames_read) at the last line

    def frame(self, infer_s, persons):
        self.infer.append(infer_s)
        self.persons += persons

    def camera_fps(self, cap, dt):
        """Frames per second the camera itself delivered over the window, or
        None when the capture does not count them (no capture thread, a video
        file) or was reopened inside the window."""
        count = getattr(cap, "frames_read", None)
        if not isinstance(count, int):
            self.last_camera = None
            return None
        prev, self.last_camera = self.last_camera, (cap, count)
        if prev is None or prev[0] is not cap or dt <= 0:
            return None
        return (count - prev[1]) / dt

    def maybe_print(self, outbox, cap=None):
        now = time.monotonic()
        dt = now - self.t0
        if dt < self.PERIOD_S:
            return
        n = len(self.infer)
        ms = sorted(x * 1000.0 for x in self.infer)
        sent = outbox.sent_packets if outbox else 0
        dropped = outbox.dropped_packets if outbox else 0
        # fps = frames this server processed; camera_fps = frames the camera
        # delivered. fps well under camera_fps: inference is the limit.
        # Both low: the camera is (exposure in a dim room).
        cam = self.camera_fps(cap, dt)
        print("POSE_STATS: fps=%.1f infer_ms_mean=%.1f infer_ms_p95=%.1f persons=%.2f "
              "sent=%d dropped=%d window_s=%.1f%s" % (
                  n / dt, (sum(ms) / n) if n else 0.0, ms[int(0.95 * (n - 1))] if n else 0.0,
                  (self.persons / n) if n else 0.0, sent - self.last_sent,
                  dropped - self.last_dropped, dt,
                  "" if cam is None else " camera_fps=%.1f" % cam), file=sys.stderr, flush=True)
        self.t0 = now
        self.infer = []
        self.persons = 0
        self.last_sent, self.last_dropped = sent, dropped


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="DC3 Pose Estimation Server")
    parser.add_argument("--socket", default="/tmp/dc3_pose.sock", help="Unix socket path")
    input_group = parser.add_mutually_exclusive_group()
    input_group.add_argument("--camera", default="auto",
                             help="auto (default: the first V4L2 video-capture device), "
                                  "an index, a /dev/videoN path, a /dev/v4l/by-id name, or "
                                  "what --list-cameras prints for a camera: its spec "
                                  "(id:<identity>) or its name")
    parser.add_argument("--list-cameras", nargs="?", const="json", default=None,
                        choices=("json", "tsv"), metavar="json|tsv",
                        help="Print this box's cameras (identity, name, the --camera spec) "
                             "and exit, without opening one. json (default) or tsv, which "
                             "is what the game's camera setting reads.")
    input_group.add_argument("--video", default=None, help="Path to a video file to use as input instead of a camera")
    parser.add_argument("--loop", action="store_true", help="Loop the video at EOF (video mode only)")
    parser.add_argument("--fps", type=float, default=None,
                         help="Wall-clock pacing override for video mode (default: video's own CAP_PROP_FPS, "
                              "fallback 20.0 if unreadable). Ignored in camera mode.")
    parser.add_argument("--width", type=int, default=640, help="Capture width")
    parser.add_argument("--height", type=int, default=480, help="Capture height")
    parser.add_argument("--model", default=DEFAULT_MODEL,
                        help="MediaPipe pose landmarker .task path")
    parser.add_argument("--hfov", type=float, default=None,
                        help="Camera horizontal FOV in degrees. Default 58.51 = DC3's "
                             "own Kinect FOV, recovered from the target disassembly of "
                             "NuiTransformSkeletonToDepthImage. Absolute depth scales "
                             "with this, so calibrating it to the real camera improves "
                             "absolute-position accuracy (pose SHAPE is FOV-invariant).")
    parser.add_argument("--conf", type=float, default=0.5, help="Confidence threshold")
    parser.add_argument("--max-persons", type=int, default=6, help="Max tracked persons")
    parser.add_argument("--probe-second-person", type=float, default=0.0, metavar="S",
                        help="Track with MediaPipe num_poses 1 and look for a second person "
                             "every S seconds on a background thread, switching to num_poses 2 "
                             "while two are seen (pose_mediapipe.SecondPersonProbe). ~40%% less "
                             "inference with one dancer; a second player is found up to S s "
                             "later. 0 (default) = num_poses 2 on every frame "
                             "(docs/native/POSE_INPUT.md#performance)")
    parser.add_argument("--no-capture-thread", dest="capture_thread", action="store_false",
                        help="Read the camera in the processing loop instead of on its own "
                             "thread (NewestFrameCapture). With inference slower than the "
                             "camera this hands MediaPipe an older frame; for comparison.")
    parser.add_argument("--show", action="store_true", help="Show debug visualization window")
    parser.add_argument("--source-latency-ms", type=float, default=None, metavar="MS",
                        help="Declare this source's scoring latency to the game (0..1000 ms). "
                             "Default: 0 for --video (a recorded file has no camera delay), "
                             "nothing for a camera (the game keeps its 100 ms). The game's "
                             "DC3_POSE_LATENCY_MS overrides either.")
    parser.add_argument("--frames", default=None, metavar="WxH",
                        help="Also send the camera picture and a person mask at WxH "
                             "(protocol v3, the in-game player view; PLAN P-4)")
    parser.add_argument("--record-video", default=None, metavar="PATH",
                        help="Also write every frame given to MediaPipe, at the size the "
                             "source delivered it, to PATH (a video; .mkv recommended) with "
                             "a STEM.frames.tsv of each frame's packet id and capture "
                             "timestamp, on a thread of its own: a frame the writer cannot "
                             "take at once is dropped and counted, never waited for "
                             "(video_recorder.py). The game passes DC3_POSE_RECORD_VIDEO.")
    parser.add_argument("--record-max-gb", type=float, default=None, metavar="GB",
                        help="Stop recording when the parts reach GB (default "
                             "video_recorder.DEFAULT_MAX_GB). The game passes "
                             "DC3_POSE_RECORD_VIDEO_MAX_GB.")
    parser.add_argument("--record-min-free-gb", type=float, default=None, metavar="GB",
                        help="Stop recording when the disk has less than GB free "
                             "(default video_recorder.DEFAULT_MIN_FREE_GB)")
    parser.add_argument("--record-codec", default=None, metavar="FOURCC",
                        help="The recording's codec (default mp4v: pip OpenCV has no H.264 encoder)")
    parser.add_argument("--mask", choices=MASK_MODES, default="refined",
                        help="The person mask of --frames: refined (default: steadied across "
                             "frames, holes closed, tracked limbs filled in -- MaskRefiner) or "
                             "raw (MediaPipe's mask cut at 0.5, as before PLAN P-20). The game "
                             "passes DC3_POSE_MASK=raw as --mask raw.")
    args = parser.parse_args(argv)
    if args.source_latency_ms is not None and not 0 <= args.source_latency_ms <= 1000:
        parser.error("--source-latency-ms wants 0..1000")
    if args.frames:
        try:
            args.frames_size = parse_frames(args.frames)
        except ValueError as e:
            parser.error(str(e))
    else:
        args.frames_size = None
    return args


def _default_backend(args):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from pose_mediapipe import MediaPipeBackend, NUM_DC3_JOINTS
        print(f"Loading MediaPipe model: {args.model}", file=sys.stderr)
        started = time.monotonic()
        backend = MediaPipeBackend(args.model, **backend_kwargs(args))
        print(f"MediaPipe model ready after {time.monotonic() - started:.2f} s",
              file=sys.stderr, flush=True)
    except ImportError as e:
        raise ServerExit(EXIT_NO_DEPS, f"mediapipe not importable: {e}")
    return backend, NUM_DC3_JOINTS, 1


def make_recorder(args, cv2, fps):
    """--record-video: the VideoRecorder, or None."""
    path = getattr(args, "record_video", None)
    if not path:
        return None
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import video_recorder as vr
    kw = {}
    if getattr(args, "record_max_gb", None) is not None:
        kw["max_bytes"] = args.record_max_gb * vr.GB
    if getattr(args, "record_min_free_gb", None) is not None:
        kw["min_free_bytes"] = args.record_min_free_gb * vr.GB
    if getattr(args, "record_codec", None):
        kw["codec"] = args.record_codec
    return vr.VideoRecorder(path, cv2, fps=fps, **kw)


def sigterm_is_a_clean_shutdown():
    """The game stops its server with SIGTERM (SIGKILL 2 s later).  With a
    recording open, turn it into the KeyboardInterrupt run() already shuts
    down on, so the video is closed and its sidecar gets its last line."""
    import signal

    def handler(signum, frame):
        raise KeyboardInterrupt
    try:
        signal.signal(signal.SIGTERM, handler)
    except ValueError:   # not the main thread
        pass


def run(args, cv2=None, make_backend=_default_backend, clock=time.monotonic,
        sleep=time.sleep, devices=None):
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
        if devices is None:
            devices = V4L2Devices()

        def open_resolved():
            source, desc = resolve_camera(args.camera, devices)
            print(f"Camera: using {desc}, requesting {args.width}x{args.height} "
                  f"@ {CAMERA_FPS:g} fps", file=sys.stderr)
            c, f = open_camera(cv2, source, args.width, args.height, clock=clock, sleep=sleep)
            h, w = f.shape[:2]
            print(f"Camera {source} delivering {w}x{h} "
                  f"(driver reports {c.get(cv2.CAP_PROP_FPS):g} fps)", file=sys.stderr)
            if getattr(args, "capture_thread", True):
                c = NewestFrameCapture(c, cv2, clock=clock)
            return c, f

        def arrival(c):
            """When the frame c.read() just returned came off the device."""
            t = getattr(c, "read_time", None)
            return clock() if t is None else t

        cap, first_frame = open_resolved()
        stamps = CaptureClock(cv2)

    # Set up Unix socket server. Bound BEFORE the model loads: the game is
    # blocked in Start() until it can connect, and loading takes ~1 s.
    declared = source_latency_ms(args)
    base_flags = header_flags(declared)   # every packet; FLAG_NO_CAMERA is added while lost
    if declared is None:
        print("Scoring latency: not declared (a live camera; the game keeps its default)",
              file=sys.stderr)
    else:
        print(f"Scoring latency: declaring {declared:.0f} ms to the game "
              f"({'--source-latency-ms' if args.source_latency_ms is not None else 'a recorded file'})",
              file=sys.stderr)

    sock_path = args.socket
    if os.path.exists(sock_path):
        os.unlink(sock_path)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock_path)
    server.listen(1)
    server.settimeout(0.1)  # Non-blocking accept

    print(f"Listening on {sock_path}", file=sys.stderr)

    client = None
    outbox = None
    frames_size = getattr(args, "frames_size", None)
    np = None
    refiner = None
    if frames_size:
        import numpy as np  # noqa: F811 -- only the v3 path needs it
        if getattr(args, "mask", "refined") != "raw":
            refiner = MaskRefiner(cv2, np)
        print(f"Person mask: {'refined' if refiner else 'raw'} (--mask)", file=sys.stderr)
    recorder = None
    try:
        recorder = make_recorder(args, cv2, video_fps if video_mode else CAMERA_FPS)
        backend, num_landmarks, layout = make_backend(args)

        stats = Stats() if os.environ.get("DC3_POSE_STATS") else None
        frame_id = 0
        next_frame_time = clock()  # video-mode pacing anchor
        eof_drain_deadline = None  # video-mode, --loop off: send ~2s of zero-person frames after EOF, then exit
        failing_since = None       # camera mode: first of a run of failed reads
        logged_ts_source = None
        lost_since = None          # camera mode: the camera is gone since (clock)
        next_reopen = None         # ... and the next reopen attempt is due at
        reopen_tries = 0
        flags = base_flags

        while True:
            # Accept new connections
            if client is None:
                try:
                    client, _ = server.accept()
                    client.setblocking(False)
                    sndbuf = widen_send_buffer(client)
                    outbox = Outbox(client)
                    print("Client connected (send buffer %d KB)" % (sndbuf // 1024),
                          file=sys.stderr)
                except socket.timeout:
                    pass

            if video_mode and eof_drain_deadline is not None:
                # Past EOF, not looping: emit zero-person frames for ~2s then exit cleanly.
                frame = None
                if clock() >= eof_drain_deadline:
                    print("Video EOF drain complete, exiting", file=sys.stderr)
                    break
            elif lost_since is not None:
                # The camera is gone: tell the game (FLAG_NO_CAMERA, no
                # persons) and reopen it -- re-resolving --camera, so a
                # re-enumerated device is found under its new node.
                frame = None
                now = clock()
                if now >= next_reopen:
                    reopen_tries += 1
                    try:
                        cap, first_frame = open_resolved()
                    except ServerExit as e:
                        if now - lost_since >= CAMERA_REOPEN_BUDGET_S:
                            raise ServerExit(EXIT_CAMERA_LOST,
                                             f"camera lost and not back after "
                                             f"{now - lost_since:.0f} s ({reopen_tries} tries): {e}")
                        delay = CAMERA_REOPEN_DELAYS_S[min(reopen_tries, len(CAMERA_REOPEN_DELAYS_S) - 1)]
                        print(f"Camera reopen {reopen_tries} failed ({e}); next try in {delay:g} s",
                              file=sys.stderr)
                        next_reopen = clock() + delay
                    else:
                        print(f"CAMERA_BACK after {clock() - lost_since:.1f} s "
                              f"({reopen_tries} tries)", file=sys.stderr)
                        lost_since = None
                        flags = base_flags
                        failing_since = None
                        continue
                if lost_since is not None:
                    timestamp = clock()
                    ts_ms = None
                    sleep(NO_CAMERA_PACKET_S)
            elif first_frame is not None:
                frame, first_frame = first_frame, None
                read_time = clock()
            else:
                ret, frame = cap.read()
                read_time = clock() if video_mode else arrival(cap)
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
                            print(f"CAMERA_LOST: no frame for {CAMERA_LOST_AFTER_S:g} s; "
                                  f"reopening (up to {CAMERA_REOPEN_BUDGET_S:g} s)", file=sys.stderr)
                            try:
                                cap.release()
                            except Exception:
                                pass
                            lost_since = read_time
                            next_reopen = read_time + CAMERA_REOPEN_DELAYS_S[0]
                            reopen_tries = 0
                            flags = base_flags | FLAG_NO_CAMERA
                            continue
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
            elif lost_since is None:
                timestamp, ts_ms, source = stamps.stamp(cap, read_time)
                if source != logged_ts_source:
                    logged_ts_source = source
                    print(f"Camera timestamps: {source} clock", file=sys.stderr)

            persons = []
            frame_w, frame_h = (args.width, args.height)
            if frame is not None:
                frame_h, frame_w = frame.shape[:2]
                if recorder is not None:
                    recorder.offer(frame, frame_id, timestamp, ts_ms)
                t_infer = time.perf_counter()
                for tid, joints in backend.process(frame, ts_ms):
                    if len(persons) >= args.max_persons:
                        break
                    persons.append((tid, joints))
                if stats:
                    stats.frame(time.perf_counter() - t_infer, len(persons))

            # Pack and send (protocol v2, or v3 with --frames — see module docstring)
            if client is not None:
                image = None
                if frames_size:
                    image = ()
                    if frame is not None:
                        masks = list(getattr(backend, "last_masks", []) or [])[:len(persons)]
                        landmarks = list(getattr(backend, "last_landmarks", []) or [])[:len(persons)]
                        image = camera_block(frame, masks, frames_size, cv2, np, refiner,
                                             [tid for tid, _ in persons], landmarks)
                msg = pack_frame(frame_id, timestamp, frame_w, frame_h,
                                 num_landmarks, layout, persons, image, flags=flags)
                try:
                    outbox.offer(msg)
                except (BrokenPipeError, ConnectionResetError, OSError):
                    print("Client disconnected", file=sys.stderr)
                    client.close()
                    client = None
                    outbox = None

            if args.show and frame is not None:
                cv2.imshow("DC3 Pose Server", frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break

            frame_id += 1
            if stats:
                stats.maybe_print(outbox, cap)

    except KeyboardInterrupt:
        print("\nShutting down", file=sys.stderr)
    finally:
        if recorder is not None:
            recorder.close()
        cap.release()
        if client:
            client.close()
        server.close()
        if os.path.exists(sock_path):
            os.unlink(sock_path)
        if args.show:
            cv2.destroyAllWindows()


PRIO_DARWIN_PROCESS = 4  # <sys/resource.h> on macOS


def clear_background_scheduling():
    """A live Mac sensor must not inherit the launcher's background policy.

    A game started from a background job hands its policy to the server it
    launches. MEASURED on an M3 MacBook Air: the server's first inferences
    then took 237-580 ms, against 22 ms afterwards.

    setpriority(PRIO_DARWIN_PROCESS, 0, 0) takes this process out of the
    background state, without root privileges; inference/capture workers
    created later inherit the normal policy. It succeeds whether or not the
    process was in that state, so the line printed says what was done, not
    what was found.
    """
    if sys.platform != "darwin":
        return False
    # os.PRIO_DARWIN_PROCESS is in the os module from Python 3.12 only; the
    # value is <sys/resource.h>'s, and MediaPipe's wheels run on older Pythons.
    which = getattr(os, "PRIO_DARWIN_PROCESS", PRIO_DARWIN_PROCESS)
    try:
        os.setpriority(which, 0, 0)
    except (AttributeError, OSError) as error:
        print(f"Pose scheduling: could not clear macOS background policy: {error}",
              file=sys.stderr, flush=True)
        return False
    print("Pose scheduling: macOS background policy cleared for this process",
          file=sys.stderr, flush=True)
    return True


def print_camera_list(fmt, out=None, lister=list_cameras):
    """--list-cameras: always exits 0; a box that cannot be asked prints no
    cameras and says why."""
    out = sys.stdout if out is None else out
    cameras, error = [], None
    try:
        cameras = lister()
    except (OSError, ValueError) as e:
        error = f"could not list the cameras: {e}"
        print(error, file=sys.stderr)
    out.write(format_camera_list(cameras, fmt, error=error))
    out.flush()


def main(argv=None):
    args = parse_args(argv)
    fmt = getattr(args, "list_cameras", None)
    if fmt:
        print_camera_list(fmt)
        return
    clear_background_scheduling()
    if getattr(args, "record_video", None):
        sigterm_is_a_clean_shutdown()
    try:
        run(args)
    except ServerExit as e:
        # One machine-greppable line; the game shows its own message for the code.
        print(f"POSE_SERVER_ERROR exit={e.code}: {e}", file=sys.stderr)
        sys.exit(e.code)


if __name__ == "__main__":
    main()
