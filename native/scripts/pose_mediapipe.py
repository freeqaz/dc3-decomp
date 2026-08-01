#!/usr/bin/env python3
"""MediaPipe BlazePose GHUM backend for the DC3 pose server.

WHY THIS EXISTS: the (retired) YOLO11n-pose backend produced COCO-17 keypoints with NO
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

# Kinect's kJointFoot sits ~7.9 cm from the ankle, pointing DOWN AND FORWARD at
# -50 deg elevation (dy -0.059, dz -0.038), not straight down. Measured over
# 116 k frames of real Kinect v1 output in tools/pose_corpus/bench_utd_mhad.py,
# and strikingly stable: per-action mean 0.074-0.084 m at -45 to -52 deg on all
# 27 actions. (The single baked frame in src/system/gesture/StubCameraInput.cpp
# :57-61 reads 4.6 cm nearly straight down -- that frame is NOT representative,
# so do not re-derive this constant from it.) BlazePose's foot_index is the toe
# tip, ~10.6 cm out, so blend it toward the heel to land near the mid-foot.
# Sweeping the blend against Kinect's own foot-minus-ankle offset gives a flat
# minimum at 0.30-0.35 (0.0320 m limb-relative); the resulting joint is 0.079 m
# at -58 deg, matching the reference's length to 0.5 mm.
FOOT_TOE_BLEND = 0.35

# ---------------------------------------------------------------------------
# CENTRE JOINTS -- HipCenter / Spine / ShoulderCenter are NOT midpoints.
#
# DC3's choreography, gesture filters and scoring thresholds were all authored
# against Kinect v1 skeletons, so these three joints have to reproduce KINECT'S
# definitions, not anatomical midpoints. Measured on UTD-MHAD (861 trials,
# 116 k frames of real Kinect v1 skeletons over 8 subjects,
# tools/pose_corpus/bench_utd_mhad.py). Expressing each centre joint in a body
# frame built from Kinect's OWN hips and shoulders --
#     hip_mid + a*(sho_mid - hip_mid) + b*|torso|*back
# -- gives, pooled over all 8 subjects (between-subject sd in brackets):
#     HipCenter        a 0.193 [0.008]  b +0.008 [0.003]
#     Spine            a 0.370 [0.007]  b +0.096 [0.016]
#     ShoulderCenter   a 1.285 [0.013]  b +0.019 [0.025]
# i.e. HipCenter sits ~19% up the torso (7.2 cm above the hip line -- confirmed
# independently by the baked capture, HipCenter.y 0.1118 vs HipLeft.y 0.0333),
# ShoulderCenter ~29% ABOVE the shoulder line (the neck base), and Spine only
# ~37% up -- plus a real 3.6 cm POSTERIOR offset, because Kinect's Spine is a
# spine-surface point rather than a point on the hip-to-shoulder line. Those
# fractions are scale-invariant (fractions of the subject's own torso), which is
# what makes them safe for players of any size.
#
# The fractions below are NOT those numbers, because BlazePose's torso is not
# Kinect's torso: body-aligned over the same corpus our hip landmarks sit 5.5 cm
# LOWER and our shoulder landmarks 7.5 cm HIGHER than Kinect's, so our
# hip_mid->sho_mid span is 0.488 m against Kinect's 0.372 m (1.32x). The
# coefficients are therefore fitted in OUR body frame by least squares against
# the Kinect target on subjects 1-5, and validated on held-out subjects 6-8
# (body-aligned mean error, held-out, metres):
#     HipCenter       0.1385 -> 0.0486     Spine  0.0878 -> 0.0555
#     ShoulderCenter  0.1434 -> 0.1389
# and because DC3 root-aligns on HipCenter, this drops the ROOT-ALIGNED error of
# every other joint too: mean over all 20 joints 0.219 -> 0.165 m held-out.
#
# WHY ONLY SPINE GETS A PERPENDICULAR TERM: the fit "wants" a 0.147 posterior
# term on ShoulderCenter as well (held-out 0.1389 -> 0.1028), but Kinect's own
# ShoulderCenter is b = +0.019, i.e. essentially ON its shoulder line -- that
# 0.147 is compensating the 5.7 cm ANTERIOR bias of our shoulder LANDMARKS, and
# baking it into one derived joint would leave the neck sitting behind our own
# ShoulderLeft/Right and skew every shoulder-relative vector the scorer forms.
# Spine's term survives the same test: Kinect's intrinsic 0.096*0.372 = 3.6 cm
# and the value fitted in our frame, 0.085*0.488 = 4.2 cm, agree, so it is a
# definition and not bias absorption. HipCenter needs none (b 0.008 = 3 mm).
#
# CAVEAT for anyone re-deriving these: the per-subject spread of the fitted
# axial term is wide (HipCenter 0.18-0.36 across the 8 subjects) because it
# absorbs how BlazePose places hips on that particular body. The pooled value
# still beats the midpoint on every subject; do not read the last digit as
# precise. ShoulderCenter's 1.04 is within that spread of 1.00 -- Kinect's 29%
# neck-base rise very nearly cancels against our higher shoulder landmarks, so
# only ~2 cm of correction survives.
HIP_CENTER_UP = 0.24        # fraction of the hip_mid -> sho_mid torso vector
SPINE_UP = 0.36
SPINE_BACK = 0.085          # fraction of torso LENGTH, posterior (away from camera)
SHOULDER_CENTER_UP = 1.04


class OneEuroFilter:
    """Speed-adaptive low-pass (Casiez et al.): smooth when slow, responsive
    when fast.

    Applied to the recovered root DEPTH only. Justification is measured, not
    assumed: against AIST++ ground truth (7 sequences incl. 1.8 m of real
    depth travel), the raw per-frame root z carries 0.089 m/frame of jitter
    where the true motion is 0.017 -- phantom depth velocity that feeds
    straight into displacement scoring. min_cutoff=0.2 Hz, beta=0.1 was grid-
    swept against that GT at 15 and 30 fps: it cuts jitter 4.7x AND lowers
    absolute |z| error ~10% (0.176 -> 0.159 m mean, p90 0.363 -> 0.312),
    because the noise is larger than the true motion. x/y are left raw: their
    measured jitter is near the true-motion floor and lateral dance moves are
    fast and real.
    """

    def __init__(self, min_cutoff=0.2, beta=0.1, d_cutoff=1.0):
        self.min_cutoff = min_cutoff
        self.beta = beta
        self.d_cutoff = d_cutoff
        self._x = None
        self._dx = 0.0
        self._t = None

    def __call__(self, x, t_s):
        if self._x is None:
            self._x, self._t = x, t_s
            return x
        dt = t_s - self._t
        if dt <= 0.0:
            return self._x
        self._t = t_s

        def alpha(cutoff):
            tau = 1.0 / (2.0 * math.pi * cutoff)
            return 1.0 / (1.0 + tau / dt)

        dx = (x - self._x) / dt
        self._dx += alpha(self.d_cutoff) * (dx - self._dx)
        cutoff = self.min_cutoff + self.beta * abs(self._dx)
        self._x += alpha(cutoff) * (x - self._x)
        return self._x


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
        self._root_z_filters = {}  # track id -> OneEuroFilter on root depth
        self._filter_last_seen = {}  # track id -> last process() call index
        self._frame_count = 0
        self._hfov = math.radians(hfov_deg)
        self.num_landmarks = NUM_DC3_JOINTS
        self.layout = 1  # DC3-20, camera-space metres

    def close(self):
        self._landmarker.close()

    def reset_tracks(self):
        self._tracker.reset()
        self._root_z_filters.clear()
        self._filter_last_seen.clear()

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
            people.append((joints, confs, float(root[2])))
            hip_c = (image_xy[L_HIP] + image_xy[R_HIP]) * 0.5
            centroids.append((float(hip_c[0]), float(hip_c[1])))

        ids = self._tracker.update(centroids)
        self._frame_count += 1
        t_s = timestamp_ms / 1000.0
        out = []
        for (joints, confs, root_z), tid in zip(people, ids):
            # Filter the root DEPTH per persistent track and shift the whole
            # skeleton by the correction -- root noise is a rigid z offset, so
            # every joint moves together and the pose shape is untouched.
            filt = self._root_z_filters.setdefault(int(tid), OneEuroFilter())
            self._filter_last_seen[int(tid)] = self._frame_count
            joints = joints.copy()
            joints[:, 2] += filt(root_z, t_s) - root_z
            out.append((int(tid), [
                (float(joints[j][0]), float(joints[j][1]), float(joints[j][2]),
                 float(confs[j]))
                for j in range(NUM_DC3_JOINTS)
            ]))
        # Age out filters for departed tracks (past the tracker's own
        # max_missing grace, so a brief dropout keeps its filter state).
        stale = [t for t, seen in self._filter_last_seen.items()
                 if self._frame_count - seen > 60]
        for t in stale:
            self._filter_last_seen.pop(t, None)
            self._root_z_filters.pop(t, None)
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
        j[SHOULDER_LEFT], c[SHOULDER_LEFT] = cam[L_SHOULDER], vis[L_SHOULDER]
        j[SHOULDER_RIGHT], c[SHOULDER_RIGHT] = cam[R_SHOULDER], vis[R_SHOULDER]

        # Centre joints: fractions along the subject's own torso, not midpoints
        # (see the CENTRE JOINTS block above for the measurement).
        hip_mid = mid(L_HIP, R_HIP)
        torso = mid(L_SHOULDER, R_SHOULDER) - hip_mid
        torso_len = float(np.linalg.norm(torso))
        # Body-frame POSTERIOR direction. cross(up, player-left) is +Z (away
        # from the camera) for a player facing the sensor and rotates with them,
        # so the Spine offset stays behind the spine whichever way they turn.
        # Orthogonalising the hip axis against the torso first would not change
        # this: cross(t, l - (l.t)t) == cross(t, l).
        back = np.cross(torso, cam[L_HIP] - cam[R_HIP])
        back_len = float(np.linalg.norm(back))
        back = back / back_len if back_len > 1e-9 else np.zeros(3)

        centre_conf = minc(L_HIP, R_HIP, L_SHOULDER, R_SHOULDER)
        j[HIP_CENTER] = hip_mid + HIP_CENTER_UP * torso
        j[SPINE] = hip_mid + SPINE_UP * torso + (SPINE_BACK * torso_len) * back
        j[SHOULDER_CENTER] = hip_mid + SHOULDER_CENTER_UP * torso
        c[HIP_CENTER] = c[SPINE] = c[SHOULDER_CENTER] = centre_conf

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

        # Real hand landmarks at last: the knuckle line (pinky/index midpoint)
        # is about where Kinect's hand joint sits. Measured, not assumed: over
        # 116 k real Kinect v1 frames (bench_utd_mhad.py) Kinect puts its hand
        # joint 0.068 m past the wrist at -42.3 deg elevation and this
        # construction puts ours 0.083 m out at -42.8 deg -- same direction,
        # 1.5 cm long. (Two independent hand-GT corpora say the true wrist-to-
        # knuckle distance is 0.076-0.080 m: bench_panoptic_hands.py on CMU
        # Panoptic 21-keypoint hands and bench_3dhp.py. The older "~0.10 m past
        # the wrist" note here was wrong -- what we actually emit is
        # 0.042-0.060 m, so we UNDER-reach rather than over-reach.) Shortening
        # to wrist + 0.7*(knuckle - wrist) does buy ~8 mm against Kinect, but it
        # is NOT applied, on the other two corpora's evidence: 3DHP sweeps that
        # exact factor and k = 1.0 is an INTERIOR minimum there (offset error
        # 0.0603 at k=1.0 against 0.0660 at 0.7, 0.0690 at 0.6, 0.0698 at 1.6),
        # so 0.7 costs 5.7 mm -- as much as it gains -- and Panoptic finds any
        # rescale in x0.78-x1.24 worth under 1 mm. Kinect's own hand joint also
        # sits 18.8 px (~10 cm at 2.85 m) from the visible hand in the image, so
        # the 8 mm is well inside the reference's own error bar. Two anatomical
        # corpora against one estimator quirk: keep the knuckle midpoint.
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
