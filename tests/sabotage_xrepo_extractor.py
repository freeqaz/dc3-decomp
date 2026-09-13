#!/usr/bin/env python3
"""Mutation harness for `scripts/analysis/xrepo_audit.py`'s function extractor.

Reverts each extraction guard in turn and REQUIRES `extractor_selftest` to go
red.  A guard nobody has seen fail is an untested branch of the instrument, and
this extractor shipped for a day returning 61% artifacts while its selftest was
green -- because it had no extractor cases at all.

Two guards were deleted rather than kept after scoring SURVIVED here (a
`(?<![\\w~])` lookbehind and an explicit containment test); if you add a guard,
add the sabotage that proves it, or the next reader inherits decoration.

    python3 tests/sabotage_xrepo_extractor.py        # exit 0 iff all detected
"""
import contextlib
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, 'scripts', 'analysis', 'xrepo_audit.py')

# (label, exact text to replace, replacement).  Plain string replace, never
# re.sub: a regex replacement string eats the backslashes the pattern under
# test is MADE of, and a substitution that silently does nothing reads as
# "SURVIVED" -- a vacuous sabotage that looks like a real negative result.
SABOTAGES = [
    ('revert lazy+optional sig prefix (the keyword-truncation fix)',
     r"(?:[A-Za-z_~][\w\s:*&<>,\[\]()~]*?)??", r"[A-Za-z_~][\w\s:*&<>,\[\]()~]*?"),
    ('drop the ~dtor name alternative',
     r"|~[A-Za-z_]\w*|operator", r"|operator"),
    ('disable macro-invocation filter',
     "    return bool(_MACROISH_RE.match(name))", "    return False"),
    ('disable args well-formedness (the swallow fix)',
     '    depth = 0\n    for c in args:', '    return True\n    depth = 0\n    for c in args:'),
    ('resume at match end instead of body end (loses TOP-LEVEL scoping)',
     '        consumed_to = pos = i + 1', '        consumed_to = i + 1\n        pos = m.end()'),
    ('restore POSITIONAL overload keys',
     "key = name if seen[name] == 1 else f'{name}#a{ar}'",
     "key = name if seen[name] == 1 else f'{name}#p{used[name]}'"),
    ('greedy args (the regression that broke ctor init-lists)',
     r"(?P<args>[^;{}]*?)\)", r"(?P<args>[^;{}]*)\)"),
]


def run(src):
    ns = {'__name__': 'xrepo_sabotage', '__file__': TARGET}
    try:
        exec(compile(src, 'xrepo_sabotage.py', 'exec'), ns)
    except Exception as e:
        return [f'IMPORT ERROR ({e})']
    with contextlib.redirect_stdout(io.StringIO()):
        try:
            return ns['extractor_selftest'](verbose=False)
        except Exception as e:
            return [f'RAISED ({type(e).__name__}: {e})']


def main():
    src = open(TARGET).read()
    base = run(src)
    if base:
        print('BASELINE IS NOT CLEAN -- every sabotage result below is uninterpretable.')
        for f in base:
            print('  ' + f)
        return 1
    print('baseline: CLEAN')

    detected = vacuous = 0
    for label, old, new in SABOTAGES:
        if old not in src:
            vacuous += 1
            print(f'  [VACUOUS ] {label}\n             text not found -- this sabotage changed NOTHING')
            continue
        res = run(src.replace(old, new, 1))
        detected += bool(res)
        print(f'  [{"DETECTED" if res else "SURVIVED"}] {label}')
        if res:
            print(f'             {res[0][:130]}')
    print(f'\n{detected}/{len(SABOTAGES)} sabotages detected'
          + (f', {vacuous} VACUOUS' if vacuous else ''))
    return 0 if detected == len(SABOTAGES) and not vacuous else 1


if __name__ == '__main__':
    sys.exit(main())
