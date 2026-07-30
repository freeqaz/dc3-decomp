#!/usr/bin/env python3
"""Empirically determine MediaPipe world-landmark axis conventions.

DC3 camera space (ground truth, from the baked Kinect capture in StubCameraInput
and FillDummySkeleton): player-left = -X, player-right = +X, Y up, +Z away from
the camera.

We do NOT trust the docs for handedness. We measure it: on footage of a person
facing the camera, the anatomical relationships are unambiguous, so the sign of
each axis falls out.
"""
import sys
import numpy as np
import cv2
import mediapipe as mp
from mediapipe.tasks import python as mpp
from mediapipe.tasks.python import vision

MODEL = sys.argv[1] if len(sys.argv) > 1 else "native/models/pose_landmarker_full.task"
VIDEO = sys.argv[2] if len(sys.argv) > 2 else "/home/free/tmp/dc3-pose-footage/solo_dancer_sattriya.mp4"
NFRAMES = int(sys.argv[3]) if len(sys.argv) > 3 else 120

# BlazePose 33-landmark indices
NOSE, L_EAR, R_EAR = 0, 7, 8
L_SHOULDER, R_SHOULDER = 11, 12
L_ELBOW, R_ELBOW = 13, 14
L_WRIST, R_WRIST = 15, 16
L_HIP, R_HIP = 23, 24
L_KNEE, R_KNEE = 25, 26
L_ANKLE, R_ANKLE = 27, 28
L_HEEL, R_HEEL = 29, 30
L_FOOT, R_FOOT = 31, 32

opts = vision.PoseLandmarkerOptions(
    base_options=mpp.BaseOptions(model_asset_path=MODEL),
    running_mode=vision.RunningMode.VIDEO,
    num_poses=2,
    output_segmentation_masks=False,
)
landmarker = vision.PoseLandmarker.create_from_options(opts)

cap = cv2.VideoCapture(VIDEO)
fps = cap.get(cv2.CAP_PROP_FPS) or 30.0

acc = {k: [] for k in (
    "shoulder_dx", "hip_dx", "head_dy", "knee_dy", "nose_dz", "wrist_z_range",
    "torso_len", "shoulder_span", "upper_arm", "forearm", "thigh", "shin",
    "hand_bone", "foot_bone",
)}
nframes = 0

while nframes < NFRAMES:
    ok, frame = cap.read()
    if not ok:
        break
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    mp_img = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
    ts = int(nframes * 1000.0 / fps)
    res = landmarker.detect_for_video(mp_img, ts)
    nframes += 1
    if not res.pose_world_landmarks:
        continue
    w = res.pose_world_landmarks[0]
    p = lambda i: np.array([w[i].x, w[i].y, w[i].z])

    hip_c = (p(L_HIP) + p(R_HIP)) / 2
    sho_c = (p(L_SHOULDER) + p(R_SHOULDER)) / 2

    # Sign probes. Person faces the camera, so anatomical relationships are known.
    acc["shoulder_dx"].append(p(L_SHOULDER)[0] - p(R_SHOULDER)[0])
    acc["hip_dx"].append(p(L_HIP)[0] - p(R_HIP)[0])
    acc["head_dy"].append(p(NOSE)[1] - hip_c[1])      # head is ABOVE hips
    acc["knee_dy"].append(p(L_KNEE)[1] - hip_c[1])    # knees are BELOW hips
    acc["nose_dz"].append(p(NOSE)[2] - hip_c[2])      # nose is IN FRONT of hips
    acc["wrist_z_range"].append(p(L_WRIST)[2])

    # Bone lengths, to sanity-check that this really is metric.
    acc["torso_len"].append(np.linalg.norm(sho_c - hip_c))
    acc["shoulder_span"].append(np.linalg.norm(p(L_SHOULDER) - p(R_SHOULDER)))
    acc["upper_arm"].append(np.linalg.norm(p(L_SHOULDER) - p(L_ELBOW)))
    acc["forearm"].append(np.linalg.norm(p(L_ELBOW) - p(L_WRIST)))
    acc["thigh"].append(np.linalg.norm(p(L_HIP) - p(L_KNEE)))
    acc["shin"].append(np.linalg.norm(p(L_KNEE) - p(L_ANKLE)))
    acc["hand_bone"].append(np.linalg.norm(p(L_WRIST) - p(L_FOOT if False else 19)))
    acc["foot_bone"].append(np.linalg.norm(p(L_ANKLE) - p(L_FOOT)))

print(f"frames processed: {nframes}, with pose: {len(acc['shoulder_dx'])}\n")


def report(key, expect, meaning):
    v = np.array(acc[key])
    if len(v) == 0:
        print(f"  {key:16s} NO DATA")
        return None
    med = float(np.median(v))
    frac_pos = float((v > 0).mean())
    verdict = "POSITIVE" if med > 0 else "NEGATIVE"
    agree = "consistent" if max(frac_pos, 1 - frac_pos) > 0.9 else "MIXED/UNSTABLE"
    print(f"  {key:16s} median={med:+.4f}  {verdict:8s}  ({frac_pos*100:5.1f}% pos, {agree})")
    print(f"                   -> {meaning}")
    return med


print("AXIS SIGN PROBES (subject faces camera)")
sdx = report("shoulder_dx", None, "anatomical-left shoulder minus right, along MP x")
report("hip_dx", None, "anatomical-left hip minus right, along MP x")
hdy = report("head_dy", None, "nose minus hip-centre, along MP y (head is physically ABOVE)")
report("knee_dy", None, "knee minus hip-centre, along MP y (knee is physically BELOW)")
ndz = report("nose_dz", None, "nose minus hip-centre, along MP z (nose is physically IN FRONT)")

print("\nMETRIC PLAUSIBILITY (adult ranges in metres)")
for k, lo, hi in (
    ("torso_len", 0.40, 0.65), ("shoulder_span", 0.30, 0.50),
    ("upper_arm", 0.22, 0.36), ("forearm", 0.20, 0.32),
    ("thigh", 0.32, 0.50), ("shin", 0.32, 0.48),
    ("hand_bone", 0.05, 0.15), ("foot_bone", 0.10, 0.25),
):
    v = np.array(acc[k])
    if not len(v):
        continue
    med = float(np.median(v))
    flag = "OK" if lo <= med <= hi else f"OUT OF RANGE (expect {lo}-{hi})"
    print(f"  {k:16s} median={med:.3f} m   {flag}")

print("\n=== DERIVED MAPPING TO DC3 CAMERA SPACE ===")
print("DC3 wants: player-left = -X, player-right = +X, Y up, +Z away from camera.")
if sdx is not None:
    print(f"  X: MP anatomical-left is at {'+x' if sdx > 0 else '-x'};"
          f" DC3 needs it at -X  ->  {'FLIP X' if sdx > 0 else 'keep X'}")
if hdy is not None:
    print(f"  Y: MP head is at {'+y' if hdy > 0 else '-y'} relative to hips;"
          f" DC3 needs up = +Y  ->  {'keep Y' if hdy > 0 else 'FLIP Y'}")
if ndz is not None:
    print(f"  Z: MP nose (toward camera) is at {'+z' if ndz > 0 else '-z'};"
          f" DC3 needs away-from-camera = +Z  ->  {'FLIP Z' if ndz > 0 else 'keep Z'}")
