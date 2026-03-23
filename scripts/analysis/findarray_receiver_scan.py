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
"""

import re
import sys
import os
from collections import defaultdict
from pathlib import Path

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
        child_vars = {c[0] for c in children}
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
    """Scan a single file for suspicious patterns."""
    try:
        with open(filepath) as f:
            lines = f.readlines()
    except (IOError, UnicodeDecodeError):
        return []

    # Quick relevance check
    content = ''.join(lines)
    has_findarray = content.count('FindArray') >= 2
    has_objdiriter = 'ObjDirItr' in content and 'SetName' in content

    if not has_findarray and not has_objdiriter:
        return []

    functions = extract_functions(lines)
    all_findings = []

    for func_start, func_end, func_lines in functions:
        if 'findarray' in checks and has_findarray:
            all_findings.extend(scan_findarray(func_lines, filepath, func_start))
        if 'objdiriter' in checks and has_objdiriter:
            all_findings.extend(scan_objdiriter(func_lines, filepath, func_start))

    return all_findings


# ============================================================================
# Output
# ============================================================================

def print_findarray_findings(findings, show_shadow=False):
    """Print FindArray receiver findings."""
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
    args = parser.parse_args()

    checks = set()
    if args.check in ('findarray', 'both'):
        checks.add('findarray')
    if args.check in ('objdiriter', 'both'):
        checks.add('objdiriter')

    files = []
    for p in args.paths:
        p = Path(p)
        if p.is_dir():
            files.extend(p.rglob('*.cpp'))
            files.extend(p.rglob('*.h'))
        elif p.is_file():
            files.append(p)

    all_findings = []
    for f in sorted(files):
        findings = scan_file(str(f), checks)
        all_findings.extend(findings)

    # Filter weak signals unless --all
    if not args.all:
        all_findings = [f for f in all_findings if f['severity'] != 'SHADOW_PARENT']

    if args.json:
        import json
        print(json.dumps(all_findings, indent=2))
        return

    if not all_findings:
        print("No suspicious receiver confusion patterns found.")
        return

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
        parts.append(f"{mixed_count} MIXED_RECEIVER, {shadow_count} SHADOW_PARENT")
    if 'objdiriter' in checks:
        parts.append(f"{iter_count} ITER_DEST, {parallel_count} PARALLEL_MISMATCH")
    print(f"Total: {', '.join(parts)}")


if __name__ == '__main__':
    main()
