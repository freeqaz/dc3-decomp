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

  python3 -I scripts/video_calib/fixture_transform.py IN OUT [--hold] [--mirror] [--shift 0.1]
"""
import argparse
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--hold", action="store_true")
    ap.add_argument("--mirror", action="store_true")
    ap.add_argument("--shift", type=float, default=0.0)
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
    with open(a.out, "w") as o:
        for h in header:
            o.write(h + "\n")
        o.write(f"# fixture_transform.py hold={int(a.hold)} mirror={int(a.mirror)} shift={a.shift} "
                f"held={held}\n")
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
