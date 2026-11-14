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
  * the menus run paced at 60 fps, so the input script's 1800-frame waits are
    30 s of wall time on a loaded box (unpaced, a DLC boot's main_screen wait
    timed out at load ~200), and pacing is switched off (PUT /api/pace?fps=0)
    once game_screen is up; an input-script TIMEOUT before game_screen ends
    the run at once as a problem ("input route gave up");
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
# The input script gave up on a wait or a highlight (Joypad_Native.cpp): every
# press after it lands on the wrong screen, so the run cannot reach its song.
ROUTE_TIMEOUT_RE = re.compile(r"DC3 Input: TIMEOUT .*")
# Menus run paced so the input script's frame-counted waits (1800 frames) are
# 30 s of wall time whatever the box load; play_song unpaces at game_screen.
MENU_PACE_FPS = "60"
RATING_RE = re.compile(r"DC3 MOVE_RATING p=(\d+) measure=(-?\d+) move=(\S+) rating=(\S+) "
                       r"lastfrac=(-?[\d.]+) score=(-?\d+) diff=(\S+)(?: songbeat=(-?\d+))?")
# Known exception (PLAN H-52): a rating taken while the tick song position
# still reads the LAST beat of the measure being rated (`songbeat=3`).
# set_cur_move fires on the float beat crossing the bar line; when the frame
# lands on the bar line exactly, float rounding leaves the tick position a
# hair behind (beat 104.000000, tick 49919.9961 < 49920), the move's async
# detector is polled with the old measure and does not refresh, and the
# rating reads the previous refresh -- 0 (Bad) when the detector was just
# enabled.  The image does the same (Game::SetHamMove, MoveDetector::Poll and
# MoveAsyncDetector::MoveRatingFrac all 100% matched); DC3_FAST_TIME's fixed
# 1/120 s step reproduces it deterministically where a real clock lands on a
# bar line only by chance.  Counted, and kept out of the non-Perfect count.
BAR_LINE_LAG_BEAT = 3
MODAL_RE = re.compile(r"^MILO_(FAIL|NOTIFY|WARN|ASSERT)\w*: (.*)$")
FILE_RE = re.compile(r"(could not (open|find|load|cache)|couldn't (open|find|load)|"
                     r"can't (open|find)|not found|missing|no such file|failed to (open|load))",
                     re.I)
ENDGAME_SCREENS = ("perform_endgame_screen",)
# multiuser_screen's difficulty list opens on beginner
DIFFICULTY_DOWNS = {"beginner": 0, "easy": 1, "medium": 2, "expert": 3}


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
        self.route_timeouts = []  # "DC3 Input: TIMEOUT ..." lines
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

    def put(self, path, timeout=10):
        try:
            req = urllib.request.Request(self.base + path, data=b"", method="PUT")
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return json.loads(r.read())
        except Exception as e:  # noqa: BLE001
            return {"ok": False, "error": str(e)}

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
            line = raw.decode("utf-8", "replace")
            m = SCREEN_RE.search(line)
            if m:
                self.screens.append((round(now, 1), m.group(1)))
            m = ROUTE_TIMEOUT_RE.search(line)
            if m:
                self.route_timeouts.append(m.group(0).strip())

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


# --unlock-all: frames the route waits on main_screen before its confirm, so the
# sweep can evaluate {toggle_unlock_all} (the debug cheat, MetaPanel::
# ToggleUnlockAll) before song select builds its list.  Discovery and play_song's
# menus run paced at 60 fps (300 frames = 5 s); the unpaced figure is for a
# caller that turned pacing off (--env DC3_HTTP_PACE_FPS=0).
UNLOCK_PAUSE_FRAMES = 3000
UNLOCK_PAUSE_FRAMES_PACED = 300


def unlock_all(e):
    """{toggle_unlock_all} once main_screen is up: True if it is now on.  The
    image's own debug cheat (config/cheats.dta 'Meta: unlock all'): every
    profile-locked song (Black and Tan, unlocked by rank in the image) lists."""
    r = e.jget("/screen/wait/main_screen?timeout=120", timeout=130)
    if not (r and r.get("ok")):
        return False
    return e.eval("{toggle_unlock_all}").get("data", {}).get("value") in (1, True)


def route_script(downs, difficulty_downs=1, song=None, main_pause=30):
    """title -> main -> choose_mode (perform) -> song select -> multiuser
    (difficulty, play, skip_waiting).  The song is picked by NAME (`highlight`,
    scripts/dc3-input-flows/README.txt) when `song` is given, else by `downs`
    presses from the entry focus.  Spacing as betteroffalone.txt and
    song-select-scroll.txt; no tutorial screens (all marked seen)."""
    lines = ["wait_screen title_screen", "+30 confirm",
             "wait_screen main_screen", f"+{main_pause} confirm",
             "wait_screen choose_mode_screen", "+30 confirm",
             "wait_screen song_select_screen"]
    if song:
        lines.append(f"+30 highlight {song}")
        lines.append("+60 confirm")
    else:
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


def discover(binary, port, out, dlc="", unlock=False):
    """Song-select rows as the game lists them: [(index, symbol, is_header)]."""
    d = os.path.join(out, "_discover")
    pause = UNLOCK_PAUSE_FRAMES_PACED if unlock else 30
    lines = ["wait_screen title_screen", "+30 confirm", "wait_screen main_screen",
             f"+{pause} confirm", "wait_screen choose_mode_screen", "+30 confirm"]
    # DC3_DLC=0: the dev profile would otherwise mount <data>/dlc.
    env = {"DC3_HTTP_PACE_FPS": "60", "DC3_DLC": "0"}
    if dlc:
        env["DC3_DLC_DIR"] = dlc
    e = Engine(binary, port, d, lines, gpu=False, extra_env=env)
    rows = []
    try:
        deadline = time.time() + 180
        while time.time() < deadline and e.alive() and not e.jget("/health", 2):
            time.sleep(1)
        if unlock and not unlock_all(e):
            raise RuntimeError("discovery: {toggle_unlock_all} did not turn unlock-all on")
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
    # ratings taken in the bar-line lag (BAR_LINE_LAG_BEAT): all, the
    # non-Perfect ones (the known exceptions), and which they were
    lag = {"rated": 0, "not_perfect": 0, "moves": []}
    diffs = {}
    kinds = {}
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
                if m.group(8) is not None and int(m.group(8)) == BAR_LINE_LAG_BEAT:
                    lag["rated"] += 1
                    if m.group(4) != "move_perfect":
                        lag["not_perfect"] += 1
                        lag["moves"].append(f"{m.group(2)}:{m.group(3)}:{m.group(4)}")
                diffs[m.group(7)] = diffs.get(m.group(7), 0) + 1
                score = int(m.group(6))
                continue
            m = MODAL_RE.match(line)
            if m:
                key = (m.group(1), re.sub(r"0x[0-9a-f]+|\d+", "#", m.group(2))[:160])
                modals[key] = modals.get(key, 0) + 1
                kinds[m.group(1)] = kinds.get(m.group(1), 0) + 1
                if FILE_RE.search(m.group(2)):
                    file_lines[m.group(2)[:200]] = file_lines.get(m.group(2)[:200], 0) + 1
                continue
            if line.startswith("DC3_EXIT:"):
                exit_line = line
            elif ("Caught signal" in line or "SIGSEGV" in line or "SIGABRT" in line) \
                    and "DTA eval recovered" not in line:
                signal_line = signal_line or line[:200]
            elif FILE_RE.search(line) and ("milo" in line or ".mid" in line or "songs/" in line):
                file_lines[line[:200]] = file_lines.get(line[:200], 0) + 1
    return {
        "moves_rated": moves, "ratings": ratings, "final_score": score,
        "bar_line_lag": lag,
        "difficulty_rated": diffs, "modal_kinds": kinds,
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


# On multiuser_screen (the difficulty screen): who is in each player slot.
# pad_num / skeleton_tracking_id / playing are HamPlayerData properties, the
# player_name is what the screen's player box shows (PLAN P-16).
PLAYER_PROBE = {
    "pad_num": "{gamedata getp %d pad_num}",
    "playing": "{gamedata getp %d playing}",
    "skeleton_tracking_id": "{gamedata getp %d skeleton_tracking_id}",
    "player_name": "{{gamedata getp %d provider} get player_name}",
}


def probe_players(e):
    out = {"has_active_profile": (e.eval("{profile_mgr has_active_profile}") or {})
           .get("data", {}).get("value")}
    for p in (0, 1):
        out[f"p{p}"] = {k: (e.eval(x % p) or {}).get("data", {}).get("value")
                        for k, x in PLAYER_PROBE.items()}
    return out


def play_song(a, song, downs, port, venue, difficulty="easy"):
    tag = song + (f"_{venue}" if venue else "") + (f"_{difficulty}" if a.multi_difficulty else "")
    out = os.path.join(os.path.abspath(a.out), tag)
    extra = {"DC3_POSE": "external", "DC3_POSE_PERFORM": a.performer.replace("{song}", song),
             "DC3_MOVE_RATING_LOG": "1", "DC3_FAST_TIME": "1",
             "DC3_CONTROLLER_MODE": "forced"}
    if a.real_time:
        del extra["DC3_FAST_TIME"]
    extra["DC3_DLC"] = "0"  # only the roots named by --dlc (not the dev profile's <data>/dlc)
    menu_paced = not a.real_time
    if menu_paced:
        extra["DC3_HTTP_PACE_FPS"] = MENU_PACE_FPS
    if a.dlc:
        extra["DC3_DLC_DIR"] = a.dlc
    for kv in a.env:
        k, _, v = kv.partition("=")
        extra[k] = v
        if k == "DC3_HTTP_PACE_FPS":
            menu_paced = False  # the caller's pace holds for the whole run
    paced = extra.get("DC3_HTTP_PACE_FPS", "0") not in ("", "0")
    main_pause = ((UNLOCK_PAUSE_FRAMES_PACED if paced else UNLOCK_PAUSE_FRAMES)
                  if a.unlock_all else 30)
    e = Engine(a.binary, port, out,
               route_script(downs, DIFFICULTY_DOWNS[difficulty],
                            None if a.by_position else song, main_pause),
               gpu=not a.no_gpu, extra_env=extra)
    res = {"song": song, "venue_asked": venue, "difficulty_asked": difficulty,
           "downs": downs, "port": port,
           "out": out, "gameplay": False, "results": False, "bounced_to_title": False,
           "hang": None, "selected": None}
    t_start = time.time()
    try:
        deadline = time.time() + 180
        while time.time() < deadline and e.alive() and not e.jget("/health", 2):
            time.sleep(1)
        if a.unlock_all:
            res["unlock_all"] = unlock_all(e)
        # menus -> multiuser (driven by the input script)
        venue_done = False
        evals_done = False
        hard = time.time() + a.timeout
        last_beat, last_move = None, time.time()
        shot_game = False
        game_t = None
        song_secs = None
        state = None
        while time.time() < hard and e.alive():
            e.pump()
            names = [s for _, s in e.screens]
            if not evals_done and "multiuser_screen" in names:
                res["players_at_difficulty"] = probe_players(e)
                if a.eval:
                    res["eval_replies"] = [e.eval(x).get("data", {}).get("value") for x in a.eval]
                evals_done = True
            if venue and not venue_done and "multiuser_screen" in names:
                r = e.eval(f"{{meta_performer set_venue_pref {venue}}}"
                           "{meta_performer setup_venue}{gamedata get venue}")
                res["venue_set_reply"] = r.get("data", {}).get("value")
                venue_done = True
            if "loading_screen" in names and "title_screen" in names[names.index("loading_screen"):]:
                res["bounced_to_title"] = True
                break
            if e.route_timeouts and "game_screen" not in names:
                res["route_timeout"] = e.route_timeouts[0]
                break
            if "game_screen" in names:
                if game_t is None:
                    game_t = time.time()
                    if menu_paced:  # song time is 1/120 s per frame: run it unpaced
                        res["unpaced"] = (e.put("/pace?fps=0") or {}).get("ok")
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
                    else:  # a second look: tells a black frame from a black screen
                        res["gameplay_end_png"] = e.screenshot("gameplay_end.png")
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
            # The results card's own numbers (perform_endgame.dta): gems =
            # num_perfect, green = num_awesome, both counted by perform.dta's
            # move handler only from start_score_move_index on -- so they are
            # NOT the counts of DC3 MOVE_RATING lines -- and the percentage
            # is meta_performer get_moves_passed.
            card = {}
            for key, expr in (("num_perfect", "{{gamedata getp 0 provider} get num_perfect}"),
                              ("num_awesome", "{{gamedata getp 0 provider} get num_awesome}"),
                              ("moves_passed_pct", "{meta_performer get_moves_passed 0}")):
                card[key] = (e.eval(expr) or {}).get("data", {}).get("value")
            res["card"] = card
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


DIFF_RATED = {"easy": "easy", "medium": "medium", "expert": "expert"}


def run_problems(a, r):
    """Why a run does not count as 'played to results cleanly' (empty = ok)."""
    p = []
    if r.get("error"):
        p.append("driver error: " + r["error"])
    if not r["gameplay"]:
        p.append("no gameplay" + (f" (stuck on {r['stuck_screen']})" if r.get("stuck_screen") else ""))
    elif not r["results"] and not a.song_seconds:
        p.append("no results")
    if r["bounced_to_title"]:
        p.append("bounced to title")
    if r["hang"]:
        p.append("hang: " + r["hang"])
    if r.get("route_timeout"):
        p.append("input route gave up: " + r["route_timeout"])
    if r.get("signal_line"):
        p.append("signal: " + r["signal_line"])
    if r.get("alive_at_stop") is False and r.get("exit_code") not in (0, None):
        p.append(f"engine exited {r['exit_code']} on its own")
    if r.get("selected") and r["selected"] != r["song"]:
        p.append(f"selected {r['selected']}")
    want = DIFF_RATED.get(r.get("difficulty_asked"))
    got = r.get("difficulty_rated") or {}
    if want and got and set(got) != {want}:
        p.append(f"rated at {sorted(got)} not {want}")
    if want and r["results"] and not r.get("moves_rated"):
        p.append("no move rated")
    kinds = r.get("modal_kinds") or {}
    if kinds.get("FAIL") or kinds.get("ASSERT"):
        p.append(f"FAIL {kinds.get('FAIL', 0)} / ASSERT {kinds.get('ASSERT', 0)}")
    if a.unlock_all and not r.get("unlock_all"):
        p.append("{toggle_unlock_all} did not turn unlock-all on")
    pl = (r.get("players_at_difficulty") or {}).get("p0") or {}
    if r.get("players_at_difficulty") and (pl.get("pad_num") in (-1, None) or not pl.get("player_name")):
        p.append(f"player 1 not signed in at the difficulty screen ({pl})")
    return p


def rating_totals(rows):
    """Player-1 ratings over all runs: rated, not Perfect, and the known
    exceptions among the not-Perfect (bar-line lag, BAR_LINE_LAG_BEAT)."""
    t = {"rated": 0, "not_perfect": 0, "known_exceptions": 0}
    for r in rows:
        rat = r.get("ratings") or {}
        n = sum(rat.values())
        t["rated"] += n
        t["not_perfect"] += n - rat.get("move_perfect", 0)
        t["known_exceptions"] += (r.get("bar_line_lag") or {}).get("not_perfect", 0)
    return t


def summarise(a, results):
    rows = sorted(results, key=lambda r: (r["song"], r.get("venue_asked") or "",
                                          list(DIFFICULTY_DOWNS).index(r.get("difficulty_asked") or "easy")))
    md = ["| song | diff | venue | gameplay | results | P/A/O/B | known exc. | score | NOTIFY/WARN/FAIL/ASSERT | file lines | load s | peak RSS MB | wall s | problems |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        r["problems"] = run_problems(a, r)
        k = r.get("modal_kinds") or {}
        lag = r.get("bar_line_lag") or {}
        md.append("| {song} | {d} | {venue} | {g} | {res} | {rat} | {ke} | {sc} | {mk} | {fl} | {ld} | {rss} | {w} | {n} |".format(
            ke=lag.get("not_perfect", 0),
            song=r["song"], d=r.get("difficulty_asked"), venue=r.get("venue") or "-",
            g="yes" if r["gameplay"] else "NO", res="yes" if r["results"] else ("n/a" if a.song_seconds else "NO"),
            rat=fmt_ratings(r["ratings"]), sc=r["final_score"],
            mk="/".join(str(k.get(x, 0)) for x in ("NOTIFY", "WARN", "FAIL", "ASSERT")),
            fl=len(r["file_lines"]), ld=r["load_s"], rss=r["peak_rss_mb"], w=r["wall_s"],
            n="; ".join(r["problems"]).replace("|", "/")))
    bad = [r for r in rows if r["problems"]]
    tot = rating_totals(rows)
    head = [f"# Song sweep: {len(rows)} runs, {len(rows) - len(bad)} clean, {len(bad)} with problems", "",
            f"commit {a.commit}; difficulties {','.join(a.difficulties)}; "
            f"DLC {a.dlc or 'off'}; performer {a.performer}; jobs {a.jobs}", "",
            f"moves rated {tot['rated']}: not Perfect {tot['not_perfect']}, of which "
            f"{tot['known_exceptions']} known exceptions (bar-line lag, PLAN H-52) -> "
            f"{tot['not_perfect'] - tot['known_exceptions']} unexplained", ""]
    with open(os.path.join(a.out, "summary.md"), "w") as f:
        f.write("\n".join(head + md) + "\n")
    with open(os.path.join(a.out, "summary.json"), "w") as f:
        json.dump(rows, f, indent=1)
    # The machine-readable verdict: one record per run, and the totals.
    with open(os.path.join(a.out, "sweep.json"), "w") as f:
        json.dump({
            "commit": a.commit, "binary": a.binary, "difficulties": a.difficulties,
            "dlc": a.dlc, "performer": a.performer, "jobs": a.jobs,
            "songs": sorted({r["song"] for r in rows}), "runs": len(rows),
            "clean": len(rows) - len(bad),
            "rating_totals": tot,
            "problems": {f"{r['song']}/{r.get('difficulty_asked')}": r["problems"] for r in bad},
            "results": [{k: r.get(k) for k in (
                "song", "difficulty_asked", "venue", "gameplay", "results", "ratings",
                "bar_line_lag", "final_score", "card", "modal_kinds", "load_s", "wall_s", "peak_rss_mb",
                "exit_code", "players_at_difficulty", "unpaced", "route_timeout", "problems")} for r in rows],
        }, f, indent=1)
    if shutil.which("montage"):
        for kind in ("gameplay", "results"):
            pngs = [(r["song"] + (f"_{r['venue_asked']}" if r.get("venue_asked") else "")
                     + (f"_{r['difficulty_asked']}" if a.multi_difficulty else ""), r.get(f"{kind}_png"))
                    for r in rows if r.get(f"{kind}_png")]
            if not pngs:
                continue
            cmd = ["montage"]
            for label, p in pngs:
                cmd += ["-label", label, p]
            cmd += ["-tile", "6x", "-geometry", "320x180+4+4", "-pointsize", "14",
                    os.path.join(a.out, f"contact_{kind}.png")]
            subprocess.run(cmd, check=False)
    print("\n".join(head + md))
    return bad


def selftest():
    """--selftest: the known-exception counting on a synthetic log (no engine).
    Exit 0 when every check holds, 1 otherwise."""
    import tempfile
    lines = [
        # usual case: song position crossed with the float beat
        "DC3 MOVE_RATING p=0 measure=24 move=A.move rating=move_perfect lastfrac=1.0000 score=10 diff=easy songbeat=0",
        # the H-52 case: lag, not Perfect -> a known exception
        "DC3 MOVE_RATING p=0 measure=25 move=B.move rating=move_bad lastfrac=0.0000 score=10 diff=easy songbeat=3",
        # lag but Perfect (stale 1.0): counted as lag, not an exception
        "DC3 MOVE_RATING p=0 measure=26 move=B.move rating=move_perfect lastfrac=1.0000 score=20 diff=easy songbeat=3",
        # not Perfect WITHOUT the lag: must stay unexplained
        "DC3 MOVE_RATING p=0 measure=27 move=B.move rating=move_awesome lastfrac=0.8000 score=30 diff=easy songbeat=0",
        # a log from before songbeat= existed: parsed, never an exception
        "DC3 MOVE_RATING p=0 measure=28 move=B.move rating=move_ok lastfrac=0.5000 score=40 diff=easy",
        # player 2: ignored
        "DC3 MOVE_RATING p=1 measure=28 move=B.move rating=move_bad lastfrac=0.0000 score=40 diff=easy songbeat=3",
    ]
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        path = f.name
    try:
        r = analyse_log(path)
    finally:
        os.unlink(path)
    tot = rating_totals([r])
    checks = [
        ("moves rated", r["moves_rated"], 5),
        ("ratings", r["ratings"], {"move_perfect": 2, "move_bad": 1, "move_awesome": 1, "move_ok": 1}),
        ("lag rated", r["bar_line_lag"]["rated"], 2),
        ("lag not Perfect", r["bar_line_lag"]["not_perfect"], 1),
        ("lag moves", r["bar_line_lag"]["moves"], ["25:B.move:move_bad"]),
        ("totals", tot, {"rated": 5, "not_perfect": 3, "known_exceptions": 1}),
    ]
    bad = 0
    for name, got, want in checks:
        ok = got == want
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {got}" + ("" if ok else f" (want {want})"))
    print("selftest: " + ("PASS" if not bad else f"{bad} FAILED"))
    return 1 if bad else 0


def main():
    if "--selftest" in sys.argv[1:]:
        sys.exit(selftest())
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--binary", default=os.path.join(REPO, "native", "build", "dc3-native"))
    ap.add_argument("--songs", default="all", help="comma list, or 'all' (every song row)")
    ap.add_argument("--venue", default="", help="venue preference (dci, rollerrink, ...); "
                    "comma list runs each song in each")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--port-base", type=int, default=9220)
    ap.add_argument("--performer", default="choreo",
                    help="DC3_POSE_PERFORM mode; '{song}' is replaced by the song id "
                         "(replay:DIR/{song}.dc3pose)")
    ap.add_argument("--difficulty", default="easy",
                    help="comma list of " + ",".join(DIFFICULTY_DOWNS) + " (expert is the "
                    "screen's HARD), or 'all'; each song is played once per difficulty")
    ap.add_argument("--dlc", default="",
                    help="DC3_DLC_DIR for discovery and every run (a ':'-separated list "
                         "of DLC roots); the song list then includes the packages' songs")
    ap.add_argument("--only-dlc", action="store_true",
                    help="with --dlc: sweep only the songs the packages add (rows that "
                         "a discovery without DLC does not list)")
    ap.add_argument("--eval", action="append", default=[], metavar="DTA",
                    help="DTA evaluated once on multiuser_screen, repeatable; e.g. "
                         "'{profile_mgr toggle_disable_freestyle}' (Options > Freestyle off)")
    ap.add_argument("--env", action="append", default=[], metavar="K=V",
                    help="extra engine environment, repeatable (e.g. DC3_POSE_LATENCY_MS=250)")
    ap.add_argument("--song-seconds", type=float, default=0,
                    help="stop this many song-seconds into gameplay (0 = whole song)")
    ap.add_argument("--timeout", type=int, default=1800, help="wall cap per song")
    ap.add_argument("--stall", type=int, default=180, help="beat-not-moving cap")
    ap.add_argument("--no-gpu", action="store_true", help="MILO_NORENDER=1 (no screenshots)")
    ap.add_argument("--by-position", action="store_true",
                    help="pick songs by counted downs instead of `highlight NAME` "
                         "(a binary whose engine predates the directive)")
    ap.add_argument("--unlock-all", action="store_true",
                    help="evaluate {toggle_unlock_all} (the debug cheat) on main_screen, so "
                         "profile-locked songs (blackandtan) are listed and played")
    ap.add_argument("--selftest", action="store_true",
                    help="check the rating / known-exception counting on a synthetic log and exit")
    ap.add_argument("--real-time", action="store_true",
                    help="no DC3_FAST_TIME: song time on the real clock (pair with DC3_HTTP_PACE_FPS)")
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    os.makedirs(a.out, exist_ok=True)
    a.binary = os.path.abspath(a.binary)
    a.commit = subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
    if not os.access(a.binary, os.X_OK):
        sys.exit(f"error: {a.binary} not built")

    a.difficulties = list(DIFFICULTY_DOWNS) if a.difficulty == "all" else a.difficulty.split(",")
    bad = [d for d in a.difficulties if d not in DIFFICULTY_DOWNS]
    if bad:
        sys.exit(f"error: unknown difficulty {bad}")
    a.multi_difficulty = len(a.difficulties) > 1
    if a.dlc:
        a.dlc = ":".join(os.path.abspath(x) for x in a.dlc.split(":") if x)
    entry, rows = discover(a.binary, a.port_base, a.out, a.dlc, a.unlock_all)
    songs = [(n, s) for n, s, hdr in rows if not hdr and s not in ("playlists", "random_song")]
    if a.dlc and a.only_dlc:
        _, disc_rows = discover(a.binary, a.port_base, os.path.join(a.out, "_disc"),
                                unlock=a.unlock_all)
        disc = {s for _, s, _ in disc_rows}
        songs = [(n, s) for n, s in songs if s not in disc]
    print(f"[sweep] song select: {len(rows)} rows, {len(songs)} songs, entry focus {entry}", flush=True)
    if a.songs != "all":
        want = a.songs.split(",")
        missing = [w for w in want if w not in {s for _, s in songs}]
        if missing:
            sys.exit(f"error: not in song select: {missing}")
        songs = [(n, s) for n, s in songs if s in want]
    venues = [v for v in a.venue.split(",") if v] or [""]
    jobs = [(s, n - entry, v, d) for n, s in songs for v in venues for d in a.difficulties]
    results = []
    ports = list(range(a.port_base + 1, a.port_base + 1 + a.jobs))
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        futs = {}
        free = list(ports)
        pending = list(jobs)

        def submit():
            s, d, v, diff = pending.pop(0)
            p = free.pop(0)
            futs[ex.submit(play_song, a, s, d, p, v, diff)] = (s, v, p, diff)
        while pending and free:
            submit()
        while futs:
            done, _ = concurrent.futures.wait(futs, return_when=concurrent.futures.FIRST_COMPLETED)
            for fu in done:
                s, v, p, diff = futs.pop(fu)
                free.append(p)
                try:
                    r = fu.result()
                except Exception as e:  # noqa: BLE001
                    r = {"song": s, "venue_asked": v, "difficulty_asked": diff,
                         "error": repr(e), "gameplay": False,
                         "results": False, "bounced_to_title": False, "hang": None,
                         "ratings": {}, "final_score": None, "modals": [], "file_lines": {},
                         "load_s": None, "peak_rss_mb": None, "wall_s": None}
                results.append(r)
                print(f"[sweep] {s}{'/' + v if v else ''}/{diff}: gameplay={r['gameplay']} "
                      f"results={r['results']} bounced={r['bounced_to_title']} hang={r['hang']} "
                      f"ratings={fmt_ratings(r['ratings'])} score={r['final_score']} "
                      f"load={r['load_s']}s rss={r['peak_rss_mb']}MB wall={r['wall_s']}s",
                      flush=True)
                if pending:
                    submit()
    bad = summarise(a, results)
    sys.exit(0 if not bad else 3)


if __name__ == "__main__":
    main()
