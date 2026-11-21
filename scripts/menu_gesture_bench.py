#!/usr/bin/env python3
"""menu_gesture_bench.py -- how reliably do the menus select, on a webcam? (PLAN P-3)

The native port drives DC3's own, unchanged menu-gesture detectors
(HandHeightGestureFilter, DirectionGestureFilterSingleUser / ArcDetector, in
HamNavList) from a webcam through MediaPipe instead of a Kinect.  A Kinect
delivers 30 skeletons a second with millimetre noise; a webcam in a dim room
delivers 8, with centimetres.  This measures what that costs, through the
real path -- pose socket -> native sensor -> SkeletonUpdate -> HamNavList --
and what a fix buys.

  THE PERFORMER IS SYNTHETIC.  People have played the game on real webcams,
  menus included; what nobody has recorded is a success RATE (attempts against
  selects), and `run` does not supply one for a person either: it measures the
  detectors on a modelled camera.  `replay` plays real recorded sessions back
  (see there).  The performer is a scripted arm
  (scripts/synthetic_kinect.py geometry) with a person's habits: it raises the
  arm over ~0.45 s, watches the highlight and nudges the hand until the row it
  wants is lit, hovers, sweeps the forearm across the body (or, in hold mode,
  keeps still), and tries again when nothing happens.  Sweep size, speed,
  hover time and aim error vary per trial from a seed, so two runs of the same
  cell make the same gestures.  The camera is modelled as: a pose every
  1/rate s of game time (the game runs 30 UI frames a second headless), with
  per-joint noise of a measured level:

    none    exact joints
    video   sigma 1.7 mm, hands z 3.9 mm    MediaPipe VIDEO mode on real
                                            footage (POSE_INPUT.md, "Our
                                            skeletons vs a Kinect's")
    photo   x 3.8 y 4.2 z 14 mm, hands z 17 a still photo + video noise, the
                                            middle of that table's range
    gpd     x 7.5 y 8 z 18 mm (elbows 10/10/20, wrists and hands 13/15/29),
            wrists Inferred 3.5% and hands 6.7% of frames
                                            frame-to-frame differences of a
                                            real session of the owner's on the
                                            GPD webcam at 7.5 frames/s
                                            (np-pose-rough, 2026-11-13).  An
                                            upper bound: it includes his real
                                            movement.
    gpdvis  gpd, and each wrist under the Tracked score for stretches of
            0.40 s on average with 0.23 s Tracked between; 23% of the low
            stretches are under the NotTracked score
                                            how a raised arm's wrist scores in
                                            the worse of two recorded GPD
                                            sessions (MENU_GESTURES.md, "The
                                            Tracked gate"): the worst case,
                                            for DC3_MENU_ARC_START.

  Noise is AR(1) with a 0.1 s correlation time (MediaPipe smooths its own
  landmarks), independent per joint and axis.

  The engine runs headless at 30 frames a second of wall time (--pace-fps 30),
  so its UI clock (1/30 s a frame) is real time.  That matters to anything
  that asks how fast the camera is: the pose fill (DC3_POSE_FILL_MENU, off
  by default outside a song; before this branch the song's extrap ran in the
  menus too) reads the camera's rate off its clock, which in the menus is
  the shell music's -- wall time.  Paced at 120 the same camera reads four
  times faster than it is, which is not what a player gets.

What a trial is, and what is counted (every rate is n / trials):
  select   "highlight row k, then select it": first_try (the first swipe
           selected the wanted row), ok (within 3 tries), wrong_row (a select
           landed on another row), false_select (a select before the performer
           swiped at all -- while raising the arm or aiming), none (3 tries,
           no select), aim_fail (the wanted row could not be highlighted in
           8 s).  Time-to-select is from the arm starting up to the select,
           for trials that ended ok.  Each sweep is also kept on its own
           (result.json "sweeps": taken or not, the most arc nodes and swipe
           amount the detector reached, and whether its had-progress latch
           was set when the hand set off).
  read     the arm held on a row for 5 s, drifting slowly, never swiping:
           false_select if anything selects; highlight_moved if the highlight
           leaves the row.  Then the arm comes down: a select in the 1.2 s
           after that is late_select, counted apart (the table's "late").
  idle     arms down for 5 s: any select or engagement is false.
  stray    a select that no trial was watching for (it fired between two
           trials): result.json "stray_selects", the table's "stray".  Until
           2026-11-20 the first look of the next trial dropped these and the
           late ones: the engine's own log of 59 runs held 1 to 3 of them for
           every 100 reads and scans, on the code before this branch as on
           the code after.

Lists: main (the main menu), diff (the difficulty list), songs (song select:
a row BELOW the 5th, which needs the list scrolled), back (the help bar's
Back, left hand).

Usage:
  scripts/menu_gesture_bench.py run --out DIR [--engine native/build/dc3-native]
      [--lists main,diff,songs,back] [--rates 30,15,8] [--noise none,photo,gpd]
      [--trials 30] [--reads 10] [--scans 0] [--idles 2] [--seed 0]
      [--select swipe|hold] [--jobs 6] [--env KEY=VALUE]...
          the matrix: one engine per (list, rate, noise).  --select hold: the
          performer keeps still on the row instead of sweeping (pass
          --env DC3_MENU_SELECT=hold with it).  --scans: 5 s of reading down
          the list row by row (0.6-1.1 s a row), counted like a read.
          --seed N: another draw of the noise and the performer's habits.
  scripts/menu_gesture_bench.py table DIR[+DIR2...] [BASELINE_DIR[+...]]
          DIR1+DIR2: runs of the same cells with different seeds, added up.
  scripts/menu_gesture_bench.py check --out DIR
          DC3_MENU_SELECT end to end on one engine: 17 PASS/FAIL lines, exit 1
          on a FAIL (native/tests/test_native_gesture_menus.cpp runs it).
  scripts/menu_gesture_bench.py replay --out DIR [--session FILE]...
      [--window FILE:FROM:TO]... [--screen main_screen] [--env KEY=VALUE]...
      [--trace]
          real recordings (a playtest's pose.dc3session): the footage outside
          songs of each --session, and/or recorded wall seconds FROM..TO,
          played by wall time in front of one screen; counts what the lists
          did.  Nobody in that footage was aiming at THIS screen's rows: a
          select count here is "how often does this footage set a list off",
          comparable between builds and modes, not a success rate.
  scripts/menu_gesture_bench.py replay-table DIR...

Exit: 0 ran; 1 (check) a check failed; 2 setup error (an engine did not come
up, a screen was not reached).
"""
import argparse
import http.client
import json
import math
import os
import random
import signal
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import synthetic_kinect as sk  # noqa: E402
from synthetic_kinect import SyntheticKinect  # noqa: E402
from engine_http_port import EnginePortError, engine_port_env, port_file, wait_for_port  # noqa: E402
from gesture_menu_flow import Engine, Flow, FlowError, seed_all_tutorials_seen  # noqa: E402

FRAME_S = 1.0 / 30.0     # headless UI time per engine frame

# ---- the camera ------------------------------------------------------------------
# sigma in metres (x, y, z): body joints, elbows, wrists+hands; then the chance
# per frame that a wrist / a hand is reported Inferred instead of Tracked.
NOISE = {
    "none": None,
    "video": dict(body=(0.0017, 0.0017, 0.0017), elbow=(0.0017, 0.0017, 0.0017),
                  hand=(0.0017, 0.0017, 0.0039), wrist_inferred=0.0, hand_inferred=0.0),
    "photo": dict(body=(0.0038, 0.0042, 0.014), elbow=(0.0038, 0.0042, 0.014),
                  hand=(0.0038, 0.0042, 0.017), wrist_inferred=0.0, hand_inferred=0.0),
    "gpd": dict(body=(0.0075, 0.008, 0.018), elbow=(0.010, 0.010, 0.020),
                hand=(0.013, 0.015, 0.029), wrist_inferred=0.035, hand_inferred=0.067),
    "gpdvis": dict(body=(0.0075, 0.008, 0.018), elbow=(0.010, 0.010, 0.020),
                   hand=(0.013, 0.015, 0.029), wrist_inferred=0.035, hand_inferred=0.067,
                   arm_low=(0.40, 0.23, 0.23)),
}
NOISE_TAU_S = 0.1
HAND_JOINTS = (sk.WRIST_L, sk.HAND_L, sk.WRIST_R, sk.HAND_R)
ELBOW_JOINTS = (sk.ELBOW_L, sk.ELBOW_R)
INFERRED_SCORE = 0.45    # Skeleton_Native.cpp ConfidenceFromScore: 0.3 .. 0.6 is Inferred
NOT_TRACKED_SCORE = 0.2  # under 0.3


def min_jerk(u):
    u = 0.0 if u < 0.0 else 1.0 if u > 1.0 else u
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


class Track:
    """One number that moves to each new target along a minimum-jerk curve."""

    def __init__(self, value):
        self.v0 = self.v1 = value
        self.t0 = 0.0
        self.dur = 0.0

    def at(self, t):
        if self.dur <= 0.0 or t >= self.t0 + self.dur:
            return self.v1
        return self.v0 + (self.v1 - self.v0) * min_jerk((t - self.t0) / self.dur)

    def go(self, t, value, dur):
        self.v0 = self.at(t)
        self.v1 = value
        self.t0 = t
        self.dur = dur


class Arm:
    """out 0 = hanging at the side, 1 = the menu arm (synthetic_kinect.menu_arm);
    height = the menu hand height; angle = the forearm's turn round the elbow
    (0 out to the side, 90 at the camera); tilt = the forearm's rise in degrees."""

    def __init__(self):
        self.out = Track(0.0)
        self.height = Track(0.5)
        self.angle = Track(0.0)
        self.tilt = Track(0.0)


class BenchKinect(SyntheticKinect):
    """The synthetic sensor as a camera of a given frame rate and noise."""

    def __init__(self, path, depth=2.5):
        super().__init__(path, people=1, depth=depth)
        self.rate = 30.0
        self.noise = None
        self.rng = random.Random(1)
        self.arms = {"right": Arm(), "left": Arm()}
        self.noise_state = [[0.0, 0.0, 0.0] for _ in range(sk.NUM_JOINTS)]
        self.noise_t = None
        self.vis_rng = random.Random(2)
        self.vis = {}            # arm -> [score of its wrist now, engine time it changes at]
        self.packets = 0
        self.absent = False
        self.hands_up = False    # both hands above the head (HandsUpGestureFilter)

    def configure(self, rate, noise, seed):
        with self.lock:
            self.rate = float(rate)
            self.noise = NOISE[noise]
            self.rng = random.Random(seed)
            self.noise_state = [[0.0, 0.0, 0.0] for _ in range(sk.NUM_JOINTS)]
            self.noise_t = None
            # its own stream, so gpd and gpdvis of one seed have the same joints
            self.vis_rng = random.Random(seed ^ 0x5EED)
            self.vis = {}

    # -- geometry
    def _clean(self, t):
        ox = self.offsets[0]
        pts = {j: (x, y, 0.0) for j, (x, y) in sk.STAND.items()}
        for side, arm in self.arms.items():
            out = arm.out.at(t)
            if out <= 0.0:
                continue
            tilt = math.radians(arm.tilt.at(t))
            target = sk.menu_arm(side, arm.height.at(t), arm.angle.at(t))
            elbow = sk.ELBOW_R if side == "right" else sk.ELBOW_L
            ex, ey, ez = target[elbow]
            for j, (x, y, z) in target.items():
                if j != elbow and tilt != 0.0:
                    # the forearm rises by `tilt` about the elbow
                    r = math.hypot(x - ex, z - ez)
                    c = math.cos(tilt)
                    x, z = ex + (x - ex) * c, ez + (z - ez) * c
                    y = y + r * math.sin(tilt)
                sx, sy, sz = pts[j]
                pts[j] = (sx + (x - sx) * out, sy + (y - sy) * out, sz + (z - sz) * out)
        if self.hands_up:
            for j, (x, y) in list(sk.RAISE_RIGHT.items()) + list(sk.RAISE_LEFT.items()):
                pts[j] = (x, y, 0.0)
        return [(pts[j][0] + ox, pts[j][1], self.depth + pts[j][2]) for j in range(sk.NUM_JOINTS)]

    def _joints(self, i, pose):
        t = self.now()
        joints = self._clean(t)
        n = self.noise
        if n is None:
            return [(x, y, z, 1.0) for x, y, z in joints]
        dt = (t - self.noise_t) if self.noise_t is not None else None
        self.noise_t = t
        rho = 0.0 if dt is None or dt <= 0.0 else math.exp(-dt / NOISE_TAU_S)
        drive = math.sqrt(max(0.0, 1.0 - rho * rho))
        out = []
        for j, (x, y, z) in enumerate(joints):
            sig = n["hand"] if j in HAND_JOINTS else n["elbow"] if j in ELBOW_JOINTS else n["body"]
            st = self.noise_state[j]
            for a in range(3):
                st[a] = rho * st[a] + drive * self.rng.gauss(0.0, sig[a])
            c = 1.0
            if j in (sk.WRIST_L, sk.WRIST_R) and self.rng.random() < n["wrist_inferred"]:
                c = INFERRED_SCORE
            if j in (sk.HAND_L, sk.HAND_R) and self.rng.random() < n["hand_inferred"]:
                c = INFERRED_SCORE
            out.append((x + st[0], y + st[1], z + st[2], c))
        low = n.get("arm_low")
        if low:
            # stretches of an arm's wrist under the Tracked score: mean `low[0]`
            # s of it, mean `low[1]` s Tracked between; a share `low[2]` of the
            # stretches is under the NotTracked score instead
            for arm, wrist in (("left", sk.WRIST_L), ("right", sk.WRIST_R)):
                v = self.vis.get(arm)
                if v is None:
                    v = self.vis[arm] = [1.0, t + self.vis_rng.expovariate(1.0 / low[1])]
                while t >= v[1]:
                    if v[0] >= 1.0:
                        v[0] = NOT_TRACKED_SCORE if self.vis_rng.random() < low[2] else INFERRED_SCORE
                        v[1] += self.vis_rng.expovariate(1.0 / low[0])
                    else:
                        v[0] = 1.0
                        v[1] += self.vis_rng.expovariate(1.0 / low[1])
                if v[0] < 1.0:
                    x, y, z, c = out[wrist]
                    out[wrist] = (x, y, z, min(c, v[0]))
        return out

    # -- the camera's frame clock: one packet every 1/rate s of engine time
    def _run(self):
        import socket
        client = None
        while not self.stop_evt.is_set() and client is None:
            try:
                client, _ = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                return
        if client is None:
            return
        self.connected = True
        frame_id = 0
        due = None
        while not self.stop_evt.is_set():
            if self.engine_t is None:
                # before follow_engine (the engine is booting): a 30 Hz wall-clock camera
                self.send_evt.wait(1.0 / 30.0)
                self.send_evt.clear()
                if self.engine_t is not None:
                    continue
            else:
                got = self.send_evt.wait(0.2)
                self.send_evt.clear()
                if not got:
                    continue      # a stalled engine (a load) gets no frames
                t = self.engine_t
                if due is not None and t + 1e-6 < due:
                    continue
                period = 1.0 / self.rate
                due = t + period if due is None or t - due > period else due + period
            try:
                client.sendall(self._packet(frame_id))
            except OSError:
                break
            frame_id += 1
            self.frames_sent = frame_id
            self.packets += 1
        try:
            client.close()
        except OSError:
            pass

    def _packet(self, frame_id):
        if self.absent:
            with self.lock:
                self.poses[0] = "absent"
        else:
            with self.lock:
                self.poses[0] = "bench"
        return super()._packet(frame_id)


# ---- the engine ----------------------------------------------------------------
class Bench:
    def __init__(self, a, workdir, log):
        self.a = a
        self.workdir = workdir
        self.log = log
        self.proc = None
        self.k = None
        self.e = None
        self.since = 0

    def note(self, msg):
        self.log.write(msg + "\n")
        self.log.flush()

    def start(self, extra_env, session=None):
        """`session`: a dc3-pose-session file the engine replays by wall time
        (a real recording) instead of the synthetic sensor."""
        os.makedirs(self.workdir, exist_ok=True)
        for d in ("cache", "saves"):
            os.makedirs(os.path.join(self.workdir, d), exist_ok=True)
        seed_all_tutorials_seen(os.path.join(self.workdir, "cache"))
        if session is None:
            sock = os.path.join(self.workdir, "kinect.sock")
            self.k = BenchKinect(sock)
            self.k.start()
            pose_env = SyntheticKinect.engine_env(sock)
        else:
            pose_env = {"DC3_POSE": "external", "DC3_POSE_NO_SPAWN": "1",
                        "DC3_POSE_PERFORM": "session:" + os.path.abspath(session),
                        "DC3_POSE_SESSION_SYNC": "wall"}
        env = dict(os.environ)
        for v in ("MILO_INPUT_SCRIPT", "DC3_FAST_BOOT", "MILO_RENDER", "DC3_MENU_SELECT"):
            env.pop(v, None)
        env.update({
            "MILO_HEADLESS": "1", "MILO_FATAL_FAILS": "0", "DC3_SHOW_SPLASH": "0",
            "DC3_FAST_TIME": "1", "MILO_MAX_FRAMES": "0", "MILO_NORENDER": "1",
            "DC3_HTTP_PACE_FPS": str(self.a.pace_fps),
            "DC3_CACHE_DIR": os.path.join(self.workdir, "cache"),
            "DC3_SAVE_DIR": os.path.join(self.workdir, "saves"),
        })
        for v in ("DC3_POSE_SOCKET", "DC3_POSE_PERFORM", "DC3_POSE_SESSION_SYNC", "DC3_POSE_RECORD"):
            env.pop(v, None)
        env.update(pose_env)
        env.update(engine_port_env(self.workdir, 0))
        for kv in extra_env:
            key, _, val = kv.partition("=")
            env[key] = val
        engine = os.path.abspath(self.a.engine)
        with open(os.path.join(self.workdir, "engine.log"), "w") as lf:
            self.proc = subprocess.Popen([engine], cwd=os.path.dirname(engine), env=env,
                                         stdout=lf, stderr=subprocess.STDOUT,
                                         start_new_session=True)
        self.t_spawn = time.monotonic()
        try:
            port = wait_for_port(port_file(self.workdir), self.proc, timeout=120)
        except EnginePortError as ex:
            raise FlowError(str(ex))
        self.e = Engine(f"http://127.0.0.1:{port}/api")
        end = time.monotonic() + 120
        while self.e.screen() is None:
            if self.proc.poll() is not None:
                raise FlowError(f"engine exited early ({self.proc.returncode})")
            if time.monotonic() > end:
                raise FlowError("engine HTTP server never answered")
            time.sleep(0.2)
        if self.k is not None:
            self.k.follow_engine(self.e.base)

    def stop(self):
        if self.proc is not None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except Exception:
                pass
            try:
                self.proc.wait(10)
            except Exception:
                try:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                except Exception:
                    pass
        if self.k is not None:
            self.k.stop()

    # -- reading the screen
    def state(self, reset=False, rows=False):
        q = f"/menu/state?since={self.since}"
        if reset:
            q += "&reset=1"
        if rows:
            q += "&rows=1"
        why = "no answer"
        t_end = time.time() + 2.0         # three tries and 2 s of patience, then the reason
        attempt = 0
        while attempt < 3 or time.time() < t_end:
            attempt += 1
            try:
                d = self.e._req("GET", q)
                if d.get("ok"):
                    return d["data"]
                why = "answered " + json.dumps(d)[:200]
            except Exception as ex:
                why = f"{type(ex).__name__}: {ex}"
                if isinstance(ex, ValueError):
                    why += " | " + self._raw_state(q)
            time.sleep(0.05)
        raise FlowError("the engine stopped answering /api/menu/state (" + why + ")")

    def _raw_state(self, q):
        """What the engine sent when its answer was not JSON (a nan in it?)."""
        try:
            c = http.client.HTTPConnection(self.e.host, self.e.port, timeout=10)
            try:
                c.request("GET", self.e.root + q)
                body = c.getresponse().read().decode("utf-8", "replace")
            finally:
                c.close()
            i = max(body.find("nan"), body.find("inf"))
            return body[max(0, i - 160):i + 80] if i >= 0 else body[:240]
        except Exception as ex:
            return f"(raw read failed: {ex})"

    def list_state(self, st, hand, name=None):
        best = None
        for l in st["lists"]:
            if l["hand"] != hand or not l["enabled"]:
                continue
            if name and l["name"] != name:
                continue
            if best is None or l["num_data"] > best["num_data"]:
                best = l
        return best

    def take_events(self, st):
        ev = st["events"]
        if ev:
            self.since = max(e["seq"] for e in ev)
        return ev

    def wait(self, seconds):
        if self.k is None:
            time.sleep(seconds)      # a replay runs at pace 30: UI time is wall time
        else:
            self.k.wait(seconds)

    def wait_screen(self, names, timeout=60.0, settle=1.0):
        if isinstance(names, str):
            names = [names]
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            s = self.e.screen()
            if s in names:
                self.wait(settle)
                return s
            time.sleep(0.01)
        raise FlowError(f"timed out waiting for {names}; screen is {self.e.screen()}")


# ---- the performer ---------------------------------------------------------------
# What each list is: its screen, the HamNavList, the hand, the rows to select,
# and a screen to leave through when a select does not leave by itself.
LISTS = {
    "main": dict(screen="main_screen", name="right_hand.hnl", hand="right",
                 targets=["gameplay", "campaign_mode", "fitness_mode", "challenges", "store"]),
    "diff": dict(screen="multiuser_screen", name="right_hand_p1.hnl", hand="right",
                 targets=["beginner", "easy", "medium", "expert"], via="main_screen"),
    # rows 8..19 of the song list: none is among the five showing on entry
    "songs": dict(screen="song_select_screen", name="right_hand.hnl", hand="right",
                  targets=["betteroffalone", "thehustle", "starships", "venus", "lalaland"],
                  home=(2, 2)),
    "back": dict(screen="song_select_screen", name="left_hand.hnl", hand="left",
                 targets=["screen_name_mode_select"]),
}
NUM_SELECTABLE = 5       # HamListRibbon::sNumListSelectable
TICK_S = 0.1             # the performer looks at the screen this often
REACTION_TICKS = 2       # ... and acts on what it saw this many looks ago (0.2 s)
MAX_TRIES = 3
AIM_TIMEOUT_S = 8.0
SCROLL_TIMEOUT_S = 25.0
SELECT_WAIT_S = 1.0      # after a swipe: this long for the select to show
HOLD_WAIT_S = 4.0        # hold mode: keep still this long before giving up


def seed_for(*parts):
    import zlib
    if parts and parts[-1] == 0 and isinstance(parts[-1], int) and len(parts) > 2:
        parts = parts[:-1]      # --seed 0: the seeds every published table used
    return zlib.crc32(":".join(str(p) for p in parts).encode())


class Performer:
    def __init__(self, bench, spec, select_mode):
        self.b = bench
        self.k = bench.k
        self.spec = spec
        self.hand = spec["hand"]
        self.mode = select_mode
        self.rows = None
        self.stray = []                 # selects nobody was watching for

    # -- helpers
    def first_look(self):
        """A trial's first look.  A select waiting here fired between two
        trials: it is kept (stray), and since it may have moved the screen on
        the list is put back before the trial starts."""
        st, l = self.look(reset=True)
        ev = self.b.take_events(st)
        if ev:
            self.stray.extend(ev)
            self.to_screen(force=True)
            st, l = self.look(reset=True)
            self.b.take_events(st)
        return st, l

    def look(self, reset=False):
        st = self.b.state(reset=reset, rows=self.rows is None)
        l = self.b.list_state(st, self.hand, self.spec["name"])
        if l is not None and self.rows is None and "rows" in l:
            self.rows = l["rows"]
            self.flags = l["row_flags"]
        return st, l

    def arms_down(self, wait=0.7):
        t = self.k.now()
        for arm in self.k.arms.values():
            arm.out.go(t, 0.0, 0.3)
        self.b.wait(0.3)
        t = self.k.now()
        for arm in self.k.arms.values():
            arm.angle.go(t, 0.0, 0.0)
            arm.tilt.go(t, 0.0, 0.0)
        self.b.wait(wait)

    def aim(self, l, want):
        """('point', height) for the hand height that highlights row `want`,
        ('down'|'up', height) when the list has to scroll first."""
        if l["scrollable"]:
            first = l["first_showing"]
            if want < first:
                return "up", -0.02
            if want > first + NUM_SELECTABLE - 1:
                return "down", 1.03
            n = l["num_items"]
            pos = (want - first) + (0 if l["at_top"] else 1)
            return "point", pos / float(n - 1) if n > 1 else 0.5
        n = l["num_items"]
        k = sum(1 for i in range(want) if not (self.flags[i] & 2))
        return "point", (k / float(n - 1)) if n > 1 else 0.5

    def to_screen(self, force=False):
        """Back to the list's screen (setup, not measured: ui goto_screen)."""
        e = self.b.e
        screen = self.spec["screen"]
        end = time.monotonic() + 60
        self.b.wait(0.3)
        while e.eval("{ui in_transition}") not in (0, False, "0"):
            if time.monotonic() > end:
                raise FlowError("a transition never ended")
            self.b.wait(0.1)
        cur = e.screen()
        if cur == screen and not force:
            self.settle()
            return
        if cur == screen:
            via = self.spec.get("via", "main_screen")
            e.eval("{ui goto_screen %s}" % via)
            self.b.wait_screen(via, timeout=30, settle=0.5)
            while e.eval("{ui in_transition}") not in (0, False, "0"):
                if time.monotonic() > end:
                    raise FlowError("a transition never ended")
                self.b.wait(0.1)
        e.eval("{ui goto_screen %s}" % screen)
        self.b.wait_screen(screen, timeout=30, settle=1.5)
        self.settle()

    def settle(self):
        """The list as a first visit shows it (song select comes back on the
        song last chosen: every trial has to scroll from the same place)."""
        e = self.b.e
        st, l = self.look()
        if l is None:
            self.b.wait(1.0)
            st, l = self.look()
        if l is None:
            raise FlowError(f"{self.spec['screen']} has no enabled {self.hand}-hand list "
                            f"{self.spec['name']}")
        home = self.spec.get("home")
        if home is not None and (l["first_showing"], l["selected"]) != tuple(home):
            e.eval("{{{{ui focus_panel} loaded_dir} find %s} scroll_to_index %d %d}"
                   % (self.spec["name"], home[1], home[0]))
            self.b.wait(0.8)
            st, l = self.look()
            if l is None or l["first_showing"] != home[0]:
                raise FlowError(f"could not put {self.spec['name']} back at row {home[0]}: "
                                f"{l and l['first_showing']}")
        self.b.take_events(st)

    # -- one "highlight this row, then select it"
    def select_trial(self, target, rng):
        k, hand = self.k, self.hand
        arm = k.arms[hand]
        st, l = self.first_look()
        if l is None:
            raise FlowError("the list under test is not on screen")
        if self.rows is None:
            self.b.wait(0.1)
            st, l = self.look()
        want = self.rows.index(target)
        t0 = st["ui_seconds"]
        res = dict(target=target, outcome=None, tries=0, t_select=None, events=[],
                   max_swipe_amt=0.0, max_nodes=0, selected_sym=None, early=None)
        # this person's habits for this trial
        aim_err = rng.gauss(0.0, 0.02)
        start_angle = rng.uniform(-5.0, 10.0)
        for attempt in range(MAX_TRIES):
            res["tries"] = attempt + 1
            sweep = rng.uniform(85.0, 120.0)
            dur = rng.uniform(0.25, 0.45)
            hover = rng.uniform(0.2, 0.8)
            tilt_end = rng.uniform(-8.0, 8.0)
            kind, h = self.aim(l, want)
            now = k.now()
            arm.height.go(now, h + aim_err, 0.0)
            arm.angle.go(now, start_angle, 0.0)
            arm.tilt.go(now, 0.0, 0.0)
            arm.out.go(now, 1.0, 0.45)
            # aim: watch the highlight, move the hand until the row is lit
            seen = []
            lit_since = None
            nudge = 0.0
            last_move = now + 0.45      # when the hand last came to rest
            aim_end = now + AIM_TIMEOUT_S
            scroll_end = now + SCROLL_TIMEOUT_S
            fired = None
            while True:
                self.b.wait(TICK_S)
                st, l = self.look()
                ev = self.b.take_events(st)
                if ev:
                    fired = ev
                    break
                if l is None:
                    res["outcome"] = "lost_list"
                    return res
                seen.append(l)
                old = seen[-1 - REACTION_TICKS] if len(seen) > REACTION_TICKS else None
                now = k.now()
                if old is None:
                    continue
                kind, h = self.aim(old, want)
                if kind != "point":
                    # hold the hand in the scroll zone until the row shows
                    if abs(arm.height.v1 - h) > 1e-6:
                        arm.height.go(now, h, 0.25)
                        last_move = now + 0.25
                    lit_since = None
                    nudge = 0.0
                    aim_end = now + AIM_TIMEOUT_S
                    if now > scroll_end:
                        res["outcome"] = "aim_fail"
                        break
                    continue
                goal = h + aim_err + nudge
                if abs(arm.height.v1 - goal) > 1e-6:
                    arm.height.go(now, goal, 0.25)
                    last_move = now + 0.25
                if (old["sym"] == target and old["mode"] != 3 and not old["is_scrolling"]
                        and old["scroll_dir"] == 0):
                    if lit_since is None:
                        lit_since = now
                    if now - lit_since >= hover:
                        break
                else:
                    lit_since = None
                    if now - last_move > 0.6 and old["mode"] != 3:
                        # the hand has rested 0.6 s and the wrong row is lit:
                        # move half a row towards the wanted one
                        n = max(2, old["num_items"])
                        step = 0.5 / (n - 1)
                        nudge += step if old["selected"] < want else -step
                        nudge = max(-2.0 * step, min(2.0 * step, nudge))
                if now > aim_end:
                    res["outcome"] = "aim_fail"
                    break
            if res["outcome"] == "aim_fail":
                self.arms_down(0.3)
                return res
            if fired is None and self.mode == "swipe":
                # the swipe
                st, l2 = self.look(reset=True)
                if l2 is not None:
                    res["at_swipe"] = dict(sym=l2["sym"], first_showing=l2["first_showing"],
                                           mode=l2["mode"], scroll_dir=l2["scroll_dir"],
                                           hand_height=l2["hand_height"], hover_ms=l2["hover_ms"])
                # one entry a sweep: was the arc detector refusing to start one
                # (ArcDetector::mHadProgress) when the hand set off, and what
                # the detector made of it
                this_sweep = dict(latched=bool(l2 and l2.get("had_progress")), nodes=0, amt=0.0,
                                  taken=False)
                res.setdefault("sweeps", []).append(this_sweep)
                now = k.now()
                arm.angle.go(now, start_angle + sweep, dur)
                arm.tilt.go(now, tilt_end, dur)
                end = now + dur + sk.SWIPE_HOLD_S
                dropped = False
                while k.now() < end + SELECT_WAIT_S:
                    self.b.wait(TICK_S / 2)
                    st, l2 = self.look()
                    if l2 is not None:
                        res["max_swipe_amt"] = max(res["max_swipe_amt"], l2["max_swipe_amt"])
                        res["max_nodes"] = max(res["max_nodes"], l2["max_nodes"])
                        this_sweep["amt"] = max(this_sweep["amt"], l2["max_swipe_amt"])
                        this_sweep["nodes"] = max(this_sweep["nodes"], l2["max_nodes"])
                    ev = self.b.take_events(st)
                    if ev:
                        fired = ev
                        res["early"] = False
                        this_sweep["taken"] = True
                        break
                    if not dropped and k.now() >= end:
                        arm.out.go(k.now(), 0.0, 0.35)
                        dropped = True
            elif fired is None:
                # hold mode: keep the hand where it is
                end = k.now() + HOLD_WAIT_S
                while k.now() < end:
                    self.b.wait(TICK_S)
                    st, l2 = self.look()
                    ev = self.b.take_events(st)
                    if ev:
                        fired = ev
                        res["early"] = False
                        break
            else:
                res["early"] = True
            if fired:
                e0 = fired[0]
                res["events"] = fired
                res["selected_sym"] = e0["sym"]
                res["source"] = e0.get("source")
                res["t_select"] = e0["ui_seconds"] - t0
                if e0["hand"] != hand or e0["list"] != self.spec["name"]:
                    res["outcome"] = "false_select"       # the other hand's list
                elif res["early"] and self.mode == "swipe":
                    res["outcome"] = "false_select"       # before any swipe
                elif e0["sym"] == target:
                    res["outcome"] = "ok"
                else:
                    res["outcome"] = "wrong_row"
                self.arms_down(0.2)
                return res
            self.arms_down(0.5)
            st, l = self.look()
            if l is None:
                res["outcome"] = "lost_list"
                return res
        res["outcome"] = "none"
        return res

    # -- the arm on a row for 5 s, never swiping
    def read_trial(self, target, rng, seconds=5.0, scan=False):
        k, hand = self.k, self.hand
        arm = k.arms[hand]
        st, l = self.first_look()
        if l is None:
            raise FlowError("the list under test is not on screen")
        want = self.rows.index(target)
        res = dict(target=target, outcome="clean", highlight_moved=False, events=[],
                   max_swipe_amt=0.0, engaged=False, late_events=[])
        kind, h = self.aim(l, want)
        if kind != "point":
            res["outcome"] = "skipped"
            return res
        now = k.now()
        arm.height.go(now, h, 0.0)
        arm.angle.go(now, rng.uniform(-5.0, 10.0), 0.0)
        arm.tilt.go(now, 0.0, 0.0)
        arm.out.go(now, 1.0, 0.45)
        self.b.wait(1.0)
        st, l = self.look(reset=True)
        ev = self.b.take_events(st)
        lit = l is not None and l["sym"] == target and l["mode"] != 3
        res["engaged"] = lit
        n = max(2, l["num_items"]) if l else 5
        spacing = 1.0 / (n - 1)
        end = k.now() + seconds
        next_move = 0.0
        scan_row = int(round(h / spacing))
        scan_dir = 1 if rng.random() < 0.5 else -1
        if scan:
            # the first row has been lit for about 0.6 s by now (the arm took
            # 0.45 s to come up, then the 1.0 s wait above): the same 0.6 ..
            # 1.1 s as every other row.  (With 0.3 .. 0.8 s more here the
            # first row got up to 1.6 s, and a 1.5 s hold selected it in 3 of
            # 8 scans -- the trial's fault, not a row-to-row reading's.)
            next_move = k.now() + rng.uniform(0.0, 0.5)
        trace = []                      # the last 4 s, kept only with a select
        while not ev and k.now() < end:
            now = k.now()
            if now >= next_move and scan:
                # reading down (and back up) the list: on to a neighbouring row
                # in 0.3 s, 0.6 .. 1.1 s on it, the forearm still wandering
                scan_row += scan_dir
                if not 0 <= scan_row <= n - 1:
                    scan_dir = -scan_dir
                    scan_row += 2 * scan_dir
                arm.height.go(now, min(1.0, max(0.0, scan_row * spacing)), 0.3)
                d = 0.3 + rng.uniform(0.6, 1.1)
                arm.angle.go(now, rng.uniform(-8.0, 14.0), d)
                arm.tilt.go(now, rng.uniform(-5.0, 5.0), d)
                next_move = now + d
            elif now >= next_move:
                # a slow wander: a quarter of a row up or down, the forearm a
                # few degrees in or out, over 0.8 .. 1.6 s
                d = rng.uniform(0.8, 1.6)
                arm.height.go(now, h + rng.uniform(-0.25, 0.25) * spacing, d)
                arm.angle.go(now, rng.uniform(-8.0, 14.0), d)
                arm.tilt.go(now, rng.uniform(-5.0, 5.0), d)
                next_move = now + d
            self.b.wait(TICK_S)
            st, l = self.look()
            ev = self.b.take_events(st)
            if l is not None:
                res["max_swipe_amt"] = max(res["max_swipe_amt"], l["max_swipe_amt"])
                if lit and (l["sym"] != target or l["mode"] == 3):
                    res["highlight_moved"] = True
                trace.append([round(st["ui_seconds"], 2), l["sym"], l["selected"],
                              l["first_showing"], l["mode"], l["is_scrolling"], l["scroll_dir"],
                              round(l["hand_height"], 3), l.get("hold_progress", 0.0)])
                del trace[:-120]
        if ev:
            res["events"] = ev
            res["outcome"] = "false_select"
            # [ui s, row, selected, first showing, ribbon mode, scrolling, scroll
            # dir, hand height, hold progress] per look, for the reader
            res["trace"] = trace
        self.arms_down(0.3)
        if not ev:
            # the arm coming down is part of the read: a select it sets off
            # (or one that lands just after) is this trial's, not nobody's
            self.b.wait(0.6)
            st, l = self.look()
            late = self.b.take_events(st)
            if late:
                res["late_events"] = late
                res["outcome"] = "late_select"
        return res

    # -- arms down
    def idle_trial(self, seconds=5.0):
        self.first_look()
        res = dict(outcome="clean", events=[], engaged=False)
        end = self.k.now() + seconds
        while self.k.now() < end:
            self.b.wait(TICK_S)
            st = self.b.state()
            ev = self.b.take_events(st)
            for l in st["lists"]:
                if l["mode"] != 3 and l["enabled"]:
                    res["engaged"] = True
            if ev:
                res["events"] = ev
                res["outcome"] = "false_select"
                break
        return res


def run_cell(a):
    """One (list, rate, noise) on one engine.  Writes OUT/result.json."""
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "bench.log"), "w")
    spec = LISTS[a.list]
    result = dict(list=a.list, rate=a.rate, noise=a.noise, select=a.select, env=a.env,
                  seed=a.seed, pace_fps=a.pace_fps, trials=[], reads=[], idles=[], scans=[],
                  error=None)
    b = Bench(a, a.out, log)

    def on_term(signum, frame):
        raise SystemExit(143)
    signal.signal(signal.SIGTERM, on_term)
    try:
        b.start(a.env)
        b.wait_screen("title_screen", timeout=180, settle=1.5)
        p = Performer(b, spec, a.select)
        p.to_screen(force=True)
        b.k.configure(a.rate, a.noise, seed_for("cam", a.list, a.rate, a.noise, a.seed))
        b.wait(1.5)
        # the camera as the list sees it: filter updates per poll over 3 s
        p.look(reset=True)
        b.wait(3.0)
        st, l = p.look()
        result["updates_per_poll"] = (l["sensor_updates"] / float(l["polls"])
                                      if l and l["polls"] else None)
        fill0 = st["fill"]
        arc0 = st.get("arc_start")
        for i in range(a.idles):
            r = p.idle_trial()
            result["idles"].append(r)
            b.note(f"idle {i}: {r['outcome']} engaged={r['engaged']}")
            if r["events"]:
                p.to_screen(force=True)
        for i in range(a.trials):
            target = spec["targets"][i % len(spec["targets"])]
            r = p.select_trial(target, random.Random(seed_for("select", a.list, i, a.seed)))
            result["trials"].append(r)
            b.note(f"select {i} {target}: {r['outcome']} tries={r['tries']} "
                   f"t={r['t_select']} sym={r['selected_sym']} amt={r['max_swipe_amt']:.2f} "
                   f"nodes={r['max_nodes']}")
            p.to_screen(force=bool(r["events"]))
        for i in range(a.reads):
            target = spec["targets"][i % len(spec["targets"])]
            if a.list == "songs":
                # reading is done on the rows showing on entry (3..6; 2 is a header)
                target = p.rows[3 + i % 4]
            r = p.read_trial(target, random.Random(seed_for("read", a.list, i, a.seed)))
            result["reads"].append(r)
            b.note(f"read {i} {target}: {r['outcome']} moved={r['highlight_moved']} "
                   f"amt={r['max_swipe_amt']:.2f}")
            p.to_screen(force=bool(r["events"] or r["late_events"]))
        st1 = b.state()
        fill1 = st1["fill"]
        if arc0 and st1.get("arc_start"):
            # per arm: frames the arc detector had no path on, those the image's
            # gate (wrist and elbow Tracked) refused, those started anyway
            result["arc_start"] = {k: [x - y for x, y in zip(st1["arc_start"][k], arc0[k])]
                                   for k in ("left", "right")}
        fills, reals = fill1["fills"] - fill0["fills"], fill1["reals"] - fill0["reals"]
        result["fill"] = dict(mode=fill1["mode"], engaged_at_end=fill1["engaged"], fills=fills,
                              reals=reals,
                              clock_carried=fill1.get("clock_carried", 0.0) - fill0.get("clock_carried", 0.0))
        for i in range(a.scans):
            target = spec["targets"][i % len(spec["targets"])]
            if a.list == "songs":
                target = p.rows[3 + i % 4]
            r = p.read_trial(target, random.Random(seed_for("scan", a.list, i, a.seed)), scan=True)
            result["scans"].append(r)
            b.note(f"scan {i} from {target}: {r['outcome']}")
            p.to_screen(force=bool(r["events"] or r["late_events"]))
        # whatever fired after the last trial
        b.wait(0.5)
        p.stray.extend(b.take_events(b.state()))
        result["stray_selects"] = p.stray
        if p.stray:
            b.note("stray selects: " + " ".join("%s/%s" % (e["list"], e["sym"]) for e in p.stray))
    except FlowError as ex:
        result["error"] = str(ex)
        b.note(f"FAILED: {ex}")
    finally:
        b.stop()
    try:
        with open(os.path.join(a.out, "engine.log"), errors="replace") as lf:
            text = lf.read()
        result["fill_log"] = [ln for ln in text.splitlines() if "DC3 POSE FILL" in ln][:6]
        result["menu_select_log"] = [ln for ln in text.splitlines() if "DC3 MENU SELECT" in ln][:4]
    except OSError:
        pass
    with open(os.path.join(a.out, "result.json"), "w") as f:
        json.dump(result, f, indent=1)
    return 2 if result["error"] else 0


def run_matrix(a):
    os.makedirs(a.out, exist_ok=True)
    cells = [(l, r, n) for l in a.lists.split(",") for r in a.rates.split(",")
             for n in a.noise.split(",")]
    for l, r, n in cells:
        if l not in LISTS or n not in NOISE:
            print(f"unknown list or noise: {l} {n}", file=sys.stderr)
            return 2
    pending = list(cells)
    running = []
    failed = 0
    t0 = time.monotonic()

    def on_term(signum, frame):
        for proc, _, _ in running:
            proc.terminate()
        raise SystemExit(143)
    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)
    while pending or running:
        while pending and len(running) < a.jobs:
            l, r, n = pending.pop(0)
            out = os.path.join(a.out, f"{l}_{r}_{n}")
            cmd = [sys.executable, os.path.abspath(__file__), "cell", "--engine", a.engine,
                   "--out", out, "--pace-fps", str(a.pace_fps), "--list", l, "--rate", r,
                   "--noise", n, "--trials", str(a.trials), "--reads", str(a.reads),
                   "--idles", str(a.idles), "--select", a.select, "--seed", str(a.seed),
                   "--scans", str(a.scans)]
            for kv in a.env:
                cmd += ["--env", kv]
            os.makedirs(out, exist_ok=True)
            proc = subprocess.Popen(cmd, stdout=open(os.path.join(out, "cell.out"), "w"),
                                    stderr=subprocess.STDOUT)
            running.append((proc, (l, r, n), time.monotonic()))
        time.sleep(0.5)
        for item in list(running):
            proc, cell, started = item
            rc = proc.poll()
            if rc is None and time.monotonic() - started > a.cell_timeout:
                proc.terminate()        # the cell stops its engine
                try:
                    proc.wait(20)
                except subprocess.TimeoutExpired:
                    proc.kill()
                rc = -9
            if rc is not None:
                running.remove(item)
                failed += rc != 0
                print(f"[{time.monotonic() - t0:6.0f}s] {'/'.join(cell)}: "
                      f"{'done' if rc == 0 else 'FAILED rc=%d' % rc}", flush=True)
    print_table(a.out, None)
    return 2 if failed else 0


# ---- the select switch, end to end ------------------------------------------------
# `check`: one engine, the main menu, and what DC3_MENU_SELECT promises -- the
# flow test (MenuSelectFlow in native/tests/test_native_gesture_menus.cpp)
# runs this.  PASS/FAIL per line, exit 1 when any failed.
def _raise_arm(p, target):
    k, arm = p.k, p.k.arms[p.hand]
    st, l = p.look(reset=True)
    if l is None:
        raise FlowError("the list under test is not on screen")
    p.b.take_events(st)
    if p.rows is None:
        p.b.wait(0.15)
        st, l = p.look()
    kind, h = p.aim(l, p.rows.index(target))
    now = k.now()
    arm.height.go(now, h, 0.0)
    arm.angle.go(now, 0.0, 0.0)
    arm.tilt.go(now, 0.0, 0.0)
    arm.out.go(now, 1.0, 0.45)
    return st["ui_seconds"]


def _watch(p, seconds, target=None, stop=True):
    """The hand where it is for `seconds`: the selects, how far a hold got."""
    out = dict(events=[], max_progress=0.0, slide=False, lit=False, t=None, controller=0)
    t0 = None
    end = p.k.now() + seconds
    while p.k.now() < end:
        p.b.wait(0.05)
        st, l = p.look()
        if t0 is None:
            t0 = st["ui_seconds"]
        out["controller"] = max(out["controller"], st.get("controller_mode", 0))
        ev = p.b.take_events(st)
        if l is not None:
            out["max_progress"] = max(out["max_progress"], l.get("hold_progress", 0.0))
            if l.get("hold_progress", 0.0) > 0.0 and l["mode"] == 1:
                out["slide"] = True
            if target is not None and l["sym"] == target and l["mode"] != 3:
                out["lit"] = True
        if ev:
            out["events"] += ev
            if out["t"] is None:
                out["t"] = ev[0]["ui_seconds"]
            if stop:
                break
    return out


def _hold_on(p, target, seconds, stop=True):
    t0 = _raise_arm(p, target)
    out = _watch(p, seconds, target, stop)
    if out["t"] is not None:
        out["t"] -= t0
    return out


def _swipe_on(p, target):
    """Raise the arm on the row, sweep and drop the arm at once: the hand is
    off the list about 1.6 s after the arm started up, before a hold begun
    when the row lit (settle 0.3 + hold 1.2 s, from ~0.4 s) could select."""
    _raise_arm(p, target)
    p.b.wait(0.8)
    arm = p.k.arms[p.hand]
    arm.angle.go(p.k.now(), 110.0, 0.35)
    out = _watch(p, 0.35 + sk.SWIPE_HOLD_S, target)
    p.arms_down(0.0)
    if not out["events"]:
        more = _watch(p, 1.5)
        out["events"] += more["events"]
        out["max_progress"] = max(out["max_progress"], more["max_progress"])
    return out


def run_check(a):
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "bench.log"), "w")
    b = Bench(a, a.out, log)
    fails = []

    def check(ok, what, detail=""):
        line = ("PASS  " if ok else "FAIL  ") + what + (f"  [{detail}]" if detail else "")
        print(line, flush=True)
        b.note(line)
        if not ok:
            fails.append(what)

    def on_term(signum, frame):
        raise SystemExit(143)
    signal.signal(signal.SIGTERM, on_term)

    def src(out):
        return [(e["sym"], e.get("source")) for e in out["events"]]

    try:
        b.start(a.env)
        e = b.e
        b.wait_screen("title_screen", timeout=180, settle=1.5)
        p = Performer(b, LISTS["main"], "hold")
        p.to_screen(force=True)
        b.k.configure(30, "none", 1)
        b.wait(1.0)
        mode = lambda: b.state()["menu_select"]

        # 0. outside a song the lists get the camera's own frames
        #    (DC3_POSE_FILL_MENU, default off; the song's default is extrap)
        fill = b.state()["fill"]["mode"]
        check(fill == "off", "outside a song the pose fill is off", fill)

        # 1. the default is the original's swipe, untouched
        check(mode() == "swipe", "the default is swipe", mode())
        r = _hold_on(p, "campaign_mode", 3.5)
        check(r["lit"] and not r["events"] and r["max_progress"] == 0.0,
              "swipe: a hand held 3.5 s on a row selects nothing and shows no progress",
              f"lit={r['lit']} events={src(r)} progress={r['max_progress']:.2f}")
        p.arms_down(0.5)
        if r["events"]:
            p.to_screen(force=True)
        r = _swipe_on(p, "campaign_mode")
        check(src(r) == [("campaign_mode", "swipe")], "swipe: a swipe selects the row", str(src(r)))
        p.to_screen(force=True)

        # 2. the runtime switch
        ans = e.eval("{native_settings set_menu_select hold}")
        check(ans in (1, "1") and mode() == "hold",
              "{native_settings set_menu_select hold} switches at run time", f"{ans} {mode()}")
        check(str(e.eval("{native_settings menu_select}")) == "hold",
              "{native_settings menu_select} names the mode", str(e.eval("{native_settings menu_select}")))
        check(e.eval("{native_settings set_menu_select dwell}") in (0, "0") and mode() == "hold",
              "an unknown mode is refused")

        # 3. hold selects, with the slide as its progress; no second select
        #    from the hand left where it was
        r = _hold_on(p, "fitness_mode", 4.0)
        check(src(r) == [("fitness_mode", "hold")] and r["t"] is not None and 1.2 <= r["t"] <= 2.6,
              "hold: the hand kept on a row selects it after about 1.5 s",
              f"{src(r)} t={r['t']}")
        check(r["slide"] and r["max_progress"] > 0.3,
              "hold: the list slid as the hold progressed",
              f"slide={r['slide']} progress={r['max_progress']:.2f}")
        again = _watch(p, 2.0, stop=False)
        check(not again["events"], "hold: nothing else is selected in the next 2 s from the "
              "hand left where it was", str(src(again)))
        p.arms_down(0.5)
        p.to_screen(force=True)

        # 4. hold only: a swipe does not select
        r = _swipe_on(p, "challenges")
        check(not r["events"], "hold: a swipe selects nothing", str(src(r)))
        p.to_screen(force=bool(r["events"]))

        # 5. controller mode: no hold
        b.wait(0.5)
        e._req("POST", "/input/press", json.dumps({"button": "down"}))
        b.wait(0.3)
        r = _hold_on(p, "campaign_mode", 2.5)
        check(r["controller"] == 1 and not r["events"] and r["max_progress"] == 0.0,
              "hold: nothing is held or selected in controller mode",
              f"controller={r['controller']} events={src(r)} progress={r['max_progress']:.2f}")
        p.arms_down(0.5)
        # The press keeps the menus for the press hold (minutes; PLAN H-157):
        # the performer takes them back the way a player does, both hands up.
        b.wait(1.5)
        b.k.hands_up = True
        end = time.monotonic() + 30
        while b.state().get("controller_mode") and time.monotonic() < end:
            b.wait(0.5)
        b.k.hands_up = False
        b.wait(1.0)
        p.to_screen(force=True)

        # 6. both: either selects
        e.eval("{native_settings cycle_menu_select}")
        check(mode() == "both", "{native_settings cycle_menu_select} steps hold -> both", mode())
        r = _swipe_on(p, "campaign_mode")
        check(src(r) == [("campaign_mode", "swipe")], "both: a swipe selects", str(src(r)))
        p.to_screen(force=True)
        r = _hold_on(p, "store", 4.0)
        check(src(r) == [("store", "hold")], "both: a hold selects", str(src(r)))
        p.arms_down(0.5)

        # 7. the left hand's Back, by hold
        e.eval("{native_settings set_menu_select hold}")
        pb = Performer(b, LISTS["back"], "hold")
        pb.to_screen(force=True)
        r = _hold_on(pb, "screen_name_mode_select", 4.0)
        check(len(r["events"]) == 1 and r["events"][0]["hand"] == "left"
              and r["events"][0].get("source") == "hold" and 1.9 <= r["t"] <= 3.4,
              "hold: the left hand's Back is selected by a hold, after about 2.1 s",
              f"{src(r)} t={r['t']}")
        pb.arms_down(0.5)

        # 8. back to swipe: a hold selects nothing again
        e.eval("{native_settings set_menu_select swipe}")
        p.to_screen(force=True)
        r = _hold_on(p, "campaign_mode", 3.0)
        check(not r["events"] and r["max_progress"] == 0.0,
              "back under swipe a held hand selects nothing", str(src(r)))
        p.arms_down(0.3)
    except FlowError as ex:
        check(False, f"the flow ran to its end ({ex})")
    finally:
        b.stop()
    print("MENU SELECT CHECK: %s" % ("ok" if not fails else "%d FAILED" % len(fails)), flush=True)
    return 1 if fails else 0


# ---- real recordings -------------------------------------------------------------
# A playtest's pose.dc3session (DC3_POSE_RECORD; docs/native/PLAYTEST.md) holds
# every camera frame the game accepted, people, joints and confidences as the
# pose server sent them, with wall times.  `replay` plays the footage that is
# not inside a song (the menus, the pause menu, the results screens) onto one
# menu screen by wall time, through the engine's own session replay
# (DC3_POSE_PERFORM=session:FILE DC3_POSE_SESSION_SYNC=wall), and counts what
# the lists did.  Nobody in that footage was asked to select anything on THIS
# screen, so a select here is a measure of how often a person standing at,
# walking up to or using a pad in front of the camera sets a list off; it is
# not a success rate.
REPLAY_LEAD_S = 150.0    # nobody in view while the engine boots and reaches the screen
REPLAY_GAP_S = 3.0       # nobody in view between two stretches of footage
REPLAY_PAD_HZ = 8.0


def build_replay_cut(paths, out_path, lead=REPLAY_LEAD_S, max_seconds=None, windows=()):
    """One session file: LEAD seconds of nobody, then every non-song segment of
    every session in `paths`, then every window (path, wall_from, wall_to) --
    whatever the game was showing then -- back to back.  Returns the table of
    what went where (cut_from, cut_to, session, segment, recorded_wall_from,
    frames, with_person) and the cut's length."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "playtest"))
    import pose_session as ps
    lines = [ps.HEADER, "# cut for menu_gesture_bench.py replay"]
    table = []
    seq = [0]
    pad_id = [0x40000000]

    def header(wall, ts, frame_id, rest, npersons):
        seq[0] += 1
        # F seq rnd wall song frame_id ts unlevelled level nx ny nz num_persons
        return "F %d %d %.6f %.6f %d %.17g %s %d" % (seq[0], seq[0], wall, wall, frame_id, ts,
                                                      rest, npersons)

    def pad(t_from, t_to, ts_base):
        t = t_from
        while t < t_to - 1e-9:
            pad_id[0] += 1
            lines.append(header(t, ts_base + t, pad_id[0], "1 0 0 1 0", 0))
            t += 1.0 / REPLAY_PAD_HZ

    t = 0.0
    pad(0.0, lead, 1000.0)
    t = lead
    pieces = []      # (path, label, frames)
    for path in paths:
        _, frames = ps.load(path)
        segs = ps.segments(frames)
        for row, (a, b) in zip(ps.describe(frames, segs), segs):
            if row["kind"] != "song":
                pieces.append((path, row["n"], frames[a:b]))
    for path, w_from, w_to in windows:
        _, frames = ps.load(path)
        fr = [f for f in frames if w_from <= f.wall <= w_to]
        if fr:
            pieces.append((path, "%g-%g" % (w_from, w_to), fr))
    for path, label, fr in pieces:
        row = dict(n=label)
        if True:
            if max_seconds is not None and t - lead >= max_seconds:
                break
            w0 = fr[0].wall
            for f in fr:
                h = f.lines[0].split()
                wall = t + (f.wall - w0)
                ts = float(h[6]) - f.wall + wall
                lines.append(header(wall, ts, int(h[5]), " ".join(h[7:12]), f.num_persons))
                lines.extend(f.lines[1:])
            end = t + (fr[-1].wall - w0)
            table.append(dict(cut_from=t, cut_to=end, session=path, segment=row["n"],
                              recorded_wall_from=w0, frames=len(fr),
                              with_person=sum(1 for f in fr if f.valid)))
            pad(end + 1.0 / REPLAY_PAD_HZ, end + REPLAY_GAP_S, 1000.0)
            t = end + REPLAY_GAP_S
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return table, t


def run_replay(a):
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "bench.log"), "w")
    cut = os.path.join(a.out, "cut.dc3session")
    windows = []
    for w in a.window:
        path, w_from, w_to = w.rsplit(":", 2)
        windows.append((path, float(w_from), float(w_to)))
    if not a.session and not windows:
        print("replay: give --session and/or --window", file=sys.stderr)
        return 2
    table, total = build_replay_cut(a.session, cut, max_seconds=a.max_seconds, windows=windows)
    footage = sum(r["cut_to"] - r["cut_from"] for r in table)
    result = dict(kind="replay", screen=a.screen, env=a.env, sessions=a.session, segments=table,
                  footage_s=footage, events=[], lists={}, away_s=0.0, error=None)
    b = Bench(a, a.out, log)

    def on_term(signum, frame):
        raise SystemExit(143)
    signal.signal(signal.SIGTERM, on_term)

    def cut_time():
        # the replay's clock: it starts with the engine's first frame, a few
        # seconds after the spawn, so this runs that much ahead
        return time.monotonic() - b.t_spawn

    def where(tc):
        for r in table:
            if r["cut_from"] - 0.5 <= tc <= r["cut_to"] + REPLAY_GAP_S:
                return r
        return None

    def to_screen(force):
        e = b.e
        end = time.monotonic() + 60
        time.sleep(0.3)
        while e.eval("{ui in_transition}") not in (0, False, "0"):
            if time.monotonic() > end:
                raise FlowError("a transition never ended")
            time.sleep(0.1)
        if e.screen() == a.screen and not force:
            return
        if e.screen() == a.screen:
            via = "main_screen" if a.screen != "main_screen" else "choose_mode_screen"
            e.eval("{ui goto_screen %s}" % via)
            b.wait_screen(via, timeout=30, settle=0.5)
            while e.eval("{ui in_transition}") not in (0, False, "0"):
                if time.monotonic() > end:
                    raise FlowError("a transition never ended")
                time.sleep(0.1)
        e.eval("{ui goto_screen %s}" % a.screen)
        b.wait_screen(a.screen, timeout=30, settle=1.0)

    trace = None
    try:
        b.start(a.env, session=cut)
        b.wait_screen("title_screen", timeout=180, settle=1.5)
        to_screen(True)
        ready = cut_time()
        b.note(f"on {a.screen} at cut time {ready:.1f} s (footage starts at {REPLAY_LEAD_S:.0f})")
        if ready > REPLAY_LEAD_S - 2.0:
            raise FlowError(f"the engine took {ready:.0f} s to reach {a.screen}: raise REPLAY_LEAD_S")
        st = b.state(reset=True)
        b.take_events(st)
        arc0 = st.get("arc_start")
        lists = result["lists"]
        last = time.monotonic()
        trace = open(os.path.join(a.out, "trace.jsonl"), "w") if a.trace else None
        while cut_time() < total + 1.0:
            time.sleep(0.1)
            now = time.monotonic()
            dt, last = now - last, now
            st = b.state(reset=True)
            if trace:
                tc = cut_time()
                r = where(tc)
                trace.write(json.dumps(dict(
                    cut=round(tc, 2), wall=r and round(r["recorded_wall_from"] + tc - r["cut_from"], 2),
                    lists=[[l["hand"][0], l["mode"], l["selected"], l["sym"], l["hand_height"],
                            l["swipe_amt"], l["max_swipe_amt"], l["nodes"], l["max_nodes"],
                            l.get("had_progress"), l["hover_ms"], l["sensor_updates"]]
                           for l in st["lists"] if l["enabled"]])) + "\n")
            for l in st["lists"]:
                if not l["enabled"]:
                    continue
                key = l["hand"] + ":" + l["name"]
                d = lists.setdefault(key, dict(engaged_s=0.0, highlight_changes=0, sliding_s=0.0,
                                               max_swipe_amt=0.0, last=None, rows=l["num_data"]))
                if l["mode"] != 3:
                    d["engaged_s"] += dt
                    if d["last"] is not None and d["last"] != l["selected"]:
                        d["highlight_changes"] += 1
                    d["last"] = l["selected"]
                    if l["mode"] == 1:
                        d["sliding_s"] += dt
                else:
                    d["last"] = None
                d["max_swipe_amt"] = max(d["max_swipe_amt"], l["max_swipe_amt"])
            ev = b.take_events(st)
            if ev:
                tc = cut_time()
                r = where(tc)
                for e in ev:
                    rec = dict(list=e["list"], hand=e["hand"], sym=e["sym"], source=e.get("source"),
                               cut_time=tc, session=r and os.path.basename(os.path.dirname(r["session"])),
                               segment=r and r["segment"],
                               recorded_wall=r and r["recorded_wall_from"] + tc - r["cut_from"])
                    result["events"].append(rec)
                    b.note("select %s" % json.dumps(rec))
                t0 = time.monotonic()
                to_screen(True)
                st = b.state(reset=True)
                b.take_events(st)
                for d in lists.values():
                    d["last"] = None
                last = time.monotonic()
                result["away_s"] += last - t0
        for d in lists.values():
            d.pop("last", None)
        arc1 = b.state().get("arc_start")
        if arc0 and arc1:
            result["arc_start"] = {k: [x - y for x, y in zip(arc1[k], arc0[k])]
                                   for k in ("left", "right")}
    except FlowError as ex:
        result["error"] = str(ex)
        b.note(f"FAILED: {ex}")
    finally:
        b.stop()
        if trace:
            trace.close()
    try:
        with open(os.path.join(a.out, "engine.log"), errors="replace") as lf:
            text = lf.read()
        result["menu_select_log"] = [ln for ln in text.splitlines() if "DC3 MENU SELECT" in ln][:4]
        result["perform_log"] = [ln for ln in text.splitlines() if "DC3 PERFORM" in ln][:4]
    except OSError:
        pass
    with open(os.path.join(a.out, "result.json"), "w") as f:
        json.dump(result, f, indent=1)
    print_replay(a.out)
    return 2 if result["error"] else 0


def print_replay(*dirs):
    """One line per replay result under each directory."""
    for d in dirs:
        paths = [os.path.join(d, "result.json")] if os.path.exists(os.path.join(d, "result.json")) \
            else sorted(os.path.join(d, n, "result.json") for n in os.listdir(d)
                        if os.path.exists(os.path.join(d, n, "result.json")))
        for path in paths:
            with open(path) as f:
                r = json.load(f)
            if r.get("kind") != "replay":
                continue
            mins = max(1e-9, (r["footage_s"] - r["away_s"]) / 60.0)
            by = {}
            for e in r["events"]:
                k = "%s %s" % (e["hand"], e.get("source") or "swipe")
                by[k] = by.get(k, 0) + 1
            print(f"{os.path.basename(os.path.dirname(path)):28s} {r['screen']:20s} "
                  f"footage {r['footage_s']:5.0f} s (away {r['away_s']:4.0f} s)  "
                  f"selects {len(r['events']):3d} = {len(r['events']) / mins:5.2f}/min  {by or ''}"
                  f"{'  ERROR: ' + r['error'] if r.get('error') else ''}")
            for key, l in sorted(r["lists"].items()):
                print(f"    {key:28s} engaged {l['engaged_s']:6.1f} s  highlight changes "
                      f"{l['highlight_changes']:4d}  sliding {l['sliding_s']:5.1f} s  "
                      f"max swipe {l['max_swipe_amt']:.2f}")
    return 0


def load_results(d):
    """DIR, or DIR1+DIR2+... (runs of the same cells with different --seed):
    their trials, reads and idles added up."""
    if "+" in d:
        out = {}
        for part in d.split("+"):
            for k, r in load_results(part).items():
                if k not in out:
                    out[k] = r
                    continue
                o = out[k]
                for f in ("trials", "reads", "idles", "scans"):
                    o[f] = o.get(f, []) + r.get(f, [])
                if o.get("fill") and r.get("fill"):
                    for f in ("fills", "reals"):
                        o["fill"][f] += r["fill"][f]
                o["error"] = o.get("error") or r.get("error")
                if "stray_selects" in o and "stray_selects" in r:
                    o["stray_selects"] = o["stray_selects"] + r["stray_selects"]
                else:
                    o.pop("stray_selects", None)     # one part predates them: unknown
        return out
    out = {}
    for name in sorted(os.listdir(d)):
        path = os.path.join(d, name, "result.json")
        if os.path.exists(path):
            with open(path) as f:
                r = json.load(f)
            if r.get("kind") == "replay":
                continue
            out[(r["list"], "%g" % float(r["rate"]), r["noise"])] = r
    return out


def summarize(r):
    tr = r["trials"]
    n = len(tr)
    c = lambda o: sum(1 for t in tr if t["outcome"] == o)
    ok = [t for t in tr if t["outcome"] == "ok"]
    first = sum(1 for t in ok if t["tries"] == 1)
    times = sorted(t["t_select"] for t in ok)
    reads = [x for x in r["reads"] if x["outcome"] != "skipped"]
    return dict(n=n, first=first, ok=len(ok), wrong=c("wrong_row"), false=c("false_select"),
                none=c("none"), aim=c("aim_fail") + c("lost_list"),
                t_med=times[len(times) // 2] if times else None,
                t_p90=times[min(len(times) - 1, int(len(times) * 0.9))] if times else None,
                reads=len(reads),
                read_false=sum(1 for x in reads if x["outcome"] == "false_select"),
                read_moved=sum(1 for x in reads if x["highlight_moved"]),
                scans=len([x for x in r.get("scans", []) if x["outcome"] != "skipped"]),
                scan_false=sum(1 for x in r.get("scans", []) if x["outcome"] == "false_select"),
                # None: a result from before late and stray selects were kept
                late=(sum(1 for x in r["reads"] + r.get("scans", [])
                          if x["outcome"] == "late_select")
                      if "stray_selects" in r else None),
                stray=len(r["stray_selects"]) if "stray_selects" in r else None,
                idles=len(r["idles"]),
                idle_false=sum(1 for x in r["idles"] if x["outcome"] == "false_select"),
                upp=r.get("updates_per_poll"), error=r.get("error"),
                fill=(lambda f: None if not f or not (f["fills"] + f["reals"])
                      else f["fills"] / float(f["fills"] + f["reals"]))(r.get("fill")))


def print_table(d, base):
    res = load_results(d)
    bres = load_results(base) if base else {}
    order = {"main": 0, "diff": 1, "songs": 2, "back": 3}
    norder = {"none": 0, "video": 1, "photo": 2, "gpd": 3}
    keys = sorted(res, key=lambda k: (order.get(k[0], 9), norder.get(k[2], 9), -float(k[1])))
    hdr = ("list   Hz  noise | first try      ok(<=3)    wrong  false  none  aim | "
           "t med   p90 | read: false moved | scan false | idle false | late stray | "
           "upd/poll fill")
    print(hdr)
    print("-" * len(hdr))
    for k in keys:
        for tag, r in (("", res[k]),) + ((("  was", bres[k]),) if k in bres else ()):
            s = summarize(r)
            n = s["n"]
            pct = lambda x: f"{x:3d}/{n:<3d} {100.0 * x / n:3.0f}%" if n else "   -      "
            t = lambda x: f"{x:5.1f}" if x is not None else "    -"
            print(f"{k[0]:5s} {k[1]:>3s} {k[2]:6s}| {pct(s['first'])}  {pct(s['ok'])}  "
                  f"{s['wrong']:4d}  {s['false']:4d}  {s['none']:4d} {s['aim']:4d} | "
                  f"{t(s['t_med'])} {t(s['t_p90'])} | {s['read_false']:5d}/{s['reads']:<2d} "
                  f"{s['read_moved']:3d}/{s['reads']:<2d} | "
                  f"{('%3d/%-2d' % (s['scan_false'], s['scans'])) if s['scans'] else '   -  '}     | "
                  f"{s['idle_false']:3d}/{s['idles']:<2d}    | "
                  f"{'   ?     ?' if s['late'] is None else '%4d %5d' % (s['late'], s['stray'])} | "
                  f"{'-' if s['upp'] is None else '%.2f' % s['upp']} "
                  f"{'   -' if s['fill'] is None else '%3.0f%%' % (100 * s['fill'])}{tag}"
                  f"{'  ERROR: ' + s['error'] if s['error'] else ''}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    here = os.path.dirname(os.path.abspath(__file__))
    default_engine = os.path.normpath(os.path.join(here, "..", "native", "build", "dc3-native"))

    def common(p):
        p.add_argument("--engine", default=default_engine)
        p.add_argument("--out", required=True, help="results directory")
        p.add_argument("--pace-fps", type=float, default=30.0,
                       help="engine frames per wall second (DC3_HTTP_PACE_FPS); 30 = real "
                            "time, the only pace at which the pose fill sees the camera's "
                            "true rate in the menus (see the docstring)")
        p.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the engine")
        p.add_argument("--trials", type=int, default=30, help="select trials per cell")
        p.add_argument("--reads", type=int, default=10, help="5 s reads per cell")
        p.add_argument("--idles", type=int, default=2, help="5 s arms-down waits per cell")
        p.add_argument("--scans", type=int, default=0,
                       help="5 s of reading down the list row by row (0.6-1.1 s a row) per cell")
        p.add_argument("--seed", type=int, default=0,
                       help="another draw of the camera noise and of the performer's habits "
                            "(0 = the published one); run several and add them up")
        p.add_argument("--select", default="swipe", choices=["swipe", "hold"],
                       help="how the performer selects (hold needs DC3_MENU_SELECT=hold|both)")

    p = sub.add_parser("run", help="the matrix: one engine per (list, rate, noise)")
    common(p)
    p.add_argument("--lists", default="main,diff,songs,back")
    p.add_argument("--rates", default="30,15,8")
    p.add_argument("--noise", default="none,photo")
    p.add_argument("--jobs", type=int, default=6)
    p.add_argument("--cell-timeout", type=float, default=1800.0)
    p = sub.add_parser("cell", help="one (list, rate, noise); what `run` starts")
    common(p)
    p.add_argument("--list", required=True, choices=sorted(LISTS))
    p.add_argument("--rate", type=float, required=True)
    p.add_argument("--noise", required=True, choices=sorted(NOISE))
    p = sub.add_parser("table", help="print the table of a results directory")
    p.add_argument("dir")
    p.add_argument("baseline", nargs="?")
    p = sub.add_parser("check", help="DC3_MENU_SELECT end to end on one engine (the flow test)")
    common(p)
    p = sub.add_parser("replay", help="real recordings: play the non-song footage of "
                                      "pose.dc3session files onto one menu screen")
    common(p)
    p.add_argument("--session", action="append", default=[],
                   help="a pose.dc3session: all of its footage outside songs")
    p.add_argument("--window", action="append", default=[], metavar="FILE:FROM:TO",
                   help="recorded wall seconds FROM..TO of a pose.dc3session, whatever was on "
                        "screen then (a pause menu inside a song, a results screen)")
    p.add_argument("--screen", default="main_screen")
    p.add_argument("--max-seconds", type=float, default=None, help="stop adding segments after this much footage")
    p.add_argument("--trace", action="store_true",
                   help="write OUT/trace.jsonl: every look at the lists (ten a second) with the "
                        "recorded wall second -- hand, mode, row, sym, hand height, swipe amount "
                        "now and at most since the last look, arc nodes now and at most, the arc "
                        "detector's had-progress latch, hover ms, sensor updates")
    p = sub.add_parser("replay-table", help="print replay results")
    p.add_argument("dirs", nargs="+")
    p = sub.add_parser("explore", help="developer aid: run --step actions, print the lists")
    common(p)
    p.add_argument("--step", action="append", default=[],
                   help="goto:SCREEN | dta:EXPR | wait:SECONDS | lists (in order)")
    a = ap.parse_args()
    if a.cmd == "table":
        return print_table(a.dir, a.baseline)
    if a.cmd == "replay-table":
        return print_replay(*a.dirs)
    if not os.path.exists(a.engine):
        print(f"engine not found: {a.engine}", file=sys.stderr)
        return 2
    if a.cmd == "explore":
        return explore(a)
    if a.cmd == "cell":
        return run_cell(a)
    if a.cmd == "replay":
        return run_replay(a)
    if a.cmd == "check":
        return run_check(a)
    return run_matrix(a)


def explore(a):
    """Developer aid: go to each --screen (ui goto_screen) and print its lists."""
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "bench.log"), "w")
    b = Bench(a, a.out, log)

    def on_term(signum, frame):
        raise SystemExit(143)
    signal.signal(signal.SIGTERM, on_term)
    try:
        b.start(a.env)
        b.wait_screen("title_screen", timeout=180, settle=1.5)
        for step in a.step:
            kind, _, arg = step.partition(":")
            if kind == "goto":
                print("goto", arg, "->", b.e.eval("{ui goto_screen %s}" % arg))
                try:
                    b.wait_screen(arg, timeout=20, settle=2.0)
                except FlowError as ex:
                    print("  ", ex)
            elif kind == "dta":
                print(arg, "->", b.e.eval(arg))
            elif kind == "wait":
                b.wait(float(arg))
            elif kind == "lists":
                b.state(rows=True)
                b.wait(0.2)
                st = b.state(rows=True)
                print("screen", b.e.screen())
                for l in st["lists"]:
                    print("  ", {k: v for k, v in l.items() if k not in ("rows", "row_flags")})
                    print("     rows", l.get("rows", [])[:60],
                          "".join(str(x) for x in l.get("row_flags", [])[:60]))
        return 0
    except FlowError as ex:
        print("FAILED:", ex)
        return 2
    finally:
        b.stop()


if __name__ == "__main__":
    sys.exit(main())
