#!/usr/bin/env python3
"""Localise the ~30 deg TOE-DOWN foot pitch bench_extremities.py found, and cost
out every fix that is implementable in the live server.

CONTEXT: bench_extremities.py (Q3) measured, over 7 AIST++ sequences / 11,794
frames, that during GT-verified stance the predicted toe (BlazePose foot_index,
lm 31/32) sits ~6.7 cm BELOW the heel (lm 29/30) -- a ~30 deg toe-down pitch,
unimodal, 26-42 deg per sequence, 98% of frames. A planted foot is flat. Via
FOOT_TOE_BLEND = 0.35 that drags DC3's FootL/R downward on every frame.
That bench could not say WHERE the error lives, because it never left DC3
camera space: with no floor and no gravity it can only measure the heel->toe
vector against itself. This probe adds the two references AIST++ actually has
(a calibrated floor plane, and the model's own 2D heatmap head) and asks:

  A  FLOOR-REFERENCED HEIGHTS. Invert bench_model_z.gt_to_dc3 to put our
     predicted absolute 3D back into AIST++ WORLD coordinates, where +Y is up
     and the floor is a level plane (verified: a plane fit to GT stance ankles
     has a normal within 1.5 deg of +Y on the 5 sequences with >100 stance
     frames; the two with <30 frames fit noise and print a large tilt, which
     is why the tilt column is printed rather than trusted). Floor level is
     GT's own: p2 of the GT stance-ankle height minus ANKLE_ABOVE_FLOOR_CM.
     Then: does the predicted toe go BELOW the floor, and does the heel sit at
     a plausible sole height? Reported twice -- raw absolute (carries the
     recovered root's error) and GT-ankle-anchored (predicted foot re-anchored
     so our ankle coincides with the GT ankle, which removes root error and
     model ankle error, leaving only the foot's own shape).

  B  WHAT THE 2D SAYS. The cached img landmarks are the heatmap head's direct
     image evidence, independent of the GHUM 3D fit. Anchored on the SAME
     predicted heel, two 3D toes are projected into the (undistorted) image:
       (a) GHUM's own toe;
       (b) FLAT-FOOT toe = heel + |heel->toe| along the horizontal (floor-
           parallel) component of the heel->toe direction.
     Whichever lands closer to the observed 2D toe is the one the model's own
     perception supports. The heel's own reprojection residual is printed as
     the noise floor -- a difference smaller than that means nothing. A third
     construction (RAY x FLAT-PLANE: intersect the camera ray through the
     observed 2D toe with the horizontal plane through the heel) is a
     falsifiable cross-check: if flat-foot is right, that intersection must
     come out at a sane foot length, not at 5 cm or 2 m. It comes out at 36 cm
     -- NOT because a flat foot is wrong, but because the ray grazes; see H,
     which asks the same question in a well-conditioned form.

  C  DECOMPOSITION against the GT-validated ankle. Heel and toe expressed
     relative to our own predicted ankle, in gravity-aligned world axes, vs
     anthropometry (ankle joint centre ~7-9 cm above the sole; heel landmark
     behind and ~4-6 cm below the ankle; toe ~15-16 cm forward at sole level).
     Plus the ankle's own height error vs GT, which separates "toe too low"
     from "heel too high". And a rigidity test: the angle between the heel->toe
     vector and the shank -- if that angle barely varies, GHUM is not
     articulating the ankle at all and the pitch is a rest-pose constant.

  D  CANDIDATE FIXES, EVALUATED NOT SHIPPED. Production has NO floor and NO
     gravity vector, so the flat-foot construction of B is NOT implementable;
     the only up reference a live webcam has is +Y of camera space (the
     camera-is-level assumption). Every candidate here is written against that
     proxy, and the cost of the assumption is bounded by re-running each
     candidate with the TRUE floor normal (AIST++ cameras are 0.2-3.6 deg off
     level, so this is a lower bound on the error a tilted webcam would add --
     stated, not hidden). Candidates are scored on stance frames (flatness,
     floor clearance, FootL/R height) AND on high-ankle-velocity frames, where
     a correction that flattens genuine plantarflexion -- kicks, releves,
     toe-points, all of which DC3 choreography contains -- would be a
     regression the stance metrics cannot see.

  E  IS THE PITCH A CONSTANT OFFSET? Signed pitch percentiles per sequence in
     three regimes (stance / swing / all). No dance corpus can be 100% non-flat.

  F  IS THE FOOT ARTICULATED? Foot pitch regressed on shank pitch, plus the
     shank^foot angle's spread. A model that barely moves the ankle is
     reporting a rest pose, not a measurement.

  G  LANDMARK SEMANTICS FROM 2D ALONE. Heel/toe marker offsets from the ankle,
     in cm at the GT ankle's depth, with NO GHUM 3D anywhere in the number.

  H  PUT THE 2D TOE ON THE FLOOR. The confound-free version of G: a grazing
     ray cannot separate "lower" from "nearer", so instead of reading the toe's
     pixel height, assume the toe is on the floor, let the ray pick the depth,
     and check whether it lands an anatomical distance from the GT ankle.

HEADLINE (7 sequences, 4,929 GT-verified stance frames, hfov=true):

  The bias is REAL but roughly HALF of it is landmark semantics, not error, and
  the residual error is a HEIGHT error at the toe of only ~2-3 cm -- so the
  "2.3 cm of downward drag on FootL/R" is closer to ~1 cm.

  * WHERE IT LIVES: the GHUM 3D fit, not the 2D head, and not our geometry.
    Our ankle matches the GT ankle to 0.1 cm, so root recovery and the axis
    chain are exonerated. Reprojected, GHUM's toe is as good as the heel
    control (9.5 px vs 9.9 px) -- but a SIGNED heel->toe pixel drop, where the
    root cancels, says observed 2D +14.0 px against GHUM's own +20.9 px: the
    3D head over-pitches by ~6 px beyond what its own 2D supports. Pinning the
    foot length and taking only the DIRECTION from 2D (the test that survives
    the grazing-ray ill-conditioning) gives -19.9 deg at GHUM's own 13.8 cm
    foot and -10.2 deg at an anatomical 19 cm, against GHUM's -29.6 deg.

  * THE FLAT-FOOT HYPOTHESIS LOSES ON RAW REPROJECTION (13.6 px vs 9.5 px, wins
    23% of frames) -- but that comparison is rigged against it, because GHUM's
    foot is 13.8 cm where a real one is ~19, so a length-preserving flat toe
    cannot reach the observed pixel. Section H settles it properly: assume the
    toe is ON the floor and let the ray choose the depth, and it lands 12.6 cm
    horizontally from the GT ankle -- an anatomical toe position. A flat foot
    is fully consistent with the image.

  * MOST OF THE DROP IS SEMANTICS. The BlazePose heel landmark is not on the
    sole: from 2D alone at the GT ankle's depth it sits 6.7 cm above the floor
    (GHUM's 3D says 4.9), only 3 cm behind the ankle. A truly flat foot with
    THESE landmarks would still show ~4-5 cm of heel-above-toe. Measured 6.4.
    The excess is ~2 cm, concentrated in the toe: 1.0-1.5 cm BELOW the floor
    plane (69% of stance frames), where it should be 1-3 cm above.

  * THE ANGLE IS INFLATED BY A SHORT FOOT. heel->toe is 13.8 cm vs ~19 real,
    and the toe reaches only 11.5 cm forward of the ankle vs ~15.5. The same
    vertical drop over a 27% shorter lever is what turns ~4-5 cm into "30 deg".

  * THE ANKLE IS BARELY ARTICULATED. shank^foot angle 81 deg with p5-p95 of
    62-97 -- about +-17 deg of total ankle range across 23k frames, where a
    real ankle covers ~70. Foot pitch is >= 0 on 0-4% of frames in ANY regime,
    including 11k frames of stance. Whatever GHUM emits for the foot is close
    to a rest-pose constant hung off the shank.

  * FIXES. A fixed pitch-correction rotation about the heel, clipped at
    horizontal so it can never invert a real toe-up pose, is the only candidate
    that is implementable live (production has no floor and no gravity; the
    camera's +Y is the only up reference, and it costs <0.1 cm here because
    AIST++ cameras are 0.2-3.6 deg off level -- a LOWER bound for a living
    room). +10 to +15 deg is what the 2D evidence supports and it moves FootL/R
    by only 0.8-1.3 cm while lifting the toe to +1.1/+2.2 cm above the floor
    and retaining 72%/58% of >20 deg swing plantarflexion. The +30 deg version
    that "fixes" the headline number destroys 3/4 of it. Blend-weight changes
    (C1/C2) do NOT touch the pitch at all -- they only slide FootL/R along an
    unchanged, wrongly-pitched foot -- and cost 2.8-4.8 cm of joint movement.
    NOTHING HERE IS SHIPPABLE YET: there is no GT for the DC3 Foot joint, so
    the target height of FootL/R is unknown; see the report for the Kinect
    SkeletonClip validation that has to run first.

CONVENTIONS (inherited, see bench_model_z.py docstring for the derivations):
  * gt_to_dc3: AIST++ world cm -> DC3 camera metres, X=-X_cv, Y=-Y_cv, Z=+Z_cv.
    This probe's dc3_to_world is its exact inverse (verified to 1e-13 cm).
  * mirrored pinhole: u = W/2 - f*X/Z, v = H/2 - f*Y/Z, f = _focal_px(W).
    AIST++ has fx == fy with the principal point exactly centred, so with
    hfov=true this reproduces cv2.projectPoints to 1e-13 px.
  * observed 2D is undistorted with cv2.undistortPoints(P=K) before comparison.

Run:  .venv/bin/python tools/pose_corpus/probe_foot_pitch.py \
          [--gt-dir /home/free/tmp/pose_gt] [--seqs ...] [--theta 10 15 20 30]
      .venv/bin/python tools/pose_corpus/probe_foot_pitch.py --dump-frames 3
          -> annotated stance-frame foot crops in /tmp/foot_probe, for the
             eyeball check that no metric can replace (the AIST++ dancers are
             visibly flat-footed in sneakers, and the heel marker is visibly
             above the shoe).
"""

import argparse
import os
import sys

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_model_z as bmz  # noqa: E402
import bench_extremities as bx  # noqa: E402
from bench_model_z import MODEL_PATH  # noqa: E402
from pose_mediapipe import MediaPipeBackend, FOOT_TOE_BLEND  # noqa: E402

L_ANK, R_ANK = bx.L_ANK, bx.R_ANK
L_HEE, R_HEE = bx.L_HEE, bx.R_HEE
L_FOO, R_FOO = bx.L_FOO, bx.R_FOO
L_KNE, R_KNE = bx.L_KNE, bx.R_KNE

# Ankle joint centre (lateral malleolus height) above the ground, standing.
# Anthropometric tables put it at 6-8 cm for adults; 7.5 is the midpoint. The
# floor level is derived from GT ankles by subtracting this, so every "height
# above floor" below inherits its uncertainty -- ~1.5 cm, COMMON to heel, toe
# and ankle alike, which is why heel-MINUS-toe (the pitch) does not depend on it.
ANKLE_ABOVE_FLOOR_CM = 7.5

# Anthropometric expectations for the two foot landmarks, relative to the ankle
# joint centre, for a flat planted foot (used only as printed reference values).
EXP_HEEL_BELOW_ANKLE_CM = 5.5     # heel landmark on the shoe, below+behind ankle
EXP_HEEL_BEHIND_ANKLE_CM = 4.5
EXP_TOE_BELOW_ANKLE_CM = 6.0      # toe box top surface, ~1-2 cm above the sole
EXP_TOE_AHEAD_ANKLE_CM = 15.5

STANCE_VMAX = 0.15                # m/s, GT ankle speed (same as bench_extremities)
SWING_VMIN = 1.00                 # m/s, "the foot is genuinely moving" threshold


# --------------------------------------------------------------------------
# geometry helpers
# --------------------------------------------------------------------------
def dc3_to_world(P_dc3_m, cam):
    """DC3 camera metres -> AIST++ world centimetres. Inverse of gt_to_dc3.

    DC3 -> OpenCV camera axes is the same double flip (X, Y negated), then
    metres -> cm, then X_world = R^T (X_cam - t). Written as (X_cam - t) @ R
    because R^T v == v @ R for the row-vector layout used here.
    """
    Xc = np.stack([-P_dc3_m[..., 0], -P_dc3_m[..., 1], P_dc3_m[..., 2]], axis=-1) * 100.0
    return (Xc - cam["t"]) @ cam["R"]


def world_up_in_dc3(cam, normal_world=None):
    """The world 'up' direction expressed in DC3 camera axes (unit vector)."""
    n = np.array([0.0, 1.0, 0.0]) if normal_world is None else np.asarray(normal_world)
    up_cv = cam["R"] @ n
    up = np.array([-up_cv[0], -up_cv[1], up_cv[2]])
    return up / np.linalg.norm(up)


def project(P, W, H, f):
    """Mirrored pinhole, DC3 axes -> pixels. P: (..., 3)."""
    return np.stack([W * 0.5 - f * P[..., 0] / P[..., 2],
                     H * 0.5 - f * P[..., 1] / P[..., 2]], axis=-1)


def unproject(uv, z, W, H, f):
    """Pixels + depth -> DC3 camera 3D (exact inverse of project)."""
    return np.stack([(W * 0.5 - uv[..., 0]) * z / f,
                     (H * 0.5 - uv[..., 1]) * z / f,
                     z], axis=-1)


def undistort(px, cam):
    """(N,2) distorted pixels -> ideal pinhole pixels (same K)."""
    if len(px) == 0:
        return px
    p = np.ascontiguousarray(px.reshape(-1, 1, 2), dtype=np.float64)
    return cv2.undistortPoints(p, cam["K"], cam["dist"], P=cam["K"]).reshape(px.shape)


def flatten_vec(v, up):
    """Component of v perpendicular to `up`, rescaled to v's original length.

    This is the FLAT-FOOT construction: same length, same horizontal heading,
    zero vertical component. Degenerate (near-vertical v) rows come back as NaN
    rather than exploding.
    """
    v = np.atleast_2d(v)
    h = v - np.outer(v @ up, up)
    hn = np.linalg.norm(h, axis=1)
    ln = np.linalg.norm(v, axis=1)
    out = np.full_like(v, np.nan)
    ok = hn > 1e-6
    out[ok] = h[ok] * (ln[ok] / hn[ok])[:, None]
    return out


def rotate_toward_horizontal(v, up, deg):
    """Rotate v by `deg` toward the horizontal plane, in the plane spanned by v
    and `up` (i.e. the sagittal-ish plane of the foot). Positive deg lifts a
    toe-down vector up. NEVER rotates past horizontal -- the rotation is clipped
    at the flat pose, so a foot that is already flat or toe-UP is left alone.
    This clipping is what keeps the correction from inverting a real releve.
    """
    v = np.atleast_2d(v)
    vert = v @ up                                    # signed vertical component
    horiz = v - np.outer(vert, up)
    hn = np.linalg.norm(horiz, axis=1)
    ln = np.linalg.norm(v, axis=1)
    out = v.copy()
    ok = (hn > 1e-6) & (ln > 1e-6)
    pitch = np.arctan2(vert[ok], hn[ok])             # negative = toe-down
    # only correct toe-DOWN vectors, and never overshoot horizontal
    delta = np.clip(np.radians(deg), 0, None)
    newp = np.minimum(pitch + delta, 0.0)
    newp = np.where(pitch >= 0.0, pitch, newp)       # toe-up left untouched
    hdir = horiz[ok] / hn[ok][:, None]
    out[ok] = (hdir * (ln[ok] * np.cos(newp))[:, None]
               + np.outer(ln[ok] * np.sin(newp), up))
    return out


def signed_pitch_deg(v, up):
    """Signed pitch of vector v about the horizontal: negative = toe-down."""
    v = np.atleast_2d(v)
    vert = v @ up
    horiz = np.linalg.norm(v - np.outer(vert, up), axis=1)
    return np.degrees(np.arctan2(vert, horiz))


def ray_at_length(uv, heel, L, W, H, f, z_hint):
    """Point on the camera ray through pixel `uv` that lies exactly L metres
    from `heel` -- i.e. TAKE THE DIRECTION FROM 2D, TAKE THE LENGTH AS GIVEN.

    This is the decisive form of the 2D test. The failure mode of naive
    ray/plane intersection is that a foot ray is nearly grazing (the AIST++
    cameras look down at the feet by only ~14 deg), so a few cm of height maps
    to tens of cm along the ray and any plane intersection is wildly
    ill-conditioned. Constraining |toe - heel| removes that degree of freedom
    entirely: the only thing left is WHERE ALONG THE RAY, and the sphere of
    radius L around the heel picks it out. If the 2D head really saw a flat
    foot, the point it selects would have ~0 pitch.

    Rays that miss the sphere (the 2D toe is not reachable at that length from
    the heel at all) fall back to the ray's closest approach, and the miss rate
    is reported by the caller.
    """
    d = unproject(uv, np.ones(len(uv)), W, H, f)     # direction (z = 1)
    dd = np.einsum("ij,ij->i", d, d)
    dh = np.einsum("ij,ij->i", d, heel)
    hh = np.einsum("ij,ij->i", heel, heel)
    disc = dh * dh - dd * (hh - L * L)
    miss = disc < 0
    s = np.sqrt(np.maximum(disc, 0.0))
    z1 = (dh - s) / dd
    z2 = (dh + s) / dd
    pick = np.where(np.abs(z1 - z_hint) <= np.abs(z2 - z_hint), z1, z2)
    pick = np.where(miss, dh / dd, pick)             # closest approach
    return d * pick[:, None], miss


# --------------------------------------------------------------------------
# per-sequence data assembly
# --------------------------------------------------------------------------
def stance_mask(gt_dc3, ank_coco, fps, vmax=STANCE_VMAX, min_len=10):
    m = np.zeros(len(gt_dc3), dtype=bool)
    for s, e in bx.stance_runs(gt_dc3, ank_coco, fps=fps, vmax=vmax, min_len=min_len):
        m[s:e] = True
    return m


def gt_ankle_speed(gt_dc3, ank_coco, fps):
    g = gt_dc3[:, ank_coco]
    v = np.full(len(g), np.nan)
    v[1:-1] = np.linalg.norm(g[2:] - g[:-2], axis=1) * fps / 2.0
    return v


def build(seq, gt_dir, cache_dir):
    """Everything downstream needs, for one sequence, both sides."""
    cache, gt_dc3, gtmask, cam = bx.load_seq(seq, gt_dir, cache_dir)
    be = MediaPipeBackend(MODEL_PATH, num_poses=1, hfov_deg=cam["hfov_deg"])
    f = be._focal_px(cam["W"])
    A = bx.abs_joints(be, cache, cam["W"], cam["H"])      # (F,33,3) DC3 metres
    be.close()
    fps = float(cache["fps"])
    Aw = dc3_to_world(A, cam)                             # (F,33,3) world cm
    gt_w = dc3_to_world(gt_dc3, cam)                      # (F,17,3) world cm

    # --- floor from GT stance ankles ------------------------------------
    st = {}
    for side, coco in (("L", bmz.C_LANK), ("R", bmz.C_RANK)):
        st[side] = stance_mask(gt_dc3, coco, fps)
    stance_ank_y = np.concatenate([
        gt_w[st["L"], bmz.C_LANK, 1], gt_w[st["R"], bmz.C_RANK, 1]])
    stance_ank_y = stance_ank_y[np.isfinite(stance_ank_y)]
    if len(stance_ank_y) < 20:
        return None
    ank_level = float(np.percentile(stance_ank_y, 2))
    floor_y = ank_level - ANKLE_ABOVE_FLOOR_CM

    # plane-fit diagnostic: how level is the floor really, in world axes?
    P = np.concatenate([gt_w[st["L"]][:, bmz.C_LANK], gt_w[st["R"]][:, bmz.C_RANK]])
    P = P[np.isfinite(P).all(1)]
    c = P.mean(0)
    n = np.linalg.svd(P - c)[2][2]
    if n[1] < 0:
        n = -n
    floor_tilt = float(np.degrees(np.arccos(np.clip(abs(n[1]), -1, 1))))

    up_dc3 = world_up_in_dc3(cam)                 # true up, DC3 axes
    up_proxy = np.array([0.0, 1.0, 0.0])          # what production can use
    cam_tilt = float(np.degrees(np.arccos(np.clip(up_dc3[1], -1, 1))))

    return dict(seq=seq, cache=cache, cam=cam, f=f, fps=fps, A=A, Aw=Aw,
                gt_dc3=gt_dc3, gt_w=gt_w, gtmask=gtmask, stance=st,
                floor_y=floor_y, ank_level=ank_level, floor_tilt=floor_tilt,
                floor_normal_world=n, up_dc3=up_dc3, up_proxy=up_proxy,
                cam_tilt=cam_tilt)


def side_idx(side):
    if side == "L":
        return L_ANK, L_HEE, L_FOO, L_KNE, bmz.C_LANK
    return R_ANK, R_HEE, R_FOO, R_KNE, bmz.C_RANK


def frame_mask(d, side, kind):
    """Valid frames for `side`: root recovered, GT finite, both foot landmarks
    seen, and the requested motion regime ('stance' | 'swing' | 'all')."""
    ank, hee, foo, _kne, coco = side_idx(side)
    cache = d["cache"]
    m = np.isfinite(d["A"][:, 0, 0]) & d["gtmask"]
    v = cache["vis"]
    m &= (v[:, hee] > 0.5) & (v[:, foo] > 0.5) & (v[:, ank] > 0.5)
    if kind == "stance":
        m &= d["stance"][side]
    elif kind == "swing":
        spd = gt_ankle_speed(d["gt_dc3"], coco, d["fps"])
        m &= np.nan_to_num(spd, nan=0.0) > SWING_VMIN
    return m


# --------------------------------------------------------------------------
# A: floor-referenced heights
# --------------------------------------------------------------------------
def analysis_A(ds):
    print("\n" + "=" * 78)
    print("A. FLOOR-REFERENCED HEIGHTS during GT-verified stance (cm above floor)")
    print("=" * 78)
    print(f"   floor := p2(GT stance ankle height) - {ANKLE_ABOVE_FLOOR_CM} cm ankle offset.")
    print("   'anchored' re-anchors our foot on the GT ankle (removes root + ankle error),")
    print("   so it isolates the FOOT's own shape. 'absolute' keeps our recovered root.")
    print(f"   {'sequence':28s} {'cam':>5s} {'flr':>5s} | "
          f"{'ankle':>6s} {'heel':>6s} {'toe':>6s} | {'heel*':>6s} {'toe*':>6s} "
          f"{'below0':>7s}   n")
    print(f"   {'':28s} {'tilt':>5s} {'tilt':>5s} | {'--- absolute ---':>20s} | "
          f"{'-- GT-ankle anchored --':>21s}")
    agg = {k: [] for k in ("ank", "hee", "toe", "hee_a", "toe_a")}
    for d in ds:
        rows = {k: [] for k in agg}
        for side in "LR":
            ank, hee, foo, _k, coco = side_idx(side)
            m = frame_mask(d, side, "stance")
            if m.sum() < 8:
                continue
            Aw, gtw = d["Aw"][m], d["gt_w"][m]
            fy = d["floor_y"]
            rows["ank"].append(Aw[:, ank, 1] - fy)
            rows["hee"].append(Aw[:, hee, 1] - fy)
            rows["toe"].append(Aw[:, foo, 1] - fy)
            # anchored: predicted foot rigidly translated onto the GT ankle
            shift = gtw[:, coco] - Aw[:, ank]
            rows["hee_a"].append(Aw[:, hee, 1] + shift[:, 1] - fy)
            rows["toe_a"].append(Aw[:, foo, 1] + shift[:, 1] - fy)
        if not rows["ank"]:
            continue
        r = {k: np.concatenate(v) for k, v in rows.items()}
        for k in agg:
            agg[k].append(r[k])
        below = float(np.mean(r["toe_a"] < 0.0)) * 100
        print(f"   {d['seq'][:28]:28s} {d['cam_tilt']:5.1f} {d['floor_tilt']:5.1f} | "
              f"{np.median(r['ank']):6.1f} {np.median(r['hee']):6.1f} "
              f"{np.median(r['toe']):6.1f} | {np.median(r['hee_a']):6.1f} "
              f"{np.median(r['toe_a']):6.1f} {below:6.0f}% {len(r['ank']):5d}")
    a = {k: np.concatenate(v) for k, v in agg.items() if v}
    if not a:
        return None
    print(f"   {'ALL (median)':28s} {'':5s} {'':5s} | "
          f"{np.median(a['ank']):6.1f} {np.median(a['hee']):6.1f} "
          f"{np.median(a['toe']):6.1f} | {np.median(a['hee_a']):6.1f} "
          f"{np.median(a['toe_a']):6.1f} {np.mean(a['toe_a']<0)*100:6.0f}% "
          f"{len(a['ank']):5d}")
    print(f"\n   expectation, flat planted foot: ankle ~{ANKLE_ABOVE_FLOOR_CM:.0f}, "
          f"heel ~2-4 (shoe sole + heel pad), toe ~1-3, none below 0.")
    print(f"   anchored heel {np.median(a['hee_a']):+.1f} cm, anchored toe "
          f"{np.median(a['toe_a']):+.1f} cm -> "
          f"{'TOE PENETRATES THE FLOOR' if np.median(a['toe_a']) < 0 else 'toe above floor'}"
          f"; {np.mean(a['toe_a']<0)*100:.0f}% of stance frames below floor.")
    return a


# --------------------------------------------------------------------------
# B: 2D hypothesis test
# --------------------------------------------------------------------------
def analysis_B(ds):
    print("\n" + "=" * 78)
    print("B. WHAT THE 2D HEATMAP SAYS: GHUM toe vs FLAT-FOOT toe, reprojected (px)")
    print("=" * 78)
    print("   Both hypotheses share the SAME predicted heel; only the heel->toe")
    print("   direction differs. Observed 2D is undistorted. The heel's own residual")
    print("   is the noise floor: a difference below it is not evidence.")
    print(f"   {'sequence':28s} {'heel':>7s} | {'GHUM':>7s} {'flat':>7s} "
          f"{'delta':>7s} | {'flat wins':>9s} {'raylen':>7s}   n")
    tot = {k: [] for k in ("heel", "ghum", "flat", "raylen", "raypitch", "ghumpitch",
                           "dv_obs", "dv_ghum", "dv_flat", "rv_heel", "rv_toe",
                           "pitch2d_g", "pitch2d_g_miss",
                           "pitch2d_19", "pitch2d_19_miss", "ghumlen")}
    for d in ds:
        rows = {k: [] for k in tot}
        cam, f = d["cam"], d["f"]
        W, H = cam["W"], cam["H"]
        up = d["up_dc3"]
        for side in "LR":
            ank, hee, foo, _k, _c = side_idx(side)
            m = frame_mask(d, side, "stance")
            if m.sum() < 8:
                continue
            A = d["A"][m]
            obs = d["cache"]["img"][m] * np.array([W, H])
            obs_u = undistort(obs.reshape(-1, 2), cam).reshape(obs.shape)
            heel3, toe3 = A[:, hee], A[:, foo]
            v = toe3 - heel3
            flat3 = heel3 + flatten_vec(v, up)
            p_heel = project(heel3, W, H, f)
            p_ghum = project(toe3, W, H, f)
            p_flat = project(flat3, W, H, f)
            rows["heel"].append(np.linalg.norm(p_heel - obs_u[:, hee], axis=1))
            rows["ghum"].append(np.linalg.norm(p_ghum - obs_u[:, foo], axis=1))
            rows["flat"].append(np.linalg.norm(p_flat - obs_u[:, foo], axis=1))
            # RAY x FLAT-PLANE: camera ray through the observed 2D toe, met with
            # the horizontal plane through the heel. Falsifiable: a wrong flat
            # hypothesis produces an absurd foot length here.
            dirv = unproject(obs_u[:, foo], np.ones(len(A)), W, H, f)
            denom = dirv @ up
            zz = (heel3 @ up) / np.where(np.abs(denom) < 1e-6, np.nan, denom)
            hit = dirv * zz[:, None]
            rows["raylen"].append(np.linalg.norm(hit - heel3, axis=1) * 100)
            rows["raypitch"].append(signed_pitch_deg(hit - heel3, up))
            rows["ghumpitch"].append(signed_pitch_deg(v, up))
            rows["ghumlen"].append(np.linalg.norm(v, axis=1) * 100)
            # SIGNED vertical image geometry -- root error is common-mode and
            # cancels in a heel->toe pixel DIFFERENCE, unlike the residual norms
            # above, so this is the cleanest statement the 2D can make.
            rows["dv_obs"].append(obs_u[:, foo, 1] - obs_u[:, hee, 1])
            rows["dv_ghum"].append(p_ghum[:, 1] - p_heel[:, 1])
            rows["dv_flat"].append(p_flat[:, 1] - p_heel[:, 1])
            rows["rv_heel"].append(p_heel[:, 1] - obs_u[:, hee, 1])
            rows["rv_toe"].append(p_ghum[:, 1] - obs_u[:, foo, 1])
            # direction from 2D, length pinned: at GHUM's own foot length and at
            # an anatomical 19 cm.
            Lg = np.linalg.norm(v, axis=1)
            for key, L in (("pitch2d_g", Lg), ("pitch2d_19", 0.19)):
                Lv = L if np.ndim(L) else np.full(len(A), L)
                pts, miss = ray_at_length(obs_u[:, foo], heel3, Lv, W, H, f,
                                          toe3[:, 2])
                rows[key].append(signed_pitch_deg(pts - heel3, up))
                rows[key + "_miss"].append(miss.astype(float))
        if not rows["heel"]:
            continue
        r = {k: np.concatenate(v) for k, v in rows.items()}
        for k in tot:
            tot[k].append(r[k])
        win = float(np.mean(r["flat"] < r["ghum"])) * 100
        print(f"   {d['seq'][:28]:28s} {np.median(r['heel']):7.1f} | "
              f"{np.median(r['ghum']):7.1f} {np.median(r['flat']):7.1f} "
              f"{np.median(r['flat'])-np.median(r['ghum']):+7.1f} | {win:8.0f}% "
              f"{np.nanmedian(r['raylen']):7.1f} {len(r['heel']):5d}")
    t = {k: np.concatenate(v) for k, v in tot.items() if v}
    if not t:
        return None
    win = float(np.mean(t["flat"] < t["ghum"])) * 100
    print(f"   {'ALL (median px)':28s} {np.median(t['heel']):7.1f} | "
          f"{np.median(t['ghum']):7.1f} {np.median(t['flat']):7.1f} "
          f"{np.median(t['flat'])-np.median(t['ghum']):+7.1f} | {win:8.0f}% "
          f"{np.nanmedian(t['raylen']):7.1f} {len(t['heel']):5d}")
    verdict = ("2D FAVOURS FLAT-FOOT -> the GHUM 3D toe is the culprit"
               if np.median(t["flat"]) < np.median(t["ghum"]) else
               "2D FAVOURS GHUM -> the whole model perceives the foot toe-down")
    print(f"\n   {verdict}")
    print(f"   GHUM toe reproj {np.median(t['ghum']):.1f} px vs heel control "
          f"{np.median(t['heel']):.1f} px; flat-foot toe {np.median(t['flat']):.1f} px.")
    print(f"   RAY x FLAT-PLANE implied foot length median "
          f"{np.nanmedian(t['raylen']):.1f} cm "
          f"(real shoe heel->toe-box ~16-22 cm) -- if this is sane, a flat foot is")
    print("   geometrically consistent with the observed 2D; if absurd, it is not.")
    print(f"   GHUM stance pitch median {np.median(t['ghumpitch']):+.1f} deg; the same")
    print(f"   foot reconstructed from 2D at the flat plane: "
          f"{np.nanmedian(t['raypitch']):+.1f} deg by construction (0 = flat).")

    print("\n   -- SIGNED heel->toe pixel drop (root error cancels in the difference) --")
    print(f"   observed 2D               {np.median(t['dv_obs']):+7.1f} px "
          f"[p25 {np.percentile(t['dv_obs'],25):+.1f}, "
          f"p75 {np.percentile(t['dv_obs'],75):+.1f}]")
    print(f"   GHUM 3D predicts          {np.median(t['dv_ghum']):+7.1f} px")
    print(f"   a FLAT foot would predict {np.median(t['dv_flat']):+7.1f} px")
    print(f"   per-landmark signed v residual (pred - obs): heel "
          f"{np.median(t['rv_heel']):+.1f} px, toe {np.median(t['rv_toe']):+.1f} px")

    print("\n   -- DIRECTION FROM 2D, LENGTH PINNED (the ill-conditioning-free test) --")
    for key, lbl in (("pitch2d_g", "at GHUM's own foot length"),
                     ("pitch2d_19", "at an anatomical 19 cm")):
        p = t[key]
        print(f"   toe pitch from the 2D ray, {lbl:26s} "
              f"median {np.nanmedian(p):+6.1f} deg "
              f"[p25 {np.nanpercentile(p,25):+.1f}, p75 {np.nanpercentile(p,75):+.1f}] "
              f"miss {np.mean(t[key+'_miss'])*100:.0f}%")
    print("   If these land near GHUM's own pitch, the 2D head sees the same toe-down")
    print("   foot and the bias is the WHOLE MODEL, not the 3D fit alone.")

    print("\n   -- BUDGET: how much of the heel-above-toe drop does the 2D support? --")
    Lg = np.nanmedian(t["ghumlen"])
    drop_3d = Lg * np.sin(np.radians(-np.median(t["ghumpitch"])))
    drop_2d_g = Lg * np.sin(np.radians(-np.nanmedian(t["pitch2d_g"])))
    drop_2d_19 = 19.0 * np.sin(np.radians(-np.nanmedian(t["pitch2d_19"])))
    print(f"   GHUM 3D heel->toe drop                       {drop_3d:5.1f} cm "
          f"(at its own {Lg:.1f} cm foot)")
    print(f"   supported by the 2D at that same foot length {drop_2d_g:5.1f} cm")
    print(f"   supported by the 2D at an anatomical 19 cm   {drop_2d_19:5.1f} cm")
    print(f"   => the 3D head adds {drop_3d - drop_2d_g:4.1f} cm of pitch its OWN 2D does")
    print(f"      not support; the remaining {drop_2d_g:.1f} cm is what the image says,")
    print("      and section G tests whether THAT is real plantarflexion or the")
    print("      heel/toe landmarks simply not being at the sole.")
    return t


def analysis_G_2d_anthro(ds):
    """Landmark semantics, from 2D ONLY -- no GHUM 3D anywhere in this number.

    Pixels are converted to centimetres at the GT ankle's depth (the foot is
    within ~15 cm of the ankle in depth, so the scale is good to ~3%), and the
    image's v axis is world-vertical to within the camera tilt (0.2-3.6 deg).
    So this asks the question the pictures ask: in the image, where do the heel
    and toe MARKERS actually sit relative to the ankle? If the heel marker is
    barely behind and barely below the ankle, it is not on the back of the
    shoe, and a "flat foot" was never going to give heel-toe dy ~ 0.
    """
    print("\n" + "=" * 78)
    print("G. LANDMARK SEMANTICS FROM 2D ALONE (stance, cm at the GT ankle's depth)")
    print("=" * 78)
    acc = {k: [] for k in ("h_dy", "h_dx", "t_dy", "t_dx", "ht_dy", "ht_dx")}
    for d in ds:
        cam, f = d["cam"], d["f"]
        W, H = cam["W"], cam["H"]
        for side in "LR":
            ank, hee, foo, _k, coco = side_idx(side)
            m = frame_mask(d, side, "stance")
            if m.sum() < 8:
                continue
            obs = d["cache"]["img"][m] * np.array([W, H])
            obs = undistort(obs.reshape(-1, 2), cam).reshape(obs.shape)
            z = d["gt_dc3"][m][:, coco, 2]           # metres, GT depth
            s = (z / f)[:, None] * 100.0             # cm per px
            dh = (obs[:, hee] - obs[:, ank]) * s
            dt = (obs[:, foo] - obs[:, ank]) * s
            acc["h_dy"].append(-dh[:, 1])            # image v is DOWN -> negate
            acc["t_dy"].append(-dt[:, 1])
            acc["h_dx"].append(np.abs(dh[:, 0]))
            acc["t_dx"].append(np.abs(dt[:, 0]))
            d2 = (obs[:, foo] - obs[:, hee]) * s
            acc["ht_dy"].append(-d2[:, 1])
            acc["ht_dx"].append(np.abs(d2[:, 0]))
    a = {k: np.concatenate(v) for k, v in acc.items() if v}
    if not a:
        return
    print(f"   {'observed marker':34s} {'height':>8s} {'|horiz|':>8s} {'expected':>24s}")
    print(f"   {'heel marker vs ankle':34s} {np.median(a['h_dy']):+8.1f} "
          f"{np.median(a['h_dx']):8.1f} {'-5.5 cm / 4.5 cm behind':>24s}")
    print(f"   {'toe marker vs ankle':34s} {np.median(a['t_dy']):+8.1f} "
          f"{np.median(a['t_dx']):8.1f} {'-6.0 cm / 15.5 cm ahead':>24s}")
    print(f"   {'toe marker vs heel marker':34s} {np.median(a['ht_dy']):+8.1f} "
          f"{np.median(a['ht_dx']):8.1f} {'~0 cm / ~19 cm (flat)':>24s}")
    print("\n   The heel row is the tell: a marker that is only a couple of cm behind")
    print("   the ankle is on the ANKLE, not on the back of the sole -- so part of")
    print("   the heel-above-toe drop is landmark semantics and would survive any")
    print("   3D fix. The toe row says whether the toe marker itself is sane.")
    print("   CAUTION reading the height column: the cameras look DOWN at the feet by")
    print("   ~14 deg, so a landmark that is merely CLOSER to the camera also drops in")
    print("   the image. Section H removes that confound properly.")


def analysis_H_floor_ray(ds, heights=(0.0, 2.0, 4.0)):
    """Put the observed 2D toe ON THE FLOOR and see whether it lands somewhere a
    real toe could be. This is the confound-free version of section G.

    A monocular ray cannot separate "lower" from "nearer" when it grazes; the
    AIST++ cameras look down at the feet by only ~14 deg, so a toe that is 15 cm
    NEARER than the ankle drops ~3.6 cm-equivalent in the image with no change
    in height at all. The clean question is therefore not "how low is the toe
    pixel" but: if we assume the toe is on the floor (h cm above it) and let the
    2D ray choose the depth, does it land at an anatomically plausible distance
    from the GT ankle? ~13-17 cm = yes, a flat foot fully explains the image and
    the 3D fit merely resolved the depth/height ambiguity the wrong way.
    """
    print("\n" + "=" * 78)
    print("H. PUT THE OBSERVED 2D TOE ON THE FLOOR: where does it land? (stance)")
    print("=" * 78)
    print(f"   {'toe assumed h cm above floor':32s} {'horiz from GT ankle':>20s} "
          f"{'depth vs GHUM toe':>19s}")
    for h in heights:
        dist, dz = [], []
        for d in ds:
            cam, f = d["cam"], d["f"]
            W, H = cam["W"], cam["H"]
            C = dc3_to_world(np.zeros((1, 3)), cam)[0]        # camera centre, world
            for side in "LR":
                ank, hee, foo, _k, coco = side_idx(side)
                m = frame_mask(d, side, "stance")
                if m.sum() < 8:
                    continue
                obs = d["cache"]["img"][m] * np.array([W, H])
                obs = undistort(obs.reshape(-1, 2), cam).reshape(obs.shape)
                ray = unproject(obs[:, foo], np.ones(m.sum()), W, H, f)
                Pw = dc3_to_world(ray, cam)                    # a point on the ray
                dirw = Pw - C
                tgt = d["floor_y"] + h
                with np.errstate(divide="ignore", invalid="ignore"):
                    tt = (tgt - C[1]) / dirw[:, 1]
                hit = C + dirw * tt[:, None]
                gt_ank = d["gt_w"][m][:, coco]
                dist.append(np.linalg.norm(hit[:, [0, 2]] - gt_ank[:, [0, 2]], axis=1))
                # same point expressed as a camera depth, vs GHUM's toe depth
                hit_cam = (hit @ cam["R"].T + cam["t"])        # cm, CV axes
                dz.append(hit_cam[:, 2] - d["A"][m][:, foo, 2] * 100.0)
        if not dist:
            continue
        dist = np.concatenate(dist)
        dz = np.concatenate(dz)
        print(f"   {h:>6.0f}{'':26s} {np.nanmedian(dist):17.1f} cm "
              f"{np.nanmedian(dz):16.1f} cm")
    # And the mirror question for the heel: the heel marker sits almost directly
    # under the ankle (2-3 cm behind it), so fixing its depth at the GT ankle's
    # depth costs at most ~1 cm of height and gives a 3D-fit-free read of how
    # high the heel MARKER really is. This is the number that decides how much
    # of the heel-above-toe drop is legitimate landmark semantics.
    hh = []
    for d in ds:
        cam, f = d["cam"], d["f"]
        W, H = cam["W"], cam["H"]
        for side in "LR":
            ank, hee, foo, _k, coco = side_idx(side)
            m = frame_mask(d, side, "stance")
            if m.sum() < 8:
                continue
            obs = d["cache"]["img"][m] * np.array([W, H])
            obs = undistort(obs.reshape(-1, 2), cam).reshape(obs.shape)
            z = d["gt_dc3"][m][:, coco, 2]
            P = unproject(obs[:, hee], z, W, H, f)
            hh.append(dc3_to_world(P, cam)[:, 1] - d["floor_y"])
    if hh:
        hh = np.concatenate(hh)
        print(f"   heel MARKER height above floor, from 2D at the GT ankle's depth: "
              f"{np.nanmedian(hh):.1f} cm")
        print("   (a marker on the SOLE would read ~2-3 cm; higher means the marker is")
        print("    on the back of the ankle, and that part of the drop is not an error)")
    print("   expected horizontal reach for a real toe marker: ~13-17 cm from the ankle.")
    print("   A sane number here means the image is fully consistent with a FLAT foot")
    print("   on the floor, and the depth column is how far GHUM mis-placed it along")
    print("   the ray (negative = GHUM put the toe too FAR from the camera).")


# --------------------------------------------------------------------------
# C: decomposition against the GT ankle
# --------------------------------------------------------------------------
def analysis_C(ds):
    print("\n" + "=" * 78)
    print("C. DECOMPOSITION: heel/toe relative to our own ankle, gravity-aligned (cm)")
    print("=" * 78)
    print("   'up' = world +Y (heights), 'fwd' = horizontal distance from the ankle,")
    print("   signed + when it points the same way as the horizontal heel->toe axis.")
    print(f"   {'quantity':34s} {'measured':>10s} {'expected':>10s} {'delta':>8s}")
    acc = {k: [] for k in ("hee_dy", "hee_fwd", "toe_dy", "toe_fwd", "ank_err",
                           "shank_ang", "shank_pitch", "foot_pitch", "len")}
    for d in ds:
        for side in "LR":
            ank, hee, foo, kne, coco = side_idx(side)
            m = frame_mask(d, side, "stance")
            if m.sum() < 8:
                continue
            Aw, gtw = d["Aw"][m], d["gt_w"][m]
            up = np.array([0.0, 1.0, 0.0])
            a3, h3, t3, k3 = Aw[:, ank], Aw[:, hee], Aw[:, foo], Aw[:, kne]
            fdir = t3 - h3
            fh = fdir - np.outer(fdir[:, 1], up)
            fhn = np.linalg.norm(fh, axis=1)
            u = fh / np.maximum(fhn, 1e-6)[:, None]
            acc["hee_dy"].append(h3[:, 1] - a3[:, 1])
            acc["toe_dy"].append(t3[:, 1] - a3[:, 1])
            acc["hee_fwd"].append(np.einsum("ij,ij->i", h3 - a3, u))
            acc["toe_fwd"].append(np.einsum("ij,ij->i", t3 - a3, u))
            acc["ank_err"].append(a3[:, 1] - gtw[:, coco, 1])
            shank = a3 - k3
            acc["shank_ang"].append(np.degrees(np.arccos(np.clip(
                np.einsum("ij,ij->i", shank, fdir)
                / (np.linalg.norm(shank, axis=1) * np.linalg.norm(fdir, axis=1)),
                -1, 1))))
            acc["shank_pitch"].append(signed_pitch_deg(shank, up))
            acc["foot_pitch"].append(signed_pitch_deg(fdir, up))
            acc["len"].append(np.linalg.norm(fdir, axis=1))
    a = {k: np.concatenate(v) for k, v in acc.items() if v}
    if not a:
        return None

    def row(label, vals, exp):
        med = np.median(vals)
        print(f"   {label:34s} {med:+10.1f} {exp:+10.1f} {med - exp:+8.1f}")

    row("heel height rel. ankle", a["hee_dy"], -EXP_HEEL_BELOW_ANKLE_CM)
    row("heel fwd  rel. ankle", a["hee_fwd"], -EXP_HEEL_BEHIND_ANKLE_CM)
    row("toe  height rel. ankle", a["toe_dy"], -EXP_TOE_BELOW_ANKLE_CM)
    row("toe  fwd  rel. ankle", a["toe_fwd"], EXP_TOE_AHEAD_ANKLE_CM)
    row("heel->toe length", a["len"], 19.0)
    print(f"   {'our ankle height error vs GT':34s} "
          f"{np.median(a['ank_err']):+10.1f} {0.0:+10.1f} "
          f"{np.median(a['ank_err']):+8.1f}")
    print(f"\n   foot pitch (stance)   median {np.median(a['foot_pitch']):+.1f} deg "
          f"[p10 {np.percentile(a['foot_pitch'],10):+.1f}, "
          f"p90 {np.percentile(a['foot_pitch'],90):+.1f}]  (0 = flat)")
    print(f"   shank pitch (stance)  median {np.median(a['shank_pitch']):+.1f} deg "
          f"(90 = knee straight above ankle)")
    print(f"   shank^foot angle      median {np.median(a['shank_ang']):.1f} deg, "
          f"std {a['shank_ang'].std():.1f} deg  (a rigid, un-articulated ankle")
    print("                          would show a near-constant angle here)")
    print(f"\n   VERDICT INPUTS: toe sits {np.median(a['toe_dy']) + EXP_TOE_BELOW_ANKLE_CM:+.1f} cm")
    print(f"   lower than anthropometry and {np.median(a['toe_fwd']) - EXP_TOE_AHEAD_ANKLE_CM:+.1f} cm")
    print(f"   short of the expected forward reach; heel is "
          f"{np.median(a['hee_dy']) + EXP_HEEL_BELOW_ANKLE_CM:+.1f} cm off in height.")
    return a


def analysis_C_depth(ds):
    """Is the toe too LOW (y) or too CLOSE (z)? Ask the 2D."""
    print("\n   -- y-vs-z decomposition from the 2D ray (stance, cm) --")
    print("   For each frame, walk the camera ray through the OBSERVED 2D toe and")
    print("   find the depth that makes the foot flat. Comparing that depth (and the")
    print("   resulting 3D point) with GHUM's toe separates a depth error from a")
    print("   height error: if only the depth differs, the toe is mis-placed ALONG")
    print("   the ray; if the ray itself misses, the 2D and 3D heads disagree.")
    acc = {k: [] for k in ("dz", "dy", "dperp", "ghum_z", "flat_z")}
    for d in ds:
        cam, f = d["cam"], d["f"]
        W, H = cam["W"], cam["H"]
        up = d["up_dc3"]
        for side in "LR":
            ank, hee, foo, _k, _c = side_idx(side)
            m = frame_mask(d, side, "stance")
            if m.sum() < 8:
                continue
            A = d["A"][m]
            obs = d["cache"]["img"][m] * np.array([W, H])
            obs_u = undistort(obs.reshape(-1, 2), cam).reshape(obs.shape)
            heel3, toe3 = A[:, hee], A[:, foo]
            dirv = unproject(obs_u[:, foo], np.ones(len(A)), W, H, f)
            denom = dirv @ up
            zz = (heel3 @ up) / np.where(np.abs(denom) < 1e-6, np.nan, denom)
            hit = dirv * zz[:, None]
            acc["dz"].append((hit[:, 2] - toe3[:, 2]) * 100)
            acc["ghum_z"].append(toe3[:, 2] * 100)
            acc["flat_z"].append(hit[:, 2] * 100)
            # vertical (world-up) difference and residual perpendicular distance
            diff = hit - toe3
            acc["dy"].append((diff @ up) * 100)
            acc["dperp"].append(np.linalg.norm(
                diff - np.outer(diff @ up, up), axis=1) * 100)
    a = {k: np.concatenate(v) for k, v in acc.items() if v}
    if not a:
        return
    print(f"   depth shift needed to flatten the foot   "
          f"median {np.nanmedian(a['dz']):+7.1f} cm "
          f"(|.| p90 {np.nanpercentile(np.abs(a['dz']),90):5.1f})")
    print(f"   vertical rise from GHUM toe to flat toe  "
          f"median {np.nanmedian(a['dy']):+7.1f} cm")
    print(f"   horizontal residual (ray vs GHUM toe)    "
          f"median {np.nanmedian(a['dperp']):+7.1f} cm")
    print(f"   GHUM toe depth {np.nanmedian(a['ghum_z']):.0f} cm vs flat-toe depth "
          f"{np.nanmedian(a['flat_z']):.0f} cm")


# --------------------------------------------------------------------------
# D: candidate fixes
# --------------------------------------------------------------------------
def candidates(heel, toe, ank, up, obs_toe_uv, W, H, f, theta):
    """Every candidate's (foot_joint, corrected_toe) in DC3 camera metres.

    `up` is whatever up-vector the caller supplies -- production would pass
    +Y_cam (camera assumed level); the analysis also runs the true floor normal
    to bound the cost of that assumption.
    """
    b = FOOT_TOE_BLEND
    v = toe - heel
    out = {}
    out["C0 baseline blend 0.35"] = (heel * (1 - b) + toe * b, toe)
    out["C1 blend 0.00 (heel only)"] = (heel.copy(), toe)
    out["C2 blend 0.15"] = (heel * 0.85 + toe * 0.15, toe)
    flat_toe = heel + flatten_vec(v, up)
    out["C3 blend 0.35 on FLAT toe"] = (heel * (1 - b) + flat_toe * b, flat_toe)
    for th in theta:
        rot_toe = heel + rotate_toward_horizontal(v, up, th)
        out[f"C4 pitch-correct +{th:.0f} deg"] = (heel * (1 - b) + rot_toe * b, rot_toe)
    # C5: keep GHUM's toe DEPTH, take the toe's image position from the 2D head.
    z = toe[:, 2]
    twod_toe = unproject(obs_toe_uv, z, W, H, f)
    out["C5 2D toe @ GHUM depth"] = (heel * (1 - b) + twod_toe * b, twod_toe)
    return out


def analysis_D(ds, theta):
    print("\n" + "=" * 78)
    print("D. CANDIDATE FIXES -- evaluated, NOT shipped")
    print("=" * 78)
    print("   All candidates use up := +Y of camera space (the only reference a live")
    print("   webcam has). C3 is listed for reference only: it needs a true floor")
    print("   normal in the general case and is NOT implementable as written.")
    print("   STANCE (want: flat foot, toe above floor, FootL/R near ~3-4 cm):")
    print(f"   {'candidate':30s} {'pitch':>7s} {'toe hgt':>8s} {'below':>6s} "
          f"{'FootLR':>7s} {'dFoot':>7s}")

    def collect(kind, up_key):
        acc = {}
        for d in ds:
            cam, f = d["cam"], d["f"]
            W, H = cam["W"], cam["H"]
            up = d[up_key]
            for side in "LR":
                ank, hee, foo, _k, _c = side_idx(side)
                m = frame_mask(d, side, kind)
                if m.sum() < 8:
                    continue
                A = d["A"][m]
                obs = d["cache"]["img"][m] * np.array([W, H])
                obs_u = undistort(obs.reshape(-1, 2), cam).reshape(obs.shape)
                heel, toe, ankp = A[:, hee], A[:, foo], A[:, ank]
                cands = candidates(heel, toe, ankp, up, obs_u[:, foo], W, H, f, theta)
                base = cands["C0 baseline blend 0.35"][0]
                for name, (fj, ct) in cands.items():
                    fw = dc3_to_world(fj, cam)
                    tw = dc3_to_world(ct, cam)
                    e = acc.setdefault(name, {k: [] for k in
                                              ("pitch", "toeh", "footh", "dfoot")})
                    e["pitch"].append(signed_pitch_deg(ct - heel, d["up_dc3"]))
                    e["toeh"].append(tw[:, 1] - d["floor_y"])
                    e["footh"].append(fw[:, 1] - d["floor_y"])
                    e["dfoot"].append(np.linalg.norm(fj - base, axis=1) * 100)
        return {n: {k: np.concatenate(v) for k, v in e.items()}
                for n, e in acc.items()}

    st = collect("stance", "up_proxy")
    for name, e in st.items():
        print(f"   {name:30s} {np.nanmedian(e['pitch']):+7.1f} "
              f"{np.nanmedian(e['toeh']):8.1f} "
              f"{np.nanmean(e['toeh']<0)*100:5.0f}% "
              f"{np.nanmedian(e['footh']):7.1f} {np.nanmedian(e['dfoot']):7.1f}")
    print("   pitch = heel->toe pitch, deg (0 = flat); toe hgt / FootLR = cm above")
    print("   floor; below = % of stance frames with the toe under the floor;")
    print("   dFoot = how far this moves FootL/R from the shipping baseline (cm).")

    sw = collect("swing", "up_proxy")
    print(f"\n   SWING (GT ankle > {SWING_VMIN:.1f} m/s -- real plantarflexion MUST survive):")
    print(f"   {'candidate':30s} {'pitch p5':>9s} {'pitch med':>10s} "
          f"{'pitch p95':>10s} {'toe-dn>20':>10s} {'dFoot':>7s}")
    for name, e in sw.items():
        p = e["pitch"]
        print(f"   {name:30s} {np.nanpercentile(p,5):+9.1f} {np.nanmedian(p):+10.1f} "
              f"{np.nanpercentile(p,95):+10.1f} "
              f"{np.nanmean(p < -20)*100:9.0f}% {np.nanmedian(e['dfoot']):7.1f}")
    print("   toe-dn>20 = share of swing frames still pitched >20 deg toe-down after")
    print("   the correction. A fix that zeroes this has flattened real motion.")

    # cost of the camera-is-level assumption
    st_true = collect("stance", "up_dc3")
    print("\n   COST OF up := +Y_cam vs the TRUE floor normal (stance, cm of FootL/R):")
    for name in st:
        if name in st_true:
            a, b = st[name]["footh"], st_true[name]["footh"]
            n = min(len(a), len(b))
            print(f"   {name:30s} {np.nanmedian(a[:n]-b[:n]):+7.2f} cm")
    print(f"   (AIST++ cameras are only {min(d['cam_tilt'] for d in ds):.1f}-"
          f"{max(d['cam_tilt'] for d in ds):.1f} deg off level, so this is a LOWER")
    print("   BOUND on what a tilted living-room webcam would cost.)")
    return st, sw


# --------------------------------------------------------------------------
def analysis_pitch_regimes(ds):
    """Is the pitch a constant offset or does it track real foot motion?"""
    print("\n" + "=" * 78)
    print("E. IS THE PITCH A CONSTANT OFFSET? (signed pitch, deg; 0 = flat)")
    print("=" * 78)
    print(f"   {'sequence':28s} {'regime':8s} {'p5':>7s} {'p25':>7s} {'med':>7s} "
          f"{'p75':>7s} {'p95':>7s} {'>=0':>6s}   n")
    for d in ds:
        for kind in ("stance", "swing", "all"):
            vals = []
            for side in "LR":
                ank, hee, foo, _k, _c = side_idx(side)
                m = frame_mask(d, side, kind)
                if m.sum() < 8:
                    continue
                A = d["A"][m]
                vals.append(signed_pitch_deg(A[:, foo] - A[:, hee], d["up_dc3"]))
            if not vals:
                continue
            p = np.concatenate(vals)
            print(f"   {d['seq'][:28] if kind=='stance' else '':28s} {kind:8s} "
                  f"{np.percentile(p,5):+7.1f} {np.percentile(p,25):+7.1f} "
                  f"{np.median(p):+7.1f} {np.percentile(p,75):+7.1f} "
                  f"{np.percentile(p,95):+7.1f} {np.mean(p>=0)*100:5.0f}% {len(p):5d}")
    print("\n   If even the p95 of the ALL regime is far below 0, no frame in the whole")
    print("   corpus shows a flat foot -- which no real dance sequence can satisfy,")
    print("   and the pitch is therefore an additive model bias, not perception.")


def analysis_rigidity(ds):
    """Does GHUM articulate the ankle at all, or is the foot welded to the shank?

    If the foot were rigid to the shank, foot pitch would be shank pitch plus a
    constant: slope ~1 against a tight residual. A real ankle swings +-30-40 deg
    independently, so a rigid fit would mean the ~30 deg is a REST-POSE constant
    of the model's foot rather than anything measured from the image.
    """
    print("\n" + "=" * 78)
    print("F. IS THE FOOT ARTICULATED, OR WELDED TO THE SHANK? (all frames)")
    print("=" * 78)
    fp, sp, ang = [], [], []
    for d in ds:
        for side in "LR":
            ank, hee, foo, kne, _c = side_idx(side)
            m = frame_mask(d, side, "all")
            if m.sum() < 8:
                continue
            Aw = d["Aw"][m]
            up = np.array([0.0, 1.0, 0.0])
            fv = Aw[:, foo] - Aw[:, hee]
            sv = Aw[:, ank] - Aw[:, kne]
            fp.append(signed_pitch_deg(fv, up))
            sp.append(signed_pitch_deg(sv, up))
            ang.append(np.degrees(np.arccos(np.clip(
                np.einsum("ij,ij->i", sv, fv)
                / (np.linalg.norm(sv, axis=1) * np.linalg.norm(fv, axis=1)), -1, 1))))
    fp, sp, ang = np.concatenate(fp), np.concatenate(sp), np.concatenate(ang)
    slope, inter = np.polyfit(sp, fp, 1)
    resid = fp - (slope * sp + inter)
    print(f"   foot pitch = {slope:.2f} * shank pitch {inter:+.1f} deg, "
          f"corr {np.corrcoef(sp, fp)[0,1]:+.2f}, residual std {resid.std():.1f} deg")
    print(f"   shank^foot angle: median {np.median(ang):.1f} deg, std {ang.std():.1f}, "
          f"p5 {np.percentile(ang,5):.1f}, p95 {np.percentile(ang,95):.1f}  "
          f"(n={len(ang)})")
    print(f"   foot pitch spread: std {fp.std():.1f} deg, "
          f"p5 {np.percentile(fp,5):+.1f}, p95 {np.percentile(fp,95):+.1f}")
    print("   A near-1 slope with a small residual = the ankle is not being")
    print("   articulated and the pitch is a model constant, not an observation.")


def dump_frames(d, n, outdir):
    """Save annotated foot crops so the toe-down claim can be checked by eye.

    Draws the model's OWN observed 2D landmarks (ankle/heel/toe) plus the
    reprojection of the GHUM 3D toe and of the flat-foot toe, on real stance
    frames. If the dancer is visibly flat-footed while the toe marker sits low,
    the landmark semantics are the story; if the dancer is on the ball of the
    foot, the model is right and the anatomy assumption is wrong.
    """
    os.makedirs(outdir, exist_ok=True)
    cam, f = d["cam"], d["f"]
    W, H = cam["W"], cam["H"]
    vid = os.path.join(os.path.dirname(os.path.dirname(d["cam_path"])), "videos",
                       f"{d['seq']}.mp4") if "cam_path" in d else None
    vid = vid or os.path.join("/home/free/tmp/pose_gt", "videos", f"{d['seq']}.mp4")
    cap = cv2.VideoCapture(vid)
    m = frame_mask(d, "L", "stance") & frame_mask(d, "R", "stance")
    idx = np.flatnonzero(m)
    if len(idx) == 0:
        idx = np.flatnonzero(frame_mask(d, "L", "stance"))
    idx = idx[np.linspace(0, len(idx) - 1, min(n, len(idx))).astype(int)]
    for i in idx:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(i))
        ok, frame = cap.read()
        if not ok:
            continue
        pts = []
        for side in "LR":
            ank, hee, foo, _k, _c = side_idx(side)
            obs = d["cache"]["img"][i] * np.array([W, H])
            A = d["A"][i]
            flat = A[hee] + flatten_vec(A[foo] - A[hee], d["up_dc3"])[0]
            for lab, p, col in (("ank", obs[ank], (0, 255, 255)),
                                ("heel", obs[hee], (0, 255, 0)),
                                ("toe", obs[foo], (0, 0, 255))):
                cv2.circle(frame, tuple(np.int32(p)), 5, col, -1)
                cv2.putText(frame, lab, tuple(np.int32(p) + 6),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.4, col, 1)
                pts.append(p)
            pf = project(flat[None], W, H, f)[0]
            cv2.drawMarker(frame, tuple(np.int32(pf)), (255, 0, 255),
                           cv2.MARKER_TILTED_CROSS, 12, 2)
            pts.append(pf)
        P = np.array(pts)
        x0, y0 = np.int32(P.min(0)) - 90
        x1, y1 = np.int32(P.max(0)) + 90
        crop = frame[max(y0, 0):min(y1, H), max(x0, 0):min(x1, W)]
        if crop.size:
            cv2.imwrite(os.path.join(outdir, f"{d['seq']}_f{i:05d}.png"), crop)
    cap.release()
    print(f"   wrote {len(idx)} annotated crops to {outdir} "
          f"(green=observed heel, red=observed toe, magenta x=flat-foot toe)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gt-dir", default="/home/free/tmp/pose_gt")
    ap.add_argument("--cache-dir", default=None)
    ap.add_argument("--seqs", nargs="*", default=None)
    ap.add_argument("--theta", type=float, nargs="*", default=[10.0, 15.0, 20.0, 30.0],
                    help="pitch-correction angles swept for candidate C4 (deg)")
    ap.add_argument("--dump-frames", type=int, default=0,
                    help="save N annotated stance-frame foot crops per sequence")
    ap.add_argument("--dump-dir", default="/tmp/foot_probe")
    args = ap.parse_args()
    cache_dir = args.cache_dir or os.path.join(args.gt_dir, "cache")
    seqs = args.seqs or sorted(os.path.splitext(f)[0]
                               for f in os.listdir(cache_dir) if f.endswith(".npz"))
    ds = []
    for s in seqs:
        d = build(s, args.gt_dir, cache_dir)
        if d is None:
            print(f"  skip {s}: too few stance frames")
            continue
        ds.append(d)
        print(f"  loaded {s}  ({len(d['cache']['det'])} frames, "
              f"floor y={d['floor_y']:.1f} cm, cam tilt {d['cam_tilt']:.1f} deg)")
    if not ds:
        sys.exit("no sequences")
    if args.dump_frames:
        for d in ds:
            dump_frames(d, args.dump_frames, args.dump_dir)
        return
    analysis_A(ds)
    analysis_B(ds)
    analysis_C(ds)
    analysis_C_depth(ds)
    analysis_G_2d_anthro(ds)
    analysis_H_floor_ray(ds)
    analysis_pitch_regimes(ds)
    analysis_rigidity(ds)
    analysis_D(ds, args.theta)


if __name__ == "__main__":
    main()
