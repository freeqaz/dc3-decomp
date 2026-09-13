#!/usr/bin/env python3
"""Float-literal oracle: probe a TARGET OBJECT's bytes for a literal's encoding.

PROVENANCE
----------
Written 2026-09-13 as `/tmp/float_oracle.py` + `/tmp/float_oracle2.py` during the
cross-repo drift lane; `/tmp` is tmpfs on this box and does not survive a reboot,
so the two are merged here, self-contained.  Rescued via
/home/free/tmp/preserved-instruments-20260913/.  In its first pass it found a
real 5th-significant-digit divergence in `MicXbox::Poll` and CLEARED a false one
in the same run -- both invisible to every objdiff ruler.

The `/tmp` originals did `sys.path.insert(0, '/tmp')`, imported a second `/tmp`
module, and read `/tmp/audit2_<sib>.json` for three hardcoded siblings.  All of
that is gone: the helper is imported as a sibling module, and every input is an
argument.

WHY THIS EXISTS -- the class no ruler can see
---------------------------------------------
A float literal is emitted into a constant pool and reached through a relocation.
The linker map names such an address only with a PLACEHOLDER (`lbl_<addr>`), and
`name_check` EXEMPTS placeholder-named relocation targets before the wrong-target
detector runs.  `normalized` ignores relocations entirely.  So the literal's
VALUE is never compared by any ruler:

    rb3-xenon `CharEyes::ProceduralBlinkUpdate` shipped 5.405405f where retail's
    `lbl_82045624` holds 40 AC F9 15 == 5.4054055f.  objdiff read 100.0%,
    103/103 equal, EITHER WAY.

The only instrument that settles it is reading the four bytes.

WHAT IT CAN SEE
---------------
  * A float literal spelled in our source whose exact big-endian float32 bit
    pattern is ABSENT from the target object that TU compiles against.  That is
    a strong "we invented this constant" signal.
  * With `--from-audit`, the cross-repo form: for every function where
    `xrepo_audit.py` flagged a constant divergence, probe dc3-only and
    sibling-only values.  dc3-only ABSENT while a sibling-only value is PRESENT
    is `DC3_SUSPECT`.

WHAT IT CANNOT SEE  (say all of this before quoting a clean run)
-----------------------------------------------------------------
  * PRESENCE IS NOT PROOF, AND THE MATCH IS NOT POOL-SCOPED.  The probe searches
    the whole object file.  It reports the SECTION and FILE OFFSET of every hit
    so a coincidental match inside `.text`/`.pdata` is legible rather than
    silently counted, but a hit in a data section still only means "these four
    bytes exist somewhere", not "the site under study loads this word".
    **ABSENCE is the load-bearing verdict; presence is at most a non-refutation.**
  * INTEGRAL CONSTANTS.  Values with `v == int(v)` and `|v| < 65536` are skipped
    entirely, because MSVC materialises those as immediates rather than pool
    words, so their absence would carry no information.  The probe is therefore
    STRUCTURALLY BLIND to a wrong integral constant -- a clean run says nothing
    at all about that class.
  * DOUBLES.  float32 only; a `double` literal is 8 bytes and is not probed.
  * A value computed at runtime, constant-folded into an expression, or reached
    through another TU's pool.
  * WHICH TREE IS RIGHT.  A cross-repo divergence is a hypothesis; only this
    binary's own bytes settle it for this binary.
  * Its FUNCTION ATTRIBUTION is approximate.  The brace-matcher in
    `xrepo_audit.extract_functions` sometimes clips the first character of a
    qualified name and disambiguates overloads as `f#N`, so the row label tells
    you roughly where in the file to look, not exactly which overload.  The
    literal, its bytes and the section hit are exact; the name beside them is a
    signpost.

PORTABILITY -- THE METHOD TRAVELS, THE PATHS DO NOT
---------------------------------------------------
This adjudicates against **dc3-decomp's** target objects (title `373307D9`).
Pointing it at another tree's source without also re-pointing `--obj-root` would
answer a question about the WRONG BINARY -- exactly the error that produced a
false cross-repo lead on 2026-09-13.  `--obj-root` and `--title` are explicit and
there is deliberately no cross-repo default: to run this against `rb3-xenon`,
pass that tree's own object root, and read the result as a statement about that
binary only.

CONTROLS
--------
`--selftest` runs four controls and exits non-zero if any fails:
  1. ENDIANNESS (synthetic): a value packed big-endian into a buffer must be
     found; the same value packed ONLY little-endian must NOT be.  This is the
     control that matters -- a probe packing the wrong way round sails through
     any round-trip test and reports the whole world ABSENT.
  2. REAL FILE PRESENT: a 4-byte window lifted out of an actual target object
     must be found, and resolve to the section it came from.
  3. REAL FILE ABSENT: a pattern verified absent must be reported absent.
  4. VACUITY: the object root and objdiff.json mapping must exist and be
     non-empty (exit 9).
Additionally, every reporting pass carries an IN-PASS POSITIVE CONTROL: if a run
prints ABSENT verdicts but never once observed a PRESENT one, the probe has
proven nothing -- most likely a wrong endianness or a wrong object root -- and
the run exits 6 as vacuous rather than reporting a clean sweep.

USAGE
-----
  python3 scripts/analysis/float_oracle.py --selftest
  python3 scripts/analysis/float_oracle.py --value 5.405405 --source src/system/char/CharEyes.cpp
  python3 scripts/analysis/float_oracle.py --scan-source src/system/mic/MicXbox.cpp
  python3 scripts/analysis/float_oracle.py --from-audit a.json --sibling rb3-xenon
"""
import argparse
import json
import os
import random
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coffx import read_coff  # noqa: E402
from xrepo_audit import (DC3_ROOT, SIBLING_PARENT, drop_native_blocks,  # noqa: E402
                         extract_functions, strip_comments)

DEFAULT_TITLE = '373307D9'

# Float literals worth probing.  See "WHAT IT CANNOT SEE": integral values under
# 65536 are immediates, not pool words, and are dropped by `lits()`.
FLIT = re.compile(r'(?<![\w.])-?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?f(?![\w.])'
                  r'|(?<![\w.])-?(?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?(?![\w.f])')

CONST_KINDS = {'CONST_SIGN', 'CONST_FACTOR2', 'CONST_FLOAT_NEAR', 'CONST_DIFF'}


# --------------------------------------------------------------------------
# the probe
# --------------------------------------------------------------------------
def be_bytes(value):
    """Big-endian IEEE-754 float32 encoding.  Xbox 360 is big-endian; packing
    little-endian here is the single most likely way to make this tool silently
    report everything ABSENT -- hence selftest control 1."""
    return struct.pack('>f', value)


class Hit:
    __slots__ = ('offset', 'section', 'sec_offset', 'in_code')

    def __init__(self, offset, section, sec_offset, in_code):
        self.offset, self.section = offset, section
        self.sec_offset, self.in_code = sec_offset, in_code

    def __repr__(self):
        return f'{self.section}+0x{self.sec_offset:x}'


def _section_index(blob):
    """[(rawptr, rawend, name, is_code)] for the object's raw section ranges."""
    secs, _ = read_coff(blob)
    if not secs:
        return []
    return [(s.rawptr, s.rawptr + s.rawsize, s.name, s.is_code)
            for s in secs if s.rawptr and s.rawsize]


def probe(blob, value, secindex=None):
    """Every occurrence of `value`'s big-endian float32 bytes, with section
    attribution.  Returns [Hit, ...]."""
    pat = be_bytes(value)
    hits = []
    start = 0
    while True:
        off = blob.find(pat, start)
        if off < 0:
            break
        sec, secoff, code = '?', off, False
        for a, b, name, is_code in (secindex or []):
            if a <= off < b:
                sec, secoff, code = name, off - a, is_code
                break
        hits.append(Hit(off, sec, secoff, code))
        start = off + 1
    return hits


def fmt_hits(hits, limit=4):
    if not hits:
        return 'none'
    body = ', '.join(repr(h) for h in hits[:limit])
    return body + (f' (+{len(hits) - limit} more)' if len(hits) > limit else '')


def data_hits(hits):
    """Hits outside executable sections -- the ones that could be a pool word."""
    return [h for h in hits if not h.in_code]


# --------------------------------------------------------------------------
# source -> target object mapping
# --------------------------------------------------------------------------
class Objects:
    """The target-object side of one decomp tree.  Explicit, never inferred
    across repos."""

    def __init__(self, repo_root, obj_root, title):
        self.repo_root = repo_root
        self.title = title
        self.obj_root = obj_root or os.path.join(repo_root, 'build', title, 'obj')
        self._map = None
        self._blobs = {}
        self._secs = {}

    def unit_map(self):
        """{source_path: target_obj_abspath} from this tree's objdiff.json,
        with target paths re-anchored onto `obj_root`."""
        if self._map is None:
            with open(os.path.join(self.repo_root, 'objdiff.json')) as f:
                cfg = json.load(f)
            default_root = os.path.join(self.repo_root, 'build', self.title, 'obj')
            self._map = {}
            for u in cfg.get('units', []):
                src = (u.get('metadata') or {}).get('source_path')
                tgt = u.get('target_path')
                if not (src and tgt):
                    continue
                abs_t = os.path.join(self.repo_root, tgt)
                rel = os.path.relpath(abs_t, default_root)
                self._map[src] = os.path.normpath(os.path.join(self.obj_root, rel))
        return self._map

    def blob(self, source_path):
        p = self.unit_map().get(source_path)
        if p is None:
            return None, None
        if p not in self._blobs:
            self._blobs[p] = open(p, 'rb').read() if os.path.exists(p) else None
            self._secs[p] = _section_index(self._blobs[p]) if self._blobs[p] else []
        return self._blobs[p], self._secs[p]

    def rel(self, source_path):
        p = self.unit_map().get(source_path)
        return os.path.relpath(p, self.repo_root) if p else '?'


def lits(body):
    """{value: source_spelling} for probe-worthy float literals in a body."""
    out = {}
    for m in FLIT.finditer(body):
        t = m.group(0)
        try:
            v = float(t.rstrip('f'))
        except ValueError:
            continue
        if v == int(v) and abs(v) < 65536:
            continue  # integral -> immediate, not a pool word (documented blind spot)
        out.setdefault(v, t)
    return out


def load_funcs(path):
    with open(path, 'r', errors='replace') as f:
        return extract_functions(drop_native_blocks(strip_comments(f.read())))


# --------------------------------------------------------------------------
# in-pass positive control
# --------------------------------------------------------------------------
class PassControl:
    """A reporting pass may only publish ABSENT verdicts if it saw at least one
    PRESENT one.  All-negative is indistinguishable from a broken probe."""

    def __init__(self):
        self.present = 0
        self.absent = 0

    def note(self, hits):
        if hits:
            self.present += 1
        else:
            self.absent += 1

    def finish(self):
        if self.absent and not self.present:
            print(f'\nVACUOUS: {self.absent} probe(s), ZERO of them present. A pass that never')
            print('observes a hit has not demonstrated it can find one -- most likely a wrong')
            print('object root or wrong endianness. Refusing to report this as a clean sweep.')
            return 6
        print(f'\nin-pass control: {self.present} present / {self.absent} absent '
              f'({self.present + self.absent} probes) -- positive control fired.'
              if self.present else
              f'\nin-pass control: {self.present} present / {self.absent} absent.')
        return 0


# --------------------------------------------------------------------------
# CONTROLS
# --------------------------------------------------------------------------
def selftest(objs, verbose=True):
    def say(ok, label, detail=''):
        if verbose:
            print(f'  [{"PASS" if ok else "FAIL"}] {label:34} {detail}')
        return ok

    failures = []
    try:
        um = objs.unit_map()
    except OSError as e:
        print(f'VACUOUS: cannot read objdiff.json ({e}). Exit 9.')
        return 9
    src = objpath = None
    for s, p in sorted(um.items()):
        if os.path.exists(p) and os.path.getsize(p) > 4096:
            src, objpath = s, p
            break
    if not um or objpath is None:
        print(f'VACUOUS: no target objects under {objs.obj_root}. `dtk xex split` has not run,')
        print('so every probe would report ABSENT for the wrong reason. Exit 9.')
        return 9
    say(True, 'vacuity: target objects present',
        f'{len(um)} units, sample {os.path.basename(objpath)}')

    v = 5.4054055
    buf_be = b'\x00' * 7 + struct.pack('>f', v) + b'\x00' * 7
    buf_le = b'\x00' * 7 + struct.pack('<f', v) + b'\x00' * 7
    nbe, nle = len(probe(buf_be, v)), len(probe(buf_le, v))
    if not say(nbe == 1 and nle == 0, 'endianness: BE found, LE not found', f'be={nbe} le={nle}'):
        failures.append('endianness control: the probe is not reading big-endian float32')

    blob = open(objpath, 'rb').read()
    secindex = _section_index(blob)
    if not say(bool(secindex), 'coff: sections parsed', f'{len(secindex)} raw sections'):
        failures.append('section control: COFF section table unreadable, hits cannot be attributed')

    # lift the control value from INSIDE a real section, so a hit must resolve
    # to a named section -- otherwise a broken section index would read PASS.
    present_val = present_sec = None
    for a, b, name, _is_code in secindex:
        for off in range(a, min(b, a + 200000) - 4, 4):
            f = struct.unpack('>f', blob[off:off + 4])[0]
            if f == f and 1e-6 < abs(f) < 1e6 and f != int(f):
                present_val, present_sec = f, name
                break
        if present_val is not None:
            break
    ph = probe(blob, present_val, secindex) if present_val is not None else []
    attributed = any(h.section == present_sec for h in ph)
    if not say(bool(ph) and attributed, 'real object: known-present found + attributed',
               f'{present_val!r} in {present_sec} -> {fmt_hits(ph)}'):
        failures.append('present control: probe missed a value lifted out of the object itself, '
                        'or could not attribute the hit to the section it came from')

    rnd = random.Random(0xDC3)
    absent_val = None
    for _ in range(4000):
        w = bytes(rnd.randrange(256) for _ in range(4))
        if w in blob:
            continue
        f = struct.unpack('>f', w)[0]
        if f != f:
            continue
        absent_val = f
        break
    ah = probe(blob, absent_val, secindex) if absent_val is not None else [1]
    if not say(not ah, 'real object: known-absent reported absent', repr(absent_val)):
        failures.append('absent control: probe reports a pattern present that is not in the file')

    if failures:
        print('\nSELFTEST FAILED -- this oracle cannot distinguish present from absent, so any')
        print('verdict it prints is VACUOUS. Refusing to present it as a measurement.')
        for f in failures:
            print('  ' + f)
        return 5
    if verbose:
        print('\nSELFTEST OK: endianness pinned, sections resolvable, present and absent'
              ' both demonstrable.')
    return 0


# --------------------------------------------------------------------------
# modes
# --------------------------------------------------------------------------
def mode_value(objs, value, source):
    blob, secindex = objs.blob(source)
    if blob is None:
        print(f'no target object for {source} under {objs.obj_root}')
        return 9
    hits = probe(blob, value, secindex)
    dh = data_hits(hits)
    print(f'{value!r}  bytes={be_bytes(value).hex()}  obj={objs.rel(source)}')
    print(f'  hits: {len(hits)} total, {len(dh)} outside executable sections')
    for h in hits:
        print(f'    file+0x{h.offset:06x}  {h.section}+0x{h.sec_offset:x}'
              f'{"  (CODE -- coincidental match, not a pool word)" if h.in_code else ""}')
    print('PRESENT in a data section (not refuted)' if dh else
          ('PRESENT only inside code (no pool word -- treat as ABSENT)' if hits else
           'ABSENT -- retail never spells this constant in this TU'))
    return 0


def mode_scan_source(objs, source):
    blob, secindex = objs.blob(source)
    if blob is None:
        print(f'no target object for {source} under {objs.obj_root}')
        return 9
    funcs = load_funcs(os.path.join(objs.repo_root, source))
    print(f'# {source}  vs  {objs.rel(source)}   ({len(funcs)} function bodies)')
    print(f'{"verdict":8} {"function":38} {"literal":>16}  bytes      where')
    ctl = PassControl()
    for fn, body in sorted(funcs.items()):
        for v, t in sorted(lits(body).items()):
            hits = probe(blob, v, secindex)
            dh = data_hits(hits)
            ctl.note(dh)
            print(f'{"ABSENT" if not dh else "present":8} {fn[:38]:38} {t:>16}  '
                  f'{be_bytes(v).hex()}  {fmt_hits(dh)}')
    return ctl.finish()


def mode_from_audit(objs, audit_path, sibling, sibroot=None):
    with open(audit_path) as f:
        audit = json.load(f)
    sibroot = sibroot or os.path.join(SIBLING_PARENT, sibling)
    rows = defaultdict(list)
    considered = skipped_noobj = skipped_nofn = 0
    ctl = PassControl()
    for r in audit['results']:
        if not any(k in CONST_KINDS for k, _ in r['finds']):
            continue
        considered += 1
        source = os.path.normpath(os.path.join('src/system', r['file']))
        blob, secindex = objs.blob(source)
        if blob is None:
            skipped_noobj += 1
            continue
        try:
            a = load_funcs(os.path.join(objs.repo_root, source))[r['func']]
            b = load_funcs(os.path.join(sibroot, source))[r['func']]
        except (KeyError, OSError):
            skipped_nofn += 1
            continue
        la, lb = lits(a), lits(b)
        for side, only in (('dc3-only', {k: x for k, x in la.items() if k not in lb}),
                           ('sib-only', {k: x for k, x in lb.items() if k not in la})):
            for v, t in sorted(only.items()):
                dh = data_hits(probe(blob, v, secindex))
                ctl.note(dh)
                rows[(r['file'], r['func'])].append((side, v, t, dh))

    print(f'DENOMINATOR: {considered} const-divergent functions considered, '
          f'{skipped_noobj} no target obj, {skipped_nofn} body not re-extractable, '
          f'{len(rows)} with probe-worthy literals')
    nsus = 0
    for (f, fn), rs in sorted(rows.items()):
        dc3_absent = [x for x in rs if x[0] == 'dc3-only' and not x[3]]
        sib_present = [x for x in rs if x[0] == 'sib-only' and x[3]]
        if dc3_absent and sib_present:
            flag = 'DC3_SUSPECT'
            nsus += 1
        elif not dc3_absent:
            continue
        else:
            flag = 'inconclusive'
        print(f'\n== {flag}  {f} :: {fn}')
        for side, v, t, dh in rs:
            print(f'     {side:9} {t:>16} = {v:<18} {fmt_hits(dh)}')
    print(f'\n{nsus} DC3_SUSPECT function(s).')
    return ctl.finish()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--repo-root', default=DC3_ROOT,
                    help='decomp tree whose objdiff.json and SOURCE are read (default: dc3-decomp)')
    ap.add_argument('--obj-root',
                    help='TARGET object root. Defaults to <repo-root>/build/<title>/obj. '
                         'There is deliberately no cross-repo default: adjudicating one tree\'s '
                         'source against another tree\'s objects answers about the wrong binary.')
    ap.add_argument('--title', default=DEFAULT_TITLE)
    ap.add_argument('--value', type=float, help='probe one value')
    ap.add_argument('--source', help='source path (with --value)')
    ap.add_argument('--scan-source', help='probe every float literal in one source file')
    ap.add_argument('--from-audit', help='xrepo_audit.py --out JSON')
    ap.add_argument('--sibling', help='sibling tree name (with --from-audit)')
    ap.add_argument('--sibling-root', help='explicit sibling tree path (overrides --sibling)')
    args = ap.parse_args()

    objs = Objects(args.repo_root, args.obj_root, args.title)
    if args.selftest:
        return selftest(objs)
    rc = selftest(objs, verbose=False)
    if rc:
        print(f'selftest returned {rc}; refusing to report. Run --selftest for detail.',
              file=sys.stderr)
        return rc

    if args.value is not None:
        if not args.source:
            ap.error('--value requires --source')
        return mode_value(objs, args.value, args.source)
    if args.scan_source:
        return mode_scan_source(objs, args.scan_source)
    if args.from_audit:
        if not (args.sibling or args.sibling_root):
            ap.error('--from-audit requires --sibling or --sibling-root')
        return mode_from_audit(objs, args.from_audit, args.sibling, args.sibling_root)
    ap.error('pick a mode: --selftest / --value / --scan-source / --from-audit')


if __name__ == '__main__':
    sys.exit(main())
