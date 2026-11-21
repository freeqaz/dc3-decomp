#!/usr/bin/env python3
"""controller_hold_check.py -- a key press keeps the menus; both hands raised give them back (PLAN H-157).

Under the faithful controller-mode policy (a camera is running) the image
hands the menus to the hand cursor 5 s after the last pad press.  On a webcam
at a desk that is the arm of the person at the keyboard, half out of the
picture, moving the highlight.  The press hold (System_Native.cpp,
DC3_CONTROLLER_HOLD_MS) keeps the menus with the keyboard/pad for the hold
after a press, and raising both hands (the image's HandsUpGestureFilter)
gives them to the hands at once.

This launches dc3-native headless with scripts/synthetic_kinect.py as the
sensor (one person standing, arms down), DC3_CONTROLLER_MODE=faithful and a
short hold (--hold-ms, default 20000), paced at 30 frames a second so the
engine's clocks are wall time, and checks, on main_screen:

  - the shell boots out of controller mode, and a press enters it
  - 9 s after the last press it is still in controller mode (the image: 5 s)
  - both hands raised: out of controller mode within 4 s, by the hands
  - that raise, kept up, does not also open the correct-identity overlay
  - a press takes the menus again, and without another one the hold ends them:
    still in controller mode 3 s before the hold is over, out 4 s after it,
    and the exit names the hold as its timeout

Every check is printed as PASS or FAIL.  --env DC3_CONTROLLER_HOLD_MS=0 is the
standing negative control (native/tests/test_native_controller_hold.cpp): the
hold off is the image, and the hold checks must fail.

Exit: 0 every check passed; 1 a check failed; 2 setup error.
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

IMAGE_TIMEOUT_S = 5.0   # the helpbar's controller_mode_timeout (ui/meta.dta)


class SetupError(Exception):
    pass


def run(a):
    os.makedirs(a.workdir, exist_ok=True)
    for d in ("cache", "saves"):
        os.makedirs(os.path.join(a.workdir, d), exist_ok=True)
    seed_all_tutorials_seen(os.path.join(a.workdir, "cache"))
    sock = os.path.join(a.workdir, "kinect.sock")
    engine_log = os.path.join(a.workdir, "engine.log")
    k = SyntheticKinect(sock, people=1)
    k.start()
    env = dict(os.environ)
    for v in ("MILO_INPUT_SCRIPT", "DC3_FAST_BOOT", "DC3_CONTROLLER_MODE_TIMEOUT_MS"):
        env.pop(v, None)
    env.update({
        "MILO_HEADLESS": "1", "MILO_NORENDER": "1", "MILO_FATAL_FAILS": "0",
        "DC3_SHOW_SPLASH": "0", "DC3_FAST_TIME": "1", "MILO_MAX_FRAMES": "0",
        "DC3_AUDIO_GAIN": "0", "DC3_HEARTBEAT_S": "0",
        # 30 engine frames a wall second: the UI clock (1/30 s a frame) and the
        # sensor's are then wall time, like ShellInput's idle timer.
        "DC3_HTTP_PACE_FPS": "30",
        "DC3_CONTROLLER_MODE": "faithful",
        "DC3_CONTROLLER_HOLD_MS": str(a.hold_ms),
        "DC3_CACHE_DIR": os.path.join(a.workdir, "cache"),
        "DC3_SAVE_DIR": os.path.join(a.workdir, "saves"),
    })
    env.pop("MILO_RENDER", None)
    env.update(SyntheticKinect.engine_env(sock))
    env.update(engine_port_env(a.workdir, 0))
    for kv in a.env:
        key, _, val = kv.partition("=")
        env[key] = val
    hold_s = int(env["DC3_CONTROLLER_HOLD_MS"]) / 1000.0
    with open(engine_log, "w") as lf:
        proc = subprocess.Popen([os.path.abspath(a.engine)],
                                cwd=os.path.dirname(os.path.abspath(a.engine)),
                                env=env, stdout=lf, stderr=subprocess.STDOUT,
                                start_new_session=True)
    results = []

    def check(ok, what, detail=""):
        results.append(bool(ok))
        print(f"{'PASS' if ok else 'FAIL'}: {what}{(' [' + detail + ']') if detail else ''}",
              flush=True)

    def log_text():
        with open(engine_log, errors="replace") as lf:
            return lf.read()

    eng = None

    def in_cm():
        v = eng.eval("{shell_input in_controller_mode}")
        return None if v is None else int(v) != 0

    def press(button):
        eng._req("POST", "/input/press", body=json.dumps({"button": button}).encode())

    def wait_screen(name, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if eng.screen() == name:
                return True
            if proc.poll() is not None:
                raise SetupError(f"engine exited ({proc.returncode}) waiting for {name}")
            time.sleep(0.05)
        return False

    def wait_cm(want, timeout):
        """Seconds until in_controller_mode == want, or None after `timeout`."""
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            if in_cm() == want:
                return time.monotonic() - t0
            time.sleep(0.1)
        return None

    try:
        try:
            port = wait_for_port(port_file(a.workdir), proc, timeout=120)
        except EnginePortError as ex:
            raise SetupError(str(ex))
        eng = Engine(f"http://127.0.0.1:{port}/api")
        end = time.monotonic() + 120
        while eng.screen() is None:
            if proc.poll() is not None:
                raise SetupError(f"engine exited early ({proc.returncode})")
            if time.monotonic() > end:
                raise SetupError("engine HTTP server never answered")
            time.sleep(0.2)
        k.follow_engine(eng.base)
        if not wait_screen("title_screen", 180):
            raise SetupError(f"never reached title_screen; at {eng.screen()}")
        k.wait(1.5)
        check(in_cm() is False, "the shell boots out of controller mode (faithful)")

        # The first press outside controller mode only enters it; the second selects.
        press("confirm")
        took = wait_cm(True, 3.0)
        check(took is not None, "a press takes the menus (controller mode)")
        time.sleep(0.5)
        press("confirm")
        if not wait_screen("main_screen", 60):
            raise SetupError(f"never reached main_screen; at {eng.screen()}")
        k.wait(1.5)
        # Screens on the way may leave controller mode by script: take the
        # menus on main_screen itself.  `down` moves the highlight, nothing else.
        press("down")
        time.sleep(0.3)
        press("down")
        last_press = time.monotonic()
        time.sleep(0.5)
        check(in_cm() is True, "in controller mode on main_screen after a press")
        check("Keyboard or controller has the menus" in log_text(),
              "the takeover notice names the way back")

        # -- the hold outlasts the image's 5 s
        wait_s = IMAGE_TIMEOUT_S + 4.0
        time.sleep(max(0.0, last_press + wait_s - time.monotonic()))
        check(in_cm() is True,
              f"still in controller mode {wait_s:.0f} s after the last press (the image: 5 s)",
              f"hold {hold_s:.0f} s")

        # -- both hands raised give the menus to the hands
        k.set_pose(0, "hands_up")
        t = wait_cm(False, 4.0)
        check(t is not None, "both hands raised: out of controller mode within 4 s",
              "%.1f s" % t if t is not None else "still in controller mode")
        check("both hands raised, the hands take the menus" in log_text(),
              "it was the raised hands that took the menus")
        time.sleep(3.0)   # the raise, kept up
        check("correct_identity.milo" not in log_text(),
              "the same raise does not open the correct-identity overlay")
        k.set_pose(0, "stand")
        time.sleep(1.0)

        # -- a press takes them again, and the hold ends without another one
        press("down")
        time.sleep(0.3)
        press("down")
        last_press = time.monotonic()
        time.sleep(0.5)
        check(in_cm() is True, "a press takes the menus again")
        time.sleep(max(0.0, last_press + hold_s - 3.0 - time.monotonic()))
        check(in_cm() is True, f"still in controller mode 3 s before the {hold_s:.0f} s hold is over")
        t = wait_cm(False, 7.0)
        check(t is not None, "out of controller mode within 4 s of the hold's end",
              "%.1f s after the last press" % (time.monotonic() - last_press))
    except SetupError as ex:
        print(f"SETUP: {ex}", flush=True)
        results.append(None)
    finally:
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
    if None in results:
        return 2
    # The idle exit names the timeout in force (ShellInput::ExitControllerMode).
    hold_ms = int(env["DC3_CONTROLLER_HOLD_MS"])
    exits = [ln for ln in log_text().splitlines()
             if ln.startswith("DC3 ControllerMode: exit (immediate=1, was_in=1,")]
    named = [ln for ln in exits if f"timeout_ms={hold_ms})" in ln]
    check(hold_ms > 0 and bool(named), "the idle exit names the hold as its timeout",
          named[-1] if named else (exits[-1] if exits else "no idle exit logged"))
    print(f"{sum(1 for r in results if r)} of {len(results)} checks passed; engine log {engine_log}",
          flush=True)
    return 0 if all(results) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--engine", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--hold-ms", type=int, default=20000,
                    help="DC3_CONTROLLER_HOLD_MS for the run (default 20000)")
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the engine")
    a = ap.parse_args()
    if not os.path.exists(a.engine):
        print(f"engine not found: {a.engine}", file=sys.stderr)
        return 2
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
