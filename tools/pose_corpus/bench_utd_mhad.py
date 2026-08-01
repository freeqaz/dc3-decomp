#!/usr/bin/env python3
"""Benchmark the PRODUCTION MediaPipe backend against a REAL Kinect v1 20-joint
skeleton -- the exact sensor and joint set Dance Central 3 was built around.

WHY THIS EXISTS: tools/pose_corpus/bench_model_z.py validated the backend's body
joints against AIST++ (root-aligned |dz| 0.085 m), but AIST++ is COCO-17: it has
no hand joint and no foot joint, so the two DC3 joints we SYNTHESISE were never
compared against anything.

  HandLeft/Right = midpoint of BlazePose pinky (17/18) and index (19/20) knuckles
  FootLeft/Right = heel (29/30) blended FOOT_TOE_BLEND=0.35 toward foot_index (31/32)

Both placements were calibrated against a SINGLE baked Kinect frame
(src/system/gesture/StubCameraInput.cpp:57-61). This script replaces that one
frame with ~45 k frames of real Kinect v1 output over 8 subjects.

WHAT KINECT IS HERE: a REFERENCE, not metric truth. Kinect v1's skeletal tracker
is itself an estimator, and its hand/foot joints are extrapolations off the
wrist/ankle rather than anatomically defined points. But it is the estimator
DC3's choreography, gesture filters and scoring thresholds were authored
against, so "agreement with Kinect" is the right target for a drop-in
replacement whatever the anatomical truth. Every number below is a
DISAGREEMENT WITH THE PRODUCT-REFERENCE SENSOR.

-------------------------------------------------------------------------------
DATASET: UTD-MHAD (UTD Multimodal Human Action Dataset)
  Chen, Jafari, Kehtarnavaz, "UTD-MHAD: A Multimodal Dataset for Human Action
  Recognition Utilizing a Depth Camera and a Wearable Inertial Sensor", ICIP 2015.
  https://personal.utdallas.edu/~kehtar/UTD-MHAD.html

  RGB.zip         .../UTD-MAD/RGB.zip           1.1 GB  640x480 15 fps .avi
  Skeleton.zip    .../UTD-MAD/Skeleton.zip       15 MB  .mat, Kinect v1 skeleton
  Sample_Code.zip .../UTD-MAD/Sample_Code.zip   143 KB  MATLAB readers + joint order
  (host: https://personal.utdallas.edu/~kehtar/UTD-MAD/)

  DOWNLOAD GOTCHAS, both hit on first attempt:
   1. the host serves an INCOMPLETE certificate chain (leaf only, missing the
      InCommon RSA Server CA 2 intermediate) so curl dies with error 60. Fetch
      the intermediate from the leaf's own AIA URL and append it:
        curl -sS -o i.crt http://crt.sectigo.com/InCommonRSAServerCA2.crt
        openssl x509 -inform DER -in i.crt -out i.pem
        cat /etc/ssl/certs/ca-certificates.crt i.pem > cabundle.pem
        curl --cacert cabundle.pem -C - -O .../RGB.zip
   2. RGB.zip truncates silently mid-transfer. Verify against the advertised
      content-length (1106885499) and resume with `curl -C -` until it matches;
      a short file unzips as "cannot find zipfile directory".

  27 actions x 8 subjects x 4 trials = 861 trials with BOTH modalities.
  Files a{action}_s{subject}_t{trial}_{color.avi, skeleton.mat}.
  Not redistributable from this repo: it lives under ~/tmp/pose_gt_utdmhad/ and
  only this script is committed.

SKELETON FORMAT: .mat, variable `d_skel`, shape (20, 3, num_frames), float64,
METRES, Kinect v1 skeleton (depth-camera) space. Joint ORDER is given in
Sample_Code/Skeleton_joint_order.txt and is NOT DC3's order -- the first four
are reversed and the feet are interleaved rather than trailing:

  UTD  1 head  2 shoulder_center  3 spine  4 hip_center
       5..8   L_shoulder L_elbow L_wrist L_hand
       9..12  R_shoulder R_elbow R_wrist R_hand
      13..16  L_hip L_knee L_ankle L_foot
      17..20  R_hip R_knee R_ankle R_foot

  VERIFIED against the data, not taken on faith (--verify prints the evidence):
   * `head` has the largest mean y of all 20 joints, the feet the smallest.
   * actions a1/a3/a5 ("right arm swipe left", "right hand wave", "right arm
     throw") move joint 11 (documented R_wrist) ~22x further than joint 7
     (L_wrist), which pins the left/right labelling independently of the video.
   * bone lengths are adult-plausible (torso 0.42 m, shoulder span 0.33 m,
     forearm 0.24 m), which is what confirms the units really are metres.

AXIS CONVENTION -- Kinect skeleton space vs DC3 camera space. Measured:
  * +y is UP (head > hip > foot), origin at SENSOR height: feet sit at
    y ~ -1.05, i.e. the sensor stood ~1.05 m above the floor.
  * +z is AWAY from the sensor (subjects at z ~ 2.3-3.0 m).
  * the player's ANATOMICAL RIGHT is at +x (R_shoulder +0.12, L_shoulder -0.21),
    i.e. player-left = -x.
DC3 camera space is defined identically -- +Y up, +Z away, player-left = -X (see
the StubCameraInput baked capture, ShoulderLeft.x -0.047 < ShoulderRight.x
+0.322). So the Kinect skeleton needs NO axis transform: it IS DC3 camera space,
unsurprising since DC3 consumed NUI_SKELETON_FRAME positions directly. The
remap is a pure index permutation. This is not assumed -- GATE 2 falsifies the
mirror explicitly.

-------------------------------------------------------------------------------
SANITY GATES -- run and read BEFORE trusting any 3D number (--gates-only stops
after them). Both gates lean on projecting the Kinect skeleton into the RGB
frame and comparing against MediaPipe's OWN 2D landmarks, using the published
Kinect v1 RGB focal length (Burrus: fx 529.2, fy 525.6 at 640x480).

  GATE 2a  MIRROR. Fit u = cx + f*(s*x/z) for s = -1 and s = +1 and read the
           SIGN of the fitted f. Only the correct handedness can produce a
           positive focal length; the wrong one fits f < 0. Measured: s = -1
           (non-mirrored camera, player's right lands on image left) gives
           f = +456..+565 per subject, s = +1 gives f = -456. Decisive, and it
           is the check that would have caught a left/right joint swap.

  GATE 2b  RESIDUAL. With f fixed at the published value, the principal point is
           fitted PER TRIAL (median offset) and the residual measured on six
           well-tracked, identically-defined joints (shoulders, hips, knees,
           ankles). PASS if the median is < 15 px. Measured: 7.9 px.

           WHY THE PRINCIPAL POINT IS FITTED RATHER THAN ASSUMED: with the
           published cx = 328.9 the residual is a ~40 px CONSTANT horizontal
           offset -- the Kinect skeleton lands bodily to the right of the person
           in the RGB frame. Per-subject fits recover f = 515-565 (vs published
           525.6, so the SCALE is right) but cx = 273-308 and cy = 243-251, i.e.
           the depth->RGB registration in this dataset is off by ~33 px in u and
           ~7 px in v -- far more than the 2.5 cm stereo baseline can explain
           (~5 px at 2.9 m). That is a property of the dataset, not of our
           pipeline, and it is why the ROOT-ALIGNED metrics are the headline:
           it shifts the whole skeleton rigidly. NOTE the consequence for the
           ABSOLUTE metric: _absolute_root assumes the principal point is the
           image centre, so a 33 px error biases our recovered root x by
           33/525.6 * 2.85 m ~ 0.18 m. Absolute numbers below therefore carry a
           known ~0.2 m lateral offset that is the DATASET's, not the backend's.

  GATE 1   TEMPORAL. RGB and skeleton were captured on separate channels and
           drift: the video has consistently FEWER frames (mean 52.2 vs 67.7,
           ratio 0.77 +/- 0.04, never more). Depth and skeleton frame counts are
           identical, so the skeleton timeline is the depth timeline. The
           skeleton is linearly resampled onto the video timeline and a residual
           LAG is scanned by minimising the reprojection error of a FAST joint
           (R_WRIST) while the static joints hold the principal point fixed.
           Measured: a clean minimum at SKEL_LAG = -3 video frames (28.7 px,
           rising monotonically to 43.8 px at +4), so the video trails the
           skeleton by ~3 frames; the static-joint residual is flat at 7.9 px
           across the whole scan, confirming the lag is temporal and not a
           calibration artefact.

           RESIDUAL TIMING JITTER IS THE MAIN LIMIT ON FAST JOINTS. Even at the
           best lag the R_WRIST residual is ~27 px, versus 7.9 px for static
           joints; at 15 fps a swiping wrist moves 30-60 px per frame, so that
           is sub-frame misalignment, not a pose error. Two defences, both
           reported: (i) every metric is also computed on QUIET frames (Kinect
           speed for that joint < 2 cm/frame), and (ii) the headline hand/foot
           findings are stated as EXCESS OVER THE WRIST/ANKLE, which share the
           same timing jitter, so it cancels in the difference.

METRICS -- four alignments, because the choice of anchor turns out to matter
more here than anything else:
  Absolute      raw camera-space 3D distance (carries the GATE 2b dataset
                offset -- do not headline it).
  Root-aligned  both skeletons re-anchored on their OWN HipCenter. DC3's actual
                anchor, but see DEFINITIONAL OFFSETS below: Kinect's HipCenter
                sits ~8 cm ABOVE its own hip-joint line while ours is exactly
                on it, so this alignment injects a ~0.19 m vertical bias into
                EVERY other joint. Reported because it is what the game does,
                not because it is the fairest view.
  Body-aligned  per-frame translation that best matches the eight reliable body
                joints (shoulders, hips, knees, ankles). Removes any rigid
                offset without trusting one mis-defined joint: the SHAPE view.
  Limb-relative for the hand and foot specifically, the offset vector past the
                wrist / ankle. Immune to every global term, and the only view
                that answers the placement question directly.

QUIET FRAMES: a joint counts as quiet when BOTH skeletons move it less than
QUIET_SPEED per video frame. Necessary because the arms are only 26-39% quiet
in this corpus and the rest of the time residual sub-frame timing error at
15 fps dominates them -- wrist root-aligned error falls 0.52 -> 0.30 m under
the filter, while the legs (already 64-74% quiet) do not move at all. That
contrast is what shows the LEG numbers are timing-clean and the ARM numbers
are not.

REFERENCE QUALITY: because Kinect is a reference and not truth, the script also
measures the REFERENCE against the image -- each Kinect joint reprojected vs
MediaPipe's own 2D heatmap landmark. Kinect's knees/ankles land 2-6 px from the
visible joint but its wrists/hands land 42-57 px away (~0.25 m at 2.8 m). Hand
disagreement below that scale is inside the reference's own error bar and must
not be read as OUR error.

FOCUS QUESTIONS
  (a) HandL/R vs WristL/R. The wrist is the CONTROL -- a direct landmark copy,
      so its error is what every joint pays anyway. Hand minus wrist is the
      price of our synthesis, and the difference cancels shared timing jitter.
  (b) FootL/R vs AnkleL/R, same construction.
  (c) THE OFFSET VECTORS: Kinect's own (hand - wrist) and (foot - ankle) length
      and direction against ours -- the placement question in its purest form.
  (d) FOOT PITCH on stance frames: our heel-vs-toe height and the resulting
      foot elevation, against Kinect's.

PLACEMENT SWEEP: FOOT_TOE_BLEND and eight hand-placement rules swept in
LIMB-RELATIVE space (and root-aligned for comparison), so the output is
directly actionable.

DEFINITIONAL OFFSETS: the script prints each joint's mean HipCenter-relative
position for both skeletons side by side. This is where the derived-joint
mismatches surface -- Kinect's HipCenter, ShoulderCenter and Spine are NOT the
plain midpoints _remap builds them as.

-------------------------------------------------------------------------------
MEASURED (full run, 861 trials / 44,933 video frames / 8 subjects, 2026-08-01)

  GATES. Lag scan bottoms cleanly at -3 (16.2 px, rising monotonically both
  ways to 19.9/29.7 px at -6/+6). Mirror: f = +527 px for x_sign=-1 versus
  -527 px for x_sign=+1, against a published 529 -- the handedness is settled
  and the recovered focal length independently confirms the projection model.
  Static-joint residual 8.5 px median (PASS, threshold 15). Detection 100%.

  FEET: OUR SYNTHESIS IS RIGHT, AND FOOT_TOE_BLEND = 0.35 IS THE OPTIMUM.
  Kinect's own foot-minus-ankle offset is 0.079 m at -50 deg elevation
  (dy -0.059, dz -0.038: down AND forward, NOT straight down); ours is 0.079 m
  at -58 deg. Same length to 0.5 mm, 8 deg steeper. Sweeping the blend in
  limb-relative space gives a flat minimum at 0.30-0.35 (0.0320 m) -- the
  shipped value. FootL/R costs only +0.0066 m over its own AnkleL/R control.
  NOTE this REFUTES the justification written at pose_mediapipe.py:76-79: the
  single baked frame in StubCameraInput says 4.6 cm nearly straight down, but
  across 116 k real Kinect frames the offset is 7.9 cm at -50 deg, and it is
  strikingly stable (per-action mean 0.074-0.084 m, elevation -45 to -52 deg on
  all 27 actions). The blend value is right; the reason recorded for it is not.

  FOOT PITCH: our ~30 deg toe-down bias is REAL but SMALLER THAN THE
  REFERENCE'S OWN TILT. On 84 k stance frames our toe sits 6.3 cm below our
  heel (-26.6 deg), which is the bias bench_extremities.py found. But Kinect's
  own foot joint sits 6.0 cm below its ankle (-50.4 deg) and ours 6.6 cm
  (-57.8 deg): after the blend, the resulting DC3 FootL/R is only 6 mm lower
  and 7 deg steeper than the reference. The toe-down pitch does NOT translate
  into a comparable Foot-joint error, because the blend already absorbs most
  of it.

  HANDS: mildly over-extended, but the disagreement is dominated by the
  REFERENCE. Kinect's hand-minus-wrist is 0.068 m, ours 0.083 m -- we overshoot
  by 1.5 cm -- at an almost identical elevation (-42.3 vs -42.8 deg) and 20.6
  deg median direction difference on quiet frames. Sweeping wrist + a*(knuckle
  - wrist) bottoms at a ~= 0.7 (0.0408 m) versus 0.0492 m at the shipped a = 1
  (knuckle midpoint): an 8 mm mean improvement. Weigh that against the
  REFERENCE QUALITY row: Kinect's own hand joint sits 18.8 px (~10 cm) from the
  visible hand in the image, so an 8 mm placement tweak is far inside the
  reference's error bar. Real, in the right direction, not urgent.

  THE ACTUAL DEFECTS ARE IN THE THREE CENTRE JOINTS, not the extremities. In
  HipCenter-relative terms our whole skeleton is ~0.11 m too HIGH because
  Kinect's HipCenter is NOT the hip midpoint: it sits ~7 cm ABOVE the HipL/R
  line (confirmed independently by the baked capture, where HipCenter.y 0.1118
  > HipLeft.y 0.0333). Ours is exactly on the line. Likewise Kinect's
  ShoulderCenter sits ~11 cm above its own shoulder line (neck base) where ours
  is the plain midpoint, and Kinect's Spine is ~16% of the way up the torso
  where ours is the 50% midpoint (ours +0.237 vs Kinect +0.063). These three
  are pure definition errors in _remap, they are systematic on every frame, and
  because DC3 root-aligns on HipCenter they contaminate EVERY other joint --
  which is exactly why the root-aligned column reads worse than the
  body-aligned one for joints that are otherwise fine (KneeRight 0.162 ra vs
  0.067 body). Fixing them is a bigger win than anything in the hands or feet.

CACHING: raw landmarker output per video in <data>/cache/<trial>.npz
(world/img/vis/det). ~30 ms/frame CPU, 861 trials x ~52 frames ~ 25 min once;
analysis re-runs in seconds afterwards.

Run:
  .venv/bin/python tools/pose_corpus/bench_utd_mhad.py \
      --data-dir /home/free/tmp/pose_gt_utdmhad [--verify] [--gates-only]
      [--actions a1 a22] [--max-trials N] [--lag -3] [--recompute]
"""

import argparse
import glob
import os
import re
import sys

import cv2
import numpy as np
import scipy.io

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO, "native", "scripts"))
from pose_mediapipe import (  # noqa: E402
    MediaPipeBackend, DC3_JOINT_NAMES, NUM_DC3_JOINTS, FOOT_TOE_BLEND,
    HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD,
    SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT, HAND_LEFT,
    SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT, HAND_RIGHT,
    HIP_LEFT, KNEE_LEFT, ANKLE_LEFT, HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT,
    FOOT_LEFT, FOOT_RIGHT,
    L_SHOULDER, R_SHOULDER, L_ELBOW, R_ELBOW, L_HIP, R_HIP, L_KNEE, R_KNEE,
    L_WRIST, R_WRIST, L_PINKY, R_PINKY, L_INDEX, R_INDEX,
    L_ANKLE, R_ANKLE, L_HEEL, R_HEEL, L_FOOT, R_FOOT,
)

MODEL_PATH = os.path.join(_REPO, "native", "models", "pose_landmarker_full.task")

# --- UTD-MHAD joint index (0-based) -> DC3 joint index ------------------------
UTD_NAMES = [
    "head", "shoulder_center", "spine", "hip_center",
    "L_shoulder", "L_elbow", "L_wrist", "L_hand",
    "R_shoulder", "R_elbow", "R_wrist", "R_hand",
    "L_hip", "L_knee", "L_ankle", "L_foot",
    "R_hip", "R_knee", "R_ankle", "R_foot",
]
UTD_TO_DC3 = [
    HEAD, SHOULDER_CENTER, SPINE, HIP_CENTER,
    SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT, HAND_LEFT,
    SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT, HAND_RIGHT,
    HIP_LEFT, KNEE_LEFT, ANKLE_LEFT, FOOT_LEFT,
    HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT, FOOT_RIGHT,
]
UTD_RWRI, UTD_LWRI = 10, 6

# --- Kinect v1 RGB intrinsics (Burrus), 640x480 -------------------------------
# Only the FOCAL LENGTH is used as published; the principal point is fitted per
# trial (see GATE 2b in the module docstring for why).
RGB_FX, RGB_FY = 529.215, 525.564
RGB_CX_NOMINAL, RGB_CY_NOMINAL = 328.942, 267.481

# Video trails the skeleton by this many video frames (GATE 1, measured).
SKEL_LAG = -3

# Kinect joints that are (i) well tracked and (ii) defined the same way by
# BlazePose, so their reprojection residual measures alignment and nothing else.
GATE_PAIRS = [  # (mediapipe landmark idx, utd joint idx)
    (L_SHOULDER, 4), (R_SHOULDER, 8), (L_HIP, 12), (R_HIP, 16),
    (L_KNEE, 13), (R_KNEE, 17), (L_ANKLE, 14), (R_ANKLE, 18),
]

# "Quiet" = BOTH skeletons move that joint less than this, metres per video frame.
QUIET_SPEED = 0.02

# Joints used to fit the body-aligned translation: big, reliably tracked by both
# systems, and not derived by either. NOT the hip/shoulder CENTRES, which is the
# whole point (see DEFINITIONAL OFFSETS).
BODY_ANCHOR = [SHOULDER_LEFT, SHOULDER_RIGHT, HIP_LEFT, HIP_RIGHT,
               KNEE_LEFT, KNEE_RIGHT, ANKLE_LEFT, ANKLE_RIGHT]

# MediaPipe 2D landmark(s) whose mean is the image evidence for a DC3 joint,
# used by the REFERENCE QUALITY section.
MP_EVIDENCE = {
    SHOULDER_LEFT: [L_SHOULDER], SHOULDER_RIGHT: [R_SHOULDER],
    ELBOW_LEFT: [L_ELBOW], ELBOW_RIGHT: [R_ELBOW],
    WRIST_LEFT: [L_WRIST], WRIST_RIGHT: [R_WRIST],
    HAND_LEFT: [L_PINKY, L_INDEX], HAND_RIGHT: [R_PINKY, R_INDEX],
    HIP_LEFT: [L_HIP], HIP_RIGHT: [R_HIP],
    KNEE_LEFT: [L_KNEE], KNEE_RIGHT: [R_KNEE],
    ANKLE_LEFT: [L_ANKLE], ANKLE_RIGHT: [R_ANKLE],
}

TRIAL_RE = re.compile(r"^a(\d+)_s(\d+)_t(\d+)$")


# ---------------------------------------------------------------------------
# dataset IO
# ---------------------------------------------------------------------------
def find_trials(data_dir):
    """Trial ids having BOTH a skeleton .mat and a colour .avi, in action order."""
    skel = {os.path.basename(p)[: -len("_skeleton.mat")]
            for p in glob.glob(os.path.join(data_dir, "**", "*_skeleton.mat"),
                               recursive=True) if "Sample_Code" not in p}
    vid = {os.path.basename(p)[: -len("_color.avi")]
           for p in glob.glob(os.path.join(data_dir, "**", "*_color.avi"),
                              recursive=True)}

    def key(t):
        m = TRIAL_RE.match(t)
        return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (99, 99, 99)

    return sorted(skel & vid, key=key), sorted(skel - vid), sorted(vid - skel)


def path_for(data_dir, trial, kind):
    hits = [p for p in glob.glob(os.path.join(data_dir, "**", f"{trial}_{kind}"),
                                 recursive=True) if "Sample_Code" not in p]
    return hits[0] if hits else None


def load_skeleton(mat_path):
    """(20,3,F) UTD order -> (F,20,3) DC3 order, metres, DC3 camera axes.

    Pure permutation: Kinect skeleton space IS DC3 camera space (docstring).
    """
    s = scipy.io.loadmat(mat_path)["d_skel"].astype(np.float64)
    utd = np.transpose(s, (2, 0, 1))
    out = np.empty_like(utd)
    out[:, UTD_TO_DC3, :] = utd
    return out


def resample_to(skel, n_out, lag=0):
    """Linearly resample (Ns,20,3) onto n_out video frames, offset by `lag`
    VIDEO frames (GATE 1). lag<0 samples the skeleton earlier."""
    n_in = skel.shape[0]
    if n_in == 1:
        return np.repeat(skel, n_out, axis=0)
    step = (n_in - 1.0) / max(n_out - 1.0, 1.0)
    t = np.clip((np.arange(n_out) + lag) * step, 0.0, n_in - 1.0)
    lo = np.clip(np.floor(t).astype(int), 0, n_in - 2)
    frac = (t - lo)[:, None, None]
    return skel[lo] * (1.0 - frac) + skel[lo + 1] * frac


# ---------------------------------------------------------------------------
# projection (GATE 2)
# ---------------------------------------------------------------------------
def project_dc3(pts, W, H, cx, cy, x_sign=-1.0):
    """DC3/Kinect camera-space metres -> RGB pixels, given a principal point.

    Skeleton space is +y UP so v always flips. The horizontal sign is what GATE
    2a determines: x_sign=-1 is a NON-mirrored camera, where the player's
    anatomical right (at +x) lands on the image LEFT.
    """
    sx, sy = W / 640.0, H / 480.0
    z = np.maximum(pts[..., 2], 1e-6)
    u = cx + (RGB_FX * sx) * (x_sign * pts[..., 0]) / z
    v = cy + (RGB_FY * sy) * (-pts[..., 1]) / z
    return np.stack([u, v], axis=-1)


def fit_principal_point(mp_px, kin, W, H, x_sign=-1.0):
    """Median (cx, cy) that maps the Kinect skeleton onto MediaPipe's 2D, with
    the focal length held at the published value. See GATE 2b."""
    sx, sy = W / 640.0, H / 480.0
    du, dv = [], []
    for mp_i, utd_i in GATE_PAIRS:
        j = UTD_TO_DC3[utd_i]
        z = np.maximum(kin[:, j, 2], 1e-6)
        du.append(mp_px[:, mp_i, 0] - (RGB_FX * sx) * (x_sign * kin[:, j, 0]) / z)
        dv.append(mp_px[:, mp_i, 1] - (RGB_FY * sy) * (-kin[:, j, 1]) / z)
    return float(np.median(np.concatenate(du))), float(np.median(np.concatenate(dv)))


def rgb_hfov_deg(W=640):
    return float(np.degrees(2.0 * np.arctan((W / 2.0) / RGB_FX)))


# ---------------------------------------------------------------------------
# inference
# ---------------------------------------------------------------------------
def _ts_base(be):
    """Monotonic per-trial timestamp base: VIDEO mode requires strictly
    increasing timestamps and one landmarker is reused across trials."""
    be._utd_ts = getattr(be, "_utd_ts", 0) + 10_000
    return be._utd_ts


def run_landmarker(video_path, cache_path, backend=None):
    """Cache the production PoseLandmarker's raw output for one video.

    Mirrors MediaPipeBackend.process() and bench_model_z.run_landmarker: VIDEO
    running mode, num_poses=1, min_conf 0.5, BGR->RGB, frame-index timestamps.
    """
    if os.path.exists(cache_path):
        d = np.load(cache_path)
        return {k: d[k] for k in d.files}

    own = backend is None
    be = backend or MediaPipeBackend(MODEL_PATH, num_poses=1)
    be.reset_tracks()
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"cannot open {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 15.0
    period_ms = 1000.0 / fps
    base = _ts_base(be)

    world, img, vis, det = [], [], [], []
    i, W, H = 0, 640, 480
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        H, W = frame.shape[:2]
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_img = be._mp.Image(image_format=be._mp.ImageFormat.SRGB, data=rgb)
        res = be._landmarker.detect_for_video(mp_img, int(base + i * period_ms))
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
    cap.release()
    if own:
        be.close()

    out = dict(world=np.asarray(world, dtype=np.float32).reshape(-1, 33, 3),
               img=np.asarray(img, dtype=np.float32).reshape(-1, 33, 2),
               vis=np.asarray(vis, dtype=np.float32).reshape(-1, 33),
               det=np.asarray(det, dtype=bool),
               fps=np.float64(fps),
               frame_wh=np.array([W, H]))
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    np.savez_compressed(cache_path, **out)
    return out


def backend_geometry(geom_be, cache, W, H):
    """Replay the production per-frame geometry on cached landmarks.

    Returns (joints (F,20,3), all-33 landmarks in DC3 camera space (F,33,3),
    valid mask). The 33 landmarks are needed for the placement sweep and the
    foot-pitch probe; they use the same recovered root, so nothing is
    reimplemented.
    """
    F = len(cache["det"])
    joints = np.full((F, NUM_DC3_JOINTS, 3), np.nan)
    lms = np.full((F, 33, 3), np.nan)
    valid = np.zeros(F, dtype=bool)
    for i in range(F):
        if not cache["det"][i]:
            continue
        world = cache["world"][i].astype(np.float64)
        image_xy = cache["img"][i].astype(np.float64)
        vis = cache["vis"][i].astype(np.float64)
        root = geom_be._absolute_root(world, image_xy, vis, W, H)
        if root is None:
            continue
        joints[i], _ = geom_be._remap(world, vis, root)
        lms[i, :, 0] = -world[:, 0] + root[0]
        lms[i, :, 1] = -world[:, 1] + root[1]
        lms[i, :, 2] = world[:, 2] + root[2]
        valid[i] = True
    return joints, lms, valid


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def cat(a):
    return np.concatenate(a) if a else np.array([])


def stats_row(name, e, width=26):
    e = np.asarray(e)
    e = e[np.isfinite(e)]
    if e.size == 0:
        return f"  {name:<{width}s}       (no data)"
    return (f"  {name:<{width}s} n={e.size:7d}  mean {e.mean():+.4f}  "
            f"p50 {np.percentile(e, 50):+.4f}  p90 {np.percentile(e, 90):+.4f}")


def angle_between(a, b):
    na, nb = np.linalg.norm(a, axis=-1), np.linalg.norm(b, axis=-1)
    c = np.sum(a * b, axis=-1) / np.maximum(na * nb, 1e-9)
    return np.degrees(np.arccos(np.clip(c, -1.0, 1.0)))


def speed(track):
    """Per-frame displacement of an (F,3) track, first frame duplicated."""
    if len(track) < 2:
        return np.zeros(len(track))
    d = np.linalg.norm(np.diff(track, axis=0), axis=-1)
    return np.concatenate([d[:1], d])


def elevation(v):
    """Degrees above horizontal: +90 straight up, -90 straight down."""
    return np.degrees(np.arctan2(v[..., 1], np.linalg.norm(v[..., [0, 2]], axis=-1)))


# ---------------------------------------------------------------------------
# --verify: the joint-order evidence
# ---------------------------------------------------------------------------
def verify_joint_order(data_dir, trials):
    print("\n" + "=" * 78)
    print("JOINT-ORDER VERIFICATION (Skeleton_joint_order.txt vs the data)")
    print("=" * 78)
    ys = [scipy.io.loadmat(path_for(data_dir, t, "skeleton.mat"))["d_skel"][:, 1, :]
          .mean(axis=1) for t in trials[:200]]
    ys = np.mean(ys, axis=0)
    order = np.argsort(-ys)
    print(f"  highest mean y {UTD_NAMES[order[0]]} ({ys[order[0]]:+.3f}), "
          f"lowest {UTD_NAMES[order[-1]]} ({ys[order[-1]]:+.3f})  "
          f"-> head is highest: {UTD_NAMES[order[0]] == 'head'}")
    for act, label in (("a1", "right arm swipe left"), ("a3", "right hand wave"),
                       ("a5", "right arm throw")):
        lm, rm = [], []
        for t in [x for x in trials if x.startswith(act + "_")]:
            s = scipy.io.loadmat(path_for(data_dir, t, "skeleton.mat"))["d_skel"]
            lm.append(np.linalg.norm(np.diff(s[UTD_LWRI], axis=1), axis=0).sum())
            rm.append(np.linalg.norm(np.diff(s[UTD_RWRI], axis=1), axis=0).sum())
        if lm:
            print(f"  {act} ({label:20s}) path len L_wrist {np.mean(lm):5.2f} m  "
                  f"R_wrist {np.mean(rm):5.2f} m  "
                  f"ratio {np.mean(rm)/max(np.mean(lm),1e-6):5.1f}x")
    m = np.nanmean(load_skeleton(path_for(data_dir, trials[0], "skeleton.mat")), axis=0)
    print(f"  bone lengths ({trials[0]}): torso "
          f"{np.linalg.norm(m[SHOULDER_CENTER]-m[HIP_CENTER]):.3f} m, shoulder span "
          f"{np.linalg.norm(m[SHOULDER_LEFT]-m[SHOULDER_RIGHT]):.3f} m, forearm "
          f"{np.linalg.norm(m[ELBOW_LEFT]-m[WRIST_LEFT]):.3f} m")
    print(f"  handedness: ShoulderLeft.x {m[SHOULDER_LEFT][0]:+.3f} < ShoulderRight.x "
          f"{m[SHOULDER_RIGHT][0]:+.3f} -> player-left = -X, matching DC3's baked "
          f"capture (-0.047 < +0.322)")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", default="/home/free/tmp/pose_gt_utdmhad")
    ap.add_argument("--cache-dir", default=None)
    ap.add_argument("--trials", nargs="*", default=None)
    ap.add_argument("--actions", nargs="*", default=None)
    ap.add_argument("--max-trials", type=int, default=0)
    ap.add_argument("--lag", type=int, default=SKEL_LAG)
    ap.add_argument("--lag-scan", action="store_true",
                    help="scan the temporal lag instead of using --lag")
    ap.add_argument("--recompute", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--gates-only", action="store_true")
    ap.add_argument("--hfov", type=float, default=None)
    args = ap.parse_args()

    cache_dir = args.cache_dir or os.path.join(args.data_dir, "cache")
    trials, skel_only, vid_only = find_trials(args.data_dir)
    if not trials:
        sys.exit(f"no trials found under {args.data_dir}")
    if args.actions:
        trials = [t for t in trials if t.split("_")[0] in set(args.actions)]
    if args.trials:
        trials = [t for t in trials if t in set(args.trials)]
    if args.max_trials:
        trials = trials[: args.max_trials]

    print(f"UTD-MHAD: {len(trials)} paired trials "
          f"(skeleton-only {len(skel_only)}, video-only {len(vid_only)})")
    if args.verify:
        verify_joint_order(args.data_dir, trials)

    hfov = args.hfov if args.hfov is not None else rgb_hfov_deg()
    print(f"backend hFOV {hfov:.2f} deg (published Kinect RGB fx {RGB_FX:.1f} px "
          f"@640; DC3's shipping default is {MediaPipeBackend.DEFAULT_HFOV_DEG:.2f})")
    print(f"skeleton lag {args.lag:+d} video frames")

    if args.recompute:
        for t in trials:
            p = os.path.join(cache_dir, f"{t}.npz")
            if os.path.exists(p):
                os.remove(p)

    infer_be = MediaPipeBackend(MODEL_PATH, num_poses=1, hfov_deg=hfov)
    geom_be = MediaPipeBackend(MODEL_PATH, num_poses=1, hfov_deg=hfov)

    # accumulators ---------------------------------------------------------
    count_v, count_s, det_rate = [], [], []
    pp_fit, mirror_f = [], {-1.0: [], +1.0: []}
    gate_static, gate_fast = [], {l: [] for l in range(-6, 7)}
    abs_err = [[] for _ in range(NUM_DC3_JOINTS)]
    ral_err = [[] for _ in range(NUM_DC3_JOINTS)]
    bod_err = [[] for _ in range(NUM_DC3_JOINTS)]
    bod_quiet = [[] for _ in range(NUM_DC3_JOINTS)]
    rel_pos_o = [[] for _ in range(NUM_DC3_JOINTS)]
    rel_pos_k = [[] for _ in range(NUM_DC3_JOINTS)]
    ref_px = {j: [] for j in MP_EVIDENCE}
    off = {k: [] for k in ("k_hand", "o_hand", "k_foot", "o_foot")}
    ang = {"hand": [], "foot": []}
    sweep_foot = {b: [] for b in np.round(np.arange(-0.4, 1.01, 0.1), 2)}
    sweep_foot_rel = {b: [] for b in np.round(np.arange(-0.4, 1.01, 0.1), 2)}
    sweep_hand = {k: [] for k in ("wrist", "knuckle_mid", "index", "pinky",
                                  "w+0.25k", "w+0.50k", "w+0.75k", "w+1.25k")}
    sweep_hand_rel = {k: [] for k in sweep_hand}
    pitch = {k: [] for k in ("ours_toe_minus_heel_y", "ours_heel_toe_elev",
                             "ours_foot_minus_ankle_y", "ours_foot_elev",
                             "kin_foot_minus_ankle_y", "kin_foot_elev")}
    root_bias = []

    for n, t in enumerate(trials):
        cache = run_landmarker(path_for(args.data_dir, t, "color.avi"),
                               os.path.join(cache_dir, f"{t}.npz"), infer_be)
        W, H = (int(x) for x in cache["frame_wh"])
        skel = load_skeleton(path_for(args.data_dir, t, "skeleton.mat"))
        Nv = len(cache["det"])
        count_v.append(Nv); count_s.append(len(skel))
        det_rate.append(float(cache["det"].mean()))

        kin = resample_to(skel, Nv, args.lag)
        ours, lms, valid = backend_geometry(geom_be, cache, W, H)
        ok = valid & np.isfinite(kin).all(axis=(1, 2))
        if ok.sum() < 5:
            continue
        mp_px = cache["img"][ok].astype(np.float64) * np.array([W, H])
        o, k, L = ours[ok], kin[ok], lms[ok]

        # -- GATE 2a: mirror. Fitted focal length must be positive. ---------
        for sign in (-1.0, +1.0):
            uu, xx = [], []
            for mp_i, utd_i in GATE_PAIRS:
                j = UTD_TO_DC3[utd_i]
                uu.append(mp_px[:, mp_i, 0]); xx.append(sign * k[:, j, 0] / k[:, j, 2])
            uu, xx = np.concatenate(uu), np.concatenate(xx)
            A = np.stack([np.ones_like(uu), xx], 1)
            mirror_f[sign].append(np.linalg.lstsq(A, uu, rcond=None)[0][1])

        # -- GATE 2b: per-trial principal point, static-joint residual ------
        cx, cy = fit_principal_point(mp_px, k, W, H)
        pp_fit.append((cx, cy))
        uv = project_dc3(k, W, H, cx, cy)
        for mp_i, utd_i in GATE_PAIRS:
            gate_static.append(np.linalg.norm(mp_px[:, mp_i] - uv[:, UTD_TO_DC3[utd_i]],
                                              axis=-1))

        # -- GATE 1: lag scan on a FAST joint, principal point held fixed ----
        if args.lag_scan:
            for lg in gate_fast:
                kl = resample_to(skel, Nv, lg)[ok]
                uvl = project_dc3(kl, W, H, cx, cy)
                gate_fast[lg].append(np.linalg.norm(
                    mp_px[:, R_WRIST] - uvl[:, WRIST_RIGHT], axis=-1))

        if args.gates_only:
            if n % 100 == 0:
                print(f"  ...{n+1}/{len(trials)}", flush=True)
            continue

        # -- REFERENCE QUALITY: Kinect vs the image's own evidence -----------
        for j, idxs in MP_EVIDENCE.items():
            ref_px[j].append(np.linalg.norm(uv[:, j] - mp_px[:, idxs].mean(1),
                                            axis=-1))

        # -- per-joint error, three alignments -------------------------------
        ra_o = o - o[:, HIP_CENTER:HIP_CENTER + 1, :]
        ra_k = k - k[:, HIP_CENTER:HIP_CENTER + 1, :]
        # body-aligned: per-frame translation matching the anchor joints only
        shift = (k[:, BODY_ANCHOR] - o[:, BODY_ANCHOR]).mean(axis=1, keepdims=True)
        d_abs = np.linalg.norm(o - k, axis=-1)
        d_ral = np.linalg.norm(ra_o - ra_k, axis=-1)
        d_bod = np.linalg.norm((o + shift) - k, axis=-1)
        root_bias.append(o[:, HIP_CENTER] - k[:, HIP_CENTER])
        for j in range(NUM_DC3_JOINTS):
            abs_err[j].append(d_abs[:, j])
            ral_err[j].append(d_ral[:, j])
            bod_err[j].append(d_bod[:, j])
            rel_pos_o[j].append(ra_o[:, j])
            rel_pos_k[j].append(ra_k[:, j])
            q = (speed(k[:, j]) < QUIET_SPEED) & (speed(o[:, j]) < QUIET_SPEED)
            if q.any():
                bod_quiet[j].append(d_bod[q, j])

        # -- (c) offset vectors ----------------------------------------------
        off["k_hand"].append(np.concatenate([k[:, HAND_LEFT] - k[:, WRIST_LEFT],
                                             k[:, HAND_RIGHT] - k[:, WRIST_RIGHT]]))
        off["o_hand"].append(np.concatenate([o[:, HAND_LEFT] - o[:, WRIST_LEFT],
                                             o[:, HAND_RIGHT] - o[:, WRIST_RIGHT]]))
        off["k_foot"].append(np.concatenate([k[:, FOOT_LEFT] - k[:, ANKLE_LEFT],
                                             k[:, FOOT_RIGHT] - k[:, ANKLE_RIGHT]]))
        off["o_foot"].append(np.concatenate([o[:, FOOT_LEFT] - o[:, ANKLE_LEFT],
                                             o[:, FOOT_RIGHT] - o[:, ANKLE_RIGHT]]))
        # The |v| and elevation stats are each self-consistent within one
        # skeleton, so timing cannot corrupt them; the ANGLE between the two
        # offsets can, because a limb mid-swing rotates. Angles use quiet frames.
        qwl = ((speed(k[:, WRIST_LEFT]) < QUIET_SPEED)
               & (speed(o[:, WRIST_LEFT]) < QUIET_SPEED))
        qwr = ((speed(k[:, WRIST_RIGHT]) < QUIET_SPEED)
               & (speed(o[:, WRIST_RIGHT]) < QUIET_SPEED))
        qal = ((speed(k[:, ANKLE_LEFT]) < QUIET_SPEED)
               & (speed(o[:, ANKLE_LEFT]) < QUIET_SPEED))
        qar = ((speed(k[:, ANKLE_RIGHT]) < QUIET_SPEED)
               & (speed(o[:, ANKLE_RIGHT]) < QUIET_SPEED))
        ang["hand"].append(np.concatenate([
            angle_between(o[qwl][:, HAND_LEFT] - o[qwl][:, WRIST_LEFT],
                          k[qwl][:, HAND_LEFT] - k[qwl][:, WRIST_LEFT]),
            angle_between(o[qwr][:, HAND_RIGHT] - o[qwr][:, WRIST_RIGHT],
                          k[qwr][:, HAND_RIGHT] - k[qwr][:, WRIST_RIGHT])]))
        ang["foot"].append(np.concatenate([
            angle_between(o[qal][:, FOOT_LEFT] - o[qal][:, ANKLE_LEFT],
                          k[qal][:, FOOT_LEFT] - k[qal][:, ANKLE_LEFT]),
            angle_between(o[qar][:, FOOT_RIGHT] - o[qar][:, ANKLE_RIGHT],
                          k[qar][:, FOOT_RIGHT] - k[qar][:, ANKLE_RIGHT])]))

        # -- placement sweep, LIMB-RELATIVE (immune to every global term) and
        #    root-aligned (what the whole-skeleton metric would see) ---------
        hip = o[:, HIP_CENTER]
        koff_fl = k[:, FOOT_LEFT] - k[:, ANKLE_LEFT]
        koff_fr = k[:, FOOT_RIGHT] - k[:, ANKLE_RIGHT]
        for b in sweep_foot:
            fl = L[:, L_HEEL] * (1 - b) + L[:, L_FOOT] * b
            fr = L[:, R_HEEL] * (1 - b) + L[:, R_FOOT] * b
            sweep_foot[b].append(np.concatenate([
                np.linalg.norm((fl - hip) - ra_k[:, FOOT_LEFT], axis=-1),
                np.linalg.norm((fr - hip) - ra_k[:, FOOT_RIGHT], axis=-1)]))
            sweep_foot_rel[b].append(np.concatenate([
                np.linalg.norm((fl - o[:, ANKLE_LEFT]) - koff_fl, axis=-1),
                np.linalg.norm((fr - o[:, ANKLE_RIGHT]) - koff_fr, axis=-1)]))
        kn_l = (L[:, L_PINKY] + L[:, L_INDEX]) * 0.5
        kn_r = (L[:, R_PINKY] + L[:, R_INDEX]) * 0.5
        cand = {"wrist": (L[:, L_WRIST], L[:, R_WRIST]),
                "knuckle_mid": (kn_l, kn_r),
                "index": (L[:, L_INDEX], L[:, R_INDEX]),
                "pinky": (L[:, L_PINKY], L[:, R_PINKY])}
        for a in (0.25, 0.50, 0.75, 1.25):
            cand[f"w+{a:.2f}k"] = (L[:, L_WRIST] + a * (kn_l - L[:, L_WRIST]),
                                   L[:, R_WRIST] + a * (kn_r - L[:, R_WRIST]))
        koff_hl = k[:, HAND_LEFT] - k[:, WRIST_LEFT]
        koff_hr = k[:, HAND_RIGHT] - k[:, WRIST_RIGHT]
        qh = ((speed(k[:, WRIST_LEFT]) < QUIET_SPEED)
              & (speed(o[:, WRIST_LEFT]) < QUIET_SPEED))
        qhr = ((speed(k[:, WRIST_RIGHT]) < QUIET_SPEED)
               & (speed(o[:, WRIST_RIGHT]) < QUIET_SPEED))
        for key, (cl, cr) in cand.items():
            sweep_hand[key].append(np.concatenate([
                np.linalg.norm((cl - hip) - ra_k[:, HAND_LEFT], axis=-1),
                np.linalg.norm((cr - hip) - ra_k[:, HAND_RIGHT], axis=-1)]))
            # limb-relative sweep on QUIET frames only: an arm mid-swipe would
            # otherwise be scored against a Kinect wrist half a frame away.
            sweep_hand_rel[key].append(np.concatenate([
                np.linalg.norm((cl[qh] - o[qh][:, WRIST_LEFT]) - koff_hl[qh], axis=-1),
                np.linalg.norm((cr[qhr] - o[qhr][:, WRIST_RIGHT]) - koff_hr[qhr],
                               axis=-1)]))

        # -- (d) foot pitch on stance frames ---------------------------------
        for heel, toe, ankle, foot in ((L_HEEL, L_FOOT, ANKLE_LEFT, FOOT_LEFT),
                                       (R_HEEL, R_FOOT, ANKLE_RIGHT, FOOT_RIGHT)):
            st = speed(k[:, ankle]) < QUIET_SPEED
            if not st.any():
                continue
            h, tt = L[st][:, heel], L[st][:, toe]
            pitch["ours_toe_minus_heel_y"].append(tt[:, 1] - h[:, 1])
            pitch["ours_heel_toe_elev"].append(elevation(tt - h))
            ov = o[st][:, foot] - o[st][:, ankle]
            pitch["ours_foot_minus_ankle_y"].append(ov[:, 1])
            pitch["ours_foot_elev"].append(elevation(ov))
            kv = k[st][:, foot] - k[st][:, ankle]
            pitch["kin_foot_minus_ankle_y"].append(kv[:, 1])
            pitch["kin_foot_elev"].append(elevation(kv))

        if n % 100 == 0:
            print(f"  ...{n+1}/{len(trials)}", flush=True)

    infer_be.close(); geom_be.close()

    # ---------------- gates ------------------------------------------------
    print("\n" + "=" * 78)
    print("SANITY GATES")
    print("=" * 78)
    cv_, cs_ = np.array(count_v), np.array(count_s)
    print(f"GATE 1  frames: video mean {cv_.mean():.1f}, skeleton mean {cs_.mean():.1f}, "
          f"ratio {np.mean(cv_/cs_):.3f} +/- {np.std(cv_/cs_):.3f} "
          f"(video is never longer: max delta {int((cv_-cs_).max())})")
    print(f"        MediaPipe detection rate {np.mean(det_rate)*100:.1f}% of frames")
    if args.lag_scan:
        for lg in sorted(gate_fast):
            d = cat(gate_fast[lg])
            print(f"        lag {lg:+2d}: R_WRIST reproj median {np.median(d):5.1f} px")
        bl = min(gate_fast, key=lambda l: np.median(cat(gate_fast[l])))
        print(f"        best lag {bl:+d} (in use: {args.lag:+d})")
    fneg, fpos = np.mean(mirror_f[-1.0]), np.mean(mirror_f[+1.0])
    print(f"GATE 2a mirror: fitted focal length is {fneg:+.0f} px for x_sign=-1 "
          f"(non-mirrored) and {fpos:+.0f} px for x_sign=+1")
    print(f"        -> x_sign=-1 is the physical solution "
          f"({'PASS' if fneg > 0 and fpos < 0 else 'FAIL'}: only it fits f>0). "
          f"Published fx {RGB_FX:.0f}.")
    pp = np.array(pp_fit)
    gs = cat(gate_static)
    print(f"GATE 2b reprojection, static joints, published f + per-trial principal "
          f"point:")
    print(f"        median {np.median(gs):.1f} px, p90 {np.percentile(gs,90):.1f} px  "
          f"({'PASS' if np.median(gs) < 15 else 'FAIL'}; threshold 15 px)")
    print(f"        fitted principal point cx {pp[:,0].mean():.1f} +/- {pp[:,0].std():.1f}, "
          f"cy {pp[:,1].mean():.1f} +/- {pp[:,1].std():.1f} "
          f"(published {RGB_CX_NOMINAL:.1f}, {RGB_CY_NOMINAL:.1f}; "
          f"image centre 320.0, 240.0)")
    print(f"        => the dataset's depth->RGB registration is off by "
          f"{320.0-pp[:,0].mean():+.0f} px in u, {240.0-pp[:,1].mean():+.0f} px in v; "
          f"_absolute_root assumes the image centre, so ABSOLUTE x carries a "
          f"~{abs(320.0-pp[:,0].mean())/RGB_FX*2.85:.2f} m dataset bias. Read the "
          f"root-aligned columns.")
    if args.gates_only:
        return

    rb = np.concatenate(root_bias)
    print(f"        measured HipCenter offset (ours - Kinect): "
          f"dx {rb[:,0].mean():+.3f}  dy {rb[:,1].mean():+.3f}  dz {rb[:,2].mean():+.3f} m")

    # ---------------- reference quality ------------------------------------
    print("\n" + "=" * 78)
    print("REFERENCE QUALITY: Kinect's own joints vs the image (px, median)")
    print("  distance from the Kinect joint's reprojection to MediaPipe's 2D")
    print("  heatmap landmark for the same body part. This bounds how much of")
    print("  any disagreement can even be OURS.")
    print("=" * 78)
    for j in sorted(ref_px):
        d = cat(ref_px[j])
        print(f"  {DC3_JOINT_NAMES[j]:16s} {np.median(d):6.1f} px  "
              f"(~{np.median(d)/RGB_FX*2.85*100:5.1f} cm at 2.85 m)")

    # ---------------- definitional offsets ---------------------------------
    print("\n" + "=" * 78)
    print("DEFINITIONAL OFFSETS: mean HipCenter-relative position (metres)")
    print("  a large, CONSISTENT delta here is a placement definition mismatch,")
    print("  not tracking error")
    print("=" * 78)
    print(f"  {'joint':16s} {'ours x':>8s}{'y':>8s}{'z':>8s}   "
          f"{'Kinect x':>8s}{'y':>8s}{'z':>8s}   {'delta x':>8s}{'y':>8s}{'z':>8s}")
    for j in range(NUM_DC3_JOINTS):
        a = np.concatenate(rel_pos_o[j]).mean(0)
        b = np.concatenate(rel_pos_k[j]).mean(0)
        d = a - b
        print(f"  {DC3_JOINT_NAMES[j]:16s} {a[0]:+8.3f}{a[1]:+8.3f}{a[2]:+8.3f}   "
              f"{b[0]:+8.3f}{b[1]:+8.3f}{b[2]:+8.3f}   "
              f"{d[0]:+8.3f}{d[1]:+8.3f}{d[2]:+8.3f}")

    # ---------------- per-joint table --------------------------------------
    print("\n" + "=" * 78)
    print("PER-JOINT DISAGREEMENT WITH THE KINECT REFERENCE (metres)")
    print("  * = synthesised/derived by us")
    print("  abs = raw; ra = HipCenter-aligned (DC3's anchor, carries the")
    print("  HipCenter definition mismatch); body = translation-aligned on the")
    print("  8 anchor joints (SHAPE view, headline); quiet = body-aligned on")
    print(f"  frames where BOTH skeletons move that joint < {QUIET_SPEED*100:.0f} cm/frame")
    print("=" * 78)
    print(f"  {'joint':16s} {'abs mean':>9s} {'ra mean':>9s} {'body mean':>9s} "
          f"{'body p50':>9s} {'body p90':>9s} {'quiet':>9s} {'quiet%':>7s}")
    synth = {HAND_LEFT, HAND_RIGHT, FOOT_LEFT, FOOT_RIGHT, SPINE, HEAD,
             HIP_CENTER, SHOULDER_CENTER}
    per_j = {}
    for j in range(NUM_DC3_JOINTS):
        a, r, b, q = (cat(abs_err[j]), cat(ral_err[j]), cat(bod_err[j]),
                      cat(bod_quiet[j]))
        per_j[j] = (a, r, b, q)
        print(f"  {DC3_JOINT_NAMES[j] + ('*' if j in synth else ' '):16s} "
              f"{a.mean():9.4f} {r.mean():9.4f} {b.mean():9.4f} "
              f"{np.percentile(b,50):9.4f} {np.percentile(b,90):9.4f} "
              f"{(q.mean() if q.size else np.nan):9.4f} "
              f"{(100.0*q.size/max(b.size,1)):6.1f}%")

    # ---------------- (a) and (b) ------------------------------------------
    print("\n" + "=" * 78)
    print("(a) HAND vs WRIST   (b) FOOT vs ANKLE")
    print("    the control is a DIRECT landmark copy, so the excess is the price")
    print("    of our synthesis -- and it cancels shared timing jitter")
    print("=" * 78)
    for label, syn, ctl in (("Hand", (HAND_LEFT, HAND_RIGHT), (WRIST_LEFT, WRIST_RIGHT)),
                            ("Foot", (FOOT_LEFT, FOOT_RIGHT), (ANKLE_LEFT, ANKLE_RIGHT))):
        for col, idx in (("abs  ", 0), ("ra   ", 1), ("body ", 2), ("quiet", 3)):
            s = np.concatenate([per_j[j][idx] for j in syn])
            c = np.concatenate([per_j[j][idx] for j in ctl])
            if s.size == 0 or c.size == 0:
                continue
            print(f"  {label:5s} {col} synth {s.mean():.4f}  control {c.mean():.4f}  "
                  f"excess {s.mean()-c.mean():+.4f} m  ({s.mean()/c.mean():.2f}x)")

    # ---------------- (c) offset vectors -----------------------------------
    print("\n" + "=" * 78)
    print("(c) OFFSET VECTORS -- what does the reference put past wrist / ankle?")
    print("    elev: +90 = straight up, -90 = straight down; -z = toward camera")
    print("=" * 78)
    for label, kk, oo, akey in (("hand - wrist", "k_hand", "o_hand", "hand"),
                                ("foot - ankle", "k_foot", "o_foot", "foot")):
        for who, key in (("Kinect", kk), ("ours  ", oo)):
            v = np.concatenate(off[key])
            nrm = np.linalg.norm(v, axis=-1)
            el = elevation(v)
            print(f"  {label:12s} {who}  |v| mean {nrm.mean():.4f} p50 "
                  f"{np.percentile(nrm,50):.4f} p90 {np.percentile(nrm,90):.4f}  "
                  f"dx {v[:,0].mean():+.4f} dy {v[:,1].mean():+.4f} "
                  f"dz {v[:,2].mean():+.4f}  elev {el.mean():+6.1f} deg")
        a = np.concatenate(ang[akey])
        print(f"  {label:12s} angle(ours, Kinect), quiet frames: mean "
              f"{np.nanmean(a):.1f} deg  p50 {np.nanpercentile(a,50):.1f}  "
              f"p90 {np.nanpercentile(a,90):.1f}  (n={a.size})")

    # ---------------- (d) foot pitch ---------------------------------------
    print("\n" + "=" * 78)
    print("(d) FOOT PITCH on stance frames (Kinect ankle < "
          f"{QUIET_SPEED*100:.0f} cm/frame)")
    print("=" * 78)
    print(stats_row("ours toe.y - heel.y (m)", cat(pitch["ours_toe_minus_heel_y"])))
    print(stats_row("ours heel->toe elev (deg)", cat(pitch["ours_heel_toe_elev"])))
    print(stats_row("ours Foot.y - Ankle.y (m)", cat(pitch["ours_foot_minus_ankle_y"])))
    print(stats_row("ours ankle->foot elev(deg)", cat(pitch["ours_foot_elev"])))
    print(stats_row("Kinect Foot.y - Ankle.y (m)", cat(pitch["kin_foot_minus_ankle_y"])))
    print(stats_row("Kinect ankle->foot elev   ", cat(pitch["kin_foot_elev"])))

    # ---------------- placement sweep --------------------------------------
    print("\n" + "=" * 78)
    print("PLACEMENT SWEEP (root-aligned 3D disagreement with Kinect, metres)")
    print("=" * 78)
    print(f"  FOOT_TOE_BLEND (shipping = {FOOT_TOE_BLEND})   "
          f"[limb-rel = vs Kinect's own foot-minus-ankle offset]")
    best_r = min(sweep_foot_rel, key=lambda b: cat(sweep_foot_rel[b]).mean())
    best_b = min(sweep_foot, key=lambda b: cat(sweep_foot[b]).mean())
    print(f"    {'blend':>7s} {'limb-rel mean':>14s} {'p50':>8s}   "
          f"{'root-aln mean':>14s}")
    for b in sorted(sweep_foot):
        er, e = cat(sweep_foot_rel[b]), cat(sweep_foot[b])
        tag = "  <-- shipping" if abs(b - FOOT_TOE_BLEND) < 1e-6 else ""
        tag += "  <-- best(limb-rel)" if b == best_r else ""
        tag += "  <-- best(root-aln)" if b == best_b else ""
        print(f"    {b:+7.2f} {er.mean():14.4f} {np.percentile(er,50):8.4f}   "
              f"{e.mean():14.4f}{tag}")
    print("  HAND placement (shipping = knuckle_mid); limb-rel is QUIET frames only")
    print(f"    {'rule':<14s} {'limb-rel mean':>14s} {'p50':>8s}   "
          f"{'root-aln mean':>14s}")
    for key in sorted(sweep_hand_rel, key=lambda k: cat(sweep_hand_rel[k]).mean()):
        er, e = cat(sweep_hand_rel[key]), cat(sweep_hand[key])
        tag = "  <-- shipping" if key == "knuckle_mid" else ""
        print(f"    {key:<14s} {er.mean():14.4f} {np.percentile(er,50):8.4f}   "
              f"{e.mean():14.4f}{tag}")


if __name__ == "__main__":
    main()
