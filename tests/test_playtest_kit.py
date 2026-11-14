#!/usr/bin/env python3
"""Tests for the playtest kit's own tools (scripts/playtest/, PLAN L-1).

Stdlib unittest, no GPU, no camera, no game data: synthetic files only.

  pose_session.py    a session splits into song segments where the song clock
                     jumps back; a song is a segment that starts in the
                     count-in; a cut reads back frame for frame; a file cut off
                     mid-frame loses only that frame; a wrong header is refused;
                     compare passes a faithful replay (held frames, later song
                     times) and fails a changed person, a missing frame, a
                     frame played early and frames out of order
  session_summary.py ratings, results screen, frame rates, memory, modal
                     counts and the exit line out of an engine log; a log with
                     no DC3_EXIT line says so
  asset_manifest.py  check: ok on a box that matches, exit 3 naming what is
                     missing, exit 4 for a short archive, exit 4 for a data
                     path with capitals (H-17)
  kitdrive.Kit.guarded  only expressions built on {ui current_screen} get the
                     null-screen guard

Registered as ctest PlaytestKit.Python (native/CMakeLists.txt).
"""
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "scripts", "playtest"))
import asset_manifest  # noqa: E402
import kitdrive  # noqa: E402
import pose_session  # noqa: E402
import session_summary  # noqa: E402

JOINTS = " ".join("0.1 0.2 2.5 2" for _ in range(20))


def frame(seq, song, persons=1, wall=None):
    out = [f"F {seq} {seq} {wall if wall is not None else seq / 30:.6f} {song:.6f} {seq} "
           f"{seq / 30:.17g} 0 1 0 1 0 {persons}"]
    for p in range(persons):
        out.append(f"P {p + 1} 1 {JOINTS}")
    return out


def write_session(path, songs):
    lines = ["# dc3-pose-session v1", "# test"]
    seq = 0
    for clock in songs:
        for t in clock:
            seq += 1
            lines += frame(seq, t)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


class PoseSessionTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="playtest_kit_")
        self.path = os.path.join(self.dir, "pose.dc3session")
        menus = [i * 0.5 for i in range(80)]                 # shell clock 0..39.5
        song1 = [-5 + i / 30 for i in range(30 * 40)]       # count-in, 35 s of song
        menus2 = [i * 0.5 for i in range(10)]
        song2 = [-5 + i / 30 for i in range(30 * 30)]
        write_session(self.path, [menus, song1, menus2, song2])

    def test_segments_and_kinds(self):
        _, frames = pose_session.load(self.path)
        rows = pose_session.describe(frames, pose_session.segments(frames))
        self.assertEqual([r["kind"] for r in rows], ["menus/other", "song", "menus/other", "song"])
        self.assertEqual([r["frames"] for r in rows], [80, 1200, 10, 900])

    def test_cut_reads_back_frame_for_frame(self):
        out = os.path.join(self.dir, "seg.dc3session")
        with redirect_stdout(io.StringIO()):
            self.assertEqual(pose_session.main(["cut", self.path, "2", out]), 0)
        _, whole = pose_session.load(self.path)
        _, cut = pose_session.load(out)
        s, e = pose_session.segments(whole)[1]
        self.assertEqual([f.lines for f in cut], [f.lines for f in whole[s:e]])

    def test_a_file_cut_off_mid_frame_loses_only_that_frame(self):
        with open(self.path) as f:
            text = f.read()
        with open(self.path, "w") as f:
            f.write(text + "\n".join(frame(99999, 30.0, persons=2)[:2]) + "\n")  # 1 of 2 people
        _, frames = pose_session.load(self.path)
        self.assertEqual(len(frames), 80 + 1200 + 10 + 900)

    def test_a_wrong_header_is_refused(self):
        with open(self.path, "w") as f:
            f.write("# dc3-pose-fixture v1\n")
        with self.assertRaises(ValueError):
            pose_session.load(self.path)

    def test_compare_a_replay_with_its_source(self):
        _, src = pose_session.load(self.path)
        a, b = pose_session.segments(src)[1]
        src = src[a:b]

        def replayed(edit=None):
            lines = ["# dc3-pose-session v1"]
            hold = 0
            for k, f in enumerate(src):
                if k % 50 == 0:  # a held frame in between: ignored
                    hold += 1
                    lines += frame(0x80000000 + hold, f.song)
                fl = list(f.lines)
                tok = fl[0].split()
                tok[1], tok[2], tok[3] = "9", "9", "9"       # seq, rnd, wall: free
                tok[4] = f"{f.song + 0.004:.6f}"             # a render frame later
                fl[0] = " ".join(tok)
                if edit:
                    fl = edit(k, fl)
                if fl:
                    lines += fl
            path = os.path.join(self.dir, "replayed.dc3session")
            with open(path, "w") as fh:
                fh.write("\n".join(lines) + "\n")
            return pose_session.load(path)[1]

        ok = pose_session.compare(src, replayed())
        self.assertTrue(ok["identical"], ok)
        self.assertEqual(ok["unknown_ids"], 0)
        self.assertAlmostEqual(ok["lag_min"], 0.004, places=5)

        def person(k, fl):
            return fl[:1] + [fl[1].replace("0.1 0.2", "0.1 0.3", 1)] if k == 100 else fl
        self.assertEqual(pose_session.compare(src, replayed(person))["mismatched"], 1)

        def drop(k, fl):
            return [] if k == 200 else fl
        r = pose_session.compare(src, replayed(drop))
        self.assertFalse(r["identical"])
        self.assertEqual(r["missing"], 1)

        # The replay's clock stopped before its last frames came due: reported
        # (how many, how much song), not a drop.
        n = len(src)
        r = pose_session.compare(src, replayed(lambda k, fl: [] if k >= n - 90 else fl))
        self.assertTrue(r["identical"], r)
        self.assertEqual(r["not_reached"], 90)
        self.assertAlmostEqual(r["not_reached_s"], 3.0, places=2)
        self.assertEqual(r["missing"], 0)

        def early(k, fl):
            if k == 300:
                tok = fl[0].split()
                tok[4] = f"{float(tok[4]) - 0.1:.6f}"
                fl = [" ".join(tok)] + fl[1:]
            return fl
        self.assertFalse(pose_session.compare(src, replayed(early))["identical"])

        swapped = replayed()
        swapped[10], swapped[11] = swapped[11], swapped[10]
        r = pose_session.compare(src, swapped)
        self.assertFalse(r["identical"])
        self.assertEqual(r["out_of_order"], 1)

        def stranger(k, fl):
            return fl + frame(99999, 0.0)[:1] + fl[1:] if k == 400 else fl
        self.assertFalse(pose_session.compare(src, replayed(stranger))["identical"])


LOG = """\
DC3 Native: something
DC3 UI: Screen 'game_screen' Enter (from 'real_loading_screen')
DC3 HamSongData::Load — midi='songs/macarena/macarena.mid'
DC3 MOVE_RATING p=0 measure=4 move=a.move rating=move_perfect lastfrac=0.9 score=1000 diff=medium
DC3 MOVE_RATING p=0 measure=5 move=b.move rating=move_bad lastfrac=0.1 score=1000 diff=medium
DC3 MOVE_RATING p=1 measure=5 move=b.move rating=move_ok lastfrac=0.5 score=200 diff=medium
DC3 PERF: fps=60.0 pose_fps=29.0 persons=1.00 window_s=5.0 provider=1
DC3 PERF: fps=40.0 pose_fps=25.0 persons=1.00 window_s=5.0 provider=1
POSE_STATS: fps=30.0 infer_ms_mean=25.0 infer_ms_p95=31.0 persons=1.00 sent=150 dropped=0 window_s=5.0
MILO_FAIL: something broke at 0x1234
MILO_FAIL: something broke at 0x5678
MILO_NOTIFY: a notice
DC3_HEARTBEAT: uptime_s=60 rss_kb=600000
DC3_HEARTBEAT: uptime_s=120 rss_kb=700000
DC3 UI: Screen 'perform_endgame_screen' Enter (from '<null>')
"""


class SessionSummaryTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="playtest_sum_")
        self.log = os.path.join(self.dir, "engine.log")

    def write(self, text):
        with open(self.log, "w") as f:
            f.write(text)

    def test_reads_what_happened(self):
        self.write(LOG + "DC3_EXIT: how=main_loop_returned detail=quit_key code=0 uptime_s=130 "
                   "rss_kb=700000 pid=1\n")
        text, r = session_summary.summarise(self.dir)
        self.assertEqual(len(r["songs"]), 1)
        s = r["songs"][0]
        self.assertEqual(s["song"], "macarena")
        self.assertEqual(s["ratings"][0], {"move_perfect": 1, "move_bad": 1})
        self.assertEqual(s["ratings"][1], {"move_ok": 1})
        self.assertEqual(s["results_screen"], "perform_endgame_screen")
        self.assertEqual([p[0] for p in r["perf"]], [60.0, 40.0])
        self.assertEqual(r["rss"], [(60, 600000), (120, 700000)])
        fails = [e["count"] for k, e in r["modals"].items() if k.startswith("FAIL")]
        self.assertEqual(fails, [2])  # the two addresses fold into one kind
        self.assertEqual(r["exit"]["how"], "main_loop_returned")
        self.assertIn("game fps: mean 50.0", text)
        self.assertIn("tracker: 30.0 fps", text)

    def test_no_exit_line_says_so(self):
        self.write(LOG)
        text, r = session_summary.summarise(self.dir)
        self.assertIsNone(r["exit"])
        self.assertIn("no DC3_EXIT line", text)


class AssetManifestTest(unittest.TestCase):
    def make_box(self, root, short=None):
        man = {"format": "dc3-player-assets v1", "archives": [], "loose": [],
               "song_dirs": ["macarena", "ymca"], "extracted_summary": {}}
        for rel in asset_manifest.ARKS:
            p = os.path.join(root, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(b"x" * (10 if rel != short else 9))
            man["archives"].append({"path": rel, "size": 10, "sha256": ""})
        for rel in asset_manifest.SONG_DTAS + [asset_manifest.SKELETON]:
            p = os.path.join(root, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(b"dta")
            man["loose"].append({"path": rel, "size": 3, "sha256": asset_manifest.sha256(p)})
        for s in man["song_dirs"]:
            os.makedirs(os.path.join(root, "extracted", "songs", s), exist_ok=True)
        mp = os.path.join(root, "manifest.json")
        with open(mp, "w") as f:
            json.dump(man, f)
        return mp

    def check(self, data, manifest):
        with redirect_stdout(io.StringIO()) as out:
            rc = asset_manifest.main(["check", "--data", data, "--manifest", manifest])
        return rc, out.getvalue()

    def test_ok_missing_short_and_capitals(self):
        base = tempfile.mkdtemp(prefix="playtest_assets_")
        good = os.path.join(base, "good")
        mp = self.make_box(good)
        self.assertEqual(self.check(good, mp)[0], 0)

        os.rmdir(os.path.join(good, "extracted", "songs", "ymca"))
        rc, out = self.check(good, mp)
        self.assertEqual(rc, 3)
        self.assertIn("1 of 2 song folders", out)

        short = os.path.join(base, "short")
        mp2 = self.make_box(short, short="gen/main_xbox_3.ark")
        rc, out = self.check(short, mp2)
        self.assertEqual(rc, 4)
        self.assertIn("main_xbox_3.ark is 9 bytes", out)

        caps = os.path.join(base, "Caps")
        mp3 = self.make_box(caps)
        rc, out = self.check(caps, mp3)
        self.assertEqual(rc, 4)
        self.assertIn("H-17", out)


class KitDriveTest(unittest.TestCase):
    def test_only_current_screen_expressions_are_guarded(self):
        g = kitdrive.Kit.guarded
        self.assertEqual(g("{taskmgr seconds}"), "{taskmgr seconds}")
        self.assertEqual(g("{{ui current_screen} name}"),
                         "{if_else {ui current_screen} {{ui current_screen} name} -2}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
