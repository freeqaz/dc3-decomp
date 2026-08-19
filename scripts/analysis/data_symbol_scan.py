#!/usr/bin/env python3
"""
data_symbol_scan.py — hunt for WRONG DATA-SYMBOL CONSTRUCTION bugs that the
normalized code metric ignores entirely (wave-11 Lane C).

Background (the bug class):
  objdiff's normal code scoring (functionRelocDiffs=none) diffs *functions* and
  never looks at DATA symbols. A class whose virtual-declaration order is wrong
  in the header still compiles, and every *function* may read 100% normalized,
  yet the compiled VTABLE puts the wrong method in a slot. The construction-side
  analogue of lane-A's wrong-virtual-CALL hunt: lane A finds wrong virtual calls,
  this finds wrong vtable/data CONSTRUCTION. Lives entirely in the data plane.

Signal we hunt (per data symbol, via objdiff-cli --include-data):
  - data_diff.relocations[].kind == "replace" with a base_target_symbol
      => the slot resolves to a DIFFERENT function on the two sides.
      The HIGH-SIGNAL sub-case: target points to ?MethodA@<Class>@@ and base
      points to ?MethodB@<Class>@@ where BOTH are real methods of the SAME class
      (a swapped/misordered virtual declaration).
  - data_diff.relocations[].kind == "insert"  => extra/misordered entry.
  - data_diff.segments[].kind in (replace) => raw byte mismatch (string typo,
      wrong enum/init value).

ICF NOISE (the dominant benign case, filtered out):
  The target binary is heavily ICF-folded. A vtable slot whose TARGET symbol is
  an ICF artifact — OnlyReturns / merged_<addr> / merged_Returns1 / Returns1 /
  or an UNRELATED class's method that happens to share machine code
  (e.g. Curl_gethostname, ?SetEngine@CTrigramStore@NUISPEECH@@) — is benign:
  the linker folded our correct method into an identical-bytes symbol. We only
  flag a reloc "replace" as a CANDIDATE BUG when neither side is an ICF artifact
  AND both sides name a real method (ideally of the same declaring class, which
  is the decl-order-swap fingerprint).

Read-only: diffs already-built .obj files (NO --build). Safe alongside the
build/permuter fleet. Never writes decomp.db.

Usage:
    python3 scripts/analysis/data_symbol_scan.py --project . \
        --classes Splash DxRnd RndShaderMgr NgShaderMgr NgSpotlightDrawer
    python3 scripts/analysis/data_symbol_scan.py --project . --units-grep rndobj
    python3 scripts/analysis/data_symbol_scan.py --project . --all-vtables   # every vtable in every unit (bounded)
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

# --------------------------------------------------------------------------- #
# COFF symbol enumeration (reused from scripts/dump_vtable.py)
# --------------------------------------------------------------------------- #

def read_coff_symbols(data):
    machine, num_sections, ts, symoff, nsym, optsz, flags = struct.unpack_from('<HHIIIHH', data, 0)
    stroff = symoff + nsym * 18
    strsz = struct.unpack_from('<I', data, stroff)[0]
    strtab = data[stroff:stroff + strsz]

    def name(off):
        if data[off:off + 4] == b'\x00\x00\x00\x00':
            so = struct.unpack_from('<I', data, off + 4)[0]
            end = strtab.index(b'\x00', so)
            return strtab[so:end].decode('ascii', 'replace')
        return data[off:off + 8].rstrip(b'\x00').decode('ascii', 'replace')

    syms = []
    i = 0
    while i < nsym:
        so = symoff + i * 18
        nm = name(so)
        val, sec, tv, st, ax = struct.unpack_from('<IhHBB', data, so + 8)
        syms.append(nm)
        i += 1 + ax
    return syms


# A "data symbol" we care about: vtable, RTTI, or any other ?? data symbol.
def classify_data_symbol(name):
    if name.startswith('??_7'):
        return 'vtable-layout'
    if name.startswith('??_R4') or name.startswith('??_R0') or \
       name.startswith('??_R1') or name.startswith('??_R2') or name.startswith('??_R3'):
        return 'rtti'
    if name.startswith('??_C'):
        return 'string-pool'
    return None


# --------------------------------------------------------------------------- #
# ICF / merged-symbol classification
# --------------------------------------------------------------------------- #

ICF_LITERAL = {'OnlyReturns', 'merged_Returns1', 'Returns1', 'Returns0',
               'OnlyReturnsTrue', 'OnlyReturnsFalse', 'DoNothing'}

# Lazy linker-map symbol->address index, so we can DEFINITIVELY prove two symbols
# are ICF-folded (identical address) vs genuinely different functions.
_MAP_ADDR = None


_MAP_LOCK = threading.Lock()


def load_map_addresses(project):
    """Symbol -> linker address, from the original's .map.

    MUST be race-free: this is called from every worker thread, and the whole
    point of the index is the *authoritative* ICF verdict.  Publishing a
    half-filled dict (the old code assigned `_MAP_ADDR = {}` and then populated
    it, so a second thread could observe an incomplete index) made
    same_icf_address() return None for symbols that were merely not-read-yet, and
    those rows fell through to `bug_rows` -- proven ICF folds reported as
    candidate bugs, differently on every run.  Build into a local, publish once,
    under a lock.
    """
    global _MAP_ADDR
    if _MAP_ADDR is not None:
        return _MAP_ADDR
    with _MAP_LOCK:
        if _MAP_ADDR is not None:  # another thread finished while we waited
            return _MAP_ADDR
        idx = {}
        mp = os.path.join(project, 'orig', '373307D9', 'ham_xbox_r.map')
        if os.path.exists(mp):
            # lines like: " 0005:003d26f0   ?Sym@@...  827026f0 f   obj"
            rx = re.compile(
                r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s')
            try:
                with open(mp, 'r', errors='replace') as f:
                    for line in f:
                        m = rx.match(line)
                        if m:
                            idx[m.group(1)] = m.group(2).lower()
            except OSError:
                pass
        _MAP_ADDR = idx
        return _MAP_ADDR


def same_icf_address(project, a, b):
    """True iff both symbols resolve to the same linker address (proven ICF fold)."""
    idx = load_map_addresses(project)
    if not idx:
        return None  # unknown
    aa = idx.get(a)
    bb = idx.get(b)
    if aa is None or bb is None:
        return None
    return aa == bb


def is_icf_artifact(sym):
    """A symbol that is an ICF/merge fold, not the real method of the class."""
    if sym is None:
        return False
    if sym in ICF_LITERAL:
        return True
    if sym.startswith('merged_'):
        return True
    # bare C-style names with no MSVC mangling are usually C libs folded in by ICF
    if not sym.startswith('?'):
        return True
    return False


_DECL_CLASS_RE = re.compile(r'^\?(?:\?_[GE1])?([A-Za-z_][A-Za-z_0-9]*)@(?:\?\$)?([A-Za-z_][A-Za-z_0-9]*)@')


def declaring_class(sym):
    """Best-effort: the class that declares this method symbol.
    For ?Method@Class@@... returns Class. For dtor thunks ??_G/??_E/??1 the class
    is the first token. Returns None if not parseable."""
    if not sym or not sym.startswith('?'):
        return None
    if sym.startswith('??_G') or sym.startswith('??_E'):
        m = re.match(r'^\?\?_[GE]([A-Za-z_][A-Za-z_0-9]*)@', sym)
        return m.group(1) if m else None
    if sym.startswith('??1'):
        m = re.match(r'^\?\?1([A-Za-z_][A-Za-z_0-9]*)@', sym)
        return m.group(1) if m else None
    # ?Method@Class@Namespace@@...  -> class is token AFTER method
    m = re.match(r'^\?([A-Za-z_][A-Za-z_0-9]*)@([A-Za-z_][A-Za-z_0-9]*)@', sym)
    if m:
        return m.group(2)
    return None


def vtable_owner_class(symname):
    """Extract the class name a vtable symbol belongs to: ??_7Class@@6B... -> Class."""
    m = re.match(r'^\?\?_7([A-Za-z_][A-Za-z_0-9]*)@', symname)
    return m.group(1) if m else None


# --------------------------------------------------------------------------- #
# objdiff data diff
# --------------------------------------------------------------------------- #

def run_data_diff(project, unit, symbol, timeout=90):
    cmd = ['bin/objdiff-cli', 'diff', '-p', project, '-u', unit, symbol,
           '-f', 'json', '--include-data']
    try:
        p = subprocess.run(cmd, cwd=project, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, 'timeout'
    if p.returncode != 0:
        return None, f'exit{p.returncode}:{p.stderr.strip()[:120]}'
    try:
        d = json.loads(p.stdout)
    except json.JSONDecodeError:
        return None, 'jsonerr'
    return d.get('data_diff'), None


def triage_symbol(symname, kind, dd, project='.'):
    """Return (verdict, detail_rows). verdict in
    {clean, icf-benign, candidate-bug}."""
    if dd is None:
        return 'no-data', []
    mp = dd.get('match_percent', 100.0)
    relocs = dd.get('relocations', [])
    segs = dd.get('segments', [])
    owner = vtable_owner_class(symname) if kind == 'vtable-layout' else None

    bug_rows = []
    icf_rows = []
    insert_rows = []
    seg_rows = []

    for r in relocs:
        rk = r.get('kind')
        if rk == 'equal':
            continue
        tgt = r.get('target_symbol')
        base = r.get('base_target_symbol')
        off = r.get('offset')
        if rk == 'insert':
            insert_rows.append({'offset': off, 'base': base, 'target': tgt})
            continue
        if rk == 'replace':
            # The slot resolves to a different symbol on each side.
            t_icf = is_icf_artifact(tgt)
            b_icf = is_icf_artifact(base)
            # base==None means target has a reloc the base build lacks for this
            # slot at the SAME offset but with a different symbol mapping; objdiff
            # reports base_target_symbol only when it differs. None base with a
            # real target is typically the *equal-bytes-different-pairing* case
            # (the slot bytes match; the symbol pairing is what differs) — for
            # vtables a None-base replace almost always means our build's slot
            # folded into an ICF symbol that objdiff couldn't name. Treat as ICF
            # unless the target itself is non-ICF AND there's a base symbol.
            if base is None:
                # cannot prove a real divergence without both sides
                icf_rows.append({'offset': off, 'target': tgt, 'base': base,
                                 'reason': 'base-none'})
                continue
            if t_icf or b_icf:
                icf_rows.append({'offset': off, 'target': tgt, 'base': base,
                                 'reason': 'icf-fold'})
                continue

            # AUTHORITATIVE: if both symbols resolve to the SAME linker address,
            # this is a proven ICF fold — benign regardless of class names.
            if same_icf_address(project, tgt, base) is True:
                icf_rows.append({'offset': off, 'target': tgt, 'base': base,
                                 'reason': 'icf-same-address'})
                continue

            # Both real symbols.
            t_cls = declaring_class(tgt)
            b_cls = declaring_class(base)
            same_owner = (owner and t_cls == owner and b_cls == owner)

            # CROSS-CLASS ICF FOLD (heuristic fallback when the map can't resolve):
            # target names a method of an UNRELATED class while base names the
            # correct owner method. The linker folded our correct method into an
            # identical-bytes function from another TU. Benign — our source is right.
            if owner and t_cls and t_cls != owner and b_cls == owner:
                icf_rows.append({'offset': off, 'target': tgt, 'base': base,
                                 'reason': 'cross-class-icf'})
                continue

            # SCALAR-vs-VECTOR DELETING DTOR (??_G target vs ??_E base, same class):
            # a compiler dtor-wrapper lowering choice, NOT a decl-order bug.
            # Documented compiler floor (unfixable-compiler.md ??_G vs ~T).
            if (tgt.startswith('??_G') and base and base.startswith('??_E')) or \
               (tgt.startswith('??_E') and base and base.startswith('??_G')):
                icf_rows.append({'offset': off, 'target': tgt, 'base': base,
                                 'reason': 'dtor-thunk-lowering'})
                continue

            bug_rows.append({'offset': off, 'target': tgt, 'base': base,
                             'target_class': t_cls, 'base_class': b_cls,
                             'same_owner': same_owner, 'owner': owner})

    for s in segs:
        if s.get('kind') == 'replace':
            seg_rows.append({'offset': s.get('offset'), 'size': s.get('size'),
                             'bytes': s.get('bytes'), 'base_bytes': s.get('base_bytes')})

    if bug_rows:
        return 'candidate-bug', {'match_percent': mp, 'bug_rows': bug_rows,
                                 'insert_rows': insert_rows, 'seg_rows': seg_rows,
                                 'icf_count': len(icf_rows)}
    if seg_rows and kind == 'string-pool':
        # a string typo / init mismatch is a real bug independent of relocs
        return 'candidate-bug', {'match_percent': mp, 'bug_rows': [],
                                 'insert_rows': insert_rows, 'seg_rows': seg_rows,
                                 'icf_count': len(icf_rows)}
    if seg_rows:
        return 'candidate-bug', {'match_percent': mp, 'bug_rows': [],
                                 'insert_rows': insert_rows, 'seg_rows': seg_rows,
                                 'icf_count': len(icf_rows)}
    if insert_rows:
        return 'review-insert', {'match_percent': mp, 'bug_rows': [],
                                 'insert_rows': insert_rows, 'seg_rows': [],
                                 'icf_count': len(icf_rows)}
    if icf_rows:
        return 'icf-benign', {'match_percent': mp, 'icf_count': len(icf_rows)}
    return 'clean', {'match_percent': mp}


# --------------------------------------------------------------------------- #
# Unit / symbol enumeration
# --------------------------------------------------------------------------- #

def load_units(project):
    with open(os.path.join(project, 'objdiff.json')) as f:
        cfg = json.load(f)
    return cfg.get('units', [])


def enum_data_symbols(project, unit_entry):
    rel = unit_entry.get('target_path')
    if not rel:
        return []
    tp = os.path.join(project, rel)
    if not os.path.exists(tp):
        return []
    try:
        data = open(tp, 'rb').read()
        syms = read_coff_symbols(data)
    except Exception:
        return []
    out = []
    seen = set()
    for s in syms:
        if s in seen:
            continue
        seen.add(s)
        k = classify_data_symbol(s)
        if k:
            out.append((s, k))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default='.')
    ap.add_argument('--classes', nargs='*', default=[],
                    help='Only scan vtables/rtti of these class names')
    ap.add_argument('--units-grep', default=None,
                    help='Only scan units whose name matches this substring')
    ap.add_argument('--all-vtables', action='store_true',
                    help='Scan every vtable in every selected unit')
    ap.add_argument('--include-strings', action='store_true',
                    help='Also scan ??_C string-pool symbols (noisy)')
    ap.add_argument('--workers', type=int, default=8)
    ap.add_argument('--max-symbols', type=int, default=0,
                    help='Hard cap on total data symbols diffed. 0 = NO CAP (default). '
                         'A non-zero value makes the run a SAMPLE: it prints a TRUNCATED '
                         'banner and exits 3 unless --allow-truncation is given. '
                         '(Was 4000 until 2026-08-19, which silently turned an '
                         '18,549-symbol census into a 22%% sample.)')
    ap.add_argument('--out', default=None)
    add_coverage_args(ap)
    args = ap.parse_args()

    project = os.path.abspath(args.project)
    units = load_units(project)
    cov = CoverageReport('data_symbol_scan', args=args)

    # Select units
    sel = []
    for u in units:
        nm = u.get('name', '')
        if args.units_grep and args.units_grep not in nm:
            continue
        sel.append(u)
    cov.extra('units_total', len(units))
    cov.extra('units_selected', len(sel))
    if len(sel) != len(units):
        cov.note(f'--units-grep={args.units_grep!r} selected {len(sel)} of {len(units)} units; '
                 f'the data symbols of the other {len(units) - len(sel)} were never enumerated')

    # Build (unit, symbol, kind) tasklist.  Every discard below goes through
    # cov.drop(), so `universe - examined - dropped` must come out at zero; if it
    # does not, emit() says so instead of printing a clean total.
    tasks = []
    universe = 0
    no_target_obj_units = 0
    classes_lc = set(args.classes)
    for u in sel:
        dsyms = enum_data_symbols(project, u)
        _tp = u.get('target_path')
        if not dsyms and (not _tp or not os.path.exists(os.path.join(project, _tp))):
            no_target_obj_units += 1
        universe += len(dsyms)
        for (sym, kind) in dsyms:
            if kind == 'string-pool' and not args.include_strings:
                cov.drop('string-pool-not-requested', note='pass --include-strings')
                continue
            if classes_lc:
                # restrict to symbols whose owner/declaring class matches
                owner = vtable_owner_class(sym) if kind == 'vtable-layout' else None
                rtti_owner = None
                m = re.match(r'^\?\?_R\d([A-Za-z_][A-Za-z_0-9]*)@', sym)
                if m:
                    rtti_owner = m.group(1)
                if owner not in classes_lc and rtti_owner not in classes_lc:
                    cov.drop('class-not-in---classes')
                    continue
            tasks.append((u['name'], sym, kind))

    cov.universe(universe, 'data symbols (??_7/??_R*/??_C) in the selected units\' target .obj files')
    if no_target_obj_units:
        cov.note(f'{no_target_obj_units} selected unit(s) had no readable target .obj — their '
                 f'data symbols are absent from the universe entirely, so this denominator '
                 f'is itself a lower bound')

    # NOTE: without --classes/--all-vtables the scanner scans EVERY vtable it
    # enumerated.  It used to print "Nothing to scan." here while doing exactly
    # that, which is its own small lie; the coverage block now states the real
    # examined count either way.
    if not classes_lc and not args.all_vtables and not args.include_strings:
        cov.note('no selector given (--classes/--all-vtables): scanning every enumerated '
                 'vtable and RTTI symbol')

    if args.max_symbols and len(tasks) > args.max_symbols:
        cov.cap('--max-symbols', args.max_symbols, before=len(tasks), after=args.max_symbols,
                note='rows beyond the cap were NEVER diffed')
        tasks = tasks[:args.max_symbols]
    cov.examine(len(tasks))

    results = {'candidate-bug': [], 'review-insert': [], 'icf-benign': 0,
               'clean': 0, 'no-data': 0, 'errors': []}

    def work(t):
        unit, sym, kind = t
        dd, err = run_data_diff(project, unit, sym)
        if err:
            return ('error', unit, sym, kind, err, None)
        verdict, detail = triage_symbol(sym, kind, dd, project=project)
        return (verdict, unit, sym, kind, None, detail)

    # Warm the linker-map index on the main thread so no worker ever races on it.
    load_map_addresses(project)

    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = [ex.submit(work, t) for t in tasks]
        for fu in as_completed(futs):
            verdict, unit, sym, kind, err, detail = fu.result()
            if verdict == 'error':
                results['errors'].append({'unit': unit, 'sym': sym, 'err': err})
            elif verdict in ('candidate-bug', 'review-insert'):
                results[verdict].append({'unit': unit, 'sym': sym, 'kind': kind,
                                         'detail': detail})
            elif verdict in ('icf-benign', 'clean', 'no-data'):
                results[verdict] += 1

    # Thread completion order is not a sort key.  Two runs of a census must be
    # byte-identical or the census is not a measurement.
    for k in ('candidate-bug', 'review-insert'):
        results[k].sort(key=lambda r: (r['unit'], r['sym']))
    results['errors'].sort(key=lambda r: (r['unit'], r['sym']))

    results['_coverage'] = cov.as_dict()
    # Kept for backward compatibility with existing consumers of _summary.
    results['_summary'] = {
        'units_selected': len(sel),
        'symbols_scanned': len(tasks),
        'symbols_total': universe,
        'dropped': cov.as_dict()['dropped'],
        'truncated': cov.truncated,
    }

    out = json.dumps(results, indent=2)
    if args.out:
        with open(args.out, 'w') as f:
            f.write(out)
    print(out)
    print(f"\n# candidate-bug={len(results['candidate-bug'])} "
          f"review-insert={len(results['review-insert'])} "
          f"icf-benign={results['icf-benign']} clean={results['clean']} "
          f"no-data={results['no-data']} errors={len(results['errors'])} "
          f"scanned={len(tasks)} of {universe}", file=sys.stderr)
    return cov.emit()


if __name__ == '__main__':
    sys.exit(main())
