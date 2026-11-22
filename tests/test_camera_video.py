"""A playtest's camera video after the session: the summary line, the cut onto
the song's time line, offline re-tracking (scripts/playtest/camera_video.py),
and scripts/play.sh's per-machine settings file that turns recording on.
Synthetic sessions and frames only -- never a real session's video.

Run: python3 -m pytest tests/test_camera_video.py
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))
sys.path.insert(0, os.path.join(REPO, "scripts", "playtest"))

import numpy as np  # noqa: E402
import cv2  # noqa: E402
import video_recorder as vr  # noqa: E402
import camera_video  # noqa: E402
import session_summary  # noqa: E402

PLAY = os.path.join(REPO, "scripts", "play.sh")
FPS = 30.0
T_CAP0 = 1000.0     # monotonic capture time of video frame 0
STEADY_T0 = 900.0   # the session's wall-column zero on the same clock
SONG0 = -5.0 + 0.5 / FPS   # song time at the capture of video frame 0 (half a frame off the clip grid: no ties)
ID0 = 100           # packet id of video frame 0


def scratch(test):
    d = tempfile.mkdtemp(prefix="camera_video_")
    test.addCleanup(shutil.rmtree, d, True)
    return d


def frame(k, w=160, h=120):
    f = np.full((h, w, 3), 40, np.uint8)
    x = (k % 20) * 7
    f[:, x:x + 6] = 230
    return f


def which(f):
    return int(round(int(np.argmax(f.mean(axis=(0, 2)) > 135)) / 7.0))


def delay(k):
    """How long the game took to accept frame k: not constant."""
    return 0.03 + 0.02 * (k % 3)


def make_session(d, n=900, taken=lambda k: k % 2 == 0, t0_line=True):
    """A session with camera.mkv (n frames at 30 fps from song time -5 s) and
    a pose.dc3session in which the game took the frames `taken` says."""
    rec = vr.VideoRecorder(os.path.join(d, "camera.mkv"), cv2, fps=FPS, log=lambda m: None)
    for k in range(n):
        rec.offer(frame(k), ID0 + k, T_CAP0 + k / FPS, k * 1000.0 / FPS)
        while rec.queue.qsize() > 20:
            time.sleep(0.001)
    rec.close(timeout_s=10)
    lines = ["# dc3-pose-session v1", "# started x pid 1"]
    if t0_line:
        lines.append(f"# steady_t0 {STEADY_T0:.9f}")
    seq = 0
    for k in range(n):
        if not taken(k):
            continue
        seq += 1
        ts = T_CAP0 + k / FPS
        wall = ts + delay(k) - STEADY_T0
        song = SONG0 + k / FPS + delay(k)
        lines.append(f"F {seq} {seq} {wall:.6f} {song:.6f} {ID0 + k} {ts!r} 1 1 0 1 0 1")
        lines.append("P 3 1")
    with open(os.path.join(d, "pose.dc3session"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return rec


class Args:
    def __init__(self, **kw):
        self.__dict__.update(kw)


class CutTest(unittest.TestCase):
    def check_cut(self, t0_line):
        d = scratch(self)
        make_session(d, t0_line=t0_line)
        out = os.path.join(d, "song.mkv")
        rc = camera_video.cmd_cut(Args(session=d, n=1, out=out, pre=8.0, post=1.0, fps=FPS,
                                       codec="mp4v"))
        self.assertEqual(rc, 0)
        rows = []
        with open(os.path.join(d, "song.cut.tsv")) as f:
            for line in f:
                if not line.startswith("#"):
                    k, song, fid, ts = line.split()
                    rows.append((int(k), float(song), int(fid)))
        song_to = SONG0 + 898 / FPS + delay(898)          # the last frame the game took
        self.assertEqual(len(rows), int(math.floor((song_to + 1.0 + 8.0) * FPS)) + 1)
        for k, song, fid in rows:
            self.assertAlmostEqual(song, k / FPS - 8.0, places=3)
            # the latest recorded frame captured at or before that song time
            # (the session's %.6f columns move a frame that lands exactly on a
            # clip time by a microsecond either way: a tie, not an error)
            x = (k / FPS - 8.0 - SONG0) * FPS
            allowed = {math.floor(x)} if abs(x - round(x)) > 1e-3 else {round(x), round(x) - 1}
            allowed = {min(899, max(0, j)) for j in allowed}
            self.assertIn(fid - ID0, allowed, f"clip frame {k} (song {song:.3f})")
        cap = cv2.VideoCapture(out)
        for k, _, fid in rows[:400]:
            ok, f = cap.read()
            self.assertTrue(ok)
            self.assertEqual(which(f), (fid - ID0) % 20, f"clip frame {k}")

    def test_a_clip_frame_is_the_frame_captured_at_its_song_time(self):
        self.check_cut(t0_line=True)

    def test_without_steady_t0_the_quickest_frame_is_the_zero(self):
        # delay() is 30..70 ms; without the header line the quickest (30 ms)
        # is taken as zero, so frames move up to 40 ms: at most one 30 fps frame.
        d = scratch(self)
        make_session(d, t0_line=False)
        tl = camera_video.Timeline(d)
        _, rows, how = tl.segment(1)
        self.assertIn("no steady_t0", how)
        for r in rows:
            k = r.frame_id - ID0
            self.assertLessEqual(abs(r.song - (SONG0 + k / FPS)), 0.0401 + 0.03)

    def test_with_steady_t0_every_frame_is_at_its_capture_song_time(self):
        d = scratch(self)
        make_session(d)
        tl = camera_video.Timeline(d)
        _, rows, how = tl.segment(1)
        self.assertEqual(how, "steady_t0")
        self.assertEqual(len(rows), 899)            # frame 0 .. 898, taken or not
        for r in rows:
            k = r.frame_id - ID0
            self.assertAlmostEqual(r.song, SONG0 + k / FPS, places=4)
            self.assertEqual(r.live_valid, 1 if k % 2 == 0 else -1)


class DetectTest(unittest.TestCase):
    def test_every_frame_is_retracked_in_order_and_compared_with_the_live_game(self):
        d = scratch(self)
        make_session(d, n=700)
        seen = []

        class Backend:
            def process(self, f, ms):
                seen.append((which(f), ms))
                return [(1, None)] if which(f) < 10 else []

        out = os.path.join(d, "det.tsv")
        rc = camera_video.main(["detect", d, "1", "--out", out, "--", "--conf", "0.3"], cv2=cv2,
                               make_backend=lambda args: (Backend(), Args(max_persons=6, conf=args)))
        self.assertEqual(rc, 0)
        self.assertEqual([s[0] for s in seen], [k % 20 for k in range(699)])
        self.assertTrue(all(b[1] > a[1] for a, b in zip(seen, seen[1:])))
        rows = [line.split() for line in open(out) if not line.startswith("#")]
        live = [r for r in rows if r[5] != "-1"]
        self.assertEqual(len(live), 350)
        self.assertTrue(all(r[5] == "1" for r in live), "the live game's valid people per frame")
        both = sum(1 for r in live if r[6] == "1")
        self.assertEqual(both, sum(1 for k in range(0, 699, 2) if k % 20 < 10))


class SummaryLineTest(unittest.TestCase):
    def test_frames_duration_size_drops_and_not_packed(self):
        d = scratch(self)
        rec = make_session(d, n=90)
        line = session_summary.camera_video_line(d)
        self.assertIn("camera video: " + os.path.join(d, "camera.mkv"), line)
        self.assertIn("90 frames", line)
        self.assertIn(f"{89 / FPS / 60:.1f} min", line)
        self.assertIn(f"{os.path.getsize(os.path.join(d, 'camera.mkv')) / 1e6:.0f} MB", line)
        self.assertIn(f"{rec.dropped} dropped", line)
        self.assertIn("not in the .tar.gz", line)
        self.assertNotIn("cut off", line)

    def test_a_killed_recording_and_its_drops_are_said(self):
        d = scratch(self)
        with open(os.path.join(d, "camera.frames.tsv"), "w") as f:
            f.write("# dc3-camera-frames v1\n0 1 10.0 0.0\n1 2 10.5 33.0\n"
                    "# status written=2 dropped=7 bytes=1000\n2 3 11.0")
        line = session_summary.camera_video_line(d)
        self.assertIn("2 frames", line)
        self.assertIn("7 dropped", line)
        self.assertIn("cut off", line)

    def test_no_video_no_line(self):
        self.assertIsNone(session_summary.camera_video_line(scratch(self)))


class PlaySettingsTest(unittest.TestCase):
    def show(self, content, *flags, env=None):
        d = scratch(self)
        if content is not None:
            os.makedirs(os.path.join(d, "dc3"))
            with open(os.path.join(d, "dc3", "playtest.env"), "w") as f:
                f.write(content)
        e = {k: v for k, v in os.environ.items() if not k.startswith(("DC3_", "MILO_"))}
        e["XDG_CONFIG_HOME"] = d
        e.update(env or {})
        p = subprocess.run(["bash", PLAY, "--show-settings", *flags], env=e, capture_output=True,
                           text=True, timeout=30)
        self.assertEqual(p.returncode, 0, p.stderr)
        return p.stdout, d

    def test_only_dc3_and_milo_lines_are_taken_and_nothing_is_run(self):
        marker = os.path.join(scratch(self), "ran")
        out, d = self.show("# the GPD\nDC3_PLAY_RECORD_VIDEO=1\n DC3_HEARTBEAT_S = \"10\"\n"
                           "PATH=/nowhere\nLD_PRELOAD=/x.so\ndc3_lower=1\njust words\n"
                           f"DC3_X=$(touch {marker})\nMILO_MAX_FRAMES=5\n")
        self.assertIn("DC3_PLAY_RECORD_VIDEO=1", out)
        self.assertIn("DC3_HEARTBEAT_S=10", out)
        self.assertIn("MILO_MAX_FRAMES=5", out)
        for bad in ("PATH=/nowhere", "LD_PRELOAD=/x.so", "dc3_lower=1", "just words"):
            self.assertIn(f"REFUSED (only DC3_*/MILO_* KEY=VALUE lines): line", out)
            self.assertRegex(out, r"REFUSED .*: line \d+: " + bad.replace("*", r"\*"))
        self.assertIn("4 set", out)
        self.assertFalse(os.path.exists(marker))
        self.assertIn("Camera video: on (DC3_PLAY_RECORD_VIDEO)", out)

    def test_the_option_overrides_the_file_and_the_environment_wins_over_it(self):
        out, _ = self.show("DC3_PLAY_RECORD_VIDEO=1\n", "--no-record-video")
        self.assertIn("Camera video: off (the option)", out)
        out, _ = self.show("DC3_PLAY_RECORD_VIDEO=0\n", "--record-video")
        self.assertIn("Camera video: on (the option)", out)
        out, _ = self.show("DC3_PLAY_RECORD_VIDEO=1\n", env={"DC3_PLAY_RECORD_VIDEO": "0"})
        self.assertIn("already in the environment, kept: DC3_PLAY_RECORD_VIDEO", out)
        self.assertIn("Camera video: off (default)", out)

    def test_the_file_records_the_camera_only(self):
        for flags in (("--video", "/dev/null"), ("--no-pose",), ("--replay", "/dev/null")):
            out, _ = self.show("DC3_PLAY_RECORD_VIDEO=1\n", *flags)
            self.assertIn("Camera video: off", out, flags)

    def test_no_file_is_fine(self):
        out, d = self.show(None)
        self.assertIn(f"Settings: no {d}/dc3/playtest.env", out)
        self.assertIn("Camera video: off (default)", out)


if __name__ == "__main__":
    unittest.main()
