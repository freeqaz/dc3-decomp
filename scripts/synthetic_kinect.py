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

REPLAYING (k.replay(fixture) before k.perform(...); CLI --replay FIXTURE)
  Instead of the game's own target, person 0 performs a recorded DC3-20
  keypoint stream (scripts/pose_fixture_record.py: MediaPipe run offline over a
  video), keyed on SONG time: at song second t it shows the fixture frame at
  t mod the clip length. With DC3_FAST_TIME that is deterministic -- the same
  joints at the same song time on every run. --replay-still holds the
  fixture's first frame all song: the same body standing still, the negative
  control. Outside a song (no MoveDir) the person stands.

WATCHING ONLY (CLI --perform URL without --socket)
  No socket is served; the performing loop only reads /api/pose/target and
  records scores and ratings -- a score recorder for runs whose skeleton comes
  from elsewhere (the dummy, DC3_POSE_SELFTEST).

USAGE (as a library; scripts/native_assert_harvest.py --route party uses it)
  k = SyntheticKinect("/tmp/x.sock", people=2)
  k.start()                      # listens; the engine connects at boot
  k.set_pose(0, "raise_right")   # person 0 raises the right hand and holds it
  k.set_pose(0, "stand")
  k.set_pose(1, "absent")        # person 1 steps out of view (and "stand" back in)
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
MAGIC_V3 = 0x44503303       # v2 + a camera picture block (pose_server.py --frames)
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

# ---- menu gestures ----------------------------------------------------------
# DC3's shell lists are HamNavLists driven by one hand each (right_hand.hnl:
# the screen's list; the helpbar's left_hand.hnl: tertiary + back).  What the
# decompiled detectors need, read from src/:
#
#  ENGAGE  DirectionGestureFilterSingleUser::IsValidSwipePosition: the hand is
#          outside an ellipse round the spine in the HORIZONTAL plane -- in
#          the hip frame, lateral half-axis 1.3 shoulder widths (1.1 once the
#          list is engaged), depth half-axis 0.9 (0.8).  With these people's
#          0.40 m shoulders: the hand more than 0.52 m out to the side.
#  POINT   HandHeightGestureFilter: height = (shoulder.y - hand.y) /
#          (shoulder.y - hip.y) + 0.15, same side.  HamNavList::
#          DetermineHighlightedItem highlights round(height * (n - 1)) of an
#          n-item (non-scrolling) list, with hysteresis 0.1 + half an item.
#  SWIPE   ArcDetector (primary wrist, secondary elbow): the forearm vector
#          wrist - elbow sweeps round the elbow in the horizontal plane, from
#          pointing out to the side, through pointing at the camera, towards
#          the other side, at constant height.  Swipe amount = (path length^1.5
#          - error / 0.9) / threshold, threshold 0.1 m (sNavGestureThreshold)
#          falling to 0.03 after 600 ms of hovering (ArcDetector mHoverTimer);
#          >= 1 selects (DirectionGestureFilterSingleUser::Update), then a
#          0.5 s cooldown per skeleton.  A 0.24 m forearm swept a quarter turn
#          has path length 0.38 m -> amount 2.3 unhovered.
#  BACK    the same on the helpbar's left_hand.hnl with the left arm (2 items:
#          tertiary on top, back below; 'select_left_list_item 1' is back).
#
# Kinect camera space, as these people stand in it: +x is the person's right
# (the sensor's left), +y up, +z away from the sensor.
ARM_SHOULDER_Y = 1.40           # STAND shoulders
ARM_HIP_Y = 0.85                # STAND hips (HandHeightGestureFilter's span)
ARM_ELBOW_X = 0.42              # elbow out to the side
ARM_FOREARM = 0.24              # elbow -> wrist
ARM_HAND = 0.06                 # wrist -> hand
SWIPE_HOLD_S = 0.10             # end of a swipe held this long, then the arm drops


def hand_y_for_height(height):
    """The hand y that HandHeightGestureFilter reads as `height`."""
    return ARM_SHOULDER_Y - (height - 0.15) * (ARM_SHOULDER_Y - ARM_HIP_Y)


def height_for_item(index, count):
    """Hand height that highlights item `index` of a non-scrolling `count`-item
    list (HamNavList::DetermineHighlightedItem: round(height * (count - 1)))."""
    return 0.5 if count <= 1 else index / float(count - 1)


def menu_arm(side, height, angle_deg=0.0):
    """Joint overrides (x, y offsets from the person's spot) for an arm held
    out to `side` ('right'/'left') with the hand at menu `height`, the forearm
    rotated `angle_deg` round the elbow in the horizontal plane: 0 = pointing
    straight out to that side, 90 = at the camera, 180 = across the body.
    Returns {joint: (x, y, dz)} -- dz is added to the person's depth."""
    import math
    sgn = 1.0 if side == "right" else -1.0
    elbow, wrist, hand = (ELBOW_R, WRIST_R, HAND_R) if side == "right" else \
        (ELBOW_L, WRIST_L, HAND_L)
    y = hand_y_for_height(height)
    a = math.radians(angle_deg)
    ex = sgn * ARM_ELBOW_X
    # out to the side = +sgn x; at the camera = -z
    fx, fz = sgn * math.cos(a), -math.sin(a)
    return {elbow: (ex, y, 0.0),
            wrist: (ex + ARM_FOREARM * fx, y, ARM_FOREARM * fz),
            hand: (ex + (ARM_FOREARM + ARM_HAND) * fx, y, (ARM_FOREARM + ARM_HAND) * fz)}


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
        self.watch_only = False
        self.perf_stop = threading.Event()
        self.perf_stats = {}          # source -> packets sent with it
        self.perf_samples = []        # the engine's answer, every ~1 s
        self.perf_events = []         # every change of a player's score / rating / fatality
        self.perf_fatal_frames = []   # the first fatality answers, joints and all
        self.send_evt = threading.Event()
        # sensor clock (the packet timestamp): wall time, except that while
        # performing it advances by the engine's song time
        self.clock = time.monotonic()
        self.last_tick = time.monotonic()
        self.last_perf_packet = 0.0
        self.replay_frames = None     # [(joints|None)] at replay_fps, see replay()
        self.replay_fps = 30.0
        self.replay_still = False
        self.second_from = None       # --second-person-from SONG_S (perform loop)
        self.jump_at = None           # --jump-at SONG_S: person 0 re-acquired elsewhere
        self.jumped = False
        self.replay_shown = 0         # packets that carried a fixture frame
        # following the engine clock (follow_engine): engine frame -> seconds
        self.follow_thread = None
        self.follow_stop = threading.Event()
        self.engine_frame = None
        self.engine_t = None
        # (w, h): every packet is protocol v3 and carries a camera picture
        # rendered from the joints it sends (scripts/camera_fixture.py), the way
        # pose_server.py --frames sends the webcam's.  None: v2, no picture.
        self.frames = None
        self.camera_height = 0.9

    # ---- pose control -------------------------------------------------------
    def set_pose(self, person, pose):
        with self.lock:
            if self.poses[person] != pose:
                self.poses[person] = pose
                self.log.append((time.time(), f"person{person}={pose}"))

    def point(self, person, side, height):
        """Hold an arm out to `side` with the hand at menu `height` (0 = the
        top item's height, 1 = the bottom's): engages that side's HamNavList
        and hovers over an item.  See the menu-gesture notes at the top."""
        self.set_pose(person, ("point", side, float(height)))

    def swipe(self, person, side, height, duration=0.4, sweep=120.0):
        """From the pointing pose, sweep the forearm `sweep` degrees round the
        elbow towards the body over `duration` seconds, hold the end for
        SWIPE_HOLD_S, then drop the arm to the side (the "stand" arm).
        ArcDetector reads the sweep as a swipe: the highlighted item is
        selected.  Seconds are sensor seconds (now()).  Measured working:
        duration 0.3 s, sweep 110 degrees after a >= 0.6 s hover (the hover
        drops ArcDetector's threshold from 0.1 to 0.03), the engine paced so
        the sweep spans ~9 sensor frames (scripts/gesture_menu_flow.py).  A
        sweep that arrives as <= 2 path nodes caps at 0.5 and never selects."""
        with self.lock:
            self.poses[person] = ("swipe", side, float(height), self.now(),
                                  float(duration), float(sweep))
            self.log.append((time.time(), f"person{person}=swipe {side} {height:.2f}"))

    # ---- clock ----------------------------------------------------------------
    def now(self):
        """Sensor time: the engine's (follow_engine) or wall time."""
        t = self.engine_t
        return t if t is not None else time.monotonic()

    def follow_engine(self, http_base, frame_seconds=1.0 / 30.0):
        """Live in the engine's clock instead of the wall clock.  A headless
        engine (MILO_HEADLESS) advances UI time by a fixed 1/30 s per frame
        (UIManager::Poll, TaskMgr::Poll) however fast it runs, so against a
        wall-clock sensor a GPU-less run sees every gesture thousands of times
        slower than it was made -- and the game's UI-time windows (the 0.5 s
        swipe cooldown, DirectionGestureFilter::Update) expire mid-gesture.
        Here a thread long-polls GET /api/frame/wait/<n+1>; each new engine
        frame sets now() = frame * frame_seconds and sends one packet stamped
        with it, so the skeleton's elapsed ms, the gesture animations and the
        engine's UI clock are one time base.  The engine never waits for the
        sensor: it reads the newest packet, like a camera slower than the
        game loop -- so pace it (DC3_HTTP_PACE_FPS / PUT /api/pace, e.g. 120)
        whenever a gesture has to be seen frame by frame; unpaced, a GPU-less
        engine runs ~3000 fps and one packet covers several frames."""
        if self.follow_thread:
            return
        self.follow_stop.clear()
        self.follow_thread = threading.Thread(target=self._follow_loop,
                                              args=(http_base, frame_seconds), daemon=True)
        self.follow_thread.start()

    def _follow_loop(self, http_base, frame_seconds):
        u = urllib.parse.urlparse(http_base)
        root = u.path.rstrip("/")
        conn = None
        last = 0
        while not self.follow_stop.is_set():
            try:
                if conn is None:
                    conn = http.client.HTTPConnection(u.hostname, u.port, timeout=15)
                conn.request("GET", f"{root}/frame/wait/{last + 1}?timeout=5")
                r = conn.getresponse()
                body = r.read()
                if r.status != 200:
                    continue
                f = int(json.loads(body)["data"]["frame"])
            except Exception:
                if conn is not None:
                    conn.close()
                conn = None
                time.sleep(0.05)
                continue
            if f <= last:
                continue
            last = f
            with self.lock:
                self.engine_frame = f
                self.engine_t = f * frame_seconds
            self.send_evt.set()
        if conn is not None:
            conn.close()

    def wait(self, seconds):
        """Let `seconds` of sensor time pass (engine time when following)."""
        end = self.now() + seconds
        while self.now() < end and not self.stop_evt.is_set():
            time.sleep(0.001 if self.engine_t is not None else 0.01)

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
    def perform(self, http_base, lead_ms=0.0, watch_only=False):
        """Start imitating the game's targets (see module doc).  `http_base`
        is the debug server's /api root, e.g. http://127.0.0.1:9191/api.
        watch_only: the NEGATIVE CONTROL -- read and record the targets and
        scores exactly as when performing, but keep everyone standing still."""
        if self.perf_thread:
            return
        self.watch_only = watch_only
        self.perf_stop.clear()
        self.perf_thread = threading.Thread(target=self._perform_loop,
                                            args=(http_base, lead_ms), daemon=True)
        self.perf_thread.start()
        self.log.append((time.time(), "perform"))

    def replay(self, path, still=False):
        """Person 0 performs the fixture at `path` keyed on song time (see
        module doc). Call before perform()."""
        self.replay_frames, self.replay_fps = load_fixture(path)
        self.replay_still = still

    def _replay_at(self, song):
        frames = self.replay_frames
        if self.replay_still:
            idx = 0
        else:
            idx = int(song * self.replay_fps) % len(frames)
        # an untracked fixture frame holds the last tracked one before it
        for k in range(idx, idx - len(frames), -1):
            if frames[k % len(frames)] is not None:
                return frames[k % len(frames)]
        return None

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
            if self.second_from is not None and len(self.poses) > 1:
                # --second-person-from: person 1 walks in mid-song
                arrived = bool(d.get("moveDir")) and song is not None and song >= self.second_from
                with self.lock:
                    self.poses[1] = "stand" if arrived else "absent"
            if self.jump_at is not None and not self.jumped and d.get("moveDir") \
                    and song is not None and song >= self.jump_at:
                # --jump-at: what a camera cut (or a detector that lost her and
                # found her again) reports -- the same dancer, 0.6 m along x,
                # under a new tracking id
                with self.lock:
                    self.jumped = True
                    self.offsets[0] += 0.6
            targets = [None] * len(self.targets)
            sources = []
            if self.replay_frames is not None and d.get("moveDir") and song is not None \
                    and song > 0.0:
                joints = self._replay_at(song)
                if joints is not None:
                    targets[0] = [(x + self.offsets[0], y, z, c) for x, y, z, c in joints]
                    self.replay_shown += 1
            for pl in d.get("players", []):
                src = pl.get("source", "none")
                sources.append(src)
                if src == "none" or "joints" not in pl:
                    continue
                i = self._person_for(pl["player"], pl.get("trackingId"))
                if i is None or targets[i] is not None or self.watch_only \
                        or self.replay_frames is not None:
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
                key = (pl.get("score"), pl.get("rating"), pl.get("inFatality"),
                       pl.get("fatalPose"),
                       None if pl.get("fatalMatch") is None else round(pl["fatalMatch"], 1))
                prev = self._last_state.get(pl.get("player"))
                if prev is not None and key != prev:
                    self.perf_events.append({"song": song, "beat": d.get("beat"),
                                             "player": pl.get("player"), "move": pl.get("move"),
                                             "score": key[0], "rating": key[1],
                                             "inFatality": key[2], "source": pl.get("source"),
                                             **{k: pl[k] for k in pl if k.startswith("fatal")}})
                self._last_state[pl.get("player")] = key
            for pl in d.get("players", []):
                if pl.get("source") == "fatality" and len(self.perf_fatal_frames) < 20:
                    self.perf_fatal_frames.append({"song": song, **pl})
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
            return [(p[0], p[1], p[2], p[3] if len(p) > 3 else 1.0) for p in target]
        ox = self.offsets[i]
        pts = dict(STAND)
        dz = {}
        if isinstance(pose, tuple) and pose[0] in ("point", "swipe"):
            angle = 0.0
            if pose[0] == "swipe":
                _, side, height, t0, dur, sweep = pose
                el = self.now() - t0
                frac = min(1.0, max(0.0, el / dur)) if dur > 0 else 1.0
                angle = sweep * frac
                # then the arm drops back to the side, as a person's does
                if el > dur + SWIPE_HOLD_S:
                    side = None
            else:
                _, side, height = pose
            if side is not None:
                for j, (x, y, z) in menu_arm(side, height, angle).items():
                    pts[j] = (x, y)
                    dz[j] = z
        elif pose == "raise_right":
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
            out.append((x + ox, y, self.depth + dz.get(j, 0.0), 1.0))
        return out

    def _packet(self, frame_id):
        with self.lock:
            poses = list(self.poses)
            if self.engine_t is not None:
                # following the engine: its frame clock is the sensor clock
                self.clock = self.engine_t
            elif not self.perf_thread or time.monotonic() - self.last_perf_packet > 0.25:
                # not performing (or the engine is not answering, e.g. mid-load):
                # the sensor clock runs on wall time
                now = time.monotonic()
                self.clock += now - self.last_tick
                self.last_tick = now
            stamp = self.clock
        # "absent": the person has stepped out of view -- not in the frame at
        # all, as a Kinect reports a person it no longer sees.
        present = [i for i, pose in enumerate(poses) if pose != "absent"]
        body = struct.pack("<IIIdHHBBH", MAGIC_V3 if self.frames else MAGIC, frame_id,
                           len(present), stamp, 640, 480, NUM_JOINTS, LAYOUT_DC3_20, 0)
        persons, heights = [], []
        for i in present:
            pose = poses[i]
            tid = 101 if i == 0 and self.jumped else 5 + 4 * i  # Kinect tracking ids are > 0
            body += struct.pack("<i", tid)
            with self.lock:
                performing = i < len(self.targets) and self.targets[i] is not None
            joints = self._joints(i, pose)
            persons.append(joints)
            # the engine's targets are sensor-relative (hips ~ y 0); the
            # built-in poses stand on y = 0 (camera_fixture.py)
            heights.append(0.0 if performing else self.camera_height)
            for x, y, z, c in joints:
                body += struct.pack("<ffff", x, y, z, c)
        if self.frames:
            body += self._camera_block(persons, heights)
        return struct.pack("<I", len(body)) + body

    def _camera_block(self, persons, heights):
        """The v3 picture block: mask ordinal k = the k-th person above."""
        import camera_fixture     # numpy; only needed with frames on
        w, h = self.frames
        rgb, mask, w, h = camera_fixture.camera_block(persons, w, h, heights)
        return struct.pack("<HHBBH", w, h, 1, 1, 0) + rgb + mask

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
                if self.follow_thread:
                    # one packet per new engine frame; a stalled engine (a
                    # long load) gets none, as a camera's frames would be
                    # dropped, not queued
                    if got:
                        break
                    continue
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
        self.follow_stop.set()
        if self.follow_thread:
            self.follow_thread.join(6)
        self.stop_evt.set()
        self.send_evt.set()
        server = getattr(self, "server", None)   # absent when only recording
        if server is not None:
            try:
                server.close()
            except OSError:
                pass
        if self.thread:
            self.thread.join(2)
        if self.path and server is not None and os.path.exists(self.path):
            os.unlink(self.path)

    @staticmethod
    def engine_env(path):
        """Environment that points dc3-native's external pose provider here."""
        return {"DC3_POSE": "external", "DC3_POSE_NO_SPAWN": "1", "DC3_POSE_SOCKET": path}


def load_fixture(path):
    """(frames, fps) from a scripts/pose_fixture_record.py file: frames[k] is
    20 (x, y, z, conf) tuples in camera-space metres, or None if untracked."""
    fps = 30.0
    frames = []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                for tok in line[1:].split():
                    if tok.startswith("fps="):
                        fps = float(tok[4:])
                continue
            parts = line.split()
            if not parts:
                continue
            if parts[1] == "0":
                frames.append(None)
                continue
            v = [float(x) for x in parts[2:]]
            if len(v) != NUM_JOINTS * 4:
                raise ValueError(f"{path}: frame {len(frames)} has {len(v)} values")
            frames.append([tuple(v[4 * j:4 * j + 4]) for j in range(NUM_JOINTS)])
    if not any(f is not None for f in frames):
        raise ValueError(f"{path}: no tracked frames")
    return frames, fps


def main():
    """Stand-alone sensor: serve the socket until SIGTERM/SIGINT; with
    --perform, dance what the engine at --http asks.  On exit the performing
    record (sources, per-second samples, every score/rating change) is written
    to --record as JSON."""
    import argparse
    import signal
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--socket", default=None,
                    help="unix socket path (DC3_POSE_SOCKET); omit with --perform to only "
                         "record scores")
    ap.add_argument("--people", type=int, default=1)
    ap.add_argument("--perform", metavar="HTTP_API",
                    help="debug-server /api root to read targets from, "
                         "e.g. http://127.0.0.1:9191/api")
    ap.add_argument("--perform-port-file", metavar="PATH",
                    help="like --perform, for an engine on DC3_HTTP_PORT=0: wait for the "
                         "port it writes to DC3_HTTP_PORT_FILE=PATH, then perform against "
                         "http://127.0.0.1:<port>/api (scripts/engine_http_port.py)")
    ap.add_argument("--watch-only", action="store_true",
                    help="negative control: record targets/scores but stand still")
    ap.add_argument("--lead-ms", type=float, default=0.0)
    ap.add_argument("--record", default=None, help="write the performing record here on exit")
    ap.add_argument("--replay", default=None, metavar="FIXTURE",
                    help="person 0 performs this keypoint fixture keyed on song time")
    ap.add_argument("--replay-still", action="store_true",
                    help="with --replay: hold the fixture's first frame all song")
    ap.add_argument("--offset", type=float, default=None, metavar="METRES",
                    help="with --people 1: stand this far along camera x (+ = the "
                         "player's right) instead of on the axis")
    ap.add_argument("--second-person-from", type=float, default=None, metavar="SONG_S",
                    help="with --people 2 and --perform*: person 1 is out of view until "
                         "song second SONG_S, then stands beside person 0 (a second "
                         "person walking in mid-song who never raises a hand to join)")
    ap.add_argument("--jump-at", type=float, default=None, metavar="SONG_S",
                    help="with --perform*: at song second SONG_S person 0 reappears 0.6 m "
                         "along x under a new tracking id (a camera cut, or a detector "
                         "re-acquiring her as someone new)")
    ap.add_argument("--frames", default=None, metavar="WxH",
                    help="send protocol v3 with a WxH camera picture + person mask "
                         "rendered from the joints (needs numpy)")
    a = ap.parse_args()
    if a.perform and a.perform_port_file:
        ap.error("--perform and --perform-port-file are exclusive")
    if not a.socket and not (a.perform or a.perform_port_file):
        ap.error("--socket is required unless --perform only records")
    k = SyntheticKinect(a.socket, people=a.people)
    if a.frames:
        fw, fh = (int(v) for v in a.frames.lower().split("x"))
        k.frames = (fw, fh)
    if a.jump_at is not None:
        if not (a.perform or a.perform_port_file):
            ap.error("--jump-at needs --perform/--perform-port-file")
        k.jump_at = a.jump_at
    if a.second_person_from is not None:
        if a.people != 2 or not (a.perform or a.perform_port_file):
            ap.error("--second-person-from needs --people 2 and --perform/--perform-port-file")
        k.second_from = a.second_person_from
        k.poses[1] = "absent"
    if a.offset is not None:
        if a.people != 1:
            ap.error("--offset needs --people 1")
        k.offsets = [a.offset]
    if a.replay:
        k.replay(a.replay, still=a.replay_still)
    if a.socket:
        k.start()
    done = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: done.set())
    if a.perform_port_file:
        # The engine boots after this socket is up, so its port is not known
        # yet.  Wait for it (until stopped; the launcher bounds the run).
        from engine_http_port import read_port
        port = None
        while not done.is_set() and not port:
            port = read_port(a.perform_port_file)
            if not port:
                done.wait(0.1)
        if port:
            a.perform = f"http://127.0.0.1:{port}/api"
            print(f"synthetic_kinect: performing against {a.perform}", flush=True)
    if a.perform:
        k.perform(a.perform, lead_ms=a.lead_ms, watch_only=a.watch_only)
    while not done.is_set():
        done.wait(1.0)
    k.stop()
    if a.record:
        with open(a.record, "w") as f:
            json.dump({"frames_sent": k.frames_sent, "replay_frames_shown": k.replay_shown,
                       "perform_sources": k.perf_stats,
                       "perform_samples": k.perf_samples, "perform_events": k.perf_events,
                       "perform_fatal_frames": k.perf_fatal_frames},
                      f, indent=1)


if __name__ == "__main__":
    main()
