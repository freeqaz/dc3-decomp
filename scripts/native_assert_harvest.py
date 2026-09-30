#!/usr/bin/env python3
"""native_assert_harvest.py -- run the native game over a fixed HTTP-driven route
and harvest every MILO_ASSERT / MILO_FAIL / MILO_WARN / MILO_NOTIFY it emits.

WHY
  On native, MILO_ASSERT and MILO_FAIL are NON-FATAL (Debug::Fail returns under
  HX_NATIVE), so a mis-decompiled function can fail an assert every frame while
  the game keeps running on corrupted state.  Each distinct site is a lead.

HOW
  * launches native/build/dc3-native headless with DC3_TAG_MODALS=1, which makes
    src/system/os/Debug.cpp re-emit every Warn/Notify/Fail as one tagged
    `MILO_<KIND>: <msg>` line and dump a raw backtrace for the first occurrence
    of each distinct message (MILO_BT_BASE / MILO_BT / MILO_BT_END);
  * drives the menus with a generated MILO_INPUT_SCRIPT (route_input.txt:
    title -> main -> choose_mode[--mode-downs] -> song_select[--song-downs] ->
    multiuser_screen[--multiuser: controller presses through its panes, as on
    the 360 -- there is no native auto-advance] -> --confirm-screens ->
    --post-screens).  Screens whose input must wait on game STATE (party
    mode's photo confirm, its standings and rematch screens) are pressed over
    /api/input/press instead, only while that screen is current -- the
    endpoint works since native-partyplay (it was dead before, see launch());
  * watches progress over the HTTP debug server using ONLY side-effect-free
    probes (screen, frame, telemetry, screenshot).  It deliberately never calls
    /api/dta/eval: a probe query that fails raises its own FAIL/NOTIFY and the
    harvest would count the harvester's own messages (measured 2026-09-30: four
    self-inflicted FAILs and an 'unhandled msg: focus_component' NOTIFY from
    hand-probing sessions);
  * attributes each message to a stage by log byte offset: every screen the UI
    entered (from the engine's own `DC3 UI: Screen 'X' Enter` lines, so a
    screen that auto-advanced faster than any poll still counts) plus the
    harvester's checkpoints (game_screen, gameover, post-song screens);
  * de-duplicates by SITE: an assert by file:line, anything else by its message
    with numbers/hex/quoted names normalised; the first-occurrence backtrace is
    symbolised with addr2line to name the emitting function.

DENOMINATOR
  The report always prints which stages were REACHED and which were not.  A run
  that never reached gameplay says so in its first lines; it never reports a
  bare "0 asserts".  Exit code: 0 all requested stages reached, 3 some stage not
  reached, 4 the engine died, 2 usage/setup error.

USAGE
  perform (default route):
    python3 scripts/native_assert_harvest.py --out /tmp/h-perform
  dance battle (both multiuser sides readied with the controller):
    python3 scripts/native_assert_harvest.py --out /tmp/h-battle --port 9195 --mode-downs 2 \
        --post-screens dancebattle_perform_endgame_screen,dancebattle_perform_complete_screen
  practice (--route practice = the flags below; practice_welcome_screen needs
  its confirm too, without it the route stalls there):
    python3 scripts/native_assert_harvest.py --out /tmp/h-practice --port 9195 \
        --mode-downs 1 --multiuser none --confirm-screens \
        seldiff_practice_screen,startgame_practice_screen,practice_welcome_screen \
        --post-screens practice_endgame_screen
  party mode / Crew Throwdown (crew select -> team photos -> hub -> every
  event -> standings -> rematch -> main_screen; see route_party):
    python3 scripts/native_assert_harvest.py --out /tmp/h-party --port 9196 --route party
  (--route perform|battle|practice|party are presets for the flags above.)
  re-analyse a finished run:  --out <dir> --analyse-only
  (GPU access: run outside the sandbox.  Needs a built native/build/dc3-native.)
"""
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from synthetic_kinect import SyntheticKinect  # noqa: E402
MENU_SCREENS = {"main_screen", "choose_mode_screen", "song_select_screen"}


class Harvest:
    def __init__(self, args):
        self.args = args
        self.base = f"http://127.0.0.1:{args.port}/api"
        self.out = os.path.abspath(args.out)
        os.makedirs(self.out, exist_ok=True)
        self.log_path = os.path.join(self.out, "engine.log")
        self.stages = []  # dicts: name, reached, frame, offset, note
        self.proc = None

    # ---- HTTP -----------------------------------------------------------------
    def get(self, path, timeout=10):
        with urllib.request.urlopen(self.base + path, timeout=timeout) as r:
            return r.read()

    def jget(self, path, timeout=10):
        try:
            return json.loads(self.get(path, timeout))
        except Exception:
            return None

    def post(self, path, body, timeout=10):
        req = urllib.request.Request(self.base + path, data=body.encode(), method="POST")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()

    def screen(self):
        d = self.jget("/screen")
        return d["data"]["screen"] if d and d.get("ok") else None

    def frame(self):
        d = self.jget("/frame")
        return d["data"]["frame"] if d and d.get("ok") else -1

    def telemetry(self):
        d = self.jget("/telemetry")
        return d["data"] if d and d.get("ok") else {}

    def press(self, button):
        """One pad-0 press over HTTP.  Dispatched by the engine's
        HttpServer::DispatchInjectedButtons (dead before native-partyplay: the
        queued bits were never drained)."""
        try:
            self.post("/input/press", json.dumps({"button": button}))
            return True
        except Exception:
            return False

    def wait_frames(self, n):
        target = self.frame() + n
        try:
            self.get(f"/frame/wait/{target}", timeout=70)
        except Exception:
            pass

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def wait_screen(self, name, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline and self.alive():
            left = max(1, min(55, int(deadline - time.time())))
            try:
                d = json.loads(self.get(f"/screen/wait/{name}?timeout={left}", timeout=left + 10))
                if d.get("ok"):
                    return True
            except Exception:
                time.sleep(1)
        return False

    def screenshot(self, tag):
        try:
            png = self.get("/screenshot", timeout=30)
            p = os.path.join(self.out, f"{len(self.stages):02d}_{tag}.png")
            with open(p, "wb") as f:
                f.write(png)
            return p
        except Exception:
            return None

    # ---- stage bookkeeping ------------------------------------------------------
    def mark(self, name, reached, note=""):
        off = os.path.getsize(self.log_path) if os.path.exists(self.log_path) else 0
        st = {"name": name, "reached": bool(reached), "frame": self.frame() if self.alive() else -1,
              "offset": off, "note": note, "t": round(time.time() - self.t0, 1)}
        if reached and self.alive():
            st["screenshot"] = self.screenshot(name)
        self.stages.append(st)
        print(f"[harvest] stage {name}: {'REACHED' if reached else 'NOT REACHED'} "
              f"frame={st['frame']} {note}", flush=True)
        return reached

    # ---- launch ----------------------------------------------------------------
    def launch(self):
        build_dir = os.path.join(REPO, "native", "build")
        # --binary runs a saved executable (an A/B pair, a diagnostic build)
        # from the same working directory the tree's own build would use.
        binary = os.path.abspath(self.args.binary) if self.args.binary else \
            os.path.join(build_dir, "dc3-native")
        if not os.access(binary, os.X_OK):
            print(f"error: {binary} not built", file=sys.stderr)
            sys.exit(2)
        env = dict(os.environ)
        env.update({
            "DC3_HTTP": "1", "DC3_HTTP_PORT": str(self.args.port), "DC3_FAST_BOOT": "1",
            "DC3_TEL": "1", "DC3_SHOW_SPLASH": "0", "MILO_MAX_FRAMES": "0",
            "MILO_HEADLESS": "1", "DC3_FAST_TIME": "1", "MILO_RENDER": "1",
            "DC3_TAG_MODALS": "1",
            "DC3_DATA": env.get("DC3_DATA", os.path.join(REPO, "orig-assets")),
        })
        # Menus are driven by the in-engine input-script runner, not by HTTP
        # presses.  Two measured reasons (2026-09-30):
        #  * the title screen must be CONFIRMED, not skipped: title_panel's
        #    NAV_SELECT_MSG is what sets $post_load_dest_screen.  DC3_FAST_BOOT's
        #    native boot-advance (UIManager::Poll, HX_NATIVE) jumps title_screen ->
        #    wait_main_after_saveload_screen after 60 frames without it, and an
        #    HTTP round trip cannot reliably land inside that window;
        #  * /api/input/press WAS dead in dc3-native (fixed on native-partyplay:
        #    HttpServer::DispatchInjectedButtons now drains it; the frame-exact
        #    menu presses stay in the script): it answered {"ok":true} but
        #    the press never reaches a pad.  Since the shared-engine extraction
        #    the Joypad_Native.cpp that links is milo-native-engine's, compiled
        #    in libmilo-engine.a WITHOUT DC3_HTTP_SERVER, so its
        #    `newButtons |= TheHttpServer->ConsumeHttpButtons()` is compiled
        #    out (nm: ConsumeHttpButtons is defined, never called).  Measured
        #    2026-09-30: zero effect on main_screen and results screens,
        #    immediate or delayed.
        # The in-engine input-script runner is the only working input path.
        a = self.args
        self.kinect = None
        if a.route == "party":
            # Party mode is gated on skeleton input the controller cannot give
            # (see synthetic_kinect.py): two people stand in front of a
            # scripted stand-in for the sensor for the whole run.
            sock = os.path.join(self.out, "kinect.sock")
            self.kinect = SyntheticKinect(sock, people=2)
            self.kinect.start()
            env.update(SyntheticKinect.engine_env(sock))
        # A HamNavList ignores every button while its enter animation runs
        # (~20 UI frames; the image's HamNavList::OnMsg(ButtonDownMsg) checks
        # IsAnimating(), and native now does too), so the first press on each
        # nav-list screen waits NAV_SETTLE frames.  On title_screen it must
        # also land before the 60-frame fast-boot skip.
        NAV_SETTLE = 30
        lines = ["wait_screen title_screen", f"+{NAV_SETTLE} confirm",
                 "wait_screen main_screen", f"+{NAV_SETTLE} confirm",
                 "wait_screen choose_mode_screen"]
        t = NAV_SETTLE
        for _ in range(a.mode_downs):
            lines.append(f"+{t} down")
            t += 15
        lines.append(f"+{t} confirm")
        if a.route == "party":
            # choose_mode `crew_showdown` -> party_mode_branch_screen, whose
            # first item is `crew_showdown_start` -> party_mode_welcome_screen,
            # which moves on by itself to crew_throwdown_multiuser_screen.
            # Crew select is the multiuser panel with a crew_select_pane per
            # side: side 0 picks its highlighted crew, DLeft moves focus to
            # right_hand_p2 (MultiUserGesturePanel::OnMsg(ButtonDownMsg)),
            # side 1 picks.  Everything after that is gated on the synthetic
            # Kinect and on state-gated HTTP presses (route_party).
            lines += ["wait_screen party_mode_branch_screen", f"+{NAV_SETTLE} confirm",
                      "wait_screen crew_throwdown_multiuser_screen",
                      "+60 confirm", "+130 left", "+200 confirm"]
            a.confirm_screens = a.post_screens = ""
            lines_done = True
        else:
            lines_done = False
        if not lines_done:
            lines.append("wait_screen song_select_screen")
            t = 30
            for _ in range(a.song_downs):
                lines.append(f"+{t} down")
                t += 15
            lines.append(f"+{t} confirm")
        # multiuser_screen is driven by controller input as on the 360 (native
        # is pinned in controller mode).  Its two nav lists (right_hand_p1 /
        # right_hand_p2) each walk seldiff_pane -> startgame_pane; `play` sets
        # the side ready, and in controller mode can_enter_game needs BOTH
        # sides ready unless the side's readywait_pane offers skip_waiting
        # (modes that do not require 2 players).  Every pane change replays
        # the list's enter animation, during which presses are dropped, so
        # presses are MULTIUSER_GAP frames apart.  Offsets are relative to the
        # wait_screen, so they must increase.
        mu = "none" if a.route == "party" else a.multiuser
        if mu == "auto":
            mu = {0: "solo", 2: "duo"}.get(a.mode_downs, "none")
        if mu != "none":
            MULTIUSER_GAP = 70
            presses = ["confirm", "confirm"]            # side 0: difficulty, play
            if mu == "solo":
                presses += ["confirm"]                  # readywait: skip_waiting
            else:
                # DLeft moves focus right_hand_p1 -> right_hand_p2
                # (MultiUserGesturePanel::OnMsg(ButtonDownMsg)); side 1:
                # difficulty, play -> both ready -> start_game.
                presses += ["left", "confirm", "confirm"]
            lines.append("wait_screen multiuser_screen")
            t = 40
            for b in presses:
                lines.append(f"+{t} {b}")
                t += MULTIUSER_GAP
        # Mode-specific screens between song select and gameplay (practice's
        # seldiff_practice_screen, ...): confirm each, in order.
        for scr in [x for x in a.confirm_screens.split(",") if x]:
            lines += [f"wait_screen {scr}", "+30 confirm"]
        # Post-song screens.  The runner abandons a wait_screen after 1800
        # frames, and a song is ~20k, so the first post-song wait is repeated
        # to span gameplay: every copy after the satisfied one is satisfied on
        # the same frame, so the extra copies cost nothing.
        for i, scr in enumerate([x for x in a.post_screens.split(",") if x]):
            lines += [f"wait_screen {scr}"] * (30 if i == 0 else 1)
            lines.append("+30 confirm")
        boot_script = os.path.join(self.out, "route_input.txt")
        with open(boot_script, "w") as f:
            f.write("\n".join(lines) + "\n")
        env["MILO_INPUT_SCRIPT"] = boot_script
        # Pin the exact executable this run uses: symbolising a backtrace
        # against a binary relinked mid-run names the wrong functions (seen
        # 2026-09-30: every site resolved to Debug::Modal / MakeString after a
        # rebuild during the run).  A hard link keeps the old inode alive when
        # the linker replaces the file; fall back to a copy across filesystems.
        pinned = os.path.join(self.out, "dc3-native.pinned")
        try:
            if os.path.exists(pinned):
                os.unlink(pinned)
            os.link(binary, pinned)
        except OSError:
            import shutil
            shutil.copy2(binary, pinned)
        self.binary = pinned
        self.logf = open(self.log_path, "wb")
        cmd = [binary]
        if a.gdb:
            # Diagnostic: run under gdb with a command file (e.g. one that
            # catches SIGSEGV, prints a backtrace and continues, so the
            # engine's own handlers -- the Draw() sigsetjmp recovery
            # included -- still run).  Its output lands in engine.log.
            cmd = ["gdb", "-q", "-batch", "-x", os.path.abspath(a.gdb), "--args", binary]
        self.proc = subprocess.Popen(cmd, cwd=build_dir, env=env,
                                     stdout=self.logf, stderr=subprocess.STDOUT)
        self.t0 = time.time()
        deadline = time.time() + 120
        while time.time() < deadline:
            if not self.alive():
                return self.mark("boot", False, "engine exited during boot")
            if self.jget("/health", timeout=2):
                return self.mark("boot", True)
            time.sleep(1)
        return self.mark("boot", False, "health endpoint never answered")

    def shutdown(self):
        if getattr(self, "kinect", None):
            self.kinect.stop()
        if self.alive():
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.logf.close()

    # ---- route -----------------------------------------------------------------
    def goto(self, screen, timeout, then=None, settle=45):
        ok = self.wait_screen(screen, timeout)
        self.mark(screen, ok, "" if ok else f"timed out after {timeout}s (current={self.screen()!r})")
        if ok and then:
            self.wait_frames(settle)
            *navs, final = then
            for b in navs:
                self.press(b)
                self.wait_frames(20)
            self.press_until_leaves(final, screen)
        return ok

    def press_until_leaves(self, button, screen, tries=12, gap=45):
        """A press during an enter animation is dropped by HamNavList, so retry
        (bounded) until the UI starts a transition away from `screen`."""
        for i in range(tries):
            self.press(button)
            self.wait_frames(gap)
            if self.screen() != screen:
                return i + 1
        return 0

    def route(self):
        a = self.args
        if a.route == "party":
            return self.route_party()
        # menus: driven by route_input.txt (see launch); a stage for every
        # screen entered is recovered from the engine log afterwards.
        if not self.goto("game_screen", 300):
            return
        # --gameplay-eval: (beat, DTA) pairs, each POSTed once when the song
        # beat first reaches `beat`.
        pending = []
        for spec in a.gameplay_eval:
            beat, _, expr = spec.partition(":")
            pending.append((float(beat), expr))
        pending.sort(key=lambda p: p[0])

        def run_evals(beat):
            while pending and beat is not None and beat >= pending[0][0]:
                at, expr = pending.pop(0)
                try:
                    r = self.post("/dta/eval", expr).decode("utf-8", "replace")
                except Exception as e:  # noqa: BLE001 -- report, keep harvesting
                    r = f"error: {e}"
                print(f"[harvest] gameplay-eval @beat {beat:.1f} (asked {at}) "
                      f"{expr!r} -> {r}", flush=True)
                self.mark(f"eval@{at:g}", True, r[:200])
        # gameplay: sample until the game reports gameover (idle PAST song end)
        # Wall-clock is the wrong bound: on a loaded box the frame rate (and so,
        # under DC3_FAST_TIME, song progress) varies ~10x.  Bound on PROGRESS
        # instead: give up only when the beat has not moved for --stall-timeout
        # seconds, or at the hard --gameplay-timeout cap.
        deadline = time.time() + a.gameplay_timeout
        mid_shot = False
        state = None
        last_beat, last_move = None, time.time()
        while time.time() < deadline and self.alive():
            tel = self.telemetry()
            state = tel.get("state")
            beat = tel.get("beat")
            run_evals(beat)
            if beat != last_beat:
                last_beat, last_move = beat, time.time()
            elif time.time() - last_move > a.stall_timeout and state != "gameover":
                break
            if not mid_shot and tel.get("beat", 0) > 60:
                self.screenshot("gameplay_mid")
                mid_shot = True
            if state == "gameover":
                break
            time.sleep(2)
        if not self.mark("gameover", state == "gameover",
                         f"state={state!r} beat={self.telemetry().get('beat')}"):
            return
        # post-song: walk whatever screens follow, pressing confirm, until a menu
        seen = []
        deadline = time.time() + a.post_timeout
        last = None
        while time.time() < deadline and self.alive():
            s = self.screen()
            if s and s != last:
                seen.append(s)
                self.mark(f"post:{s}", True)
                last = s
                if s in MENU_SCREENS:
                    break
            self.wait_frames(90)  # presses come from route_input.txt
        back = last in MENU_SCREENS
        self.mark("back_to_menu", back, f"post-song screens: {' -> '.join(seen) or '(none)'}")


    # ---- party mode (Crew Throwdown) ---------------------------------------------
    # The checkpoints a full party passes, in order.  Every one is reported
    # REACHED or NOT REACHED; a run that stops early says where.
    PARTY_CHECKPOINTS = [
        "crew_throwdown_multiuser_screen",   # crew select (controller)
        "party_mode_signin_screen",          # team 1 enrollment (raised hand + photo)
        "party_mode_crew_announcement_screen",
        "party_mode_signin#2",               # team 2 enrollment
        "party_mode_hub_screen",             # round 1 hub (high five to start)
        "game_screen",                       # the first event's gameplay
        "gameover",
        "party_mode_standings_screen",
        "party_mode_rematch_screen",         # after the last event's standings
        "back_to_menu",
    ]

    def entered_screens(self):
        """Screens entered since the last call, in order, from the engine's own
        `DC3 UI: Screen 'X' Enter` lines.  /api/screen is not usable as an
        edge detector here: it also reports a transition's TARGET, which can
        be a screen that is never entered (party mode redirects
        dancebattle_perform_endgame_screen to meta_loading_party_cleanup_screen),
        and it flips back, which read as a second game_screen."""
        out = []
        try:
            with open(self.log_path, "rb") as f:
                f.seek(getattr(self, "_follow_off", 0))
                data = f.read()
        except OSError:
            return out
        end = data.rfind(b"\n") + 1
        self._follow_off = getattr(self, "_follow_off", 0) + end
        for raw in data[:end].splitlines():
            m = SCREEN_ENTER_RE.match(raw.decode("utf-8", "replace"))
            if m:
                out.append(m.group(1))
        return out

    def route_party(self):
        """Drive Crew Throwdown from crew select to the end of the party.

        Menus up to crew select come from route_input.txt.  From there the
        route is a state machine on the current screen (read over HTTP):
          * two synthetic people stand in front of the sensor all run, so both
            sides of crew select are `player_present` -> ready;
          * party_mode_signin_screen (once per team): one person raises a hand
            and holds it -- HandRaisedGestureFilter claims a photo frame, the
            enrollment timer runs out, the photo is taken -- and pad confirms
            (HTTP, only while this screen is current) accept the photo;
          * party_mode_hub_screen: the two people high-five
            (HighFiveGestureFilter -> hamprovider high_five -> on_high_five);
          * game_screen: sampled until gameover, as the other routes;
          * party_mode_standings_screen after the last event shows a continue
            panel: pad confirm;  party_mode_rematch_screen: `crew_showdown_no_more`
            (two downs, confirm) -> main_screen.
        No game logic is shortcut: every transition is the image's own DTA /
        gesture code reacting to pad or skeleton input."""
        a = self.args
        k = self.kinect
        seen_order = []
        counts = {}
        reached = set()
        last = None
        entered_at = time.time()
        songs = 0
        stall_deadline = time.time() + a.party_stall_timeout
        hub_fives = 0
        next_press = 0.0
        deadline = time.time() + a.party_timeout

        def cp(name, note=""):
            if name not in reached:
                reached.add(name)
                self.mark(name, True, note)

        pending = []
        while time.time() < deadline and self.alive():
            pending += self.entered_screens()
            entered = pending.pop(0) if pending else None
            s = entered or last
            now = time.time()
            if entered:
                counts[s] = counts.get(s, 0) + 1
                seen_order.append(s)
                print(f"[harvest] party screen {s} (#{counts[s]})", flush=True)
                last, entered_at = s, now
                stall_deadline = now + a.party_stall_timeout
                hub_fives = 0
                next_press = now + 8.0
                k.all_stand()
                self.wait_frames(20)  # let the screen draw before its screenshot
                if s == "party_mode_signin_screen" and counts[s] == 2:
                    cp("party_mode_signin#2")
                elif s in self.PARTY_CHECKPOINTS and s not in reached:
                    cp(s)
                else:
                    self.mark(f"party:{s}#{counts[s]}", True)
                if s == "party_mode_signin_screen":
                    # team 1 = person 0, team 2 = person 1
                    k.set_pose((counts[s] - 1) % 2, "raise_right")
                if s == "main_screen" and "party_mode_rematch_screen" in reached:
                    cp("back_to_menu", f"screens: {' -> '.join(seen_order)}")
                    return
                if s == "game_screen":
                    songs += 1
                    self.party_gameplay(songs)
                    stall_deadline = time.time() + a.party_stall_timeout
                    if songs >= a.party_songs:
                        self.mark("party_songs_limit", True, f"stopped after {songs} song(s)")
                        break
                    continue
            if pending:
                continue  # catch up: act only on the screen that is current
            if s == "party_mode_signin_screen" and now >= next_press:
                # Accepting the team photo.  Before the photo exists the
                # continue panel is disabled and a confirm does nothing.
                self.press("confirm")
                next_press = now + 3.0
            elif s == "party_mode_hub_screen" and now - entered_at > 4.0 + hub_fives * 15.0 \
                    and hub_fives < 4:
                k.high_five()
                time.sleep(3.0)
                k.all_stand()
                hub_fives += 1
            elif s in ("party_mode_standings_screen", "party_mode_rematch_screen") \
                    and now >= next_press + 4.0:
                if s == "party_mode_rematch_screen":
                    # rematch / new showdown / no more
                    for b in ("down", "down", "confirm"):
                        self.press(b)
                        time.sleep(1.5)
                else:
                    self.press("confirm")
                next_press = time.time() + 4.0
            if s and now > stall_deadline:
                self.mark("party_stall", False,
                          f"no screen change for {a.party_stall_timeout}s on {s}")
                break
            # short: the round standings and crew announcement screens can be
            # gone in about a second
            time.sleep(0.25)
        for c in self.PARTY_CHECKPOINTS:
            if c not in reached:
                self.mark(c, False, f"last screen {last!r}; screens: {' -> '.join(seen_order)}")

    def party_gameplay(self, n):
        """One party event: sample until gameover (idle past song end)."""
        a = self.args
        last_beat, last_move = None, time.time()
        state = None
        shot = False
        deadline = time.time() + a.gameplay_timeout
        while time.time() < deadline and self.alive():
            tel = self.telemetry()
            state, beat = tel.get("state"), tel.get("beat")
            if tel.get("screen") not in (None, "", "game_screen") and state != "gameover":
                break  # left gameplay without a gameover
            if beat != last_beat:
                last_beat, last_move = beat, time.time()
            elif time.time() - last_move > a.stall_timeout and state != "gameover":
                break
            if not shot and (beat or 0) > 60:
                self.screenshot(f"party_event{n}_mid")
                shot = True
            if state == "gameover":
                break
            time.sleep(2)
        ok = state == "gameover"
        self.mark("gameover" if n == 1 else f"gameover#{n}", ok,
                  f"event {n}: state={state!r} beat={self.telemetry().get('beat')}")

# ---- log analysis ---------------------------------------------------------------
TAG_RE = re.compile(r"^MILO_(WARN|NOTIFY|FAIL): (.*)$")
ASSERT_RE = re.compile(r"File: (\S+) Line: (\d+) Error: (.*?)(?:\\n)*$")
CRASH_RE = re.compile(r"(AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:|"
                      r"DC3_EXIT: how=fatal_signal|Segmentation fault|SIGSEGV|SIGABRT)")
SKIP_FUNCS = ("NativeModalTap", "Debug::Fail", "Debug::Warn", "Debug::Notify",
              "DebugFailer::operator", "DebugWarner::operator", "DebugNotifier::operator",
              "DebugWarnOncer", "DebugNotifyOncer", "DataFail", "DataArray::Execute",
              "DataNode::Evaluate")


def normalise(msg):
    m = msg.replace("\\n", " ").strip()
    m = re.sub(r"0x[0-9a-fA-F]+", "<hex>", m)
    m = re.sub(r"'[^']*'", "'<s>'", m)
    m = re.sub(r"\"[^\"]*\"", "\"<s>\"", m)
    m = re.sub(r"-?\d+(\.\d+)?", "<n>", m)
    return m[:200]


def symbolise_all(binary, base, addr_lists):
    """ONE addr2line call for every return address of every site (per-site calls
    against a ~130 MB debug binary took minutes).  Returns, per input list, a
    list of (function, file:line)."""
    if base is None:
        return [[] for _ in addr_lists]
    uniq = sorted({a for lst in addr_lists for a in lst})
    if not uniq:
        return [[] for _ in addr_lists]
    offs = [hex(a - base - 1) for a in uniq]  # -1: return address -> call site
    try:
        out = subprocess.run(["addr2line", "-f", "-C", "-e", binary] + offs,
                             capture_output=True, text=True, timeout=600).stdout.splitlines()
    except Exception:
        return [[] for _ in addr_lists]
    table = {}
    for i, a in enumerate(uniq):
        if 2 * i + 1 >= len(out):
            break
        fn, loc = out[2 * i], out[2 * i + 1]
        loc = re.sub(r" \(discriminator \d+\)", "", loc)
        loc = loc.replace(REPO + "/native/../", "").replace(REPO + "/", "")
        table[a] = (fn, loc)
    return [[table.get(a, ("??", "??")) for a in lst] for lst in addr_lists]


SCREEN_ENTER_RE = re.compile(r"^DC3 UI: Screen '([^']+)' Enter")


def screen_stages(log_path):
    """Every screen the UI actually entered, with its log offset -- recovered
    from the engine's own `DC3 UI: Screen 'X' Enter` lines, so a screen that
    auto-advanced faster than any HTTP poll is still counted."""
    out = []
    with open(log_path, "rb") as f:
        off = 0
        for raw in f:
            m = SCREEN_ENTER_RE.match(raw.decode("utf-8", "replace"))
            if m:
                out.append({"name": f"screen:{m.group(1)}", "reached": True, "frame": -1,
                            "offset": off, "note": "from engine log", "t": ""})
            off += len(raw)
    return out


def analyse(h):
    stages = sorted(h.stages + screen_stages(h.log_path), key=lambda st: st["offset"])
    h.all_stages = stages
    sites = {}
    crashes = []
    base = None
    pending = None  # site awaiting its backtrace
    bt = []
    with open(h.log_path, "rb") as f:
        offset = 0
        for raw in f:
            line = raw.decode("utf-8", "replace").rstrip("\n")
            here = offset
            offset += len(raw)
            if line.startswith("MILO_BT_BASE: "):
                try:
                    base = int(line.split()[1], 16)
                except ValueError:
                    pass
                continue
            if line.startswith("MILO_BT: "):
                try:
                    bt.append(int(line.split()[1], 16))
                except ValueError:
                    pass
                continue
            if line == "MILO_BT_END":
                if pending is not None and not pending["bt"]:
                    pending["bt"] = bt
                bt, pending = [], None
                continue
            m = TAG_RE.match(line)
            if m:
                kind, msg = m.groups()
                am = ASSERT_RE.search(msg)
                if am:
                    path = am.group(1).replace(REPO + "/native/../", "").replace(REPO + "/", "")
                    key = f"ASSERT {path}:{am.group(2)}"
                    kind = "ASSERT"
                    text = am.group(3)
                else:
                    key = f"{kind} {normalise(msg)}"
                    text = msg
                stage = "pre-boot"
                for st in stages:
                    if st["offset"] <= here:
                        stage = st["name"]
                s = sites.setdefault(key, {"kind": kind, "example": text[:300], "count": 0,
                                           "stages": {}, "bt": []})
                s["count"] += 1
                s["stages"][stage] = s["stages"].get(stage, 0) + 1
                pending = s
                continue
            if CRASH_RE.search(line):
                crashes.append(line[:300])
    vals = list(sites.values())
    all_frames = symbolise_all(h.binary, base, [s["bt"] for s in vals])
    for s, frames in zip(vals, all_frames):
        s["frames"] = [f"{fn} ({loc})" for fn, loc in frames[:18]]
        site = next((f"{fn} ({loc})" for fn, loc in frames
                     if not any(k in fn for k in SKIP_FUNCS)), "?")
        s["site"] = site
        del s["bt"]
    return sites, crashes


def report(h, sites, crashes, rc_engine):
    stages = getattr(h, "all_stages", h.stages)
    reached = [s for s in stages if s["reached"]]
    missed = [s for s in h.stages if not s["reached"]]
    lines = []
    lines.append(f"# native assert harvest -- {time.strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append("")
    lines.append(f"stages REACHED {len(reached)} / listed {len(stages)} "
                 f"(harvester checkpoints {len(h.stages)}, of which NOT reached: {len(missed)})")
    for s in stages:
        lines.append(f"  [{'x' if s['reached'] else ' '}] {s['name']:<32} frame={s['frame']:<7} "
                     f"t={s['t']} {s['note']}")
    if missed:
        lines.append(f"!! NOT REACHED: {', '.join(s['name'] for s in missed)} -- messages below "
                     f"cover ONLY the stages marked [x].")
    lines.append(f"engine exit: {rc_engine}")
    total = sum(s["count"] for s in sites.values())
    lines.append(f"distinct sites: {len(sites)}   total messages: {total}   "
                 f"crash/sanitizer lines: {len(crashes)}")
    lines.append("")
    lines.append("| kind | count | stages | emitting function | message |")
    lines.append("|---|---|---|---|---|")
    for key, s in sorted(sites.items(), key=lambda kv: (-kv[1]["count"], kv[0])):
        st = ", ".join(f"{k}x{v}" for k, v in s["stages"].items())
        msg = s["example"].replace("|", "\\|").replace("\\n", " ")[:160]
        lines.append(f"| {s['kind']} | {s['count']} | {st} | {s['site']} | {msg} |")
    if crashes:
        lines.append("")
        lines.append("crash / sanitizer lines:")
        lines.extend("  " + c for c in crashes[:50])
    text = "\n".join(lines)
    with open(os.path.join(h.out, "report.md"), "w") as f:
        f.write(text + "\n")
    with open(os.path.join(h.out, "report.json"), "w") as f:
        json.dump({"stages": h.stages, "sites": sites, "crashes": crashes,
                   "engine_exit": rc_engine}, f, indent=1)
    print(text)
    return missed


ROUTE_PRESETS = {
    "perform": dict(mode_downs=0),
    "battle": dict(mode_downs=2, multiuser="duo",
                   post_screens="dancebattle_perform_endgame_screen,"
                                "dancebattle_perform_complete_screen"),
    # practice_welcome_screen needs its confirm too: without it the route
    # stalls there (measured on native-suspects' baseline binary).
    "practice": dict(mode_downs=1, multiuser="none",
                     confirm_screens="seldiff_practice_screen,startgame_practice_screen,"
                                     "practice_welcome_screen",
                     post_screens="practice_endgame_screen"),
    # choose_mode's 5th item is crew_showdown (Crew Throwdown)
    "party": dict(mode_downs=4, multiuser="none", confirm_screens="", post_screens=""),
}


def apply_route_preset(args):
    for k, v in ROUTE_PRESETS.get(args.route, {}).items():
        setattr(args, k, v)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--port", type=int, default=9191)
    ap.add_argument("--route", choices=["custom", "perform", "battle", "practice", "party"],
                    default="custom",
                    help="preset route (fills --mode-downs/--confirm-screens/--post-screens/"
                         "--multiuser); custom = use those flags as given")
    ap.add_argument("--party-songs", type=int, default=99,
                    help="party route: stop after this many events (default: the whole party)")
    ap.add_argument("--party-timeout", type=int, default=10800,
                    help="party route: hard cap on the whole party, seconds")
    ap.add_argument("--party-stall-timeout", type=int, default=240,
                    help="party route: give up when the screen has not changed for this long")
    ap.add_argument("--mode-downs", type=int, default=0, help="downs on choose_mode (0=perform)")
    ap.add_argument("--song-downs", type=int, default=5, help="downs on song select")
    ap.add_argument("--confirm-screens", default="",
                    help="comma list of screens after song select to confirm, in order "
                         "(e.g. seldiff_practice_screen for practice)")
    ap.add_argument("--post-screens",
                    default="perform_final_results_screen,perform_complete_screen",
                    help="comma list of post-song screens to confirm, in order")
    ap.add_argument("--multiuser", choices=["auto", "none", "solo", "duo"], default="auto",
                    help="how to drive multiuser_screen: solo = one side, then "
                         "skip_waiting; duo = ready both sides (modes requiring 2 "
                         "players); auto = solo for perform, duo for dance battle, "
                         "none otherwise (practice does not pass multiuser_screen)")
    ap.add_argument("--gameplay-eval", action="append", default=[],
                    metavar="BEAT:DTA",
                    help="POST DTA to /api/dta/eval once, when the song beat first "
                         "reaches BEAT (repeatable; e.g. "
                         "'0:{toggle_autoplay 0}{toggle_autoplay 1}').  Breaks the "
                         "no-eval rule on purpose: any message it raises is the "
                         "caller's, not the game's")
    ap.add_argument("--gameplay-timeout", type=int, default=3600,
                    help="hard cap on the gameplay stage, seconds")
    ap.add_argument("--stall-timeout", type=int, default=180,
                    help="give up if the song beat has not advanced for this long")
    ap.add_argument("--post-timeout", type=int, default=900)
    ap.add_argument("--binary", default=None,
                    help="dc3-native executable to run (default: native/build/dc3-native); "
                         "it is still launched from native/build")
    ap.add_argument("--gdb", metavar="CMDFILE", default=None,
                    help="run the engine under gdb with this command file (it must `run`); "
                         "its output lands in engine.log")
    ap.add_argument("--analyse-only", action="store_true",
                    help="re-analyse an existing --out dir (needs stages.json)")
    args = ap.parse_args()
    apply_route_preset(args)
    h = Harvest(args)
    h.binary = os.path.join(REPO, "native", "build", "dc3-native")
    if os.path.exists(os.path.join(h.out, "dc3-native.pinned")):
        h.binary = os.path.join(h.out, "dc3-native.pinned")
    if args.analyse_only:
        with open(os.path.join(h.out, "stages.json")) as f:
            h.stages = json.load(f)
        sites, crashes = analyse(h)
        missed = report(h, sites, crashes, "(analyse-only)")
        return 3 if missed else 0
    rc_engine = None
    try:
        if h.launch():
            h.route()
    finally:
        died = not h.alive()
        rc_engine = h.proc.returncode if died and h.proc else "alive at end (SIGTERM by harvester)"
        h.shutdown()
    with open(os.path.join(h.out, "stages.json"), "w") as f:
        json.dump(h.stages, f, indent=1)
    sites, crashes = analyse(h)
    missed = report(h, sites, crashes, rc_engine)
    if died:
        return 4
    return 3 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
