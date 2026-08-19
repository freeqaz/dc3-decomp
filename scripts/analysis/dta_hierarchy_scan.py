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
from collections import Counter, defaultdict
from pathlib import Path

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

# --------------------------------------------------------------------------
# Empty-corpus gate — shared by every DTA scanner in this directory
#
# `orig-assets/` is NOT tracked by git: it exists in the main checkout and is
# absent from every worktree.  Each of these scanners loaded its config behind a
# bare `if path.exists():` with no `else`, so from a worktree they parsed ZERO
# files, every key became unresolvable, every check short-circuited, and they
# printed "No ... issues found." to STDOUT while the only hint (`Parsed 0 DTA
# files`) went to STDERR.  Redirect stdout to a file and all you keep is the
# reassuring half.  A scanner with an empty corpus checked NOTHING and must say
# so, on stdout, and exit non-zero.
# --------------------------------------------------------------------------

# Distinct from coverage.EXIT_TRUNCATED (3) / EXIT_UNACCOUNTED (4) so a caller
# can tell "my input was missing" from "the scanner's arithmetic is broken".
EXIT_EMPTY_CORPUS = 5

_BAR = "=" * 78

CORPUS_HINT = (
    "orig-assets/ is present in the main checkout but is NOT tracked by git, so "
    "it is absent from every worktree. Run from the main checkout, or point "
    "--dta-dir / --main-configs at a real corpus."
)


def parent_key_for(parent_name, key, line_no):
    """Identity of an assignment site, shared by the two passes that see it.

    The FINDARRAY_RE pass counts the site; the deferred assign_checks loop
    disposes of it.  They must agree on what "the same site" means or the
    coverage arithmetic cannot balance.
    """
    return (parent_name, key, line_no)


def empty_corpus_banner(scanner, dta_count, key_count, sources):
    """Return the loud INCONCLUSIVE block, or None when the corpus is non-empty.

    Printed on STDOUT on purpose: the whole failure mode was that the warning
    went to stderr and only the clean verdict survived a redirect.
    """
    if dta_count > 0 and key_count > 0:
        return None
    L = [_BAR,
         f"INCONCLUSIVE: {scanner} parsed {dta_count} DTA files "
         f"({key_count} unique keys) — THIS RUN CHECKED NOTHING.",
         _BAR,
         "Every key lookup resolves against an empty hierarchy, so every check",
         "short-circuits and no finding can possibly be produced. A clean result",
         "here means 'no input', NOT 'no bugs'.",
         "",
         f"  {CORPUS_HINT}",
         "",
         "  Searched:"]
    for s in sources:
        L.append(f"    {s}")
    L.append(_BAR)
    return "\n".join(L)


# --------------------------------------------------------------------------
# DTA Parser (minimal S-expression parser)
# --------------------------------------------------------------------------

# Parser-level losses that used to be invisible.  An unresolvable `#include`
# silently yields a TRUNCATED hierarchy tree, and the hierarchy is what every
# check in this family depends on — so a silent one shrinks the audit itself.
#
# MAIN-THREAD ONLY (same rule as CoverageReport): these scanners are
# single-threaded; do not increment these from a worker pool.
PARSE_STATS = Counter()
# include name -> number of times it could not be resolved / read
UNRESOLVED_INCLUDES = Counter()


def reset_parse_stats():
    PARSE_STATS.clear()
    UNRESOLVED_INCLUDES.clear()

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
        # Silent depth cap: the subtree below this point is simply missing from
        # the hierarchy.  Count it so a deep #include chain cannot quietly
        # shrink the corpus every check is measured against.
        PARSE_STATS['include-depth-capped'] += 1
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
            PARSE_STATS['include-unresolved'] += 1
            UNRESOLVED_INCLUDES[filename] += 1
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
            PARSE_STATS['include-resolved'] += 1
        except (IOError, UnicodeDecodeError):
            PARSE_STATS['include-unreadable'] += 1
            UNRESOLVED_INCLUDES[filename] += 1

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
            # Bare atoms at root level (e.g., from #include of value-only files
            # like ham_version.dta which contains just: "Build: 120916")
            # Collect them so process_include can transfer to parent node.
            pos[0] += 1
            root.children.append(tok)

    return root


def parse_dta_file(filepath):
    """Parse a DTA file with #include resolution, returning None on error."""
    try:
        with open(filepath, encoding='utf-8', errors='replace') as f:
            text = f.read()
        return parse_dta(text, str(filepath), str(Path(filepath).parent))
    except Exception as e:
        PARSE_STATS['dta-file-unparseable'] += 1
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
        """Parse and index one DTA file. Returns True iff it was added.

        The return value is new; callers that ignore it behave exactly as
        before, but a caller counting its corpus can no longer credit itself
        with a file that failed to parse.
        """
        root = parse_dta_file(filepath)
        if root is None:
            return False
        self.roots[str(filepath)] = root
        tags = root.all_tags_recursive()
        for tag, parents in tags.items():
            self.key_parents[tag].update(parents)
            self.key_files[tag].add(str(filepath))
            for p in parents:
                self.edges[(p, tag)].add(str(filepath))
        return True

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


def scan_source_against_hierarchy(src_dir, hierarchy, cov_files=None,
                                  cov_sites=None, unverifiable=None):
    """Scan source files and cross-reference FindArray calls against DTA hierarchy.

    `cov_files` / `cov_sites` are optional CoverageReports: the first counts
    SOURCE FILES (denominator known up front), the second counts Find* CALL
    SITES.  `unverifiable` is an optional Counter that receives the
    key-absent-from-every-DTA population — the typo'd-FindArray bug class,
    which this scanner used to `continue` past uncounted.

    All three are keyword-only-in-practice additions; existing two-argument
    callers behave exactly as before.
    """
    findings = []
    n_sites = 0          # independent tally: the denominator for cov_sites
    # DISTINCT sites, not site EVENTS.  Every `var = recv->FindArray("k")` is
    # matched twice -- once by the ASSIGN_FINDARRAY_RE pass and once by the
    # FINDARRAY_RE pass -- and both passes bumped n_sites and both disposed of
    # it.  Measured 2026-08-19: 331 events over 274 distinct sites, 57 of them
    # counted twice.  The published "21%" was 70/331; the honest distinct-site
    # figure is 70/274 = 25.5%.
    #
    # Note this is INVISIBLE to the exit-4 tripwire: because each event bumps
    # the universe and lands in exactly one of examined/dropped, a
    # double-counted site balances perfectly.  The contract catches an
    # UNCOUNTED row; it cannot catch a TWICE-COUNTED one.
    seen_sites = set()
    #: (parent, key, line) of every assignment that really reaches assign_checks
    assign_sites = {}
    #: site key -> the deferred check that will dispose of it
    deferred = {}

    def site_new(key):
        """True the first time this textual site is offered for counting."""
        if key in seen_sites:
            return False
        seen_sites.add(key)
        return True

    # A site is counted ONCE in the universe and disposed ONCE.  Assignment
    # sites are counted by the FINDARRAY_RE pass and disposed later by the
    # deferred assign_checks loop, so disposition has to be keyed too or the
    # arithmetic double-counts on the other side of the ledger.
    disposed = set()

    def site_drop(reason, note="", key=None):
        if key is not None:
            if key in disposed:
                return
            disposed.add(key)
        if cov_sites is not None:
            cov_sites.drop(reason, note=note)

    def site_keep(key=None):
        if key is not None:
            if key in disposed:
                return
            disposed.add(key)
        if cov_sites is not None:
            cov_sites.examine()

    # Collect all source files
    src_path = Path(src_dir)
    files = list(src_path.rglob('*.cpp')) + list(src_path.rglob('*.h'))
    if cov_files is not None:
        cov_files.universe(len(files), f"source files under {src_dir} (.cpp/.h)")

    for filepath in sorted(files):
        try:
            with open(filepath) as f:
                lines = f.readlines()
        except (IOError, UnicodeDecodeError):
            if cov_files is not None:
                cov_files.drop("source-unreadable", note=str(filepath))
            continue

        # Quick check — need any Find* method
        if not any('Find' in l and '->' in l for l in lines):
            if cov_files is not None:
                cov_files.drop("no-find-call-syntax",
                               note="file contains no `->Find*(` at all")
            continue

        if cov_files is not None:
            cov_files.examine()

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
                # NOT counted here: the FINDARRAY_RE pass below sees the very
                # same text and counts it there.  Counting in both passes is
                # what made 274 sites read as 331.
                # Skip self-reassignment (def = def->FindArray("editor"))
                # which creates circular references in the tracker
                if var_name == parent_name:
                    continue
                var_parent[var_name] = parent_name
                var_key[var_name] = key
                # Also check this assignment's key against hierarchy.  The
                # site key travels with it: the FINDARRAY_RE pass below counts
                # this site into the universe, and THIS deferred check is what
                # disposes of it.
                assign_sites[(parent_name, key, line_no)] = None
                assign_checks.append((line_no, parent_name, key))

            # Check all Find* calls
            for m in FINDARRAY_RE.finditer(line):
                receiver, key = m.groups()
                if not site_new((str(filepath), line_no, m.span())):
                    continue          # already counted; do not double-dispose
                n_sites += 1

                # Skip if this is an assignment (we handle those above).
                #
                # THE REASON HAD TO BE MADE TRUE.  This predicate matched the
                # bare prefix `\w+ = receiver->FindArray` with no constraint on
                # the arguments, while ASSIGN_FINDARRAY_RE -- the regex that
                # actually feeds assign_checks -- requires the closing paren
                # immediately after the key, i.e. it matches ONE-ARGUMENT
                # FindArray only.  So every `var = recv->FindArray("k", true)`
                # was dropped with the note "re-examined via assign_checks"
                # while assign_checks never received it.  Measured on this tree
                # 2026-08-19: 106 sites carried the label, 57 were captured by
                # ASSIGN_FINDARRAY_RE, and 49 were captured by nothing.
                #
                # A drop with a FALSE reason is worse than an uncounted drop:
                # an uncounted drop is invisible, but this one told the reader
                # the population was covered elsewhere when it was not.  The
                # two populations are now separate slugs, so the coverage block
                # states the real size of the hole.
                if re.search(rf'\w+\s*=\s*{re.escape(receiver)}->FindArray', line):
                    matching_assigns = [
                        am for am in ASSIGN_FINDARRAY_RE.finditer(line)
                        if am.group(2) == receiver and am.group(3) == key]
                    really_rechecked = bool(matching_assigns)
                    # `def = def->FindArray("editor")` IS captured by
                    # ASSIGN_FINDARRAY_RE but the assign pass drops it as
                    # circular before it ever reaches assign_checks -- so
                    # nothing downstream disposes of it either.  Exactly one
                    # site on this tree (src/system/flow/Flow.cpp:701), and it
                    # was found by the exit-4 check firing at -1 after the
                    # de-duplication above.
                    self_reassign = any(am.group(1) == am.group(2)
                                        for am in matching_assigns)
                    if self_reassign:
                        site_drop("assign-self-reassignment",
                                  note="def = def->FindArray(..) — circular in "
                                       "the tracker, so the assign pass drops "
                                       "it and assign_checks never sees it",
                                  key=(str(filepath), line_no, m.span()))
                    elif really_rechecked:
                        # Do NOT dispose here: the deferred assign_checks loop
                        # is the real check for this site and will dispose it
                        # with its own reason.  Disposing in both places is the
                        # mirror image of counting in both places.
                        deferred[(parent_key_for(receiver, key, line_no))] = \
                            (str(filepath), line_no, m.span())
                    else:
                        # TODO(heuristic): widening ASSIGN_FINDARRAY_RE to accept
                        # trailing arguments would let these be checked -- but
                        # that CHANGES WHAT THIS TOOL FINDS, which does not
                        # belong in an honesty pass.  Left as a counted,
                        # honestly-named gap, the same way findarray_receiver_scan
                        # left its gate widening.
                        site_drop("assignment-site-NOT-rechecked-anywhere",
                                  note="looks like an assignment, so the Find* "
                                       "pass skips it -- but ASSIGN_FINDARRAY_RE "
                                       "matches one-argument FindArray only, so "
                                       "assign_checks never receives it. Nothing "
                                       "checks these. 49 of 106 on this tree")
                    continue

                # Skip optional lookups: FindData("key", var, false)
                # These intentionally handle missing keys
                if re.search(rf'{re.escape(receiver)}->FindData\(\s*"{re.escape(key)}".*,\s*false\s*\)', line):
                    site_drop("optional-lookup-allowed-to-miss",
                              note="FindData(\"k\", v, false) intentionally tolerates a miss")
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
                    # Key not found in ANY DTA. This is NOT "nothing to see" —
                    # it is the typo'd-FindArray population, the most valuable
                    # thing this scanner could report. Count it and surface it.
                    if unverifiable is not None:
                        unverifiable[key] += 1
                    site_drop("key-absent-from-every-dta",
                              note="key exists in no parsed DTA — typo'd-FindArray candidates")
                    continue

                if effective_parent is None:
                    # Can't determine parent (e.g., function parameter).
                    # Likely the MAJORITY of call sites — never let that be
                    # invisible, or "0 findings" reads as "0 bugs".
                    site_drop("receiver-unresolvable",
                              note="receiver is a function parameter / untracked expr")
                    continue

                site_keep()

                if effective_parent == '__ROOT__':
                    # Looking up from true root
                    if not hierarchy.is_root_key(key):
                        # sorted list, not a set: a raw set interpolates into
                        # the text report in hash order (nondeterministic).
                        actual_parents = sorted(parents - {'__ROOT__'})
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
                        # sorted list, not a set: a raw set interpolates into
                        # the text report in hash order (nondeterministic).
                        actual_parents = sorted(parents - {'__ROOT__'})
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
            skey = deferred.get(parent_key_for(parent_name, key, line_no))
            effective_parent = _resolve_effective_parent(
                parent_name, var_config, var_parent, var_key
            )
            parents = hierarchy.get_parents(key)
            if not parents:
                if unverifiable is not None:
                    unverifiable[key] += 1
                site_drop("key-absent-from-every-dta",
                          note="key exists in no parsed DTA — typo'd-FindArray candidates",
                          key=skey)
                continue
            if effective_parent is None:
                site_drop("receiver-unresolvable",
                          note="receiver is a function parameter / untracked expr",
                          key=skey)
                continue

            site_keep(key=skey)

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
                    actual_parents = sorted(parents - {'__ROOT__'})
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
                    actual_parents = sorted(parents - {'__ROOT__'})
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

    if cov_sites is not None:
        # Declared LAST but tallied INDEPENDENTLY of the dispositions above, so
        # a `continue` that forgets to call site_drop() shows up as UNACCOUNTED
        # instead of silently shrinking the denominator.
        cov_sites.universe(n_sites, "Find*() call sites in scanned source")
        # See dta_access_audit: a balanced run that examined 0 sites is
        # arithmetically clean and epistemically empty.
        cov_sites.require_examined(
            "no Find*() call site in the scanned sources could be checked -- "
            "the corpus parsed, but every site was dropped")
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
    parser.add_argument('--extra-root', default='orig-assets/extracted',
                        help='extra DTA tree swept IN ADDITION to --dta-dir, relative to CWD (default: orig-assets/extracted). This sweep used to be hardcoded and unconditional, so --dta-dir could not actually bound the corpus: pointing it at an empty path still picked up 247 files whenever CWD happened to have orig-assets. Pass a nonexistent path to disable.')
    parser.add_argument('--src-dir', default='src/',
                        help='Source directory to scan')
    parser.add_argument('--dump-hierarchy', action='store_true',
                        help='Print parsed DTA hierarchy and exit')
    parser.add_argument('--query', type=str,
                        help='Query a specific key to see where it exists in hierarchy')
    parser.add_argument('--json', action='store_true',
                        help='Output findings as JSON')
    add_coverage_args(parser)
    args = parser.parse_args()

    # Build hierarchy from main config files first (with #include resolution)
    # These give us the TRUE hierarchy with proper nesting
    hierarchy = DTAHierarchy()
    dta_count = 0
    searched = []
    missing_inputs = []

    for main_cfg in args.main_configs:
        main_path = Path(main_cfg)
        if main_path.exists():
            if hierarchy.add_file(main_path):
                dta_count += 1
            searched.append(f"{main_cfg}  [main config: parsed]")
        else:
            # Was silently skipped — while the --dta-dir branch below DID warn.
            # A missing main config is the single biggest corpus loss there is.
            print(f"Warning: main config {main_cfg} does not exist", file=sys.stderr)
            missing_inputs.append(main_cfg)
            searched.append(f"{main_cfg}  [main config: MISSING]")

    # Then add individual DTA files (for keys not in main configs)
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
                if hierarchy.add_file(dta_file):
                    dta_count += 1
                    n += 1
        searched.append(f"{dta_dir}  [dta dir: {n} files]")

    # Also scan all DTA files in orig-assets
    extra_root = Path(args.extra_root)
    if not extra_root.exists():
        print(f"Warning: {extra_root} does not exist", file=sys.stderr)
        missing_inputs.append(str(extra_root))
        searched.append(f"{extra_root}  [tree: MISSING]")
    else:
        n = 0
        for dta_file in sorted(extra_root.rglob('*.dta')):
            if str(dta_file) not in hierarchy.roots:
                if hierarchy.add_file(dta_file):
                    dta_count += 1
                    n += 1
        searched.append(f"{extra_root}  [tree: {n} files]")

    key_count = len(hierarchy.key_parents)
    print(f"Parsed {dta_count} DTA files, {key_count} unique keys", file=sys.stderr)

    cov = CoverageReport("dta_hierarchy_scan", args=args)
    cov.extra("dta_files_parsed", dta_count)
    cov.extra("dta_unique_keys", key_count)
    cov.extra("missing_inputs", sorted(missing_inputs))
    cov.extra("parse_stats", dict(sorted(PARSE_STATS.items())))
    cov.extra("unresolved_includes",
              dict(sorted(UNRESOLVED_INCLUDES.items())))
    if missing_inputs:
        cov.note(f"{len(missing_inputs)} declared corpus input(s) DO NOT EXIST: "
                 + ", ".join(sorted(missing_inputs)))
    for slug in ('include-unresolved', 'include-unreadable', 'include-depth-capped',
                 'dta-file-unparseable'):
        if PARSE_STATS.get(slug):
            cov.note(f"hierarchy TRUNCATED at parse time: {slug} x{PARSE_STATS[slug]} "
                     f"(the tree every check runs against is smaller than the corpus)")

    # ---- the empty-corpus gate: a clean verdict is FORBIDDEN from here ----
    banner = empty_corpus_banner("dta_hierarchy_scan", dta_count, key_count, searched)
    if banner is not None:
        print(banner)                       # STDOUT — survives a redirect
        cov.universe(0, "DTA files parsed")
        sys.exit(cov.emit() or EXIT_EMPTY_CORPUS)

    if args.query:
        key = args.query
        parents = hierarchy.get_parents(key)
        if not parents:
            print(f"Key '{key}' not found in any of the {dta_count} parsed DTA files.")
        else:
            print(f"Key '{key}' appears as child of: {sorted(parents)}")
            print(f"  Files: {sorted(hierarchy.key_files.get(key, set()))}")
            is_root = hierarchy.is_root_key(key)
            print(f"  Is root-level: {is_root}")
        cov.universe(dta_count, "DTA files parsed")
        cov.examine(dta_count)
        sys.exit(cov.emit())

    if args.dump_hierarchy:
        for filepath, root in sorted(hierarchy.roots.items()):
            root.print_tree()
            print()
        cov.universe(dta_count, "DTA files parsed")
        cov.examine(dta_count)
        sys.exit(cov.emit())

    # Scan source code
    cov_sites = CoverageReport("dta_hierarchy_scan:call-sites",
                               allow_truncation=args.allow_truncation)
    unverifiable = Counter()
    findings = scan_source_against_hierarchy(args.src_dir, hierarchy,
                                             cov_files=cov, cov_sites=cov_sites,
                                             unverifiable=unverifiable)
    cov.extra("call_sites", cov_sites.as_dict())
    cov.extra("keys_absent_from_every_dta",
              dict(sorted(unverifiable.items(), key=lambda kv: (-kv[1], kv[0]))))

    def finish():
        """Emit BOTH coverage blocks; the worst exit code wins."""
        rc_sites = cov_sites.emit(sys.stderr)
        rc_files = cov.emit()
        return rc_files or rc_sites

    site_d = cov_sites.as_dict()

    if args.json:
        import json
        # Convert sets to lists for JSON
        for f in findings:
            f['actual_parents'] = sorted(f['actual_parents'])
        print(json.dumps(findings, indent=2))
        sys.exit(finish())

    # The key-absent population is NOT "nothing to report": it is the typo'd-
    # FindArray bug class, which this scanner used to drop with a bare continue.
    if unverifiable:
        n_sites_absent = sum(unverifiable.values())
        print(f"\n=== UNVERIFIABLE: key present in NO parsed DTA "
              f"({n_sites_absent} call sites, {len(unverifiable)} distinct keys) ===\n")
        print("  These are not clean — they are unchecked. A typo'd key looks")
        print("  exactly like a key whose DTA we simply did not parse.\n")
        ranked = sorted(unverifiable.items(), key=lambda kv: (-kv[1], kv[0]))
        for key, n in ranked[:20]:
            print(f"  {n:5d}x  {key}")
        if len(ranked) > 20:
            print(f"  ... and {len(ranked) - 20} more distinct keys "
                  f"(full list in --coverage-json)")

    if not findings:
        # NEVER a bare "No ... found." — the denominator travels with it.
        print(f"\nNo DTA hierarchy mismatches found "
              f"among {site_d['examined']} verifiable call sites "
              f"(of {site_d['universe']} total) against {dta_count} DTA files.")
        sys.exit(finish())

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

    print(f"Total: {len(high)} HIGH, {len(medium)} MEDIUM "
          f"from {site_d['examined']} verifiable call sites "
          f"(of {site_d['universe']} total) against {dta_count} DTA files")
    sys.exit(finish())


if __name__ == '__main__':
    main()
