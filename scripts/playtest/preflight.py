#!/usr/bin/env python3
"""preflight.py -- before a playtest, check everything a session needs and say,
in plain words, what is wrong and what to do about it.  Run by scripts/play.sh
(always, and alone with --check).  Standard library only: it must run on a box
where the pose packages are the thing that is missing.

  preflight.py [--camera auto|N|PATH | --video FILE] [--python PY] [--data DIR]
               [--binary EXE] [--model M] [--no-pose] [--windowed|--headless]
               [--json OUT] [--no-person]

Each check is OK, WARN (the game runs, something is lost) or FAIL (do not
start). Exit 0 when nothing FAILs, 1 otherwise.
"""
import argparse
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import camera_resolve  # noqa: E402  (standard library only)

MODEL_SHA256 = "4eaa5eb7a98365221087693fcc286334cf0858e2eb6e15b506aa4a7ecdcec4ad"
PINNED = {"mediapipe": "1.0.0", "numpy": "2.2.6"}  # native/scripts/pose-requirements.txt

results = []


def add(status, what, detail, fix=""):
    results.append({"status": status, "what": what, "detail": detail, "fix": fix})


def run(cmd, timeout=20, env=None):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
        return p.returncode, p.stdout, p.stderr
    except FileNotFoundError:
        return 127, "", "not found"
    except subprocess.TimeoutExpired:
        return 124, "", "timed out"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def check_binary(a):
    if os.path.isfile(a.binary) and os.access(a.binary, os.X_OK):
        add("OK", "game", f"built: {a.binary}")
    else:
        add("FAIL", "game", f"the game is not built ({a.binary} is missing)",
            "run scripts/playtest/bootstrap.sh --fix")


def check_data(a):
    rc, out, _ = run([sys.executable, "-I", os.path.join(HERE, "asset_manifest.py"), "check",
                      "--data", a.data], timeout=120)
    text = out.strip().replace("\n", " / ")
    if rc == 0:
        add("OK", "game data", text)
    else:
        add("FAIL", "game data", text or f"asset check exit {rc}",
            "copy the game data from the main box (docs/native/PLAYTEST.md, 'Setting up "
            "a machine'), then run scripts/playtest/bootstrap.sh")


def check_display(a):
    if a.headless:
        return
    if os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY"):
        add("OK", "display", "WAYLAND_DISPLAY=%s DISPLAY=%s" % (
            os.environ.get("WAYLAND_DISPLAY", ""), os.environ.get("DISPLAY", "")))
    else:
        add("FAIL", "display", "no desktop session (neither DISPLAY nor WAYLAND_DISPLAY is set)",
            "run this from a terminal on the machine's own desktop, not over plain ssh")


def check_gpu(a):
    if not shutil.which("vulkaninfo"):
        add("WARN", "graphics", "vulkaninfo is not installed, so the GPU was not checked",
            "install vulkan-tools (Arch: pacman -S vulkan-tools)")
        return
    rc, out, err = run(["vulkaninfo", "--summary"], timeout=30)
    names = [l.split("=", 1)[1].strip() for l in out.splitlines() if "deviceName" in l]
    types = [l.split("=", 1)[1].strip() for l in out.splitlines() if "deviceType" in l]
    if rc != 0 or not names:
        why = (err or out).strip().splitlines()[-1:] or ["no output"]
        add("FAIL", "graphics", f"no working Vulkan driver ({why[0][:160]})",
            "install your GPU's Vulkan driver (AMD: vulkan-radeon; NVIDIA: nvidia-utils, "
            "then reboot if the driver was just upgraded)")
        return
    real = [n for n, t in zip(names, types) if "CPU" not in t]
    if real:
        add("OK", "graphics", ", ".join(sorted(set(real))))
    else:
        add("WARN", "graphics", f"only a software renderer ({', '.join(names)}): it will be "
            "far too slow to play", "install your GPU's Vulkan driver")


def check_audio(a):
    if shutil.which("pactl"):
        rc, out, _ = run(["pactl", "info"], timeout=10)
        sink = next((l.split(":", 1)[1].strip() for l in out.splitlines()
                     if l.startswith("Default Sink")), "")
        if rc == 0 and sink and "auto_null" not in sink:
            add("OK", "sound", f"output: {sink}")
            return
        if rc == 0:
            add("WARN", "sound", "the sound server has no output device (no speakers or "
                "headphones?): the game will be silent", "plug in or select an output")
            return
    cards = glob.glob("/proc/asound/card[0-9]*")
    if cards:
        add("WARN", "sound", "no PulseAudio/PipeWire session found; ALSA has "
            f"{len(cards)} card(s), so sound may still work", "")
    else:
        add("WARN", "sound", "no sound device found: the game will be silent", "")


def check_python(a):
    if a.no_pose:
        return False
    py = a.python
    if not py or not os.path.exists(py):
        add("FAIL", "pose packages", f"no Python for the pose tracker ({py or 'none found'})",
            "run scripts/playtest/bootstrap.sh --fix (it makes the venv)")
        return False
    code = ("import json,sys\nout={}\n"
            "for m in ('mediapipe','cv2','numpy'):\n"
            "  try:\n    mod=__import__(m); out[m]=getattr(mod,'__version__','?')\n"
            "  except Exception as e:\n    out[m]='MISSING: %s'%e\n"
            "out['python']=sys.version.split()[0]\nprint(json.dumps(out))")
    rc, out, err = run([py, "-I", "-c", code], timeout=60)
    try:
        v = json.loads(out.strip().splitlines()[-1])
    except (ValueError, IndexError):
        add("FAIL", "pose packages", f"{py} did not run ({err.strip()[:200]})",
            "run scripts/playtest/bootstrap.sh --fix")
        return False
    bad = [m for m in ("mediapipe", "cv2", "numpy") if str(v.get(m, "")).startswith("MISSING")]
    if bad:
        add("FAIL", "pose packages", f"{py} lacks {', '.join(bad)}",
            "run scripts/playtest/bootstrap.sh --fix")
        return False
    off = [f"{m} {v[m]} (tested {want})" for m, want in PINNED.items() if v.get(m) != want]
    desc = f"python {v['python']}, mediapipe {v['mediapipe']}, opencv {v['cv2']}, numpy {v['numpy']}"
    if off:
        add("WARN", "pose packages", desc + "; differs from the tested set: " + ", ".join(off),
            "scripts/playtest/bootstrap.sh --fix rebuilds the venv from "
            "native/scripts/pose-requirements.txt")
    else:
        add("OK", "pose packages", desc + f" ({py})")
    return True


def check_model(a):
    if a.no_pose:
        return False
    if not os.path.isfile(a.model):
        add("FAIL", "pose model", f"{a.model} is missing",
            "run scripts/playtest/bootstrap.sh --fix (downloads it, 9 MB)")
        return False
    if sha256(a.model) != MODEL_SHA256:
        add("WARN", "pose model", f"{a.model} is not the tested model (sha256 differs)",
            "scripts/playtest/bootstrap.sh --fix replaces it")
    else:
        add("OK", "pose model", a.model)
    return True


MIN_CAMERA_FPS = 15   # below this the scorer sees too little of each move
WANT_CAMERA_FPS = 30


def camera_list(r):
    return ", ".join(f"{os.path.basename(n)} '{d}'" + ("" if ok else " (not a camera)")
                     for n, d, ok in r["cameras"]) or "none"


def check_camera(a, have_py, have_model):
    if a.no_pose:
        add("WARN", "camera", "pose tracking is off (--no-pose): nothing you do is scored", "")
        return
    if a.video:
        src = ["--video", a.video, "--at", "30"]
        what = "footage"
        if not os.path.isfile(a.video):
            add("FAIL", what, f"{a.video} does not exist", "")
            return
    else:
        what = "camera"
        r = camera_resolve.resolve(a.camera)
        if r["node"] is None:
            if not any(ok for _, _, ok in r["cameras"]):
                add("FAIL", what, "no camera found (no video-capture device"
                    + (f"; devices: {camera_list(r)})" if r["cameras"] else ")"),
                    "plug in a webcam, check it is not switched off (some laptops have a key "
                    "or a shutter), then run the check again")
            elif str(a.camera).isdigit():
                add("FAIL", what, camera_resolve.missing_index_words(a.camera, r), "")
            else:
                add("FAIL", what, f"{r['error']}; devices: {camera_list(r)}",
                    f"use --camera auto (the default) or the {camera_resolve.BY_ID} name")
            return
        if r["fallback"]:
            # The server falls back to a scan, so the game still gets a
            # camera -- but not the one asked for, and maybe not next time.
            add("WARN", what, r["fallback"] + f"; the game will use {r['node']}", "")
        src = ["--camera", a.camera]
    if not have_py:
        add("WARN", what, "not tested: the pose packages are missing", "")
        return
    cmd = [a.python, "-I", os.path.join(HERE, "camera_probe.py")] + src
    cmd += ["--model", a.model] if (have_model and not a.no_person) else ["--no-pose"]
    rc, out, err = run(cmd, timeout=90)
    try:
        p = json.loads(out.strip().splitlines()[-1])
    except (ValueError, IndexError):
        add("FAIL", what, f"the probe crashed: {err.strip()[-300:]}", "")
        return
    if not p.get("ok"):
        kind = p.get("kind")
        devs = camera_list(p["camera"]) if p.get("camera") else ""
        if kind == "busy":
            users = ", ".join(f"{n} (pid {pid})" for pid, n in p.get("holders", []))
            add("FAIL", what, f"another program is using the camera: {users}",
                "close it (a browser tab, a video call, another game), then run the check again")
        elif kind == "no_frames":
            add("FAIL", what, f"{p.get('error')}: it opened but sent no picture"
                + (f"; devices: {devs}" if devs else ""),
                "check the camera's shutter or privacy switch; if the box has several "
                "cameras, pick one with --camera N or its /dev/v4l/by-id name")
        else:
            add("FAIL", what, f"{p.get('error', 'unknown error')}"
                + (f"; devices: {devs}" if devs else ""),
                "use --camera auto (the default); unplug and replug the camera")
        return
    desc = f"{p['width']}x{p['height']}"
    if p.get("camera"):
        desc = f"{p['camera'].get('desc') or p['camera'].get('node')}, " + desc
    if p.get("fps"):
        desc += f" at {p['fps']} fps measured"
        if p["fps"] < MIN_CAMERA_FPS and not a.video:
            add("WARN", what + " speed",
                f"the camera delivers only {p['fps']} pictures a second ({WANT_CAMERA_FPS} "
                "wanted). The game scores each move from the poses it sees, so at this rate "
                "it misses much of every move and scores feel rough and late (a session at "
                "7.7 a second scored badly)",
                "turn on more light or face a window: in dim light a webcam lengthens its "
                "exposure and slows down. Then run the check again")
    add("OK", what, desc)
    if "advice" in p:
        status = "OK" if p.get("advice", "").startswith("Looks good") else "WARN"
        add(status, "you, in the picture", p["advice"],
            "" if status == "OK" else "see docs/native/PLAYTEST.md, 'Where to stand'")


def main(argv=None):
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--camera", default="auto",
                   help="auto (the first video-capture device), an index, a path or a "
                        "/dev/v4l/by-id name, as pose_server.py --camera")
    g.add_argument("--video")
    ap.add_argument("--python", default="")
    ap.add_argument("--data", default=os.environ.get("DC3_DATA") or os.path.join(REPO, "orig-assets"))
    ap.add_argument("--binary", default=os.path.join(REPO, "native", "build", "dc3-native"))
    ap.add_argument("--model", default=os.path.join(REPO, "native", "models", "pose_landmarker_full.task"))
    ap.add_argument("--no-pose", action="store_true")
    ap.add_argument("--no-person", action="store_true", help="open the camera but do not look for a person")
    ap.add_argument("--headless", action="store_true")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    a.data = os.path.abspath(a.data)

    check_binary(a)
    check_data(a)
    check_display(a)
    check_gpu(a)
    check_audio(a)
    have_py = check_python(a)
    have_model = check_model(a)
    check_camera(a, have_py, have_model)

    width = max(len(r["what"]) for r in results)
    for r in results:
        print(f"  {r['status']:<4}  {r['what']:<{width}}  {r['detail']}")
        if r["fix"] and r["status"] != "OK":
            print(f"  {'':<4}  {'':<{width}}  -> {r['fix']}")
    fails = [r for r in results if r["status"] == "FAIL"]
    warns = [r for r in results if r["status"] == "WARN"]
    print()
    if fails:
        print(f"NOT READY: {len(fails)} problem(s) above must be fixed first.")
    elif warns:
        print(f"Ready, with {len(warns)} warning(s).")
    else:
        print("Ready.")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(results, f, indent=1)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
