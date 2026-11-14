#!/usr/bin/env python3
"""fps_probe.py -- game frame rate and pose rate together, windowed, with the
real tracker, through the playtest kit (PLAN L-3).

  fps_probe.py --clip CLIP --song SONG --out DIR [--gpu-lock FILE] [--camera auto|N|PATH]
               [--difficulty medium] [--headless]

Starts scripts/play.sh (windowed unless --headless; DISPLAY must name a
desktop or an Xvfb), plays SONG once with CLIP as the camera (or --camera SPEC, a
real one), and reads the game's own counters out of the session log:

  DC3 PERF: fps=  pose_fps=  persons=   game frames per second, and sensor
                                        frames the game accepted per second
                                        (GestureMgr_Native.cpp, DC3_PERF_LOG)
  POSE_STATS: fps= infer_ms_mean= ...   the tracker's own rate and MediaPipe
                                        inference time (pose_server.py,
                                        DC3_POSE_STATS)

split into menus (before the song) and the song itself.  Prints one table and
writes DIR/fps.json.  Also records the machine (CPU, GPU, cores) so numbers
from two boxes can sit side by side in STATUS.md.
"""
import argparse
import json
import os
import platform
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kitdrive  # noqa: E402

PERF_RE = re.compile(r"DC3 PERF: fps=([\d.]+) pose_fps=([\d.]+) persons=([\d.]+)")
POSE_RE = re.compile(r"POSE_STATS: fps=([\d.]+) infer_ms_mean=([\d.]+) infer_ms_p95=([\d.]+)")


def lines(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace").splitlines()


def stats(xs):
    if not xs:
        return None
    s = sorted(xs)
    return {"n": len(s), "mean": round(sum(s) / len(s), 1), "min": s[0],
            "p10": s[len(s) // 10], "max": s[-1]}


def window(ls):
    perf = [tuple(map(float, m.groups())) for m in map(PERF_RE.search, ls) if m]
    pose = [tuple(map(float, m.groups())) for m in map(POSE_RE.search, ls) if m]
    return {"game_fps": stats([p[0] for p in perf]),
            "pose_accepted_fps": stats([p[1] for p in perf]),
            "persons": stats([p[2] for p in perf]),
            "tracker_fps": stats([p[0] for p in pose]),
            "infer_ms_mean": stats([p[1] for p in pose]),
            "infer_ms_p95": stats([p[2] for p in pose])}


def machine():
    m = {"host": platform.node(), "kernel": platform.release()}
    try:
        cpu = [l for l in open("/proc/cpuinfo") if l.startswith("model name")]
        m["cpu"] = cpu[0].split(":", 1)[1].strip()
        m["threads"] = len(cpu)
    except OSError:
        pass
    try:
        out = subprocess.run(["vulkaninfo", "--summary"], capture_output=True, text=True,
                             timeout=30).stdout
        m["gpu"] = sorted({l.split("=", 1)[1].strip() for l in out.splitlines()
                           if "deviceName" in l})
    except (OSError, subprocess.TimeoutExpired):
        pass
    return m


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--clip")
    ap.add_argument("--camera", default="auto",
                    help="a real camera instead of --clip: auto (default), an index, a path")
    ap.add_argument("--song", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--difficulty", default="medium")
    ap.add_argument("--gpu-lock")
    ap.add_argument("--headless", action="store_true")
    ap.add_argument("--song-seconds", type=float, default=90,
                    help="measure this long into the song, then quit")
    a = ap.parse_args(argv)
    out = os.path.abspath(a.out)
    home = os.path.join(out, "home")
    kitdrive.seed_home(home)
    args = ["--fast-boot"] + (["--headless"] if a.headless else [])
    args += ["--video", os.path.abspath(a.clip)] if a.clip else ["--camera", a.camera]
    env = {"MILO_RENDER": "1"} if a.headless else {}
    kit = kitdrive.Kit(os.path.join(out, "session"), home, args=args, env=env,
                       gpu_lock=a.gpu_lock, log=lambda m: print("  ", m, flush=True))
    marks = {}
    try:
        kit.wait_port()
        kit.boot_to_main()
        marks["menus_from"] = len(lines(kit.engine_log))
        kit.perform(a.song, a.difficulty)
        marks["song_from"] = len(lines(kit.engine_log))
        end = time.time() + a.song_seconds
        while time.time() < end and kit.alive():
            if kit.screen() == "perform_endgame_screen":
                break
            time.sleep(1)
        marks["song_to"] = len(lines(kit.engine_log))
    finally:
        kit.quit()
    ls = lines(kit.engine_log)
    res = {"machine": machine(), "windowed": not a.headless,
           "display": os.environ.get("DISPLAY"), "source": a.clip or f"camera {a.camera}",
           "menus": window(ls[marks.get("menus_from", 0):marks.get("song_from", 0)]),
           "song": window(ls[marks.get("song_from", 0):marks.get("song_to", len(ls))]),
           "session": kit.session}
    with open(os.path.join(out, "fps.json"), "w") as f:
        json.dump(res, f, indent=1)
    print(json.dumps(res["machine"]))
    for part in ("menus", "song"):
        w = res[part]
        def g(k, f="mean"):
            return (w[k] or {}).get(f)
        print(f"{part:<6} game fps mean {g('game_fps')} (p10 {g('game_fps', 'p10')}, min "
              f"{g('game_fps', 'min')}); pose accepted {g('pose_accepted_fps')}/s (p10 "
              f"{g('pose_accepted_fps', 'p10')}); tracker {g('tracker_fps')} fps, inference "
              f"{g('infer_ms_mean')} ms mean, p95 up to {g('infer_ms_p95', 'max')} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
