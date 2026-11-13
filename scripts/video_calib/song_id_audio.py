#!/usr/bin/env python3
"""Find DC3 songs in a long recording by audio cross-correlation.

Each song dir under orig-assets/extracted/songs/ ships `<id>_prev.webm`, an
unencrypted 30 s clip of the song's preview window (songs.dta `(preview A B)`,
ms of song time).  The full `.mogg` is encrypted, so the preview is the
reference.  A 20 s slice of it (from --skip s in, to dodge the fade-in) is
slid along the recording's audio by normalised cross-correlation (NCC, 8 kHz
mono); a peak gives both the song and the recording time of song time 0:

    song_t0 = peak_time - (preview_A_ms / 1000 + skip)

USAGE
  python3 -I scripts/video_calib/song_id_audio.py --audio stream_8k.wav \
      --songs-dir orig-assets/extracted/songs --out song_hits.json [--min-ncc 0.3]

The recording's audio is a mix (game + microphone), so a true hit reads
NCC ~0.3-0.9 and noise ~0.05; every peak above --min-ncc is kept with
non-maximum suppression of 60 s per song, so a song played twice is two hits.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import wave

import numpy as np
from scipy.signal import fftconvolve

SR = 8000


def read_wav(path):
    with wave.open(path) as w:
        assert w.getframerate() == SR and w.getnchannels() == 1 and w.getsampwidth() == 2
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    return x.astype(np.float32) / 32768.0


def decode(path):
    raw = subprocess.run(
        ["ffmpeg", "-nostdin", "-loglevel", "error", "-i", path, "-vn", "-ac", "1",
         "-ar", str(SR), "-f", "s16le", "-"], check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0


def previews(songs_dta):
    out, cur = {}, None
    for line in open(songs_dta, encoding="utf-8", errors="replace"):
        m = re.match(r"^\((\S+)", line)
        if m:
            cur = m.group(1)
        m = re.search(r"\(preview (\d+) (\d+)\)", line)
        if m and cur:
            out[cur] = (int(m.group(1)), int(m.group(2)))
    return out


def ncc(signal, tmpl, chunk=SR * 600):
    """Normalised cross-correlation of tmpl against every offset of signal."""
    t = tmpl - tmpl.mean()
    t /= np.linalg.norm(t) + 1e-12
    n = len(t)
    out = np.zeros(max(0, len(signal) - n + 1), dtype=np.float32)
    # running energy of the signal under the template window
    c2 = np.concatenate([[0.0], np.cumsum(signal.astype(np.float64) ** 2)])
    c1 = np.concatenate([[0.0], np.cumsum(signal.astype(np.float64))])
    for s in range(0, len(out), chunk):
        e = min(len(out), s + chunk)
        seg = signal[s:e + n - 1]
        num = fftconvolve(seg, t[::-1], mode="valid")[: e - s]
        idx = np.arange(s, e)
        energy = (c2[idx + n] - c2[idx]) - (c1[idx + n] - c1[idx]) ** 2 / n
        out[s:e] = num / np.sqrt(np.maximum(energy, 1e-9))
    return out


def peaks(corr, min_ncc, sep):
    hits = []
    c = corr.copy()
    while True:
        i = int(np.argmax(c))
        if c[i] < min_ncc:
            break
        hits.append((i, float(corr[i])))
        c[max(0, i - sep): i + sep] = -1
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", required=True, help="8 kHz mono s16 wav of the recording")
    ap.add_argument("--songs-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--skip", type=float, default=3.0)
    ap.add_argument("--len", type=float, default=20.0)
    ap.add_argument("--min-ncc", type=float, default=0.25)
    ap.add_argument("--only", default="", help="comma list of song ids")
    a = ap.parse_args()

    sig = read_wav(a.audio)
    prev = previews(os.path.join(a.songs_dir, "songs.dta"))
    only = set(filter(None, a.only.split(",")))
    hits = []
    for song in sorted(os.listdir(a.songs_dir)):
        clip = os.path.join(a.songs_dir, song, f"{song}_prev.webm")
        if not os.path.exists(clip) or (only and song not in only):
            continue
        x = decode(clip)
        tmpl = x[int(a.skip * SR): int((a.skip + a.len) * SR)]
        corr = ncc(sig, tmpl)
        best = float(corr.max())
        found = peaks(corr, a.min_ncc, 60 * SR)
        pa = prev.get(song, (0, 0))[0] / 1000.0
        for i, v in found:
            t = i / SR
            hits.append({"song": song, "peak_s": round(t, 4), "ncc": round(v, 3),
                         "song_t0_s": round(t - (pa + a.skip), 4), "preview_ms": pa * 1000})
        print(f"{song:24s} best {best:.3f} hits {len(found)} "
              + " ".join(f"{i / SR:.1f}s:{v:.2f}" for i, v in found), flush=True)
    hits.sort(key=lambda h: h["peak_s"])
    with open(a.out, "w") as f:
        json.dump(hits, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
