#!/usr/bin/env python3
"""Cross-repo SOURCE semantic-divergence audit (token-level, four precise detectors).

PROVENANCE
----------
Written 2026-09-13 as `/tmp/xrepo_audit.py` + `/tmp/xrepo_audit2.py` during the
cross-repo drift lane that found `CharIKFingers::CalculateFingerDest` (a negated
`angle03` and a 2x-too-large curl angle, dc3 `143dab01f`, 88.4 -> 90.4).  `/tmp`
is tmpfs on this box and does not survive a reboot; the two files are merged here
into one self-contained module so the instrument outlives the session that wrote
it.  Rescued via /home/free/tmp/preserved-instruments-20260913/.

WHAT IT IS FOR
--------------
dc3-decomp shares most of `src/system/**` with three sibling decomp trees
(`og-dc3-decomp`, `rb3-xenon`, `rb3`).  Where our source diverges SEMANTICALLY
from a sibling's -- a flipped comparison, a negated term, a constant off by a
factor of two, two call arguments in the other order -- one of the two trees is
probably mis-decompiled.  Those defects are frequently INVISIBLE to objdiff: a
sign flip on a float constant costs zero because the constant is reached through
a relocation whose target the map names only `lbl_<addr>`, and a comparison
direction can be lost in a block-placement rotation.

WHAT IT CAN SEE
---------------
  Pass A  CONST_SIGN / CONST_FACTOR2 / CONST_FLOAT_NEAR
          whole-function numeric-literal VALUE multiset diff, with MILO_ASSERT
          trailing line-number literals stripped (those legitimately differ).
  Pass B  CMP_DIR / EQ_POLARITY / LOGIC_OP / ARITH_OP / BITOP
          short aligned replace-hunks differing in exactly ONE operator token
          with otherwise identical operands.
  Pass C  UNARY_MINUS_* / NOT_*
          a pure unary `-` or `!` inserted or deleted on an otherwise identical
          token run.
  Pass D  ARG_SWAP
          a two-argument call appearing with its arguments in the other order.

WHAT IT CANNOT SEE  (state this before quoting a clean sweep)
-------------------------------------------------------------
  * Anything in a function that exists in only one tree, or whose token
    similarity falls below `--min-sim` (default 0.6).  Those are counted as
    `too_different` in the stats block, NOT as clean.
  * Anything in a file not shared by both trees.
  * Semantics carried by names rather than tokens (calling a different function
    of the same arity reads as a rename, not a divergence).
  * Whether OUR side is the wrong one.  A hit says the two trees differ; it does
    NOT say which is correct.  dc3 POSTDATES RB3 and both trees carry genuine
    engine divergences -- `Rnd::Terminate` legitimately calls two Terminates here
    and one there.  ADJUDICATE EACH SIDE AGAINST ITS OWN TARGET BYTES.
  * It reads SOURCE, never object bytes.  For a float literal the companion
    instrument `float_oracle.py` reads the 4 bytes out of dc3's own target
    object and is the only thing that settles the value.

CONTROL
-------
`--selftest` runs each detector against a synthetic pair engineered to trip it,
and an identical pair that must trip nothing.  It exits 5 if any detector fails
to fire (the tool would then report a clean sweep it did not earn) or if the
negative pair produces a finding.  A sweep that cannot fail is not a measurement.

USAGE
-----
  python3 scripts/analysis/xrepo_audit.py --selftest
  python3 scripts/analysis/xrepo_audit.py --sibling rb3-xenon --out /tmp/a.json
"""
import argparse
import difflib
import json
import subprocess
import os
import re
import sys
from collections import Counter

DC3_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


def _sibling_parent():
    """Directory the sibling decomp trees live in.

    NOT `dirname(DC3_ROOT)`: inside a git worktree that resolves to
    `<repo>/.claude/worktrees`, and every `--sibling rb3-xenon` then fails with
    "no such sibling tree" from a path nobody would recognise.  `git rev-parse
    --git-common-dir` points at the MAIN checkout's .git in a worktree and at
    the local one otherwise -- the same idiom that fixed MILO_ENGINE_PATH."""
    try:
        out = subprocess.run(['git', '-C', DC3_ROOT, 'rev-parse', '--git-common-dir'],
                             capture_output=True, text=True, timeout=10)
        if out.returncode == 0 and out.stdout.strip():
            common = os.path.abspath(os.path.join(DC3_ROOT, out.stdout.strip()))
            return os.path.dirname(os.path.dirname(common))
    except (OSError, subprocess.SubprocessError):
        pass
    return os.path.dirname(DC3_ROOT)


SIBLING_PARENT = _sibling_parent()

# --------------------------------------------------------------------------
# preprocessing
# --------------------------------------------------------------------------


def strip_comments(src):
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n:
            if src[i + 1] == '/':
                j = src.find('\n', i)
                if j < 0:
                    break
                i = j
                continue
            if src[i + 1] == '*':
                j = src.find('*/', i + 2)
                if j < 0:
                    break
                out.append(' ')
                i = j + 2
                continue
        if c in '"\'':
            q = c
            j = i + 1
            while j < n:
                if src[j] == '\\':
                    j += 2
                    continue
                if src[j] == q:
                    j += 1
                    break
                j += 1
            out.append(src[i:j])
            i = j
            continue
        out.append(c)
        i += 1
    return ''.join(out)


NATIVE_MACROS = ('HX_NATIVE', 'MILO_NATIVE', '__linux__', 'PLATFORM_PC', 'MILO_TESTS')


def drop_native_blocks(src):
    """Keep the NON-native branch of every `#ifdef HX_NATIVE`, and delete all
    other preprocessor directive LINES while keeping their guarded content.

    Necessary because a verbatim og-port drops native guards (see
    project_og_port_drops_hx_native_guards) -- without this the native branch
    reads as a semantic divergence in every guarded function."""
    out = []
    stack = []  # entries: [kind, emitting]
    for ln in src.split('\n'):
        s = ln.strip()
        if s.startswith('#'):
            d = s[1:].strip()
            if re.match(r'^(ifdef|ifndef|if)\b', d):
                is_nat = any(m in d for m in NATIVE_MACROS)
                neg = d.startswith('ifndef') or ('!defined' in d) or bool(re.match(r'^if\s*!', d))
                stack.append(['nat', bool(neg)] if is_nat else ['other', True])
                continue
            if re.match(r'^(else|elif)\b', d):
                if stack and stack[-1][0] == 'nat':
                    stack[-1][1] = not stack[-1][1]
                continue
            if re.match(r'^endif\b', d):
                if stack:
                    stack.pop()
                continue
            continue
        if all(e[1] for e in stack):
            out.append(ln)
    return '\n'.join(out)


# --------------------------------------------------------------------------
# function extraction
# --------------------------------------------------------------------------
FUNC_RE = re.compile(
    r'(?:^|\n)[ \t]*(?P<sig>[A-Za-z_~][\w\s:*&<>,\[\]()~]*?'
    r'(?P<name>(?:[A-Za-z_]\w*(?:<[^;{}()]*>)?::)+~?[A-Za-z_]\w*|operator[^\s(]*|[A-Za-z_]\w*)'
    r'\s*\((?P<args>[^;{}]*)\)\s*(?:const\s*)?(?::[^;{}]*?)?)\{'
)
_NOT_A_FUNCTION = {'if', 'for', 'while', 'switch', 'catch', 'return', 'else', 'do',
                   'sizeof', 'new', 'delete'}


def extract_functions(src):
    """Return {qualname: body_text} for brace-balanced definitions in `src`."""
    res = {}
    for m in FUNC_RE.finditer(src):
        name = m.group('name')
        if name in _NOT_A_FUNCTION:
            continue
        start = m.end() - 1
        depth, i, n = 0, m.end() - 1, len(src)
        while i < n:
            ch = src[i]
            if ch in '"\'':
                q = ch
                i += 1
                while i < n:
                    if src[i] == '\\':
                        i += 2
                        continue
                    if src[i] == q:
                        break
                    i += 1
            elif ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if depth != 0:
            continue
        key = name if name not in res else f'{name}#{len(res)}'
        res[key] = src[start:i + 1]
    return res


TOK_RE = re.compile(r'0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][-+]?\d+)?[fFuUlL]*|\.\d+[fF]?|'
                    r'[A-Za-z_]\w*|"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|'
                    r'<<=|>>=|->\*|\.\.\.|<<|>>|<=|>=|==|!=|&&|\|\||\+\+|--|->|::|'
                    r'[-+*/%=<>!&|^~?:;,.(){}\[\]]')


def tokenize(body):
    return TOK_RE.findall(body)


def load(path):
    with open(path, 'r', errors='replace') as f:
        return extract_functions(drop_native_blocks(strip_comments(f.read())))


# --------------------------------------------------------------------------
# Pass A -- numeric literal value multiset
# --------------------------------------------------------------------------
ASSERT_RE = re.compile(r'\bMILO_(ASSERT\w*|WARN\w*|FAIL\w*|NOTIFY\w*|LOG\w*)\s*\(')


def strip_assert_lineno(body):
    """`MILO_ASSERT(expr, 0xAB)` -- the trailing literal is a SOURCE LINE NUMBER
    and legitimately differs between trees.  Drop it or every assert in every
    file reads as a constant divergence."""
    out, i = [], 0
    n = len(body)
    while True:
        m = ASSERT_RE.search(body, i)
        if not m:
            out.append(body[i:])
            break
        out.append(body[i:m.end()])
        depth, j, last_comma = 1, m.end(), -1
        while j < n and depth:
            c = body[j]
            if c in '"\'':
                q = c
                j += 1
                while j < n:
                    if body[j] == '\\':
                        j += 2
                        continue
                    if body[j] == q:
                        break
                    j += 1
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    break
            elif c == ',' and depth == 1:
                last_comma = j
            j += 1
        inner = body[m.end():j]
        if last_comma >= 0:
            tail = body[last_comma + 1:j].strip()
            if re.fullmatch(r'(0[xX][0-9a-fA-F]+|\d+)', tail):
                inner = body[m.end():last_comma]
        out.append(inner)
        i = j
    return ''.join(out)


NUM_RE = re.compile(r'\b0[xX][0-9a-fA-F]+\b|(?<![\w.])\d+\.\d*(?:[eE][-+]?\d+)?f?|'
                    r'(?<![\w.])\.\d+(?:[eE][-+]?\d+)?f?|(?<![\w.])\d+(?![\w.])')


def numvals(body):
    """Signed numeric literal values in a body, with a directly-preceding unary
    minus folded into the value."""
    body = strip_assert_lineno(body)
    vals = []
    for m in NUM_RE.finditer(body):
        t = m.group(0)
        try:
            v = float(int(t, 16)) if t.lower().startswith('0x') else float(t.rstrip('f'))
        except Exception:
            continue
        k = m.start() - 1
        while k >= 0 and body[k] in ' \t':
            k -= 1
        if k >= 0 and body[k] == '-':
            k2 = k - 1
            while k2 >= 0 and body[k2] in ' \t\n':
                k2 -= 1
            if k2 < 0 or body[k2] in '(,=+-*/%<>!&|^?:;{[':
                v = -v
        vals.append(v)
    return vals


TRIVIAL = {0.0, 1.0, 2.0, 3.0, 4.0, -1.0, 8.0, 16.0, 32.0, 64.0, 100.0, 255.0, 256.0,
           1000.0, 0.5, 10.0, 12.0, 5.0, 6.0, 7.0, 20.0, 24.0, 128.0, 512.0, 1024.0}


def pass_A(b1, b2):
    v1, v2 = Counter(numvals(b1)), Counter(numvals(b2))
    finds = []
    for a in (v1 - v2):
        for b in (v2 - v1):
            if a == 0 or b == 0:
                continue
            r = b / a
            if abs(r + 1.0) < 1e-9:
                finds.append(('CONST_SIGN', f'{a:g}', f'{b:g}'))
            elif abs(r - 2.0) < 1e-9 or abs(r - 0.5) < 1e-9:
                finds.append(('CONST_FACTOR2', f'{a:g}', f'{b:g}'))
            elif (a not in TRIVIAL and b not in TRIVIAL
                  and (a != int(a) or b != int(b)) and 0.2 < abs(r) < 5.0):
                finds.append(('CONST_FLOAT_NEAR', f'{a:g}', f'{b:g}'))
    return finds


# --------------------------------------------------------------------------
# Passes B / C -- single-operator replace hunks, unary insert/delete
# --------------------------------------------------------------------------
CMP = {'<', '>', '<=', '>='}
EQ = {'==', '!='}
LOGIC = {'&&', '||'}


def pass_BC(t1, t2):
    finds = []
    sm = difflib.SequenceMatcher(a=t1, b=t2, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        A, B = t1[i1:i2], t2[j1:j2]
        ctx = (' '.join(t1[max(0, i1 - 8):i1]) + '  [[' + ' '.join(A) + ' | ' + ' '.join(B)
               + ']]  ' + ' '.join(t1[i2:i2 + 8]))
        if tag == 'insert' and B == ['-']:
            finds.append(('UNARY_MINUS_SIB_ONLY', ctx))
            continue
        if tag == 'delete' and A == ['-']:
            finds.append(('UNARY_MINUS_DC3_ONLY', ctx))
            continue
        if tag == 'insert' and B == ['!']:
            finds.append(('NOT_SIB_ONLY', ctx))
            continue
        if tag == 'delete' and A == ['!']:
            finds.append(('NOT_DC3_ONLY', ctx))
            continue
        if tag != 'replace' or len(A) != len(B) or len(A) > 6:
            continue
        diffpos = [k for k in range(len(A)) if A[k] != B[k]]
        if len(diffpos) != 1:
            continue
        a, b = A[diffpos[0]], B[diffpos[0]]
        if a in CMP and b in CMP:
            finds.append(('CMP_DIR', ctx))
        elif a in EQ and b in EQ:
            finds.append(('EQ_POLARITY', ctx))
        elif a in LOGIC and b in LOGIC:
            finds.append(('LOGIC_OP', ctx))
        elif {a, b} <= {'+', '-'} or {a, b} <= {'*', '/'}:
            finds.append(('ARITH_OP', ctx))
        elif {a, b} <= {'&', '|'} or {a, b} <= {'<<', '>>'}:
            finds.append(('BITOP', ctx))
    return finds


# --------------------------------------------------------------------------
# Pass D -- swapped two-argument call
# --------------------------------------------------------------------------
CALL_RE = re.compile(r'([A-Za-z_]\w*)\s*\(\s*([A-Za-z_][\w\.\->:]*)\s*,\s*([A-Za-z_][\w\.\->:]*)\s*\)')


def pass_D(b1, b2):
    c1 = {(m.group(1), m.group(2), m.group(3)) for m in CALL_RE.finditer(b1)}
    c2 = {(m.group(1), m.group(2), m.group(3)) for m in CALL_RE.finditer(b2)}
    return [('ARG_SWAP', f'{f}({x}, {y})  |  {f}({y}, {x})')
            for f, x, y in (c1 - c2) if (f, y, x) in c2]


PRIO = {'CONST_SIGN': 100, 'CONST_FACTOR2': 95, 'ARG_SWAP': 90,
        'UNARY_MINUS_DC3_ONLY': 85, 'UNARY_MINUS_SIB_ONLY': 85,
        'CMP_DIR': 75, 'EQ_POLARITY': 70, 'NOT_DC3_ONLY': 65, 'NOT_SIB_ONLY': 65,
        'CONST_FLOAT_NEAR': 60, 'ARITH_OP': 55, 'BITOP': 50, 'LOGIC_OP': 40}


def compare_bodies(b1, b2):
    """All findings for one paired body.  Returns [(kind, detail), ...]."""
    t1, t2 = tokenize(b1), tokenize(b2)
    finds = [(k, f'{a}  |  {b}') for k, a, b in pass_A(b1, b2)]
    finds += pass_D(b1, b2)
    finds += pass_BC(t1, t2)
    return finds


# --------------------------------------------------------------------------
# CONTROL -- each detector must fire on a pair built to trip it
# --------------------------------------------------------------------------
_CASES = [
    ('CONST_SIGN',
     '{ float a = 0.3927f; return a; }',
     '{ float a = -0.3927f; return a; }'),
    ('CONST_FACTOR2',
     '{ float curl = 1.5708f * t; return curl; }',
     '{ float curl = 0.7854f * t; return curl; }'),
    ('CONST_FLOAT_NEAR',
     '{ return x * 5.405405f; }',
     '{ return x * 5.4054055f; }'),
    ('CMP_DIR',
     '{ if (count < limit) { doThing(); } }',
     '{ if (count > limit) { doThing(); } }'),
    ('EQ_POLARITY',
     '{ if (state == kReady) { go(); } }',
     '{ if (state != kReady) { go(); } }'),
    ('LOGIC_OP',
     '{ if (alpha && beta) { go(); } }',
     '{ if (alpha || beta) { go(); } }'),
    ('ARITH_OP',
     '{ return base + delta; }',
     '{ return base - delta; }'),
    ('BITOP',
     '{ return flags & mask; }',
     '{ return flags | mask; }'),
    ('UNARY_MINUS_SIB_ONLY',
     '{ float w = angle03 * scale; return w; }',
     '{ float w = -angle03 * scale; return w; }'),
    ('NOT_SIB_ONLY',
     '{ if (ready) { go(); } }',
     '{ if (!ready) { go(); } }'),
    ('ARG_SWAP',
     '{ Interp(from, to); }',
     '{ Interp(to, from); }'),
]

# Bodies that MUST produce nothing.  Each is calibrated so that removing the
# corresponding suppression makes it fire -- a negative control that cannot be
# broken is not a control.
#   neg_assert_lineno: 0x2A (42) and 0x54 (84) are exactly 2x apart, so deleting
#                      the strip_assert_lineno() call trips CONST_FACTOR2.
#   neg_rename:        identifier renames and reformatting only.
#   neg_native_guard:  the sibling dropped the HX_NATIVE guard (a real og-port
#                      habit).  The native-only 3.1416f is 4x the sibling-only
#                      0.7854f, so deleting drop_native_blocks() lets the native
#                      branch leak into the multiset and trips CONST_FLOAT_NEAR.
_NEGATIVES = [
    ('assert_lineno',
     '{ MILO_ASSERT(ptr, 0x2A); int total = ptr->Count(); return total; }',
     '{ MILO_ASSERT(ptr, 0x54); int n = ptr->Count(); return n; }'),
    ('rename',
     '{ int accum = 0; for (int i = 0; i < mNum; i++) accum += mVals[i]; return accum; }',
     '{ int sum = 0;   for (int j = 0; j < mNum; j++) sum   += mVals[j]; return sum; }'),
    ('native_guard',
     '{\n#ifdef HX_NATIVE\n    float k = 3.1416f;\n#else\n    float k = 1.5708f;\n#endif\n    return k * n;\n}',
     '{\n    float k = 1.5708f;\n    return k * n * 0.7854f;\n}'),
]


def _prepare(body):
    """The exact pipeline a real file goes through, so the controls exercise the
    preprocessing (native-guard elision) and not just the detectors."""
    return drop_native_blocks(strip_comments(body))


def selftest(verbose=True):
    failures = []
    for kind, a, b in _CASES:
        kinds = {k for k, _ in compare_bodies(_prepare(a), _prepare(b))}
        ok = kind in kinds
        if not ok:
            failures.append(f'{kind}: detector did NOT fire (got {sorted(kinds) or "nothing"})')
        if verbose:
            print(f'  [{"PASS" if ok else "FAIL"}] {kind:24} -> {sorted(kinds) or "nothing"}')
    for label, a, b in _NEGATIVES:
        neg = compare_bodies(_prepare(a), _prepare(b))
        if neg:
            failures.append(f'NEGATIVE control {label!r} fired: {neg}')
        if verbose:
            print(f'  [{"PASS" if not neg else "FAIL"}] {"neg:" + label:24} -> '
                  f'{sorted({k for k, _ in neg}) or "nothing"}')
    if failures:
        print('\nSELFTEST FAILED -- this tool cannot distinguish a divergence from a clean pair,')
        print('so any sweep it reports is VACUOUS. Refusing to present it as a measurement.')
        for f in failures:
            print('  ' + f)
        return 5
    if verbose:
        print(f'\nSELFTEST OK: {len(_CASES)} detectors fire, negative control silent.')
    return 0


# --------------------------------------------------------------------------
def shared_files(sibroot, subdir='src/system'):
    """Relative paths of .cpp files present under `subdir` in BOTH trees."""
    a, b = os.path.join(DC3_ROOT, subdir), os.path.join(sibroot, subdir)
    out = []
    for root, _, files in os.walk(a):
        for fn in files:
            if not fn.endswith('.cpp'):
                continue
            rel = os.path.relpath(os.path.join(root, fn), a)
            if os.path.exists(os.path.join(b, rel)):
                out.append(rel)
    return sorted(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--selftest', action='store_true',
                    help='run the detector controls and exit (5 if any is vacuous)')
    ap.add_argument('--sibling', help='sibling tree name under %s' % SIBLING_PARENT)
    ap.add_argument('--sibling-root', help='explicit sibling tree path (overrides --sibling)')
    ap.add_argument('--subdir', default='src/system')
    ap.add_argument('--min-sim', type=float, default=0.6)
    ap.add_argument('--out', help='write full JSON results here')
    ap.add_argument('--top', type=int, default=40)
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.sibling:
        ap.error('--sibling is required (or use --selftest)')

    # the control gates the sweep: never report a population from a vacuous tool
    if selftest(verbose=False) != 0:
        return 5

    sibroot = args.sibling_root or os.path.join(SIBLING_PARENT, args.sibling)
    if not os.path.isdir(sibroot):
        print(f'no such sibling tree: {sibroot}', file=sys.stderr)
        return 2
    rels = shared_files(sibroot, args.subdir)
    st = Counter()
    results = []
    for rel in rels:
        try:
            f1 = load(os.path.join(DC3_ROOT, args.subdir, rel))
            f2 = load(os.path.join(sibroot, args.subdir, rel))
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
            t1, t2 = tokenize(b1), tokenize(b2)
            if len(t1) < 10 or len(t2) < 10:
                st['tiny'] += 1
                continue
            sim = difflib.SequenceMatcher(a=t1, b=t2, autojunk=False).ratio()
            if sim >= 0.99999:
                st['identical'] += 1
                continue
            if sim < args.min_sim:
                st['too_different'] += 1
                continue
            st['comparable'] += 1
            finds = compare_bodies(b1, b2)
            if not finds:
                continue
            st['divergent'] += 1
            results.append(dict(file=rel, func=name, sim=round(sim, 3), ntok=len(t1),
                                score=round(sum(PRIO.get(k, 0) for k, _ in finds) * sim, 1),
                                finds=finds))
    results.sort(key=lambda r: -r['score'])
    print('DENOMINATOR:', json.dumps(dict(st)))
    print('kinds:', dict(Counter(k for r in results for k, _ in r['finds']).most_common()))
    if args.out:
        with open(args.out, 'w') as f:
            json.dump(dict(sibling=args.sibling, stats=dict(st), results=results), f, indent=1)
        print(f'wrote {args.out}')
    for r in results[:args.top]:
        print(f"\n=== {r['file']} :: {r['func']}  sim={r['sim']} score={r['score']}")
        for k, d in r['finds'][:6]:
            print(f'   [{k}] {d[:180]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
