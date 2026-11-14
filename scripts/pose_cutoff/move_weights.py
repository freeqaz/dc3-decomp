#!/usr/bin/env python3
"""move_weights.py -- which joints the move scorer weighs, from the engine's
DC3_MOVE_WEIGHTS_LOG lines (PLAN H-90).

  python3 scripts/pose_cutoff/move_weights.py SWEEP_DIR [--song ymca]

SWEEP_DIR is a native_song_sweep.py --out run with --env
DC3_MOVE_WEIGHTS_LOG=1.  Each line carries, per ham2 error node, the node's
share of DetectFrame::LimbPSNR's weight denominator over the move's frames.
Prints, per song and over all songs, the mean share of the legs (knees,
ankles, feet: displacement + position nodes), the feet alone, the ankles,
the knees, the arms and the head/torso, over the moves that have frames,
and how many moves put more than a quarter of their weight on the legs.
"""
import argparse
import os
import re
import sys

GROUPS = {
    "knees": ("l_knee", "r_knee"),
    "ankles": ("l_ankle", "r_ankle"),
    "feet": ("l_foot", "r_foot"),
    "arms": ("l_shoulder", "l_elbow", "l_wrist", "l_hand", "r_shoulder", "r_elbow", "r_wrist",
             "r_hand"),
    "torso": ("hip", "shoulder", "head"),
}


def parse(sweep):
    per_song = {}
    for song in sorted(os.listdir(sweep)):
        log = os.path.join(sweep, song, "engine.log")
        if not os.path.isfile(log):
            continue
        moves = {}
        for line in open(log, errors="replace"):
            if "DC3 MOVE_WEIGHTS move=" not in line:
                continue
            m = re.search(r"move=(\S+) frames=(\d+) (.*)", line)
            if not m or int(m.group(2)) == 0:
                continue
            shares = {}
            for kv in m.group(3).split():
                k, v = kv.split("=")
                base = k.rsplit("_", 1)[0]
                shares[base] = shares.get(base, 0.0) + float(v)
            moves[m.group(1)] = shares
        if moves:
            per_song[song] = moves
    return per_song


def summarize(moves):
    n = len(moves)
    out = {g: sum(sum(s.get(j, 0.0) for j in js) for s in moves.values()) / n
           for g, js in GROUPS.items()}
    legs = [sum(s.get(j, 0.0) for g in ("knees", "ankles", "feet") for j in GROUPS[g])
            for s in moves.values()]
    out["legs"] = sum(legs) / n
    out["legheavy"] = sum(1 for x in legs if x > 0.25)
    out["n"] = n
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sweep")
    ap.add_argument("--song")
    a = ap.parse_args()
    per_song = parse(a.sweep)
    if a.song:
        per_song = {k: v for k, v in per_song.items() if k.startswith(a.song)}
    cols = ("legs", "knees", "ankles", "feet", "arms", "torso")
    print(f"{'song':24s} {'moves':>5s} " + " ".join(f"{c:>6s}" for c in cols) + "  legs>25%")
    every = {}
    for song, moves in per_song.items():
        s = summarize(moves)
        every.update({song + "/" + k: v for k, v in moves.items()})
        print(f"{song:24s} {s['n']:5d} " + " ".join(f"{s[c]:6.3f}" for c in cols)
              + f"  {s['legheavy']}")
    if every:
        s = summarize(every)
        print(f"{'ALL':24s} {s['n']:5d} " + " ".join(f"{s[c]:6.3f}" for c in cols)
              + f"  {s['legheavy']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
