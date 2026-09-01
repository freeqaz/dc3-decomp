#!/usr/bin/env python3
"""Classify cross-repo source drift (dc3-decomp vs a sibling decomp) into buckets.

Rule: a "fix-shaped" divergence is a SMALL, TOKEN-PRESERVING edit -- one side
looks like a corrected version of the other. Constant retuning, extra feature
blocks and whole-function differences are NOT fix-shaped.

PROVENANCE
----------
This is the tracked, reproducible successor to an untracked scratch script
(`/home/free/scratch/drift-audit/classify.py`, 2026-08-31) whose bucket census
lived only in untracked JSON. It carries three fixes; see `--selftest`, and
docs/decomp/patterns/subset-predicates-classify-everything.md.

  D1  EMPTY-DIFFSET SUBSET TEST.  `if diffset <= WHITELIST:` is satisfied by the
      EMPTY SET, so a hunk containing no whitelisted token at all fell into the
      bucket.  Measured on the original census: 85 of 97 COMPARISON_FLIP hunks
      had an empty diffset -- the bucket was inflated ~8x.  SIGN_FLIP had the
      identical shape but already carried `and diffset`, so it was unaffected
      (0 of 7).  Fixed by requiring a non-empty diffset.

  D2  DEAD WHITELIST ENTRIES.  The tokenizer's operator alternative is
      `[^\\sA-Za-z0-9_]` -- exactly ONE non-alphanumeric character -- so it can
      never emit `<=`, `>=`, `==`, `!=`, `&&` or `||`.  Six of the eight
      COMPARISON_FLIP whitelist members were unreachable.  Fixed by tokenizing
      multi-character operators as single tokens.

  D3  AMBIGUOUS `<` / `>`.  Under the old tokenizer `p->x`, `a >> 1` and
      `a << 1` each inject a bare `<`/`>` into the diffset, so pointer and shift
      drift was scored as comparison drift.  3 of the 12 genuine-diffset
      survivors were this artifact.  Fixed by D2's tokenizer.

Modes:
  --selftest        sabotage-checked unit tests of the predicates (exit 1 on fail)
  --legacy          reproduce the ORIGINAL buggy predicates, for an A/B
  --ab              run both rulers over the same input and print a per-bucket delta
"""
import argparse
import collections
import difflib
import json
import os
import re
import subprocess
import sys

# --- tokenizer -------------------------------------------------------------
# D2/D3: multi-character operators must be SINGLE tokens, longest-match first,
# or the comparison whitelist is unreachable and `->`/`>>`/`<<` masquerade as
# comparisons.  Order matters: three-char before two-char before one-char.
_MULTI = [
    '<<=', '>>=', '->*', '...',
    '<<', '>>', '<=', '>=', '==', '!=', '&&', '||', '->', '::', '++', '--',
    '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '.*',
]
TOKEN = re.compile(
    r'[A-Za-z_][A-Za-z0-9_]*'
    r'|\d+\.?\d*'
    r'|' + '|'.join(re.escape(op) for op in _MULTI) +
    r'|[^\sA-Za-z0-9_]'
)
# The original, preserved verbatim so --legacy is a true reproduction.
TOKEN_LEGACY = re.compile(r'[A-Za-z_][A-Za-z0-9_]*|\d+\.?\d*|[^\sA-Za-z0-9_]')

NUM = re.compile(r'\b\d+\.?\d*[fFuUlL]*\b|0[xX][0-9a-fA-F]+')

CMP_OPS = {'<', '>', '<=', '>=', '==', '!=', '&&', '||'}
SIGN_OPS = {'-', '+', '!', '~'}

GUARDY = re.compile(r'\b(if|return|continue|break|MILO_ASSERT|MILO_WARN)\b')
NULLY = re.compile(r'(==\s*(0|NULL|nullptr)|!=\s*(0|NULL|nullptr)|\bif\s*\(\s*!)')


def toks(s, legacy=False):
    return (TOKEN_LEGACY if legacy else TOKEN).findall(s)


def strip_num(s):
    return NUM.sub('#', s)


def norm(lines):
    return [l.rstrip() for l in lines]


def classify_replace(a, b, legacy=False):
    """a,b are equal-ish small line blocks that were replaced.

    Returns a bucket name, or None for the unclassified sink.
    """
    ta, tb = toks(' '.join(a), legacy), toks(' '.join(b), legacy)
    if ta == tb:
        return None                       # whitespace only
    sa, sb = strip_num(' '.join(a)), strip_num(' '.join(b))
    if toks(sa, legacy) == toks(sb, legacy):
        return 'CONST_RETUNE'             # only numeric literals differ -> legit tuning
    if sorted(ta) == sorted(tb):
        return 'OPERAND_REORDER'          # same multiset, different order

    # `diffset` is a SET symmetric difference, so it is blind to multiplicity:
    # a hunk that only changed how many times a token appears (helper arity,
    # statement splitting, log text) yields the EMPTY set.  Every subset test
    # below must therefore assert non-emptiness -- see D1.
    diffset = set(ta) ^ set(tb)

    if diffset and diffset <= SIGN_OPS:
        return 'SIGN_FLIP'

    if legacy:
        # ORIGINAL PREDICATE, BUG INTACT: no non-empty guard on clause 1.
        if diffset <= CMP_OPS or (len(diffset) <= 2 and diffset & CMP_OPS):
            return 'COMPARISON_FLIP'
    else:
        # D1: clause 1 needs the non-empty guard.  Clause 2 is structurally
        # safe already (`diffset & CMP_OPS` is falsy when diffset is empty) but
        # is made explicit so the invariant is local and readable.
        if diffset and (diffset <= CMP_OPS
                        or (len(diffset) <= 2 and diffset & CMP_OPS)):
            return 'COMPARISON_FLIP'

    r = difflib.SequenceMatcher(None, ta, tb).ratio()
    if r >= 0.75 and len(ta) <= 40:
        return 'NEAR_IDENTICAL_EDIT'
    return None


def analyse(pa, pb, legacy=False):
    try:
        A = norm(open(pa, errors='replace').read().splitlines())
        B = norm(open(pb, errors='replace').read().splitlines())
    except OSError:
        return []
    out = []
    sm = difflib.SequenceMatcher(None, A, B, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        na, nb = i2 - i1, j2 - j1
        if tag == 'replace' and na <= 4 and nb <= 4:
            c = classify_replace(A[i1:i2], B[j1:j2], legacy)
            if c:
                out.append((c, i1 + 1, j1 + 1, A[i1:i2], B[j1:j2]))
        elif tag in ('insert', 'delete') and (na + nb) <= 4:
            blk = B[j1:j2] if tag == 'insert' else A[i1:i2]
            txt = ' '.join(blk)
            if not txt.strip() or txt.strip().startswith('//'):
                continue
            if GUARDY.search(txt) and NULLY.search(txt):
                out.append(('ONE_SIDED_GUARD', i1 + 1, j1 + 1,
                            A[i1:i2] if tag == 'delete' else [],
                            B[j1:j2] if tag == 'insert' else []))
    return out


# --- reproducible input derivation ----------------------------------------

def git_tracked(root, subdir):
    """Tracked files under <root>/<subdir>, relative to <subdir>.

    Using git ls-files rather than a filesystem walk keeps the file list a
    function of TRACKED inputs, so the census is re-derivable by anyone with
    the two repository revisions.
    """
    out = subprocess.run(['git', '-C', root, 'ls-files', '--', subdir],
                         capture_output=True, text=True, check=True).stdout
    pre = subdir.rstrip('/') + '/'
    return {p[len(pre):] for p in out.split() if p.startswith(pre)}


def git_rev(root):
    try:
        return subprocess.run(['git', '-C', root, 'rev-parse', 'HEAD'],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return 'unknown'


def build_filelist(ra, rb, subdir, listfile=None):
    if listfile:
        return sorted(set(open(listfile).read().split()))
    return sorted(git_tracked(ra, subdir) & git_tracked(rb, subdir))


def run_census(ra, rb, subdir, files, legacy=False):
    rows = []
    base_a, base_b = os.path.join(ra, subdir), os.path.join(rb, subdir)
    for f in files:
        pa, pb = os.path.join(base_a, f), os.path.join(base_b, f)
        if not (os.path.isfile(pa) and os.path.isfile(pb)):
            continue
        for c, la, lb, av, bv in analyse(pa, pb, legacy):
            rows.append(dict(file=f, kind=c, line_a=la, line_b=lb, a=av, b=bv))
    return rows


# --- sabotage-checked selftest --------------------------------------------

# Each case: (name, a-lines, b-lines, expected-bucket-or-None, precondition).
#
# `precondition` is what makes this suite non-vacuous, and it exists because the
# first draft of it WAS vacuous: 3 of the 4 "empty diffset" cases did not
# actually produce an empty diffset (`f(a,a)` vs `f(a)` differs by a bare `,`),
# so they exercised nothing and passed under the buggy predicate too.  A case
# tagged 'empty' MUST yield an empty diffset and a case tagged 'cmp' MUST yield
# a diffset containing a real comparison operator; a mis-specified case is a
# hard FAIL, not a silent pass.
#   'empty' -> set(ta) ^ set(tb) must be EMPTY  (exercises D1)
#   'cmp'   -> diffset must intersect CMP_OPS   (exercises the positive path)
#   'noncmp'-> diffset must be non-empty and NOT touch CMP_OPS (exercises D3)
#   None    -> no precondition
SELFTEST_CASES = [
    # ---- D1 negative controls: EMPTY diffset must NOT be a comparison flip --
    ('empty_diffset__helper_arity',
     ['UtilDrawSphere(p, 0.2f, Color(1, 0, 0), 0);'],
     ['UtilDrawSphere(p, 0.2f, Color(1, 0, 0));'],
     'NEAR_IDENTICAL_EDIT', 'empty'),
    ('empty_diffset__pure_multiplicity',
     ['f(a, a, a);'],
     ['f(a, a);'],
     'NEAR_IDENTICAL_EDIT', 'empty'),
    ('empty_diffset__unclassified_sink',
     ['x = a + a + a + a + a + a + a + a + a + a + a + a + a + a;'],
     ['x = a + a;'],
     None, 'empty'),
    ('empty_diffset__log_text_arity',
     ['MILO_WARN("load failed %s %s", name, name);'],
     ['MILO_WARN("load failed %s", name);'],
     'NEAR_IDENTICAL_EDIT', 'empty'),

    # ---- D1 positive controls: genuine comparison flips must be CAUGHT ------
    ('genuine_flip__lt_gt',
     ['if (colRad < maxRad) {'],
     ['if (colRad > maxRad) {'],
     'COMPARISON_FLIP', 'cmp'),
    ('genuine_flip__eq_ne',
     ['if (a == b) {'],
     ['if (a != b) {'],
     'COMPARISON_FLIP', 'cmp'),
    ('genuine_flip__le_lt',
     ['if (a <= b) {'],
     ['if (a < b) {'],
     'COMPARISON_FLIP', 'cmp'),
    ('genuine_flip__and_or',
     ['if (a && b) {'],
     ['if (a || b) {'],
     'COMPARISON_FLIP', 'cmp'),

    # ---- D3 negative controls: `->`, `>>` are NOT comparisons --------------
    ('arrow_is_not_comparison',
     ['float dz = axf.v.z - pos.z;'],
     ['float dz = axf.v.z - p->pos.z;'],
     'NEAR_IDENTICAL_EDIT', 'noncmp'),
    ('rshift_is_not_comparison',
     ['mData1.resize(((unsigned int)mFftSize >> 1) + 2, 0.0f);'],
     ['mData1.resize((unsigned int)mFftSize + 2, 0.0f);'],
     'NEAR_IDENTICAL_EDIT', 'noncmp'),

    # ---- unchanged buckets: guard against collateral damage from the fix ---
    ('sign_flip_still_works',
     ['x = a - b;'], ['x = a + b;'], 'SIGN_FLIP', None),
    ('const_retune_still_works',
     ['float k = 0.5f;'], ['float k = 0.25f;'], 'CONST_RETUNE', None),
    ('operand_reorder_still_works',
     ['x = a * b + c;'], ['x = c + a * b;'], 'OPERAND_REORDER', None),
    ('whitespace_only_is_none',
     ['x = a + b;'], ['x   =  a + b;  '], None, None),
]


def _precondition_error(kind, a, b):
    """Return an error string if the case does not exercise what it claims."""
    if kind is None:
        return None
    ds = set(toks(' '.join(a))) ^ set(toks(' '.join(b)))
    if kind == 'empty' and ds:
        return f'claims empty diffset but got {sorted(ds)}'
    if kind == 'cmp' and not (ds & CMP_OPS):
        return f'claims a comparison in the diffset but got {sorted(ds)}'
    if kind == 'noncmp' and (not ds or (ds & CMP_OPS)):
        return f'claims a non-empty non-comparison diffset but got {sorted(ds)}'
    return None


def selftest():
    fails, vacuous, sabotage_confirmed = [], [], []
    for name, a, b, expect, pre in SELFTEST_CASES:
        perr = _precondition_error(pre, a, b)
        if perr:
            vacuous.append((name, perr))
        got = classify_replace(a, b, legacy=False)
        if got != expect:
            fails.append((name, expect, got))
        # Negative control: does the LEGACY predicate get this wrong?  A case
        # the old code already passed is not evidence that the fix works.
        old = classify_replace(a, b, legacy=True)
        differs = old != got
        if differs:
            sabotage_confirmed.append((name, old, got))
        status = 'FAIL' if (perr or got != expect) else 'ok'
        print(f'  [{status:4s}] {name:34s} expect={str(expect):20s} '
              f'got={str(got):20s} legacy={str(old):20s}'
              f'{"  <-- legacy was WRONG" if differs else ""}'
              f'{"  <-- VACUOUS: " + perr if perr else ""}')

    print()
    print(f'  {len(SELFTEST_CASES)} cases, {len(fails)} failing under the '
          f'fixed predicate, {len(vacuous)} mis-specified')
    print(f'  {len(sabotage_confirmed)} are NEGATIVE CONTROLS -- '
          f'the legacy predicate classified them differently.')
    if vacuous:
        for n, e in vacuous:
            print(f'  VACUOUS {n}: {e}', file=sys.stderr)
        return 4
    if not sabotage_confirmed:
        print('  VACUOUS SUITE: no case discriminates the fix from the bug.',
              file=sys.stderr)
        return 5
    if fails:
        for n, e, g in fails:
            print(f'  FAIL {n}: expected {e}, got {g}', file=sys.stderr)
        return 1
    print('  SELFTEST PASS')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo-a', default=os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        help='dc3-decomp root (default: this repo)')
    ap.add_argument('--repo-b', default=None,
                    help='sibling decomp root (default: ../rb3-xenon)')
    ap.add_argument('--subdir', default='src/system')
    ap.add_argument('--listfile', default=None,
                    help='explicit file list; default derives it from git ls-files')
    ap.add_argument('--out', default=None, help='write the FULL census JSON here')
    ap.add_argument('--summary-out', default=None,
                    help='write per-bucket counts plus the small signal-bearing '
                         'buckets (everything except NEAR_IDENTICAL_EDIT and '
                         'CONST_RETUNE, which are bulk and regenerable)')
    ap.add_argument('--legacy', action='store_true',
                    help='use the ORIGINAL buggy predicates')
    ap.add_argument('--ab', action='store_true',
                    help='run both rulers and print the per-bucket delta')
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    ra = os.path.abspath(args.repo_a)
    rb = os.path.abspath(args.repo_b or os.path.join(ra, '..', 'rb3-xenon'))
    files = build_filelist(ra, rb, args.subdir, args.listfile)

    prov = dict(repo_a=ra, repo_a_rev=git_rev(ra),
                repo_b=rb, repo_b_rev=git_rev(rb),
                subdir=args.subdir, files_compared=len(files))

    BULK = {'NEAR_IDENTICAL_EDIT', 'CONST_RETUNE'}

    def census(legacy):
        rows = run_census(ra, rb, args.subdir, files, legacy)
        return rows, collections.Counter(r['kind'] for r in rows)

    def write_summary(path, rows, counts, counts_legacy=None):
        doc = dict(provenance=prov, counts=dict(counts))
        if counts_legacy is not None:
            doc['counts_legacy'] = dict(counts_legacy)
        doc['note'] = ('hunks[] omits the bulk buckets '
                       + ', '.join(sorted(BULK))
                       + '; regenerate the full census with --out')
        doc['hunks'] = [r for r in rows if r['kind'] not in BULK]
        json.dump(doc, open(path, 'w'), indent=1)
        print(f'wrote {path} ({len(doc["hunks"])} signal-bearing hunks)')

    if args.ab:
        old_rows, old_c = census(True)
        new_rows, new_c = census(False)
        print(f"files compared: {len(files)}  "
              f"(a={prov['repo_a_rev'][:9]}  b={prov['repo_b_rev'][:9]})")
        print(f"{'bucket':24s} {'legacy':>8s} {'fixed':>8s} {'delta':>8s}")
        for k in sorted(set(old_c) | set(new_c)):
            o, n = old_c.get(k, 0), new_c.get(k, 0)
            print(f'{k:24s} {o:8d} {n:8d} {n - o:+8d}')
        print(f"{'TOTAL (classified)':24s} {len(old_rows):8d} "
              f"{len(new_rows):8d} {len(new_rows) - len(old_rows):+8d}")
        if args.out:
            json.dump(dict(provenance=prov,
                           counts_legacy=dict(old_c), counts_fixed=dict(new_c),
                           hunks=new_rows),
                      open(args.out, 'w'), indent=1)
            print(f'wrote {args.out}')
        if args.summary_out:
            write_summary(args.summary_out, new_rows, new_c, old_c)
        return 0

    rows, c = census(args.legacy)
    print(f"{'legacy' if args.legacy else 'fixed'}: {len(rows)} hunks over "
          f"{len(set(r['file'] for r in rows))} files "
          f"({len(files)} files compared)")
    for k, v in c.most_common():
        print(f'  {k:24s} {v}')
    if args.out:
        json.dump(dict(provenance=prov, counts=dict(c), hunks=rows),
                  open(args.out, 'w'), indent=1)
        print(f'wrote {args.out}')
    if args.summary_out:
        write_summary(args.summary_out, rows, c)
    return 0


if __name__ == '__main__':
    sys.exit(main())
