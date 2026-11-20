#!/usr/bin/env python3
"""pose_mask_quality.py -- how good is the player's silhouette mask? (PLAN P-20)

The silhouette in the menu player boxes and the gameplay box is drawn from the
person mask `native/scripts/pose_server.py camera_block()` puts in the protocol
v3 camera block (docs/native/POSE_INPUT.md, "Player on screen (P-4)"). This
runs the server's own backend and its own `camera_block` over recorded footage,
offline, and reports per clip and per mask variant:

  flicker    of consecutive frame pairs in which no tracked joint moved more
             than --still-px (1 px at 320 wide), the pixels of a person's mask
             that changed, as a share of the mask's area. `n` = such pairs.
             `change` is the same over every pair (real motion included).
  coverage   of the joints MediaPipe reports as tracked (visibility >= 0.5 and
             inside the picture): head (nose), elbows, wrists, knees, ankles --
             the share whose pixel is inside that person's mask.
  holes      background pixels the mask encloses (not 4-connected to the
             picture's border), per frame and as a share of the mask; `small`
             counts only holes of <= 30 px (at 320x240), which a body rarely
             makes on purpose (an arm akimbo makes a large one).
  fragments  connected pieces of a person's mask beyond the largest, per
             frame, and their pixels as a share of the mask.
  ghost      mask pixels where this frame's own soft mask says "certainly
             background" (< 0.1): what a temporal filter or a limb fill adds
             that the model does not see. 0 for today's mask by definition; a
             cost to watch, not an error by itself (a filled forearm the model
             dropped counts here too).
  ms         `camera_block` per frame, median and p95, on this box.

Sources: --video clips (the np-video-calib footage and the rendered-dancer
fixture `archive/screenshots-old/woot/taye01_stand_dance.mp4`; the docs name
them), from --start for --seconds. --every N hands the tracker every N-th
frame (2 = the 15 poses/s of a hot laptop). --still holds the frame at --start
and adds seeded sensor noise, so every pair is a still pair: the flicker of a
person standing motionless. --gain / --noise darken and add noise in motion
too (a stand-in for a dim room on a laptop camera; synthetic, say so).

Needs the pose environment (mediapipe, cv2, numpy), i.e. the playtest venv:
  .venv/bin/python scripts/pose_mask_quality.py --video clip.mp4 --start 60 --seconds 30
  ... --variants raw,default            compare mask variants in one pass
  ... --dump DIR --dump-at 30,200       pictures + .npz of those frames
  ... --hold DIR/clip_f0030.npz --variant raw --socket /tmp/s.sock
        serve that one frame, held, as protocol v3 (for a screenshot of the
        game drawing exactly that mask)
"""

import argparse
import json
import os
import socket
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))

GROUPS = {"head": (0,), "elbows": (13, 14), "wrists": (15, 16),
          "knees": (25, 26), "ankles": (27, 28)}
MOTION_JOINTS = (0, 11, 12, 13, 14, 15, 16, 23, 24, 25, 26, 27, 28)
TRACKED_VIS = 0.5
SMALL_HOLE_PX = 30       # at 320x240; scaled by area for other sizes
GHOST_BELOW = 0.1

# name -> keyword arguments of pose_server.camera_block beyond today's.
VARIANTS = {
    "raw": {},
}


def tracked(xy, vis, j):
    return vis[j] >= TRACKED_VIS and 0.0 <= xy[j][0] < 1.0 and 0.0 <= xy[j][1] < 1.0


class Meter:
    """Accumulates the measures over one clip for one variant."""

    def __init__(self, np, cv2, still_px):
        self.np, self.cv2, self.still_px = np, cv2, still_px
        self.frames = self.person_frames = self.no_mask = 0
        self.area = 0
        self.still, self.change = [], []
        self.cov = {g: [0, 0] for g in GROUPS}      # inside, tracked
        self.holes = self.hole_px = self.small_px = self.hole_frames = 0
        self.frags = self.frag_px = self.frag_frames = 0
        self.ghost_px = 0
        self.ms = []
        self.prev = {}

    def add(self, ordinal, ids, landmarks, soft):
        np, cv2 = self.np, self.cv2
        h, w = ordinal.shape
        scale = (w * h) / (320.0 * 240.0)
        self.frames += 1
        if not ids:
            self.prev = {}
            return
        bg = (ordinal == 0).astype(np.uint8)
        n, lab, stats, _ = cv2.connectedComponentsWithStats(bg, connectivity=4)
        outside = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]])).tolist())
        holes = 0
        for c in range(1, n):
            if c in outside:
                continue
            a = int(stats[c, cv2.CC_STAT_AREA])
            holes += 1
            self.hole_px += a
            if a <= SMALL_HOLE_PX * scale:
                self.small_px += a
        self.holes += holes
        self.hole_frames += holes > 0

        now = {}
        for k, tid in enumerate(ids, start=1):
            self.person_frames += 1
            b = ordinal == k
            area = int(b.sum())
            self.area += area
            xy, vis = landmarks[k - 1]
            for g, idxs in GROUPS.items():
                for j in idxs:
                    if tracked(xy, vis, j):
                        self.cov[g][1] += 1
                        self.cov[g][0] += bool(b[int(xy[j][1] * h), int(xy[j][0] * w)])
            if area == 0:
                self.no_mask += 1
            else:
                n, lab, stats, _ = cv2.connectedComponentsWithStats(b.astype(np.uint8), connectivity=8)
                areas = stats[1:, cv2.CC_STAT_AREA]
                self.frags += n - 2
                self.frag_px += area - int(areas.max())
                self.frag_frames += n > 2
                m = soft[k - 1]
                if m is not None:
                    m = cv2.resize(m, (w, h), interpolation=cv2.INTER_LINEAR)
                    self.ghost_px += int((b & (m < GHOST_BELOW)).sum())
            was = self.prev.get(tid)
            if was is not None and area and was[3]:
                pb, pxy, pvis, parea = was
                both = [j for j in MOTION_JOINTS if tracked(xy, vis, j) and tracked(pxy, pvis, j)]
                changed = float((b ^ pb).sum()) / (0.5 * (area + parea))
                self.change.append(changed)
                if len(both) >= 8:
                    d = (xy[both] - pxy[both]) * (w, h)
                    if float(np.abs(d).max()) < self.still_px * (w / 320.0):
                        self.still.append(changed)
            now[tid] = (b, xy, vis, area)
        self.prev = now

    def report(self):
        np = self.np
        area = max(self.area, 1)
        pf = max(self.person_frames, 1)
        cov = {g: (100.0 * a / t if t else None, t) for g, (a, t) in self.cov.items()}
        ins = sum(a for a, _ in self.cov.values())
        tot = sum(t for _, t in self.cov.values())
        ms = sorted(self.ms) or [0.0]
        return {
            "frames": self.frames, "person_frames": self.person_frames,
            "no_mask_frames": self.no_mask,
            "flicker_still_pct": 100.0 * float(np.mean(self.still)) if self.still else None,
            "flicker_still_p95_pct": 100.0 * float(np.percentile(self.still, 95)) if self.still else None,
            "still_pairs": len(self.still),
            "change_all_pct": 100.0 * float(np.mean(self.change)) if self.change else None,
            "pairs": len(self.change),
            "coverage_pct": 100.0 * ins / tot if tot else None, "coverage_joints": tot,
            "coverage": cov,
            "holes_per_frame": self.holes / pf, "hole_frames_pct": 100.0 * self.hole_frames / pf,
            "hole_px_per_frame": self.hole_px / pf, "hole_pct_of_mask": 100.0 * self.hole_px / area,
            "small_hole_px_per_frame": self.small_px / pf,
            "fragments_per_frame": self.frags / pf, "fragment_frames_pct": 100.0 * self.frag_frames / pf,
            "fragment_pct_of_mask": 100.0 * self.frag_px / area,
            "ghost_pct_of_mask": 100.0 * self.ghost_px / area,
            "mask_px_per_person": self.area / pf,
            "ms_median": ms[len(ms) // 2], "ms_p95": ms[min(len(ms) - 1, int(len(ms) * 0.95))],
        }


def overlay(np, cv2, rgb, ordinal, landmarks, label):
    """One 2x panel: the mask in white over the dimmed picture, the measured
    joints as dots (green inside the mask, red outside)."""
    h, w = ordinal.shape
    img = (rgb[:, :, ::-1] * 0.35).astype(np.uint8)
    img[ordinal > 0] = (235, 235, 235)
    img[ordinal > 1] = (235, 200, 120)
    img = cv2.resize(img, (w * 2, h * 2), interpolation=cv2.INTER_NEAREST)
    for k, (xy, vis) in enumerate(landmarks, start=1):
        for idxs in GROUPS.values():
            for j in idxs:
                if tracked(xy, vis, j):
                    inside = ordinal[int(xy[j][1] * h), int(xy[j][0] * w)] == k
                    cv2.circle(img, (int(xy[j][0] * w * 2), int(xy[j][1] * h * 2)), 3,
                               (0, 200, 0) if inside else (0, 0, 255), -1)
    cv2.putText(img, label, (6, 16), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1, cv2.LINE_AA)
    return img


def run_clip(path, args, names):
    import cv2
    import numpy as np
    import pose_server as ps
    from pose_mediapipe import MediaPipeBackend

    size = ps.parse_frames(args.frames)
    server_args = ps.parse_args(["--video", path, "--frames", args.frames, "--model", args.model])
    backend = MediaPipeBackend(args.model, **ps.backend_kwargs(server_args))
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        raise SystemExit(f"cannot open {path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    cap.set(cv2.CAP_PROP_POS_FRAMES, int(round(args.start * fps)))
    total = int(round(args.seconds * fps / args.every))
    meters = {n: Meter(np, cv2, args.still_px) for n in names}
    blocks = {n: ps.mask_variant(VARIANTS[n]) if VARIANTS[n] else None for n in names}
    rng = np.random.default_rng(1234)
    held = None
    dump_at = set(int(v) for v in args.dump_at.split(",") if v) if args.dump else set()
    stem = os.path.splitext(os.path.basename(path))[0] + ("_still" if args.still else "")
    for i in range(total):
        if args.still and held is not None:
            frame = held
        else:
            for _ in range(args.every):
                ok, frame = cap.read()
            if not ok or frame is None:
                break
            held = frame
        if args.gain != 1.0 or args.noise > 0:
            noise = rng.normal(0.0, args.noise, frame.shape) if args.noise > 0 else 0.0
            frame = np.clip(frame.astype(np.float32) * args.gain + noise, 0, 255).astype(np.uint8)
        persons = backend.process(frame, i * args.every * 1000.0 / fps)
        ids = [tid for tid, _ in persons]
        masks = list(backend.last_masks)[:len(persons)]
        lms = list(backend.last_landmarks)[:len(persons)]
        panels, saved = [], {}
        for n in names:
            t = time.perf_counter()
            if blocks[n] is None:
                rgb, mask, w, h = ps.camera_block(frame, masks, size, cv2, np)
            else:
                rgb, mask, w, h = blocks[n](frame, masks, size, cv2, np, ids, lms)
            meters[n].ms.append((time.perf_counter() - t) * 1e3)
            ordinal = np.frombuffer(mask, dtype=np.uint8).reshape(h, w)
            meters[n].add(ordinal, ids, lms, masks)
            if i in dump_at:
                rgb_a = np.frombuffer(rgb, dtype=np.uint8).reshape(h, w, 3)
                panels.append(overlay(np, cv2, rgb_a, ordinal, lms, n))
                saved["mask_" + n] = ordinal
                saved["rgb"] = rgb_a
        if i in dump_at:
            os.makedirs(args.dump, exist_ok=True)
            base = os.path.join(args.dump, f"{stem}_f{i:04d}")
            cam = cv2.resize(saved["rgb"][:, :, ::-1], (size[0] * 2, size[1] * 2))
            cv2.imwrite(base + ".png", np.hstack([cam] + panels))
            np.savez_compressed(
                base + ".npz", ids=np.array(ids, dtype=np.int64),
                joints=np.array([j for _, j in persons], dtype=np.float32).reshape(len(persons), -1, 4),
                frame_wh=np.array(frame.shape[1::-1]), **saved)
    cap.release()
    backend.close()
    return {n: m.report() for n, m in meters.items()}


def fmt(v, spec="5.2f"):
    return "    -" if v is None else format(v, spec)


def print_table(clip, cond, reports):
    print(f"\n== {clip} [{cond}]")
    print("variant      still-flicker% (p95, n)   change%  coverage% (n)   head elbow wrist  knee ankle"
          "   holes/f hole-px/f small-px/f hole%   frags/f frag%  ghost%  px/person  ms med/p95")
    for n, r in reports.items():
        c = r["coverage"]
        print(f"{n:12s} {fmt(r['flicker_still_pct'])} ({fmt(r['flicker_still_p95_pct'])}, {r['still_pairs']:4d})"
              f"     {fmt(r['change_all_pct'])}  {fmt(r['coverage_pct'], '5.1f')} ({r['coverage_joints']:5d})  "
              + " ".join(fmt(c[g][0], "5.1f") for g in GROUPS)
              + f"   {r['holes_per_frame']:6.3f} {r['hole_px_per_frame']:8.2f} {r['small_hole_px_per_frame']:9.2f}"
              f" {r['hole_pct_of_mask']:5.2f}   {r['fragments_per_frame']:6.3f} {r['fragment_pct_of_mask']:5.2f}"
              f"  {r['ghost_pct_of_mask']:5.2f}  {r['mask_px_per_person']:8.0f}"
              f"  {r['ms_median']:.2f}/{r['ms_p95']:.2f}")
    r = next(iter(reports.values()))
    print(f"   frames {r['frames']}, person-frames {r['person_frames']}, "
          f"persons without any mask pixel {r['no_mask_frames']}, pairs {r['pairs']}")


def hold(args):
    """Serve one dumped frame, held, as protocol v3 until killed."""
    import numpy as np
    import pose_server as ps
    d = np.load(args.hold)
    mask = d["mask_" + args.variant]
    h, w = mask.shape
    image = (np.ascontiguousarray(d["rgb"]).tobytes(), np.ascontiguousarray(mask).tobytes(), w, h)
    persons = [(int(t), [tuple(float(v) for v in j) for j in js]) for t, js in zip(d["ids"], d["joints"])]
    fw, fh = (int(v) for v in d["frame_wh"])
    if os.path.exists(args.socket):
        os.unlink(args.socket)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(args.socket)
    srv.listen(1)
    print(f"holding {args.hold} [{args.variant}] on {args.socket}", file=sys.stderr, flush=True)
    try:
        while True:
            client, _ = srv.accept()
            frame_id = 0
            try:
                while True:
                    client.sendall(ps.pack_frame(frame_id, time.monotonic(), fw, fh, 20, 1, persons, image,
                                                 flags=ps.header_flags(0)))
                    frame_id += 1
                    time.sleep(1.0 / 30.0)
            except OSError:
                client.close()
    finally:
        srv.close()
        if os.path.exists(args.socket):
            os.unlink(args.socket)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--video", action="append", default=[], help="a clip (repeatable)")
    ap.add_argument("--start", type=float, default=0.0, help="seconds into the clip")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--every", type=int, default=1, help="hand the tracker every N-th frame")
    ap.add_argument("--still", action="store_true", help="hold the frame at --start, add noise")
    ap.add_argument("--noise", type=float, default=None,
                    help="seeded sensor noise, sigma on 0..255 (default 3 with --still, else 0)")
    ap.add_argument("--gain", type=float, default=1.0,
                    help="multiply the picture (0.3 with --noise 8 = a dim room on a laptop "
                         "camera; a synthetic stress, not a recording)")
    ap.add_argument("--still-px", type=float, default=1.0,
                    help="a pair is still when no tracked joint moved this far (px at 320 wide)")
    ap.add_argument("--frames", default="320x240", help="mask size, as the game asks (WxH)")
    ap.add_argument("--model", default=os.path.join(REPO, "native", "models", "pose_landmarker_full.task"))
    ap.add_argument("--variants", default="raw", help="comma list of: " + ", ".join(VARIANTS))
    ap.add_argument("--json", default=None, help="also write the numbers here")
    ap.add_argument("--dump", default=None, metavar="DIR")
    ap.add_argument("--dump-at", default="", help="frame indices of the run, comma list")
    ap.add_argument("--hold", default=None, metavar="NPZ")
    ap.add_argument("--variant", default="raw", help="--hold: which mask of the .npz")
    ap.add_argument("--socket", default="/tmp/dc3_pose.sock")
    args = ap.parse_args(argv)
    if args.hold:
        return hold(args)
    if args.noise is None:
        args.noise = 3.0 if args.still else 0.0
    names = [n for n in args.variants.split(",") if n]
    for n in names:
        if n not in VARIANTS:
            ap.error(f"unknown variant {n!r} (have: {', '.join(VARIANTS)})")
    if not args.video:
        ap.error("give --video")
    cond = (f"{'still' if args.still else 'motion'}, gain {args.gain:g}, noise {args.noise:g}, "
            f"from {args.start:g} s for {args.seconds:g} s, every {args.every}, {args.frames}")
    out = {}
    for path in args.video:
        reports = run_clip(path, args, names)
        print_table(os.path.basename(path), cond, reports)
        out[os.path.basename(path)] = {"condition": cond, "variants": reports}
    if args.json:
        with open(args.json, "w") as f:
            json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
