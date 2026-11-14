#!/usr/bin/env python3
"""Measure dc3-native's CPU use per scenario (menu, song select, gameplay, results).

One engine run walks the four scenarios in order, driven over the HTTP debug
server (scripted input is disabled in windowed runs, so HTTP is the one driver
that works in both modes).  At each scenario it samples, over --seconds of wall
clock:

  * frames advanced (GET /api/frame) -> fps
  * per-thread CPU from /proc/<pid>/task/*/stat (utime+stime), grouped by
    thread name, as % of one core
  * optionally `perf record -g` of the whole process (--perf DIR)

The run uses a private, seeded persistence root (move tutorial seen, as
native/tests/test_dta_flow.cpp's SeedGlobalOptions does) so the flow is the
same on every machine, DC3_POSE=off (the pose server is not ours to measure),
and the song is YMCA (song select: down, confirm), as scripts/dc3-input-flows
/ymca.txt plays it.  Song time runs on the wall clock (no DC3_FAST_TIME), so a
full run takes ~4-5 minutes; --skip-results stops after gameplay.

Usage:
  scripts/native_cpu_profile.py [--windowed] [--seconds 10] [--perf OUTDIR]
                                [--env K=V ...] [--json OUT.json]
  (GPU: run under `flock -s ... gpu1.lock`, outside the sandbox.)
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# ProfileMgr global-options cache with mTutorialsSeen at 0x28 (see
# native/tests/test_dta_flow.cpp SeedGlobalOptions; bytes copied verbatim).
GLOBAL_OPTIONS = bytes([
    0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xc1, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
])
TUTORIALS_SEEN = 1 | 128 | 2048

CLK_TCK = os.sysconf("SC_CLK_TCK")


def seed_root():
    root = tempfile.mkdtemp(prefix="dc3-cpuprof-")
    os.makedirs(os.path.join(root, "cache", "global"))
    os.makedirs(os.path.join(root, "saves"))
    b = bytearray(GLOBAL_OPTIONS)
    b[0x28:0x2C] = TUTORIALS_SEEN.to_bytes(4, "little")
    with open(os.path.join(root, "cache", "global", "global"), "wb") as f:
        f.write(b)
    return root


class Engine:
    def __init__(self, port):
        self.base = "http://127.0.0.1:%d" % port

    def get(self, path, timeout=70):
        with urllib.request.urlopen(self.base + path, timeout=timeout) as r:
            return json.loads(r.read().decode())

    def post(self, path, body):
        req = urllib.request.Request(self.base + path, data=json.dumps(body).encode(),
                                     method="POST")
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.loads(r.read().decode())

    def frame(self):
        return self.get("/api/frame")["data"]["frame"]

    def screen(self):
        return self.get("/api/screen")["data"]["screen"]

    def wait_screen(self, name, timeout_s):
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            try:
                d = self.get("/api/screen/wait/%s?timeout=%d" % (name, 30), timeout=40)
                if d.get("ok") and d["data"].get("screen") == name:
                    return True
            except Exception:
                time.sleep(0.5)
            if self.screen() == name:
                return True
        return False

    def press(self, button):
        self.post("/api/input/press", {"button": button})


def thread_times(pid):
    out = {}
    tdir = "/proc/%d/task" % pid
    for tid in os.listdir(tdir):
        try:
            with open("%s/%s/stat" % (tdir, tid)) as f:
                s = f.read()
        except OSError:
            continue
        comm = s[s.index("(") + 1:s.rindex(")")]
        fields = s[s.rindex(")") + 2:].split()
        out[int(tid)] = (comm, int(fields[11]) + int(fields[12]))
    return out


def power_sources():
    """Readable power sensors (watts): the APU's socket power (amdgpu hwmon
    PPT, power1_average/power1_input) and the battery's discharge rate."""
    srcs = {}
    base = "/sys/class/hwmon"
    for h in sorted(os.listdir(base)) if os.path.isdir(base) else []:
        try:
            name = open(os.path.join(base, h, "name")).read().strip()
        except OSError:
            continue
        for f in ("power1_average", "power1_input"):
            path = os.path.join(base, h, f)
            if os.path.exists(path):
                srcs["%s_W" % name] = path
                break
    return srcs


def read_watts(path):
    try:
        return int(open(path).read().strip()) / 1e6
    except (OSError, ValueError):
        return None


def sample(eng, pid, seconds, perf_out=None):
    perf = None
    srcs = power_sources()
    watts = {k: [] for k in srcs}
    if perf_out:
        perf = subprocess.Popen(
            # task-clock:u + --user-callchain: at perf_event_paranoid=2 a plain
            # `-g` (which asks for kernel callchains too) fails to open any event.
            # dwarf, not fp: libc/libstdc++ and the GPU driver have no frame
            # pointers, so fp unwinding attributes ~70% of samples to no caller.
            ["perf", "record", "-e", "task-clock:u", "--call-graph", "dwarf,16384",
             "--user-callchain", "-F", "199",
             "-p", str(pid), "-o", perf_out,
             "--", "sleep", str(seconds)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    f0, t0, w0 = eng.frame(), thread_times(pid), time.time()
    while time.time() - w0 < seconds:
        for k, path in srcs.items():
            w = read_watts(path)
            if w is not None:
                watts[k].append(w)
        time.sleep(0.25)
    f1, t1, w1 = eng.frame(), thread_times(pid), time.time()
    if perf:
        perf.wait()
    dt = w1 - w0
    by_name = {}
    main_pct = 0.0
    for tid, (comm, ticks) in t1.items():
        d = ticks - t0.get(tid, (comm, 0))[1]
        pct = 100.0 * d / CLK_TCK / dt
        if tid == pid:
            main_pct = pct
            continue
        by_name[comm] = by_name.get(comm, 0.0) + pct
    frames = max(1, f1 - f0)
    return {
        "fps": (f1 - f0) / dt,
        # Main-thread CPU per frame: the load-robust number to compare builds
        # by (fps on a shared machine moves with everyone else's work).
        "main_ms_per_frame": main_pct / 100.0 * dt / frames * 1000.0,
        "main_pct": main_pct,
        "other_pct": sum(by_name.values()),
        "threads": {k: round(v, 1) for k, v in sorted(by_name.items(), key=lambda kv: -kv[1])
                    if v >= 0.5},
        "nthreads": len(t1),
        "seconds": dt,
        "watts": {k: round(sum(v) / len(v), 2) for k, v in watts.items() if v},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=os.path.join(ROOT, "native/build/dc3-native"))
    ap.add_argument("--windowed", action="store_true")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--settle", type=float, default=3.0,
                    help="wall seconds to wait on a screen before sampling")
    ap.add_argument("--gameplay-delay", type=float, default=40.0,
                    help="wall seconds into game_screen before sampling gameplay")
    ap.add_argument("--perf", metavar="DIR")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--json")
    ap.add_argument("--skip-results", action="store_true")
    ap.add_argument("--log", default=None)
    a = ap.parse_args()

    root = seed_root()
    portfile = os.path.join(root, "http_port")
    env = dict(os.environ)
    env.update({
        "DC3_HTTP": "1", "DC3_HTTP_PORT": "0", "DC3_HTTP_PORT_FILE": portfile,
        "DC3_SHOW_SPLASH": "0", "DC3_FAST_BOOT": "1", "MILO_MAX_FRAMES": "0",
        "MILO_FATAL_FAILS": "0", "DC3_POSE": "off",
        "DC3_CACHE_DIR": root + "/cache", "DC3_SAVE_DIR": root + "/saves",
    })
    if a.windowed:
        env.pop("MILO_HEADLESS", None)
    else:
        env["MILO_HEADLESS"] = "1"
    # The song is YMCA by one counted down: pin the disc list, which the dev
    # profile would otherwise extend with <data>/dlc (DC3_DLC); --env wins.
    env.setdefault("DC3_DLC", "0")
    for kv in a.env:
        k, v = kv.split("=", 1)
        env[k] = v
    log = open(a.log or os.path.join(root, "engine.log"), "w")
    proc = subprocess.Popen([a.binary], cwd=os.path.dirname(a.binary), env=env,
                            stdout=log, stderr=subprocess.STDOUT)
    print("engine pid %d, log %s" % (proc.pid, log.name), file=sys.stderr)
    results = {}
    try:
        deadline = time.time() + 120
        while not os.path.exists(portfile):
            if proc.poll() is not None or time.time() > deadline:
                raise SystemExit("engine exited/never bound HTTP (rc=%s)" % proc.poll())
            time.sleep(0.2)
        eng = Engine(int(open(portfile).read().strip()))

        def step(screen, presses, timeout=180):
            if not eng.wait_screen(screen, timeout):
                raise SystemExit("never reached %s (at %s)" % (screen, eng.screen()))
            for delay, btn in presses:
                time.sleep(delay)
                eng.press(btn)

        def measure(name):
            perf_out = os.path.join(a.perf, name + ".perf.data") if a.perf else None
            r = sample(eng, proc.pid, a.seconds, perf_out)
            r["screen"] = eng.screen()
            results[name] = r
            print("%-11s fps %6.1f  main %5.1f%% (%.2f ms/frame)  other %5.1f%%  %s %s" % (
                name, r["fps"], r["main_pct"], r["main_ms_per_frame"], r["other_pct"],
                r["threads"], r["watts"]),
                file=sys.stderr)

        # Boot -> main menu (fast boot auto-advances attract/title).
        step("main_screen", [])
        time.sleep(a.settle)
        measure("menu")
        eng.press("confirm")
        step("choose_mode_screen", [(2.0, "confirm")])
        step("song_select_screen", [])
        time.sleep(a.settle)
        measure("songselect")
        eng.press("down")
        time.sleep(3.0)
        eng.press("confirm")
        step("multiuser_screen", [(2.0, "confirm"), (4.0, "confirm"), (6.0, "confirm")])
        step("game_screen", [], timeout=240)
        time.sleep(a.gameplay_delay)
        measure("gameplay")
        if not a.skip_results:
            step("perform_final_results_screen", [], timeout=420)
            time.sleep(a.settle + 2)
            measure("results")
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    out = json.dumps(results, indent=1)
    if a.json:
        open(a.json, "w").write(out)
    print(out)


if __name__ == "__main__":
    main()
