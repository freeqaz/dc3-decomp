#!/usr/bin/env python3
"""gen_sfx_ogg.py — transcode RB3PCM01 .pcm SFX sidecars to compact .ogg twins.

DC3's web XMA->PCM sidecar bridge (native/src/platform/XmaPcmSidecar.h) fetches
one raw-PCM sidecar per distinct kXMA SFX. Raw PCM neither compresses on the
wire (brotli -5%) nor amortizes: measured 514 fetches / 82.8 MB during a single
boot->song_select run. This script mirrors rb3's W5 fix: encode each
<key>.pcm to a <key>.ogg (libvorbis, ~10% of the bytes) NEXT TO the .pcm in the
same sidecar dir. The runtime loader tries .ogg first and falls back to .pcm,
so a partially-transcoded dir is always safe.

Usage:
  python3 scripts/web/gen_sfx_ogg.py                 # default sidecar dir
  python3 scripts/web/gen_sfx_ogg.py --dir PATH      # explicit dir
  python3 scripts/web/gen_sfx_ogg.py --quality 4     # vorbis -q:a (default 4)
  python3 scripts/web/gen_sfx_ogg.py --jobs 8        # parallel ffmpeg procs

Idempotent: an up-to-date .ogg (mtime >= its .pcm) is skipped, so re-running
after a native run adds sidecars only transcodes the new ones.
"""

import argparse
import concurrent.futures
import os
import struct
import subprocess
import sys

REPO_ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_DIR = os.path.join(REPO_ROOT, "orig-assets", "extracted", "sfx", "gen", "xma_pcm")

MAGIC = b"RB3PCM01"
HEADER_LEN = 24  # magic(8) sampleRate(i32) numSamples(i32) numChannels(i32) rsvd(i32), LE


def parse_header(path):
    """Return (sample_rate, num_samples, num_channels, data_offset) or None."""
    with open(path, "rb") as fh:
        hdr = fh.read(HEADER_LEN)
    if len(hdr) != HEADER_LEN or hdr[:8] != MAGIC:
        return None
    sr, ns, ch, _rsvd = struct.unpack("<iiii", hdr[8:])
    if sr <= 0 or ns <= 0 or ch <= 0 or ch > 8:
        return None
    return sr, ns, ch, HEADER_LEN


def transcode_one(pcm_path, quality):
    """Encode one sidecar. Returns (status, pcm_path, detail)."""
    ogg_path = pcm_path[:-4] + ".ogg"
    try:
        if (os.path.isfile(ogg_path)
                and os.path.getmtime(ogg_path) >= os.path.getmtime(pcm_path)):
            return ("skip", pcm_path, "up-to-date")
        parsed = parse_header(pcm_path)
        if parsed is None:
            return ("bad", pcm_path, "not RB3PCM01 / bad header")
        sr, ns, ch, off = parsed
        want = ns * ch * 2
        with open(pcm_path, "rb") as fh:
            fh.seek(off)
            data = fh.read(want)
        if len(data) != want:
            return ("bad", pcm_path, "truncated PCM payload")
        tmp = ogg_path + ".tmp"
        cmd = [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "s16le", "-ar", str(sr), "-ac", str(ch), "-i", "pipe:0",
            "-c:a", "libvorbis", "-q:a", str(quality), "-f", "ogg", tmp,
        ]
        proc = subprocess.run(cmd, input=data, capture_output=True)
        if proc.returncode != 0 or not os.path.getsize(tmp):
            if os.path.exists(tmp):
                os.unlink(tmp)
            return ("fail", pcm_path, proc.stderr.decode(errors="replace")[:200])
        os.replace(tmp, ogg_path)
        return ("ok", pcm_path,
                "%d -> %d bytes" % (HEADER_LEN + want, os.path.getsize(ogg_path)))
    except Exception as e:  # keep the batch going on any single-file surprise
        return ("fail", pcm_path, repr(e))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dir", default=DEFAULT_DIR, help="sidecar dir (default: %(default)s)")
    ap.add_argument("--quality", type=float, default=4, help="vorbis -q:a (default 4, ~128kbps)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    if not os.path.isdir(args.dir):
        print("no such sidecar dir: %s" % args.dir, file=sys.stderr)
        return 1
    pcms = sorted(
        os.path.join(args.dir, f) for f in os.listdir(args.dir) if f.endswith(".pcm")
    )
    if not pcms:
        print("no .pcm sidecars in %s" % args.dir, file=sys.stderr)
        return 1

    counts = {"ok": 0, "skip": 0, "bad": 0, "fail": 0}
    in_bytes = out_bytes = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for status, path, detail in pool.map(
                lambda p: transcode_one(p, args.quality), pcms):
            counts[status] += 1
            if status == "ok":
                in_bytes += os.path.getsize(path)
                out_bytes += os.path.getsize(path[:-4] + ".ogg")
            elif status in ("bad", "fail"):
                print("%s: %s (%s)" % (status.upper(), os.path.basename(path), detail),
                      file=sys.stderr)

    print("gen_sfx_ogg: %d encoded, %d up-to-date, %d bad-header, %d failed"
          % (counts["ok"], counts["skip"], counts["bad"], counts["fail"]))
    if counts["ok"]:
        print("  encoded bytes: %.1f MB pcm -> %.1f MB ogg (%.1f%%)"
              % (in_bytes / 1e6, out_bytes / 1e6, 100.0 * out_bytes / max(1, in_bytes)))
    return 0 if counts["fail"] == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
