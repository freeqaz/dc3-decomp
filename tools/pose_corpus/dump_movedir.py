#!/usr/bin/env python3
"""Dump DancerSequence + SkeletonClip 3D joint data out of a DC3 moves.milo_xbox (MoveDir).

Strategy: milo ObjectDir files are (type,name) entry table followed by per-object
data blobs separated by the 0xADDEADDE sentinel. We don't need a full Milo object
parser -- we split on the sentinel and parse only the blobs whose entry type is
DancerSequence / SkeletonClip.
"""
import struct
import sys
import os

sys.path.insert(0, "/home/free/code/milohax/dc3-decomp/scripts/milo")
from inflate_milo import decompress_milo

SENTINEL = b"\xad\xde\xad\xde"


def rstr(d, o):
    (n,) = struct.unpack_from(">I", d, o)
    o += 4
    return d[o:o + n].decode("latin-1"), o + n


def parse_entries(d):
    """Parse the ObjectDir header entry table: returns (list[(type,name)], offset_after)."""
    o = 0
    (rev,) = struct.unpack_from(">I", d, o); o += 4
    dtype, o = rstr(d, o)          # dir class, e.g. "MoveDir"
    (count,) = struct.unpack_from(">I", d, o); o += 4
    ents = []
    for _ in range(count):
        t, o = rstr(d, o)
        n, o = rstr(d, o)
        ents.append((t, n))
    return rev, dtype, ents, o


def split_blobs(d, start):
    out = []
    i = start
    while True:
        j = d.find(SENTINEL, i)
        if j < 0:
            out.append(d[i:])
            break
        out.append(d[i:j])
        i = j + 4
    return out


def parse_dancersequence(b):
    """Return list of frames: (moveIdx, moveFrameIdx, 20x(pos,disp), elapsedMs).
    rev 8 layout (DancerSequence.cpp:51). We brute-force the header length by
    searching for the numFrames field that makes the size arithmetic work."""
    PER = 2 + 2 + 20 * (12 + 12) + 4          # 488
    for hdr in range(0, min(len(b), 512) - 4):
        (n,) = struct.unpack_from(">I", b, hdr)
        if n <= 0 or n > 100000:
            continue
        if hdr + 4 + n * PER == len(b):
            frames = []
            o = hdr + 4
            for _ in range(n):
                mi, mfi = struct.unpack_from(">hh", b, o); o += 4
                pos = []
                disp = []
                for _j in range(20):
                    pos.append(struct.unpack_from(">3f", b, o)); o += 12
                    disp.append(struct.unpack_from(">3f", b, o)); o += 12
                (ms,) = struct.unpack_from(">i", b, o); o += 4
                frames.append((mi, mfi, pos, disp, ms))
            return hdr, frames
    return None, None


def main(path):
    data, info = decompress_milo(path)
    data = bytes(data)
    rev, dtype, ents, off = parse_entries(data)
    blobs = split_blobs(data, off)
    print(f"{os.path.basename(path)}: dirRev={rev} dirType={dtype} entries={len(ents)} blobs={len(blobs)}")
    from collections import Counter
    print("  entry types:", dict(Counter(t for t, _ in ents)))

    total_frames = 0
    ok = 0
    for idx, (t, n) in enumerate(ents):
        if t != "DancerSequence":
            continue
        if idx >= len(blobs):
            break
        hdr, frames = parse_dancersequence(blobs[idx])
        if frames is None:
            print(f"  [MISS] {n} blobsize={len(blobs[idx])}")
            continue
        ok += 1
        total_frames += len(frames)
        if ok <= 2:
            print(f"  [OK] {n}: hdr={hdr} frames={len(frames)} ms={frames[0][4]}")
            for j, p in enumerate(frames[0][2]):
                print(f"        joint{j:2d} = ({p[0]:8.4f},{p[1]:8.4f},{p[2]:8.4f})")
    print(f"  DancerSequence parsed OK: {ok} objects, {total_frames} frames")
    return total_frames


if __name__ == "__main__":
    tot = 0
    for p in sys.argv[1:]:
        tot += main(p) or 0
    print("GRAND TOTAL FRAMES:", tot)
