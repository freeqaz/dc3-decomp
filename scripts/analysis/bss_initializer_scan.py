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

A ZERO FROM THIS SCAN IS NOT AN EXHAUSTION PROOF.  It joins our symbols to the
target's BY NAME, within a matched object pair, and that join is structurally
blind two ways -- both of which real bugs have already walked through:

  * the target side is often an UNNAMED `lbl_<addr>` (dtk names function-local
    statics that way), so there is no name to join on.  `Game::Poll::sLastBeat`
    escaped here and was found by mutable_float_audit.py instead.
  * OUR definition may sit in a different TU than the target's, and the pairing
    is per-object, so the two never meet.  `GainEffect::sGain` escaped here.

Read the COVERAGE block: the `no-name-match-in-paired-target` drop is that
population, and on the current tree it is the large majority of our .bss
symbols.  See docs/decomp/patterns/dropped-static-initializer.md.

Usage:
    python3 scripts/analysis/bss_initializer_scan.py
    python3 scripts/analysis/bss_initializer_scan.py --max-size 4096
"""
import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coffx  # noqa: E402
from coverage import CoverageReport, add_coverage_args  # noqa: E402

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
    add_coverage_args(ap)
    a = ap.parse_args()

    ours_root = os.path.join(a.project, 'build/373307D9/src')
    tgt_root = os.path.join(a.project, 'build/373307D9/obj')

    cov = CoverageReport('bss_initializer_scan', args=a)
    cov.require_examined('no .bss static was comparable against a target symbol')

    # PASS 1 -- the denominator, computed independently of any disposition.
    # Counting it inside the classification loop would make the arithmetic
    # check vacuous (universe would equal examined+drops by construction),
    # which is the exit-4 bypass coverage.py exists to prevent.
    pairs = []
    n_obj_unpaired = 0
    universe = 0
    for op in sorted(glob.glob(os.path.join(ours_root, '**', '*.obj'), recursive=True)):
        rel = os.path.relpath(op, ours_root)
        tp = os.path.join(tgt_root, rel)
        if not os.path.exists(tp):
            n_obj_unpaired += 1
            continue
        ours = defined_symbols(op)
        bss = {n: v for n, v in ours.items()
               if v[0].chars & IMAGE_SCN_CNT_UNINITIALIZED_DATA}
        universe += len(bss)
        pairs.append((rel, tp, bss))
    cov.universe(universe, 'defined symbols WE place in .bss, across paired objects')

    # PASS 2 -- classify. Every discard is counted.
    hits = []
    for rel, tp, bss in pairs:
        tgt = None
        for name, (sec, val, size) in bss.items():
            if tgt is None:
                tgt = defined_symbols(tp)
            if name not in tgt:
                # THE BLIND SPOT, now counted rather than silent. Two known
                # real bugs escaped exactly here, both AFTER this scan was
                # declared exhausted at 10/10:
                #   * Game::Poll::sLastBeat -- dtk names a function-local
                #     static `lbl_<addr>` on the target side, so there is no
                #     NAME to join on. Found instead by mutable_float_audit
                #     (lbl_82F1A524 = -1, ours .bss zero).  88c3d9c20c
                #   * GainEffect::sGain -- target side IS named, but OUR
                #     definition sat in the wrong TU (Mic.cpp), and this scan
                #     pairs object-by-object, so the two never met.  df13adcd1c
                cov.drop('no-name-match-in-paired-target',
                         note='target-side lbl_* statics, and definitions we put '
                              'in a different TU, are both structurally invisible here')
                continue
            tsec, tval, tsize = tgt[name]
            if not (tsec.chars & IMAGE_SCN_CNT_INITIALIZED_DATA):
                cov.drop('target-sym-not-in-initialized-data')
                continue
            n = tsize or size or 4
            if n > a.max_size:
                cov.drop('capped-by-max-size',
                         note='target symbol larger than --max-size; raise it to include these')
                continue
            blob = tsec.data[tval:tval + n]
            if not blob:
                cov.drop('target-blob-unreadable')
                continue
            cov.examine()
            if not any(blob):
                continue
            hits.append((rel, name, tsec.name, tval, blob))

    if n_obj_unpaired:
        cov.note(f'{n_obj_unpaired} of our objects have no target counterpart; their '
                 f'symbols are outside the universe entirely')
    cov.note('joins BY NAME within a matched object pair -- see the '
             'no-name-match-in-paired-target drop for what that cannot see')
    cov.extra('object_pairs', len(pairs))
    cov.extra('hits', len(hits))

    print(f"scanned {len(pairs)} object pairs; {len(hits)} statics land in .bss "
          f"but have NONZERO content in the shipped image")
    print("(read the COVERAGE block below before calling this class exhausted -- "
          "the object-pair count is NOT this scan's denominator)\n")
    for rel, name, secname, val, blob in sorted(hits):
        show = blob[:32].hex()
        print(f"{rel}\n   {name}\n   target {secname}+0x{val:x}  {len(blob)}B  {show}"
              f"{'...' if len(blob) > 32 else ''}\n")
    return cov.emit()


if __name__ == '__main__':
    sys.exit(main())
