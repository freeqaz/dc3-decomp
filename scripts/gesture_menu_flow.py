#!/usr/bin/env python3
"""gesture_menu_flow.py -- drive DC3's menus with gestures alone (PLAN P-3).

A synthetic performer (scripts/synthetic_kinect.py) stands in front of the
native sensor and navigates the shell the way a person does on a 360 with no
controller: hold the right arm out to the side, raise or lower the hand until
the item they want is highlighted, swipe the forearm across the body to
select; the left arm does the same on the helpbar's back button.  Nothing
here presses a button or calls into the UI: no MILO_INPUT_SCRIPT, no
/api/input, no DTA that navigates.  The only engine calls are READS -- which
screen is up, which list has focus, which item that list highlights -- the
things a person reads off the screen.

The route (one engine run):
  title_screen -> main_screen -> choose_mode_screen (perform)
  -> song_select_screen (Y.M.C.A.) -> multiuser_screen (easy, start)
  -> the song (the performer stands still)
  -> perform_endgame_screen -> perform_final_results_screen (continue)
  -> perform_complete_screen (new song) -> song_select_screen
  -> back with the left hand (helpbar) -> choose_mode_screen -> main_screen.

Every select is a swipe: the engine logs each HamNavList select with
`selecting=0` for a gesture (HamNavList::UpdateGestures -> SetSelecting(false))
and `selecting=1` for a controller confirm (HamNavList::OnMsg(ButtonDownMsg)
-> SetSelecting(true)).  The report carries both counts.

Timing: headless engines advance UI time by 1/30 s per frame however fast
they run, so the performer follows the engine's frame clock
(SyntheticKinect.follow_engine) and every hold/swipe below is in engine
seconds.  The engine is paced (DC3_HTTP_PACE_FPS, --pace-fps, default 120 =
4x real time) so the performer answers every frame.

Usage (normally run by native/tests/test_native_gesture_menus.cpp):
  python3 scripts/gesture_menu_flow.py --engine native/build/dc3-native \\
      --workdir /tmp/x --report /tmp/x/report.json [--gpu --shots DIR]
      [--env DC3_CONTROLLER_MODE=forced]   # sabotage: gestures must then fail

Exit: 0 route completed; 1 route failed (report says where); 2 setup error.
"""
import argparse
import http.client
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from synthetic_kinect import SyntheticKinect  # noqa: E402


class FlowError(Exception):
    pass


class Engine:
    def __init__(self, base):
        self.base = base  # http://127.0.0.1:port/api
        u = urllib.parse.urlparse(base)
        self.host, self.port, self.root = u.hostname, u.port, u.path.rstrip("/")

    def _req(self, method, path, body=None, timeout=10):
        c = http.client.HTTPConnection(self.host, self.port, timeout=timeout)
        try:
            c.request(method, self.root + path, body=body)
            r = c.getresponse()
            return json.loads(r.read() or b"{}")
        finally:
            c.close()

    def screen(self):
        try:
            return self._req("GET", "/screen")["data"]["screen"]
        except Exception:
            return None

    def eval(self, dta):
        try:
            d = self._req("POST", "/dta/eval", body=dta.encode())
            return d.get("data", {}).get("value") if d.get("ok") else None
        except Exception:
            return None

    def pace(self, fps):
        try:
            self._req("PUT", f"/pace?fps={fps}")
        except Exception:
            pass

    def frame(self):
        try:
            return int(self._req("GET", "/frame")["data"]["frame"])
        except Exception:
            return -1

    def screenshot(self, path):
        c = http.client.HTTPConnection(self.host, self.port, timeout=30)
        try:
            c.request("GET", self.root + "/screenshot")
            r = c.getresponse()
            data = r.read()
            if r.status == 200 and data[:4] == b"\x89PNG":
                with open(path, "wb") as f:
                    f.write(data)
                return True
        except Exception:
            pass
        finally:
            c.close()
        return False


class Flow:
    def __init__(self, eng, kinect, log, shots=None):
        self.e = eng
        self.k = kinect
        self.log = log
        self.shots = shots
        self.steps = []
        self.nshot = 0

    def note(self, msg):
        line = f"[f{self.e.frame()} t{self.k.now():.1f}] {msg}"
        self.log.write(line + "\n")
        self.log.flush()
        print(line, flush=True)

    def shot(self, name):
        if self.shots:
            self.nshot += 1
            p = os.path.join(self.shots, f"{self.nshot:02d}_{name}.png")
            if self.e.screenshot(p):
                self.note(f"screenshot {p}")

    # ---- reading the screen (what a person sees) ------------------------------
    def focus_list(self):
        """The focused component of the focused panel (the right-hand list)."""
        return self.e.eval("{{{ui focus_panel} loaded_dir} focus_name}")

    def right_sym(self, lst):
        return self.e.eval("{{{{ui focus_panel} loaded_dir} find %s} get_selected_sym}" % lst)

    def left_index(self):
        return self.e.eval("{{{helpbar loaded_dir} find left_hand.hnl} get_selected}")

    def wait_screen(self, names, timeout=120.0, settle=1.0):
        """Wait (engine seconds of sensor time, bounded in wall time) for one
        of `names`, then let it settle (list enter animations)."""
        if isinstance(names, str):
            names = [names]
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            s = self.e.screen()
            if s in names:
                self.k.wait(settle)
                self.note(f"screen {s}")
                return s
            time.sleep(0.02)
        raise FlowError(f"timed out waiting for {names}; screen is {self.e.screen()}")

    # ---- gestures --------------------------------------------------------------
    def point_at(self, side, want, read, heights=None, hold=0.5):
        """Raise/lower the hand until `read()` reports `want` highlighted.
        Returns the hand height that highlights it."""
        if heights is None:
            heights = [i / 20.0 for i in range(0, 21)]
        seen = []
        for h in heights:
            self.k.point(0, side, h)
            self.k.wait(hold)
            got = read()
            seen.append((round(h, 2), got))
            if got == want:
                self.note(f"{side} hand at height {h:.2f} highlights {want!r}")
                return h
        raise FlowError(f"no {side} hand height highlights {want!r}; saw {seen}")

    def swipe_select(self, side, h, hover=1.0, shot=None):
        """Hover over the highlighted item, then swipe across the body."""
        self.k.point(0, side, h)
        self.k.wait(hover)
        if shot:
            self.shot(shot)
        self.k.swipe(0, side, h, duration=0.3, sweep=110.0)
        self.k.wait(0.6)       # sweep 0.3 + hold, then the arm drops
        self.k.set_pose(0, "stand")
        self.k.wait(0.3)

    def pick(self, want, expect_screen=None, list_name=None, timeout=60.0):
        """Select `want` on the focused right-hand list by gesture."""
        lst = list_name or self.focus_list()
        if not lst:
            raise FlowError(f"no focused list on {self.e.screen()}")
        before = self.e.screen()
        h = self.point_at("right", want, lambda: self.right_sym(lst))
        self.swipe_select("right", h, shot=f"{before}_{want}")
        self.steps.append({"screen": before, "list": lst, "select": want, "height": h})
        if expect_screen:
            return self.wait_screen(expect_screen, timeout=timeout)
        return None

    SHELL = ("song_select_screen", "main_screen", "choose_mode_screen")

    # What a player picks on each post-song screen (ui/endgame/perform_endgame.dta):
    # perform_endgame_screen plays the score count-up and moves on by itself;
    # perform_final_results_panel's right_hand.hnl offers continue (-> the
    # complete screen), flaunt, leaderboards, share photos; perform_complete_panel
    # offers new_song (-> song select), restart, change difficulty, main menu...
    RESULTS_PICKS = {
        "perform_final_results_screen": "continue",
        "perform_complete_screen": "new_song",
    }

    def results(self, timeout=240.0):
        """From the end of the song to the shell, by gesture."""
        end = time.monotonic() + timeout
        last = None
        while time.monotonic() < end:
            s = self.e.screen()
            if s in self.SHELL:
                return s
            if s != last:
                self.note(f"results screen {s}")
                last = s
            want = self.RESULTS_PICKS.get(s)
            if want and self.focus_list():
                self.k.wait(1.0)   # the list's enter animation
                if self.e.screen() == s:
                    self.pick(want)
                    self.k.wait(1.0)
                continue
            self.k.wait(0.5)
        raise FlowError(f"results never reached the shell; at {self.e.screen()} "
                        f"(focus list {self.focus_list()})")

    def back(self, expect_screen, timeout=60.0):
        """The helpbar's back button: left arm, lower item (index 1)."""
        before = self.e.screen()
        h = self.point_at("left", 1, self.left_index, heights=[1.0, 0.9, 0.8, 0.7, 0.6])
        self.swipe_select("left", h, shot=f"{before}_back")
        self.steps.append({"screen": before, "list": "left_hand.hnl", "select": "back", "height": h})
        return self.wait_screen(expect_screen, timeout=timeout)


# The global-options cache of a console that has seen every tutorial (the
# voice-control tutorial after the title screen, playlists, ...): the same
# 0x39 bytes as test_native_twoplayer.cpp's SeedAllTutorialsSeen /
# test_dta_flow.cpp's SeedGlobalOptions, mTutorialsSeen (int, LE) at 0x28.
_GLOBAL_OPTIONS = bytes([
    0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xc1, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
])


def seed_all_tutorials_seen(cache_dir):
    b = bytearray(_GLOBAL_OPTIONS)
    b[0x28:0x2c] = (0x7fffffff).to_bytes(4, "little")
    os.makedirs(os.path.join(cache_dir, "global"), exist_ok=True)
    with open(os.path.join(cache_dir, "global", "global"), "wb") as fh:
        fh.write(bytes(b))


def free_port():
    for _ in range(50):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        p = s.getsockname()[1]
        s.close()
        if 20000 <= p < 60000:
            return p
    return 0


def run(a):
    os.makedirs(a.workdir, exist_ok=True)
    for d in ("cache", "saves"):
        os.makedirs(os.path.join(a.workdir, d), exist_ok=True)
    if a.all_tutorials_seen:
        seed_all_tutorials_seen(os.path.join(a.workdir, "cache"))
    port = a.port or free_port()
    sock = os.path.join(a.workdir, "kinect.sock")
    engine_log = os.path.join(a.workdir, "engine.log")
    flow_log = open(os.path.join(a.workdir, "flow.log"), "w")
    k = SyntheticKinect(sock, people=1)
    k.start()
    env = dict(os.environ)
    for v in ("MILO_INPUT_SCRIPT", "DC3_FAST_BOOT"):
        env.pop(v, None)
    env.update({
        "MILO_HEADLESS": "1", "MILO_FATAL_FAILS": "0", "DC3_SHOW_SPLASH": "0",
        "DC3_FAST_TIME": "1", "DC3_HTTP": "1", "DC3_HTTP_PORT": str(port),
        "MILO_MAX_FRAMES": "0",
        # A frame window the performer can answer in (HttpServer.cpp PaceFrame);
        # unpaced, a GPU-less engine runs ~3000 fps and a swipe arrives as a
        # handful of repeated poses that never reach ArcDetector's 1.0.
        "DC3_HTTP_PACE_FPS": str(a.pace_fps),
        "DC3_CACHE_DIR": os.path.join(a.workdir, "cache"),
        "DC3_SAVE_DIR": os.path.join(a.workdir, "saves"),
    })
    env.update(SyntheticKinect.engine_env(sock))
    if a.gpu:
        env.pop("MILO_NORENDER", None)
        env["MILO_RENDER"] = "1"
    else:
        env.pop("MILO_RENDER", None)
        env["MILO_NORENDER"] = "1"
    for kv in a.env:
        key, _, val = kv.partition("=")
        env[key] = val
    cmd = [os.path.abspath(a.engine)]
    if a.gpu_lock:
        cmd = ["flock", "-s", "-w", "300", a.gpu_lock] + cmd
    with open(engine_log, "w") as lf:
        proc = subprocess.Popen(cmd, cwd=os.path.dirname(os.path.abspath(a.engine)),
                                env=env, stdout=lf, stderr=subprocess.STDOUT,
                                start_new_session=True)
    report = {"ok": False, "failed_at": None, "error": None, "steps": [],
              "screens": [], "engine_log": engine_log, "port": port}
    eng = Engine(f"http://127.0.0.1:{port}/api")
    f = Flow(eng, k, flow_log, a.shots)
    try:
        end = time.monotonic() + 120
        while eng.screen() is None:
            if proc.poll() is not None:
                raise FlowError(f"engine exited early ({proc.returncode})")
            if time.monotonic() > end:
                raise FlowError("engine HTTP server never answered")
            time.sleep(0.2)
        k.follow_engine(eng.base)
        f.note("engine up; performer following the engine clock")

        f.wait_screen("title_screen", timeout=180, settle=1.5)
        f.shot("title")
        f.pick("title_screen_menu", expect_screen="main_screen", timeout=30)
        f.shot("main")
        f.pick("gameplay", expect_screen="choose_mode_screen")
        f.pick("perform", expect_screen="song_select_screen")
        f.pick("ymca", expect_screen="multiuser_screen")
        lst = f.focus_list()
        f.pick("easy", list_name=lst)
        f.k.wait(2.5)    # the side's pane changes: seldiff -> startgame
        f.pick("play", list_name=f.focus_list(), expect_screen=["loading_screen", "game_screen"],
               timeout=120)
        f.wait_screen("game_screen", timeout=300, settle=3.0)
        f.shot("gameplay")
        # Nobody points at anything during the song: run it unpaced.
        eng.pace(a.song_pace_fps)
        f.note(f"dancing (standing) through the song, paced at {a.song_pace_fps} fps")
        f.wait_screen("perform_endgame_screen", timeout=a.song_timeout, settle=0.0)
        eng.pace(a.pace_fps)
        f.k.wait(3.0)
        f.shot("endgame")
        s = f.results()
        f.note(f"after results: {s}")
        f.shot("after_results")
        if s == "song_select_screen":
            s = f.back(["choose_mode_screen", "main_screen"])
        if s == "choose_mode_screen":
            s = f.back("main_screen")
        if s != "main_screen":
            raise FlowError(f"expected main_screen at the end, at {s}")
        f.shot("main_again")
        report["ok"] = True
    except FlowError as ex:
        report["error"] = str(ex)
        report["failed_at"] = eng.screen()
        f.note(f"FAILED: {ex}")
        f.shot("failure")
    finally:
        report["steps"] = f.steps
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except Exception:
            pass
        try:
            proc.wait(10)
        except Exception:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except Exception:
                pass
        k.stop()
    with open(engine_log, errors="replace") as lf:
        text = lf.read()
    sel = [ln for ln in text.splitlines() if "DC3 HamNavList: select " in ln]
    report["selects_by_gesture"] = sum(1 for ln in sel if "selecting=0" in ln)
    report["selects_by_button"] = sum(1 for ln in sel if "selecting=1" in ln)
    report["controller_mode_enters"] = text.count("DC3 ControllerMode: enter")
    report["screens"] = [ln.split("Screen '")[1].split("'")[0]
                         for ln in text.splitlines() if "DC3 UI: Screen '" in ln and "' Enter" in ln]
    report["policy"] = [ln for ln in text.splitlines() if "controller mode policy" in ln][:3]
    with open(a.report, "w") as rf:
        json.dump(report, rf, indent=1)
    f.note(f"report {a.report}: ok={report['ok']}")
    return 0 if report["ok"] else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--engine", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--gpu", action="store_true", help="render (needed for --shots)")
    ap.add_argument("--gpu-lock", default=None, help="flock -s this file around the engine")
    ap.add_argument("--shots", default=None, help="screenshot directory (with --gpu)")
    ap.add_argument("--all-tutorials-seen", action="store_true",
                    help="seed the global options cache of a console that has seen every tutorial")
    ap.add_argument("--song-timeout", type=float, default=600.0)
    ap.add_argument("--pace-fps", type=float, default=120.0,
                    help="DC3_HTTP_PACE_FPS for the engine (engine frames per wall second)")
    ap.add_argument("--song-pace-fps", type=float, default=0.0,
                    help="pace during the song (0 = unpaced; nobody gestures there)")
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the engine")
    a = ap.parse_args()
    if not os.path.exists(a.engine):
        print(f"engine not found: {a.engine}", file=sys.stderr)
        return 2
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
