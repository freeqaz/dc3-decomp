#!/usr/bin/env python3
"""gpu_pass_ab.py -- GPU time per render pass in gameplay, and an A/B of one
switch inside the same run (engine MILO_GPU_TIMERS, dc3 GET /api/gputime).

  gpu_pass_ab.py [--toggle DTA] [--no-toggle] [--windows K] [--settle S]
                 [--env KEY=VAL ...] [--binary PATH] [--out DIR] [--json FILE]

Boots native/build/dc3-native headless through the ymca input flow (the same
flow and environment as native/tests/test_dta_flow.cpp: fresh cache/save
root, DC3_FAST_BOOT, a 240 fps cap) with MILO_GPU_TIMERS=1 and the debug
server on a free port. Once the song is playing (GET /api/telemetry
gameStage=playing) it skips S timer windows, records K one-second windows
(A), evaluates the toggle DTA on the main thread (POST /api/dta/eval; default
`{rnd toggle_all_postprocs}`, which sends the whole retail post chain to the
identity copy), skips S windows, records K more (B), and quits the game.
Prints per pass: mean ms per frame in A and B, the difference, and passes
per frame -- a pass that the toggle removes reads `-` in B.

The GPU is shared: take the lock, and run with the sandbox off.

  flock -s -w 3600 /home/free/tmp/claude-1000/gpu1.lock \\
      scripts/playtest/gpu_pass_ab.py
  # one post pass, e.g. the DC3 retail chain's bloom only, via its env knob:
  scripts/playtest/gpu_pass_ab.py --no-toggle --env DC3_RETAIL_POST=grade

Exit: 0 table printed; 2 timers unavailable (the reason is printed: no
timestamp-query on this adapter, or a web build); 3 the song never started
within the deadline; 4 the engine exited early.
"""
import argparse
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def http(port, path, body=None, timeout=10):
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}",
                                 data=body.encode() if body is not None else None,
                                 method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def gputime(port):
    return http(port, "/api/gputime")["data"]


def wait_windows(port, proc, n, deadline):
    """The next n published windows (each a dict), never one seen before."""
    out = []
    last = gputime(port).get("window", 0)
    while len(out) < n:
        if time.time() > deadline:
            raise TimeoutError("timer windows stopped arriving")
        if proc.poll() is not None:
            raise ChildProcessError(f"engine exited ({proc.returncode})")
        time.sleep(0.25)
        g = gputime(port)
        if g.get("window", 0) != last:
            last = g["window"]
            if g.get("frames", 0) > 0:
                out.append(g)
    return out


def summarize(windows):
    """name -> (mean ms per frame, max ms, passes per frame), frame-weighted."""
    frames = sum(w["frames"] for w in windows)
    acc = {}
    for w in windows:
        for p in w["passes"]:
            a = acc.setdefault(p["name"], [0.0, 0.0, 0.0])
            a[0] += p["ms"] * w["frames"]
            a[1] = max(a[1], p["maxMs"])
            a[2] += p["n"] * w["frames"]
    rows = {k: (v[0] / frames, v[1], v[2] / frames) for k, v in acc.items()}
    tot = {
        "frames": frames,
        "sum": sum(w["gpuSumMs"] * w["frames"] for w in windows) / frames,
        "span": sum(w["gpuSpanMs"] * w["frames"] for w in windows) / frames,
        "skipped": sum(w["skipped"] for w in windows),
    }
    return rows, tot


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--toggle", default="{rnd toggle_all_postprocs}",
                    help="DTA evaluated between A and B (default: %(default)s)")
    ap.add_argument("--no-toggle", action="store_true", help="only the A table")
    ap.add_argument("--windows", type=int, default=5, help="1 s windows per side")
    ap.add_argument("--settle", type=int, default=2, help="windows skipped before each side")
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VAL for the engine")
    ap.add_argument("--binary", default=os.path.join(REPO, "native", "build", "dc3-native"))
    ap.add_argument("--out", help="session dir (default: a new temp dir)")
    ap.add_argument("--json", help="write both summaries here")
    ap.add_argument("--deadline", type=float, default=300.0, help="wall seconds for the run")
    a = ap.parse_args()

    out = a.out or tempfile.mkdtemp(prefix="gpu-pass-ab-")
    os.makedirs(os.path.join(out, "cache"), exist_ok=True)
    os.makedirs(os.path.join(out, "saves"), exist_ok=True)
    port = free_port()
    env = dict(os.environ)
    env.update({
        "MILO_GPU_TIMERS": "1", "MILO_HEADLESS": "1", "MILO_FATAL_FAILS": "0",
        "DC3_SHOW_SPLASH": "0", "DC3_FAST_BOOT": "1", "DC3_TEL": "api",
        "DC3_FPS_CAP": "240", "DC3_AUDIO_GAIN": "0", "MILO_MAX_FRAMES": "0",
        "DC3_HTTP": "1", "DC3_HTTP_PORT": str(port),
        "DC3_CACHE_DIR": os.path.join(out, "cache"),
        "DC3_SAVE_DIR": os.path.join(out, "saves"),
        "MILO_INPUT_SCRIPT": os.path.join(REPO, "scripts", "dc3-input-flows", "ymca.txt"),
    })
    for kv in a.env:
        k, _, v = kv.partition("=")
        env[k] = v
    log_path = os.path.join(out, "engine.log")
    log = open(log_path, "wb")
    proc = subprocess.Popen([a.binary], env=env, stdout=log, stderr=subprocess.STDOUT,
                            start_new_session=True)
    deadline = time.time() + a.deadline
    code = 0
    try:
        # Server up.
        while True:
            if proc.poll() is not None:
                print(f"engine exited ({proc.returncode}) before its debug server answered; "
                      f"log: {log_path}")
                return 4
            if time.time() > deadline:
                return 3
            try:
                http(port, "/api/health", timeout=2)
                break
            except OSError:
                time.sleep(0.5)
        g = gputime(port)
        if g.get("requested") and not g.get("active") and g.get("unavailable"):
            print(f"GPU timers unavailable: {g['unavailable']}")
            return 2
        # Song playing.
        while True:
            if proc.poll() is not None:
                print(f"engine exited ({proc.returncode}) before the song; log: {log_path}")
                return 4
            if time.time() > deadline:
                print(f"the song did not start within {a.deadline:.0f} s; log: {log_path}")
                return 3
            t = http(port, "/api/telemetry")["data"]
            if t.get("gameStage") == "playing":
                break
            time.sleep(0.5)
        g = gputime(port)
        if not g.get("active"):
            print(f"GPU timers unavailable: {g.get('unavailable') or 'not active'}")
            return 2
        wait_windows(port, proc, a.settle, deadline)
        side_a = wait_windows(port, proc, a.windows, deadline)
        side_b = None
        if not a.no_toggle:
            r = http(port, "/api/dta/eval", a.toggle)
            print(f"toggle {a.toggle} -> {json.dumps(r.get('data', r))[:120]}")
            wait_windows(port, proc, a.settle, deadline)
            side_b = wait_windows(port, proc, a.windows, deadline)
    except (TimeoutError, ChildProcessError) as e:
        print(f"{e}; log: {log_path}")
        return 4
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        log.close()

    gpu = ""
    with open(log_path, "rb") as f:
        m = re.search(rb"GpuDevice: GPU = ([^\n]*)", f.read())
        if m:
            gpu = m.group(1).decode("utf-8", "replace")
    ra, ta = summarize(side_a)
    rb, tb = summarize(side_b) if side_b else ({}, None)
    print(f"GPU: {gpu}")
    print(f"A: {ta['frames']} frames, {len(side_a)} windows, {ta['skipped']} skipped"
          + (f" | B: {tb['frames']} frames, {len(side_b)} windows, {tb['skipped']} skipped"
             if tb else ""))
    names = sorted(set(ra) | set(rb), key=lambda n: -max(ra.get(n, (0,))[0], rb.get(n, (0,))[0]))
    w = max([len(n) for n in names] + [20])
    if tb:
        print(f"{'pass':<{w}} {'A ms':>8} {'B ms':>8} {'B-A':>8} {'A n':>6} {'B n':>6} {'A max':>8}")
    else:
        print(f"{'pass':<{w}} {'ms':>8} {'max':>8} {'n':>6}")
    for n in names:
        a_ = ra.get(n)
        b_ = rb.get(n)
        if tb:
            fa = f"{a_[0]:8.4f}" if a_ else f"{'-':>8}"
            fb = f"{b_[0]:8.4f}" if b_ else f"{'-':>8}"
            d = (b_[0] if b_ else 0.0) - (a_[0] if a_ else 0.0)
            na = f"{a_[2]:6.2f}" if a_ else f"{'-':>6}"
            nb = f"{b_[2]:6.2f}" if b_ else f"{'-':>6}"
            mx = f"{a_[1]:8.4f}" if a_ else f"{'-':>8}"
            print(f"{n:<{w}} {fa} {fb} {d:+8.4f} {na} {nb} {mx}")
        else:
            print(f"{n:<{w}} {a_[0]:8.4f} {a_[1]:8.4f} {a_[2]:6.2f}")
    if tb:
        print(f"{'(frame pass sum)':<{w}} {ta['sum']:8.4f} {tb['sum']:8.4f} {tb['sum'] - ta['sum']:+8.4f}")
        print(f"{'(frame span)':<{w}} {ta['span']:8.4f} {tb['span']:8.4f} {tb['span'] - ta['span']:+8.4f}")
    else:
        print(f"{'(frame pass sum)':<{w}} {ta['sum']:8.4f}")
        print(f"{'(frame span)':<{w}} {ta['span']:8.4f}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"gpu": gpu, "toggle": None if a.no_toggle else a.toggle,
                       "A": {"passes": ra, "total": ta}, "B": {"passes": rb, "total": tb}}, f,
                      indent=1)
    print(f"log: {log_path}")
    return code


if __name__ == "__main__":
    sys.exit(main())
