#!/usr/bin/env python3
"""footage_camera.py -- a recorded person in place of the webcam, in step with
the game's song clock, as if a camera were pointed at someone dancing along.

It is a drop-in for native/scripts/pose_server.py: the game starts it through
DC3_POSE_SERVER with pose_server's own command line (scripts/play.sh --video
sets that up), and it runs pose_server's real code -- the MediaPipe backend,
the wire protocol, the pacing -- with only cv2.VideoCapture replaced.

Which frame is shown:
  * outside a song (any screen but game_screen), frame 0 of the clip: the
    dancer standing, which is what a person waiting in front of the sensor
    looks like;
  * during a song, clip time = (now - W0) + PRE, where W0 is the wall time of
    song time 0, measured by polling the game's own debug server
    ({taskmgr seconds} on game_screen, median of the last 15 samples), and
    PRE is how many seconds of clip come before song time 0;
  * past the clip's end, its last frame.

Clips: --video FILE from the command line (and DC3_FOOTAGE_PRE, default 8),
or, when DC3_FOOTAGE_CONTROL names a file, the clip and pre-roll written there
("CLIP PRE" on one line; re-read every half second), so a driver can change
the dancer between songs.  make_footage.py cuts clips in this shape.

Needs DC3_HTTP_PORT_FILE (play.sh sets it) to find the game.  Without it the
clip just plays from the start, like pose_server.py --video.
"""
import json
import os
import statistics
import sys
import threading
import time
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))
import cv2 as real_cv2  # noqa: E402
import pose_server  # noqa: E402

PRE = float(os.environ.get("DC3_FOOTAGE_PRE", "8"))
CONTROL = os.environ.get("DC3_FOOTAGE_CONTROL")
PORT_FILE = os.environ.get("DC3_HTTP_PORT_FILE")


def log(msg):
    print(f"FOOTAGE: {msg}", file=sys.stderr, flush=True)


class Sync:
    """W0 (wall time of song time 0) from the game's debug server."""

    def __init__(self):
        self.w0 = None
        self.screen = None
        self.samples = []
        self.port = None
        self.lock = threading.Lock()
        threading.Thread(target=self.loop, daemon=True).start()

    def eval(self, expr):
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}/api/dta/eval",
                                     data=expr.encode(), method="POST")
        with urllib.request.urlopen(req, timeout=2) as r:
            return json.loads(r.read())["data"]["value"]

    def current_screen(self):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/api/screen", timeout=2) as r:
            return (json.loads(r.read()).get("data") or {}).get("screen")

    def loop(self):
        while True:
            time.sleep(0.5)
            try:
                if self.port is None:
                    if not PORT_FILE:
                        continue
                    with open(PORT_FILE) as f:
                        self.port = int(f.read().split()[0])
                    log(f"syncing to the game on port {self.port}")
                # /api/screen, not {{ui current_screen} name}: between screens
                # current_screen is null, and the DTA form raises a MILO_FAIL
                # (and answers 0) every time it is asked then.
                screen = self.current_screen()
                t1 = time.time()
                secs = float(self.eval("{taskmgr seconds}"))
                t2 = time.time()
            except Exception:  # noqa: BLE001 -- the game is booting or busy
                continue
            if not screen:
                continue  # between two screens: keep what we had
            with self.lock:
                if screen != self.screen:
                    log(f"screen {screen}")
                    self.screen = screen
                if screen != "game_screen" or secs < -15:
                    if self.w0 is not None:
                        log(f"left the song ({screen}): holding the last frame")
                    self.samples, self.w0 = [], None
                    continue
                self.samples = (self.samples + [(t1 + t2) / 2 - secs])[-15:]
                w0 = statistics.median(self.samples)
                if self.w0 is None:
                    log(f"song clock found: song time {secs:.2f} s")
                elif abs(w0 - self.w0) > 0.5:
                    # A jump: restart, rewind (practice), a pause.  Start over.
                    log(f"song clock jumped by {w0 - self.w0:+.2f} s")
                    self.samples = [(t1 + t2) / 2 - secs]
                    w0 = self.samples[0]
                self.w0 = w0

    def get(self):
        with self.lock:
            return self.w0


class Cap:
    def __init__(self, path):
        self.default = (path, PRE)
        self.sync = Sync() if PORT_FILE else None
        self.cap = None
        self.clip = None
        self.control_mtime = None
        self.last_control = 0.0
        self.open(*self.default)

    def open(self, path, pre):
        if self.cap is not None:
            self.cap.release()
        self.cap = real_cv2.VideoCapture(path)
        if not self.cap.isOpened():
            log(f"cannot open {path}")
        self.fps = self.cap.get(real_cv2.CAP_PROP_FPS) or 30.0
        ok, self.first = self.cap.read()
        self.cur = self.first
        self.idx = 0
        self.eof = not ok
        self.clip = (path, pre)
        self.pre = pre
        log(f"clip {path} (pre-roll {pre:.2f} s, {self.fps:.2f} fps)")

    def control(self):
        if not CONTROL or time.time() - self.last_control < 0.5:
            return
        self.last_control = time.time()
        try:
            st = os.stat(CONTROL)
            if st.st_mtime == self.control_mtime:
                return
            self.control_mtime = st.st_mtime
            with open(CONTROL) as f:
                parts = f.read().split()
            want = (parts[0], float(parts[1]) if len(parts) > 1 else PRE)
        except (OSError, ValueError, IndexError):
            return
        if want != self.clip:
            self.open(*want)

    # cv2.VideoCapture interface used by pose_server
    def isOpened(self):  # noqa: N802
        return self.cap.isOpened()

    def get(self, prop):
        if prop == real_cv2.CAP_PROP_FPS:
            return self.fps
        return self.cap.get(prop)

    def set(self, *a):
        return True

    def release(self):
        self.cap.release()

    def read(self):
        self.control()
        w0 = self.sync.get() if self.sync else None
        if self.sync and w0 is None:
            # Not in a song (menus, a pause, the results): hold the frame
            # last shown -- the dancer stays where she stopped.  Going back to
            # the clip's first frame here left a paused Cupid Shuffle with
            # nobody in view (that clip's first frame is empty), so the pause
            # menu's Resume stayed disabled for 6 minutes (pause.dta enables
            # it only with a player on the sensor).  The next song reopens the
            # clip from its start (want < idx - 1 below).
            return True, self.cur.copy()
        t = (time.time() - w0 + self.pre) if w0 is not None else None
        want = int(t * self.fps) if t is not None else self.idx + 1
        if want < 0:
            # Before the clip's first frame (a count-in longer than the
            # pre-roll: Dance Battle starts at -10.3 s): the standing frame.
            # Reopening here, as below, reopened the file on every read.
            return True, self.first.copy()
        if want < self.idx - 1:
            self.open(*self.clip)
        while self.idx < want and not self.eof:
            ok, fr = self.cap.read()
            if not ok:
                self.eof = True
                break
            self.idx += 1
            self.cur = fr
        return True, self.cur.copy()


class FakeCv2:
    def __getattr__(self, name):
        return getattr(real_cv2, name)

    def VideoCapture(self, path, *rest):  # noqa: N802
        return Cap(path)


def main(argv):
    # Our pace is the song clock, not the file: drop --loop.
    argv = [x for x in argv if x != "--loop"]
    args = pose_server.parse_args(argv)
    if not args.video:
        log("needs --video CLIP (scripts/play.sh --video sets it)")
        return 2
    try:
        pose_server.run(args, cv2=FakeCv2())
    except pose_server.ServerExit as e:
        print(f"POSE_SERVER_ERROR exit={e.code}: {e}", file=sys.stderr)
        return e.code
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
