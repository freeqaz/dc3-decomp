#!/usr/bin/env python3
"""pose_session.py -- read, cut and compare dc3-pose-session files.

A session file (DC3_POSE_RECORD, written by native/src/platform/
PoseSession_Native.cpp) holds every sensor frame the game ACCEPTED, as the
scorer received it, with the song clock at that moment.  A whole playtest is
one file; the song clock restarts for every song, and a song-synced replay
needs one song's frames, so:

  pose_session.py info  FILE            frames, people, and the song segments
  pose_session.py split FILE OUTDIR     one file per song segment (seg01.dc3session ...)
  pose_session.py cut   FILE N OUT      only segment N (as listed by info)
  pose_session.py compare SOURCE REPLAYED
                                        did a replay feed the scorer SOURCE?  REPLAYED
                                        is the recording the replaying game made
                                        (play.sh records every session): each SOURCE
                                        frame must appear once, in order, with the
                                        same id, timestamp, people and floor, and
                                        its song time no earlier than recorded

Replay one cut with
  scripts/play.sh --replay OUT     (or DC3_POSE=external DC3_POSE_PERFORM=session:OUT)
starting the same song on the same difficulty.

Standard library only.
"""
import argparse
import os
import sys

HEADER = "# dc3-pose-session v1"
# A song segment: the song clock runs forward; it ends where the clock jumps
# backwards by more than this (a new song, a restart, back to the menus).
JUMP_BACK_S = 0.5
MIN_SONG_S = 20.0


class Frame:
    __slots__ = ("seq", "rnd", "wall", "song", "frame_id", "num_persons", "lines", "valid")

    def __init__(self, line):
        f = line.split()
        if len(f) != 13 or f[0] != "F":
            raise ValueError("bad F line")
        self.seq = int(f[1])
        self.rnd = int(f[2])
        self.wall = float(f[3])
        self.song = float(f[4])
        self.frame_id = int(f[5])
        self.num_persons = int(f[12])
        self.lines = [line]
        self.valid = 0


def load(path):
    frames, comments = [], []
    with open(path) as fh:
        first = fh.readline().rstrip("\n")
        if first != HEADER:
            raise ValueError(f"{path}: not a dc3-pose-session v1 file (first line {first!r})")
        comments.append(first)
        for n, line in enumerate(fh, 2):
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("#"):
                comments.append(line)
            elif line.startswith("F "):
                frames.append(Frame(line))
            elif line.startswith("P "):
                if not frames:
                    raise ValueError(f"{path}:{n}: person line before any frame")
                frames[-1].lines.append(line)
                if line.split()[2] == "1":
                    frames[-1].valid += 1
            else:
                raise ValueError(f"{path}:{n}: unknown line {line[:40]!r}")
    # A file cut off mid-frame (the game was killed while writing) loses only
    # its last frame.
    if frames and len(frames[-1].lines) != 1 + frames[-1].num_persons:
        frames.pop()
    return comments, frames


def segments(frames):
    """[(start, end_exclusive)] runs over which the song clock does not jump back."""
    out, start = [], 0
    for i in range(1, len(frames)):
        if frames[i].song < frames[i - 1].song - JUMP_BACK_S:
            out.append((start, i))
            start = i
    if frames:
        out.append((start, len(frames)))
    return out


def describe(frames, segs):
    rows = []
    for k, (a, b) in enumerate(segs, 1):
        fr = frames[a:b]
        span = fr[-1].song - fr[0].song
        wall = fr[-1].wall - fr[0].wall
        seen = sum(1 for f in fr if f.valid)
        rows.append({"n": k, "frames": b - a, "song_from": fr[0].song, "song_to": fr[-1].song,
                     "wall_from": fr[0].wall, "wall_s": wall, "song_s": span,
                     "fps": (b - a) / wall if wall > 0 else 0.0,
                     "with_person": seen / (b - a),
                     # A song's clock starts in the count-in (negative); the menus
                     # run the shell music's clock from 0.
                     "kind": "song" if span >= MIN_SONG_S and fr[0].song < 0 else "menus/other"})
    return rows


def write(path, comments, frames):
    with open(path, "w") as fh:
        fh.write("\n".join(comments) + "\n")
        for f in frames:
            fh.write("\n".join(f.lines) + "\n")


HELD_ID = 0x80000000  # PoseSession_Native.cpp: a held frame's id has the high bit set


def payload(f):
    """What the scorer receives from a frame: everything but seq, rnd, wall
    and song time (frame id, timestamp, unlevelled, level, floor normal,
    people)."""
    return [" ".join(f.lines[0].split()[5:])] + f.lines[1:]


def compare(source, replayed):
    """Match REPLAYED's recorded (non-held) frames to SOURCE by frame id.
    Returns a dict of counts and song-time lags (replayed - source, s)."""
    by_id = {}
    for k, f in enumerate(source):
        by_id.setdefault(f.frame_id, k)
    seen, lags, mismatched, unknown, out_of_order = set(), [], [], 0, 0
    last = -1
    for f in replayed:
        if f.frame_id >= HELD_ID:
            continue
        k = by_id.get(f.frame_id)
        if k is None:
            unknown += 1
            continue
        if k in seen or k < last:
            out_of_order += 1
        seen.add(k)
        last = k
        if payload(f) != payload(source[k]):
            mismatched.append(f.frame_id)
        lags.append(f.song - source[k].song)
    lags.sort()
    # Frames after the last one replayed were never due: the replay ended (its
    # song clock stopped at the results) before reaching them.  Up to a second
    # of song time is allowed for that; anything earlier missing is a drop.
    reached = max(seen) if seen else -1
    tail = len(source) - 1 - reached
    tail_s = source[-1].song - source[reached].song if seen else float("inf")
    return {"source_frames": len(source), "replayed": len(seen),
            "missing": reached + 1 - len(seen), "not_reached": tail,
            "not_reached_s": round(tail_s, 3), "mismatched": len(mismatched),
            "first_mismatch": mismatched[0] if mismatched else None,
            "unknown_ids": unknown, "out_of_order": out_of_order,
            "lag_min": lags[0] if lags else None, "lag_max": lags[-1] if lags else None,
            "lag_median": lags[len(lags) // 2] if lags else None,
            "identical": bool(source) and reached + 1 == len(seen) and tail_s < 1.0
            and not mismatched
            and not out_of_order and not unknown and (not lags or lags[0] >= -1e-6)}


def cmd_compare(a):
    _, src = load(a.source)
    _, rep = load(a.replayed)
    r = compare(src, rep)
    print(f"{r['replayed']} of {r['source_frames']} frames replayed, {r['missing']} missing, "
          f"{r['not_reached']} at the end not reached ({r['not_reached_s']} s), "
          f"{r['mismatched']} differ, {r['out_of_order']} out of order, {r['unknown_ids']} "
          f"not in the source; song-time lag {r['lag_min']}..{r['lag_max']} s "
          f"(median {r['lag_median']})")
    print("IDENTICAL" if r["identical"] else "DIFFERENT")
    return 0 if r["identical"] else 1


def cmd_info(a):
    comments, frames = load(a.file)
    print(f"{a.file}: {len(frames)} frames over {frames[-1].wall - frames[0].wall:.1f} s"
          if frames else f"{a.file}: no frames")
    for r in describe(frames, segments(frames)):
        print(f"  seg{r['n']:02d}  {r['kind']:<11}  {r['frames']:6d} frames  song "
              f"{r['song_from']:8.2f}..{r['song_to']:8.2f} s  at wall {r['wall_from']:8.1f} s  "
              f"{r['fps']:5.1f} fps  person in {100 * r['with_person']:5.1f}%")
    return 0


def cmd_split(a):
    comments, frames = load(a.file)
    os.makedirs(a.outdir, exist_ok=True)
    for r, (s, e) in zip(describe(frames, segments(frames)), segments(frames)):
        if r["kind"] != "song" and not a.all:
            continue
        out = os.path.join(a.outdir, f"seg{r['n']:02d}.dc3session")
        write(out, comments + [f"# cut from {os.path.basename(a.file)} segment {r['n']}"],
              frames[s:e])
        print(out)
    return 0


def cmd_cut(a):
    comments, frames = load(a.file)
    segs = segments(frames)
    if not 1 <= a.n <= len(segs):
        print(f"no segment {a.n} (there are {len(segs)})", file=sys.stderr)
        return 1
    s, e = segs[a.n - 1]
    write(a.out, comments + [f"# cut from {os.path.basename(a.file)} segment {a.n}"], frames[s:e])
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    i = sub.add_parser("info")
    i.add_argument("file")
    s = sub.add_parser("split")
    s.add_argument("file")
    s.add_argument("outdir")
    s.add_argument("--all", action="store_true", help="also the menu segments")
    c = sub.add_parser("cut")
    c.add_argument("file")
    c.add_argument("n", type=int)
    c.add_argument("out")
    m = sub.add_parser("compare")
    m.add_argument("source")
    m.add_argument("replayed")
    a = ap.parse_args(argv)
    try:
        return {"info": cmd_info, "split": cmd_split, "cut": cmd_cut,
                "compare": cmd_compare}[a.cmd](a)
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
