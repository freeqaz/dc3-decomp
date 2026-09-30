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
    --confirm-screens -> --post-screens).  NOT with /api/input/press, which is
    dead in dc3-native (see launch());
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
  practice:
    python3 scripts/native_assert_harvest.py --out /tmp/h-practice --port 9195 \
        --mode-downs 1 --confirm-screens seldiff_practice_screen,startgame_practice_screen \
        --post-screens practice_endgame_screen
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
        try:
            self.post("/input/press", json.dumps({"button": button}))
        except Exception:
            pass

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
        binary = os.path.join(REPO, "native", "build", "dc3-native")
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
        #    wait_main_after_saveload_screen after 10 frames without it, and an
        #    HTTP round trip cannot reliably beat a 10-frame window;
        #  * /api/input/press is DEAD in dc3-native: it answers {"ok":true} but
        #    the press never reaches a pad.  Since the shared-engine extraction
        #    the Joypad_Native.cpp that links is milo-native-engine's, compiled
        #    in libmilo-engine.a WITHOUT DC3_HTTP_SERVER, so its
        #    `newButtons |= TheHttpServer->ConsumeHttpButtons()` is compiled
        #    out (nm: ConsumeHttpButtons is defined, never called).  Measured
        #    2026-09-30: zero effect on main_screen and results screens,
        #    immediate or delayed.
        # The in-engine input-script runner is the only working input path.
        a = self.args
        lines = ["wait_screen title_screen", "+2 confirm",
                 "wait_screen main_screen", "+10 confirm",
                 "wait_screen choose_mode_screen"]
        t = 10
        for _ in range(a.mode_downs):
            lines.append(f"+{t} down")
            t += 15
        lines.append(f"+{t} confirm")
        lines.append("wait_screen song_select_screen")
        t = 30
        for _ in range(a.song_downs):
            lines.append(f"+{t} down")
            t += 15
        lines.append(f"+{t} confirm")
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
        self.proc = subprocess.Popen([binary], cwd=os.path.dirname(binary), env=env,
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
        # menus: driven by route_input.txt (see launch); a stage for every
        # screen entered is recovered from the engine log afterwards.
        if not self.goto("game_screen", 300):
            return
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--port", type=int, default=9191)
    ap.add_argument("--mode-downs", type=int, default=0, help="downs on choose_mode (0=perform)")
    ap.add_argument("--song-downs", type=int, default=5, help="downs on song select")
    ap.add_argument("--confirm-screens", default="",
                    help="comma list of screens after song select to confirm, in order "
                         "(e.g. seldiff_practice_screen for practice)")
    ap.add_argument("--post-screens",
                    default="perform_final_results_screen,perform_complete_screen",
                    help="comma list of post-song screens to confirm, in order")
    ap.add_argument("--gameplay-timeout", type=int, default=3600,
                    help="hard cap on the gameplay stage, seconds")
    ap.add_argument("--stall-timeout", type=int, default=180,
                    help="give up if the song beat has not advanced for this long")
    ap.add_argument("--post-timeout", type=int, default=900)
    ap.add_argument("--analyse-only", action="store_true",
                    help="re-analyse an existing --out dir (needs stages.json)")
    args = ap.parse_args()
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
