#!/usr/bin/env python3
"""Measure the PRODUCTION MediaPipe backend's end-to-end 3D error on real video
with metric ground truth (AIST++), and compare it against the constant-z
baseline the YOLO path ships.

WHY: tools/pose_corpus/bench_z.py proved the downstream GEOMETRY (absolute-root
recovery) is exact given perfect world landmarks, and that constant z = 3.0
carries ~0.109 m mean per-joint |z-3| on the game's own choreography corpus.
What it could NOT measure is the MODEL: whether real BlazePose GHUM world
landmarks + our root recovery, run over real video, land closer than constant z.
This script closes that gap.

WHAT IS MEASURED: the exact shipping code. We import MediaPipeBackend from
native/scripts/pose_mediapipe.py, run its own PoseLandmarker (production
constructor options: VIDEO running mode, min_conf 0.5, full model), and call its
_absolute_root + _remap verbatim -- mirroring the exact call sequence in
MediaPipeBackend.process() (pose_mediapipe.py:310-324). Nothing is
reimplemented. The only deviation from the live server: num_poses=1 instead of
2 (AIST++ videos contain exactly one dancer; num_poses caps how many poses are
returned, it does not change per-pose inference), and frames are fed at the
video's native rate with detect_for_video timestamps derived from frame index
exactly like pose_server.py:206 does in --video mode.

GROUND TRUTH: AIST++ (https://google.github.io/aistplusplus_dataset/).
  - keypoints3d_optim: (F, 17, 3) COCO-17 joints, WORLD coordinates,
    CENTIMETRES, 60 fps, frame-aligned to the videos.
  - cameras/settingN.json: per-camera OpenCV rvec/tvec (world->camera),
    intrinsic matrix K (fx = fy, principal point exactly at the image centre),
    and k1 radial distortion.
  License: annotations CC BY 4.0 (Google); videos from the AIST Dance Video DB,
  free for research use, redistribution prohibited -- which is why everything
  lives under ~/tmp/pose_gt/ and NOT in this repo.

AXIS CONVENTIONS -- every flip documented, because a sign error here poisons
the whole comparison (see bench_z.py:to_camera_view for the war story):

  1. AIST++ world -> camera: X_cam = R @ X_world + t (OpenCV Rodrigues
     convention, exactly what cv2.projectPoints uses; validated below by
     reprojecting GT onto the video and matching MediaPipe's own 2D landmarks
     to ~6 px median).
  2. OpenCV camera axes: +X = image right, +Y = image DOWN, +Z = into the
     scene (away from camera).
  3. DC3 camera space (README.md "Space" row; StubCameraInput baked capture):
     +Y = UP, +Z = away from camera, player-left = -X. A player facing the
     camera has their anatomical left on image RIGHT (+X_cv), and DC3 calls
     that -X. So:
         X_dc3 = -X_cv        (image right  -> negative)
         Y_dc3 = -Y_cv        (image down   -> negative, i.e. up positive)
         Z_dc3 = +Z_cv        (depth unchanged)
     Two flips = 180-degree rotation about Z: handedness preserved, exactly
     the same rotation pose_mediapipe.py applies to MediaPipe world landmarks
     (its docstring lines 21-27). GT and prediction therefore land in the SAME
     frame with no mirror ambiguity.
  4. Units: AIST++ centimetres -> metres (/100).

JOINT MATCHING: COCO-17 has no hands, feet, or spine, so we evaluate the
16-joint common subset: 12 direct (shoulders, elbows, wrists, hips, knees,
ankles) + 4 derived THE SAME WAY ON BOTH SIDES. The three centre joints are
built with the backend's own torso fractions (HIP_CENTER_UP / SPINE_UP +
SPINE_BACK / SHOULDER_CENTER_UP, imported from pose_mediapipe so the two sides
can never drift apart), and Head is the ear midpoint, matching the backend's
own choice in _remap. DROPPED: HandLeft/Right (backend: knuckle line),
FootLeft/Right (backend: heel/toe blend) -- no COCO-17 counterpart exists. Head
is reported but flagged: the backend substitutes the nose when ears are
occluded, a definitional mismatch the GT cannot see.

  WHY THE CENTRE JOINTS ARE BUILT FROM FRACTIONS AND NOT MIDPOINTS: they were
  plain midpoints on both sides until UTD-MHAD (bench_utd_mhad.py, 116 k frames
  of real Kinect v1 skeletons) showed Kinect -- the convention DC3's
  choreography was authored in -- does not define them that way, and the
  backend was changed to match. Applying the SAME construction to the GT keeps
  this comparison apples-to-apples: it measures the backend's TRACKING, not the
  centre-joint definition, which UTD-MHAD is the right corpus to judge. AIST++
  cannot arbitrate the definition itself; its COCO-17 GT has no Kinect centre
  joints to compare against. Consequence to expect when re-running across that
  change: the four DERIVED rows move (both sides moved), the twelve DIRECT
  rows' ABSOLUTE numbers are bit-identical, and the direct rows' ROOT-ALIGNED
  numbers move slightly because root alignment re-anchors on HipCenter, which
  is now a different point on both skeletons.

CONDITIONS
  hfov=true      backend given the real camera's horizontal FOV computed from
                 the dataset K: hfov = 2*atan((W/2)/fx). The calibrated case.
  hfov=58.51     the shipping default (DC3's Kinect FOV) -- what an
                 uncalibrated webcam user actually gets.

METRIC FAMILIES: absolute camera-space error AND root-aligned error. The
scorer is translation-invariant -- PositionNode differences two joints of the
SAME skeleton (ErrorNode.cpp:405-412), so a rigid whole-skeleton offset mostly
cancels. Root-aligned (subtract each skeleton's own HipCenter, re-anchor on the
GT root) is therefore the scoring-relevant view; absolute error still matters
for anything using CameraToPlayerXfm / displacement magnitudes, and a root
bias that WOBBLES frame-to-frame injects displacement noise, so the root-z
error is decomposed into per-sequence bias + residual jitter.

BASELINES (computed from the SAME GT frames)
  const z=3.0    GT x/y, z := 3.0 for every joint. The literal YOLO-path
                 substitute (Skeleton_Native mViewDepth). NOTE: flatters the
                 baseline -- its 2D is perfect while MediaPipe's measured 2D
                 error is included in the MediaPipe rows.
  const z=opt    GT x/y, z := per-sequence mean hip depth. The most generous
                 constant-z possible (a user standing exactly where the
                 constant assumes); isolates "no z variation" cost from
                 "wrong absolute distance" cost.
  flat @ root    GT x/y, z := per-FRAME GT hip-centre depth. A perfect
                 root-tracker with a flat skeleton; MediaPipe's relative depth
                 only adds value if it beats this.
  mp xy + GT z   MediaPipe x/y, z := GT z. Isolates how much of MediaPipe's
                 3D error is z vs xy.

SANITY GATES (printed, compare against published numbers before believing
anything): root-relative MPJPE of the raw world landmarks should land in the
35-90 mm band BlazePose GHUM publishes; the GT->2D reprojection vs MediaPipe's
own 2D landmarks should agree to ~a few px.

CACHING: raw landmarker output (world, image, visibility per frame) is cached
per video in <cache>/<seq>.npz -- inference is the slow step (~30 ms/frame
CPU); analysis re-runs in seconds. Cache is hfov-independent (the landmarker
never sees the FOV). Delete the .npz or pass --recompute to re-infer.

Run:
  .venv/bin/python tools/pose_corpus/bench_model_z.py \
      --gt-dir /home/free/tmp/pose_gt [--seqs gBR_sFM_c01_d04_mBR0_ch01 ...] \
      [--quick 300] [--recompute]
"""

import argparse
import json
import os
import pickle
import sys

import cv2
import numpy as np

# Import the PRODUCTION backend -- the entire point is to measure shipping code.
_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO, "native", "scripts"))
from pose_mediapipe import (  # noqa: E402
    MediaPipeBackend, DC3_JOINT_NAMES, NUM_DC3_JOINTS,
    HIP_CENTER_UP, SPINE_UP, SPINE_BACK, SHOULDER_CENTER_UP,
    HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD,
    SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT,
    SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT,
    HIP_LEFT, KNEE_LEFT, ANKLE_LEFT, HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT,
)

MODEL_PATH = os.path.join(_REPO, "native", "models", "pose_landmarker_full.task")

# COCO-17 indices (AIST++ keypoints3d order)
C_NOSE = 0
C_LEAR, C_REAR = 3, 4
C_LSHO, C_RSHO = 5, 6
C_LELB, C_RELB = 7, 8
C_LWRI, C_RWRI = 9, 10
C_LHIP, C_RHIP = 11, 12
C_LKNE, C_RKNE = 13, 14
C_LANK, C_RANK = 15, 16

# The 16-joint common subset: (dc3_index, name).  Order = report order.
DIRECT = [
    (SHOULDER_LEFT, C_LSHO), (SHOULDER_RIGHT, C_RSHO),
    (ELBOW_LEFT, C_LELB), (ELBOW_RIGHT, C_RELB),
    (WRIST_LEFT, C_LWRI), (WRIST_RIGHT, C_RWRI),
    (HIP_LEFT, C_LHIP), (HIP_RIGHT, C_RHIP),
    (KNEE_LEFT, C_LKNE), (KNEE_RIGHT, C_RKNE),
    (ANKLE_LEFT, C_LANK), (ANKLE_RIGHT, C_RANK),
]
SUBSET = [HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD] + [d for d, _ in DIRECT]

# MediaPipe 33-landmark indices needed for the GT-synth geometry-floor probe.
MP_NOSE, MP_LEAR, MP_REAR = 0, 7, 8
MP_LSHO, MP_RSHO, MP_LELB, MP_RELB, MP_LWRI, MP_RWRI = 11, 12, 13, 14, 15, 16
MP_LHIP, MP_RHIP, MP_LKNE, MP_RKNE, MP_LANK, MP_RANK = 23, 24, 25, 26, 27, 28
MP_FROM_COCO = [
    (MP_NOSE, C_NOSE), (MP_LEAR, C_LEAR), (MP_REAR, C_REAR),
    (MP_LSHO, C_LSHO), (MP_RSHO, C_RSHO), (MP_LELB, C_LELB), (MP_RELB, C_RELB),
    (MP_LWRI, C_LWRI), (MP_RWRI, C_RWRI), (MP_LHIP, C_LHIP), (MP_RHIP, C_RHIP),
    (MP_LKNE, C_LKNE), (MP_RKNE, C_RKNE), (MP_LANK, C_LANK), (MP_RANK, C_RANK),
]


def load_camera(gt_dir, setting, cam_name):
    cams = json.load(open(os.path.join(gt_dir, "cameras", f"{setting}.json")))
    c = next(x for x in cams if x["name"] == cam_name)
    R, _ = cv2.Rodrigues(np.asarray(c["rotation"], dtype=float))
    t = np.asarray(c["translation"], dtype=float)
    K = np.asarray(c["matrix"], dtype=float)
    dist = np.asarray(c["distortions"], dtype=float)
    W, H = c["size"]
    # True horizontal FOV from the calibrated fx (principal point is exactly
    # centred in AIST++, so the symmetric formula is exact).
    hfov_deg = float(np.degrees(2.0 * np.arctan((W / 2.0) / K[0, 0])))
    return dict(R=R, t=t, K=K, dist=dist, W=W, H=H, hfov_deg=hfov_deg,
                rvec=np.asarray(c["rotation"], dtype=float))


def load_mapping(gt_dir):
    out = {}
    with open(os.path.join(gt_dir, "cameras", "mapping.txt")) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                out[parts[0]] = parts[1]
    return out


def gt_to_dc3(kp_world_cm, cam):
    """AIST++ world (cm) -> DC3 camera space (m). See module docstring step 1-4.

    Returns (F, 17, 3) in DC3 axes: X = -X_cv, Y = -Y_cv, Z = +Z_cv, metres.
    """
    Xc = np.einsum("ij,fkj->fki", cam["R"], kp_world_cm) + cam["t"]  # cm, CV axes
    out = np.empty_like(Xc)
    out[..., 0] = -Xc[..., 0]   # image-right (+X_cv) -> player-left is -X_dc3
    out[..., 1] = -Xc[..., 1]   # image-down (+Y_cv)  -> up is +Y_dc3
    out[..., 2] = Xc[..., 2]    # depth unchanged
    return out / 100.0


def gt_subset(gt_dc3):
    """(F,17,3) GT -> (F,16,3) in SUBSET order, derived joints built the same
    way the backend builds them (_remap).

    The three centre joints use the backend's torso fractions verbatim -- same
    body frame (hip midpoint, torso vector, posterior direction from
    cross(torso, hip axis)), same constants, so nothing here can drift from
    production. Head is the ear midpoint, as in _remap.
    """
    F = gt_dc3.shape[0]
    out = np.empty((F, len(SUBSET), 3))
    hip_mid = (gt_dc3[:, C_LHIP] + gt_dc3[:, C_RHIP]) * 0.5
    sho_mid = (gt_dc3[:, C_LSHO] + gt_dc3[:, C_RSHO]) * 0.5
    torso = sho_mid - hip_mid
    torso_len = np.linalg.norm(torso, axis=-1, keepdims=True)
    back = np.cross(torso, gt_dc3[:, C_LHIP] - gt_dc3[:, C_RHIP])
    back /= np.maximum(np.linalg.norm(back, axis=-1, keepdims=True), 1e-9)
    out[:, 0] = hip_mid + HIP_CENTER_UP * torso                     # HipCenter
    out[:, 1] = hip_mid + SPINE_UP * torso + (SPINE_BACK * torso_len) * back
    out[:, 2] = hip_mid + SHOULDER_CENTER_UP * torso            # ShoulderCenter
    out[:, 3] = (gt_dc3[:, C_LEAR] + gt_dc3[:, C_REAR]) * 0.5   # Head (ear mid)
    for i, (_, coco) in enumerate(DIRECT):
        out[:, 4 + i] = gt_dc3[:, coco]
    return out


def run_landmarker(video_path, cache_path, quick=0):
    """Run the production backend's own PoseLandmarker over the video, caching
    (world, image_xy, visibility, detected) per frame. Mirrors the feeding in
    pose_server.py --video mode: sequential frames, BGR->RGB inside the
    backend's process() path, detect_for_video timestamps = frame_idx * period.
    """
    if os.path.exists(cache_path):
        d = np.load(cache_path)
        return {k: d[k] for k in d.files}

    be = MediaPipeBackend(MODEL_PATH, num_poses=1)  # single dancer; see docstring
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        sys.exit(f"cannot open {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 60.0
    period_ms = 1000.0 / fps

    world, img, vis, det = [], [], [], []
    i = 0
    while True:
        ok, frame = cap.read()
        if not ok or (quick and i >= quick):
            break
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)   # process() line 301
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
               fps=np.float64(fps),
               frame_wh=np.array([1920, 1080]))
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    np.savez_compressed(cache_path, **out)
    return out


def backend_joints(geom_be, cache, W, H):
    """Replay the exact production per-frame pipeline on cached landmarks.

    Mirrors MediaPipeBackend.process() lines 310-324 verbatim: build world /
    image_xy / vis arrays, _absolute_root, then _remap. geom_be only supplies
    _hfov for _focal_px -- the landmarks are the cached production outputs.
    Returns (F, 20, 3) joints and (F,) valid mask.
    """
    F = len(cache["det"])
    joints = np.full((F, NUM_DC3_JOINTS, 3), np.nan)
    valid = np.zeros(F, dtype=bool)
    for i in range(F):
        if not cache["det"][i]:
            continue
        world, image_xy, vis = cache["world"][i], cache["img"][i], cache["vis"][i]
        root = geom_be._absolute_root(world, image_xy, vis, W, H)
        if root is None:
            continue
        j, _c = geom_be._remap(world, vis, root)
        joints[i] = j
        valid[i] = True
    return joints, valid


def geometry_floor_root(geom_be, gt_dc3, cam):
    """Perfect-landmark lower bound for the ROOT: feed _absolute_root synthetic
    landmarks built from GT (exact hip-relative offsets in MediaPipe's axis
    convention + exact pinhole projections, no distortion), so the only error
    left is geometric. bench_z.py showed this is ~0 for the LLS solver; here it
    confirms the same on AIST++ posture statistics.

    Inverse of the backend's flip (pose_mediapipe.py:217): MediaPipe world =
    (-X_dc3, -Y_dc3, +Z_dc3) relative to the hip midpoint.
    """
    F = gt_dc3.shape[0]
    hipc = (gt_dc3[:, C_LHIP] + gt_dc3[:, C_RHIP]) * 0.5
    W, H = cam["W"], cam["H"]
    f = geom_be._focal_px(W)
    roots = np.full((F, 3), np.nan)
    for i in range(F):
        world = np.zeros((33, 3)); vis = np.zeros(33); img = np.zeros((33, 2))
        for mp_i, coco_i in MP_FROM_COCO:
            rel = gt_dc3[i, coco_i] - hipc[i]
            world[mp_i] = [-rel[0], -rel[1], rel[2]]
            # Exact mirrored pinhole (the convention _absolute_root assumes,
            # pose_mediapipe.py:195): u = W/2 - f*X/Z, v = H/2 - f*Y/Z, X in
            # DC3 axes. Normalised to [0,1] like real image landmarks.
            X = gt_dc3[i, coco_i]
            u = W * 0.5 - f * X[0] / X[2]
            v = H * 0.5 - f * X[1] / X[2]
            img[mp_i] = [u / W, v / H]
            vis[mp_i] = 1.0
        r = geom_be._absolute_root(world, img, vis, W, H)
        if r is not None:
            roots[i] = r
    return roots, hipc


def stats(err, name, f=sys.stdout):
    e = err[np.isfinite(err)]
    if len(e) == 0:
        print(f"  {name:24s} (no data)", file=f); return
    print(f"  {name:24s} mean {e.mean():.4f}  p50 {np.percentile(e,50):.4f}  "
          f"p90 {np.percentile(e,90):.4f}  max {e.max():.4f}", file=f)


def eval_sequence(seq_c01, gt_dir, cache_dir, quick, conditions):
    """Returns dict: condition -> dict of error arrays (frames x 16 joints)."""
    seq_all = seq_c01.replace("_c01_", "_cAll_")
    cam_name = seq_c01.split("_")[2]
    mapping = load_mapping(gt_dir)
    cam = load_camera(gt_dir, mapping[seq_all], cam_name)

    kp_cm = pickle.load(open(os.path.join(gt_dir, "keypoints3d", f"{seq_all}.pkl"),
                             "rb"))["keypoints3d_optim"].astype(np.float64)
    print(f"  camera {cam_name} ({mapping[seq_all]}): fx {cam['K'][0,0]:.0f} px, "
          f"true hfov {cam['hfov_deg']:.2f} deg (default assumption 58.51)")
    video = os.path.join(gt_dir, "videos", f"{seq_c01}.mp4")
    print(f"  landmarking (cached after first run)...", flush=True)
    cache = run_landmarker(video, os.path.join(cache_dir, f"{seq_c01}.npz"), quick)

    F = min(len(cache["det"]), len(kp_cm))
    kp_cm = kp_cm[:F]
    gt_dc3 = gt_to_dc3(kp_cm, cam)              # (F,17,3) metres, DC3 axes
    gt16 = gt_subset(gt_dc3)                    # (F,16,3)
    gt_ok = np.isfinite(gt16).all(axis=(1, 2))  # AIST++ has occasional NaN frames

    # ---- sanity gate 1: GT reprojection vs MediaPipe 2D (px) --------------
    det = cache["det"][:F] & gt_ok
    uv_gt = []
    for i in np.flatnonzero(det)[:400]:
        uv, _ = cv2.projectPoints(kp_cm[i].reshape(-1, 1, 3), cam["rvec"],
                                  cam["t"], cam["K"], cam["dist"])
        uv_gt.append(uv.reshape(-1, 2))
    if uv_gt:
        idx = np.flatnonzero(det)[:400]
        mp_px = cache["img"][:F][idx] * np.array([cam["W"], cam["H"]])
        pairs = [(MP_LSHO, C_LSHO), (MP_RSHO, C_RSHO), (MP_LHIP, C_LHIP),
                 (MP_RHIP, C_RHIP), (MP_LKNE, C_LKNE), (MP_RKNE, C_RKNE)]
        d = [np.linalg.norm(mp_px[k][a] - uv_gt[k][b])
             for k in range(len(idx)) for a, b in pairs]
        print(f"  sanity: GT-reprojection vs MediaPipe 2D, median {np.median(d):.1f} px "
              f"(should be ~a few px; large => misalignment)")

    # ---- sanity gate 2: root-relative world-landmark MPJPE ----------------
    hipc_gt = (gt_dc3[:, C_LHIP] + gt_dc3[:, C_RHIP]) * 0.5
    mp_rel_err = []
    for mp_i, coco_i in MP_FROM_COCO:
        if coco_i in (C_NOSE, C_LEAR, C_REAR):
            continue
        # world -> DC3 axes (flip x, flip y) then compare hip-relative offsets
        w = cache["world"][:F][:, mp_i, :]
        pred_rel = np.stack([-w[:, 0], -w[:, 1], w[:, 2]], axis=1)
        gt_rel = gt_dc3[:, coco_i] - hipc_gt
        mp_rel_err.append(np.linalg.norm(pred_rel - gt_rel, axis=1))
    rel_mpjpe = np.nanmean(np.where(det, np.stack(mp_rel_err, 1).mean(1), np.nan))
    print(f"  sanity: root-relative world-landmark MPJPE {rel_mpjpe*1000:.0f} mm "
          f"(published BlazePose GHUM full: ~35-90 mm)")

    out = {"gt16": gt16, "det": det, "cam": cam, "hipc_gt": hipc_gt}
    for cond_name, hfov in conditions.items():
        hfov_deg = cam["hfov_deg"] if hfov == "true" else hfov
        # Geometry-only backend instance: same class, same methods; the model
        # load is unavoidable ctor cost but detect is never called on it.
        geom_be = MediaPipeBackend(MODEL_PATH, num_poses=1, hfov_deg=hfov_deg)
        joints, valid = backend_joints(geom_be, cache, cam["W"], cam["H"])
        joints = joints[:F]; valid = valid[:F] & gt_ok
        pred16 = joints[:, SUBSET, :]
        out[cond_name] = {"pred16": pred16, "valid": valid, "hfov_deg": hfov_deg}
        if hfov == "true":
            roots_floor, _ = geometry_floor_root(geom_be, gt_dc3, cam)
            out["floor_root"] = roots_floor
        geom_be.close()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gt-dir", default="/home/free/tmp/pose_gt")
    ap.add_argument("--cache-dir", default=None,
                    help="landmark cache (default <gt-dir>/cache)")
    ap.add_argument("--seqs", nargs="*", default=None,
                    help="c01 video basenames; default = all in <gt-dir>/videos")
    ap.add_argument("--quick", type=int, default=0,
                    help="limit frames per video (testing)")
    ap.add_argument("--recompute", action="store_true")
    args = ap.parse_args()

    cache_dir = args.cache_dir or os.path.join(args.gt_dir, "cache")
    if args.seqs is None:
        args.seqs = sorted(os.path.splitext(f)[0]
                           for f in os.listdir(os.path.join(args.gt_dir, "videos"))
                           if f.endswith(".mp4"))
    if args.recompute:
        for s in args.seqs:
            p = os.path.join(cache_dir, f"{s}.npz")
            if os.path.exists(p):
                os.remove(p)

    conditions = {"hfov=true": "true", "hfov=58.51 (default)": 58.51}
    subset_names = ["HipCenter", "Spine", "ShoulderCenter", "Head*"] + \
                   [DC3_JOINT_NAMES[d] for d, _ in DIRECT]

    per_seq = {}
    for s in args.seqs:
        print(f"\n=== {s} ===")
        per_seq[s] = eval_sequence(s, args.gt_dir, cache_dir, args.quick, conditions)

    # ---- aggregate ------------------------------------------------------
    print("\n" + "=" * 74)
    print("AGGREGATE over all sequences (16-joint common subset, metres)")
    print("  Head* = definitional mismatch possible (backend may use nose)")
    print("=" * 74)

    agg = {}

    def add(cond, dz, dxy, d3):
        a = agg.setdefault(cond, {"dz": [], "dxy": [], "d3": []})
        a["dz"].append(dz); a["dxy"].append(dxy); a["d3"].append(d3)

    for s, r in per_seq.items():
        gt16, det = r["gt16"], r["det"]
        z_opt = np.nanmean(r["hipc_gt"][det][:, 2]) if det.any() else 3.0
        for cond in conditions:
            pred, valid = r[cond]["pred16"], r[cond]["valid"]
            m = valid
            dz = np.abs(pred[m][..., 2] - gt16[m][..., 2])
            dxy = np.linalg.norm(pred[m][..., :2] - gt16[m][..., :2], axis=-1)
            d3 = np.linalg.norm(pred[m] - gt16[m], axis=-1)
            add(cond, dz, dxy, d3)
            # mp xy + GT z (isolates how much of the 3D error is z vs xy)
            add(cond + " [mp xy + GT z]", np.zeros_like(dz), dxy, dxy.copy())
            # ROOT-ALIGNED: subtract each skeleton's own HipCenter, re-anchor
            # on the GT root. This is the scoring-relevant view (the scorer
            # differences joints of the same skeleton).
            pal = pred[m] - pred[m][:, 0:1, :] + gt16[m][:, 0:1, :]
            dza = np.abs(pal[..., 2] - gt16[m][..., 2])
            dxya = np.linalg.norm(pal[..., :2] - gt16[m][..., :2], axis=-1)
            d3a = np.linalg.norm(pal - gt16[m], axis=-1)
            add(cond + " [root-aligned]", dza, dxya, d3a)
        m = det
        gtm = gt16[m]
        for name, zsub in (("const z=3.0 (YOLO path)", 3.0),
                           ("const z=opt (per-seq)", z_opt)):
            dz = np.abs(gtm[..., 2] - zsub)
            add(name, dz, np.zeros_like(dz), dz)
        rootz = r["hipc_gt"][m][:, 2]
        dz = np.abs(gtm[..., 2] - rootz[:, None])
        # Note: root-aligned, every constant-z baseline IS this flat skeleton,
        # so this row is the constant-z competitor in the root-aligned family.
        add("flat @ GT root depth", dz, np.zeros_like(dz), dz)

    order = (list(conditions)
             + [c + " [root-aligned]" for c in conditions]
             + [c + " [mp xy + GT z]" for c in conditions]
             + ["const z=3.0 (YOLO path)", "const z=opt (per-seq)",
                "flat @ GT root depth"])
    for metric in ("dz", "dxy", "d3"):
        label = {"dz": "|dz|  depth error", "dxy": "|dxy| image-plane error",
                 "d3": "full 3D error"}[metric]
        print(f"\n-- {label} (m), all joints x frames --")
        for cond in order:
            if cond in agg:
                stats(np.concatenate([x.ravel() for x in agg[cond][metric]]), cond)

    # ---- does MediaPipe's RELATIVE depth carry signal? -------------------
    # Correlation between predicted and GT hip-relative z, plus root-aligned
    # |dz| head-to-head vs the flat skeleton. If corr <= 0 the z channel is
    # noise (or a sign error); if root-aligned |dz| < flat |dz| the relative
    # depth genuinely helps scoring.
    print("\n-- relative-depth signal (hfov=true) --")
    corr_all, rel_pred_all, rel_gt_all = [], [], []
    for s, r in per_seq.items():
        pred, valid, gt16 = r["hfov=true"]["pred16"], r["hfov=true"]["valid"], r["gt16"]
        rp = (pred[valid] - pred[valid][:, 0:1, :])[..., 2].ravel()
        rg = (gt16[valid] - gt16[valid][:, 0:1, :])[..., 2].ravel()
        ok = np.isfinite(rp) & np.isfinite(rg)
        c = np.corrcoef(rp[ok], rg[ok])[0, 1]
        corr_all.append(c)
        rel_pred_all.append(rp[ok]); rel_gt_all.append(rg[ok])
        print(f"  {s[:40]:40s} corr(rel z) {c:+.3f}")
    rp = np.concatenate(rel_pred_all); rg = np.concatenate(rel_gt_all)
    print(f"  {'ALL':40s} corr(rel z) {np.corrcoef(rp, rg)[0,1]:+.3f}   "
          f"gt rel-z std {rg.std():.3f} m, pred rel-z std {rp.std():.3f} m")

    # ---- per-joint breakdown (true-FOV condition) ------------------------
    print("\n-- per-joint |dz| / 3D (hfov=true) vs const z=3.0 --")
    print(f"  {'joint':16s} {'mp |dz|':>9s} {'mp 3D':>9s} {'z3 |dz|':>9s}")
    dz_j = {j: [] for j in range(len(SUBSET))}
    d3_j = {j: [] for j in range(len(SUBSET))}
    z3_j = {j: [] for j in range(len(SUBSET))}
    for s, r in per_seq.items():
        pred, valid = r["hfov=true"]["pred16"], r["hfov=true"]["valid"]
        gt16 = r["gt16"]
        for j in range(len(SUBSET)):
            dz_j[j].append(np.abs(pred[valid][:, j, 2] - gt16[valid][:, j, 2]))
            d3_j[j].append(np.linalg.norm(pred[valid][:, j] - gt16[valid][:, j], axis=-1))
            z3_j[j].append(np.abs(gt16[r["det"]][:, j, 2] - 3.0))
    for j, nm in enumerate(subset_names):
        a = np.concatenate(dz_j[j]); b = np.concatenate(d3_j[j]); c = np.concatenate(z3_j[j])
        print(f"  {nm:16s} {np.nanmean(a):9.4f} {np.nanmean(b):9.4f} {np.nanmean(c):9.4f}")

    # ---- root-depth error: bias vs jitter --------------------------------
    # Decompose the root-z error into a per-sequence constant bias (mostly
    # cancelled by the scorer's translation invariance) and residual jitter
    # (which does NOT cancel: it wobbles displacement). Also frame-to-frame
    # diff std as a direct displacement-noise proxy, with the GT's own
    # frame-to-frame root motion for scale.
    print("\n-- root (HipCenter) z error: model-driven vs geometry floor --")
    print(f"  {'sequence':30s} {'condition':22s} {'bias':>7s} {'jitter':>7s} "
          f"{'|z|':>7s} {'d(z)/frame':>10s}")
    for s, r in per_seq.items():
        det = r["det"]
        gt_root = r["hipc_gt"]
        gt_step = np.diff(gt_root[det][:, 2])
        for cond in conditions:
            pred, valid = r[cond]["pred16"], r[cond]["valid"]
            dz = pred[valid][:, 0, 2] - gt_root[valid][:, 2]
            step = np.diff(pred[valid][:, 0, 2] - gt_root[valid][:, 2])
            print(f"  {s[:30]:30s} {cond:22s} {np.nanmean(dz):+7.3f} "
                  f"{np.nanstd(dz):7.3f} {np.nanmean(np.abs(dz)):7.3f} "
                  f"{np.nanstd(step):10.3f}")
        fr = r.get("floor_root")
        if fr is not None:
            dz = np.abs(fr[det][:, 2] - gt_root[det][:, 2])
            print(f"  {s[:30]:30s} {'geometry floor (GT lm)':22s}         "
                  f"        {np.nanmean(dz):7.4f}  (gt d(z)/frame std "
                  f"{np.nanstd(gt_step):.3f})")

    # ---- per-sequence decision metric ------------------------------------
    # ra|dz| = root-aligned depth error (the scoring-relevant number) vs
    # flat|dz| = flat-skeleton-at-GT-root (what ANY constant-z reduces to under
    # root alignment). MediaPipe's relative depth only earns its keep on a
    # sequence if ra < flat there.
    print("\n-- per-sequence: detection, root-aligned |dz| vs flat, 3D (hfov=true) --")
    for s, r in per_seq.items():
        det, valid = r["det"], r["hfov=true"]["valid"]
        pred, gt16 = r["hfov=true"]["pred16"], r["gt16"]
        d3 = np.linalg.norm(pred[valid] - gt16[valid], axis=-1)
        pal = pred[valid] - pred[valid][:, 0:1, :] + gt16[valid][:, 0:1, :]
        ra = np.abs(pal[..., 2] - gt16[valid][..., 2])
        flat = np.abs(gt16[det][..., 2] - r["hipc_gt"][det][:, 2][:, None])
        depth = r["hipc_gt"][det][:, 2]
        print(f"  {s[:36]:36s} n={len(det):5d} det={det.mean()*100:5.1f}% "
              f"ra|dz| {np.nanmean(ra):.3f} vs flat {np.nanmean(flat):.3f}  "
              f"3D {np.nanmean(d3):.3f}  depth {depth.min():.1f}-{depth.max():.1f} m")


if __name__ == "__main__":
    main()
