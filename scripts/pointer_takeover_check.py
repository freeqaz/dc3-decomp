#!/usr/bin/env python3
"""pointer_takeover_check.py -- the pointer takes the menus by the rule a key press obeys (np-pointer).

The pointer (Pointer_Native.cpp PointerAct) must take the menus from the hand
cursor exactly as a pad press does (ShellInput::OnMsg(ButtonDownMsg) and
HamUI::OnMsg(ButtonDownMsg)): never for a mouse that only moves, through
EnterControllerMode(false) and never on a gameplay panel, with the press
hold's notice, and restarting the idle timer and the hold (PLAN H-157) on
every deliberate act.  And a tap meant to stop a moving list must not select.

This launches dc3-native headless WITH rendering (the pointer hit-tests the
rows where they were drawn) and scripts/synthetic_kinect.py as the sensor (one
person standing), DC3_CONTROLLER_MODE=faithful, a 20 s hold, paced at 30
frames a second, DC3_SONG_BROWSER=0 (the classic song list is a scrolling
HamNavList), and checks:

  - 50 mouse moves over the title leave controller mode off, and log no
    `ControllerMode: enter`
  - a tap on the title (no rows: the pad's A) takes the menus and posts the
    hold's notice
  - main_screen, the hands raised give the menus back; then a tap on a row
    takes them again and they are still the pointer's 9 s later (the image:
    5 s after the last press -- the tap armed the hold)
  - song select: a flick, then a tap 1.2 s later while rows are still owed
    (pending_steps > 0): the tap selects nothing and the screen stays
  - a song reached by taps: taps there log no `ControllerMode: enter` (the
    game's panel is is_gameplay_panel; SKIP when no song is reached)

Needs the GPU: run it with the sandbox off (and the NVIDIA prefix on this box).
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
    for v in ("MILO_INPUT_SCRIPT", "DC3_FAST_BOOT", "DC3_CONTROLLER_MODE_TIMEOUT_MS",
              "MILO_NORENDER", "DC3_POINTER"):
        env.pop(v, None)
    env.update({
        "MILO_HEADLESS": "1", "MILO_FATAL_FAILS": "0",
        "DC3_SHOW_SPLASH": "0", "DC3_FAST_TIME": "1", "MILO_MAX_FRAMES": "0",
        "DC3_AUDIO_GAIN": "0", "DC3_HEARTBEAT_S": "0",
        "DC3_HTTP_PACE_FPS": "30",
        "DC3_CONTROLLER_MODE": "faithful",
        "DC3_CONTROLLER_HOLD_MS": "20000",
        "DC3_SONG_BROWSER": "0",
        "DC3_CACHE_DIR": os.path.join(a.workdir, "cache"),
        "DC3_SAVE_DIR": os.path.join(a.workdir, "saves"),
    })
    env.update(SyntheticKinect.engine_env(sock))
    env.update(engine_port_env(a.workdir, 0))
    for kv in a.env:
        key, _, val = kv.partition("=")
        env[key] = val
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

    def ptr(op, **kw):
        q = "&".join(f"{k}={v}" for k, v in kw.items())
        return eng._req("POST", f"/input/pointer?op={op}" + ("&" + q if q else ""))

    def state():
        return eng._req("GET", "/pointer/state").get("data", {})

    def wait_screen(pred, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            s = eng.screen()
            if s and pred(s):
                return s
            if proc.poll() is not None:
                raise SetupError(f"engine exited ({proc.returncode})")
            time.sleep(0.1)
        return None

    def wait_cm(want, timeout):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            if in_cm() == want:
                return time.monotonic() - t0
            time.sleep(0.1)
        return None

    def row_of(name=None, row=None, scrollable=None):
        for l in state().get("lists", []):
            if not l["rows"] or (name and l["name"] != name):
                continue
            if scrollable is not None and l["scrollable"] != scrollable:
                continue
            r = next((r for r in l["rows"] if row is None or r["row"] == row), None)
            if r:
                return l, r
        return None, None

    def enters():
        return log_text().count("ControllerMode: enter")

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
        if not wait_screen(lambda s: s == "title_screen", 180):
            raise SetupError(f"never reached title_screen; at {eng.screen()}")
        k.wait(1.5)
        if in_cm() is not False:
            raise SetupError("the shell did not boot out of controller mode (faithful)")

        # -- a mouse that only moves takes nothing
        n0 = enters()
        for i in range(50):
            ptr("move", x=100 + (i * 23) % 1100, y=80 + (i * 37) % 560)
            time.sleep(0.05)
        time.sleep(1.0)
        check(in_cm() is False and enters() == n0,
              "50 mouse moves leave controller mode off and log no `ControllerMode: enter`",
              f"in_cm={in_cm()}, enters {enters() - n0}")

        # -- a tap on the title takes the menus, with the notice: the pad's A
        # where no row is drawn, PointerAct between rows
        ptr("tap", x=640, y=400)
        check(wait_cm(True, 3.0) is not None, "a tap on the title takes the menus",
              f"pointer: '{state().get('last')}'")
        check("Keyboard or controller has the menus" in log_text(),
              "and posts the hold's notice")
        time.sleep(0.5)
        for _ in range(4):
            if eng.screen() == "main_screen":
                break
            _, r0 = row_of("right_hand.hnl", 0)
            ptr("tap", x=r0["x"] if r0 else 640, y=r0["y"] if r0 else 400)
            wait_screen(lambda s: s == "main_screen", 8)
        if eng.screen() != "main_screen":
            raise SetupError(f"never reached main_screen; at {eng.screen()}")
        k.wait(2.0)

        # -- give the menus back to the hands, then a row tap takes them for the hold
        k.set_pose(0, "hands_up")
        if wait_cm(False, 6.0) is None:
            raise SetupError("raised hands did not take the menus back")
        k.set_pose(0, "stand")
        time.sleep(1.5)
        lst, r = row_of("right_hand.hnl")
        if not r:
            lst, r = row_of()
        if not r:
            raise SetupError(f"no rows drawn on main_screen: {json.dumps(state())[:300]}")
        ptr("tap", x=r["x"], y=r["y"])
        tapped = time.monotonic()
        check(wait_cm(True, 3.0) is not None, f"a tap on {lst['name']} row {r['row']} takes the menus")
        time.sleep(max(0.0, tapped + 9.0 - time.monotonic()))
        check(in_cm() is True, "the menus are still the pointer's 9 s after the row tap (the image: 5 s)",
              f"on {eng.screen()}")

        # -- song select: a tap meant to stop the list does not select
        for _ in range(8):
            s = eng.screen() or ""
            if "song_select" in s:
                break
            l2, r2 = row_of("right_hand.hnl", 0)
            if not r2:
                time.sleep(1.0)
                continue
            ptr("tap", x=r2["x"], y=r2["y"])
            wait_screen(lambda n: n != s, 10)
            time.sleep(2.0)
        s = eng.screen() or ""
        if "song_select" not in s:
            raise SetupError(f"never reached song select by taps; at {s}")
        time.sleep(3.0)
        sl, _ = row_of(scrollable=True)
        if not sl:
            raise SetupError(f"no scrollable list on {s}: {json.dumps(state())[:400]}")
        top, bot = sl["rows"][0], sl["rows"][-1]
        ptr("drag", x=bot["x"], y=bot["y"], x2=top["x"], y2=top["y"], ms=120)
        time.sleep(1.2)
        st = state()
        owed = st.get("pending_steps", 0)
        sel0 = st.get("tap_selects", 0)
        if owed == 0:
            print(f"NOTE: no rows owed 1.2 s after the flick (steps_done {st.get('steps_done')})",
                  flush=True)
        check(owed != 0, "rows are still owed 1.2 s after the flick (the tap below is not vacuous)",
              f"pending_steps {owed}")
        mid = sl["rows"][len(sl["rows"]) // 2]
        ptr("tap", x=mid["x"], y=mid["y"])
        time.sleep(2.5)
        st = state()
        check(st.get("tap_selects", 0) == sel0 and (eng.screen() or "") == s,
              "the tap that stopped the list selected nothing and the screen stayed",
              f"tap_selects {sel0} -> {st.get('tap_selects')}, screen {s} -> {eng.screen()}, "
              f"pending_steps now {st.get('pending_steps')}, last '{st.get('last')}'")

        # -- a song: a tap there is the game's (is_gameplay_panel), never a takeover
        if not a.no_song:
            reached = None
            end = time.monotonic() + a.song_timeout
            while time.monotonic() < end:
                s = eng.screen() or ""
                if s.startswith("game"):
                    reached = s
                    break
                l3, r3 = row_of(scrollable=True) if "song_select" in s else row_of("right_hand.hnl", 0)
                if r3 and "song_select" in s:
                    r3 = l3["rows"][min(2, len(l3["rows"]) - 1)]
                ptr("tap", x=r3["x"] if r3 else 640, y=r3["y"] if r3 else 400)
                wait_screen(lambda n: n != s, 6)
                time.sleep(1.5)
            if not reached:
                print(f"SKIP: no song reached by taps in {a.song_timeout} s (at {eng.screen()})", flush=True)
            else:
                k.wait(6.0)
                was = in_cm()
                n0 = enters()
                for _ in range(3):
                    ptr("tap", x=640, y=360)
                    time.sleep(1.0)
                check(enters() == n0, f"3 taps during the song ({reached}) log no `ControllerMode: enter`",
                      f"controller mode before {was}, after {in_cm()}, enters +{enters() - n0}, "
                      f"pointer '{state().get('last')}'")
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
    print(f"{sum(1 for r in results if r)} of {len(results)} checks passed; engine log {engine_log}",
          flush=True)
    if None in results:
        return 2
    return 0 if all(results) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--engine", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the engine")
    ap.add_argument("--no-song", action="store_true", help="skip the in-song phase")
    ap.add_argument("--song-timeout", type=float, default=120.0)
    a = ap.parse_args()
    if not os.path.exists(a.engine):
        print(f"engine not found: {a.engine}", file=sys.stderr)
        return 2
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
