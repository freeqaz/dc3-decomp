#!/usr/bin/env python3
"""Measure depth recovery against the game's own 3D reference choreography.

Two questions this answers, both with numbers rather than vibes:

  1. HOW WRONG IS CONSTANT z? The native port fed the scorer z = 3.0 m for every
     joint until the MediaPipe backend landed. This quantifies the error that
     approximation carries, which is the headroom any estimator has to beat.

  2. IS OUR ABSOLUTE-ROOT RECOVERY CORRECT? MediaPipe emits hip-RELATIVE metres,
     so pose_mediapipe.py recovers the absolute root by similar triangles on the
     torso. That is our code and a genuinely risky heuristic -- the torso
     foreshortens when a dancer leans toward or away from the camera, and the
     estimator has no way to know. Feeding it PERFECT world landmarks isolates
     the geometry from the model: whatever error shows up here is a floor that
     no amount of model quality can remove.

Ground truth: tools/pose_corpus (see README). Projection uses the exact Kinect
intrinsics recovered from the target disassembly:

    u = 160 + 285.63 * x/z      v = 120 - 285.63 * y/z      (320x240)

Run:  python3 tools/pose_corpus/bench_z.py [--npz build/pose_corpus/poses.npz]
"""

import argparse
import sys
import numpy as np

FOCAL = 285.63
CX, CY = 160.0, 120.0
DEPTH_W, DEPTH_H = 320, 240
TARGET_HIP_Z = 3.0

# DC3 joint indices (src/system/gesture/BaseSkeleton.h:29-49)
HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD = 0, 1, 2, 3
SHOULDER_LEFT, SHOULDER_RIGHT = 4, 8
JOINT_NAMES = [
    "HipCenter", "Spine", "ShoulderCenter", "Head",
    "ShoulderLeft", "ElbowLeft", "WristLeft", "HandLeft",
    "ShoulderRight", "ElbowRight", "WristRight", "HandRight",
    "HipLeft", "KneeLeft", "AnkleLeft",
    "HipRight", "KneeRight", "AnkleRight",
    "FootLeft", "FootRight",
]


def normalise_framing(pos):
    """Recentre each pose so the hip sits on the optical axis at TARGET_HIP_Z.

    Corpus poses are authored body-centred with lateral travel to +/-1.2 m, so
    projecting them raw puts almost nothing in frame. This is legitimate rather
    than a fudge: the scorer is translation-invariant (PositionNode differences
    two joints of the SAME skeleton, ErrorNode.cpp:405-412), so a rigid shift
    changes no score.
    """
    hip = pos[:, HIP_CENTER, :].copy()
    out = pos.copy()
    out[:, :, 0] -= hip[:, None, 0]
    out[:, :, 1] -= hip[:, None, 1]
    out[:, :, 2] += (TARGET_HIP_Z - hip[:, None, 2])
    # Deterministic lateral/vertical offsets. Without these the hip sits exactly
    # on the optical axis, so u == CX, and the X/Y recovery below would be
    # trivially exact -- a vacuous test that looks like a perfect result.
    n = len(out)
    ox = np.linspace(-0.45, 0.45, n)
    oy = np.linspace(-0.25, 0.25, n)[::-1].copy()
    out[:, :, 0] += ox[:, None]
    out[:, :, 1] += oy[:, None]
    return out


def project(pos):
    """3D camera-space metres -> (u, v) pixels in the 320x240 depth image."""
    z = np.maximum(pos[..., 2], 1e-6)
    u = CX + FOCAL * pos[..., 0] / z
    v = CY - FOCAL * pos[..., 1] / z
    return np.stack([u, v], axis=-1)


def to_camera_view(uv):
    """Mirror horizontally to simulate what a real webcam sees.

    DC3's depth image is a MIRROR view: its own projection (u = 160 + 285.63x/z)
    puts the player's anatomical left -- which sits at -X, per the baked capture
    in StubCameraInput.cpp:47,51 -- at SMALLER u, i.e. image-left. A normal
    camera photographing someone facing it puts their left at LARGER u. That is
    why pose_mediapipe.py negates x when converting.

    So to exercise the real pipeline's X path we must feed it a camera-view
    image, not DC3's own mirrored one. Without this the benchmark would report a
    ~0.42 m X error that is purely an artifact of testing the flip against an
    already-flipped projection.
    """
    out = uv.copy()
    out[..., 0] = 2.0 * CX - out[..., 0]
    return out


def in_frame(uv):
    return ((uv[..., 0] >= 0) & (uv[..., 0] < DEPTH_W)
            & (uv[..., 1] >= 0) & (uv[..., 1] < DEPTH_H)).all(axis=-1)


def recover_root_pnp(rel, uv, weights=None):
    """Exact linear least-squares root recovery (DLT with known structure).

    The torso similar-triangles estimate -- even with the in-plane fix -- is an
    approximation: it assumes the whole segment sits at one depth, but a tilted
    torso spans a depth range and perspective makes the projected length depend
    on WHERE in that range each endpoint sits. That is the second-order residual
    (-0.24 m at sin(tilt) > 0.5) the tilt table shows.

    But the problem is exactly solvable. We know each landmark's full 3D offset
    from the root, rel_i (world landmarks are hip-relative 3D), and its pixel
    position. With the mirrored camera-view projection u = CX - f*X/Z,
    v = CY - f*Y/Z and X_i = rel_i + R:

        (u_i - CX)(Rz + rel_z_i) = -f(Rx + rel_x_i)
        (v_i - CY)(Rz + rel_z_i) = -f(Ry + rel_y_i)

    which is LINEAR in R = (Rx, Ry, Rz):

        f*Rx + a_i*Rz = -f*rel_x_i - a_i*rel_z_i      a_i = u_i - CX
        f*Ry + b_i*Rz = -f*rel_y_i - b_i*rel_z_i      b_i = v_i - CY

    Two equations per landmark, three unknowns total: massively overdetermined,
    solved by (optionally weighted) normal equations, batched across poses.
    On perfect landmarks the residual is zero -- no tilt bias, no perspective
    approximation. On noisy landmarks it averages over every joint instead of
    leaning on three.
    """
    a = uv[:, :, 0] - CX
    b = uv[:, :, 1] - CY
    ru = -FOCAL * rel[:, :, 0] - a * rel[:, :, 2]
    rv = -FOCAL * rel[:, :, 1] - b * rel[:, :, 2]

    w = np.ones_like(a) if weights is None else weights
    f2 = FOCAL * FOCAL

    ata = np.zeros((len(rel), 3, 3))
    aty = np.zeros((len(rel), 3))
    ata[:, 0, 0] = (w * f2).sum(axis=1)
    ata[:, 1, 1] = (w * f2).sum(axis=1)
    ata[:, 0, 2] = ata[:, 2, 0] = (w * FOCAL * a).sum(axis=1)
    ata[:, 1, 2] = ata[:, 2, 1] = (w * FOCAL * b).sum(axis=1)
    ata[:, 2, 2] = (w * (a * a + b * b)).sum(axis=1)
    aty[:, 0] = (w * FOCAL * ru).sum(axis=1)
    aty[:, 1] = (w * FOCAL * rv).sum(axis=1)
    aty[:, 2] = (w * (a * ru + b * rv)).sum(axis=1)

    root = np.linalg.solve(ata, aty[:, :, None])[:, :, 0]
    return root[:, 0], root[:, 1], np.clip(root[:, 2], 0.8, 8.0)


def recover_root(pos, uv, in_plane=True):
    """Port of MediaPipe backend's _absolute_root, fed PERFECT world landmarks.

    Mirrors pose_mediapipe.py: torso length in metres over its projected pixel
    length gives depth by similar triangles, then the hip's image offset gives
    x and y. Isolates the geometry from any model error.
    """
    sho_m = (pos[:, SHOULDER_LEFT, :] + pos[:, SHOULDER_RIGHT, :]) * 0.5
    hip_m = pos[:, HIP_CENTER, :]
    torso_vec = sho_m - hip_m
    if in_plane:
        # Correct form: a segment tilted out of the image plane projects to
        # L*cos(tilt), so similar triangles needs the IN-PLANE extent.
        torso_m = np.hypot(torso_vec[:, 0], torso_vec[:, 1])
    else:
        torso_m = np.linalg.norm(torso_vec, axis=-1)

    sho_px = (uv[:, SHOULDER_LEFT, :] + uv[:, SHOULDER_RIGHT, :]) * 0.5
    hip_px = uv[:, HIP_CENTER, :]
    torso_px = np.linalg.norm(sho_px - hip_px, axis=-1)

    ok = (torso_m > 1e-3) & (torso_px > 1.0)
    z_hip = np.full(len(pos), np.nan)
    z_hip[ok] = FOCAL * torso_m[ok] / torso_px[ok]
    z_hip = np.clip(z_hip, 0.8, 8.0)

    x_hip = -(hip_px[:, 0] - CX) / FOCAL * z_hip
    y_hip = -(hip_px[:, 1] - CY) / FOCAL * z_hip
    return x_hip, y_hip, z_hip, ok


def pct(a, p):
    return float(np.nanpercentile(a, p))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--npz", default="build/pose_corpus/poses.npz")
    args = ap.parse_args()

    try:
        d = np.load(args.npz)
    except FileNotFoundError:
        sys.exit(f"{args.npz} not found — regenerate with "
                 "tools/pose_corpus/extract_seqs.py (see README)")
    pos = d["pos"].astype(np.float64)
    print(f"corpus: {pos.shape[0]} poses x {pos.shape[1]} joints\n")

    norm = normalise_framing(pos)
    uv = project(norm)
    vis = in_frame(uv)
    print(f"framing: {vis.mean()*100:.1f}% of poses project fully inside "
          f"{DEPTH_W}x{DEPTH_H} after hip recentring")
    norm, uv = norm[vis], uv[vis]
    uv_cam = to_camera_view(uv)
    z = norm[:, :, 2]

    # -- Q1: what does constant z cost? ----------------------------------
    print("\n" + "=" * 68)
    print("Q1  ERROR CARRIED BY CONSTANT z = 3.0 m")
    print("=" * 68)
    err = np.abs(z - TARGET_HIP_Z)
    print(f"  per-joint |z - 3.0|   mean {err.mean():.4f} m   "
          f"p50 {pct(err,50):.4f}   p90 {pct(err,90):.4f}   max {err.max():.4f}")
    spread = z.max(axis=1) - z.min(axis=1)
    print(f"  within-pose z spread  mean {spread.mean():.4f} m   "
          f"p50 {pct(spread,50):.4f}   p90 {pct(spread,90):.4f}   max {spread.max():.4f}")
    print("\n  worst joints by mean |z - 3.0| (constant z is most wrong here):")
    order = np.argsort(-err.mean(axis=0))
    for j in order[:6]:
        print(f"    {JOINT_NAMES[j]:16s} mean {err[:, j].mean():.4f} m   "
              f"p90 {pct(err[:, j], 90):.4f}   max {err[:, j].max():.4f}")
    # Scale reference: how big is that error next to the limb it perturbs?
    forearm = np.linalg.norm(norm[:, 6, :] - norm[:, 5, :], axis=-1).mean()
    print(f"\n  for scale, mean forearm length = {forearm:.4f} m, so the mean "
          f"per-joint depth error is {err.mean()/forearm:.2f}x a forearm")

    # -- Q2: is the absolute-root recovery sound? -------------------------
    print("\n" + "=" * 68)
    print("Q2  ABSOLUTE-ROOT RECOVERY (fed perfect world landmarks)")
    print("=" * 68)
    true_hip = norm[:, HIP_CENTER, :]
    rel = norm - true_hip[:, None, :]
    for label, ip in (("full 3D length (buggy)", False), ("in-plane extent (fixed)", True)):
        _x, _y, _z, _ok = recover_root(norm, uv_cam, in_plane=ip)
        e = np.abs(_z - true_hip[:, 2])
        print(f"  [{label:24s}] hip Z abs-mean {np.nanmean(e):.4f} m  "
              f"p90 {pct(e,90):.4f}  max {np.nanmax(e):.4f}")
    px, py, pz = recover_root_pnp(rel, uv_cam)
    e = np.abs(pz - true_hip[:, 2])
    print(f"  [{'PnP least-squares':24s}] hip Z abs-mean {np.nanmean(e):.4f} m  "
          f"p90 {pct(e,90):.4f}  max {np.nanmax(e):.4f}")
    print()
    x_hip, y_hip, z_hip, ok = recover_root(norm, uv_cam, in_plane=True)
    dz = z_hip - true_hip[:, 2]
    dx = x_hip - true_hip[:, 0]
    dy = y_hip - true_hip[:, 1]
    print(f"  usable on {ok.mean()*100:.1f}% of poses")
    print(f"  hip Z error   mean {np.nanmean(dz):+.4f} m   "
          f"abs-mean {np.nanmean(np.abs(dz)):.4f}   p90 {pct(np.abs(dz),90):.4f}   "
          f"max {np.nanmax(np.abs(dz)):.4f}")
    print(f"  hip X error   abs-mean {np.nanmean(np.abs(dx)):.4f} m   "
          f"p90 {pct(np.abs(dx),90):.4f}")
    print(f"  hip Y error   abs-mean {np.nanmean(np.abs(dy)):.4f} m   "
          f"p90 {pct(np.abs(dy),90):.4f}")

    # The predicted failure mode: torso foreshortening. If the torso tilts out of
    # the image plane its projection shortens, similar triangles reads that as
    # "further away", and depth is over-estimated. Test it directly.
    torso_vec = ((norm[:, SHOULDER_LEFT, :] + norm[:, SHOULDER_RIGHT, :]) * 0.5
                 - norm[:, HIP_CENTER, :])
    torso_len = np.linalg.norm(torso_vec, axis=-1)
    # |z-component| / length = sin(tilt out of the image plane); 0 = fronto-parallel
    tilt = np.abs(torso_vec[:, 2]) / np.maximum(torso_len, 1e-6)
    dz_pnp = pz - true_hip[:, 2]
    print("\n  hip Z error vs torso tilt out of the image plane"
          " (the predicted failure mode):")
    print(f"    {'':22s}{'in-plane torso':>26s}{'PnP least-squares':>26s}")
    edges = [0.0, 0.1, 0.2, 0.3, 0.5, 1.01]
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (tilt >= lo) & (tilt < hi)
        if m.sum() < 20:
            continue
        print(f"    sin(tilt) {lo:.1f}-{hi:.1f}  n={m.sum():6d}  "
              f"mean {np.nanmean(dz[m]):+.4f} abs {np.nanmean(np.abs(dz[m])):.4f}   "
              f"mean {np.nanmean(dz_pnp[m]):+.4f} abs {np.nanmean(np.abs(dz_pnp[m])):.4f}")

    # -- Q3: which estimator degrades better under NOISE? ------------------
    # Q2's perfect-landmark result is guaranteed by construction for PnP (zero
    # residual). The real question is robustness: live MediaPipe world landmarks
    # carry model error (tens of mm) and image landmarks carry pixel jitter. An
    # exact solver that amplified noise would be a step backwards.
    print("\n" + "=" * 68)
    print("Q3  NOISE SENSITIVITY (world-landmark sigma in mm + 1 px pixel noise)")
    print("=" * 68)
    rng = np.random.default_rng(0)
    sub = rng.choice(len(norm), size=min(8000, len(norm)), replace=False)
    nsub, uvsub, hipsub = norm[sub], uv_cam[sub], true_hip[sub]
    relsub = nsub - hipsub[:, None, :]
    print(f"  n={len(sub)} poses; abs-mean hip error in metres (Z | X | Y)")
    for sigma_mm in (0.0, 10.0, 25.0, 50.0):
        wnoise = rng.normal(0.0, sigma_mm / 1000.0, relsub.shape)
        pxnoise = rng.normal(0.0, 1.0, uvsub.shape)
        rel_n = relsub + wnoise
        uv_n = uvsub + pxnoise
        # Torso estimator reads metric lengths from pos differences, so the root
        # offset cancels -- feed noisy rel re-anchored at the true root.
        pos_n = rel_n + hipsub[:, None, :]
        tx, ty, tz, _ = recover_root(pos_n, uv_n, in_plane=True)
        qx, qy, qz = recover_root_pnp(rel_n, uv_n)
        et = [np.nanmean(np.abs(v)) for v in (tz - hipsub[:, 2], tx - hipsub[:, 0], ty - hipsub[:, 1])]
        eq = [np.nanmean(np.abs(v)) for v in (qz - hipsub[:, 2], qx - hipsub[:, 0], qy - hipsub[:, 1])]
        print(f"    sigma {sigma_mm:4.0f} mm   torso {et[0]:.4f} | {et[1]:.4f} | {et[2]:.4f}"
              f"     pnp {eq[0]:.4f} | {eq[1]:.4f} | {eq[2]:.4f}")

    print("\n" + "=" * 68)
    print("READ THIS BEFORE QUOTING THE NUMBERS")
    print("=" * 68)
    print("""  Q2 feeds the estimator PERFECT hip-relative 3D. Real MediaPipe world
  landmarks carry their own error on top, so Q2 is a LOWER BOUND on root
  error, not a prediction of live accuracy.

  The corpus is authored choreography: no sensor noise, no occlusion
  dropout, feet clamped to a flat floor, and frames at beat resolution
  rather than 30 Hz. A method that does well here is not thereby validated
  against real Kinect or webcam jitter.""")


if __name__ == "__main__":
    main()
