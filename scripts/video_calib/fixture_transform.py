#!/usr/bin/env python3
"""Transform a dc3-pose-fixture v1 file.

  --hold     untracked frames repeat the last tracked frame (frames before the
             first tracked one take it).  The in-process performer publishes
             an untracked frame as 0 persons, and a few seconds of nobody
             pauses the song ("stand in front of the Kinect"), so a replay
             of real footage needs MediaPipe's dropouts held, as POSE_INPUT's
             correct-choreography experiment did.
  --mirror   x -> -x and every Left joint swapped with its Right twin: what
             the camera would have seen had the picture been flipped.  Use it
             to test which way a recording faces.
  --shift S  add S seconds to every timestamp.
  --pitch D  rotate every joint about the camera's x axis by D degrees
             (y' = y cos - z sin, z' = y sin + z cos): positive D undoes a
             camera that looks DOWN at the player, whose feet then read
             farther away than the head.
  --one-euro MINCUT,BETA
             causal One Euro filter (Casiez et al. 2012) per joint and axis,
             what a live pipeline could run: cutoff = MINCUT + BETA*|speed|
             Hz, derivative cutoff 1 Hz.  Applied after --hold, so held
             frames are filtered like real ones.  To test whether smoothing
             MediaPipe changes scores.

  python3 -I scripts/video_calib/fixture_transform.py IN OUT [--hold] [--mirror] [--shift 0.1]
"""
import argparse
import math
import sys

# DC3 joint order (native/scripts/pose_mediapipe.py DC3_JOINT_NAMES)
SWAP = {4: 8, 5: 9, 6: 10, 7: 11, 12: 15, 13: 16, 14: 17, 18: 19}
SWAP.update({v: k for k, v in list(SWAP.items())})


def mirror(vals):
    j = [vals[i * 4:(i + 1) * 4] for i in range(20)]
    out = []
    for i in range(20):
        x, y, z, c = j[SWAP.get(i, i)]
        out.append([-x, y, z, c])
    return [v for jj in out for v in jj]


def pitch(vals, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    out = list(vals)
    for i in range(20):
        y, z = vals[i * 4 + 1], vals[i * 4 + 2]
        out[i * 4 + 1] = y * c - z * s
        out[i * 4 + 2] = y * s + z * c
    return out


def _alpha(cutoff, dt):
    tau = 1.0 / (2 * math.pi * cutoff)
    return 1.0 / (1.0 + tau / dt)


def one_euro(frames, mincut, beta, dcut=1.0):
    """In place, causal: each tracked frame only sees earlier ones.  The
    confidence channel is left alone; an untracked gap restarts the filter."""
    prev_t = x_hat = dx_hat = None
    for f in frames:
        t, v = f
        if v is None:
            prev_t = None
            continue
        if prev_t is None:
            x_hat, dx_hat = list(v), [0.0] * len(v)
            prev_t = t
            continue
        dt = max(t - prev_t, 1e-6)
        prev_t = t
        out = list(v)
        for i in range(len(v)):
            if i % 4 == 3:
                continue
            dx = (v[i] - x_hat[i]) / dt
            dx_hat[i] += _alpha(dcut, dt) * (dx - dx_hat[i])
            x_hat[i] += _alpha(mincut + beta * abs(dx_hat[i]), dt) * (v[i] - x_hat[i])
            out[i] = x_hat[i]
        f[1] = out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--hold", action="store_true")
    ap.add_argument("--mirror", action="store_true")
    ap.add_argument("--shift", type=float, default=0.0)
    ap.add_argument("--pitch", type=float, default=0.0)
    ap.add_argument("--one-euro", default="", metavar="MINCUT,BETA")
    a = ap.parse_args()

    header, frames = [], []
    for line in open(a.inp):
        if line.startswith("#"):
            header.append(line.rstrip("\n"))
            continue
        p = line.split()
        if not p:
            continue
        t = float(p[0]) + a.shift
        vals = [float(v) for v in p[2:]] if p[1] == "1" else None
        if vals is not None and a.mirror:
            vals = mirror(vals)
        if vals is not None and a.pitch:
            vals = pitch(vals, a.pitch)
        frames.append([t, vals])
    held = 0
    if a.hold:
        first = next((v for _, v in frames if v is not None), None)
        last = first
        for f in frames:
            if f[1] is None:
                f[1] = last
                held += 1
            else:
                last = f[1]
    if a.one_euro:
        mincut, beta = (float(v) for v in a.one_euro.split(","))
        one_euro(frames, mincut, beta)
    with open(a.out, "w") as o:
        for h in header:
            o.write(h + "\n")
        o.write(f"# fixture_transform.py hold={int(a.hold)} mirror={int(a.mirror)} shift={a.shift} pitch={a.pitch} "
                f"one_euro={a.one_euro or '-'} held={held}\n")
        for t, v in frames:
            if v is None:
                o.write(f"{t:.4f} 0\n")
            else:
                o.write(f"{t:.4f} 1 " + " ".join(
                    f"{v[i]:.5f} {v[i + 1]:.5f} {v[i + 2]:.5f} {v[i + 3]:.3f}"
                    for i in range(0, 80, 4)) + "\n")
    print(f"{a.out}: {len(frames)} frames, held {held}")


if __name__ == "__main__":
    sys.exit(main())
