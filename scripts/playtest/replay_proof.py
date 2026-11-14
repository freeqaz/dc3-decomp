#!/usr/bin/env python3
"""replay_proof.py -- prove that a recorded playtest session replays offline
to the same scoring (PLAN L-1).

  replay_proof.py --clip macarena.mp4 --song macarena --out DIR [--difficulty medium]
                  [--gpu-lock FILE] [--replays 2]

1. LIVE: scripts/play.sh --headless --video CLIP: the real tracker
   (pose_server.py + MediaPipe, through footage_camera.py) watches recorded
   footage of a person dancing the song, in step with the song clock; the
   game records every frame it accepts to pose.dc3session.
2. The song's segment is cut out of the recording (pose_session.py).
3. REPLAY: scripts/play.sh --headless --replay SEGMENT, a fresh home, the
   same song and difficulty, no camera, no tracker -- N times in real time
   (--replays) and M times under DC3_FAST_TIME (--fast-replays: song time is
   1/120 s per game frame, so a run is a pure function of its input).
4. Checks, in this order:
   a. STREAM: each replay records what its sensor accepted (play.sh records
      every session); pose_session.compare says whether every frame of the
      segment reached the scorer once, in order, with the same id, timestamp,
      people and floor normal, no earlier in the song than live -- up to where
      the replay's song clock stopped -- and the replay rated as many moves
      as the live run.
   b. DETERMINISM: the fast-time replays rate every move the same.
   c. RATINGS: every move, live vs each replay and replay vs replay, side by
      side in ratings.tsv, with how many agree.
   Exit 0 when a and b hold (a on the fast-time replays when there are
   any: a real-time replay accepts one frame per render frame, as the live
   game did, so a box rendering below 30 fps drops frames).  c is reported,
   not gated: the scorer samples the
   skeleton on render frames (MoveDir::PostUpdateFilters enqueues a filter job
   at the song time of the game frame it runs on; FilterQueue::Poll reads the
   skeleton of a later frame), so in real time two runs of the SAME input
   differ by which render frames the song times fall on -- the live run was
   such a run too.  The real-time replay-vs-replay agreement is the scorer's
   own noise floor, and the live-vs-replay agreement is read against it.

Each leg is a whole session folder (engine.log, summary.txt) under DIR.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kitdrive  # noqa: E402
import pose_session  # noqa: E402

RATING_RE = re.compile(r"DC3 MOVE_RATING p=(\d+) measure=(-?\d+) move=(\S+) rating=(\S+) "
                       r"lastfrac=(-?[\d.]+) score=(-?\d+)")


def say(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def play_one_song(kit, song, difficulty, song_timeout=420):
    """title -> main -> choose_mode -> song select -> multiuser -> song -> results."""
    kit.wait_port()
    kit.boot_to_main()
    kit.perform(song, difficulty)
    say("song started")
    end = kit.wait_screen("perform_endgame_screen", song_timeout)
    if not end:
        raise RuntimeError(f"no results screen within {song_timeout} s (at {kit.screen()})")
    time.sleep(6)
    return kit.card(0)


def ratings(log):
    out = []
    with open(log, "rb") as f:
        for raw in f:
            m = RATING_RE.search(raw.decode("utf-8", "replace"))
            if m and m.group(1) == "0":
                out.append((int(m.group(2)), m.group(3), m.group(4), float(m.group(5)),
                            int(m.group(6))))
    return out


def agreement(x, y):
    same = sum(1 for p, q in zip(x, y) if p[2] == q[2])
    return same, max(len(x), len(y))


def leg(name, a, args, env):
    out = os.path.join(a.out, name)
    home = os.path.join(out, "home")
    kitdrive.seed_home(home)
    say(f"{name}: scripts/play.sh {' '.join(args)}")
    kit = kitdrive.Kit(os.path.join(out, "session"), home, args=args, env=env,
                       gpu_lock=a.gpu_lock)
    card, err = None, None
    try:
        card = play_one_song(kit, a.song, a.difficulty)
    except RuntimeError as e:
        err = str(e)
        say(f"{name}: {err}")
    finally:
        rc = kit.quit()
    r = ratings(kit.engine_log)
    say(f"{name}: play.sh rc {rc}, {len(r)} moves rated, card {card}")
    return {"session": kit.session, "card": card, "error": err, "ratings": r, "rc": rc}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--clip")
    ap.add_argument("--song")
    ap.add_argument("--recheck", action="store_true",
                    help="only re-run the checks over OUT/proof.json's finished legs")
    ap.add_argument("--out", required=True)
    ap.add_argument("--difficulty", default="medium", choices=("beginner", "easy", "medium", "expert"))
    ap.add_argument("--pre", type=float, default=8.0)
    ap.add_argument("--replays", type=int, default=2, help="real-time replays")
    ap.add_argument("--fast-replays", type=int, default=2, help="DC3_FAST_TIME replays")
    ap.add_argument("--from-live", help="skip the live leg: reuse DIR/live of an earlier run")
    ap.add_argument("--gpu-lock")
    ap.add_argument("--sync", default="song", choices=("song", "wall"))
    a = ap.parse_args(argv)
    a.out = os.path.abspath(a.out)
    if a.recheck:
        with open(os.path.join(a.out, "proof.json")) as f:
            return evaluate(a.out, json.load(f))
    if not (a.clip or a.from_live) or not a.song:
        ap.error("--clip (or --from-live) and --song are required")
    os.makedirs(a.out, exist_ok=True)
    env = {"MILO_RENDER": "1"}

    if a.from_live:
        with open(os.path.join(a.from_live, "proof.json")) as f:
            live = json.load(f)["live"]
        live["ratings"] = [tuple(x) for x in live["ratings"]]
        say(f"live: reused from {a.from_live}")
    else:
        live = leg("live", a, ["--headless", "--fast-boot", "--video", os.path.abspath(a.clip),
                               "--video-pre", str(a.pre)], env)
    rec = os.path.join(live["session"], "pose.dc3session")
    _, frames = pose_session.load(rec)
    segs = pose_session.segments(frames)
    rows = pose_session.describe(frames, segs)
    songs = [r for r in rows if r["kind"] == "song"]
    say(f"recording: {len(frames)} frames, segments: "
        + "; ".join(f"seg{r['n']} {r['kind']} {r['frames']}f song {r['song_from']:.1f}.."
                    f"{r['song_to']:.1f}" for r in rows))
    if not songs:
        say("no song segment in the recording")
        return 1
    cut = os.path.join(a.out, f"{a.song}.dc3session")
    s, e = segs[songs[-1]["n"] - 1]
    pose_session.write(cut, ["# dc3-pose-session v1", f"# {a.song} cut from {rec}"], frames[s:e])

    results = {"live": live, "replays": [], "cut": cut}
    legs = [(f"replay{i + 1}", {}) for i in range(a.replays)]
    legs += [(f"fast{i + 1}", {"DC3_FAST_TIME": "1"}) for i in range(a.fast_replays)]
    for name, extra in legs:
        # Pad-only menus in the replays: a replay holds the session's first
        # frame until the song (the dancer mid-intro, arms anywhere), and under
        # `auto` that pose drives the hand menus.  Menus are not what is being
        # proved; scoring does not read the controller mode.
        r = leg(name, a, ["--headless", "--fast-boot", "--replay", cut, "--replay-sync", a.sync],
                dict(env, DC3_CONTROLLER_MODE="forced", **extra))
        r["name"] = name
        results["replays"].append(r)
    return evaluate(a.out, results)


def evaluate(out, results):
    """The checks (a)-(c) of the docstring over finished legs; writes
    proof.json and ratings.tsv.  --recheck runs only this."""
    live = results["live"]
    _, src = pose_session.load(results["cut"])
    ok = bool(live["ratings"])
    reps = results["replays"]
    for r in reps:
        try:
            _, rframes = pose_session.load(os.path.join(r["session"], "pose.dc3session"))
            r["stream"] = pose_session.compare(src, rframes)
        except (OSError, ValueError) as ex:
            r["stream"] = {"identical": False, "error": str(ex)}
        st = r["stream"]
        # The stream, and the whole song: as many moves rated as live (a
        # replay whose clock stopped early would rate fewer).  Gated on the
        # fast-time replays when there are any: a real-time replay accepts one
        # frame per render frame, as the live game did, so a box that renders
        # below the sensor's 30 Hz for a while drops frames (measured on a
        # loaded box: 5 s windows at 13.6 / 21.5 / 27.7 fps, 72 of 4877
        # frames dropped); under DC3_FAST_TIME the song waits for the frames.
        gated = r["name"].startswith("fast") or not any(
            x["name"].startswith("fast") for x in reps)
        r["gated"] = gated
        if gated and (not st.get("identical") or len(r["ratings"]) != len(live["ratings"])):
            ok = False
        same, n = agreement(live["ratings"], r["ratings"])
        r["same_as_live"] = [same, n]
        perf = []
        try:
            with open(os.path.join(r["session"], "engine.log"), "rb") as fh:
                perf = [float(m.group(1)) for m in
                        re.finditer(rb"DC3 PERF: fps=([\d.]+)", fh.read())]
        except OSError:
            pass
        r["lowest_fps_window"] = min(perf) if perf else None
        say(f"{r['name']}{'' if gated else ' (reported, not gated)'}: stream "
            f"{'IDENTICAL' if st.get('identical') else 'DIFFERENT'} "
            f"({st.get('replayed')}/{st.get('source_frames')} frames, {st.get('missing')} "
            f"dropped, {st.get('not_reached')} after the last not reached "
            f"({st.get('not_reached_s')} s of song), "
            f"{st.get('mismatched')} differ, song-time lag {st.get('lag_min')}.."
            f"{st.get('lag_max')} s; slowest 5 s at {r['lowest_fps_window']} fps); {same}/{n} "
            f"moves rated as live; card {r['card']}")
    pairs = {}
    for i in range(len(reps)):
        for j in range(i + 1, len(reps)):
            same, n = agreement(reps[i]["ratings"], reps[j]["ratings"])
            pairs[f"{reps[i]['name']}~{reps[j]['name']}"] = [same, n]
            say(f"{reps[i]['name']} vs {reps[j]['name']}: {same}/{n} moves rated the same")
    results["pairs"] = pairs
    fast = [r for r in reps if r["name"].startswith("fast")]
    if len(fast) >= 2:
        # Rating and the detector's fraction, per move.  The running score is
        # reported, not gated: measured 2026-11-13, two fast-time replays
        # agreed on every rating and fraction and ended 1108172 vs 1108173
        # (one point, from one move on).
        def key(r):
            return [list(x[:4]) for x in r["ratings"]]
        det = all(key(fast[0]) == key(r) and r["ratings"] for r in fast[1:])
        results["fast_deterministic"] = det
        scores = [r["ratings"][-1][4] if r["ratings"] else None for r in fast]
        say("fast-time replays " + ("rate every move identically (rating and fraction)"
                                    if det else "DIFFER from each other")
            + f"; final scores {scores}")
        if not det:
            ok = False
    results["holds"] = ok
    with open(os.path.join(out, "proof.json"), "w") as f:
        json.dump(results, f, indent=1)
    with open(os.path.join(out, "ratings.tsv"), "w") as f:
        f.write("measure\tmove\tlive\t" + "\t".join(r["name"] for r in reps) + "\n")
        for k, x in enumerate(live["ratings"]):
            row = [str(x[0]), x[1], f"{x[2]} {x[3]:.3f}"]
            for r in reps:
                row.append(f"{r['ratings'][k][2]} {r['ratings'][k][3]:.3f}"
                           if k < len(r["ratings"]) else "-")
            f.write("\t".join(row) + "\n")
    say("PROOF " + ("HOLDS" if ok else "FAILS") + f": {out}/ratings.tsv")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
