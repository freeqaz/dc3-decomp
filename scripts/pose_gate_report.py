#!/usr/bin/env python3
"""Judge a scripts/pose_scoring_gate.sh run (PLAN P-5c).

Reads, per configuration, the engine log (<dir>/<cfg>.log) and the score
record written by scripts/synthetic_kinect.py (<dir>/<cfg>.json), prints one
table, and exits 1 if any assertion fails.

Per configuration it measures:
  song     did the song END (perform_endgame_screen entered) -- a run that
           stops at a frame cap scores only part of the song
  crash    'Caught SIGSEGV' in the log
  frac     player 0's ham2 DetectFrac samples: count, mean, min, max, distinct
  score    player 0's final score (last score the game reported)
  ratings  player 0's rated events (one per change of move, rating or score)

Assertions (the gate is DIFFERENTIAL; one config alone proves little):
  * every config completes the song, does not crash, and produces ham2 samples
  * no config's DetectFrac is degenerate (one value, at 0 or 1 -- a broken
    error kernel, twice seen in this project) -- except that selftest and
    sensor, both exact copies of the choreography, may sit at 1.0 every move,
    which is their correct answer; both means must be >= --min-mimic
  * selftest (perfect mimicry) beats dummy (standing) on mean DetectFrac AND
    final score, and sensor (live replay of the choreography) beats dummy on
    both
  * replay (a recorded human-video keypoint stream, keyed on song time) is
    told apart from still (the same stream's first frame held all song) MOVE
    BY MOVE: the mean per-move |DetectFrac(replay) - DetectFrac(still)| must
    exceed --min-motion-ratio x the same distance between still and dummy --
    two standing bodies -- (or --min-motion-diff absolutely when dummy did
    not run); both runs scored the same moves; both showed fixture frames
  * a stream that is not the choreography (dummy, replay, still) never rates
    perfect on half or more of its rated moves -- a kernel gone lenient
Maximums alone are reported but never decide: a single lucky window could make
two very different runs share a maximum.

What is deliberately NOT asserted: that replay out-SCORES still. The fixture
is a different dance from the song's, and measured on Better Off Alone
(2026-11-13) the still body out-scores it, 49,119 to 33,662 (the opening
moves rate a standing body 0.80-0.90; the wrong dance draws 'bad'). That is a
scoring-believability finding (PLAN P-7), not a property to freeze into a
gate. A positive control for that direction needs a recording of the song's
OWN choreography, which this repo does not have.
"""

import argparse
import json
import os
import re
import sys

RATING_ORDER = ["move_perfect", "move_awesome", "move_ok", "move_bad", ""]


def frac_stats(log_text):
    vals = []
    for line in log_text.splitlines():
        if "DC3 DETECTFRAC" in line and "branch=ham2" in line and " p=0 " in line:
            m = re.search(r"frac=([0-9.]+)", line)
            if m:
                vals.append(float(m.group(1)))
    if not vals:
        return {"n": 0, "mean": None, "min": None, "max": None, "distinct": 0, "fracs": []}
    return {"n": len(vals), "mean": sum(vals) / len(vals), "min": min(vals),
            "max": max(vals), "distinct": len(set(vals)), "fracs": vals}


def profile_distance(a, b):
    """Mean per-move |DetectFrac a - DetectFrac b|; None unless both runs
    scored the same number of moves (the same song, start to finish)."""
    if not a or len(a) != len(b):
        return None
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def score_stats(record):
    """Final score and rating histogram for player 0 from a synthetic_kinect
    record. synthetic_kinect logs an event whenever player 0's (move, score,
    rating) changes, so:
      scored   = number of score increases (moves that earned points)
      ratings  = histogram of the rating carried by each rated event -- a
                 'bad' (0 points) shows up here and nowhere in the score.
    Both undercount a run of identical consecutive moves with identical
    ratings (no change, no event); they are comparable between configs of
    the same song, which is all the gate uses them for."""
    events = [e for e in record.get("perform_events", []) if e.get("player") == 0]
    hist = {}
    last = 0
    moves = 0
    for e in events:
        sc = e.get("score") or 0
        if sc > last:
            moves += 1
        last = max(last, sc)
        r = e.get("rating") or ""
        if r:
            hist[r] = hist.get(r, 0) + 1
    return {"score": last, "moves": moves, "ratings": hist,
            "replay_shown": record.get("replay_frames_shown", 0),
            "max_song": max([e.get("song") or 0 for e in record.get("perform_events", [])]
                            + [s.get("song") or 0 for s in record.get("perform_samples", [])]
                            + [0])}


def load(dirpath, cfg):
    log_path = os.path.join(dirpath, cfg + ".log")
    rec_path = os.path.join(dirpath, cfg + ".json")
    if not os.path.exists(log_path):
        return None
    text = open(log_path, errors="replace").read()
    rec = {}
    if os.path.exists(rec_path):
        try:
            rec = json.load(open(rec_path))
        except ValueError:
            rec = {}
    out = {"cfg": cfg,
           "song_done": "Screen 'perform_endgame_screen' Enter" in text,
           "crash": text.count("Caught SIGSEGV"),
           "has_record": bool(rec)}
    out.update(frac_stats(text))
    out.update(score_stats(rec))
    return out


def fmt(v, spec):
    return "-" if v is None else format(v, spec)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dir")
    ap.add_argument("--configs", default="selftest,dummy,sensor,replay,still,video")
    ap.add_argument("--min-motion-ratio", type=float, default=2.0,
                    help="replay-vs-still move distance over still-vs-dummy (measured 2.96)")
    ap.add_argument("--min-motion-diff", type=float, default=0.07,
                    help="replay-vs-still move distance floor without dummy (measured 0.146)")
    ap.add_argument("--max-perfect-frac", type=float, default=0.5)
    ap.add_argument("--min-mimic", type=float, default=0.95,
                    help="selftest and sensor mean DetectFrac floor (both are 1.0000 on main)")
    ap.add_argument("--json", default=None, help="also write the measurements here")
    a = ap.parse_args(argv)

    rows = {}
    for cfg in a.configs.split(","):
        r = load(a.dir, cfg)
        if r is not None:
            rows[cfg] = r

    print(f"{'config':9s} {'song':5s} {'segv':4s} {'frac n':>6s} {'mean':>6s} {'min':>6s} "
          f"{'max':>6s} {'dist':>4s} {'score':>9s} {'scored':>6s}  ratings")
    for cfg, r in rows.items():
        ratings = " ".join(f"{k.replace('move_', '') or '-'}:{v}" for k, v in
                           sorted(r["ratings"].items(),
                                  key=lambda kv: RATING_ORDER.index(kv[0])
                                  if kv[0] in RATING_ORDER else 99))
        print(f"{cfg:9s} {'END' if r['song_done'] else 'cut':5s} {r['crash']:4d} {r['n']:6d} "
              f"{fmt(r['mean'], '.4f'):>6s} {fmt(r['min'], '.4f'):>6s} {fmt(r['max'], '.4f'):>6s} "
              f"{r['distinct']:4d} {r['score']:9d} {r['moves']:6d}  {ratings}")

    fails = []

    def need(cond, msg):
        if not cond:
            fails.append(msg)

    wanted = [c for c in ("selftest", "dummy", "sensor", "replay", "still")
              if c in a.configs.split(",")]
    for cfg in wanted:
        need(cfg in rows, f"{cfg}: no log -- configuration did not run")
    asserted = [c for c in wanted if c in rows]
    for cfg in asserted:
        r = rows[cfg]
        need(r["song_done"], f"{cfg}: the song did not finish (perform_endgame_screen never entered) -- "
                             f"only part of it was scored")
        need(r["crash"] == 0, f"{cfg}: crashed (SIGSEGV)")
        need(r["n"] > 0, f"{cfg}: no ham2 DetectFrac samples (pipeline not reached)")
        need(r["has_record"], f"{cfg}: no score record")
        # selftest feeds the reference back as the player and sensor replays it
        # exactly through the socket, so 1.0 every move is the CORRECT answer
        # for both; for an input that is not the choreography (dummy, replay,
        # still) a single value at 0 or 1 is a broken kernel -- and 0 every
        # move is broken everywhere.
        pinned_ok = 1.0 if cfg in ("selftest", "sensor") else None
        if r["distinct"] == 1 and r["min"] in (0.0, 1.0) and r["min"] != pinned_ok:
            fails.append(f"{cfg}: DetectFrac degenerate (always {r['min']:.4f}) -- "
                         f"scoring kernel broken, not 'stable'")
    for cfg in ("selftest", "sensor"):
        if cfg in rows and rows[cfg]["mean"] is not None:
            need(rows[cfg]["mean"] >= a.min_mimic,
                 f"{cfg}: mean DetectFrac {rows[cfg]['mean']:.4f} < {a.min_mimic:g} "
                 f"-- an exact copy of the choreography no longer scores as one")

    def beats(a_cfg, b_cfg, key, ratio=1.0):
        if a_cfg not in rows or b_cfg not in rows:
            return
        av, bv = rows[a_cfg][key], rows[b_cfg][key]
        if av is None or bv is None:
            fails.append(f"{a_cfg} vs {b_cfg}: no {key} to compare")
            return
        need(av > bv * ratio if ratio != 1.0 else av > bv,
             f"{a_cfg} {key} {av:.4g} does not beat {b_cfg} {bv:.4g}"
             + (f" by x{ratio:g}" if ratio != 1.0 else ""))

    beats("selftest", "dummy", "mean")
    beats("selftest", "dummy", "score")
    beats("sensor", "dummy", "mean")
    beats("sensor", "dummy", "score")
    for cfg in ("replay", "still"):
        if cfg in rows:
            need(rows[cfg]["replay_shown"] > 0, f"{cfg}: no fixture frame was ever sent")
    for cfg in ("dummy", "replay", "still"):
        if cfg in rows:
            hist = rows[cfg]["ratings"]
            rated = sum(hist.values())
            perfect = hist.get("move_perfect", 0)
            need(rated == 0 or perfect < a.max_perfect_frac * rated,
                 f"{cfg}: {perfect} of {rated} rated moves perfect -- a stream that is "
                 f"not the choreography should not rate perfect")

    motion = {}
    if "replay" in rows and "still" in rows:
        rs = profile_distance(rows["replay"]["fracs"], rows["still"]["fracs"])
        motion["replay_vs_still"] = rs
        if rs is None:
            fails.append(f"replay vs still: scored {rows['replay']['n']} vs "
                         f"{rows['still']['n']} moves -- not the same song")
        elif "dummy" in rows:
            sd = profile_distance(rows["still"]["fracs"], rows["dummy"]["fracs"])
            motion["still_vs_dummy"] = sd
            if sd is None:
                fails.append(f"still vs dummy: scored {rows['still']['n']} vs "
                             f"{rows['dummy']['n']} moves -- not the same song")
            else:
                need(rs > a.min_motion_ratio * sd,
                     f"replay is not told apart from still: per-move distance {rs:.4f} "
                     f"<= {a.min_motion_ratio:g} x still-vs-dummy {sd:.4f} -- the "
                     f"fixture's motion is not reaching the scorer")
        else:
            need(rs > a.min_motion_diff,
                 f"replay is not told apart from still: per-move distance {rs:.4f} "
                 f"<= {a.min_motion_diff:g}")
    for k, v in motion.items():
        print(f"per-move DetectFrac distance {k.replace('_', ' ')}: {fmt(v, '.4f')}")

    if a.json:
        with open(a.json, "w") as f:
            json.dump({"rows": rows, "fails": fails}, f, indent=1)
    for msg in fails:
        print("FAIL: " + msg)
    print("PASS" if not fails else "GATE FAILED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
