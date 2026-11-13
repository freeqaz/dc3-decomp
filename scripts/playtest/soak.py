#!/usr/bin/env python3
"""soak.py -- play one long session through the playtest kit, the way a player
would, and write down everything that goes wrong (PLAN L-2).

  soak.py --footage DIR --out DIR [--gpu-lock FILE] [--short] [--norender]

Every game process is started by scripts/play.sh --headless --video, so the
real tracker (pose_server.py + MediaPipe, via footage_camera.py) watches
recorded footage of a person dancing, in step with the song clock; menus are
pressed over the debug server, a pad wake before each, as a player picking up
a pad would.  FOOTAGE DIR holds clips cut by make_footage.py (<song>.mp4 and
<song>_duo.mp4).  One home folder (saves, cache) carries across the whole
soak, as one console would.

The full run (~45-60 min):
  A  boot; Perform song 1, 2, 3 back to back (results each time); back to
     the main menu; Rehearse one song (time-boxed); quit (SIGTERM, as closing
     the window does) -- the saves are checked here
  B  relaunch: the profile must come back; Dance Battle with two people
     (a stand-in clip: the same dancer twice); Story from the start, first
     song to its results and on; quit
  C  relaunch; Fitness: fitness mode on, a playlist song to its results; quit

--short: boot, one Perform song, quit, relaunch, check the save (a few
minutes).  Writes OUT/soak.json and OUT/soak.txt (the report) and one session
folder per process under OUT/.

Watched all the time: RSS of the game (every 10 s), the frame counter (a
hang = no new frame for 20 s while the process lives), the pause flag during
songs (a pause is recorded, then Continue is pressed: PLAN P-12 is lane
np-tracking's), every MILO_FAIL / ASSERT / NOTIFY (from each session's
engine.log, by session_summary.py), crashes (no DC3_EXIT line).
"""
import argparse
import json
import os
import shutil
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kitdrive  # noqa: E402
import session_summary  # noqa: E402

T0 = time.time()
EVENTS = []


def say(*a):
    msg = " ".join(str(x) for x in a)
    line = f"{time.strftime('%H:%M:%S')} +{(time.time() - T0) / 60:5.1f}m  {msg}"
    print(line, flush=True)
    EVENTS.append(line)


class Watch(threading.Thread):
    """RSS, hangs and pauses, sampled from outside the game."""

    def __init__(self, kit, label):
        super().__init__(daemon=True)
        self.kit, self.label = kit, label
        self.rss, self.hangs, self.pauses = [], [], []
        self.stop = False
        self.in_song = False
        self.paused_since = None

    def run(self):
        last_frame, last_change = None, time.time()
        while not self.stop and self.kit.alive():
            now = time.time()
            r = self.kit.rss_kb()
            if r:
                self.rss.append((round(now - T0, 1), r))
            h = self.kit.jget("/health", 5)
            frame = (h or {}).get("data", {}).get("frame") if h else None
            if frame is not None and frame != last_frame:
                if last_frame is not None and now - last_change > 20:
                    self.hangs[-1]["end"] = round(now - T0, 1)
                last_frame, last_change = frame, now
            elif now - last_change > 20 and (not self.hangs or "end" in self.hangs[-1]):
                self.hangs.append({"start": round(last_change - T0, 1), "screen": self.kit.screen(),
                                   "frame": last_frame})
                say(f"[{self.label}] HANG: no new frame for 20 s on {self.kit.screen()}")
            if self.in_song:
                p = self.kit.eval("{game get_paused}", 3)
                if p in (1, True) and self.paused_since is None:
                    self.paused_since = now
                    song_s = self.kit.eval("{taskmgr seconds}", 3)
                    self.pauses.append({"at": round(now - T0, 1), "song_s": song_s,
                                        "screen": self.kit.screen()})
                    say(f"[{self.label}] PAUSE in a song at song time {song_s}")
                elif p in (0, False):
                    self.paused_since = None
            time.sleep(10 if not self.in_song else 2)


def advance(kit, targets, timeout, prefer=("continue", "done", "play_again_no", "next"),
            label=""):
    """Get to one of `targets` the way a player mashing A would: pick a
    preferred row when the focused list has one, else press confirm."""
    targets = (targets,) if isinstance(targets, str) else tuple(targets)
    end = time.time() + timeout
    seen = []
    while time.time() < end and kit.alive():
        s = kit.screen()
        if s in targets:
            return s
        if not seen or seen[-1] != s:
            seen.append(s)
            say(f"{label} on {s}")
            time.sleep(2.0)
            continue
        rows = kit.rows(kit.focus_list(), limit=12)
        pick = next((p for p in prefer if p in rows), None)
        if pick:
            kit.choose(pick)
        else:
            kit.press("confirm")
        time.sleep(3.0)
    return None


def footage(dirn, song, duo=False):
    p = os.path.join(dirn, f"{song}{'_duo' if duo else ''}.mp4")
    return p if os.path.exists(p) else None


class Soak:
    def __init__(self, a):
        self.a = a
        self.home = os.path.join(a.out, "home")
        kitdrive.seed_home(self.home, tutorials_seen=not a.fresh)
        self.control = os.path.join(a.out, "footage_control.txt")
        self.default_clip = footage(a.footage, "macarena") or sorted(
            os.path.join(a.footage, f) for f in os.listdir(a.footage) if f.endswith(".mp4"))[0]
        self.results = {"legs": [], "processes": []}
        self.kit = None
        self.watch = None
        self.n = 0

    def clip(self, path):
        with open(self.control + ".tmp", "w") as f:
            f.write(f"{path} {self.a.pre}\n")
        os.replace(self.control + ".tmp", self.control)
        say(f"footage: {os.path.basename(path)}")

    def launch(self):
        self.n += 1
        label = f"proc{self.n}"
        env = {"DC3_FOOTAGE_CONTROL": self.control}
        env["MILO_NORENDER" if self.a.norender else "MILO_RENDER"] = "1"
        self.clip(self.default_clip)
        self.kit = kitdrive.Kit(os.path.join(self.a.out, label), self.home,
                                args=["--headless", "--video", self.default_clip,
                                      "--video-pre", str(self.a.pre)],
                                env=env, gpu_lock=self.a.gpu_lock,
                                log=lambda m: say(f"[{label}] {m}"))
        self.kit.wait_port()
        self.watch = Watch(self.kit, label)
        self.watch.start()
        say(f"[{label}] game up (pid {self.kit.game_pid()})")
        return label

    def close(self, why):
        label = f"proc{self.n}"
        self.watch.stop = True
        alive = self.kit.alive()
        rc = self.kit.quit()
        text, parsed = session_summary.summarise(self.kit.session)
        saves = {}
        sd = os.path.join(self.home, "saves")
        for f in sorted(os.listdir(sd)) if os.path.isdir(sd) else []:
            st = os.stat(os.path.join(sd, f))
            saves[f] = {"bytes": st.st_size, "mtime": round(st.st_mtime - T0, 1)}
        snap = os.path.join(self.a.out, f"{label}-saves-after")
        shutil.copytree(sd, snap, dirs_exist_ok=True)
        proc = {"label": label, "why_closed": why, "alive_at_close": alive, "rc": rc,
                "session": self.kit.session, "rss": self.watch.rss, "hangs": self.watch.hangs,
                "pauses": self.watch.pauses, "saves": saves, "summary": text,
                "exit": parsed["exit"], "signal": parsed["signal"],
                "modal_kinds": len(parsed["modals"]),
                "modals": {k: v["count"] for k, v in parsed["modals"].items()},
                "songs": parsed["songs"]}
        self.results["processes"].append(proc)
        say(f"[{label}] closed ({why}): rc {rc}; saves {saves}")
        return proc

    # ---- legs ----
    def leg(self, name, fn, *args):
        t = time.time()
        rec = {"leg": name, "start_min": round((t - T0) / 60, 2), "process": f"proc{self.n}"}
        say(f"== {name}")
        try:
            rec.update(fn(*args) or {})
            rec["ok"] = rec.get("ok", True)
        except RuntimeError as e:
            rec["ok"] = False
            rec["error"] = str(e)
            rec["screen"] = self.kit.screen()
            say(f"== {name} FAILED: {e}")
        rec["minutes"] = round((time.time() - t) / 60, 2)
        self.results["legs"].append(rec)
        return rec

    def song(self, mode, song, difficulty, end_screens, song_timeout=420, sides=1, clip=None,
             back_to=("song_select_screen",)):
        k = self.kit
        self.clip(clip or footage(self.a.footage, song) or self.default_clip)
        if k.screen() != "song_select_screen":
            k.perform(song, difficulty, mode=mode) if sides == 1 else self._battle(song, difficulty)
        else:
            k.pick_song(song)
            k.multiuser(difficulty, sides=sides)
        return self.play_to_results(end_screens, song_timeout, back_to)

    def _battle(self, song, difficulty):
        k = self.kit
        if not k.choose("gameplay"):
            raise RuntimeError("main_screen has no gameplay row")
        k.need("choose_mode_screen")
        if not k.choose("dance_battle"):
            raise RuntimeError("no dance_battle row")
        k.need("song_select_screen")
        k.pick_song(song)
        k.multiuser(difficulty, sides=2)

    def play_to_results(self, end_screens, song_timeout, back_to):
        k = self.kit
        self.watch.in_song = True
        t = time.time()
        got = None
        try:
            end = time.time() + song_timeout
            while time.time() < end and k.alive():
                s = k.screen()
                if s in end_screens:
                    got = s
                    break
                if self.watch.paused_since and time.time() - self.watch.paused_since > 4:
                    rows = k.rows(k.focus_list(), limit=12)
                    say(f"paused; pause menu rows {rows}: pressing Continue")
                    pick = next((p for p in ("resume", "continue", "keep_dancing") if p in rows),
                                None)
                    k.choose(pick) if pick else k.press("confirm")
                    self.watch.paused_since = time.time()
                time.sleep(2)
        finally:
            self.watch.in_song = False
        if not got:
            raise RuntimeError(f"no results within {song_timeout} s (at {k.screen()})")
        secs = time.time() - t
        time.sleep(6)
        card = k.card(0)
        card1 = k.card(1)
        say(f"results on {got} after {secs:.0f} s: p1 {card}"
            + (f", p2 {card1}" if card1.get("score") else ""))
        nxt = advance(k, back_to, 180, label="after results") if back_to else None
        return {"results_screen": got, "song_s": round(secs), "card": card, "card_p2": card1,
                "next": nxt, "ok": bool(nxt) or not back_to}

    def back_to_main(self):
        k = self.kit
        for _ in range(6):
            if k.screen() == "main_screen":
                return {"ok": True}
            k.press("cancel")
            time.sleep(3)
        return {"ok": k.screen() == "main_screen", "screen": k.screen()}

    def rehearse(self, song):
        k = self.kit
        self.clip(footage(self.a.footage, song) or self.default_clip)
        k.perform(song, "medium", mode="practice")
        self.watch.in_song = True
        end = time.time() + self.a.rehearse_min * 60
        seen = []
        try:
            while time.time() < end and k.alive():
                s = k.screen()
                if not seen or seen[-1] != s:
                    seen.append(s)
                    say(f"rehearse: {s}")
                if s == "practice_endgame_screen":
                    break
                time.sleep(3)
        finally:
            self.watch.in_song = False
        reached = k.screen()
        return {"screens": seen, "ended_on": reached,
                "reached_results": reached == "practice_endgame_screen",
                "ok": reached in ("practice_endgame_screen", "game_screen")}

    def story(self):
        k = self.kit
        if not k.choose("campaign_mode"):
            raise RuntimeError("main_screen has no campaign_mode row")
        got = advance(k, ("game_screen",), 240, prefer=("continue",), label="story")
        if not got:
            raise RuntimeError(f"story never reached gameplay (at {k.screen()})")
        r = self.play_to_results(("campaign_perform_endgame_screen", "perform_endgame_screen"),
                                 480, None)
        nxt = advance(k, ("game_screen", "campaign_intro_perform_continue_screen",
                          "playlist_nextsong_screen", "playlist_nextsong_loading_screen"),
                      240, label="story after song 1")
        r["after_song1"] = nxt
        r["ok"] = bool(nxt)
        return r

    def fitness(self, song_timeout=480):
        k = self.kit
        if not k.choose("fitness_mode"):
            raise RuntimeError("main_screen has no fitness_mode row")
        k.need("fitness_screen")
        rows = k.rows(k.focus_list(), limit=8)
        say(f"fitness rows {rows}")
        k.choose(rows[0] if rows else "fitness_option")   # fitness mode on
        time.sleep(3)
        if not k.choose("playlists"):
            raise RuntimeError("fitness_screen has no playlists row")
        got = advance(k, ("song_select_playlist_screen",), 60, label="fitness")
        if not got:
            raise RuntimeError(f"no playlist screen (at {k.screen()})")
        time.sleep(2)
        rows = k.rows(k.focus_list(), limit=20)
        say(f"playlist rows {rows}")
        pick = next((r for r in rows if r.endswith("_playlist") and "custom" not in r
                     and "create" not in r), None)
        if not pick or not k.choose(pick):
            raise RuntimeError(f"no playlist to pick in {rows}")
        time.sleep(3)
        got = advance(k, ("multiuser_screen", "game_screen"), 60, prefer=("playlist_play",),
                      label="playlist")
        if got == "multiuser_screen":
            k.multiuser("medium")
        r = self.play_to_results(("perform_endgame_screen", "fitness_endgame_screen"),
                                 song_timeout, None)
        r["after"] = advance(k, ("game_screen", "playlist_nextsong_screen",
                                 "playlist_nextsong_loading_screen", "fitness_screen",
                                 "main_screen"), 240, label="fitness after song")
        return r

    def check_profile_back(self):
        """After a relaunch: was the save read back?  The game logs every load."""
        k = self.kit
        k.boot_to_main()
        time.sleep(2)
        log = open(k.engine_log, "rb").read().decode("utf-8", "replace")
        loaded = [l for l in log.splitlines() if "loaded" in l and "save" in l.lower()][:3]
        return {"save_load_lines": loaded, "ok": bool(loaded)}

    def run(self):
        a = self.a
        self.launch()
        self.leg("boot", self.kit.boot_to_main)
        songs = a.songs[:1] if a.short else a.songs
        for s in songs:
            self.leg(f"perform {s}", self.song, "perform", s, "medium",
                     ("perform_endgame_screen",))
        if not a.short:
            self.leg("back to main", self.back_to_main)
            self.leg(f"rehearse {a.rehearse}", self.rehearse, a.rehearse)
        self.close("quit and relaunch (mid-soak)")
        self.launch()
        self.leg("relaunch: profile back", self.check_profile_back)
        if a.short:
            self.close("end of short soak")
            return
        self.leg(f"battle {a.battle}", self.song, "dance_battle", a.battle, "medium",
                 ("perform_endgame_screen", "battle_endgame_screen", "dance_battle_endgame_screen"),
                 480, 2, footage(a.footage, a.battle, duo=True), None)
        self.leg("back to main", self.back_to_main)
        self.leg("story", self.story)
        self.close("quit and relaunch (story is not left by the menus here)")
        self.launch()
        self.leg("relaunch: profile back", self.check_profile_back)
        self.leg("fitness", self.fitness)
        self.close("end of soak")


def report(res, out):
    lines = [f"soak: {res['minutes']:.1f} min, {len(res['processes'])} game processes, "
             f"{len(res['legs'])} legs"]
    for l in res["legs"]:
        c = l.get("card") or {}
        lines.append(f"  {'ok ' if l.get('ok') else 'BAD'} {l['leg']:<28} {l['minutes']:5.1f} min"
                     + (f"  {l.get('results_screen')}: perfect {c.get('num_perfect')} awesome "
                        f"{c.get('num_awesome')} score {c.get('score')}" if c else "")
                     + (f"  p2 score {l['card_p2'].get('score')}"
                        if (l.get("card_p2") or {}).get("score") else "")
                     + (f"  ERROR {l['error']} (on {l.get('screen')})" if l.get("error") else ""))
    for p in res["processes"]:
        rss = p["rss"]
        lines.append(f"{p['label']}: closed ({p['why_closed']}), rc {p['rc']}, "
                     f"exit {(p['exit'] or {}).get('how', 'NONE (crash?)')}")
        if rss:
            lines.append(f"  RSS {rss[0][1] / 1024:.0f} MB -> {rss[-1][1] / 1024:.0f} MB, peak "
                         f"{max(r[1] for r in rss) / 1024:.0f} MB over {len(rss)} samples")
        lines.append(f"  hangs {len(p['hangs'])}, pauses in songs {len(p['pauses'])}"
                     + (f": {p['pauses']}" if p["pauses"] else ""))
        if p["signal"]:
            lines.append(f"  CRASH {p['signal']}")
        fails = {k: v for k, v in p["modals"].items() if k.startswith(("FAIL", "ASSERT"))}
        lines.append(f"  messages: {sum(p['modals'].values())} in {p['modal_kinds']} kinds; "
                     f"FAIL/ASSERT kinds {len(fails)}")
        for k, v in sorted(fails.items(), key=lambda kv: -kv[1])[:15]:
            lines.append(f"    {v:4d}x {k[:160]}")
        lines.append(f"  saves after: {p['saves']}")
    text = "\n".join(lines)
    with open(os.path.join(out, "soak.txt"), "w") as f:
        f.write(text + "\n\nevents:\n" + "\n".join(EVENTS) + "\n")
    return text


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--footage", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--gpu-lock")
    ap.add_argument("--short", action="store_true")
    ap.add_argument("--norender", action="store_true", help="no GPU (MILO_NORENDER=1)")
    ap.add_argument("--fresh", action="store_true", help="a console that has seen no tutorial")
    ap.add_argument("--pre", type=float, default=8.0)
    ap.add_argument("--songs", nargs="+", default=["macarena", "cupidshuffle", "iceicebaby"])
    ap.add_argument("--rehearse", default="getlow")
    ap.add_argument("--rehearse-min", type=float, default=6.0)
    ap.add_argument("--battle", default="macarena")
    a = ap.parse_args(argv)
    a.out = os.path.abspath(a.out)
    a.footage = os.path.abspath(a.footage)
    os.makedirs(a.out, exist_ok=True)
    s = Soak(a)
    try:
        s.run()
    except Exception as e:  # noqa: BLE001 -- record, then still write the report
        say(f"SOAK ABORTED: {type(e).__name__}: {e}")
        s.results["aborted"] = f"{type(e).__name__}: {e}"
        if s.kit and s.kit.alive():
            s.close("soak aborted")
    s.results["minutes"] = (time.time() - T0) / 60
    s.results["events"] = EVENTS
    with open(os.path.join(a.out, "soak.json"), "w") as f:
        json.dump(s.results, f, indent=1, default=str)
    print(report(s.results, a.out))
    bad = [l for l in s.results["legs"] if not l.get("ok")]
    crashed = [p for p in s.results["processes"] if not p["exit"]]
    return 1 if (bad or crashed or s.results.get("aborted")) else 0


if __name__ == "__main__":
    sys.exit(main())
