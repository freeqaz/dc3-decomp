#!/usr/bin/env python3
"""cutoff.py -- make cut-off variants of recorded pose streams (PLAN H-90).

A webcam in a small room does not see the whole player: the lower legs, the
head, or a hand at full reach leave the picture.  This tool takes a recorded
stream -- a dc3-pose-session v1 file (scripts/playtest/pose_session.py: what
the game accepted, joint confidence 0/1/2) or a dc3-pose-fixture v1 file
(scripts/pose_replay_record.py: MediaPipe output, confidence 0..1) -- and
writes the same stream with a body region marked NOT TRACKED, which is what
the tracker reports for a joint outside the picture.  Deterministic and fast,
so every scoring run of a variant sees the same joints.

  cutoff.py cut IN OUT --cut knees [--pos keep|drop]
  cutoff.py trim IN OUT          # session: drop the empty count-in / start stall
  cutoff.py stats IN             # per-joint tracked fraction in 10 s bins
  cutoff.py --selftest

Cuts (DC3 joint indices, gesture/BaseSkeleton.h):
  knees    knees, ankles, feet              (frame bottom at the knees)
  midshin  ankles, feet
  ankles   feet NOT TRACKED, ankles INFERRED (frame bottom at the ankles)
  head     head
  left     player's left elbow, wrist, hand (that side at full reach)
  right    player's right elbow, wrist, hand
  close    knees-down and head (the dancer too close)

--pos keep (default) leaves the cut joints' positions as recorded; --pos drop
moves them 2 m below the hip, an obviously wrong place, so a run can show that
nothing downstream trusts the position of a joint marked not tracked.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "playtest"))

NJ = 20
HIP_CENTER, HEAD = 0, 3
KNEE_L, ANKLE_L, KNEE_R, ANKLE_R, FOOT_L, FOOT_R = 13, 14, 16, 17, 18, 19
ELBOW_L, WRIST_L, HAND_L = 5, 6, 7
ELBOW_R, WRIST_R, HAND_R = 9, 10, 11

CUTS = {
    # name: (not tracked, inferred)
    "full": ((), ()),
    "knees": ((KNEE_L, ANKLE_L, KNEE_R, ANKLE_R, FOOT_L, FOOT_R), ()),
    "midshin": ((ANKLE_L, ANKLE_R, FOOT_L, FOOT_R), ()),
    "ankles": ((FOOT_L, FOOT_R), (ANKLE_L, ANKLE_R)),
    "head": ((HEAD,), ()),
    "left": ((ELBOW_L, WRIST_L, HAND_L), ()),
    "right": ((ELBOW_R, WRIST_R, HAND_R), ()),
    "close": ((HEAD, KNEE_L, ANKLE_L, KNEE_R, ANKLE_R, FOOT_L, FOOT_R), ()),
}


def kind_of(path):
    with open(path) as fh:
        first = fh.readline().strip()
    if first == "# dc3-pose-session v1":
        return "session"
    if first == "# dc3-pose-fixture v1":
        return "fixture"
    sys.exit(f"{path}: neither a dc3-pose-session v1 nor a dc3-pose-fixture v1 file")


def cut_joints(vals, cut, pos, conf_scale):
    """vals: 80 strings (20 x x y z c). conf_scale: 'int' (0/1/2) or 'float'."""
    dead, inferred = CUTS[cut]
    hip = [float(v) for v in vals[HIP_CENTER * 4:HIP_CENTER * 4 + 3]]
    for j in dead:
        if conf_scale == "int":
            vals[j * 4 + 3] = "0"
        else:
            vals[j * 4 + 3] = "0.000"
        if pos == "drop":
            vals[j * 4 + 0] = f"{hip[0]:.5f}"
            vals[j * 4 + 1] = f"{hip[1] - 2.0:.5f}"
            vals[j * 4 + 2] = f"{hip[2]:.5f}"
    for j in inferred:
        if conf_scale == "int":
            vals[j * 4 + 3] = str(min(int(vals[j * 4 + 3]), 1))
        else:
            vals[j * 4 + 3] = f"{min(float(vals[j * 4 + 3]), 0.45):.3f}"
    return vals


def cut_line_session(line, cut, pos):
    f = line.split()
    if f[0] != "P":
        return line
    vals = cut_joints(f[3:3 + 4 * NJ], cut, pos, "int")
    return " ".join(f[:3] + vals)


def cut_line_fixture(line, cut, pos):
    if line.startswith("#"):
        return line
    f = line.split()
    if len(f) < 2 + 4 * NJ or f[1] != "1":
        return line
    vals = cut_joints(f[2:2 + 4 * NJ], cut, pos, "float")
    return " ".join(f[:2] + vals)


def cmd_cut(a):
    kind = kind_of(a.inp)
    fn = cut_line_session if kind == "session" else cut_line_fixture
    out = []
    with open(a.inp) as fh:
        for n, line in enumerate(fh):
            line = line.rstrip("\n")
            out.append(fn(line, a.cut, a.pos) if line else line)
    # provenance after the format line
    out.insert(1, f"# cutoff.py cut={a.cut} pos={a.pos}")
    with open(a.out, "w") as fh:
        fh.write("\n".join(out) + "\n")
    print(f"{a.out}: {kind}, cut {a.cut}")
    return 0


def cmd_trim(a):
    import pose_session
    comments, frames = pose_session.load(a.inp)
    if not frames:
        sys.exit("no frames")
    # The song clock starts moving at the first frame more than 0.2 s past the
    # first frame's time; before that the game was in the count-in or held
    # (a pause, a player not yet in view).  Keep only the KEEP frames with a
    # person just before it, so a replay starts with the player in view.
    t0 = max(frames[0].song, 0.0)
    m = next((i for i, f in enumerate(frames) if f.song > t0 + 0.2), len(frames))
    keep_before = [i for i in range(m) if frames[i].valid][-a.keep:]
    kept = [frames[i] for i in keep_before] + frames[m:]
    comments.append(f"# cutoff.py trim: dropped {len(frames) - len(kept)} start frames")
    pose_session.write(a.out, comments, kept)
    print(f"{a.out}: {len(kept)} of {len(frames)} frames, starts at song {kept[0].song:.3f}")
    return 0


def cmd_still(a):
    """A fixture of one standing frame held for the whole span of IN: the
    standing-still control, with the same body and camera as IN."""
    rows = [l.rstrip("\n") for l in open(a.inp)]
    data = [l.split() for l in rows if l and not l.startswith("#")]
    pick = next((f for f in data if float(f[0]) >= a.at and f[1] == "1"), None)
    if pick is None:
        sys.exit(f"{a.inp}: no tracked frame at or after {a.at}")
    t0, t1 = float(data[0][0]), float(data[-1][0])
    with open(a.out, "w") as fh:
        fh.write("# dc3-pose-fixture v1\n")
        fh.write(f"# cutoff.py still of {os.path.basename(a.inp)} frame t={pick[0]}\n")
        n = int((t1 - t0) * 30.0) + 1
        for k in range(n):
            fh.write(f"{t0 + k / 30.0:.4f} 1 " + " ".join(pick[2:]) + "\n")
    print(f"{a.out}: still, frame t={pick[0]}, {n} frames")
    return 0


def iter_people(path):
    """(song_t, [conf per joint as 0/1/2]) for the first valid person per frame."""
    kind = kind_of(path)
    t = None
    with open(path) as fh:
        for line in fh:
            f = line.split()
            if not f or f[0].startswith("#"):
                continue
            if kind == "session":
                if f[0] == "F":
                    t = float(f[4])
                    done = False
                elif f[0] == "P" and f[2] == "1" and not done:
                    done = True
                    yield t, [int(f[3 + 4 * j + 3]) for j in range(NJ)]
            else:
                if len(f) >= 2 + 4 * NJ and f[1] == "1":
                    cs = [float(f[2 + 4 * j + 3]) for j in range(NJ)]
                    yield float(f[0]), [0 if c < 0.3 else 1 if c < 0.6 else 2 for c in cs]


def cmd_stats(a):
    names = ["hipC", "spine", "shC", "head", "shL", "elL", "wrL", "haL", "shR", "elR",
             "wrR", "haR", "hipL", "knL", "anL", "hipR", "knR", "anR", "ftL", "ftR"]
    bins = {}
    for t, cs in iter_people(a.inp):
        b = int(t // a.bin) * a.bin
        acc = bins.setdefault(b, [0, [0] * NJ])
        acc[0] += 1
        for j, c in enumerate(cs):
            if c == 2:
                acc[1][j] += 1
    cols = [3, 7, 11, 13, 14, 16, 17, 18, 19]
    print("song_s   n  " + " ".join(f"{names[j]:>5}" for j in cols) + "   (% tracked)")
    for b in sorted(bins):
        n, tr = bins[b]
        print(f"{b:6.0f} {n:4d}  " + " ".join(f"{100 * tr[j] / n:5.0f}" for j in cols))
    return 0


def selftest():
    import tempfile
    d = tempfile.mkdtemp(prefix="cutoff-selftest-")
    sess = os.path.join(d, "s.dc3session")
    joints = " ".join(f"{j * 0.1:.5f} {1.0 - j * 0.05:.5f} 2.00000 2" for j in range(NJ))
    with open(sess, "w") as fh:
        fh.write("# dc3-pose-session v1\n")
        fh.write("F 1 1 0.0 1.0 1 1.0 1 1 0 1 0 1\n")
        fh.write(f"P 7 1 {joints}\n")
    out = os.path.join(d, "o.dc3session")
    cmd_cut(argparse.Namespace(inp=sess, out=out, cut="knees", pos="drop"))
    rows = [l.split() for l in open(out) if l.startswith("P ")]
    confs = [rows[0][3 + 4 * j + 3] for j in range(NJ)]
    dead = set(CUTS["knees"][0])
    ok = all((c == "0") == (j in dead) for j, c in enumerate(confs))
    ok = ok and float(rows[0][3 + 4 * KNEE_L + 1]) < -0.9  # dropped 2 m below the hip (y 1.0)
    fx = os.path.join(d, "f.dc3pose")
    with open(fx, "w") as fh:
        fh.write("# dc3-pose-fixture v1\n")
        fh.write("0.0 1 " + " ".join(f"{j * 0.1:.5f} 1.0 2.0 0.900" for j in range(NJ)) + "\n")
        fh.write("0.033 0\n")
    fo = os.path.join(d, "fo.dc3pose")
    cmd_cut(argparse.Namespace(inp=fx, out=fo, cut="ankles", pos="keep"))
    rows = [l.split() for l in open(fo) if not l.startswith("#")]
    c = [float(rows[0][2 + 4 * j + 3]) for j in range(NJ)]
    ok = ok and c[FOOT_L] == 0.0 and c[ANKLE_L] == 0.45 and c[KNEE_L] == 0.9
    ok = ok and rows[1] == ["0.033", "0"]
    print("selftest", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("cut")
    c.add_argument("inp")
    c.add_argument("out")
    c.add_argument("--cut", choices=sorted(CUTS), required=True)
    c.add_argument("--pos", choices=("keep", "drop"), default="keep")
    t = sub.add_parser("trim")
    t.add_argument("inp")
    t.add_argument("out")
    t.add_argument("--keep", type=int, default=10)
    st = sub.add_parser("still")
    st.add_argument("inp")
    st.add_argument("out")
    st.add_argument("--at", type=float, default=-4.0, help="song time of the frame to hold")
    s = sub.add_parser("stats")
    s.add_argument("inp")
    s.add_argument("--bin", type=float, default=10.0)
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.cmd == "cut":
        return cmd_cut(a)
    if a.cmd == "trim":
        return cmd_trim(a)
    if a.cmd == "still":
        return cmd_still(a)
    if a.cmd == "stats":
        return cmd_stats(a)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
