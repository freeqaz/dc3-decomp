#!/usr/bin/env python3
"""Measure what CAN be measured about the hand/foot joints without hand/foot GT.

WHY: AIST++ GT is COCO-17 -- it stops at wrists/ankles, but DC3's 20-joint
skeleton extends past both: HandL/R = pinky/index knuckle midpoint, FootL/R =
heel/toe blend (pose_mediapipe.py _remap). Those four joints are heavily
weighted by the scorer and are exactly the ones bench_model_z.py had to drop.
Until external hand/foot GT lands (CMU Panoptic hands / Kinect datasets / a
real DC3 SkeletonClip capture), this script extracts every hand/foot quality
signal the cached AIST++ runs already contain:

  Q1  2D REPROJECTION CONSISTENCY. The 2D heatmap landmarks are direct image
      evidence -- heatmap models localise visible extremities well. The GHUM
      3D fit is a separate model head. Projecting our absolute 3D (world +
      recovered root, hfov=true) back into the image and comparing against
      MediaPipe's own (undistorted) 2D landmarks measures how well the 3D
      honours the image evidence, per landmark. Body joints act as the
      control: if hands/feet reproject ~as well as elbows/knees, their
      image-plane 3D is equally trustworthy; the residual open question is
      then only their DEPTH. (Distortion note: AIST++ has k1 radial
      distortion; observed 2D is undistorted via cv2.undistortPoints before
      comparing against the ideal pinhole projection, so edge-of-frame
      landmarks are not penalised.)

  Q2  BONE RIGIDITY. wrist->knuckle and ankle->heel/toe are rigid segments;
      their per-sequence length spread vs forearm/shank controls measures 3D
      noise on the extremity end of the bone, independent of root recovery
      (world space, root-free). READ THE std COLUMN, NOT CV: CV = std/mean is
      scale-confounded, and the extremity bones are 3-5x shorter than the
      controls, so their CV is inflated by the denominator alone.

  Q3  STANCE-CONDITIONED NOISE. When the GT ankle is still (<0.15 m/s over a
      >=10-frame run), the foot is planted: ALL variance in our predicted
      heel/toe over that run is noise. GT is used only as a stillness oracle;
      its own within-run std is printed as the oracle floor (must be << the
      measured noise, else the oracle is broken).
      Reported TWO ways, because the split is the whole point:
        absolute      -- includes the recovered root, which is a COMMON-MODE
                         term shared by every joint (and which the shipping
                         server additionally OneEuro-filters on z, ~4.7x; not
                         applied here, so absolute numbers are pre-filter).
        ankle-relative-- heel/toe minus our own predicted ankle. Root cancels
                         exactly, so this is the foot landmarks' OWN 3D noise.
      Bonus anatomical checks per stance run: ankle height above heel
      (real: ~6-9 cm) and heel-vs-toe height difference. Median as well as
      mean, because "GT ankle still" includes ball-of-foot (heel-raised)
      stances, which are a real bimodal population in dance, not an error.

  Q4  NOISE GRADIENT ALONG THE LIMB. Mean |second difference| (accel proxy at
      the native ~60 fps; real dance motion is smooth, frame-scale
      acceleration is mostly noise) for shoulder->elbow->wrist->knuckles and
      hip->knee->ankle->heel/toe, in world space. The absolute values include
      real acceleration, so only the GRADIENT is meaningful -- the per-hop
      INCREMENT is printed explicitly. CAVEAT: world landmarks are
      HIP-RELATIVE (the hip midpoint IS the origin), so the "hip" entry is a
      degenerate near-zero and is not a usable control; compare each
      extremity against its own parent joint instead.

VALIDATED (probes run 2026-07-31, all 7 cached sequences):
  * Sign convention. Applying u = W/2 - f*X/Z, v = H/2 - f*Y/Z (the form
    _absolute_root's docstring assumes) to bench_model_z.gt_to_dc3 output
    reproduces cv2.projectPoints(dist=0) to 1e-13 px. AIST++ has fx == fy and
    the principal point EXACTLY at (W/2, H/2), and hfov is derived from fx, so
    _focal_px(W) == K[0,0] identically -- the Q1 projection is exact, not
    approximate.
  * Undistortion direction. cv2.undistortPoints(P=K) maps observed (distorted)
    pixels into the ideal pinhole frame, which is the direction needed here.
    Its magnitude is small at the median (0.01-0.20 px) but reaches 4-11 px at
    frame edges on the k1 = +0.31 sequence, so it matters for the p90 tail.
  * Q1 is per-landmark, not a common root shift: re-running with the per-frame
    visibility-weighted mean residual subtracted moves every group median by
    < 0.15 px.
  * The Y sign convention is self-checking: the SAME axis flip yields an
    anatomically CORRECT ankle-above-heel (+4.3 cm) and an anatomically WRONG
    heel-above-toe (+6.7 cm). A flipped Y would have broken both, so the foot
    result below is a shape error, not a sign error.

HEADLINE (7 AIST++ sequences, 11,794 frames, 99.8% detection):
  Hands are as trustworthy as the GT-validated wrist. +0.4 px reprojection
  over the body control, 1.3-1.7 cm knuckle-bone noise (BELOW the forearm's
  3.0 cm), +5.0 mm/frame^2 jitter past the wrist vs the wrist's own +19.3
  increment past the elbow. No axis stands out.
  Feet are noisier but not by much, and the real defect is a BIAS, not noise.
  Reprojection +3.0 px (+1.2 cm) over control, worst of any group; ankle-heel
  is the most rigid bone measured (0.3 cm); stance jitter, once the common-mode
  root is removed, is 2.5 mm (heel) / 9.2 mm (toe). But the foot carries a
  ~30 deg TOE-DOWN PITCH in 98% of all frames (unimodal, no flat-foot mode,
  every sequence 26-42 deg): the toe lands ~6.7 cm below the heel where a
  planted foot should be within ~3 cm. Via FOOT_TOE_BLEND = 0.35 that drags
  DC3's FootL/R about 2.3 cm downward -- a systematic offset the scorer sees
  on every frame, and a candidate contributor to the feet-in-floor lane.
  Caveat on the absolute Q3 z numbers (~8 cm, identical for heel/toe/ankle):
  that is entirely recovered-root depth jitter, shared by every joint, and the
  shipping server OneEuro-filters it (~4.7x) downstream of what is replayed
  here. It is not foot-specific noise.

Run:  .venv/bin/python tools/pose_corpus/bench_extremities.py \
          [--gt-dir /home/free/tmp/pose_gt] [--seqs ...]
"""

import argparse
import os
import pickle
import sys

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_model_z as bmz  # noqa: E402  (camera/GT loaders, conventions)
from bench_model_z import MODEL_PATH  # noqa: E402
from pose_mediapipe import MediaPipeBackend  # noqa: E402

# BlazePose 33-landmark indices (pose_mediapipe.py:44-55)
NOSE = 0
L_EAR, R_EAR = 7, 8
L_SHO, R_SHO = 11, 12
L_ELB, R_ELB = 13, 14
L_WRI, R_WRI = 15, 16
L_PIN, R_PIN = 17, 18
L_IDX, R_IDX = 19, 20
L_HIP, R_HIP = 23, 24
L_KNE, R_KNE = 25, 26
L_ANK, R_ANK = 27, 28
L_HEE, R_HEE = 29, 30
L_FOO, R_FOO = 31, 32

GROUPS = {
    "body (control)": [L_SHO, R_SHO, L_ELB, R_ELB, L_HIP, R_HIP, L_KNE, R_KNE],
    "wrist": [L_WRI, R_WRI],
    "ankle": [L_ANK, R_ANK],
    "hand (pinky/index)": [L_PIN, R_PIN, L_IDX, R_IDX],
    "foot (heel/toe)": [L_HEE, R_HEE, L_FOO, R_FOO],
}

BONES = [
    # (name, a, b, is_control)
    ("upper arm  L", L_SHO, L_ELB, True), ("upper arm  R", R_SHO, R_ELB, True),
    ("forearm    L", L_ELB, L_WRI, True), ("forearm    R", R_ELB, R_WRI, True),
    ("wrist-pinky L", L_WRI, L_PIN, False), ("wrist-pinky R", R_WRI, R_PIN, False),
    ("wrist-index L", L_WRI, L_IDX, False), ("wrist-index R", R_WRI, R_IDX, False),
    ("thigh      L", L_HIP, L_KNE, True), ("thigh      R", R_HIP, R_KNE, True),
    ("shank      L", L_KNE, L_ANK, True), ("shank      R", R_KNE, R_ANK, True),
    ("ankle-heel L", L_ANK, L_HEE, False), ("ankle-heel R", R_ANK, R_HEE, False),
    ("ankle-toe  L", L_ANK, L_FOO, False), ("ankle-toe  R", R_ANK, R_FOO, False),
    ("heel-toe   L", L_HEE, L_FOO, False), ("heel-toe   R", R_HEE, R_FOO, False),
]

CHAIN_ARM = [("shoulder", [L_SHO, R_SHO]), ("elbow", [L_ELB, R_ELB]),
             ("wrist", [L_WRI, R_WRI]), ("knuckles", [L_PIN, R_PIN, L_IDX, R_IDX])]
CHAIN_LEG = [("hip", [L_HIP, R_HIP]), ("knee", [L_KNE, R_KNE]),
             ("ankle", [L_ANK, R_ANK]), ("heel", [L_HEE, R_HEE]),
             ("toe", [L_FOO, R_FOO])]


def load_seq(seq_c01, gt_dir, cache_dir):
    seq_all = seq_c01.replace("_c01_", "_cAll_")
    cam_name = seq_c01.split("_")[2]
    mapping = bmz.load_mapping(gt_dir)
    cam = bmz.load_camera(gt_dir, mapping[seq_all], cam_name)
    kp_cm = pickle.load(open(os.path.join(gt_dir, "keypoints3d", f"{seq_all}.pkl"),
                             "rb"))["keypoints3d_optim"].astype(np.float64)
    d = np.load(os.path.join(cache_dir, f"{seq_c01}.npz"))
    cache = {k: d[k] for k in d.files}
    F = min(len(cache["det"]), len(kp_cm))
    for k in ("world", "img", "vis", "det"):
        cache[k] = cache[k][:F]
    gt_dc3 = bmz.gt_to_dc3(kp_cm[:F], cam)
    gt_ok = np.isfinite(gt_dc3).all(axis=(1, 2))
    return cache, gt_dc3, cache["det"] & gt_ok, cam


def abs_joints(geom_be, cache, W, H):
    """Absolute 3D for all 33 landmarks (DC3 axes) + per-frame root validity."""
    F = len(cache["det"])
    out = np.full((F, 33, 3), np.nan)
    for i in range(F):
        if not cache["det"][i]:
            continue
        world, img, vis = cache["world"][i], cache["img"][i], cache["vis"][i]
        root = geom_be._absolute_root(world, img, vis, W, H)
        if root is None:
            continue
        out[i, :, 0] = -world[:, 0] + root[0]
        out[i, :, 1] = -world[:, 1] + root[1]
        out[i, :, 2] = world[:, 2] + root[2]
    return out


def q1_reprojection(abs3d, cache, cam, f):
    """Per-group reprojection residual: ideal-pinhole projection of our 3D vs
    the model's own UNDISTORTED 2D landmarks. Returns {group: (px, m)} arrays."""
    W, H = cam["W"], cam["H"]
    valid = np.isfinite(abs3d[:, 0, 0])
    empty = np.zeros(0)
    if not valid.any():                       # undistortPoints rejects 0 points
        return {g: (empty, empty) for g in GROUPS}
    A = abs3d[valid]
    obs_px = cache["img"][valid] * np.array([W, H])            # distorted image px
    vis = cache["vis"][valid]
    # Undistort observations into the ideal pinhole frame (same K). cv2 wants
    # an (N,1,2) float64 point cloud; flatten frames+landmarks and restore.
    und = cv2.undistortPoints(np.ascontiguousarray(obs_px.reshape(-1, 1, 2), dtype=np.float64),
                              cam["K"], cam["dist"], P=cam["K"]).reshape(A.shape[0], 33, 2)
    # Mirrored pinhole (DC3 axes): u = W/2 - f*X/Z, v = H/2 - f*Y/Z
    u = W * 0.5 - f * A[..., 0] / A[..., 2]
    v = H * 0.5 - f * A[..., 1] / A[..., 2]
    err_px = np.hypot(u - und[..., 0], v - und[..., 1])
    err_m = err_px * A[..., 2] / f          # metres at the landmark's own depth
    out = {}
    for g, idx in GROUPS.items():
        m = vis[:, idx] > 0.5
        out[g] = (err_px[:, idx][m], err_m[:, idx][m])
    return out


def q2_bones(cache):
    """World-space bone lengths: per-bone (mean, std, CV) over well-seen frames."""
    w, vis, det = cache["world"], cache["vis"], cache["det"]
    rows = []
    for name, a, b, ctrl in BONES:
        m = det & (vis[:, a] > 0.5) & (vis[:, b] > 0.5)
        if m.sum() < 30:
            continue
        L = np.linalg.norm(w[m, a] - w[m, b], axis=1)
        rows.append((name, ctrl, L.mean(), L.std(), L.std() / L.mean(), int(m.sum())))
    return rows


def stance_runs(gt_dc3, ank_coco, fps=59.94, vmax=0.15, min_len=10):
    """Contiguous frame runs where the GT ankle moves < vmax m/s."""
    g = gt_dc3[:, ank_coco]
    v = np.full(len(g), np.inf)
    v[1:-1] = np.linalg.norm(g[2:] - g[:-2], axis=1) * fps / 2.0
    still = v < vmax
    runs, s = [], None
    for i, b in enumerate(still):
        if b and s is None:
            s = i
        elif not b and s is not None:
            if i - s >= min_len:
                runs.append((s, i))
            s = None
    if s is not None and len(still) - s >= min_len:
        runs.append((s, len(still)))
    return runs


def q3_stance(abs3d, gt_dc3, gtmask, side, fps):
    """side: 'L' or 'R'. Returns per-run rows of noise std + anatomy checks.

    Both views are recorded per run: absolute std (root included, i.e. the
    common-mode term every joint shares) and ankle-relative std (root cancels
    exactly, leaving the heel/toe landmark's own 3D noise).
    """
    ank_c = bmz.C_LANK if side == "L" else bmz.C_RANK
    ank, hee, foo = (L_ANK, L_HEE, L_FOO) if side == "L" else (R_ANK, R_HEE, R_FOO)
    rows = []
    for s, e in stance_runs(gt_dc3, ank_c, fps=fps):
        seg = slice(s, e)
        ok = np.isfinite(abs3d[seg, 0, 0]) & gtmask[seg]
        if ok.sum() < 8:
            continue
        P = abs3d[seg][ok]
        P_hee, P_foo, P_ank = P[:, hee], P[:, foo], P[:, ank]
        gt_ank = gt_dc3[seg][ok][:, ank_c]
        rows.append(dict(
            n=int(ok.sum()),
            heel_std=P_hee.std(axis=0), toe_std=P_foo.std(axis=0),
            ank_std=P_ank.std(axis=0), gt_std=gt_ank.std(axis=0),
            heel_rel_std=(P_hee - P_ank).std(axis=0),
            toe_rel_std=(P_foo - P_ank).std(axis=0),
            ank_above_heel=float((P_ank[:, 1] - P_hee[:, 1]).mean()),
            heel_toe_dy=float((P_hee[:, 1] - P_foo[:, 1]).mean()),
            heel_toe_len=float(np.linalg.norm(P_hee - P_foo, axis=1).mean()),
        ))
    return rows


def q4_chain(cache):
    """Mean |second difference| (metres/frame^2 at native fps) along each chain.

    Second difference over consecutive frames only: the mask requires all three
    of (k, k+1, k+2) valid, so no gap is ever differenced across.
    """
    w, vis, det = cache["world"], cache["vis"], cache["det"]
    out = {}
    for chain in (CHAIN_ARM, CHAIN_LEG):
        for name, idx in chain:
            vals = []
            for i in idx:
                m = det & (vis[:, i] > 0.5)
                m3 = m[:-2] & m[1:-1] & m[2:]
                if not m3.any():
                    continue
                d2 = w[2:][m3, i] - 2 * w[1:-1][m3, i] + w[:-2][m3, i]
                vals.append(np.linalg.norm(d2, axis=1))
            out[name] = np.concatenate(vals) if vals else np.zeros(0)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gt-dir", default="/home/free/tmp/pose_gt")
    ap.add_argument("--cache-dir", default=None)
    ap.add_argument("--seqs", nargs="*", default=None)
    args = ap.parse_args()
    cache_dir = args.cache_dir or os.path.join(args.gt_dir, "cache")
    seqs = args.seqs or sorted(os.path.splitext(f)[0] for f in os.listdir(cache_dir)
                               if f.endswith(".npz"))

    q1_agg = {g: ([], []) for g in GROUPS}
    q2_agg = {}
    q3_rows = []
    q4_agg = {}

    for seq in seqs:
        cache, gt_dc3, gtmask, cam = load_seq(seq, args.gt_dir, cache_dir)
        geom_be = MediaPipeBackend(MODEL_PATH, num_poses=1, hfov_deg=cam["hfov_deg"])
        f = geom_be._focal_px(cam["W"])
        A = abs_joints(geom_be, cache, cam["W"], cam["H"])
        geom_be.close()

        fps = float(cache["fps"])
        for g, (px, m) in q1_reprojection(A, cache, cam, f).items():
            q1_agg[g][0].append(px)
            q1_agg[g][1].append(m)
        for name, ctrl, mean, std, cv, n in q2_bones(cache):
            q2_agg.setdefault((name, ctrl), []).append((mean, std, cv, n))
        for side in "LR":
            q3_rows += q3_stance(A, gt_dc3, gtmask, side, fps)
        for name, arr in q4_chain(cache).items():
            q4_agg.setdefault(name, []).append(arr)
        print(f"  processed {seq}  ({len(cache['det'])} frames, "
              f"det {cache['det'].mean()*100:.1f}%, {fps:.2f} fps)")

    print("\n== Q1: 2D reprojection consistency (our 3D vs model's own undistorted 2D) ==")
    print("   self-consistency, NOT ground truth: measures whether the GHUM 3D head")
    print("   honours the heatmap 2D evidence. Body row is the control.")
    print(f"  {'group':22s} {'med px':>8s} {'p90 px':>8s} {'med m':>8s} {'p90 m':>8s} "
          f"{'vs body':>9s}   n")
    ctrl_med = None
    for g in GROUPS:
        px = np.concatenate(q1_agg[g][0]) if q1_agg[g][0] else np.zeros(0)
        m = np.concatenate(q1_agg[g][1]) if q1_agg[g][1] else np.zeros(0)
        if len(px) == 0:
            print(f"  {g:22s} {'(no data)':>8s}")
            continue
        med = np.median(px)
        if ctrl_med is None:
            ctrl_med = med
        print(f"  {g:22s} {med:8.1f} {np.percentile(px,90):8.1f} "
              f"{np.median(m):8.3f} {np.percentile(m,90):8.3f} "
              f"{med - ctrl_med:+8.1f}p   {len(px)}")

    print("\n== Q2: bone rigidity, world space (root-free) ==")
    print("   READ std, NOT CV: the extremity bones are 3-5x shorter, so CV = std/mean")
    print("   is inflated by its denominator alone. std is the endpoint noise in metres.")
    print(f"  {'bone':16s} {'mean m':>8s} {'std m':>8s} {'CV %':>6s}")
    for (name, ctrl), vals in q2_agg.items():
        mean = np.mean([v[0] for v in vals]); std = np.mean([v[1] for v in vals])
        cv = np.mean([v[2] for v in vals]) * 100
        tag = "" if ctrl else "   <- extremity"
        print(f"  {name:16s} {mean:8.3f} {std:8.3f} {cv:6.1f}{tag}")

    print("\n== Q3: stance-conditioned foot noise (GT-ankle-still runs; std = pure noise) ==")
    if q3_rows:
        n_runs = len(q3_rows)
        frames = sum(r["n"] for r in q3_rows)

        def agg(key):
            return np.mean([r[key] for r in q3_rows], axis=0)

        gs = agg("gt_std")
        print(f"  {n_runs} stance runs, {frames} frames")
        print(f"  oracle floor: GT ankle within-run std "
              f"{gs[0]*100:.2f}/{gs[1]*100:.2f}/{gs[2]*100:.2f} cm (xyz) -- "
              f"{'OK, oracle valid' if gs.max() < 0.01 else 'BROKEN, >1 cm: runs are not still'}")
        print(f"\n  {'landmark':22s} {'std x':>7s} {'std y':>7s} {'std z':>7s}  (m, mean over runs)")
        print("  -- absolute (root included; root is COMMON-MODE, and unfiltered here) --")
        for nm, k in (("heel", "heel_std"), ("toe", "toe_std"), ("ankle", "ank_std")):
            s = agg(k)
            print(f"  {nm:22s} {s[0]:7.3f} {s[1]:7.3f} {s[2]:7.3f}")
        print("  -- ankle-relative (root cancels: the landmark's OWN 3D noise) --")
        for nm, k in (("heel - ankle", "heel_rel_std"), ("toe - ankle", "toe_rel_std")):
            s = agg(k)
            print(f"  {nm:22s} {s[0]:7.4f} {s[1]:7.4f} {s[2]:7.4f}")
        aah = np.array([r["ank_above_heel"] for r in q3_rows]) * 100
        htd = np.array([r["heel_toe_dy"] for r in q3_rows]) * 100
        print(f"  anatomy: ankle above heel      mean {aah.mean():+.1f} "
              f"median {np.median(aah):+.1f} cm  (real ~4-7)")
        # Heel-above-toe is a foot PITCH, and pitch is root-free: it depends only
        # on the heel/toe landmarks' relative geometry, so neither root recovery
        # nor the missing floor reference can explain it away. A ball-of-foot
        # stance would make the distribution BIMODAL (flat runs near 0, raised
        # runs high); a unimodal offset with a high >5 cm fraction is instead a
        # systematic toe-down tilt baked into the GHUM foot.
        pitch = np.degrees(np.arcsin(np.clip(
            htd / 100.0 / np.array([r["heel_toe_len"] for r in q3_rows]), -1, 1)))
        frac = float(np.mean(htd > 5))
        print(f"  anatomy: heel above toe        mean {htd.mean():+.1f} "
              f"median {np.median(htd):+.1f} cm  (flat foot ~0-3)")
        print(f"           = toe-down pitch {pitch.mean():+.0f} deg "
              f"(median {np.median(pitch):+.0f}); {frac*100:.0f}% of runs > 5 cm")
        verdict = ("UNIMODAL -> systematic toe-down tilt in the GHUM foot"
                   if frac > 0.6 else
                   "BIMODAL -> consistent with genuine ball-of-foot stances")
        print(f"           {verdict}")
    else:
        print("  no stance runs found")

    print("\n== Q4: noise gradient along the limb (mean |2nd diff|, mm/frame^2 @ ~60 fps) ==")
    print("   absolute values include REAL acceleration -- only the per-hop increment")
    print("   is a noise signal. 'hip' is degenerate: world landmarks are hip-relative.")
    for chain in (CHAIN_ARM, CHAIN_LEG):
        parts, prev = [], None
        for name, _ in chain:
            arr = q4_agg.get(name)
            arr = np.concatenate(arr) if arr else np.zeros(0)
            val = arr.mean() * 1000 if len(arr) else float("nan")
            parts.append(f"{name} {val:.1f}" + ("" if prev is None else f" ({val-prev:+.1f})"))
            prev = val
        print("  " + "  ->  ".join(parts))


if __name__ == "__main__":
    main()
