#!/usr/bin/env python3
"""Measure the PRODUCTION MediaPipe backend's HAND-joint 3D accuracy against
real triangulated 3D hand ground truth (CMU Panoptic Studio), on dance footage.

WHY: tools/pose_corpus/bench_model_z.py validated the backend's BODY joints
against AIST++ (COCO-17), but COCO-17 has no hands, so HandLeft/HandRight were
DROPPED from that evaluation entirely. The backend nevertheless synthesises them
(pose_mediapipe.py:_remap): HandL/R = midpoint of BlazePose's pinky (17/18) and
index (19/20) knuckle landmarks, roughly 8-10 cm past the wrist, on the claim
that "the knuckle line is about where Kinect's hand joint sits". That claim has
never been checked against a real 3D hand measurement. This script checks it.

The interesting question is not "what is the hand error" in isolation -- the
hand inherits the whole arm's error -- but the INCREMENT over the wrist: does
the synthesised 8 cm offset point the right way and have the right length, or
would we be better off leaving Hand at the wrist? So every hand number here is
reported next to the same-frame WRIST number, plus a direct offset-vector
comparison (direction cosine + length) that isolates placement from inherited
arm noise.

GROUND TRUTH: CMU Panoptic Studio (http://domedb.perception.cs.cmu.edu/),
sequence 170307_dance5 -- a solo dancer, which is exactly the motion class DC3
grades. Data is served over plain HTTP, no login:

    http://domedb.perception.cs.cmu.edu/webdata/dataset/170307_dance5/
        calibration_170307_dance5.json          (252 KB)
        hdHand3d.tar                            ( 64 MB)  3D hands
        hdPose3d_stage1_coco19.tar              ( 18 MB)  3D body
        videos/hd_shared_crf20/hd_00_21.mp4     (1.2 GB)  one HD view

  hdHand3d/handRecon3D_hd<8-digit>.json: people[].{left_hand,right_hand} with
    landmarks = 63 floats = 21 x XYZ in CENTIMETRES, dome world frame, in
    OpenPose hand order (0 = wrist; then thumb/index/middle/ring/pinky, each
    MCP, PIP, DIP, tip -- so index-MCP = 5, pinky-MCP = 17). Per-keypoint
    averageScore, averageReproError, validity, and visibility (the list of
    camera indices that saw it). Triangulated from ~500 cameras.
  hdPose3d_stage1_coco19/body3DScene_<8-digit>.json: bodies[].joints19 =
    [x,y,z,confidence] x 19, CENTIMETRES, dome world frame. Order per the
    toolbox README: 0 Neck, 1 Nose, 2 BodyCenter, 3-5 lShoulder/lElbow/lWrist,
    6-8 lHip/lKnee/lAnkle, 9-11 r arm, 12-14 r leg, 15-18 eyes/ears.
  calibration_*.json: per-camera K (3x3), distCoef (5, OpenCV [k1 k2 p1 p2 k3]),
    R (3x3), t (3, CENTIMETRES). World -> camera is X_cam = R @ X_world + t.

  Cite: Joo et al., "Panoptic Studio: A Massively Multiview System for Social
  Interaction Capture" (ICCV 2015 / TPAMI 2017); Simon et al., "Hand Keypoint
  Detection in Single Images using Multiview Bootstrapping" (CVPR 2017) for the
  hand annotations specifically. The dataset is free for research use but is
  NOT redistributable, which is why everything lives under
  ~/tmp/pose_gt_panoptic/ and NOTHING but this script is in the repo.

HD VIEW CHOICE: hd_00_21. Chosen by projecting the GT body + both hands into
all 31 HD cameras over the whole sequence and keeping the view with the highest
"every keypoint inside the frame" rate at a usable subject size (00_21: 93% of
frames fully in-frame, 100% of frames with BOTH hands in-frame, ~450 px subject
height). Bigger views (00_16, 00_11) crop limbs; every fully-in-frame view is
smaller. Reproduce with --pick-view.

FRAME ALIGNMENT: HD video frame index == the 8-digit index in the GT filenames
(the dataset's own convention -- hdImgs/00_21/00_21_<idx>.jpg is frame <idx> of
hd_00_21.mp4). GT exists for indices 161..11776. Verified, not assumed, by the
overlay gate below: a one-frame slip on a dancer moving at ~1 m/s shows up
immediately as a limb-width reprojection offset.

AXIS CONVENTIONS -- every flip spelled out, because a sign error here silently
poisons everything (see bench_model_z.py for the same war story):

  1. Panoptic world -> camera: X_cam = R @ X_world + t (OpenCV convention;
     panoptic-toolbox/python/panutils.py:projectPoints does exactly this, and
     it is what cv2.projectPoints implements, which is what we call).
  2. OpenCV camera axes: +X image right, +Y image DOWN, +Z into the scene.
  3. DC3 camera space (pose_mediapipe.py module docstring): +Y up, +Z away from
     camera, player-left = -X. So
         X_dc3 = -X_cv,  Y_dc3 = -Y_cv,  Z_dc3 = +Z_cv
     -- a 180 deg rotation about Z, determinant +1, handedness preserved. This
     is the identical transform bench_model_z.gt_to_dc3 applies to AIST++.
  4. Units: Panoptic centimetres -> metres (/100).
  5. Hand sides: Panoptic "left_hand" is the subject's ANATOMICAL left, and so
     is DC3 HandLeft (pose_mediapipe.py maps BlazePose L_PINKY/L_INDEX, which
     are anatomical-left). No mirror.

WHAT IS MEASURED: the shipping code, not a reimplementation. MediaPipeBackend
is imported from native/scripts/pose_mediapipe.py; its own PoseLandmarker runs
in VIDEO mode over the video, and _absolute_root + _remap are called verbatim,
mirroring MediaPipeBackend.process(). The only deviations: num_poses=1 (the
sequence is a verified solo -- exactly one body in every annotated frame; see
--survey), and frames are fed by index like pose_server.py --video mode.

CAMERA MODEL / the one real caveat: the backend assumes an IDEAL pinhole with
the principal point exactly at the image centre and no lens distortion, because
that is all a webcam user can be assumed to have. The Panoptic HD cameras have
an off-centre principal point (~20 px) and real barrel distortion (k1 ~ -0.22,
worth ~20 px near the edges). Two conditions are therefore run:

  raw         production fed the untouched video. Honest end-to-end, but the
              backend's pinhole misreads the lens, and that error is charged to
              the model.
  undistorted frames remapped to an ideal pinhole (same fx, principal point
              forced to the image centre, distCoef zeroed) so the image the
              backend sees actually obeys the model it assumes; GT is projected
              with the same ideal K. Isolates joint placement from lens
              mismatch. This is the condition to read for the hand verdict.

hfov is taken from the calibrated fx in both cases: hfov = 2*atan((W/2)/fx).

GT FILTERING: a Panoptic hand keypoint is used only when the hand is present in
the frame's JSON, belongs to the tracked body (id >= 0; id == -1 marks spurious
extra detections), and the three keypoints we consume (0 wrist, 5 index-MCP,
17 pinky-MCP) each have validity == 1, averageScore >= --min-score (default
0.5), and averageReproError <= --max-repro (default 20 px; invalid keypoints
carry a 1e5 sentinel). The filtered fraction is printed -- read it before
believing any table.

SANITY GATES (printed first; do not read the 3D tables if these fail):
  G1 overlay   GT body + hands projected into the view and drawn on real
               frames (--overlay writes PNGs). Look at them.
  G2 2D        GT-reprojected shoulders/hips vs MediaPipe's own 2D landmarks,
               median pixel distance. Should be well under ~15 px on a ~450 px
               subject. This is the alignment gate: it catches frame slip,
               wrong camera, and every sign error at once.
  G3 body 3D   root-relative body MPJPE, for continuity with the AIST++ numbers
               (BlazePose GHUM full publishes ~35-90 mm).

METRICS (each absolute AND root-aligned; root-aligned = subtract each
skeleton's own HipCenter and re-anchor on a GT root built the SAME way -- see
gt_root; a plain GT hip midpoint would not be apples-to-apples now that the
backend's HipCenter is a torso fraction -- which is the
scoring-relevant view because DC3's ErrorNode differences two joints of the
SAME skeleton and is therefore translation-invariant):
  (a) HandL/R vs GT knuckle midpoint (kpt5 + kpt17)/2 -- 3D, |dz|, |dxy|.
  (b) WristL/R vs GT wrist (kpt0) on the SAME frames, so the hand's increment
      over the wrist baseline is explicit rather than buried.
  (c) OFFSET VECTOR: ours (Hand - Wrist) vs GT (knuckle-mid - wrist) --
      direction cosine and length. This is the placement question with the
      inherited arm error removed.
  (d) splits by MediaPipe knuckle-landmark visibility and by GT hand speed.
  Plus ablations: Hand := Wrist (drop the offset entirely), and Hand := Wrist +
  the best fixed offset in a forearm-anchored local frame (the correction a fix
  would actually apply), so "is a correction warranted" is answered with a
  number rather than a hunch.

hfov INVARIANCE: hfov enters only through _absolute_root, i.e. as a per-frame
TRANSLATION of the whole skeleton. The root-aligned family and the entire
offset-vector analysis are therefore EXACTLY invariant to it -- only the
absolute rows move. That is why the shipping default (58.51 deg) is not run as
a separate condition here; it cannot change the hand verdict.

GATES OBSERVED (2026-08-01, both sequences, all conditions PASS):
  Overlays inspected by eye at 4 frames of 170307_dance5 and 1 of 171204_pose1:
  the 21-keypoint hand skeletons sit on the hands, the marked kpt 0 / 5 / 17
  land on wrist and knuckles.
  170307_dance5 / hd_00_21 (fx 1397, hfov 69.0 deg): GT-2D vs MediaPipe-2D
  median 15.2 px raw / 15.3 px undistorted = 3.4 cm at the subject's 3.11 m
  median depth; de-translated 12.2 px; frame-slip sweep minimises at offset 0.
  171204_pose1 / hd_00_16: 14.9 px, de-translated 11.8 px, offset 0 within 1.3%
  of the best. The residual is DEFINITIONAL, not misalignment: it is largest at
  the hips (17-23 px) and smallest at the shoulders (11-15 px), exactly the
  BlazePose-vs-coco19 joint-centre gap, and the frame-slip sweep rules out any
  temporal offset. Body MPJPE 163 mm (dance5) / 120 mm (pose1), above the
  published 35-90 mm because dance5 is contemporary dance with floor work and
  heavy self-occlusion. Detection 91.1% / 94.1%. GT hand instances passing the
  filter: 91.2% / 90.9%.

HEADLINE (23,000+ scored hand-frames across the two sequences):
  * THE HAND JOINT IS FINE. Root-aligned 3D error 0.244 / 0.298 m (dance5 L/R)
    and 0.187 / 0.273 m (pose1) -- only +1.2 to +3.5 cm worse than the SAME
    frames' wrist, which AIST++ already validated. Deleting the offset entirely
    (Hand := Wrist) is WORSE on all four side/sequence cells (+0.1 to +1.7 cm),
    so the synthesised knuckle offset earns its keep.
  * THE ERROR IS DEPTH, NOT PLACEMENT. Root-aligned |dz| 0.19-0.24 m vs |dxy|
    0.11-0.13 m; the offset vector's own error is 0.054-0.059 m. The hand
    inherits the arm's depth error; where it is put on the end of that arm is a
    second-order term.
  * NO PLACEMENT CORRECTION IS WARRANTED. In a forearm-anchored frame built
    from our own joints, the mean GT offset is (+0.054, +0.004, +0.005) m and
    ours is (+0.053, +0.001, +0.002) m on dance5 -- agreement to 1 mm along the
    forearm and under 5 mm off-axis. Held-out re-scoring with the best fixed
    offset changes 3D error by -1.1 mm (best cell) to +6.8 mm (worst); a pure
    length rescale (x0.78-x1.24) changes it by under 1 mm. There is no bias
    left to remove; the residual is direction NOISE (mean cos 0.60-0.70, median
    angle 37-44 deg on an 8 cm vector -- which is what a few cm of landmark
    noise on a short vector looks like).
  * ONE DOC FIX. pose_mediapipe.py:_remap claims the knuckle line sits "~0.10 m
    past the wrist". Panoptic measures the real distance at 0.076-0.080 m, and
    what we actually emit is 0.042-0.060 m -- we under-reach by 25-45%.
    Correcting it is not worth doing (see above), but the comment is wrong.
  * The wrist baseline is not a definitional artifact: Panoptic's hand kpt 0
    and its coco19 body wrist are separately annotated and differ by only
    1.4-1.9 cm median, and our wrist scores the same against either.
  * Error falls monotonically with MediaPipe's own knuckle visibility on
    dance5 (0.291 -> 0.222 m left) and rises mildly with GT hand speed
    (0.231 -> 0.262 m), so the confidence channel is usable as a gate.

UPDATE 2026-08-01 -- root anchor changed, verdicts unchanged. The backend's
HipCenter stopped being the hip midpoint (it is now a torso fraction matching
the Kinect convention, pose_mediapipe.HIP_CENTER_UP), so gt_root above was
changed to match and every ROOT-ALIGNED number here moved. Re-run on
170307_dance5 (undistorted): Hand 0.2448/0.2976 -> 0.2373/0.2805 m (L/R),
Wrist 0.2222/0.2722 -> 0.2126/0.2535, Hand:=Wrist ablation 0.2531/0.3071 ->
0.2445/0.2904. The new anchor is slightly BETTER on this corpus too -- it
averages the shoulders in, so it is a less noisy root than the hip midpoint
alone. The hand increment over the wrist (+0.025/+0.027 m) and the ablation's
sign are unchanged, and the offset-vector analysis is anchor-free, so the
verdicts above all stand. 171204_pose1 was NOT re-run; its numbers above
predate the change.

Run:
  .venv/bin/python tools/pose_corpus/bench_panoptic_hands.py \
      --data-dir /home/free/tmp/pose_gt_panoptic --seq 170307_dance5 \
      [--cam 00_21] [--conditions raw undistorted] [--quick 600] \
      [--overlay 500,2000,5000] [--pick-view] [--survey] [--recompute]
"""

import argparse
import glob
import json
import os
import sys

import cv2
import numpy as np

# Import the PRODUCTION backend -- the entire point is to measure shipping code.
_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO, "native", "scripts"))
from pose_mediapipe import (  # noqa: E402
    MediaPipeBackend, DC3_JOINT_NAMES, NUM_DC3_JOINTS,
    HIP_CENTER_UP,
    HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD,
    SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT, HAND_LEFT,
    SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT, HAND_RIGHT,
    HIP_LEFT, KNEE_LEFT, ANKLE_LEFT, HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT,
)

MODEL_PATH = os.path.join(_REPO, "native", "models", "pose_landmarker_full.task")

# ---------------------------------------------------------------------------
# Panoptic coco19 body indices (toolbox README).
# ---------------------------------------------------------------------------
P_NECK, P_NOSE, P_BODYCENTER = 0, 1, 2
P_LSHO, P_LELB, P_LWRI = 3, 4, 5
P_LHIP, P_LKNE, P_LANK = 6, 7, 8
P_RSHO, P_RELB, P_RWRI = 9, 10, 11
P_RHIP, P_RKNE, P_RANK = 12, 13, 14
P_LEYE, P_LEAR, P_REYE, P_REAR = 15, 16, 17, 18

# OpenPose hand order (21 keypoints).
H_WRIST = 0
H_INDEX_MCP = 5
H_PINKY_MCP = 17
HAND_KPTS_USED = (H_WRIST, H_INDEX_MCP, H_PINKY_MCP)

# MediaPipe 33-landmark indices used for the 2D gate and visibility splits.
MP_LSHO, MP_RSHO = 11, 12
MP_LWRI, MP_RWRI = 15, 16
MP_LPINKY, MP_RPINKY = 17, 18
MP_LINDEX, MP_RINDEX = 19, 20
MP_LHIP, MP_RHIP = 23, 24
MP_LELB, MP_RELB = 13, 14
MP_LKNE, MP_RKNE = 25, 26

# 2D alignment gate pairs: (mediapipe index, panoptic coco19 index).
GATE_PAIRS = [(MP_LSHO, P_LSHO), (MP_RSHO, P_RSHO),
              (MP_LHIP, P_LHIP), (MP_RHIP, P_RHIP)]
# Body MPJPE pairs for gate 3 (root-relative).
BODY_PAIRS = [(MP_LSHO, P_LSHO), (MP_RSHO, P_RSHO), (MP_LELB, P_LELB),
              (MP_RELB, P_RELB), (MP_LWRI, P_LWRI), (MP_RWRI, P_RWRI),
              (MP_LHIP, P_LHIP), (MP_RHIP, P_RHIP), (MP_LKNE, P_LKNE),
              (MP_RKNE, P_RKNE)]

# Per-side wiring: (label, dc3 hand joint, dc3 wrist joint, dc3 elbow joint,
#                   dc3 shoulder joint, panoptic hand key, mp pinky, mp index)
SIDES = [
    ("Left", HAND_LEFT, WRIST_LEFT, ELBOW_LEFT, SHOULDER_LEFT,
     "left_hand", MP_LPINKY, MP_LINDEX),
    ("Right", HAND_RIGHT, WRIST_RIGHT, ELBOW_RIGHT, SHOULDER_RIGHT,
     "right_hand", MP_RPINKY, MP_RINDEX),
]


# ---------------------------------------------------------------------------
# Calibration / geometry
# ---------------------------------------------------------------------------
def load_camera(seq_dir, seq, cam_name, undistort):
    """Panoptic calibration -> the dict the rest of the script uses.

    When `undistort` is set, the OUTPUT camera is an ideal pinhole (same fx/fy,
    principal point at the exact image centre, zero distortion) and frames are
    remapped onto it, so the image obeys the model the backend assumes.
    """
    calib = json.load(open(os.path.join(
        seq_dir, f"calibration_{seq}.json")))
    c = next(x for x in calib["cameras"] if x["name"] == cam_name)
    K = np.asarray(c["K"], dtype=float)
    dist = np.asarray(c["distCoef"], dtype=float)
    R = np.asarray(c["R"], dtype=float)
    t = np.asarray(c["t"], dtype=float).reshape(3)
    W, H = c["resolution"]
    rvec, _ = cv2.Rodrigues(R)

    if undistort:
        newK = np.array([[K[0, 0], 0.0, W * 0.5],
                         [0.0, K[1, 1], H * 0.5],
                         [0.0, 0.0, 1.0]])
        map1, map2 = cv2.initUndistortRectifyMap(
            K, dist, None, newK, (W, H), cv2.CV_16SC2)
        proj_K, proj_dist = newK, np.zeros(5)
    else:
        map1 = map2 = None
        proj_K, proj_dist = K, dist

    return dict(K=K, dist=dist, R=R, t=t, rvec=rvec, W=W, H=H,
                proj_K=proj_K, proj_dist=proj_dist, map1=map1, map2=map2,
                undistort=bool(undistort),
                hfov_deg=float(np.degrees(2.0 * np.arctan((W / 2.0) / K[0, 0]))))


def project(cam, X_cm):
    """World cm -> pixels, via the condition's camera model (see load_camera)."""
    X_cm = np.asarray(X_cm, dtype=float).reshape(-1, 1, 3)
    uv, _ = cv2.projectPoints(X_cm, cam["rvec"], cam["t"],
                              cam["proj_K"], cam["proj_dist"])
    return uv.reshape(-1, 2)


def to_dc3(cam, X_cm):
    """World cm -> DC3 camera space metres. See module docstring, conventions 1-4."""
    X_cm = np.asarray(X_cm, dtype=float)
    shape = X_cm.shape
    flat = X_cm.reshape(-1, 3)
    Xc = flat @ cam["R"].T + cam["t"]          # cm, OpenCV camera axes
    out = np.empty_like(Xc)
    out[:, 0] = -Xc[:, 0]
    out[:, 1] = -Xc[:, 1]
    out[:, 2] = Xc[:, 2]
    return (out / 100.0).reshape(shape)


# ---------------------------------------------------------------------------
# Ground-truth loading
# ---------------------------------------------------------------------------
def load_gt(seq_dir, n_frames, min_score, max_repro):
    """Load body + hand GT indexed by HD frame number.

    Returns dict of arrays length n_frames:
      body      (F,19,3) cm world, NaN where absent
      body_c    (F,19)   per-joint confidence
      body_ok   (F,)     exactly one annotated body this frame
      hand      {side: (F,21,3) cm world, NaN where absent}
      hand_ok   {side: (F,) bool}  passed the validity/score/repro filter
      counts    filtering bookkeeping
    """
    body = np.full((n_frames, 19, 3), np.nan)
    body_c = np.zeros((n_frames, 19))
    body_ok = np.zeros(n_frames, dtype=bool)
    hand = {s: np.full((n_frames, 21, 3), np.nan) for s in ("left_hand", "right_hand")}
    hand_ok = {s: np.zeros(n_frames, dtype=bool) for s in ("left_hand", "right_hand")}
    counts = dict(gt_frames=0, body_frames=0, no_body=0, multi_body=0,
                  hand_absent=0, hand_bad=0, hand_ok=0, spurious_person=0)

    bdir = os.path.join(seq_dir, "hdPose3d_stage1_coco19")
    hdir = os.path.join(seq_dir, "hdHand3d")
    for f in sorted(glob.glob(os.path.join(bdir, "body3DScene_*.json"))):
        idx = int(os.path.basename(f)[len("body3DScene_"):-len(".json")])
        if idx >= n_frames:
            continue
        counts["gt_frames"] += 1
        bodies = json.load(open(f))["bodies"]
        if len(bodies) == 0:
            counts["no_body"] += 1
            continue
        if len(bodies) > 1:
            counts["multi_body"] += 1
        b = bodies[0]
        j = np.asarray(b["joints19"], dtype=float).reshape(-1, 4)
        body[idx] = j[:, :3]
        body_c[idx] = j[:, 3]
        body_ok[idx] = True
        counts["body_frames"] += 1

        hf = os.path.join(hdir, f"handRecon3D_hd{idx:08d}.json")
        if not os.path.exists(hf):
            continue
        people = json.load(open(hf))["people"]
        counts["spurious_person"] += sum(1 for p in people if p.get("id", -1) < 0)
        # Prefer the person whose track id matches the body; else the first
        # non-spurious one. (id == -1 marks Panoptic's own untracked junk.)
        cand = [p for p in people if p.get("id", -1) == b["id"]]
        if not cand:
            cand = [p for p in people if p.get("id", -1) >= 0]
        if not cand:
            counts["hand_absent"] += 2
            continue
        p = cand[0]
        for side in ("left_hand", "right_hand"):
            h = p.get(side)
            if h is None:
                counts["hand_absent"] += 1
                continue
            lm = np.asarray(h["landmarks"], dtype=float).reshape(21, 3)
            val = np.asarray(h["validity"])
            sc = np.asarray(h["averageScore"], dtype=float)
            re = np.asarray(h["averageReproError"], dtype=float)
            hand[side][idx] = lm
            good = all(val[k] == 1 and sc[k] >= min_score and re[k] <= max_repro
                       for k in HAND_KPTS_USED)
            if good:
                hand_ok[side][idx] = True
                counts["hand_ok"] += 1
            else:
                counts["hand_bad"] += 1
    return dict(body=body, body_c=body_c, body_ok=body_ok,
                hand=hand, hand_ok=hand_ok, counts=counts)


# ---------------------------------------------------------------------------
# Landmarker (production model) with caching
# ---------------------------------------------------------------------------
def run_landmarker(video_path, cache_path, cam, quick=0):
    """Run the production backend's own PoseLandmarker over the video.

    Mirrors bench_model_z.run_landmarker and, through it, pose_server.py's
    --video feeding: sequential frames, BGR->RGB, detect_for_video timestamps
    from the frame index. Caches (world, img, vis, det) so analysis re-runs in
    seconds. The cache is per (video, condition) because the undistorted
    condition feeds a different image.
    """
    if os.path.exists(cache_path):
        d = np.load(cache_path)
        return {k: d[k] for k in d.files}

    be = MediaPipeBackend(MODEL_PATH, num_poses=1)  # verified solo sequence
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        sys.exit(f"cannot open {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 29.97
    period_ms = 1000.0 / fps

    world, img, vis, det = [], [], [], []
    i = 0
    while True:
        ok, frame = cap.read()
        if not ok or (quick and i >= quick):
            break
        if cam["map1"] is not None:
            frame = cv2.remap(frame, cam["map1"], cam["map2"], cv2.INTER_LINEAR)
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
        if i % 1000 == 0:
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


def backend_joints(geom_be, cache, W, H):
    """Replay MediaPipeBackend.process()'s per-frame geometry verbatim.

    _absolute_root then _remap on the cached production landmarks; geom_be only
    supplies _hfov via _focal_px. Returns (F,20,3) and a validity mask.
    """
    F = len(cache["det"])
    joints = np.full((F, NUM_DC3_JOINTS, 3), np.nan)
    valid = np.zeros(F, dtype=bool)
    for i in range(F):
        if not cache["det"][i]:
            continue
        root = geom_be._absolute_root(cache["world"][i], cache["img"][i],
                                      cache["vis"][i], W, H)
        if root is None:
            continue
        j, _c = geom_be._remap(cache["world"][i], cache["vis"][i], root)
        joints[i] = j
        valid[i] = True
    return joints, valid


# ---------------------------------------------------------------------------
# Reporting helpers
# ---------------------------------------------------------------------------
def stats(err, name, width=34):
    e = np.asarray(err)
    e = e[np.isfinite(e)]
    if e.size == 0:
        print(f"  {name:<{width}} (no data)")
        return
    print(f"  {name:<{width}} n {e.size:6d}  mean {e.mean():.4f}  "
          f"p50 {np.percentile(e,50):.4f}  p90 {np.percentile(e,90):.4f}  "
          f"max {e.max():.4f}")


def err_triplet(pred, gt):
    """(3D, |dz|, |dxy|) for matched (N,3) arrays."""
    d = pred - gt
    return (np.linalg.norm(d, axis=-1), np.abs(d[..., 2]),
            np.linalg.norm(d[..., :2], axis=-1))


def local_basis(elbow, wrist, shoulder):
    """Right-handed forearm-anchored basis built from OUR OWN predicted joints.

    e1 = forearm direction (elbow -> wrist); e2 perpendicular to the arm plane
    (forearm x upper-arm); e3 completes it. Any correction expressed here can be
    applied at runtime, because it needs nothing the backend does not already
    have. Falls back to world up when the arm is straight (degenerate plane).
    """
    e1 = wrist - elbow
    n1 = np.linalg.norm(e1, axis=-1, keepdims=True)
    e1 = np.divide(e1, np.where(n1 > 1e-6, n1, 1.0))
    upper = elbow - shoulder
    e2 = np.cross(e1, upper)
    n2 = np.linalg.norm(e2, axis=-1, keepdims=True)
    fallback = np.cross(e1, np.broadcast_to(np.array([0.0, 1.0, 0.0]), e1.shape))
    nf = np.linalg.norm(fallback, axis=-1, keepdims=True)
    e2 = np.where(n2 > 1e-6, np.divide(e2, np.where(n2 > 1e-6, n2, 1.0)),
                  np.divide(fallback, np.where(nf > 1e-6, nf, 1.0)))
    e3 = np.cross(e1, e2)
    return e1, e2, e3


# ---------------------------------------------------------------------------
# Utility modes
# ---------------------------------------------------------------------------
def pick_view(seq_dir, seq, stride=50):
    """Score all 31 HD views by how completely they contain body + both hands.

    Printed so the --cam default is reproducible rather than folklore.
    """
    calib = json.load(open(os.path.join(seq_dir, f"calibration_{seq}.json")))
    hd = [c for c in calib["cameras"] if c["type"] == "hd"]
    bfiles = sorted(glob.glob(os.path.join(
        seq_dir, "hdPose3d_stage1_coco19", "body3DScene_*.json")))[::stride]
    pts = []
    for f in bfiles:
        idx = int(os.path.basename(f)[len("body3DScene_"):-len(".json")])
        bodies = json.load(open(f))["bodies"]
        hf = os.path.join(seq_dir, "hdHand3d", f"handRecon3D_hd{idx:08d}.json")
        if len(bodies) != 1 or not os.path.exists(hf):
            continue
        people = [p for p in json.load(open(hf))["people"] if p.get("id", -1) >= 0]
        if not people or not all(s in people[0] for s in ("left_hand", "right_hand")):
            continue
        P = [np.asarray(bodies[0]["joints19"]).reshape(-1, 4)[:, :3]]
        for s in ("left_hand", "right_hand"):
            P.append(np.asarray(people[0][s]["landmarks"]).reshape(21, 3))
        pts.append(np.vstack(P))
    X = np.asarray(pts)
    print(f"scoring {len(hd)} HD views over {len(X)} frames with body + 2 hands")
    rows = []
    for c in hd:
        cam = dict(K=np.asarray(c["K"]), dist=np.asarray(c["distCoef"]),
                   R=np.asarray(c["R"]), t=np.asarray(c["t"]).reshape(3))
        cam["rvec"], _ = cv2.Rodrigues(cam["R"])
        cam["proj_K"], cam["proj_dist"] = cam["K"], cam["dist"]
        W, H = c["resolution"]
        uv = np.stack([project(cam, X[i]) for i in range(len(X))])
        Z = (X.reshape(-1, 3) @ cam["R"].T + cam["t"])[:, 2].reshape(X.shape[:2])
        m = 8
        inside = ((uv[..., 0] > m) & (uv[..., 0] < W - m) &
                  (uv[..., 1] > m) & (uv[..., 1] < H - m) & (Z > 0))
        h = np.median(np.linalg.norm(
            uv[:, P_NECK] - 0.5 * (uv[:, P_LANK] + uv[:, P_RANK]), axis=1))
        rows.append((c["name"], inside.all(1).mean(), inside[:, 19:].all(1).mean(), h))
    rows.sort(key=lambda r: -(r[1] * r[3]))
    for n, a, hh, h in rows:
        print(f"  {n}  all-in {a:.2f}  hands-in {hh:.2f}  subject {h:.0f} px")


def write_overlays(video, cam, gt, indices, out_dir):
    """GATE 1: draw GT body + both hands on real frames. Look at these."""
    os.makedirs(out_dir, exist_ok=True)
    cap = cv2.VideoCapture(video)
    edges = [(0, 1), (0, 2), (0, 3), (3, 4), (4, 5), (0, 9), (9, 10), (10, 11),
             (2, 6), (6, 7), (7, 8), (2, 12), (12, 13), (13, 14)]
    hedges = [(0, 1), (1, 2), (2, 3), (3, 4), (0, 5), (5, 6), (6, 7), (7, 8),
              (0, 9), (9, 10), (10, 11), (11, 12), (0, 13), (13, 14), (14, 15),
              (15, 16), (0, 17), (17, 18), (18, 19), (19, 20)]
    written = []
    for idx in indices:
        cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ok, frame = cap.read()
        if not ok:
            continue
        if cam["map1"] is not None:
            frame = cv2.remap(frame, cam["map1"], cam["map2"], cv2.INTER_LINEAR)
        if gt["body_ok"][idx]:
            uv = project(cam, gt["body"][idx])
            for a, b in edges:
                cv2.line(frame, tuple(uv[a].astype(int)), tuple(uv[b].astype(int)),
                         (0, 255, 0), 2)
            for u in uv:
                cv2.circle(frame, tuple(u.astype(int)), 4, (0, 200, 0), -1)
        for side, col in (("left_hand", (255, 80, 0)), ("right_hand", (0, 80, 255))):
            if not np.isfinite(gt["hand"][side][idx]).all():
                continue
            uv = project(cam, gt["hand"][side][idx])
            for a, b in hedges:
                cv2.line(frame, tuple(uv[a].astype(int)), tuple(uv[b].astype(int)),
                         col, 2)
            # The three keypoints this benchmark actually consumes.
            for k, c2 in ((H_WRIST, (255, 255, 255)), (H_INDEX_MCP, (0, 255, 255)),
                          (H_PINKY_MCP, (255, 0, 255))):
                cv2.circle(frame, tuple(uv[k].astype(int)), 7, c2, -1)
            mid = 0.5 * (uv[H_INDEX_MCP] + uv[H_PINKY_MCP])
            cv2.circle(frame, tuple(mid.astype(int)), 9, (255, 255, 255), 2)
        cv2.putText(frame, f"frame {idx}  ({'undist' if cam['undistort'] else 'raw'})",
                    (20, 50), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (255, 255, 255), 3)
        # Crop to the subject so the hands are legible at review size.
        if gt["body_ok"][idx]:
            uv = project(cam, gt["body"][idx])
            cx, cy = uv.mean(0)
            half = 540
            x0 = int(np.clip(cx - half, 0, cam["W"] - 2 * half))
            y0 = int(np.clip(cy - half, 0, cam["H"] - 2 * half))
            frame = frame[y0:y0 + 2 * half, x0:x0 + 2 * half]
        p = os.path.join(out_dir, f"overlay_{idx:08d}.png")
        cv2.imwrite(p, frame)
        written.append(p)
    cap.release()
    for p in written:
        print(f"  wrote {p}")
    return written


# ---------------------------------------------------------------------------
# Main evaluation
# ---------------------------------------------------------------------------
def evaluate(cond, cam, gt, cache, args):
    W, H = cam["W"], cam["H"]
    F = len(cache["det"])
    print(f"\n{'='*78}\nCONDITION: {cond}   (camera {args.cam}, "
          f"fx {cam['K'][0,0]:.0f} px, hfov {cam['hfov_deg']:.2f} deg; "
          f"backend default is 58.51)\n{'='*78}")

    # ---- GATE 2: GT reprojection vs MediaPipe's own 2D landmarks ----------
    # Reported four ways, because the RAW number conflates two very different
    # things: (i) actual misalignment -- wrong frame, wrong camera, a sign
    # error -- which is fatal, and (ii) the definitional gap between BlazePose's
    # joint centres and Panoptic's coco19 ones, which is real, irreducible, and
    # harmless to a differential hand measurement. The de-translated number and
    # the frame-slip sweep separate them.
    det = cache["det"] & gt["body_ok"][:F]
    idx = np.flatnonzero(det)
    sample = idx[:: max(1, len(idx) // 800)]
    per_pair, resid, depths = {a: [] for a, _ in GATE_PAIRS}, [], []
    for i in sample:
        uv = project(cam, gt["body"][i])
        mp_px = cache["img"][i] * np.array([W, H])
        d = np.stack([mp_px[a] - uv[b] for a, b in GATE_PAIRS])
        for k, (a, _b) in enumerate(GATE_PAIRS):
            per_pair[a].append(float(np.linalg.norm(d[k])))
        # Per-frame common translation removed: what is left is pure shape /
        # definitional disagreement.
        resid.extend(np.linalg.norm(d - np.median(d, axis=0), axis=1).tolist())
        depths.append(float((gt["body"][i, P_BODYCENTER] @ cam["R"].T
                             + cam["t"])[2] / 100.0))
    gate = np.concatenate([np.asarray(v) for v in per_pair.values()])
    resid = np.asarray(resid)
    z = float(np.median(depths))
    px_per_cm = cam["proj_K"][0, 0] / (z * 100.0)
    print(f"  GATE2 GT-2D vs MediaPipe-2D (shoulders+hips): median "
          f"{np.median(gate):.1f} px = {np.median(gate)/px_per_cm:.1f} cm at the "
          f"subject's median depth {z:.2f} m ({px_per_cm:.1f} px/cm)")
    print("        per pair (px): " + "  ".join(
        f"{nm}={np.median(per_pair[a]):.1f}" for a, nm in
        zip([p[0] for p in GATE_PAIRS], ["Lsho", "Rsho", "Lhip", "Rhip"])))
    print(f"        after removing each frame's common translation: median "
          f"{np.median(resid):.1f} px  <- this is the SHAPE disagreement; the "
          f"rest is a rigid offset")
    # Frame-slip sweep: alignment is confirmed by the MINIMUM sitting at 0, not
    # by the absolute value. A dancer moving ~1 m/s at 30 fps shifts ~15 px per
    # frame here, so a slip of even one frame is visible.
    slips = {}
    for s in (-2, -1, 0, 1, 2):
        acc = []
        for i in sample:
            j = i + s
            if j < 0 or j >= F or not gt["body_ok"][j]:
                continue
            uv = project(cam, gt["body"][j])
            mp_px = cache["img"][i] * np.array([W, H])
            acc.extend(float(np.linalg.norm(mp_px[a] - uv[b]))
                       for a, b in GATE_PAIRS)
        slips[s] = float(np.median(acc)) if acc else np.nan
    best = min(slips, key=lambda k: slips[k])
    # The criterion is "offset 0 is within 5% of the best offset", not "0 is
    # the argmin". On a near-static subject the sweep is almost flat and the
    # argmin wanders on noise; on a moving subject a real slip costs >> 5%.
    slip_ratio = slips[0] / slips[best] if slips[best] > 0 else np.inf
    print("        frame-slip sweep (median px): " +
          "  ".join(f"{s:+d}:{v:.1f}" for s, v in slips.items()) +
          f"   -> best {best:+d}, offset-0 is {100*(slip_ratio-1):.1f}% above it")
    gate_pass = slip_ratio < 1.05 and np.median(resid) < 15.0
    print(f"  GATE2 {'PASS' if gate_pass else 'FAIL'} (criterion: offset 0 "
          f"within 5% of the best frame offset AND de-translated median "
          f"< 15 px)")

    # ---- GT in DC3 space --------------------------------------------------
    body_dc3 = to_dc3(cam, np.nan_to_num(gt["body"], nan=0.0))
    body_dc3[~np.isfinite(gt["body"])] = np.nan
    body_dc3 = body_dc3[:F]
    gt_hip = 0.5 * (body_dc3[:, P_LHIP] + body_dc3[:, P_RHIP])

    # Root-alignment anchor. NOT the hip midpoint: the backend's HipCenter is
    # a fraction up the subject's own torso (pose_mediapipe.HIP_CENTER_UP, from
    # the Kinect convention measured in bench_utd_mhad.py), so anchoring our
    # skeleton on that and the GT on a plain midpoint would inject a ~12 cm
    # vertical offset into every root-aligned number here. Build the GT anchor
    # the same way, from coco19's own hips and shoulders, and the alignment
    # measures tracking again. (gt_hip itself stays the plain midpoint: GATE 3
    # compares RAW BlazePose world landmarks, whose origin IS the hip midpoint.)
    gt_sho = 0.5 * (body_dc3[:, P_LSHO] + body_dc3[:, P_RSHO])
    gt_root = gt_hip + HIP_CENTER_UP * (gt_sho - gt_hip)

    # ---- GATE 3: root-relative body MPJPE --------------------------------
    rel = []
    for mp_i, p_i in BODY_PAIRS:
        w = cache["world"][:, mp_i, :]
        pred_rel = np.stack([-w[:, 0], -w[:, 1], w[:, 2]], axis=1)
        rel.append(np.linalg.norm(pred_rel - (body_dc3[:, p_i] - gt_hip), axis=1))
    mpjpe = np.nanmean(np.where(det, np.stack(rel, 1).mean(1), np.nan))
    print(f"  GATE3 root-relative body MPJPE {mpjpe*1000:.0f} mm "
          f"[BlazePose GHUM full publishes ~35-90 mm]")

    # ---- production joints ------------------------------------------------
    be = MediaPipeBackend(MODEL_PATH, num_poses=1, hfov_deg=cam["hfov_deg"])
    joints, valid = backend_joints(be, cache, W, H)
    be.close()
    valid = valid & gt["body_ok"][:F] & np.isfinite(gt_hip).all(1)
    print(f"  detection {det.mean()*100:.1f}% of {F} frames; "
          f"usable body frames {valid.sum()}")

    fps = float(cache["fps"])
    out = {}
    body_wrist = {"left_hand": body_dc3[:, P_LWRI], "right_hand": body_dc3[:, P_RWRI]}
    for label, J_HAND, J_WRIST, J_ELB, J_SHO, side_key, mp_pinky, mp_index in SIDES:
        hok = gt["hand_ok"][side_key][:F] & valid
        hand_cm = gt["hand"][side_key][:F]
        gh = to_dc3(cam, np.nan_to_num(hand_cm, nan=0.0))
        gh[~np.isfinite(hand_cm)] = np.nan

        gt_knuckle = 0.5 * (gh[:, H_INDEX_MCP] + gh[:, H_PINKY_MCP])
        gt_wrist = gh[:, H_WRIST]
        m = hok & np.isfinite(gt_knuckle).all(1) & np.isfinite(gt_wrist).all(1)

        pred_hand = joints[:, J_HAND]
        pred_wrist = joints[:, J_WRIST]
        pred_hip = joints[:, HIP_CENTER]

        # Root-aligned = own HipCenter subtracted, the identically-constructed
        # GT root added back (see gt_root above).
        ra_hand = pred_hand - pred_hip + gt_root
        ra_wrist = pred_wrist - pred_hip + gt_root

        # GT hand speed (m/s) for the motion split.
        spd = np.full(F, np.nan)
        d = np.diff(gt_knuckle, axis=0)
        spd[1:] = np.linalg.norm(d, axis=1) * fps

        vis_kn = np.minimum(cache["vis"][:, mp_pinky], cache["vis"][:, mp_index])

        out[label] = dict(
            m=m, spd=spd, vis=vis_kn,
            pred_hand=pred_hand, pred_wrist=pred_wrist,
            ra_hand=ra_hand, ra_wrist=ra_wrist,
            gt_knuckle=gt_knuckle, gt_wrist=gt_wrist,
            gt_body_wrist=body_wrist[side_key],
            elbow=joints[:, J_ELB], shoulder=joints[:, J_SHO],
        )
    return out, dict(gate_px=float(np.median(gate)),
                     gate_resid_px=float(np.median(resid)),
                     gate_slip=int(best), gate_slip_ratio=float(slip_ratio),
                     gate_pass=gate_pass,
                     mpjpe_mm=float(mpjpe * 1000), det=float(det.mean()),
                     n_valid=int(valid.sum()))


def report(cond, res, args):
    print(f"\n-- (a)+(b) HAND vs WRIST error, {cond} (metres) --")
    print("     Hand = our HandL/R vs GT (index-MCP + pinky-MCP)/2;  "
          "Wrist = our WristL/R vs GT hand kpt 0, SAME frames.")
    for label in ("Left", "Right"):
        r = res[label]
        m = r["m"]
        print(f"\n  [{label}]  n = {m.sum()} frames")
        for tag, pred_h, pred_w, gt_h, gt_w in (
                ("absolute", r["pred_hand"], r["pred_wrist"],
                 r["gt_knuckle"], r["gt_wrist"]),
                ("root-aligned", r["ra_hand"], r["ra_wrist"],
                 r["gt_knuckle"], r["gt_wrist"])):
            d3h, dzh, dxyh = err_triplet(pred_h[m], gt_h[m])
            d3w, dzw, dxyw = err_triplet(pred_w[m], gt_w[m])
            # Ablation: drop the synthesised offset entirely (Hand := Wrist).
            d3n, dzn, dxyn = err_triplet(pred_w[m], gt_h[m])
            print(f"    {tag}:")
            stats(d3h, f"Hand   3D", 24); stats(dzh, "Hand   |dz|", 24)
            stats(dxyh, "Hand   |dxy|", 24)
            stats(d3w, "Wrist  3D  (baseline)", 24)
            stats(d3n, "Hand:=Wrist 3D (ablation)", 24)
            # Cross-check: the hand JSON's kpt 0 and the body JSON's coco19
            # wrist are separately annotated. If the wrist baseline above were
            # inflated by a hand-root-vs-body-wrist definitional gap, these two
            # rows would disagree; they do not.
            d3b, _, _ = err_triplet(pred_w[m], r["gt_body_wrist"][m])
            stats(d3b, "Wrist  3D  vs coco19 wri", 24)
            if tag == "absolute":
                gap = np.linalg.norm(
                    (r["gt_body_wrist"] - r["gt_wrist"])[m], axis=-1)
                stats(gap, "GT hand-kpt0 vs coco19", 24)

    print(f"\n-- (c) OFFSET VECTOR agreement, {cond} --")
    print("     ours = HandL/R - WristL/R;  GT = knuckle-mid - GT wrist. "
          "Placement only: the arm's own error cancels in the difference.")
    print(f"  {'side':6s} {'stat':>8s} {'|ours| m':>9s} {'|GT| m':>9s} "
          f"{'len err m':>10s} {'cos':>7s} {'angle deg':>10s} {'vec err m':>10s}")
    for label in ("Left", "Right"):
        r = res[label]
        m = r["m"]
        ours = (r["pred_hand"] - r["pred_wrist"])[m]
        gtv = (r["gt_knuckle"] - r["gt_wrist"])[m]
        lo = np.linalg.norm(ours, axis=1)
        lg = np.linalg.norm(gtv, axis=1)
        cos = np.clip((ours * gtv).sum(1) / np.maximum(lo * lg, 1e-9), -1, 1)
        ang = np.degrees(np.arccos(cos))
        ve = np.linalg.norm(ours - gtv, axis=1)
        print(f"  {label:6s} {'mean':>8s} {lo.mean():9.4f} {lg.mean():9.4f} "
              f"{np.mean(lo-lg):+10.4f} {cos.mean():7.3f} {ang.mean():10.1f} "
              f"{ve.mean():10.4f}   (n={m.sum()})")
        for p in (10, 50, 90):
            print(f"  {'':6s} {'p%d' % p:>8s} {np.percentile(lo,p):9.4f} "
                  f"{np.percentile(lg,p):9.4f} {np.percentile(lo-lg,p):+10.4f} "
                  f"{np.percentile(cos,p):7.3f} {np.percentile(ang,p):10.1f} "
                  f"{np.percentile(ve,p):10.4f}")

    # ---- would a fixed placement correction help? -------------------------
    print(f"\n-- placement correction, {cond} --")
    print("     Fit the mean GT offset in a forearm-anchored local frame built "
          "from OUR OWN joints,\n     then re-score Hand := Wrist + that fixed "
          "offset. Half the frames fit, half score (holdout).")
    for label in ("Left", "Right"):
        r = res[label]
        m = np.flatnonzero(r["m"])
        if len(m) < 20:
            continue
        fit, hold = m[0::2], m[1::2]
        e1, e2, e3 = local_basis(r["elbow"], r["pred_wrist"], r["shoulder"])
        gtv = r["gt_knuckle"] - r["gt_wrist"]
        comp = np.stack([(gtv * e1).sum(1), (gtv * e2).sum(1), (gtv * e3).sum(1)], 1)
        ours = r["pred_hand"] - r["pred_wrist"]
        ocomp = np.stack([(ours * e1).sum(1), (ours * e2).sum(1),
                          (ours * e3).sum(1)], 1)
        mean_gt = np.nanmean(comp[fit], axis=0)
        mean_ours = np.nanmean(ocomp[fit], axis=0)
        corr = (mean_gt[0] * e1 + mean_gt[1] * e2 + mean_gt[2] * e3)
        # Minimal one-constant alternative: keep our offset's DIRECTION, fix
        # only its length (scale fit by least squares on the fit half).
        num = float(np.nansum((ours[fit] * gtv[fit]).sum(1)))
        den = float(np.nansum((ours[fit] * ours[fit]).sum(1)))
        scale = num / den if den > 1e-12 else 1.0
        d_prod = np.linalg.norm(r["pred_hand"][hold] - r["gt_knuckle"][hold], axis=1)
        d_corr = np.linalg.norm(
            (r["pred_wrist"] + corr)[hold] - r["gt_knuckle"][hold], axis=1)
        d_scale = np.linalg.norm(
            (r["pred_wrist"] + scale * ours)[hold] - r["gt_knuckle"][hold], axis=1)
        d_wrist = np.linalg.norm(r["pred_wrist"][hold] - r["gt_knuckle"][hold], axis=1)
        print(f"  [{label}] GT offset in (forearm, arm-normal, binormal) = "
              f"({mean_gt[0]:+.4f}, {mean_gt[1]:+.4f}, {mean_gt[2]:+.4f}) m")
        print(f"         ours                                        = "
              f"({mean_ours[0]:+.4f}, {mean_ours[1]:+.4f}, {mean_ours[2]:+.4f}) m")
        print(f"         holdout 3D: production {np.nanmean(d_prod):.4f}  "
              f"fixed-offset {np.nanmean(d_corr):.4f}  "
              f"scaled x{scale:.2f} {np.nanmean(d_scale):.4f}  "
              f"wrist-only {np.nanmean(d_wrist):.4f}")

    # ---- (d) splits -------------------------------------------------------
    print(f"\n-- (d) splits, {cond} (Hand 3D error, root-aligned) --")
    for label in ("Left", "Right"):
        r = res[label]
        m = r["m"]
        d3 = np.linalg.norm(r["ra_hand"] - r["gt_knuckle"], axis=1)
        vis = r["vis"]
        print(f"  [{label}] by MediaPipe knuckle visibility:")
        for lo, hi in ((0.0, 0.5), (0.5, 0.8), (0.8, 1.01)):
            k = m & (vis >= lo) & (vis < hi)
            stats(d3[k], f"vis [{lo:.1f},{hi:.1f})", 24)
        spd = r["spd"]
        q = np.nanpercentile(spd[m], [33, 66]) if m.sum() else [0, 0]
        print(f"  [{label}] by GT hand speed (terciles at "
              f"{q[0]:.2f} / {q[1]:.2f} m/s):")
        for lo, hi, nm in ((-np.inf, q[0], "slow"), (q[0], q[1], "medium"),
                           (q[1], np.inf, "fast")):
            k = m & (spd >= lo) & (spd < hi)
            stats(d3[k], f"speed {nm}", 24)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", default="/home/free/tmp/pose_gt_panoptic")
    ap.add_argument("--seq", default="170307_dance5")
    ap.add_argument("--cam", default="00_21")
    ap.add_argument("--conditions", nargs="*", default=["raw", "undistorted"],
                    choices=["raw", "undistorted"])
    ap.add_argument("--quick", type=int, default=0, help="limit frames")
    ap.add_argument("--min-score", type=float, default=0.5)
    ap.add_argument("--max-repro", type=float, default=20.0)
    ap.add_argument("--overlay", default="", help="comma-separated frame indices")
    ap.add_argument("--pick-view", action="store_true")
    ap.add_argument("--survey", action="store_true",
                    help="print GT population/filter survey and exit")
    ap.add_argument("--recompute", action="store_true")
    args = ap.parse_args()

    seq_dir = os.path.join(args.data_dir, args.seq)
    if args.pick_view:
        pick_view(seq_dir, args.seq)
        return

    video = os.path.join(seq_dir, "hdVideos", f"hd_{args.cam}.mp4")
    cap = cv2.VideoCapture(video)
    n_video = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    cap.release()
    n_frames = args.quick or n_video
    print(f"video {video}: {n_video} frames; evaluating {n_frames}")

    gt = load_gt(seq_dir, n_frames, args.min_score, args.max_repro)
    c = gt["counts"]
    tot_hand = c["hand_ok"] + c["hand_bad"] + c["hand_absent"]
    print(f"GT: {c['gt_frames']} annotated frames, body present "
          f"{c['body_frames']} ({c['no_body']} empty, {c['multi_body']} "
          f"multi-body), {c['spurious_person']} spurious id=-1 hand people")
    print(f"GT hands: {c['hand_ok']}/{tot_hand} hand-instances pass "
          f"(validity+score>={args.min_score}+repro<={args.max_repro} px on "
          f"kpts 0/5/17) = {100.0*c['hand_ok']/max(tot_hand,1):.1f}%; "
          f"{c['hand_bad']} filtered out, {c['hand_absent']} absent")
    if args.survey:
        return

    if args.overlay:
        for cond in args.conditions:
            cam = load_camera(seq_dir, args.seq, args.cam, cond == "undistorted")
            print(f"overlays ({cond}):")
            write_overlays(video, cam, gt,
                           [int(x) for x in args.overlay.split(",")],
                           os.path.join(args.data_dir, "overlays", cond))
        return

    summaries = {}
    for cond in args.conditions:
        cam = load_camera(seq_dir, args.seq, args.cam, cond == "undistorted")
        cache_path = os.path.join(args.data_dir, "cache",
                                  f"{args.seq}_{args.cam}_{cond}"
                                  f"{'_q%d' % args.quick if args.quick else ''}.npz")
        if args.recompute and os.path.exists(cache_path):
            os.remove(cache_path)
        print(f"\nlandmarking [{cond}] (cached after first run)...", flush=True)
        cache = run_landmarker(video, cache_path, cam, args.quick)
        n = min(len(cache["det"]), n_frames)
        cache = {k: (v[:n] if getattr(v, "ndim", 0) else v) for k, v in cache.items()}
        res, summ = evaluate(cond, cam, gt, cache, args)
        report(cond, res, args)
        summaries[cond] = summ

    print("\n" + "=" * 78)
    print("GATE SUMMARY")
    for cond, s in summaries.items():
        print(f"  {cond:12s} GT-2D vs MP-2D median {s['gate_px']:5.1f} px "
              f"(de-translated {s['gate_resid_px']:4.1f} px, best frame slip "
              f"{s['gate_slip']:+d}, offset-0 cost "
              f"{100*(s['gate_slip_ratio']-1):+.1f}%) "
              f"[{'PASS' if s['gate_pass'] else 'FAIL'}]   "
              f"body MPJPE {s['mpjpe_mm']:.0f} mm   detection "
              f"{s['det']*100:.1f}%   usable frames {s['n_valid']}")


if __name__ == "__main__":
    main()
