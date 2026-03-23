#!/usr/bin/env python3
"""DTA-aware FindArray receiver bug detector.

Parses all DTA config files to build a key hierarchy, then cross-references
source code FindArray calls to detect wrong-level lookups.

Example: metagame_rank.dta has:
  (tasks
    (one_time ...)
    (repeatable ...))

If source does `rankCfg->FindArray("one_time")` but "one_time" only exists
as a child of "tasks" (not at root), this is a bug.

Usage:
  python3 scripts/analysis/dta_hierarchy_scan.py
  python3 scripts/analysis/dta_hierarchy_scan.py --dta-dir orig-assets/extracted/config
  python3 scripts/analysis/dta_hierarchy_scan.py --dump-hierarchy  # show parsed tree
"""

import re
import sys
import os
from collections import defaultdict
from pathlib import Path

# --------------------------------------------------------------------------
# DTA Parser (minimal S-expression parser)
# --------------------------------------------------------------------------

class DTANode:
    """A node in the parsed DTA tree."""
    def __init__(self, tag=None, children=None, is_root=False):
        self.tag = tag          # First symbol in this array (the key)
        self.children = children or []  # List of child DTANodes
        self.is_root = is_root
        self.source_file = None

    def find_array(self, tag):
        """Simulate FindArray — find child array with matching tag."""
        for c in self.children:
            if isinstance(c, DTANode) and c.tag == tag:
                return c
        return None

    def child_tags(self):
        """Return set of all child array tags."""
        return {c.tag for c in self.children if isinstance(c, DTANode) and c.tag}

    def all_tags_recursive(self):
        """Return dict mapping tag -> set of parent tags it appears under."""
        result = defaultdict(set)
        self._collect_tags(result, parent_tag=None)
        return result

    def _collect_tags(self, result, parent_tag):
        for c in self.children:
            if isinstance(c, DTANode) and c.tag:
                result[c.tag].add(parent_tag if parent_tag else '__ROOT__')
                c._collect_tags(result, c.tag)

    def print_tree(self, indent=0):
        prefix = '  ' * indent
        if self.is_root:
            print(f"{prefix}[ROOT: {self.source_file}]")
        elif self.tag:
            child_tags = self.child_tags()
            if child_tags:
                print(f"{prefix}{self.tag} -> {{{', '.join(sorted(child_tags))}}}")
            else:
                nchildren = len(self.children)
                print(f"{prefix}{self.tag} ({nchildren} values)")
        for c in self.children:
            if isinstance(c, DTANode) and c.children:
                c.print_tree(indent + 1)


def tokenize_dta(text):
    """Tokenize DTA text into a list of tokens.

    Handles #include directives specially, emitting them as
    ('#include', 'filename.dta') token pairs.
    """
    tokens = []
    i = 0
    while i < len(text):
        c = text[i]

        # Skip whitespace
        if c in ' \t\n\r':
            i += 1
            continue

        # Skip comments (;; to end of line)
        if c == ';':
            while i < len(text) and text[i] != '\n':
                i += 1
            continue

        # Preprocessor directives
        if c == '#':
            # Extract directive
            j = i
            while j < len(text) and text[j] != '\n':
                j += 1
            directive = text[i:j].strip()

            if directive.startswith('#include '):
                filename = directive[len('#include '):].strip()
                # Handle case like "#include file.dta)" where ) closes parent
                trailing_parens = 0
                while filename.endswith(')'):
                    filename = filename[:-1]
                    trailing_parens += 1
                tokens.append(('#include', filename.strip()))
                for _ in range(trailing_parens):
                    tokens.append(')')
                i = j
            elif directive.startswith('#define '):
                # DTA #define puts value on NEXT line as (value)
                # Skip the directive line AND the next parenthesized value
                i = j
                # Skip whitespace to find the value
                while i < len(text) and text[i] in ' \t\n\r':
                    i += 1
                # If next non-ws is '(', skip the entire parenthesized expr
                if i < len(text) and text[i] == '(':
                    depth = 0
                    while i < len(text):
                        if text[i] == '(':
                            depth += 1
                        elif text[i] == ')':
                            depth -= 1
                            if depth == 0:
                                i += 1
                                break
                        i += 1
            else:
                # Skip other directives (#ifdef, #ifndef, #else, #endif, etc.)
                # But preserve any trailing parentheses (e.g., "#endif)")
                rest = directive
                # Strip the directive keyword
                for kw in ('#endif', '#else', '#ifdef', '#ifndef'):
                    if rest.startswith(kw):
                        rest = rest[len(kw):].strip()
                        break
                else:
                    rest = ''
                # Emit trailing parens
                for ch in rest:
                    if ch == ')':
                        tokens.append(')')
                    elif ch == '(':
                        tokens.append('(')
                i = j
            continue

        # Parens
        if c == '(':
            tokens.append('(')
            i += 1
            continue
        if c == ')':
            tokens.append(')')
            i += 1
            continue

        # Quoted string
        if c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                if text[j] == '\\':
                    j += 1  # skip escaped char
                j += 1
            tokens.append(text[i:j+1])
            i = j + 1
            continue

        # Quoted symbol
        if c == "'":
            j = i + 1
            while j < len(text) and text[j] not in ' \t\n\r()':
                j += 1
            tokens.append(text[i:j])
            i = j
            continue

        # Word/number/symbol
        j = i
        while j < len(text) and text[j] not in ' \t\n\r();"\'':
            j += 1
        tokens.append(text[i:j])
        i = j

    return tokens


def parse_dta(text, source_file=None, base_dir=None, include_depth=0):
    """Parse DTA text into a DTANode tree, resolving #include directives."""
    if include_depth > 10:
        return DTANode(is_root=True)  # prevent infinite recursion

    tokens = tokenize_dta(text)
    root = DTANode(is_root=True)
    root.source_file = source_file

    if base_dir is None and source_file:
        base_dir = str(Path(source_file).parent)

    pos = [0]

    def resolve_include(filename):
        """Resolve an #include filename relative to base_dir."""
        if base_dir is None:
            return None
        # Try relative to base_dir
        candidate = Path(base_dir) / filename
        if candidate.exists():
            return str(candidate)
        # Try stripping leading ../
        stripped = filename.lstrip('./')
        candidate = Path(base_dir) / stripped
        if candidate.exists():
            return str(candidate)
        return None

    def process_include(filename, target_node):
        """Parse an #include file and add its contents to target_node."""
        filepath = resolve_include(filename)
        if filepath is None:
            return
        try:
            with open(filepath, encoding='utf-8', errors='replace') as f:
                inc_text = f.read()
            inc_root = parse_dta(inc_text, filepath,
                                 str(Path(filepath).parent),
                                 include_depth + 1)
            # Add included file's root children to target node
            for child in inc_root.children:
                target_node.children.append(child)
        except (IOError, UnicodeDecodeError):
            pass

    def parse_list():
        """Parse a parenthesized list into a DTANode."""
        node = DTANode()
        while pos[0] < len(tokens):
            tok = tokens[pos[0]]
            if tok == ')':
                pos[0] += 1
                break
            elif isinstance(tok, tuple) and tok[0] == '#include':
                pos[0] += 1
                process_include(tok[1], node)
            elif tok == '(':
                pos[0] += 1
                child = parse_list()
                if node.tag is None and not node.children:
                    node.children.append(child)
                else:
                    node.children.append(child)
            else:
                pos[0] += 1
                if node.tag is None:
                    node.tag = tok.strip('"').strip("'")
                else:
                    node.children.append(tok)
        return node

    while pos[0] < len(tokens):
        tok = tokens[pos[0]]
        if tok == '(':
            pos[0] += 1
            child = parse_list()
            root.children.append(child)
        elif isinstance(tok, tuple) and tok[0] == '#include':
            pos[0] += 1
            process_include(tok[1], root)
        else:
            pos[0] += 1

    return root


def parse_dta_file(filepath):
    """Parse a DTA file with #include resolution, returning None on error."""
    try:
        with open(filepath, encoding='utf-8', errors='replace') as f:
            text = f.read()
        return parse_dta(text, str(filepath), str(Path(filepath).parent))
    except Exception as e:
        print(f"Warning: failed to parse {filepath}: {e}", file=sys.stderr)
        return None


# --------------------------------------------------------------------------
# Build hierarchy database from all DTA files
# --------------------------------------------------------------------------

class DTAHierarchy:
    """Database of which keys exist at which levels in DTA configs."""

    def __init__(self):
        # key -> set of parent keys it appears under
        self.key_parents = defaultdict(set)
        # key -> set of DTA files it appears in
        self.key_files = defaultdict(set)
        # (parent_key, child_key) -> set of files
        self.edges = defaultdict(set)
        # file -> root DTANode
        self.roots = {}

    def add_file(self, filepath):
        root = parse_dta_file(filepath)
        if root is None:
            return
        self.roots[str(filepath)] = root
        tags = root.all_tags_recursive()
        for tag, parents in tags.items():
            self.key_parents[tag].update(parents)
            self.key_files[tag].add(str(filepath))
            for p in parents:
                self.edges[(p, tag)].add(str(filepath))

    def is_root_key(self, key):
        """Check if key ever appears at root level in any DTA file."""
        return '__ROOT__' in self.key_parents.get(key, set())

    def get_parents(self, key):
        """Get all parent keys where this key appears as a child."""
        return self.key_parents.get(key, set())

    def get_siblings(self, key):
        """Get keys that share the same parent(s) as this key."""
        siblings = set()
        for parent in self.get_parents(key):
            for other_key, other_parents in self.key_parents.items():
                if parent in other_parents and other_key != key:
                    siblings.add(other_key)
        return siblings


# --------------------------------------------------------------------------
# Source code scanner
# --------------------------------------------------------------------------

# All DataArray lookup methods that navigate by key name
_FIND_METHODS = r'(?:FindArray|FindStr|FindFloat|FindInt|FindData|FindSym|FindVar)'

# Match: expr->FindXxx("key")
FINDARRAY_RE = re.compile(rf'(\w+)->{_FIND_METHODS}\(\s*"([^"]+)"\s*')

# Match: var = expr->FindArray("key")  (only FindArray returns DataArray*)
ASSIGN_FINDARRAY_RE = re.compile(
    r'(\w+)\s*=\s*(\w+)->FindArray\(\s*"([^"]+)"\s*\)'
)

# Match: SystemConfig("key") or DataReadFile("key")
CONFIG_LOAD_RE = re.compile(r'SystemConfig\(\s*"([^"]+)"\s*\)')


def _resolve_effective_parent(receiver, var_config, var_parent, var_key):
    """Resolve what DTA node a variable points to.

    Returns the DTA key name that the receiver is "at", or:
      '__ROOT__' if at the root of the DTA tree
      None if we can't determine the level (e.g., function parameters)
    """
    # Direct SystemConfig: cfg = SystemConfig("X") → receiver is at "X"
    if receiver in var_config:
        return var_config[receiver]

    # FindArray chain: var = parent->FindArray("Y") → receiver is at "Y"
    if receiver in var_key:
        return var_key[receiver]

    # Walk the parent chain
    cur = receiver
    seen = set()
    while cur in var_parent and cur not in seen:
        seen.add(cur)
        parent = var_parent[cur]
        if parent in var_config:
            # Parent was from SystemConfig("X"), and cur = parent->FindArray("K")
            # So cur is at key K, which should be child of X
            return var_key.get(cur)
        cur = parent

    # Can't determine — probably a function parameter
    return None


def scan_source_against_hierarchy(src_dir, hierarchy):
    """Scan source files and cross-reference FindArray calls against DTA hierarchy."""
    findings = []

    # Collect all source files
    src_path = Path(src_dir)
    files = list(src_path.rglob('*.cpp')) + list(src_path.rglob('*.h'))

    for filepath in sorted(files):
        try:
            with open(filepath) as f:
                lines = f.readlines()
        except (IOError, UnicodeDecodeError):
            continue

        # Quick check — need any Find* method
        if not any('Find' in l and '->' in l for l in lines):
            continue

        # Track variable -> config name mapping (rough)
        # e.g., DataArray *cfg = SystemConfig("metagame_rank")
        var_config = {}  # var_name -> config_root_key
        var_parent = {}  # var_name -> parent_var (from FindArray chain)
        var_key = {}     # var_name -> key used in FindArray
        assign_checks = []  # deferred checks: (line_no, parent_name, key)

        for line_no, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('//'):
                continue

            # Track SystemConfig loads
            for m in CONFIG_LOAD_RE.finditer(line):
                config_key = m.group(1)
                # Look for assignment: var = SystemConfig("...")
                assign_m = re.search(r'(\w+)\s*=\s*SystemConfig', line)
                if assign_m:
                    var_config[assign_m.group(1)] = config_key

            # Track FindArray assignments AND check them against hierarchy
            for m in ASSIGN_FINDARRAY_RE.finditer(line):
                var_name, parent_name, key = m.groups()
                # Skip self-reassignment (def = def->FindArray("editor"))
                # which creates circular references in the tracker
                if var_name == parent_name:
                    continue
                var_parent[var_name] = parent_name
                var_key[var_name] = key
                # Also check this assignment's key against hierarchy
                assign_checks.append((line_no, parent_name, key))

            # Check all Find* calls
            for m in FINDARRAY_RE.finditer(line):
                receiver, key = m.groups()

                # Skip if this is an assignment (we handle those above)
                if re.search(rf'\w+\s*=\s*{re.escape(receiver)}->FindArray', line):
                    continue

                # Skip optional lookups: FindData("key", var, false)
                # These intentionally handle missing keys
                if re.search(rf'{re.escape(receiver)}->FindData\(\s*"{re.escape(key)}".*,\s*false\s*\)', line):
                    continue

                # Resolve the receiver's depth in the hierarchy
                # Walk the chain: receiver -> var_parent -> var_parent -> ...
                chain = [receiver]
                cur = receiver
                while cur in var_parent:
                    cur = var_parent[cur]
                    chain.append(cur)
                    if len(chain) > 10:
                        break

                # The root of the chain might be a config variable
                root_var = chain[-1]
                config_name = var_config.get(root_var)

                # Resolve the effective DTA parent key for this receiver
                # SystemConfig("X") puts you at the "X" node
                # var = parent->FindArray("Y") puts you at the "Y" node
                effective_parent = _resolve_effective_parent(
                    receiver, var_config, var_parent, var_key
                )

                # Check if the key exists at the expected depth
                parents = hierarchy.get_parents(key)
                if not parents:
                    continue  # Key not found in any DTA — can't verify

                if effective_parent is None:
                    # Can't determine parent (e.g., function parameter)
                    # Only flag if key NEVER appears at root level
                    # AND there's no config context
                    continue

                if effective_parent == '__ROOT__':
                    # Looking up from true root
                    if not hierarchy.is_root_key(key):
                        actual_parents = parents - {'__ROOT__'}
                        findings.append({
                            'file': str(filepath),
                            'line': line_no,
                            'receiver': receiver,
                            'key': key,
                            'chain': chain,
                            'config': config_name,
                            'expected_depth': 'root',
                            'actual_parents': actual_parents,
                            'severity': 'HIGH',
                        })
                else:
                    # Looking up from a known DTA node
                    if effective_parent not in parents:
                        # Key is NOT a child of where we're looking
                        actual_parents = parents - {'__ROOT__'}
                        findings.append({
                            'file': str(filepath),
                            'line': line_no,
                            'receiver': receiver,
                            'key': key,
                            'chain': chain,
                            'config': config_name,
                            'expected_depth': f'child of {effective_parent}',
                            'actual_parents': actual_parents,
                            'severity': 'HIGH',
                        })

        # Process deferred assignment checks
        for line_no, parent_name, key in assign_checks:
            effective_parent = _resolve_effective_parent(
                parent_name, var_config, var_parent, var_key
            )
            parents = hierarchy.get_parents(key)
            if not parents or effective_parent is None:
                continue

            chain = [parent_name]
            root_var = parent_name
            cur = parent_name
            while cur in var_parent:
                cur = var_parent[cur]
                chain.append(cur)
                root_var = cur
                if len(chain) > 10:
                    break
            config_name = var_config.get(root_var)

            if effective_parent == '__ROOT__':
                if not hierarchy.is_root_key(key):
                    actual_parents = parents - {'__ROOT__'}
                    findings.append({
                        'file': str(filepath),
                        'line': line_no,
                        'receiver': parent_name,
                        'key': key,
                        'chain': chain,
                        'config': config_name,
                        'expected_depth': 'root',
                        'actual_parents': actual_parents,
                        'severity': 'HIGH',
                    })
            else:
                if effective_parent not in parents:
                    actual_parents = parents - {'__ROOT__'}
                    findings.append({
                        'file': str(filepath),
                        'line': line_no,
                        'receiver': parent_name,
                        'key': key,
                        'chain': chain,
                        'config': config_name,
                        'expected_depth': f'child of {effective_parent}',
                        'actual_parents': actual_parents,
                        'severity': 'HIGH',
                    })

    return findings


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(description='DTA-aware FindArray bug detector')
    parser.add_argument('--dta-dir', nargs='*',
                        default=['orig-assets/extracted/config',
                                 'orig-assets/extracted/(..)/(..)/system/run/config'],
                        help='Directories containing DTA files')
    parser.add_argument('--main-configs', nargs='*',
                        default=['orig-assets/extracted/config/ham_keep.dta',
                                 'orig-assets/extracted/(..)/(..)/system/run/config/default.dta'],
                        help='Main config files to parse with #include resolution')
    parser.add_argument('--src-dir', default='src/',
                        help='Source directory to scan')
    parser.add_argument('--dump-hierarchy', action='store_true',
                        help='Print parsed DTA hierarchy and exit')
    parser.add_argument('--query', type=str,
                        help='Query a specific key to see where it exists in hierarchy')
    parser.add_argument('--json', action='store_true',
                        help='Output findings as JSON')
    args = parser.parse_args()

    # Build hierarchy from main config files first (with #include resolution)
    # These give us the TRUE hierarchy with proper nesting
    hierarchy = DTAHierarchy()
    dta_count = 0

    for main_cfg in args.main_configs:
        main_path = Path(main_cfg)
        if main_path.exists():
            hierarchy.add_file(main_path)
            dta_count += 1

    # Then add individual DTA files (for keys not in main configs)
    for dta_dir in args.dta_dir:
        dta_path = Path(dta_dir)
        if not dta_path.exists():
            print(f"Warning: {dta_dir} does not exist", file=sys.stderr)
            continue
        for dta_file in sorted(dta_path.rglob('*.dta')):
            if str(dta_file) not in hierarchy.roots:
                hierarchy.add_file(dta_file)
                dta_count += 1

    # Also scan all DTA files in orig-assets
    for dta_file in sorted(Path('orig-assets/extracted').rglob('*.dta')):
        if str(dta_file) not in hierarchy.roots:
            hierarchy.add_file(dta_file)
            dta_count += 1

    print(f"Parsed {dta_count} DTA files, {len(hierarchy.key_parents)} unique keys",
          file=sys.stderr)

    if args.query:
        key = args.query
        parents = hierarchy.get_parents(key)
        if not parents:
            print(f"Key '{key}' not found in any DTA file.")
        else:
            print(f"Key '{key}' appears as child of: {parents}")
            print(f"  Files: {hierarchy.key_files.get(key, set())}")
            is_root = hierarchy.is_root_key(key)
            print(f"  Is root-level: {is_root}")
        return

    if args.dump_hierarchy:
        for filepath, root in sorted(hierarchy.roots.items()):
            root.print_tree()
            print()
        return

    # Scan source code
    findings = scan_source_against_hierarchy(args.src_dir, hierarchy)

    if args.json:
        import json
        # Convert sets to lists for JSON
        for f in findings:
            f['actual_parents'] = sorted(f['actual_parents'])
        print(json.dumps(findings, indent=2))
        return

    if not findings:
        print("No DTA hierarchy mismatches found.")
        return

    # Report
    high = [f for f in findings if f['severity'] == 'HIGH']
    medium = [f for f in findings if f['severity'] == 'MEDIUM']

    if high:
        print(f"\n=== HIGH: Key looked up at wrong depth ({len(high)} findings) ===\n")
        for f in high:
            print(f"  {f['file']}:{f['line']}")
            print(f"    {f['receiver']}->FindArray(\"{f['key']}\")")
            print(f"    Key \"{f['key']}\" is NOT a root-level key.")
            print(f"    It exists as child of: {f['actual_parents']}")
            if f['config']:
                print(f"    Config: {f['config']}")
            print()

    if medium:
        print(f"\n=== MEDIUM: Key at unexpected parent ({len(medium)} findings) ===\n")
        for f in medium:
            print(f"  {f['file']}:{f['line']}")
            print(f"    {f['receiver']}->FindArray(\"{f['key']}\")")
            print(f"    Expected under: {f['expected_depth']}")
            print(f"    Actually under: {f['actual_parents']}")
            print()

    print(f"Total: {len(high)} HIGH, {len(medium)} MEDIUM")


if __name__ == '__main__':
    main()
