#!/usr/bin/env python3
"""Extract every DancerSequence (named, labelled) from DC3 move_data.milo_xbox files
into a single .npz + manifest.

Output:
  poses.npz  -> 'pos'  float32 (N, 20, 3)   Kinect camera-space metres
                'disp' float32 (N, 20, 3)   per-joint displacement over 'ms'
                'ms'   int32   (N,)         displacement window in ms (-1 = none)
                'move_idx', 'move_frame_idx' int16 (N,)
                'seq_id' int32 (N,)         index into manifest
  manifest.tsv -> seq_id, song, seq_name, n_frames
"""
import sys, os, struct, glob, numpy as np
sys.path.insert(0, "/home/free/code/milohax/dc3-decomp/scripts/milo")
sys.path.insert(0, "/tmp/dc3_pose")
from inflate_milo import decompress_milo
from scan_fast import scan, PER

def rstr(d, o):
    n, = struct.unpack_from(">I", d, o); o += 4
    return d[o:o+n].decode("latin-1"), o+n

def entries(d):
    o = 4
    _t, o = rstr(d, o); _n, o = rstr(d, o); o += 9
    c, = struct.unpack_from(">I", d, o); o += 4
    out = []
    for _ in range(c):
        a, o = rstr(d, o); b, o = rstr(d, o); out.append((a, b))
    return out

def main(paths, outdir):
    P, D, MS, MI, MFI, SID = [], [], [], [], [], []
    man = []
    sid = 0
    for p in paths:
        song = os.path.basename(os.path.dirname(os.path.dirname(p)))
        d, _ = decompress_milo(p); d = bytes(d)
        names = [n for t, n in entries(d) if t == "DancerSequence"]
        runs = scan(d)
        if len(names) != len(runs):
            print(f"WARN {song}: {len(names)} seq entries vs {len(runs)} runs -> labels unreliable", file=sys.stderr)
        for k, (o, c, ver) in enumerate(runs):
            nm = names[k] if k < len(names) else f"<unlabelled_{k}>"
            man.append((sid, song, nm, c))
            for f in range(c):
                b = o + f * PER
                mi, mfi = struct.unpack_from(">hh", d, b)
                v = np.frombuffer(d, dtype=">f4", count=120, offset=b+4).reshape(20, 6)
                P.append(v[:, 0:3].astype(np.float32))
                D.append(v[:, 3:6].astype(np.float32))
                ms, = struct.unpack_from(">i", d, b+484)
                MS.append(ms); MI.append(mi); MFI.append(mfi); SID.append(sid)
            sid += 1
        print(f"{song}: {len(runs)} seqs, {sum(c for _,c,_ in runs)} frames", flush=True)
    os.makedirs(outdir, exist_ok=True)
    np.savez_compressed(os.path.join(outdir, "poses.npz"),
                        pos=np.array(P, dtype=np.float32),
                        disp=np.array(D, dtype=np.float32),
                        ms=np.array(MS, dtype=np.int32),
                        move_idx=np.array(MI, dtype=np.int16),
                        move_frame_idx=np.array(MFI, dtype=np.int16),
                        seq_id=np.array(SID, dtype=np.int32))
    with open(os.path.join(outdir, "manifest.tsv"), "w") as f:
        f.write("seq_id\tsong\tseq_name\tn_frames\n")
        for r in man:
            f.write("\t".join(map(str, r)) + "\n")
    print(f"WROTE {outdir}: {len(P)} frames, {len(man)} sequences")

if __name__ == "__main__":
    main(sys.argv[2:], sys.argv[1])
