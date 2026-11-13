#!/usr/bin/env python3
"""Compare native replay runs with the original game's results cards.

Reads the manifest (scripts/video_calib/<video>.json: per song the card the
original showed -- gems = num_perfect, green = num_awesome, score,
moves-passed percent) and one or more native_song_sweep.py output dirs, each
run with the song's MediaPipe fixture (DC3_POSE_PERFORM=replay:...).  The
native side's card comes from result.json "card" (read from the engine at the
results screen, the same provider fields the card draws), so both sides
count exactly the same moves.

Per run dir it prints one row per song and a summary line:
  P / A       ours vs the original's perfect / awesome counts
  rest        moves counted but neither (ok + bad), derived from the
              per-move log over the card's window
  score       ours / original
and the summary: total perfect and awesome over all songs, mean |dP|, and
the score ratio.  `--json` writes the same numbers.

  python3 -I scripts/video_calib/compare_cards.py --manifest M.json RUN_DIR [RUN_DIR ...]
"""
import argparse
import json
import os
import re
import sys

RATING_RE = re.compile(r"DC3 MOVE_RATING p=0 measure=(-?\d+) move=(\S+) rating=(\S*) "
                       r"lastfrac=(-?[\d.]+) score=(-?\d+)")


def per_move(log):
    out = []
    for line in open(log, errors="replace"):
        m = RATING_RE.search(line)
        if m and m.group(3):
            out.append((int(m.group(1)), m.group(2), m.group(3), float(m.group(4)), int(m.group(5))))
    return out


def run_table(manifest, run_dir):
    rows = []
    for s in manifest["songs"]:
        if not s.get("card") or s.get("exclude"):
            continue
        rj = os.path.join(run_dir, s["song"], "result.json")
        if not os.path.exists(rj):
            continue
        r = json.load(open(rj))
        card = r.get("card") or {}
        moves = per_move(os.path.join(run_dir, s["song"], "engine.log"))
        hist = {}
        for _, _, rating, _, _ in moves:
            hist[rating] = hist.get(rating, 0) + 1
        mean_frac = sum(m[3] for m in moves) / len(moves) if moves else None

        def num(v):
            try:
                return int(float(v))
            except (TypeError, ValueError):
                return None
        rows.append({
            "song": s["song"],
            "orig": s["card"],
            "ours": {"perfect": num(card.get("num_perfect")), "awesome": num(card.get("num_awesome")),
                     "pct": num(card.get("moves_passed_pct")), "score": r.get("final_score")},
            "log_hist": hist, "mean_lastfrac": mean_frac,
            "results": r.get("results"), "wall_s": r.get("wall_s"),
        })
    return rows


def summary(rows):
    ok = [r for r in rows if r["ours"]["perfect"] is not None and r["ours"]["score"]]
    if not ok:
        return {}
    P = sum(r["ours"]["perfect"] for r in ok)
    A = sum(r["ours"]["awesome"] for r in ok)
    oP = sum(r["orig"]["perfect"] for r in ok)
    oA = sum(r["orig"]["awesome"] for r in ok)
    return {
        "songs": len(ok), "ours_P": P, "ours_A": A, "orig_P": oP, "orig_A": oA,
        "mean_abs_dP": round(sum(abs(r["ours"]["perfect"] - r["orig"]["perfect"]) for r in ok) / len(ok), 2),
        "mean_abs_dPA": round(sum(abs(r["ours"]["perfect"] + r["ours"]["awesome"]
                                      - r["orig"]["perfect"] - r["orig"]["awesome"]) for r in ok) / len(ok), 2),
        "score_ratio": round(sum(r["ours"]["score"] for r in ok) / sum(r["orig"]["score"] for r in ok), 4),
        "mean_lastfrac": round(sum(r["mean_lastfrac"] or 0 for r in ok) / len(ok), 4),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--json")
    ap.add_argument("--quiet", action="store_true", help="summary lines only")
    a = ap.parse_args()
    manifest = json.load(open(a.manifest))
    allres = {}
    for run in a.runs:
        rows = run_table(manifest, run)
        sm = summary(rows)
        allres[run] = {"rows": rows, "summary": sm}
        if not a.quiet:
            print(f"== {run}")
            print(f"{'song':22s} {'P ours/orig':>12s} {'A ours/orig':>12s} {'%':>8s} {'score ours/orig':>22s} frac")
            for r in rows:
                o, g = r["ours"], r["orig"]
                print(f"{r['song']:22s} {o['perfect']!s:>5}/{g['perfect']:<5d} {o['awesome']!s:>5}/{g['awesome']:<5d} "
                      f"{o['pct']!s:>3}/{g['pct']:<3d} {o['score']!s:>10}/{g['score']:<10d} "
                      f"{(r['mean_lastfrac'] or 0):.3f}")
        print(f"{os.path.basename(run.rstrip('/')):28s} {json.dumps(sm)}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(allres, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
