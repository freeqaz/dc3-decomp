#!/usr/bin/env python3
"""make_footage.py -- cut camera-shaped clips of a real person dancing DC3 songs
out of the calibration recording, for scripts/play.sh --video and the soak.

The source is the 2.5 h stream listed in scripts/video_calib/mqcXJrH5vUo.json
(someone else's content: not in the repo; download it with yt-dlp into a fresh
directory).  That manifest gives, per song, t0 = the video time of song time 0
and the crop that holds the dancer.  A clip is what a webcam in front of that
person would see:

  crop 1056x1080 at x=280 (the manifest's pose_crop), mirror it back (the
  stream is a selfie view), scale to 470x480, pad to 640x480, 30 fps,
  starting PRE seconds before song time 0 (default 8: the dancer standing).

  make_footage.py --source video.mkv --out DIR [--song S ...] [--pre 8] [--dur 260]
  make_footage.py --source video.mkv --out DIR --duo SONG
        two people for Battle: the same dancer twice, side by side, the right
        one 1.5 s behind, each at half size (a stand-in: no two-person
        footage exists; see docs/native/PLAYTEST.md)

Writes DIR/<song>.mp4 (and DIR/<song>_duo.mp4) and prints them.  Needs ffmpeg.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(REPO, "scripts", "video_calib", "mqcXJrH5vUo.json")
ONE = "crop={crop},hflip,scale=470:480,pad=640:480:(ow-iw)/2:(oh-ih)/2,fps=30"


def songs(manifest):
    with open(manifest) as f:
        m = json.load(f)
    return m, {s["song"]: s for s in m["songs"]}


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"ffmpeg failed: {p.stderr.strip()[-400:]}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--source", required=True, help="the downloaded stream (video.mkv)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest", default=MANIFEST)
    ap.add_argument("--song", action="append", default=[])
    ap.add_argument("--duo", action="append", default=[])
    ap.add_argument("--pre", type=float, default=8.0)
    ap.add_argument("--dur", type=float, default=260.0, help="clip length, s (songs are 3-4 min)")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args(argv)
    m, by = songs(a.manifest)
    if a.list:
        for s in m["songs"]:
            print(f"{s['song']:<24} t0 {s['t0']:8.2f}")
        return 0
    crop = m["layout"]["pose_crop"]
    os.makedirs(a.out, exist_ok=True)
    for name in a.song:
        s = by.get(name) or sys.exit(f"{name}: not in {a.manifest} (--list)")
        out = os.path.join(a.out, f"{name}.mp4")
        run(["ffmpeg", "-v", "error", "-y", "-ss", f"{s['t0'] - a.pre:.3f}", "-t", f"{a.dur:.3f}",
             "-i", a.source, "-an", "-vf", ONE.format(crop=crop), "-c:v", "libx264",
             "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p", out])
        print(out)
    for name in a.duo:
        s = by.get(name) or sys.exit(f"{name}: not in {a.manifest} (--list)")
        out = os.path.join(a.out, f"{name}_duo.mp4")
        lag = 1.5
        half = "crop={crop},hflip,scale=235:240".format(crop=crop)
        graph = (f"[0:v]{half}[l];[1:v]{half}[r];[l][r]hstack=inputs=2,"
                 "pad=640:480:(ow-iw)/2:200,fps=30[v]")
        run(["ffmpeg", "-v", "error", "-y",
             "-ss", f"{s['t0'] - a.pre:.3f}", "-t", f"{a.dur:.3f}", "-i", a.source,
             "-ss", f"{s['t0'] - a.pre - lag:.3f}", "-t", f"{a.dur:.3f}", "-i", a.source,
             "-filter_complex", graph, "-map", "[v]", "-an", "-c:v", "libx264",
             "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p", out])
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
