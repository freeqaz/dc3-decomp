#!/usr/bin/env python3
"""session_summary.py -- what happened in a playtest session, from its folder.

  session_summary.py SESSION_DIR [--json OUT]

Reads SESSION_DIR/engine.log (and pose.dc3session and the camera video's
timestamp sidecars, camera*.frames.tsv, when there are) and
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
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "native", "scripts"))
import video_recorder  # noqa: E402  (standard library only at import)

RATING_RE = re.compile(r"DC3 MOVE_RATING p=(\d+) measure=(-?\d+) move=(\S+) rating=(\S+) "
                       r"lastfrac=(-?[\d.]+) score=(-?\d+)")
MODAL_RE = re.compile(r"^MILO_(FAIL|NOTIFY|WARN|ASSERT)\w*: (.*)$")
# A disc song loads midi='songs/<name>/...'; a DLC or custom song's path carries
# its content root first, midi='cnt00000004:/songs/<name>/...'.  Without the
# optional root every DLC song was "?" (or, after a disc song, was counted as
# that one: the 2026-11-21 GPD session's "betteroffalone" row held fireburning's
# moves, and its "?" row two abandoned thefox attempts and milkshake).
SONG_RE = re.compile(r"HamSongData::Load .*midi='(?:[^':/]+:/)?songs/([^/']+)/")
# seen= on a MOVE_RATING line (native, GestureMgr_Native.cpp): the fraction of
# the camera's frames during that measure in which the player's body was
# really detected (not held, not missing).  -1 = no body bound.
SEEN_RE = re.compile(r" seen=(-?[\d.]+)")
# A measure the camera saw the player in less than this much of is reported
# apart: its rating says more about the tracker than about the dancing.
SEEN_LOW = 0.8
# The difficulty each player was rated at.  Shown per row: in the 2026-11-21
# GPD session player 2 danced betteroffalone on expert beside player 1 on easy,
# which nothing in the summary said.
DIFF_RE = re.compile(r" diff=(\w+)")
SCREEN_RE = re.compile(r"DC3 UI: Screen '([^']+)' Enter")
PERF_RE = re.compile(r"DC3 PERF: fps=([\d.]+) pose_fps=([\d.]+) persons=([\d.]+)")
POSE_RE = re.compile(r"POSE_STATS: fps=([\d.]+) infer_ms_mean=([\d.]+) infer_ms_p95=([\d.]+)")
BEAT_RE = re.compile(r"DC3_HEARTBEAT: uptime_s=(\d+) rss_kb=(\d+)")
EXIT_RE = re.compile(r"DC3_EXIT: how=(\S+) detail=(\S+) code=(-?\d+) uptime_s=(\d+) rss_kb=(\d+)")
# The engine's AudioDevice says when the window stops drawing (the display is
# off, the lid is shut, the window is hidden) and when it draws again.  While it
# does not draw, the main loop runs at about 1 frame a second, waiting for the
# compositor: those DC3 PERF windows measure the display being off, not the game.
HIDDEN_RE = re.compile(r"AudioDevice: window hidden and not drawing")
SHOWN_RE = re.compile(r"AudioDevice: window is drawing again")
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
    perf_lines, notices, hidden_at = [], [], []   # line numbers; hidden state per PERF line
    hidden = False
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
                dm = DIFF_RE.search(line)
                if dm:
                    cur.setdefault("difficulty", {}).setdefault(p, set()).add(dm.group(1))
                sm = SEEN_RE.search(line)
                if sm and float(sm.group(1)) >= 0.0:
                    lo = cur.setdefault("unseen", {}).setdefault(p, {})
                    if float(sm.group(1)) < SEEN_LOW:
                        lo[m.group(4)] = lo.get(m.group(4), 0) + 1
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
                if (m.group(1) in RESULT_SCREENS or m.group(1).endswith("endgame_screen")) \
                        and cur is not None and cur["results_screen"] is None:
                    cur["results_screen"] = m.group(1)
                continue
            m = PERF_RE.search(line)
            if m:
                perf.append(tuple(float(x) for x in m.groups()))
                perf_lines.append(n)
                hidden_at.append(hidden)
                continue
            if HIDDEN_RE.search(line) or SHOWN_RE.search(line):
                hidden = bool(HIDDEN_RE.search(line))
                notices.append(n)
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
            "rss": rss, "exit": exit_info, "signal": signal,
            "perf_not_drawing": not_drawing(perf_lines, hidden_at, notices),
            "sleeps": sleeps(rss, exit_info)}


def not_drawing(perf_lines, hidden_at, notices):
    """For each DC3 PERF window: True when the window was not drawing in it.

    The hidden notice comes after the stall that caused it (the mute is decided
    on the frame that ends the gap), so a window is counted as not drawing when
    the window was hidden at its line, or when a notice falls between the PERF
    lines either side of it."""
    out, j = [], 0
    for i, n in enumerate(perf_lines):
        lo = perf_lines[i - 1] if i else 0
        hi = perf_lines[i + 1] if i + 1 < len(perf_lines) else float("inf")
        while j < len(notices) and notices[j] <= lo:
            j += 1
        out.append(hidden_at[i] or (j < len(notices) and notices[j] < hi))
    return out


def sleeps(rss, exit_info):
    """Spans the machine was asleep (or the process frozen): [(from_s, to_s)].

    DC3_HEARTBEAT sleeps on the steady clock and prints the wall-clock uptime,
    so heartbeats that are much further apart on the wall clock than their
    interval mean the steady clock stopped -- a suspend.  The same holds from
    the last heartbeat to DC3_EXIT, which a suspend just before quitting leaves
    as one long silent stretch."""
    if len(rss) < 2:
        return []
    steps = sorted(b[0] - a[0] for a, b in zip(rss, rss[1:]))
    interval = steps[len(steps) // 2]
    if interval <= 0:
        return []
    marks = [t for t, _ in rss]
    if exit_info and exit_info["uptime_s"] >= marks[-1]:
        marks.append(exit_info["uptime_s"])
    return [(a, b) for a, b in zip(marks, marks[1:]) if b - a > interval * 1.5 + 30]


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
            diff = "/".join(sorted(s.get("difficulty", {}).get(p, ()))) or "?"
            lines.append(f"  {s['song']:<22} p{p} {diff:<7} {sum(rt.values()):3d} moves  score "
                         f"{s['score'].get(p, 0):>9,}  {parts}"
                         f"{'' if s['results_screen'] else '  (no results screen)'}")
            lo = s.get("unseen", {}).get(p)
            if lo is not None:
                n = sum(lo.values())
                lines.append(f"  {'':<22}    {'':<7} {n:3d} of them with the body seen in under "
                             f"{SEEN_LOW:.0%} of the measure's camera frames"
                             + (f": move_bad {lo.get('move_bad', 0)}" if n else ""))
    asleep = sum(b - a for a, b in r["sleeps"])
    if asleep:
        lines.append(f"asleep: {asleep / 60:.1f} min with the game stopped (the machine slept?), "
                     + ", ".join(f"{a} s..{b} s" for a, b in r["sleeps"])
                     + f"; awake {(ex['uptime_s'] - asleep) / 60 if ex else 0:.1f} min")
    if r["perf"]:
        dark = r["perf_not_drawing"]
        drawn = [p for p, d in zip(r["perf"], dark) if not d]
        fps = [p[0] for p in drawn]
        pfs = [p[1] for p in drawn if p[1] > 0]
        if fps:
            lines.append(f"game fps: mean {mean(fps):.1f}, lowest 5 s window {min(fps):.1f}; "
                         f"pose frames the game accepted: mean {mean(pfs):.1f}/s"
                         + (f", lowest {min(pfs):.1f}" if pfs else ""))
        if any(dark):
            lines.append(f"window not drawing (display off, lid shut or hidden; the game waits "
                         f"for the compositor, about 1 fps): {sum(dark)} of {len(dark)} PERF "
                         f"windows, about {sum(dark) * 5 / 60:.1f} min, left out of the fps above")
    if r["pose"]:
        lines.append(f"tracker: {mean([p[0] for p in r['pose']]):.1f} fps, inference "
                     f"{mean([p[1] for p in r['pose']]):.1f} ms mean, "
                     f"{max(p[2] for p in r['pose']):.1f} ms worst p95")
    if r["rss"]:
        first, last = r["rss"][0], r["rss"][-1]
        peak = max(x[1] for x in r["rss"])
        lines.append(f"memory: {first[1] / 1024:.0f} MB at {first[0]} s, {last[1] / 1024:.0f} MB "
                     f"at {last[0]} s, peak {peak / 1024:.0f} MB"
                     + (f", {ex['rss_kb'] / 1024:.0f} MB at exit" if ex and ex["rss_kb"] > 0 else ""))
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
    video = camera_video_line(session)
    if video:
        lines.append(video)
    return "\n".join(lines), r


def camera_video_line(session, name="camera.mkv"):
    """One line about the camera video (scripts/play.sh --record-video), or
    None when the session has none.  Read from the sidecars, which the .tar.gz
    carries; the video's size from the video when it is there."""
    try:
        parts = video_recorder.parts_of(os.path.join(session, name))
    except OSError:
        parts = []
    if not parts:
        return None
    frames = dropped = size = 0
    span = 0.0
    cut_off, problems = [], []
    for p in parts:
        side = video_recorder.sidecar_path(p)
        try:
            _, rows, counts = video_recorder.read_sidecar(side)
        except (OSError, ValueError) as e:
            problems.append(f"{os.path.basename(side)} unreadable ({e})")
            continue
        frames += len(rows)
        dropped += counts.get("dropped", 0)
        if len(rows) > 1:
            span += rows[-1][2] - rows[0][2]
        if counts["stopped"] is None:
            cut_off.append(os.path.basename(p))
        elif counts["stopped"] not in ("closed",):
            problems.append(f"{os.path.basename(p)} stopped: {counts['stopped'].replace('_', ' ')}")
        size += os.path.getsize(p) if os.path.exists(p) else counts.get("bytes", 0)
    extra = f" (+{len(parts) - 1} more part{'s' if len(parts) > 2 else ''})" if len(parts) > 1 else ""
    text = (f"camera video: {parts[0]}{extra}, {frames} frames, {span / 60:.1f} min, "
            f"{size / 1e6:.0f} MB, {dropped} dropped -- not in the .tar.gz (it stays in the "
            f"session folder; scripts/playtest/camera_video.py info {session})")
    if cut_off:
        text += f"; cut off without its last line (killed?): {', '.join(cut_off)}"
    if problems:
        text += "; " + "; ".join(problems)
    return text


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
