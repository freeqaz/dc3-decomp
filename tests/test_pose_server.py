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

    def VideoCapture(self, source, *api):
        self.opened_with.append(source)
        return self.capture


class FakeDevices:
    """V4L2Devices' interface over a dict {node: (is_capture, card)} plus
    by-id symlinks {name: node}; `replace` swaps the set, as a USB camera
    re-enumerating under new node numbers does."""
    by_id_dir = "/dev/v4l/by-id"

    def __init__(self, nodes=None, by_id=None, available=True):
        self.replace(nodes or {}, by_id or {})
        self._available = available

    def replace(self, nodes, by_id=None):
        self._nodes = dict(nodes)
        self._by_id = dict(by_id or {})

    def available(self):
        return self._available

    def nodes(self):
        return sorted(self._nodes, key=lambda n: int(n[len("/dev/video"):]))

    def query(self, path):
        if path not in self._nodes:
            return False, "cannot open (No such device)"
        ok, card = self._nodes[path]
        return (True, card) if ok else (False, f"{card}: no video capture")

    def realpath(self, path):
        name = os.path.basename(path)
        if path.startswith(self.by_id_dir + "/") and name in self._by_id:
            return self._by_id[name]
        return path

    def exists(self, path):
        if path.startswith(self.by_id_dir + "/"):
            return os.path.basename(path) in self._by_id
        return path in self._nodes


GPD_BEFORE = {"/dev/video0": (True, "Integrated Camera"), "/dev/video1": (False, "Integrated Camera")}
GPD_AFTER = {"/dev/video1": (True, "Integrated Camera"), "/dev/video2": (False, "Integrated Camera")}
BY_ID = "usb-SunplusIT_Inc_Integrated_Camera_JX20250923V2-video-index0"


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
                           clock=clock, sleep=clock.sleep,
                           devices=FakeDevices({"/dev/video0": (True, "Fake")}))
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


class ResolveCameraTest(unittest.TestCase):
    """--camera -> device, on the GPD's two layouts (its integrated camera
    re-enumerates: video0 + metadata video1, then video1 + metadata video2)."""

    def resolve(self, spec, devices):
        logs = []
        source, desc = ps.resolve_camera(spec, devices, log=logs.append)
        return source, desc, logs

    def test_auto_takes_the_first_capture_node_and_skips_metadata(self):
        src, desc, _ = self.resolve("auto", FakeDevices(GPD_BEFORE))
        self.assertEqual(src, "/dev/video0")
        meta_first = {"/dev/video1": (False, "Integrated Camera"),
                      "/dev/video2": (True, "Integrated Camera")}
        src, desc, _ = self.resolve("auto", FakeDevices(meta_first))
        self.assertEqual(src, "/dev/video2")
        self.assertIn("skipped /dev/video1", desc)
        self.assertEqual(self.resolve(None, FakeDevices(GPD_AFTER))[0], "/dev/video1")

    def test_the_default_is_auto(self):
        self.assertEqual(ps.parse_args([]).camera, "auto")

    def test_a_missing_index_falls_back_to_the_scan_and_says_so(self):
        src, desc, logs = self.resolve("0", FakeDevices(GPD_AFTER))
        self.assertEqual(src, "/dev/video1")
        self.assertTrue(any("/dev/video0 does not exist" in l for l in logs), logs)

    def test_an_index_that_is_not_a_camera_falls_back_to_the_scan(self):
        src, _, logs = self.resolve("2", FakeDevices(GPD_AFTER))
        self.assertEqual(src, "/dev/video1")
        self.assertTrue(any("not a camera" in l for l in logs), logs)

    def test_a_present_index_is_used(self):
        self.assertEqual(self.resolve("1", FakeDevices(GPD_AFTER))[0], "/dev/video1")

    def test_by_id_names_and_paths_follow_the_device(self):
        for nodes, node in ((GPD_BEFORE, "/dev/video0"), (GPD_AFTER, "/dev/video1")):
            devs = FakeDevices(nodes, {BY_ID: node})
            self.assertEqual(self.resolve(BY_ID, devs)[0], node)
            self.assertEqual(self.resolve("/dev/v4l/by-id/" + BY_ID, devs)[0], node)
        self.assertEqual(self.resolve("/dev/video1", FakeDevices(GPD_AFTER))[0], "/dev/video1")

    def test_nothing_to_open_is_no_camera(self):
        for spec, devs in (("auto", FakeDevices({})),
                           ("auto", FakeDevices({"/dev/video3": (False, "meta")})),
                           ("/dev/video7", FakeDevices(GPD_AFTER)),
                           (BY_ID, FakeDevices(GPD_AFTER)),
                           ("/dev/video2", FakeDevices(GPD_AFTER))):
            with self.assertRaises(ps.ServerExit, msg=spec) as cm:
                ps.resolve_camera(spec, devs, log=lambda _m: None)
            self.assertEqual(cm.exception.code, ps.EXIT_NO_CAMERA)

    def test_without_v4l2_an_index_goes_to_opencv(self):
        devs = FakeDevices(available=False)
        self.assertEqual(ps.resolve_camera("auto", devs)[0], 0)
        self.assertEqual(ps.resolve_camera("2", devs)[0], 2)

    def test_a_path_opens_through_v4l2(self):
        clock = FakeClock()
        cv2 = FakeCv2(FakeCapture(clock))
        cv2.CAP_V4L2 = 200
        calls = []
        orig = cv2.VideoCapture
        cv2.VideoCapture = lambda *a: (calls.append(a), orig(*a))[1]
        ps.open_camera(cv2, "/dev/video1", 640, 480, clock=clock, sleep=clock.sleep)
        ps.open_camera(cv2, 3, 640, 480, clock=clock, sleep=clock.sleep)
        self.assertEqual(calls, [("/dev/video1", 200), (3,)])

    @unittest.skipUnless(sys.platform.startswith("linux"), "V4L2 is Linux")
    def test_the_real_query_reads_this_boxs_nodes(self):
        devs = ps.V4L2Devices()
        nodes = devs.nodes()
        for n in nodes:
            ok, desc = devs.query(n)
            self.assertIsInstance(ok, bool)
            self.assertTrue(desc)
        if not nodes:
            with self.assertRaises(ps.ServerExit):
                ps.resolve_camera("auto", devs, log=lambda _m: None)


class SwitchingCv2(FakeCv2):
    """A cv2 whose VideoCapture hands out a capture per device path; any other
    path opens nothing (the node is gone)."""

    def __init__(self, captures):
        super().__init__(None)
        self.captures = captures

    def VideoCapture(self, source, *api):
        self.opened_with.append(source)
        cap = self.captures.get(source)
        return cap if cap is not None else FakeCapture(FakeClock(), opens=False)


class CameraLossTest(unittest.TestCase):
    """A camera that vanishes mid-stream and comes back under a new node (the
    GPD's re-enumeration): the server keeps the socket, flags FLAG_NO_CAMERA
    packets while it is gone, finds the camera again, and only exits 7 when it
    stays gone past the budget."""

    def serve(self, cv2, devices, clock, n_packets, camera="auto"):
        backend = FakeBackend()
        result = {}
        with tempfile.TemporaryDirectory() as d:
            model = os.path.join(d, "m.task")
            open(model, "w").close()
            sock_path = os.path.join(d, "s.sock")
            a = args_for(socket=sock_path, model=model, camera=camera)

            def run():
                try:
                    ps.run(a, cv2=cv2, make_backend=lambda _a: (backend, 20, 1),
                           clock=clock, sleep=clock.sleep, devices=devices)
                    result["exit"] = None
                except ps.ServerExit as e:
                    result["exit"] = e.code
                    result["msg"] = str(e)

            th = threading.Thread(target=run, daemon=True)
            th.start()
            deadline = time.monotonic() + 10
            while not os.path.exists(sock_path) and time.monotonic() < deadline:
                time.sleep(0.005)
            c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            c.connect(sock_path)
            packets = read_packets(c, n_packets, timeout=20)
            th.join(30)
            c.close()
        self.assertFalse(th.is_alive(), "server still running")
        return packets, result

    def test_a_re_enumerated_camera_is_found_again_under_its_new_node(self):
        clock = FakeClock()
        devices = FakeDevices(GPD_BEFORE)

        class Vanishing(FakeCapture):
            def read(self):
                ok, frame = super().read()
                if not ok:
                    # Mid-stream: the device goes away and comes back renumbered.
                    devices.replace(GPD_AFTER)
                return ok, frame

        first = Vanishing(clock, frames=60, real_delay=0.004)
        second = FakeCapture(clock, frames=60, real_delay=0.004)
        cv2 = SwitchingCv2({"/dev/video0": first, "/dev/video1": second})
        packets, result = self.serve(cv2, devices, clock, 10 ** 6)

        flags = [p[8] for p in packets]
        persons = [p[2] for p in packets]
        lost = [i for i, f in enumerate(flags) if f & ps.FLAG_NO_CAMERA]
        self.assertTrue(lost, "no FLAG_NO_CAMERA packet while the camera was gone")
        self.assertTrue(all(persons[i] == 0 for i in lost))
        self.assertTrue(all(f & ps.FLAG_UNLEVELLED for f in flags))
        # Frames before the loss, flagged packets during it, frames after it
        # (then a second loss: the renumbered camera runs out too).
        self.assertGreater(lost[0], 0)
        end = lost[0]
        while end < len(packets) and flags[end] & ps.FLAG_NO_CAMERA:
            end += 1
        resumed = [i for i in range(end, len(packets)) if persons[i] == 1]
        self.assertGreaterEqual(len(resumed), 50, "tracking did not resume after the camera came back")
        self.assertEqual(second.reads, 60, "the renumbered camera was not read")
        self.assertEqual(cv2.opened_with[0], "/dev/video0")
        self.assertIn("/dev/video1", cv2.opened_with)
        ids = [p[1] for p in packets]
        self.assertEqual(ids, list(range(ids[0], ids[0] + len(ids))))
        # The second camera then stops too and stays gone: bounded, exit 7.
        self.assertEqual(result.get("exit"), ps.EXIT_CAMERA_LOST, result)

    def test_a_camera_that_never_comes_back_is_exit_7_after_the_budget(self):
        clock = FakeClock()

        class Counting(FakeDevices):
            scans = 0

            def nodes(self):
                Counting.scans += 1
                return super().nodes()
        devices = Counting(GPD_BEFORE)
        cap = FakeCapture(clock, frames=30, real_delay=0.004)
        cv2 = SwitchingCv2({"/dev/video0": cap})

        def vanish():
            devices.replace({})
        orig_read = cap.read

        def read():
            ok, f = orig_read()
            if not ok:
                vanish()
            return ok, f
        cap.read = read
        packets, result = self.serve(cv2, devices, clock, 10 ** 6)
        self.assertEqual(result.get("exit"), ps.EXIT_CAMERA_LOST, result)
        lost = [p for p in packets if p[8] & ps.FLAG_NO_CAMERA]
        self.assertTrue(lost)
        span = lost[-1][3] - lost[0][3]
        self.assertGreaterEqual(span, ps.CAMERA_REOPEN_BUDGET_S - 5)
        self.assertLess(span, ps.CAMERA_REOPEN_BUDGET_S + 10)
        # Retries back off: a rescan per try, far fewer than heartbeats
        # (0.5, 1, 2, then every 4 s over a 60 s budget is ~17).
        self.assertLess(Counting.scans, 25)
        self.assertGreater(Counting.scans, 10)
        self.assertGreater(len(lost), 100)


def _have_cv2_numpy():
    try:
        import cv2  # noqa: F401
        import numpy  # noqa: F401
        return True
    except ImportError:
        return False


def read_raw_packets(sock, n, timeout=10.0):
    """Whole packet bodies (after the length prefix)."""
    sock.settimeout(timeout)
    buf, out = b"", []
    while len(out) < n:
        chunk = sock.recv(1 << 20)
        if not chunk:
            break
        buf += chunk
        while len(buf) >= 4:
            (length,) = struct.unpack_from("<I", buf)
            if len(buf) < 4 + length:
                break
            out.append(buf[4:4 + length])
            buf = buf[4 + length:]
    return out


def split_v3(body):
    """(header, persons, camera block) of a v3 body."""
    hdr = struct.unpack_from("<IIIdHHBBH", body)
    off = struct.calcsize("<IIIdHHBBH")
    persons = []
    for _ in range(hdr[2]):
        (tid,) = struct.unpack_from("<i", body, off)
        off += 4 + hdr[6] * 16
        persons.append(tid)
    w, h, rf, mf, _pad = struct.unpack_from("<HHBBH", body, off)
    off += 8
    rgb = body[off:off + w * h * 3]
    mask = body[off + w * h * 3:off + w * h * 4]
    return hdr, persons, (w, h, rf, mf, rgb, mask, len(body) - off - w * h * 4)


class CameraFramesTest(unittest.TestCase):
    """Protocol v3 (--frames, PLAN P-4): the camera picture + person mask."""

    def test_frames_flag_parses_and_rejects_nonsense(self):
        self.assertEqual(ps.parse_args(["--frames", "320x240"]).frames_size, (320, 240))
        self.assertIsNone(ps.parse_args([]).frames_size)
        with self.assertRaises(SystemExit):
            ps.parse_args(["--frames", "big"])

    def test_without_frames_the_packet_is_v2_byte_for_byte(self):
        persons = [(7, [(0.1, 0.2, 2.5, 0.9)] * 20)]
        v2 = ps.pack_frame(3, 1.5, 640, 480, 20, 1, persons)
        self.assertEqual(struct.unpack_from("<I", v2, 4)[0], ps.PROTOCOL_MAGIC)
        self.assertEqual(len(v2), 4 + 28 + 4 + 20 * 16)

    def test_every_packet_says_the_camera_does_not_know_its_gravity(self):
        # Header flags bit 0: the game estimates a webcam's tilt and levels
        # it (PoseLevel_Native.h); a 0 there reads as a level Kinect.
        persons = [(7, [(0.1, 0.2, 2.5, 0.9)] * 20)]
        for image in (None, (), (bytes(12), bytes(4), 2, 2)):
            body = ps.pack_frame(3, 1.5, 640, 480, 20, 1, persons, image)[4:]
            flags = struct.unpack_from("<IIIdHHBBH", body)[-1]
            self.assertEqual(flags & ps.FLAG_UNLEVELLED, 1)

    def test_v3_appends_the_block_and_an_empty_block_means_no_picture(self):
        persons = [(7, [(0.1, 0.2, 2.5, 0.9)] * 20)]
        rgb, mask = bytes(range(6)) * 2, bytes([0, 1, 1, 0])
        body = ps.pack_frame(3, 1.5, 640, 480, 20, 1, persons, (rgb, mask, 2, 2))[4:]
        hdr, tids, (w, h, rf, mf, prgb, pmask, rest) = split_v3(body)
        self.assertEqual(hdr[0], ps.PROTOCOL_MAGIC_V3)
        self.assertEqual(tids, [7])
        self.assertEqual((w, h, rf, mf, prgb, pmask, rest), (2, 2, 1, 1, rgb, mask, 0))
        empty = ps.pack_frame(3, 1.5, 640, 480, 20, 1, [], ())[4:]
        _, _, (w, h, _, _, _, _, rest) = split_v3(empty)
        self.assertEqual((w, h, rest), (0, 0, 0))

    @unittest.skipUnless(_have_cv2_numpy(), "needs cv2 + numpy")
    def test_mask_names_the_most_confident_person_above_threshold(self):
        import cv2
        import numpy as np
        frame = np.zeros((480, 640, 3), dtype=np.uint8)
        frame[:, :, 2] = 200                       # BGR red
        a = np.zeros((480, 640), np.float32)
        b = np.zeros((480, 640), np.float32)
        a[:, :320] = 0.9                            # person 1: left half
        b[:, 200:] = 0.7                            # person 2: right part
        b[:, 200:320] = 0.95                        # ... and wins the overlap
        a[:240, 500:] = 0.4                         # below threshold: ignored
        rgb, mask, w, h = ps.camera_block(frame, [a, b], (320, 240), cv2, np)
        self.assertEqual((w, h, len(rgb), len(mask)), (320, 240, 320 * 240 * 3, 320 * 240))
        m = np.frombuffer(mask, np.uint8).reshape(240, 320)
        self.assertEqual(m[120, 10], 1)
        self.assertEqual(m[120, 130], 2)            # overlap -> higher confidence
        self.assertEqual(m[120, 300], 2)
        self.assertEqual(m[60, 290], 2)             # 0.4 never beats threshold or b
        px = np.frombuffer(rgb, np.uint8).reshape(240, 320, 3)
        self.assertEqual(tuple(px[10, 10]), (200, 0, 0))   # RGB order on the wire
        # A missing mask (None) is nobody, not a crash.
        _, mask2, _, _ = ps.camera_block(frame, [None, b], (320, 240), cv2, np)
        self.assertNotIn(1, set(mask2))

    def test_outbox_never_interleaves_a_partial_packet(self):
        class Trickle:
            """A socket that takes 1000 bytes per send, then would block."""
            def __init__(self):
                self.data = b""
                self.budget = 0

            def send(self, b):
                if self.budget <= 0:
                    raise BlockingIOError()
                n = min(len(b), self.budget, 1000)
                self.budget -= n
                self.data += b[:n]
                return n

        sock = Trickle()
        box = ps.Outbox(sock)
        p1 = struct.pack("<I", 2500) + b"a" * 2500
        p2 = struct.pack("<I", 10) + b"b" * 10
        sock.budget = 1000
        self.assertTrue(box.offer(p1))          # queued, 1000 bytes out
        self.assertFalse(box.offer(p2))         # still in flight: dropped whole
        sock.budget = 10**6
        self.assertTrue(box.offer(p2))          # rest of p1, then all of p2
        self.assertEqual(sock.data, p1 + p2)
        self.assertEqual((box.sent_packets, box.dropped_packets), (2, 1))

    @unittest.skipUnless(_have_cv2_numpy(), "needs cv2 + numpy")
    def test_stream_carries_picture_and_mask_for_the_packets_persons(self):
        import numpy as np
        clock = FakeClock()

        class RealFrameCapture(FakeCapture):
            def read(self):
                ok, _ = super().read()
                if not ok:
                    return False, None
                f = np.zeros((480, 640, 3), np.uint8)
                f[:, :, 1] = 150                       # green
                return True, f

        class MaskBackend(FakeBackend):
            def process(self, frame, ts_ms):
                out = super().process(frame, ts_ms)
                m = np.zeros(frame.shape[:2], np.float32)
                m[100:400, 200:440] = 1.0
                self.last_masks = [m]
                return out

        cap = RealFrameCapture(clock, frames=20, real_delay=0.004)
        with tempfile.TemporaryDirectory() as d:
            model = os.path.join(d, "m.task")
            open(model, "w").close()
            sock_path = os.path.join(d, "s.sock")
            a = args_for(socket=sock_path, model=model)
            a.frames, a.frames_size = "160x120", (160, 120)
            th = threading.Thread(target=lambda: self._serve(a, cap, clock, MaskBackend()),
                                  daemon=True)
            th.start()
            deadline = time.monotonic() + 10
            while not os.path.exists(sock_path) and time.monotonic() < deadline:
                time.sleep(0.005)
            c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            c.connect(sock_path)
            bodies = read_raw_packets(c, 5)
            c.close()
            th.join(20)
        self.assertGreaterEqual(len(bodies), 5)
        for body in bodies:
            hdr, tids, (w, h, rf, mf, rgb, mask, rest) = split_v3(body)
            self.assertEqual((hdr[0], tids, w, h, rest), (ps.PROTOCOL_MAGIC_V3, [7], 160, 120, 0))
            m = np.frombuffer(mask, np.uint8).reshape(120, 160)
            self.assertEqual(m[60, 80], 1)          # inside the person
            self.assertEqual(m[5, 5], 0)            # background
            self.assertEqual(tuple(np.frombuffer(rgb, np.uint8)[:3]), (0, 150, 0))

    @staticmethod
    def _serve(a, cap, clock, backend):
        try:
            ps.run(a, cv2=_RealCv2WithCapture(cap), make_backend=lambda _a: (backend, 20, 1),
                   devices=FakeDevices({"/dev/video0": (True, "Fake")}),
                   clock=clock, sleep=clock.sleep)
        except ps.ServerExit:
            pass


class _RealCv2WithCapture:
    """Real cv2 (resize / cvtColor for the camera block) with a fake camera."""

    def __init__(self, capture):
        import cv2
        self._cv2 = cv2
        self._capture = capture

    def VideoCapture(self, source, *api):
        return self._capture

    def __getattr__(self, name):
        return getattr(self._cv2, name)


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
        # A path, not an index: an index whose node is missing now falls
        # back to the auto scan, which finds a real camera on a box that has one.
        p, secs = self.run_server("--camera", "/dev/video-np-pose-no-such-node")
        self.assertEqual(p.returncode, ps.EXIT_NO_CAMERA, p.stderr[-800:])
        self.assertIn("POSE_SERVER_ERROR exit=3", p.stderr)
        self.assertLess(secs, ps.FIRST_FRAME_TIMEOUT_S + 3.0)


class NoAudioImportTest(unittest.TestCase):
    """mediapipe 1.0.0 imports sounddevice eagerly; on a PipeWire + rtkit box
    that made Python's main thread SCHED_FIFO under an RLIMIT_RTTIME budget,
    and the kernel SIGKILLed the server during the model load (the GPD
    playtest box, exit 137, every run). MediaPipeBackend must import
    mediapipe without the sound system (pose_mediapipe.block_audio_import)."""

    PROBE = (
        "import sys; sys.path.insert(0, %r)\n"
        "import pose_mediapipe as pm\n"
        "%s\n"
        "try:\n"
        "    pm.MediaPipeBackend('/nonexistent/model.task')\n"
        "except Exception:\n"
        "    pass\n"
        "import mediapipe\n"
        "print('SD', type(sys.modules.get('sounddevice')).__name__)\n")

    def probe(self, sabotage=""):
        code = self.PROBE % (os.path.join(REPO, "native", "scripts"), sabotage)
        p = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, timeout=120)
        line = [l for l in p.stdout.splitlines() if l.startswith("SD ")]
        self.assertTrue(line, p.stderr[-800:])
        return line[0].split()[1]

    def test_constructing_the_backend_does_not_import_sounddevice(self):
        try:
            import importlib.util
            if importlib.util.find_spec("mediapipe") is None:
                self.skipTest("mediapipe not installed for this python")
            have_sd = importlib.util.find_spec("sounddevice") is not None
        except ImportError:
            self.skipTest("mediapipe not importable")
        self.assertEqual(self.probe(), "NoneType")   # blocked, never loaded
        if have_sd:
            # Control: with the guard disabled the same probe loads it, so the
            # assertion above is not vacuous on this python.
            self.assertEqual(self.probe("pm.block_audio_import = lambda: None"), "module")


if __name__ == "__main__":
    unittest.main(verbosity=2)
