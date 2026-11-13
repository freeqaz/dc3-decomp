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
3. REPLAY (N times): scripts/play.sh --headless --replay SEGMENT, a fresh
   home, the same song and difficulty.  No camera, no tracker.
4. Every move's rating, live vs each replay, side by side; exit 0 only if
   every replay rated every move the same as the live run.

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
    ap.add_argument("--clip", required=True)
    ap.add_argument("--song", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--difficulty", default="medium", choices=("beginner", "easy", "medium", "expert"))
    ap.add_argument("--pre", type=float, default=8.0)
    ap.add_argument("--replays", type=int, default=2)
    ap.add_argument("--gpu-lock")
    ap.add_argument("--sync", default="song", choices=("song", "wall"))
    a = ap.parse_args(argv)
    a.out = os.path.abspath(a.out)
    os.makedirs(a.out, exist_ok=True)
    env = {"MILO_RENDER": "1"}

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
    ok = bool(live["ratings"])
    for i in range(a.replays):
        # Pad-only menus in the replays: before its first frame a replay holds
        # the session's first frame (the dancer mid-intro, arms anywhere), and
        # under `auto` that pose drives the hand menus.  Menus are not what is
        # being proved; scoring does not read the controller mode.
        r = leg(f"replay{i + 1}", a, ["--headless", "--fast-boot", "--replay", cut,
                                      "--replay-sync", a.sync],
                dict(env, DC3_CONTROLLER_MODE="forced"))
        results["replays"].append(r)
        same = [x[2] == y[2] for x, y in zip(live["ratings"], r["ratings"])]
        r["same_rating"] = sum(same)
        r["compared"] = len(same)
        r["same_score"] = (live["ratings"][-1][4] == r["ratings"][-1][4]) if r["ratings"] and live["ratings"] else False
        if not (len(r["ratings"]) == len(live["ratings"]) and all(same)):
            ok = False
        say(f"replay{i + 1}: {sum(same)}/{len(same)} moves rated the same "
            f"(live {len(live['ratings'])} moves, replay {len(r['ratings'])})")
    with open(os.path.join(a.out, "proof.json"), "w") as f:
        json.dump(results, f, indent=1)
    # side-by-side table
    with open(os.path.join(a.out, "ratings.tsv"), "w") as f:
        f.write("measure\tmove\tlive\t" + "\t".join(f"replay{i + 1}" for i in range(a.replays)) + "\n")
        for k, x in enumerate(live["ratings"]):
            row = [str(x[0]), x[1], x[2]]
            for r in results["replays"]:
                row.append(r["ratings"][k][2] if k < len(r["ratings"]) else "-")
            f.write("\t".join(row) + "\n")
    say("PROOF " + ("HOLDS" if ok else "FAILS") + f": {a.out}/ratings.tsv")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
