"""The camera video recorder of native/scripts/pose_server.py (--record-video,
native/scripts/video_recorder.py).  Synthetic frames only: nothing here may
ever read a real session's video (it is the owner's room).

Run: python3 -m pytest tests/test_video_recorder.py
"""
import os
import shutil
import sys
import tempfile
import threading
import time
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))

import numpy as np  # noqa: E402
import cv2  # noqa: E402
import video_recorder as vr  # noqa: E402
import pose_server as ps  # noqa: E402


def scratch(test):
    d = tempfile.mkdtemp(prefix="video_rec_")
    test.addCleanup(shutil.rmtree, d, True)
    return d


def frame(k, w=320, h=240):
    """A synthetic frame whose bright bar's position says which one it is."""
    f = np.full((h, w, 3), 40, np.uint8)
    x = (k * 7) % (w - 6)
    f[:, x:x + 6] = 230
    return f


def which(f):
    """The k of frame(k) (k < w / 7)."""
    cols = f.mean(axis=(0, 2))
    return int(round((int(np.argmax(cols > 135)) / 7.0)))


class Logs(list):
    def __call__(self, msg):
        self.append(msg)


class BlockingWriter:
    """A VideoWriter whose write() waits for `gate`: the encoder is stuck."""

    def __init__(self, gate, written):
        self.gate, self.written = gate, written

    def isOpened(self):  # noqa: N802
        return True

    def write(self, f):
        self.gate.wait(10)
        self.written.append(f)

    def release(self):
        pass


class FakeCv2:
    def __init__(self, gate=None):
        self.gate = gate or threading.Event()
        if gate is None:
            self.gate.set()
        self.written = []

    def VideoWriter_fourcc(self, *c):  # noqa: N802
        return 0

    def VideoWriter(self, path, fourcc, fps, size):  # noqa: N802
        open(path, "wb").close()
        return BlockingWriter(self.gate, self.written)


class RecorderTest(unittest.TestCase):
    def test_video_frame_k_is_sidecar_line_k_with_the_packets_id_and_timestamp(self):
        d = scratch(self)
        path = os.path.join(d, "camera.mkv")
        logs = Logs()
        rec = vr.VideoRecorder(path, cv2, fps=30, log=logs)
        ids = [7 + 2 * k for k in range(40)]           # the packet ids need not be consecutive
        stamps = [81234.5 + k / 30.0 + 1e-9 * k for k in range(40)]
        for k in range(40):
            self.assertTrue(rec.offer(frame(k), ids[k], stamps[k], k * 33.0))
            time.sleep(0.002)
        rec.close(timeout_s=5)
        _, rows, counts = vr.read_sidecar(vr.sidecar_path(path))
        self.assertEqual([r[0] for r in rows], list(range(40)))
        self.assertEqual([r[1] for r in rows], ids)
        self.assertEqual([r[2] for r in rows], stamps)    # exactly: they join pose.dc3session's ts
        self.assertEqual(counts["stopped"], "closed")
        self.assertEqual(counts["written"], 40)
        self.assertEqual(counts["dropped"], 0)
        cap = cv2.VideoCapture(path)
        levels = []
        while True:
            ok, f = cap.read()
            if not ok:
                break
            levels.append(which(f))
        self.assertEqual(levels, list(range(40)))
        self.assertTrue(any("recording the camera frames to" in m for m in logs))
        self.assertTrue(any(m.startswith(f"stopped {path}: 40 frames written, 0 dropped") for m in logs))

    def test_a_stuck_writer_never_blocks_the_tracker_and_every_drop_is_counted(self):
        d = scratch(self)
        fake = FakeCv2(gate=threading.Event())
        rec = vr.VideoRecorder(os.path.join(d, "camera.mkv"), fake, queue_frames=4, log=Logs())
        worst = 0.0
        for k in range(200):
            t = time.perf_counter()
            rec.offer(frame(k), k, float(k), float(k))
            worst = max(worst, time.perf_counter() - t)
        self.assertLess(worst, 0.05, "offer() waited for the writer")
        fake.gate.set()
        rec.close(timeout_s=5)
        self.assertEqual(rec.offered, 200)
        self.assertGreater(rec.dropped, 150)
        self.assertEqual(rec.written + rec.dropped, 200)
        _, rows, counts = vr.read_sidecar(vr.sidecar_path(os.path.join(d, "camera.mkv")))
        self.assertEqual(len(rows), rec.written)
        self.assertEqual(counts["dropped"], rec.dropped)
        # the frames written are the ones that were queued, in order
        self.assertEqual([r[1] for r in rows], sorted(r[1] for r in rows))

    def test_the_size_cap_stops_recording_once_and_says_so(self):
        d = scratch(self)
        logs = Logs()
        rec = vr.VideoRecorder(os.path.join(d, "camera.mkv"), FakeCv2(), max_bytes=1000,
                               getsize=lambda p: 5000, log=logs)
        for k in range(100):
            rec.offer(frame(k), k, float(k), float(k))
            time.sleep(0.001)
        rec.close(timeout_s=5)
        self.assertEqual(rec.written, vr.STATUS_EVERY)
        self.assertIn("size cap", rec.stopped)
        self.assertEqual(rec.written + rec.dropped + rec.after_stop, 100)
        stops = [m for m in logs if m.startswith("stopped")]
        self.assertEqual(len(stops), 1, logs)
        _, rows, counts = vr.read_sidecar(vr.sidecar_path(os.path.join(d, "camera.mkv")))
        self.assertEqual(len(rows), vr.STATUS_EVERY)
        self.assertTrue(counts["stopped"].startswith("size_cap"))

    def test_low_disk_stops_recording(self):
        d = scratch(self)

        class St:
            f_bavail, f_frsize = 1, 4096
        rec = vr.VideoRecorder(os.path.join(d, "camera.mkv"), FakeCv2(), statvfs=lambda p: St(),
                               log=Logs())
        for k in range(vr.FREE_CHECK_EVERY + 20):
            rec.offer(frame(k), k, float(k), float(k))
            while rec.queue.qsize() > 30:
                time.sleep(0.001)
        rec.close(timeout_s=5)
        self.assertEqual(rec.written, vr.FREE_CHECK_EVERY)
        self.assertIn("disk free", rec.stopped)

    def test_an_existing_file_is_never_overwritten_and_a_new_size_is_a_new_part(self):
        d = scratch(self)
        path = os.path.join(d, "camera.mkv")
        with open(path, "w") as f:
            f.write("an earlier server's recording")
        rec = vr.VideoRecorder(path, FakeCv2(), log=Logs())
        for k in range(5):
            rec.offer(frame(k), k, float(k), 0.0)
        for k in range(5, 8):
            rec.offer(frame(k, 640, 480), k, float(k), 0.0)
        rec.close(timeout_s=5)
        with open(path) as f:
            self.assertEqual(f.read(), "an earlier server's recording")
        self.assertEqual([os.path.basename(p["path"]) for p in rec.parts],
                         ["camera-2.mkv", "camera-3.mkv"])
        self.assertEqual([p["written"] for p in rec.parts], [5, 3])
        self.assertEqual(vr.parts_of(path), [path, os.path.join(d, "camera-2.mkv"),
                                             os.path.join(d, "camera-3.mkv")])


class ServerRecordsWhatItFeedsMediaPipeTest(unittest.TestCase):
    def test_every_frame_given_to_the_backend_is_in_the_video_with_its_packet_stamp(self):
        d = scratch(self)
        clip = os.path.join(d, "clip.avi")
        w = cv2.VideoWriter(clip, cv2.VideoWriter_fourcc(*"MJPG"), 30, (160, 120))
        for k in range(12):
            w.write(frame(k, 160, 120))
        w.release()
        model = os.path.join(d, "m.task")
        open(model, "w").close()
        seen = []

        class Backend:
            def process(self, f, ts_ms):
                seen.append((which(f), ts_ms))
                return []

        out = os.path.join(d, "session", "camera.mkv")
        args = ps.parse_args(["--video", clip, "--model", model, "--socket", os.path.join(d, "s.sock"),
                              "--record-video", out])
        ps.run(args, make_backend=lambda a: (Backend(), 20, 1))
        _, rows, counts = vr.read_sidecar(vr.sidecar_path(out))
        self.assertEqual(len(rows), len(seen))
        self.assertEqual(len(rows), 12)
        self.assertEqual([r[3] for r in rows], [s[1] for s in seen])   # MediaPipe's own timestamps
        self.assertEqual([r[1] for r in rows], list(range(12)))
        self.assertEqual(counts["stopped"], "closed")
        cap = cv2.VideoCapture(out)
        n = 0
        while cap.read()[0]:
            n += 1
        self.assertEqual(n, 12)

    def test_no_flag_no_recorder(self):
        args = ps.parse_args(["--video", "x.mp4"])
        self.assertIsNone(ps.make_recorder(args, cv2, 30))


if __name__ == "__main__":
    unittest.main()
