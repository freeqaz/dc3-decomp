#!/usr/bin/env python3
"""Seconds from launching dc3-native to the main menu, as a player gets there.

    python3 -I scripts/native_boot_time.py [--runs 3] [--binary native/build/dc3-native]
        [--console returning|first] [--headless] [--env K=V ...] [--keep]

One private persistence root (DC3_CACHE_DIR / DC3_SAVE_DIR). `--console
returning` (the default) first boots it once to the main menu, so the global
options and the profile save are on disk, the way they are on a console that
has been played; `--console first` leaves it empty for every run.

Each measured run starts the engine with NO boot switch set (DC3_FAST_BOOT and
DC3_SHOW_SPLASH are removed from the environment; --env adds your own) and an
player who presses A as soon as each screen takes it:

    attract_screen   A (skips the intro movie)
    title_screen     A

(a window takes real keys, sent with xdotool; --headless uses an input script).

It prints when each screen was entered, measured on the wall clock from the
fork, and stops the engine at main_screen.

The engine runs WINDOWED, muted, in a private Xvfb (no window manager needed),
because that is the only run whose clocks are a player's: a window is paced at
60 fps and UI time is the wall clock. `--headless` (MILO_HEADLESS,
DC3_FPS_CAP=60) is there for a box with no Xvfb, and it is NOT the same
measurement: headless advances UI time by 1/30 s per frame whatever the frame
rate (UIManager::Poll), so at 60 fps every game-timed wait (the 4 s autosave
warning) takes half as long as it does for a player.

Needs a GPU (run outside the sandbox). Bounded: each run is killed after
--timeout seconds (default 180) and reported as a timeout, never as a time.
"""
import argparse
import os
import pty
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FLOW = """\
# scripts/native_boot_time.py: a player with a pad, as fast as the game allows.
# Each wait_screen times out on its own when a boot never shows that screen.
wait_screen attract_screen
+28 wake
+30 confirm
wait_screen title_screen
+43 wake
+45 confirm
wait_screen main_screen
"""

SCREEN_RE = re.compile(rb"DC3 UI: Screen '([^']+)' Enter \(from")
EXIT_RE = re.compile(rb"DC3 UI: Screen '([^']+)' Exit \(to")
MARKS = [
    (re.compile(rb"DC3 Boot: (.*)"), "boot"),
    (re.compile(rb"DC3 Native: frame cap: (.*)"), "frame cap"),
    # The engine's [PERF] line: "N fps" before engine gfx/PerfWindow.h, "N frames in
    # S s (R a second)" after it.
    (re.compile(rb"(\[PERF\] (?:[0-9.]+ fps|[0-9]+ frames in [0-9.]+ s \([0-9.]+ a second\)))"), "frame rate"),
]
# A window takes real keys, not the input script (Joypad_Native.cpp): Enter is
# A.  The first press on a screen comes this long after it was entered (the
# title list ignores buttons while its enter animation runs), then one every
# half second until the screen is left.
PRESS_AFTER = {"attract_screen": 0.5, "title_screen": 0.75}
PRESS_EVERY = 0.5


def run_once(binary, env, timeout, stop_at="main_screen"):
    """Returns (screens [(name, seconds)], notes, outcome)."""
    master, slave = pty.openpty()  # a tty: the engine's stdout is line-buffered
    t0 = time.monotonic()
    proc = subprocess.Popen([binary], env=env, cwd=REPO, stdin=subprocess.DEVNULL,
                            stdout=slave, stderr=slave, start_new_session=True)
    os.close(slave)
    screens, notes, buf, outcome = [], [], b"", "timeout"
    windowed = "MILO_HEADLESS" not in env
    current, entered, pressed, presses = None, 0.0, 0.0, 0
    deadline = t0 + timeout
    try:
        while time.monotonic() < deadline:
            r, _, _ = select.select([master], [], [], 0.05)
            now = time.monotonic() - t0
            if (windowed and current in PRESS_AFTER and now - entered >= PRESS_AFTER[current]
                    and now - pressed >= PRESS_EVERY):
                pressed = now
                presses += 1
                subprocess.run(["xdotool", "key", "Return"], env=env, timeout=10,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if not r:
                if proc.poll() is not None:
                    outcome = "exited %s before %s" % (proc.returncode, stop_at)
                    break
                continue
            try:
                chunk = os.read(master, 65536)
            except OSError:
                chunk = b""
            if not chunk:
                outcome = "exited before %s" % stop_at
                break
            buf += chunk
            *lines, buf = buf.split(b"\n")
            done = False
            for line in lines:
                m = SCREEN_RE.search(line)
                if m:
                    name = m.group(1).decode()
                    screens.append((name, now))
                    current, entered = name, now
                    if name == stop_at:
                        done = True
                m = EXIT_RE.search(line)
                if m and m.group(1).decode() == current:
                    current = None
                for rx, label in MARKS:
                    m = rx.search(line)
                    if m:
                        notes.append("%s: %s" % (label, m.group(1).decode(errors="replace").strip()))
            if done:
                outcome = "ok"
                break
    finally:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()
        os.close(master)
    if windowed:
        notes.append("key presses sent: %d" % presses)
    return screens, notes, outcome


def start_xvfb():
    """A private X server on a free display. Returns (process, ":N")."""
    if not shutil.which("Xvfb") or not shutil.which("xdotool"):
        sys.exit("needs Xvfb and xdotool (or pass --headless; read the caveat in --help)")
    r, w = os.pipe()
    proc = subprocess.Popen(["Xvfb", "-displayfd", str(w), "-screen", "0", "1280x720x24",
                             "-nolisten", "tcp"], pass_fds=[w],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(w)
    ready, _, _ = select.select([r], [], [], 20)
    num = os.read(r, 32).decode().strip() if ready else ""
    os.close(r)
    if not num:
        proc.kill()
        sys.exit("Xvfb did not come up within 20 s")
    return proc, ":" + num


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=os.path.join(REPO, "native", "build", "dc3-native"))
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--console", choices=["returning", "first"], default="returning")
    ap.add_argument("--headless", action="store_true",
                    help="no window: UI time runs at 2x (see above), so not a player's time")
    ap.add_argument("--env", action="append", default=[], metavar="K=V")
    ap.add_argument("--timeout", type=float, default=180)
    ap.add_argument("--keep", action="store_true", help="keep the persistence root")
    a = ap.parse_args()
    if not os.access(a.binary, os.X_OK):
        sys.exit("no engine at %s (cmake --build native/build --target dc3-native)" % a.binary)

    root = tempfile.mkdtemp(prefix="dc3-boot-time-")
    flow = os.path.join(root, "boot.txt")
    with open(flow, "w") as f:
        f.write(FLOW)

    xvfb, display = (None, None) if a.headless else start_xvfb()

    def env_for(persist, extra):
        env = dict(os.environ)
        for k in ("DC3_FAST_BOOT", "DC3_SHOW_SPLASH", "DC3_BOOT_INTRO", "MILO_INPUT_SCRIPT",
                  "MILO_MAX_FRAMES", "MILO_HEADLESS", "DC3_FPS_CAP", "WAYLAND_DISPLAY"):
            env.pop(k, None)
        env.update({
            "MILO_FATAL_FAILS": "0", "DC3_HTTP": "0", "DC3_DLC": "0", "DC3_AUDIO_GAIN": "0",
            "DC3_CACHE_DIR": os.path.join(persist, "cache"),
            "DC3_SAVE_DIR": os.path.join(persist, "saves"),
            "MILO_INPUT_SCRIPT": flow,
        })
        if a.headless:
            env.update({"MILO_HEADLESS": "1", "DC3_FPS_CAP": "60"})
        else:
            env["DISPLAY"] = display
        env.update(extra)
        return env

    extra = dict(kv.split("=", 1) for kv in a.env)
    results = []
    try:
        persist = os.path.join(root, "console")
        if a.console == "returning":
            # One boot to the main menu writes the global options and the save.
            seed_env = env_for(persist, {"DC3_FAST_BOOT": "1", "DC3_SHOW_SPLASH": "0"})
            screens, _, outcome = run_once(a.binary, seed_env, a.timeout)
            have = os.path.exists(os.path.join(persist, "cache", "global", "global"))
            print("seed boot: %s; global options on disk: %s" % (outcome, have))
            print("    " + "  ".join("%s %.1f" % (n, t) for n, t in screens))
            if outcome != "ok" or not have:
                sys.exit("could not make a returning console in %s" % persist)
        for i in range(a.runs):
            if a.console == "first":
                persist = os.path.join(root, "first-%d" % i)
            screens, notes, outcome = run_once(a.binary, env_for(persist, extra), a.timeout)
            main_t = next((t for n, t in screens if n == "main_screen"), None)
            results.append(main_t if outcome == "ok" else None)
            print("run %d (%s console%s): %s" % (
                i + 1, a.console, "".join(" %s=%s" % kv for kv in extra.items()), outcome))
            for n in notes:
                print("    " + n)
            print("    " + "  ".join("%s %.1f" % (n, t) for n, t in screens))
    finally:
        if xvfb:
            xvfb.kill()
            xvfb.wait()
        if a.keep:
            print("kept: %s" % root)
        else:
            shutil.rmtree(root, ignore_errors=True)
    ok = [t for t in results if t is not None]
    if ok:
        print("BOOT-TO-MAIN %s console, %s: %s s (median %.1f, %d of %d runs)" % (
            a.console, "headless (UI time 2x)" if a.headless else "windowed",
            " ".join("%.1f" % t for t in ok), sorted(ok)[len(ok) // 2], len(ok), len(results)))
    if len(ok) != len(results):
        print("BOOT-TO-MAIN: %d run(s) did not reach the main menu" % (len(results) - len(ok)))
        sys.exit(1)


if __name__ == "__main__":
    main()
