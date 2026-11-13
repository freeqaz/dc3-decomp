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


def python_ids(seq):
    t = CentroidTracker()
    return [t.update([tuple(c) for c in cents]) for cents in seq]


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


@unittest.skipUnless(shutil.which("node"), "node not installed: web parity not checked")
class TrackerWebParity(unittest.TestCase):
    def test_the_web_port_gives_the_same_ids(self):
        script = (
            "import { CentroidTracker } from %s;\n"
            "const seqs = JSON.parse(process.argv[1]);\n"
            "const out = {};\n"
            "for (const [name, seq] of Object.entries(seqs)) {\n"
            "  const t = new CentroidTracker();\n"
            "  out[name] = seq.map((c) => t.update(c));\n"
            "}\n"
            "console.log(JSON.stringify(out));\n" % json.dumps("file://" + GEOMETRY_JS)
        )
        res = subprocess.run(
            ["node", "--input-type=module", "-e", script, json.dumps(SEQUENCES)],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)
        js = json.loads(res.stdout)
        for name, seq in SEQUENCES.items():
            self.assertEqual(js[name], python_ids(seq), name)


if __name__ == "__main__":
    unittest.main()
