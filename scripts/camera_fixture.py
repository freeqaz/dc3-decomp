#!/usr/bin/env python3
"""camera_fixture.py -- a deterministic camera picture + person mask from skeletons.

The native port's player view (PLAN P-4) is drawn from the webcam picture and a
person-segmentation mask (pose_server.py protocol v3, --frames).  Tests and
headless runs have no webcam, so this renders the same two images from the DC3
camera-space joints a synthetic performer already sends: each person is a set
of capsules (limbs, torso) and a disc (head), projected with the Kinect's own
depth-camera model (u = 160 + 285.63 x/z, v = 120 - 285.63 y'/z over 320x240,
scaled to the requested size; the same projection Skeleton_Native.h documents
and pose_mediapipe.py's default focal length uses).

y' = y - camera_height, per person.  A real Kinect (and pose_server.py)
reports y relative to the sensor, about hip-high, and so does the choreography
/api/pose/target hands the performing sensor (hips at y ~ 0, feet ~ -1):
camera_height 0 for those.  synthetic_kinect.py's own STAND / raise poses are
floor-based (feet at y = 0), so it passes DEFAULT_CAMERA_HEIGHT (0.9 m) for
them, which puts that body in frame the way a camera would see it.  Only the
picture is shifted; the joints the game receives are what they always were,
so for a standing synthetic person the game's own ScreenPos (e.g. where
CursorPanel puts the crown) sits ~0.9 m above the picture's head.

Picture: background = fixed vertical stripes (two greys), person k a solid
colour (PERSON_RGB[k-1]); mask: 0 nobody, k = k-th person.  Pure function of
the joints and the size: byte-identical on every run.

Library use: camera_block(persons, w, h) -> (rgb_bytes, mask_bytes, w, h), the
tuple pose_server.pack_frame takes.  CLI: write one frame as PNGs for a look.
"""

import numpy as np

KINECT_FOCAL_320 = 285.63
DEFAULT_CAMERA_HEIGHT = 0.9

# BaseSkeleton.h SkeletonJoint order (same as synthetic_kinect.py).
(HIP_C, SPINE, SHOULDER_C, HEAD, SHOULDER_L, ELBOW_L, WRIST_L, HAND_L,
 SHOULDER_R, ELBOW_R, WRIST_R, HAND_R, HIP_L, KNEE_L, ANKLE_L,
 HIP_R, KNEE_R, ANKLE_R, FOOT_L, FOOT_R) = range(20)

# (a, b, radius m)
BONES = [
    (HIP_C, SPINE, 0.15), (SPINE, SHOULDER_C, 0.16),
    (SHOULDER_C, SHOULDER_L, 0.07), (SHOULDER_L, ELBOW_L, 0.055), (ELBOW_L, WRIST_L, 0.045),
    (WRIST_L, HAND_L, 0.05),
    (SHOULDER_C, SHOULDER_R, 0.07), (SHOULDER_R, ELBOW_R, 0.055), (ELBOW_R, WRIST_R, 0.045),
    (WRIST_R, HAND_R, 0.05),
    (HIP_C, HIP_L, 0.10), (HIP_L, KNEE_L, 0.075), (KNEE_L, ANKLE_L, 0.06), (ANKLE_L, FOOT_L, 0.05),
    (HIP_C, HIP_R, 0.10), (HIP_R, KNEE_R, 0.075), (KNEE_R, ANKLE_R, 0.06), (ANKLE_R, FOOT_R, 0.05),
    (SHOULDER_C, HEAD, 0.06),
]
HEAD_RADIUS = 0.12

BACKGROUND_RGB = ((70, 70, 80), (110, 110, 120))
STRIPE_PX_AT_320 = 16
PERSON_RGB = ((230, 120, 30), (40, 190, 90), (60, 120, 230),
              (220, 60, 160), (230, 210, 40), (40, 200, 210))


def project(joints, w, h, camera_height=DEFAULT_CAMERA_HEIGHT):
    """[(x, y, z, ...)] camera metres -> (N, 2) pixel coords and the pixel
    scale (px per metre) at each joint's depth."""
    f = KINECT_FOCAL_320 * w / 320.0
    pts = np.array([(j[0], j[1] - camera_height, max(j[2], 0.3)) for j in joints], np.float64)
    u = w / 2.0 + f * pts[:, 0] / pts[:, 2]
    # square pixels: the vertical scale is the horizontal one
    v = h / 2.0 - f * pts[:, 1] / pts[:, 2]
    return np.stack([u, v], axis=1), f / pts[:, 2]


def person_coverage(joints, w, h, camera_height=DEFAULT_CAMERA_HEIGHT):
    """Boolean (h, w): pixels the person covers."""
    uv, scale = project(joints, w, h, camera_height)
    ys, xs = np.mgrid[0:h, 0:w]
    px = xs + 0.5
    py = ys + 0.5
    cover = np.zeros((h, w), bool)
    for a, b, r in BONES:
        ax, ay = uv[a]
        bx, by = uv[b]
        rad = r * 0.5 * (scale[a] + scale[b])
        dx, dy = bx - ax, by - ay
        l2 = dx * dx + dy * dy
        if l2 < 1e-9:
            t = np.zeros_like(px)
        else:
            t = np.clip(((px - ax) * dx + (py - ay) * dy) / l2, 0.0, 1.0)
        qx = ax + t * dx - px
        qy = ay + t * dy - py
        cover |= (qx * qx + qy * qy) <= rad * rad
    hx, hy = uv[HEAD]
    hr = HEAD_RADIUS * scale[HEAD]
    cover |= ((px - hx) ** 2 + (py - hy) ** 2) <= hr * hr
    return cover


def render(persons, w, h, camera_height=DEFAULT_CAMERA_HEIGHT):
    """persons: list of joint lists (ordinal = index + 1); camera_height one
    value or one per person.  Returns (rgb (h,w,3)
    uint8, mask (h,w) uint8). Nearer people (smaller hip z) draw over farther."""
    stripe = max(1, STRIPE_PX_AT_320 * w // 320)
    xs = np.arange(w)
    rgb = np.empty((h, w, 3), np.uint8)
    rgb[:, :] = np.where(((xs // stripe) % 2 == 0)[:, None],
                         np.array(BACKGROUND_RGB[0], np.uint8),
                         np.array(BACKGROUND_RGB[1], np.uint8))[None, :, :]
    mask = np.zeros((h, w), np.uint8)
    heights = camera_height if isinstance(camera_height, (list, tuple)) \
        else [camera_height] * len(persons)
    order = sorted(range(len(persons)), key=lambda i: -persons[i][HIP_C][2])
    for i in order:
        cover = person_coverage(persons[i], w, h, heights[i])
        mask[cover] = i + 1
        rgb[cover] = PERSON_RGB[i % len(PERSON_RGB)]
    return rgb, mask


def camera_block(persons, w, h, camera_height=DEFAULT_CAMERA_HEIGHT):
    """The (rgb_bytes, mask_bytes, w, h) tuple pose_server.pack_frame takes."""
    rgb, mask = render(persons, w, h, camera_height)
    return rgb.tobytes(), mask.tobytes(), w, h


def main():
    import argparse
    import sys
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import synthetic_kinect as sk
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--size", default="320x240")
    ap.add_argument("--people", type=int, default=1)
    ap.add_argument("--out", required=True, help="output prefix; writes <out>_rgb.png, <out>_mask.png")
    a = ap.parse_args()
    w, h = (int(v) for v in a.size.split("x"))
    k = sk.SyntheticKinect(None, people=a.people)
    persons = [k._joints(i, "stand") for i in range(a.people)]
    rgb, mask = render(persons, w, h, DEFAULT_CAMERA_HEIGHT)   # STAND is floor-based
    import cv2
    cv2.imwrite(a.out + "_rgb.png", cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR))
    cv2.imwrite(a.out + "_mask.png", (mask * (255 // max(1, a.people))).astype(np.uint8))
    print(f"wrote {a.out}_rgb.png {a.out}_mask.png; person pixels {(mask > 0).sum()}")


if __name__ == "__main__":
    main()
