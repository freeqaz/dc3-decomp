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

THE `--min-sim` FLOOR IS A CUT, AND IT WAS HIDING REAL DEFECTS
---------------------------------------------------------------
`--min-sim` DEFAULTED TO 0.6 AND STILL DOES, for continuity with the runs that
are already recorded.  **That default is not a recommendation.**  Measured
2026-09-13 over all three siblings with the repaired extractor
(`xrepo_floor_sweep.py --floors 0.6 0.4 0.3 0.2 --noise`), high-precision
candidates admitted, and the marginal band each step adds:

    sibling          0.60   0.40   0.30   0.20     decoy noise rate per band
    og-dc3-decomp     169   +26     +6     +4      1.4% / 2.6% / 4.7%
    rb3-xenon         134   +28     +2     +2      1.3% / 2.6% / 5.0%
    rb3               208   +37     +8     +6      1.4% / 2.4% / 4.4%

The [0.40, 0.60) band is not the dregs -- it is the CLEANEST band in the sweep.
Its findings-per-comparable-pair runs 7-16x the shuffled-pairing decoy rate,
against 2.5-4.9x for everything above 0.60, because a genuine semantic defect
DEPRESSES similarity.  Cutting at 0.60 removed the highest-signal slice.  Below
0.40 the enrichment falls to 1.8-5.5x and the marginal yield collapses to 2-8
functions, so 0.40 is the recommended floor and 0.30 is defensible when working
a single sibling exhaustively.  Real defects do live below 0.40 -- `TrigTableInit`,
which wrote index 513 of a 512-float table, sits at sim 0.394 against og-dc3.

WHAT IT CANNOT SEE  (state this before quoting a clean sweep)
-------------------------------------------------------------
  * Anything in a function that exists in only one tree, or whose token
    similarity falls below `--min-sim`.  Those are counted as `too_different`
    in the stats block, NOT as clean.  Quote the floor with the population.
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

EXTRACTOR ARTIFACTS  (fixed 2026-09-13 -- what the old extractor was reporting)
-------------------------------------------------------------------------------
`extract_functions` was mis-parsing on a scale that dominated its own output.
Over the 678 files shared with og-dc3-decomp it returned **25,869** "functions"
on the dc3 side, of which **15,828 were artifacts**; the repaired extractor
returns **10,267**, none of them artifact-shaped.  Four distinct defects:

  1. THE SIGNATURE PREFIX ATE THE FIRST CHARACTER.  `(?P<sig>[A-Za-z_~]...)` was
     mandatory, so the name group had to start at the SECOND character of a
     line: `if (` parsed as name `f`, `while` as `hile`, `switch` as `witch`,
     `FOREACH` as `OREACH`.  `_NOT_A_FUNCTION` never saw the keyword it was
     written to reject, and **13,958 `f` bodies** -- every `if` block in
     src/system -- entered the population as functions.  The SAME bite hit every
     real definition with no return type: `CharBones::CharBones()` at line start
     was recorded as `harBones::CharBones`, a real function under a name that
     joins to nothing.  Fixed by making the prefix optional and LAZY.
  2. FILE-SCOPE MACRO TABLES were admitted as functions (`BEGIN_HANDLERS`,
     `DEF_DATA_FUNC`, `INIT_REVS`, ...).  They pair across trees by ordinal
     coincidence, never by identity.
  3. THE ARGUMENT GROUP SWALLOWED THE NEXT DEFINITION.  `args` is `[^;{}]*` and
     crosses newlines, so `PropSync(a, b)` on one line and `void Bar::Sync(int i)
     {` on the next parsed as ONE call -- and Bar::Sync vanished from the
     population entirely.  Now rejected by `_args_well_formed`.
  4. OVERLOAD KEYS WERE POSITIONAL.  `name#{len(res)}` numbers by order of
     appearance, so `Foo#3` here and `Foo#3` there were the same overload only
     by luck.  Keys are now `name#a{arity}`, which is order-independent.

Effect on the joinability of the population, same floor (0.60), same trees:
**87 high-precision keys could not be joined to report.json before (65 of them
artifact-named); 18 after, 0 artifact-named.**  The 18 that remain are real
names the report genuinely does not score (internal-linkage statics, free
`operator<<`).  The union population itself moved 471 -> 430, because the
artifacts left and 37 previously-hidden real functions arrived.

CONTROL
-------
`--selftest` runs each detector against a synthetic pair engineered to trip it,
and an identical pair that must trip nothing.  It exits 5 if any detector fails
to fire (the tool would then report a clean sweep it did not earn) or if the
negative pair produces a finding.  A sweep that cannot fail is not a measurement.

It ALSO runs `extractor_selftest`, whose seven cases are the four defects above
plus in-class ctor/dtor keying and overload-order stability.  Each expectation
is exact (`==`), so a regression that ADDS a phantom fails as loudly as one that
drops a definition.  `tests/sabotage_xrepo_extractor.py` reverts each guard in
turn and requires the suite to go red: **7 of 7 detected.**  Two guards that
scored SURVIVED there were REMOVED rather than kept as decoration -- a
`(?<![\w~])` lookbehind and an `m.start() < consumed_to` containment test, both
unreachable once the lazy prefix and the end-of-body resume were in place.

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
# Two quantifier choices carry the extraction fix; the sabotage harness proves
# each one independently (see EXTRACTOR ARTIFACTS in the module docstring).
#   `(?P<sig>...)??`  the return-type/storage-class prefix is OPTIONAL and LAZY,
#                     so the SHORTEST parse is tried first and a keyword head is
#                     seen whole (`switch`) instead of decapitated (`witch`).
#                     This alone closes the keyword class AND the eaten first
#                     character of a return-type-less `CharBones::CharBones()`.
#   `(?P<args>...?)`  LAZY: greedy, it swallows a constructor's `) : mInit(0`,
#                     which `_args_well_formed` then rejects as a swallow.
#   `~[A-Za-z_]\w*`   an in-class inline `~Foo()` must key as `~Foo`, never as
#                     `Foo` -- otherwise a destructor pairs with a constructor.
# A `(?<![\w~])` lookbehind was tried here and REMOVED: with the lazy prefix and
# the `~` alternative in place it cannot change any parse, and the sabotage
# harness scored it SURVIVED.  A guard that cannot fail is not a guard.
FUNC_RE = re.compile(
    r'(?:^|\n)[ \t]*(?P<sig>(?:[A-Za-z_~][\w\s:*&<>,\[\]()~]*?)??'
    r'(?P<name>(?:[A-Za-z_]\w*(?:<[^;{}()]*>)?::)+~?[A-Za-z_]\w*'
    r'|~[A-Za-z_]\w*|operator[^\s(]*|[A-Za-z_]\w*)'
    r'\s*\((?P<args>[^;{}]*?)\)\s*(?:const\s*)?(?::[^;{}]*?)?)\{'
)
_NOT_A_FUNCTION = {'if', 'for', 'while', 'switch', 'catch', 'return', 'else', 'do',
                   'sizeof', 'new', 'delete', 'try', 'case', 'default', 'throw',
                   'and', 'or', 'not', 'struct', 'class', 'union', 'enum', 'namespace',
                   'extern', 'static', 'inline', 'const', 'typedef', 'template'}

# A file-scope ALL-CAPS "call" followed by a brace-balanced run is a MACRO
# invocation (BEGIN_HANDLERS, BEGIN_PROPSYNCS, DEF_DATA_FUNC, INIT_REVS, ...),
# not a function definition.  It has no stable identity to pair on -- two trees
# emit N of them per file and the Nth of one is not the Nth of the other -- so
# admitting them manufactures pairings out of ordinal coincidence.
# ALL-CAPS, three or more characters.  An underscore is NOT required: `FOREACH`
# has none and leaked through the underscore-only form of this rule.  Every Milo
# function name is CamelCase, so this costs nothing -- verified by extracting
# all of src/system and confirming no SHOUTING name is a real definition.
_MACROISH_RE = re.compile(r'^[A-Z][A-Z0-9_]{2,}$')


def _is_macro_invocation(name):
    return bool(_MACROISH_RE.match(name))


def _args_well_formed(args):
    """True iff `args` is a self-contained argument list.

    `FUNC_RE`'s `args` group is `[^;{}]*`, which crosses newlines, so it will
    happily run past the real `)` of a preceding macro line and close on a
    LATER function's `)` -- swallowing that function's definition whole.  A
    depth that goes negative is the signature of exactly that."""
    depth = 0
    for c in args:
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth < 0:
                return False
    return depth == 0


def _arity(args):
    """Comma count at paren/angle/bracket depth 0, as an overload discriminator.

    Positional ordinals (the old `name#len(res)`) are NOT a discriminator: they
    number by order of appearance, so `Foo#3` in one tree and `Foo#3` in another
    are the same overload only by luck.  Arity is a property of the declaration
    and travels between trees."""
    args = args.strip()
    if not args or args == 'void':
        return 0
    depth, n = 0, 1
    for c in args:
        if c in '(<[':
            depth += 1
        elif c in ')>]':
            depth -= 1
        elif c == ',' and depth == 0:
            n += 1
    return n


def extract_functions(src):
    """Return {qualname: body_text} for brace-balanced TOP-LEVEL definitions.

    Three artifact classes are excluded by construction -- see EXTRACTOR
    ARTIFACTS in the module docstring for what each one used to cost."""
    found = []
    consumed_to, pos, srclen = -1, 0, len(src)
    while pos < srclen:
        m = FUNC_RE.search(src, pos)
        if not m:
            break
        name = m.group('name')
        args = m.group('args')
        # Rejected heads resume ONE LINE on, never past the match: `args` is
        # `[^;{}]*` and spans newlines, so a rejected macro line's match can
        # reach over the real definition beneath it and swallow it whole.
        # `BEGIN_HANDLERS(C) ... END_HANDLERS \n void C::Poll(` ... `) {`
        # parses as one "call", and skipping to m.end() loses C::Poll.
        if (name in _NOT_A_FUNCTION or _is_macro_invocation(name)
                # a `)` at depth 0 inside `args` means the regex ran PAST the
                # real closing paren to find a later one -- the same swallow
                # without an ALL-CAPS head to recognise it by.  Counting parens
                # is not enough: `PropSync(a, b)\nvoid Bar::Sync(int i` is
                # perfectly balanced and still a swallow.
                or not _args_well_formed(args)):
            pos = m.start() + 1
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
            pos = m.start() + 1
            continue
        # Resuming at the END of the accepted body is what keeps extraction
        # TOP-LEVEL: every control-flow head and macro invocation inside the
        # body is stepped over, so none of them is recorded as a function whose
        # "body" is a fragment of one we already have.  (An explicit
        # `m.start() < consumed_to` containment test was tried here and removed
        # -- `search` never starts before `pos`, so it was unreachable and the
        # sabotage harness scored it SURVIVED.)
        consumed_to = pos = i + 1
        found.append((name, _arity(args), src[start:i + 1]))

    # Key in a SECOND pass, so a name's key does not depend on how many later
    # definitions happen to share it.  Unique name -> bare name (the form that
    # joins to report.json).  Overloaded -> `name#aN`, which is the same string
    # in both trees whatever order the overloads are declared in.
    seen = Counter(nm for nm, _, _ in found)
    res, used = {}, Counter()
    for name, ar, body in found:
        key = name if seen[name] == 1 else f'{name}#a{ar}'
        used[key] += 1
        if used[key] > 1:
            key = f'{key}@{used[key]}'
        res[key] = body
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


# --------------------------------------------------------------------------
# CONTROL -- the EXTRACTOR.  Every case here was a live artifact in the
# 2026-09-13 population; each is written so that reverting the corresponding
# guard makes it fail, and the expected sets are exact (`==`, not `<=`) so a
# regression that ADDS a phantom is caught as well as one that drops a real
# definition.
# --------------------------------------------------------------------------
_EXTRACT_CASES = [
    # keyword heads: the sig prefix used to eat the first character, so
    # `if`->`f`, `while`->`hile`, `switch`->`witch` all became "functions"
    # and `_NOT_A_FUNCTION` never saw them.  13,958 `f` bodies in src/system.
    ('keyword_heads', """
void Foo::Bar(int x) {
    if (x > 0) { doIt(); }
    while (x) { y(); }
    switch (x) { case 1: break; }
    for (int i = 0; i < 3; i++) { z(); }
    do { w(); } while (x);
}
""", {'Foo::Bar'}),
    # same bite on a definition with NO return type: the leading `C` was eaten
    # and the function was recorded as `harBones::CharBones` -- a REAL function
    # under a name that joins to nothing.
    ('no_return_type', """
CharBones::CharBones() : mA(0) {
    mB = 1;
}
CharBones::~CharBones() {
    Cleanup();
}
""", {'CharBones::CharBones', 'CharBones::~CharBones'}),
    # a file-scope ALL-CAPS macro with a BRACE-BALANCED body parses as a
    # perfectly well-formed "function"; nothing but the macro filter rejects it
    ('macro_with_body', """
DEF_DATA_FUNC(Foo) { return DataNode(1); }
DEF_DATA_FUNC(Bar) { return DataNode(2); }
void A::B() { mX = 1; }
""", {'A::B'}),
    # file-scope macro tables pair by ordinal coincidence, never by identity
    ('macro_tables', """
BEGIN_HANDLERS(CharBones)
    HANDLE_ACTION(foo, Foo())
END_HANDLERS
void CharBones::Poll() { mX++; }
""", {'CharBones::Poll'}),
    # a macro invocation INSIDE a body is not a nested definition
    ('nested_macro', """
void Char::Update() {
    FOREACH (it, mList) { it->Poll(); }
    mDone = true;
}
""", {'Char::Update'}),
    # ... and a LOWERCASE one is caught by nothing but the top-level scoping:
    # it is not a keyword and not ALL-CAPS, so the head passes every name
    # filter.  Resuming at the accepted body's END is the only thing that
    # stops it becoming a "function" whose body is a slice of Char::Update.
    ('nested_lowercase_macro', """
void Char::Update() {
    foreach (it, mList) { it->Poll(); }
    mDone = true;
}
""", {'Char::Update'}),
    # an in-class inline dtor must NOT be recorded under the ctor's name --
    # dropping the `~` pairs a destructor against a constructor
    ('inclass_ctor_dtor', """
class NullLoader : public Loader {
public:
    NullLoader(const FilePath &fp, LoaderPos pos)
        : Loader(fp, pos) { mDone = true; }
    virtual ~NullLoader() { Cleanup(); }
};
""", {'NullLoader', '~NullLoader'}),
    # a non-ALL-CAPS macro line has the same swallow shape but no ALL-CAPS head
    # to recognise it by; the unbalanced-paren check is what catches it
    ('swallow_lowercase_macro', """
PropSync(mFoo, prop, i, PROP_SET)
void Bar::Sync(int i) { mN = i; }
""", {'Bar::Sync'}),
    # overloads must key identically regardless of declaration ORDER
    ('overload_order', """
void A::Set(int a) { mI = a; }
void A::Set(int a, int b) { mI = a; mJ = b; }
""", {'A::Set#a1', 'A::Set#a2'}),
    ('overload_order_reversed', """
void A::Set(int a, int b) { mI = a; mJ = b; }
void A::Set(int a) { mI = a; }
""", {'A::Set#a1', 'A::Set#a2'}),
]


def extractor_selftest(verbose=True):
    failures = []
    for label, src, expect in _EXTRACT_CASES:
        got = set(extract_functions(strip_comments(src)))
        ok = got == expect
        if not ok:
            failures.append(f'extract:{label}: got {sorted(got)}, expected {sorted(expect)}')
        if verbose:
            print(f'  [{"PASS" if ok else "FAIL"}] {"extract:" + label:24} -> {sorted(got)}')
    # Cross-tree stability: the two overload_order cases must produce the SAME
    # key set.  Without it `#a{arity}` could degenerate to a positional ordinal
    # again and this suite would still pass case-by-case.
    _src = {lb: s for lb, s, _ in _EXTRACT_CASES}
    a = set(extract_functions(strip_comments(_src['overload_order'])))
    b = set(extract_functions(strip_comments(_src['overload_order_reversed'])))
    if a != b:
        failures.append(f'extract:overload keys are ORDER-DEPENDENT: {sorted(a)} vs {sorted(b)}')
    if verbose:
        print(f'  [{"PASS" if a == b else "FAIL"}] {"extract:order-stable":24} -> {sorted(a)}')
    return failures


def _prepare(body):
    """The exact pipeline a real file goes through, so the controls exercise the
    preprocessing (native-guard elision) and not just the detectors."""
    return drop_native_blocks(strip_comments(body))


def selftest(verbose=True):
    failures = extractor_selftest(verbose)
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
