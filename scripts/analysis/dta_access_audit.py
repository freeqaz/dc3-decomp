#!/usr/bin/env python3
"""DTA access auditor — validates source code DTA access patterns against actual data.

Checks:
1. Key existence: FindArray("key") — does "key" exist at the expected depth?
2. Positional bounds: FindArray("key")->Int(N) — does the array have N+1 elements?
3. Element types: FindArray("key")->Int(N) — is element N actually an int?
4. SystemConfig sections: SystemConfig("name") — does the section exist?

Uses the DTA hierarchy scanner's parser with #include resolution.

Usage:
  python3 scripts/analysis/dta_access_audit.py
  python3 scripts/analysis/dta_access_audit.py --check-bounds    # positional bounds + types
  python3 scripts/analysis/dta_access_audit.py --check-sections   # SystemConfig validation
  python3 scripts/analysis/dta_access_audit.py --trace src/lazer/meta_ham/MetagameRank.cpp
"""

import re
import sys
import os
from collections import defaultdict
from pathlib import Path

# Import the DTA parser from the hierarchy scanner
from dta_hierarchy_scan import (
    parse_dta_file, DTANode, DTAHierarchy
)


# --------------------------------------------------------------------------
# Enhanced DTA node for type tracking
# --------------------------------------------------------------------------

    # Well-known DTA macros that expand to integers
_INT_MACROS = {'TRUE', 'FALSE', 'NULL',
               'kPlatformNone', 'kPlatformXbox', 'kPlatformPS3', 'kPlatformPC'}


def classify_atom(value):
    """Classify a DTA atom string as int, float, or symbol."""
    if not isinstance(value, str):
        return 'array'
    # Strip quotes
    v = value.strip('"').strip("'")
    # Well-known int macros
    if v in _INT_MACROS:
        return 'int'
    # Try int
    try:
        int(v)
        return 'int'
    except ValueError:
        pass
    # Try float
    try:
        float(v)
        return 'float'
    except ValueError:
        pass
    # Must be a symbol/string
    if value.startswith('"'):
        return 'string'
    # Check for kFoo-style enum macros (likely int)
    if v.startswith('k') and v[1:2].isupper():
        return 'int'
    return 'symbol'


def get_element_info(node, index):
    """Get type and value info for element at index in a DTANode.

    Returns (type, value) or None if index out of bounds.
    DTA arrays: element 0 is the tag, element 1+ are values.
    """
    if not isinstance(node, DTANode):
        return None

    # Reconstruct full element list: [tag, ...children]
    elements = []
    if node.tag:
        elements.append(node.tag)
    for child in node.children:
        elements.append(child)

    if index >= len(elements):
        return None

    elem = elements[index]
    if isinstance(elem, DTANode):
        return ('array', f'({elem.tag} ...)')
    return (classify_atom(elem), elem)


# --------------------------------------------------------------------------
# Access chain tracer
# --------------------------------------------------------------------------

# Match: SystemConfig("section")
SYSCONFIG_RE = re.compile(r'SystemConfig\(\s*"([^"]+)"')

# Match: var = SystemConfig("section") or var = expr->FindArray("key")
ASSIGN_RE = re.compile(
    r'(\w+)\s*=\s*(?:SystemConfig\(\s*"([^"]+)"'
    r'|(\w+)->FindArray\(\s*"([^"]+)"\s*(?:,\s*(?:true|false)\s*)?\))'
)

# Match: expr->FindArray("key")
FINDARRAY_RE = re.compile(r'(\w+)->FindArray\(\s*"([^"]+)"\s*(?:,\s*(?:true|false)\s*)?\)')

# Match: expr->(Int|Float|Sym|Str)(N)
POSITIONAL_RE = re.compile(r'(\w+)->(Int|Float|Sym|Str)\(\s*(\d+)\s*\)')

# Match: chained: FindArray("key")->(Int|Float|Sym|Str)(N)
CHAINED_RE = re.compile(
    r'(?:(\w+)->)?FindArray\(\s*"([^"]+)"\s*(?:,\s*(?:true|false)\s*)?\)'
    r'\s*->\s*(Int|Float|Sym|Str)\(\s*(\d+)\s*\)'
)


def _build_key_node_map(main_roots):
    """Build a map of key_name -> [DTANode, ...] from all DTA roots.

    For each unique key name, collects all DTANode instances with that tag
    across all config trees. Used for key-based validation.
    """
    key_nodes = defaultdict(list)

    def collect(node):
        if isinstance(node, DTANode):
            if node.tag:
                key_nodes[node.tag].append(node)
            for child in node.children:
                collect(child)

    for root in main_roots.values():
        collect(root)

    return key_nodes


def _check_key_based_accesses(lines, filepath, all_key_nodes):
    """Check FindArray("key")->Accessor(N) against all DTA nodes named "key".

    For each chained access, verifies:
    1. Bounds: the array has enough elements
    2. Types: element N is the expected type

    Only flags if ALL instances of the key fail the check (to avoid
    false positives from same-named keys in different contexts).
    """
    findings = []

    for line_no, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith('//'):
            continue

        for m in CHAINED_RE.finditer(line):
            key = m.group(2)
            accessor = m.group(3)
            index = int(m.group(4))

            nodes = all_key_nodes.get(key, [])
            if not nodes:
                continue  # Key not found in any DTA — can't verify

            # Check bounds: does ANY instance have enough elements?
            any_has_bounds = False
            all_type_mismatch = True
            expected_type = {
                'Int': 'int', 'Float': 'float',
                'Sym': 'symbol', 'Str': 'string'
            }.get(accessor)

            for node in nodes:
                info = get_element_info(node, index)
                if info is not None:
                    any_has_bounds = True
                    actual_type, _ = info
                    # Check type compatibility
                    numeric = {'int', 'float'}
                    textual = {'symbol', 'string'}
                    if not ((actual_type in numeric and expected_type in textual) or
                            (actual_type in textual and expected_type in numeric)):
                        all_type_mismatch = False

            if not any_has_bounds:
                # NO instance of this key has enough elements
                sizes = [1 + len(n.children) for n in nodes]
                findings.append({
                    'file': str(filepath),
                    'line': line_no,
                    'type': 'key_index_oob',
                    'detail': f'FindArray("{key}")->{accessor}({index}) — '
                              f'all {len(nodes)} DTA instances of "{key}" '
                              f'have sizes {sorted(set(sizes))} (need {index + 1}+)',
                    'receiver': '',
                    'severity': 'HIGH',
                })
            elif all_type_mismatch and len(nodes) > 0:
                # ALL instances have wrong type at this index
                types_found = set()
                for node in nodes:
                    info = get_element_info(node, index)
                    if info:
                        types_found.add(info[0])
                findings.append({
                    'file': str(filepath),
                    'line': line_no,
                    'type': 'key_type_mismatch',
                    'detail': f'FindArray("{key}")->{accessor}({index}) — '
                              f'all instances have type(s) {types_found}, '
                              f'accessed as {expected_type}',
                    'receiver': '',
                    'severity': 'MEDIUM',
                })

    return findings


def trace_dta_accesses(filepath, hierarchy, main_roots, all_key_nodes=None):
    """Trace all DTA access chains in a source file.

    Returns list of findings with validation results.
    """
    findings = []

    try:
        with open(filepath) as f:
            lines = f.readlines()
    except (IOError, UnicodeDecodeError):
        return findings

    # Quick check
    if not any('Find' in l or 'SystemConfig' in l for l in lines):
        return findings

    # Track variable context
    var_config = {}   # var -> config section name
    var_dta_node = {} # var -> DTANode (resolved)
    var_parent = {}   # var -> parent var name
    var_key = {}      # var -> key used in FindArray

    def resolve_node(config_section, key_chain):
        """Resolve a DTA node from config section + chain of keys.

        Tries ALL main config roots since the runtime config is a merge
        of default.dta and ham_keep.dta. A key may exist in one but not the other.
        """
        for root in main_roots.values():
            section_node = root.find_array(config_section)
            if section_node is None:
                continue
            # Follow the key chain
            node = section_node
            success = True
            for key in key_chain:
                child = node.find_array(key)
                if child is None:
                    success = False
                    break
                node = child
            if success:
                return node
        return None

    def get_var_node(var_name):
        """Try to resolve the DTANode a variable points to."""
        if var_name in var_dta_node:
            return var_dta_node[var_name]

        # Build key chain by walking parents
        chain = []
        cur = var_name
        seen = set()
        while cur in var_key and cur not in seen:
            seen.add(cur)
            chain.append(var_key[cur])
            if cur in var_parent:
                cur = var_parent[cur]
            else:
                break

        chain.reverse()

        # Find config root
        root_var = cur
        if root_var in var_config:
            config_section = var_config[root_var]
            node = resolve_node(config_section, chain)
            if node:
                var_dta_node[var_name] = node
                return node

        return None

    for line_no, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith('//'):
            continue

        # Track assignments
        for m in ASSIGN_RE.finditer(line):
            var_name = m.group(1)
            if m.group(2):  # SystemConfig("section")
                var_config[var_name] = m.group(2)
            elif m.group(3) and m.group(4):  # parent->FindArray("key")
                parent_name = m.group(3)
                key = m.group(4)
                if var_name != parent_name:  # skip self-reassignment
                    var_parent[var_name] = parent_name
                    var_key[var_name] = key

        # Check chained accesses: FindArray("key")->Int(N)
        for m in CHAINED_RE.finditer(line):
            receiver = m.group(1)  # may be None for inline chains
            key = m.group(2)
            accessor = m.group(3)  # Int, Float, Sym, Str
            index = int(m.group(4))

            # Try to resolve the receiver's DTA context
            if receiver:
                parent_node = get_var_node(receiver)
                if parent_node is None:
                    continue

                # Find the child array
                target_node = parent_node.find_array(key)
            else:
                continue  # can't trace without receiver

            if target_node is None:
                findings.append({
                    'file': str(filepath),
                    'line': line_no,
                    'type': 'missing_key',
                    'detail': f'FindArray("{key}") — key not found in DTA',
                    'receiver': receiver,
                    'severity': 'HIGH',
                })
                continue

            # Check bounds
            elem_info = get_element_info(target_node, index)
            if elem_info is None:
                total_elements = 1 + len(target_node.children)  # tag + children
                findings.append({
                    'file': str(filepath),
                    'line': line_no,
                    'type': 'index_oob',
                    'detail': f'FindArray("{key}")->{accessor}({index}) — '
                              f'array has {total_elements} elements (0..{total_elements-1})',
                    'receiver': receiver,
                    'severity': 'HIGH',
                })
                continue

            # Check type
            actual_type, actual_value = elem_info
            expected_type = {
                'Int': 'int', 'Float': 'float',
                'Sym': 'symbol', 'Str': 'string'
            }.get(accessor, 'unknown')

            # Type compatibility:
            # int ↔ float is usually ok (implicit conversion)
            # symbol ↔ string is usually ok
            # int/float ↔ symbol/string is a real mismatch
            numeric = {'int', 'float'}
            textual = {'symbol', 'string'}

            if actual_type in numeric and expected_type in textual:
                findings.append({
                    'file': str(filepath),
                    'line': line_no,
                    'type': 'type_mismatch',
                    'detail': f'FindArray("{key}")->{accessor}({index}) — '
                              f'element is {actual_type} ({actual_value}), '
                              f'accessed as {expected_type}',
                    'receiver': receiver,
                    'severity': 'MEDIUM',
                })
            elif actual_type in textual and expected_type in numeric:
                findings.append({
                    'file': str(filepath),
                    'line': line_no,
                    'type': 'type_mismatch',
                    'detail': f'FindArray("{key}")->{accessor}({index}) — '
                              f'element is {actual_type} ({actual_value}), '
                              f'accessed as {expected_type}',
                    'receiver': receiver,
                    'severity': 'MEDIUM',
                })

        # Check positional accesses on tracked variables
        for m in POSITIONAL_RE.finditer(line):
            receiver = m.group(1)
            accessor = m.group(2)
            index = int(m.group(3))

            # Skip if this is part of a chained expression (handled above)
            if f'FindArray' in line and f'->{accessor}({index})' in line:
                pos = m.start()
                before = line[:pos]
                if 'FindArray' in before and ')' in before[before.rfind('FindArray'):]:
                    continue

            node = get_var_node(receiver)
            if node is None:
                continue

            # Check bounds
            elem_info = get_element_info(node, index)
            if elem_info is None:
                total_elements = 1 + len(node.children)
                findings.append({
                    'file': str(filepath),
                    'line': line_no,
                    'type': 'index_oob',
                    'detail': f'{receiver}->{accessor}({index}) — '
                              f'array has {total_elements} elements (0..{total_elements-1})',
                    'receiver': receiver,
                    'severity': 'HIGH',
                })

    # ----- Key-based validation (no chain resolution needed) -----
    # For any FindArray("key")->Accessor(N), verify "key" arrays in DTA
    findings.extend(
        _check_key_based_accesses(lines, filepath, all_key_nodes)
    )

    return findings


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(description='DTA access auditor')
    parser.add_argument('--src-dir', default='src/',
                        help='Source directory to scan')
    parser.add_argument('--trace', type=str,
                        help='Trace a specific source file')
    parser.add_argument('--check-sections', action='store_true',
                        help='Validate SystemConfig section names')
    parser.add_argument('--check-bounds', action='store_true', default=True,
                        help='Check positional access bounds and types')
    parser.add_argument('--json', action='store_true',
                        help='Output as JSON')
    args = parser.parse_args()

    # Build hierarchy and keep root nodes for direct access
    main_configs = [
        'orig-assets/extracted/config/ham_keep.dta',
        'orig-assets/extracted/(..)/(..)/system/run/config/default.dta',
    ]
    main_roots = {}
    hierarchy = DTAHierarchy()

    for cfg in main_configs:
        p = Path(cfg)
        if p.exists():
            root = parse_dta_file(str(p))
            if root:
                main_roots[cfg] = root
                hierarchy.add_file(p)

    for f in Path('orig-assets/extracted').rglob('*.dta'):
        if str(f) not in hierarchy.roots:
            hierarchy.add_file(f)

    print(f"Loaded {len(main_roots)} main configs, "
          f"{len(hierarchy.key_parents)} unique keys", file=sys.stderr)

    # SystemConfig section validation
    if args.check_sections:
        sections = set()
        src_path = Path(args.src_dir)
        for f in list(src_path.rglob('*.cpp')) + list(src_path.rglob('*.h')):
            try:
                text = f.read_text()
            except:
                continue
            for m in SYSCONFIG_RE.finditer(text):
                sections.add(m.group(1))

        print(f"\n=== SystemConfig Section Validation ({len(sections)} sections) ===\n")
        bad = []
        for section in sorted(sections):
            found = False
            for root in main_roots.values():
                if root.find_array(section):
                    found = True
                    break
            if not found:
                # Also check standalone DTA files
                if hierarchy.is_root_key(section):
                    found = True
            if found:
                print(f"  OK  {section}")
            else:
                print(f"  BAD {section}")
                bad.append(section)
        if bad:
            print(f"\n{len(bad)} sections not found: {bad}")
        else:
            print(f"\nAll {len(sections)} sections valid.")
        return

    # Trace accesses
    if args.trace:
        files = [Path(args.trace)]
    else:
        src_path = Path(args.src_dir)
        files = sorted(list(src_path.rglob('*.cpp')) + list(src_path.rglob('*.h')))

    # Build key->node map for key-based validation
    all_key_nodes = _build_key_node_map(main_roots)
    print(f"Built key-node map: {len(all_key_nodes)} unique keys with DTA nodes",
          file=sys.stderr)

    all_findings = []
    for f in files:
        findings = trace_dta_accesses(str(f), hierarchy, main_roots, all_key_nodes)
        all_findings.extend(findings)

    if args.json:
        import json
        print(json.dumps(all_findings, indent=2))
        return

    if not all_findings:
        print("No DTA access issues found.")
        return

    # Group by type
    by_type = defaultdict(list)
    for f in all_findings:
        by_type[f['type']].append(f)

    for bug_type, items in sorted(by_type.items()):
        severity = items[0]['severity']
        print(f"\n=== {bug_type.upper()} [{severity}] ({len(items)} findings) ===\n")
        for item in items:
            print(f"  {item['file']}:{item['line']}")
            print(f"    {item['detail']}")
            print()

    print(f"Total: {sum(len(v) for v in by_type.values())} findings "
          f"({', '.join(f'{k}:{len(v)}' for k,v in sorted(by_type.items()))})")


if __name__ == '__main__':
    main()
