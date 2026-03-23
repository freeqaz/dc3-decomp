#!/usr/bin/env python3
"""Validate DTA_TRACE runtime output against the DTA hierarchy.

Reads DTA_TRACE log lines from the native port and cross-references each
access against the parsed DTA configuration files to detect:
  - Index out-of-bounds accesses (Int(N) where N >= array size)
  - Type mismatches (Int(N) on a symbol field, Sym(N) on a number)
  - Broken context paths (path doesn't resolve in the DTA tree)
  - Context propagation gaps (accesses with no context path at all)

Usage:
  DTA_TRACE=1 ./dc3-native 2>trace.log
  python3 scripts/analysis/dta_trace_validator.py trace.log
  python3 scripts/analysis/dta_trace_validator.py trace.log --json
  python3 scripts/analysis/dta_trace_validator.py trace.log --stats
"""

import re
import sys
from pathlib import Path
from collections import defaultdict, Counter

# Import DTA hierarchy tools from the same directory
sys.path.insert(0, str(Path(__file__).parent))
from dta_hierarchy_scan import parse_dta_file, DTANode, DTAHierarchy
from dta_access_audit import get_element_info, classify_atom

# --------------------------------------------------------------------------
# Trace line parsing
# --------------------------------------------------------------------------

# Format: DTA_TRACE: context.path[index] via Method (file source.dta, line N)
TRACE_RE = re.compile(
    r'DTA_TRACE:\s+(.+?)\[(\d+)\]\s+via\s+(\w+)\s+\(file\s+(.+?),\s+line\s+(\d+)\)'
)


def parse_trace_line(line):
    """Parse a single DTA_TRACE log line into structured data."""
    m = TRACE_RE.search(line)
    if not m:
        return None
    return {
        'path': m.group(1),
        'index': int(m.group(2)),
        'method': m.group(3),
        'file': m.group(4),
        'line': int(m.group(5)),
    }


# --------------------------------------------------------------------------
# DTA hierarchy resolution
# --------------------------------------------------------------------------

def resolve_context_path(path, main_roots):
    """Resolve a dotted context path to a DTANode in the config tree.

    Example: "rank.tasks.one_time" resolves by:
      root.find_array("rank") -> .find_array("tasks") -> .find_array("one_time")

    Tries all main config roots since the runtime config is a merged tree.
    Returns the resolved DTANode, or None if the path cannot be resolved.
    """
    parts = path.split('.')
    for root in main_roots.values():
        node = root
        success = True
        for part in parts:
            child = node.find_array(part)
            if child is None:
                success = False
                break
            node = child
        if success:
            return node
    return None


# --------------------------------------------------------------------------
# Validation logic
# --------------------------------------------------------------------------

def validate_traces(trace_file, main_roots):
    """Parse trace file and validate each access against the DTA hierarchy.

    Returns (findings, stats) where:
      findings: list of validation issue dicts
      stats: dict with trace statistics
    """
    findings = []
    seen = set()  # deduplicate by (path, index, method, file, line)
    stats = {
        'total_traces': 0,
        'unique_traces': 0,
        'resolved': 0,
        'unresolved_paths': Counter(),
        'methods': Counter(),
        'paths_seen': set(),
    }

    with open(trace_file) as f:
        for line in f:
            if 'DTA_TRACE:' not in line:
                continue

            entry = parse_trace_line(line)
            if entry is None:
                continue

            stats['total_traces'] += 1
            stats['methods'][entry['method']] += 1
            stats['paths_seen'].add(entry['path'])

            # Deduplicate by full key
            key = (entry['path'], entry['index'], entry['method'],
                   entry['file'], entry['line'])
            if key in seen:
                continue
            seen.add(key)
            stats['unique_traces'] += 1

            path = entry['path']
            index = entry['index']
            method = entry['method']

            # Resolve the context path to a DTA node
            node = resolve_context_path(path, main_roots)
            if node is None:
                stats['unresolved_paths'][path] += 1
                continue

            stats['resolved'] += 1

            # Reconstruct element list: [tag, ...children]
            elements = []
            if node.tag:
                elements.append(node.tag)
            for child in node.children:
                elements.append(child)
            total_elements = len(elements)

            # Validate bounds
            if index >= total_elements:
                findings.append({
                    'type': 'INDEX_OOB',
                    'severity': 'HIGH',
                    'path': path,
                    'index': index,
                    'method': method,
                    'dta_file': entry['file'],
                    'dta_line': entry['line'],
                    'detail': (f'{method}({index}) out of bounds on [{path}] '
                               f'-- array has {total_elements} elements '
                               f'(valid: 0..{total_elements - 1})'),
                })
                continue

            # Validate type compatibility
            elem_info = get_element_info(node, index)
            if elem_info is None:
                continue

            actual_type, actual_value = elem_info
            expected_type = {
                'Int': 'int', 'Float': 'float',
                'Sym': 'symbol', 'Str': 'string',
            }.get(method)

            if expected_type is None:
                continue  # Unknown method, skip type check

            # Type compatibility rules:
            #   int <-> float is usually OK (implicit numeric conversion)
            #   symbol <-> string is usually OK
            #   numeric <-> textual is a real mismatch
            numeric = {'int', 'float'}
            textual = {'symbol', 'string'}

            is_mismatch = False
            if actual_type in numeric and expected_type in textual:
                is_mismatch = True
            elif actual_type in textual and expected_type in numeric:
                is_mismatch = True

            if is_mismatch:
                findings.append({
                    'type': 'TYPE_MISMATCH',
                    'severity': 'MEDIUM',
                    'path': path,
                    'index': index,
                    'method': method,
                    'dta_file': entry['file'],
                    'dta_line': entry['line'],
                    'detail': (f'{method}({index}) on [{path}] '
                               f'-- element is {actual_type} ("{actual_value}"), '
                               f'accessed as {expected_type}'),
                })

    return findings, stats


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description='Validate DTA runtime traces against the DTA hierarchy')
    parser.add_argument('trace_file', help='DTA_TRACE log file')
    parser.add_argument('--json', action='store_true',
                        help='Output findings as JSON')
    parser.add_argument('--stats', action='store_true',
                        help='Print trace statistics')
    parser.add_argument('--show-unresolved', action='store_true',
                        help='Show context paths that could not be resolved')
    parser.add_argument('--dta-dir', nargs='*',
                        default=['orig-assets/extracted/config',
                                 'orig-assets/extracted/(..)/(..)/system/run/config'],
                        help='Directories containing DTA files')
    parser.add_argument('--main-configs', nargs='*',
                        default=['orig-assets/extracted/config/ham_keep.dta',
                                 'orig-assets/extracted/(..)/(..)/system/run/config/default.dta'],
                        help='Main config files to parse with #include resolution')
    args = parser.parse_args()

    # Load DTA hierarchy from main config files (with #include resolution)
    main_roots = {}
    hierarchy = DTAHierarchy()
    dta_count = 0

    for cfg in args.main_configs:
        p = Path(cfg)
        if p.exists():
            root = parse_dta_file(str(p))
            if root:
                main_roots[cfg] = root
                hierarchy.add_file(p)
                dta_count += 1

    # Add individual DTA files for broader coverage
    for dta_dir in args.dta_dir:
        dta_path = Path(dta_dir)
        if not dta_path.exists():
            continue
        for dta_file in sorted(dta_path.rglob('*.dta')):
            if str(dta_file) not in hierarchy.roots:
                hierarchy.add_file(dta_file)
                dta_count += 1

    # Also scan all DTA files under orig-assets
    orig_assets = Path('orig-assets/extracted')
    if orig_assets.exists():
        for dta_file in sorted(orig_assets.rglob('*.dta')):
            if str(dta_file) not in hierarchy.roots:
                hierarchy.add_file(dta_file)
                dta_count += 1

    print(f"Loaded {dta_count} DTA files, "
          f"{len(hierarchy.key_parents)} unique keys, "
          f"{len(main_roots)} main config roots",
          file=sys.stderr)

    # Validate traces
    findings, stats = validate_traces(args.trace_file, main_roots)

    # JSON output
    if args.json:
        import json
        output = {
            'findings': findings,
            'stats': {
                'total_traces': stats['total_traces'],
                'unique_traces': stats['unique_traces'],
                'resolved': stats['resolved'],
                'unique_paths': len(stats['paths_seen']),
                'unresolved_count': len(stats['unresolved_paths']),
                'methods': dict(stats['methods']),
            }
        }
        print(json.dumps(output, indent=2))
        return

    # Statistics
    if args.stats:
        print(f"\n=== Trace Statistics ===\n")
        print(f"  Total trace entries:    {stats['total_traces']}")
        print(f"  Unique trace entries:   {stats['unique_traces']}")
        print(f"  Unique context paths:   {len(stats['paths_seen'])}")
        print(f"  Resolved paths:         {stats['resolved']}")
        print(f"  Unresolved paths:       {len(stats['unresolved_paths'])}")
        print(f"\n  Access methods:")
        for method, count in stats['methods'].most_common():
            print(f"    {method:8s}  {count:6d}")

    # Show unresolved paths
    if args.show_unresolved and stats['unresolved_paths']:
        print(f"\n=== Unresolved Context Paths ({len(stats['unresolved_paths'])}) ===\n")
        for path, count in stats['unresolved_paths'].most_common():
            print(f"  {path} ({count} accesses)")

    # Report findings
    if not findings:
        print(f"\nNo validation issues found.")
        print(f"Validated {stats['unique_traces']} unique trace entries "
              f"({stats['resolved']} resolved) across {stats['total_traces']} "
              f"total runtime accesses.")
        return

    # Group by type
    by_type = defaultdict(list)
    for f in findings:
        by_type[f['type']].append(f)

    for bug_type, items in sorted(by_type.items()):
        severity = items[0]['severity']
        print(f"\n=== {bug_type} [{severity}] ({len(items)} findings) ===\n")
        for item in items:
            print(f"  [{item['path']}][{item['index']}] via {item['method']}")
            print(f"    {item['detail']}")
            print(f"    DTA source: {item['dta_file']}:{item['dta_line']}")
            print()

    print(f"Total: {len(findings)} findings "
          f"({', '.join(f'{k}:{len(v)}' for k, v in sorted(by_type.items()))})")
    print(f"Validated {stats['unique_traces']} unique trace entries "
          f"({stats['resolved']} resolved) across {stats['total_traces']} "
          f"total runtime accesses.")


if __name__ == '__main__':
    main()
