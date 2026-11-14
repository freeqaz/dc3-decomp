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
  kitdrive ownership a used session folder is refused; quit() leaves a game it
                     did not start and stops its own; a debug port answered by
                     another game is not driven; presses are >= 3 frames apart and
                     held >= 2 frames
  camera_resolve / preflight camera
                     auto and a by-id name find a camera that came back as
                     /dev/video1; a missing index is explained (re-connected,
                     use auto) and never called "in use"; "another program is
                     using it" only with a holder; below 15 fps WARNs with the
                     light advice
  pose_python.py     the game's interpreter order (the same cases as the C++
                     PosePythonChoiceTest): DC3_POSE_PYTHON, the playtest venv
                     (DC3_POSE_VENV replaces the default), the checkout's
                     .venv, a worktree's main checkout, python3; fake
                     interpreters print which one ran; preflight checks that
                     interpreter and names bootstrap.sh --fix when it lacks
                     the packages

Registered as ctest PlaytestKit.Python (native/CMakeLists.txt).
"""
import argparse
import io
import json
import os
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "scripts", "playtest"))
import asset_manifest  # noqa: E402
import camera_resolve  # noqa: E402
import preflight  # noqa: E402
import kitdrive  # noqa: E402
import pose_python  # noqa: E402
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
        # Capitals in the data path are fine since PLAN H-17 was fixed (the
        # game keeps a host directory's spelling); the check used to refuse it.
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
        self.assertEqual(rc, 0, out)
        self.assertNotIn("capital", out)


class KitDriveTest(unittest.TestCase):
    def test_only_current_screen_expressions_are_guarded(self):
        g = kitdrive.Kit.guarded
        self.assertEqual(g("{taskmgr seconds}"), "{taskmgr seconds}")
        self.assertEqual(g("{{ui current_screen} name}"),
                         "{if_else {ui current_screen} {{ui current_screen} name} -2}")

    def test_every_link_of_the_focus_list_chain_is_guarded(self):
        """A screen with no focus panel, or a panel without the list, used to
        put 15 MILO_FAILs of the kit's own into one soak's log."""
        k = kitdrive.Kit.__new__(kitdrive.Kit)
        lst = k.focus_list()
        g = kitdrive.Kit.guarded("{%s num_data}" % lst)
        self.assertEqual(
            g, "{if_else {ui current_screen} {if_else {{ui current_screen} focus_panel} "
               "{if_else {{{ui current_screen} focus_panel} find right_hand.hnl} "
               "{{{{ui current_screen} focus_panel} find right_hand.hnl} num_data} -2} -2} -2}")
        self.assertEqual(g.count("{") , g.count("}"))


class KitDriveRowsTest(unittest.TestCase):
    """PLAN H-28: rows() must never ask a provider for a row past its end.
    Soak #5 read data_symbol 4 of FitnessProvider's 4 rows because
    get_num_display (slots on screen) said 5; the range FAIL then fell
    through into a past-the-end read and a SIGSEGV in the eval."""

    class FakeList:
        def __init__(self, syms, slots):
            self.syms, self.slots, self.asked = syms, slots, []

        def eval(self, expr, timeout=10):
            parts = expr.strip("{}").split()
            op = parts[-2] if parts[-1].isdigit() else parts[-1]
            if op == "num_data":
                return len(self.syms)
            if op == "get_num_display":
                return self.slots
            if op == "data_symbol":
                n = int(parts[-1])
                self.asked.append(n)
                return self.syms[n] if n < len(self.syms) else "GARBAGE"
            return None

    def kit_on(self, fake):
        k = kitdrive.Kit.__new__(kitdrive.Kit)
        k.eval = fake.eval
        return k

    def test_reads_exactly_the_providers_rows(self):
        fake = self.FakeList(["fitness_option", "fitness_set_fitness_goals",
                              "fitness_body_profile", "playlists"], slots=5)
        rows = self.kit_on(fake).rows("{lst}", limit=8)
        self.assertEqual(rows, fake.syms)
        self.assertEqual(max(fake.asked), 3, "asked past the provider's last row")

    def test_no_count_no_rows(self):
        fake = self.FakeList(["a", "b"], slots=5)
        fake.eval = lambda expr, timeout=10: (5 if "get_num_display" in expr else None)
        self.assertEqual(self.kit_on(fake).rows("{lst}"), [])


class KitDriveOwnershipTest(unittest.TestCase):
    """The driver only drives and quits a game it started (a fallback in
    another driver quit a person's live song, np-gpd-test 2026-11-14)."""

    def kit(self, session, proc):
        k = kitdrive.Kit.__new__(kitdrive.Kit)
        k.session, k.proc, k.log, k.port = session, proc, lambda *a: None, None
        return k

    def test_descends_from(self):
        self.assertTrue(kitdrive.descends_from(os.getpid(), os.getppid()))
        self.assertTrue(kitdrive.descends_from(os.getpid(), os.getpid()))
        self.assertFalse(kitdrive.descends_from(os.getppid(), os.getpid()))

    def test_a_used_session_folder_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("game.pid", "http_port"):
                path = os.path.join(d, name)
                with open(path, "w") as f:
                    f.write("1\n")
                def no_launch(*a, **k):
                    raise AssertionError("the driver went on to launch a game")
                real, kitdrive.subprocess.Popen = kitdrive.subprocess.Popen, no_launch
                try:
                    with self.assertRaises(kitdrive.NotOurGame):
                        kitdrive.Kit(d, os.path.join(d, "home"))
                finally:
                    kitdrive.subprocess.Popen = real
                os.unlink(path)

    def test_quit_leaves_a_game_it_did_not_start(self):
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            foreign = subprocess.Popen(["sleep", "30"])
            try:
                with open(os.path.join(d, "game.pid"), "w") as f:
                    f.write(str(foreign.pid))
                launcher = subprocess.Popen(["sleep", "0.2"])
                self.kit(d, launcher).quit(timeout=10)
                self.assertIsNone(foreign.poll(), "quit() killed a game it did not start")
            finally:
                foreign.kill()
                foreign.wait()

    def test_quit_stops_its_own_game(self):
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            pidfile = os.path.join(d, "game.pid")
            launcher = subprocess.Popen(["sh", "-c", f"sleep 30 & echo $! > {pidfile}; wait"])
            for _ in range(100):
                if os.path.getsize(pidfile) if os.path.exists(pidfile) else 0:
                    break
                time.sleep(0.02)
            t0 = time.monotonic()
            rc = self.kit(d, launcher).quit(timeout=10)
            self.assertIsNotNone(rc)
            self.assertLess(time.monotonic() - t0, 5.0, "its own game was not stopped by SIGTERM")

    def test_a_port_answered_by_another_game_is_not_driven(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "http_port"), "w") as f:
                f.write("9\n")
            k = self.kit(d, type("P", (), {"pid": os.getpid(), "poll": lambda self: None})())
            k.jget = lambda path, timeout=10: {"data": {"pid": os.getppid()}}
            with self.assertRaises(kitdrive.NotOurGame):
                k.wait_port(timeout=5)
            k.jget = lambda path, timeout=10: {"data": {"pid": os.getpid()}}
            self.assertEqual(k.wait_port(timeout=5), 9)

    def test_presses_are_spaced(self):
        k = self.kit("/nonexistent", None)
        sent = []
        k._req = lambda path, data=None, timeout=10: sent.append(json.loads(data))
        real_sleep, time.sleep = time.sleep, lambda s: None
        try:
            k.press("down", "down", gap=0)
            k.press("confirm", gap=1, wake=False)
        finally:
            time.sleep = real_sleep
        delays = [e["delay"] for seq in sent for e in seq if e["button"] != "l3"]
        self.assertEqual(len(delays), 3)
        self.assertTrue(all(x >= kitdrive.MIN_GAP_FRAMES for x in delays), delays)
        holds = [e.get("hold", 1) for seq in sent for e in seq]
        self.assertTrue(all(h >= 2 for h in holds), holds)
        self.assertGreater(kitdrive.MIN_GAP_FRAMES, kitdrive.HOLD_FRAMES)


class FakeCameras:
    """The V4L2 surface camera_resolve (and pose_server.resolve_camera) asks."""
    by_id_dir = "/dev/v4l/by-id"

    def __init__(self, nodes, by_id=None):
        self._nodes = nodes  # {node: (is_capture, card)}
        self._by_id = by_id or {}

    def available(self):
        return True

    def nodes(self):
        return sorted(self._nodes, key=lambda n: int(n[10:]))

    def query(self, node):
        ok, card = self._nodes[node]
        return ok, card if ok else f"{card}: no video capture"

    def name(self, node):
        return self._nodes.get(node, (False, "?"))[1]

    def exists(self, path):
        return path in self._nodes or path in [os.path.join(self.by_id_dir, n)
                                               for n in self._by_id]

    def realpath(self, path):
        return self._by_id.get(os.path.basename(path), path)

    def by_id(self):
        return dict(self._by_id)


class CameraChoiceTest(unittest.TestCase):
    """--camera auto|N|PATH is resolved as the pose server resolves it, and the
    preflight says in plain words what is wrong (the GPD's camera came back as
    /dev/video1 eight times in a day)."""

    GPD = {"/dev/video1": (True, "Integrated Camera"), "/dev/video2": (False, "Integrated Camera")}
    BY_ID = {"usb-Generic_Integrated_Camera-video-index0": "/dev/video1"}

    def setUp(self):
        self.fake = FakeCameras(self.GPD, self.BY_ID)
        self._devices = camera_resolve.Devices
        camera_resolve.Devices = lambda: self.fake
        preflight.results.clear()

    def tearDown(self):
        camera_resolve.Devices = self._devices
        preflight.results.clear()

    def test_auto_and_by_id_find_the_camera(self):
        self.assertEqual(camera_resolve.resolve("auto")["node"], "/dev/video1")
        self.assertEqual(camera_resolve.resolve(None)["node"], "/dev/video1")
        self.assertEqual(camera_resolve.resolve(
            "usb-Generic_Integrated_Camera-video-index0")["node"], "/dev/video1")

    def test_a_missing_index_says_what_happened(self):
        r = camera_resolve.resolve("0")
        words = r["fallback"] if r["node"] else camera_resolve.missing_index_words("0", r)
        for want in ("camera 0 (/dev/video0) does not exist", "re-connected under another number",
                     "now: video1 'Integrated Camera'", "--camera auto",
                     "/dev/v4l/by-id", "usb-Generic_Integrated_Camera-video-index0"):
            self.assertIn(want, words)
        self.assertNotIn("another program", words)

    def preflight_camera(self, camera="auto", probe=None):
        a = argparse.Namespace(no_pose=False, video=None, camera=camera, python=sys.executable,
                               model="m", no_person=True)
        if probe is not None:
            preflight.run = lambda cmd, timeout=20, env=None: (0, json.dumps(probe), "")
        try:
            preflight.check_camera(a, probe is not None, False)
        finally:
            preflight.run = self._run
        return [(r["status"], r["what"], r["detail"], r["fix"]) for r in preflight.results]

    _run = staticmethod(preflight.run)

    def test_preflight_missing_index(self):
        rows = self.preflight_camera("0")
        text = " ".join(" ".join(r) for r in rows)
        self.assertIn("camera 0 (/dev/video0) does not exist", text)
        self.assertNotIn("another program", text)

    def test_preflight_busy_only_when_held(self):
        cam = camera_resolve.resolve("auto")
        rows = self.preflight_camera(probe={"ok": False, "kind": "busy", "camera": cam,
                                            "holders": [[4242, "firefox"]],
                                            "error": "could not open camera /dev/video1"})
        self.assertEqual(rows[-1][0], "FAIL")
        self.assertIn("another program is using the camera: firefox (pid 4242)", rows[-1][2])
        preflight.results.clear()
        rows = self.preflight_camera(probe={"ok": False, "kind": "no_frames", "camera": cam,
                                            "error": "camera /dev/video1 opened but sent no frame"})
        self.assertNotIn("another program", " ".join(rows[-1]))

    def test_probe_names_the_program_holding_the_device(self):
        import subprocess
        import camera_probe
        with tempfile.NamedTemporaryFile() as node:
            holder = subprocess.Popen([sys.executable, "-c",
                                       f"f = open({node.name!r}); import time; time.sleep(30)"])
            try:
                for _ in range(200):
                    got = camera_probe.holders(node.name)
                    if got:
                        break
                    time.sleep(0.02)
                self.assertIn(holder.pid, [pid for pid, _ in got])
            finally:
                holder.kill()
                holder.wait()
            self.assertEqual(camera_probe.holders(node.name), [])

    def test_preflight_slow_camera_warns(self):
        cam = camera_resolve.resolve("auto")
        ok = {"ok": True, "width": 640, "height": 480, "camera": cam}
        rows = self.preflight_camera(probe=dict(ok, fps=7.7))
        speed = [r for r in rows if r[1] == "camera speed"]
        self.assertEqual(len(speed), 1, rows)
        self.assertEqual(speed[0][0], "WARN")
        self.assertIn("more light", speed[0][3])
        preflight.results.clear()
        rows = self.preflight_camera(probe=dict(ok, fps=15.0))
        self.assertFalse([r for r in rows if r[1] == "camera speed"], rows)


class PosePythonTest(unittest.TestCase):
    """Must agree with NativeSkeletonProvider::ChoosePosePython: these are the
    cases of PosePythonChoiceTest (native/tests/test_pose_session.cpp)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = r = self._tmp.name
        self.home, self.tree, self.main = f"{r}/home", f"{r}/tree", f"{r}/main"
        for venv, name in ((f"{self.home}/.local/share/dc3/pose-venv", "home-venv"),
                           (f"{r}/custom-venv", "custom-venv"),
                           (f"{self.tree}/.venv", "tree-venv"),
                           (f"{self.main}/.venv", "main-venv")):
            os.makedirs(f"{venv}/bin")
            with open(f"{venv}/bin/python3", "w") as f:
                f.write(f"#!/bin/sh\necho {name}\n")
            os.chmod(f"{venv}/bin/python3", 0o755)
        with open(f"{self.tree}/.git", "w") as f:
            f.write(f"gitdir: {self.main}/.git/worktrees/tree\n")

    def tearDown(self):
        self._tmp.cleanup()

    def ran(self, env=None, home=None):
        import subprocess
        path, _ = pose_python.choose(self.tree, self.home if home is None else home, env or {})
        if path == "python3":
            return "python3"
        return subprocess.run([path], capture_output=True, text=True).stdout.strip()

    def test_the_games_order(self):
        custom = f"{self.root}/custom-venv"
        self.assertEqual(self.ran({"DC3_POSE_PYTHON": f"{custom}/bin/python3"}), "custom-venv")
        self.assertEqual(pose_python.choose(self.tree, self.home,
                                            {"DC3_POSE_PYTHON": "x"})[1], "DC3_POSE_PYTHON")
        self.assertEqual(self.ran(), "home-venv")
        self.assertEqual(self.ran({"DC3_POSE_VENV": custom}), "custom-venv")
        self.assertEqual(self.ran({"DC3_POSE_VENV": f"{self.root}/no-such-venv"}), "tree-venv")
        os.unlink(f"{self.home}/.local/share/dc3/pose-venv/bin/python3")
        self.assertEqual(self.ran(), "tree-venv")
        os.unlink(f"{self.tree}/.venv/bin/python3")
        self.assertEqual(self.ran(), "main-venv")
        self.assertIn("worktree", pose_python.choose(self.tree, self.home, {})[1])
        os.unlink(f"{self.main}/.venv/bin/python3")
        path, why = pose_python.choose(self.tree, self.home, {})
        self.assertEqual(path, "python3")
        self.assertIn("PATH", why)
        self.assertEqual(self.ran(home=""), "python3")  # no $HOME at all

    def test_a_file_that_cannot_run_is_skipped(self):
        os.chmod(f"{self.home}/.local/share/dc3/pose-venv/bin/python3", 0o644)
        self.assertEqual(self.ran(), "tree-venv")

    def test_preflight_checks_the_games_choice(self):
        # The game's choice is a Python without the packages: preflight FAILs
        # naming that interpreter and the fix, instead of finding a better one.
        bare = f"{self.root}/bare/bin/python3"
        os.makedirs(os.path.dirname(bare))
        with open(bare, "w") as f:  # answers preflight's import probe
            f.write("#!/bin/sh\necho '{\"mediapipe\": \"MISSING: no module\", "
                    "\"cv2\": \"MISSING: no module\", \"numpy\": \"2.2.6\", "
                    "\"python\": \"3.12.0\"}'\n")
        os.chmod(bare, 0o755)
        real = pose_python.choose
        pose_python.choose = lambda *a, **k: (bare, "the playtest venv")
        preflight.results.clear()
        try:
            a = argparse.Namespace(no_pose=False, python="")
            self.assertFalse(preflight.check_python(a))
        finally:
            pose_python.choose = real
        rows = [r for r in preflight.results if r["what"] == "pose packages"]
        preflight.results.clear()
        self.assertEqual(len(rows), 1, rows)
        self.assertEqual(rows[0]["status"], "FAIL")
        self.assertIn(bare, rows[0]["detail"])
        self.assertIn("mediapipe", rows[0]["detail"])
        self.assertIn("bootstrap.sh --fix", rows[0]["fix"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
