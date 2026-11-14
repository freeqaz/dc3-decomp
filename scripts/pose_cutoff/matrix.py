#!/usr/bin/env python3
"""matrix.py -- score a set of pose-fixture variants with the native game and
tabulate them (PLAN H-90).

A VARIANT is a directory holding <song>.dc3pose for each song (cutoff.py
writes them: right_<cut>/, wrong_<cut>/, still_<cut>/).  Each variant is one
native_song_sweep.py run with --performer replay:<dir>/{song}.dc3pose, so the
game's own detector rates the replayed skeleton, Medium, freestyle off.

  python3 scripts/pose_cutoff/matrix.py run --label off --out OUT \
      --variants FX/right_full,FX/right_knees --songs macarena,supersonic \
      [--env DC3_POSE_ESTIMATE=off] [--jobs 3] [--port-base 9700]
  python3 scripts/pose_cutoff/matrix.py table OUT [--label off]

The table prints, per label and variant: summed final score, per-rating
counts, and the mean last-frame detect fraction (`lastfrac`) over rated
moves, from the engine's DC3 MOVE_RATING lines.  --measures 4-27,28-39 splits
the rating counts by measure range (the owner's YMCA session: legs cut, then
legs seen).  A session variant (a
.dc3session file instead of a directory) runs with --performer session:.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
RATING_RE = re.compile(r"DC3 MOVE_RATING p=(\d+) measure=(-?\d+) move=(\S+) rating=(\S*) "
                       r"lastfrac=(-?[\d.]+) score=(-?\d+)")


def variant_name(v):
    return os.path.basename(v.rstrip("/")).replace(".dc3session", "")


def cmd_run(a):
    env = []
    for e in a.env:
        env += ["--env", e]
    rc = 0
    for v in a.variants.split(","):
        out = os.path.join(a.out, a.label, variant_name(v))
        if os.path.exists(os.path.join(out, "summary.json")) and not a.force:
            print(f"skip {out} (summary.json present)", flush=True)
            continue
        if v.endswith(".dc3session"):
            perf, songs, diff, extra = f"session:{v}", a.session_song, a.session_difficulty, []
        else:
            perf, songs, diff = f"replay:{v}/{{song}}.dc3pose", a.songs, a.difficulty
            extra = ["--eval", "{profile_mgr toggle_disable_freestyle}",
                     "--env", "DC3_POSE_PERFORM_REPLAY_SYNC=song"]
        cmd = [sys.executable, os.path.join(REPO, "scripts/native_song_sweep.py"), "--out", out,
               "--binary", a.binary, "--no-gpu", "--jobs", str(a.jobs), "--port-base",
               str(a.port_base), "--songs", songs, "--difficulty", diff, "--performer", perf,
               "--env", "DC3_AUDIO_GAIN=0", "--stall", "90", "--timeout", "500"] + extra + env
        print(" ".join(cmd), flush=True)
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"{a.label}/{variant_name(v)} exit {r.returncode}", flush=True)
        rc = rc or r.returncode
    return rc


def ratings(song_dir, max_measure=None, min_measure=None):
    """[(measure, move, rating, lastfrac, score)] for player 0."""
    path = os.path.join(song_dir, "engine.log")
    rows = []
    if not os.path.exists(path):
        return rows
    for line in open(path, errors="replace"):
        m = RATING_RE.search(line)
        if not m or m.group(1) != "0" or not m.group(4):
            continue
        meas = int(m.group(2))
        if max_measure is not None and meas > max_measure:
            continue
        if min_measure is not None and meas < min_measure:
            continue
        rows.append((meas, m.group(3), m.group(4), float(m.group(5)), int(m.group(6))))
    return rows


def summarize(vdir, measures=None):
    tot = {"score": 0, "n": 0, "bad": 0, "ok": 0, "awesome": 0, "perfect": 0, "frac": 0.0,
           "songs": 0, "failed": 0}
    if not os.path.isdir(vdir):
        return None
    for song in sorted(os.listdir(vdir)):
        sd = os.path.join(vdir, song)
        rj = os.path.join(sd, "result.json")
        if not os.path.isfile(rj):
            continue
        r = json.load(open(rj))
        tot["songs"] += 1
        if not r.get("results"):
            tot["failed"] += 1
        tot["score"] += r.get("final_score") or 0
        lo, hi = measures if measures else (None, None)
        for meas, move, rating, frac, score in ratings(sd, max_measure=hi, min_measure=lo):
            tot["n"] += 1
            key = rating.replace("move_", "")
            if key in tot:
                tot[key] += 1
            tot["frac"] += max(frac, 0.0)
    if tot["n"]:
        tot["frac"] /= tot["n"]
    return tot


def cmd_table(a):
    labels = [a.label] if a.label else sorted(os.listdir(a.out))
    ranges = [None]
    if a.measures:
        ranges = [tuple(int(x) for x in r.split("-")) for r in a.measures.split(",")]
    print(f"{'label':10s} {'variant':16s} {'songs':>5s} {'score':>8s} {'bad':>4s} {'ok':>4s} "
          f"{'awe':>4s} {'perf':>4s} {'frac':>5s}")
    for lab in labels:
        ld = os.path.join(a.out, lab)
        if not os.path.isdir(ld):
            continue
        for v in sorted(os.listdir(ld)):
            for rg in ranges:
                t = summarize(os.path.join(ld, v), rg)
                if not t:
                    continue
                flag = f" ({t['failed']} no results)" if t["failed"] else ""
                if rg:
                    flag += f" measures {rg[0]}-{rg[1]}"
                print(f"{lab:10s} {v:16s} {t['songs']:5d} {t['score']:8d} {t['bad']:4d} {t['ok']:4d} "
                      f"{t['awesome']:4d} {t['perfect']:4d} {t['frac']:5.3f}{flag}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--label", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--variants", required=True, help="comma list of variant dirs / .dc3session")
    r.add_argument("--songs", default="macarena,supersonic,12step,iceicebaby")
    r.add_argument("--difficulty", default="medium")
    r.add_argument("--session-song", default="ymca")
    r.add_argument("--session-difficulty", default="easy")
    r.add_argument("--binary", default=os.path.join(REPO, "native/build/dc3-native"))
    r.add_argument("--jobs", type=int, default=3)
    r.add_argument("--port-base", type=int, default=9700)
    r.add_argument("--env", action="append", default=[])
    r.add_argument("--force", action="store_true")
    t = sub.add_parser("table")
    t.add_argument("out")
    t.add_argument("--label")
    t.add_argument("--measures", help="comma list of LO-HI measure ranges: rating counts per "
                   "range (the score column stays the whole run's)")
    a = ap.parse_args()
    return cmd_run(a) if a.cmd == "run" else cmd_table(a)


if __name__ == "__main__":
    sys.exit(main())
