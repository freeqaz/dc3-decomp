#!/usr/bin/env python3
"""song_browser_hands.py -- the song browser's auto rule, against a body.

Boots dc3-native headless with scripts/synthetic_kinect.py as its pose
provider (one person, standing, so there is a tracked body from the first
frame) and NO input script, which is the game as a person launches it:
DC3_SONG_BROWSER is unset (auto) and controller mode follows the image's own
rules with a short timeout.  Then it walks to song select with the pad and
plays one sequence, printing the browser's state at each named point:

    OBS <name> <GET /api/songbrowser/state>
    VAL <name> <number>

The judgement is the caller's (native/tests/test_native_song_select.cpp,
SongBrowserHandsTest): this script only drives and records.  The sequence:

    arrive       on song select, reached with the pad
    idle60       the person stood there for 60 s of game time, nothing pressed
    after_down   one Down after that idle
    after_up     one Up (back where it was)
    control_down one Down again, with no idle before it
    short_hand   an arm out for 0.25 s, then down again
    hand         an arm out and held (VAL hand_seconds: game time to the switch)
    hand_away    the arm dropped, 2 s later
    press        one Down, 6 frames later
    press_idle   10 s of game time after that
    hand2        an arm out and held again
    typed        a typed character while the arm is still out, 6 frames later
    typed_hold   the arm still out 3 s later
    rearm        the arm dropped, then out and held again
    press_hold   one Down with the arm still out, then 2 s more of it

Poses are held in ENGINE time (SyntheticKinect.follow_engine), so a hold of
0.75 s is 0.75 s of the game's own UI clock whatever the machine is doing.
The controller-mode timeout is wall clock, which is why idle60 also reports
VAL idle_wall_seconds: the idle only proves anything if it outlasted it.

Usage:
    scripts/song_browser_hands.py --engine native/build/dc3-native --workdir DIR
        [--env KEY=VALUE ...] [--pace-fps 240] [--timeout-ms 1500]
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from synthetic_kinect import SyntheticKinect  # noqa: E402
from engine_http_port import EnginePortError, engine_port_env, port_file, wait_for_port  # noqa: E402
from gesture_menu_flow import Engine, seed_all_tutorials_seen  # noqa: E402


class Stop(Exception):
    pass


class Run:
    def __init__(self, eng, kinect):
        self.e = eng
        self.k = kinect

    def state(self):
        try:
            return self.e._req("GET", "/songbrowser/state").get("data", {})
        except Exception:
            return {}

    def obs(self, name):
        s = self.state()
        print(f"OBS {name} {json.dumps(s, separators=(',', ':'))}", flush=True)
        return s

    def val(self, name, value):
        print(f"VAL {name} {value}", flush=True)

    def frames(self, count):
        """Let `count` engine frames go by (1/30 s of game time each)."""
        target = self.e.frame() + count
        while True:
            now = self.e.frame()
            if now < 0:
                raise Stop("the engine stopped answering")
            if now >= target:
                return
            try:
                self.e._req("GET", f"/frame/wait/{min(target, now + 300)}", timeout=70)
            except Exception:
                raise Stop("the engine stopped answering during a frame wait")

    def press(self, button, device=None):
        body = {"button": button}
        if device:
            body["device"] = device
        self.e._req("POST", "/input/press", body=json.dumps(body).encode())

    def text(self, text):
        self.e._req("POST", "/input/text", body=json.dumps({"text": text}).encode())

    def confirm_to(self, screen, tries=8):
        """Confirm until `screen` is up.  With a body in view the image's
        shell takes the first press after a quiet spell to wake the pad (and
        the spell is --timeout-ms long here), so a step is: wake it if it is
        asleep, then confirm before it sleeps again."""
        for _ in range(tries):
            if self.e.screen() == screen:
                return
            if not self.state().get("controller_mode"):
                self.press("confirm")
                self.frames(10)
            self.press("confirm")
            self.frames(60)
            end = time.monotonic() + 6
            while time.monotonic() < end and self.e.screen() != screen:
                time.sleep(0.1)
        if self.e.screen() != screen:
            raise Stop(f"never reached {screen} (on {self.e.screen()})")

    def until_shown(self, want, seconds):
        """Game seconds until the browser's `shown` is `want`; -1 if it never was."""
        start = self.k.now()
        while self.k.now() - start < seconds:
            if self.state().get("shown") is want:
                return self.k.now() - start
            time.sleep(0.005)
        return -1.0


def sequence(r):
    k = r.k
    r.obs("arrive")

    # A person in view who presses nothing: 60 s of game time.
    wall = time.monotonic()
    r.frames(1800)
    r.val("idle_wall_seconds", f"{time.monotonic() - wall:.2f}")
    r.obs("idle60")
    r.press("down")
    r.frames(20)
    r.obs("after_down")
    r.press("up")
    r.frames(20)
    r.obs("after_up")
    r.press("down")
    r.frames(20)
    r.obs("control_down")

    # An arm that passes through the pose is not a hand held there.
    k.point(0, "right", 0.5)
    k.wait(0.25)
    k.set_pose(0, "stand")
    k.wait(1.5)
    r.obs("short_hand")

    # Held: the list's own.
    k.point(0, "right", 0.5)
    r.val("hand_seconds", f"{r.until_shown(False, 4.0):.3f}")
    k.wait(0.3)
    r.obs("hand")
    k.set_pose(0, "stand")
    k.wait(2.0)
    r.obs("hand_away")

    # A press: the browser's again, at once, and it stays.
    r.press("down")
    r.frames(6)
    r.obs("press")
    r.frames(300)
    r.obs("press_idle")

    # A typed character, with the arm still out.
    k.point(0, "right", 0.5)
    r.val("hand2_seconds", f"{r.until_shown(False, 4.0):.3f}")
    k.wait(0.3)
    r.obs("hand2")
    r.text("a")
    r.frames(6)
    r.obs("typed")
    k.wait(3.0)
    r.obs("typed_hold")

    # The arm has to go away and come back to count again.
    k.set_pose(0, "stand")
    k.wait(0.6)
    k.point(0, "right", 0.5)
    r.val("rearm_seconds", f"{r.until_shown(False, 4.0):.3f}")
    k.wait(0.3)
    r.obs("rearm")
    r.press("down")
    r.frames(6)
    k.wait(2.0)
    r.obs("press_hold")
    k.set_pose(0, "stand")


def run(a):
    os.makedirs(a.workdir, exist_ok=True)
    for d in ("cache", "saves"):
        os.makedirs(os.path.join(a.workdir, d), exist_ok=True)
    seed_all_tutorials_seen(os.path.join(a.workdir, "cache"))
    sock = os.path.join(a.workdir, "kinect.sock")
    k = SyntheticKinect(sock, people=1)
    k.start()
    env = dict(os.environ)
    for v in ("MILO_INPUT_SCRIPT", "DC3_SONG_BROWSER", "DC3_CLASSIC_SONG_SELECT",
              "DC3_CONTROLLER_MODE", "MILO_RENDER"):
        env.pop(v, None)
    env.update({
        "MILO_HEADLESS": "1", "MILO_NORENDER": "1", "MILO_FATAL_FAILS": "0",
        "DC3_SHOW_SPLASH": "0", "DC3_FAST_BOOT": "1", "DC3_DLC": "0", "DC3_FAST_TIME": "1",
        "MILO_MAX_FRAMES": "0",
        "DC3_HTTP_PACE_FPS": str(a.pace_fps),
        "DC3_CONTROLLER_MODE_TIMEOUT_MS": str(a.timeout_ms),
        "DC3_CACHE_DIR": os.path.join(a.workdir, "cache"),
        "DC3_SAVE_DIR": os.path.join(a.workdir, "saves"),
    })
    env.update(SyntheticKinect.engine_env(sock))
    env.update(engine_port_env(a.workdir, a.port))
    for kv in a.env:
        key, _, val = kv.partition("=")
        env[key] = val
    with open(os.path.join(a.workdir, "engine.log"), "w") as lf:
        proc = subprocess.Popen([os.path.abspath(a.engine)],
                                cwd=os.path.dirname(os.path.abspath(a.engine)),
                                env=env, stdout=lf, stderr=subprocess.STDOUT,
                                start_new_session=True)
    code = 1
    try:
        try:
            port = wait_for_port(port_file(a.workdir), proc, timeout=120)
        except EnginePortError as ex:
            raise Stop(str(ex))
        eng = Engine(f"http://127.0.0.1:{port}/api")
        end = time.monotonic() + 150
        while eng.screen() not in ("title_screen", "main_screen"):
            if proc.poll() is not None:
                raise Stop(f"the engine exited early ({proc.returncode})")
            if time.monotonic() > end:
                raise Stop(f"never reached the title (on {eng.screen()})")
            time.sleep(0.2)
        k.follow_engine(eng.base)
        r = Run(eng, k)
        r.frames(90)
        r.confirm_to("main_screen")
        r.frames(60)
        r.confirm_to("choose_mode_screen")
        r.frames(90)
        r.confirm_to("song_select_screen")
        r.frames(240)
        sequence(r)
        print("DONE", flush=True)
        code = 0
    except Stop as ex:
        print(f"STOPPED {ex}", flush=True)
    finally:
        k.stop()
        if proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
                proc.wait(timeout=10)
            except Exception:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except Exception:
                    pass
    return code


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--engine", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--pace-fps", type=int, default=240)
    ap.add_argument("--timeout-ms", type=int, default=1500)
    ap.add_argument("--env", action="append", default=[])
    sys.exit(run(ap.parse_args()))


if __name__ == "__main__":
    main()
