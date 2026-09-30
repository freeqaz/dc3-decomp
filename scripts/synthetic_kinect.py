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

  Poses are static and hand-authored from NativeSkeletonProvider::
  FillDummySkeleton's standing pose; they are NOT dance input.  Scoring still
  sees two people standing still.

USAGE (as a library; scripts/native_assert_harvest.py --route party uses it)
  k = SyntheticKinect("/tmp/x.sock", people=2)
  k.start()                      # listens; the engine connects at boot
  k.set_pose(0, "raise_right")   # person 0 raises the right hand and holds it
  k.set_pose(0, "stand")
  k.high_five()                  # person 0 and 1 meet hands between them
  k.stop()
"""
import os
import socket
import struct
import threading
import time

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
        self.offsets = [-spacing, spacing][:people] if people <= 2 else \
            [(-spacing * (people - 1) + 2 * spacing * i) for i in range(people)]
        self.poses = ["stand"] * people
        self.lock = threading.Lock()
        self.stop_evt = threading.Event()
        self.thread = None
        self.frames_sent = 0
        self.connected = False
        self.log = []   # (monotonic time, event)

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

    # ---- geometry -----------------------------------------------------------
    def _joints(self, i, pose):
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
        body = struct.pack("<IIIdHHBBH", MAGIC, frame_id, len(poses), time.monotonic(),
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
            time.sleep(period)
        try:
            client.close()
        except OSError:
            pass

    def stop(self):
        self.stop_evt.set()
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
