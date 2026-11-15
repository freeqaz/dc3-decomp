"""The pose tracker's identity through detection dropouts (PLAN P-12).

MediaPipe returns poses with no identity; CentroidTracker
(native/scripts/pose_mediapipe.py, ported to native/web/pose/pose_geometry.js)
supplies the tracking id the engine keys player slots on. A Kinect keeps one
tracking id for a person who stays in view. MediaPipe loses a dancer who never
left the frame for up to ~1.4 s (the np-dogfood3 cupidshuffle clip: 1.43 s at
song 42.0), and with the old 15-frame memory she came back as someone new.

What is pinned:
  * a 1.43 s dropout (43 frames at 30 fps) keeps the id;
  * a dropout longer than the 2 s memory (61 frames) does not: someone who
    walked out and back is a new person, as on a Kinect;
  * two people keep their own ids through one of them dropping out;
  * the web port gives the SAME ids as the desktop tracker on every sequence
    (desktop/web parity; needs `node`, skipped inside this file without it).

One body, one track. MediaPipe with num_poses 2 reports one person twice on
some frames (a played Milkshake session on the GPD: 41 such frames, the two
hip centres 1-17 cm apart, two real people never under 43 cm). The second
report became a second track on the same body and the dancer's id then
alternated between the twins -- 84 one-body id changes in 8.5 minutes, each a
new person to the engine (player unbound and rebound: the flickering HUD).
Also pinned, with each detection's torso length given:
  * a second report of a body gets no id and makes no track, whether the body
    is already tracked or new this frame, and the id then survives the
    detector alternating between its two reports;
  * a track left behind by a jump past max_dist is forgotten once the body is
    matched on top of it, so the id cannot flip back;
  * none of that touches two people: side by side, just outside the body
    radius, and two ESTABLISHED tracks that come together (one person passing
    in front of the other) all keep their own ids;
  * MediaPipeBackend.process and the web PoseGeometry.process hand the engine
    one person for a body reported twice.

Plain unittest; registered with ctest as PoseTracker.Python.
Run: python3 tests/test_pose_tracker.py
"""

import json
import os
import shutil
import subprocess
import sys
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))
from pose_mediapipe import CentroidTracker  # noqa: E402

GEOMETRY_JS = os.path.join(REPO, "native", "web", "pose", "pose_geometry.js")


def frames(*runs):
    """runs: (count, [centroid, ...]) -> a list of per-frame centroid lists."""
    out = []
    for count, cents in runs:
        out += [list(cents)] * count
    return out


A = (0.45, 0.55)
A_MOVED = (0.48, 0.56)  # a dancer moves a little while undetected
B = (0.80, 0.55)

SEQUENCES = {
    # np-dogfood3: tracked, lost 1.43 s mid-song, back where she was.
    "dropout_1430ms": frames((30, [A]), (43, []), (30, [A_MOVED])),
    # Past the memory: a real exit and return.
    "exit_2033ms": frames((30, [A]), (61, []), (30, [A])),
    # Exactly at the bound: 60 missing frames still match, 61 do not.
    "dropout_60_frames": frames((10, [A]), (60, []), (5, [A])),
    # Two people; B drops out for 1 s and comes back; A never leaves.
    "two_people": frames((30, [A, B]), (30, [A]), (30, [A, B])),
    # Detections listed in the other order must not swap ids.
    "two_people_swapped_order": frames((20, [A, B]), (20, [B, A])),
}


# Detections with a torso length: (x, y, size). The body radius is half of it.
A2 = (0.47, 0.58)              # the detector's second report of the body at A
NEAR = (0.20,)                 # a dancer about 2.5 m away: radius 0.10
CLOSE_UP = (0.70,)             # leaning over the keyboard: radius 0.35
LOW = (0.50, 0.82)             # her cut-off hips, guessed 0.32 further down
BETWEEN = (0.50, 0.62)         # the next guess, nearer the old track
C = (0.50, 0.50)
WALK = (1, 2, 3, 4)            # B's steps from 0.80 towards A at 0.45

SIZED_SEQUENCES = {
    # The played session: tracked, then both reports on some frames and
    # either one alone on others.
    "one_body_reported_twice": frames(
        (10, [A + NEAR]), (1, [A + NEAR, A2 + NEAR]), (1, [A2 + NEAR]), (1, [A + NEAR]),
        (1, [A2 + NEAR, A + NEAR]), (3, [A2 + NEAR]), (3, [A + NEAR])),
    # Nobody tracked yet and the first frame already has both reports.
    "new_body_reported_twice": frames((1, [A + NEAR, A2 + NEAR]), (5, [A2 + NEAR]), (5, [A + NEAR])),
    # A jump past max_dist makes a new track; the old one must not win the
    # body back two frames later.
    "twin_from_a_jump": frames(
        (10, [C + CLOSE_UP]), (1, [LOW + CLOSE_UP]), (3, [BETWEEN + CLOSE_UP]), (3, [C + CLOSE_UP])),
    # Controls: none of the above may merge two people.
    "two_people_sized": frames((30, [A + NEAR, B + NEAR]), (30, [A + NEAR]), (30, [A + NEAR, B + NEAR])),
    "two_people_just_apart": frames((20, [A + NEAR, (A[0] + 0.11, A[1]) + NEAR])),
    # B walks over to stand in front of A (0.07 a frame), stays, walks back.
    "two_people_come_together": (
        frames((20, [A + NEAR, B + NEAR]))
        + [[A + NEAR, (B[0] - 0.07 * k, B[1]) + NEAR] for k in WALK]
        + frames((5, [A + NEAR, A2 + NEAR]))
        + [[A + NEAR, (B[0] - 0.07 * k, B[1]) + NEAR] for k in reversed(WALK)]
        + frames((20, [A + NEAR, B + NEAR]))),
}


def python_ids(seq):
    t = CentroidTracker()
    return [t.update([tuple(c) for c in cents]) for cents in seq]


def python_sized_ids(seq, **kw):
    t = CentroidTracker(**kw)
    return [t.update([tuple(c[:2]) for c in cents], [c[2] for c in cents]) for cents in seq]


class TrackerIdentity(unittest.TestCase):
    def test_a_1430ms_dropout_keeps_the_id(self):
        ids = python_ids(SEQUENCES["dropout_1430ms"])
        self.assertEqual(ids[0], [1])
        self.assertEqual(ids[-1], [1], "she came back as someone new")

    def test_a_dropout_past_the_memory_is_a_new_person(self):
        ids = python_ids(SEQUENCES["exit_2033ms"])
        self.assertEqual(ids[0], [1])
        self.assertEqual(ids[-1], [2])

    def test_the_bound_is_60_frames(self):
        ids = python_ids(SEQUENCES["dropout_60_frames"])
        self.assertEqual(ids[-1], [1])
        longer = python_ids(frames((10, [A]), (61, []), (5, [A])))
        self.assertEqual(longer[-1], [2])

    def test_two_people_keep_their_ids(self):
        ids = python_ids(SEQUENCES["two_people"])
        self.assertEqual(ids[0], [1, 2])
        self.assertEqual(ids[40], [1])
        self.assertEqual(ids[-1], [1, 2])
        swapped = python_ids(SEQUENCES["two_people_swapped_order"])
        self.assertEqual(swapped[-1], [2, 1])


class OneBodyOneTrack(unittest.TestCase):
    def test_a_second_report_of_a_tracked_body_gets_no_id(self):
        ids = python_sized_ids(SIZED_SEQUENCES["one_body_reported_twice"])
        self.assertEqual(ids[10], [1, None])
        self.assertEqual(ids[13], [None, 1], "the matched report is the one kept, whichever comes first")
        self.assertEqual({i for frame in ids for i in frame}, {1, None},
                         "the dancer changed id: %s" % ids)

    def test_without_the_rule_the_dancer_alternates_between_two_ids(self):
        # The session's bug, kept as the control that the sequence can show it.
        ids = python_sized_ids(SIZED_SEQUENCES["one_body_reported_twice"], body_ratio=0.0)
        self.assertEqual(ids[10], [1, 2])
        self.assertEqual([frame for frame in ids[11:13]], [[2], [1]])

    def test_a_new_body_reported_twice_is_one_person(self):
        ids = python_sized_ids(SIZED_SEQUENCES["new_body_reported_twice"])
        self.assertEqual(ids[0], [1, None])
        self.assertEqual(ids[-1], [1])

    def test_a_track_left_behind_by_a_jump_is_forgotten(self):
        ids = python_sized_ids(SIZED_SEQUENCES["twin_from_a_jump"])
        self.assertEqual(ids[9], [1])
        self.assertEqual(ids[10], [2], "0.32 is past max_dist: a new id, as before")
        self.assertEqual(ids[11:], [[2]] * 6, "the id flipped back to the leftover track")
        without = python_sized_ids(SIZED_SEQUENCES["twin_from_a_jump"], body_ratio=0.0)
        self.assertEqual(without[11], [1], "control: the sequence no longer shows the flip")

    def test_two_people_are_not_merged(self):
        ids = python_sized_ids(SIZED_SEQUENCES["two_people_sized"])
        self.assertEqual(ids[0], [1, 2])
        self.assertEqual(ids[40], [1])
        self.assertEqual(ids[-1], [1, 2], "the one who dropped out lost her track")
        self.assertEqual(python_sized_ids(SIZED_SEQUENCES["two_people_just_apart"])[-1], [1, 2])

    def test_two_established_tracks_that_come_together_keep_their_ids(self):
        ids = python_sized_ids(SIZED_SEQUENCES["two_people_come_together"])
        self.assertEqual(ids[26], [1, 2], "standing 4 cm apart in the image, both tracked before")
        self.assertTrue(all(frame == [1, 2] for frame in ids), ids)

    def test_without_sizes_nothing_is_dropped(self):
        t = CentroidTracker()
        self.assertEqual(t.update([A, A2]), [1, 2])


def synthetic_pose(root=(0.1, 0.0, 2.5), width=640, height=480, hfov_deg=58.51):
    """A standing person as MediaPipe would report her: 33 world landmarks
    (hip-relative metres, y down) and their image positions through the
    backend's own pinhole (u = W/2 - f X/Z with X = -world_x + root_x)."""
    import math
    world = [[0.0, -0.55, 0.0] for _ in range(33)]          # head cluster
    def put(i, x, y):
        world[i] = [x, y, 0.0]
    put(11, 0.18, -0.45), put(12, -0.18, -0.45)             # shoulders
    put(13, 0.25, -0.20), put(14, -0.25, -0.20)             # elbows
    for i in (15, 17, 19, 21):
        put(i, 0.28, 0.05)                                  # left wrist, hand
    for i in (16, 18, 20, 22):
        put(i, -0.28, 0.05)
    put(23, 0.10, 0.0), put(24, -0.10, 0.0)                 # hips
    put(25, 0.11, 0.42), put(26, -0.11, 0.42)               # knees
    for i in (27, 29, 31):
        put(i, 0.12, 0.84)                                  # left ankle, foot
    for i in (28, 30, 32):
        put(i, -0.12, 0.84)
    f = (width * 0.5) / math.tan(math.radians(hfov_deg) * 0.5)
    image = []
    for wx, wy, wz in world:
        x, y, z = -wx + root[0], -wy + root[1], wz + root[2]
        image.append([(width * 0.5 - f * x / z) / width, (height * 0.5 - f * y / z) / height])
    return world, image


class Landmark:
    def __init__(self, x, y, z=0.0):
        self.x, self.y, self.z, self.visibility = x, y, z, 1.0


class BackendDropsTheSecondReport(unittest.TestCase):
    """MediaPipeBackend.process with MediaPipe replaced by a canned result."""

    def backend(self, poses):
        import math
        import pose_mediapipe as pm

        class Result:
            pose_world_landmarks = [[Landmark(*p) for p in world] for world, _ in poses]
            pose_landmarks = [[Landmark(*p) for p in image] for _, image in poses]
            segmentation_masks = None

        class FakeMp:
            class ImageFormat:
                SRGB = 1

            @staticmethod
            def Image(image_format, data):
                return data

        class FakeLandmarker:
            def detect_for_video(self, image, ts_ms):
                return Result

        b = pm.MediaPipeBackend.__new__(pm.MediaPipeBackend)   # no model load
        b._mp, b.probe, b._landmarker, b.segmentation = FakeMp, None, FakeLandmarker(), False
        b._tracker, b._root_z_filters, b._filter_last_seen = CentroidTracker(), {}, {}
        b._frame_count, b._hfov = 0, math.radians(pm.MediaPipeBackend.DEFAULT_HFOV_DEG)
        return b

    def run_poses(self, poses):
        try:
            import cv2  # noqa: F401  (process converts the frame with it)
            import numpy as np
        except ImportError:
            self.skipTest("cv2/numpy not installed")
        b = self.backend(poses)
        out = b.process(np.zeros((480, 640, 3), dtype=np.uint8), 0)
        self.assertEqual(len(b.last_masks), len(out), "masks and persons must stay paired")
        return out

    def test_one_body_reported_twice_is_one_person(self):
        out = self.run_poses([synthetic_pose(root=(0.10, 0.0, 2.5)), synthetic_pose(root=(0.15, 0.03, 2.5))])
        self.assertEqual([tid for tid, _ in out], [1])

    def test_two_people_are_two_persons(self):
        out = self.run_poses([synthetic_pose(root=(-0.40, 0.0, 2.5)), synthetic_pose(root=(0.40, 0.0, 2.5))])
        self.assertEqual([tid for tid, _ in out], [1, 2])


@unittest.skipUnless(shutil.which("node"), "node not installed: web parity not checked")
class TrackerWebParity(unittest.TestCase):
    def test_the_web_port_gives_the_same_ids(self):
        script = (
            "import { CentroidTracker } from %s;\n"
            "const { plain, sized } = JSON.parse(process.argv[1]);\n"
            "const out = {};\n"
            "for (const [name, seq] of Object.entries(plain)) {\n"
            "  const t = new CentroidTracker();\n"
            "  out[name] = seq.map((c) => t.update(c));\n"
            "}\n"
            "for (const [name, seq] of Object.entries(sized)) {\n"
            "  const t = new CentroidTracker();\n"
            "  out[name] = seq.map((c) => t.update(c.map((d) => [d[0], d[1]]), c.map((d) => d[2])));\n"
            "}\n"
            "console.log(JSON.stringify(out));\n" % json.dumps("file://" + GEOMETRY_JS)
        )
        res = subprocess.run(
            ["node", "--input-type=module", "-e", script,
             json.dumps({"plain": SEQUENCES, "sized": SIZED_SEQUENCES})],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)
        js = json.loads(res.stdout)
        for name, seq in SEQUENCES.items():
            self.assertEqual(js[name], python_ids(seq), name)
        for name, seq in SIZED_SEQUENCES.items():
            self.assertEqual(js[name], python_sized_ids(seq), name)
        self.assertIn(None, js["one_body_reported_twice"][10], "the parity run never saw a duplicate")

    def test_the_web_geometry_hands_over_one_person_for_a_body_reported_twice(self):
        def pose(root):
            world, image = synthetic_pose(root=root)
            return {"world": world, "image": image, "vis": [1.0] * 33}
        script = (
            "import { PoseGeometry } from %s;\n"
            "const cases = JSON.parse(process.argv[1]);\n"
            "const out = {};\n"
            "for (const [name, poses] of Object.entries(cases)) {\n"
            "  out[name] = new PoseGeometry().process(poses, 640, 480, 0).map((p) => [p.trackId, p.poseIndex]);\n"
            "}\n"
            "console.log(JSON.stringify(out));\n" % json.dumps("file://" + GEOMETRY_JS)
        )
        cases = {"twice": [pose((0.10, 0.0, 2.5)), pose((0.15, 0.03, 2.5))],
                 "two": [pose((-0.40, 0.0, 2.5)), pose((0.40, 0.0, 2.5))]}
        res = subprocess.run(["node", "--input-type=module", "-e", script, json.dumps(cases)],
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)
        js = json.loads(res.stdout)
        self.assertEqual(js["twice"], [[1, 0]])
        self.assertEqual(js["two"], [[1, 0], [2, 1]])


if __name__ == "__main__":
    unittest.main()
