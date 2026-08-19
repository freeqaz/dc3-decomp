#!/usr/bin/env python3
"""Find statics we drop the initializer for: ours lands in .bss, the target's has content.

dc3-decomp (title 373307D9).  A `static float sZoom;` at namespace/class scope
lands in our .obj's `.bss` (IMAGE_SCN_CNT_UNINITIALIZED_DATA) and reads 0 at
runtime.  If the shipped image defines the same symbol with NONZERO bytes, our
declaration dropped a static initializer and the game starts that variable at
the wrong value.

This class is INVISIBLE to objdiff, which scores instruction streams and never
asks what a static's initial bytes were.  It is a pure behaviour bug, so the
match percentage will not move when you fix one.

Note the target objects are dtk splits of the image, so *every* section carries
raw bytes -- including the one named `.bss`.  The discriminator is therefore the
CONTENT (nonzero), never the section name.

Usage:
    python3 scripts/analysis/bss_initializer_scan.py
    python3 scripts/analysis/bss_initializer_scan.py --min-size 1 --max-size 4096
"""
import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coffx  # noqa: E402

IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040


def load(path):
    try:
        data = open(path, 'rb').read()
    except OSError:
        return None, None
    return coffx.read_coff(data)


def defined_symbols(path):
    """name -> (section, value, size) for symbols with a real section."""
    secs, syms = load(path)
    if not secs:
        return {}
    coffx.infer_sizes(secs, syms)
    out = {}
    for s in syms:
        if not s.name or s.sec is None or s.sec <= 0 or s.sec > len(secs):
            continue
        if s.cls not in (coffx.IMAGE_SYM_CLASS_EXTERNAL, coffx.IMAGE_SYM_CLASS_STATIC):
            continue
        sec = secs[s.sec - 1]
        if sec.is_code:
            continue
        out.setdefault(s.name, (sec, s.value, getattr(s, 'size', 0) or 0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default=os.getcwd())
    ap.add_argument('--max-size', type=int, default=4096,
                    help='ignore target symbols larger than this (default 4096)')
    a = ap.parse_args()

    ours_root = os.path.join(a.project, 'build/373307D9/src')
    tgt_root = os.path.join(a.project, 'build/373307D9/obj')

    hits = []
    scanned = 0
    for op in sorted(glob.glob(os.path.join(ours_root, '**', '*.obj'), recursive=True)):
        rel = os.path.relpath(op, ours_root)
        tp = os.path.join(tgt_root, rel)
        if not os.path.exists(tp):
            continue
        scanned += 1
        ours = defined_symbols(op)
        tgt = None
        for name, (sec, val, size) in ours.items():
            if not (sec.chars & IMAGE_SCN_CNT_UNINITIALIZED_DATA):
                continue
            if tgt is None:
                tgt = defined_symbols(tp)
            if name not in tgt:
                continue
            tsec, tval, tsize = tgt[name]
            if not (tsec.chars & IMAGE_SCN_CNT_INITIALIZED_DATA):
                continue
            n = tsize or size or 4
            if n > a.max_size:
                continue
            blob = tsec.data[tval:tval + n]
            if not blob or not any(blob):
                continue
            hits.append((rel, name, tsec.name, tval, blob))

    print(f"scanned {scanned} object pairs; {len(hits)} statics land in .bss "
          f"but have NONZERO content in the shipped image\n")
    for rel, name, secname, val, blob in sorted(hits):
        show = blob[:32].hex()
        print(f"{rel}\n   {name}\n   target {secname}+0x{val:x}  {len(blob)}B  {show}"
              f"{'...' if len(blob) > 32 else ''}\n")
    return 0


if __name__ == '__main__':
    sys.exit(main())
