#!/usr/bin/env python3
"""MediaPipe BlazePose GHUM backend for the DC3 pose server.

WHY THIS EXISTS: the YOLO11n-pose backend produces COCO-17 keypoints with NO
depth, so the C++ side had to invent a constant z (3.0 m for every joint). DC3's
scoring is genuinely 3D -- DetectFrame::LimbPSNR dots a per-move Vector3 weight
against a Vector3 error -- and because the numerator is Dot(w,e)^2 rather than a
per-component sum, a wrong z can partially CANCEL an x error. Constant z does not
merely add a fixed penalty, it corrupts x/y grading.

BlazePose GHUM emits `pose_world_landmarks`: 33 landmarks in METRES with the
hip midpoint as origin, fitted against the GHUM statistical body model. That is
root-relative rather than camera-absolute, so we recover the absolute root by
pinhole projection (see AbsoluteRoot below).

AXIS CONVENTION -- measured, not assumed. Probed over 120 frames of real
footage, 100% sign agreement on every axis, plus every derived bone length
landing in a plausible adult range (torso 0.497 m, shoulder span 0.347 m,
forearm 0.229 m) which is what confirms the output really is metric:

    MediaPipe                          DC3 camera space         action
    anatomical-left shoulder at +x     player-left is -X        flip x
    head at -y relative to hips        up is +Y                 flip y
    nose (toward camera) at -z         away-from-camera is +Z   keep z

Flipping x and y while keeping z is a 180-degree rotation about z, determinant
+1, so handedness is PRESERVED -- a reflection here would mean a sign was misread.

This backend emits DC3's own 20-joint skeleton in camera-space metres (protocol
layout 1), NOT raw landmarks. Two reasons: the remap, axis flips and root
recovery are all pure geometry that is far easier to test in Python, and sending
metres directly bypasses the C++ NormalizedToMeters helper whose hardcoded
2.4 x 1.8 m view box assumes 4:3 and anisotropically distorts every angle on a
16:9 camera.
"""

import math

import numpy as np

# ---------------------------------------------------------------------------
# BlazePose 33-landmark indices
# ---------------------------------------------------------------------------
NOSE = 0
L_EAR, R_EAR = 7, 8
L_SHOULDER, R_SHOULDER = 11, 12
L_ELBOW, R_ELBOW = 13, 14
L_WRIST, R_WRIST = 15, 16
L_PINKY, R_PINKY = 17, 18
L_INDEX, R_INDEX = 19, 20
L_HIP, R_HIP = 23, 24
L_KNEE, R_KNEE = 25, 26
L_ANKLE, R_ANKLE = 27, 28
L_HEEL, R_HEEL = 29, 30
L_FOOT, R_FOOT = 31, 32

# ---------------------------------------------------------------------------
# DC3 joint indices (src/system/gesture/BaseSkeleton.h:29-49). Wire order.
# ---------------------------------------------------------------------------
DC3_JOINT_NAMES = [
    "HipCenter", "Spine", "ShoulderCenter", "Head",
    "ShoulderLeft", "ElbowLeft", "WristLeft", "HandLeft",
    "ShoulderRight", "ElbowRight", "WristRight", "HandRight",
    "HipLeft", "KneeLeft", "AnkleLeft",
    "HipRight", "KneeRight", "AnkleRight",
    "FootLeft", "FootRight",
]
NUM_DC3_JOINTS = 20
(HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD,
 SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT, HAND_LEFT,
 SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT, HAND_RIGHT,
 HIP_LEFT, KNEE_LEFT, ANKLE_LEFT,
 HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT,
 FOOT_LEFT, FOOT_RIGHT) = range(NUM_DC3_JOINTS)

# Kinect's kJointFoot sits only ~4.6 cm from the ankle and almost straight DOWN
# (measured from the baked capture in src/system/gesture/StubCameraInput.cpp:57-61:
# offset dx=-0.002 dy=-0.046 dz=-0.004). BlazePose's foot_index is the toe tip,
# ~10.6 cm out, so blend it toward the heel to land near the mid-foot instead.
FOOT_TOE_BLEND = 0.35


class CentroidTracker:
    """Assign persistent IDs across frames by nearest hip-centre match.

    Ultralytics handed us BOTSORT track IDs for free; MediaPipe Pose Landmarker
    returns up to N poses per frame with NO identity. The C++ AssignSlots relies
    on IDs being stable to keep a person in the same skeleton slot, so we supply
    them here. Matching on the hip centre in normalized image space is adequate
    for a living-room dance game (1-2 people, well separated).
    """

    def __init__(self, max_dist=0.25, max_missing=15):
        self.max_dist = max_dist
        self.max_missing = max_missing
        self._next_id = 1
        self._tracks = {}  # id -> [centroid(np.array 2), missing_frames]

    def update(self, centroids):
        """centroids: list of (x, y) in normalized image space. Returns list of ids."""
        assigned = [None] * len(centroids)
        used = set()

        # Greedy nearest-neighbour over all (track, detection) pairs, closest first.
        pairs = []
        for tid, (prev, _missing) in self._tracks.items():
            for di, c in enumerate(centroids):
                pairs.append((float(np.linalg.norm(np.asarray(c) - prev)), tid, di))
        pairs.sort()
        for dist, tid, di in pairs:
            if dist > self.max_dist or tid in used or assigned[di] is not None:
                continue
            assigned[di] = tid
            used.add(tid)

        # Unmatched detections become new tracks.
        for di, c in enumerate(centroids):
            if assigned[di] is None:
                assigned[di] = self._next_id
                self._next_id += 1

        # Refresh matched tracks, age out the rest.
        for di, tid in enumerate(assigned):
            self._tracks[tid] = [np.asarray(centroids[di], dtype=float), 0]
        for tid in list(self._tracks):
            if tid not in assigned:
                self._tracks[tid][1] += 1
                if self._tracks[tid][1] > self.max_missing:
                    del self._tracks[tid]

        return assigned

    def reset(self):
        self._tracks.clear()


class MediaPipeBackend:
    """Wraps MediaPipe PoseLandmarker and emits DC3-20 camera-space joints."""

    # DC3's own Kinect horizontal FOV, recovered from the target disassembly of
    # NuiTransformSkeletonToDepthImage (build/373307D9/asm/system/gesture/JointUtl.s
    # :580-627): u = 160 + 285.63*x/z over a 320x240 depth image, so
    # hFOV = 2*atan(160/285.63) = 58.51 deg. Used as the default because it both
    # matches typical webcam optics and is the framing the choreography was
    # authored against; override with --hfov to match a measured camera.
    DEFAULT_HFOV_DEG = 58.51

    def __init__(self, model_path, num_poses=2, hfov_deg=DEFAULT_HFOV_DEG,
                 min_conf=0.5, delegate="cpu"):
        import mediapipe as mp
        from mediapipe.tasks import python as mpp
        from mediapipe.tasks.python import vision

        self._mp = mp
        base_kwargs = {"model_asset_path": model_path}
        if delegate == "gpu":
            base_kwargs["delegate"] = mpp.BaseOptions.Delegate.GPU
        opts = vision.PoseLandmarkerOptions(
            base_options=mpp.BaseOptions(**base_kwargs),
            running_mode=vision.RunningMode.VIDEO,
            num_poses=num_poses,
            min_pose_detection_confidence=min_conf,
            min_pose_presence_confidence=min_conf,
            min_tracking_confidence=min_conf,
            output_segmentation_masks=False,
        )
        self._landmarker = vision.PoseLandmarker.create_from_options(opts)
        self._tracker = CentroidTracker()
        self._hfov = math.radians(hfov_deg)
        self.num_landmarks = NUM_DC3_JOINTS
        self.layout = 1  # DC3-20, camera-space metres

    def close(self):
        self._landmarker.close()

    def reset_tracks(self):
        self._tracker.reset()

    # -- geometry ----------------------------------------------------------
    def _focal_px(self, width):
        """Pinhole focal length in pixels from the assumed horizontal FOV.

        Using ONE focal length for both axes (rather than separate x/y view
        extents) is what makes the mapping aspect-correct: square pixels means
        f_x == f_y, so a 16:9 frame no longer skews angles.
        """
        return (width * 0.5) / math.tan(self._hfov * 0.5)

    def _absolute_root(self, world, image_xy, vis, width, height):
        """Recover the world-landmark origin's camera-space position, in metres.

        Exact linear least-squares (DLT with known structure). We know every
        landmark's full 3D offset from the origin (world landmarks, flipped into
        DC3 axes: rel_i) and its pixel position. The projection u = W/2 - f*X/Z,
        v = H/2 - f*Y/Z with X_i = rel_i + R rearranges to equations LINEAR in
        the root R = (Rx, Ry, Rz):

            f*Rx + a_i*Rz = -f*rel_x_i - a_i*rel_z_i     a_i = u_i - W/2
            f*Ry + b_i*Rz = -f*rel_y_i - b_i*rel_z_i     b_i = v_i - H/2

        Two equations per landmark, three unknowns: solve visibility-weighted
        normal equations. Benchmarked against the 30k-pose reference corpus
        (tools/pose_corpus/bench_z.py) this is EXACT on perfect landmarks --
        unlike torso similar triangles, which even with the in-plane fix carries
        a -0.24 m perspective residual at high tilt -- and it degrades ~7-10x
        more gracefully under noise (1 px pixel jitter: 0.010 m vs 0.102 m;
        50 mm world noise: 0.052 m vs 0.397 m) because it averages ~66 equations
        instead of leaning on one ~47 px torso segment.

        Falls back to the torso method when too few landmarks are visible or the
        system is degenerate; returns None only if that fallback also fails.
        """
        f = self._focal_px(width)
        # World -> DC3 axes: flip x and y, keep z (matches _remap).
        a = image_xy[:, 0] * width - width * 0.5
        b = image_xy[:, 1] * height - height * 0.5
        rel_x, rel_y, rel_z = -world[:, 0], -world[:, 1], world[:, 2]
        w = np.where(vis >= 0.3, np.clip(vis, 0.0, 1.0), 0.0)
        if np.count_nonzero(w) < 4:
            return self._absolute_root_torso(world, image_xy, width, height)

        ru = -f * rel_x - a * rel_z
        rv = -f * rel_y - b * rel_z
        wf2 = float((w * f * f).sum())
        wfa = float((w * f * a).sum())
        wfb = float((w * f * b).sum())
        ata = np.array([
            [wf2, 0.0, wfa],
            [0.0, wf2, wfb],
            [wfa, wfb, float((w * (a * a + b * b)).sum())],
        ])
        aty = np.array([
            float((w * f * ru).sum()),
            float((w * f * rv).sum()),
            float((w * (a * ru + b * rv)).sum()),
        ])
        try:
            root = np.linalg.solve(ata, aty)
        except np.linalg.LinAlgError:
            return self._absolute_root_torso(world, image_xy, width, height)
        if not np.isfinite(root).all():
            return self._absolute_root_torso(world, image_xy, width, height)
        # A living room is roughly 1-6 m; clamp so a bad frame cannot fling the
        # skeleton to infinity and poison displacement scoring.
        root[2] = min(max(root[2], 0.8), 8.0)
        return root

    def _absolute_root_torso(self, world, image_xy, width, height):
        """Fallback root recovery: similar triangles on the torso.

        A segment of known metric length S projecting to s pixels sits at
        Z = f * S / s. The torso (hip centre to shoulder centre) is the best
        single segment -- long, rigid, rarely occluded. Kept only as the
        degenerate-case fallback for _absolute_root, which supersedes it.

        Returns None when the torso projects to a degenerate length.
        """
        f = self._focal_px(width)
        hip_w = (world[L_HIP] + world[R_HIP]) * 0.5
        sho_w = (world[L_SHOULDER] + world[R_SHOULDER]) * 0.5

        # Use the torso's IN-PLANE extent, not its full 3D length. A segment
        # tilted out of the image plane projects to L*cos(tilt), so dividing the
        # measured pixel length by the full L over-estimates depth by exactly
        # 1/cos(tilt). Measured against the reference corpus, using the full
        # length biased hip depth by +0.0045 m when fronto-parallel but +0.83 m
        # once sin(tilt) exceeded 0.5 -- a systematic error, always positive.
        # We know the out-of-plane component because world landmarks are 3D, so
        # projecting the torso onto the image plane removes the bias entirely.
        torso_vec = sho_w - hip_w
        torso_m = float(np.hypot(torso_vec[0], torso_vec[1]))

        hip_px = (image_xy[L_HIP] + image_xy[R_HIP]) * 0.5
        sho_px = (image_xy[L_SHOULDER] + image_xy[R_SHOULDER]) * 0.5
        hip_px = np.array([hip_px[0] * width, hip_px[1] * height])
        sho_px = np.array([sho_px[0] * width, sho_px[1] * height])
        torso_px = float(np.linalg.norm(sho_px - hip_px))

        if torso_m < 1e-3 or torso_px < 1.0:
            return None

        z_hip = f * torso_m / torso_px
        # A living room is roughly 1-6 m; clamp so a bad frame cannot fling the
        # skeleton to infinity and poison displacement scoring.
        z_hip = min(max(z_hip, 0.8), 8.0)

        # Screen-right is -X and screen-down is -Y in DC3 camera space (matches
        # NormalizedToMeters' `0.5 - n` form and the baked capture, where the feet
        # sit at y=-0.92 below a sensor ~0.92 m above the floor -- note DC3's Y
        # origin is the SENSOR, not the floor).
        x_hip = -(hip_px[0] - width * 0.5) / f * z_hip
        y_hip = -(hip_px[1] - height * 0.5) / f * z_hip
        return np.array([x_hip, y_hip, z_hip])

    # -- main entry --------------------------------------------------------
    def process(self, frame_bgr, timestamp_ms):
        """Returns list of (track_id, [(x,y,z,conf)] * 20) in camera-space metres."""
        import cv2

        height, width = frame_bgr.shape[:2]
        rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        mp_img = self._mp.Image(image_format=self._mp.ImageFormat.SRGB, data=rgb)
        res = self._landmarker.detect_for_video(mp_img, int(timestamp_ms))

        if not res.pose_world_landmarks:
            self._tracker.update([])
            return []

        people, centroids = [], []
        for pi, wlms in enumerate(res.pose_world_landmarks):
            world = np.array([[l.x, l.y, l.z] for l in wlms], dtype=float)
            if pi < len(res.pose_landmarks):
                ilms = res.pose_landmarks[pi]
                image_xy = np.array([[l.x, l.y] for l in ilms], dtype=float)
                vis = np.array([getattr(l, "visibility", 1.0) for l in ilms], dtype=float)
            else:
                image_xy = np.zeros((len(wlms), 2))
                vis = np.ones(len(wlms))

            root = self._absolute_root(world, image_xy, vis, width, height)
            if root is None:
                continue

            joints, confs = self._remap(world, vis, root)
            people.append((joints, confs))
            hip_c = (image_xy[L_HIP] + image_xy[R_HIP]) * 0.5
            centroids.append((float(hip_c[0]), float(hip_c[1])))

        ids = self._tracker.update(centroids)
        out = []
        for (joints, confs), tid in zip(people, ids):
            out.append((int(tid), [
                (float(joints[j][0]), float(joints[j][1]), float(joints[j][2]),
                 float(confs[j]))
                for j in range(NUM_DC3_JOINTS)
            ]))
        return out

    def _remap(self, world, vis, root):
        """BlazePose 33 (hip-relative metres) -> DC3 20 (camera-space metres)."""
        # Axis flip, then translate onto the recovered absolute root.
        cam = np.empty_like(world)
        cam[:, 0] = -world[:, 0] + root[0]
        cam[:, 1] = -world[:, 1] + root[1]
        cam[:, 2] = world[:, 2] + root[2]

        j = np.zeros((NUM_DC3_JOINTS, 3), dtype=float)
        c = np.zeros(NUM_DC3_JOINTS, dtype=float)

        def mid(a, b):
            return (cam[a] + cam[b]) * 0.5

        def minc(*idx):
            return float(min(vis[i] for i in idx))

        j[HIP_LEFT], c[HIP_LEFT] = cam[L_HIP], vis[L_HIP]
        j[HIP_RIGHT], c[HIP_RIGHT] = cam[R_HIP], vis[R_HIP]
        j[HIP_CENTER], c[HIP_CENTER] = mid(L_HIP, R_HIP), minc(L_HIP, R_HIP)
        j[SHOULDER_LEFT], c[SHOULDER_LEFT] = cam[L_SHOULDER], vis[L_SHOULDER]
        j[SHOULDER_RIGHT], c[SHOULDER_RIGHT] = cam[R_SHOULDER], vis[R_SHOULDER]
        j[SHOULDER_CENTER] = mid(L_SHOULDER, R_SHOULDER)
        c[SHOULDER_CENTER] = minc(L_SHOULDER, R_SHOULDER)
        j[SPINE] = (j[HIP_CENTER] + j[SHOULDER_CENTER]) * 0.5
        c[SPINE] = min(c[HIP_CENTER], c[SHOULDER_CENTER])

        # Kinect's head is ~skull centre; the ear midpoint is much closer to that
        # than the nose, which sits anterior and inferior. Nose is the fallback
        # when a profile view occludes an ear.
        if minc(L_EAR, R_EAR) > 0.3:
            j[HEAD], c[HEAD] = mid(L_EAR, R_EAR), minc(L_EAR, R_EAR)
        else:
            j[HEAD], c[HEAD] = cam[NOSE], vis[NOSE]

        j[ELBOW_LEFT], c[ELBOW_LEFT] = cam[L_ELBOW], vis[L_ELBOW]
        j[WRIST_LEFT], c[WRIST_LEFT] = cam[L_WRIST], vis[L_WRIST]
        j[ELBOW_RIGHT], c[ELBOW_RIGHT] = cam[R_ELBOW], vis[R_ELBOW]
        j[WRIST_RIGHT], c[WRIST_RIGHT] = cam[R_WRIST], vis[R_WRIST]

        # Real hand landmarks at last: the knuckle line (pinky/index midpoint) is
        # about where Kinect's hand joint sits, ~0.10 m past the wrist.
        j[HAND_LEFT], c[HAND_LEFT] = mid(L_PINKY, L_INDEX), minc(L_PINKY, L_INDEX)
        j[HAND_RIGHT], c[HAND_RIGHT] = mid(R_PINKY, R_INDEX), minc(R_PINKY, R_INDEX)

        j[KNEE_LEFT], c[KNEE_LEFT] = cam[L_KNEE], vis[L_KNEE]
        j[ANKLE_LEFT], c[ANKLE_LEFT] = cam[L_ANKLE], vis[L_ANKLE]
        j[KNEE_RIGHT], c[KNEE_RIGHT] = cam[R_KNEE], vis[R_KNEE]
        j[ANKLE_RIGHT], c[ANKLE_RIGHT] = cam[R_ANKLE], vis[R_ANKLE]

        j[FOOT_LEFT] = cam[L_HEEL] * (1.0 - FOOT_TOE_BLEND) + cam[L_FOOT] * FOOT_TOE_BLEND
        c[FOOT_LEFT] = minc(L_HEEL, L_FOOT)
        j[FOOT_RIGHT] = cam[R_HEEL] * (1.0 - FOOT_TOE_BLEND) + cam[R_FOOT] * FOOT_TOE_BLEND
        c[FOOT_RIGHT] = minc(R_HEEL, R_FOOT)

        return j, c
