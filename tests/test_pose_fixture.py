"""The keypoint-replay fixture and its song-time replay (PLAN P-5b), no GPU.

native/tests/data/pose/taye01_stand_dance.dc3pose is MediaPipe's DC3-20 output
for archive/screenshots-old/woot/taye01_stand_dance.mp4 (8 s, 240 frames),
recorded once by scripts/pose_fixture_record.py. scripts/pose_scoring_gate.sh
replays it into a whole song (configs `replay` and `still`); this file checks,
cheaply, the things that gate's verdict depends on:

  * it parses, and every frame is a plausible human skeleton (bone lengths
    stable across the clip, feet on the floor, standing at the replay depth);
  * it is a DANCING stream: the hands travel, frame to frame and over the clip
    -- if it were a still person the gate's replay-vs-still comparison would
    be meaningless;
  * synthetic_kinect.py's replay is keyed on song time: the frame at song t is
    the fixture frame at t mod 8 s, independent of how often it is asked;
    --replay-still is frame 0 at every t;
  * re-recording is byte-identical (when mediapipe, cv2 and the gitignored
    video are present; skipped otherwise).

Plain unittest; registered with ctest as PoseFixture.Python.
"""

import math
import os
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(REPO, "native", "tests", "data", "pose", "taye01_stand_dance.dc3pose")
VIDEO = os.path.join(REPO, "archive", "screenshots-old", "woot", "taye01_stand_dance.mp4")
MODEL = os.path.join(REPO, "native", "models", "pose_landmarker_full.task")
sys.path.insert(0, os.path.join(REPO, "scripts"))

import synthetic_kinect as sk  # noqa: E402

HIP_C, SHO_L, ELB_L, WRI_L, HAND_L = 0, 4, 5, 6, 7
HAND_R, HIP_L, KNEE_L, FOOT_L, FOOT_R = 11, 12, 13, 18, 19


def dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def std(xs):
    m = sum(xs) / len(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))


class FixtureContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frames, cls.fps = sk.load_fixture(FIXTURE)
        cls.tracked = [f for f in cls.frames if f is not None]

    def test_shape(self):
        self.assertEqual(self.fps, 30.0)
        self.assertEqual(len(self.frames), 240)
        self.assertGreaterEqual(len(self.tracked), 230)
        self.assertLess(os.path.getsize(FIXTURE), 1 << 20)

    def test_every_frame_is_a_plausible_standing_person(self):
        for bone, lo, hi in (((SHO_L, ELB_L), 0.15, 0.45), ((HIP_L, KNEE_L), 0.25, 0.6)):
            lengths = [dist(f[bone[0]], f[bone[1]]) for f in self.tracked]
            self.assertTrue(lo < min(lengths) and max(lengths) < hi, (bone, min(lengths), max(lengths)))
            self.assertLess(std(lengths), 0.05, bone)
        feet = [min(f[FOOT_L][1], f[FOOT_R][1]) for f in self.tracked]
        self.assertAlmostEqual(min(feet), 0.0, delta=0.01)
        hips_z = [f[HIP_C][2] for f in self.tracked]
        self.assertAlmostEqual(sum(hips_z) / len(hips_z), 2.5, delta=0.01)

    def test_it_is_a_dancing_stream(self):
        for joint in (HAND_L, HAND_R):
            rel = [[f[joint][k] - f[HIP_C][k] for k in range(3)] for f in self.tracked]
            spread = max(std([r[k] for r in rel]) for k in range(3))
            self.assertGreater(spread, 0.08, f"joint {joint} barely moves over the clip")
            path = sum(dist(a, b) for a, b in zip(rel, rel[1:]))
            self.assertGreater(path, 3.0, f"joint {joint} travels only {path:.2f} m in 8 s")


class SongKeyedReplayTest(unittest.TestCase):
    def kinect(self, still=False):
        k = sk.SyntheticKinect(None, people=1)
        k.replay(FIXTURE, still=still)
        self.addCleanup(k.stop)
        return k

    def test_frame_at_song_time_is_fixture_frame_at_t_mod_clip(self):
        k = self.kinect()
        frames = k.replay_frames
        for t in (0.0, 0.5, 3.21, 7.99):
            want = frames[int(t * 30.0)]
            self.assertEqual(k._replay_at(t), want, t)
            self.assertEqual(k._replay_at(t + 8.0), want, t)      # loops every 8 s
            self.assertEqual(k._replay_at(t + 80.0), want, t)
        self.assertNotEqual(k._replay_at(1.0), k._replay_at(1.5))
        # asking twice at the same song time changes nothing (no wall clock)
        self.assertEqual(k._replay_at(2.0), k._replay_at(2.0))

    def test_still_is_frame_zero_all_song(self):
        k = self.kinect(still=True)
        f0 = k.replay_frames[0]
        for t in (0.0, 1.0, 4.4, 123.4):
            self.assertEqual(k._replay_at(t), f0)


@unittest.skipUnless(os.path.exists(VIDEO) and os.path.exists(MODEL), "video/model not present")
class RecorderDeterminismTest(unittest.TestCase):
    def test_rerecording_is_byte_identical(self):
        try:
            import cv2  # noqa: F401
            import mediapipe  # noqa: F401
        except ImportError:
            self.skipTest("cv2/mediapipe not installed")
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "again.dc3pose")
            subprocess.run([sys.executable, os.path.join(REPO, "scripts", "pose_fixture_record.py"),
                            VIDEO, out], check=True, capture_output=True, timeout=600)
            with open(out, "rb") as a, open(FIXTURE, "rb") as b:
                self.assertEqual(a.read(), b.read())


if __name__ == "__main__":
    unittest.main(verbosity=2)
