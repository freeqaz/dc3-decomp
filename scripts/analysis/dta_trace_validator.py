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

import os
import re
import sys
from pathlib import Path
from collections import defaultdict, Counter

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

# Import DTA hierarchy tools from the same directory
sys.path.insert(0, str(Path(__file__).parent))
from dta_hierarchy_scan import (
    parse_dta_file, DTANode, DTAHierarchy,
    empty_corpus_banner, EXIT_EMPTY_CORPUS, PARSE_STATS, UNRESOLVED_INCLUDES,
)
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

def validate_traces(trace_file, main_roots, cov=None):
    """Parse trace file and validate each access against the DTA hierarchy.

    Returns (findings, stats) where:
      findings: list of validation issue dicts
      stats: dict with trace statistics

    `cov` is an optional CoverageReport; the denominator is the number of lines
    that CLAIM to be traces (contain the `DTA_TRACE:` marker), NOT the number
    that happened to parse. If the native emitter's format ever drifts, 40k
    malformed lines used to vanish before `total_traces += 1` and the tool
    reported "Validated 0 ... across 0" — a format break rendering as a
    successful empty run.
    """
    findings = []
    seen = set()  # deduplicate by (path, index, method, file, line)
    stats = {
        'total_traces': 0,
        'unique_traces': 0,
        'resolved': 0,
        'marker_lines': 0,       # lines containing 'DTA_TRACE:' — the denominator
        'malformed_lines': 0,    # marker present but TRACE_RE did not match
        'malformed_samples': [],
        'duplicate_traces': 0,
        'unknown_method': 0,
        'element_info_missing': 0,
        'non_trace_lines': 0,
        'unresolved_paths': Counter(),
        'methods': Counter(),
        'paths_seen': set(),
    }

    def drop(reason, note=""):
        if cov is not None:
            cov.drop(reason, note=note)

    with open(trace_file) as f:
        for line in f:
            if 'DTA_TRACE:' not in line:
                stats['non_trace_lines'] += 1
                continue

            stats['marker_lines'] += 1
            entry = parse_trace_line(line)
            if entry is None:
                # A line that SAYS DTA_TRACE but does not match TRACE_RE is an
                # emitter/parser format mismatch, not an absence of data.
                stats['malformed_lines'] += 1
                if len(stats['malformed_samples']) < 5:
                    stats['malformed_samples'].append(line.rstrip()[:200])
                drop("trace-line-malformed",
                     note="line contains DTA_TRACE: but does not match TRACE_RE "
                          "— emitter format drift")
                continue

            stats['total_traces'] += 1
            stats['methods'][entry['method']] += 1
            stats['paths_seen'].add(entry['path'])

            # Deduplicate by full key
            key = (entry['path'], entry['index'], entry['method'],
                   entry['file'], entry['line'])
            if key in seen:
                stats['duplicate_traces'] += 1
                drop("duplicate-trace-entry",
                     note="same (path,index,method,file,line) already validated")
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
                drop("context-path-unresolvable",
                     note="dotted path does not resolve in any main config root")
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
                if cov is not None:
                    cov.examine()      # bounds check ran and FIRED
                continue

            # Validate type compatibility
            elem_info = get_element_info(node, index)
            if elem_info is None:
                # In bounds by the count above but the element could not be
                # described — the type check never ran on this row.
                stats['element_info_missing'] += 1
                drop("element-info-unavailable",
                     note="in bounds but get_element_info() returned None; "
                          "the TYPE check did not run")
                continue

            actual_type, actual_value = elem_info
            expected_type = {
                'Int': 'int', 'Float': 'float',
                'Sym': 'symbol', 'Str': 'string',
            }.get(method)

            if expected_type is None:
                # Method outside Int/Float/Sym/Str: bounds were checked, the
                # TYPE check was not. Counted, so "no findings" cannot silently
                # mean "every access used a method we do not understand".
                stats['unknown_method'] += 1
                drop("method-not-type-checked",
                     note="method is not Int/Float/Sym/Str — bounds checked, "
                          "type check skipped")
                continue

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

            if cov is not None:
                cov.examine()          # both checks actually ran on this row

    if cov is not None:
        # Tallied independently of the dispositions above: a `continue` that
        # forgets to drop() surfaces as UNACCOUNTED instead of shrinking the
        # denominator.
        cov.universe(stats['marker_lines'], "log lines containing 'DTA_TRACE:'")
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
    add_coverage_args(parser)
    args = parser.parse_args()

    # Load DTA hierarchy from main config files (with #include resolution)
    main_roots = {}
    hierarchy = DTAHierarchy()
    dta_count = 0
    searched = []
    missing_inputs = []

    for cfg in args.main_configs:
        p = Path(cfg)
        if p.exists():
            root = parse_dta_file(str(p))
            if root:
                main_roots[cfg] = root
                hierarchy.add_file(p)
                dta_count += 1
                searched.append(f"{cfg}  [main config: parsed]")
            else:
                missing_inputs.append(cfg)
                searched.append(f"{cfg}  [main config: UNPARSEABLE]")
        else:
            # Softened relative to its siblings because `resolved` IS printed
            # below — but a missing main config still means resolve_context_path
            # can never succeed, so say so out loud.
            print(f"Warning: main config {cfg} does not exist", file=sys.stderr)
            missing_inputs.append(cfg)
            searched.append(f"{cfg}  [main config: MISSING]")

    # Add individual DTA files for broader coverage
    for dta_dir in args.dta_dir:
        dta_path = Path(dta_dir)
        if not dta_path.exists():
            print(f"Warning: {dta_dir} does not exist", file=sys.stderr)
            missing_inputs.append(dta_dir)
            searched.append(f"{dta_dir}  [dta dir: MISSING]")
            continue
        n = 0
        for dta_file in sorted(dta_path.rglob('*.dta')):
            if str(dta_file) not in hierarchy.roots:
                hierarchy.add_file(dta_file)
                dta_count += 1
                n += 1
        searched.append(f"{dta_dir}  [dta dir: {n} files]")

    # Also scan all DTA files under orig-assets
    orig_assets = Path('orig-assets/extracted')
    if orig_assets.exists():
        n = 0
        for dta_file in sorted(orig_assets.rglob('*.dta')):
            if str(dta_file) not in hierarchy.roots:
                hierarchy.add_file(dta_file)
                dta_count += 1
                n += 1
        searched.append(f"{orig_assets}  [tree: {n} files]")
    else:
        print(f"Warning: {orig_assets} does not exist", file=sys.stderr)
        missing_inputs.append(str(orig_assets))
        searched.append(f"{orig_assets}  [tree: MISSING]")

    key_count = len(hierarchy.key_parents)
    print(f"Loaded {dta_count} DTA files, "
          f"{key_count} unique keys, "
          f"{len(main_roots)} main config roots",
          file=sys.stderr)

    cov = CoverageReport("dta_trace_validator", args=args)
    cov.extra("dta_files_parsed", dta_count)
    cov.extra("dta_unique_keys", key_count)
    cov.extra("main_config_roots", len(main_roots))
    cov.extra("missing_inputs", sorted(missing_inputs))
    cov.extra("parse_stats", dict(sorted(PARSE_STATS.items())))
    cov.extra("unresolved_includes", dict(sorted(UNRESOLVED_INCLUDES.items())))
    if missing_inputs:
        cov.note(f"{len(missing_inputs)} declared corpus input(s) DO NOT EXIST: "
                 + ", ".join(sorted(missing_inputs)))
    if not main_roots:
        cov.note("ZERO main config roots — resolve_context_path() cannot "
                 "succeed for any trace line")

    # ---- the empty-corpus gate: a clean verdict is FORBIDDEN from here ----
    banner = empty_corpus_banner("dta_trace_validator", dta_count, key_count, searched)
    if banner is not None:
        print(banner)                       # STDOUT — survives a redirect
        cov.universe(0, "DTA files parsed")
        sys.exit(cov.emit() or EXIT_EMPTY_CORPUS)

    # Validate traces
    findings, stats = validate_traces(args.trace_file, main_roots, cov=cov)
    cov.extra("malformed_trace_lines", stats['malformed_lines'])
    cov.extra("malformed_samples", list(stats['malformed_samples']))
    if stats['malformed_lines']:
        cov.note(f"{stats['malformed_lines']} line(s) carry the DTA_TRACE: marker "
                 f"but do not match TRACE_RE — emitter/parser format drift")

    # JSON output
    if args.json:
        import json
        output = {
            'findings': findings,
            'stats': {
                'marker_lines': stats['marker_lines'],
                'malformed_lines': stats['malformed_lines'],
                'total_traces': stats['total_traces'],
                'unique_traces': stats['unique_traces'],
                'duplicate_traces': stats['duplicate_traces'],
                'resolved': stats['resolved'],
                'unknown_method': stats['unknown_method'],
                'element_info_missing': stats['element_info_missing'],
                'unique_paths': len(stats['paths_seen']),
                'unresolved_count': len(stats['unresolved_paths']),
                # sorted by (-count, name): most_common() breaks ties by
                # insertion order, so the JSON differed between runs.
                'methods': dict(sorted(stats['methods'].items(),
                                       key=lambda kv: (-kv[1], kv[0]))),
            },
            '_coverage': cov.as_dict(),
        }
        print(json.dumps(output, indent=2))
        sys.exit(cov.emit())

    # Statistics
    if args.stats:
        print(f"\n=== Trace Statistics ===\n")
        print(f"  Lines with DTA_TRACE:   {stats['marker_lines']}")
        print(f"  MALFORMED (unparsed):   {stats['malformed_lines']}")
        print(f"  Total trace entries:    {stats['total_traces']}")
        print(f"  Unique trace entries:   {stats['unique_traces']}")
        print(f"  Unique context paths:   {len(stats['paths_seen'])}")
        print(f"  Resolved paths:         {stats['resolved']}")
        print(f"  Unresolved paths:       {len(stats['unresolved_paths'])}")
        print(f"  Method not type-checked:{stats['unknown_method']}")
        print(f"\n  Access methods:")
        for method, count in sorted(stats['methods'].items(),
                                    key=lambda kv: (-kv[1], kv[0])):
            print(f"    {method:8s}  {count:6d}")
        if stats['malformed_lines']:
            print(f"\n  !! {stats['malformed_lines']} line(s) carry the DTA_TRACE: "
                  f"marker but do not match TRACE_RE (format drift). Samples:")
            for s in stats['malformed_samples']:
                print(f"       {s}")

    # Show unresolved paths
    if args.show_unresolved and stats['unresolved_paths']:
        print(f"\n=== Unresolved Context Paths ({len(stats['unresolved_paths'])}) ===\n")
        # most_common() ties break by insertion order → nondeterministic.
        for path, count in sorted(stats['unresolved_paths'].items(),
                                  key=lambda kv: (-kv[1], kv[0])):
            print(f"  {path} ({count} accesses)")

    # Report findings
    if not findings:
        if stats['marker_lines'] == 0:
            print(f"\nINCONCLUSIVE: {args.trace_file} contains 0 lines with the "
                  f"DTA_TRACE: marker — this run checked nothing.")
        else:
            print(f"\nNo validation issues found.")
        print(f"Validated {stats['unique_traces']} unique trace entries "
              f"({stats['resolved']} resolved) across {stats['total_traces']} "
              f"total runtime accesses, from {stats['marker_lines']} DTA_TRACE: "
              f"lines ({stats['malformed_lines']} malformed).")
        rc = cov.emit()
        if stats['marker_lines'] == 0:
            rc = rc or EXIT_EMPTY_CORPUS
        sys.exit(rc)

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
          f"total runtime accesses, from {stats['marker_lines']} DTA_TRACE: "
          f"lines ({stats['malformed_lines']} malformed).")
    sys.exit(cov.emit())


if __name__ == '__main__':
    main()
