#!/usr/bin/env python3
"""kitdrive.py -- drive a scripts/play.sh session from a program: the same
launcher, environment and session folder a player gets, with the menus
pressed over the game's debug server instead of by hand or by gesture.

Used by the record/replay proof (replay_proof.py) and the soak (soak.py).
A press is preceded by the controller-mode wake button (l3), as a player
picking up a pad would: under DC3_CONTROLLER_MODE=auto with a tracker running
the shell is in the hand menus, and the first pad press only wakes it
(docs/debugging/native.md, Controller Mode Policy).

It only ever drives -- and only ever quits -- a game it started itself: the
session folder must be new (no game.pid / http_port from an earlier game),
the debug server it talks to must report the pid of the game its own
play.sh launched, and quit() refuses any pid that is not a descendant of
that play.sh.  A driver that "fell back" to whatever game answered once quit
a person's song mid-way (np-gpd-test, 2026-11-14).

Presses go through POST /api/input/sequence: the game turns each into a
ButtonDown message on one frame and the ButtonUp HOLD_FRAMES later
(HttpServer.cpp DispatchInjectedButtons, "hold"), so a press can never fall
between two frames the way a window-system key tap can, and is never a
one-frame blip.  Presses are spaced at least MIN_GAP_FRAMES apart (more than
HOLD_FRAMES), so a button is up before it is pressed again and two presses of
one button are never merged into one.
"""
import json
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# Global-options cache entry with every tutorial seen (as
# scripts/native_song_sweep.py seed_root and native/tests/test_dta_flow.cpp).
_GLOBAL_OPTIONS = bytes([
    0x1c, 0, 0, 0, 0, 0, 0, 0xb0, 0xc1, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 9, 4, 0, 0, 0, 0, 0, 0])


HOLD_FRAMES = 2      # frames a press stays down
MIN_GAP_FRAMES = 3   # frames between presses; > HOLD_FRAMES


class NotOurGame(RuntimeError):
    """The game in front of the driver is not one it started."""


def descends_from(pid, ancestor):
    """Whether process `pid` is `ancestor` or one of its descendants."""
    seen = 0
    while pid and pid > 1 and seen < 64:
        if pid == ancestor:
            return True
        try:
            with open(f"/proc/{pid}/stat") as f:
                pid = int(f.read().rsplit(")", 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            return False
        seen += 1
    return False


def seed_home(home, tutorials_seen=True):
    """A playtest home (saves, cache) as a console that has seen every tutorial."""
    os.makedirs(os.path.join(home, "cache", "global"), exist_ok=True)
    os.makedirs(os.path.join(home, "saves"), exist_ok=True)
    if tutorials_seen:
        b = bytearray(_GLOBAL_OPTIONS)
        b[0x28:0x2c] = (0x7FFFFFFF).to_bytes(4, "little")
        with open(os.path.join(home, "cache", "global", "global"), "wb") as f:
            f.write(b)


class Kit:
    def __init__(self, session, home, args=(), env=None, gpu_lock=None, log=print):
        self.session = os.path.abspath(session)
        self.home = os.path.abspath(home)
        self.log = log
        os.makedirs(self.session, exist_ok=True)
        for leftover in ("game.pid", "http_port"):
            if os.path.exists(os.path.join(self.session, leftover)):
                # An earlier game's files: its pid or port would point the
                # driver at a game it did not start (maybe one being played).
                raise NotOurGame(f"{self.session} already holds {leftover} from another game; "
                                 "a driven session needs a new folder")
        e = dict(os.environ)
        e.update(env or {})
        cmd = [os.path.join(REPO, "scripts", "play.sh"), "--home", self.home,
               "--session-dir", self.session, "--no-pack"] + list(args)
        if gpu_lock:
            cmd = ["flock", "-s", "-w", "3600", gpu_lock] + cmd
        self.out = open(os.path.join(self.session, "play_sh.out"), "wb")
        self.proc = subprocess.Popen(cmd, env=e, stdout=self.out, stderr=subprocess.STDOUT,
                                     start_new_session=True)
        self.port = None
        self.t0 = time.time()
        self.engine_log = os.path.join(self.session, "engine.log")
        self._off = 0
        self.screens = []

    # ---- process ----
    def game_pid(self):
        """The pid play.sh recorded -- only if it is this driver's game."""
        try:
            with open(os.path.join(self.session, "game.pid")) as f:
                pid = int(f.read())
        except (OSError, ValueError):
            return None
        return pid if self.owns(pid) else None

    def owns(self, pid):
        return descends_from(pid, self.proc.pid)

    def alive(self):
        pid = self.game_pid()
        if pid is None:
            return self.proc.poll() is None
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        try:  # a zombie is not alive
            with open(f"/proc/{pid}/stat") as f:
                return f.read().split(")")[-1].split()[0] != "Z"
        except OSError:
            return False

    def rss_kb(self):
        pid = self.game_pid()
        try:
            with open(f"/proc/{pid}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1])
        except (OSError, TypeError):
            pass
        return None

    def wait_port(self, timeout=240):
        pf = os.path.join(self.session, "http_port")
        end = time.time() + timeout
        while time.time() < end:
            if self.proc.poll() is not None:
                raise RuntimeError(f"play.sh exited {self.proc.returncode} before the game was up "
                                   f"(see {self.session}/play_sh.out)")
            try:
                self.port = int(open(pf).read().split()[0])
                h = self.jget("/health", 2)
                if h:
                    pid = (h.get("data") or {}).get("pid")
                    if pid is not None and not self.owns(int(pid)):
                        self.port = None
                        raise NotOurGame(f"port {pf} answers for pid {pid}, which this driver "
                                         "did not start; not driving it")
                    return self.port
            except (OSError, ValueError, IndexError):
                pass
            time.sleep(0.5)
        raise RuntimeError("the game's debug server never answered")

    def quit(self, timeout=60):
        """SIGTERM this driver's own game (it quits at a frame boundary,
        H-9), then let play.sh write its summary.  Never anything else: a
        pid that is not a descendant of our play.sh is left alone."""
        pid = self.game_pid()  # None unless it is ours
        if pid and self.alive():
            os.kill(pid, signal.SIGTERM)
        try:
            return self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            if pid and self.owns(pid):
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError:
                    pass
            return self.proc.wait(30)

    def wait_exit(self, timeout):
        try:
            return self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            return None

    # ---- http ----
    def _req(self, path, data=None, timeout=10):
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}/api{path}",
                                     data=data.encode() if isinstance(data, str) else data,
                                     method="POST" if data is not None else "GET")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()

    def jget(self, path, timeout=10):
        try:
            return json.loads(self._req(path, timeout=timeout))
        except (urllib.error.URLError, OSError, ValueError):
            return None

    @staticmethod
    def guarded(expr, default="-2"):
        """expr evaluated only while a screen is current.  Between two screens
        {ui current_screen} is null, and anything built on it raises a
        MILO_FAIL; the driver must not put its own FAILs in the log it is
        there to read."""
        if "current_screen" not in expr:
            return expr
        return f"{{if_else {{ui current_screen}} {expr} {default}}}"

    def eval(self, expr, timeout=10):
        try:
            r = json.loads(self._req("/dta/eval", self.guarded(expr), timeout))
            return r.get("data", {}).get("value") if r.get("ok") else None
        except (urllib.error.URLError, OSError, ValueError):
            return None

    def screen(self):
        r = self.jget("/screen", 5)
        return (r or {}).get("data", {}).get("screen")

    def wait_screen(self, name, timeout=120):
        """Wall-clock wait (the right way for these screens, STATUS 'Reaching
        Story and Fitness').  name may be a tuple of acceptable screens."""
        names = (name,) if isinstance(name, str) else tuple(name)
        end = time.time() + timeout
        while time.time() < end and self.alive():
            s = self.screen()
            if s in names:
                return s
            time.sleep(0.25)
        return None

    def press(self, *buttons, gap=20, wake=True):
        """Press buttons in order, `gap` frames apart (at least
        MIN_GAP_FRAMES), after a wake."""
        gap = max(int(gap), MIN_GAP_FRAMES)
        seq = [{"button": "l3", "delay": 0, "hold": HOLD_FRAMES}] if wake else []
        if not buttons and not seq:
            return
        for i, b in enumerate(buttons):
            seq.append({"button": b, "delay": MIN_GAP_FRAMES if (i == 0 and wake) else gap,
                        "hold": HOLD_FRAMES})
        try:
            self._req("/input/sequence", json.dumps(seq), 10)
        except (urllib.error.URLError, OSError) as e:
            self.log(f"press {buttons} failed: {e}")
        # the sequence runs over frames; give it wall time to land
        time.sleep(0.15 + 0.04 * gap * len(buttons) / 1.2)

    def list_select(self, panel_list, sym, timeout=30):
        """Move a HamNavList's highlight to `sym` with up/down presses."""
        end = time.time() + timeout
        while time.time() < end:
            want = self.eval(f"{{{panel_list} data_index {sym}}}")
            cur = self.eval(f"{{{panel_list} get_selected}}")
            if want in (None, -2) or cur in (None, -2):  # -2: between screens
                time.sleep(0.5)
                continue
            if int(want) < 0:
                return False
            if cur == want:
                return True
            n = int(want) - int(cur)
            self.press(*(["down"] * n if n > 0 else ["up"] * -n), gap=12)
            time.sleep(1.0)
        return False

    def focus_list(self, name="right_hand.hnl"):
        return f"{{{{{{ui current_screen}} focus_panel}} find {name}}}"

    def has_row(self, lst, sym):
        """Whether the list has a row `sym` (data_index; safe on every
        provider, where data_symbol past the end can raise a MILO_FAIL)."""
        i = self.eval(f"{{{lst} data_index {sym}}}", timeout=3)
        return isinstance(i, int) and i >= 0

    def rows(self, lst, limit=60):
        """The list's row symbols, for logs and for lists that are discovered
        (fitness, playlists).  Reads no further than the provider's row count
        (`num_data`, a native-only HamNavList handler): every provider
        raises a MILO_FAIL for an index past its end (MainMenuProvider 14x in
        one soak; FitnessProvider.cpp:146 in soak #5, PLAN H-28, where
        get_num_display -- the slots on screen -- said 5 of a 4-row list).
        No count, no rows: guessing is what raised those FAILs."""
        n = self.eval(f"{{{lst} num_data}}", timeout=3)
        if not isinstance(n, int) or n <= 0:
            return []
        limit = min(limit, n)
        out = []
        for n in range(limit):
            sym = self.eval(f"{{{lst} data_symbol {n}}}", timeout=3)
            if sym in (None, 0, -2, "0", "") or (out and sym == out[0]):
                break
            out.append(sym)
        return out

    def choose(self, sym, lst=None, timeout=30):
        """Wake the pad, move the list's highlight to `sym`, confirm.  In the
        hand menus the highlight follows the hand, so it starts wherever the
        person's hand is; this does not assume a starting row."""
        lst = lst or self.focus_list()
        self.press(wake=True)
        time.sleep(0.6)
        if not self.list_select(lst, sym, timeout):
            self.log(f"choose {sym}: not in {lst} on {self.screen()}: rows {self.rows(lst)}")
            return False
        time.sleep(0.6)
        self.press("confirm", wake=False)
        return True

    # ---- routes ----
    def need(self, screen, timeout=90):
        got = self.wait_screen(screen, timeout)
        if not got:
            raise RuntimeError(f"expected {screen}, at {self.screen()}")
        time.sleep(1.5)  # the list's enter animation drops presses
        return got

    def boot_to_main(self):
        """With a person on the sensor the title signs them in and moves on by
        itself (as a Kinect does); with none, it needs a press."""
        got = self.wait_screen(("title_screen", "main_screen"), 240)
        if got == "title_screen" and not self.wait_screen("main_screen", 8):
            # By row, not by a bare confirm: the highlight follows the
            # person's hand, and the title's second row is Start the Party
            # (a bare confirm once landed the soak in infinite party mode).
            if not self.choose("title_screen_menu"):
                self.press("confirm")
        self.need("main_screen")

    def perform(self, song, difficulty, mode="perform"):
        """main_screen -> (perform | practice | dance_battle) -> song -> gameplay."""
        if not self.choose("gameplay"):
            raise RuntimeError("main_screen has no perform row")
        self.need("choose_mode_screen")
        if not self.choose(mode):
            raise RuntimeError(f"choose_mode_screen has no {mode} row")
        self.need("song_select_screen")
        self.pick_song(song)
        if mode == "practice":
            # Rehearse picks its difficulty on its own screen (rows easy,
            # medium, expert), then a start screen whose row is
            # break_it_down_title_song (ui/startgame/startgame.dta); with two
            # people present it asks first (practice_confirm_screen, ok).
            self.need("seldiff_practice_screen")
            if not self.choose(difficulty):
                raise RuntimeError(f"seldiff_practice_screen has no {difficulty}")
            got = self.need(("startgame_practice_screen", "game_screen"), 120)
            if got == "startgame_practice_screen":
                if not self.choose("break_it_down_title_song"):
                    raise RuntimeError("startgame_practice_screen has no break_it_down_title_song")
                got = self.need(("practice_confirm_screen", "game_screen"), 120)
                if got == "practice_confirm_screen":
                    self.choose("ok")  # the dialog's rows are ok / no
            return self.need(("game_screen",), 120)
        return self.multiuser(difficulty)

    def pick_song(self, song):
        time.sleep(1.0)
        if not self.choose(song, "{song_select_panel find right_hand.hnl}"):
            raise RuntimeError(f"song_select_screen has no {song}")

    def multiuser(self, difficulty, sides=1):
        self.need("multiuser_screen")
        for side in range(sides):
            lst = f"{{multiuser_panel find right_hand_p{side + 1}.hnl}}"
            if side:
                self.press("left")
                time.sleep(2.5)
            if not self.choose(difficulty, lst):
                raise RuntimeError(f"multiuser_screen has no {difficulty}")
            time.sleep(2.5)
            for sym in ("play", "skip_waiting"):
                if self.has_row(lst, sym):
                    self.choose(sym, lst)
                    time.sleep(2.5)
                if self.screen() != "multiuser_screen":
                    break
        return self.need(("game_screen",), 120)

    def card(self, p=0):
        return {k: self.eval(e) for k, e in (
            ("num_perfect", f"{{{{gamedata getp {p} provider}} get num_perfect}}"),
            ("num_awesome", f"{{{{gamedata getp {p} provider}} get num_awesome}}"),
            ("score", f"{{{{gamedata getp {p} provider}} get score}}"))}

    def screenshot(self, path):
        try:
            png = self._req("/screenshot", timeout=60)
            with open(path, "wb") as f:
                f.write(png)
            return True
        except (urllib.error.URLError, OSError):
            return False


if __name__ == "__main__":
    print(__doc__)
    sys.exit(0)
