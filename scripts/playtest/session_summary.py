#!/usr/bin/env python3
"""session_summary.py -- what happened in a playtest session, from its folder.

  session_summary.py SESSION_DIR [--json OUT]

Reads SESSION_DIR/engine.log (and pose.dc3session when there is one) and
prints: how the game ended, each song played with its per-move ratings, the
frame rates (DC3 PERF / POSE_STATS lines), memory over time, and every
MILO_FAIL / MILO_ASSERT / MILO_NOTIFY / MILO_WARN the game raised, counted.
scripts/play.sh runs it at the end of every session; the soak
(scripts/playtest/soak.py) uses the same parser.  Standard library only.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pose_session  # noqa: E402

RATING_RE = re.compile(r"DC3 MOVE_RATING p=(\d+) measure=(-?\d+) move=(\S+) rating=(\S+) "
                       r"lastfrac=(-?[\d.]+) score=(-?\d+)")
MODAL_RE = re.compile(r"^MILO_(FAIL|NOTIFY|WARN|ASSERT)\w*: (.*)$")
SONG_RE = re.compile(r"HamSongData::Load .*midi='songs/([^/']+)/")
SCREEN_RE = re.compile(r"DC3 UI: Screen '([^']+)' Enter")
PERF_RE = re.compile(r"DC3 PERF: fps=([\d.]+) pose_fps=([\d.]+) persons=([\d.]+)")
POSE_RE = re.compile(r"POSE_STATS: fps=([\d.]+) infer_ms_mean=([\d.]+) infer_ms_p95=([\d.]+)")
BEAT_RE = re.compile(r"DC3_HEARTBEAT: uptime_s=(\d+) rss_kb=(\d+)")
EXIT_RE = re.compile(r"DC3_EXIT: how=(\S+) detail=(\S+) code=(-?\d+) uptime_s=(\d+) rss_kb=(\d+)")
SIGNAL_RE = re.compile(r"Caught signal|SIGSEGV|SIGABRT|SIGBUS|Segmentation fault")
GAMEPLAY_SCREENS = ("game_screen",)
RESULT_SCREENS = ("perform_endgame_screen", "practice_endgame_screen", "battle_endgame_screen",
                  "campaign_endgame_screen", "fitness_endgame_screen")
NUMS = re.compile(r"0x[0-9a-fA-F]+|\d+(\.\d+)?")


def mean(xs):
    return sum(xs) / len(xs) if xs else 0.0


def parse_log(path):
    songs, cur = [], None
    modals, screens, perf, pose, rss = {}, [], [], [], []
    exit_info, signal = None, None
    with open(path, "rb") as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.decode("utf-8", "replace").rstrip("\n")
            m = RATING_RE.search(line)
            if m:
                if cur is None:
                    cur = {"song": "?", "line": n, "ratings": {}, "moves": 0, "score": {},
                           "results_screen": None}
                    songs.append(cur)
                p = int(m.group(1))
                r = cur["ratings"].setdefault(p, {})
                r[m.group(4)] = r.get(m.group(4), 0) + 1
                cur["score"][p] = int(m.group(6))
                cur["moves"] += 1
                continue
            m = SONG_RE.search(line)
            if m:
                cur = {"song": m.group(1), "line": n, "ratings": {}, "moves": 0, "score": {},
                       "results_screen": None}
                songs.append(cur)
                continue
            m = MODAL_RE.match(line)
            if m:
                key = m.group(1) + ": " + NUMS.sub("#", m.group(2))[:200]
                e = modals.setdefault(key, {"count": 0, "first_line": n, "example": m.group(2)[:300]})
                e["count"] += 1
                continue
            m = SCREEN_RE.search(line)
            if m:
                screens.append((n, m.group(1)))
                if m.group(1) in RESULT_SCREENS and cur is not None and cur["results_screen"] is None:
                    cur["results_screen"] = m.group(1)
                continue
            m = PERF_RE.search(line)
            if m:
                perf.append(tuple(float(x) for x in m.groups()))
                continue
            m = POSE_RE.search(line)
            if m:
                pose.append(tuple(float(x) for x in m.groups()))
                continue
            m = BEAT_RE.search(line)
            if m:
                rss.append((int(m.group(1)), int(m.group(2))))
                continue
            m = EXIT_RE.search(line)
            if m:
                exit_info = {"how": m.group(1), "detail": m.group(2), "code": int(m.group(3)),
                             "uptime_s": int(m.group(4)), "rss_kb": int(m.group(5))}
                continue
            if signal is None and SIGNAL_RE.search(line):
                signal = line[:300]
    songs = [s for s in songs if s["moves"] or s["results_screen"]]
    return {"songs": songs, "modals": modals, "screens": screens, "perf": perf, "pose": pose,
            "rss": rss, "exit": exit_info, "signal": signal}


def summarise(session):
    log = os.path.join(session, "engine.log")
    r = parse_log(log)
    lines = []
    ex = r["exit"]
    rc = None
    try:
        rc = int(open(os.path.join(session, "exit_code")).read().strip())
    except (OSError, ValueError):
        pass
    if ex:
        lines.append(f"ended: {ex['how']} ({ex['detail']}), exit {ex['code']}, after "
                     f"{ex['uptime_s'] / 60:.1f} min")
    else:
        lines.append(f"ended: no DC3_EXIT line (crash or kill?), exit code {rc}")
    if r["signal"]:
        lines.append(f"CRASH: {r['signal']}")
    lines.append(f"songs: {len(r['songs'])}")
    for s in r["songs"]:
        for p in sorted(s["ratings"]) or [0]:
            rt = s["ratings"].get(p, {})
            parts = ", ".join(f"{k} {v}" for k, v in sorted(rt.items(), key=lambda kv: -kv[1]))
            lines.append(f"  {s['song']:<22} p{p}  {sum(rt.values()):3d} moves  score "
                         f"{s['score'].get(p, 0):>9,}  {parts}"
                         f"{'' if s['results_screen'] else '  (no results screen)'}")
    if r["perf"]:
        fps = [p[0] for p in r["perf"]]
        pfs = [p[1] for p in r["perf"] if p[1] > 0]
        lines.append(f"game fps: mean {mean(fps):.1f}, lowest 5 s window {min(fps):.1f}; "
                     f"pose frames the game accepted: mean {mean(pfs):.1f}/s"
                     + (f", lowest {min(pfs):.1f}" if pfs else ""))
    if r["pose"]:
        lines.append(f"tracker: {mean([p[0] for p in r['pose']]):.1f} fps, inference "
                     f"{mean([p[1] for p in r['pose']]):.1f} ms mean, "
                     f"{max(p[2] for p in r['pose']):.1f} ms worst p95")
    if r["rss"]:
        first, last = r["rss"][0], r["rss"][-1]
        peak = max(x[1] for x in r["rss"])
        lines.append(f"memory: {first[1] / 1024:.0f} MB at {first[0]} s, {last[1] / 1024:.0f} MB "
                     f"at {last[0]} s, peak {peak / 1024:.0f} MB")
    lines.append(f"game messages (MILO_FAIL/ASSERT/NOTIFY/WARN): {len(r['modals'])} kinds, "
                 f"{sum(e['count'] for e in r['modals'].values())} total")
    order = {"FAIL": 0, "ASSERT": 0, "WARN": 1, "NOTIFY": 2}
    for k, e in sorted(r["modals"].items(), key=lambda kv: (order[kv[0].split(':')[0]],
                                                              -kv[1]["count"]))[:25]:
        lines.append(f"  {e['count']:5d}x  {k[:150]}")
    sess = os.path.join(session, "pose.dc3session")
    if os.path.exists(sess):
        try:
            _, frames = pose_session.load(sess)
            rows = pose_session.describe(frames, pose_session.segments(frames))
            lines.append(f"pose recording: {len(frames)} frames, "
                         f"{sum(1 for x in rows if x['kind'] == 'song')} song segment(s) "
                         f"(scripts/playtest/pose_session.py info {sess})")
        except (OSError, ValueError) as e:
            lines.append(f"pose recording unreadable: {e}")
    return "\n".join(lines), r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("session")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    if not os.path.exists(os.path.join(a.session, "engine.log")):
        print(f"{a.session}: no engine.log", file=sys.stderr)
        return 2
    text, r = summarise(a.session)
    print(text)
    if a.json:
        r = dict(r)
        r["screens"] = r["screens"][-200:]
        with open(a.json, "w") as fh:
            json.dump(r, fh, indent=1, default=str)
    return 0


if __name__ == "__main__":
    sys.exit(main())
