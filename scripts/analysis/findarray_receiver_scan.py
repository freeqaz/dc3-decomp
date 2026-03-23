#!/usr/bin/env python3
"""Scan for potential FindArray receiver bugs.

Detects the pattern where a FindArray result is stored in a variable,
but later the PARENT object is used for another FindArray call where
the child variable would be the correct receiver.

Example bug (MetagameRank.cpp):
    DataArray *taskArr = rankCfg->FindArray("tasks");
    gOneTimeTasks = rankCfg->FindArray("one_time");  // BUG: should be taskArr->
    gRepeatableTasks = taskArr->FindArray("repeatable");

Heuristics:
  1. "mixed receiver" — same parent used for FindArray after a child var
     is stored AND that child var is also used for FindArray (strongest signal)
  2. "shadow parent" — same parent used for FindArray after a child var
     is stored (weaker signal, more false positives)
"""

import re
import sys
import os
from collections import defaultdict
from pathlib import Path

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

# Match function-like boundaries (rough: type name(...) {)
FUNC_START_RE = re.compile(r'^[A-Za-z_].*\)\s*\{?\s*$')
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


def scan_function(func_lines, filepath, func_start_line):
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


def scan_file(filepath):
    """Scan a single file for suspicious FindArray patterns."""
    try:
        with open(filepath) as f:
            lines = f.readlines()
    except (IOError, UnicodeDecodeError):
        return []

    # Quick check: need at least 2 FindArray calls
    findarray_count = sum(1 for l in lines if 'FindArray' in l)
    if findarray_count < 2:
        return []

    functions = extract_functions(lines)
    all_findings = []

    for func_start, func_end, func_lines in functions:
        findings = scan_function(func_lines, filepath, func_start)
        all_findings.extend(findings)

    return all_findings


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Scan for FindArray receiver bugs')
    parser.add_argument('paths', nargs='*', default=['src/'],
                        help='Directories or files to scan')
    parser.add_argument('--all', action='store_true',
                        help='Show SHADOW_PARENT findings too (more false positives)')
    parser.add_argument('--json', action='store_true',
                        help='Output as JSON')
    args = parser.parse_args()

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
        findings = scan_file(str(f))
        all_findings.extend(findings)

    if not args.all:
        all_findings = [f for f in all_findings if f['severity'] == 'MIXED_RECEIVER']

    if args.json:
        import json
        print(json.dumps(all_findings, indent=2))
        return

    if not all_findings:
        print("No suspicious FindArray receiver patterns found.")
        return

    # Group by severity
    mixed = [f for f in all_findings if f['severity'] == 'MIXED_RECEIVER']
    shadow = [f for f in all_findings if f['severity'] == 'SHADOW_PARENT']

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

    if shadow and args.all:
        print(f"\n=== SHADOW_PARENT ({len(shadow)} findings) ===")
        print("Parent used for FindArray when a child variable exists (child not used for FindArray).\n")
        for f in shadow:
            print(f"  {f['file']}:{f['line']}")
            print(f"    {f['parent']}->FindArray(\"{f['key']}\")  <-- possibly suspicious")
            for c in f['children']:
                print(f"    {c['var']} = {f['parent']}->FindArray(\"{c['key']}\") at line {c['assigned_line']}")
            print()

    # Summary
    print(f"Total: {len(mixed)} MIXED_RECEIVER, {len(shadow)} SHADOW_PARENT")


if __name__ == '__main__':
    main()
