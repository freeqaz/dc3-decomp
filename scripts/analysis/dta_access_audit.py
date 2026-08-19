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
from collections import Counter, defaultdict
from pathlib import Path

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
if str(Path(__file__).parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).parent))
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

# Import the DTA parser from the hierarchy scanner
from dta_hierarchy_scan import (
    parse_dta_file, DTANode, DTAHierarchy,
    empty_corpus_banner, EXIT_EMPTY_CORPUS, PARSE_STATS, UNRESOLVED_INCLUDES,
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



class SiteLedger:
    """One row per DISTINCT access site, however many passes look at it.

    `trace_dta_accesses` walks each line with CHAINED_RE and disposes of every
    match; `_check_key_based_accesses` then re-walks THE SAME LINES with THE
    SAME REGEX and disposes again.  Both also bumped `site_totals['sites']`, so
    a chained site entered the universe twice and left it twice.  Measured
    2026-08-19: 1,679 site events over 1,658 distinct sites -- 21 counted
    twice, of which 8 were EXAMINED twice.  The published "2.2%" was 37/1,679;
    the honest distinct-site figure is 29/1,658 = 1.75%.

    Note this was INVISIBLE to the exit-4 tripwire: each event bumped the
    universe and landed in exactly one of examined/dropped, so a double-counted
    site balanced perfectly.  The coverage contract catches an UNCOUNTED row;
    it cannot catch a TWICE-COUNTED one.

    Resolution rule: a site is EXAMINED if ANY pass managed to check it, and
    dropped only if none did.  Two passes checking different things (receiver
    resolution vs bounds/type) are two chances to examine one site, not two
    sites.
    """

    def __init__(self):
        self._examined = {}      # key -> True
        self._dropped = {}       # key -> (reason, note)

    def bump(self, key):
        """Register a site. Returns True the first time this site is seen."""
        first = key not in self._examined and key not in self._dropped
        if first:
            self._dropped[key] = None      # placeholder: seen, not yet disposed
        return first

    def examine(self, key):
        self._examined[key] = True
        self._dropped.pop(key, None)

    def drop(self, key, reason, note=""):
        if key in self._examined:
            return                          # a check DID run on this site
        if self._dropped.get(key) is None:
            self._dropped[key] = (reason, note)

    def flush(self, cov_sites):
        """Emit exactly one disposition per distinct site."""
        if cov_sites is None:
            return
        for _ in self._examined:
            cov_sites.examine()
        for key, val in sorted(self._dropped.items(), key=lambda kv: str(kv[0])):
            if val is None:
                cov_sites.drop("site-seen-but-never-disposed",
                               note="registered by a pass that then neither "
                                    "examined nor dropped it")
            else:
                cov_sites.drop(val[0], note=val[1])

    @property
    def distinct(self):
        return len(self._examined) + len(self._dropped)


def _check_key_based_accesses(lines, filepath, all_key_nodes,
                              cov_sites=None, unverifiable=None, site_totals=None,
                              ledger=None):
    """Check FindArray("key")->Accessor(N) against all DTA nodes named "key".

    For each chained access, verifies:
    1. Bounds: the array has enough elements
    2. Types: element N is the expected type

    Only flags if ALL instances of the key fail the check (to avoid
    false positives from same-named keys in different contexts).

    The optional coverage arguments are additive: a two/three-argument caller
    behaves exactly as before.
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
            # Same site the CHAINED_RE pass in trace_dta_accesses already
            # registered: the ledger counts it once and lets a check here
            # UPGRADE an earlier drop to an examine.
            skey = (str(filepath), line_no, m.span())
            if ledger is not None:
                if ledger.bump(skey) and site_totals is not None:
                    site_totals['sites'] += 1
            elif site_totals is not None:
                site_totals['sites'] += 1

            nodes = all_key_nodes.get(key, [])
            if not nodes:
                # NOT "nothing to verify" — this is the key-does-not-exist-
                # ANYWHERE population, i.e. the typo'd-FindArray bug class and
                # the single most valuable thing this scanner could report.
                # It used to vanish through a bare `continue`.
                if unverifiable is not None:
                    unverifiable[key] += 1
                if ledger is not None:
                    ledger.drop(skey, "key-absent-from-every-dta",
                                note="key exists in no parsed DTA — "
                                     "typo'd-FindArray candidates")
                elif cov_sites is not None:
                    cov_sites.drop("key-absent-from-every-dta",
                                   note="key exists in no parsed DTA — "
                                        "typo'd-FindArray candidates")
                continue
            if ledger is not None:
                ledger.examine(skey)
            elif cov_sites is not None:
                cov_sites.examine()

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
                # sorted(): a raw set renders in hash order, so the same run
                # produced different finding text from one invocation to the next.
                types_found = sorted(types_found)
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


def trace_dta_accesses(filepath, hierarchy, main_roots, all_key_nodes=None,
                       cov_files=None, cov_sites=None, unverifiable=None,
                       site_totals=None):
    """Trace all DTA access chains in a source file.

    Returns list of findings with validation results.

    The coverage arguments are optional and additive.  Without them this
    behaves exactly as before — but then an unreadable file and a clean file
    produce the identical empty list, which is the shape this whole contract
    exists to outlaw.
    """
    findings = []
    # One ledger per FILE, shared with the second pass below, so a chained site
    # both passes see is one row rather than two.  See SiteLedger.
    ledger = SiteLedger()
    _cur = {"key": None}

    def site_drop(reason, note=""):
        if _cur["key"] is not None:
            ledger.drop(_cur["key"], reason, note)
        elif cov_sites is not None:
            cov_sites.drop(reason, note=note)

    def site_keep():
        if _cur["key"] is not None:
            ledger.examine(_cur["key"])
        elif cov_sites is not None:
            cov_sites.examine()

    def bump(key=None):
        if key is not None:
            _cur["key"] = key
            if ledger.bump(key) and site_totals is not None:
                site_totals['sites'] += 1
            return
        if site_totals is not None:
            site_totals['sites'] += 1

    try:
        with open(filepath) as f:
            lines = f.readlines()
    except (IOError, UnicodeDecodeError):
        # An unreadable source file is NOT a clean source file.
        if cov_files is not None:
            cov_files.drop("source-unreadable", note=str(filepath))
        return findings

    # Quick check
    if not any('Find' in l or 'SystemConfig' in l for l in lines):
        if cov_files is not None:
            cov_files.drop("no-dta-access-syntax",
                           note="no `Find` / `SystemConfig` token anywhere in the file")
        return findings

    if cov_files is not None:
        cov_files.examine()

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
            bump((str(filepath), line_no, m.span()))

            # Try to resolve the receiver's DTA context
            if receiver:
                parent_node = get_var_node(receiver)
                if parent_node is None:
                    site_drop("receiver-unresolvable",
                              note="receiver's DTA context could not be traced "
                                   "(function parameter / cross-function flow)")
                    continue

                # Find the child array
                target_node = parent_node.find_array(key)
            else:
                site_drop("inline-chain-no-receiver",
                          note="FindArray(..)->Acc(N) with no named receiver to anchor")
                continue

            site_keep()

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
            # A bare `x->Int(N)` is its own site (no key), but it must go
            # through the same ledger or it inflates the universe without ever
            # appearing on the disposition side.
            bump((str(filepath), line_no, ("pos",) + m.span()))

            # Skip if this is part of a chained expression (handled above)
            if f'FindArray' in line and f'->{accessor}({index})' in line:
                pos = m.start()
                before = line[:pos]
                if 'FindArray' in before and ')' in before[before.rfind('FindArray'):]:
                    site_drop("part-of-chained-expression",
                              note="already examined by the CHAINED_RE pass above")
                    continue

            node = get_var_node(receiver)
            if node is None:
                site_drop("receiver-unresolvable",
                          note="receiver's DTA context could not be traced "
                               "(function parameter / cross-function flow)")
                continue

            site_keep()

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
        _check_key_based_accesses(lines, filepath, all_key_nodes,
                                  cov_sites=cov_sites, unverifiable=unverifiable,
                                  site_totals=site_totals, ledger=ledger)
    )

    # Exactly one disposition per DISTINCT site, after both passes have had
    # their chance to check it.
    ledger.flush(cov_sites)
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
    parser.add_argument('--main-configs', nargs='*',
                        default=['orig-assets/extracted/config/ham_keep.dta',
                                 'orig-assets/extracted/(..)/(..)/system/run/config/default.dta'],
                        help='Main config files to parse with #include resolution '
                             '(default: the two orig-assets paths this tool always used)')
    parser.add_argument('--dta-root', default='orig-assets/extracted',
                        help='Tree searched recursively for *.dta '
                             '(default: orig-assets/extracted, as before)')
    add_coverage_args(parser)
    args = parser.parse_args()

    # Build hierarchy and keep root nodes for direct access
    main_configs = list(args.main_configs)
    main_roots = {}
    hierarchy = DTAHierarchy()
    dta_count = 0
    searched = []
    missing_inputs = []

    for cfg in main_configs:
        p = Path(cfg)
        if p.exists():
            root = parse_dta_file(str(p))
            if root:
                main_roots[cfg] = root
                if hierarchy.add_file(p):
                    dta_count += 1
                searched.append(f"{cfg}  [main config: parsed]")
            else:
                # parse_dta_file already printed the reason; do not let an
                # unparseable main config read as "loaded".
                missing_inputs.append(cfg)
                searched.append(f"{cfg}  [main config: UNPARSEABLE]")
        else:
            # Was a bare `if p.exists():` with NO else. From any worktree this
            # loaded 0 configs and every downstream check short-circuited.
            print(f"Warning: main config {cfg} does not exist", file=sys.stderr)
            missing_inputs.append(cfg)
            searched.append(f"{cfg}  [main config: MISSING]")

    dta_root = Path(args.dta_root)
    if not dta_root.exists():
        print(f"Warning: {dta_root} does not exist", file=sys.stderr)
        missing_inputs.append(str(dta_root))
        searched.append(f"{dta_root}  [tree: MISSING]")
    else:
        n = 0
        # sorted(): the sibling scanners sort their rglob; an unsorted walk made
        # parse order (and therefore warning order) filesystem-dependent.
        for f in sorted(dta_root.rglob('*.dta')):
            if str(f) not in hierarchy.roots:
                if hierarchy.add_file(f):
                    dta_count += 1
                    n += 1
        searched.append(f"{dta_root}  [tree: {n} files]")

    key_count = len(hierarchy.key_parents)
    print(f"Loaded {len(main_roots)} main configs, {dta_count} DTA files, "
          f"{key_count} unique keys", file=sys.stderr)

    cov = CoverageReport("dta_access_audit", args=args)
    cov.extra("main_configs_loaded", len(main_roots))
    cov.extra("dta_files_parsed", dta_count)
    cov.extra("dta_unique_keys", key_count)
    cov.extra("missing_inputs", sorted(missing_inputs))
    cov.extra("parse_stats", dict(sorted(PARSE_STATS.items())))
    cov.extra("unresolved_includes", dict(sorted(UNRESOLVED_INCLUDES.items())))
    if missing_inputs:
        cov.note(f"{len(missing_inputs)} declared corpus input(s) DO NOT EXIST: "
                 + ", ".join(sorted(missing_inputs)))
    for slug in ('include-unresolved', 'include-unreadable', 'include-depth-capped',
                 'dta-file-unparseable'):
        if PARSE_STATS.get(slug):
            cov.note(f"hierarchy TRUNCATED at parse time: {slug} x{PARSE_STATS[slug]} "
                     f"(the tree every check runs against is smaller than the corpus)")

    # ---- the empty-corpus gate: a clean verdict is FORBIDDEN from here ----
    banner = empty_corpus_banner("dta_access_audit", dta_count, key_count, searched)
    if banner is not None:
        print(banner)                       # STDOUT — survives a redirect
        cov.universe(0, "DTA files parsed")
        sys.exit(cov.emit() or EXIT_EMPTY_CORPUS)

    # SystemConfig section validation
    if args.check_sections:
        sections = set()
        src_path = Path(args.src_dir)
        src_files = sorted(list(src_path.rglob('*.cpp')) + list(src_path.rglob('*.h')))
        cov.universe(len(src_files), f"source files under {args.src_dir} (.cpp/.h)")
        for f in src_files:
            try:
                text = f.read_text()
            # A bare `except:` also swallows KeyboardInterrupt and SystemExit —
            # ^C during a scan looked like "one more clean file".
            except (OSError, UnicodeDecodeError, ValueError) as e:
                cov.drop("source-unreadable", note=f"{f}: {type(e).__name__}")
                continue
            cov.examine()
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
            print(f"\n{len(bad)} of {len(sections)} sections not found "
                  f"(checked against {dta_count} DTA files): {sorted(bad)}")
        else:
            print(f"\nAll {len(sections)} sections valid "
                  f"(checked against {dta_count} DTA files, {key_count} keys).")
        sys.exit(cov.emit())

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
    cov.extra("key_node_map_keys", len(all_key_nodes))
    if not all_key_nodes:
        # The main configs are what _build_key_node_map walks: an empty map
        # means every key-based check below is a no-op even if the wider
        # hierarchy parsed fine.
        cov.note("key-node map is EMPTY — every key-based bounds/type check "
                 "below short-circuits; only chain-resolved checks can fire")

    cov.universe(len(files), "source files to trace (.cpp/.h)")
    cov_sites = CoverageReport("dta_access_audit:access-sites",
                               allow_truncation=args.allow_truncation)
    unverifiable = Counter()
    site_totals = Counter()

    all_findings = []
    for f in files:
        findings = trace_dta_accesses(str(f), hierarchy, main_roots, all_key_nodes,
                                      cov_files=cov, cov_sites=cov_sites,
                                      unverifiable=unverifiable,
                                      site_totals=site_totals)
        all_findings.extend(findings)

    # Declared last, tallied independently: a `continue` that forgets to drop
    # shows up as UNACCOUNTED rather than shrinking the denominator.
    cov_sites.universe(site_totals['sites'], "DTA access sites in scanned source")
    # Balanced is not the same as non-empty: drop every site for good reasons
    # and the books still balance at examined == 0.  The corpus gate above keys
    # on "the corpus was empty"; this keys on "this run checked nothing".
    cov_sites.require_examined(
        "no DTA access site in the scanned sources could be checked -- the "
        "corpus parsed, but every site was dropped")
    site_d = cov_sites.as_dict()
    cov.extra("access_sites", site_d)
    cov.extra("keys_absent_from_every_dta",
              dict(sorted(unverifiable.items(), key=lambda kv: (-kv[1], kv[0]))))

    def finish():
        """Emit BOTH coverage blocks; the worst exit code wins."""
        rc_sites = cov_sites.emit(sys.stderr)
        rc_files = cov.emit()
        return rc_files or rc_sites

    if args.json:
        import json
        print(json.dumps(all_findings, indent=2))
        sys.exit(finish())

    # The key-absent population is its own reported category. It is NOT a clean
    # result: a typo'd FindArray key and a key whose DTA we never parsed are
    # indistinguishable from here, and the first is a real bug class.
    if unverifiable:
        n_absent = sum(unverifiable.values())
        print(f"\n=== UNVERIFIABLE: key present in NO parsed DTA "
              f"({n_absent} access sites, {len(unverifiable)} distinct keys) ===\n")
        ranked = sorted(unverifiable.items(), key=lambda kv: (-kv[1], kv[0]))
        for key, n in ranked[:20]:
            print(f"  {n:5d}x  {key}")
        if len(ranked) > 20:
            print(f"  ... and {len(ranked) - 20} more distinct keys "
                  f"(full list in --coverage-json)")

    if not all_findings:
        # NEVER a bare "No DTA access issues found." — the denominator travels
        # with the verdict, always.
        print(f"\nNo DTA access issues found among {site_d['examined']} verifiable "
              f"access sites (of {site_d['universe']} total) in "
              f"{cov.as_dict()['examined']} source files, checked against "
              f"{dta_count} DTA files / {key_count} keys.")
        sys.exit(finish())

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
          f"({', '.join(f'{k}:{len(v)}' for k,v in sorted(by_type.items()))}) "
          f"from {site_d['examined']} verifiable access sites of "
          f"{site_d['universe']} total, in {cov.as_dict()['examined']} source "
          f"files, against {dta_count} DTA files / {key_count} keys")
    sys.exit(finish())


if __name__ == '__main__':
    main()
