#!/usr/bin/env python3
"""Record the choreography reference skeleton of a song over its whole length.

Plays the song (Perform, chosen difficulty, freestyle off by default -- the
story-mode routines have none) with the in-process `choreo` performer and
polls GET /api/pose/target as fast as the engine answers.  Each answer is
taken between two frames and carries refSeconds -- the song time the scorer
compares the current skeleton against -- and player 0's reference joints in
Kinect camera space.  Output, one JSON object per line:

    {"ref": <refSeconds>, "measure": m, "move": "...", "joints": [[x,y,z] * 20]}

so a recorded webcam fixture can be compared with the reference offline at
any time offset (scripts/video_calib/pose_vs_reference.py).

  python3 scripts/video_calib/dump_target.py --song macarena --song-order RUN/song_order.json \\
      --port 9320 --out macarena.target.jsonl

--song-order is the song_order.json a native_song_sweep.py run writes (so no
discovery run is needed).  The engine is paced (DC3_HTTP_PACE_FPS, default
240 frames/s, i.e. 2x song speed under DC3_FAST_TIME) so the poller sees
most frames.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import native_song_sweep as nss  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--song", required=True)
    ap.add_argument("--song-order", required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--difficulty", default="medium", choices=sorted(nss.DIFFICULTY_DOWNS))
    ap.add_argument("--freestyle", action="store_true", help="keep freestyle sections (default off)")
    ap.add_argument("--pace", default="240")
    ap.add_argument("--binary", default=os.path.join(nss.REPO, "native", "build", "dc3-native"))
    ap.add_argument("--timeout", type=int, default=900)
    a = ap.parse_args()

    order = json.load(open(a.song_order))
    idx = {s: n for n, s, hdr in order["rows"] if not hdr}
    downs = idx[a.song] - order["entry_index"]
    work = os.path.abspath(a.out) + ".run"
    env = {"DC3_POSE": "external", "DC3_POSE_PERFORM": "choreo", "DC3_FAST_TIME": "1",
           "DC3_CONTROLLER_MODE": "forced", "DC3_HTTP_PACE_FPS": a.pace}
    e = nss.Engine(os.path.abspath(a.binary), a.port, work,
                   nss.route_script(downs, nss.DIFFICULTY_DOWNS[a.difficulty]), gpu=False, extra_env=env)
    n = 0
    last_ref = None
    try:
        deadline = time.time() + 180
        while time.time() < deadline and e.alive() and not e.jget("/health", 2):
            time.sleep(1)
        evaled = a.freestyle
        hard = time.time() + a.timeout
        with open(a.out, "w") as out:
            while time.time() < hard and e.alive():
                e.pump()
                names = [s for _, s in e.screens]
                if not evaled and "multiuser_screen" in names:
                    e.eval("{profile_mgr toggle_disable_freestyle}")
                    evaled = True
                if any(s in names for s in nss.ENDGAME_SCREENS):
                    break
                if "game_screen" not in names:
                    time.sleep(0.5)
                    continue
                d = (e.jget("/pose/target", 5) or {}).get("data") or {}
                p0 = (d.get("players") or [{}])[0]
                ref = d.get("refSeconds")
                if ref is None or "joints" not in p0 or ref == last_ref:
                    continue
                last_ref = ref
                out.write(json.dumps({"ref": ref, "measure": d.get("measure"), "move": p0.get("move"),
                                      "source": p0.get("source"), "joints": p0["joints"]}) + "\n")
                n += 1
    finally:
        e.stop()
    print(f"{a.out}: {n} reference samples")
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
