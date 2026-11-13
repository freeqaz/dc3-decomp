#!/usr/bin/env python3
"""preflight.py -- before a playtest, check everything a session needs and say,
in plain words, what is wrong and what to do about it.  Run by scripts/play.sh
(always, and alone with --check).  Standard library only: it must run on a box
where the pose packages are the thing that is missing.

  preflight.py [--camera N | --video FILE] [--python PY] [--data DIR]
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


def camera_names():
    out = []
    for d in sorted(glob.glob("/sys/class/video4linux/video*")):
        try:
            name = open(os.path.join(d, "name")).read().strip()
        except OSError:
            name = "?"
        out.append((os.path.basename(d), name))
    return out


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
        cams = camera_names()
        if not cams:
            add("FAIL", what, "no camera found (no /dev/video* device)",
                "plug in a webcam, check it is not switched off (some laptops have a key or "
                "a shutter), then run the check again")
            return
        src = ["--camera", str(a.camera)]
    if not have_py:
        add("WARN", what, "not tested: the pose packages are missing", "")
        return
    cmd = [a.python, "-I", os.path.join(HERE, "camera_probe.py")] + src
    cmd += ["--model", a.model] if (have_model and not a.no_person) else ["--no-pose"]
    rc, out, err = run(cmd, timeout=90)
    try:
        r = json.loads(out.strip().splitlines()[-1])
    except (ValueError, IndexError):
        add("FAIL", what, f"the probe crashed: {err.strip()[-300:]}", "")
        return
    if not r.get("ok"):
        devs = ", ".join(f"{d} ({n})" for d, n in camera_names()) or "none"
        add("FAIL", what, f"{r.get('error', 'unknown error')}; devices: {devs}",
            "close any other program using the camera (browser, video call), or pick "
            "another one with --camera N")
        return
    desc = f"{r['width']}x{r['height']}"
    if r.get("fps"):
        desc += f" at {r['fps']} fps measured"
        if r["fps"] < 24 and not a.video:
            add("WARN", what + " speed", f"the camera delivers only {r['fps']} fps (30 wanted)",
                "more light usually helps: webcams slow down in the dark")
    add("OK", what, desc)
    if "advice" in r:
        status = "OK" if r.get("advice", "").startswith("Looks good") else "WARN"
        add(status, "you, in the picture", r["advice"],
            "" if status == "OK" else "see docs/native/PLAYTEST.md, 'Where to stand'")


def main(argv=None):
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--camera", type=int, default=0)
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
