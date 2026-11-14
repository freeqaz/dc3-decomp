#!/usr/bin/env python3
"""signin_probe.py -- is a person in view signed in at the difficulty screen?
(PLAN P-16)

  signin_probe.py --out DIR [--mode default|forced|auto] [--people 1|2]
                  [--x METRES] [--pose stand|raise_right|...]
                  [--gap S --period S] [--runs N] [--song ymca]

Each run starts dc3-native headless with a synthetic person in view
(scripts/synthetic_kinect.py over the external pose socket: the decompiled
gesture code decides who is present, as with a camera), drives the menus with
the playtest kit's own keyboard driver (kitdrive.Kit: wake, highlight by
name, confirm -- the driver that saw P-16 on the GPD), and on
multiuser_screen reads the panel's sign-in state every 0.5 s for --watch
seconds: each side's pane (seldiff_pane = choosing a difficulty,
readywait_pane = already readied, its only row skip_waiting), presence,
player_provider sides, controller mode, the players' pads.  Then it plays
kitdrive's multiuser() and records whether gameplay started.

--gap/--period make the person drop out of view (gap seconds every period),
the tracker-dropout case; --people 2 puts a second person on the other side.

A run is BAD when side 0's list at multiuser entry does not offer the
difficulties (P-16's "only skip_waiting"), p0 has no pad, or gameplay did not
start.  Writes DIR/signin_<mode>_<x><tag>.json; exit 0 if every run is good,
3 if any is bad.  Mute (DC3_AUDIO_GAIN=0), DC3_DLC=0, private cache/saves.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "scripts"))
sys.path.insert(0, HERE)
from synthetic_kinect import SyntheticKinect  # noqa: E402
import kitdrive  # noqa: E402

DIFFICULTIES = ["beginner", "easy", "medium", "expert"]
PROBES = {
    "s0_pane": "{multiuser_panel get s0_pane}",
    "s1_pane": "{multiuser_panel get s1_pane}",
    "s0_present": "{multiuser_panel get s0_present}",
    "s1_present": "{multiuser_panel get s1_present}",
    "s0_ready": "{multiuser_panel get s0_ready}",
    "skel0": "{multiuser_panel is_skeleton_present 0}",
    "skel1": "{multiuser_panel is_skeleton_present 1}",
    "pp1_present": "{player_provider_1 get player_present}",
    "pp1_side": "{player_provider_1 get side}",
    "pp2_present": "{player_provider_2 get player_present}",
    "pp2_side": "{player_provider_2 get side}",
    "ctrl": "{shell_input in_controller_mode}",
    "p0_pad": "{gamedata getp 0 pad_num}",
    "p1_pad": "{gamedata getp 1 pad_num}",
}


class ProbeKit(kitdrive.Kit):
    """kitdrive.Kit's driving half on an engine this script started itself
    (Kit.__init__ launches play.sh, which needs a camera)."""

    def __init__(self, proc, port, session):  # noqa: super().__init__ not wanted
        self.proc, self.port, self.session = proc, port, session
        self.log = lambda *x: print("[kit]", *x, flush=True)
        self.screens = []

    def owns(self, pid):
        return pid == self.proc.pid

    def alive(self):
        return self.proc.poll() is None


def state(kit):
    return {n: kit.eval(e, timeout=3) for n, e in PROBES.items()}


def one(a, idx):
    work = tempfile.mkdtemp(prefix=f"signin-{a.mode}-{idx}-", dir=a.out)
    kitdrive.seed_home(work)
    sock = os.path.join(work, "kinect.sock")
    k = SyntheticKinect(sock, people=a.people)
    if a.people == 1:
        k.offsets = [a.x]
    if a.pose != "stand":
        k.set_pose(0, a.pose)
    k.start()
    stop = threading.Event()

    def flicker():
        while not stop.wait(a.period):
            k.set_pose(0, "absent")
            stop.wait(a.gap)
            k.set_pose(0, a.pose)
    if a.gap > 0:
        threading.Thread(target=flicker, daemon=True).start()
    portfile = os.path.join(work, "http_port")
    env = dict(os.environ)
    env.pop("MILO_INPUT_SCRIPT", None)
    env.update({
        "MILO_HEADLESS": "1", "MILO_NORENDER": "1", "MILO_FATAL_FAILS": "0",
        "DC3_SHOW_SPLASH": "0", "DC3_FAST_BOOT": "1", "MILO_MAX_FRAMES": "0",
        "DC3_HTTP": "1", "DC3_HTTP_PORT": "0", "DC3_HTTP_PORT_FILE": portfile,
        "DC3_HTTP_PACE_FPS": "60", "DC3_AUDIO_GAIN": "0", "DC3_DLC": "0",
        "DC3_CACHE_DIR": os.path.join(work, "cache"),
        "DC3_SAVE_DIR": os.path.join(work, "saves"),
    })
    if a.mode != "default":
        env["DC3_CONTROLLER_MODE"] = a.mode
    env.update(SyntheticKinect.engine_env(sock))
    log = open(os.path.join(work, "engine.log"), "w")
    proc = subprocess.Popen([a.binary], cwd=os.path.dirname(a.binary), env=env,
                            stdout=log, stderr=subprocess.STDOUT)
    res = {"run": idx, "mode": a.mode, "x": a.x, "people": a.people, "pose": a.pose,
           "gap": a.gap, "work": work}
    try:
        end = time.time() + 120
        port = None
        while time.time() < end and port is None and proc.poll() is None:
            try:
                port = int(open(portfile).read().split()[0])
            except (OSError, ValueError, IndexError):
                time.sleep(0.3)
        if port is None:
            raise RuntimeError("the engine never wrote its port")
        kit = ProbeKit(proc, port, work)
        kit.boot_to_main()
        if not kit.choose("gameplay"):
            raise RuntimeError("main_screen has no perform row")
        kit.need("choose_mode_screen")
        if not kit.choose("perform"):
            raise RuntimeError("choose_mode_screen has no perform row")
        kit.need("song_select_screen")
        kit.pick_song(a.song)
        if not kit.wait_screen("multiuser_screen", 90):
            raise RuntimeError(f"never reached multiuser_screen (at {kit.screen()})")
        t0 = time.time()
        samples = []
        while time.time() - t0 < a.watch:
            s = state(kit)
            s["t"] = round(time.time() - t0, 2)
            s["rows_p1"] = kit.rows("{multiuser_panel find right_hand_p1.hnl}", 8)
            s["rows_p2"] = kit.rows("{multiuser_panel find right_hand_p2.hnl}", 8)
            samples.append(s)
            time.sleep(0.5)
        res["samples"] = samples
        first = samples[0] if samples else {}
        res["side0_offers_difficulty"] = first.get("rows_p1") == DIFFICULTIES
        res["p0_pad"] = first.get("p0_pad")
        try:
            res["multiuser"] = kit.multiuser(a.difficulty)
        except Exception as e:  # noqa: BLE001 -- recorded, not raised
            res["multiuser"] = repr(e)
        res["after"] = dict(state(kit), screen=kit.screen())
    except Exception as e:  # noqa: BLE001
        res["fail"] = repr(e)
    finally:
        proc.terminate()
        try:
            proc.wait(20)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        log.close()
        stop.set()
        k.stop()
    res["good"] = (not res.get("fail") and res.get("side0_offers_difficulty")
                   and res.get("p0_pad") not in (None, -1)
                   and res.get("multiuser") == "game_screen")
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=os.path.join(REPO, "native", "build", "dc3-native"))
    ap.add_argument("--mode", default="default",
                    help="DC3_CONTROLLER_MODE ('default' leaves it unset: auto)")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--people", type=int, default=1)
    ap.add_argument("--x", type=float, default=0.0, help="the one person's x, metres")
    ap.add_argument("--pose", default="stand", help="synthetic_kinect pose of person 0")
    ap.add_argument("--gap", type=float, default=0.0, help="person 0 out of view this long ...")
    ap.add_argument("--period", type=float, default=3.0, help="... every this many seconds")
    ap.add_argument("--song", default="ymca")
    ap.add_argument("--difficulty", default="easy")
    ap.add_argument("--watch", type=float, default=6.0, help="seconds of multiuser samples")
    ap.add_argument("--tag", default="")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    a.binary = os.path.abspath(a.binary)
    os.makedirs(a.out, exist_ok=True)
    results = []
    for i in range(a.runs):
        r = one(a, i)
        results.append(r)
        print(json.dumps({k: r.get(k) for k in ("run", "good", "fail", "side0_offers_difficulty",
                                                 "p0_pad", "multiuser")}), flush=True)
    path = os.path.join(a.out, f"signin_{a.mode}_{a.x}{a.tag}.json")
    with open(path, "w") as f:
        json.dump(results, f, indent=1)
    bad = sum(1 for r in results if not r["good"])
    print(f"[signin] {len(results) - bad} of {len(results)} runs signed in and played; {path}")
    return 3 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
