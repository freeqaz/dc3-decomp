"""Camera mode of native/scripts/pose_server.py, without a camera (PLAN P-2).

No webcam, no v4l2loopback and no /dev/video* on the box this was written on,
so the camera half of the server is exercised through a fake capture object
that behaves like cv2.VideoCapture on a V4L2 device: it records what the server
asks for, serves frames on a fake clock, stamps them through CAP_PROP_POS_MSEC
the way the V4L2 backend does, and can stop delivering.

What is pinned:
  * the capture request: 640x480, 30 fps, a one-frame driver buffer;
  * "no camera" (will not open / opens but never delivers) is EXIT_NO_CAMERA,
    decided before the model loads, and the real script exits with it fast;
  * timestamps: the packet carries the frame's capture time (device clock when
    plausible, else read time), MediaPipe gets the same instant as strictly
    increasing integer ms (it raises on a repeat);
  * a camera that stops delivering mid-run is EXIT_CAMERA_LOST, not a silent
    socket;
  * HFOV: --hfov reaches the backend, and the default focal length is the
    Kinect's own (285.63 px at 320 wide);
  * the wire packet's frame size is the frame actually delivered, not the size
    requested.

Plain unittest (no pytest needed); registered with ctest as PoseServer.Python.
Run: python3 tests/test_pose_server.py
"""

import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(REPO, "native", "scripts", "pose_server.py")
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))

import pose_server as ps  # noqa: E402  (imports nothing heavy at module level)


class FakeClock:
    def __init__(self, t=1000.0):
        self.t = t

    def __call__(self):
        return self.t

    def sleep(self, s):
        self.t += max(s, 0.0)


class FakeFrame:
    def __init__(self, w, h):
        self.shape = (h, w, 3)


class FakeCv2:
    """The slice of the cv2 API pose_server.py uses, around FakeCapture."""
    CAP_PROP_FRAME_WIDTH = 3
    CAP_PROP_FRAME_HEIGHT = 4
    CAP_PROP_FPS = 5
    CAP_PROP_POS_MSEC = 0
    CAP_PROP_POS_FRAMES = 1
    CAP_PROP_BUFFERSIZE = 38

    def __init__(self, capture):
        self.capture = capture
        self.opened_with = []

    def VideoCapture(self, source):
        self.opened_with.append(source)
        return self.capture


class FakeCapture:
    """A V4L2-like camera. `frames` frames are delivered (None = forever), each
    `period` s apart on `clock`, captured `latency` s before read() returns and
    stamped on the same clock through CAP_PROP_POS_MSEC (`device_ts`)."""

    def __init__(self, clock, opens=True, frames=None, size=(640, 480), period=1 / 30.0,
                 latency=0.02, device_ts=True, real_delay=0.0):
        self.clock, self.opens, self.frames = clock, opens, frames
        self.size, self.period, self.latency = size, period, latency
        self.device_ts, self.real_delay = device_ts, real_delay
        self.sets = []
        self.reads = 0
        self.last_capture = 0.0
        self.captures = []
        self.released = False

    def isOpened(self):
        return self.opens

    def set(self, prop, value):
        self.sets.append((prop, value))
        return True

    def get(self, prop):
        if prop == FakeCv2.CAP_PROP_POS_MSEC:
            return self.last_capture * 1000.0 if self.device_ts else 0.0
        if prop == FakeCv2.CAP_PROP_FPS:
            return 30.0
        return 0.0

    def read(self):
        if self.real_delay:
            time.sleep(self.real_delay)
        self.clock.sleep(self.period)
        if self.frames is not None and self.reads >= self.frames:
            return False, None
        self.reads += 1
        self.last_capture = self.clock() - self.latency
        self.captures.append(self.last_capture)
        return True, FakeFrame(*self.size)

    def release(self):
        self.released = True


class FakeBackend:
    def __init__(self):
        self.timestamps = []

    def process(self, frame, ts_ms):
        self.timestamps.append(ts_ms)
        joints = [(0.01 * j, 1.0, 2.5, 0.9) for j in range(20)]
        return [(7, joints)]


def args_for(**kw):
    a = ps.parse_args(["--socket", kw.pop("socket", "/nonexistent.sock")])
    for k, v in kw.items():
        setattr(a, k, v)
    return a


class CameraRequestTest(unittest.TestCase):
    def test_requests_640x480_at_30fps_with_a_one_frame_buffer(self):
        clock = FakeClock()
        cap = FakeCapture(clock)
        cv2 = FakeCv2(cap)
        got_cap, frame = ps.open_camera(cv2, 2, 640, 480, clock=clock, sleep=clock.sleep)
        self.assertIs(got_cap, cap)
        self.assertEqual(cv2.opened_with, [2])
        self.assertEqual(frame.shape[:2], (480, 640))
        sets = dict(cap.sets)
        self.assertEqual(sets[FakeCv2.CAP_PROP_FRAME_WIDTH], 640)
        self.assertEqual(sets[FakeCv2.CAP_PROP_FRAME_HEIGHT], 480)
        self.assertEqual(sets[FakeCv2.CAP_PROP_FPS], 30.0)
        self.assertEqual(sets[FakeCv2.CAP_PROP_BUFFERSIZE], 1)

    def test_a_camera_that_will_not_open_is_no_camera(self):
        clock = FakeClock()
        with self.assertRaises(ps.ServerExit) as cm:
            ps.open_camera(FakeCv2(FakeCapture(clock, opens=False)), 0, 640, 480,
                           clock=clock, sleep=clock.sleep)
        self.assertEqual(cm.exception.code, ps.EXIT_NO_CAMERA)

    def test_a_camera_that_opens_but_never_delivers_is_no_camera(self):
        clock = FakeClock()
        cap = FakeCapture(clock, frames=0)
        t0 = clock()
        with self.assertRaises(ps.ServerExit) as cm:
            ps.open_camera(FakeCv2(cap), 0, 640, 480, clock=clock, sleep=clock.sleep)
        self.assertEqual(cm.exception.code, ps.EXIT_NO_CAMERA)
        self.assertTrue(cap.released)
        self.assertGreaterEqual(clock() - t0, ps.FIRST_FRAME_TIMEOUT_S)
        self.assertLess(clock() - t0, ps.FIRST_FRAME_TIMEOUT_S + 1.0)


class CaptureClockTest(unittest.TestCase):
    def test_device_timestamp_is_used_when_it_is_on_our_clock(self):
        clock = FakeClock()
        cap = FakeCapture(clock, latency=0.045)
        cap.read()
        stamps = ps.CaptureClock(FakeCv2(cap))
        t, ms, source = stamps.stamp(cap, clock())
        self.assertEqual(source, "device")
        self.assertAlmostEqual(t, clock() - 0.045, places=6)
        self.assertEqual(ms, 0)

    def test_read_time_when_the_device_stamp_is_absent_or_foreign(self):
        clock = FakeClock()
        for kwargs in ({"device_ts": False}, {"latency": 30.0}, {"latency": -1.0}):
            cap = FakeCapture(clock, **kwargs)
            cap.read()
            stamps = ps.CaptureClock(FakeCv2(cap))
            t, _, source = stamps.stamp(cap, clock())
            self.assertEqual(source, "read", kwargs)
            self.assertEqual(t, clock(), kwargs)

    def test_mediapipe_timestamps_strictly_increase_even_when_the_clock_does_not(self):
        clock = FakeClock()
        cap = FakeCapture(clock, device_ts=False)
        stamps = ps.CaptureClock(FakeCv2(cap))
        seen = [stamps.stamp(cap, 5.0)[1] for _ in range(4)]   # same instant x4
        seen.append(stamps.stamp(cap, 5.0004)[1])               # < 1 ms later
        seen.append(stamps.stamp(cap, 5.1)[1])
        self.assertEqual(seen, [0, 1, 2, 3, 4, 100])


class HfovTest(unittest.TestCase):
    def test_hfov_reaches_the_backend_only_when_given(self):
        self.assertNotIn("hfov_deg", ps.backend_kwargs(args_for()))
        self.assertEqual(ps.backend_kwargs(args_for(hfov=70.0))["hfov_deg"], 70.0)

    def test_default_focal_length_is_the_kinects(self):
        try:
            import numpy  # noqa: F401  (pose_mediapipe imports it)
        except ImportError:
            self.skipTest("numpy not installed")
        import math
        from pose_mediapipe import MediaPipeBackend
        b = MediaPipeBackend.__new__(MediaPipeBackend)   # no model load
        b._hfov = math.radians(MediaPipeBackend.DEFAULT_HFOV_DEG)
        # JointUtl.s: u = 160 + 285.63 * x/z over 320 px -> 571.26 px at 640.
        self.assertAlmostEqual(b._focal_px(640), 2 * 285.63, delta=0.05)
        self.assertAlmostEqual(b._focal_px(1280), 4 * 285.63, delta=0.1)
        b._hfov = math.radians(70.0)
        self.assertAlmostEqual(b._focal_px(640), 320 / math.tan(math.radians(35.0)), places=6)


def read_packets(sock, n, timeout=10.0):
    sock.settimeout(timeout)
    buf = b""
    out = []
    while len(out) < n:
        chunk = sock.recv(65536)
        if not chunk:
            break
        buf += chunk
        while len(buf) >= 4:
            (length,) = struct.unpack_from("<I", buf)
            if len(buf) < 4 + length:
                break
            body, buf = buf[4:4 + length], buf[4 + length:]
            hdr = struct.unpack_from("<IIIdHHBBH", body)
            out.append(hdr)
    return out


class ServerLoopTest(unittest.TestCase):
    def test_stream_carries_capture_times_and_actual_size_then_reports_a_lost_camera(self):
        clock = FakeClock()
        # A 16:9 camera that ignored the 640x480 request; delivers 120 frames
        # (a little real delay so the test's client can connect), then stops.
        cap = FakeCapture(clock, frames=120, size=(1280, 720), latency=0.03, real_delay=0.004)
        backend = FakeBackend()
        with tempfile.TemporaryDirectory() as d:
            model = os.path.join(d, "m.task")
            open(model, "w").close()
            sock_path = os.path.join(d, "s.sock")
            a = args_for(socket=sock_path, model=model)
            result = {}

            def serve():
                try:
                    ps.run(a, cv2=FakeCv2(cap), make_backend=lambda _a: (backend, 20, 1),
                           clock=clock, sleep=clock.sleep)
                    result["exit"] = None
                except ps.ServerExit as e:
                    result["exit"] = e.code

            th = threading.Thread(target=serve, daemon=True)
            th.start()
            deadline = time.monotonic() + 10
            while not os.path.exists(sock_path) and time.monotonic() < deadline:
                time.sleep(0.005)
            c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            c.connect(sock_path)
            packets = read_packets(c, 40)
            th.join(20)
            c.close()

        self.assertFalse(th.is_alive(), "server did not exit after the camera stopped")
        self.assertEqual(result.get("exit"), ps.EXIT_CAMERA_LOST)
        self.assertGreaterEqual(len(packets), 40)
        for magic, _fid, persons, _ts, w, h, nl, layout, _pad in packets:
            self.assertEqual(magic, ps.PROTOCOL_MAGIC)
            self.assertEqual((w, h), (1280, 720))
            self.assertEqual((nl, layout, persons), (20, 1, 1))
        ids = [p[1] for p in packets]
        self.assertEqual(ids, list(range(ids[0], ids[0] + len(ids))))
        ts = [p[3] for p in packets]
        self.assertTrue(all(b > a for a, b in zip(ts, ts[1:])))
        # Capture time, not read time: each stamp is exactly a capture instant
        # (read() returned 30 ms later), and frames are a camera period apart.
        captures = set(cap.captures)
        self.assertTrue(all(t in captures for t in ts), "packet stamps are not capture times")
        steps = [b - a for a, b in zip(ts, ts[1:])]
        self.assertAlmostEqual(min(steps), 1 / 30.0, delta=1e-6)
        self.assertAlmostEqual(max(steps), 1 / 30.0, delta=1e-6)
        mp_ts = backend.timestamps
        self.assertTrue(all(isinstance(t, int) for t in mp_ts))
        self.assertTrue(all(b > a for a, b in zip(mp_ts, mp_ts[1:])))


@unittest.skipUnless(sys.platform.startswith("linux"), "camera index probing is Linux-specific")
class RealScriptTest(unittest.TestCase):
    """The actual script, as the game spawns it, on a camera that cannot exist."""

    def run_server(self, *extra):
        with tempfile.TemporaryDirectory() as d:
            model = os.path.join(d, "m.task")
            open(model, "w").close()
            argv = [sys.executable, SERVER, "--socket", os.path.join(d, "s.sock"),
                    "--model", model] + list(extra)
            t0 = time.monotonic()
            p = subprocess.run(argv, capture_output=True, text=True, timeout=60)
            return p, time.monotonic() - t0

    def test_missing_model_is_exit_5_before_anything_else(self):
        p, secs = self.run_server("--model", "/nonexistent/model.task")
        self.assertEqual(p.returncode, ps.EXIT_NO_MODEL, p.stderr[-500:])
        self.assertIn("POSE_SERVER_ERROR exit=5", p.stderr)

    def test_no_camera_is_exit_3_fast(self):
        try:
            import cv2  # noqa: F401
        except ImportError:
            self.skipTest("cv2 not installed for this python")
        p, secs = self.run_server("--camera", "63")
        self.assertEqual(p.returncode, ps.EXIT_NO_CAMERA, p.stderr[-800:])
        self.assertIn("POSE_SERVER_ERROR exit=3", p.stderr)
        self.assertLess(secs, ps.FIRST_FRAME_TIMEOUT_S + 3.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
