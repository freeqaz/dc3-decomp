#!/usr/bin/env python3
"""menu_gesture_bench.py -- how reliably do the menus select, on a webcam? (PLAN P-3)

The native port drives DC3's own, unchanged menu-gesture detectors
(HandHeightGestureFilter, DirectionGestureFilterSingleUser / ArcDetector, in
HamNavList) from a webcam through MediaPipe instead of a Kinect.  A Kinect
delivers 30 skeletons a second with millimetre noise; a webcam in a dim room
delivers 8, with centimetres.  This measures what that costs, through the
real path -- pose socket -> native sensor -> SkeletonUpdate -> HamNavList --
and what a fix buys.

  NOBODY REAL IS MEASURED HERE.  The performer is a scripted arm
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
                                            frame-to-frame differences of the
                                            owner's one real session on the GPD
                                            webcam at 7.5 frames/s (np-pose-rough,
                                            2026-11-13).  An upper bound: it
                                            includes his real movement.

  Noise is AR(1) with a 0.1 s correlation time (MediaPipe smooths its own
  landmarks), independent per joint and axis.

What a trial is, and what is counted (every rate is n / trials):
  select   "highlight row k, then select it": first_try (the first swipe
           selected the wanted row), ok (within 3 tries), wrong_row (a select
           landed on another row), false_select (a select before the performer
           swiped at all -- while raising the arm or aiming), none (3 tries,
           no select), aim_fail (the wanted row could not be highlighted in
           8 s).  Time-to-select is from the arm starting up to the select,
           for trials that ended ok.
  read     the arm held on a row for 5 s, drifting slowly, never swiping:
           false_select if anything selects; highlight_moved if the highlight
           leaves the row.
  idle     arms down for 5 s: any select or engagement is false.

Lists: main (the main menu), diff (the difficulty list), songs (song select:
a row BELOW the 5th, which needs the list scrolled), back (the help bar's
Back, left hand).

Usage:
  scripts/menu_gesture_bench.py run --out DIR [--engine native/build/dc3-native]
      [--lists main,diff,songs,back] [--rates 30,15,8] [--noise none,photo,gpd]
      [--trials 30] [--select swipe|hold] [--jobs 6] [--env KEY=VALUE]...
  scripts/menu_gesture_bench.py table DIR [BASELINE_DIR]

Exit: 0 ran; 2 setup error (an engine did not come up, a screen was not reached).
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
}
NOISE_TAU_S = 0.1
HAND_JOINTS = (sk.WRIST_L, sk.HAND_L, sk.WRIST_R, sk.HAND_R)
ELBOW_JOINTS = (sk.ELBOW_L, sk.ELBOW_R)
INFERRED_SCORE = 0.45    # Skeleton_Native.cpp ConfidenceFromScore: 0.3 .. 0.6 is Inferred


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
        self.packets = 0
        self.absent = False

    def configure(self, rate, noise, seed):
        with self.lock:
            self.rate = float(rate)
            self.noise = NOISE[noise]
            self.rng = random.Random(seed)
            self.noise_state = [[0.0, 0.0, 0.0] for _ in range(sk.NUM_JOINTS)]
            self.noise_t = None

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

    def start(self, extra_env):
        os.makedirs(self.workdir, exist_ok=True)
        for d in ("cache", "saves"):
            os.makedirs(os.path.join(self.workdir, d), exist_ok=True)
        seed_all_tutorials_seen(os.path.join(self.workdir, "cache"))
        sock = os.path.join(self.workdir, "kinect.sock")
        self.k = BenchKinect(sock)
        self.k.start()
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
        env.update(SyntheticKinect.engine_env(sock))
        env.update(engine_port_env(self.workdir, 0))
        for kv in extra_env:
            key, _, val = kv.partition("=")
            env[key] = val
        engine = os.path.abspath(self.a.engine)
        with open(os.path.join(self.workdir, "engine.log"), "w") as lf:
            self.proc = subprocess.Popen([engine], cwd=os.path.dirname(engine), env=env,
                                         stdout=lf, stderr=subprocess.STDOUT,
                                         start_new_session=True)
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
        for _ in range(3):
            try:
                d = self.e._req("GET", q)
                if d.get("ok"):
                    return d["data"]
            except Exception:
                time.sleep(0.01)
        raise FlowError("the engine stopped answering /api/menu/state")

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
    return zlib.crc32(":".join(str(p) for p in parts).encode())


class Performer:
    def __init__(self, bench, spec, select_mode):
        self.b = bench
        self.k = bench.k
        self.spec = spec
        self.hand = spec["hand"]
        self.mode = select_mode
        self.rows = None

    # -- helpers
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
        st, l = self.look(reset=True)
        if l is None:
            raise FlowError("the list under test is not on screen")
        self.b.take_events(st)
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
            last_move = now
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
                        last_move = now
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
                    last_move = now
                if (old["sym"] == target and old["mode"] != 3 and not old["is_scrolling"]
                        and old["scroll_dir"] == 0):
                    if lit_since is None:
                        lit_since = now
                    if now - lit_since >= hover:
                        break
                else:
                    lit_since = None
                    if now - last_move > 0.6 and old["mode"] != 3:
                        # lit, but the wrong row: move half a row towards it
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
                    ev = self.b.take_events(st)
                    if ev:
                        fired = ev
                        res["early"] = False
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
    def read_trial(self, target, rng, seconds=5.0):
        k, hand = self.k, self.hand
        arm = k.arms[hand]
        st, l = self.look(reset=True)
        if l is None:
            raise FlowError("the list under test is not on screen")
        self.b.take_events(st)
        want = self.rows.index(target)
        res = dict(target=target, outcome="clean", highlight_moved=False, events=[],
                   max_swipe_amt=0.0, engaged=False)
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
        while not ev and k.now() < end:
            now = k.now()
            if now >= next_move:
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
        if ev:
            res["events"] = ev
            res["outcome"] = "false_select"
        self.arms_down(0.3)
        return res

    # -- arms down
    def idle_trial(self, seconds=5.0):
        st, l = self.look(reset=True)
        self.b.take_events(st)
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
                  trials=[], reads=[], idles=[], error=None)
    b = Bench(a, a.out, log)
    try:
        b.start(a.env)
        b.wait_screen("title_screen", timeout=180, settle=1.5)
        p = Performer(b, spec, a.select)
        p.to_screen(force=True)
        b.k.configure(a.rate, a.noise, seed_for("cam", a.list, a.rate, a.noise))
        b.wait(1.5)
        # the camera as the list sees it: filter updates per poll over 3 s
        p.look(reset=True)
        b.wait(3.0)
        st, l = p.look()
        result["updates_per_poll"] = (l["sensor_updates"] / float(l["polls"])
                                      if l and l["polls"] else None)
        for i in range(a.idles):
            r = p.idle_trial()
            result["idles"].append(r)
            b.note(f"idle {i}: {r['outcome']} engaged={r['engaged']}")
            if r["events"]:
                p.to_screen(force=True)
        for i in range(a.trials):
            target = spec["targets"][i % len(spec["targets"])]
            r = p.select_trial(target, random.Random(seed_for("select", a.list, i)))
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
            r = p.read_trial(target, random.Random(seed_for("read", a.list, i)))
            result["reads"].append(r)
            b.note(f"read {i} {target}: {r['outcome']} moved={r['highlight_moved']} "
                   f"amt={r['max_swipe_amt']:.2f}")
            p.to_screen(force=bool(r["events"]))
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
    while pending or running:
        while pending and len(running) < a.jobs:
            l, r, n = pending.pop(0)
            out = os.path.join(a.out, f"{l}_{r}_{n}")
            cmd = [sys.executable, os.path.abspath(__file__), "cell", "--engine", a.engine,
                   "--out", out, "--pace-fps", str(a.pace_fps), "--list", l, "--rate", r,
                   "--noise", n, "--trials", str(a.trials), "--reads", str(a.reads),
                   "--idles", str(a.idles), "--select", a.select]
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
                proc.kill()
                rc = -9
            if rc is not None:
                running.remove(item)
                failed += rc != 0
                print(f"[{time.monotonic() - t0:6.0f}s] {'/'.join(cell)}: "
                      f"{'done' if rc == 0 else 'FAILED rc=%d' % rc}", flush=True)
    print_table(a.out, None)
    return 2 if failed else 0


def load_results(d):
    out = {}
    for name in sorted(os.listdir(d)):
        path = os.path.join(d, name, "result.json")
        if os.path.exists(path):
            with open(path) as f:
                r = json.load(f)
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
                idles=len(r["idles"]),
                idle_false=sum(1 for x in r["idles"] if x["outcome"] == "false_select"),
                upp=r.get("updates_per_poll"), error=r.get("error"))


def print_table(d, base):
    res = load_results(d)
    bres = load_results(base) if base else {}
    order = {"main": 0, "diff": 1, "songs": 2, "back": 3}
    norder = {"none": 0, "video": 1, "photo": 2, "gpd": 3}
    keys = sorted(res, key=lambda k: (order.get(k[0], 9), norder.get(k[2], 9), -float(k[1])))
    hdr = ("list   Hz  noise | first try      ok(<=3)    wrong  false  none  aim | "
           "t med   p90 | read: false moved | idle false | upd/poll")
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
                  f"{s['read_moved']:3d}/{s['reads']:<2d} | {s['idle_false']:3d}/{s['idles']:<2d}    | "
                  f"{s['upp'] if s['upp'] is None else round(s['upp'], 2)}{tag}"
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
        p.add_argument("--pace-fps", type=float, default=120.0,
                       help="engine frames per wall second (DC3_HTTP_PACE_FPS)")
        p.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the engine")
        p.add_argument("--trials", type=int, default=30, help="select trials per cell")
        p.add_argument("--reads", type=int, default=10, help="5 s reads per cell")
        p.add_argument("--idles", type=int, default=2, help="5 s arms-down waits per cell")
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
    p = sub.add_parser("explore", help="developer aid: run --step actions, print the lists")
    common(p)
    p.add_argument("--step", action="append", default=[],
                   help="goto:SCREEN | dta:EXPR | wait:SECONDS | lists (in order)")
    a = ap.parse_args()
    if a.cmd == "table":
        return print_table(a.dir, a.baseline)
    if not os.path.exists(a.engine):
        print(f"engine not found: {a.engine}", file=sys.stderr)
        return 2
    if a.cmd == "explore":
        return explore(a)
    if a.cmd == "cell":
        return run_cell(a)
    return run_matrix(a)


def explore(a):
    """Developer aid: go to each --screen (ui goto_screen) and print its lists."""
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "bench.log"), "w")
    b = Bench(a, a.out, log)
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
