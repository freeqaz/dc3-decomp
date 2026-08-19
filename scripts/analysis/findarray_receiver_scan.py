#!/usr/bin/env python3
"""Scan for receiver / source-vs-destination confusion bugs.

Two independent checkers:

1. FindArray receiver bugs (original)
   Detects the pattern where a FindArray result is stored in a variable,
   but later the PARENT object is used for another FindArray call where
   the child variable would be the correct receiver.

   Example bug (MetagameRank.cpp):
       DataArray *taskArr = rankCfg->FindArray("tasks");
       gOneTimeTasks = rankCfg->FindArray("one_time");  // BUG: should be taskArr->
       gRepeatableTasks = taskArr->FindArray("repeatable");

2. ObjDirItr source/dest confusion (new)
   Detects patterns where objects are iterated from one dir and transferred
   to another, but the iteration source is the *destination* dir (empty)
   instead of the *source* dir.

   Example bug (HamDirector.cpp):
       clipsDir->Reserve(clipsDirHash, clipsDirStr);
       for (ObjDirItr<CharClip> it(clipsDir, false); ...)  // BUG: should be moveMgrDir
           cur->SetName(name, clipsDir);

   Heuristics:
     a. "iter_dest" — ObjDirItr iterates the same dir that SetName transfers INTO
     b. "parallel_mismatch" — two structurally similar iterate-and-transfer blocks
        in the same function use different source dirs for the same destination

Usage:
  python3 scripts/analysis/findarray_receiver_scan.py
  python3 scripts/analysis/findarray_receiver_scan.py --all     # include weaker signals
  python3 scripts/analysis/findarray_receiver_scan.py --json

COVERAGE (see scripts/analysis/coverage.py)
  Every run prints a COVERAGE block naming its DENOMINATOR: how many source
  files were considered, how many were unreadable, and how many were skipped by
  the relevance gate (broken out so the gate's known blind spot is a NUMBER and
  not a silence).  Three ways this scanner used to be able to print a confident
  "No suspicious receiver confusion patterns found":

    * run from any cwd but the repo root — the default path `src/` is RELATIVE
      and a non-existent path was skipped with no `else`, so the file list came
      out empty and the scan reported a clean bill of health for zero files;
    * a file that raised UnicodeDecodeError became `return []`, i.e. "no bugs";
    * without `--all`, SHADOW_PARENT findings were filtered out BEFORE counting
      and the summary then printed `0 SHADOW_PARENT` while 14 existed.
"""

import re
import sys
import os
from collections import defaultdict
from pathlib import Path

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

# ============================================================================
# Shared utilities
# ============================================================================

BRACE_OPEN = re.compile(r'\{')
BRACE_CLOSE = re.compile(r'\}')


def extract_functions(lines):
    """Rough function boundary extraction. Returns list of (start, end, lines)."""
    functions = []
    depth = 0
    func_start = None
    func_lines = []

    for i, line in enumerate(lines):
        opens = len(BRACE_OPEN.findall(line))
        closes = len(BRACE_CLOSE.findall(line))

        if depth == 0 and opens > 0:
            func_start = i
            func_lines = []

        if func_start is not None:
            func_lines.append((i, line))

        depth += opens - closes

        if depth <= 0 and func_start is not None:
            functions.append((func_start, i, func_lines))
            func_start = None
            func_lines = []
            depth = 0

    return functions


# ============================================================================
# Checker 1: FindArray receiver bugs
# ============================================================================

# Methods that look up children by name (DataArray and similar)
LOOKUP_METHODS = ['FindArray', 'FindStr', 'FindFloat', 'FindInt', 'FindData', 'FindVar']
LOOKUP_METHODS_RE = '|'.join(LOOKUP_METHODS)

# Match: var = expr->FindArray("key")  or  var = expr->FindArray(key_var)
ASSIGN_RE = re.compile(
    rf'(\w+)\s*=\s*(\w+)->({LOOKUP_METHODS_RE})\(\s*"([^"]+)"\s*'
)

# Match: expr->FindArray("key")  (any context)
CALL_RE = re.compile(
    rf'(\w+)->({LOOKUP_METHODS_RE})\(\s*"([^"]+)"\s*'
)


def scan_findarray(func_lines, filepath, func_start_line):
    """Scan a function's lines for suspicious FindArray receiver patterns."""
    findings = []

    # Track: parent -> [(var_name, key, line_no)]
    stored_children = defaultdict(list)
    # Track: var -> parent it came from
    var_parent = {}
    # Track: parent -> [(key, line_no)] for direct calls
    parent_calls = defaultdict(list)
    # Track: var -> [(key, line_no)] for calls on stored children
    child_calls = defaultdict(list)

    for line_no, line in func_lines:
        # Skip comments
        stripped = line.strip()
        if stripped.startswith('//'):
            continue

        # Find assignments: var = parent->Method("key")
        assigned_on_this_line = set()  # (receiver, method, key) tuples from assignments
        for m in ASSIGN_RE.finditer(line):
            var_name, parent_name, method, key = m.groups()
            stored_children[parent_name].append((var_name, key, line_no))
            var_parent[var_name] = parent_name
            assigned_on_this_line.add((parent_name, method, key))

        # Find all lookup calls (skip ones that are part of assignments)
        for m in CALL_RE.finditer(line):
            receiver, method, key = m.groups()
            # Skip if this call was already captured as an assignment
            if (receiver, method, key) in assigned_on_this_line:
                continue

            if receiver in var_parent:
                # This is a call on a stored child variable
                child_calls[receiver].append((key, line_no))
            else:
                parent_calls[receiver].append((key, line_no))

    # Now detect suspicious patterns
    for parent, children in stored_children.items():
        # DETERMINISM: this used to be a `set`, and it is ITERATED below to build
        # `child_info`, so string hash randomisation drove the order of the
        # emitted findings — four distinct output hashes under four
        # PYTHONHASHSEED values. Sorted list, so two runs agree.
        child_vars = sorted({c[0] for c in children})
        child_var_map = {c[0]: (c[1], c[2]) for c in children}

        # Get direct calls on this parent AFTER the first child assignment
        first_child_line = min(c[2] for c in children)

        for key, call_line in parent_calls.get(parent, []):
            if call_line <= first_child_line:
                continue  # Call before any child was stored — probably fine

            # Check if any child var is used for FindArray (mixed receiver signal)
            any_child_used = any(
                len(child_calls.get(cv, [])) > 0 for cv in child_vars
            )

            # Build the finding
            child_info = []
            for cv in child_vars:
                cv_key, cv_line = child_var_map[cv]
                cv_calls = child_calls.get(cv, [])
                child_info.append({
                    'var': cv,
                    'key': cv_key,
                    'assigned_line': cv_line + 1,
                    'used_for_findarray': len(cv_calls) > 0,
                    'findarray_keys': [c[0] for c in cv_calls],
                })

            severity = 'MIXED_RECEIVER' if any_child_used else 'SHADOW_PARENT'

            findings.append({
                'file': filepath,
                'line': call_line + 1,
                'severity': severity,
                'parent': parent,
                'key': key,
                'children': child_info,
            })

    return findings


# ============================================================================
# Checker 2: ObjDirItr source/dest confusion
# ============================================================================

# Match: ObjDirItr<Type> varname(dirVar, bool)
OBJDIRITER_RE = re.compile(
    r'ObjDirItr<(\w+)>\s+(\w+)\((\w+)'
)

# Match: var->SetName(nameExpr, destDir)
#   SetName's second arg is the destination dir
SETNAME_RE = re.compile(
    r'(\w+)->SetName\(\s*\w+\s*,\s*(\w+)\s*\)'
)

# Match: dir->Reserve(...)
RESERVE_RE = re.compile(
    r'(\w+)->Reserve\('
)

# Match: dir->FindObject(name, ...)
FINDOBJECT_RE = re.compile(
    r'(\w+)->FindObject\(\s*(\w+)'
)

# Match: dir->HashTableSize() or dir->StrTableSize()
TABLESIZE_RE = re.compile(
    r'(\w+)->(HashTableSize|StrTableSize)\(\)'
)


def scan_objdiriter(func_lines, filepath, func_start_line):
    """Scan for ObjDirItr source/dest confusion in merge/transfer blocks."""
    findings = []

    # Collect all relevant operations with line numbers
    iterators = []    # (line_no, type, iter_var, source_dir)
    setnames = []     # (line_no, obj_var, dest_dir)
    reserves = []     # (line_no, dir_var)
    findobjects = []  # (line_no, dir_var)
    tablesize_refs = defaultdict(set)  # dir_var -> set of dirs referenced in size calc

    for line_no, line in func_lines:
        stripped = line.strip()
        if stripped.startswith('//'):
            continue

        for m in OBJDIRITER_RE.finditer(line):
            iter_type, iter_var, source_dir = m.groups()
            iterators.append((line_no, iter_type, iter_var, source_dir))

        for m in SETNAME_RE.finditer(line):
            obj_var, dest_dir = m.groups()
            setnames.append((line_no, obj_var, dest_dir))

        for m in RESERVE_RE.finditer(line):
            reserves.append((line_no, m.group(1)))

        for m in FINDOBJECT_RE.finditer(line):
            findobjects.append((line_no, m.group(1)))

        # Track which dirs contribute to size calculations on a Reserve line
        # e.g. clipsDir->Reserve(clipsDir->HashTableSize() + moveMgrDir->HashTableSize(), ...)
        # We want to know that moveMgrDir is the "source" contributing size
        for m in TABLESIZE_RE.finditer(line):
            ts_dir = m.group(1)
            # Find the Reserve call on the same line to know which dir is being reserved
            reserve_match = RESERVE_RE.search(line)
            if reserve_match:
                reserve_dir = reserve_match.group(1)
                if ts_dir != reserve_dir:
                    tablesize_refs[reserve_dir].add(ts_dir)

    if not iterators:
        return findings

    # --- Check A: iter_dest ---
    # ObjDirItr iterates dir X, then SetName transfers INTO dir X
    # This means you're iterating the destination (likely empty) instead of the source
    for iter_line, iter_type, iter_var, source_dir in iterators:
        # Find SetName calls that use source_dir as destination AND are
        # close to this iterator (within same block, ~30 lines)
        for sn_line, sn_obj, sn_dest in setnames:
            if sn_dest != source_dir:
                continue
            if sn_line < iter_line or sn_line > iter_line + 30:
                continue

            # The iterator's source_dir matches SetName's dest_dir.
            # This means we're iterating the destination dir — suspicious.
            #
            # Exception: if the dir is 'this' or 'Dir()' — self-iteration
            # with self-registration is normal (e.g., Instance::Load)
            if source_dir in ('this',):
                continue

            # Check if there's a FindObject guard on the dest dir between
            # iterator and SetName — this is the standard "check if exists,
            # then transfer" pattern. The FindObject should be on the DEST dir,
            # not the source, so this is actually expected.
            # But the iterator itself should be on the SOURCE dir.

            # Check if a Reserve was called on source_dir before the iterator
            # (further evidence that source_dir is the destination being prepared)
            has_reserve = any(
                r_dir == source_dir and r_line < iter_line
                for r_line, r_dir in reserves
            )

            # Check if tablesize_refs tell us about an expected source dir
            expected_sources = tablesize_refs.get(source_dir, set())

            severity = 'ITER_DEST'
            detail = {
                'file': filepath,
                'line': iter_line + 1,
                'severity': severity,
                'iter_type': iter_type,
                'iter_var': iter_var,
                'iter_source': source_dir,
                'setname_dest': sn_dest,
                'setname_line': sn_line + 1,
                'has_reserve_on_dest': has_reserve,
                'expected_sources': sorted(expected_sources),
            }
            findings.append(detail)
            break  # One finding per iterator

    # --- Check B: parallel_mismatch ---
    # Two structurally similar iterate-and-transfer blocks where one uses
    # a different source dir than the other, suggesting copy-paste confusion
    transfer_blocks = []  # (iter_line, iter_type, source_dir, dest_dir)

    for iter_line, iter_type, iter_var, source_dir in iterators:
        # Find the SetName dest for this iterator's block
        for sn_line, sn_obj, sn_dest in setnames:
            if sn_line < iter_line or sn_line > iter_line + 30:
                continue
            transfer_blocks.append((iter_line, iter_type, source_dir, sn_dest))
            break

    # Look for pairs where dest is the same but source differs
    for i, (line_a, type_a, src_a, dst_a) in enumerate(transfer_blocks):
        for line_b, type_b, src_b, dst_b in transfer_blocks[i + 1:]:
            # Same destination, different source — could be intentional
            # (different source dirs merging into same target)
            # But flag if one source == dest (iterating the destination)
            if dst_a == dst_b and src_a != src_b:
                bad_src = None
                if src_a == dst_a:
                    bad_src = src_a
                    bad_line = line_a
                    good_src = src_b
                elif src_b == dst_b:
                    bad_src = src_b
                    bad_line = line_b
                    good_src = src_a

                if bad_src:
                    # Already caught by ITER_DEST, skip duplicate
                    continue

            # Different destinations with shared source context — less common,
            # check for source == one of the dests
            if src_a == dst_b or src_b == dst_a:
                # Source of one block is the dest of the other — suspicious
                if src_a == dst_b:
                    bad_line = line_a
                    detail_msg = (f"Block at line {line_a + 1} iterates {src_a} "
                                  f"which is the destination of the block at line {line_b + 1}")
                else:
                    bad_line = line_b
                    detail_msg = (f"Block at line {line_b + 1} iterates {src_b} "
                                  f"which is the destination of the block at line {line_a + 1}")

                findings.append({
                    'file': filepath,
                    'line': bad_line + 1,
                    'severity': 'PARALLEL_MISMATCH',
                    'detail': detail_msg,
                    'block_a': {'line': line_a + 1, 'type': type_a, 'source': src_a, 'dest': dst_a},
                    'block_b': {'line': line_b + 1, 'type': type_b, 'source': src_b, 'dest': dst_b},
                })

    return findings


# ============================================================================
# File scanning
# ============================================================================

def scan_file(filepath, checks=('findarray', 'objdiriter')):
    """Scan a single file for suspicious patterns.

    Returns `(findings, disposition)`.  The disposition is one of:

      'examined'                   the file passed a relevance gate and was parsed
      'unreadable'                 open()/decode failed — NOT the same as "clean"
      'gate-missed-non-findarray'  >= 2 lookup calls, but < 2 of them spelled
                                   `FindArray`, so the FindArray gate skips it
      'not-relevant'               no lookup pattern of any kind

    The old signature returned a bare `[]` for the last three, which made
    "this file could not be decoded" and "this file has no bugs" print
    identically — and made the gate's blind spot unobservable.
    """
    try:
        with open(filepath) as f:
            lines = f.readlines()
    except (IOError, UnicodeDecodeError):
        return [], 'unreadable'

    # Quick relevance check
    content = ''.join(lines)
    has_findarray = content.count('FindArray') >= 2
    has_objdiriter = 'ObjDirItr' in content and 'SetName' in content

    if not has_findarray and not has_objdiriter:
        # TODO(heuristic): WIDEN THIS GATE — separate work, deliberately not done
        # here.  `LOOKUP_METHODS` (and therefore ASSIGN_RE / CALL_RE) covers six
        # methods: FindArray, FindStr, FindFloat, FindInt, FindData, FindVar —
        # but the gate above only counts the string 'FindArray'.  A file that
        # does `DataArray *a = cfg->FindArray("x"); ... cfg->FindStr("y")` is
        # skipped before the regexes ever run.  In this tree 127 files have >= 2
        # lookup calls; only 88 have >= 2 spelled FindArray, so 39 files (30.7%)
        # are invisible to the receiver check.  Widening the gate CHANGES WHAT
        # THIS SCANNER FINDS, so it is counted here and left for its own change.
        n_lookups = sum(content.count(m) for m in LOOKUP_METHODS)
        if n_lookups >= 2:
            return [], 'gate-missed-non-findarray'
        return [], 'not-relevant'

    functions = extract_functions(lines)
    all_findings = []

    for func_start, func_end, func_lines in functions:
        if 'findarray' in checks and has_findarray:
            all_findings.extend(scan_findarray(func_lines, filepath, func_start))
        if 'objdiriter' in checks and has_objdiriter:
            all_findings.extend(scan_objdiriter(func_lines, filepath, func_start))

    return all_findings, 'examined'


# ============================================================================
# Output
# ============================================================================

def print_findarray_findings(findings, show_shadow=False):
    """Print FindArray receiver findings.

    `findings` must be the UNFILTERED list; SHADOW_PARENT suppression happens
    here, at print time, so the count is taken before the filter.  Filtering
    them out upstream is what let the summary print `0 SHADOW_PARENT` while 14
    existed.
    """
    mixed = [f for f in findings if f['severity'] == 'MIXED_RECEIVER']
    shadow = [f for f in findings if f['severity'] == 'SHADOW_PARENT']

    if mixed:
        print(f"=== MIXED_RECEIVER ({len(mixed)} findings) ===")
        print("Parent used for FindArray when a child variable exists AND is also used for FindArray.\n")
        for f in mixed:
            print(f"  {f['file']}:{f['line']}")
            print(f"    {f['parent']}->FindArray(\"{f['key']}\")  <-- suspicious")
            for c in f['children']:
                used = f" (also used: {', '.join(c['findarray_keys'])})" if c['used_for_findarray'] else ""
                print(f"    {c['var']} = {f['parent']}->FindArray(\"{c['key']}\") at line {c['assigned_line']}{used}")
            print()

    if shadow and not show_shadow:
        print(f"=== SHADOW_PARENT: {len(shadow)} finding(s) SUPPRESSED (use --all to show) ===\n")

    if shadow and show_shadow:
        print(f"\n=== SHADOW_PARENT ({len(shadow)} findings) ===")
        print("Parent used for FindArray when a child variable exists (child not used for FindArray).\n")
        for f in shadow:
            print(f"  {f['file']}:{f['line']}")
            print(f"    {f['parent']}->FindArray(\"{f['key']}\")  <-- possibly suspicious")
            for c in f['children']:
                print(f"    {c['var']} = {f['parent']}->FindArray(\"{c['key']}\") at line {c['assigned_line']}")
            print()

    return len(mixed), len(shadow)


def print_objdiriter_findings(findings):
    """Print ObjDirItr source/dest findings."""
    iter_dest = [f for f in findings if f['severity'] == 'ITER_DEST']
    parallel = [f for f in findings if f['severity'] == 'PARALLEL_MISMATCH']

    if iter_dest:
        print(f"=== ITER_DEST ({len(iter_dest)} findings) ===")
        print("ObjDirItr iterates the same dir that objects are transferred INTO.\n")
        for f in iter_dest:
            print(f"  {f['file']}:{f['line']}")
            print(f"    ObjDirItr<{f['iter_type']}> {f['iter_var']}({f['iter_source']}, ...)")
            print(f"    SetName(name, {f['setname_dest']}) at line {f['setname_line']}")
            if f['has_reserve_on_dest']:
                print(f"    ^ Reserve() called on {f['iter_source']} before iteration (confirms it's the dest)")
            if f['expected_sources']:
                print(f"    ^ Expected source dir(s): {', '.join(f['expected_sources'])}")
            print()

    if parallel:
        print(f"=== PARALLEL_MISMATCH ({len(parallel)} findings) ===")
        print("Structurally similar transfer blocks with cross-wired source/dest.\n")
        for f in parallel:
            print(f"  {f['file']}:{f['line']}")
            print(f"    {f['detail']}")
            a, b = f['block_a'], f['block_b']
            print(f"    Block A (line {a['line']}): ObjDirItr<{a['type']}>({a['source']}) -> SetName(_, {a['dest']})")
            print(f"    Block B (line {b['line']}): ObjDirItr<{b['type']}>({b['source']}) -> SetName(_, {b['dest']})")
            print()

    return len(iter_dest), len(parallel)


def resolve_paths(raw_paths):
    """Turn CLI path arguments into (files, resolved, errors).

    COVERAGE FIX, not a heuristic change.  The default `src/` is RELATIVE, and
    the old loop was

        if p.is_dir():   ...
        elif p.is_file(): ...
                                # <- no else

    so from any cwd but the repo root the path simply did not exist, the file
    list came out EMPTY, and the scan printed "No suspicious receiver confusion
    patterns found."  A missing input path is now a LOUD ERROR.  As a
    convenience a relative path that does not exist in the cwd is retried
    against the repo root (announced on stderr), because the documented
    invocation is `python3 scripts/analysis/findarray_receiver_scan.py` with the
    implied `src/` — but if neither location exists we stop, we do not scan zero
    files and call it clean.
    """
    files, resolved, errors = [], [], []
    for raw in raw_paths:
        p = Path(raw)
        if not p.exists() and not p.is_absolute():
            alt = Path(REPO) / raw
            if alt.exists():
                print(f"[findarray_receiver_scan] {raw!r} not found in {os.getcwd()}; "
                      f"using {alt}", file=sys.stderr)
                p = alt
        if p.is_dir():
            found = sorted(set(list(p.rglob('*.cpp')) + list(p.rglob('*.h'))))
            if not found:
                errors.append(f"{p}: directory contains no .cpp/.h files")
            files.extend(found)
            resolved.append(str(p))
        elif p.is_file():
            files.append(p)
            resolved.append(str(p))
        else:
            errors.append(f"{raw}: no such file or directory "
                          f"(cwd={os.getcwd()}, repo={REPO})")
    return sorted(set(files)), resolved, errors


def main():
    import argparse
    parser = argparse.ArgumentParser(
        description='Scan for receiver confusion bugs (FindArray + ObjDirItr)')
    parser.add_argument('paths', nargs='*', default=['src/'],
                        help='Directories or files to scan')
    parser.add_argument('--all', action='store_true',
                        help='Show weaker signals (SHADOW_PARENT)')
    parser.add_argument('--check', choices=['findarray', 'objdiriter', 'both'],
                        default='both',
                        help='Which checker(s) to run (default: both)')
    parser.add_argument('--json', action='store_true',
                        help='Output as JSON')
    add_coverage_args(parser)
    args = parser.parse_args()

    checks = set()
    if args.check in ('findarray', 'both'):
        checks.add('findarray')
    if args.check in ('objdiriter', 'both'):
        checks.add('objdiriter')

    files, resolved, path_errors = resolve_paths(args.paths)
    if path_errors:
        for e in path_errors:
            print(f"ERROR: {e}", file=sys.stderr)
        print("ERROR: refusing to scan — an input path did not resolve. A scan of "
              "zero files is not a clean result.", file=sys.stderr)
        sys.exit(2)

    cov = CoverageReport("findarray_receiver_scan", args=args)
    cov.universe(len(files), "source files (*.cpp/*.h) under the requested paths")
    cov.extra("paths", sorted(resolved))
    cov.extra("checks", sorted(checks))

    all_findings = []
    for f in files:
        findings, disposition = scan_file(str(f), checks)
        if disposition == 'examined':
            cov.examine()
            all_findings.extend(findings)
        elif disposition == 'unreadable':
            cov.drop('file-unreadable', 1,
                     note='open()/decode failed — NOT the same as "no bugs here"')
        elif disposition == 'gate-missed-non-findarray':
            cov.drop('relevance-gate-counts-only-FindArray', 1,
                     note='>=2 lookups but <2 spelled FindArray; see TODO(heuristic) '
                          'in scan_file — widening the gate is separate work')
        else:
            cov.drop('no-lookup-pattern', 1,
                     note='<2 lookup calls and no ObjDirItr+SetName')

    # DETERMINISM: findings arrive per-file in rglob order and per-function in
    # dict order. Pin a total order with the file path as the primary key.
    all_findings.sort(key=lambda f: (f['file'], f['line'], f['severity'],
                                     f.get('parent', ''), f.get('key', '')))

    # Count BEFORE any display filter. The old code filtered SHADOW_PARENT out
    # of `all_findings` here, so the summary's shadow_count was structurally
    # pinned to 0 whenever --all was off.
    by_sev = {}
    for f in all_findings:
        by_sev[f['severity']] = by_sev.get(f['severity'], 0) + 1
    for sev in ('MIXED_RECEIVER', 'SHADOW_PARENT', 'ITER_DEST', 'PARALLEL_MISMATCH'):
        cov.extra(f"findings_{sev}", by_sev.get(sev, 0))
    if not args.all and by_sev.get('SHADOW_PARENT'):
        cov.note(f"{by_sev['SHADOW_PARENT']} SHADOW_PARENT finding(s) exist and are "
                 f"SUPPRESSED from the listing (use --all); they are still counted here")

    if args.json:
        import json
        shown = all_findings if args.all else [
            f for f in all_findings if f['severity'] != 'SHADOW_PARENT']
        # The payload is now an OBJECT, not a bare list: a consumer handed only
        # a list has no way to learn how many files were skipped, how many were
        # unreadable, or that SHADOW_PARENT rows were withheld. `findings` holds
        # exactly what the old top-level list held.
        print(json.dumps({
            "findings": shown,
            "counts_before_display_filter": dict(sorted(by_sev.items())),
            "_coverage": cov.as_dict(),
        }, indent=2))
        sys.exit(cov.emit())

    if not all_findings:
        print(f"No suspicious receiver confusion patterns found "
              f"(in the {cov.as_dict()['examined']} file(s) that passed the relevance "
              f"gate, out of {len(files)} scanned — see the COVERAGE block).")
        sys.exit(cov.emit())

    # Split by checker type
    fa_findings = [f for f in all_findings
                   if f['severity'] in ('MIXED_RECEIVER', 'SHADOW_PARENT')]
    od_findings = [f for f in all_findings
                   if f['severity'] in ('ITER_DEST', 'PARALLEL_MISMATCH')]

    mixed_count = shadow_count = 0
    iter_count = parallel_count = 0

    if fa_findings:
        mixed_count, shadow_count = print_findarray_findings(
            fa_findings, show_shadow=args.all)

    if od_findings:
        if fa_findings:
            print()
        iter_count, parallel_count = print_objdiriter_findings(od_findings)

    # Summary
    parts = []
    if 'findarray' in checks:
        suppressed = "" if args.all else " (suppressed, use --all)"
        parts.append(f"{mixed_count} MIXED_RECEIVER, "
                     f"{shadow_count} SHADOW_PARENT{suppressed if shadow_count else ''}")
    if 'objdiriter' in checks:
        parts.append(f"{iter_count} ITER_DEST, {parallel_count} PARALLEL_MISMATCH")
    d = cov.as_dict()
    print(f"Total: {', '.join(parts)}")
    print(f"  ...from {d['examined']} file(s) examined out of {d['universe']} scanned "
          f"({d['dropped_total']} dropped — see the COVERAGE block for why)")

    sys.exit(cov.emit())


if __name__ == '__main__':
    main()
