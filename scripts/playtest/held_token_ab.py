#!/usr/bin/env python3
"""held_token_ab.py -- held-gameplay frame A/B of a native_look token pair, one launch.

  held_token_ab.py --repo DC3_TREE --out DIR --on TOKEN --off TOKEN [--size WxH]
              [--shot P1_area1_far01.shot] [--icd radeon|none] [--seq on,on,off,off,on,off]

Boots like scripts/playtest/screen_cost.py (headless, fast boot, pose off,
the ymca flow), waits for the song to play, holds the frame
({game_panel cheat_pause TRUE}, picking off, one forced shot), then for each
step of --seq sends {rnd native_look TOKEN}, waits, and saves a screenshot.
Prints, for every frame, the number of pixels differing from the first frame
of each setting and from the first frame overall (max abs channel delta too):
same-setting pairs are the no-change control.
"""
import argparse, os, sys, time, re
try:
    import numpy as np
    from PIL import Image
except ImportError:  # the GPD's python3 has no numpy: compare file hashes
    np = None
import hashlib

ap = argparse.ArgumentParser()
ap.add_argument("--repo", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--on", required=True)
ap.add_argument("--off", required=True)
ap.add_argument("--size", default="1268x713")
ap.add_argument("--shot", default="P1_area1_far01.shot")
ap.add_argument("--icd", default="radeon")
ap.add_argument("--seq", default="on,on,off,off,on,off")
ap.add_argument("--settle", type=float, default=2.0)
ap.add_argument("--env", action="append", default=[])
ap.add_argument("--gpuwin", type=int, default=0, help="GPU timer windows per step")
a = ap.parse_args()

sys.path.insert(0, os.path.join(a.repo, "scripts", "playtest"))
import screen_cost as sc  # noqa: E402
import kitdrive  # noqa: E402

out = os.path.abspath(a.out)
os.makedirs(out, exist_ok=True)
home = os.path.join(out, "home")
kitdrive.seed_home(home)
w, h = a.size.split("x")
env = dict(os.environ)
env.update({
    "MILO_HEADLESS": "1", "MILO_FATAL_FAILS": "0", "MILO_MAX_FRAMES": "0",
    "DC3_SHOW_SPLASH": "0", "DC3_FAST_BOOT": "1", "DC3_POSE": "off",
    "DC3_TUTORIALS_SEEN": "1", "DC3_AUDIO_GAIN": "0", "DC3_HTTP": "1",
    "DC3_HTTP_PORT": "0", "DC3_HTTP_PORT_FILE": os.path.join(out, "http_port"),
    "DC3_SAVE_DIR": os.path.join(home, "saves"), "DC3_CACHE_DIR": os.path.join(home, "cache"),
    "MILO_PIPELINE_CACHE": os.path.join(os.path.dirname(out), "cache"),
    "DC3_DATA": os.environ.get("DC3_DATA", os.path.join(a.repo, "orig-assets")),
    "MILO_WIDTH": w, "MILO_HEIGHT": h, "MILO_GPU_TIMERS": "1",
})
os.makedirs(env["MILO_PIPELINE_CACHE"], exist_ok=True)
if a.icd != "none":
    env["VK_DRIVER_FILES"] = sc.ICDS[a.icd]
for kv in a.env:
    k, _, v = kv.partition("=")
    env[k] = v
sc.REPO = a.repo
env["MILO_INPUT_SCRIPT"] = sc.write_flow(out, 300)
kit = sc.DirectKit(out, os.path.join(a.repo, "native", "build", "dc3-native"), env)
frames = []
gpu = []
try:
    assert kit.wait_port(), "no debug server"
    assert kit.wait_screen("song_select_screen", 300), "no song select"
    end = time.time() + 150
    while time.time() < end:
        t = (kit.jget("/telemetry") or {}).get("data") or {}
        if t.get("gameStage") == "playing":
            break
        time.sleep(0.5)
    time.sleep(6)
    for e in ("{$hamdirector set disable_picking TRUE}",
              f"{{$hamdirector force_shot {a.shot}}}", "{game_panel cheat_pause TRUE}"):
        print(e, "->", kit.eval(e), flush=True)
    time.sleep(4)
    for i, st in enumerate(a.seq.split(",")):
        tok = a.on if st == "on" else a.off
        print(f"step {i} {st}: {tok} ->", kit.eval(f"{{rnd native_look {tok}}}"), flush=True)
        time.sleep(a.settle)
        p = os.path.join(out, f"f{i}_{st}.png")
        ok = kit.screenshot(p)
        frames.append((st, p if ok else None))
        print(f"  frame {kit.frame()} saved {ok}", flush=True)
        if a.gpuwin:
            sc.windows(kit, 1, time.time() + 60)  # skip the window the switch landed in
            ws = sc.windows(kit, a.gpuwin, time.time() + 120)
            fr = sum(x["frames"] for x in ws) or 1
            span = sum(x["gpuSpanMs"] * x["frames"] for x in ws) / fr
            per = {}
            for x in ws:
                for q in x["passes"]:
                    per[q["name"]] = per.get(q["name"], 0.0) + q["ms"] * x["frames"] / fr
            gpu.append((st, span, per))
            print(f"  gpu span {span:.3f} ms  " + " ".join(
                f"{k}={v:.3f}" for k, v in sorted(per.items(), key=lambda kv: -kv[1])[:5]), flush=True)
finally:
    kit.quit()

if np is None:
    for i, (st, p) in enumerate(frames):
        print(f"f{i} {st:<4} sha256 {hashlib.sha256(open(p, 'rb').read()).hexdigest()[:16] if p else None}")
imgs = [] if np is None else [(st, np.asarray(Image.open(p).convert("RGB")).astype(int)) for st, p in frames if p]
first = {}
base = imgs[0][1] if imgs else None
if imgs:
    print(f"\nsize {base.shape[1]}x{base.shape[0]}")
for i, (st, im) in enumerate(imgs):
    ref = first.setdefault(st, (i, im))
    d_same = np.abs(im - ref[1]).max(axis=2)
    d0 = np.abs(im - base).max(axis=2)
    print(f"f{i} {st:<4} vs first {st} (f{ref[0]}): {int((d_same > 0).sum())} px differ, max {int(d_same.max())}"
          f" | vs f0 ({imgs[0][0]}): {int((d0 > 0).sum())} px differ, max {int(d0.max())}")
if gpu:
    for st in ("on", "off"):
        v = [g[1] for g in gpu if g[0] == st]
        print(f"{st}: span mean {sum(v)/len(v):.3f} ms over {len(v)} steps: " + " ".join(f"{x:.3f}" for x in v))
    on = [g[1] for g in gpu]
    # per adjacent on/off pair difference (off - on)
    diffs = []
    for i in range(1, len(gpu)):
        if gpu[i][0] != gpu[i-1][0]:
            d = gpu[i][1] - gpu[i-1][1]
            diffs.append(d if gpu[i][0] == "off" else -d)
    if diffs:
        m = sum(diffs) / len(diffs)
        sd = (sum((x - m) ** 2 for x in diffs) / max(1, len(diffs) - 1)) ** 0.5
        print(f"off - on, adjacent pairs: mean {m:+.3f} ms sd {sd:.3f} n {len(diffs)}: " + " ".join(f"{x:+.3f}" for x in diffs))
log = open(kit.engine_log, errors="replace").read()
print("look lines:", len(re.findall(r"\[look\] resolve predict", log)))
for m in re.findall(r"\[RESOLVE\][^\n]*", log)[-6:]:
    print(m)
