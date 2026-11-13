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
  orig        the card's perfect / awesome, and rest = N - P - A where N is
              solved from the card's percent (get_moves_passed); the card
              does not split ok from bad
  ours        perfect / awesome / ok / bad over every DC3 MOVE_RATING line
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


def orig_moves(card):
    """N such that the card's percent = (perfect + awesome) * 100 // N
    (MetaPerformer::GetMovesPassed); the smallest that fits."""
    pa = card["perfect"] + card["awesome"]
    for n in range(pa, 3 * pa + 1):
        if pa * 100 // n == card["pct"]:
            return n
    return None


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
        orig = dict(s["card"])
        orig["moves"] = orig_moves(orig)
        orig["rest"] = orig["moves"] - orig["perfect"] - orig["awesome"] if orig["moves"] else None
        rows.append({
            "song": s["song"],
            "orig": orig,
            "log": {"perfect": hist.get("move_perfect", 0), "awesome": hist.get("move_awesome", 0),
                    "ok": hist.get("move_ok", 0), "bad": hist.get("move_bad", 0), "moves": len(moves)},
            "ours": {"perfect": num(card.get("num_perfect")), "awesome": num(card.get("num_awesome")),
                     "pct": num(card.get("moves_passed_pct")), "score": r.get("final_score")},
            "log_hist": hist, "mean_lastfrac": mean_frac,
            "results": r.get("results"), "wall_s": r.get("wall_s"),
        })
    return rows


def summary(rows):
    """Totals over songs, on both sides counted over every rated move (the
    story-mode card counts every move: SongSequence sets
    start_score_move_index -1).  Rates are per rated move, so a run that
    rates a different number of moves (freestyle on: 8 fewer) still
    compares."""
    ok = [r for r in rows if r["log"]["moves"] and r["orig"]["moves"]]
    if not ok:
        return {}
    def tot(side, k):
        return sum(r[side][k] for r in ok)
    n_o, n_u = tot("orig", "moves"), tot("log", "moves")
    out = {"songs": len(ok), "orig_moves": n_o, "ours_moves": n_u,
           "orig_P": tot("orig", "perfect"), "orig_A": tot("orig", "awesome"), "orig_rest": tot("orig", "rest"),
           "ours_P": tot("log", "perfect"), "ours_A": tot("log", "awesome"),
           "ours_O": tot("log", "ok"), "ours_B": tot("log", "bad")}
    out["orig_rates"] = [round(out["orig_P"] / n_o, 3), round(out["orig_A"] / n_o, 3),
                         round(out["orig_rest"] / n_o, 3)]
    out["ours_rates"] = [round(out["ours_P"] / n_u, 3), round(out["ours_A"] / n_u, 3),
                         round((out["ours_O"] + out["ours_B"]) / n_u, 3)]
    # per-song absolute error of the perfect rate, averaged
    out["mean_abs_dPrate"] = round(sum(abs(r["log"]["perfect"] / r["log"]["moves"]
                                           - r["orig"]["perfect"] / r["orig"]["moves"]) for r in ok) / len(ok), 3)
    out["mean_abs_dPArate"] = round(sum(abs((r["log"]["perfect"] + r["log"]["awesome"]) / r["log"]["moves"]
                                            - (r["orig"]["perfect"] + r["orig"]["awesome"]) / r["orig"]["moves"])
                                        for r in ok) / len(ok), 3)
    out["score_ratio"] = round(sum(r["ours"]["score"] or 0 for r in ok) / sum(r["orig"]["score"] for r in ok), 4)
    out["mean_lastfrac"] = round(sum(r["mean_lastfrac"] or 0 for r in ok) / len(ok), 4)
    return out


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
            print(f"{'song':22s} {'orig P/A/rest of N':>20s}   {'ours P/A/O/B of N':>20s}  {'score ours/orig':>21s}  frac")
            for r in rows:
                o, g = r["log"], r["orig"]
                print(f"{r['song']:22s} {g['perfect']:>4d}/{g['awesome']:<3d}/{g['rest']!s:<3} of {g['moves']!s:<4}"
                      f"   {o['perfect']:>3d}/{o['awesome']:<3d}/{o['ok']:<3d}/{o['bad']:<3d} of {o['moves']:<4d}"
                      f" {r['ours']['score']!s:>10}/{g['score']:<10d} {(r['mean_lastfrac'] or 0):.3f}")
        print(f"{os.path.basename(run.rstrip('/')):28s} {json.dumps(sm)}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(allres, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
