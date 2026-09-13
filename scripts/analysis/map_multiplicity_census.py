#!/usr/bin/env python3
"""Every symbol `report.json` scores FEWER TIMES than `ham_xbox_r.map` lists it.

WHY THIS EXISTS
---------------
The weak form of this question was "a symbol defined out-of-line twice where
retail has one header definition".  That found `?FillCompressedVertex@@...`,
listed at two map addresses with identical size, whose second body nothing ever
scored and which was wrong twice (UV truncation instead of half-encode, colour
packed ABGR instead of ARGB) -- two live native-port rendering defects.

The STRONG form, which this script implements, is:

    a symbol the report scores fewer times than the linker map lists.

It is strictly better because the double-body precondition is not necessary.
Three of rb3-xenon's four real hits are *genuinely distinct functions sharing a
mangled name*, one per translation unit, with different bodies BY DESIGN -- a
static dispatch wrapper vs a default modal callback, a platform pair, two
unrelated static callbacks.  None of the double-definition preconditions hold,
yet `report.json` scores only ONE of each pair.

The mechanism is `config/<title>/symbols.txt`: dtk can only bind a mangled name
to ONE address, so the second definition is carved as `fn_<addr>`.  Nothing then
pairs it with our source symbol, and the body is unmeasured BY CONSTRUCTION.

⚠ THE EXPOSURE COMPOUNDS.  A placeholder `fn_<addr>` target-side name is also
EXEMPTED by the `name_check` relocation-name detector before it runs.  So a call
site that passes the WRONG function pointer -- one our tree fabricated, that
appears nowhere in the retail map -- reads `100.0% canonical, all equal`.
Measured on dc3 `?Init@UIManager@@UAAXXZ`: two `diff_arg` rows naming a
fabricated `?UITerminateCallback@@YAXXZ` against retail's `fn_8277AFC0`,
scoring 100.0.  Unmeasured symbol + exempted call site = invisible to every
ruler AND to the native gate.

WHAT IT CAN SEE
---------------
  * Any CODE symbol the linker map lists at two or more DISTINCT addresses whose
    report.json scored-count is lower than that address count.
  * The owning object of each map address, so the unscored copy can be found.
  * The `symbols.txt` name dtk gave the unscored address (normally `fn_<addr>`),
    and hence the report unit whose listing contains retail's unscored body.
  * Whether OUR tree emits the symbol in the object that owns the unscored copy
    (`--check-objs`), which is what makes a row actionable.

WHAT IT CANNOT SEE
------------------
  * Whether the unscored body is CORRECT.  It says only that nothing measured
    it.  Adjudicate each real hit by reading retail's listing at the unscored
    address (`build/<title>/asm/<unit>.s`, symbol `fn_<addr>`) against our
    compiled body.  Expect the outcome to be metric-invisible: closing a
    fabricated callee name on a placeholder-named target costs 0.0 points.
  * Symbols with INTERNAL linkage that MSVC did not emit a map entry for.  The
    map's "Static symbols" section is read, but a symbol the linker discarded is
    not there at all.
  * DATA symbols.  Only sections whose map Class is CODE are considered.
  * ICF fold members.  A name folded onto another name's body appears at ONE
    address, so it has multiplicity 1 and is outside this census entirely.  The
    much larger "map lists it once, report scores it zero times" population
    (29,757 names on dc3) is dominated by exactly that class and is reported
    only as a denominator, never as a finding.
  * Anything about a tree that has not been built.  report.json must exist and
    must post-date the split (`scripts/verify_split_current.py --check`).

CONTROLS -- the benign majority IS the control
----------------------------------------------
The population splits into a large benign class (EH `__unwind$` / `__catch$`
ordinal collisions, `??__E`/`??__F` dynamic-init and atexit thunks for per-TU
statics, `operator new`/`delete`, per-TU template instantiations, CRT and XDK
library internals we do not compile) and a small REAL residue.

If a run returns NONE of the benign rows, the map parse or the report join is
broken -- the tree is not clean.  `main()` REFUSES to print a null REAL result
while the control population is empty and exits 4.

`--selftest` runs five synthetic fixtures through the same code paths and
requires each to be detected; it exits 5 if any control is vacuous.

USAGE
-----
  python3 scripts/analysis/map_multiplicity_census.py
  python3 scripts/analysis/map_multiplicity_census.py --check-objs
  python3 scripts/analysis/map_multiplicity_census.py --json-out /tmp/c.json
  python3 scripts/analysis/map_multiplicity_census.py --selftest

EXIT CODES
----------
  0  census printed (real hits may or may not be present)
  2  a required input is missing
  4  CONTROL EMPTY -- the benign population is zero, so a null is not a result
  5  --selftest: a control was vacuous or a fixture went undetected
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coff_defined_symbols import defined as coff_defined  # noqa: E402

TITLE = '373307D9'

# " 0005:00000000 00baab90H .text                   CODE"
SECTAB_RE = re.compile(
    r'^\s+([0-9a-fA-F]{4}):[0-9a-fA-F]{8}\s+[0-9a-fA-F]{8}H\s+(\S+)\s+(\S+)\s*$')
# " 0005:00001fa8       ?DebugModal@@YAX... 82331fa8 f   App.obj"
SYM_RE = re.compile(
    r'^\s+([0-9a-fA-F]{4}):[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s*(.*)$')
SYMTXT_RE = re.compile(r'^(\S+)\s*=\s*\.(\w+):0x([0-9A-Fa-f]+);')
PLACEHOLDER = re.compile(r'^(fn|lbl|sub|func)_[0-9A-Fa-f]{6,8}$')

BENIGN = [
    # (class label, predicate on the mangled name)
    ('eh_unwind',       lambda n: n.startswith('__unwind$')),
    ('eh_catch',        lambda n: n.startswith('__catch$') or
                                  n.startswith('__tryblocktable')),
    ('dynamic_init',    lambda n: n.startswith('??__E')),
    ('atexit_dtor',     lambda n: n.startswith('??__F')),
    ('operator_new_del', lambda n: n.split('@')[0] in ('??2', '??3', '??_U', '??_V')),
    ('vector_ctor_iter', lambda n: n.split('@')[0] in ('??_L', '??_M')),
    ('template_inst',   lambda n: n.startswith('??$')),
]


def classify(name: str, units: set) -> str:
    for label, pred in BENIGN:
        if pred(name):
            return label
    # Library code we do not compile: every unit that could hold it is under xdk/.
    if units and all(u.startswith('default/xdk/') for u in units):
        return 'library_not_compiled'
    return 'REAL'


# ---------------------------------------------------------------- map parsing
def parse_map(text: str):
    """-> ({name: {addr: object}}, stats).  CODE-class sections only.

    Reads BOTH the "Publics by Value" and the "Static symbols" sections: a
    per-TU static is exactly the shape this census hunts, and it only appears in
    the second one.
    """
    code_sections = set()
    seen_classes = Counter()
    by_name = defaultdict(dict)
    static_names = set()
    section = None
    n_lines = 0
    for line in text.splitlines():
        s = line.strip()
        if s == 'Static symbols':
            section = 'static'
            continue
        if 'Publics by Value' in s:
            section = 'public'
            continue
        m = SECTAB_RE.match(line)
        if m and section is None:
            seen_classes[m.group(3)] += 1
            if m.group(3) == 'CODE':
                code_sections.add(m.group(1))
            continue
        m = SYM_RE.match(line)
        if not m or m.group(1) not in code_sections:
            continue
        n_lines += 1
        rest = [t for t in m.group(4).split() if t not in ('f', 'i')]
        by_name[m.group(2)].setdefault(int(m.group(3), 16), rest[-1] if rest else '')
        if section == 'static':
            static_names.add(m.group(2))
    stats = dict(symbol_lines=n_lines, code_sections=len(code_sections),
                 section_classes=dict(seen_classes), names=len(by_name))
    return by_name, static_names, stats


def parse_symbols_txt(text: str):
    out = {}
    for line in text.splitlines():
        m = SYMTXT_RE.match(line)
        if m:
            out[int(m.group(3), 16)] = m.group(1)
    return out


def parse_report(report: dict):
    """-> (Counter name->times scored, {name: [unit,...]})."""
    cnt = Counter()
    units = defaultdict(list)
    for u in report.get('units') or []:
        for f in u.get('functions') or []:
            cnt[f['name']] += 1
            units[f['name']].append(u['name'])
    return cnt, units


# ------------------------------------------------------------------- census
def census(map_by_name, static_names, scored, scored_units, symtxt):
    """-> (rows, denominator dict).  A row is a name scored fewer times than the
    map lists DISTINCT addresses for it."""
    rows = []
    multi = 0
    single_zero = 0
    for name, addrs in map_by_name.items():
        n_map = len(addrs)
        n_scored = scored.get(name, 0)
        if n_map > 1:
            multi += 1
        if n_scored >= n_map:
            continue
        if n_map == 1:
            # "map lists once, report scores zero" -- the ICF fold-member class.
            single_zero += 1
            continue
        placements = []
        for a in sorted(addrs):
            dtk = symtxt.get(a, '')
            placements.append(dict(addr='%08x' % a, obj=addrs[a], dtk_name=dtk,
                                   dtk_is_placeholder=bool(PLACEHOLDER.match(dtk))))
        units = set(scored_units.get(name, []))
        rows.append(dict(name=name, map_count=n_map, scored_count=n_scored,
                         static=name in static_names,
                         scored_units=sorted(units),
                         placements=placements,
                         klass=classify(name, units)))
    denom = dict(map_code_names=len(map_by_name),
                 map_names_multi_address=multi,
                 undercounted_multi_address=len(rows),
                 undercounted_single_address_icf_fold_class=single_zero)
    return rows, denom


def unit_of_dtk_name(dtk_name, scored_units):
    u = scored_units.get(dtk_name)
    return u[0] if u else ''


def check_objs(rows, root: Path, scored_units):
    """For each placement, does OUR object define this symbol?  Uses the unit
    that scores the dtk placeholder name, which is the object dtk carved the
    unscored body into."""
    cache = {}
    for r in rows:
        for p in r['placements']:
            unit = unit_of_dtk_name(p['dtk_name'], scored_units) if p['dtk_name'] else ''
            if not unit:
                unit = r['scored_units'][0] if r['scored_units'] else ''
            p['unit'] = unit
            p['ours_defines'] = None
            if not unit.startswith('default/'):
                continue
            rel = unit[len('default/'):]
            obj = root / 'build' / TITLE / 'src' / (rel + '.obj')
            if not obj.exists():
                continue
            if obj not in cache:
                cache[obj] = {n for n, _s, _c, _t, _v in coff_defined(str(obj), True)}
            p['ours_defines'] = r['name'] in cache[obj]


# ------------------------------------------------------------------ selftest
_FIXTURE_MAP = """ ham_xbox_r

 Start         Length     Name                   Class
 0001:00000000 00000100H .rdata                  DATA
 0005:00000000 00001000H .text                   CODE

  Address         Publics by Value              Rva+Base       Lib:Object

 0001:00000000       ?Twice@@YAXXZ              81000000     a:decoy.obj
 0005:00000000       ?Benign@@YAXXZ             82000000     a:one.obj
 0005:00000100       ?OnlyOnce@@YAXXZ           82000100     a:one.obj

 Static symbols

 0005:00000200       ??__EsThing@@YAXXZ         82000200 f   a:one.obj
 0005:00000300       ??__EsThing@@YAXXZ         82000300 f   a:two.obj
 0005:00000400       ?Twice@@YAXXZ              82000400 f   a:one.obj
 0005:00000500       ?Twice@@YAXXZ              82000500 f   a:two.obj
"""


def selftest() -> int:
    fails = []
    by_name, statics, stats = parse_map(_FIXTURE_MAP)

    # C1: the section table must actually classify, or every symbol is dropped.
    if stats['section_classes'].get('CODE') != 1 or stats['code_sections'] != 1:
        fails.append('C1 section-table parse: %r' % (stats,))
    # C2: the Static symbols section must be read -- the whole real residue is
    #     per-TU statics, which appear ONLY there.
    if '?Twice@@YAXXZ' not in statics or len(by_name.get('?Twice@@YAXXZ', {})) != 2:
        fails.append('C2 static section not parsed: %r' % (by_name.get('?Twice@@YAXXZ'),))
    # C3: a DATA-class section must NOT contribute symbols.  The fixture plants
    #     a DATA-section `?Twice@@YAXXZ` at 0x81000000 precisely so that
    #     dropping the CODE filter is DETECTED rather than silently tolerated.
    if any(a < 0x82000000 for d in by_name.values() for a in d):
        fails.append('C3 DATA-class section leaked into the census')
    if len(by_name.get('?Twice@@YAXXZ', {})) != 2:
        fails.append('C3b DATA decoy changed the multiplicity of a CODE name')

    symtxt = {0x82000500: 'fn_82000500', 0x82000300: 'fn_82000300'}

    # POSITIVE fixture: the report scores one of each pair.
    rep = {'units': [{'name': 'default/a/one', 'functions': [
        {'name': '?Benign@@YAXXZ'}, {'name': '?OnlyOnce@@YAXXZ'},
        {'name': '??__EsThing@@YAXXZ'}, {'name': '?Twice@@YAXXZ'}]},
        {'name': 'default/a/two', 'functions': [
            {'name': 'fn_82000300'}, {'name': 'fn_82000500'}]}]}
    scored, sunits = parse_report(rep)
    rows, denom = census(by_name, statics, scored, sunits, symtxt)
    names = {r['name']: r for r in rows}
    if '?Twice@@YAXXZ' not in names or names['?Twice@@YAXXZ']['klass'] != 'REAL':
        fails.append('C4 planted REAL row undetected: %r' % (sorted(names),))
    if '??__EsThing@@YAXXZ' not in names or \
            names['??__EsThing@@YAXXZ']['klass'] != 'dynamic_init':
        fails.append('C5 planted benign control row undetected')
    if '?OnlyOnce@@YAXXZ' in names or '?Benign@@YAXXZ' in names:
        fails.append('C6 a fully-scored single-address symbol was reported')
    if denom['map_names_multi_address'] != 2:
        fails.append('C7 denominator wrong: %r' % (denom,))

    # NEGATIVE fixture: both copies scored -> the REAL row must disappear.  A
    # census that reports the row anyway is measuring the map alone.
    rep2 = json.loads(json.dumps(rep))
    rep2['units'][1]['functions'] = [{'name': '?Twice@@YAXXZ'},
                                     {'name': '??__EsThing@@YAXXZ'}]
    scored2, sunits2 = parse_report(rep2)
    rows2, _ = census(by_name, statics, scored2, sunits2, symtxt)
    if any(r['name'] == '?Twice@@YAXXZ' for r in rows2):
        fails.append('C8 NEGATIVE control: row survived being scored twice')
    if any(r['name'] == '??__EsThing@@YAXXZ' for r in rows2):
        fails.append('C9 NEGATIVE control: benign row survived being scored twice')

    # VACUITY: the positive fixture must have produced both a control row and a
    # REAL row, or C4/C5 proved nothing.
    if not fails and not (names and any(r['klass'] == 'REAL' for r in rows)
                          and any(r['klass'] != 'REAL' for r in rows)):
        print('SELFTEST VACUOUS: fixture produced no population', file=sys.stderr)
        return 5

    for f in fails:
        print('SELFTEST FAIL: ' + f, file=sys.stderr)
    print("selftest: %d/%d controls passed" % (10 - len(fails), 10))
    return 5 if fails else 0


# ---------------------------------------------------------------------- main
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', default=os.environ.get('REPO_ROOT', '.'))
    ap.add_argument('--map', default=None)
    ap.add_argument('--report', default=None)
    ap.add_argument('--symbols', default=None)
    ap.add_argument('--check-objs', action='store_true',
                    help='ask our own COFF objects whether they define each hit')
    ap.add_argument('--all', action='store_true',
                    help='print benign rows too, not just the REAL residue')
    ap.add_argument('--json-out')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args(argv)

    if a.selftest:
        return selftest()

    root = Path(a.root).resolve()
    mp = Path(a.map or root / 'orig' / TITLE / 'ham_xbox_r.map')
    rp = Path(a.report or root / 'build' / TITLE / 'report.json')
    sp = Path(a.symbols or root / 'config' / TITLE / 'symbols.txt')
    for p in (mp, rp, sp):
        if not p.exists():
            print('missing input: %s' % p, file=sys.stderr)
            return 2

    by_name, statics, stats = parse_map(mp.read_text(errors='replace'))
    symtxt = parse_symbols_txt(sp.read_text(errors='replace'))
    scored, sunits = parse_report(json.loads(rp.read_text()))
    rows, denom = census(by_name, statics, scored, sunits, symtxt)
    if a.check_objs:
        check_objs(rows, root, sunits)

    by_class = Counter(r['klass'] for r in rows)
    control_n = sum(v for k, v in by_class.items() if k != 'REAL')
    real = [r for r in rows if r['klass'] == 'REAL']

    print('# map-multiplicity census  (%s)' % TITLE)
    print()
    print('map:     %s   (%d CODE symbol lines, %d CODE sections, classes %s)'
          % (mp, stats['symbol_lines'], stats['code_sections'],
             stats['section_classes']))
    print('report:  %s   (%d scored function entries)'
          % (rp, sum(scored.values())))
    print()
    print('## denominator')
    for k, v in denom.items():
        print('  %-46s %7d' % (k, v))
    print()
    print('## classification of the %d under-scored multi-address names' % len(rows))
    for k, v in sorted(by_class.items(), key=lambda kv: -kv[1]):
        print('  %-24s %7d %s' % (k, v, '<-- the residue' if k == 'REAL' else ''))
    print('  %-24s %7d  (in-pass control population)' % ('[benign total]', control_n))
    print()

    if control_n == 0:
        print('CONTROL EMPTY -- the benign class (EH ordinal collisions, ??__E/??__F '
              'thunks, operator new/delete, per-TU template instantiations) came back '
              'zero.\nThat means the map parse or the report join is broken, NOT that '
              'the tree is clean.  Refusing to report.', file=sys.stderr)
        return 4

    show = rows if a.all else real
    print('## %s' % ('all rows' if a.all else 'REAL residue'))
    for r in sorted(show, key=lambda r: (r['klass'], r['name'])):
        print('  %s  [%s]  scored %d / map %d%s'
              % (r['name'], r['klass'], r['scored_count'], r['map_count'],
                 '  (static)' if r['static'] else ''))
        for p in r['placements']:
            mark = 'SCORED  ' if not p['dtk_is_placeholder'] else 'unscored'
            extra = ''
            if 'ours_defines' in p:
                extra = '  ours_defines=%s  unit=%s' % (p['ours_defines'], p.get('unit', ''))
            print('      %s 0x%s  %-34s dtk=%s%s'
                  % (mark, p['addr'], p['obj'], p['dtk_name'] or '(none)', extra))
    if not show:
        print('  (none)')
    print()
    print('Read an unscored body with:  build/%s/asm/<unit>.s  symbol fn_<addr>' % TITLE)

    if a.json_out:
        Path(a.json_out).write_text(json.dumps(
            dict(denominator=denom, map_stats=stats, by_class=dict(by_class),
                 control_population=control_n, rows=rows), indent=1))
        print('json -> %s' % a.json_out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
