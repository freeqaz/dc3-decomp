#!/usr/bin/env python3
"""screen_cost.py -- where frame time goes, screen by screen, on one GPU:
GPU time per render pass (MILO_GPU_TIMERS, GET /api/gputime) and the CPU
side of each frame (MILO_FRAME_TIMES: game update, draw, waits) for the main
menu, song select and gameplay in one venue, in one headless run.

  screen_cost.py --out DIR [--venue V] [--song S] [--difficulty D]
                 [--windows K] [--settle S] [--toggle DTA] [--env K=V ...]
                 [--binary PATH] [--icd radeon|nvidia|none]

Boots dc3-native headless (DC3_FAST_BOOT, pose off, every tutorial seen,
fresh saves; the shader/pipeline cache dir is DIR/../cache so a second run is
warm), waits on each of main_screen, song_select_screen and game_screen
(once the song is playing), skips S one-second GPU timer windows, records K,
and tags the frame-times rows recorded meanwhile by frame number. With
--toggle, after the gameplay windows it evaluates the DTA, settles and
records K more (gameplay_b), then evaluates it again (it is meant to be a
toggle), records K more (gameplay_c, the control), and quits.

Writes DIR/summary.json and prints, per screen: frames per wall second, mean
frame period, the mean of each frame-times column that carries time, the GPU
pass sum and span per frame, and every pass's mean ms per frame.

Take the GPU lock exclusively around it; with --icd radeon the run is pinned
to the RADV ICD (VK_DRIVER_FILES) and refuses a log naming another GPU.
"""
import argparse
import csv
import json
import os
import re
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import kitdrive  # noqa: E402

ICDS = {"radeon": "/usr/share/vulkan/icd.d/radeon_icd.json",
        "nvidia": "/usr/share/vulkan/icd.d/nvidia_icd.json"}
FT_COLS = ("period_ms", "cpu_ms", "game_ms", "game_cpu_ms", "draw_ms", "gpu_wait_ms",
           "pace_ms", "acquire_ms", "present_ms", "pt_wait_ms", "pt_end_wait_ms",
           "pipe_ms", "tex_ms", "mesh_ms", "bg_ms", "file_ms")


class DirectKit(kitdrive.Kit):
    """kitdrive's menu driving, around a dc3-native this script starts itself
    (no play.sh, no tracker)."""

    def __init__(self, session, binary, env):
        self.session = os.path.abspath(session)
        self.log = print
        os.makedirs(self.session, exist_ok=True)
        self.engine_log = os.path.join(self.session, "engine.log")
        self.out = open(self.engine_log, "wb")
        self.proc = subprocess.Popen([binary], env=env, stdout=self.out,
                                     stderr=subprocess.STDOUT, start_new_session=True,
                                     cwd=os.path.dirname(binary))
        self.port = None
        self.t0 = time.time()
        self._off = 0
        self.screens = []

    def game_pid(self):
        return self.proc.pid

    def owns(self, pid):
        return pid == self.proc.pid

    def alive(self):
        return self.proc.poll() is None

    def wait_port(self, timeout=240):
        f = os.path.join(self.session, "http_port")
        end = time.time() + timeout
        while time.time() < end and self.alive():
            try:
                with open(f) as fh:
                    self.port = int(fh.read().strip())
                if self.jget("/health", 2):
                    return self.port
            except (OSError, ValueError):
                pass
            time.sleep(0.5)
        return None

    def quit(self, timeout=30):
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGTERM)
            try:
                self.proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid, signal.SIGKILL)
                self.proc.wait()
        self.out.close()


def windows(kit, n, deadline):
    out = []
    g = kit.jget("/gputime") or {}
    last = (g.get("data") or {}).get("window", 0)
    while len(out) < n:
        if time.time() > deadline:
            raise TimeoutError("timer windows stopped arriving")
        if not kit.alive():
            raise ChildProcessError(f"engine exited ({kit.proc.returncode})")
        time.sleep(0.2)
        g = (kit.jget("/gputime") or {}).get("data") or {}
        if g.get("window", 0) != last:
            last = g["window"]
            if g.get("frames", 0) > 0:
                out.append(g)
    return out


PERF = {"on": False}


def measure(kit, label, k, settle, deadline, res):
    windows(kit, settle, deadline)
    perf = None
    if PERF["on"]:
        # The main thread and every other thread of the game, user space only
        # (perf_event_paranoid 2), for the measured windows.
        perf = subprocess.Popen(["perf", "record", "-q", "-F", "499", "-g", "-p",
                                 str(kit.proc.pid), "-o",
                                 os.path.join(kit.session, f"perf_{label}.data")],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    f0, t0 = kit.frame(), time.time()
    ws = windows(kit, k, deadline)
    f1, t1 = kit.frame(), time.time()
    if perf:
        perf.send_signal(signal.SIGINT)
        try:
            perf.wait(timeout=60)
        except subprocess.TimeoutExpired:
            perf.kill()
    res[label] = {"frame0": f0, "frame1": f1, "wall_s": t1 - t0, "windows": ws,
                  "screen": kit.screen(), "uptime": open("/proc/loadavg").read().split()[:3]}
    print(f"[{label}] {kit.screen()} frames {f0}..{f1} in {t1 - t0:.1f} s "
          f"= {(f1 - f0) / (t1 - t0):.1f} fps, load {res[label]['uptime']}", flush=True)


def ab(kit, a, screen, deadline, res):
    """B = toggle, C = untoggle (the A setting again: the control), D = toggle."""
    if not a.toggle or screen not in a.toggle_at.split(","):
        return
    for lab, expr in ((f"{screen}_b", a.toggle), (f"{screen}_c", a.untoggle or a.toggle),
                      (f"{screen}_d", a.toggle)):
        print(f"{lab}: {expr} ->", kit.eval(expr), flush=True)
        measure(kit, lab, a.windows, a.settle, deadline, res)


def write_flow(out, hold):
    """scripts/dc3-input-flows/ymca.txt with every press on main_screen and
    song_select_screen moved `hold` frames later (offsets are relative to the
    block's wait_screen)."""
    src = os.path.join(REPO, "scripts", "dc3-input-flows", "ymca.txt")
    lines, block = [], None
    for ln in open(src):
        t = ln.strip()
        if t.startswith("wait_screen"):
            block = t.split()[1]
        elif t.startswith("+") and block in ("main_screen", "song_select_screen"):
            n, rest = t[1:].split(None, 1)
            ln = f"+{int(n) + hold} {rest}\n"
        lines.append(ln)
    path = os.path.join(out, "flow.txt")
    with open(path, "w") as f:
        f.writelines(lines)
    return path


def summarize(res, ft_path):
    rows = []
    try:
        with open(ft_path) as f:
            rows = list(csv.DictReader(f))
    except OSError:
        pass
    out = {}
    for label, r in res.items():
        ws = r["windows"]
        frames = sum(w["frames"] for w in ws) or 1
        acc = {}
        for w in ws:
            for p in w["passes"]:
                a = acc.setdefault(p["name"], [0.0, 0.0, 0.0])
                a[0] += p["ms"] * w["frames"]
                a[1] = max(a[1], p["maxMs"])
                a[2] += p["n"] * w["frames"]
        passes = {kk: {"ms": v[0] / frames, "max": v[1], "n": v[2] / frames}
                  for kk, v in acc.items()}
        sel = [x for x in rows
               if r["frame0"] is not None and r["frame0"] < int(x["frame"]) <= r["frame1"]]
        ft = {}
        for c in FT_COLS:
            vals = [float(x[c]) for x in sel if c in x and x[c] not in ("", "-1")]
            if vals:
                vals.sort()
                ft[c] = {"mean": sum(vals) / len(vals), "p50": vals[len(vals) // 2],
                         "p95": vals[min(len(vals) - 1, int(len(vals) * 0.95))],
                         "max": vals[-1]}
        out[label] = {
            "screen": r["screen"], "uptime": r["uptime"],
            "fps": (r["frame1"] - r["frame0"]) / r["wall_s"] if r["frame0"] is not None else None,
            "ft_rows": len(sel), "ft": ft,
            "gpu_sum_ms": sum(w["gpuSumMs"] * w["frames"] for w in ws) / frames,
            "gpu_span_ms": sum(w["gpuSpanMs"] * w["frames"] for w in ws) / frames,
            "skipped": sum(w.get("skipped", 0) for w in ws),
            "passes": dict(sorted(passes.items(), key=lambda kv: -kv[1]["ms"])),
        }
    return out


def show(summary, gpu):
    print(f"GPU: {gpu}")
    for label, s in summary.items():
        ft = s["ft"]
        fts = " ".join(f"{c}={ft[c]['mean']:.2f}" for c in FT_COLS if c in ft
                       and ft[c]["mean"] >= 0.05)
        print(f"\n== {label} ({s['screen']}) fps={s['fps']:.1f} load={s['uptime']}")
        print(f"   frame-times (mean ms, {s['ft_rows']} rows): {fts}")
        if "period_ms" in ft:
            print(f"   period p50={ft['period_ms']['p50']:.2f} p95={ft['period_ms']['p95']:.2f} "
                  f"max={ft['period_ms']['max']:.2f}")
        print(f"   GPU pass sum {s['gpu_sum_ms']:.2f} ms, span {s['gpu_span_ms']:.2f} ms "
              f"(skipped {s['skipped']})")
        for name, p in list(s["passes"].items())[:14]:
            print(f"     {name:<34} {p['ms']:7.3f} ms  max {p['max']:7.3f}  n {p['n']:.1f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--venue", default=None, help="meta_performer set_venue_pref")
    ap.add_argument("--song", default="ymca")
    ap.add_argument("--difficulty", default="easy")
    ap.add_argument("--windows", type=int, default=5)
    ap.add_argument("--settle", type=int, default=3)
    ap.add_argument("--toggle", default=None, help="DTA for the B (and D) side")
    ap.add_argument("--untoggle", default=None,
                    help="DTA back to A for the C side (default: --toggle again)")
    ap.add_argument("--toggle-at", default="gameplay",
                    help="screens (main,songselect,gameplay) that get B/C/D sides")
    ap.add_argument("--screens", default="main,songselect,gameplay")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--binary", default=os.path.join(REPO, "native", "build", "dc3-native"))
    ap.add_argument("--icd", default="radeon", choices=["radeon", "nvidia", "none"])
    ap.add_argument("--deadline", type=float, default=540.0)
    ap.add_argument("--hold", type=int, default=1500,
                    help="frames the flow waits on main and song select")
    ap.add_argument("--perf", action="store_true", help="perf record each measured span")
    a = ap.parse_args()
    PERF["on"] = a.perf

    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    home = os.path.join(out, "home")
    kitdrive.seed_home(home)
    cache = os.path.join(os.path.dirname(out), "cache")
    os.makedirs(cache, exist_ok=True)
    env = dict(os.environ)
    env.update({
        "MILO_HEADLESS": "1", "MILO_GPU_TIMERS": "1", "MILO_PERF": "1",
        "MILO_FRAME_TIMES": os.path.join(out, "frame_times.csv"),
        "MILO_FATAL_FAILS": "0", "MILO_MAX_FRAMES": "0",
        "DC3_SHOW_SPLASH": "0", "DC3_FAST_BOOT": "1", "DC3_POSE": "off",
        "DC3_TUTORIALS_SEEN": "1", "DC3_AUDIO_GAIN": "0", "DC3_HTTP": "1",
        "DC3_HTTP_PORT": "0", "DC3_HTTP_PORT_FILE": os.path.join(out, "http_port"),
        "DC3_SAVE_DIR": os.path.join(home, "saves"),
        "DC3_CACHE_DIR": os.path.join(home, "cache"),
        "MILO_PIPELINE_CACHE": cache,
        "DC3_DATA": os.environ.get("DC3_DATA", os.path.join(REPO, "orig-assets")),
    })
    if a.icd != "none":
        env["VK_DRIVER_FILES"] = ICDS[a.icd]
    for kv in a.env:
        k, _, v = kv.partition("=")
        env[k] = v
    env["MILO_INPUT_SCRIPT"] = write_flow(out, a.hold)
    kit = DirectKit(out, os.path.abspath(a.binary), env)
    deadline = time.time() + a.deadline
    res = {}
    screens = a.screens.split(",")
    code = 0
    try:
        if not kit.wait_port():
            print("debug server never answered")
            return 4
        if a.venue:
            print("venue pref:", kit.eval(f"{{meta_performer set_venue_pref {a.venue}}}"))
        # The menus are pressed by the frame-exact ymca flow (MILO_INPUT_SCRIPT),
        # held HOLD frames on main and song select: a press-and-read driver
        # (kitdrive.list_select) overshoots the song list uncapped.
        if "main" in screens:
            if not kit.wait_screen("main_screen", 240):
                raise RuntimeError(f"never reached main_screen ({kit.screen()})")
            measure(kit, "main", a.windows, a.settle, deadline, res)
            ab(kit, a, "main", deadline, res)
        if not kit.wait_screen("song_select_screen", 240):
            raise RuntimeError(f"never reached song_select_screen ({kit.screen()})")
        if "songselect" in screens:
            measure(kit, "songselect", a.windows, a.settle, deadline, res)
            ab(kit, a, "songselect", deadline, res)
        end = time.time() + 120
        while time.time() < end:
            t = (kit.jget("/telemetry") or {}).get("data") or {}
            if t.get("gameStage") == "playing":
                break
            time.sleep(0.5)
        if "gameplay" in screens:
            measure(kit, "gameplay", a.windows, a.settle, deadline, res)
            ab(kit, a, "gameplay", deadline, res)
        res_venue = kit.eval("{meta_performer get_venue}") if a.venue else None
        print("venue now:", res_venue)
    except (TimeoutError, ChildProcessError, RuntimeError) as e:
        print(f"stopped: {e}")
        code = 3
    finally:
        kit.quit()
    gpu = ""
    with open(kit.engine_log, "rb") as f:
        m = re.search(rb"GpuDevice: GPU = ([^\n]*)", f.read())
        gpu = m.group(1).decode("utf-8", "replace") if m else ""
    if a.icd == "radeon" and "RADV" not in gpu:
        print(f"REFUSED: the log names {gpu!r}, not RADV")
        return 5
    summary = summarize(res, os.path.join(out, "frame_times.csv"))
    with open(os.path.join(out, "summary.json"), "w") as f:
        json.dump({"gpu": gpu, "args": vars(a), "screens": summary}, f, indent=1)
    show(summary, gpu)
    return code


if __name__ == "__main__":
    sys.exit(main())
