#!/usr/bin/env python3
"""Measure the PRODUCTION MediaPipe backend's HAND and TOE end-effector 3D error
against MPI-INF-3DHP ground truth -- the distal joints AIST++ could not score.

WHY: tools/pose_corpus/bench_model_z.py validated the backend's BODY joints
against AIST++ (root-aligned |dz| 0.085 m; wrist worst at 0.24 m) but had to
DROP HandLeft/Right and FootLeft/Right entirely: COCO-17 stops at wrist and
ankle, so the two joints the backend SYNTHESISES -- hand = pinky/index knuckle
midpoint, foot = heel/toe blend (FOOT_TOE_BLEND) -- were unmeasured. Separately
we measured an apparent ~30 deg TOE-DOWN PITCH in MediaPipe's foot during
verified stance (toe ~6.7 cm below heel where a planted foot should be flat),
with no GT able to confirm or refute it. MPI-INF-3DHP is the fix: its 28-joint
GT carries left_hand/right_hand BEYOND the wrist and left_foot/left_toe (plus
right) BEYOND the ankle, already expressed per-camera.

GROUND TRUTH: MPI-INF-3DHP (http://gvv.mpi-inf.mpg.de/3dhp-dataset/).
  - annot.mat: annot3{cam} is (F, 84) = 28 joints x (x,y,z) in MILLIMETRES,
    ALREADY in that camera's coordinate frame (no world->camera step needed --
    verified below: K @ annot3 reprojects onto annot2 to a median 0.003 px).
  - camera.calibration: "Skeletool" text format, 14 cameras, 4x4 intrinsic and
    extrinsic per camera, 2048x2048, radial 0 (undistorted -- so no distortion
    coefficients are applied anywhere in this script).
  - imageSequence/vnect_cameras.zip: video_{0,1,2,4,5,6,7,8}.avi, the "vnect"
    camera subset (util/mpii_get_camera_set.m). Frame count matches annot
    exactly, 1:1, no offset (S1/Seq1: 6416 frames @ 25 fps, per
    util/mpii_get_sequence_info.m).
  License: non-commercial research use, redistribution PROHIBITED -- which is
  why everything lives under ~/tmp/pose_gt_3dhp/ and NOT in this repo.

GT CAVEAT THAT SHAPES EVERY NUMBER BELOW -- read before quoting a table:
3DHP GT is markerless multi-camera solver output (The Captury) fitted to an
ANATOMICAL skeleton, not hand-annotated surface keypoints. Two consequences:
  (a) Distal joints are the SOLVER's estimate. 'hand' is the hand/knuckle
      region, 'toe' the toe-segment end. They are as good as a commercial
      markerless solver, not as good as markers on skin.
  (b) Joint CENTRES differ definitionally from BlazePose's surface landmarks --
      Captury hips sit at the femoral head, BlazePose's sit lower and more
      lateral. This inflates ABSOLUTE per-joint error by a fixed per-joint
      offset that is NOT model error. AIST++ (2D-annotation-derived COCO-17)
      does not have this problem, which is why its gate hit ~6 px and this
      one lands ~30 px. Absolute per-joint error here is ~0.40 m and is NOT a
      usable decision metric -- it is dominated by root recovery plus that
      offset, both of which are IDENTICAL for a parent joint and its child.
      Judge the model on (i) root-aligned error, (ii) the OFFSET error
      (report_offset_error: child minus parent on both sides, so root and the
      shared definitional offset cancel exactly -- this is what the
      FOOT_TOE_BLEND and pitch-correction sweeps are scored on), and (iii) the
      foot PITCH ANGLE, a within-skeleton direction invariant to where either
      convention puts the ankle origin.

HEADLINE FINDINGS (S1/Seq1 + S2/Seq1, camera 0, ~12.9k frames, 92-93% detected):
  * TOE-DOWN PITCH IS REAL AND CONFIRMED. During GT-verified sole-down stance
    our ankle->toe segment pitches -33.9 deg where GT pitches -12.1 deg: a
    PAIRED excess of -21.8 deg, same sign on 94% of frames, and consistent
    across both subjects (-24.4/-25.6 deg on S1, -22.8/-17.8 on S2). The
    original ~30 deg estimate was high; ~20 deg is the number. In height terms
    our toe sits 4.0 cm lower relative to the ankle than GT's.
  * THE BIAS IS GLOBAL, NOT A CONTACT-RESOLUTION FAILURE. Over ALL valid frames
    the paired delta is -16.3 deg vs -17.8 deg in stance. It is a landmark
    convention offset, so a constant rotation is a legitimate shape of fix.
  * GT CONFIRMS PLANTED FEET ARE FLAT. In sole-down stance the GT toe sits
    +0.003 to +0.006 m above the GT ball on all four subject-sides, i.e. the
    forefoot is level, with ball 0.8-2.5 cm and toe 1.1-3.1 cm above the floor
    under an ankle 4.2-5.4 cm up. Ours puts the toe 1.9-4.5 cm BELOW the heel.
  * HAND INCREMENT +0.032 to +0.037 m: our knuckle-midpoint hand adds ~3.5 cm
    of error over the wrist it is built from. Our wrist->hand segment is
    0.059-0.068 m against 0.089-0.097 m of GT and 0.099-0.122 m of Kinect --
    but SCALING IT OUT MAKES ACCURACY WORSE (offset error 0.060 at k=1.0,
    0.070 at k=1.6), because the wrist->knuckle DIRECTION is noisy and
    lengthening amplifies it. The shipped k=1.0 is an INTERIOR minimum, not an
    endpoint: shortening hurts too (0.0615 at k=0.9, 0.0660 at k=0.7, 0.0690 at
    k=0.6), which is the direct answer to the k~0.7 that UTD-MHAD's Kinect
    reference prefers -- that 8 mm Kinect gain costs 5.7 mm here. Leave the
    hand construction alone.
  * FOOT_TOE_BLEND: the two candidate targets DISAGREE and must not be
    conflated. Against 3DHP anatomy (GT ball) the optimum is 0.65
    (0.0828 m vs 0.0898 at the current 0.35). Against the KINECT convention
    DC3 actually scores -- ankle->foot 0.043 m, near-vertical -- the optimum is
    0.0 (0.0438 m, +0.0005 from Kinect; the current 0.35 is 0.069 m, +2.6 cm
    too long and 25 deg too shallow). Kinect is the scoring target.

UPDATE 2026-08-01 -- centre joints stopped being midpoints, so the ROOT-ALIGNED
columns moved. HipCenter / Spine / ShoulderCenter are now torso fractions
matching the Kinect convention (pose_mediapipe CENTRE JOINTS block, derived in
bench_utd_mhad.py) and gt_subset above builds the GT side identically. Every
ABSOLUTE row is bit-identical, and so are the foot-pitch, blend and
hand-extension sweeps (all anchor-free). What moves is root-aligned: Spine
0.164 -> 0.022, HipL/R 0.123/0.137 -> 0.068/0.071, shoulders ~0.02 better --
but knees 0.177/0.161 -> 0.213/0.200 and ankles 0.257/0.215 -> 0.310/0.269
WORSE, because the anchor moved up the torso and 3DHP's Captury hips are not
Kinect's. That is definitional, not a regression: this corpus cannot arbitrate
the Kinect convention (it has no Kinect centre joints), UTD-MHAD does, and
there the same change cut the root-aligned error of all 20 joints from 0.219 to
0.165 m. Read the ABSOLUTE and offset-vector columns here. The hand increment
is essentially unchanged (+0.040/+0.031 -> +0.037/+0.035 m).

AXIS CONVENTIONS -- every flip documented, because a sign error here poisons
the whole comparison (see bench_z.py:to_camera_view for the war story):
  1. 3DHP annot3 is already camera-space OpenCV: +X image right, +Y image DOWN,
     +Z into the scene. VERIFIED, not assumed: (a) K @ annot3 == annot2 to
     0.003 px median, (b) head_top has NEGATIVE y and ankles POSITIVE y on a
     standing subject, (c) the projected skeleton lands on the body in the
     saved gate PNGs.
  2. DC3 camera space: X = -X_cv, Y = -Y_cv, Z = +Z_cv (identical to
     bench_model_z.gt_to_dc3; two flips = 180 deg about Z, handedness kept).
  3. Units: millimetres -> metres (/1000).

PRINCIPAL POINT: the backend's pinhole (pose_mediapipe._absolute_root) assumes
the principal point is exactly at the image centre. 3DHP's is not (cam 0:
cx 1024.70, cy 1051.39 vs centre 1024, 1024 -- cy is off by 27 px). We do NOT
change the backend; instead the replay shifts landmark pixels by (W/2 - cx,
H/2 - cy) before normalising, which is exactly the image a camera with a
centred principal point would have produced. fx and fy agree to 0.04% (1497.69
vs 1497.10), so the backend's single-focal assumption holds, and
hfov = 2*atan((W/2)/fx) = 68.63 deg for camera 0.

GRAVITY: pitch is measured against TRUE VERTICAL, not against the camera's Y
axis. World up expressed in camera coordinates is column 1 of the extrinsic
rotation; for camera 0 that is 2.0 deg off the camera Y axis. Small, but the
whole toe-pitch question is an angle measurement, so it is corrected rather
than assumed away.

CAMERA CHOICE: camera 0. Of the vnect subset it is the only chest-height view
(extrinsic height 1.40 m) that keeps the WHOLE body -- feet included -- inside
the frame for the entire sequence; cam 7 crops the feet, cams 4/8 are knee-high
(0.66-0.80 m). Feet in frame is non-negotiable for a toe study.

WHAT IS MEASURED: the exact shipping code, same replay contract as
bench_model_z.py. MediaPipeBackend's own PoseLandmarker is run over the video
with production constructor options (VIDEO running mode, min_conf 0.5, full
model), consecutive frames, detect_for_video timestamps from the frame index;
then _absolute_root and _remap are called VERBATIM. Nothing is reimplemented.
Deviations from the live server: num_poses=1 (one subject), and the OneEuro
root-depth filter is not applied (bench_model_z.py replays the same way, so the
two studies stay comparable).

NOTE ON FEEDING ORDER: frames MUST be fed consecutively. detect_for_video keeps
a tracking ROI from the previous frame; sampling every Nth frame collapses
detection (measured on this sequence: 8/12 detected and ~200 px landmark std
when strided every 250th frame, 198/200 detected and 75 px median on the same
frames fed consecutively).

SANITY GATES (printed per sequence; all four must pass before a table is worth
quoting): GATE 0 annot3 -> K -> annot2 reprojection, must be sub-pixel (measured
0.003-0.005 px median); GATE 1 GT-2D vs MediaPipe-2D on shoulders/hips/knees
(29-33 px of 2048 = 15-17% of torso length -- the definitional offset, not
tracking failure; visually verified by overlay); GATE 2 detection rate (92-93%);
GATE 3 root-relative world-landmark MPJPE (160-187 mm; higher than BlazePose's
published 35-90 mm for the same definitional reason).

JOINT MAPPING (3DHP 0-indexed, from util/mpii_get_joint_set.m):
    4 pelvis  5 neck  6 head  7 head_top
    9 l_shoulder 10 l_elbow 11 l_wrist 12 l_hand
   14 r_shoulder 15 r_elbow 16 r_wrist 17 r_hand
   18 l_hip 19 l_knee 20 l_ankle 21 l_foot 22 l_toe
   23 r_hip 24 r_knee 25 r_ankle 26 r_foot 27 r_toe
Derived joints are built the SAME way on both sides (HipCenter = mid-hips,
ShoulderCenter = mid-shoulders, Spine = mid of those) so the construction
cannot itself create a difference. Head is GT joint 6 vs our ear-midpoint --
reported but flagged as a definitional mismatch. Our single FootLeft/Right is
scored against BOTH GT 'foot' and GT 'toe'; the GT geometry printout shows why
that matters (they are only ~2.7 cm apart -- 'foot' is the ball, 'toe' the tip,
and NEITHER is a heel, so our heel-biased blend has no exact GT counterpart).

Run:
  .venv/bin/python tools/pose_corpus/bench_3dhp.py \
      --gt-dir /home/free/tmp/pose_gt_3dhp [--seqs S1_Seq1 S2_Seq1] \
      [--camera 0] [--quick 2000] [--recompute]
"""

import argparse
import math
import os
import sys

import cv2
import numpy as np
import scipy.io

# Import the PRODUCTION backend -- the entire point is to measure shipping code.
_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO, "native", "scripts"))
from pose_mediapipe import (  # noqa: E402
    MediaPipeBackend, DC3_JOINT_NAMES, NUM_DC3_JOINTS, FOOT_TOE_BLEND,
    HIP_CENTER_UP, SPINE_UP, SPINE_BACK, SHOULDER_CENTER_UP,
    HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD,
    SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT, HAND_LEFT,
    SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT, HAND_RIGHT,
    HIP_LEFT, KNEE_LEFT, ANKLE_LEFT, HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT,
    FOOT_LEFT, FOOT_RIGHT,
    L_HEEL, R_HEEL, L_FOOT as MP_L_TOE, R_FOOT as MP_R_TOE,
    L_ANKLE as MP_L_ANKLE, R_ANKLE as MP_R_ANKLE,
    L_SHOULDER as MP_LSHO, R_SHOULDER as MP_RSHO,
    L_HIP as MP_LHIP, R_HIP as MP_RHIP,
    L_KNEE as MP_LKNE, R_KNEE as MP_RKNE,
)

MODEL_PATH = os.path.join(_REPO, "native", "models", "pose_landmarker_full.task")

# ---- 3DHP joint indices (0-based; util/mpii_get_joint_set.m order) ---------
G_PELVIS, G_NECK, G_HEAD, G_HEADTOP = 4, 5, 6, 7
G_LSHO, G_LELB, G_LWRI, G_LHAND = 9, 10, 11, 12
G_RSHO, G_RELB, G_RWRI, G_RHAND = 14, 15, 16, 17
G_LHIP, G_LKNE, G_LANK, G_LFOOT, G_LTOE = 18, 19, 20, 21, 22
G_RHIP, G_RKNE, G_RANK, G_RFOOT, G_RTOE = 23, 24, 25, 26, 27
NUM_GT_JOINTS = 28

# Directly-corresponding joints: (dc3_index, gt_index).  Report order.
DIRECT = [
    (SHOULDER_LEFT, G_LSHO), (SHOULDER_RIGHT, G_RSHO),
    (ELBOW_LEFT, G_LELB), (ELBOW_RIGHT, G_RELB),
    (WRIST_LEFT, G_LWRI), (WRIST_RIGHT, G_RWRI),
    (HAND_LEFT, G_LHAND), (HAND_RIGHT, G_RHAND),
    (HIP_LEFT, G_LHIP), (HIP_RIGHT, G_RHIP),
    (KNEE_LEFT, G_LKNE), (KNEE_RIGHT, G_RKNE),
    (ANKLE_LEFT, G_LANK), (ANKLE_RIGHT, G_RANK),
]
# Our FootLeft/Right has no single GT counterpart; scored against both.
FOOT_VARIANTS = [
    ("FootLeft  vs GT foot", FOOT_LEFT, G_LFOOT),
    ("FootLeft  vs GT toe ", FOOT_LEFT, G_LTOE),
    ("FootRight vs GT foot", FOOT_RIGHT, G_RFOOT),
    ("FootRight vs GT toe ", FOOT_RIGHT, G_RTOE),
]
# Derived-on-both-sides joints occupy the first 4 slots of the comparison set.
DERIVED = ["HipCenter", "Spine", "ShoulderCenter", "Head*"]
SUBSET = [HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD] + [d for d, _ in DIRECT]
SUBSET_NAMES = DERIVED + [DC3_JOINT_NAMES[d] for d, _ in DIRECT]

# 2D sanity-gate pairs (BlazePose landmark, 3DHP joint).
GATE_PAIRS = [(MP_LSHO, G_LSHO), (MP_RSHO, G_RSHO), (MP_LHIP, G_LHIP),
              (MP_RHIP, G_RHIP), (MP_LKNE, G_LKNE), (MP_RKNE, G_RKNE)]

# Stance = the GT foot is not moving and its toe is on the ground. Deliberately
# defined from GT ALONE (never from our prediction) so the pitch comparison
# cannot be circular.
# The SCORING target is not 3DHP, it is Kinect: DC3's choreography was authored
# against Kinect skeletons, and src/system/gesture/StubCameraInput.cpp:37-61
# bakes one real Kinect frame. Those offsets are the convention our synthesised
# HandLeft/Right and FootLeft/Right are supposed to imitate; 3DHP tells us what
# the ANATOMY does, Kinect tells us where DC3 expects the joint. Both are
# reported so the two are never conflated.  (DC3 camera axes, metres.)
KINECT_WRIST_HAND = {"L": np.array([-0.005124, -0.097111, -0.019606]),
                     "R": np.array([0.004749, -0.113930, -0.042040])}
KINECT_ANKLE_FOOT = {"L": np.array([-0.002411, -0.045550, -0.003471]),
                     "R": np.array([-0.010847, -0.038835, -0.007239])}

STANCE_SPEED = 0.005   # m/frame; GT ankle speed below this = planted (25 fps)
STANCE_FLOOR = 0.08    # m; GT toe must also be within this of the floor
# A PLANTED foot can still be on tiptoe (ankle high, toe down): that is a real
# pose, not a bias, and it fattens the GT pitch spread. The strict mask below
# isolates SOLE-DOWN frames for the "are planted feet flat?" question. The
# threshold is anatomy, not fitted: a flat adult ankle joint centre rides
# 5-9 cm above the floor, a tiptoe ankle 15 cm+.
FLAT_ANKLE_MAX = 0.10  # m above floor
FLAT_SPEED = 0.003     # m/frame
FLAT_TOE_MAX = 0.05    # m above floor (GT toe JOINT CENTRE, not the skin)


# ---------------------------------------------------------------------------
# Dataset loading
# ---------------------------------------------------------------------------
def parse_calibration(path):
    """Skeletool V1.0 text calibration -> {cam_id: {K, R_wc, t, W, H, up_dc3}}.

    'intrinsic' and 'extrinsic' are 4x4 row-major. The extrinsic is world->camera,
    so column 1 of its rotation is the world +Y (up) axis written in camera
    coordinates -- used to measure pitch against true vertical.
    """
    cams, cur = {}, None
    for line in open(path):
        t = line.split()
        if not t:
            continue
        if t[0] == "name":
            cur = int(t[1]); cams[cur] = {}
        elif cur is None:
            continue
        elif t[0] == "intrinsic":
            cams[cur]["K"] = np.array(list(map(float, t[1:17]))).reshape(4, 4)[:3, :3]
        elif t[0] == "extrinsic":
            E = np.array(list(map(float, t[1:17]))).reshape(4, 4)
            cams[cur]["R_wc"] = E[:3, :3]; cams[cur]["t"] = E[:3, 3]
        elif t[0] == "size":
            cams[cur]["W"], cams[cur]["H"] = int(t[1]), int(t[2])
    for c in cams.values():
        c["hfov_deg"] = float(np.degrees(2.0 * np.arctan((c["W"] / 2.0) / c["K"][0, 0])))
        up_cv = c["R_wc"][:, 1]                      # world +Y in CV camera axes
        c["up_dc3"] = np.array([-up_cv[0], -up_cv[1], up_cv[2]])
        c["up_dc3"] /= np.linalg.norm(c["up_dc3"])
        c["tilt_deg"] = float(np.degrees(np.arccos(abs(c["up_dc3"][1]))))
    return cams


def load_gt(annot_path, cam_id):
    """-> (annot3 (F,28,3) mm CV-camera, annot2 (F,28,2) px)."""
    m = scipy.io.loadmat(annot_path)
    a3 = m["annot3"][cam_id, 0].reshape(-1, NUM_GT_JOINTS, 3).astype(np.float64)
    a2 = m["annot2"][cam_id, 0].reshape(-1, NUM_GT_JOINTS, 2).astype(np.float64)
    return a3, a2


def gt_to_dc3(a3_mm):
    """3DHP camera-space mm (OpenCV axes) -> DC3 camera space, metres.

    X = -X_cv (image right -> player-left is -X), Y = -Y_cv (image down -> up
    positive), Z unchanged. Same transform as bench_model_z.gt_to_dc3 minus the
    world->camera step 3DHP has already done for us.
    """
    out = np.empty_like(a3_mm)
    out[..., 0] = -a3_mm[..., 0]
    out[..., 1] = -a3_mm[..., 1]
    out[..., 2] = a3_mm[..., 2]
    return out / 1000.0


def gt_subset(gt):
    """(F,28,3) -> (F,len(SUBSET),3), derived joints built exactly like _remap.

    The three centre joints use the backend's own torso fractions (imported
    from pose_mediapipe, so the two sides cannot drift apart): they are NOT
    midpoints -- see the CENTRE JOINTS block there and bench_utd_mhad.py for
    the Kinect-convention measurement behind them. Building them the same way
    on both sides keeps this table measuring TRACKING; it also matters for the
    root-aligned columns and the foot rows, which re-anchor on HipCenter.
    """
    F = gt.shape[0]
    out = np.empty((F, len(SUBSET), 3))
    hip_mid = (gt[:, G_LHIP] + gt[:, G_RHIP]) * 0.5
    sho_mid = (gt[:, G_LSHO] + gt[:, G_RSHO]) * 0.5
    torso = sho_mid - hip_mid
    torso_len = np.linalg.norm(torso, axis=-1, keepdims=True)
    back = np.cross(torso, gt[:, G_LHIP] - gt[:, G_RHIP])
    back /= np.maximum(np.linalg.norm(back, axis=-1, keepdims=True), 1e-9)
    out[:, 0] = hip_mid + HIP_CENTER_UP * torso
    out[:, 1] = hip_mid + SPINE_UP * torso + (SPINE_BACK * torso_len) * back
    out[:, 2] = hip_mid + SHOULDER_CENTER_UP * torso
    out[:, 3] = gt[:, G_HEAD]
    for i, (_, g) in enumerate(DIRECT):
        out[:, 4 + i] = gt[:, g]
    return out


# ---------------------------------------------------------------------------
# Inference + production replay
# ---------------------------------------------------------------------------
def run_landmarker(video_path, cache_path, quick=0, infer_scale=0):
    """Production PoseLandmarker over the video, caching (world, img, vis, det).

    Frames are fed CONSECUTIVELY (see module docstring). infer_scale>0 resizes
    the frame before inference; landmark coordinates are normalised so the
    cached values are resolution-independent either way.
    """
    if os.path.exists(cache_path):
        d = np.load(cache_path)
        return {k: d[k] for k in d.files}

    be = MediaPipeBackend(MODEL_PATH, num_poses=1)
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        sys.exit(f"cannot open {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    period_ms = 1000.0 / fps

    world, img, vis, det = [], [], [], []
    i = 0
    while True:
        ok, frame = cap.read()
        if not ok or (quick and i >= quick):
            break
        if infer_scale:
            frame = cv2.resize(frame, (infer_scale, infer_scale))
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_img = be._mp.Image(image_format=be._mp.ImageFormat.SRGB, data=rgb)
        res = be._landmarker.detect_for_video(mp_img, int(i * period_ms))
        if res.pose_world_landmarks:
            wl, il = res.pose_world_landmarks[0], res.pose_landmarks[0]
            world.append([[l.x, l.y, l.z] for l in wl])
            img.append([[l.x, l.y] for l in il])
            vis.append([getattr(l, "visibility", 1.0) for l in il])
            det.append(True)
        else:
            world.append(np.zeros((33, 3))); img.append(np.zeros((33, 2)))
            vis.append(np.zeros(33)); det.append(False)
        i += 1
        if i % 500 == 0:
            print(f"    {i} frames...", flush=True)
    cap.release(); be.close()

    out = dict(world=np.asarray(world, dtype=np.float64),
               img=np.asarray(img, dtype=np.float64),
               vis=np.asarray(vis, dtype=np.float64),
               det=np.asarray(det, dtype=bool),
               fps=np.float64(fps))
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    np.savez_compressed(cache_path, **out)
    return out


def pp_corrected_img(cache_img, cam):
    """Shift normalised landmark pixels so the true principal point lands at the
    image centre -- the geometry pose_mediapipe._absolute_root assumes. See the
    PRINCIPAL POINT paragraph in the module docstring."""
    W, H = cam["W"], cam["H"]
    u = cache_img[..., 0] * W - cam["K"][0, 2] + W * 0.5
    v = cache_img[..., 1] * H - cam["K"][1, 2] + H * 0.5
    return np.stack([u / W, v / H], axis=-1)


def backend_joints(geom_be, cache, img_pp, cam):
    """Replay the exact production per-frame pipeline (process() lines 310-324).

    Returns (F,20,3) DC3 joints, (F,) valid mask, and (F,4,3) raw camera-space
    ankle/heel/toe landmarks per side -- the raw feet are needed for the pitch
    study and are produced by the SAME axis flip + root translation _remap uses
    (pose_mediapipe.py:404-407), not by a second convention.
    """
    F = len(cache["det"])
    joints = np.full((F, NUM_DC3_JOINTS, 3), np.nan)
    raw = np.full((F, 6, 3), np.nan)   # L_ank, L_heel, L_toe, R_ank, R_heel, R_toe
    valid = np.zeros(F, dtype=bool)
    raw_idx = [MP_L_ANKLE, L_HEEL, MP_L_TOE, MP_R_ANKLE, R_HEEL, MP_R_TOE]
    for i in range(F):
        if not cache["det"][i]:
            continue
        world, image_xy, vis = cache["world"][i], img_pp[i], cache["vis"][i]
        root = geom_be._absolute_root(world, image_xy, vis, cam["W"], cam["H"])
        if root is None:
            continue
        j, _c = geom_be._remap(world, vis, root)
        joints[i] = j
        for k, li in enumerate(raw_idx):
            raw[i, k] = [-world[li, 0] + root[0], -world[li, 1] + root[1],
                         world[li, 2] + root[2]]
        valid[i] = True
    return joints, valid, raw


# ---------------------------------------------------------------------------
# Foot-pitch geometry
# ---------------------------------------------------------------------------
def pitch_deg(vec, up):
    """Signed elevation of `vec` above the horizontal plane, in degrees.

    Negative = the far end points DOWN (toe-down). Measured against the supplied
    gravity `up` rather than the camera Y axis, so a tilted camera cannot fake a
    pitch. Shape-agnostic: vec (...,3), up (3,)."""
    n = np.linalg.norm(vec, axis=-1)
    with np.errstate(invalid="ignore", divide="ignore"):
        return np.degrees(np.arcsin(np.clip((vec @ up) / n, -1.0, 1.0)))


def height_above(points, up, floor):
    """Gravity-aligned height of `points` above the fitted floor PLANE.

    `floor` is (a, b, c) with plane height = a*x + b*z + c. A plane rather than
    a scalar because a single percentile is set by outliers: on S1/Seq1 the 1st
    percentile of toe height sat 6 cm below the standing floor and starved the
    strict stance mask to 16 frames. The fitted plane's residual sd is 1.5 cm
    and its tilt 0.12 deg, which independently confirms the gravity vector."""
    a, b, c = floor
    return points @ up - (a * points[..., 0] + b * points[..., 2] + c)


def gt_speed(ankle_xyz):
    v = np.full(len(ankle_xyz), np.inf)
    d = np.linalg.norm(np.diff(ankle_xyz, axis=0), axis=1)
    if len(d):
        v[1:] = d; v[0] = d[0]
    return v


def stance_mask(ankle_xyz, toe_xyz, up, floor_h):
    """Frames where this foot is PLANTED: GT ankle speed below STANCE_SPEED and
    the toe within STANCE_FLOOR of the floor. Both conditions matter -- a slow
    foot in the air is not stance, and a low foot mid-swing is not either.
    Includes tiptoe; use flat_mask for sole-down frames only."""
    v = gt_speed(ankle_xyz)
    low = height_above(toe_xyz, up, floor_h) < STANCE_FLOOR
    return (v < STANCE_SPEED) & low & np.isfinite(ankle_xyz).all(axis=1)


def flat_mask(ankle_xyz, toe_xyz, up, floor_h):
    """Strict SOLE-DOWN stance: still, toe on the floor, and the ankle low
    enough that the foot cannot be on tiptoe. GT-only, see FLAT_ANKLE_MAX."""
    v = gt_speed(ankle_xyz)
    return ((v < FLAT_SPEED)
            & (height_above(toe_xyz, up, floor_h) < FLAT_TOE_MAX)
            & (height_above(ankle_xyz, up, floor_h) < FLAT_ANKLE_MAX)
            & np.isfinite(ankle_xyz).all(axis=1))


def fit_floor(points, up):
    """Least-squares floor plane (a, b, c), height = a*x + b*z + c, fitted to
    the lowest 15% of `points` (both toes over the whole sequence). Robust to
    the subject crouching or sitting, which a percentile is not."""
    h = points @ up
    ok = np.isfinite(h)
    points, h = points[ok], h[ok]
    lo = h < np.percentile(h, 15)
    A = np.c_[points[lo, 0], points[lo, 2], np.ones(lo.sum())]
    coef, *_ = np.linalg.lstsq(A, h[lo], rcond=None)
    resid = h[lo] - A @ coef
    return tuple(coef), float(resid.std()), float(
        np.degrees(np.arctan(np.hypot(coef[0], coef[1]))))


def floor_height(gt, up):
    """Fitted floor plane from both GT toes. Returns (a, b, c)."""
    pts = np.concatenate([gt[:, G_LTOE], gt[:, G_RTOE]])
    return fit_floor(pts, up)[0]


# ---------------------------------------------------------------------------
# Reporting helpers
# ---------------------------------------------------------------------------
def stats(err, name):
    e = err[np.isfinite(err)]
    if len(e) == 0:
        print(f"  {name:34s} (no data)"); return
    print(f"  {name:34s} mean {e.mean():.4f}  p50 {np.percentile(e,50):.4f}  "
          f"p90 {np.percentile(e,90):.4f}  max {e.max():.4f}")


def err_triplet(pred, gt):
    """-> (3D, |dz|, |dxy|) for matching (...,3) arrays."""
    return (np.linalg.norm(pred - gt, axis=-1),
            np.abs(pred[..., 2] - gt[..., 2]),
            np.linalg.norm(pred[..., :2] - gt[..., :2], axis=-1))


# ---------------------------------------------------------------------------
def eval_sequence(seq, gt_dir, cache_dir, cam_id, quick, infer_scale):
    seq_dir = os.path.join(gt_dir, seq)
    cams = parse_calibration(os.path.join(seq_dir, "camera.calibration"))
    cam = cams[cam_id]
    a3_mm, a2_px = load_gt(os.path.join(seq_dir, "annot.mat"), cam_id)

    print(f"  camera {cam_id}: {cam['W']}x{cam['H']}, fx {cam['K'][0,0]:.1f} "
          f"fy {cam['K'][1,1]:.1f}, pp ({cam['K'][0,2]:.1f},{cam['K'][1,2]:.1f}), "
          f"hfov {cam['hfov_deg']:.2f} deg (backend default 58.51)")
    print(f"  gravity: DC3 up = ({cam['up_dc3'][0]:+.4f},{cam['up_dc3'][1]:+.4f},"
          f"{cam['up_dc3'][2]:+.4f}), {cam['tilt_deg']:.2f} deg off camera Y")

    # ---- GATE 0: annot3 really is camera-space, K reprojects onto annot2 ----
    p = (cam["K"] @ a3_mm[0].T).T
    rep = np.linalg.norm(p[:, :2] / p[:, 2:3] - a2_px[0], axis=1)
    print(f"  GATE 0 annot3->K->annot2 reprojection: median {np.median(rep):.4f} px, "
          f"max {rep.max():.3f} px  (must be sub-pixel or the frame is wrong)")

    video = os.path.join(seq_dir, f"video_{cam_id}.avi")
    print("  landmarking (cached after first run)...", flush=True)
    cache = run_landmarker(video, os.path.join(cache_dir, f"{seq}_cam{cam_id}.npz"),
                           quick, infer_scale)

    F = min(len(cache["det"]), len(a3_mm))
    a3_mm, a2_px = a3_mm[:F], a2_px[:F]
    for k in ("world", "img", "vis", "det"):
        cache[k] = cache[k][:F]
    gt = gt_to_dc3(a3_mm)                      # (F,28,3) metres, DC3 axes
    img_pp = pp_corrected_img(cache["img"], cam)

    # ---- GATE 1: GT 2D vs MediaPipe 2D on torso joints ---------------------
    det = cache["det"]
    mp_px = cache["img"] * np.array([cam["W"], cam["H"]])
    d = [np.linalg.norm(mp_px[i, a] - a2_px[i, b])
         for i in np.flatnonzero(det) for a, b in GATE_PAIRS]
    torso = np.linalg.norm(a2_px[:, G_NECK] - a2_px[:, G_PELVIS], axis=1)
    print(f"  GATE 1 GT-2D vs MediaPipe-2D (shoulders/hips/knees): median "
          f"{np.median(d):.1f} px of {cam['W']} "
          f"(= {np.median(d)/np.median(torso)*100:.0f}% of median torso length "
          f"{np.median(torso):.0f} px) -- inflated by the Captury/BlazePose "
          f"definitional offset, see docstring caveat (b)")
    print(f"  GATE 2 detection rate: {det.mean()*100:.1f}% of {F} frames")

    geom_be = MediaPipeBackend(MODEL_PATH, num_poses=1, hfov_deg=cam["hfov_deg"])
    joints, valid, raw = backend_joints(geom_be, cache, img_pp, cam)
    geom_be.close()

    # ---- GATE 3: root-relative world-landmark MPJPE ------------------------
    hipc = (gt[:, G_LHIP] + gt[:, G_RHIP]) * 0.5
    rel = []
    for mp_i, g_i in [(MP_LSHO, G_LSHO), (MP_RSHO, G_RSHO), (MP_LHIP, G_LHIP),
                      (MP_RHIP, G_RHIP), (MP_LKNE, G_LKNE), (MP_RKNE, G_RKNE),
                      (MP_L_ANKLE, G_LANK), (MP_R_ANKLE, G_RANK)]:
        w = cache["world"][:, mp_i, :]
        pr = np.stack([-w[:, 0], -w[:, 1], w[:, 2]], axis=1)
        rel.append(np.linalg.norm(pr - (gt[:, g_i] - hipc), axis=1))
    rel_mpjpe = np.nanmean(np.where(det, np.stack(rel, 1).mean(1), np.nan))
    print(f"  GATE 3 root-relative world-landmark MPJPE {rel_mpjpe*1000:.0f} mm "
          f"(BlazePose GHUM full publishes ~35-90 mm on COCO-style GT; higher "
          f"here is expected, same definitional offset)")

    return dict(gt=gt, joints=joints, valid=valid & np.isfinite(gt).all(axis=(1, 2)),
                raw=raw, cam=cam, det=det, F=F)


def report_gt_geometry(per_seq):
    """What ARE the GT foot joints? Printed before any error number, because the
    ankle/foot/toe geometry decides which one our blend should be compared to."""
    print("\n" + "=" * 78)
    print("GT FOOT SEGMENT GEOMETRY (what 'foot' and 'toe' actually are)")
    print("=" * 78)
    for s, r in per_seq.items():
        gt, up = r["gt"], r["cam"]["up_dc3"]
        for side, (A, FO, T) in (("L", (G_LANK, G_LFOOT, G_LTOE)),
                                 ("R", (G_RANK, G_RFOOT, G_RTOE))):
            ank, foo, toe = gt[:, A], gt[:, FO], gt[:, T]
            print(f"  {s} {side}: |ank->foot| {np.linalg.norm(foo-ank,axis=1).mean():.4f} m   "
                  f"|ank->toe| {np.linalg.norm(toe-ank,axis=1).mean():.4f} m   "
                  f"|foot->toe| {np.linalg.norm(toe-foo,axis=1).mean():.4f} m   "
                  f"height(toe)-height(ank) {np.mean((toe-ank)@up):+.4f} m")
    print("  => 'foot' is the ball of the foot, 'toe' the toe tip ~2.7 cm further")
    print("     out. NEITHER is a heel, so our heel-biased blend (FOOT_TOE_BLEND")
    print("     = %.2f, i.e. 65%% heel) is expected to sit BEHIND both." % FOOT_TOE_BLEND)


def report_joint_table(per_seq):
    print("\n" + "=" * 78)
    print("PER-JOINT ERROR (metres) -- absolute | root-aligned (subtract own HipCenter)")
    print("  HAND and FOOT rows are the joints AIST++ could not score.")
    print("=" * 78)
    print(f"  {'joint':22s} {'abs3D':>7s} {'abs|dz|':>8s} {'abs|dxy|':>9s} "
          f"{'ra3D':>7s} {'ra|dz|':>7s} {'ra|dxy|':>8s}")

    acc = {}

    def push(key, pred, gtj, palpred, palgt):
        d3, dz, dxy = err_triplet(pred, gtj)
        r3, rz, rxy = err_triplet(palpred, palgt)
        a = acc.setdefault(key, [[] for _ in range(6)])
        for i, v in enumerate((d3, dz, dxy, r3, rz, rxy)):
            a[i].append(v)

    for s, r in per_seq.items():
        gt, joints, m = r["gt"], r["joints"], r["valid"]
        gts = gt_subset(gt)
        pred = joints[:, SUBSET, :][m]
        gtsm = gts[m]
        pal = pred - pred[:, 0:1, :] + gtsm[:, 0:1, :]
        for j, nm in enumerate(SUBSET_NAMES):
            push(nm, pred[:, j], gtsm[:, j], pal[:, j], gtsm[:, j])
        # Feet: our one blended joint against both GT candidates.
        hipc_p = joints[m][:, HIP_CENTER]
        hipc_g = gts[m][:, 0]
        for label, dj, gj in FOOT_VARIANTS:
            p, g = joints[m][:, dj], gt[m][:, gj]
            push(label, p, g, p - hipc_p + hipc_g, g)

    order = SUBSET_NAMES + [lbl for lbl, _, _ in FOOT_VARIANTS]
    for nm in order:
        a = [np.concatenate(x) for x in acc[nm]]
        mark = " <<<" if ("Hand" in nm or "Foot" in nm) else ""
        print(f"  {nm:22s} " + " ".join(f"{np.nanmean(v):>{w}.4f}"
              for v, w in zip(a, (7, 8, 9, 7, 7, 8))) + mark)
    return acc


def report_hand_increment(per_seq, acc):
    """The hand INCREMENT: how much error our synthesised knuckle-midpoint hand
    ADDS over the wrist it is built from, on the SAME frames. The arm chain's
    shared Captury/BlazePose definitional offset largely cancels in this
    difference, so it isolates the cost of the hand construction itself."""
    print("\n" + "=" * 78)
    print("HAND INCREMENT -- our Hand error MINUS our Wrist error, same frames")
    print("=" * 78)
    for side, wname, hname in (("Left", "WristLeft", "HandLeft"),
                               ("Right", "WristRight", "HandRight")):
        w3 = np.concatenate(acc[wname][0]); h3 = np.concatenate(acc[hname][0])
        wr = np.concatenate(acc[wname][3]); hr = np.concatenate(acc[hname][3])
        print(f"  {side:5s}  absolute 3D  wrist {np.nanmean(w3):.4f} -> hand "
              f"{np.nanmean(h3):.4f}   increment {np.nanmean(h3-w3):+.4f} m")
        print(f"  {side:5s}  root-aligned wrist {np.nanmean(wr):.4f} -> hand "
              f"{np.nanmean(hr):.4f}   increment {np.nanmean(hr-wr):+.4f} m")
    # Is our hand the right DISTANCE past the wrist?
    print("\n  wrist->hand segment length, ours vs GT (mean over valid frames):")
    for s, r in per_seq.items():
        gt, j, m = r["gt"], r["joints"], r["valid"]
        for side, (dw, dh, gw, gh) in (("L", (WRIST_LEFT, HAND_LEFT, G_LWRI, G_LHAND)),
                                       ("R", (WRIST_RIGHT, HAND_RIGHT, G_RWRI, G_RHAND))):
            ours = np.linalg.norm(j[m][:, dh] - j[m][:, dw], axis=1).mean()
            theirs = np.linalg.norm(gt[m][:, gh] - gt[m][:, gw], axis=1).mean()
            print(f"    {s} {side}: ours {ours:.4f} m   GT {theirs:.4f} m   "
                  f"delta {ours-theirs:+.4f} m")


def report_segment_lengths(per_seq):
    """End-effector segment LENGTHS: ours vs 3DHP GT vs the baked Kinect frame.

    Length is the convention-invariant quantity here -- the Kinect reference is
    a single captured pose, so its DIRECTION is that pose's, but how far the
    hand sits past the wrist and the foot below the ankle is the convention.
    """
    print("\n" + "=" * 78)
    print("END-EFFECTOR SEGMENT LENGTHS (m): ours vs 3DHP GT vs baked Kinect")
    print("=" * 78)
    kh = np.mean([np.linalg.norm(v) for v in KINECT_WRIST_HAND.values()])
    kf = np.mean([np.linalg.norm(v) for v in KINECT_ANKLE_FOOT.values()])
    print(f"  Kinect (StubCameraInput baked frame): wrist->hand "
          f"{np.linalg.norm(KINECT_WRIST_HAND['L']):.4f}/"
          f"{np.linalg.norm(KINECT_WRIST_HAND['R']):.4f} (mean {kh:.4f}), "
          f"ankle->foot {np.linalg.norm(KINECT_ANKLE_FOOT['L']):.4f}/"
          f"{np.linalg.norm(KINECT_ANKLE_FOOT['R']):.4f} (mean {kf:.4f})")
    for s, r in per_seq.items():
        gt, j, raw, m = r["gt"], r["joints"], r["raw"], r["valid"]
        for side, (dw, dh, gw, gh) in (("L", (WRIST_LEFT, HAND_LEFT, G_LWRI, G_LHAND)),
                                       ("R", (WRIST_RIGHT, HAND_RIGHT, G_RWRI, G_RHAND))):
            ours = np.linalg.norm(j[m][:, dh] - j[m][:, dw], axis=1).mean()
            theirs = np.linalg.norm(gt[m][:, gh] - gt[m][:, gw], axis=1).mean()
            print(f"  {s} {side} wrist->hand: ours {ours:.4f}  3DHP GT {theirs:.4f}  "
                  f"Kinect {np.linalg.norm(KINECT_WRIST_HAND[side]):.4f}   "
                  f"ours is {ours-theirs:+.4f} vs GT, "
                  f"{ours-np.linalg.norm(KINECT_WRIST_HAND[side]):+.4f} vs Kinect")
        for side, (dj, k0, gA, gFO, gT) in (
                ("L", (FOOT_LEFT, 0, G_LANK, G_LFOOT, G_LTOE)),
                ("R", (FOOT_RIGHT, 3, G_RANK, G_RFOOT, G_RTOE))):
            ours = np.linalg.norm(j[m][:, dj] - raw[m, k0], axis=1).mean()
            g_ball = np.linalg.norm(gt[m][:, gFO] - gt[m][:, gA], axis=1).mean()
            g_toe = np.linalg.norm(gt[m][:, gT] - gt[m][:, gA], axis=1).mean()
            print(f"  {s} {side} ankle->foot: ours {ours:.4f}  3DHP ankle->ball "
                  f"{g_ball:.4f}  ankle->toe {g_toe:.4f}  "
                  f"Kinect {np.linalg.norm(KINECT_ANKLE_FOOT[side]):.4f}")

    print("\n  Which FOOT_TOE_BLEND reproduces the Kinect ankle->foot LENGTH "
          f"({kf:.4f} m)?")
    print(f"    {'blend':>6s} {'|ankle->foot| ours':>20s} {'vs Kinect':>12s}")
    for b in (0.0, 0.15, 0.35, 0.5, 0.75, 1.0):
        vals = []
        for s, r in per_seq.items():
            raw, m = r["raw"], r["valid"]
            for k0 in (0, 3):
                p = raw[m, k0 + 1] * (1 - b) + raw[m, k0 + 2] * b
                vals.append(np.linalg.norm(p - raw[m, k0], axis=1).mean())
        v = float(np.mean(vals))
        print(f"    {b:6.2f} {v:20.4f} {v-kf:+12.4f}")


def report_offset_error(per_seq):
    """THE DECISION METRIC for FOOT_TOE_BLEND and any pitch correction.

    Scores the end-effector OFFSET VECTOR -- (our Hand - our Wrist) against
    (GT hand - GT wrist), and (our Foot - our Ankle) against (GT ball - GT
    ankle) -- instead of the absolute joint. This is the only view in which the
    construction is actually visible: absolute per-joint error on this dataset
    is ~0.40 m and dominated by root recovery plus the Captury/BlazePose
    definitional offset, both of which are IDENTICAL for the parent and the
    child joint and therefore cancel exactly in the difference. A 4 cm blend
    change moves the absolute number by 0.005 m (invisible) and this one by its
    full magnitude.
    """
    print("\n" + "=" * 78)
    print("END-EFFECTOR OFFSET ERROR -- the construction, isolated")
    print("  err = |(ours_child - ours_parent) - (gt_child - gt_parent)|, metres.")
    print("  Root error and the shared parent/child definitional offset cancel.")
    print("=" * 78)
    for s, r in per_seq.items():
        gt, j, m = r["gt"], r["joints"], r["valid"]
        for side, (dw, dh, gw, gh) in (("L", (WRIST_LEFT, HAND_LEFT, G_LWRI, G_LHAND)),
                                       ("R", (WRIST_RIGHT, HAND_RIGHT, G_RWRI, G_RHAND))):
            e = np.linalg.norm((j[m][:, dh] - j[m][:, dw])
                               - (gt[m][:, gh] - gt[m][:, gw]), axis=1)
            print(f"  {s} {side} HAND offset err  mean {e.mean():.4f}  p50 "
                  f"{np.median(e):.4f}  p90 {np.percentile(e,90):.4f} m")
        for side, (dj, k0, gA, gFO, gT) in (
                ("L", (FOOT_LEFT, 0, G_LANK, G_LFOOT, G_LTOE)),
                ("R", (FOOT_RIGHT, 3, G_RANK, G_RFOOT, G_RTOE))):
            o = j[m][:, dj] - r["raw"][m, k0]
            for gname, gj in (("ball", gFO), ("toe ", gT)):
                e = np.linalg.norm(o - (gt[m][:, gj] - gt[m][:, gA]), axis=1)
                print(f"  {s} {side} FOOT offset err vs GT {gname}  mean {e.mean():.4f}"
                      f"  p50 {np.median(e):.4f}  p90 {np.percentile(e,90):.4f} m")

    # -- lever 1: FOOT_TOE_BLEND -------------------------------------------
    print(f"\n  FOOT_TOE_BLEND sweep on the OFFSET metric (current {FOOT_TOE_BLEND}):")
    print(f"    {'blend':>6s} {'vs GT ball':>12s} {'vs GT toe':>12s} "
          f"{'|ank->foot|':>12s} {'vs Kinect len':>14s}")
    kf = np.mean([np.linalg.norm(v) for v in KINECT_ANKLE_FOOT.values()])
    best = None
    for b in (0.0, 0.15, 0.25, 0.35, 0.5, 0.65, 0.8, 1.0):
        eb, et, ln = [], [], []
        for s, r in per_seq.items():
            gt, raw, m = r["gt"], r["raw"], r["valid"]
            for k0, gA, gFO, gT in ((0, G_LANK, G_LFOOT, G_LTOE),
                                    (3, G_RANK, G_RFOOT, G_RTOE)):
                o = (raw[m, k0 + 1] * (1 - b) + raw[m, k0 + 2] * b) - raw[m, k0]
                eb.append(np.linalg.norm(o - (gt[m][:, gFO] - gt[m][:, gA]), axis=1))
                et.append(np.linalg.norm(o - (gt[m][:, gT] - gt[m][:, gA]), axis=1))
                ln.append(np.linalg.norm(o, axis=1))
        mb = np.concatenate(eb).mean(); mt = np.concatenate(et).mean()
        ml = np.concatenate(ln).mean()
        print(f"    {b:6.2f} {mb:12.4f} {mt:12.4f} {ml:12.4f} {ml-kf:+14.4f}")
        if best is None or mb < best[1]:
            best = (b, mb)
    print(f"    => blend minimising offset error vs GT ball: {best[0]:.2f} "
          f"({best[1]:.4f} m); current {FOOT_TOE_BLEND} is "
          f"{'optimal' if abs(best[0]-FOOT_TOE_BLEND) < 1e-9 else 'not optimal'}")

    # -- lever 0: hand extension -------------------------------------------
    # Our HandLeft/Right is the raw pinky/index knuckle midpoint. Both GT
    # conventions put the hand joint FURTHER out than that, so sweep a scale on
    # the wrist->knuckle vector: hand = wrist + k * (knuckle_mid - wrist).
    print("\n  HAND-EXTENSION sweep (hand = wrist + k*(knuckle_mid - wrist); "
          "k=1.0 is current):")
    kh = np.mean([np.linalg.norm(v) for v in KINECT_WRIST_HAND.values()])
    print(f"    {'k':>5s} {'offset err vs GT':>17s} {'|wrist->hand|':>14s} "
          f"{'vs Kinect len':>14s}")
    # k < 1 SHORTENS the offset. Swept because UTD-MHAD (bench_utd_mhad.py)
    # finds Kinect's own hand joint only 0.068 m past the wrist and prefers
    # k ~ 0.7, in the opposite direction from this corpus's anatomy: the two
    # targets disagree, so both sides of k=1 have to be visible here.
    for k in (0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.4, 1.5, 1.6, 1.8, 2.0):
        errs, lens = [], []
        for s, r in per_seq.items():
            gt, j, m = r["gt"], r["joints"], r["valid"]
            for dw, dh, gw, gh in ((WRIST_LEFT, HAND_LEFT, G_LWRI, G_LHAND),
                                   (WRIST_RIGHT, HAND_RIGHT, G_RWRI, G_RHAND)):
                o = (j[m][:, dh] - j[m][:, dw]) * k
                errs.append(np.linalg.norm(o - (gt[m][:, gh] - gt[m][:, gw]), axis=1))
                lens.append(np.linalg.norm(o, axis=1))
        e = np.concatenate(errs).mean(); ln = np.concatenate(lens).mean()
        print(f"    {k:5.2f} {e:17.4f} {ln:14.4f} {ln-kh:+14.4f}")

    # -- IS THE TOE-DOWN BIAS GLOBAL, OR ONLY IN STANCE? --------------------
    # Decides the shape of any fix. A GLOBAL bias is a rig/landmark offset and a
    # constant rotation cures it. A STANCE-ONLY bias means the model simply does
    # not resolve ground contact, and a constant rotation would damage the swing
    # and kick frames the choreography actually scores.
    print("\n  Paired ankle->toe pitch delta (ours - GT), by frame class:")
    for tag, use_stance in (("ALL valid frames", False), ("stance only", True)):
        ds = []
        for s, r in per_seq.items():
            gt, raw, m, up = r["gt"], r["raw"], r["valid"], r["cam"]["up_dc3"]
            fh = floor_height(gt, up)
            for k0, gA, gT in ((0, G_LANK, G_LTOE), (3, G_RANK, G_RTOE)):
                sel = (stance_mask(gt[:, gA], gt[:, gT], up, fh) & m) if use_stance else m
                ds.append(pitch_deg(raw[sel, k0 + 2] - raw[sel, k0], up)
                          - pitch_deg(gt[sel, gT] - gt[sel, gA], up))
        d = np.concatenate(ds)
        print(f"    {tag:18s} n={len(d):6d}  mean {d.mean():+7.2f}  p50 "
              f"{np.median(d):+7.2f}  sd {d.std():6.2f} deg")

    # -- lever 2: pitch correction ------------------------------------------
    print("\n  PITCH-CORRECTION sweep on the OFFSET metric (rotate our whole foot")
    print("  segment about the medio-lateral axis; blend held at current value).")
    print("  Swept on ALL frames and on STANCE frames separately -- if a value")
    print("  helps stance but hurts overall, a constant rotation is the wrong fix.")
    print(f"    {'deg':>6s} {'all: ball':>11s} {'all: toe':>11s} "
          f"{'stance: ball':>13s} {'stance: toe':>12s}")
    for corr_deg in (0, 5, 10, 15, 20, 22, 25, 30):
        cols = []
        for use_stance in (False, True):
            eb, et = [], []
            for s, r in per_seq.items():
                gt, raw, m, up = r["gt"], r["raw"], r["valid"], r["cam"]["up_dc3"]
                fh = floor_height(gt, up)
                for k0, gA, gFO, gT in ((0, G_LANK, G_LFOOT, G_LTOE),
                                        (3, G_RANK, G_RFOOT, G_RTOE)):
                    sel = (stance_mask(gt[:, gA], gt[:, gT], up, fh) & m) if use_stance else m
                    ank, heel, toe = raw[sel, k0], raw[sel, k0 + 1], raw[sel, k0 + 2]
                    hc, tc = rotate_foot(ank, heel, toe, up, math.radians(corr_deg))
                    o = (hc * (1 - FOOT_TOE_BLEND) + tc * FOOT_TOE_BLEND) - ank
                    eb.append(np.linalg.norm(o - (gt[sel][:, gFO] - gt[sel][:, gA]), axis=1))
                    et.append(np.linalg.norm(o - (gt[sel][:, gT] - gt[sel][:, gA]), axis=1))
            cols += [np.concatenate(eb).mean(), np.concatenate(et).mean()]
        print(f"    {corr_deg:6d} {cols[0]:11.4f} {cols[1]:11.4f} "
              f"{cols[2]:13.4f} {cols[3]:12.4f}")

    # -- our FootLeft/Right offset vs the KINECT convention ------------------
    # Kinect's foot joint is ~4.4 cm essentially straight DOWN from the ankle.
    # Length was compared above; this is the DIRECTION.
    print("\n  Our ankle->Foot offset vs the Kinect convention (pitch below "
          "horizontal;")
    print("  Kinect baked frame: L %.1f deg, R %.1f deg -- i.e. nearly straight down):"
          % tuple(math.degrees(math.asin(v[1] / np.linalg.norm(v)))
                  for v in (KINECT_ANKLE_FOOT["L"], KINECT_ANKLE_FOOT["R"])))
    for b in (0.0, 0.35, 0.65):
        ps = []
        for s, r in per_seq.items():
            raw, m, up = r["raw"], r["valid"], r["cam"]["up_dc3"]
            for k0 in (0, 3):
                o = (raw[m, k0 + 1] * (1 - b) + raw[m, k0 + 2] * b) - raw[m, k0]
                ps.append(pitch_deg(o, up))
        p = np.concatenate(ps)
        print(f"    blend {b:4.2f}: our ankle->Foot pitch {p.mean():+7.2f} deg "
              f"(p50 {np.median(p):+7.2f})")


def rotate_foot(ank, heel, toe, up, corr):
    """Rotate the heel and toe about the per-frame medio-lateral axis through
    the ankle by `corr` radians, POSITIVE = TOE UP. The axis is up x forward,
    forward being the horizontal component of heel->toe, so the correction is a
    pure pitch in each frame's own sagittal plane and never yaws the foot.

    The sign is asserted, not assumed: rotating about (up x fwd) by +theta tilts
    the toe DOWN, so `corr` is negated below. tests: a foot with ankle->toe pitch
    -30.96 deg becomes -10.96 at corr=+20 and -50.96 at corr=-20.
    """
    fwd = toe - heel
    fwd = fwd - (fwd @ up)[:, None] * up[None, :]
    n = np.linalg.norm(fwd, axis=1, keepdims=True)
    fwd = np.divide(fwd, n, out=np.zeros_like(fwd), where=n > 1e-6)
    lat = np.cross(up[None, :], fwd)
    c, sn = math.cos(-corr), math.sin(-corr)

    def rot(v):
        return (v * c + np.cross(lat, v) * sn
                + lat * ((lat * v).sum(1, keepdims=True)) * (1 - c))

    return ank + rot(heel - ank), ank + rot(toe - ank)


def report_foot_pitch(per_seq):
    """THE toe-down question. Compares the ankle->toe segment PITCH (an angle,
    invariant to where each convention puts the ankle origin) between GT and our
    prediction, restricted to frames where GT says the foot is planted."""
    print("\n" + "=" * 78)
    print("FOOT PITCH during GT-verified STANCE (ankle speed < %.3f m/frame AND"
          % STANCE_SPEED)
    print("  toe within %.2f m of floor).  Pitch = elevation of the ankle->toe"
          % STANCE_FLOOR)
    print("  segment above horizontal, measured against GRAVITY. Negative = toe down.")
    print("=" * 78)
    print("  Reported PAIRED (ours minus GT on the SAME frame): a mask that lets")
    print("  in a tiptoe pose inflates both sides equally, so the paired delta")
    print("  survives contamination the raw means would not.")
    out = {}
    for tag, mkfn in (("planted (incl. tiptoe)", stance_mask),
                      ("sole-down only", flat_mask)):
        agg = {k: [] for k in ("gt", "ours", "delta", "gt_dh", "ours_dh", "heel_dh")}
        print(f"\n  --- mask: {tag} ---")
        for s, r in per_seq.items():
            gt, raw, m, up = r["gt"], r["raw"], r["valid"], r["cam"]["up_dc3"]
            fh = floor_height(gt, up)
            for side, (gA, gT, k0) in (("L", (G_LANK, G_LTOE, 0)),
                                       ("R", (G_RANK, G_RTOE, 3))):
                st = mkfn(gt[:, gA], gt[:, gT], up, fh) & m
                if st.sum() < 20:
                    print(f"    {s} {side}: only {st.sum()} frames, skipped"); continue
                g_pitch = pitch_deg(gt[st, gT] - gt[st, gA], up)
                o_ank, o_heel, o_toe = raw[st, k0], raw[st, k0 + 1], raw[st, k0 + 2]
                o_pitch = pitch_deg(o_toe - o_ank, up)
                dpitch = o_pitch - g_pitch
                g_dh = (gt[st, gT] - gt[st, gA]) @ up
                o_dh = (o_toe - o_ank) @ up
                o_hdh = (o_toe - o_heel) @ up
                print(f"    {s} {side} n={st.sum():5d}  GT {g_pitch.mean():+7.2f} "
                      f"(sd {g_pitch.std():5.2f})  ours {o_pitch.mean():+7.2f} "
                      f"(sd {o_pitch.std():5.2f})  PAIRED delta {dpitch.mean():+7.2f} "
                      f"(sd {dpitch.std():5.2f}, p50 {np.median(dpitch):+.2f}) deg")
                print(f"        toe-minus-ankle height: GT {g_dh.mean():+.4f}  ours "
                      f"{o_dh.mean():+.4f}  excess drop {(o_dh-g_dh).mean():+.4f} m"
                      f"   |  ours toe-minus-HEEL {o_hdh.mean():+.4f} m")
                for k, v in (("gt", g_pitch), ("ours", o_pitch), ("delta", dpitch),
                             ("gt_dh", g_dh), ("ours_dh", o_dh), ("heel_dh", o_hdh)):
                    agg[k].append(v)
        if not agg["gt"]:
            print("    (no frames)"); continue
        a = {k: np.concatenate(v) for k, v in agg.items()}
        g, o, d = a["gt"], a["ours"], a["delta"]
        print(f"    ALL n={len(g)}")
        print(f"      GT   ankle->toe pitch {g.mean():+7.2f} deg "
              f"(p10 {np.percentile(g,10):+7.2f}  p50 {np.median(g):+7.2f}  "
              f"p90 {np.percentile(g,90):+7.2f})")
        print(f"      OURS ankle->toe pitch {o.mean():+7.2f} deg "
              f"(p10 {np.percentile(o,10):+7.2f}  p50 {np.median(o):+7.2f}  "
              f"p90 {np.percentile(o,90):+7.2f})")
        print(f"      EXCESS TOE-DOWN      {d.mean():+7.2f} deg paired "
              f"(p10 {np.percentile(d,10):+7.2f}  p50 {np.median(d):+7.2f}  "
              f"p90 {np.percentile(d,90):+7.2f});  {(d<0).mean()*100:.0f}% of frames "
              f"more toe-down than GT")
        print(f"      GT   toe-vs-ankle height {a['gt_dh'].mean():+.4f} m")
        print(f"      OURS toe-vs-ankle height {a['ours_dh'].mean():+.4f} m   "
              f"excess drop {(a['ours_dh']-a['gt_dh']).mean():+.4f} m")
        print(f"      OURS toe-vs-HEEL height  {a['heel_dh'].mean():+.4f} m  <= the "
              f"'~6.7 cm below heel' claim, re-measured here")
        out[tag] = a
    return out.get("sole-down only") or out.get("planted (incl. tiptoe)")


def report_flatness(per_seq):
    """(d) Does GT confirm planted feet are flat? A flat foot means the whole
    sole is on the floor: the toe sits at floor level and the ankle rides a
    fixed anatomical height above it."""
    print("\n" + "=" * 78)
    print("IS A PLANTED FOOT FLAT?  Heights above the floor in SOLE-DOWN stance")
    print("  GT heights are above the GT floor. OUR heights are above OUR OWN")
    print("  floor (1st percentile of our toe height) -- comparing our absolute")
    print("  height to the GT floor would just re-measure the root error, which")
    print("  the per-joint table already covers. Both columns are therefore")
    print("  intra-skeleton and directly comparable.")
    print("=" * 78)
    for s, r in per_seq.items():
        gt, raw, m, up = r["gt"], r["raw"], r["valid"], r["cam"]["up_dc3"]
        fh = floor_height(gt, up)
        ofh = fit_floor(np.concatenate([raw[m, 2], raw[m, 5]]), up)[0]
        for side, (gA, gFO, gT, k0) in (
                ("L", (G_LANK, G_LFOOT, G_LTOE, 0)),
                ("R", (G_RANK, G_RFOOT, G_RTOE, 3))):
            st = flat_mask(gt[:, gA], gt[:, gT], up, fh) & m
            if st.sum() < 20:
                continue
            ha = height_above(gt[st, gA], up, fh)
            hf = height_above(gt[st, gFO], up, fh)
            ht = height_above(gt[st, gT], up, fh)
            oa = height_above(raw[st, k0], up, ofh)
            oh = height_above(raw[st, k0 + 1], up, ofh)
            ot = height_above(raw[st, k0 + 2], up, ofh)
            print(f"  {s} {side} (n={st.sum()}):")
            print(f"    GT   ankle {ha.mean():+.4f}  ball {hf.mean():+.4f}  "
                  f"toe {ht.mean():+.4f} m above floor   "
                  f"(toe-minus-ball {ht.mean()-hf.mean():+.4f}: ~0 => sole flat)")
            print(f"    OURS ankle {oa.mean():+.4f}  heel {oh.mean():+.4f}  "
                  f"toe {ot.mean():+.4f} m above our floor   "
                  f"(toe-minus-heel {ot.mean()-oh.mean():+.4f})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gt-dir", default="/home/free/tmp/pose_gt_3dhp")
    ap.add_argument("--cache-dir", default=None)
    ap.add_argument("--seqs", nargs="*", default=["S1_Seq1"],
                    help="sequence dirs under --gt-dir, e.g. S1_Seq1 S2_Seq1")
    ap.add_argument("--camera", type=int, default=0,
                    help="camera id; 0 is the only chest-height vnect view that "
                         "keeps the feet in frame (see docstring)")
    ap.add_argument("--quick", type=int, default=0, help="limit frames (testing)")
    ap.add_argument("--infer-scale", type=int, default=0,
                    help="resize frames to NxN before inference (0 = native 2048)")
    ap.add_argument("--recompute", action="store_true")
    args = ap.parse_args()

    cache_dir = args.cache_dir or os.path.join(args.gt_dir, "cache")
    if args.recompute:
        for s in args.seqs:
            p = os.path.join(cache_dir, f"{s}_cam{args.camera}.npz")
            if os.path.exists(p):
                os.remove(p)

    per_seq = {}
    for s in args.seqs:
        print(f"\n=== {s} camera {args.camera} ===")
        per_seq[s] = eval_sequence(s, args.gt_dir, cache_dir, args.camera,
                                   args.quick, args.infer_scale)

    report_gt_geometry(per_seq)
    acc = report_joint_table(per_seq)
    report_hand_increment(per_seq, acc)
    report_segment_lengths(per_seq)
    report_offset_error(per_seq)
    report_foot_pitch(per_seq)
    report_flatness(per_seq)


if __name__ == "__main__":
    main()
