#!/usr/bin/env python3
"""synthetic_kinect.py -- a scripted stand-in for the Kinect SENSOR, for native runs.

WHY
  Several of DC3's flows are gated on skeleton input that has no controller
  equivalent in the image: party mode (Crew Throwdown) readies each side of
  crew select only while a skeleton stands on that side
  (multiuser.dta update_crew_throwdown_waiting_text reads the side's
  `player_present`, which SkeletonChooser::SetPlayerSkeletonNavData derives
  from the tracked skeletons), enrolls team members by a raised hand
  (party_mode_signin.dta poll_frames_claimed -> skeleton_chooser
  check_hand_raised -> HandRaisedGestureFilter), and starts every event on a
  high five (SkeletonChooser::Poll -> HighFiveGestureFilter -> hamprovider
  high_five -> party_mode_hub_panel on_high_five).  Headless native feeds ONE
  static standing dummy skeleton (GestureMgr_NativePoll), so only one side can
  ever be present and nobody ever raises a hand.

  This module does NOT shortcut any of that game logic.  It plays the part of
  the camera: it serves skeleton frames over the native build's existing
  external pose-provider socket (DC3_POSE=external DC3_POSE_NO_SPAWN=1
  DC3_POSE_SOCKET=..., the same wire protocol as native/scripts/pose_server.py,
  v2 / layout DC3_20: DC3's own 20 joints in camera-space metres), and every
  decision -- who is present, whose hand is up, whether two hands met -- is
  taken by the decompiled gesture code exactly as it is on the 360.

  Menu poses are static and hand-authored from NativeSkeletonProvider::
  FillDummySkeleton's standing pose.

PERFORMING (k.perform(http_base))
  A human player imitates what is on screen.  perform() plays that human: a
  thread reads, once per game frame, what each player is being asked to do --
  GET /api/pose/target (native/src/platform/PoseTarget_Native.cpp): the
  choreography's reference skeleton for the player's scheduled move frames
  (perform, dance battle, practice), or the pose a fatality / Strike a Pose
  shows -- and streams it back as that player's person.  It is read-only on
  the engine side and supplies sensor data only: the FilterQueue error nodes,
  the async move detectors and PoseFatalities::UpdateMatchingPose all score a
  LIVE Skeleton (history, bone lengths, quality filter) exactly as they would a
  camera's.  Contrast DC3_POSE_SELFTEST, which swaps the reference in INSIDE
  the scorer and so never runs the live-skeleton half.

  Timing: the endpoint is answered on the main thread between frames, so each
  call steps with the game; the packet it produces is consumed at the start of
  the next frame.  Packets are stamped with a sensor clock that advances by
  SONG time while performing (GestureMgr_NativePoll takes a frame's elapsed ms
  from these stamps, as Xbox does from NUI_SKELETON_FRAME), so under
  DC3_FAST_TIME the replay moves at the choreography's own speed.

USAGE (as a library; scripts/native_assert_harvest.py --route party uses it)
  k = SyntheticKinect("/tmp/x.sock", people=2)
  k.start()                      # listens; the engine connects at boot
  k.set_pose(0, "raise_right")   # person 0 raises the right hand and holds it
  k.set_pose(0, "stand")
  k.high_five()                  # person 0 and 1 meet hands between them
  k.perform("http://127.0.0.1:9191/api")   # both people now dance what the game asks
  k.stop_performing()
  k.stop()
"""
import http.client
import json
import os
import socket
import struct
import threading
import time
import urllib.parse

MAGIC = 0x44503302          # Skeleton_Native.cpp kProtocolMagic
LAYOUT_DC3_20 = 1           # kLayoutDC3_20: camera-space metres
NUM_JOINTS = 20

# BaseSkeleton.h SkeletonJoint order.
(HIP_C, SPINE, SHOULDER_C, HEAD, SHOULDER_L, ELBOW_L, WRIST_L, HAND_L,
 SHOULDER_R, ELBOW_R, WRIST_R, HAND_R, HIP_L, KNEE_L, ANKLE_L,
 HIP_R, KNEE_R, ANKLE_R, FOOT_L, FOOT_R) = range(NUM_JOINTS)

# NativeSkeletonProvider::FillDummySkeleton's neutral standing pose (x, y),
# hands below the hips.  z is per person.
STAND = {
    HIP_C: (0.00, 0.90), SPINE: (0.00, 1.10), SHOULDER_C: (0.00, 1.40), HEAD: (0.00, 1.60),
    SHOULDER_L: (-0.20, 1.40), ELBOW_L: (-0.25, 1.15), WRIST_L: (-0.22, 0.90), HAND_L: (-0.22, 0.85),
    SHOULDER_R: (0.20, 1.40), ELBOW_R: (0.25, 1.15), WRIST_R: (0.22, 0.90), HAND_R: (0.22, 0.85),
    HIP_L: (-0.12, 0.85), KNEE_L: (-0.12, 0.45), ANKLE_L: (-0.12, 0.05),
    HIP_R: (0.12, 0.85), KNEE_R: (0.12, 0.45), ANKLE_R: (0.12, 0.05),
    FOOT_L: (-0.12, 0.00), FOOT_R: (0.12, 0.00),
}

# HandRaisedGestureFilter::Update: hand above shoulder + 0.1, above the elbow,
# not more than 0.3 outboard of the shoulder, on screen, standing still for
# mRequiredMs (500).
RAISE_RIGHT = {ELBOW_R: (0.28, 1.55), WRIST_R: (0.26, 1.80), HAND_R: (0.26, 1.88)}
RAISE_LEFT = {ELBOW_L: (-0.28, 1.55), WRIST_L: (-0.26, 1.80), HAND_L: (-0.26, 1.88)}


class SyntheticKinect:
    def __init__(self, path, people=2, fps=30.0, spacing=0.40, depth=2.5):
        self.path = path
        self.fps = fps
        self.depth = depth
        # person i stands at x = offsets[i]; two people stand either side of
        # the camera axis, `spacing` from it.
        self.offsets = [0.0] if people == 1 else [-spacing, spacing] if people == 2 else \
            [(-spacing * (people - 1) + 2 * spacing * i) for i in range(people)]
        self.poses = ["stand"] * people
        self.lock = threading.Lock()
        self.stop_evt = threading.Event()
        self.thread = None
        self.frames_sent = 0
        self.connected = False
        self.log = []   # (monotonic time, event)
        # performing: person index -> 20 camera-space (x, y, z) or None
        self.targets = [None] * people
        self.perf_thread = None
        self.perf_stop = threading.Event()
        self.perf_stats = {}          # source -> packets sent with it
        self.perf_samples = []        # the engine's answer, every ~1 s
        self.perf_events = []         # every change of a player's score / rating / fatality
        self.send_evt = threading.Event()
        # sensor clock (the packet timestamp): wall time, except that while
        # performing it advances by the engine's song time
        self.clock = time.monotonic()
        self.last_tick = time.monotonic()
        self.last_perf_packet = 0.0

    # ---- pose control -------------------------------------------------------
    def set_pose(self, person, pose):
        with self.lock:
            if self.poses[person] != pose:
                self.poses[person] = pose
                self.log.append((time.time(), f"person{person}={pose}"))

    def high_five(self):
        """Persons 0 and 1 raise their inner hands to meet between them
        (HighFiveGestureFilter::Update: a hand above its shoulder centre and
        the two hands within kCloseThreshold = 0.15 m)."""
        with self.lock:
            self.poses[0], self.poses[1] = "high_five", "high_five"
            self.log.append((time.time(), "high_five"))

    def all_stand(self):
        with self.lock:
            self.poses = ["stand"] * len(self.poses)
            self.log.append((time.time(), "all_stand"))

    # ---- performing -----------------------------------------------------------
    def perform(self, http_base, lead_ms=0.0):
        """Start imitating the game's targets (see module doc).  `http_base`
        is the debug server's /api root, e.g. http://127.0.0.1:9191/api."""
        if self.perf_thread:
            return
        self.perf_stop.clear()
        self.perf_thread = threading.Thread(target=self._perform_loop,
                                            args=(http_base, lead_ms), daemon=True)
        self.perf_thread.start()
        self.log.append((time.time(), "perform"))

    def stop_performing(self):
        self.perf_stop.set()
        if self.perf_thread:
            self.perf_thread.join(5)
        self.perf_thread = None
        with self.lock:
            self.targets = [None] * len(self.targets)
        self.log.append((time.time(), "stop_performing"))

    def _person_for(self, player, tracking_id):
        tid = tracking_id
        if tid is not None and tid >= 5 and (tid - 5) % 4 == 0 and (tid - 5) // 4 < len(self.targets):
            return (tid - 5) // 4
        return player if player < len(self.targets) else None

    def _perform_loop(self, http_base, lead_ms):
        u = urllib.parse.urlparse(http_base)
        path = u.path.rstrip("/") + f"/pose/target?lead_ms={lead_ms:g}"
        conn = None
        last_song = None
        last_sample = 0.0
        self._last_state = {}
        while not self.perf_stop.is_set():
            try:
                if conn is None:
                    conn = http.client.HTTPConnection(u.hostname, u.port, timeout=15)
                conn.request("GET", path)
                d = json.loads(conn.getresponse().read())["data"]
            except Exception:
                if conn is not None:
                    conn.close()
                conn = None
                time.sleep(0.2)
                continue
            song = d.get("songSeconds")
            targets = [None] * len(self.targets)
            sources = []
            for pl in d.get("players", []):
                src = pl.get("source", "none")
                sources.append(src)
                if src == "none" or "joints" not in pl:
                    continue
                i = self._person_for(pl["player"], pl.get("trackingId"))
                if i is None or targets[i] is not None:
                    continue
                targets[i] = self._place(i, pl["joints"], src)
            now = time.monotonic()
            with self.lock:
                if song is not None and last_song is not None and song > last_song \
                        and song - last_song < 0.25:
                    self.clock += song - last_song
                else:
                    self.clock += now - self.last_tick
                self.last_tick = now
                self.targets = targets
                for src in sources:
                    self.perf_stats[src] = self.perf_stats.get(src, 0) + 1
            last_song = song
            for pl in d.get("players", []):
                key = (pl.get("score"), pl.get("rating"), pl.get("inFatality"))
                prev = self._last_state.get(pl.get("player"))
                if prev is not None and key != prev:
                    self.perf_events.append({"song": song, "beat": d.get("beat"),
                                             "player": pl.get("player"), "move": pl.get("move"),
                                             "score": key[0], "rating": key[1],
                                             "inFatality": key[2], "source": pl.get("source")})
                self._last_state[pl.get("player")] = key
            if now - last_sample > 1.0:
                last_sample = now
                self.perf_samples.append({
                    "song": song, "beat": d.get("beat"),
                    "players": [{k: pl.get(k) for k in ("source", "move", "score", "rating",
                                                         "inFatality", "trackingId")}
                                for pl in d.get("players", [])]})
            self.last_perf_packet = now
            self.send_evt.set()
        if conn is not None:
            conn.close()

    def _place(self, i, joints, source):
        """The target skeleton, standing where person i stands.  The
        choreography is in the dancer's own camera space (hips near x = 0), so
        it is shifted sideways by the person's offset and keeps its own motion;
        a fatality pose comes from the character's position in the venue, so
        its hips are put on the person's spot."""
        ox = self.offsets[i]
        if source == "fatality":
            hx, _, hz = joints[HIP_C]
            return [(x - hx + ox, y, z - hz + self.depth) for x, y, z in joints]
        return [(x + ox, y, z) for x, y, z in joints]

    # ---- geometry -----------------------------------------------------------
    def _joints(self, i, pose):
        with self.lock:
            target = self.targets[i] if i < len(self.targets) else None
        if target is not None:
            return [(x, y, z, 1.0) for x, y, z in target]
        ox = self.offsets[i]
        pts = dict(STAND)
        if pose == "raise_right":
            pts.update(RAISE_RIGHT)
        elif pose == "raise_left":
            pts.update(RAISE_LEFT)
        elif pose == "high_five":
            # the hand nearer the other person, reaching to the midpoint
            # (x = 0) at head height; elbow below it, both inboard.
            if ox < 0:   # person on -x reaches +x with the right hand
                pts.update({ELBOW_R: (-ox - 0.12, 1.50), WRIST_R: (-ox - 0.03, 1.66),
                            HAND_R: (-ox - 0.02, 1.70)})
            else:
                pts.update({ELBOW_L: (-ox + 0.12, 1.50), WRIST_L: (-ox + 0.03, 1.66),
                            HAND_L: (-ox + 0.02, 1.70)})
        out = []
        for j in range(NUM_JOINTS):
            x, y = pts[j]
            out.append((x + ox, y, self.depth, 1.0))
        return out

    def _packet(self, frame_id):
        with self.lock:
            poses = list(self.poses)
            if not self.perf_thread or time.monotonic() - self.last_perf_packet > 0.25:
                # not performing (or the engine is not answering, e.g. mid-load):
                # the sensor clock runs on wall time
                now = time.monotonic()
                self.clock += now - self.last_tick
                self.last_tick = now
            stamp = self.clock
        body = struct.pack("<IIIdHHBBH", MAGIC, frame_id, len(poses), stamp,
                           640, 480, NUM_JOINTS, LAYOUT_DC3_20, 0)
        for i, pose in enumerate(poses):
            body += struct.pack("<i", 5 + 4 * i)      # Kinect tracking ids are > 0
            for x, y, z, c in self._joints(i, pose):
                body += struct.pack("<ffff", x, y, z, c)
        return struct.pack("<I", len(body)) + body

    # ---- server -------------------------------------------------------------
    def start(self):
        if os.path.exists(self.path):
            os.unlink(self.path)
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(self.path)
        self.server.listen(1)
        self.server.settimeout(0.5)
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        client = None
        while not self.stop_evt.is_set() and client is None:
            try:
                client, _ = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                return
        if client is None:
            return
        self.connected = True
        frame_id = 0
        period = 1.0 / self.fps
        while not self.stop_evt.is_set():
            try:
                client.sendall(self._packet(frame_id))
            except OSError:
                break
            frame_id += 1
            self.frames_sent = frame_id
            # performing: one packet per target the engine answered (so one per
            # game frame, never a repeat that would read as a frozen frame);
            # otherwise a camera-like fixed rate
            while not self.stop_evt.is_set():
                got = self.send_evt.wait(period)
                self.send_evt.clear()
                if got or not self.perf_thread \
                        or time.monotonic() - self.last_perf_packet > 0.25:
                    break
        try:
            client.close()
        except OSError:
            pass

    def stop(self):
        if self.perf_thread:
            self.stop_performing()
        self.stop_evt.set()
        self.send_evt.set()
        try:
            self.server.close()
        except OSError:
            pass
        if self.thread:
            self.thread.join(2)
        if os.path.exists(self.path):
            os.unlink(self.path)

    @staticmethod
    def engine_env(path):
        """Environment that points dc3-native's external pose provider here."""
        return {"DC3_POSE": "external", "DC3_POSE_NO_SPAWN": "1", "DC3_POSE_SOCKET": path}


def main():
    """Stand-alone sensor: serve the socket until SIGTERM/SIGINT; with
    --perform, dance what the engine at --http asks.  On exit the performing
    record (sources, per-second samples, every score/rating change) is written
    to --record as JSON."""
    import argparse
    import signal
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--socket", required=True, help="unix socket path (DC3_POSE_SOCKET)")
    ap.add_argument("--people", type=int, default=1)
    ap.add_argument("--perform", metavar="HTTP_API",
                    help="debug-server /api root to read targets from, "
                         "e.g. http://127.0.0.1:9191/api")
    ap.add_argument("--lead-ms", type=float, default=0.0)
    ap.add_argument("--record", default=None, help="write the performing record here on exit")
    a = ap.parse_args()
    k = SyntheticKinect(a.socket, people=a.people)
    k.start()
    if a.perform:
        k.perform(a.perform, lead_ms=a.lead_ms)
    done = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: done.set())
    while not done.is_set():
        done.wait(1.0)
    k.stop()
    if a.record:
        with open(a.record, "w") as f:
            json.dump({"frames_sent": k.frames_sent, "perform_sources": k.perf_stats,
                       "perform_samples": k.perf_samples, "perform_events": k.perf_events},
                      f, indent=1)


if __name__ == "__main__":
    main()
