#!/usr/bin/env python3
"""build_estimate_tool.py -- build estimate_tool, the offline runner of
Dc3PoseEstimate over a dc3-pose-fixture file (PLAN H-91; joint_error.py
scores its output).

  python3 scripts/pose_cutoff/build_estimate_tool.py OUT [--src PoseEstimate_Native.cpp]

Compiles scripts/pose_cutoff/estimate_tool.cpp with the estimator source,
using the flags native/build/compile_commands.json records for that source
(so a configured native build is needed, not a built one).  --src builds
against another copy of the estimator, to compare versions offline.

  estimate_tool MODE GAIN IN OUT     MODE off|tracker|kinematic
"""
import argparse
import json
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(sys.argv[0]))
REPO = os.path.dirname(os.path.dirname(HERE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--src", default=os.path.join(REPO, "native/src/platform/PoseEstimate_Native.cpp"))
    a = ap.parse_args()
    cc_path = os.path.join(REPO, "native/build/compile_commands.json")
    if not os.path.exists(cc_path):
        print(f"{cc_path}: missing -- run scripts/native_configure.sh first", file=sys.stderr)
        return 2
    entries = [e for e in json.load(open(cc_path)) if e["file"].endswith("platform/PoseEstimate_Native.cpp")]
    if not entries:
        print("PoseEstimate_Native.cpp is in no compile command", file=sys.stderr)
        return 2
    e = entries[0]
    args = e["arguments"] if "arguments" in e else shlex.split(e["command"])
    keep, i = [], 1
    while i < len(args):
        x = args[i]
        i += 1
        if x in ("-o", "-MF", "-MT", "-MQ"):
            i += 1
            continue
        if x == "-Xclang":  # CMake's precompiled-header pairs: not wanted here
            i += 1
            continue
        if x == "-c" or x.endswith(".cpp") or x.startswith("-MD") or x == "-MMD" or "pch" in x.lower():
            continue
        keep.append(x)
    cmd = [args[0]] + keep + ["-o", a.out, os.path.join(HERE, "estimate_tool.cpp"), a.src]
    r = subprocess.run(cmd, cwd=e["directory"])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
