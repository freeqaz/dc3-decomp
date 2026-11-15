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

One track, one body (np-player-identity). A recorded two-player session
(640x480, 8-10 Hz) had 13 ids for three bodies in one song: with two dancers
MediaPipe often reports one of them per frame, and its landmark tracking walks
from one body onto the other. The old pairing let a track take any detection
within max_dist (a quarter of the picture, 0.75 m at dancing distance) and the
leftover rule then deleted the neighbour's track. Also pinned, with sizes and
the frame's time given:
  * a single detection that walks from one dancer onto the other does not
    carry the first dancer's id over there, and both have their own ids when
    both are reported again;
  * two dancers who both step the same way in one frame keep their own ids
    (the pairing is the best one overall, not closest pair first);
  * a lost dancer's id waits for her and is not handed to a body 0.2 of the
    picture away -- that one is somebody new (the wrong-person control);
  * one frame reported on the neighbour does not cost the dancer her id;
  * on the session's own two-dancer stretches (hip positions from the
    recording, native/tests/data/pose/two_dancers_hips.txt) each dancer is one
    id. The file holds what the tracker handed over, not what MediaPipe gave
    it: suppressed second reports are not in it, so the old tracker run on it
    does not retrace the recorded ids frame for frame -- it does show the same
    fault (one id on two bodies, a dancer renumbered).

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


# A camera at the played session's rate: a frame every 0.1 s, torso 0.18 of
# the picture (2.5 m away), two dancers 0.16 apart (45 cm).
PERIOD = 0.1
T = (0.18,)
L = (0.60, 0.72)
R = (0.44, 0.72)


def at(p, dx, dy=0.0):
    return (round(p[0] + dx, 4), round(p[1] + dy, 4)) + T


TIMED_SEQUENCES = {
    # Only one of the two is reported, and the report walks from L onto R.
    "slide_onto_the_neighbour": frames(
        (10, [L + T, R + T]), (1, [at(L, -0.053)]), (1, [at(L, -0.107)]), (1, [R + T]), (5, [L + T, R + T])),
    # Both sidestep 0.085 (24 cm) between two frames.
    "both_step_the_same_way": frames((10, [L + T, R + T]), (5, [at(L, 0.085), at(R, 0.085)])),
    # R is lost for a second and found 0.10 from where she was.
    "lost_and_found_nearby": frames((10, [L + T, R + T]), (10, [L + T]), (5, [L + T, at(R, -0.10)])),
    # R is lost, and a body appears 0.20 (56 cm) from where she was.
    "a_body_too_far_to_be_her": frames((10, [L + T, R + T]), (10, [L + T]), (5, [L + T, at(R, -0.20)])),
    # Reported on the neighbour 0.14 away for one frame after a short gap.
    "one_frame_on_the_neighbour": frames((10, [L + T]), (3, []), (1, [at(L, -0.14, 0.03)]), (5, [L + T])),
}

HIPS = os.path.join(REPO, "native", "tests", "data", "pose", "two_dancers_hips.txt")


def recorded(section):
    """[(seconds, [(tracker id, x metres, u, v, torso), ...]), ...] of one
    section of the recorded session's hip positions."""
    out, inside = [], False
    with open(HIPS) as f:
        for line in f:
            t = line.split()
            if not t or t[0] == "#":
                continue
            if t[0] == "S":
                inside = t[1] == section
            elif inside and t[0] == "F":
                out.append((float(t[1]), []))
            elif inside and t[0] == "D":
                out[-1][1].append((int(t[1]), float(t[2]), float(t[5]), float(t[6]), float(t[7])))
    return out


def python_ids(seq):
    t = CentroidTracker()
    return [t.update([tuple(c) for c in cents]) for cents in seq]


def python_sized_ids(seq, **kw):
    t = CentroidTracker(**kw)
    return [t.update([tuple(c[:2]) for c in cents], [c[2] for c in cents]) for cents in seq]


def python_timed_ids(seq, **kw):
    t = CentroidTracker(**kw)
    return [t.update([tuple(c[:2]) for c in cents], [c[2] for c in cents], k * PERIOD)
            for k, cents in enumerate(seq)]


def python_recorded_ids(section):
    """One row per detection: (seconds into the section, recorded tracker id,
    x in metres, the id this tracker gives it)."""
    t, rows = CentroidTracker(), []
    rec = recorded(section)
    for when, dets in rec:
        ids = t.update([(d[2], d[3]) for d in dets], [d[4] for d in dets], when)
        rows += [(when - rec[0][0], d[0], d[1], i) for d, i in zip(dets, ids)]
    return rows


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


class OneTrackOneBody(unittest.TestCase):
    def test_a_report_that_walks_onto_the_neighbour_does_not_take_her_id(self):
        ids = python_timed_ids(TIMED_SEQUENCES["slide_onto_the_neighbour"])
        self.assertEqual(ids[9], [1, 2])
        self.assertEqual(ids[12], [2], "the report on R's spot is R")
        self.assertEqual(ids[13:], [[1, 2]] * 5, "a dancer was renumbered: %s" % ids[10:])

    def test_two_dancers_who_step_the_same_way_keep_their_ids(self):
        ids = python_timed_ids(TIMED_SEQUENCES["both_step_the_same_way"])
        self.assertEqual(ids[10:], [[1, 2]] * 5, "closest pair first swaps them")

    def test_a_lost_dancers_id_waits_for_her(self):
        ids = python_timed_ids(TIMED_SEQUENCES["lost_and_found_nearby"])
        self.assertEqual(ids[15], [1])
        self.assertEqual(ids[20:], [[1, 2]] * 5)

    def test_a_body_too_far_to_be_the_lost_dancer_is_somebody_new(self):
        ids = python_timed_ids(TIMED_SEQUENCES["a_body_too_far_to_be_her"])
        self.assertEqual(ids[20:], [[1, 3]] * 5, "56 cm from where R was lost a second ago")

    def test_one_frame_on_the_neighbour_does_not_cost_her_id(self):
        ids = python_timed_ids(TIMED_SEQUENCES["one_frame_on_the_neighbour"])
        self.assertEqual([frame for frame in ids if frame], [[1]] * 16, ids)

    def test_without_sizes_a_track_still_reaches_max_dist(self):
        t = CentroidTracker()
        t.update([L])
        self.assertEqual(t.update([(L[0] - 0.24, L[1])]), [1])
        self.assertEqual(t.update([(L[0] - 0.50, L[1])]), [2])


class TheRecordedSession(unittest.TestCase):
    """The two dancers of the recorded session, told apart by where they
    stood (the same stretches as test_native_pose_identity.cpp)."""

    @staticmethod
    def ids(rows, t0, t1, x0, x1):
        return {i for when, _, x, i in rows if t0 <= when <= t1 and x0 <= x <= x1}

    def test_song_2_each_dancer_is_one_id(self):
        rows = python_recorded_ids("song2")
        self.assertEqual(len(rows), 229, "two_dancers_hips.txt changed")
        left = self.ids(rows, 0.0, 21.0, -0.85, -0.5)
        centre = self.ids(rows, 0.0, 9.0, -0.15, 0.2) | self.ids(rows, 12.0, 21.0, -0.3, 0.2)
        self.assertEqual(len(left), 1, "the left dancer: %s" % left)
        self.assertEqual(len(centre), 1, "the centre dancer: %s" % centre)
        self.assertNotEqual(left, centre)
        self.assertNotIn(None, {r[3] for r in rows}, "a recorded detection was dropped as a second report")

    def test_song_3_two_dancers_two_ids(self):
        rows = python_recorded_ids("song3")
        self.assertEqual(len(rows), 705, "two_dancers_hips.txt changed")
        # The right dancer: recorded id 38 until 15.8 s, then 43.
        right = {i for when, rec, _, i in rows if (when < 15.7 and rec == 38) or (when > 16.5 and rec == 43)}
        self.assertEqual(len(right), 1, right)
        # The left dancer steps up to the camera at 10 s and back.
        left = self.ids(rows, 0.0, 9.0, -0.6, -0.05) | self.ids(rows, 11.0, 50.0, -0.6, -0.05)
        self.assertEqual(len(left), 1, "the left dancer: %s" % left)
        self.assertEqual(len({r[3] for r in rows}), 2, "two dancers were in view")


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

    @staticmethod
    def canned(poses):
        class Result:
            pose_world_landmarks = [[Landmark(*p) for p in world] for world, _ in poses]
            pose_landmarks = [[Landmark(*p) for p in image] for _, image in poses]
            segmentation_masks = None
        return Result

    def backend(self, poses):
        import math
        import pose_mediapipe as pm
        canned = self.canned

        class FakeMp:
            class ImageFormat:
                SRGB = 1

            @staticmethod
            def Image(image_format, data):
                return data

        class FakeLandmarker:
            result = canned(poses)

            def detect_for_video(self, image, ts_ms):
                return self.result

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

    def test_the_frame_time_reaches_the_tracker(self):
        # 40 cm (two thirds of a torso) in one second is her; in one 30 fps
        # frame it would not be. Only the time between the two calls says.
        try:
            import cv2  # noqa: F401
            import numpy as np
        except ImportError:
            self.skipTest("cv2/numpy not installed")
        b = self.backend([synthetic_pose(root=(0.0, 0.0, 2.5))])
        frame = np.zeros((480, 640, 3), dtype=np.uint8)
        self.assertEqual([tid for tid, _ in b.process(frame, 0)], [1])
        b._landmarker.result = self.canned([synthetic_pose(root=(0.40, 0.0, 2.5))])
        self.assertEqual([tid for tid, _ in b.process(frame, 1000)], [1])
        b._landmarker.result = self.canned([synthetic_pose(root=(0.80, 0.0, 2.5))])
        self.assertEqual([tid for tid, _ in b.process(frame, 1033)], [2], "control: 40 cm in a frame")


@unittest.skipUnless(shutil.which("node"), "node not installed: web parity not checked")
class TrackerWebParity(unittest.TestCase):
    def test_the_web_port_gives_the_same_ids(self):
        script = (
            "import { CentroidTracker } from %s;\n"
            "const { plain, sized, timed, period, sessions } = JSON.parse(process.argv[1]);\n"
            "const out = {};\n"
            "for (const [name, seq] of Object.entries(plain)) {\n"
            "  const t = new CentroidTracker();\n"
            "  out[name] = seq.map((c) => t.update(c));\n"
            "}\n"
            "for (const [name, seq] of Object.entries(sized)) {\n"
            "  const t = new CentroidTracker();\n"
            "  out[name] = seq.map((c) => t.update(c.map((d) => [d[0], d[1]]), c.map((d) => d[2])));\n"
            "}\n"
            "for (const [name, seq] of Object.entries(timed)) {\n"
            "  const t = new CentroidTracker();\n"
            "  out[name] = seq.map((c, k) => t.update(c.map((d) => [d[0], d[1]]), c.map((d) => d[2]), k * period));\n"
            "}\n"
            "for (const [name, seq] of Object.entries(sessions)) {\n"
            "  const t = new CentroidTracker();\n"
            "  out[name] = seq.flatMap(([when, c]) =>\n"
            "    t.update(c.map((d) => [d[0], d[1]]), c.map((d) => d[2]), when));\n"
            "}\n"
            "console.log(JSON.stringify(out));\n" % json.dumps("file://" + GEOMETRY_JS)
        )
        sessions = {name: [(when, [d[2:] for d in dets]) for when, dets in recorded(name)]
                    for name in ("song2", "song3")}
        res = subprocess.run(
            ["node", "--input-type=module", "-e", script,
             json.dumps({"plain": SEQUENCES, "sized": SIZED_SEQUENCES, "timed": TIMED_SEQUENCES,
                         "period": PERIOD, "sessions": sessions})],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)
        js = json.loads(res.stdout)
        for name, seq in TIMED_SEQUENCES.items():
            self.assertEqual(js[name], python_timed_ids(seq), name)
        for name in sessions:
            self.assertEqual(js[name], [r[3] for r in python_recorded_ids(name)], name)
            self.assertGreater(len(js[name]), 200, name)
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

    def test_the_web_geometry_gives_the_tracker_the_frame_time(self):
        # As BackendDropsTheSecondReport.test_the_frame_time_reaches_the_tracker.
        def pose(x):
            world, image = synthetic_pose(root=(x, 0.0, 2.5))
            return [{"world": world, "image": image, "vis": [1.0] * 33}]
        script = (
            "import { PoseGeometry } from %s;\n"
            "const steps = JSON.parse(process.argv[1]);\n"
            "const g = new PoseGeometry();\n"
            "console.log(JSON.stringify(steps.map(([poses, ms]) =>\n"
            "  g.process(poses, 640, 480, ms).map((p) => p.trackId))));\n" % json.dumps("file://" + GEOMETRY_JS)
        )
        steps = [(pose(0.0), 0), (pose(0.40), 1000), (pose(0.80), 1033)]
        res = subprocess.run(["node", "--input-type=module", "-e", script, json.dumps(steps)],
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(json.loads(res.stdout), [[1], [1], [2]])


if __name__ == "__main__":
    unittest.main()
