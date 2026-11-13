#!/usr/bin/env python3
"""native_song_sweep.py -- play every quickplay song in dc3-native and report
per song: reached gameplay / results, ratings and score from a correct
performer, Warn/Notify/Fail lines, crashes, hangs, load time, peak RSS, and a
gameplay and a results screenshot.

WHY
  Every flow test used ymca or Better Off Alone.  A per-song content bug (a
  preload budget, a milo that fails to load, a move that cannot be scored)
  only shows on that song.  2026-11-13: the song preload budget sent 34 of 57
  songs back to the title screen on web, unseen by any test.

HOW (one engine per song, `--jobs` at a time)
  * the song-select order is read from the game itself first (a discovery run
    that evaluates `right_hand.hnl data_symbol N` on song_select_screen), so
    the per-song input script presses exactly the downs that reach the song;
  * each song runs headless with DC3_FAST_TIME (song time 1/120 s per frame),
    the in-process performer (DC3_POSE=external DC3_POSE_PERFORM=choreo, the
    choreography itself, at the Kinect's 30 Hz) and DC3_TAG_MODALS=1;
    difficulty Easy (the second row; Beginner schedules no detect frames, so
    the performer would have nothing to follow);
  * a fresh persistence root per song with every tutorial marked seen;
  * `--venue V` sets the venue preference on multiuser_screen
    ({meta_performer set_venue_pref V}{meta_performer setup_venue});
    otherwise the profile has no preference and QuickplayPerformer::ChooseVenue
    picks a random unlocked venue, as on a fresh 360;
  * `--song-seconds N` stops N song-seconds into gameplay instead of at the
    end (results then not expected).
  Stages are read from the engine's own `DC3 UI: Screen 'X' Enter` lines,
  stamped with wall time as the driver sees them.

USAGE
  python3 scripts/native_song_sweep.py --out /tmp/sweep [--jobs 2] [--port-base 9220]
      [--songs ymca,omg] [--venue dci] [--song-seconds 60] [--no-gpu]
  GPU runs need the sandbox off and should take the shared GPU lock, e.g.
      flock -s -w 600 /home/free/tmp/claude-1000/gpu1.lock python3 scripts/native_song_sweep.py ...
  Writes <out>/<song>[_<venue>]/{engine.log,route_input.txt,gameplay.png,
  results.png,result.json}, <out>/summary.json, <out>/summary.md and, when
  ImageMagick is present, <out>/contact_gameplay.png / contact_results.png.
  Exit 0 when every song reached what was asked of it, 3 otherwise.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Global-options cache entry, as native/tests/test_dta_flow.cpp
# SeedGlobalOptions writes it; mTutorialsSeen is the int at 0x28.
_GLOBAL_OPTIONS = bytes([
    0x1c, 0, 0, 0, 0, 0, 0, 0xb0, 0xc1, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 9, 4, 0, 0, 0, 0, 0, 0])

SCREEN_RE = re.compile(r"DC3 UI: Screen '([^']+)' Enter")
RATING_RE = re.compile(r"DC3 MOVE_RATING p=(\d+) measure=(-?\d+) move=(\S+) rating=(\S+) "
                       r"lastfrac=(-?[\d.]+) score=(-?\d+) diff=(\S+)")
MODAL_RE = re.compile(r"^MILO_(FAIL|NOTIFY|WARN|ASSERT)\w*: (.*)$")
FILE_RE = re.compile(r"(could not (open|find|load|cache)|couldn't (open|find|load)|"
                     r"can't (open|find)|not found|missing|no such file|failed to (open|load))",
                     re.I)
ENDGAME_SCREENS = ("perform_endgame_screen",)


def seed_root(root):
    os.makedirs(os.path.join(root, "cache", "global"), exist_ok=True)
    os.makedirs(os.path.join(root, "saves"), exist_ok=True)
    b = bytearray(_GLOBAL_OPTIONS)
    b[0x28:0x2c] = (0x7FFFFFFF).to_bytes(4, "little")  # every tutorial seen
    with open(os.path.join(root, "cache", "global", "global"), "wb") as f:
        f.write(b)


class Engine:
    def __init__(self, binary, port, outdir, script_lines, gpu, extra_env=None):
        self.port = port
        self.base = f"http://127.0.0.1:{port}/api"
        self.out = outdir
        os.makedirs(outdir, exist_ok=True)
        self.root = tempfile.mkdtemp(prefix="persist-", dir=outdir)
        seed_root(self.root)
        self.script = os.path.join(outdir, "route_input.txt")
        with open(self.script, "w") as f:
            f.write("\n".join(script_lines) + "\n")
        env = dict(os.environ)
        env.update({
            "DC3_HTTP": "1", "DC3_HTTP_PORT": str(port), "DC3_FAST_BOOT": "1",
            "DC3_SHOW_SPLASH": "0", "MILO_MAX_FRAMES": "0", "MILO_HEADLESS": "1",
            "MILO_FATAL_FAILS": "0", "DC3_TAG_MODALS": "1", "DC3_TEL": "1",
            "DC3_TEL_INTERVAL": "600",
            "DC3_CACHE_DIR": os.path.join(self.root, "cache"),
            "DC3_SAVE_DIR": os.path.join(self.root, "saves"),
            "MILO_INPUT_SCRIPT": self.script,
            "DC3_DATA": env.get("DC3_DATA", os.path.join(REPO, "orig-assets")),
        })
        if gpu:
            env["MILO_RENDER"] = "1"
            env.pop("MILO_NORENDER", None)
        else:
            env["MILO_NORENDER"] = "1"
            env.pop("MILO_RENDER", None)
        env.update(extra_env or {})
        self.log_path = os.path.join(outdir, "engine.log")
        self.logf = open(self.log_path, "wb")
        self.proc = subprocess.Popen([binary], cwd=os.path.dirname(binary), env=env,
                                     stdout=self.logf, stderr=subprocess.STDOUT)
        self.t0 = time.time()
        self._off = 0
        self._partial = b""
        self.screens = []  # (wall_s, screen)
        self.lines_seen = 0

    # ---- http ----
    def get(self, path, timeout=10):
        with urllib.request.urlopen(self.base + path, timeout=timeout) as r:
            return r.read()

    def jget(self, path, timeout=10):
        try:
            return json.loads(self.get(path, timeout))
        except Exception:
            return None

    def eval(self, expr, timeout=10):
        try:
            req = urllib.request.Request(self.base + "/dta/eval", data=expr.encode(),
                                         method="POST")
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return json.loads(r.read())
        except Exception as e:  # noqa: BLE001
            return {"ok": False, "error": str(e)}

    def screenshot(self, name):
        try:
            png = self.get("/screenshot", timeout=60)
            p = os.path.join(self.out, name)
            with open(p, "wb") as f:
                f.write(png)
            return p
        except Exception:
            return None

    def alive(self):
        return self.proc.poll() is None

    def peak_rss_kb(self):
        try:
            with open(f"/proc/{self.proc.pid}/status") as f:
                for line in f:
                    if line.startswith("VmHWM:"):
                        return int(line.split()[1])
        except OSError:
            pass
        return None

    def pump(self):
        """Read new log lines; stamp screen entries with wall time."""
        try:
            with open(self.log_path, "rb") as f:
                f.seek(self._off)
                data = f.read()
        except OSError:
            return
        self._off += len(data)
        data = self._partial + data
        lines = data.split(b"\n")
        self._partial = lines.pop()
        now = time.time() - self.t0
        for raw in lines:
            m = SCREEN_RE.search(raw.decode("utf-8", "replace"))
            if m:
                self.screens.append((round(now, 1), m.group(1)))

    def entered(self, name):
        return next((t for t, s in self.screens if s == name), None)

    def stop(self):
        rss = self.peak_rss_kb()
        if self.alive():
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.logf.close()
        return rss


def route_script(downs, difficulty_downs=1):
    """title -> main -> choose_mode (perform) -> song select (downs) ->
    multiuser (difficulty, play, skip_waiting).  Spacing as betteroffalone.txt
    and song-select-scroll.txt; no tutorial screens (all marked seen)."""
    lines = ["wait_screen title_screen", "+30 confirm",
             "wait_screen main_screen", "+30 confirm",
             "wait_screen choose_mode_screen", "+30 confirm",
             "wait_screen song_select_screen"]
    t = 30
    for _ in range(downs):
        lines.append(f"+{t} down")
        t += 15
    lines.append(f"+{t + 60} confirm")
    lines.append("wait_screen multiuser_screen")
    t = 40
    for _ in range(difficulty_downs):
        lines.append(f"+{t} down")
        t += 70
    for _ in range(3):
        lines.append(f"+{t} confirm")
        t += 70
    return lines


def discover(binary, port, out):
    """Song-select rows as the game lists them: [(index, symbol, is_header)]."""
    d = os.path.join(out, "_discover")
    lines = ["wait_screen title_screen", "+30 confirm", "wait_screen main_screen",
             "+30 confirm", "wait_screen choose_mode_screen", "+30 confirm"]
    e = Engine(binary, port, d, lines, gpu=False, extra_env={"DC3_HTTP_PACE_FPS": "60"})
    rows = []
    try:
        deadline = time.time() + 180
        while time.time() < deadline and e.alive() and not e.jget("/health", 2):
            time.sleep(1)
        r = e.jget("/screen/wait/song_select_screen?timeout=120", timeout=130)
        if not (r and r.get("ok")):
            raise RuntimeError("discovery never reached song_select_screen")
        time.sleep(1)
        lst = "{song_select_panel find right_hand.hnl}"
        for n in range(200):
            sym = e.eval(f"{{{lst} data_symbol {n}}}").get("data", {}).get("value")
            if not sym or sym in ("0", 0):
                break
            hdr = e.eval(f"{{{lst} is_data_header {n}}}").get("data", {}).get("value")
            rows.append((n, sym, bool(hdr)))
        entry = e.eval(f"{{{lst} get_selected}}").get("data", {}).get("value")
    finally:
        e.stop()
    with open(os.path.join(out, "song_order.json"), "w") as f:
        json.dump({"entry_index": entry, "rows": rows}, f, indent=1)
    return entry, rows


def analyse_log(path):
    ratings = {}
    score = None
    moves = 0
    modals = {}
    file_lines = {}
    exit_line = None
    signal_line = None
    with open(path, "rb") as f:
        for raw in f:
            line = raw.decode("utf-8", "replace").rstrip("\n")
            m = RATING_RE.search(line)
            if m and m.group(1) == "0":
                moves += 1
                ratings[m.group(4)] = ratings.get(m.group(4), 0) + 1
                score = int(m.group(6))
                continue
            m = MODAL_RE.match(line)
            if m:
                key = (m.group(1), re.sub(r"0x[0-9a-f]+|\d+", "#", m.group(2))[:160])
                modals[key] = modals.get(key, 0) + 1
                if FILE_RE.search(m.group(2)):
                    file_lines[m.group(2)[:200]] = file_lines.get(m.group(2)[:200], 0) + 1
                continue
            if line.startswith("DC3_EXIT:"):
                exit_line = line
            elif "Caught signal" in line or "SIGSEGV" in line or "SIGABRT" in line:
                signal_line = signal_line or line[:200]
            elif FILE_RE.search(line) and ("milo" in line or ".mid" in line or "songs/" in line):
                file_lines[line[:200]] = file_lines.get(line[:200], 0) + 1
    return {
        "moves_rated": moves, "ratings": ratings, "final_score": score,
        "modals": [{"kind": k, "msg": m, "count": c} for (k, m), c in
                   sorted(modals.items(), key=lambda kv: -kv[1])],
        "file_lines": file_lines, "exit_line": exit_line, "signal_line": signal_line,
    }


def selected_song(log_path):
    """The row confirmed on song_select_screen, from the game's own select line."""
    with open(log_path, "rb") as f:
        data = f.read()
    at = data.find(b"Screen 'song_select_screen' Enter")
    if at < 0:
        return None
    m = re.search(rb"select name='right_hand.hnl' selected=(\d+) first=\d+ sym='(\w+)'", data[at:])
    return m.group(2).decode() if m else None


def play_song(a, song, downs, port, venue):
    tag = song + (f"_{venue}" if venue else "")
    out = os.path.join(os.path.abspath(a.out), tag)
    extra = {"DC3_POSE": "external", "DC3_POSE_PERFORM": a.performer,
             "DC3_MOVE_RATING_LOG": "1", "DC3_FAST_TIME": "1",
             "DC3_CONTROLLER_MODE": "forced"}
    if a.real_time:
        del extra["DC3_FAST_TIME"]
    e = Engine(a.binary, port, out, route_script(downs), gpu=not a.no_gpu, extra_env=extra)
    res = {"song": song, "venue_asked": venue, "downs": downs, "port": port,
           "out": out, "gameplay": False, "results": False, "bounced_to_title": False,
           "hang": None, "selected": None}
    t_start = time.time()
    try:
        deadline = time.time() + 180
        while time.time() < deadline and e.alive() and not e.jget("/health", 2):
            time.sleep(1)
        # menus -> multiuser (driven by the input script)
        venue_done = False
        hard = time.time() + a.timeout
        last_beat, last_move = None, time.time()
        shot_game = False
        game_t = None
        song_secs = None
        state = None
        while time.time() < hard and e.alive():
            e.pump()
            names = [s for _, s in e.screens]
            if venue and not venue_done and "multiuser_screen" in names:
                r = e.eval(f"{{meta_performer set_venue_pref {venue}}}"
                           "{meta_performer setup_venue}{gamedata get venue}")
                res["venue_set_reply"] = r.get("data", {}).get("value")
                venue_done = True
            if "loading_screen" in names and "title_screen" in names[names.index("loading_screen"):]:
                res["bounced_to_title"] = True
                break
            if "game_screen" in names:
                if game_t is None:
                    game_t = time.time()
                    v = e.eval("{gamedata get venue}")
                    res["venue"] = v.get("data", {}).get("value")
                tel = (e.jget("/telemetry") or {}).get("data", {})
                state = tel.get("state")
                beat = tel.get("beat")
                song_secs = tel.get("realSecs")
                if beat != last_beat:
                    last_beat, last_move = beat, time.time()
                elif time.time() - last_move > a.stall and state != "gameover":
                    res["hang"] = f"beat stuck at {beat} for {a.stall}s (state={state})"
                    break
                if not shot_game and beat is not None and beat > 48:
                    res["gameplay_png"] = e.screenshot("gameplay.png")
                    shot_game = True
                # realSecs is TaskMgr's kRealTime: it still holds the menu's
                # clock until the song starts, so count it only while playing.
                if (a.song_seconds and state == "playing" and song_secs is not None
                        and song_secs >= a.song_seconds):
                    if not shot_game:
                        res["gameplay_png"] = e.screenshot("gameplay.png")
                    break
                if any(s in names for s in ENDGAME_SCREENS):
                    break
            time.sleep(1)
        e.pump()
        names = [s for _, s in e.screens]
        res["gameplay"] = "game_screen" in names
        res["results"] = any(s in names for s in ENDGAME_SCREENS)
        res["song_secs_at_stop"] = song_secs
        res["state_at_stop"] = state
        if res["results"] and e.alive():
            # let the results tally run (UI time is 1/30 s per frame headless)
            try:
                fr = e.jget("/frame")["data"]["frame"]
                e.get(f"/frame/wait/{fr + 300}", timeout=120)
            except Exception:
                pass
            res["results_png"] = e.screenshot("results.png")
        if not res["gameplay"] and not res["bounced_to_title"] and e.alive():
            res["stuck_png"] = e.screenshot("stuck.png")
            res["stuck_screen"] = (e.jget("/screen") or {}).get("data", {}).get("screen")
        if time.time() >= hard:
            res["hang"] = res["hang"] or f"wall timeout {a.timeout}s"
    finally:
        res["alive_at_stop"] = e.alive()
        res["exit_code"] = e.proc.returncode
        res["peak_rss_mb"] = round((e.stop() or 0) / 1024, 1)
        if res["exit_code"] is None:
            res["exit_code"] = e.proc.returncode
    t_load = e.entered("loading_screen")
    t_game = e.entered("game_screen")
    res["load_s"] = round(t_game - t_load, 1) if (t_load is not None and t_game is not None) else None
    res["wall_s"] = round(time.time() - t_start, 1)
    res["screens"] = e.screens
    res.update(analyse_log(e.log_path))
    res["selected"] = selected_song(e.log_path)
    with open(os.path.join(out, "result.json"), "w") as f:
        json.dump(res, f, indent=1)
    return res


def fmt_ratings(r):
    order = ["move_perfect", "move_awesome", "move_ok", "move_bad"]
    return "/".join(str(r.get(k, 0)) for k in order) + (
        "" if set(r) <= set(order) else " +" + ",".join(f"{k}={v}" for k, v in r.items() if k not in order))


def summarise(a, results):
    rows = sorted(results, key=lambda r: (r["song"], r.get("venue_asked") or ""))
    md = ["| song | venue | sel | gameplay | results | P/A/O/B | score | modals (distinct/total) | file lines | load s | peak RSS MB | wall s | notes |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        notes = []
        if r["bounced_to_title"]:
            notes.append("BOUNCED to title")
        if r["hang"]:
            notes.append("HANG: " + r["hang"])
        if r.get("signal_line"):
            notes.append("signal: " + r["signal_line"])
        if r.get("selected") and r["selected"] != r["song"]:
            notes.append(f"selected {r['selected']}!")
        if r.get("stuck_screen"):
            notes.append(f"stuck on {r['stuck_screen']}")
        tot = sum(m["count"] for m in r["modals"])
        md.append("| {song} | {venue} | {sel} | {g} | {res} | {rat} | {sc} | {md}/{tot} | {fl} | {ld} | {rss} | {w} | {n} |".format(
            song=r["song"], venue=r.get("venue") or "-", sel="ok" if r.get("selected") == r["song"] else r.get("selected"),
            g="yes" if r["gameplay"] else "NO", res="yes" if r["results"] else ("n/a" if a.song_seconds else "NO"),
            rat=fmt_ratings(r["ratings"]), sc=r["final_score"], md=len(r["modals"]), tot=tot,
            fl=len(r["file_lines"]), ld=r["load_s"], rss=r["peak_rss_mb"], w=r["wall_s"],
            n="; ".join(notes)))
    with open(os.path.join(a.out, "summary.md"), "w") as f:
        f.write("\n".join(md) + "\n")
    with open(os.path.join(a.out, "summary.json"), "w") as f:
        json.dump(rows, f, indent=1)
    if shutil.which("montage"):
        for kind in ("gameplay", "results"):
            pngs = [(r["song"] + (f"_{r['venue_asked']}" if r.get("venue_asked") else ""), r.get(f"{kind}_png"))
                    for r in rows if r.get(f"{kind}_png")]
            if not pngs:
                continue
            cmd = ["montage"]
            for label, p in pngs:
                cmd += ["-label", label, p]
            cmd += ["-tile", "6x", "-geometry", "320x180+4+4", "-pointsize", "14",
                    os.path.join(a.out, f"contact_{kind}.png")]
            subprocess.run(cmd, check=False)
    print("\n".join(md))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--binary", default=os.path.join(REPO, "native", "build", "dc3-native"))
    ap.add_argument("--songs", default="all", help="comma list, or 'all' (every song row)")
    ap.add_argument("--venue", default="", help="venue preference (dci, rollerrink, ...); "
                    "comma list runs each song in each")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--port-base", type=int, default=9220)
    ap.add_argument("--performer", default="choreo", help="DC3_POSE_PERFORM mode")
    ap.add_argument("--song-seconds", type=float, default=0,
                    help="stop this many song-seconds into gameplay (0 = whole song)")
    ap.add_argument("--timeout", type=int, default=1800, help="wall cap per song")
    ap.add_argument("--stall", type=int, default=180, help="beat-not-moving cap")
    ap.add_argument("--no-gpu", action="store_true", help="MILO_NORENDER=1 (no screenshots)")
    ap.add_argument("--real-time", action="store_true",
                    help="no DC3_FAST_TIME: song time on the real clock (pair with DC3_HTTP_PACE_FPS)")
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    os.makedirs(a.out, exist_ok=True)
    a.binary = os.path.abspath(a.binary)
    if not os.access(a.binary, os.X_OK):
        sys.exit(f"error: {a.binary} not built")

    entry, rows = discover(a.binary, a.port_base, a.out)
    songs = [(n, s) for n, s, hdr in rows if not hdr and s not in ("playlists", "random_song")]
    print(f"[sweep] song select: {len(rows)} rows, {len(songs)} songs, entry focus {entry}", flush=True)
    if a.songs != "all":
        want = a.songs.split(",")
        missing = [w for w in want if w not in {s for _, s in songs}]
        if missing:
            sys.exit(f"error: not in song select: {missing}")
        songs = [(n, s) for n, s in songs if s in want]
    venues = [v for v in a.venue.split(",") if v] or [""]
    jobs = [(s, n - entry, v) for n, s in songs for v in venues]
    results = []
    ports = list(range(a.port_base + 1, a.port_base + 1 + a.jobs))
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        futs = {}
        free = list(ports)
        pending = list(jobs)

        def submit():
            s, d, v = pending.pop(0)
            p = free.pop(0)
            futs[ex.submit(play_song, a, s, d, p, v)] = (s, v, p)
        while pending and free:
            submit()
        while futs:
            done, _ = concurrent.futures.wait(futs, return_when=concurrent.futures.FIRST_COMPLETED)
            for fu in done:
                s, v, p = futs.pop(fu)
                free.append(p)
                try:
                    r = fu.result()
                except Exception as e:  # noqa: BLE001
                    r = {"song": s, "venue_asked": v, "error": repr(e), "gameplay": False,
                         "results": False, "bounced_to_title": False, "hang": None,
                         "ratings": {}, "final_score": None, "modals": [], "file_lines": {},
                         "load_s": None, "peak_rss_mb": None, "wall_s": None}
                results.append(r)
                print(f"[sweep] {s}{'/' + v if v else ''}: gameplay={r['gameplay']} "
                      f"results={r['results']} bounced={r['bounced_to_title']} hang={r['hang']} "
                      f"ratings={fmt_ratings(r['ratings'])} score={r['final_score']} "
                      f"load={r['load_s']}s rss={r['peak_rss_mb']}MB wall={r['wall_s']}s",
                      flush=True)
                if pending:
                    submit()
    summarise(a, results)
    ok = all(r["gameplay"] and (r["results"] or a.song_seconds) and not r["hang"]
             and not r["bounced_to_title"] for r in results)
    sys.exit(0 if ok else 3)


if __name__ == "__main__":
    main()
