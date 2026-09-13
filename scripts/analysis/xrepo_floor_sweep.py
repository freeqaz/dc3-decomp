#!/usr/bin/env python3
"""Run `xrepo_audit` across SIBLINGS x SIMILARITY FLOORS and state the denominator.

WHY THIS EXISTS
---------------
`xrepo_audit.py` has a `--min-sim` cut, and a cut you cannot see is a cut you
inherit.  The default 0.60 was demonstrably hiding real defects: lowering it to
0.40 against og-dc3-decomp alone added 21 high-precision functions, and
`TrigTableInit` -- which wrote index 513 of a 512-float table -- sits at
sim 0.457 and was never in the candidate set at 0.60.

This driver answers the three questions a floor choice needs:
  1. per sibling, how many HIGH-PRECISION candidates each floor admits;
  2. how many of those JOIN to report.json (i.e. are functions the binary
     actually scores) and how many sit below 99.9% there;
  3. what the marginal band at each step looks like, so the floor is chosen off
     a curve rather than picked silently.

IN-PASS CONTROL  (the reason a null here is reportable at all)
--------------------------------------------------------------
The benign majority is the control.  Two counts gate every population:
  * `identical` -- pairs whose token streams are EQUAL.  These are the shared
    engine converging, and there are thousands of them.  If a run returns ZERO,
    the extractor or the pairing is broken and the tree is not clean.
  * `paired`    -- names present in both trees at all.
`--check-control` refuses (exit 4) to print a population while either is empty,
because an empty result from a broken instrument reads exactly like an
exhausted class.  `--selftest` additionally proves the detectors still fire.

USAGE
-----
  python3 scripts/analysis/xrepo_floor_sweep.py --floors 0.6 0.4 0.3 0.2
  python3 scripts/analysis/xrepo_floor_sweep.py --floors 0.4 --json /tmp/o.json
"""
import argparse
import difflib
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xrepo_audit as XA  # noqa: E402

SIBLINGS = ['og-dc3-decomp', 'rb3-xenon', 'rb3']

# The detectors that assert a SPECIFIC semantic difference.  ARITH_OP / BITOP /
# LOGIC_OP are excluded: on a token stream they fire on ordinary rewrites
# (`a + -b` vs `a - b`) far too often to rank on.
HI = {'CONST_SIGN', 'CONST_FACTOR2', 'ARG_SWAP', 'UNARY_MINUS_DC3_ONLY',
      'UNARY_MINUS_SIB_ONLY', 'CMP_DIR', 'EQ_POLARITY', 'NOT_DC3_ONLY',
      'NOT_SIB_ONLY', 'CONST_FLOAT_NEAR'}


# --------------------------------------------------------------------------
# report.json join
# --------------------------------------------------------------------------
def load_report(path):
    """{(unit_rel, 'Class::Method'): (normalized_pct, size, mangled)}.

    `unit_rel` is the unit name with the `default/system/` prefix stripped, so
    it lines up with xrepo_audit's paths relative to `src/system`."""
    with open(path) as f:
        rep = json.load(f)
    out = {}
    for u in rep['units']:
        un = u['name']
        if not un.startswith('default/system/'):
            continue
        rel = un[len('default/system/'):]
        for fn in u.get('functions') or ():
            dem = (fn.get('metadata') or {}).get('demangled_name') or ''
            q = qualified_from_demangled(dem)
            if not q:
                continue
            pct = fn.get('match_percent_normalized')
            key = (rel, q)
            # keep the WORST-scoring instance of a name: an overload set joins
            # to one source key, and reporting the best would hide the defect
            if key not in out or (pct is not None and pct < out[key][0]):
                out[key] = (pct, int(fn.get('size') or 0), fn['name'])
    return out


_CALLCONV = re.compile(r'\b__(cdecl|thiscall|stdcall|fastcall)\b')


def qualified_from_demangled(dem):
    """'public: void __thiscall CharBones::Poll(void)' -> 'CharBones::Poll'."""
    if not dem:
        return None
    m = _CALLCONV.search(dem)
    s = dem[m.end():] if m else dem
    # cut at the argument list -- the first '(' at angle-bracket depth 0
    depth, cut = 0, len(s)
    for i, c in enumerate(s):
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
        elif c == '(' and depth == 0:
            cut = i
            break
    s = s[:cut].strip()
    if not s or ' ' in s.split('::')[-1]:
        return None
    # drop template arguments: the source side writes the primary name
    s = re.sub(r'<[^<>]*>', '', s)
    while '<' in s:
        s2 = re.sub(r'<[^<>]*>', '', s)
        if s2 == s:
            break
        s = s2
    return s or None


def source_key(func):
    """xrepo_audit's key -> the name report.json would carry ('#a2' dropped)."""
    return func.split('#')[0]


# --------------------------------------------------------------------------
# sweep
# --------------------------------------------------------------------------
def sweep(sibroot, subdir, floor):
    """One sibling at one floor.  Returns (stats, results)."""
    rels = XA.shared_files(sibroot, subdir)
    st = Counter()
    results = []
    for rel in rels:
        try:
            f1 = XA.load(os.path.join(XA.DC3_ROOT, subdir, rel))
            f2 = XA.load(os.path.join(sibroot, subdir, rel))
        except Exception:
            st['file_err'] += 1
            continue
        st['files'] += 1
        for name, b1 in f1.items():
            if name not in f2:
                st['unpaired'] += 1
                continue
            b2 = f2[name]
            st['paired'] += 1
            t1, t2 = XA.tokenize(b1), XA.tokenize(b2)
            if len(t1) < 10 or len(t2) < 10:
                st['tiny'] += 1
                continue
            sim = difflib.SequenceMatcher(a=t1, b=t2, autojunk=False).ratio()
            if sim >= 0.99999:
                st['identical'] += 1
                continue
            if sim < floor:
                st['too_different'] += 1
                continue
            st['comparable'] += 1
            finds = XA.compare_bodies(b1, b2)
            if not finds:
                continue
            st['divergent'] += 1
            kinds = {k for k, _ in finds}
            if kinds & HI:
                st['hi_precision'] += 1
            results.append(dict(file=rel, func=name, sim=round(sim, 3), ntok=len(t1),
                                hi=bool(kinds & HI),
                                score=round(sum(XA.PRIO.get(k, 0) for k, _ in finds) * sim, 1),
                                finds=finds))
    results.sort(key=lambda r: -r['score'])
    return st, results


# --------------------------------------------------------------------------
# NOISE CONTROL -- shuffled pairing
# --------------------------------------------------------------------------
def noise_control(sibroot, subdir, bands, seed=1234, trials=400000):
    """Empirical false-positive rate per similarity band, by DECOY PAIRING.

    Pair a dc3 function against a sibling function from a DIFFERENT FILE with a
    DIFFERENT NAME.  Such a pair is unrelated by construction, so any
    high-precision finding it produces is noise -- two constants that happen to
    sit a factor of two apart, a `!` that lands inside a big replace hunk.
    Bucketing by similarity gives P(finding | sim >= floor, unrelated), which is
    what "how much of the marginal band is noise" actually asks.

    This is a measurement, not a prior.  Its own control is the decoy COUNT: a
    band with no decoys admits no estimate, and is reported as such rather than
    as 0% noise."""
    import random
    rng = random.Random(seed)
    rels = XA.shared_files(sibroot, subdir)
    pool_a, pool_b = [], []
    for rel in rels:
        try:
            f1 = XA.load(os.path.join(XA.DC3_ROOT, subdir, rel))
            f2 = XA.load(os.path.join(sibroot, subdir, rel))
        except Exception:
            continue
        for nm, b in f1.items():
            t = XA.tokenize(b)
            if len(t) >= 10:
                pool_a.append((rel, nm, b, t))
        for nm, b in f2.items():
            t = XA.tokenize(b)
            if len(t) >= 10:
                pool_b.append((rel, nm, b, t))
    lo = min(bands)
    tot = Counter()
    hit = Counter()
    if not pool_a or not pool_b:
        return {}, 0
    for _ in range(trials):
        ra, na, ba, ta = pool_a[rng.randrange(len(pool_a))]
        rb, nb, bb, tb = pool_b[rng.randrange(len(pool_b))]
        if ra == rb or source_key(na) == source_key(nb):
            continue
        # cheap length prefilter: |len| ratio bounds the ratio() from above
        if min(len(ta), len(tb)) < lo * max(len(ta), len(tb)):
            continue
        sm = difflib.SequenceMatcher(a=ta, b=tb, autojunk=False)
        if sm.real_quick_ratio() < lo or sm.quick_ratio() < lo:
            continue
        sim = sm.ratio()
        if sim < lo or sim >= 0.99999:
            continue
        band = max(b for b in bands if sim >= b)
        tot[band] += 1
        if {k for k, _ in XA.compare_bodies(ba, bb)} & HI:
            hit[band] += 1
    return {b: (hit[b], tot[b]) for b in bands}, sum(tot.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--siblings', nargs='+', default=SIBLINGS)
    ap.add_argument('--floors', nargs='+', type=float, default=[0.6, 0.4, 0.3, 0.2])
    ap.add_argument('--subdir', default='src/system')
    ap.add_argument('--report', default=None,
                    help='report.json (default: <tree>/build/373307D9/report.json)')
    ap.add_argument('--json', help='write the full per-floor results here')
    ap.add_argument('--noise', action='store_true',
                    help='also run the shuffled-pairing decoy control per band')
    ap.add_argument('--noise-trials', type=int, default=400000)
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    if args.selftest:
        return XA.selftest()
    if XA.selftest(verbose=False) != 0:
        print('xrepo_audit selftest FAILED -- refusing to sweep', file=sys.stderr)
        return 5

    report_path = args.report or os.path.join(XA.DC3_ROOT, 'build', '373307D9', 'report.json')
    rep = load_report(report_path)
    print(f'report.json: {report_path}  ({len(rep)} src/system name keys)')
    if not rep:
        print('CONTROL EMPTY: report.json join produced no keys -- refusing to report',
              file=sys.stderr)
        return 4

    out = {}
    for sib in args.siblings:
        sibroot = os.path.join(XA.SIBLING_PARENT, sib)
        if not os.path.isdir(sibroot):
            print(f'  skip {sib}: no such tree', file=sys.stderr)
            continue
        print(f'\n{"=" * 96}\n{sib}')
        print(f'{"floor":>6} {"paired":>7} {"identical":>10} {"comparable":>11} '
              f'{"divergent":>10} {"hi-prec":>8} {"joined":>7} {"<99.9%":>7} {"new@floor":>10}')
        prev_hi = set()
        for floor in sorted(args.floors, reverse=True):
            st, res = sweep(sibroot, args.subdir, floor)
            if not st['identical'] or not st['paired']:
                print(f'\nCONTROL EMPTY for {sib} (paired={st["paired"]}, '
                      f'identical={st["identical"]}) -- the extractor or the pairing is '
                      f'broken, NOT the trees clean.  Refusing to report.', file=sys.stderr)
                return 4
            hi = [r for r in res if r['hi']]
            joined = below = 0
            for r in hi:
                unit = os.path.splitext(r['file'])[0]
                k = (unit, source_key(r['func']))
                ent = rep.get(k)
                r['report'] = None
                if ent:
                    joined += 1
                    r['report'] = dict(pct=ent[0], size=ent[1], symbol=ent[2])
                    if ent[0] is not None and ent[0] < 99.9:
                        below += 1
            ids = {(r['file'], r['func']) for r in hi}
            newly = len(ids - prev_hi) if prev_hi else 0
            prev_hi = prev_hi | ids
            print(f'{floor:>6.2f} {st["paired"]:>7} {st["identical"]:>10} '
                  f'{st["comparable"]:>11} {st["divergent"]:>10} {len(hi):>8} '
                  f'{joined:>7} {below:>7} {newly:>10}')
            out.setdefault(sib, {})[f'{floor:.2f}'] = dict(stats=dict(st), hi=hi)

        if args.noise:
            bands = sorted(args.floors, reverse=True)
            rates, ndec = noise_control(sibroot, args.subdir, bands,
                                        trials=args.noise_trials)
            print(f'  decoy control ({ndec} unrelated pairs above {min(bands):.2f}):')
            for b in bands:
                h, t = rates.get(b, (0, 0))
                lbl = f'{h}/{t} = {100.0 * h / t:.1f}%' if t else 'no decoys -- NO ESTIMATE'
                print(f'    sim band [{b:.2f}, {"1.00" if b == bands[0] else f"{bands[bands.index(b) - 1]:.2f}"}): '
                      f'P(hi-prec finding | unrelated) = {lbl}')
            out.setdefault(sib, {})['noise'] = {f'{b:.2f}': rates.get(b, (0, 0)) for b in bands}

    if args.json:
        with open(args.json, 'w') as f:
            json.dump(out, f, indent=1)
        print(f'\nwrote {args.json}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
